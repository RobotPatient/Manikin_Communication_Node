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
int main(void)
{ /* raw disk i/o */
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
	printk("ctx pointer   = %p\n", &ctx);
	printk("ctx.schedule  = %p\n", ctx.schedule);
	printk("ctx.schedule->messages = %p\n", ctx.schedule->messages);
	printk("ctx.schedule->num_of_messages = %d\n", ctx.schedule->num_of_messages);
	init_can();
	ttcan_scheduler_init(&ctx);
	ttcan_scheduler_start(&ctx);
	// Start timer to expire after 1000ms, repeat every 1000ms
	k_timer_start(&my_timer, K_MSEC(1), K_MSEC(1));

	/* Initialize the Bluetooth Subsystem */
	err = bt_enable(bt_ready);
	if (err)
	{
		LOG_ERR("Bluetooth init failed (err %d)", err);
	}
	// while(1) {
	// 	send_can();
	// 	k_msleep(1000);
	// }
	return 0;
}
