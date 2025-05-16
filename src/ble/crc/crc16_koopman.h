/**
 * @file crc16_koopman.h
 * @brief CRC-16 implementation using Koopman polynomial (0x8D95)
 */
#ifndef CRC16_KOOPMAN_H
#define CRC16_KOOPMAN_H

#include <stdint.h>
#include <stddef.h>

/**
 * @brief Calculate CRC-16 using Koopman polynomial 0x8D95
 * 
 * This function calculates a 16-bit CRC using the Koopman polynomial 0x8D95,
 * which has excellent error detection properties.
 * 
 * @param data Pointer to the data buffer
 * @param length Length of the data buffer in bytes
 * @return Calculated CRC-16 value
 */
uint16_t crc16_koopman(const uint8_t *data, size_t length);

/**
 * @brief Update an existing CRC-16 with new data
 * 
 * @param crc Initial CRC value (use 0 for first call)
 * @param data Pointer to the data buffer
 * @param length Length of the data buffer in bytes
 * @return Updated CRC-16 value
 */
uint16_t crc16_koopman_update(uint16_t crc, const uint8_t *data, size_t length);

#endif /* CRC16_KOOPMAN_H */