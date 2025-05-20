/**
 * @file sdcard_handler.c
 * @brief SD card handling functions implementation
 */

#include <zephyr/kernel.h>
#include <zephyr/fs/fs.h>
#include <zephyr/storage/disk_access.h>
#include <zephyr/logging/log.h>
#include "sdcard_handler.h"

LOG_MODULE_REGISTER(sdcard, LOG_LEVEL_INF);

/* We use the generic filesystem API instead of FATFS directly */
static struct fs_mount_t mp = {
    .type = FS_FATFS,
    .mnt_point = "/SD:",
};
static bool fs_mounted = false;

#define SD_MOUNT_POINT "/SD:"

int sdcard_init(void) {
    static const char *disk_pdrv = "SD";
    uint64_t memory_size_mb;
    uint32_t block_count;
    uint32_t block_size;
    int err;

    LOG_INF("Initializing SD card driver...");
    printk("Initializing SD card driver...\n");

    /* Check if disk is available */
    err = disk_access_init(disk_pdrv);
    if (err != 0) {
        LOG_ERR("Failed to initialize disk: %d", err);
        printk("Failed to initialize disk: %d\n", err);
        return err;
    }

    LOG_INF("Disk driver initialized successfully");
    printk("Disk driver initialized successfully\n");

    /* Get disk information */
    err = disk_access_ioctl(disk_pdrv, DISK_IOCTL_GET_SECTOR_COUNT, &block_count);
    if (err) {
        LOG_ERR("Unable to get sector count: %d", err);
        printk("Unable to get sector count: %d\n", err);
        return err;
    }
    
    err = disk_access_ioctl(disk_pdrv, DISK_IOCTL_GET_SECTOR_SIZE, &block_size);
    if (err) {
        LOG_ERR("Unable to get sector size: %d", err);
        printk("Unable to get sector size: %d\n", err);
        return err;
    }
    
    memory_size_mb = (uint64_t)block_count * block_size;
    memory_size_mb >>= 20;  /* Convert to MB */
    
    LOG_INF("Disk information - Sector count: %u, Sector size: %u bytes, Total size: %u MB", 
            block_count, block_size, (uint32_t)memory_size_mb);
    printk("Disk information - Sector count: %u, Sector size: %u bytes, Total size: %u MB\n", 
            block_count, block_size, (uint32_t)memory_size_mb);

    /* Set disk name and mount the filesystem */
    LOG_INF("Mounting FAT filesystem on %s...", disk_pdrv);
    printk("Mounting FAT filesystem on %s...\n", disk_pdrv);

    mp.storage_dev = disk_pdrv;
    mp.mnt_point = "/SD:";
    mp.type = FS_FATFS;
    mp.fs_data = NULL;
    
    err = fs_mount(&mp);
    if (err) {
        LOG_ERR("Failed to mount filesystem: %d", err);
        printk("Failed to mount filesystem: %d\n", err);
        
        /* Try to provide more specific error information */
        switch (err) {
            case -ENOENT:
                LOG_ERR("Error mounting: Path not found");
                printk("Error mounting: Path not found\n");
                break;
            case -ENODEV:
                LOG_ERR("Error mounting: No such device or device not initialized");
                printk("Error mounting: No such device or device not initialized\n");
                break;
            case -ENOEXEC:
                LOG_ERR("Error mounting: Not a FAT filesystem (wrong signature)");
                printk("Error mounting: Not a FAT filesystem (wrong signature)\n");
                break;
            case -ENOMEM:
                LOG_ERR("Error mounting: Out of memory");
                printk("Error mounting: Out of memory\n");
                break;
            default:
                LOG_ERR("Error mounting: Unknown error");
                printk("Error mounting: Unknown error\n");
                break;
        }
        return err;
    }
    
    fs_mounted = true;
    LOG_INF("Filesystem mounted successfully");
    printk("Filesystem mounted successfully\n");
    
    return 0;
}

int sdcard_write_file(const char *filename, const void *data, size_t size) {
    struct fs_file_t file;
    char filepath[128];
    int err;

    if (!fs_mounted) {
        printk("Filesystem not mounted\n");
        return -ENODEV;
    }

    /* Create complete file path */
    snprintf(filepath, sizeof(filepath), "%s/%s", SD_MOUNT_POINT, filename);
    
    printk("Creating file %s...\n", filepath);
    
    /* Initialize file structure */
    fs_file_t_init(&file);
    
    /* Create file */
    err = fs_open(&file, filepath, FS_O_CREATE | FS_O_WRITE);
    
    /* If open succeeded, write the data */
    if (err == 0) {
        printk("Writing %d bytes...\n", size);
        err = fs_write(&file, data, size);
        
        /* Close file regardless of write success */
        int close_err = fs_close(&file);
        if (close_err && err == 0) {
            err = close_err;
        }
    }
    
    if (err < 0) {
        printk("File operation failed: %d\n", err);
        return err;
    }
    
    printk("Successfully wrote %d bytes to file %s\n", size, filepath);
    return 0;
}

int sdcard_read_file(const char *filename, void *buffer, size_t buffer_size) {
    struct fs_file_t file;
    char filepath[128];
    int err;
    int bytes_read = -EIO;

    if (!fs_mounted) {
        printk("Filesystem not mounted\n");
        return -ENODEV;
    }

    /* Create complete file path */
    snprintf(filepath, sizeof(filepath), "%s/%s", SD_MOUNT_POINT, filename);
    
    /* Initialize file structure */
    fs_file_t_init(&file);
    
    /* Open file */
    err = fs_open(&file, filepath, FS_O_READ);
    
    /* If open succeeded, read the data */
    if (err == 0) {
        /* Read data from file */
        bytes_read = fs_read(&file, buffer, buffer_size);
        
        /* Close file regardless of read success */
        fs_close(&file);
    }
    
    if (err != 0) {
        printk("Failed to open file %s: %d\n", filepath, err);
        return err;
    }
    
    if (bytes_read < 0) {
        printk("Failed to read from file: %d\n", bytes_read);
        return bytes_read;
    }
    
    printk("Successfully read %d bytes from file %s\n", bytes_read, filepath);
    return bytes_read;
}

int sdcard_list_files(const char *path) {
    struct fs_dir_t dir;
    char dirpath[128];
    int err;
    int count = 0;

    if (!fs_mounted) {
        printk("Filesystem not mounted\n");
        return -ENODEV;
    }

    /* Create complete directory path */
    snprintf(dirpath, sizeof(dirpath), "%s/%s", SD_MOUNT_POINT, path);
    
    /* Initialize directory structure */
    fs_dir_t_init(&dir);
    
    /* Open directory */
    err = fs_opendir(&dir, dirpath);
    
    /* If open succeeded, read the directory entries */
    if (err == 0) {
        printk("Files in directory %s:\n", dirpath);
        
        /* Read directory entries */
        while (1) {
            struct fs_dirent entry;
            
            err = fs_readdir(&dir, &entry);
            if (err || entry.name[0] == 0) {
                break;
            }
            
            /* Print file/directory information */
            if (entry.type == FS_DIR_ENTRY_DIR) {
                printk("  [DIR] %s\n", entry.name);
            } else {
                printk("  [FILE] %s (size: %zu bytes)\n", entry.name, entry.size);
            }
            
            count++;
        }
        
        /* Close directory */
        fs_closedir(&dir);
    }
    
    if (err != 0 && err != -ENOENT) {
        printk("Failed to access directory %s: %d\n", dirpath, err);
        return err;
    }
    
    printk("Found %d items in directory\n", count);
    return count;
}