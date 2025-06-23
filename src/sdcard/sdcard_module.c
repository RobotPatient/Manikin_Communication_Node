#include "sdcard_module.h"
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/storage/disk_access.h>
#include <ff.h>
#include <string.h>
#include <stdio.h>

LOG_MODULE_REGISTER(sdcard_module, LOG_LEVEL_INF);

#define DISK_DRIVE_NAME "SD"
#define DISK_MOUNT_PT "/" DISK_DRIVE_NAME ":"
#define FILE_PATH DISK_MOUNT_PT "/hello.txt"
#define CSV_QUEUE_SIZE 16

#define CSV_LINE_MAX_LEN 256
#define CSV_QUEUE_SIZE 25 // Number of queued lines
K_MSGQ_DEFINE(csv_msgq, CSV_LINE_MAX_LEN, CSV_QUEUE_SIZE, 4);

extern struct k_msgq csv_usb_msgq;

K_THREAD_STACK_DEFINE(sd_writer_stack, 1024);
struct k_thread sd_writer_thread;

struct fs_file_t session_file;
static FATFS fat_fs;
static bool fs_mounted = false;
char csv_buffer[256];
extern bool cpr_session_active;

static struct fs_mount_t fat_fs_mnt = {
    .type = FS_FATFS,
    .fs_data = &fat_fs,
    .mnt_point = "/SD:"};

int init_sdcard(void)
{
    struct fs_file_t file;
    int ret;

    static const char *disk_pdrv = "SD";
    uint64_t memory_size_mb;
    uint32_t block_count, block_size;

    if (disk_access_init(disk_pdrv) != 0)
    {
        printk("Storage init ERROR!");
        return -1;
    }

    if (disk_access_ioctl(disk_pdrv, DISK_IOCTL_GET_SECTOR_COUNT, &block_count))
    {
        printk("Unable to get sector count");
        return -1;
    }

    if (disk_access_ioctl(disk_pdrv, DISK_IOCTL_GET_SECTOR_SIZE, &block_size))
    {
        printk("Unable to get sector size");
        return -1;
    }

    memory_size_mb = (uint64_t)block_count * block_size;
    printk("Memory Size(MB): %u\n", (uint32_t)(memory_size_mb >> 20));

    int res = fs_mount(&fat_fs_mnt);
    if (res == FR_OK)
    {
        fs_mounted = true;
        fs_file_t_init(&file);
        ret = fs_open(&file, FILE_PATH, FS_O_READ);
        if (ret < 0)
        {
            printk("Creating test file...\n");
            ret = fs_open(&file, FILE_PATH, FS_O_CREATE | FS_O_WRITE);
            if (ret < 0)
            {
                printk("Failed to create file: %d\n", ret);
                return -1;
            }
            const char *msg = "Hello World\n";
            fs_write(&file, msg, strlen(msg));
        }
        else
        {
            char buffer[64];
            ssize_t bytes_read = fs_read(&file, buffer, sizeof(buffer) - 1);
            if (bytes_read > 0)
            {
                buffer[bytes_read] = '\0';
                printk("Read from file: %s", buffer);
            }
        }
        fs_close(&file);
        k_thread_create(&sd_writer_thread, sd_writer_stack,
                        K_THREAD_STACK_SIZEOF(sd_writer_stack),
                        sd_writer_thread_func, NULL, NULL, NULL,
                        5, 0, K_NO_WAIT);
        k_thread_name_set(&sd_writer_thread, "sd_writer");
    }
    else
    {
        printk("Error mounting disk.\n");
        return -1;
    }

    return 0;
}

void write_to_session_file(char *csv_formatted_text, size_t length)
{
    if (!cpr_session_active)
    {
        printk("Session not active, skipping queue.\n");
        return;
    }

    if (length >= CSV_LINE_MAX_LEN)
    {
        printk("Line too long to queue\n");
        return;
    }

    if (k_msgq_put(&csv_msgq, csv_formatted_text, K_NO_WAIT) != 0)
        printk("CSV queue full, dropping sample\n");
}

void write_vl_to_session_file(sample_sensor1_t *vl_samples, uint8_t num)
{
    if (!cpr_session_active)
    {
        return;
    }
    for (uint8_t i = 0; i < num; i++)
    {
        memset(csv_buffer, 0x00, sizeof(csv_buffer));
        size_t len = snprintf(csv_buffer, sizeof(csv_buffer),
                              "%s,%u,%d\n",
                              vl_samples[i].sensor_name,
                              vl_samples[i].frame_id,
                              vl_samples[i].data.distance_mm);
        write_to_session_file(csv_buffer, len);

        if (k_msgq_put(&csv_usb_msgq, csv_buffer, K_NO_WAIT) != 0)
            printk("CSV USB queue full, dropping sample\n");
    }
}

void write_ads_to_session_file(sample_sensor2_t *ads_samples, uint8_t num)
{
    if (!cpr_session_active)
    {
        return;
    }
    for (uint8_t i = 0; i < num; i++)
    {
        memset(csv_buffer, 0x00, sizeof(csv_buffer));
        size_t len = snprintf(csv_buffer, sizeof(csv_buffer),
                              "%s,%u,%d,%d,%d,%d,%d,%d,%d,%d\n",
                              ads_samples[i].sensor_name,
                              ads_samples[i].frame_id,
                              ads_samples[i].data.ch1_mv,
                              ads_samples[i].data.ch2_mv,
                              ads_samples[i].data.ch3_mv,
                              ads_samples[i].data.ch4_mv,
                              ads_samples[i].data.ch5_mv,
                              ads_samples[i].data.ch6_mv,
                              ads_samples[i].data.ch7_mv,
                              ads_samples[i].data.ch8_mv);
        write_to_session_file(csv_buffer, len);
        if (k_msgq_put(&csv_usb_msgq, csv_buffer, K_NO_WAIT) != 0)
            printk("CSV USB queue full, dropping sample\n");
    }
}

void write_sdp_to_session_file(sample_sensor3_t *sdp_samples, uint8_t num)
{
    if (!cpr_session_active)
    {
        return;
    }
    for (uint8_t i = 0; i < num; i++)
    {
        memset(csv_buffer, 0x00, sizeof(csv_buffer));
        size_t len = snprintf(csv_buffer, sizeof(csv_buffer),
                              "%s,%u,%.4f,%.4f\n",
                              sdp_samples[i].sensor_name,
                              sdp_samples[i].frame_id,
                              (double)sdp_samples[i].data.pressure,
                              (double)sdp_samples[i].data.temp);
        write_to_session_file(csv_buffer, len);
        if (k_msgq_put(&csv_usb_msgq, csv_buffer, K_NO_WAIT) != 0)
            printk("CSV USB queue full, dropping sample\n");
    }
}

void write_bhi_to_session_file(sample_sensor4_t *bhi_samples, uint8_t num)
{
    if (!cpr_session_active)
    {
        return;
    }
    for (uint8_t i = 0; i < num; i++)
    {
        memset(csv_buffer, 0x00, sizeof(csv_buffer));
        size_t len = snprintf(csv_buffer, sizeof(csv_buffer),
                              "%s,%u,%.4f,%.4f,%.4f\n",
                              bhi_samples[i].sensor_name,
                              bhi_samples[i].frame_id,
                              (double)bhi_samples[i].data.pitch_deg,
                              (double)bhi_samples[i].data.roll_deg,
                              (double)bhi_samples[i].data.yaw_deg);
        write_to_session_file(csv_buffer, len);
    }
}

void sd_writer_thread_func(void *arg1, void *arg2, void *arg3)
{
    char line[CSV_LINE_MAX_LEN];
    while (1)
    {
        if (k_msgq_get(&csv_msgq, &line, K_FOREVER) == 0 && cpr_session_active)
        {
            ssize_t written = fs_write(&session_file, line, strlen(line));
            if (written < 0)
                printk("SD Write failed: %d\n", written);
        }
    }
}
