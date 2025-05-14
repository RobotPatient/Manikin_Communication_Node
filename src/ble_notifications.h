/**
 * @file ble_notifications.h
 * @brief BLE notification message types and constants
 */
#ifndef BLE_NOTIFICATIONS_H
#define BLE_NOTIFICATIONS_H

#include <zephyr/kernel.h>

/* Command structure constants */
#define BLE_COMMAND_BYTE_START     0x01    /* Start byte that must be included in all outgoing commands */

/* Message types for BLE notifications */
#define NOTIFY_TYPE_HEARTBEAT      0x01    /* Heartbeat notification with counter */
#define NOTIFY_TYPE_LED_STATE      0x10    /* LED state notification */
#define NOTIFY_TYPE_TIME_DATA      0x20    /* Time data notification */
#define NOTIFY_TYPE_CPR_TIME       0x30    /* CPR session time progress notification */
#define NOTIFY_TYPE_CPR_STATE      0x40    /* CPR session state change notification */
#define NOTIFY_TYPE_USER_ROLE      0x50    /* User role notification */
#define NOTIFY_TYPE_CPR_CMD_ACK    0x60    /* CPR command acknowledgment */

/* CPR command IDs for acknowledgments */
#define CPR_CMD_START              0x01    /* Start CPR command */
#define CPR_CMD_STOP               0x02    /* Stop CPR command */

/* Status codes for acknowledgments */
#define STATUS_OK                  0x00    /* Command executed successfully */
#define STATUS_ERROR               0x01    /* Command execution failed */

#endif /* BLE_NOTIFICATIONS_H */