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

/* Command types */
#define CMD_CONTROL_LED_OFF          0x00
#define CMD_CONTROL_LED_ON           0x01
#define CMD_CONTROL_START            0x02
#define CMD_COMMAND_STOP             0x03
#define CMD_COMMAND_DATA             0x04
#define CMD_COMMAND_TIMEDATA         0x05

/* User role identifiers */
#define USER_ROLE_INSTRUCTOR_PREFIX  "in:"
#define USER_ROLE_TRAINEE_PREFIX     "tr:"
#define USER_ROLE_INSTRUCTOR         1
#define USER_ROLE_TRAINEE            2
#define USER_ROLE_NONE               0

/* CPR session commands */
#define CMD_CPR_START               0x50
#define CMD_CPR_STOP                0x51

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