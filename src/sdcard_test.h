/**
 * @file sdcard_test.h
 * @brief SD card testing helper functions
 */

#ifndef SDCARD_TEST_H
#define SDCARD_TEST_H

#include <zephyr/kernel.h>
#include "sdcard_handler.h"

/**
 * @brief Run a minimal SD card test
 * 
 * This function performs a simple test of the SD card functionality:
 * 1. Initialize the SD card
 * 2. Write a small test file
 * 3. Read back the test file
 * 4. List files in the root directory
 * 
 * @return 0 on success, negative error code on failure
 */
int run_sdcard_test(void);

#endif /* SDCARD_TEST_H */