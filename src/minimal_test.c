/*
 * Minimal test program for debugging boot issues
 */
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(minimal_test, LOG_LEVEL_INF);

/* Simple timer callback for heartbeat */
static void heartbeat_timer_callback(struct k_timer *timer)
{
    LOG_INF("Heartbeat timer triggered");
}

/* Define a timer for heartbeat function */
K_TIMER_DEFINE(heartbeat_timer, heartbeat_timer_callback, NULL);

/* Minimal entry point to verify basic Zephyr functionality */
void minimal_test_init(void)
{
    LOG_INF("Minimal test program initialized");
    
    /* Start a simple heartbeat timer */
    k_timer_start(&heartbeat_timer, K_MSEC(1000), K_MSEC(1000));
}