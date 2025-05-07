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


/* Include message processing commands */
#include <stdint.h>
#include <string.h>

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

/* Global connection tracking variables - declared at file scope */
struct bt_conn *current_conn = NULL;
bool is_connected = false;
uint32_t connection_time = 0;   /* Time when connection was established */
uint32_t connection_ready_delay = 2000;  /* Delay in ms before sending notifications */

/* Forward declarations for CPR session management */
bool is_cpr_session_active(void);
void start_cpr_session(void);
void stop_cpr_session(void);
uint32_t get_cpr_session_time(void);

/* Helper function for sending notifications safely */
static int send_notification_safely(const void *data, uint16_t len) {
    /* We still need extern declaration for custom_svc which is defined by BT_GATT_SERVICE_DEFINE macro */
    extern const struct bt_gatt_service_static custom_svc;
    
    /* Only proceed if we have a valid connection that's had time to stabilize */
    if (!is_connected || !current_conn) {
        LOG_WRN("Cannot send notification - no active connection");
        return -ENOTCONN;
    }
    
    uint32_t now = k_uptime_get_32();
    uint32_t conn_age = now - connection_time;
    
    if (conn_age < connection_ready_delay) {
        LOG_WRN("Connection too fresh (%u ms), delaying notification", conn_age);
        return -EAGAIN;
    }
    
    /* Try to send the notification */
    int err = bt_gatt_notify(NULL, &custom_svc.attrs[4], data, len);
    
    /* Handle any errors */
    if (err) {
        LOG_ERR("Notification failed (err %d)", err);
        
        /* If error indicates connection issue, clear our connection state */
        if (err == -BT_ATT_ERR_UNLIKELY || err == -ENOMEM) {
            LOG_ERR("Connection issue detected, resetting connection state");
            if (current_conn) {
                bt_conn_unref(current_conn);
                current_conn = NULL;
            }
            is_connected = false;
        }
    }
    
    return err;
}

/* Global notification buffer and state */
static uint8_t notify_buffer[20] = {0};
static bool notify_enabled = false;

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

/* Forward declaration of our GATT service (defined later with BT_GATT_SERVICE_DEFINE) */
extern const struct bt_gatt_service_static custom_svc;

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

    /* Submit the received data to the message processor */
    int ret = submit_command(buf, len);
    if (ret) {
        LOG_ERR("Failed to submit command to message processor (err %d)", ret);
    } else {
        LOG_INF("Command submitted to message processor successfully");
    }

    /* Copy same data to notify buffer to demonstrate notifications */
    if (notify_enabled && len <= sizeof(notify_buffer)) {
        memcpy(notify_buffer, buf, len);
        
        /* Send notification with received data */
        int err = bt_gatt_notify(NULL, &custom_svc.attrs[4], notify_buffer, len);
        if (err) {
            LOG_ERR("Notification failed (err %d)", err);
        } else {
            LOG_INF("Notification sent, length: %d bytes", len);
        }
    }

    return len;
}

/* Notification timer callback */
static void notify_timer_handler(struct k_timer *timer)
{
    if (notify_enabled) {
        /* Update the notification data with a counter */
        notify_count++;
        notify_buffer[0] = notify_count;
        
        /* Send notification */
        int err = bt_gatt_notify(NULL, &custom_svc.attrs[4], notify_buffer, 1);
        if (err) {
            LOG_ERR("Periodic notification failed (err %d)", err);
        } else {
            LOG_INF("Periodic notification sent: %d", notify_count);
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
    char time_str[10];
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
    
    /* Read/Write characteristic */
    BT_GATT_CHARACTERISTIC(&custom_char_uuid.uuid,
                          BT_GATT_CHRC_WRITE | BT_GATT_CHRC_READ,
                          BT_GATT_PERM_READ | BT_GATT_PERM_WRITE,
                          NULL, custom_char_write, recv_buffer),
                          
    /* Notification characteristic */
    BT_GATT_CHARACTERISTIC(&custom_notify_uuid.uuid,
                          BT_GATT_CHRC_NOTIFY,
                          BT_GATT_PERM_READ,
                          NULL, NULL, notify_buffer),
    BT_GATT_CCC(notify_ccc_changed, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
    
    /* CPR State characteristic - with read and notify capabilities */
    BT_GATT_CHARACTERISTIC(&cpr_state_char_uuid.uuid,
                          BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
                          BT_GATT_PERM_READ,
                          cpr_state_read, NULL, cpr_state_buffer),
    BT_GATT_CCC(notify_ccc_changed, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
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
    
    /* Define the most minimal advertising parameters possible */
    static const struct bt_le_adv_param param = {
        .options = BT_LE_ADV_OPT_CONN,  /* Use the simpler connectable flag */
        .interval_min = BT_GAP_ADV_SLOW_INT_MIN,  /* Use slower interval for stability */
        .interval_max = BT_GAP_ADV_SLOW_INT_MAX,
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
        
        /* Update our notification data to reflect LED state */
        notify_buffer[0] = led_requested_state ? 0x01 : 0x00;
        
        /* Send a notification if notifications are enabled */
        if (notify_enabled) {
            int err = bt_gatt_notify(NULL, &custom_svc.attrs[4], notify_buffer, 1);
            if (err) {
                LOG_ERR("LED state notification failed (err %d)", err);
            } else {
                LOG_INF("LED state notification sent: %d", led_requested_state);
            }
        }
    }
    
    /* Check CPR session status and track elapsed time */
    uint32_t now = k_uptime_get_32();
    
    /* Update CPR session time if active */
    if (is_cpr_session_active()) {
        uint32_t elapsed_seconds = get_cpr_session_time();
        
        /* Log CPR session time every 5 seconds */
        if (elapsed_seconds % 5 == 0 && elapsed_seconds > 0) {
            uint32_t minutes = elapsed_seconds / 60;
            uint32_t seconds = elapsed_seconds % 60;
            LOG_INF("CPR Session Time: %02d:%02d (elapsed seconds: %u)", 
                   minutes, seconds, elapsed_seconds);
            
            /* If notifications are enabled or CPR notifications allowed, and we have a valid, ready connection */
            uint32_t now = k_uptime_get_32();
            bool connection_ready = (now - connection_time) >= connection_ready_delay;
            
            if ((notify_enabled || cpr_notifications_allowed) && is_connected && current_conn && connection_ready) {
                /* Format time in MM:SS format */
                uint32_t minutes = elapsed_seconds / 60;
                uint32_t seconds = elapsed_seconds % 60;
                
                /* Format: [TYPE][ELAPSED_SEC][TIME_STR] */
                notify_buffer[0] = NOTIFY_TYPE_CPR_TIME;  /* Message type: CPR Session Time */
                notify_buffer[1] = (elapsed_seconds >> 24) & 0xFF;
                notify_buffer[2] = (elapsed_seconds >> 16) & 0xFF;
                notify_buffer[3] = (elapsed_seconds >> 8) & 0xFF;
                notify_buffer[4] = elapsed_seconds & 0xFF;
                
                /* Add formatted time string "cpr:MM:SS" */
                char time_str[10];
                snprintf(time_str, sizeof(time_str), "cpr:%02d:%02d", minutes, seconds);
                size_t str_len = strlen(time_str);
                
                /* Copy the string to the notification buffer */
                memcpy(&notify_buffer[5], time_str, str_len);
                
                /* Send notification with both binary time and human-readable format */
                int err = send_notification_safely(notify_buffer, 5 + str_len);
                if (err) {
                    LOG_ERR("CPR session time notification failed (err %d)", err);
                    /* Error handling is done in send_notification_safely */
                } else {
                    LOG_DBG("CPR session time notification sent: %s (%u seconds)", 
                           time_str, elapsed_seconds);
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
                /* Format: [TYPE][STATE=0x01] */
                notify_buffer[0] = NOTIFY_TYPE_CPR_STATE;  /* Message type: CPR Session State */
                notify_buffer[1] = 0x01;  /* State: Active */
                
                int err = send_notification_safely(notify_buffer, 2);
                if (err) {
                    LOG_ERR("CPR session ACTIVE state notification failed (err %d)", err);
                    /* Don't update state so we'll try again next time */
                } else {
                    LOG_INF("CPR session ACTIVE state notification sent successfully");
                    last_notified_state = cpr_session_active;  /* Update notified state */
                }
            } else {
                /* Format: [TYPE][STATE=0x00] */
                notify_buffer[0] = NOTIFY_TYPE_CPR_STATE;  /* Message type: CPR Session State */
                notify_buffer[1] = 0x00;  /* State: Inactive */
                
                int err = send_notification_safely(notify_buffer, 2);
                if (err) {
                    LOG_ERR("CPR session INACTIVE state notification failed (err %d)", err);
                    /* Don't update state so we'll try again next time */
                } else {
                    LOG_INF("CPR session INACTIVE state notification sent successfully");
                    last_notified_state = cpr_session_active;  /* Update notified state */
                }
            }
        } else {
            LOG_INF("BLE notifications not enabled, no state notification sent");
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
            
            /* Format: [TYPE][CMD][STATUS][TIME_STR] */
            notify_buffer[0] = NOTIFY_TYPE_CPR_CMD_ACK;  /* Message type: CPR Command ACK */
            notify_buffer[1] = CPR_CMD_START;            /* Command: Start CPR */
            notify_buffer[2] = STATUS_OK;                /* Status: OK */
            
            /* Add formatted time string "cpr:MM:SS" */
            char time_str[10];
            snprintf(time_str, sizeof(time_str), "cpr:%02d:%02d", minutes, seconds);
            size_t str_len = strlen(time_str);
            
            /* Copy the string to the notification buffer */
            memcpy(&notify_buffer[3], time_str, str_len);
            
            /* Send notification with time string */
            int err = send_notification_safely(notify_buffer, 3 + str_len);
            if (err) {
                LOG_ERR("CPR start acknowledgment failed (err %d)", err);
                /* Don't mark as sent so we'll retry later */
            } else {
                LOG_INF("CPR START command acknowledgment sent: OK with time %s", time_str);
                start_ack_sent = true;
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
            
            /* Format: [TYPE][CMD][STATUS][TIME_STR] */
            notify_buffer[0] = NOTIFY_TYPE_CPR_CMD_ACK;     /* Message type: CPR Command ACK */
            notify_buffer[1] = CPR_CMD_STOP;                /* Command: Stop CPR */
            notify_buffer[2] = STATUS_OK;                   /* Status: OK */
            
            /* Format binary duration as well */
            notify_buffer[3] = (elapsed_sec >> 8) & 0xFF;   /* Duration high byte */
            notify_buffer[4] = elapsed_sec & 0xFF;          /* Duration low byte */
            
            /* Add formatted time string "cpr:MM:SS" */
            char time_str[10];
            snprintf(time_str, sizeof(time_str), "cpr:%02d:%02d", minutes, seconds);
            size_t str_len = strlen(time_str);
            
            /* Copy the string to the notification buffer */
            memcpy(&notify_buffer[5], time_str, str_len);
            
            /* Send notification with both binary duration and formatted time string */
            int err = send_notification_safely(notify_buffer, 5 + str_len);
            if (err) {
                LOG_ERR("CPR stop acknowledgment failed (err %d)", err);
                /* Don't mark as sent so we'll retry later */
            } else {
                LOG_INF("CPR STOP command acknowledgment sent: OK with time %s (%u seconds)", 
                        time_str, elapsed_sec);
                stop_ack_sent = true;
            }
        }
    }
    
    /* Periodically check if we have user role data to report */
    static uint32_t last_role_check = 0;
    
    if (now - last_role_check > 5000) {  /* Check every 5 seconds */
        last_role_check = now;
        
        uint8_t role = get_user_role();
        if (role != USER_ROLE_NONE && notify_enabled) {
            /* Prepare a structured notification with user role info */
            char id_buffer[20];
            
            /* Format: [TYPE=0x10][ROLE=0x01/0x02][ID_LEN][ID_DATA...] */
            notify_buffer[0] = 0x10;  /* Message type: User Role */
            notify_buffer[1] = role;  /* Role: 1=Instructor, 2=Trainee */
            
            /* Get the ID string based on role */
            size_t id_len = 0;
            if (role == USER_ROLE_INSTRUCTOR) {
                id_len = get_instructor_id(id_buffer, sizeof(id_buffer));
            } else if (role == USER_ROLE_TRAINEE) {
                id_len = get_trainee_id(id_buffer, sizeof(id_buffer));
            }
            
            /* Add ID to notification if we have one */
            if (id_len > 0) {
                notify_buffer[2] = (uint8_t)id_len;
                memcpy(&notify_buffer[3], id_buffer, id_len);
                
                /* Send notification with user role data */
                int err = bt_gatt_notify(NULL, &custom_svc.attrs[4], notify_buffer, 3 + id_len);
                if (err) {
                    LOG_ERR("User role notification failed (err %d)", err);
                } else {
                    LOG_INF("User role notification sent: role=%d, id=%s", role, id_buffer);
                }
            }
        }
        
        /* Also check for time data */
        if (has_received_time_data() && notify_enabled) {
            char time_buffer[20];
            size_t time_len = get_time_data(time_buffer, sizeof(time_buffer));
            
            if (time_len > 0) {
                /* Format: [TYPE=0x20][TIME_LEN][TIME_DATA...] */
                notify_buffer[0] = 0x20;  /* Message type: Time Data */
                notify_buffer[1] = (uint8_t)time_len;
                memcpy(&notify_buffer[2], time_buffer, time_len);
                
                /* Send notification with time data */
                int err = bt_gatt_notify(NULL, &custom_svc.attrs[4], notify_buffer, 2 + time_len);
                if (err) {
                    LOG_ERR("Time data notification failed (err %d)", err);
                } else {
                    LOG_INF("Time data notification sent: %s", time_buffer);
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
    
    /* Test CPR session commands */
    k_sleep(K_SECONDS(2));
    
    LOG_INF("Sending CPR START command to message processor");
    submit_direct_command(CMD_CONTROL_START);
    
    k_sleep(K_SECONDS(5));
    
    LOG_INF("Sending CPR STOP command to message processor");
    submit_direct_command(CMD_COMMAND_STOP);
    
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
        size_t rtc_len = get_rtc_time(rtc_time, sizeof(rtc_time));
        if (rtc_len > 0) {
            LOG_INF("====== CURRENT TIME: %s ======", rtc_time);
        } else {
            LOG_INF("====== RTC TIME NOT AVAILABLE ======");
        }
        
        /* Get user role information */
        uint8_t role = get_user_role();
        if (role != USER_ROLE_NONE) {
            char id_buffer[20] = {0};
            if (role == USER_ROLE_INSTRUCTOR) {
                get_instructor_id(id_buffer, sizeof(id_buffer));
                LOG_INF("Heartbeat - Role: Instructor, ID: %s", id_buffer);
            } else if (role == USER_ROLE_TRAINEE) {
                get_trainee_id(id_buffer, sizeof(id_buffer));
                LOG_INF("Heartbeat - Role: Trainee, ID: %s", id_buffer);
            }
        } else {
            LOG_INF("Heartbeat - No user role set");
        }
        
        /* Periodically display CPR session state - only log every 5 seconds to reduce noise */
        static uint32_t last_cpr_log_time = 0;
        uint32_t now = k_uptime_get_32();
        if (now - last_cpr_log_time >= 5000) {
            last_cpr_log_time = now;
            
            if (cpr_session_active) {
                /* Get current CPR session time and display it */
                uint32_t elapsed_sec = get_cpr_session_time();
                uint32_t minutes = elapsed_sec / 60;
                uint32_t seconds = elapsed_sec % 60;
                    
                LOG_INF("****** CPR SESSION ACTIVE - %02d:%02d elapsed ******", minutes, seconds);
            } else {
                LOG_INF("------ No CPR session active ------");
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
            
            /* Format: [Start][Cmd:TimeData][Time:20250506150722][End] */
            uint8_t time_cmd[] = {
                MSG_COMMAND_BYTE_START,  /* Start byte */
                MSG_COMMAND_MSG_COLON,   /* Command separator */
                CMD_COMMAND_TIMEDATA,    /* Time data command */
                MSG_COMMAND_MSG_COLON,   /* Data separator */
                '2', '0', '2', '5', '0', '5', '0', '6', '1', '5', '0', '7', '2', '2', /* Time data */
                MSG_COMMAND_MSG_END      /* End byte */
            };
            
            submit_command(time_cmd, sizeof(time_cmd));
            sent_time = true;
        }
        
        k_sleep(K_SECONDS(2));
    }
    
    return 0;
}