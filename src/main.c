#include <string.h>
#include <zephyr/kernel.h>
#include <session/session.h>
#include <can/can_transport.h>

#include <zephyr/drivers/can.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/ring_buffer.h>

#include <zephyr/usb/usb_device.h>
#include <zephyr/usb/usbd.h>
#include <zephyr/logging/log.h>

#define BUF_SIZE 64
#define START_CMD "start"
#define STOP_CMD "stop"

const struct device *const uart_dev = DEVICE_DT_GET_ONE(zephyr_cdc_acm_uart);
static const struct device *const can_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_canbus));
static bool can_sending = false;

K_THREAD_STACK_DEFINE(cdc_read_thread_stack, 1024);
struct k_thread cdc_read_thread_stack_data;

void process_command(const char *cmd) {

    struct can_frame start_frame = {
        .id = 0x0,
        .dlc = 1,
        .data = {0x01},
    };


    struct can_frame stop_frame = {
        .id = 0x0,
        .dlc = 1,
        .data = {120},
    };

    if (strncmp(cmd, START_CMD, strlen(START_CMD)) == 0) {
        can_sending = true;
        printk("CAN sending started\n");
		uart_fifo_fill(uart_dev, "CAN sending started\n", strlen("CAN sending started\n"));
        can_send(can_dev, &start_frame, K_NO_WAIT, NULL, NULL);
    } else if (strncmp(cmd, STOP_CMD, strlen(STOP_CMD)) == 0) {
        can_sending = false;
        printk("CAN sending stopped\n");
		uart_fifo_fill(uart_dev, "CAN sending stopped\n", strlen("CAN sending stopped\n"));
        can_send(can_dev, &stop_frame, K_MSEC(2), NULL, NULL);
    }
}

void cdc_read_thread(void *arg1, void *arg2, void *arg3)
{
    uint8_t buf[BUF_SIZE];
    size_t len = 0;

    while (1) {
        int r = uart_fifo_read(uart_dev, buf + len, BUF_SIZE - len);
        if (r > 0) {
            len += r;
            if (buf[len - 1] == '\n' || buf[len - 1] == '\r') {
                buf[len - 1] = '\0';
                process_command((char *)buf);
                len = 0;
            }
        }
        k_msleep(10);
    }
}

// void can_send_thread(void)
// {
//     struct can_frame frame = {
//         .id_type = CAN_STANDARD_IDENTIFIER,
//         .rtr = CAN_DATAFRAME,
//         .id = 0x123,
//         .dlc = 1,
//         .data = {0xAB},
//     };

//     while (1) {
//         if (can_sending) {
//             can_send(can_dev, &frame, K_MSEC(100), NULL, NULL);
//         }
//         k_msleep(100);
//     }
// }

/**
 * @brief Main application entry point.
 *
 */
int main(void)
{
        k_tid_t tid;
	if (!device_is_ready(uart_dev)) {
		printf("CDC ACM device not ready");
		return 0;
	}

	can_transport_init();
	// session_init();
	// ENABLE PE3
    usb_enable(NULL);
    tid = k_thread_create(&cdc_read_thread_stack_data, cdc_read_thread_stack,
                          K_THREAD_STACK_SIZEOF(cdc_read_thread_stack),
                          cdc_read_thread, NULL, NULL, NULL,
                          2, 0, K_NO_WAIT);
    if (!tid)
    {
        printk("ERROR spawning rx thread\n");
        return 0;
    }
    k_thread_name_set(tid, "rx_usb");



	while (1) {
		k_msleep(1000);
	}
}
