/* main.c - Application main entry point */

/*
 * Copyright (c) 2015-2016 Intel Corporation
 * Copyright (c) 2019 Marcio Montenegro <mtuxpe@gmail.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/types.h>
#include <stddef.h>
#include <string.h>
#include <errno.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>

#include <zephyr/fs/fs.h>
#include <zephyr/fs/fs_interface.h>
#include <zephyr/storage/disk_access.h>
#include <ff.h>

volatile size_t cnt = 0;
LOG_MODULE_REGISTER(main);

#define DISK_DRIVE_NAME "SD"

#define DISK_MOUNT_PT "/" DISK_DRIVE_NAME ":"

static FATFS fat_fs;

/* mounting info */

static struct fs_mount_t mp = {

	.type = FS_FATFS,

	.fs_data = &fat_fs,

};

#define FS_RET_OK FR_OK

#define MAX_PATH 128
#define SOME_FILE_NAME "some.dat"
#define SOME_DIR_NAME "some"
#define SOME_REQUIRED_LEN MAX(sizeof(SOME_FILE_NAME), sizeof(SOME_DIR_NAME))

static bool create_some_entries(const char *base_path)
{
	char path[MAX_PATH];
	struct fs_file_t file;
	int base = strlen(base_path);
	fs_file_t_init(&file);
	if (base >= (sizeof(path) - SOME_REQUIRED_LEN))
	{
		LOG_ERR("Not enough concatenation buffer to create file paths");
		return false;
	}

	LOG_INF("Creating some dir entries in %s", base_path);
	strncpy(path, base_path, sizeof(path));
	path[base++] = '/';
	path[base] = 0;
	strcat(&path[base], SOME_FILE_NAME);
	if (fs_open(&file, path, FS_O_CREATE) != 0)
	{
		LOG_ERR("Failed to create file %s", path);
		return false;
	}

	fs_close(&file);
	path[base] = 0;
	strcat(&path[base], SOME_DIR_NAME);
	if (fs_mkdir(path) != 0)
	{
		LOG_ERR("Failed to create dir %s", path);
		/* If code gets here, it has at least successes to create the
		 * file so allow function to return true.
		 */
	}

	return true;
}

static const char *disk_mount_pt = DISK_MOUNT_PT;

int main(void)
{

	int err;
	while (1)
	{
		cnt++;
		LOG_ERR("Hello World!\n");
		if (cnt == 5)
		{
			do {
				static const char *disk_pdrv = DISK_DRIVE_NAME;
				uint64_t memory_size_mb;
				uint32_t block_count;
				uint32_t block_size;
		
			
				if (disk_access_ioctl(disk_pdrv,
						DISK_IOCTL_GET_SECTOR_COUNT, &block_count)) {
					LOG_ERR("Unable to get sector count");
					break;
				}
				LOG_INF("Block count %u", block_count);
		
				if (disk_access_ioctl(disk_pdrv,
						DISK_IOCTL_GET_SECTOR_SIZE, &block_size)) {
					LOG_ERR("Unable to get sector size");
					break;
				}
				printk("Sector size %u\n", block_size);
		
				memory_size_mb = (uint64_t)block_count * block_size;
				printk("Memory Size(MB) %u\n", (uint32_t)(memory_size_mb >> 20));
		
				if (disk_access_ioctl(disk_pdrv,
						DISK_IOCTL_CTRL_DEINIT, NULL) != 0) {
					LOG_ERR("Storage deinit ERROR!");
					break;
				}
			} while (0);
		}
		k_msleep(1000);
	}
	return 0;
}
