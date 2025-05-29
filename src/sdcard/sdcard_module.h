#ifndef SDCARD_MODULE_H
#define SDCARD_MODULE_H

#include <zephyr/fs/fs.h>
#include "can/can_rx_types.h"
int init_sdcard(void);
void write_to_session_file(char *csv_formatted_text, size_t length);
void write_vl_to_session_file(sample_sensor1_t *vl_samples, uint8_t num);
void write_ads_to_session_file(sample_sensor2_t *ads_samples, uint8_t num);
void write_sdp_to_session_file(sample_sensor3_t *sdp_samples, uint8_t num);
void write_bhi_to_session_file(sample_sensor4_t *bhi_samples, uint8_t num);
void sd_writer_thread_func(void *arg1, void *arg2, void *arg3);

extern struct fs_file_t session_file;

#endif // SDCARD_MODULE_H
