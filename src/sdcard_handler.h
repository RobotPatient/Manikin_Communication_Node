/**
 * @file sdcard_handler.h
 * @brief SD card handling functions
 */

#ifndef SDCARD_HANDLER_H
#define SDCARD_HANDLER_H

#include <zephyr/kernel.h>
#include <zephyr/fs/fs.h>
#include <zephyr/storage/disk_access.h>

/**
 * @brief Initialize the SD card
 * 
 * This function mounts the SD card file system
 * 
 * @return 0 on success, negative error code on failure
 */
int sdcard_init(void);

/**
 * @brief Write a file to the SD card
 * 
 * This function writes the specified data to a file on the SD card
 * 
 * @param filename Name of the file to write
 * @param data Data to write
 * @param size Size of the data
 * @return 0 on success, negative error code on failure
 */
int sdcard_write_file(const char *filename, const void *data, size_t size);

/**
 * @brief Read a file from the SD card
 * 
 * This function reads the specified file from the SD card
 * 
 * @param filename Name of the file to read
 * @param buffer Buffer to store the read data
 * @param buffer_size Size of the buffer
 * @return Number of bytes read, or negative error code on failure
 */
int sdcard_read_file(const char *filename, void *buffer, size_t buffer_size);

/**
 * @brief List files in a directory
 * 
 * This function lists files in the specified directory on the SD card
 * 
 * @param path Path to the directory
 * @return 0 on success, negative error code on failure
 */
int sdcard_list_files(const char *path);

#endif /* SDCARD_HANDLER_H */