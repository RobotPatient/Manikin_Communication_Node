/*
 * Basic implementation with core system functionality
 */
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(basic_impl, LOG_LEVEL_INF);

/* Simple timer callback for heartbeat */
static void heartbeat_timer_callback(struct k_timer *timer)
{
    static uint32_t last_log_time = 0;
    uint32_t now = k_uptime_get_32();
    
    /* Only log once every 10 seconds to reduce spam */
    if (now - last_log_time >= 10000) {
        LOG_INF("Basic implementation heartbeat timer triggered");
        last_log_time = now;
    }
}

/* Define a timer for heartbeat function */
K_TIMER_DEFINE(basic_heartbeat_timer, heartbeat_timer_callback, NULL);

/* Initialize the basic system functionality */
void basic_implementation_init(void)
{
    LOG_INF("Basic implementation initialized");
    
    /* Start a simple heartbeat timer */
    k_timer_start(&basic_heartbeat_timer, K_MSEC(1000), K_MSEC(1000));
}