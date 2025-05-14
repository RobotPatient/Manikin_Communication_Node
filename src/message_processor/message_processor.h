/**
 * @file message_processor.h
 * @brief Message processing module for BLE commands
 * 
 * This module handles the processing of received BLE commands,
 * separating the message processing logic from the BLE handling.
 */

#ifndef MESSAGE_PROCESSOR_H
#define MESSAGE_PROCESSOR_H

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

/* Message protocol constants */
#define MSG_COMMAND_BYTE_START       0x01
#define MSG_COMMAND_MSG_COLON        0x3A
#define MSG_COMMAND_MSG_SEMICOLON    0x3B
#define MSG_COMMAND_MSG_END          0x17

/* Command types - aligned with protocol spec */
#define CMD_CONTROL_LED_OFF          0x00    /* LED off - internal use only */
#define CMD_CONTROL_LED_ON           0x01    /* LED on - internal use only */
#define CMD_CONTROL_START            0x02    /* Start CPR command */
#define CMD_COMMAND_STOP             0x03    /* Stop CPR command */
#define CMD_COMMAND_DATA             0x04    /* Send ID data command */
#define CMD_COMMAND_TIMEDATA         0x05    /* Send date/time command */

/* Protocol command constants for compatibility with ble_notifications.h */
#define CPR_CONTROL_START            CMD_CONTROL_START   /* Start CPR command */
#define CPR_COMMAND_STOP             CMD_COMMAND_STOP    /* Stop CPR command */

/* User role identifiers */
#define USER_ROLE_INSTRUCTOR_PREFIX  "in:"
#define USER_ROLE_TRAINEE_PREFIX     "tr:"
#define USER_ROLE_INSTRUCTOR         1
#define USER_ROLE_TRAINEE            2
#define USER_ROLE_NONE               0

/* CPR session commands */
#define CMD_CPR_START               0x50
#define CMD_CPR_STOP                0x51

/* BLE notification message types */
#define NOTIFY_TYPE_LED_STATE       0x10    /* LED state notification */
#define NOTIFY_TYPE_TIME_DATA       0x20    /* Time data notification */
#define NOTIFY_TYPE_CPR_TIME        0x30    /* CPR session time progress notification */
#define NOTIFY_TYPE_CPR_STATE       0x40    /* CPR session state change notification */
#define NOTIFY_TYPE_CPR_CMD_ACK     0x60    /* CPR command acknowledgment */

/* CPR command IDs for acknowledgments */
#define CPR_CMD_START               0x01    /* Start CPR command */
#define CPR_CMD_STOP                0x02    /* Stop CPR command */

/* Status codes for acknowledgments */
#define STATUS_OK                   0x00    /* Command executed successfully */
#define STATUS_ERROR                0x01    /* Command execution failed */

/* Forward declarations for external CPR functions */
extern bool is_cpr_session_active(void);
extern void start_cpr_session(void);
extern void stop_cpr_session(void);


/* Maximum buffer size for processing */
#define MSG_BUFFER_SIZE             40

/* Message queue configuration */
#define MSG_QUEUE_SIZE              10  /* Number of messages in the queue */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the message processor
 * 
 * Sets up any internal state for the message processor
 * 
 * @return 0 on success, negative error code on failure
 */
int message_processor_init(void);

/**
 * @brief Submit a command for processing
 * 
 * Queues a command for asynchronous processing by the message processor thread.
 * This function is safe to call from any context, including interrupt handlers
 * and limited-stack threads like the BT RX worker.
 * 
 * @param cmd_data Pointer to command data buffer
 * @param len Length of data in the buffer
 * @return 0 on success, negative error code on failure
 */
int submit_command(const uint8_t *cmd_data, uint16_t len);

/**
 * @brief Submit a direct command for processing
 * 
 * Queues a simple command byte for asynchronous processing.
 * This is a lightweight wrapper that packages the single byte into a command.
 * 
 * @param cmd_byte The command byte to process
 * @return 0 on success, negative error code on failure
 */
int submit_direct_command(uint8_t cmd_byte);

/**
 * @brief Get the current instructor ID
 * 
 * @param buffer Buffer to fill with the instructor ID
 * @param size Size of the buffer
 * @return Length of the ID string, 0 if no ID is set
 */
size_t get_instructor_id(char *buffer, size_t size);

/**
 * @brief Get the current trainee ID
 * 
 * @param buffer Buffer to fill with the trainee ID
 * @param size Size of the buffer
 * @return Length of the ID string, 0 if no ID is set
 */
size_t get_trainee_id(char *buffer, size_t size);

/**
 * @brief Get the current user role
 * 
 * @return USER_ROLE_INSTRUCTOR, USER_ROLE_TRAINEE, or USER_ROLE_NONE
 */
uint8_t get_user_role(void);

/**
 * @brief Get the current time data
 * 
 * @param buffer Buffer to fill with the time data
 * @param size Size of the buffer
 * @return Length of the time data string, 0 if no time data is set
 */
size_t get_time_data(char *buffer, size_t size);

/**
 * @brief Check if time data has been received
 * 
 * @return True if time data has been received, false otherwise
 */
bool has_received_time_data(void);

/**
 * @brief Get the current time from the RTC
 * 
 * @param buffer Buffer to fill with the current time in format "YYYY-MM-DD HH:MM:SS"
 * @param size Size of the buffer (should be at least 20 bytes)
 * @return Length of the time string written to the buffer, 0 on error
 */
size_t get_rtc_time(char *buffer, size_t size);

#ifdef __cplusplus
}
#endif

#endif /* MESSAGE_PROCESSOR_H */