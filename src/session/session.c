#include "session.h"
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/printk.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/usb/usb_device.h>
#include <zephyr/usb/usbd.h>

#include <zephyr/fs/fs.h>
#include <zephyr/storage/disk_access.h>
#include <zephyr/logging/log.h>
#include <ff.h> // for FATFS

#include "message_processor/message_processor.h"
#include "ble/led_svc.h"
#include "ble/ble_protocol.h"
#include "ble_notifications.h"
#include "can/can_transport.h"
#include "sdcard/sdcard_module.h"
#include "led_handler.h"

/* External declaration for protocol test function */
extern void test_ble_protocol(void);

/* Include message processing commands */
#include <stdint.h>
#include <string.h>

#define BUF_SIZE 64
#define START_CMD "start"
#define STOP_CMD "stop"

LOG_MODULE_REGISTER(session, LOG_LEVEL_INF);

const struct device *const uart_dev = DEVICE_DT_GET_ONE(zephyr_cdc_acm_uart);
K_THREAD_STACK_DEFINE(cdc_read_thread_stack, 1024);
struct k_thread cdc_read_thread_stack_data;

K_THREAD_STACK_DEFINE(cdc_write_thread_stack, 1024);
struct k_thread cdc_write_thread_stack_data;

/* Global connection tracking variables - declared at file scope */
struct bt_conn *current_conn = NULL;
bool is_connected = false;
uint32_t connection_time = 0;           /* Time when connection was established */
uint32_t connection_ready_delay = 2000; /* Delay in ms before sending notifications */

static struct k_timer sample_timer;

/* Global notification buffer and state */
static uint8_t notify_buffer[244] = {0}; /* Increased from 20 to 64 bytes to accommodate protocol format */

/* Forward declarations for CPR session management */
bool is_cpr_session_active(void);
void start_cpr_session(void);
void stop_cpr_session(void);
uint32_t get_cpr_session_time(void);

/* Forward declaration of our notification helper functions */
int send_notification_safely(const void *data, uint16_t len);


/* Helper function to prepare and send a notification using protocol format
 *
 * This function handles:
 * 1. Formatting according to protocol: START_BYTE + LENGTH_BYTE + COLON + MESSAGE + SEMICOLON + END_BYTE
 * 2. Adding payload data
 * 3. Safely sending the notification with connection checks
 *
 * Usage:
 * - For simple notifications with a single value:
 *   send_ble_notification(MSG_TYPE_X, &value, sizeof(value));
 *
 * - For notifications with multiple fields, create the payload first, then call:
 *   send_ble_notification(MSG_TYPE_X, payload, payload_size);
 *
 * - For command acknowledgments, use send_command_ack() instead
 */
int send_ble_notification(uint8_t msg_type, const void *payload, uint16_t payload_len)
{
    /* Calculate the total required buffer size:
     * START_BYTE(1) + LENGTH_BYTE(1) + COLON(1) + MSG_TYPE(1) + PAYLOAD(payload_len) + SEMICOLON(1) + END_BYTE(1)
     * This is 6 bytes overhead plus payload_len: START + LEN + COLON + MSG_TYPE + SEMICOLON + END
     */
    uint16_t total_len = 6 + payload_len; // 6 = START + LEN + COLON + MSG_TYPE + SEMICOLON + END

    /* Check if we have space in buffer */
    if (total_len > sizeof(notify_buffer))
    {
        LOG_ERR("Notification too large: %d bytes, max %d", total_len, sizeof(notify_buffer));
        return -EINVAL;
    }

    /* Format the notification according to protocol */
    notify_buffer[0] = BLE_COMMAND_BYTE_START; /* START_BYTE */
    notify_buffer[1] = payload_len + 1;        /* LENGTH_BYTE - payload plus msg_type byte */
    notify_buffer[2] = BLE_COMMAND_MSG_COLON;  /* COLON */
    notify_buffer[3] = msg_type;               /* Message type */

    /* Add payload data if provided */
    if (payload != NULL && payload_len > 0)
    {
        memcpy(&notify_buffer[4], payload, payload_len);
    }

    /* Add terminating bytes */
    notify_buffer[4 + payload_len] = BLE_COMMAND_MSG_SEMICOLON; /* SEMICOLON */
    notify_buffer[5 + payload_len] = BLE_COMMAND_MSG_END;       /* END_BYTE */

    /* Send notification */
    return send_notification_safely(notify_buffer, total_len);
}

/* Helper function to send a command acknowledgment
 *
 * This function creates a command acknowledgment with the same command value
 * that the iOS app is expecting according to the protocol spec
 *
 * @param cmd_byte - The original command byte to acknowledge (e.g., CPR_CONTROL_START)
 * @return 0 on success, negative error code on failure
 */
static int send_command_ack(uint8_t cmd_byte)
{
    /* Format according to protocol: START_BYTE + LENGTH_BYTE + COLON + CMD_BYTE + SEMICOLON + END_BYTE */
    uint8_t ack_buffer[6];

    ack_buffer[0] = BLE_COMMAND_BYTE_START;    /* START_BYTE */
    ack_buffer[1] = 0x01;                      /* LENGTH_BYTE - just the command byte */
    ack_buffer[2] = BLE_COMMAND_MSG_COLON;     /* COLON */
    ack_buffer[3] = cmd_byte;                  /* Original command byte */
    ack_buffer[4] = BLE_COMMAND_MSG_SEMICOLON; /* SEMICOLON */
    ack_buffer[5] = BLE_COMMAND_MSG_END;       /* END_BYTE */

    LOG_INF("Sending command acknowledgment for cmd: 0x%02x", cmd_byte);

    /* Send the acknowledgment */
    return send_notification_safely(ack_buffer, sizeof(ack_buffer));
}

/* The notification characteristic is at index 4 in our service definition, based on:
 * BT_GATT_SERVICE_DEFINE(custom_svc,
 *    [0] BT_GATT_PRIMARY_SERVICE(&custom_service_uuid),
 *
 *    [1] BT_GATT_CHARACTERISTIC(&custom_char_uuid.uuid,...)   <-- Declaration
 *    [2] ...                                                   <-- Value
 *
 *    [3] BT_GATT_CHARACTERISTIC(&custom_notify_uuid.uuid,...)  <-- Declaration
 *    [4] ...                                                    <-- Value (what we want)
 *    [5] BT_GATT_CCC(...)                                       <-- CCC descriptor
 *
 *    [6] BT_GATT_CHARACTERISTIC(...                            <-- CPR State Char
 */

/* Helper function for checking connection and sending notifications */
int send_notification_safely(const void *data, uint16_t len)
{
    /* Index 4 is the notification characteristic value attribute, from counting in service definition */
    static const int NOTIFY_CHAR_INDEX = 4;

    /* Track whether we've warned about missing connection */
    static uint32_t last_warning_time = 0;

    /* Track last ENOTSUP warning time to avoid log spam */
    static uint32_t last_enotsup_warning = 0;

    /* Rate limiter for notifications to prevent buffer overflow */
    static uint32_t last_notification_time = 0;
    static const uint32_t MIN_NOTIFICATION_INTERVAL = 100; /* Min 100ms between notifications for STM32H7 */

    /* We need extern declaration for custom_svc which is defined by BT_GATT_SERVICE_DEFINE macro */
    extern const struct bt_gatt_service_static custom_svc;

    /* Only proceed if we have a valid connection that's had time to stabilize */
    if (!is_connected || !current_conn)
    {
        /* Only log warning once per 5 seconds to reduce log spam */
        uint32_t now = k_uptime_get_32();
        if (now - last_warning_time > 5000)
        {
            LOG_WRN("Cannot send notification - no active connection");
            last_warning_time = now;
        }
        return -ENOTCONN;
    }

    uint32_t now = k_uptime_get_32();
    uint32_t conn_age = now - connection_time;

    if (conn_age < connection_ready_delay)
    {
        LOG_WRN("Connection too fresh (%u ms), delaying notification", conn_age);
        return -EAGAIN;
    }

    /* Check if we're sending notifications too quickly */
    if (now - last_notification_time < MIN_NOTIFICATION_INTERVAL)
    {
        LOG_DBG("Rate limiting notification, too soon after previous (%u ms)",
                now - last_notification_time);
        return -EAGAIN;
    }

    /* Try to send the notification */
    LOG_DBG("Sending notification: len=%d using attr[%d]", len, NOTIFY_CHAR_INDEX);
    int err = bt_gatt_notify(NULL, &custom_svc.attrs[NOTIFY_CHAR_INDEX], data, len);

    /* Update last notification time if successful or if we encountered buffer issues */
    if (err == 0 || err == -ENOMEM)
    {
        last_notification_time = now;
    }

    /* Handle any errors */
    if (err)
    {
        /* Only log detailed errors for non-connection issues to reduce spam */
        if (err != -ENOTCONN)
        {
            LOG_ERR("Notification failed (err %d): %s", err,
                    err == -ENOTSUP ? "ENOTSUP - Not supported" : err == -EINVAL ? "EINVAL - Invalid parameter"
                                                              : err == -ENOTCONN ? "ENOTCONN - Not connected"
                                                              : err == -ENOMEM   ? "ENOMEM - Out of memory"
                                                                                 : "Unknown error");
        }

        /* Only log ENOTSUP errors occasionally */
        static uint32_t last_enotsup_time = 0;
        uint32_t now_err = k_uptime_get_32();

        if (err == -ENOTSUP)
        {
            /* Use separate tracking for client notification status vs warnings */
            if (now_err - last_enotsup_time > 10000)
            {
                LOG_WRN("Client hasn't enabled notifications or attribute doesn't support them");
                last_enotsup_time = now_err;
            }

            /* Record the ENOTSUP status for each notification type */
            if (data && len >= 4)
            {
                /* Extract the message type from the notification format */
                uint8_t *msg_data = (uint8_t *)data;
                if (msg_data[0] == BLE_COMMAND_BYTE_START &&
                    msg_data[2] == BLE_COMMAND_MSG_COLON)
                {
                    uint8_t msg_type = msg_data[3];

                    /* Only log specific notification types occasionally */
                    if (now_err - last_enotsup_warning > 5000)
                    {
                        LOG_DBG("Notification type 0x%02x not enabled by client", msg_type);
                        last_enotsup_warning = now_err;
                    }
                }
            }
        }

        /* Special handling with ACL flow control enabled */
        if (err == -ENOMEM)
        {
            /* With ACL flow control, this is likely temporary buffer exhaustion - add more backoff */
            static uint32_t last_backoff_time = 0;
            if (now_err - last_backoff_time > 2000)
            {
                LOG_WRN("BLE stack buffer full (-ENOMEM), adding 250ms backoff");
                last_backoff_time = now_err;
            }
            /* Increase backoff time to 250ms to allow stack to recover */
            last_notification_time = now + 200;
        }
        else if (err == -BT_ATT_ERR_UNLIKELY || err == -ENOTCONN)
        {
            /* Only log connection resets occasionally */
            static uint32_t last_conn_reset_time = 0;
            if (now_err - last_conn_reset_time > 5000)
            {
                LOG_ERR("Connection issue detected, resetting connection state");
                last_conn_reset_time = now_err;
            }

            /* Reset connection on critical errors */
            if (current_conn)
            {
                bt_conn_unref(current_conn);
                current_conn = NULL;
            }
            is_connected = false;
        }
    }
    else
    {
        LOG_DBG("Notification sent successfully");
    }

    return err;
}

/* Constants moved to ble_notifications.h */
bool notify_enabled = false;
static bool connection_notif_reset_needed = true; /* Track when we need to reset notification states */

/* Per-connection tracking for notification support */
struct
{
    bool heartbeat_works;
    bool role_works;
    bool time_works;
    bool led_works;
    bool cpr_works;
} notification_support = {false, false, false, false, false};

/* Reference stream_notify_enable from ble_handlers.c */
extern volatile bool stream_notify_enable;

/* Always allow CPR notifications, even if standard notifications aren't enabled */
bool cpr_notifications_allowed = true;

/* External function from basic_implementation.c */
void basic_implementation_init(void);

/* Buffer for storing received data */
static uint8_t recv_buffer[20];

/* Buffer for CPR state characteristic */
static uint8_t cpr_state_buffer[20];

/* LED control flags - for message processor */
bool led_request_pending = false;
bool led_requested_state = false;

/* CPR session timing - explicitly initialized to inactive */
uint32_t cpr_session_start_time = 0;
bool cpr_session_active = false; /* MUST remain false at startup */

/* Function to check if CPR session is active */
bool is_cpr_session_active(void)
{
    /* Only log when state changes to reduce noise */
    static bool last_logged_state = false;
    if (last_logged_state != cpr_session_active)
    {
        LOG_INF("CPR session active check: state changed from %d to %d",
                last_logged_state, cpr_session_active);
        last_logged_state = cpr_session_active;
    }
    return cpr_session_active;
}

char session_file_name[512];
/* Function to handle CPR session start */
void start_cpr_session(void)
{
    LOG_INF("*******************************************");
    LOG_INF("***** STARTING CPR SESSION *****");
    LOG_INF("*******************************************");
    LOG_INF("Current state before start: active=%d, start_time=%u",
            cpr_session_active, cpr_session_start_time);

    /* Safety check to prevent activation during system startup */
    if (k_uptime_get_32() < 1000)
    {
        LOG_ERR("PREVENTING CPR session start during early boot (uptime < 1s)");
        return;
    }

    /* Always start a new session */
    cpr_session_start_time = k_uptime_get_32();
    LOG_INF("CPR session started - timer initialized at %u", cpr_session_start_time);

    /* We'll send notification from the timer handler after detecting state change */
    /* This is safer because the timer handler has context to access BLE services */
    LOG_INF("CPR session start: Notification will be sent via timer handler");
    char instructor_id[64];
    get_instructor_id(instructor_id, sizeof(instructor_id));
    char start_time[64];
    get_time_data(start_time, sizeof(start_time));
    snprintf(session_file_name, sizeof(session_file_name),
             "%s/cpr%d.csv\0", "/SD:", 0x01);
    printf("file_name: %s\n", session_file_name);
    int ret = fs_open(&session_file, session_file_name, FS_O_CREATE | FS_O_WRITE);
    if (ret < 0)
    {
        printk("Failed to create file: %d\n", ret);
        return;
    }
    const char *csv_header = "sensor_name,frame_id,data0,data1,data2,data3,data4,data5,data6,data7\n";
    ssize_t written = fs_write(&session_file, csv_header, strlen(csv_header));
    if (written < 0)
    {
        printk("Failed to write CSV header: %d\n", (int)written);
        fs_close(&session_file);
        return;
    }
    cpr_session_active = true;
}

/* Function to handle CPR session stop */
void stop_cpr_session(void)
{
    LOG_INF("*******************************************");
    LOG_INF("***** STOPPING CPR SESSION *****");
    LOG_INF("*******************************************");
    LOG_INF("Current state before stop: active=%d, start_time=%u",
            cpr_session_active, cpr_session_start_time);

    if (!cpr_session_active)
    {
        LOG_INF("CPR session already inactive - nothing to stop");

        /* We'll send notification from the timer handler after detecting state change */
        LOG_INF("CPR session stop: Already inactive - notification will be sent via timer handler");

        return;
    }

    /* Calculate final duration */
    uint32_t now = k_uptime_get_32();
    uint32_t elapsed_ms = 0;
    if (cpr_session_start_time > 0)
    {
        elapsed_ms = now - cpr_session_start_time;
    }
    uint32_t elapsed_sec = elapsed_ms / 1000;
    uint32_t minutes = elapsed_sec / 60;
    uint32_t seconds = elapsed_sec % 60;

    LOG_INF("CPR session ended at %u - Duration: %02d:%02d (%u seconds)",
            now, minutes, seconds, elapsed_sec);

    /* Reset session state */
    cpr_session_active = false;
    cpr_session_start_time = 0;
    fs_close(&session_file);
    /* Store elapsed time for notification via timer handler */
    LOG_INF("CPR session stop: Notification with duration %u seconds will be sent via timer handler", elapsed_sec);
}

/* Function to get current CPR session elapsed time in seconds */
uint32_t get_cpr_session_time(void)
{
    if (!cpr_session_active)
    {
        return 0;
    }

    uint32_t current_time = k_uptime_get_32();
    uint32_t elapsed_ms = current_time - cpr_session_start_time;
    return elapsed_ms / 1000; /* Return seconds */
}

/* Timer for sending periodic notifications */
static struct k_timer notify_timer;

/* Counter for the periodic notifications */
static uint8_t notify_count = 0;

/* Define a simple custom service UUID */
static struct bt_uuid_128 custom_service_uuid = BT_UUID_INIT_128(
    BT_UUID_128_ENCODE(0x12345678, 0x1234, 0x5678, 0x1234, 0x56789abcdef0));

/* Define characteristic UUIDs */
static struct bt_uuid_128 custom_char_uuid = BT_UUID_INIT_128(
    BT_UUID_128_ENCODE(0x12345678, 0x1234, 0x5678, 0x1234, 0x56789abcdef1));

static struct bt_uuid_128 custom_notify_uuid = BT_UUID_INIT_128(
    BT_UUID_128_ENCODE(0x12345678, 0x1234, 0x5678, 0x1234, 0x56789abcdef2));

/* CPR state characteristic UUID - for reading CPR state */
static struct bt_uuid_128 cpr_state_char_uuid = BT_UUID_INIT_128(
    BT_UUID_128_ENCODE(0x12345678, 0x1234, 0x5678, 0x1234, 0x56789abcdef3));

/* iOS Command characteristic UUID - specific for iOS app commands that require write with response */
static struct bt_uuid_128 ios_cmd_char_uuid = BT_UUID_INIT_128(
    BT_UUID_128_ENCODE(0x12345678, 0x1234, 0x5678, 0x1234, 0x56789abcdef4));

/* Forward declaration of our GATT service (defined later with BT_GATT_SERVICE_DEFINE) */
extern const struct bt_gatt_service_static custom_svc;

/* Buffer for iOS commands (using Write With Response) - increase buffer size */
static uint8_t ios_cmd_buffer[128];

/* Write callback for custom characteristic */
static ssize_t custom_char_write(struct bt_conn *conn,
                                 const struct bt_gatt_attr *attr,
                                 const void *buf, uint16_t len,
                                 uint16_t offset, uint8_t flags)
{
    if (offset + len > sizeof(recv_buffer))
    {
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
    }

    /* Copy data to our buffer */
    memcpy(recv_buffer + offset, buf, len);

    LOG_INF("Received data, length: %d bytes", len);

    /* Print the data as hex for debugging */
    LOG_HEXDUMP_INF(buf, len, "Received data");

    /* Parse the command to see if we need to send an immediate acknowledgment */
    if (len >= 6 &&
        ((uint8_t *)buf)[0] == BLE_COMMAND_BYTE_START &&
        ((uint8_t *)buf)[2] == BLE_COMMAND_MSG_COLON)
    {

        /* Extract the command byte */
        uint8_t cmd_byte = ((uint8_t *)buf)[3];

        /* Check if this is a command that requires immediate acknowledgment */
        if (cmd_byte == CPR_CONTROL_START ||
            cmd_byte == CPR_COMMAND_STOP ||
            cmd_byte == CMD_COMMAND_DATA ||
            cmd_byte == CMD_COMMAND_TIMEDATA)
        {

            LOG_INF("Received command 0x%02x, sending immediate acknowledgment", cmd_byte);

            /* Send an acknowledgment with the same command byte */
            int err = send_command_ack(cmd_byte);

            /* Only log success or non-connection errors */
            if (err == 0)
            {
                LOG_INF("Command acknowledgment sent for cmd 0x%02x", cmd_byte);
            }
            else if (err != -ENOTCONN && err != -ENOTSUP)
            {
                /* We don't log connection errors because they're expected when no device is connected */
                LOG_ERR("Failed to send command acknowledgment (err %d)", err);
            }
        }
    }

    /* Submit the received data to the message processor */
    int ret = submit_command(buf, len);
    if (ret)
    {
        LOG_ERR("Failed to submit command to message processor (err %d)", ret);
    }
    else
    {
        LOG_INF("Command submitted to message processor successfully");
    }

    return len;
}

/* Write callback for iOS command characteristic - with proper write-with-response support */
static ssize_t ios_cmd_write(struct bt_conn *conn,
                             const struct bt_gatt_attr *attr,
                             const void *buf, uint16_t len,
                             uint16_t offset, uint8_t flags)
{
    LOG_INF("iOS command received, length: %d bytes, offset: %d, flags: 0x%02x", len, offset, flags);

    /* Print the data as hex for debugging */
    LOG_HEXDUMP_INF(buf, len, "iOS command data");

    /* Check buffer size */
    if (offset + len > sizeof(ios_cmd_buffer))
    {
        LOG_ERR("iOS command buffer overflow (%d > %d)", offset + len, sizeof(ios_cmd_buffer));
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
    }

    /* Copy data to our buffer */
    memcpy(ios_cmd_buffer + offset, buf, len);

    /* If this is the beginning of a long write, wait for the complete data */
    if (flags & BT_GATT_WRITE_FLAG_PREPARE)
    {
        LOG_INF("Prepare write received, waiting for more data or execute");
        return len;
    }

    /* If it's a partial write, wait for the complete data */
    if (offset > 0 && !(flags & BT_GATT_WRITE_FLAG_EXECUTE))
    {
        LOG_INF("Partial write at offset %d, waiting for more data", offset);
        return len;
    }

    /* At this point we have the complete data, process it */
    uint16_t total_len = offset + len;
    LOG_INF("Processing complete iOS command data, total length: %d bytes", total_len);

    /* Parse the command to see if we need to send an immediate acknowledgment */
    if (total_len >= 6 &&
        ios_cmd_buffer[0] == BLE_COMMAND_BYTE_START &&
        ios_cmd_buffer[2] == BLE_COMMAND_MSG_COLON)
    {

        /* Extract the command byte */
        uint8_t cmd_byte = ios_cmd_buffer[3];

        LOG_INF("Received valid formatted iOS command with type 0x%02x", cmd_byte);

        /* Check if this is a command that requires immediate acknowledgment */
        if (cmd_byte == CPR_CONTROL_START ||
            cmd_byte == CPR_COMMAND_STOP ||
            cmd_byte == CMD_COMMAND_DATA ||
            cmd_byte == CMD_COMMAND_TIMEDATA)
        {

            LOG_INF("Received iOS command 0x%02x, sending immediate acknowledgment", cmd_byte);

            /* Before sending ack, process the command through message processor */
            int ret = submit_command(ios_cmd_buffer, total_len);
            if (ret)
            {
                LOG_ERR("Failed to submit iOS command to message processor (err %d)", ret);
            }
            else
            {
                LOG_INF("iOS command submitted to message processor successfully");
            }

            /* Send an acknowledgment with the same command byte */
            int err = send_command_ack(cmd_byte);

            /* Only log success or non-connection errors */
            if (err == 0)
            {
                LOG_INF("iOS Command acknowledgment sent for cmd 0x%02x", cmd_byte);
            }
            else if (err != -ENOTCONN && err != -ENOTSUP)
            {
                /* We don't log connection errors because they're expected when no device is connected */
                LOG_ERR("Failed to send iOS command acknowledgment (err %d)", err);
            }
            if(cmd_byte == CPR_CONTROL_START) {
                start_cpr_session();
                can_transmit_start_msg();
            } else if (cmd_byte == CPR_CMD_STOP) {
                stop_cpr_session();
                can_transmit_stop_msg();
            }


            return total_len;
        }
    }

    /* If not a special command or invalid format, still process it normally */
    LOG_INF("Processing general iOS command");
    int ret = submit_command(ios_cmd_buffer, total_len);
    if (ret)
    {
        LOG_ERR("Failed to submit iOS command to message processor (err %d)", ret);
    }
    else
    {
        LOG_INF("iOS command submitted to message processor successfully");
    }

    return total_len;
}

/* Message types are defined in ble_notifications.h */

/* Notification timer callback */
static void notify_timer_handler(struct k_timer *timer)
{
    /* Only send notifications if enabled AND we have a stable connection */
    if (notify_enabled && is_connected && current_conn)
    {
        /* Check if enough time has passed since the last notification attempt to reduce errors */
        uint32_t now = k_uptime_get_32();
        static uint32_t last_sent_time = 0;

        if (now - last_sent_time >= 250)
        { /* Ensure at least 250ms between heartbeats */
            /* Update the notification data with a counter */
            notify_count++;

            /* Only try sending if global tracking says it works */
            static uint32_t last_heartbeat_attempt = 0;

            /* Only try sending if it's worked before or we haven't tried in a while */
            if (notification_support.heartbeat_works || (now - last_heartbeat_attempt > 60000))
            {
                last_heartbeat_attempt = now;

                /* Try sending heartbeat notification */
                int err = send_ble_notification(NOTIFY_TYPE_HEARTBEAT, &notify_count, sizeof(notify_count));

                if (err == 0)
                {
                    LOG_DBG("Periodic notification sent: %d", notify_count);
                    last_sent_time = now;
                    notification_support.heartbeat_works = true;
                }
                else if (err == -ENOTSUP)
                {
                    /* This iOS client doesn't support heartbeat notifications */
                    notification_support.heartbeat_works = false;
                    LOG_INF("Heartbeat notifications disabled - not supported by client");
                }
                else if (err != -ENOTCONN)
                {
                    /* Log other non-connection errors */
                    LOG_ERR("Periodic notification failed (err %d)", err);
                }
            }
        }
    }
}

/* Read handler for CPR state characteristic */
static ssize_t cpr_state_read(struct bt_conn *conn,
                              const struct bt_gatt_attr *attr,
                              void *buf, uint16_t len,
                              uint16_t offset)
{
    /* Prepare the CPR state data in the buffer */
    uint32_t elapsed_sec = get_cpr_session_time();
    uint32_t minutes = elapsed_sec / 60;
    uint32_t seconds = elapsed_sec % 60;

    /* Format: [STATE][ELAPSED][TIME_STR] */
    cpr_state_buffer[0] = is_cpr_session_active() ? 0x01 : 0x00; /* Active/Inactive */
    cpr_state_buffer[1] = (elapsed_sec >> 24) & 0xFF;            /* MSB */
    cpr_state_buffer[2] = (elapsed_sec >> 16) & 0xFF;
    cpr_state_buffer[3] = (elapsed_sec >> 8) & 0xFF;
    cpr_state_buffer[4] = elapsed_sec & 0xFF; /* LSB */

    /* Add formatted time string "cpr:MM:SS" */
    char time_str[16]; /* Increased buffer size to avoid truncation warnings */
    snprintf(time_str, sizeof(time_str), "cpr:%02d:%02d", minutes, seconds);
    size_t str_len = strlen(time_str);

    /* Copy the string to the buffer */
    memcpy(&cpr_state_buffer[5], time_str, str_len);

    /* Calculate total response length */
    size_t total_len = 5 + str_len;

    LOG_INF("CPR state read: active=%d, time=%s (%u seconds)",
            cpr_state_buffer[0], time_str, elapsed_sec);

    /* Return the data */
    return bt_gatt_attr_read(conn, attr, buf, len, offset, cpr_state_buffer, total_len);
}

/* CCC change handler for notification characteristic */
static void notify_ccc_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
    notify_enabled = (value == BT_GATT_CCC_NOTIFY);
    LOG_INF("Notifications %s", notify_enabled ? "enabled" : "disabled");

    /* Always ensure CPR notifications are allowed, regardless of CCC setting */
    cpr_notifications_allowed = true;
    LOG_INF("CPR notifications remain allowed regardless of CCC setting");

    /* Start or stop the notification timer based on state */
    if (notify_enabled)
    {
        /* Start sending periodic notifications (1 per second) */
        k_timer_start(&notify_timer, K_MSEC(1000), K_MSEC(1000));
    }
    else
    {
        /* Stop the timer when notifications are disabled */
        k_timer_stop(&notify_timer);
    }
}

/* Define our GATT service - will be auto-registered by Zephyr */
BT_GATT_SERVICE_DEFINE(custom_svc,
                       BT_GATT_PRIMARY_SERVICE(&custom_service_uuid),

                       /* Read/Write characteristic - for typical commands without response */
                       BT_GATT_CHARACTERISTIC(&custom_char_uuid.uuid,
                                              BT_GATT_CHRC_WRITE | BT_GATT_CHRC_READ,
                                              BT_GATT_PERM_READ | BT_GATT_PERM_WRITE,
                                              NULL, custom_char_write, recv_buffer),

                       /* Notification characteristic */
                       BT_GATT_CHARACTERISTIC(&custom_notify_uuid.uuid,
                                              BT_GATT_CHRC_NOTIFY,
                                              BT_GATT_PERM_NONE, /* No direct read/write perms - notifications only */
                                              NULL, NULL, notify_buffer),
                       /* Client Characteristic Configuration - required for notifications to work */
                       BT_GATT_CCC(notify_ccc_changed, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),

                       /* CPR State characteristic - with read and notify capabilities */
                       BT_GATT_CHARACTERISTIC(&cpr_state_char_uuid.uuid,
                                              BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
                                              BT_GATT_PERM_READ,
                                              cpr_state_read, NULL, cpr_state_buffer),
                       BT_GATT_CCC(notify_ccc_changed, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),

                       /* iOS Command characteristic - specifically for 'write with response' operations */
                       BT_GATT_CHARACTERISTIC(&ios_cmd_char_uuid.uuid,
                                              BT_GATT_CHRC_WRITE,                              /* Only write with response, no notify/read */
                                              BT_GATT_PERM_WRITE | BT_GATT_PERM_PREPARE_WRITE, /* Support long writes */
                                              NULL, ios_cmd_write, ios_cmd_buffer), );
static struct k_work_delayable adv_work;
#define DEVICE_NAME CONFIG_BT_DEVICE_NAME
#define DEVICE_NAME_LEN (sizeof(DEVICE_NAME) - 1)
#define ADV_LEN 12
/* Advertising data */
static uint8_t manuf_data[ADV_LEN] = {
	0x01 /*SKD version */,
	0x83 /* STM32WB - P2P Server 1 */,
	0x00 /* GROUP A Feature  */,
	0x00 /* GROUP A Feature */,
	0x00 /* GROUP B Feature */,
	0x00 /* GROUP B Feature */,
	0x00, /* BLE MAC start -MSB */
	0x00,
	0x00,
	0x00,
	0x00,
	0x00, /* BLE MAC stop */
};

static const struct bt_data ad[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
	BT_DATA(BT_DATA_NAME_COMPLETE, DEVICE_NAME, DEVICE_NAME_LEN),
	BT_DATA(BT_DATA_MANUFACTURER_DATA, manuf_data, ADV_LEN)};
/* Robust advertising function with work queue handling */
static void advertising_work_handler(struct k_work *work)
{
    static int retry_count = 0;
    static int backoff_time = 0;

    /* Stop any existing advertising */
    bt_le_adv_stop();

    /* Define advertising parameters with higher reliability */
    static const struct bt_le_adv_param param = {
        .options = BT_LE_ADV_OPT_CONN | BT_LE_ADV_OPT_ONE_TIME, /* Connectable and one-time flag */
        .interval_min = BT_GAP_ADV_FAST_INT_MIN_2,              /* Use faster interval for better response */
        .interval_max = BT_GAP_ADV_FAST_INT_MAX_2,
        .id = BT_ID_DEFAULT,
        .sid = 0,
        .secondary_max_skip = 0,
        .peer = NULL,
    };

    /* Minimal advertising data - just flags */
    static const uint8_t flag_data[] = {BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR};

    /* Calculate backoff time based on retry count with exponential increase */
    if (retry_count == 0)
    {
        backoff_time = 3000; /* 3 seconds for first retry */
    }
    else
    {
        backoff_time = backoff_time * 2; /* Double the backoff time for each retry */
        if (backoff_time > 30000)
        {
            backoff_time = 30000; /* Max 30 seconds between retries */
        }
    }

    LOG_INF("Advertising attempt #%d", retry_count + 1);
    int err = bt_le_adv_start(&param, ad, ARRAY_SIZE(ad), NULL, 0);

    if (err)
    {
        /* Special handling for common errors */
        if (err == -ENOMEM)
        {
            LOG_ERR("Advertising failed due to memory constraints (ENOMEM), retrying in %d ms", backoff_time);
        }
        else if (err == -EALREADY)
        {
            LOG_ERR("Advertising already active (EALREADY), stopping and retrying in %d ms", backoff_time);
            bt_le_adv_stop();
        }
        else
        {
            LOG_ERR("Advertising failed (err %d), retrying in %d ms", err, backoff_time);
        }

        retry_count++;

        /* Limit number of retries to avoid infinite loop */
        if (retry_count < 10)
        {
            /* Schedule next retry with exponential backoff */
            k_work_schedule(&adv_work, K_MSEC(backoff_time));
        }
        else
        {
            LOG_ERR("Advertising retry limit reached. Giving up after %d attempts", retry_count);
            retry_count = 0; /* Reset for next time */
        }
    }
    else
    {
        LOG_INF("Advertising started successfully after %d %s",
                retry_count, retry_count == 0 ? "attempt" : "retries");
        retry_count = 0; /* Reset for next time */
    }
}

/* Start advertising with a delay to allow resource recovery */
static void start_adv_with_delay(void)
{
    LOG_INF("Scheduling advertising with delay to allow resource recovery");

    /* Stop any existing advertising */
    bt_le_adv_stop();

    /* Schedule advertising work with initial delay */
    k_work_schedule(&adv_work, K_SECONDS(3));
}

/* BT ready callback */
static void bt_ready(int err)
{
    if (err)
    {
        LOG_ERR("Bluetooth init failed (err %d)", err);
        return;
    }

    LOG_INF("Bluetooth initialized successfully");

    /* The service is already registered automatically by BT_GATT_SERVICE_DEFINE */
    LOG_INF("GATT service ready");

    /* Start advertising using our robust method */
    LOG_INF("Starting initial advertising");
    advertising_work_handler(NULL); /* Start advertising immediately */

    LOG_INF("Initial advertising request submitted");
}

/* Connected callback */
static void connected(struct bt_conn *conn, uint8_t err)
{
    if (err)
    {
        LOG_ERR("Connection failed (err %d)", err);
        return;
    }

    LOG_INF("**********************************************");
    LOG_INF("*************** CONNECTED *******************");
    LOG_INF("**********************************************");

    /* Store the connection and set connected flag */
    if (current_conn)
    {
        bt_conn_unref(current_conn);
    }
    current_conn = bt_conn_ref(conn);
    is_connected = true;
    connection_time = k_uptime_get_32(); /* Record when connection was established */

    /* Ensure CPR session is inactive when a new connection is established */
    cpr_session_active = false;
    cpr_session_start_time = 0;

    /* Reset notification tracking for all notification types on new connection */
    connection_notif_reset_needed = true;

    /* Reset notification support tracking for new connection */
    notification_support.heartbeat_works = true; /* Try once for each type */
    notification_support.role_works = true;
    notification_support.time_works = true;
    notification_support.led_works = true;
    notification_support.cpr_works = true;

    LOG_INF("Reset notification support tracking for new connection");

    LOG_INF("Connection established at %u ms, allowing %u ms before notifications",
            connection_time, connection_ready_delay);
}

/* Disconnected callback */
static void disconnected(struct bt_conn *conn, uint8_t reason)
{
    LOG_INF("**********************************************");
    LOG_INF("************* DISCONNECTED: %d *************", reason);
    LOG_INF("**********************************************");

    /* Clear connection tracking */
    if (current_conn)
    {
        bt_conn_unref(current_conn);
        current_conn = NULL;
    }
    is_connected = false;

    /* Schedule delayed advertising restart */
    LOG_INF("Scheduling advertising restart after disconnect");
    start_adv_with_delay();
}

/* Connection callbacks structure */
static struct bt_conn_cb conn_callbacks = {
    .connected = connected,
    .disconnected = disconnected,
};


/* Forward declaration for advertising timer */
static void start_adv_with_delay(void);

/* LED timer handler */
void process_command(const char *cmd)
{
    if (strncmp(cmd, START_CMD, strlen(START_CMD)) == 0)
    {
        printk("CAN sending started\n");
        uart_fifo_fill(uart_dev, "CAN sending started\n", strlen("CAN sending started\n"));
        start_cpr_session();
        can_transmit_start_msg();
    }
    else if (strncmp(cmd, STOP_CMD, strlen(STOP_CMD)) == 0)
    {
        printk("CAN sending stopped\n");
        uart_fifo_fill(uart_dev, "CAN sending stopped\n", strlen("CAN sending stopped\n"));
        stop_cpr_session();
        can_transmit_stop_msg();
    }
}
#define CSV_QUEUE_SIZE 25 // Number of queued lines
#define CSV_LINE_MAX_LEN 256
char line[128];
K_MSGQ_DEFINE(csv_usb_msgq, CSV_LINE_MAX_LEN, CSV_QUEUE_SIZE, 4);
void cdc_write_thread(void *arg1, void *arg2, void *arg3)
{
    int written = 0;
while (1) {
    if (k_msgq_get(&csv_usb_msgq, &line, K_MSEC(1)) == 0) {
        if (cpr_session_active) {
            written = uart_fifo_fill(uart_dev, line, strlen(line));
            if (written < 0) {
                printk("USB Write failed: %d\n", written);
            }
        }
    }
}

}

void cdc_read_thread(void *arg1, void *arg2, void *arg3)
{
    uint8_t buf[BUF_SIZE];
    size_t len = 0;

    while (1)
    {
        int r = uart_fifo_read(uart_dev, buf + len, BUF_SIZE - len);
        if (r > 0)
        {
            len += r;
            if (buf[len - 1] == '\n' || buf[len - 1] == '\r')
            {
                buf[len - 1] = '\0';
                process_command((char *)buf);
                len = 0;
            }
        }
        k_msleep(10);
    }
}

static uint8_t vl_buf[512];
static uint8_t ads_buf[512];
static uint8_t bhi_buf[512];
static uint8_t sdp_buf[512];
static void notify_sample_handler(struct k_timer *timer)
{
    if (!cpr_session_active)
    {
        return;
    }

    // VL6180x
    {
        size_t bytes_read = ring_buf_get(&vl_ring, vl_buf, sizeof(vl_buf));
        uint8_t num_samples = bytes_read / sizeof(sample_sensor1_t);
        if (num_samples > 0)
        {
            write_vl_to_session_file((sample_sensor1_t *)vl_buf, num_samples);
        }
    }

    // SDP810
    {
        size_t bytes_read = ring_buf_get(&sdp_ring, sdp_buf, sizeof(sdp_buf));
        uint8_t num_samples = bytes_read / sizeof(sample_sensor3_t);
        if (num_samples > 0)
        {
            write_sdp_to_session_file((sample_sensor3_t *)sdp_buf, num_samples);
        }
    }

    // ADS7138
    {
        size_t bytes_read = ring_buf_get(&ads_ring, ads_buf, sizeof(ads_buf));
        uint8_t num_samples = bytes_read / sizeof(sample_sensor2_t);
        if (num_samples > 0)
        {
            write_ads_to_session_file((sample_sensor2_t *)ads_buf, num_samples);
        }
    }

    // BHI360
    {
        size_t bytes_read = ring_buf_get(&bhi_ring, bhi_buf, sizeof(bhi_buf));
        uint8_t num_samples = bytes_read / sizeof(sample_sensor4_t);
        if (num_samples > 0)
        {
            write_bhi_to_session_file((sample_sensor4_t *)bhi_buf, num_samples);
        }
    }
}

int session_init()
{
    init_sdcard();

    fs_file_t_init(&session_file);
    int err;
    k_tid_t tid;
    if (!device_is_ready(uart_dev))
    {
        printf("CDC ACM device not ready");
        return 0;
    }
    usb_enable(NULL);
    tid = k_thread_create(&cdc_read_thread_stack_data, cdc_read_thread_stack,
                          K_THREAD_STACK_SIZEOF(cdc_read_thread_stack),
                          cdc_read_thread, NULL, NULL, NULL,
                          2, 0, K_NO_WAIT);
    if (!tid)
    {
        printk("ERROR spawning rx thread\n");
        return 0;
    }
    k_thread_name_set(tid, "rx_usb");
    tid = k_thread_create(&cdc_write_thread_stack_data, cdc_write_thread_stack,
                          K_THREAD_STACK_SIZEOF(cdc_write_thread_stack),
                          cdc_write_thread, NULL, NULL, NULL,
                          2, 0, K_NO_WAIT);
    if (!tid)
    {
        printk("ERROR spawning rx thread\n");
        return 0;
    }
    k_thread_name_set(tid, "tx_usb");
    printk("Bluetooth application with GATT service and Message Processor\n");
    LOG_INF("Starting Bluetooth application with GATT service and Message Processor");

    /* Initialize our basic implementation module */
    basic_implementation_init();

    /* Initialize the LED driver */
    err = led_init();
    if (err)
    {
        LOG_ERR("LED initialization failed (err %d)", err);
    }
    else
    {
        LOG_INF("LED initialized successfully");
        /* Flash the LED once to indicate we're running */
        led_on();
        k_sleep(K_MSEC(500));
        led_off();
    }
    led_handler_init();

    /* Initialize notification timer */
    k_timer_init(&notify_timer, notify_timer_handler, NULL);
    k_timer_init(&sample_timer, notify_sample_handler, NULL);
    k_timer_start(&sample_timer, K_MSEC(10), K_MSEC(10));

    /* Initialize the advertising work queue item */
    k_work_init_delayable(&adv_work, advertising_work_handler);

    /* Initialize the message processor */
    err = message_processor_init();
    if (err)
    {
        LOG_ERR("Message processor initialization failed (err %d)", err);
    }
    else
    {
        LOG_INF("Message processor initialized successfully");
    }

    /* Explicitly ensure CPR session is inactive on startup */
    cpr_session_active = false;
    cpr_session_start_time = 0;
    LOG_INF("CPR session explicitly set to inactive on startup");

    /* Register connection callbacks */
    bt_conn_cb_register(&conn_callbacks);

    /* Initialize Bluetooth subsystem */
    err = bt_enable(bt_ready);
    if (err)
    {
        LOG_ERR("Bluetooth init failed (err %d)", err);
    }

    return 0;
}

int session_start()
{
    return 0;
}

int session_stop()
{
    return 0;
}