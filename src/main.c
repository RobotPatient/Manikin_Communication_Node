/* main.c - Application main entry point */
/*
 * Copyright (c) 2015-2016 Intel Corporation
 * Copyright (c) 2019 Marcio Montenegro <mtuxpe@gmail.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <zephyr/types.h>
#include <stddef.h>
#include <string.h>
#include <errno.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>
#include <stm32h7xx.h> // Include STM32 headers manually if needed
#include <stm32h7xx_ll_gpio.h>
#include <stm32h7xx_hal_gpio.h>
#include <zephyr/fs/fs.h>
#include <zephyr/fs/fs_interface.h>
#include <zephyr/storage/disk_access.h>
#include "ttcan_scheduler.h"
#include "ble_handlers.h"
#include "can_wrapper.h"

LOG_MODULE_REGISTER(main);

K_TIMER_DEFINE(my_timer, ttcan_timer_trigger, ttcan_timer_stop);

#define DISK_DRIVE_NAME "SD"

ttcan_timer_type_t fake_handle;

/* Thread function to handle LED control independently of BLE thread */
void led_control_thread_fn(void *arg1, void *arg2, void *arg3)
{
    ARG_UNUSED(arg1);
    ARG_UNUSED(arg2);
    ARG_UNUSED(arg3);

    while (1) {
        /* Check if there's a pending LED control request */
        if (led_request_pending) {
            led_request_pending = false;
            LOG_INF("Processing LED request: %s", led_requested_state ? "ON" : "OFF");
            
            /* Handle the LED request from this thread where it's safe */
            if (led_requested_state) {
                led_on();
            } else {
                led_off();
            }
        }
        
        /* Sleep for a short time to avoid hogging CPU */
        k_sleep(K_MSEC(10));
    }
}

/* Define a dedicated thread for LED control with sufficient stack */
#define LED_CONTROL_STACK_SIZE 1024
#define LED_CONTROL_PRIORITY 7
K_THREAD_DEFINE(led_control_tid, LED_CONTROL_STACK_SIZE,
                led_control_thread_fn, NULL, NULL, NULL,
                LED_CONTROL_PRIORITY, 0, 0);

int main(void)
{
    /* raw disk i/o */
    int err;

    err = button_init(button_callback);
    if (err)
    {
        return 0;
    }

    err = led_init();
    if (err)
    {
        return 0;
    }

    ctx.timer = fake_handle;
    ctx.master_mode_en = 1;
    ctx.schedule = &ttcan_schedule;
    printk("ctx pointer = %p\n", &ctx);
    printk("ctx.schedule = %p\n", ctx.schedule);
    printk("ctx.schedule->messages = %p\n", ctx.schedule->messages);
    printk("ctx.schedule->num_of_messages = %d\n", ctx.schedule->num_of_messages);

    /* Note: CAN initialization will be done as part of BLE initialization */
    /* in the bt_ready callback to ensure proper ordering */

    /* Initialize the Bluetooth Subsystem */
    err = bt_enable(bt_ready);
    if (err)
    {
        LOG_ERR("Bluetooth init failed (err %d)", err);
    }
    
    /* Never return - let the threads run */
    while (1) {
        k_sleep(K_SECONDS(1));
    }
}