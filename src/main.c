/* Minimal main.c with Bluetooth initialization and GATT service */
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/printk.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/gatt.h>

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

/* External function from minimal_test.c */
void minimal_test_init(void);

/* Buffer for storing received data */
static uint8_t recv_buffer[20];

/* Notification data buffer */
static uint8_t notify_buffer[20] = {0};

/* Notification state */
static bool notify_enabled;

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

/* Forward declaration of our GATT service */
static struct bt_gatt_attr custom_service_attrs[];
static struct bt_gatt_service custom_svc;

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

    /* Copy same data to notify buffer to demonstrate notifications */
    if (notify_enabled && len <= sizeof(notify_buffer)) {
        memcpy(notify_buffer, buf, len);
        
        /* Send notification with received data */
        int err = bt_gatt_notify(NULL, &custom_service_attrs[4], notify_buffer, len);
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
        int err = bt_gatt_notify(NULL, &custom_service_attrs[4], notify_buffer, 1);
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

/* Define our GATT service attributes */
static struct bt_gatt_attr custom_service_attrs[] = {
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
};

/* Define the GATT service */
static struct bt_gatt_service custom_svc = {
    .attrs = custom_service_attrs,
    .attr_count = ARRAY_SIZE(custom_service_attrs),
};

/* Define advertising data */
static const struct bt_data ad[] = {
    BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
    BT_DATA_BYTES(BT_DATA_UUID128_ALL, 
                 BT_UUID_128_ENCODE(0x12345678, 0x1234, 0x5678, 0x1234, 0x56789abcdef0)),
};

/* Define scan response data (additional advertising data) */
static const struct bt_data sd[] = {
    BT_DATA(BT_DATA_NAME_COMPLETE, "ManikinTest", 11),
};

/* BT ready callback */
static void bt_ready(int err)
{
    if (err) {
        LOG_ERR("Bluetooth init failed (err %d)", err);
        return;
    }

    LOG_INF("Bluetooth initialized successfully");
    
    /* Register our GATT service */
    err = bt_gatt_service_register(&custom_svc);
    if (err) {
        LOG_ERR("Service registration failed (err %d)", err);
        return;
    }
    LOG_INF("GATT service registered successfully");
    
    /* Start advertising with custom data - using non-deprecated macro */
    err = bt_le_adv_start(BT_LE_ADV_CONN_NAME, ad, ARRAY_SIZE(ad), sd, ARRAY_SIZE(sd));
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

/* Main entry point */
int main(void)
{
    int err;
    
    printk("Bluetooth application with GATT service\n");
    LOG_INF("Starting Bluetooth application with GATT service");
    
    /* Initialize our minimal test module */
    minimal_test_init();
    
    /* Initialize notification timer */
    k_timer_init(&notify_timer, notify_timer_handler, NULL);
    
    /* Register connection callbacks */
    bt_conn_cb_register(&conn_callbacks);
    
    /* Initialize Bluetooth subsystem */
    err = bt_enable(bt_ready);
    if (err) {
        LOG_ERR("Bluetooth init failed (err %d)", err);
    }
    
    /* Simple heartbeat in main thread */
    while (1) {
        LOG_INF("Main thread heartbeat");
        k_sleep(K_SECONDS(2));
    }
    
    return 0;
}