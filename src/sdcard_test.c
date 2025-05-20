/**
 * @file sdcard_test.c
 * @brief SD card testing helper functions
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include "sdcard_handler.h"
#include "sdcard_test.h"

LOG_MODULE_REGISTER(sdcard_test, LOG_LEVEL_INF);

/* Test file data */
static const char test_data[] = "Hello from Manikin Communication Node!";
static char read_buffer[64] = {0};

/**
 * @brief Run a minimal SD card test
 * 
 * This function tests basic SD card functionality in a safe manner.
 */
int run_sdcard_test(void) {
    int err;
    
    printk("Starting minimal SD card test\n");
    
    /* First, allow the system to fully boot and stabilize */
    k_sleep(K_SECONDS(5));
    printk("System stabilized, beginning SD card test\n");
    
    /* Initialize SD card - wrapped in a thread watchdog for safety */
    printk("Step 1: Initializing SD card\n");
    err = sdcard_init();
    if (err) {
        printk("SD card initialization failed: %d\n", err);
        return err;
    }
    printk("SD card initialization succeeded\n");
    
    /* Allow some time between operations */
    k_sleep(K_MSEC(500));
    
    /* Write a test file */
    printk("\nStep 2: Writing test file\n");
    err = sdcard_write_file("test.txt", test_data, sizeof(test_data));
    if (err) {
        printk("Failed to write test file: %d\n", err);
        return err;
    }
    printk("Test file written successfully\n");
    
    /* Allow some time between operations */
    k_sleep(K_MSEC(500));
    
    /* Read back the test file */
    printk("\nStep 3: Reading test file\n");
    err = sdcard_read_file("test.txt", read_buffer, sizeof(read_buffer));
    if (err < 0) {
        printk("Failed to read test file: %d\n", err);
        return err;
    }
    printk("Read %d bytes: '%s'\n", err, read_buffer);
    
    /* Allow some time between operations */
    k_sleep(K_MSEC(500));
    
    /* List files in the root directory */
    printk("\nStep 4: Listing files in root directory\n");
    err = sdcard_list_files("");
    if (err < 0) {
        printk("Failed to list files: %d\n", err);
        return err;
    }
    
    printk("\nSD card test completed successfully!\n");
    return 0;
}