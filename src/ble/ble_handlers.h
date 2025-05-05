/** @file
 * @brief Button Service
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
#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include "button_svc.h"
#include "led_svc.h"

/* BLE command buffer size */
#define BLE_BUFFER_SIZE 40

/* BLE command types */

#define BLE_COMMAND_BYTE_START       0x01
#define BLE_COMMAND_MSG_COLON        0x3A
#define BLE_COMMAND_MSG_SEMICOLON    0x3B
#define BLE_COMMAND_MSG_END          0x17

#define CPR_CONTROL_LED_OFF          0x00
#define CPR_CONTROL_LED_ON           0x01
#define CPR_CONTROL_START            0x02
#define CPR_COMMAND_STOP             0x03

#ifdef __cplusplus
extern "C" {
#endif

/* Buffer for BLE commands */
extern uint8_t ble_cmd_buffer[BLE_BUFFER_SIZE];

/* Function declarations */
void bt_ready(int err);
void button_callback(const struct device *gpiob, struct gpio_callback *cb, uint32_t pins);
void can_buffer_add(const void *frame);
void send_can_message(uint32_t can_id, uint8_t *data, uint8_t len);
void process_ios_command(uint8_t *cmd_data, uint16_t len);
void restart_advertising(struct k_work *work);
void process_ble_command(uint8_t *cmd_data, uint16_t len);

#ifdef __cplusplus
}
#endif

#endif /* BLE_HANDLERS_H */