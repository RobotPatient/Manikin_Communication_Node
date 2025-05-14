/**
 * @file ble_notifications.h
 * @brief BLE notification message types and constants
 */
#ifndef BLE_NOTIFICATIONS_H
#define BLE_NOTIFICATIONS_H

#include <zephyr/kernel.h>

/* Protocol format constants */
#define BLE_COMMAND_BYTE_START     0x01    /* Start byte that must be included in all messages */
#define BLE_COMMAND_MSG_COLON      0x3A    /* Colon separator before message content (0x3A) */
#define BLE_COMMAND_MSG_SEMICOLON  0x3B    /* Semicolon separator after message content (0x3B) */
#define BLE_COMMAND_MSG_END        0x17    /* End byte that must be included in all messages */

/* Message types for BLE notifications */
#define NOTIFY_TYPE_HEARTBEAT      0x01    /* Heartbeat notification with counter */
#define NOTIFY_TYPE_LED_STATE      0x10    /* LED state notification */
#define NOTIFY_TYPE_TIME_DATA      0x20    /* Time data notification */
#define NOTIFY_TYPE_CPR_TIME       0x30    /* CPR session time progress notification */
#define NOTIFY_TYPE_CPR_STATE      0x40    /* CPR session state change notification */
#define NOTIFY_TYPE_USER_ROLE      0x50    /* User role notification */
#define NOTIFY_TYPE_CPR_CMD_ACK    0x60    /* CPR command acknowledgment */

/* Command types - aligned with protocol spec */
/* These are already defined in message_processor.h, so don't redefine them here */
/* #define CPR_CONTROL_START          0x02 */    /* Start CPR command */
/* #define CPR_COMMAND_STOP           0x03 */    /* Stop CPR command */
#define CMD_COMMAND_DATA           0x04    /* Send ID data command */
#define CMD_COMMAND_TIMEDATA       0x05    /* Send date/time command */

/* CPR command IDs for acknowledgments - internal use */
#define CPR_CMD_START              0x01    /* Start CPR command ack */
#define CPR_CMD_STOP               0x02    /* Stop CPR command ack */

/* Status codes for acknowledgments */
#define STATUS_OK                  0x00    /* Command executed successfully */
#define STATUS_ERROR               0x01    /* Command execution failed */

#endif /* BLE_NOTIFICATIONS_H */