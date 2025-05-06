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

/* Global notification buffer and state */
static uint8_t notify_buffer[20] = {0};
static bool notify_enabled = false;

/* External function from minimal_test.c */
void minimal_test_init(void);

/* Buffer for storing received data */
static uint8_t recv_buffer[20];

/* LED control flags - for message processor */
bool led_request_pending = false;
bool led_requested_state = false;

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

/* Forward declaration of our GATT service (defined later) */
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

/* CCC change handler for notification characteristic */
static void notify_ccc_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
    notify_enabled = (value == BT_GATT_CCC_NOTIFY);
    LOG_INF("Notifications %s", notify_enabled ? "enabled" : "disabled");
    
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
);

/* Define simplest valid advertising data */
static const struct bt_data ad[] = {
    BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
};

/* Empty scan response data to ensure minimal configuration */
static const struct bt_data sd[] = {
    /* Keep empty */
};

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
    
    /* Start advertising with basic configuration */
    /* Define parameters manually to avoid deprecation warnings */
    static const struct bt_le_adv_param param = {
        .options = BT_LE_ADV_OPT_CONNECTABLE | BT_LE_ADV_OPT_ONE_TIME,
        .interval_min = BT_GAP_ADV_FAST_INT_MIN_2,
        .interval_max = BT_GAP_ADV_FAST_INT_MAX_2,
        .id = BT_ID_DEFAULT,
        .sid = 0,
        .secondary_max_skip = 0,
        .peer = NULL,
    };
    
    err = bt_le_adv_start(&param, ad, ARRAY_SIZE(ad), NULL, 0);
    if (err) {
        LOG_ERR("Advertising failed to start (err %d)", err);
        return;
    }

    LOG_INF("Advertising started with custom service UUID");
}

/* Connected callback */
static void connected(struct bt_conn *conn, uint8_t err)
{
    if (err) {
        LOG_ERR("Connection failed (err %d)", err);
        return;
    }

    LOG_INF("Connected");
}

/* Disconnected callback */
static void disconnected(struct bt_conn *conn, uint8_t reason)
{
    LOG_INF("Disconnected (reason %u)", reason);
}

/* Connection callbacks structure */
static struct bt_conn_cb conn_callbacks = {
    .connected = connected,
    .disconnected = disconnected,
};

/* Timer to check for LED requests from message processor */
static struct k_timer led_timer;

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
    
    /* Periodically check if we have user role data to report */
    static uint32_t last_role_check = 0;
    uint32_t now = k_uptime_get_32();
    
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
    
    /* Initialize our minimal test module */
    minimal_test_init();
    
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
    
    /* Initialize the message processor */
    err = message_processor_init();
    if (err) {
        LOG_ERR("Message processor initialization failed (err %d)", err);
    } else {
        LOG_INF("Message processor initialized successfully");
    }
    
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
    
    /* Direct LED control test for verification */
    k_sleep(K_SECONDS(2));
    LOG_INF("Direct LED control test - ON");
    led_on();
    
    k_sleep(K_SECONDS(2));
    LOG_INF("Direct LED control test - OFF");
    led_off();
    
    /* Simple heartbeat in main thread */
    while (1) {
        LOG_INF("Main thread heartbeat");
        
        /* Make sure we can receive instructor ID commands */
        static bool sent_id = false;
        if (!sent_id && k_uptime_get_32() > 10000) {  /* After 10 seconds */
            const char *test_id = "in:test123";
            LOG_INF("Sending test instructor ID: %s", test_id);
            submit_command((const uint8_t *)test_id, strlen(test_id));
            sent_id = true;
        }
        
        k_sleep(K_SECONDS(2));
    }
    
    return 0;
}