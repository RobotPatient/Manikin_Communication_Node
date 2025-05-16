/**
 * @file ble_protocol.c
 * @brief Implementation of BLE protocol utilities
 */

#include "ble_protocol.h"
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(ble_protocol, LOG_LEVEL_INF);

void test_ble_protocol(void)
{
    uint8_t test_buffer[64];
    int len;
    
    /* Test formatting CPR Start command without CRC */
    len = format_cpr_start_command(test_buffer, sizeof(test_buffer), false);
    if (len > 0) {
        LOG_INF("Formatted CPR Start command (no CRC), length: %d bytes", len);
        LOG_HEXDUMP_INF(test_buffer, len, "CPR Start command (no CRC)");
    } else {
        LOG_ERR("Failed to format CPR Start command: %d", len);
    }
    
    /* Test formatting CPR Start command with CRC */
    len = format_cpr_start_command(test_buffer, sizeof(test_buffer), true);
    if (len > 0) {
        LOG_INF("Formatted CPR Start command (with CRC), length: %d bytes", len);
        LOG_HEXDUMP_INF(test_buffer, len, "CPR Start command (with CRC)");
        
        /* Test CRC verification */
        bool crc_valid = verify_ble_message_crc(test_buffer, len);
        LOG_INF("CRC verification %s", crc_valid ? "PASSED" : "FAILED");
    } else {
        LOG_ERR("Failed to format CPR Start command with CRC: %d", len);
    }
    
    /* Test formatting CPR Stop command */
    len = format_cpr_stop_command(test_buffer, sizeof(test_buffer), false);
    if (len > 0) {
        LOG_INF("Formatted CPR Stop command, length: %d bytes", len);
        LOG_HEXDUMP_INF(test_buffer, len, "CPR Stop command");
    } else {
        LOG_ERR("Failed to format CPR Stop command: %d", len);
    }
    
    /* Test formatting data command */
    const char *test_id = "in:test123";
    len = format_data_command(test_buffer, sizeof(test_buffer), test_id, strlen(test_id), false);
    if (len > 0) {
        LOG_INF("Formatted Data command, length: %d bytes", len);
        LOG_HEXDUMP_INF(test_buffer, len, "Data command");
    } else {
        LOG_ERR("Failed to format Data command: %d", len);
    }
    
    /* Test formatting data command with CRC */
    len = format_data_command(test_buffer, sizeof(test_buffer), test_id, strlen(test_id), true);
    if (len > 0) {
        LOG_INF("Formatted Data command (with CRC), length: %d bytes", len);
        LOG_HEXDUMP_INF(test_buffer, len, "Data command (with CRC)");
        
        /* Test CRC verification */
        bool crc_valid = verify_ble_message_crc(test_buffer, len);
        LOG_INF("CRC verification %s", crc_valid ? "PASSED" : "FAILED");
    } else {
        LOG_ERR("Failed to format Data command with CRC: %d", len);
    }
    
    /* Test formatting time data command */
    const char *test_time = "20250506150722";
    len = format_timedata_command(test_buffer, sizeof(test_buffer), test_time, strlen(test_time), false);
    if (len > 0) {
        LOG_INF("Formatted Time Data command, length: %d bytes", len);
        LOG_HEXDUMP_INF(test_buffer, len, "Time Data command");
    } else {
        LOG_ERR("Failed to format Time Data command: %d", len);
    }
    
    /* Test CRC corruption detection */
    len = format_timedata_command(test_buffer, sizeof(test_buffer), test_time, strlen(test_time), true);
    if (len > 0) {
        LOG_INF("Formatted Time Data command with CRC, length: %d bytes", len);
        LOG_HEXDUMP_INF(test_buffer, len, "Time Data command with CRC");
        
        /* Corrupt the CRC by changing one byte */
        size_t crc_pos = len - 3; /* Position of the CRC MSB */
        test_buffer[crc_pos] ^= 0x01; /* Flip one bit */
        
        /* Verify the corrupted message */
        bool crc_valid = verify_ble_message_crc(test_buffer, len);
        LOG_INF("Corrupted CRC verification %s (expected to fail)", crc_valid ? "PASSED" : "FAILED");
    } else {
        LOG_ERR("Failed to format Time Data command with CRC: %d", len);
    }
}