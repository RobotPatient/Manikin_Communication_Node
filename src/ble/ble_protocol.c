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
    
    /* Test formatting CPR Start command */
    len = format_cpr_start_command(test_buffer, sizeof(test_buffer));
    if (len > 0) {
        LOG_INF("Formatted CPR Start command, length: %d bytes", len);
        LOG_HEXDUMP_INF(test_buffer, len, "CPR Start command");
    } else {
        LOG_ERR("Failed to format CPR Start command: %d", len);
    }
    
    /* Test formatting CPR Stop command */
    len = format_cpr_stop_command(test_buffer, sizeof(test_buffer));
    if (len > 0) {
        LOG_INF("Formatted CPR Stop command, length: %d bytes", len);
        LOG_HEXDUMP_INF(test_buffer, len, "CPR Stop command");
    } else {
        LOG_ERR("Failed to format CPR Stop command: %d", len);
    }
    
    /* Test formatting data command */
    const char *test_id = "in:test123";
    len = format_data_command(test_buffer, sizeof(test_buffer), test_id, strlen(test_id));
    if (len > 0) {
        LOG_INF("Formatted Data command, length: %d bytes", len);
        LOG_HEXDUMP_INF(test_buffer, len, "Data command");
    } else {
        LOG_ERR("Failed to format Data command: %d", len);
    }
    
    /* Test formatting time data command */
    const char *test_time = "20250506150722";
    len = format_timedata_command(test_buffer, sizeof(test_buffer), test_time, strlen(test_time));
    if (len > 0) {
        LOG_INF("Formatted Time Data command, length: %d bytes", len);
        LOG_HEXDUMP_INF(test_buffer, len, "Time Data command");
    } else {
        LOG_ERR("Failed to format Time Data command: %d", len);
    }
}