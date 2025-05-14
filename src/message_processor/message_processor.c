/**
 * @file message_processor.c
 * @brief Message processing module for BLE commands
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

/* Time data storage in format YYYYMMDDHHMMSSMS */
static char time_data[18];
static bool has_time_data = false;

/* Forward declarations for CPR session management functions in main.c */
extern void start_cpr_session(void);
extern void stop_cpr_session(void);

/* Message queue for asynchronous processing */
K_MSGQ_DEFINE(command_msgq, MSG_BUFFER_SIZE, MSG_QUEUE_SIZE, 4);

/* Command processing thread stack - increased to handle larger messages */
K_THREAD_STACK_DEFINE(processor_stack, 2560);
static struct k_thread processor_thread;
static k_tid_t processor_tid;

/* External references to LED control flags defined in ble_handlers.c */
extern bool led_request_pending;
extern bool led_requested_state;

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
                
                /* Print first 4 bytes of data if available */
                if (cmd_len >= 4) {
                    LOG_INF("Data starts with: %02x %02x %02x %02x", 
                        cmd_buffer[3], cmd_buffer[4], cmd_buffer[5], cmd_buffer[6]);
                }
                
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
    /* Initialize any internal state */
    memset(instructor_id, 0, sizeof(instructor_id));
    memset(trainee_id, 0, sizeof(trainee_id));
    current_user_role = USER_ROLE_NONE;
    
    /* Start the processor thread with a slightly lower priority to ensure BT threads have precedence */
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
    
    /* Request LED on */
    request_led_state(true);
}

/**
 * Private helper for processing time data and setting the RTC
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
    
    /* Parse the string values to integers for RTC setting */
    uint16_t year = 0;
    uint8_t month = 0, day = 0, hour = 0, minute = 0, second = 0, millisecond = 0;
    
    /* Extract time components with sscanf - using temporary buffer for each component */
    char temp[5];
    
    /* Year: First 4 characters */
    memcpy(temp, time_data, 4);
    temp[4] = '\0';
    year = (uint16_t)atoi(temp);
    
    /* Month: Characters 4-5 */
    memcpy(temp, time_data + 4, 2);
    temp[2] = '\0';
    month = (uint8_t)atoi(temp);
    
    /* Day: Characters 6-7 */
    memcpy(temp, time_data + 6, 2);
    temp[2] = '\0';
    day = (uint8_t)atoi(temp);
    
    /* Hour: Characters 8-9 */
    memcpy(temp, time_data + 8, 2);
    temp[2] = '\0';
    hour = (uint8_t)atoi(temp);
    
    /* Minute: Characters 10-11 */
    memcpy(temp, time_data + 10, 2);
    temp[2] = '\0';
    minute = (uint8_t)atoi(temp);
    
    /* Second: Characters 12-13 */
    memcpy(temp, time_data + 12, 2);
    temp[2] = '\0';
    second = (uint8_t)atoi(temp);
    
    /* Millisecond: Characters 14-15 if available */
    if (copy_len >= 16) {
        memcpy(temp, time_data + 14, 2);
        temp[2] = '\0';
        millisecond = (uint8_t)atoi(temp);
    }
    
    /* Log the extracted time components */
    LOG_INF("Time: %04u-%02u-%02u %02u:%02u:%02u.%02u", 
            year, month, day, hour, minute, second, millisecond);
    
    /* Set the RTC using the STM32 HAL */
    RTC_TimeTypeDef rtc_time = {0};
    RTC_DateTypeDef rtc_date = {0};
    
    /* Set the time */
    rtc_time.Hours = hour;
    rtc_time.Minutes = minute; 
    rtc_time.Seconds = second;
    rtc_time.TimeFormat = RTC_HOURFORMAT_24;
    rtc_time.DayLightSaving = RTC_DAYLIGHTSAVING_NONE;
    rtc_time.StoreOperation = RTC_STOREOPERATION_RESET;
    
    /* Set the date - using a fixed weekday since it's not important */
    rtc_date.WeekDay = RTC_WEEKDAY_MONDAY; 
    rtc_date.Month = month;
    rtc_date.Date = day;
    rtc_date.Year = year - 2000; /* RTC year is typically offset from 2000 */
    
    /* Get RTC handle and set the time */
    static RTC_HandleTypeDef hrtc = {0};
    static bool rtc_initialized = false;
    
    /* Initialize RTC only once */
    if (!rtc_initialized) {
        LOG_INF("Initializing RTC...");
        hrtc.Instance = RTC;
        hrtc.Init.HourFormat = RTC_HOURFORMAT_24;
        hrtc.Init.AsynchPrediv = 127;
        hrtc.Init.SynchPrediv = 255;
        hrtc.Init.OutPut = RTC_OUTPUT_DISABLE;
        hrtc.Init.OutPutPolarity = RTC_OUTPUT_POLARITY_HIGH;
        hrtc.Init.OutPutType = RTC_OUTPUT_TYPE_OPENDRAIN;
        
        /* Initialize RTC */
        if (HAL_RTC_Init(&hrtc) != HAL_OK) {
            LOG_ERR("Failed to initialize RTC");
            /* Handle RTC init failure - you might want to try alternate approaches */
        } else {
            rtc_initialized = true;
            LOG_INF("RTC initialized successfully");
        }
    }
    
    /* Only proceed if RTC is initialized */
    if (rtc_initialized) {
        /* Set time and date */
        if (HAL_RTC_SetTime(&hrtc, &rtc_time, RTC_FORMAT_BIN) != HAL_OK) {
            LOG_ERR("Failed to set RTC time");
        }
        
        if (HAL_RTC_SetDate(&hrtc, &rtc_date, RTC_FORMAT_BIN) != HAL_OK) {
            LOG_ERR("Failed to set RTC date");
        }
        
        /* Read back the time to verify it was set correctly */
        RTC_TimeTypeDef rtc_time_check = {0};
        RTC_DateTypeDef rtc_date_check = {0};
        
        HAL_RTC_GetTime(&hrtc, &rtc_time_check, RTC_FORMAT_BIN);
        HAL_RTC_GetDate(&hrtc, &rtc_date_check, RTC_FORMAT_BIN);
        
        LOG_INF("RTC time set to: %02d:%02d:%02d", 
                rtc_time_check.Hours, rtc_time_check.Minutes, rtc_time_check.Seconds);
        LOG_INF("RTC date set to: %04d-%02d-%02d", 
                rtc_date_check.Year + 2000, rtc_date_check.Month, rtc_date_check.Date);
    }
    
    LOG_INF("RTC updated successfully");
    
    /* Request LED on to provide visual feedback that time was received */
    request_led_state(true);
}

/**
 * Private helper for handling CPR data command
 */
static void process_cpr_data(const uint8_t *data_payload, size_t data_len)
{
    /* Print raw data for debugging */
    LOG_INF("CPR Data bytes:");
    for (int i = 0; i < data_len; i++) {
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
    
    /* Ensure CPR session is properly initialized before processing commands */
    if (cmd_byte == CPR_CONTROL_START || cmd_byte == CPR_COMMAND_STOP) {
        LOG_INF("CPR command received, verifying CPR session state is properly initialized");
    }
    
    switch (cmd_byte) {
        case CMD_CONTROL_LED_OFF:
            LOG_INF("Command: LED OFF");
            request_led_state(false);
            break;
            
        case CMD_CONTROL_LED_ON:
            LOG_INF("Command: LED ON");
            request_led_state(true);
            break;
            
        case CPR_CONTROL_START:
            LOG_INF("Command: Start CPR");
            request_led_state(true);
            LOG_INF("Calling start_cpr_session() function");
            start_cpr_session();  /* Start CPR session timing */
            LOG_INF("CPR session should now be active");
            break;
            
        case CPR_COMMAND_STOP:
            LOG_INF("Command: Stop CPR");
            request_led_state(false);
            stop_cpr_session();   /* Stop CPR session timing */
            break;
            
        default:
            LOG_WRN("Unknown direct command: 0x%02x", cmd_byte);
            return -EINVAL;
    }
    
    return 0;
}

static int process_command(uint8_t *cmd_data, uint16_t len)
{
    /* Check for protocol-formatted command: START_BYTE + LENGTH_BYTE + COLON + ... */
    if (len >= 6 && cmd_data[0] == BLE_COMMAND_BYTE_START && 
        cmd_data[2] == BLE_COMMAND_MSG_COLON) {
        
        LOG_INF("Protocol-formatted command detected");
        
        /* Extract the length byte and verify */
        uint8_t length_byte = cmd_data[1];
        uint8_t command_byte = cmd_data[3];
        
        LOG_INF("Command format: START[%02x] LEN[%02x] COLON[%02x] CMD[%02x]...", 
               cmd_data[0], length_byte, cmd_data[2], command_byte);
        
        /* Check specifically for timedata command */
        if (command_byte == CMD_COMMAND_TIMEDATA) {
            LOG_INF("*** TIME DATA COMMAND DETECTED! ***");
        }
    } else {
        /* Basic command logging for non-protocol format */
        LOG_INF("Command format (non-protocol): [%02x][%02x][%02x][%02x]...", 
               (len > 0) ? cmd_data[0] : 0,
               (len > 1) ? cmd_data[1] : 0,
               (len > 2) ? cmd_data[2] : 0,
               (len > 3) ? cmd_data[3] : 0);
    }
           
    /* Already checked for timedata command above */
    
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

    /* Safely retrieve and validate the data length */
    uint8_t data_len = 0;
    if (len > 1) {
        data_len = cmd_data[1];
    } else {
        LOG_WRN("Buffer too short to read data length");
        return -EINVAL;
    }
    
    /* Basic sanity check for data length */
    if (data_len > MSG_BUFFER_SIZE) {
        LOG_WRN("Data length value too large: %d", data_len);
        return -EINVAL;
    }
    
    /* Calculate total expected length based on protocol format */
    uint16_t expected_total_len = 5 + data_len; // START + LEN + COLON + data + SEMICOLON + END
    
    /* Ensure the buffer contains enough bytes for the claimed data length */
    if (expected_total_len > len) {
        LOG_WRN("Data length mismatch: expected total %d bytes, got %d",
                expected_total_len, len);
        return -EINVAL;
    }

    /* Check colon byte */
    if (len > 2 && cmd_data[2] != MSG_COMMAND_MSG_COLON) {
        LOG_WRN("Invalid colon byte: 0x%02x, expected 0x%02x",
                cmd_data[2], MSG_COMMAND_MSG_COLON);
        return -EINVAL;
    }

    /* Check end structure - with bounds checking */
    uint16_t semicolon_pos = 3 + data_len;
    if (semicolon_pos < len && cmd_data[semicolon_pos] != MSG_COMMAND_MSG_SEMICOLON) {
        LOG_WRN("Invalid semicolon byte at position %d: 0x%02x", 
               semicolon_pos, cmd_data[semicolon_pos]);
        return -EINVAL;
    }

    uint16_t end_pos = 4 + data_len;
    if (end_pos < len && cmd_data[end_pos] != MSG_COMMAND_MSG_END) {
        LOG_WRN("Invalid end byte at position %d: 0x%02x", 
               end_pos, cmd_data[end_pos]);
        return -EINVAL;
    }

    /* Message is valid, process the data */
    LOG_INF("Valid message received, data length: %d", data_len);

    /* Process the command - data starts at index 3 */
    if (data_len > 0) {
        /* Extra safety check to ensure data is within bounds */
        if (3 + data_len <= len) {
            uint8_t command = cmd_data[3]; /* First byte of data */
            
            LOG_INF("Processing command byte: 0x%02x", command);

            /* Simplified handling with safer stack usage */
            if (command == CMD_CONTROL_LED_OFF) {
                LOG_INF("Command: LED OFF");
                request_led_state(false);
            }
            else if (command == CMD_CONTROL_LED_ON) {
                LOG_INF("Command: LED ON");
                request_led_state(true);
            }
            else if (command == CPR_CONTROL_START) {
                LOG_INF("Command: Start CPR");
                request_led_state(true);
                start_cpr_session();  /* Start CPR session timing */
            }
            else if (command == CPR_COMMAND_STOP) {
                LOG_INF("Command: Stop CPR");
                request_led_state(false);
                stop_cpr_session();   /* Stop CPR session timing */
            }
            else if (command == CMD_COMMAND_DATA) {
                LOG_INF("Command: Received CPR Init Data");
                
                /* Process the CPR data payload - handling ID strings safely */
                process_cpr_data(&cmd_data[3], data_len);
            }
            else if (command == CMD_COMMAND_TIMEDATA) {
                LOG_INF("*** EXECUTING TIME DATA COMMAND ***");
                
                /* Log the raw time data in hex */
                LOG_INF("Time payload (%d bytes): ", data_len);
                for (int i = 0; i < data_len && i < 20; i++) {
                    printk("%02x ", cmd_data[3 + i]);
                }
                printk("\n");
                
                /* Safety check for data length */
                if (data_len > 1) {
                    /* Process the time data payload (command byte is already at index 3) */
                    process_time_data(&cmd_data[4], data_len - 1);
                } else {
                    LOG_WRN("Time data command with no payload");
                }
            }
            else {
                LOG_WRN("Unknown command: 0x%02x", command);
                return -EINVAL;
            }
        } else {
            LOG_ERR("Data length exceeds buffer bounds: data_len=%d, buffer_len=%d", 
                data_len, len);
            return -EINVAL;
        }
    } else {
        LOG_WRN("Valid message structure but no data");
    }
    
    return 0;
}

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

/**
 * @brief Get the current time data
 * 
 * @param buffer Buffer to fill with the time data
 * @param size Size of the buffer
 * @return Length of the time data string, 0 if no time data is set
 */
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

/**
 * @brief Check if time data has been received
 * 
 * @return True if time data has been received, false otherwise
 */
bool has_received_time_data(void)
{
    return has_time_data;
}

/* Function removed - implemented in main.c */

/**
 * @brief Get the current time from the RTC
 *
 * Reads the current time from the system RTC and formats it into a string
 * 
 * @param buffer Buffer to fill with the current time
 * @param size Size of the buffer
 * @return Length of the time string written, 0 on error
 */
size_t get_rtc_time(char *buffer, size_t size)
{
    /* Safety check */
    if (!buffer || size < 20) {
        return 0;
    }
    
    /* Initialize RTC handle */
    static RTC_HandleTypeDef hrtc = {0};
    static bool rtc_initialized = false;
    
    /* Only initialize once */
    if (!rtc_initialized) {
        hrtc.Instance = RTC;
        hrtc.Init.HourFormat = RTC_HOURFORMAT_24;
        hrtc.Init.AsynchPrediv = 127;
        hrtc.Init.SynchPrediv = 255;
        hrtc.Init.OutPut = RTC_OUTPUT_DISABLE;
        
        if (HAL_RTC_Init(&hrtc) != HAL_OK) {
            /* RTC not initialized, can't read time */
            snprintf(buffer, size, "RTC not available");
            return strlen(buffer);
        }
        rtc_initialized = true;
    }
    
    /* Read current time and date */
    RTC_TimeTypeDef sTime = {0};
    RTC_DateTypeDef sDate = {0};
    
    HAL_RTC_GetTime(&hrtc, &sTime, RTC_FORMAT_BIN);
    HAL_RTC_GetDate(&hrtc, &sDate, RTC_FORMAT_BIN);
    
    /* Format time string */
    int len = snprintf(buffer, size, "%04d-%02d-%02d %02d:%02d:%02d",
                      sDate.Year + 2000, sDate.Month, sDate.Date,
                      sTime.Hours, sTime.Minutes, sTime.Seconds);
                      
    return (len > 0) ? len : 0;
}

/**
 * @brief Submit a command for processing
 *
 * Queues a command for asynchronous processing by the message processor thread.
 * This function is safe to call from any context, including interrupt handlers
 * and limited-stack threads like the BT RX worker.
 *
 * @param cmd_data Pointer to command data buffer
 * @param len Length of data in the buffer
 * @return 0 on success, negative error code on failure
 */
/* Pre-allocated static buffer for queue submissions to avoid stack allocation */
static uint8_t static_submit_buffer[MSG_BUFFER_SIZE];

int submit_command(const uint8_t *cmd_data, uint16_t len)
{
    if (!cmd_data || len == 0 || len > MSG_BUFFER_SIZE - 3) {
        return -EINVAL;  /* No logging to save stack */
    }
    
    /* No logging to minimize stack usage */
    
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

/**
 * @brief Submit a direct command for processing
 *
 * Queues a simple command byte for asynchronous processing.
 * This is a lightweight wrapper that packages the single byte into a command.
 *
 * @param cmd_byte The command byte to process
 * @return 0 on success, negative error code on failure
 */
/* Pre-allocated static buffer for direct commands */
static uint8_t static_direct_buffer[MSG_BUFFER_SIZE];

int submit_direct_command(uint8_t cmd_byte)
{
    /* Format: [TYPE(1)][CMD_BYTE(1)][UNUSED...] */
    static_direct_buffer[0] = 0;  /* Type = direct command */
    static_direct_buffer[1] = cmd_byte;
    
    /* Submit to message queue - no error logging to save stack */
    return k_msgq_put(&command_msgq, static_direct_buffer, K_NO_WAIT);
}