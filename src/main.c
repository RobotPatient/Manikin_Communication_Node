/* Minimal main.c with Bluetooth initialization and GATT service */
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/printk.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/drivers/gpio.h>
#include "message_processor/message_processor.h"
#include "ble/led_svc.h"
#include "ble/ble_protocol.h"
#include "ble/crc/crc16_koopman.h"
#include "ble_notifications.h"

/* External declaration for protocol test function */
extern void test_ble_protocol(void);


/* Include message processing commands */
#include <stdint.h>
#include <string.h>

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

/* Global connection tracking variables - declared at file scope */
struct bt_conn *current_conn = NULL;
bool is_connected = false;
uint32_t connection_time = 0;   /* Time when connection was established */
uint32_t connection_ready_delay = 2000;  /* Delay in ms before sending notifications */

/* Global notification buffer and state */
static uint8_t notify_buffer[64] = {0}; /* Increased from 20 to 64 bytes to accommodate protocol format */

/* Forward declarations for CPR session management */
bool is_cpr_session_active(void);
void start_cpr_session(void);
void stop_cpr_session(void);
uint32_t get_cpr_session_time(void);

/* Forward declaration of our notification helper functions */
static int send_notification_safely(const void *data, uint16_t len);

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
static int send_ble_notification(uint8_t msg_type, const void *payload, uint16_t payload_len) {
    /* Calculate the total required buffer size: 
     * START_BYTE(1) + LENGTH_BYTE(1) + COLON(1) + MSG_TYPE(1) + PAYLOAD(payload_len) + CRC(2) + SEMICOLON(1) + END_BYTE(1)
     * This is 8 bytes overhead plus payload_len: START + LEN + COLON + MSG_TYPE + CRC(2) + SEMICOLON + END
     */
    uint16_t total_len = 8 + payload_len; // 8 = START + LEN + COLON + MSG_TYPE + CRC(2) + SEMICOLON + END
    
    /* Check if we have space in buffer */
    if (total_len > sizeof(notify_buffer)) {
        LOG_ERR("Notification too large: %d bytes, max %d", total_len, sizeof(notify_buffer));
        return -EINVAL;
    }
    
    /* Format the notification according to protocol with CRC */
    notify_buffer[0] = BLE_COMMAND_BYTE_START;   /* START_BYTE */
    notify_buffer[1] = payload_len + 1 + 2;      /* LENGTH_BYTE - payload plus msg_type byte plus 2 CRC bytes */
    notify_buffer[2] = BLE_COMMAND_MSG_COLON;    /* COLON */
    notify_buffer[3] = msg_type;                 /* Message type */
    
    /* Add payload data if provided */
    if (payload != NULL && payload_len > 0) {
        memcpy(&notify_buffer[4], payload, payload_len);
    }
    
    /* Calculate CRC on everything from START to end of payload */
    uint16_t crc = crc16_koopman(notify_buffer, 4 + payload_len);
    
    /* Add CRC bytes (MSB first) */
    notify_buffer[4 + payload_len] = (uint8_t)(crc >> 8);        /* MSB of CRC */
    notify_buffer[5 + payload_len] = (uint8_t)(crc & 0xFF);      /* LSB of CRC */
    
    /* Add terminating bytes */
    notify_buffer[6 + payload_len] = BLE_COMMAND_MSG_SEMICOLON; /* SEMICOLON */
    notify_buffer[7 + payload_len] = BLE_COMMAND_MSG_END;       /* END_BYTE */
    
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
static int send_command_ack(uint8_t cmd_byte) {
    /* Format according to protocol: START_BYTE + LENGTH_BYTE + COLON + CMD_BYTE + CRC(2) + SEMICOLON + END_BYTE */
    uint8_t ack_buffer[8];
    
    ack_buffer[0] = BLE_COMMAND_BYTE_START;   /* START_BYTE */
    ack_buffer[1] = 0x01 + 0x02;              /* LENGTH_BYTE - command byte + 2 CRC bytes */
    ack_buffer[2] = BLE_COMMAND_MSG_COLON;    /* COLON */
    ack_buffer[3] = cmd_byte;                 /* Original command byte */
    
    /* Calculate CRC on everything from START to CMD_BYTE */
    uint16_t crc = crc16_koopman(ack_buffer, 4);
    
    /* Add CRC bytes (MSB first) */
    ack_buffer[4] = (uint8_t)(crc >> 8);      /* MSB of CRC */
    ack_buffer[5] = (uint8_t)(crc & 0xFF);    /* LSB of CRC */
    
    ack_buffer[6] = BLE_COMMAND_MSG_SEMICOLON;/* SEMICOLON */
    ack_buffer[7] = BLE_COMMAND_MSG_END;      /* END_BYTE */
    
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
static int send_notification_safely(const void *data, uint16_t len) {
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
    if (!is_connected || !current_conn) {
        /* Only log warning once per 5 seconds to reduce log spam */
        uint32_t now = k_uptime_get_32();
        if (now - last_warning_time > 5000) {
            LOG_WRN("Cannot send notification - no active connection");
            last_warning_time = now;
        }
        return -ENOTCONN;
    }
    
    uint32_t now = k_uptime_get_32();
    uint32_t conn_age = now - connection_time;
    
    if (conn_age < connection_ready_delay) {
        LOG_WRN("Connection too fresh (%u ms), delaying notification", conn_age);
        return -EAGAIN;
    }
    
    /* Check if we're sending notifications too quickly */
    if (now - last_notification_time < MIN_NOTIFICATION_INTERVAL) {
        LOG_DBG("Rate limiting notification, too soon after previous (%u ms)",
               now - last_notification_time);
        return -EAGAIN;
    }
    
    /* Try to send the notification */
    LOG_DBG("Sending notification: len=%d using attr[%d]", len, NOTIFY_CHAR_INDEX);
    int err = bt_gatt_notify(NULL, &custom_svc.attrs[NOTIFY_CHAR_INDEX], data, len);
    
    /* Update last notification time if successful or if we encountered buffer issues */
    if (err == 0 || err == -ENOMEM) {
        last_notification_time = now;
    }
    
    /* Handle any errors */
    if (err) {
        /* Only log detailed errors for non-connection issues to reduce spam */
        if (err != -ENOTCONN) {
            LOG_ERR("Notification failed (err %d): %s", err, 
                    err == -ENOTSUP ? "ENOTSUP - Not supported" :
                    err == -EINVAL ? "EINVAL - Invalid parameter" :
                    err == -ENOTCONN ? "ENOTCONN - Not connected" :
                    err == -ENOMEM ? "ENOMEM - Out of memory" : "Unknown error");
        }
        
        /* Only log ENOTSUP errors occasionally */
        static uint32_t last_enotsup_time = 0;
        uint32_t now_err = k_uptime_get_32();
        
        if (err == -ENOTSUP) {
            /* Use separate tracking for client notification status vs warnings */
            if (now_err - last_enotsup_time > 10000) {
                LOG_WRN("Client hasn't enabled notifications or attribute doesn't support them");
                last_enotsup_time = now_err;
            }
            
            /* Record the ENOTSUP status for each notification type */
            if (data && len >= 4) {
                /* Extract the message type from the notification format */
                uint8_t *msg_data = (uint8_t*)data;
                if (msg_data[0] == BLE_COMMAND_BYTE_START && 
                    msg_data[2] == BLE_COMMAND_MSG_COLON) {
                    uint8_t msg_type = msg_data[3];
                    
                    /* Only log specific notification types occasionally */
                    if (now_err - last_enotsup_warning > 5000) {
                        LOG_DBG("Notification type 0x%02x not enabled by client", msg_type);
                        last_enotsup_warning = now_err;
                    }
                }
            }
        }
        
        /* Special handling with ACL flow control enabled */
        if (err == -ENOMEM) {
            /* With ACL flow control, this is likely temporary buffer exhaustion - add more backoff */
            static uint32_t last_backoff_time = 0;
            if (now_err - last_backoff_time > 2000) {
                LOG_WRN("BLE stack buffer full (-ENOMEM), adding 250ms backoff");
                last_backoff_time = now_err;
            }
            /* Increase backoff time to 250ms to allow stack to recover */
            last_notification_time = now + 200;
        }
        else if (err == -BT_ATT_ERR_UNLIKELY || err == -ENOTCONN) {
            /* Only log connection resets occasionally */
            static uint32_t last_conn_reset_time = 0;
            if (now_err - last_conn_reset_time > 5000) {
                LOG_ERR("Connection issue detected, resetting connection state");
                last_conn_reset_time = now_err;
            }
            
            /* Reset connection on critical errors */
            if (current_conn) {
                bt_conn_unref(current_conn);
                current_conn = NULL;
            }
            is_connected = false;
        }
    } else {
        LOG_DBG("Notification sent successfully");
    }
    
    return err;
}

/* Constants moved to ble_notifications.h */
static bool notify_enabled = false;
static bool connection_notif_reset_needed = true; /* Track when we need to reset notification states */

/* Per-connection tracking for notification support */
static struct {
    bool heartbeat_works;
    bool role_works;
    bool time_works;
    bool led_works;
    bool cpr_works;
} notification_support = {false, false, false, false, false};

/* Reference stream_notify_enable from ble_handlers.c */
extern volatile bool stream_notify_enable;

/* Always allow CPR notifications, even if standard notifications aren't enabled */
static bool cpr_notifications_allowed = true;

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
static uint32_t cpr_session_start_time = 0;
static bool cpr_session_active = false;  /* MUST remain false at startup */

/* Function to check if CPR session is active */
bool is_cpr_session_active(void)
{
    /* Only log when state changes to reduce noise */
    static bool last_logged_state = false;
    if (last_logged_state != cpr_session_active) {
        LOG_INF("CPR session active check: state changed from %d to %d", 
                last_logged_state, cpr_session_active);
        last_logged_state = cpr_session_active;
    }
    return cpr_session_active;
}

/* Function to handle CPR session start */
void start_cpr_session(void) 
{
    LOG_INF("*******************************************");
    LOG_INF("***** STARTING CPR SESSION *****");
    LOG_INF("*******************************************");
    LOG_INF("Current state before start: active=%d, start_time=%u", 
            cpr_session_active, cpr_session_start_time);
            
    /* Safety check to prevent activation during system startup */
    if (k_uptime_get_32() < 1000) {
        LOG_ERR("PREVENTING CPR session start during early boot (uptime < 1s)");
        return;
    }
    
    /* Always start a new session */
    cpr_session_active = true;
    cpr_session_start_time = k_uptime_get_32();
    LOG_INF("CPR session started - timer initialized at %u", cpr_session_start_time);
    
    /* We'll send notification from the timer handler after detecting state change */
    /* This is safer because the timer handler has context to access BLE services */
    LOG_INF("CPR session start: Notification will be sent via timer handler");
}

/* Function to handle CPR session stop */
void stop_cpr_session(void)
{
    LOG_INF("*******************************************");
    LOG_INF("***** STOPPING CPR SESSION *****");
    LOG_INF("*******************************************");
    LOG_INF("Current state before stop: active=%d, start_time=%u", 
            cpr_session_active, cpr_session_start_time);
    
    if (!cpr_session_active) {
        LOG_INF("CPR session already inactive - nothing to stop");
        
        /* We'll send notification from the timer handler after detecting state change */
        LOG_INF("CPR session stop: Already inactive - notification will be sent via timer handler");
        
        return;
    }
    
    /* Calculate final duration */
    uint32_t now = k_uptime_get_32();
    uint32_t elapsed_ms = 0;
    if (cpr_session_start_time > 0) {
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
    
    /* Store elapsed time for notification via timer handler */
    LOG_INF("CPR session stop: Notification with duration %u seconds will be sent via timer handler", elapsed_sec);
}

/* Function to get current CPR session elapsed time in seconds */
uint32_t get_cpr_session_time(void)
{
    if (!cpr_session_active) {
        return 0;
    }
    
    uint32_t current_time = k_uptime_get_32();
    uint32_t elapsed_ms = current_time - cpr_session_start_time;
    return elapsed_ms / 1000;  /* Return seconds */
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
    if (offset + len > sizeof(recv_buffer)) {
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
    }

    /* Copy data to our buffer */
    memcpy(recv_buffer + offset, buf, len);
    
    LOG_INF("Received data, length: %d bytes", len);
    
    /* Print the data as hex for debugging */
    LOG_HEXDUMP_INF(buf, len, "Received data");

    /* Parse the command to see if we need to send an immediate acknowledgment */
    if (len >= 6 && 
        ((uint8_t*)buf)[0] == BLE_COMMAND_BYTE_START && 
        ((uint8_t*)buf)[2] == BLE_COMMAND_MSG_COLON) {
        
        /* Extract the command byte */
        uint8_t cmd_byte = ((uint8_t*)buf)[3];
        
        /* Check if this is a command that requires immediate acknowledgment */
        if (cmd_byte == CPR_CONTROL_START || 
            cmd_byte == CPR_COMMAND_STOP || 
            cmd_byte == CMD_COMMAND_DATA ||
            cmd_byte == CMD_COMMAND_TIMEDATA) {
            
            LOG_INF("Received command 0x%02x, sending immediate acknowledgment", cmd_byte);
            
            /* Send an acknowledgment with the same command byte */
            int err = send_command_ack(cmd_byte);
            
            /* Only log success or non-connection errors */
            if (err == 0) {
                LOG_INF("Command acknowledgment sent for cmd 0x%02x", cmd_byte);
            } else if (err != -ENOTCONN && err != -ENOTSUP) {
                /* We don't log connection errors because they're expected when no device is connected */
                LOG_ERR("Failed to send command acknowledgment (err %d)", err);
            }
        }
    }

    /* Submit the received data to the message processor */
    int ret = submit_command(buf, len);
    if (ret) {
        LOG_ERR("Failed to submit command to message processor (err %d)", ret);
    } else {
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
    if (offset + len > sizeof(ios_cmd_buffer)) {
        LOG_ERR("iOS command buffer overflow (%d > %d)", offset + len, sizeof(ios_cmd_buffer));
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
    }

    /* Copy data to our buffer */
    memcpy(ios_cmd_buffer + offset, buf, len);
    
    /* If this is the beginning of a long write, wait for the complete data */
    if (flags & BT_GATT_WRITE_FLAG_PREPARE) {
        LOG_INF("Prepare write received, waiting for more data or execute");
        return len;
    }
    
    /* If it's a partial write, wait for the complete data */
    if (offset > 0 && !(flags & BT_GATT_WRITE_FLAG_EXECUTE)) {
        LOG_INF("Partial write at offset %d, waiting for more data", offset);
        return len;
    }
    
    /* At this point we have the complete data, process it */
    uint16_t total_len = offset + len;
    LOG_INF("Processing complete iOS command data, total length: %d bytes", total_len);
    
    /* Parse the command to see if we need to send an immediate acknowledgment */
    if (total_len >= 6 && 
        ios_cmd_buffer[0] == BLE_COMMAND_BYTE_START && 
        ios_cmd_buffer[2] == BLE_COMMAND_MSG_COLON) {
        
        /* Extract the command byte */
        uint8_t cmd_byte = ios_cmd_buffer[3];
        
        LOG_INF("Received valid formatted iOS command with type 0x%02x", cmd_byte);
        
        /* Check if this is a command that requires immediate acknowledgment */
        if (cmd_byte == CPR_CONTROL_START || 
            cmd_byte == CPR_COMMAND_STOP || 
            cmd_byte == CMD_COMMAND_DATA ||
            cmd_byte == CMD_COMMAND_TIMEDATA) {
            
            LOG_INF("Received iOS command 0x%02x, sending immediate acknowledgment", cmd_byte);
            
            /* Before sending ack, process the command through message processor */
            int ret = submit_command(ios_cmd_buffer, total_len);
            if (ret) {
                LOG_ERR("Failed to submit iOS command to message processor (err %d)", ret);
            } else {
                LOG_INF("iOS command submitted to message processor successfully");
            }
            
            /* Send an acknowledgment with the same command byte */
            int err = send_command_ack(cmd_byte);
            
            /* Only log success or non-connection errors */
            if (err == 0) {
                LOG_INF("iOS Command acknowledgment sent for cmd 0x%02x", cmd_byte);
            } else if (err != -ENOTCONN && err != -ENOTSUP) {
                /* We don't log connection errors because they're expected when no device is connected */
                LOG_ERR("Failed to send iOS command acknowledgment (err %d)", err);
            }
            
            return total_len;
        }
    }

    /* If not a special command or invalid format, still process it normally */
    LOG_INF("Processing general iOS command");
    int ret = submit_command(ios_cmd_buffer, total_len);
    if (ret) {
        LOG_ERR("Failed to submit iOS command to message processor (err %d)", ret);
    } else {
        LOG_INF("iOS command submitted to message processor successfully");
    }

    return total_len;
}

/* Message types are defined in ble_notifications.h */

/* Notification timer callback */
static void notify_timer_handler(struct k_timer *timer)
{
    /* Only send notifications if enabled AND we have a stable connection */
    if (notify_enabled && is_connected && current_conn) {
        /* Check if enough time has passed since the last notification attempt to reduce errors */
        uint32_t now = k_uptime_get_32();
        static uint32_t last_sent_time = 0;
        
        if (now - last_sent_time >= 250) { /* Ensure at least 250ms between heartbeats */
            /* Update the notification data with a counter */
            notify_count++;
            
            /* Only try sending if global tracking says it works */
            static uint32_t last_heartbeat_attempt = 0;
            
            /* Only try sending if it's worked before or we haven't tried in a while */
            if (notification_support.heartbeat_works || (now - last_heartbeat_attempt > 60000)) {
                last_heartbeat_attempt = now;
                
                /* Try sending heartbeat notification */
                int err = send_ble_notification(NOTIFY_TYPE_HEARTBEAT, &notify_count, sizeof(notify_count));
                
                if (err == 0) {
                    LOG_DBG("Periodic notification sent: %d", notify_count);
                    last_sent_time = now;
                    notification_support.heartbeat_works = true;
                } else if (err == -ENOTSUP) {
                    /* This iOS client doesn't support heartbeat notifications - disable permanently */
                    notification_support.heartbeat_works = false;
                    last_heartbeat_attempt = UINT32_MAX/2; /* Effectively disable future attempts */
                    LOG_WRN("Heartbeat notifications DISABLED - not supported by client");
                } else if (err != -ENOTCONN) {
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
    cpr_state_buffer[0] = is_cpr_session_active() ? 0x01 : 0x00;  /* Active/Inactive */
    cpr_state_buffer[1] = (elapsed_sec >> 24) & 0xFF;  /* MSB */
    cpr_state_buffer[2] = (elapsed_sec >> 16) & 0xFF;
    cpr_state_buffer[3] = (elapsed_sec >> 8) & 0xFF;
    cpr_state_buffer[4] = elapsed_sec & 0xFF;          /* LSB */
    
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
    if (notify_enabled) {
        /* Start sending periodic notifications (1 per second) */
        k_timer_start(&notify_timer, K_MSEC(1000), K_MSEC(1000));
    } else {
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
                          BT_GATT_PERM_NONE,  /* No direct read/write perms - notifications only */
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
                          BT_GATT_CHRC_WRITE,  /* Only write with response, no notify/read */
                          BT_GATT_PERM_WRITE | BT_GATT_PERM_PREPARE_WRITE,  /* Support long writes */
                          NULL, ios_cmd_write, ios_cmd_buffer),
);

/* Note: Using minimal advertising data directly in the advertising function */

/* Work queue item for delayed advertising */
static struct k_work_delayable adv_work;

/* Robust advertising function with work queue handling */
static void advertising_work_handler(struct k_work *work)
{
    static int retry_count = 0;
    static int backoff_time = 0;
    
    /* Stop any existing advertising */
    bt_le_adv_stop();
    
    /* Define advertising parameters with higher reliability */
    static const struct bt_le_adv_param param = {
        .options = BT_LE_ADV_OPT_CONN | BT_LE_ADV_OPT_ONE_TIME,  /* Connectable and one-time flag */
        .interval_min = BT_GAP_ADV_FAST_INT_MIN_2,  /* Use faster interval for better response */
        .interval_max = BT_GAP_ADV_FAST_INT_MAX_2,
        .id = BT_ID_DEFAULT,
        .sid = 0,
        .secondary_max_skip = 0,
        .peer = NULL,
    };
    
    /* Minimal advertising data - just flags */
    static const uint8_t flag_data[] = { BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR };
    static const struct bt_data minimal_ad[] = {
        { BT_DATA_FLAGS, sizeof(flag_data), flag_data },
    };
    
    /* Calculate backoff time based on retry count with exponential increase */
    if (retry_count == 0) {
        backoff_time = 3000; /* 3 seconds for first retry */
    } else {
        backoff_time = backoff_time * 2; /* Double the backoff time for each retry */
        if (backoff_time > 30000) {
            backoff_time = 30000; /* Max 30 seconds between retries */
        }
    }
    
    LOG_INF("Advertising attempt #%d", retry_count + 1);
    int err = bt_le_adv_start(&param, minimal_ad, ARRAY_SIZE(minimal_ad), NULL, 0);
    
    if (err) {
        /* Special handling for common errors */
        if (err == -ENOMEM) {
            LOG_ERR("Advertising failed due to memory constraints (ENOMEM), retrying in %d ms", backoff_time);
        } else if (err == -EALREADY) {
            LOG_ERR("Advertising already active (EALREADY), stopping and retrying in %d ms", backoff_time);
            bt_le_adv_stop();
        } else {
            LOG_ERR("Advertising failed (err %d), retrying in %d ms", err, backoff_time);
        }
        
        retry_count++;
        
        /* Limit number of retries to avoid infinite loop */
        if (retry_count < 10) {
            /* Schedule next retry with exponential backoff */
            k_work_schedule(&adv_work, K_MSEC(backoff_time));
        } else {
            LOG_ERR("Advertising retry limit reached. Giving up after %d attempts", retry_count);
            retry_count = 0; /* Reset for next time */
        }
    } else {
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
    if (err) {
        LOG_ERR("Bluetooth init failed (err %d)", err);
        return;
    }

    LOG_INF("Bluetooth initialized successfully");
    
    /* The service is already registered automatically by BT_GATT_SERVICE_DEFINE */
    LOG_INF("GATT service ready");
    
    /* Start advertising using our robust method */
    LOG_INF("Starting initial advertising");
    advertising_work_handler(NULL);  /* Start advertising immediately */

    LOG_INF("Initial advertising request submitted");
}

/* Connected callback */
static void connected(struct bt_conn *conn, uint8_t err)
{
    if (err) {
        LOG_ERR("Connection failed (err %d)", err);
        return;
    }

    LOG_INF("**********************************************");
    LOG_INF("*************** CONNECTED *******************");
    LOG_INF("**********************************************");
    
    /* Store the connection and set connected flag */
    if (current_conn) {
        bt_conn_unref(current_conn);
    }
    current_conn = bt_conn_ref(conn);
    is_connected = true;
    connection_time = k_uptime_get_32();  /* Record when connection was established */
    
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
    if (current_conn) {
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

/* Timer to check for LED requests from message processor */
static struct k_timer led_timer;

/* Forward declaration for advertising timer */
static void start_adv_with_delay(void);

/* LED timer handler */
static void led_timer_handler(struct k_timer *timer)
{
    /* Check if there's a pending LED request */
    if (led_request_pending) {
        led_request_pending = false;
        LOG_INF("Processing LED request: %s", led_requested_state ? "ON" : "OFF");
        
        /* Control the physical LED */
        if (led_requested_state) {
            led_on();  /* Turn LED on using the LED service */
        } else {
            led_off(); /* Turn LED off using the LED service */
        }
        
        LOG_INF("LED is now %s", led_requested_state ? "ON" : "OFF");
        
        /* Send a notification if notifications are enabled */
        if (notify_enabled) {
            uint8_t led_state = led_requested_state ? 0x01 : 0x00;
            
            int err = send_ble_notification(NOTIFY_TYPE_LED_STATE, &led_state, sizeof(led_state));
            
            /* Only log success or non-connection errors */
            if (err == 0) {
                LOG_INF("LED state notification sent: %d", led_requested_state);
            } else if (err != -ENOTCONN && err != -ENOTSUP) {
                /* We don't log connection errors because they're expected when no device is connected */
                LOG_ERR("LED state notification failed (err %d)", err);
            }
        }
    }
    
    /* Check CPR session status and track elapsed time */
    uint32_t now = k_uptime_get_32();
    
    /* Update CPR session time if active */
    if (is_cpr_session_active()) {
        uint32_t elapsed_seconds = get_cpr_session_time();
        
        /* Log CPR session time less frequently and at debug level */
        static uint32_t last_logged_time = 0;
        if (elapsed_seconds % 10 == 0 && elapsed_seconds > 0 && elapsed_seconds != last_logged_time) {
            last_logged_time = elapsed_seconds;
            uint32_t minutes = elapsed_seconds / 60;
            uint32_t seconds = elapsed_seconds % 60;
            LOG_DBG("CPR Session Time: %02d:%02d (elapsed seconds: %u)", 
                   minutes, seconds, elapsed_seconds);
            
            /* If notifications are enabled or CPR notifications allowed, and we have a valid, ready connection */
            uint32_t now = k_uptime_get_32();
            bool connection_ready = (now - connection_time) >= connection_ready_delay;
            
            if ((notify_enabled || cpr_notifications_allowed) && is_connected && current_conn && connection_ready) {
                /* Format time in MM:SS format */
                uint32_t minutes = elapsed_seconds / 60;
                uint32_t seconds = elapsed_seconds % 60;
                
                /* Create a payload with [ELAPSED_SEC][TIME_STR] */
                uint8_t payload[32]; /* Increased size to handle protocol overhead */
                
                /* Add elapsed time as 32-bit value */
                payload[0] = (elapsed_seconds >> 24) & 0xFF;
                payload[1] = (elapsed_seconds >> 16) & 0xFF;
                payload[2] = (elapsed_seconds >> 8) & 0xFF;
                payload[3] = elapsed_seconds & 0xFF;
                
                /* Add formatted time string "cpr:MM:SS" */
                char time_str[16]; /* Increased buffer size to avoid truncation warnings */
                snprintf(time_str, sizeof(time_str), "cpr:%02d:%02d", minutes, seconds);
                size_t str_len = strlen(time_str);
                
                /* Copy the string to the payload */
                memcpy(&payload[4], time_str, str_len);
                
                /* Send notification with both binary time and human-readable format */
                int err = send_ble_notification(NOTIFY_TYPE_CPR_TIME, payload, 4 + str_len);
                
                /* Only log success or non-connection errors */
                if (err == 0) {
                    LOG_DBG("CPR session time notification sent: %s (%u seconds)", 
                           time_str, elapsed_seconds);
                } else if (err != -ENOTCONN && err != -ENOTSUP) {
                    /* We don't log connection errors because they're expected when no device is connected */
                    LOG_ERR("CPR session time notification failed (err %d)", err);
                }
            }
        }
    }
    
    /* Track CPR session state changes and command acknowledgments */
    static bool last_notified_state = false;
    static bool start_ack_sent = false;
    static bool stop_ack_sent = false;
    static uint32_t session_stop_time = 0;
    
    /* Handle CPR state changes first */
    if (last_notified_state != cpr_session_active) {
        LOG_INF("CPR session state changed for notification: %d -> %d", 
                last_notified_state, cpr_session_active);
        
        /* Update state tracking */
        if (cpr_session_active && !last_notified_state) {
            /* Session just activated - reset acknowledgment flags */
            start_ack_sent = false;
            stop_ack_sent = true;  /* Don't send stop ack yet */
        } 
        else if (!cpr_session_active && last_notified_state) {
            /* Session just stopped - record stop time for stop ack */
            session_stop_time = now;
            start_ack_sent = true;  /* Don't send start ack anymore */
            stop_ack_sent = false;  /* Need to send stop ack */
        }
        
        /* Send state change notification only if we have a valid and ready connection */
        uint32_t now = k_uptime_get_32();
        bool connection_ready = (now - connection_time) >= connection_ready_delay;
        
        if ((notify_enabled || cpr_notifications_allowed) && is_connected && current_conn && connection_ready) {
            if (cpr_session_active) {
                /* Just need to send a single byte with state value */
                uint8_t state = 0x01;  /* State: Active */
                
                int err = send_ble_notification(NOTIFY_TYPE_CPR_STATE, &state, sizeof(state));
                
                /* Only log success or non-connection errors */
                if (err == 0) {
                    LOG_INF("CPR session ACTIVE state notification sent successfully");
                    last_notified_state = cpr_session_active;  /* Update notified state */
                } else if (err != -ENOTCONN && err != -ENOTSUP) {
                    /* We don't log connection errors because they're expected when no device is connected */
                    LOG_ERR("CPR session ACTIVE state notification failed (err %d)", err);
                    /* Don't update state so we'll try again next time */
                }
            } else {
                /* Just need to send a single byte with state value */
                uint8_t state = 0x00;  /* State: Inactive */
                
                int err = send_ble_notification(NOTIFY_TYPE_CPR_STATE, &state, sizeof(state));
                
                /* Only log success or non-connection errors */
                if (err == 0) {
                    LOG_INF("CPR session INACTIVE state notification sent successfully");
                    last_notified_state = cpr_session_active;  /* Update notified state */
                } else if (err != -ENOTCONN && err != -ENOTSUP) {
                    /* We don't log connection errors because they're expected when no device is connected */
                    LOG_ERR("CPR session INACTIVE state notification failed (err %d)", err);
                    /* Don't update state so we'll try again next time */
                }
            }
        } else {
            /* Only log notification state issues occasionally to reduce log spam */
            static uint32_t last_notif_warning = 0;
            uint32_t now_warn = k_uptime_get_32();
            
            if (now_warn - last_notif_warning > 30000) { /* Only log every 30 seconds */
                last_notif_warning = now_warn;
                LOG_DBG("BLE notifications not enabled, no state notification sent");
            }
            last_notified_state = cpr_session_active;  /* Update even if no notification is sent */
        }
    }
    
    /* Now handle command acknowledgments - only if we have a valid and ready connection */
    uint32_t now_timer = k_uptime_get_32();
    bool connection_ready = (now_timer - connection_time) >= connection_ready_delay;
    
    if ((notify_enabled || cpr_notifications_allowed) && is_connected && current_conn && connection_ready) {
        /* Send start acknowledgment when first starting */
        if (cpr_session_active && !start_ack_sent) {
            /* Calculate elapsed time (should be close to 0) */
            uint32_t elapsed_sec = get_cpr_session_time();
            uint32_t minutes = elapsed_sec / 60;
            uint32_t seconds = elapsed_sec % 60;
            
            /* Prepare a payload with command details and formatted time */
            uint8_t payload[32]; /* Increased size to handle protocol overhead */
            
            payload[0] = CPR_CMD_START;       /* Command: Start CPR */
            payload[1] = STATUS_OK;           /* Status: OK */
            
            /* Add formatted time string "cpr:MM:SS" */
            char time_str[16]; /* Increased buffer size to avoid truncation warnings */
            snprintf(time_str, sizeof(time_str), "cpr:%02d:%02d", minutes, seconds);
            size_t str_len = strlen(time_str);
            
            /* Copy the string to the payload */
            memcpy(&payload[2], time_str, str_len);
            
            /* Send notification with command details and time string */
            int err = send_ble_notification(NOTIFY_TYPE_CPR_CMD_ACK, payload, 2 + str_len);
            
            /* Only log success or non-connection errors */
            if (err == 0) {
                LOG_INF("CPR START command acknowledgment sent: OK with time %s", time_str);
                start_ack_sent = true;
            } else if (err != -ENOTCONN && err != -ENOTSUP) {
                /* We don't log connection errors because they're expected when no device is connected */
                LOG_ERR("CPR start acknowledgment failed (err %d)", err);
                /* Don't mark as sent so we'll retry later */
            }
        }
        
        /* Send stop acknowledgment when stopped */
        if (!cpr_session_active && !stop_ack_sent) {
            /* Calculate session duration if we have valid times */
            uint32_t elapsed_sec = 0;
            if (session_stop_time > cpr_session_start_time && cpr_session_start_time > 0) {
                uint32_t elapsed_ms = session_stop_time - cpr_session_start_time;
                elapsed_sec = elapsed_ms / 1000;
            }
            
            /* Format elapsed time for human-readable format */
            uint32_t minutes = elapsed_sec / 60;
            uint32_t seconds = elapsed_sec % 60;
            
            /* Prepare a payload with command details, duration, and formatted time */
            uint8_t payload[32]; /* Increased size to handle protocol overhead */
            
            payload[0] = CPR_CMD_STOP;               /* Command: Stop CPR */
            payload[1] = STATUS_OK;                  /* Status: OK */
            
            /* Format binary duration as well */
            payload[2] = (elapsed_sec >> 8) & 0xFF;  /* Duration high byte */
            payload[3] = elapsed_sec & 0xFF;         /* Duration low byte */
            
            /* Add formatted time string "cpr:MM:SS" */
            char time_str[16]; /* Increased buffer size to avoid truncation warnings */
            snprintf(time_str, sizeof(time_str), "cpr:%02d:%02d", minutes, seconds);
            size_t str_len = strlen(time_str);
            
            /* Copy the string to the payload */
            memcpy(&payload[4], time_str, str_len);
            
            /* Send notification with command details, duration, and time string */
            int err = send_ble_notification(NOTIFY_TYPE_CPR_CMD_ACK, payload, 4 + str_len);
            
            /* Only log success or non-connection errors */
            if (err == 0) {
                LOG_INF("CPR STOP command acknowledgment sent: OK with time %s (%u seconds)", 
                        time_str, elapsed_sec);
                stop_ack_sent = true;
            } else if (err != -ENOTCONN && err != -ENOTSUP) {
                /* We don't log connection errors because they're expected when no device is connected */
                LOG_ERR("CPR stop acknowledgment failed (err %d)", err);
                /* Don't mark as sent so we'll retry later */
            }
        }
    }
    
    /* Periodically check if we have user role data to report */
    static uint32_t last_role_check = 0;
    
    if (now - last_role_check > 5000) {  /* Check every 5 seconds */
        last_role_check = now;
        
        uint8_t role = get_user_role();
        /* Check notification state AND connection status */
        if (role != USER_ROLE_NONE && notify_enabled && 
            is_connected && current_conn && (now - connection_time) >= connection_ready_delay) {
            /* Prepare a structured notification with user role info */
            char id_buffer[20];
            
            /* Get the ID string based on role */
            size_t id_len = 0;
            if (role == USER_ROLE_INSTRUCTOR) {
                id_len = get_instructor_id(id_buffer, sizeof(id_buffer));
            } else if (role == USER_ROLE_TRAINEE) {
                id_len = get_trainee_id(id_buffer, sizeof(id_buffer));
            }
            
            /* Add ID to notification if we have one */
            if (id_len > 0) {
                /* Prepare payload with role and ID */
                uint8_t payload[32]; /* Increased size to handle protocol overhead */
                
                payload[0] = role;               /* Role: 1=Instructor, 2=Trainee */
                payload[1] = (uint8_t)id_len;    /* Length of ID string */
                memcpy(&payload[2], id_buffer, id_len);
                
                                /* Use global notification support tracking */
                static uint32_t last_role_attempt = 0;
                
                /* Only try sending if it's worked before or we haven't tried in a while */
                if (notification_support.role_works || (now - last_role_attempt > 60000)) {
                    last_role_attempt = now;
                    
                    /* Try to send notification with role data */
                    int err = send_ble_notification(NOTIFY_TYPE_USER_ROLE, payload, 2 + id_len);
                    
                    if (err == 0) {
                        LOG_INF("User role notification sent: role=%d, id=%s", role, id_buffer);
                        notification_support.role_works = true;
                    } else if (err == -ENOTSUP) {
                        /* This iOS client doesn't support role notifications - disable permanently */
                        notification_support.role_works = false;
                        last_role_attempt = UINT32_MAX/2; /* Effectively disable future attempts */
                        LOG_WRN("User role notifications DISABLED - not supported by client");
                    } else if (err != -ENOTCONN) {
                        /* Log other non-connection errors */
                        LOG_ERR("User role notification failed (err %d)", err);
                    }
                }
            }
        }
        
        /* Also check for time data - verify both notifications AND connection status */
        if (has_received_time_data() && notify_enabled && 
            is_connected && current_conn && (now - connection_time) >= connection_ready_delay) {
            char time_buffer[20];
            size_t time_len = get_time_data(time_buffer, sizeof(time_buffer));
            
            if (time_len > 0) {
                /* Prepare payload with time data */
                uint8_t payload[32]; /* Increased size to handle protocol overhead */
                
                payload[0] = (uint8_t)time_len;  /* Length of time data */
                memcpy(&payload[1], time_buffer, time_len);
                
                                /* Use global notification support tracking */
                static uint32_t last_attempt_time = 0;
                
                /* Only try sending if it's worked before or we haven't tried in a while */
                if (notification_support.time_works || (now - last_attempt_time > 60000)) {
                    last_attempt_time = now;
                    
                    /* Try to send notification with time data */
                    int err = send_ble_notification(NOTIFY_TYPE_TIME_DATA, payload, 1 + time_len);
                    
                    if (err == 0) {
                        LOG_INF("Time data notification sent: %s", time_buffer);
                        notification_support.time_works = true;
                    } else if (err == -ENOTSUP) {
                        /* This iOS client doesn't support time data notifications - disable permanently */
                        notification_support.time_works = false;
                        last_attempt_time = UINT32_MAX/2; /* Effectively disable future attempts */
                        LOG_WRN("Time data notifications DISABLED - not supported by client");
                    } else if (err != -ENOTCONN) {
                        /* Log other non-connection errors */
                        LOG_ERR("Time data notification failed (err %d)", err);
                    }
                }
            }
        }
    }
}

/* Main entry point */
int main(void)
{
    int err;
    
    printk("Bluetooth application with GATT service and Message Processor\n");
    LOG_INF("Starting Bluetooth application with GATT service and Message Processor");
    
    /* Initialize our basic implementation module */
    basic_implementation_init();
    
    /* Initialize the LED driver */
    err = led_init();
    if (err) {
        LOG_ERR("LED initialization failed (err %d)", err);
    } else {
        LOG_INF("LED initialized successfully");
        /* Flash the LED once to indicate we're running */
        led_on();
        k_sleep(K_MSEC(500));
        led_off();
    }
    
    /* Initialize notification timer */
    k_timer_init(&notify_timer, notify_timer_handler, NULL);
    
    /* Initialize LED timer to check for LED requests */
    k_timer_init(&led_timer, led_timer_handler, NULL);
    k_timer_start(&led_timer, K_MSEC(100), K_MSEC(100));  /* Check every 100ms */
    
    /* Initialize the advertising work queue item */
    k_work_init_delayable(&adv_work, advertising_work_handler);
    
    /* Initialize the message processor */
    err = message_processor_init();
    if (err) {
        LOG_ERR("Message processor initialization failed (err %d)", err);
    } else {
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
    if (err) {
        LOG_ERR("Bluetooth init failed (err %d)", err);
    }
    
    /* Test message processor with a simple command */
    LOG_INF("Testing message processor with direct commands");
    
    /* Wait a moment for everything to initialize */
    k_sleep(K_SECONDS(2));
    
    /* Submit test commands to control LED via message processor */
    LOG_INF("Sending LED ON command to message processor");
    submit_direct_command(CMD_CONTROL_LED_ON);
    
    k_sleep(K_SECONDS(2));
    
    LOG_INF("Sending LED OFF command to message processor");
    submit_direct_command(CMD_CONTROL_LED_OFF);
    
    /* Test the BLE protocol formatting */
    LOG_INF("Testing BLE protocol formatting");
    test_ble_protocol();
    
    /* Test CPR session commands - only run if ENABLE_CPR_TEST is defined */
    k_sleep(K_SECONDS(2));
    
#ifdef ENABLE_CPR_TEST
    LOG_INF("Sending CPR START command to message processor");
    submit_direct_command(CPR_CONTROL_START);
    
    k_sleep(K_SECONDS(5));
    
    LOG_INF("Sending CPR STOP command to message processor");
    submit_direct_command(CPR_COMMAND_STOP);
#else
    LOG_INF("CPR auto-test disabled - define ENABLE_CPR_TEST to enable");
#endif
    
    /* Direct LED control test for verification */
    k_sleep(K_SECONDS(2));
    LOG_INF("Direct LED control test - ON");
    led_on();
    
    k_sleep(K_SECONDS(2));
    LOG_INF("Direct LED control test - OFF");
    led_off();
    
    /* Wait for everything to initialize */
    k_sleep(K_SECONDS(2));
    
    /* Enhanced heartbeat in main thread with time data display */
    while (1) {
        char time_buffer[24] = {0};
        char rtc_time[24] = {0};
        
        /* First get and display raw time data */
        if (has_received_time_data()) {
            size_t time_len = get_time_data(time_buffer, sizeof(time_buffer));
            if (time_len > 0) {
                /* Format time data for display: YYYYMMDDHHMMSS -> YYYY-MM-DD HH:MM:SS */
                char formatted_time[24] = {0};
                if (time_len >= 14) {
                    snprintf(formatted_time, sizeof(formatted_time), 
                             "%.4s-%.2s-%.2s %.2s:%.2s:%.2s",
                             time_buffer, time_buffer+4, time_buffer+6,
                             time_buffer+8, time_buffer+10, time_buffer+12);
                    //LOG_INF("Heartbeat - Raw time data: %s", formatted_time);
                } else {
                    LOG_INF("Heartbeat - Raw time data available but invalid format: %s", time_buffer);
                }
            } else {
                LOG_INF("Heartbeat - Raw time data empty");
            }
        } else {
            LOG_INF("Heartbeat - No raw time data received yet");
        }
        
        /* Now get and display formatted RTC time */
        /* Log RTC time only occasionally */
        static uint32_t last_rtc_log = 0;
        uint32_t now_rtc = k_uptime_get_32();
        
        if (now_rtc - last_rtc_log >= 30000) { /* Only log every 30 seconds */
            last_rtc_log = now_rtc;
            
            size_t rtc_len = get_rtc_time(rtc_time, sizeof(rtc_time));
            if (rtc_len > 0) {
                LOG_INF("====== CURRENT TIME: %s ======", rtc_time);
            } else {
                LOG_INF("====== RTC TIME NOT AVAILABLE ======");
            }
        }
        
        /* Get user role information */
        uint8_t role = get_user_role();
        if (role != USER_ROLE_NONE) {
            char id_buffer[20] = {0};
            /* Log user role information less frequently */
            static uint32_t last_role_info_log = 0;
            uint32_t now_role = k_uptime_get_32();
            
            if (now_role - last_role_info_log >= 30000) { /* Only log every 30 seconds */
                last_role_info_log = now_role;
                
                if (role == USER_ROLE_INSTRUCTOR) {
                    get_instructor_id(id_buffer, sizeof(id_buffer));
                    LOG_INF("Heartbeat - Role: Instructor, ID: %s", id_buffer);
                } else if (role == USER_ROLE_TRAINEE) {
                    get_trainee_id(id_buffer, sizeof(id_buffer));
                    LOG_INF("Heartbeat - Role: Trainee, ID: %s", id_buffer);
                }
            }
        } else {
            /* Log lack of user role less frequently */
            static uint32_t last_no_role_log = 0;
            uint32_t now_no_role = k_uptime_get_32();
            
            if (now_no_role - last_no_role_log >= 30000) { /* Only log every 30 seconds */
                last_no_role_log = now_no_role;
                LOG_INF("Heartbeat - No user role set");
            }
        }
        
        /* Periodically display CPR session state - only log every 15 seconds to reduce noise */
        static uint32_t last_cpr_log_time = 0;
        uint32_t now = k_uptime_get_32();
        if (now - last_cpr_log_time >= 15000) {
            last_cpr_log_time = now;
            
            if (cpr_session_active) {
                /* Get current CPR session time and display it */
                uint32_t elapsed_sec = get_cpr_session_time();
                uint32_t minutes = elapsed_sec / 60;
                uint32_t seconds = elapsed_sec % 60;
                    
                LOG_INF("****** CPR SESSION ACTIVE - %02d:%02d elapsed ******", minutes, seconds);
            } else {
                LOG_DBG("------ No CPR session active ------");
            }
        }
        
        /* No automatic CPR session start/stop - controlled only by commands */
        
        /* Make sure we can receive instructor ID commands */
        static bool sent_id = false;
        if (!sent_id && k_uptime_get_32() > 10000) {  /* After 10 seconds */
            const char *test_id = "in:test123";
            LOG_INF("Sending test instructor ID: %s", test_id);
            submit_command((const uint8_t *)test_id, strlen(test_id));
            sent_id = true;
        }
        
        /* After 15 seconds, send a test time data command */
        static bool sent_time = false;
        if (!sent_time && k_uptime_get_32() > 15000) {
            LOG_INF("Sending test time data: 20250506150722");
            
            /* Use the new protocol formatting */
            const char *time_data = "20250506150722";
            uint8_t time_cmd[32];
            
            int cmd_len = format_timedata_command(time_cmd, sizeof(time_cmd), 
                                                time_data, strlen(time_data));
            
            if (cmd_len > 0) {
                LOG_INF("Formatted time data command using protocol, length: %d bytes", cmd_len);
                LOG_HEXDUMP_INF(time_cmd, cmd_len, "Formatted time data command");
                submit_command(time_cmd, cmd_len);
            } else {
                LOG_ERR("Failed to format time data command: %d", cmd_len);
                
                /* Fall back to old format for backward compatibility */
                uint8_t old_time_cmd[] = {
                    MSG_COMMAND_BYTE_START,  /* Start byte */
                    MSG_COMMAND_MSG_COLON,   /* Command separator */
                    CMD_COMMAND_TIMEDATA,    /* Time data command */
                    MSG_COMMAND_MSG_COLON,   /* Data separator */
                    '2', '0', '2', '5', '0', '5', '0', '6', '1', '5', '0', '7', '2', '2', /* Time data */
                    MSG_COMMAND_MSG_END      /* End byte */
                };
                
                submit_command(old_time_cmd, sizeof(old_time_cmd));
            }
            
            sent_time = true;
        }
        
        k_sleep(K_SECONDS(2));
    }
    
    return 0;
}