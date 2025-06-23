#include <zephyr/kernel.h>
#include <zephyr/canbus/isotp.h>
#include <zephyr/drivers/can.h>
#include "can_addr_decl.h"
#include "can_rx_types.h"
#include <session/session.h>
#include <zephyr/drivers/uart.h>

#define CAN_CMD_LEN 1 // start/stop are single-byte
#define SYSTEM_CMD_STOP 0
#define SYSTEM_CMD_START 1
#define SYSTEM_CMD_RETRANSMIT_SAMPLE_SENSOR_1 2
#define SYSTEM_CMD_RETRANSMIT_SAMPLE_SENSOR_2 3
#define SYSTEM_CMD_RETRANSMIT_SAMPLE_SENSOR_3 4
#define SYSTEM_CMD_GET_STATUS 5
#define SYSTEM_CMD_GET_NUM_SAMPLES_SENSOR_1 6
#define SYSTEM_CMD_GET_NUM_SAMPLES_SENSOR_2 7
#define SYSTEM_CMD_GET_NUM_SAMPLES_SENSOR_3 8

const struct device *can_dev;
struct isotp_recv_ctx recv_ctx_sensorhub1_cmd;
struct isotp_recv_ctx recv_ctx_sensorhub1_sensor1;
struct isotp_recv_ctx recv_ctx_sensorhub1_sensor2;
struct isotp_recv_ctx recv_ctx_sensorhub1_sensor3;

struct isotp_recv_ctx recv_ctx_sensorhub2_cmd;
struct isotp_recv_ctx recv_ctx_sensorhub2_sensor1;
static const struct device *const uart_dev = DEVICE_DT_GET_ONE(zephyr_cdc_acm_uart);

K_THREAD_STACK_DEFINE(rx_sensorhub_sensor1_thread_stack, 1024);
K_THREAD_STACK_DEFINE(rx_sensorhub_sensor2_thread_stack, 1024);
K_THREAD_STACK_DEFINE(rx_sensorhub_sensor3_thread_stack, 1024);
struct k_thread rx_sensorhub_sensor1_thread_data;
struct k_thread rx_sensorhub_sensor2_thread_data;
struct k_thread rx_sensorhub_sensor3_thread_data;

K_THREAD_STACK_DEFINE(rx_sensorhub2_sensor1_thread_stack, 1024);
struct k_thread rx_sensorhub2_sensor1_thread_data;


#define MAX_FRAME_WINDOW 20

#define VL_RING_SIZE    (MAX_FRAME_WINDOW * sizeof(sample_sensor1_t))
#define ADS_RING_SIZE   (MAX_FRAME_WINDOW * sizeof(sample_sensor2_t))
#define SDP_RING_SIZE   (MAX_FRAME_WINDOW * sizeof(sample_sensor3_t))
#define BHI_RING_SIZE   (MAX_FRAME_WINDOW * sizeof(sample_sensor4_t))

RING_BUF_DECLARE(vl_ring, VL_RING_SIZE);
RING_BUF_DECLARE(ads_ring, ADS_RING_SIZE);
RING_BUF_DECLARE(sdp_ring, SDP_RING_SIZE);
RING_BUF_DECLARE(bhi_ring, BHI_RING_SIZE);

uint8_t vl_backing_array[VL_RING_SIZE];
uint8_t ads_backing_array[ADS_RING_SIZE];
uint8_t sdp_backing_array[SDP_RING_SIZE];
uint8_t bhi_backing_array[BHI_RING_SIZE];

struct ring_buf bhi_ring;
struct ring_buf vl_ring;
struct ring_buf sdp_ring;
struct ring_buf ads_ring;

int process_bhi_sample(sample_sensor4_t *sample) {
    ring_buf_put(&bhi_ring, (uint8_t *)sample, sizeof(sample_sensor4_t));
    return 0;
}

int process_sdp_sample(sample_sensor3_t *sample) {
    ring_buf_put(&sdp_ring, (uint8_t *)sample, sizeof(sample_sensor3_t));
    return 0;
}

int process_vl_sample(sample_sensor1_t *sample) {
    ring_buf_put(&vl_ring, (uint8_t *)sample, sizeof(sample_sensor1_t));
    return 0;
}

int process_ads_sample(sample_sensor2_t *sample) {
    ring_buf_put(&ads_ring, (uint8_t *)sample, sizeof(sample_sensor2_t));
    return 0;
}


void can_transmit_start_msg() {
    struct can_frame start_frame = {
        .id = 0x0,
        .dlc = 1,
        .data = {0x01},
    };
    can_send(can_dev, &start_frame, K_NO_WAIT, NULL, NULL);
}

void can_transmit_stop_msg() {
    struct can_frame stop_frame = {
        .id = 0x0,
        .dlc = 1,
        .data = {120},
    };
    can_send(can_dev, &stop_frame, K_MSEC(2), NULL, NULL);
}
char bhi360_line[256];
sample_sensor4_t bhi360_fusion_sample;
void rx_sensorhub2_sensor1_thread(void *arg1, void *arg2, void *arg3)
{
    ARG_UNUSED(arg1);
    ARG_UNUSED(arg2);
    ARG_UNUSED(arg3);
    int ret, received_len;
    static uint8_t rx_buffer[256];

    ret = isotp_bind(&recv_ctx_sensorhub2_sensor1, can_dev,
                     &tx_sensorhub2_sensor1, &rx_sensorhub2_sensor1,
                     &fc_opts_sensorhub2_sensor1, K_FOREVER);
    if (ret != ISOTP_N_OK)
    {
        printk("Failed to bind to rx ID %d [%d]\n",
               rx_sensorhub2_sensor1.std_id, ret);
        return;
    }

    while (1)
    {
        received_len = isotp_recv(&recv_ctx_sensorhub2_sensor1, rx_buffer,
                                  sizeof(rx_buffer) - 1U, K_MSEC(2000));
        if (received_len < 0)
        {
            // printk("Receiving error [%d]\n", received_len);
            continue;
        }
        if (received_len >= sizeof(sample_sensor4_t))
        {

            memcpy(&bhi360_fusion_sample, rx_buffer, sizeof(sample_sensor4_t));
            process_bhi_sample(&bhi360_fusion_sample);
            printk("Sensor: %.*s\n", 8, bhi360_fusion_sample.sensor_name);
            printk("Frame ID: %u\n", bhi360_fusion_sample.frame_id);
            printk("Pitch: %f deg\n", bhi360_fusion_sample.data.pitch_deg);
            printk("Roll: %f deg\n", bhi360_fusion_sample.data.roll_deg);
            printk("Yaw: %f deg\n", bhi360_fusion_sample.data.yaw_deg);
            size_t len = snprintf(bhi360_line, sizeof(bhi360_line), "BHI360FUS, %d, %f, %f, %f\n", bhi360_fusion_sample.frame_id, bhi360_fusion_sample.data.pitch_deg, bhi360_fusion_sample.data.roll_deg, bhi360_fusion_sample.data.yaw_deg);
            uart_fifo_fill(uart_dev, bhi360_line, len);
        }
        else
        {
            // printk("Received incomplete data (%d bytes)\n", received_len);
        }
    }
}

sample_sensor1_t sample;
void rx_sensorhub_sensor1_thread(void *arg1, void *arg2, void *arg3)
{
    ARG_UNUSED(arg1);
    ARG_UNUSED(arg2);
    ARG_UNUSED(arg3);

    int ret, rem_len, received_len;
    struct net_buf *buf;

    ret = isotp_bind(&recv_ctx_sensorhub1_sensor1, can_dev,
                     &tx_sensorhub1_sensor1, &rx_sensorhub1_sensor1,
                     &fc_opts_sensorhub1_sensor1, K_FOREVER);
    if (ret != ISOTP_N_OK)
    {
        printk("Failed to bind to rx ID %d [%d]\n",
               rx_sensorhub1_sensor1.std_id, ret);
        return;
    }

    uint8_t rx_data[SAMPLE_BUFFER_SIZE];
    uint8_t *write_ptr = rx_data;

    while (1)
    {
        received_len = 0;
        write_ptr = rx_data;

        do
        {
            rem_len = isotp_recv_net(&recv_ctx_sensorhub1_sensor1, &buf, K_MSEC(2000));
            if (rem_len < 0)
            {
                // printk("Receiving error [%d]\n", rem_len);
                break;
            }

            while (buf != NULL)
            {
                size_t copy_len = MIN(buf->len, SAMPLE_BUFFER_SIZE - received_len);
                memcpy(write_ptr, buf->data, copy_len);
                write_ptr += copy_len;
                received_len += copy_len;

                buf = net_buf_frag_del(NULL, buf);
            }
        } while (rem_len);

        // printk("Got %d bytes in total\n", received_len);

        if (received_len >= sizeof(sample_sensor1_t))
        {
            memcpy(&sample, rx_data, sizeof(sample_sensor1_t));
            process_vl_sample(&sample);
            printk("Sensor: %.*s\n", 8, sample.sensor_name);
            printk("Frame ID: %u\n", sample.frame_id);
            printk("Distance: %d mm\n", sample.data.distance_mm);
        }
        else
        {
            // printk("Received incomplete data (%d bytes)\n", received_len);
        }
    }
}
sample_sensor2_t ads7138_sample;
void rx_sensorhub_sensor2_thread(void *arg1, void *arg2, void *arg3)
{
    ARG_UNUSED(arg1);
    ARG_UNUSED(arg2);
    ARG_UNUSED(arg3);
    int ret, received_len;
    static uint8_t rx_buffer[32];

    ret = isotp_bind(&recv_ctx_sensorhub1_sensor2, can_dev,
                     &tx_sensorhub1_sensor2, &rx_sensorhub1_sensor2,
                     &fc_opts_sensorhub1_sensor2, K_FOREVER);
    if (ret != ISOTP_N_OK)
    {
        printk("Failed to bind to rx ID %d [%d]\n",
               rx_sensorhub1_sensor2.std_id, ret);
        return;
    }

    while (1)
    {
        received_len = isotp_recv(&recv_ctx_sensorhub1_sensor2, rx_buffer,
                                  sizeof(rx_buffer) - 1U, K_MSEC(2000));
        if (received_len < 0)
        {
            // printk("Receiving error [%d]\n", received_len);
            continue;
        }
        if (received_len >= sizeof(sample_sensor2_t))
        {
            memcpy(&ads7138_sample, rx_buffer, sizeof(sample_sensor2_t));
            process_ads_sample(&ads7138_sample);
            printk("Sensor: %.*s\n", 8, ads7138_sample.sensor_name);
            printk("Frame ID: %u\n", ads7138_sample.frame_id);
            printk("CH1: %d mv\n", ads7138_sample.data.ch1_mv);
            printk("CH2: %d mv\n", ads7138_sample.data.ch2_mv);
            printk("CH3: %d mv\n", ads7138_sample.data.ch3_mv);
            printk("CH4: %d mv\n", ads7138_sample.data.ch4_mv);
            printk("CH5: %d mv\n", ads7138_sample.data.ch5_mv);
            printk("CH6: %d mv\n", ads7138_sample.data.ch6_mv);
            printk("CH7: %d mv\n", ads7138_sample.data.ch7_mv);
            printk("CH8: %d mv\n", ads7138_sample.data.ch8_mv);
        }
        else
        {
            // printk("Received incomplete data (%d bytes)\n", received_len);
        }
    }
}

sample_sensor3_t sdp810_sample;
void rx_sensorhub_sensor3_thread(void *arg1, void *arg2, void *arg3)
{
    ARG_UNUSED(arg1);
    ARG_UNUSED(arg2);
    ARG_UNUSED(arg3);
    int ret, received_len;
    static uint8_t rx_buffer[32];

    ret = isotp_bind(&recv_ctx_sensorhub1_sensor3, can_dev,
                     &tx_sensorhub1_sensor3, &rx_sensorhub1_sensor3,
                     &fc_opts_sensorhub1_sensor3, K_FOREVER);
    if (ret != ISOTP_N_OK)
    {
        printk("Failed to bind to rx ID %d [%d]\n",
               rx_sensorhub1_sensor3.std_id, ret);
        return;
    }

    while (1)
    {
        received_len = isotp_recv(&recv_ctx_sensorhub1_sensor3, rx_buffer,
                                  sizeof(rx_buffer) - 1U, K_MSEC(2000));
        if (received_len < 0)
        {
            // printk("Receiving error [%d]\n", received_len);
            continue;
        }
        if (received_len >= sizeof(sample_sensor3_t))
        {

            memcpy(&sdp810_sample, rx_buffer, sizeof(sample_sensor3_t));
            printk("Sensor: %.*s\n", 8, sdp810_sample.sensor_name);
            printk("Frame ID: %u\n", sdp810_sample.frame_id);
            printk("Pressure: %.16f mbar\n", (double)sdp810_sample.data.pressure);
            printk("Temp: %.16f fahrenheit\n", (double)sdp810_sample.data.temp);
            process_sdp_sample(&sdp810_sample);
        }
        else
        {
            // printk("Received incomplete data (%d bytes)\n", received_len);
        }
    }
}

void send_complete_cb(int error_nr, void *arg)
{
    ARG_UNUSED(arg);
    printk("TX complete cb [%d]\n", error_nr);
}

int send_raw_can_cmd(uint8_t cmd)
{
    struct can_frame frame = {
        .id = BROADCAST_CAN_ID,
        .dlc = CAN_CMD_LEN,
        .flags = 0,
    };

    frame.data[0] = cmd;

    int ret = can_send(can_dev, &frame, K_MSEC(100), NULL, NULL);
    if (ret)
    {
        printk("Raw CAN send failed: %d\n", ret);
    }
    return ret;
}
int send_command(uint8_t cmd)
{
    static struct isotp_send_ctx send_ctx;
    return isotp_send(&send_ctx, can_dev,
                      &cmd, sizeof(cmd),
                      &tx_sensorhub1_cmd, &rx_sensorhub1_cmd,
                      send_complete_cb, NULL);
}

int send_command_sensorhub2(uint8_t cmd)
{
    static struct isotp_send_ctx send_ctx;
    return isotp_send(&send_ctx, can_dev,
                      &cmd, sizeof(cmd),
                      &tx_sensorhub2_cmd, &rx_sensorhub2_cmd,
                      send_complete_cb, NULL);
}


int receive_response(uint8_t *rx_buffer, size_t buffer_len, int timeout_ms)
{
    return isotp_recv(&recv_ctx_sensorhub1_cmd, rx_buffer, buffer_len, K_MSEC(timeout_ms));
}

int receive_response_sensorhub2(uint8_t *rx_buffer, size_t buffer_len, int timeout_ms)
{
    return isotp_recv(&recv_ctx_sensorhub2_cmd, rx_buffer, buffer_len, K_MSEC(timeout_ms));
}

void print_status(uint8_t *data)
{
    system_status_t *status = (system_status_t *)data;
    printk("System ID: %d\n", status->id);
    printk("State: %d\n", status->state);
    printk("Sensor 1 SR: %d Hz, Health: %d, FaultCnt: %d\n",
           status->sensor1_sr, status->sensor1_health, status->sensor1_faultcnt);
    printk("Sensor 2 SR: %d Hz, Health: %d, FaultCnt: %d\n",
           status->sensor2_sr, status->sensor2_health, status->sensor2_faultcnt);
    printk("Sensor 1 Name: %s Sensor 2 Name %s\n",
           status->sensor1_name, status->sensor2_name);
}

int can_transport_init()
{
    k_tid_t tid;
    uint8_t rx_buf[50];
    int ret = 0;
    can_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_canbus));
    if (!device_is_ready(can_dev))
    {
        printk("CAN: Device driver not ready.\n");
        return 0;
    }

    ret = can_start(can_dev);
    if (ret != 0)
    {
        printk("CAN: Failed to start device [%d]\n", ret);
        return 0;
    }

    ring_buf_init(&vl_ring, ARRAY_SIZE(vl_backing_array), vl_backing_array);
    ring_buf_init(&ads_ring, ARRAY_SIZE(ads_backing_array), ads_backing_array);
    ring_buf_init(&sdp_ring, ARRAY_SIZE(sdp_backing_array), sdp_backing_array);
    ring_buf_init(&bhi_ring, ARRAY_SIZE(bhi_backing_array), bhi_backing_array);

    tid = k_thread_create(&rx_sensorhub_sensor1_thread_data, rx_sensorhub_sensor1_thread_stack,
                          K_THREAD_STACK_SIZEOF(rx_sensorhub_sensor1_thread_stack),
                          rx_sensorhub_sensor1_thread, NULL, NULL, NULL,
                          2, 0, K_NO_WAIT);
    if (!tid)
    {
        printk("ERROR spawning rx thread\n");
        return 0;
    }
    k_thread_name_set(tid, "rx_sensorhub_sensor1");

    tid = k_thread_create(&rx_sensorhub_sensor2_thread_data, rx_sensorhub_sensor2_thread_stack,
                          K_THREAD_STACK_SIZEOF(rx_sensorhub_sensor2_thread_stack),
                          rx_sensorhub_sensor2_thread, NULL, NULL, NULL,
                          1, 0, K_NO_WAIT);
    if (!tid)
    {
        printk("ERROR spawning rx thread\n");
        return 0;
    }
    k_thread_name_set(tid, "rx_sensorhub_sensor2");

    tid = k_thread_create(&rx_sensorhub_sensor3_thread_data, rx_sensorhub_sensor3_thread_stack,
                          K_THREAD_STACK_SIZEOF(rx_sensorhub_sensor3_thread_stack),
                          rx_sensorhub_sensor3_thread, NULL, NULL, NULL,
                          2, 0, K_NO_WAIT);
    if (!tid)
    {
        printk("ERROR spawning rx thread\n");
        return 0;
    }
    k_thread_name_set(tid, "rx_sensorhub_sensor3");

    tid = k_thread_create(&rx_sensorhub2_sensor1_thread_data, rx_sensorhub2_sensor1_thread_stack,
                          K_THREAD_STACK_SIZEOF(rx_sensorhub2_sensor1_thread_stack),
                          rx_sensorhub2_sensor1_thread, NULL, NULL, NULL,
                          2, 0, K_NO_WAIT);
    if (!tid)
    {
        printk("ERROR spawning rx thread\n");
        return 0;
    }
    k_thread_name_set(tid, "rx_sensorhub2_sensor1");
    printk("Start sending data\n");
    ret = isotp_bind(&recv_ctx_sensorhub1_cmd, can_dev,
                     &rx_sensorhub1_cmd, // remote sender (0x10)
                     &tx_sensorhub1_cmd, // our receiver (0x201)
                     &fc_opts_sensorhub1_cmd, K_NO_WAIT);
    if (ret != ISOTP_N_OK)
    {
        printk("ISO-TP bind failed [%d]\n", ret);
        return -1;
    }
    ret = isotp_bind(&recv_ctx_sensorhub2_cmd, can_dev,
                     &rx_sensorhub2_cmd, // remote sender (0x10)
                     &tx_sensorhub2_cmd, // our receiver (0x201)
                     &fc_opts_sensorhub2_cmd, K_NO_WAIT);
    if (ret != ISOTP_N_OK)
    {
        printk("ISO-TP bind failed [%d]\n", ret);
        return -1;
    }
    send_command(SYSTEM_CMD_GET_STATUS);
    k_sleep(K_MSEC(200));
    ret = receive_response(rx_buf, sizeof(rx_buf), 1000);
    if (ret > 0)
    {
        print_status(rx_buf);
    }
    else
    {
        printk("Failed to get status [%d]\n", ret);
    }
    k_sleep(K_MSEC(200));
    send_command_sensorhub2(SYSTEM_CMD_GET_STATUS);
    k_sleep(K_MSEC(200));
    ret = receive_response_sensorhub2(rx_buf, sizeof(rx_buf), 1000);
    if (ret > 0)
    {
        print_status(rx_buf);
    }
    else
    {
        printk("Failed to get status [%d]\n", ret);
    }

    return 0;
}