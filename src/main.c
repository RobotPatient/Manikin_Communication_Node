#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/canbus/isotp.h>
#include <zephyr/drivers/can.h>

#define BROADCAST_CAN_ID 0x000  // or whatever your system uses
#define CAN_CMD_LEN      1      // start/stop are single-byte
#define SYSTEM_CMD_STOP                          0
#define SYSTEM_CMD_START                         1
#define SYSTEM_CMD_RETRANSMIT_SAMPLE_SENSOR_1    2
#define SYSTEM_CMD_RETRANSMIT_SAMPLE_SENSOR_2    3
#define SYSTEM_CMD_RETRANSMIT_SAMPLE_SENSOR_3    4
#define SYSTEM_CMD_GET_STATUS                    5
#define SYSTEM_CMD_GET_NUM_SAMPLES_SENSOR_1      6
#define SYSTEM_CMD_GET_NUM_SAMPLES_SENSOR_2      7
#define SYSTEM_CMD_GET_NUM_SAMPLES_SENSOR_3      8

const struct isotp_fc_opts fc_opts_8_0 = {.bs = 8, .stmin = 0};
const struct isotp_fc_opts fc_opts_0_5 = {.bs = 0, .stmin = 0};
const struct isotp_fc_opts fc_opts_0_10 = {.bs = 0, .stmin = 0};

const struct isotp_msg_id rx_addr_8_0 = {
	.std_id = 0x180,
#ifdef CONFIG_SAMPLE_CAN_FD_MODE
	.flags = ISOTP_MSG_FDF | ISOTP_MSG_BRS,
#endif
};
const struct isotp_msg_id tx_addr_8_0 = {
	.std_id = 0x080,
#ifdef CONFIG_SAMPLE_CAN_FD_MODE
	.dl = 64,
	.flags = ISOTP_MSG_FDF | ISOTP_MSG_BRS,
#endif
};
const struct isotp_msg_id rx_addr_0_5 = {
	.std_id = 0x101,
#ifdef CONFIG_SAMPLE_CAN_FD_MODE
	.flags = ISOTP_MSG_FDF | ISOTP_MSG_BRS,
#endif
};
const struct isotp_msg_id tx_addr_0_5 = {
	.std_id = 0x01,
#ifdef CONFIG_SAMPLE_CAN_FD_MODE
	.dl = 64,
	.flags = ISOTP_MSG_FDF | ISOTP_MSG_BRS,
#endif
};

const struct isotp_msg_id rx_addr_0_10 = {
	.std_id = 0x010,
#ifdef CONFIG_SAMPLE_CAN_FD_MODE
	.flags = ISOTP_MSG_FDF | ISOTP_MSG_BRS,
#endif
};

const struct isotp_msg_id tx_addr_0_10 = {
	.std_id = 0x201,
#ifdef CONFIG_SAMPLE_CAN_FD_MODE
	.dl = 64,
	.flags = ISOTP_MSG_FDF | ISOTP_MSG_BRS,
#endif
};

const struct device *can_dev;
struct isotp_recv_ctx recv_ctx_10_0;
struct isotp_recv_ctx recv_ctx_8_0;
struct isotp_recv_ctx recv_ctx_0_5;

K_THREAD_STACK_DEFINE(rx_8_0_thread_stack, 1024);
K_THREAD_STACK_DEFINE(rx_0_5_thread_stack, 1024);
struct k_thread rx_8_0_thread_data;
struct k_thread rx_0_5_thread_data;

const char tx_data_small[] = "hallo";

typedef struct __attribute__((__packed__)) {
    char sensor_name[8];
    uint32_t frame_id;
	struct {
	uint8_t distance_mm;
	}data;
} sample_sensor1_t;

typedef struct __attribute__((__packed__)) {
    char sensor_name[8];
    uint32_t frame_id;
	struct {
        uint16_t ch1_mv;
        uint16_t ch2_mv;
        uint16_t ch3_mv;
        uint16_t ch4_mv;
        uint16_t ch5_mv;
        uint16_t ch6_mv;
        uint16_t ch7_mv;
        uint16_t ch8_mv;
	}data;
} sample_sensor2_t;
#define SAMPLE2_BUFFER_SIZE sizeof(sample_sensor2_t)
#define SAMPLE_BUFFER_SIZE sizeof(sample_sensor1_t)

void rx_8_0_thread(void *arg1, void *arg2, void *arg3)
{
	ARG_UNUSED(arg1);
	ARG_UNUSED(arg2);
	ARG_UNUSED(arg3);

	int ret, rem_len, received_len;
	struct net_buf *buf;

	ret = isotp_bind(&recv_ctx_8_0, can_dev,
			 &tx_addr_8_0, &rx_addr_8_0,
			 &fc_opts_8_0, K_FOREVER);
	if (ret != ISOTP_N_OK) {
		printk("Failed to bind to rx ID %d [%d]\n",
		       rx_addr_8_0.std_id, ret);
		return;
	}

	uint8_t rx_data[SAMPLE_BUFFER_SIZE];
	uint8_t *write_ptr = rx_data;

	while (1) {
		received_len = 0;
		write_ptr = rx_data;

		do {
			rem_len = isotp_recv_net(&recv_ctx_8_0, &buf, K_MSEC(2000));
			if (rem_len < 0) {
				printk("Receiving error [%d]\n", rem_len);
				break;
			}

			while (buf != NULL) {
				size_t copy_len = MIN(buf->len, SAMPLE_BUFFER_SIZE - received_len);
				memcpy(write_ptr, buf->data, copy_len);
				write_ptr += copy_len;
				received_len += copy_len;

				buf = net_buf_frag_del(NULL, buf);
			}
		} while (rem_len);

		// printk("Got %d bytes in total\n", received_len);

		if (received_len >= sizeof(sample_sensor1_t)) {
			sample_sensor1_t sample;
			memcpy(&sample, rx_data, sizeof(sample_sensor1_t));
			printk("Sensor: %.*s\n", 8, sample.sensor_name);
			printk("Frame ID: %u\n", sample.frame_id);
			printk("Distance: %d mm\n", sample.data.distance_mm);
		} else {
			printk("Received incomplete data (%d bytes)\n", received_len);
		}
	}
}

void rx_0_5_thread(void *arg1, void *arg2, void *arg3)
{
	ARG_UNUSED(arg1);
	ARG_UNUSED(arg2);
	ARG_UNUSED(arg3);
	int ret, received_len;
	static uint8_t rx_buffer[32];

	ret = isotp_bind(&recv_ctx_0_5, can_dev,
			 &tx_addr_0_5, &rx_addr_0_5,
			 &fc_opts_0_5, K_FOREVER);
	if (ret != ISOTP_N_OK) {
		printk("Failed to bind to rx ID %d [%d]\n",
		       rx_addr_0_5.std_id, ret);
		return;
	}

	while (1) {
		received_len = isotp_recv(&recv_ctx_0_5, rx_buffer,
					  sizeof(rx_buffer)-1U, K_MSEC(2000));
		if (received_len < 0) {
			printk("Receiving error [%d]\n", received_len);
			continue;
		}
		if (received_len >= sizeof(sample_sensor2_t)) {
			sample_sensor2_t sample;
			memcpy(&sample, rx_buffer, sizeof(sample_sensor2_t));
			printk("Sensor: %.*s\n", 8, sample.sensor_name);
			printk("Frame ID: %u\n", sample.frame_id);
			printk("CH1: %d mv\n", sample.data.ch1_mv);
			printk("CH2: %d mv\n", sample.data.ch2_mv);
			printk("CH3: %d mv\n", sample.data.ch3_mv);
			printk("CH4: %d mv\n", sample.data.ch4_mv);
			printk("CH5: %d mv\n", sample.data.ch5_mv);
			printk("CH6: %d mv\n", sample.data.ch6_mv);
			printk("CH7: %d mv\n", sample.data.ch7_mv);
			printk("CH8: %d mv\n", sample.data.ch8_mv);
		} else {
			printk("Received incomplete data (%d bytes)\n", received_len);
		}
	}
}

void send_complette_cb(int error_nr, void *arg)
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
    if (ret) {
        printk("Raw CAN send failed: %d\n", ret);
    }
    return ret;
}
int send_command(uint8_t cmd)
{
	static struct isotp_send_ctx send_ctx;
    return isotp_send(&send_ctx, can_dev,
                      &cmd, sizeof(cmd),
                      &tx_addr_0_10, &rx_addr_0_10,
                      send_complette_cb, NULL);
}

int receive_response(uint8_t *rx_buffer, size_t buffer_len, int timeout_ms)
{
    return isotp_recv(&recv_ctx_10_0, rx_buffer, buffer_len, K_MSEC(timeout_ms));
}

void print_status(uint8_t *data)
{
	typedef struct
	{
			// Byte 0
			uint8_t        id : 4;
			uint8_t        startup_ok : 1;
			uint8_t        flash_ok : 1;
			uint8_t state : 2;
	
			// Byte 1
			uint8_t         sensor1_sr : 7;
			uint8_t sensor1_health : 2;
	
			// Byte 2
			uint8_t sensor1_faultcnt : 3;
			uint8_t sensor2_sr : 7;
	
			// Byte 3
			uint8_t sensor2_health : 2;
			uint8_t         sensor2_faultcnt : 3;
			char sensor1_name[8];
			char sensor2_name[8];
	} system_status_t;
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

/**
 * @brief Main application entry point.
 *
 */
int main(void)
{
	k_tid_t tid;
	static struct isotp_send_ctx send_ctx_8_0;
	static struct isotp_send_ctx send_ctx_0_10;
	uint8_t rx_buf[50];
	int ret = 0;

	can_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_canbus));
	if (!device_is_ready(can_dev)) {
		printk("CAN: Device driver not ready.\n");
		return 0;
	}

	ret = can_start(can_dev);
	if (ret != 0) {
		printk("CAN: Failed to start device [%d]\n", ret);
		return 0;
	}

	tid = k_thread_create(&rx_8_0_thread_data, rx_8_0_thread_stack,
			      K_THREAD_STACK_SIZEOF(rx_8_0_thread_stack),
			      rx_8_0_thread, NULL, NULL, NULL,
			      2, 0, K_NO_WAIT);
	if (!tid) {
		printk("ERROR spawning rx thread\n");
		return 0;
	}
	k_thread_name_set(tid, "rx_8_0");

	tid = k_thread_create(&rx_0_5_thread_data, rx_0_5_thread_stack,
			      K_THREAD_STACK_SIZEOF(rx_0_5_thread_stack),
			      rx_0_5_thread, NULL, NULL, NULL,
			      2, 0, K_NO_WAIT);
	if (!tid) {
		printk("ERROR spawning rx thread\n");
		return 0;
	}
	k_thread_name_set(tid, "rx_0_5");

	printk("Start sending data\n");
	ret = isotp_bind(&recv_ctx_10_0, can_dev,
    &rx_addr_0_10,  // remote sender (0x10)
    &tx_addr_0_10,  // our receiver (0x201)
    &fc_opts_0_10, K_NO_WAIT);
if (ret != ISOTP_N_OK) {
printk("ISO-TP bind failed [%d]\n", ret);
return -1;
}
    send_command(SYSTEM_CMD_GET_STATUS);
    k_sleep(K_MSEC(200));
	ret = receive_response(rx_buf, sizeof(rx_buf), 1000);
    if (ret > 0) {
        print_status(rx_buf);
    } else {
        printk("Failed to get status [%d]\n", ret);
    }
	while (1) {
		k_msleep(1000);
		// ret = isotp_send(&send_ctx_0_10, can_dev,
		// 		 tx_data_small, sizeof(tx_data_small),
		// 		 &tx_addr_0_10, &rx_addr_0_10,
		// 		 send_complette_cb, NULL);
		// if (ret != ISOTP_N_OK) {
		// 	printk("Error while sending data to ID %d [%d]\n",
		// 	       tx_addr_0_5.std_id, ret);
		// }

		// ret = isotp_send(&send_ctx_8_0, can_dev,
		// 		 tx_data_large, sizeof(tx_data_large),
		// 		 &tx_addr_8_0, &rx_addr_8_0, NULL, NULL);
		// if (ret != ISOTP_N_OK) {
		// 	printk("Error while sending data to ID %d [%d]\n",
		// 	       tx_addr_8_0.std_id, ret);
		// }
	}
}
