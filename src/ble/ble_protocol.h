/**
 * @file ble_protocol.h
 * @brief Protocol utilities for BLE command formatting
 */
#ifndef BLE_PROTOCOL_H
#define BLE_PROTOCOL_H

#include <zephyr/kernel.h>
#include <stdint.h>
#include <string.h>
#include "../ble_notifications.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Test function to demonstrate BLE protocol formatting
 * 
 * Creates various test messages formatted according to the protocol
 * and logs them for verification.
 */
void test_ble_protocol(void);

/**
 * @brief Format a BLE command according to the protocol specification
 * 
 * Formats a command using the protocol:
 * START_BYTE + LENGTH_BYTE + COLON + COMMAND + PAYLOAD + SEMICOLON + END_BYTE
 * 
 * @param buffer Buffer to store the formatted command
 * @param buf_size Size of the buffer
 * @param cmd Command byte
 * @param payload Optional payload data (can be NULL)
 * @param payload_len Length of the payload data (0 if no payload)
 * @return Total length of the formatted command, or negative error code
 */
static inline int format_ble_command(uint8_t *buffer, size_t buf_size, 
                                    uint8_t cmd, const void *payload, uint16_t payload_len)
{
    if (!buffer || buf_size < 6) {
        /* Minimum size: START + LEN + COLON + CMD + SEMICOLON + END */
        return -EINVAL;
    }
    
    /* Calculate total required size */
    size_t total_len = 6 + payload_len; /* START + LEN + COLON + CMD + SEMICOLON + END + payload */
    
    if (buf_size < total_len) {
        return -ENOMEM;
    }
    
    /* Format the command */
    size_t i = 0;
    buffer[i++] = BLE_COMMAND_BYTE_START;   /* START_BYTE */
    buffer[i++] = payload_len + 1;          /* LENGTH_BYTE (payload + command byte) */
    buffer[i++] = BLE_COMMAND_MSG_COLON;    /* COLON */
    buffer[i++] = cmd;                      /* Command byte */
    
    /* Add payload if provided */
    if (payload && payload_len > 0) {
        memcpy(&buffer[i], payload, payload_len);
        i += payload_len;
    }
    
    /* Add terminating bytes */
    buffer[i++] = BLE_COMMAND_MSG_SEMICOLON; /* SEMICOLON */
    buffer[i++] = BLE_COMMAND_MSG_END;       /* END_BYTE */
    
    return i; /* Return total length */
}

/**
 * @brief Format a CPR start command 
 * 
 * @param buffer Buffer to store the formatted command
 * @param buf_size Size of the buffer
 * @return Total length of the formatted command, or negative error code
 */
static inline int format_cpr_start_command(uint8_t *buffer, size_t buf_size)
{
    return format_ble_command(buffer, buf_size, CPR_CMD_START, NULL, 0);
}

/**
 * @brief Format a CPR stop command
 * 
 * @param buffer Buffer to store the formatted command
 * @param buf_size Size of the buffer
 * @return Total length of the formatted command, or negative error code
 */
static inline int format_cpr_stop_command(uint8_t *buffer, size_t buf_size)
{
    return format_ble_command(buffer, buf_size, CPR_CMD_STOP, NULL, 0);
}

/**
 * @brief Format a command with data payload (e.g., instructor or trainee ID)
 * 
 * @param buffer Buffer to store the formatted command
 * @param buf_size Size of the buffer
 * @param payload Payload data
 * @param payload_len Length of the payload data
 * @return Total length of the formatted command, or negative error code
 */
static inline int format_data_command(uint8_t *buffer, size_t buf_size, 
                                     const void *payload, uint16_t payload_len)
{
    return format_ble_command(buffer, buf_size, CMD_COMMAND_DATA, payload, payload_len);
}

/**
 * @brief Format a time data command
 * 
 * @param buffer Buffer to store the formatted command
 * @param buf_size Size of the buffer
 * @param time_str Time string (format: YYYYMMDDHHMMSS)
 * @param time_len Length of the time string
 * @return Total length of the formatted command, or negative error code
 */
static inline int format_timedata_command(uint8_t *buffer, size_t buf_size, 
                                        const char *time_str, uint16_t time_len)
{
    return format_ble_command(buffer, buf_size, CMD_COMMAND_TIMEDATA, time_str, time_len);
}

#ifdef __cplusplus
}
#endif

#endif /* BLE_PROTOCOL_H */