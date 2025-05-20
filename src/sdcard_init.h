/**
 * @file sdcard_init.h
 * @brief Header for SD card initialization
 * 
 * This file provides functions to initialize the SD card subsystem
 * in a safe manner using a dedicated work queue.
 */

#ifndef SDCARD_INIT_H
#define SDCARD_INIT_H

#include <zephyr/kernel.h>

/**
 * @brief Initialize SD card subsystem asynchronously
 * 
 * This function schedules the SD card initialization to happen
 * after a delay to ensure the system is fully booted.
 * 
 * @return 0 on success (scheduling succeeded), negative error code on failure
 */
int sdcard_init_async(void);

#endif /* SDCARD_INIT_H */