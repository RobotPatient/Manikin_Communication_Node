/* led_service.c - LED BLE Service */

#include "led_service.h"

#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/gatt.h>

LOG_MODULE_REGISTER(led_service, LOG_LEVEL_INF);

/* LED hardware configuration */
static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);
static bool led_state;

/* LED Service UUIDs */
#define BT_UUID_LED_SERVICE_VAL \
    BT_UUID_128_ENCODE(0x12345678, 0x1234, 0x5678, 0x1234, 0x56789abcdef0)

#define BT_UUID_LED_TOGGLE_CHARACTERISTIC_VAL \
    BT_UUID_128_ENCODE(0x12345678, 0x1234, 0x5678, 0x1234, 0x56789abcdef1)

static struct bt_uuid_128 led_service_uuid = BT_UUID_INIT_128(BT_UUID_LED_SERVICE_VAL);
static struct bt_uuid_128 led_toggle_char_uuid = BT_UUID_INIT_128(BT_UUID_LED_TOGGLE_CHARACTERISTIC_VAL);

/* LED state - for BLE notifications */
static uint8_t led_toggle_value;

/* Write callback for the LED toggle characteristic */
static ssize_t write_led_toggle(struct bt_conn *conn,
                               const struct bt_gatt_attr *attr,
                               const void *buf,
                               uint16_t len, uint16_t offset, uint8_t flags)
{
    /* Toggle the LED regardless of the value written */
    led_toggle();
    
    /* Return the number of bytes written */
    return len;
}

/* LED service definition */
BT_GATT_SERVICE_DEFINE(led_svc,
    BT_GATT_PRIMARY_SERVICE(&led_service_uuid),
    BT_GATT_CHARACTERISTIC(&led_toggle_char_uuid.uuid,
                          BT_GATT_CHRC_WRITE | BT_GATT_CHRC_WRITE_WITHOUT_RESP,
                          BT_GATT_PERM_WRITE,
                          NULL, write_led_toggle, &led_toggle_value),
);

/* Public functions */

int led_init(void)
{
    int ret;

    if (!gpio_is_ready_dt(&led)) {
        LOG_ERR("LED GPIO device not ready");
        return -ENODEV;
    }

    ret = gpio_pin_configure_dt(&led, GPIO_OUTPUT_INACTIVE);
    if (ret < 0) {
        LOG_ERR("Error %d: failed to configure LED GPIO", ret);
        return ret;
    }

    led_state = false; /* Initial state is off */
    LOG_INF("LED initialized");
    return 0;
}

void led_toggle(void)
{
    led_state = !led_state;
    gpio_pin_set_dt(&led, led_state);
    LOG_INF("LED %s", led_state ? "ON" : "OFF");
}

void led_service_init(void)
{
    LOG_INF("LED service initialized");
}