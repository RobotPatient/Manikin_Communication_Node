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

volatile size_t cnt = 0;
LOG_MODULE_REGISTER(main);

int main(void)
{     

	int err;
	while(1) {
		cnt++;
		LOG_ERR("Hello World!\n");
		k_msleep(1000);
	}
	return 0;
}
