#include "can_wrapper.h"
#include "ttcan_scheduler.h"

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/device.h>
#include <zephyr/drivers/can.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/byteorder.h>

#define RX_THREAD_STACK_SIZE 512
#define RX_THREAD_PRIORITY 2
#define STATE_POLL_THREAD_STACK_SIZE 512
#define STATE_POLL_THREAD_PRIORITY 2
#define LED_MSG_ID 0x10
#define COUNTER_MSG_ID 0x12345
#define SET_LED 1
#define RESET_LED 0
#define SLEEP_TIME K_MSEC(250)

uint8_t ttcan_tick_cnt[8];

/* Simple timer handle for TTCAN */
static uint8_t timer_handle_storage[4];
ttcan_timer_type_t timer_handle = timer_handle_storage;
uint8_t ttcan_sens_ctrl[2] = {0x20,0x40};
uint8_t ttcan_sens_data[2] = {0x50,0x70};
ttcan_data_timeslot_t ttcan_messages[] = {
    {
        .node_id = 10,
        .window_num = 0,
        .message_type = TTCAN_MSG_READ,
        .data_ptr = &ttcan_sens_ctrl[0],
        .num_of_bytes = sizeof(ttcan_sens_ctrl),
    },
    {
        .node_id = 10,
        .window_num = 1,
        .message_type = TTCAN_MSG_WRITE,
        .data_ptr = &ttcan_sens_ctrl[0],
        .num_of_bytes = sizeof(ttcan_sens_ctrl),
    },
    {
        .node_id = 10,
        .window_num = 2,
        .message_type = TTCAN_MSG_WRITE,
        .data_ptr = &ttcan_sens_data[0],
        .num_of_bytes = sizeof(ttcan_sens_data),
    },
    {
        .node_id = 10,
        .window_num = 10,
        .message_type = TTCAN_MSG_WRITE,
        .data_ptr = &ttcan_sens_data[0],
        .num_of_bytes = sizeof(ttcan_sens_data),
    },
};

ttcan_schedule_t ttcan_schedule = {
    .node_id = 0,
    .messages = ttcan_messages,
    .num_of_messages = (sizeof(ttcan_messages) / sizeof(ttcan_messages[0])),
    .tick_frequency = 1000,
    .ref_tick_frequency = 100,
    .tick_window_size = 1,
    .free_tick_window_size = 1,
};
ttcan_scheduler_ctx_t ctx;

K_THREAD_STACK_DEFINE(rx_thread_stack, RX_THREAD_STACK_SIZE);
K_THREAD_STACK_DEFINE(poll_state_stack, STATE_POLL_THREAD_STACK_SIZE);

/* Define a placeholder for the CAN device */
#define MY_DEVICE_DTS_FAKE 1
const struct device *can_dev = NULL; // Initialize to NULL

struct gpio_dt_spec led = GPIO_DT_SPEC_GET_OR(DT_ALIAS(led0), gpios, {0});

struct k_thread rx_thread_data;
struct k_thread poll_state_thread_data;
struct k_work_poll change_led_work;
struct k_work state_change_work;
enum can_state current_state;
struct can_bus_err_cnt current_err_cnt;

CAN_MSGQ_DEFINE(change_led_msgq, 2);
CAN_MSGQ_DEFINE(counter_msgq, 2);

static struct k_poll_event change_led_events[1] = {
    K_POLL_EVENT_STATIC_INITIALIZER(K_POLL_TYPE_MSGQ_DATA_AVAILABLE,
                                    K_POLL_MODE_NOTIFY_ONLY,
                                    &change_led_msgq, 0)};

void tx_irq_callback(const struct device *dev, int error, void *arg)
{
    char *sender = (char *)arg;

    ARG_UNUSED(dev);

    if (error != 0)
    {
        printf("Callback! error-code: %d\nSender: %s\n",
               error, sender);
    }
}

void rx_thread(void *arg1, void *arg2, void *arg3)
{
    ARG_UNUSED(arg1);
    ARG_UNUSED(arg2);
    ARG_UNUSED(arg3);
    
    /* Disabled CAN thread - just sleep forever */
    printf("CAN RX thread started (but CAN disabled)\n");
    
    while (1) {
        k_sleep(K_SECONDS(10));
    }
}

void change_led_work_handler(struct k_work *work)
{
    /* Disabled CAN work handler */
    printf("LED work handler called (but CAN disabled)\n");
}

char *state_to_str(enum can_state state)
{
    switch (state)
    {
    case CAN_STATE_ERROR_ACTIVE:
        return "error-active";
    case CAN_STATE_ERROR_WARNING:
        return "error-warning";
    case CAN_STATE_ERROR_PASSIVE:
        return "error-passive";
    case CAN_STATE_BUS_OFF:
        return "bus-off";
    case CAN_STATE_STOPPED:
        return "stopped";
    default:
        return "unknown";
    }
}

void poll_state_thread(void *unused1, void *unused2, void *unused3)
{
    /* Disabled CAN state poll thread */
    printf("CAN state poll thread started (but CAN disabled)\n");
    
    while (1) {
        /* Just sleep */
        k_sleep(K_SECONDS(10));
    }
}

void state_change_work_handler(struct k_work *work)
{
    printf("CAN state handler disabled\n");
}

/* Simplified callback just for compilation */
void state_change_callback(const struct device *dev, enum can_state state,
                           struct can_bus_err_cnt err_cnt, void *user_data)
{
    /* Disabled */
}

uint8_t toggle = 1;
uint16_t counter = 0;
struct can_frame change_led_frame = {
    .flags = 0,
    .id = LED_MSG_ID,
    .dlc = 1};

struct can_frame schedule_frame = {
        .flags = 0,
        .id = LED_MSG_ID,
        .dlc = 1};
        

struct can_frame counter_frame = {
    .flags = CAN_FRAME_IDE,
    .id = COUNTER_MSG_ID,
    .dlc = 2};

int init_can()
{
    int ret = 0;
    
    /* Initialize TTCAN scheduler regardless of CAN availability */
    ctx.schedule = &ttcan_schedule;
    ctx.master_mode_en = 1;
    ctx.timer = timer_handle;
    ctx.curr_timeslot = 0;
    ctx.curr_window = 0;
    ctx.curr_sched_idx = 0;
    
    /* Initialize the TTCAN scheduler */
    ttcan_scheduler_init(&ctx);
    printf("TTCAN scheduler initialized with %d messages\n", ttcan_schedule.num_of_messages);

    /* Skip actual CAN initialization for now */
    printf("CAN: Skipping hardware initialization\n");
    return 0;

#if 0  /* Disabled CAN initialization that requires hardware */
    const struct can_filter change_led_filter = {
        .flags = 0U,
        .id = LED_MSG_ID,
        .mask = CAN_STD_ID_MASK};

    k_tid_t rx_tid, get_state_tid;

    /* Look up the CAN device - check if the chosen node exists first */
#if DT_HAS_CHOSEN(zephyr_canbus)
    can_dev = DEVICE_DT_GET_OR_NULL(DT_CHOSEN(zephyr_canbus));
#endif
    
    if (can_dev == NULL) {
        printf("CAN: No device available\n");
        return -ENODEV;
    }
#endif
    
    /* Disabled until CAN support is properly available
    if (!device_is_ready(can_dev))
    {
        printf("CAN: Device %s not ready.\n", can_dev->name);
        return 0; 
    }
    */

    /* We've disabled CAN functionality for now since it's not available on this board
     * This will be re-enabled when proper CAN hardware is available.
     */
    
    printf("Finished init (CAN disabled).\n");
    return 0;
}

void send_can()
{
    /* Disabled CAN functionality */
    printf("CAN sending disabled - no hardware support\n");
    
    /* Still toggle the counter for simulation */
    toggle++;
    counter++;
    k_sleep(SLEEP_TIME);
}

void ttcan_timer_trigger(struct k_timer *timer_id)
{
    /* Simplified TTCAN timer callback - just logs a message to avoid crashing */
    printk("TTCAN timer triggered - system is alive\n");
}

void ttcan_timer_stop(struct k_timer *timer_id)
{
    printk("Timer stopped.\n");
}