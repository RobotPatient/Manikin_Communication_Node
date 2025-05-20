#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <zephyr/kernel.h>
#include "ble/ble_can_interface.h"

/* Ultra-minimal implementation - just a stub */

/* Implementation of send_can_message */
void send_can_message(uint32_t can_id, uint8_t *data, uint8_t len)
{
    printf("MOCK: CAN message with ID 0x%08x, len %d\n", can_id, len);
}