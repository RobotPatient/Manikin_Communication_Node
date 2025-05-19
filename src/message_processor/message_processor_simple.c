/**
 * @file message_processor_simple.c
 * @brief Simplified message processing module for BLE commands
 */

#include "message_processor.h"
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <stdlib.h>

LOG_MODULE_REGISTER(message_processor);

/* Forward declarations */
static int process_direct_command(uint8_t cmd_byte);
static int process_command(uint8_t *cmd_data, uint16_t len);

/* Internal state storage */
static char instructor_id[MSG_BUFFER_SIZE/2];
static char trainee_id[MSG_BUFFER_SIZE/2];
static uint8_t current_user_role = USER_ROLE_NONE;

/* Time data storage */
static char time_data[18];
static bool has_time_data = false;

/* Base time and timestamp for RTC simulation */
static struct {
    int year;
    int month;
    int day;
    int hour;
    int min;
    int sec;
    uint32_t base_ticks;  /* System ticks when time was set */
} rtc_base;

/* Message queue for asynchronous processing */
K_MSGQ_DEFINE(command_msgq, MSG_BUFFER_SIZE, MSG_QUEUE_SIZE, 4);

/* Command processing thread stack */
K_THREAD_STACK_DEFINE(processor_stack, 2048);
static struct k_thread processor_thread;
static k_tid_t processor_tid;

/* External references to LED control flags defined in main.c */
extern bool led_request_pending;
extern bool led_requested_state;

/* External references to CPR session functions in main.c */
extern void start_cpr_session(void);
extern void stop_cpr_session(void);

/**
 * Thread function for asynchronous command processing
 */
static void processor_thread_func(void *arg1, void *arg2, void *arg3)
{
    uint8_t cmd_buffer[MSG_BUFFER_SIZE];
    int ret;

    LOG_INF("Message processor thread started");

    while (1) {
        /* Wait for a command to arrive in the queue */
        ret = k_msgq_get(&command_msgq, cmd_buffer, K_FOREVER);
        
        if (ret == 0) {
            /* Message received successfully */
            uint8_t cmd_type = cmd_buffer[0];
            uint16_t cmd_len = 0;
            
            if (cmd_type == 0) {
                /* Single byte direct command */
                uint8_t cmd_byte = cmd_buffer[1];
                LOG_INF("Processing direct command: 0x%02x", cmd_byte);
                process_direct_command(cmd_byte);
            } else {
                /* Full command buffer */
                cmd_len = (cmd_buffer[1] << 8) | cmd_buffer[2];
                LOG_INF("Processing command with data length: %d bytes", cmd_len);
                process_command(&cmd_buffer[3], cmd_len);
            }
        } else {
            /* Error handling with minimal logging */
            k_sleep(K_MSEC(10));
        }
    }
}

int message_processor_init(void)
{
    /* Initialize internal state */
    memset(instructor_id, 0, sizeof(instructor_id));
    memset(trainee_id, 0, sizeof(trainee_id));
    current_user_role = USER_ROLE_NONE;
    
    /* Clear time data and RTC base */
    memset(time_data, 0, sizeof(time_data));
    memset(&rtc_base, 0, sizeof(rtc_base));
    
    /* Start the processor thread */
    processor_tid = k_thread_create(&processor_thread,
                                   processor_stack,
                                   K_THREAD_STACK_SIZEOF(processor_stack),
                                   processor_thread_func,
                                   NULL, NULL, NULL,
                                   7, 0, K_NO_WAIT);
    
    if (processor_tid == NULL) {
        LOG_ERR("Failed to create message processor thread");
        return -ENOMEM;
    }
    
    k_thread_name_set(processor_tid, "msg_proc");
    
    return 0;
}

/**
 * Private helper to request LED control (via flags)
 */
static void request_led_state(bool state)
{
    led_requested_state = state;
    led_request_pending = true;
    LOG_INF("LED state requested: %s", state ? "ON" : "OFF");
}

/**
 * Private helper to handle ID string processing
 */
static void process_id_string(const uint8_t *data, size_t len, bool is_instructor)
{
    /* Safety check */
    if (!data || len == 0) {
        return;
    }
    
    /* Calculate prefix length */
    size_t prefix_len = strlen(is_instructor ? 
                             USER_ROLE_INSTRUCTOR_PREFIX : 
                             USER_ROLE_TRAINEE_PREFIX);
    
    /* Ensure we have enough data */
    if (len <= prefix_len) {
        LOG_WRN("ID string too short");
        return;
    }
    
    /* Calculate available ID length (with bounds check) */
    char *id_storage = is_instructor ? instructor_id : trainee_id;
    size_t max_id_len = (MSG_BUFFER_SIZE/2) - 1;
    size_t avail_data = len - prefix_len;
    size_t id_len = (avail_data < max_id_len) ? avail_data : max_id_len;
    
    /* Copy and null-terminate */
    memset(id_storage, 0, max_id_len + 1);
    memcpy(id_storage, data + prefix_len, id_len);
    id_storage[id_len] = '\0';
    
    /* Set user role */
    current_user_role = is_instructor ? USER_ROLE_INSTRUCTOR : USER_ROLE_TRAINEE;
    
    /* Visual feedback */
    LOG_INF("Set %s ID: %s", 
            is_instructor ? "instructor" : "trainee", 
            id_storage);
            
    /* Add prominent notification in terminal */
    printk("\n>>> RECEIVED %s ID: %s <<<\n", 
            is_instructor ? "INSTRUCTOR" : "TRAINEE", 
            id_storage);
    
    /* Request LED on */
    request_led_state(true);
}

/**
 * Private helper for processing time data
 */
static void process_time_data(const uint8_t *data_payload, size_t data_len)
{
    /* Expected format: YYYYMMDDHHMMSSMS (14 or 16 characters) */
    const size_t expected_time_len_min = 14;  /* At minimum, we need YYYYMMDDHHMMSS */
    
    /* Validate data length with minimal logging */
    if (data_len < expected_time_len_min) {
        LOG_WRN("Time data too short: %d bytes", data_len);
        return;
    }
    
    /* Copy time data safely with bounds checking */
    size_t copy_len = data_len < sizeof(time_data) - 1 ? data_len : sizeof(time_data) - 1;
    memcpy(time_data, data_payload, copy_len);
    time_data[copy_len] = '\0';
    
    /* Mark as valid */
    has_time_data = true;
    
    /* Log the time data for debugging */
    LOG_INF("Time data received: %s", time_data);
    
    /* Add prominent notification in terminal */
    printk("\n>>> RECEIVED TIME DATA: %s <<<\n", time_data);
    
    /* Format for display */
    if (copy_len >= 14) {
        /* Extract individual components */
        char year_str[5] = {0};
        char month_str[3] = {0};
        char day_str[3] = {0};
        char hour_str[3] = {0};
        char min_str[3] = {0};
        char sec_str[3] = {0};
        
        /* Copy components with null termination */
        memcpy(year_str, time_data, 4);
        memcpy(month_str, time_data + 4, 2);
        memcpy(day_str, time_data + 6, 2);
        memcpy(hour_str, time_data + 8, 2);
        memcpy(min_str, time_data + 10, 2);
        memcpy(sec_str, time_data + 12, 2);
        
        /* Convert to integers */
        int year = atoi(year_str);
        int month = atoi(month_str);
        int day = atoi(day_str);
        int hour = atoi(hour_str);
        int min = atoi(min_str);
        int sec = atoi(sec_str);
        
        /* Validate ranges */
        if (year >= 2023 && year <= 2100 && 
            month >= 1 && month <= 12 &&
            day >= 1 && day <= 31 &&
            hour >= 0 && hour <= 23 &&
            min >= 0 && min <= 59 &&
            sec >= 0 && sec <= 59) {
            
            /* Looks like valid time data */
            LOG_INF("Parsed time: %04d-%02d-%02d %02d:%02d:%02d", 
                    year, month, day, hour, min, sec);
            
            /* Store the parsed time as our base time */
            rtc_base.year = year;
            rtc_base.month = month;
            rtc_base.day = day;
            rtc_base.hour = hour;
            rtc_base.min = min;
            rtc_base.sec = sec;
            rtc_base.base_ticks = k_uptime_get_32();
            
            LOG_INF("Base time set with system ticks: %u", rtc_base.base_ticks);
        } else {
            LOG_WRN("Time data has invalid values: %s", time_data);
        }
    }
    
    /* Request LED on to provide visual feedback that time was received */
    request_led_state(true);
    
    /* Blink LED to indicate time data received (this is better feedback) */
    k_sleep(K_MSEC(300));
    request_led_state(false);
    k_sleep(K_MSEC(300));
    request_led_state(true);
}

/**
 * Private helper for handling CPR data command
 */
static void process_cpr_data(const uint8_t *data_payload, size_t data_len)
{
    /* Print raw data for debugging */
    LOG_INF("CPR Data bytes:");
    for (int i = 0; i < data_len && i < 20; i++) {
        printk("0x%02x ", data_payload[i]);
    }
    printk("\n");
    
    /* Handle protocol-specific identifier strings */
    if (data_len > 3) {
        /* Check for instructor ID format */
        if (data_len >= strlen(USER_ROLE_INSTRUCTOR_PREFIX) + 1 &&
            memcmp(data_payload + 1, USER_ROLE_INSTRUCTOR_PREFIX, strlen(USER_ROLE_INSTRUCTOR_PREFIX)) == 0) {
            
            process_id_string(data_payload + 1, data_len - 1, true);
        }
        /* Check for trainee ID format */
        else if (data_len >= strlen(USER_ROLE_TRAINEE_PREFIX) + 1 &&
                 memcmp(data_payload + 1, USER_ROLE_TRAINEE_PREFIX, strlen(USER_ROLE_TRAINEE_PREFIX)) == 0) {
                 
            process_id_string(data_payload + 1, data_len - 1, false);
        }
    }
}

static int process_direct_command(uint8_t cmd_byte)
{
    LOG_INF("Processing direct command: 0x%02x", cmd_byte);
    
    switch (cmd_byte) {
        case CMD_CONTROL_LED_OFF:
            LOG_INF("Command: LED OFF");
            request_led_state(false);
            break;
            
        case CMD_CONTROL_LED_ON:
            LOG_INF("Command: LED ON");
            request_led_state(true);
            break;
            
        case CMD_CONTROL_START:
            LOG_INF("Command: Start CPR");
            printk("\n>>> iOS SENT DIRECT CPR START COMMAND (0x%02x) <<<\n", cmd_byte);
            start_cpr_session();
            request_led_state(true);
            break;
            
        case CMD_COMMAND_STOP:
            LOG_INF("Command: Stop CPR");
            printk("\n>>> iOS SENT DIRECT CPR STOP COMMAND (0x%02x) <<<\n", cmd_byte);
            stop_cpr_session();
            request_led_state(false);
            break;
            
        default:
            LOG_WRN("Unknown direct command: 0x%02x", cmd_byte);
            return -EINVAL;
    }
    
    return 0;
}

static int process_command(uint8_t *cmd_data, uint16_t len)
{
    /* Check for direct text ID formats (no protocol framing) */
    size_t instr_prefix_len = strlen(USER_ROLE_INSTRUCTOR_PREFIX);
    if (len >= instr_prefix_len &&
        memcmp(cmd_data, USER_ROLE_INSTRUCTOR_PREFIX, instr_prefix_len) == 0) {
        
        process_id_string(cmd_data, len, true);
        return 0;
    }
    
    size_t train_prefix_len = strlen(USER_ROLE_TRAINEE_PREFIX);
    if (len >= train_prefix_len &&
        memcmp(cmd_data, USER_ROLE_TRAINEE_PREFIX, train_prefix_len) == 0) {
        
        process_id_string(cmd_data, len, false);
        return 0;
    }
    
    /* Process structured protocol messages */
    
    /* Validate minimum length for protocol structure */
    if (len < 5) {
        LOG_WRN("Command too short, %d bytes", len);
        return -EINVAL;
    }

    /* Check start byte */
    if (cmd_data[0] != MSG_COMMAND_BYTE_START) {
        LOG_WRN("Invalid start byte: 0x%02x, expected 0x%02x",
                cmd_data[0], MSG_COMMAND_BYTE_START);
        return -EINVAL;
    }

    /* Get the command type */
    uint8_t command = (len > 3) ? cmd_data[3] : 0xFF;
            
    LOG_INF("Processing command byte: 0x%02x", command);

    /* Simple command handling */
    if (command == CMD_CONTROL_LED_OFF) {
        LOG_INF("Command: LED OFF");
        request_led_state(false);
    }
    else if (command == CMD_CONTROL_LED_ON) {
        LOG_INF("Command: LED ON");
        request_led_state(true);
    }
    else if (command == CMD_CONTROL_START) {
        LOG_INF("Command: Start CPR");
        printk("\n>>> iOS SENT CPR START COMMAND (0x%02x) <<<\n", command);
        request_led_state(true);
        LOG_INF("Calling start_cpr_session() from message_processor_simple");
        start_cpr_session();  /* Call CPR session start function */
    }
    else if (command == CMD_COMMAND_STOP) {
        LOG_INF("Command: Stop CPR");
        printk("\n>>> iOS SENT CPR STOP COMMAND (0x%02x) <<<\n", command);
        request_led_state(false);
        LOG_INF("Calling stop_cpr_session() from message_processor_simple");
        stop_cpr_session();   /* Call CPR session stop function */
    }
    else if (command == CMD_COMMAND_DATA) {
        LOG_INF("Command: Received CPR Init Data");
        printk("\n>>> iOS SENT ID DATA COMMAND (0x%02x) <<<\n", command);
        
        /* Process the CPR data payload */
        if (len > 4) {
            process_cpr_data(&cmd_data[3], len - 3);
        }
    }
    else if (command == CMD_COMMAND_TIMEDATA) {
        LOG_INF("Command: Received Time Data");
        printk("\n>>> iOS SENT TIME DATA COMMAND (0x%02x) <<<\n", command);
        
        /* Process the time data payload */
        if (len > 4) {
            process_time_data(&cmd_data[4], len - 4);
        } else {
            LOG_WRN("Time data command with no payload");
        }
    }
    else {
        LOG_WRN("Unknown command: 0x%02x", command);
        return -EINVAL;
    }
    
    return 0;
}

/* API Implementation */
size_t get_instructor_id(char *buffer, size_t size)
{
    if (!buffer || size == 0) {
        return 0;
    }
    
    size_t id_len = strlen(instructor_id);
    if (id_len == 0) {
        buffer[0] = '\0';
        return 0;
    }
    
    size_t copy_len = (id_len < size - 1) ? id_len : size - 1;
    memcpy(buffer, instructor_id, copy_len);
    buffer[copy_len] = '\0';
    
    return copy_len;
}

size_t get_trainee_id(char *buffer, size_t size)
{
    if (!buffer || size == 0) {
        return 0;
    }
    
    size_t id_len = strlen(trainee_id);
    if (id_len == 0) {
        buffer[0] = '\0';
        return 0;
    }
    
    size_t copy_len = (id_len < size - 1) ? id_len : size - 1;
    memcpy(buffer, trainee_id, copy_len);
    buffer[copy_len] = '\0';
    
    return copy_len;
}

uint8_t get_user_role(void)
{
    return current_user_role;
}

size_t get_time_data(char *buffer, size_t size)
{
    if (!buffer || size == 0 || !has_time_data) {
        return 0;
    }
    
    size_t time_len = strlen(time_data);
    if (time_len == 0) {
        buffer[0] = '\0';
        return 0;
    }
    
    size_t copy_len = (time_len < size - 1) ? time_len : size - 1;
    memcpy(buffer, time_data, copy_len);
    buffer[copy_len] = '\0';
    
    return copy_len;
}

bool has_received_time_data(void)
{
    return has_time_data;
}

/* Helper to add seconds to a time and handle carries */
static void add_seconds_to_time(int *year, int *month, int *day, int *hour, int *min, int *sec, uint32_t seconds)
{
    static const int days_in_month[] = {0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    
    /* Add seconds */
    *sec += seconds;
    
    /* Carry to minutes */
    if (*sec >= 60) {
        *min += *sec / 60;
        *sec %= 60;
    }
    
    /* Carry to hours */
    if (*min >= 60) {
        *hour += *min / 60;
        *min %= 60;
    }
    
    /* Carry to days */
    if (*hour >= 24) {
        *day += *hour / 24;
        *hour %= 24;
    }
    
    /* Handle month changes - simplified for demonstration */
    while (1) {
        /* Check if we need to advance to the next month */
        int max_days = days_in_month[*month];
        
        /* Handle February in leap years */
        if (*month == 2 && (*year % 4 == 0 && (*year % 100 != 0 || *year % 400 == 0))) {
            max_days = 29;
        }
        
        if (*day <= max_days) {
            break;  /* Day is valid for this month */
        }
        
        /* Roll over to next month */
        *day -= max_days;
        *month += 1;
        
        /* Roll over to next year if needed */
        if (*month > 12) {
            *month = 1;
            *year += 1;
        }
    }
}

size_t get_rtc_time(char *buffer, size_t size)
{
    /* If we've received time data, calculate and format current time */
    if (has_time_data && buffer && size >= 20 && rtc_base.base_ticks != 0) {
        /* Calculate elapsed time since base time was set */
        uint32_t current_ticks = k_uptime_get_32();
        uint32_t elapsed_ms = current_ticks - rtc_base.base_ticks;
        uint32_t elapsed_sec = elapsed_ms / 1000;
        
        /* Don't update too frequently to avoid uint32_t overflow */
        if (elapsed_sec < 100000000) {  /* ~3 years */
            /* Start with the base time */
            int year = rtc_base.year;
            int month = rtc_base.month;
            int day = rtc_base.day;
            int hour = rtc_base.hour;
            int min = rtc_base.min;
            int sec = rtc_base.sec;
            
            /* Add elapsed seconds */
            add_seconds_to_time(&year, &month, &day, &hour, &min, &sec, elapsed_sec);
            
            /* Format the calculated time */
            char formatted_time[80]; // Increased substantially to handle any possible output
            snprintf(formatted_time, sizeof(formatted_time),
                    "%04d-%02d-%02d %02d:%02d:%02d",
                    (year > 9999 || year < 0) ? 9999 : year,  // Handle negative values too
                    (month > 99 || month < 0) ? 99 : month,
                    (day > 99 || day < 0) ? 99 : day,
                    (hour > 99 || hour < 0) ? 99 : hour,
                    (min > 99 || min < 0) ? 99 : min,
                    (sec > 99 || sec < 0) ? 99 : sec);
            
            size_t len = strlen(formatted_time);
            size_t copy_len = (len < size - 1) ? len : size - 1;
            
            memcpy(buffer, formatted_time, copy_len);
            buffer[copy_len] = '\0';
            
            return copy_len;
        }
    }
    
    /* Fallback if no time data or invalid base time */
    if (buffer && size >= 20) {
        const char *dummy_time = "2023-01-01 12:00:00";
        size_t len = strlen(dummy_time);
        size_t copy_len = (len < size - 1) ? len : size - 1;
        
        memcpy(buffer, dummy_time, copy_len);
        buffer[copy_len] = '\0';
        
        return copy_len;
    }
    
    return 0;
}

/* Pre-allocated static buffer for queue submissions */
static uint8_t static_submit_buffer[MSG_BUFFER_SIZE];

int submit_command(const uint8_t *cmd_data, uint16_t len)
{
    if (!cmd_data || len == 0 || len > MSG_BUFFER_SIZE - 3) {
        return -EINVAL;
    }
    
    /* Format: [TYPE(1)][LEN_MSB(1)][LEN_LSB(1)][DATA(len)] */
    static_submit_buffer[0] = 1;  /* Type = command buffer */
    static_submit_buffer[1] = (len >> 8) & 0xFF;  /* Length MSB */
    static_submit_buffer[2] = len & 0xFF;        /* Length LSB */
    
    /* Copy command data */
    memcpy(&static_submit_buffer[3], cmd_data, len);
    
    /* Submit to message queue */
    int ret = k_msgq_put(&command_msgq, static_submit_buffer, K_NO_WAIT);
    
    return ret;
}

/* Pre-allocated static buffer for direct commands */
static uint8_t static_direct_buffer[MSG_BUFFER_SIZE];

int submit_direct_command(uint8_t cmd_byte)
{
    /* Format: [TYPE(1)][CMD_BYTE(1)][UNUSED...] */
    static_direct_buffer[0] = 0;  /* Type = direct command */
    static_direct_buffer[1] = cmd_byte;
    
    /* Submit to message queue */
    return k_msgq_put(&command_msgq, static_direct_buffer, K_NO_WAIT);
}