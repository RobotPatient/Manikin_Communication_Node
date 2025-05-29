#include <zephyr/kernel.h>        // k_timer, k_uptime_get_32
#include <zephyr/logging/log.h>   // LOG_INF, LOG_ERR, LOG_DBG
#include <zephyr/types.h>         // uint8_t, uint32_t
#include <string.h>               // memcpy, strlen
#include <stdio.h>                // snprintf

#include "led_svc.h"          
#include "ble_notifications.h"   
#include "session.h"        
#include "message_processor.h"            

LOG_MODULE_REGISTER(led_handler, LOG_LEVEL_INF);

extern bool led_request_pending;
extern bool notify_enabled;
extern bool led_requested_state;
extern bool cpr_notifications_allowed;
extern bool is_connected;
extern bool cpr_session_active;
extern uint32_t connection_time;          /* Time when connection was established */
extern uint32_t connection_ready_delay; /* Delay in ms before sending notifications */
extern uint32_t cpr_session_start_time;
extern struct bt_conn *current_conn;

extern int send_notification_safely(const void *data, uint16_t len);
extern int send_ble_notification(uint8_t msg_type, const void *payload, uint16_t payload_len);
extern uint32_t get_cpr_session_time(void);

extern struct
{
    bool heartbeat_works;
    bool role_works;
    bool time_works;
    bool led_works;
    bool cpr_works;
} notification_support;

/* Timer to check for LED requests from message processor */
static struct k_timer led_timer;

// --- Utility functions ---
static inline bool is_connection_ready(void) {
    return is_connected && current_conn &&
           (k_uptime_get_32() - connection_time) >= connection_ready_delay;
}

static void send_led_notification_if_needed(void) {
    if (!led_request_pending) return;
    led_request_pending = false;

    led_requested_state ? led_on() : led_off();
    LOG_INF("LED %s", led_requested_state ? "ON" : "OFF");

    if (!notify_enabled) return;

    uint8_t state = led_requested_state ? 0x01 : 0x00;
    int err = send_ble_notification(NOTIFY_TYPE_LED_STATE, &state, sizeof(state));
    if (err == 0)
        LOG_INF("LED state notification sent: %d", led_requested_state);
    else if (err != -ENOTCONN && err != -ENOTSUP)
        LOG_ERR("LED state notification failed (err %d)", err);
}

static void maybe_notify_cpr_time(void) {
    if (!is_cpr_session_active()) return;

    uint32_t elapsed = get_cpr_session_time();
    if (elapsed == 0 || (elapsed % 5 != 0)) return;

    if (!is_connection_ready() || (!notify_enabled && !cpr_notifications_allowed)) return;

    uint8_t payload[32];
    char time_str[16];
    snprintf(time_str, sizeof(time_str), "cpr:%02u:%02u", elapsed / 60, elapsed % 60);

    payload[0] = (elapsed >> 24) & 0xFF;
    payload[1] = (elapsed >> 16) & 0xFF;
    payload[2] = (elapsed >> 8) & 0xFF;
    payload[3] = elapsed & 0xFF;
    memcpy(&payload[4], time_str, strlen(time_str));

    int err = send_ble_notification(NOTIFY_TYPE_CPR_TIME, payload, 4 + strlen(time_str));
    if (err == 0)
        LOG_DBG("CPR time notified: %s (%u s)", time_str, elapsed);
    else if (err != -ENOTCONN && err != -ENOTSUP)
        LOG_ERR("CPR time notification failed (err %d)", err);
}

static void handle_cpr_state_notification(void) {
    static bool last_state = false;
    static bool start_ack_sent = false, stop_ack_sent = false;
    static uint32_t session_stop_time = 0;

    bool current_state = cpr_session_active;
    if (current_state == last_state) return;

    LOG_INF("CPR state change: %d -> %d", last_state, current_state);

    if (!is_connection_ready() || (!notify_enabled && !cpr_notifications_allowed)) {
        last_state = current_state;
        return;
    }

    uint8_t state = current_state ? 0x01 : 0x00;
    int err = send_ble_notification(NOTIFY_TYPE_CPR_STATE, &state, sizeof(state));
    if (err == 0) {
        LOG_INF("CPR state %s notification sent", current_state ? "ACTIVE" : "INACTIVE");
        last_state = current_state;
        if (current_state) {
            start_ack_sent = false;
            stop_ack_sent = true;
        } else {
            session_stop_time = k_uptime_get_32();
            stop_ack_sent = false;
        }
    } else if (err != -ENOTCONN && err != -ENOTSUP) {
        LOG_ERR("CPR state notification failed (err %d)", err);
    }
}

static void handle_cpr_acknowledgments(void) {
    static bool start_ack_sent = false, stop_ack_sent = false;
    static uint32_t session_stop_time = 0;

    if (!is_connection_ready() || (!notify_enabled && !cpr_notifications_allowed)) return;

    uint8_t payload[32];
    char time_str[16];

    if (cpr_session_active && !start_ack_sent) {
        uint32_t elapsed = get_cpr_session_time();
        snprintf(time_str, sizeof(time_str), "cpr:%02u:%02u", elapsed / 60, elapsed % 60);
        payload[0] = CPR_CMD_START;
        payload[1] = STATUS_OK;
        memcpy(&payload[2], time_str, strlen(time_str));

        int err = send_ble_notification(NOTIFY_TYPE_CPR_CMD_ACK, payload, 2 + strlen(time_str));
        if (err == 0) {
            LOG_INF("CPR START ack sent: %s", time_str);
            start_ack_sent = true;
        } else if (err != -ENOTCONN && err != -ENOTSUP) {
            LOG_ERR("CPR START ack failed (err %d)", err);
        }
    }

    if (!cpr_session_active && !stop_ack_sent) {
        uint32_t elapsed_sec = 0;
        if (session_stop_time > cpr_session_start_time)
            elapsed_sec = (session_stop_time - cpr_session_start_time) / 1000;

        snprintf(time_str, sizeof(time_str), "cpr:%02u:%02u", elapsed_sec / 60, elapsed_sec % 60);

        payload[0] = CPR_CMD_STOP;
        payload[1] = STATUS_OK;
        payload[2] = (elapsed_sec >> 8) & 0xFF;
        payload[3] = elapsed_sec & 0xFF;
        memcpy(&payload[4], time_str, strlen(time_str));

        int err = send_ble_notification(NOTIFY_TYPE_CPR_CMD_ACK, payload, 4 + strlen(time_str));
        if (err == 0) {
            LOG_INF("CPR STOP ack sent: %s (%u s)", time_str, elapsed_sec);
            stop_ack_sent = true;
        } else if (err != -ENOTCONN && err != -ENOTSUP) {
            LOG_ERR("CPR STOP ack failed (err %d)", err);
        }
    }
}

static void maybe_notify_user_role_and_time(void) {
    static uint32_t last_check = 0;
    uint32_t now = k_uptime_get_32();
    if (now - last_check < 5000) return;
    last_check = now;

    // Role notification logic...
    // Time data notification logic...
    // (Omitted here for brevity, but would be similarly modularized)
}

// --- Timer handler ---
static void led_timer_handler(struct k_timer *timer) {
    send_led_notification_if_needed();
    maybe_notify_cpr_time();
    handle_cpr_state_notification();
    handle_cpr_acknowledgments();
    maybe_notify_user_role_and_time();
}

// --- Initialization ---
void led_handler_init(void) {
    k_timer_init(&led_timer, led_timer_handler, NULL);
    k_timer_start(&led_timer, K_MSEC(100), K_MSEC(100));
}