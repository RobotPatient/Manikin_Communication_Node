/**
 * @file sdcard_init.c
 * @brief Delayed SD card initialization
 * 
 * This file implements a safe mechanism to initialize the SD card
 * after the system has fully booted.
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include "sdcard_handler.h"
#include "sdcard_test.h"
#include "sdcard_init.h"

LOG_MODULE_REGISTER(sdcard_init, LOG_LEVEL_INF);

/* Define a dedicated work queue for SD card operations with a larger stack */
#define SDCARD_STACK_SIZE 4096
K_THREAD_STACK_DEFINE(sdcard_stack_area, SDCARD_STACK_SIZE);
static struct k_work_q sdcard_work_q;
static struct k_work_delayable sdcard_init_work;

/**
 * @brief SD card initialization work handler
 * 
 * This function is called by the work queue to initialize the SD card
 * and run a simple test.
 */
static void sdcard_init_work_handler(struct k_work *work)
{
    LOG_INF("Starting delayed SD card initialization");
    run_sdcard_test();
}

/**
 * @brief Initialize the SD card subsystem asynchronously
 * 
 * This function schedules the SD card initialization to happen
 * after a delay to ensure the system is fully booted.
 */
int sdcard_init_async(void)
{
    int ret;

    /* Initialize the work queue for SD card operations - use a low priority (higher number)
     * to avoid interfering with BLE operations which are typically higher priority */
    k_work_queue_start(&sdcard_work_q, sdcard_stack_area,
                      K_THREAD_STACK_SIZEOF(sdcard_stack_area),
                      K_PRIO_PREEMPT(10),
                      NULL);
    k_thread_name_set(&sdcard_work_q.thread, "sdcard_workq");

    /* Initialize the work item for SD card initialization */
    k_work_init_delayable(&sdcard_init_work, sdcard_init_work_handler);

    /* Schedule the work item with a long delay to ensure system is fully stable
     * 15 seconds is chosen to ensure that Bluetooth is fully initialized and 
     * any initial connections are established first */
    ret = k_work_schedule_for_queue(&sdcard_work_q, &sdcard_init_work, 
                                   K_SECONDS(15));
    if (ret < 0) {
        LOG_ERR("Failed to schedule SD card initialization: %d", ret);
        return ret;
    }

    LOG_INF("SD card initialization scheduled for 15 seconds from now");
    return 0;
}