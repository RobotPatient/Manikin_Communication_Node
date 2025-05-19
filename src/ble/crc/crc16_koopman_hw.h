/**
 * @file crc16_koopman_hw.h
 * @brief Hardware-accelerated CRC-16 implementation using Koopman polynomial (0x8D95)
 * 
 * This implementation uses the Zephyr CRC library which may use hardware 
 * acceleration if available on the platform.
 */
#ifndef CRC16_KOOPMAN_HW_H
#define CRC16_KOOPMAN_HW_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/**
 * @brief Initialize the CRC module for Koopman polynomial
 * 
 * This function initializes the CRC module for the Koopman polynomial (0x8D95) 
 * configuration. It should be called once during system initialization.
 *
 * @return true if successful, false otherwise
 */
bool crc16_koopman_hw_init(void);

/**
 * @brief Check if optimized CRC is available
 *
 * @return true if optimized CRC functionality is available, false otherwise
 */
bool crc16_koopman_hw_available(void);

/**
 * @brief Calculate CRC-16 using optimized implementation if available
 * 
 * This function calculates a 16-bit CRC using the Koopman polynomial 0x8D95
 * with optimized implementation if available, or falls back to software implementation.
 * 
 * @param data Pointer to the data buffer
 * @param length Length of the data buffer in bytes
 * @return Calculated CRC-16 value
 */
uint16_t crc16_koopman_hw(const uint8_t *data, size_t length);

/**
 * @brief Update an existing CRC-16 with new data using optimized implementation if available
 * 
 * @param crc Initial CRC value (use 0 for first call)
 * @param data Pointer to the data buffer
 * @param length Length of the data buffer in bytes
 * @return Updated CRC-16 value
 */
uint16_t crc16_koopman_hw_update(uint16_t crc, const uint8_t *data, size_t length);

#endif /* CRC16_KOOPMAN_HW_H */