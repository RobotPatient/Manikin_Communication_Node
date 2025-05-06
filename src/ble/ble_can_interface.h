#ifndef BLE_CAN_INTERFACE_H
#define BLE_CAN_INTERFACE_H

#include <zephyr/kernel.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Stub CAN interface - does nothing but print logs
 * 
 * This is a simplified interface for systems without CAN hardware.
 */

/**
 * @brief Send a CAN message - STUB implementation
 *
 * This function is a stub that simply logs the message data.
 * No actual CAN hardware is accessed.
 *
 * @param can_id CAN message ID
 * @param data Pointer to message data
 * @param len Length of message data (0-8 bytes)
 */
void send_can_message(uint32_t can_id, uint8_t *data, uint8_t len);

#ifdef __cplusplus
}
#endif

#endif /* BLE_CAN_INTERFACE_H */