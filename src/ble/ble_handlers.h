/** @file
 *  @brief Button Service
 */

/*
 * Copyright (c) 2019 Marcio Montenegro <mtuxpe@gmail.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

 #ifndef BLE_HANDLERS_H
 #define BLE_HANDLERS_H
 
 #include <zephyr/drivers/gpio.h>
 #include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/logging/log.h>
#include "button_svc.h"
#include "led_svc.h"
 #ifdef __cplusplus
 extern "C" {
 #endif
 
 void button_callback(const struct device *gpiob, struct gpio_callback *cb,
    uint32_t pins);

void bt_ready(int err);
 
 #ifdef __cplusplus
 }
 #endif
 
 #endif
 