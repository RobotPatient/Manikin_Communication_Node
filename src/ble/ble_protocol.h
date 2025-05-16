/**
 * @file ble_protocol.h
 * @brief Protocol utilities for BLE command formatting
 */
#ifndef BLE_PROTOCOL_H
#define BLE_PROTOCOL_H

#include <zephyr/kernel.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include "../ble_notifications.h"
#include "crc/crc16_koopman.h"

/* Define our own error constants in case errno.h doesn't provide them */
#ifndef EINVAL
#define EINVAL 22  /* Invalid argument */
#endif

#ifndef ENOMEM
#define ENOMEM 12   /* Out of memory */
#endif

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
 * @brief Format a BLE command according to the protocol specification with CRC
 * 
 * Formats a command using the protocol:
 * START_BYTE + LENGTH_BYTE + COLON + COMMAND + PAYLOAD + CRC (2 bytes) + SEMICOLON + END_BYTE
 * 
 * @param buffer Buffer to store the formatted command
 * @param buf_size Size of the buffer
 * @param cmd Command byte
 * @param payload Optional payload data (can be NULL)
 * @param payload_len Length of the payload data (0 if no payload)
 * @param add_crc Set to true to add a 16-bit CRC before the SEMICOLON
 * @return Total length of the formatted command, or negative error code
 */
static inline int format_ble_command(uint8_t *buffer, size_t buf_size, 
                                    uint8_t cmd, const void *payload, uint16_t payload_len,
                                    bool add_crc)
{
    /* Determine minimum required size based on whether CRC is included */
    size_t min_size = add_crc ? 8 : 6; /* With CRC: START + LEN + COLON + CMD + CRC(2) + SEMICOLON + END */
    
    if (!buffer || buf_size < min_size) {
        /* Buffer too small */
        return -EINVAL;
    }
    
    /* Calculate total required size */
    size_t crc_len = add_crc ? 2 : 0;
    size_t total_len = 6 + payload_len + crc_len; /* START + LEN + COLON + CMD + SEMICOLON + END + payload + crc */
    
    if (buf_size < total_len) {
        return -ENOMEM;
    }
    
    /* Format the command */
    size_t i = 0;
    buffer[i++] = BLE_COMMAND_BYTE_START;   /* START_BYTE */
    
    /* LENGTH_BYTE includes command byte, payload, and CRC if present */
    buffer[i++] = payload_len + 1 + crc_len;
    
    buffer[i++] = BLE_COMMAND_MSG_COLON;    /* COLON */
    buffer[i++] = cmd;                      /* Command byte */
    
    /* Add payload if provided */
    if (payload && payload_len > 0) {
        memcpy(&buffer[i], payload, payload_len);
        i += payload_len;
    }
    
    /* Calculate and add CRC if requested */
    if (add_crc) {
        /* CRC calculation starts from START_BYTE and includes everything up to this point */
        uint16_t crc = crc16_koopman(buffer, i);
        
        /* Add the CRC bytes (big endian) */
        buffer[i++] = (uint8_t)(crc >> 8);    /* MSB of CRC */
        buffer[i++] = (uint8_t)(crc & 0xFF);  /* LSB of CRC */
    }
    
    /* Add terminating bytes */
    buffer[i++] = BLE_COMMAND_MSG_SEMICOLON; /* SEMICOLON */
    buffer[i++] = BLE_COMMAND_MSG_END;       /* END_BYTE */
    
    return i; /* Return total length */
}

/**
 * @brief Format a BLE command without CRC (legacy format)
 * 
 * For backwards compatibility with existing code.
 */
static inline int format_ble_command_no_crc(uint8_t *buffer, size_t buf_size, 
                                          uint8_t cmd, const void *payload, uint16_t payload_len)
{
    return format_ble_command(buffer, buf_size, cmd, payload, payload_len, false);
}

/**
 * @brief Format a CPR start command 
 * 
 * @param buffer Buffer to store the formatted command
 * @param buf_size Size of the buffer
 * @param add_crc Set to true to add CRC-16 to the command
 * @return Total length of the formatted command, or negative error code
 */
static inline int format_cpr_start_command(uint8_t *buffer, size_t buf_size, bool add_crc)
{
    return format_ble_command(buffer, buf_size, CPR_CMD_START, NULL, 0, add_crc);
}

/**
 * @brief Format a CPR stop command
 * 
 * @param buffer Buffer to store the formatted command
 * @param buf_size Size of the buffer
 * @param add_crc Set to true to add CRC-16 to the command
 * @return Total length of the formatted command, or negative error code
 */
static inline int format_cpr_stop_command(uint8_t *buffer, size_t buf_size, bool add_crc)
{
    return format_ble_command(buffer, buf_size, CPR_CMD_STOP, NULL, 0, add_crc);
}

/**
 * @brief Format a command with data payload (e.g., instructor or trainee ID)
 * 
 * @param buffer Buffer to store the formatted command
 * @param buf_size Size of the buffer
 * @param payload Payload data
 * @param payload_len Length of the payload data
 * @param add_crc Set to true to add CRC-16 to the command
 * @return Total length of the formatted command, or negative error code
 */
static inline int format_data_command(uint8_t *buffer, size_t buf_size, 
                                     const void *payload, uint16_t payload_len,
                                     bool add_crc)
{
    return format_ble_command(buffer, buf_size, CMD_COMMAND_DATA, payload, payload_len, add_crc);
}

/**
 * @brief Format a time data command
 * 
 * @param buffer Buffer to store the formatted command
 * @param buf_size Size of the buffer
 * @param time_str Time string (format: YYYYMMDDHHMMSS)
 * @param time_len Length of the time string
 * @param add_crc Set to true to add CRC-16 to the command
 * @return Total length of the formatted command, or negative error code
 */
static inline int format_timedata_command(uint8_t *buffer, size_t buf_size, 
                                        const char *time_str, uint16_t time_len,
                                        bool add_crc)
{
    return format_ble_command(buffer, buf_size, CMD_COMMAND_TIMEDATA, time_str, time_len, add_crc);
}

/* Legacy function versions without CRC for backward compatibility */

static inline int format_cpr_start_command_no_crc(uint8_t *buffer, size_t buf_size)
{
    return format_cpr_start_command(buffer, buf_size, false);
}

static inline int format_cpr_stop_command_no_crc(uint8_t *buffer, size_t buf_size)
{
    return format_cpr_stop_command(buffer, buf_size, false);
}

static inline int format_data_command_no_crc(uint8_t *buffer, size_t buf_size, 
                                          const void *payload, uint16_t payload_len)
{
    return format_data_command(buffer, buf_size, payload, payload_len, false);
}

static inline int format_timedata_command_no_crc(uint8_t *buffer, size_t buf_size, 
                                               const char *time_str, uint16_t time_len)
{
    return format_timedata_command(buffer, buf_size, time_str, time_len, false);
}

/**
 * @brief Verify the CRC of a received BLE message
 * 
 * Verifies the CRC-16 checksum in a received BLE message. The CRC is expected to be
 * the two bytes right before the SEMICOLON marker.
 * 
 * @param buffer The complete message buffer
 * @param length Total length of the message
 * @return true if CRC is valid or if the message doesn't contain a CRC, false otherwise
 */
static inline bool verify_ble_message_crc(const uint8_t *buffer, size_t length)
{
    /* Minimum valid message with CRC: START + LEN + COLON + CMD + CRC(2) + SEMICOLON + END */
    if (!buffer || length < 8) {
        return false;
    }
    
    /* Check for valid start and end markers */
    if (buffer[0] != BLE_COMMAND_BYTE_START || 
        buffer[length-1] != BLE_COMMAND_MSG_END || 
        buffer[length-2] != BLE_COMMAND_MSG_SEMICOLON) {
        return false;
    }
    
    /* Extract the message length from the LENGTH_BYTE */
    uint8_t msg_len = buffer[1];
    
    /* Check if the message includes CRC (at least 3 bytes: CMD + CRC(2)) */
    if (msg_len < 3) {
        /* No CRC present, consider it valid */
        return true;
    }
    
    /* Verify message format is valid */
    if (length < 4 || buffer[2] != BLE_COMMAND_MSG_COLON) {
        return false;
    }
    
    /* Make sure the length is as expected */
    if (length != (size_t)(msg_len + 5)) { /* START + LEN + COLON + MSG_LEN + SEMICOLON + END */
        return false;
    }
    
    /* Calculate CRC on the message excluding the CRC bytes, SEMICOLON, and END_BYTE */
    size_t crc_data_len = length - 4; /* Exclude CRC(2) + SEMICOLON + END */
    
    /* Sanity check the calculated length */
    if (crc_data_len < 4 || crc_data_len >= length) {
        return false;
    }
    
    uint16_t calculated_crc = crc16_koopman(buffer, crc_data_len);
    
    /* Extract received CRC (big endian) */
    uint16_t received_crc = ((uint16_t)buffer[crc_data_len] << 8) | buffer[crc_data_len + 1];
    
    /* Compare calculated CRC with received CRC */
    return calculated_crc == received_crc;
}

#ifdef __cplusplus
}
#endif

#endif /* BLE_PROTOCOL_H */