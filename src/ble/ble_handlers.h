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

/* Include our message processor for command definitions */
#include "../message_processor/message_processor.h"

/* Backwards compatibility for existing code */
#define BLE_COMMAND_BYTE_START       MSG_COMMAND_BYTE_START
#define BLE_COMMAND_MSG_COLON        MSG_COMMAND_MSG_COLON
#define BLE_COMMAND_MSG_SEMICOLON    MSG_COMMAND_MSG_SEMICOLON
#define BLE_COMMAND_MSG_END          MSG_COMMAND_MSG_END

#define CPR_CONTROL_LED_OFF          CMD_CONTROL_LED_OFF
#define CPR_CONTROL_LED_ON           CMD_CONTROL_LED_ON
#define CPR_CONTROL_START            CMD_CONTROL_START
#define CPR_COMMAND_STOP             CMD_COMMAND_STOP
#define CPR_COMMAND_DATA             CMD_COMMAND_DATA
#define CPR_COMMAND_TIMEDATA         CMD_COMMAND_TIMEDATA

#ifdef __cplusplus
extern "C" {
#endif

/* Buffer for BLE commands */
extern uint8_t ble_cmd_buffer[BLE_BUFFER_SIZE];

/* LED control flags - for safe LED control from BLE thread */
extern bool led_request_pending;
extern bool led_requested_state;

/* Function declarations */
void bt_ready(int err);
void button_callback(const struct device *gpiob, struct gpio_callback *cb, uint32_t pins);
void can_buffer_add(const void *frame);
void send_can_message(uint32_t can_id, uint8_t *data, uint8_t len);
void process_ios_command(uint8_t *cmd_data, uint16_t len);
void restart_advertising(struct k_work *work);
void process_ble_command(uint8_t *cmd_data, uint16_t len);
/* These functions are now provided by the message processor module */
// int detect_and_print_user_role(const uint8_t *cmd_data, size_t data_len, size_t len);


#ifdef __cplusplus
}
#endif

#endif /* BLE_HANDLERS_H */