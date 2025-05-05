/**
 * @file message_processor.c
 * @brief Message processing module for BLE commands
 */

#include "message_processor.h"
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

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
        if (k_msgq_get(&command_msgq, cmd_buffer, K_FOREVER) == 0) {
            /* Extract command type and length from buffer */
            uint8_t cmd_type = cmd_buffer[0];
            uint16_t cmd_len = 0;
            
            /* Minimal logging to reduce stack usage */
            LOG_INF("Processing command type: %d", cmd_type);
            
            if (cmd_type == 0) {
                /* Single byte direct command */
                uint8_t cmd_byte = cmd_buffer[1];
                
                ret = process_direct_command(cmd_byte);
                if (ret) {
                    LOG_WRN("Error processing direct command: %d", ret);
                }
            } else {
                /* Full command buffer */
                cmd_len = (cmd_buffer[1] << 8) | cmd_buffer[2];

                if (cmd_len > 3) {
                    LOG_INF("Command: %d", cmd_buffer[3]);
                }
                
                ret = process_command(&cmd_buffer[3], cmd_len);
                if (ret) {
                    LOG_WRN("Error processing command: %d", ret);
                }
            }
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
 * Private helper for processing time data
 * 
 * Optimized version with minimal stack usage and reduced logging
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
    
    /* Single minimal log message */
    LOG_INF("Time data set: %.4s-%.2s-%.2s %.2s:%.2s:%.2s", 
            time_data, time_data + 4, time_data + 6,
            time_data + 8, time_data + 10, time_data + 12);
    
    /* Parse time into system time if needed */
    /* TODO: Future enhancement - convert to system time and sync RTC */
    
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
            request_led_state(true);
            break;
            
        case CMD_COMMAND_STOP:
            LOG_INF("Command: Stop CPR");
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
    LOG_INF("Processing command data with length: %d", len);
    LOG_INF("Start byte: 0x%02x", cmd_data[0]);
    
    /* Simplified logging for safety */
    LOG_INF("First bytes: %02x %02x %02x %02x", 
           (len > 0) ? cmd_data[0] : 0,
           (len > 1) ? cmd_data[1] : 0,
           (len > 2) ? cmd_data[2] : 0,
           (len > 3) ? cmd_data[3] : 0);
           
    /* Check specifically for timedata command with safer bounds checking */
    if (len > 4 && cmd_data[0] == MSG_COMMAND_BYTE_START && 
        cmd_data[2] == MSG_COMMAND_MSG_COLON && cmd_data[3] == CMD_COMMAND_TIMEDATA) {
        LOG_INF("Time data command found");
    }
    
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
            else if (command == CMD_CONTROL_START) {
                LOG_INF("Command: Start CPR");
                request_led_state(true);
            }
            else if (command == CMD_COMMAND_STOP) {
                LOG_INF("Command: Stop CPR");
                request_led_state(false);
            }
            else if (command == CMD_COMMAND_DATA) {
                LOG_INF("Command: Received CPR Init Data");
                
                /* Process the CPR data payload - handling ID strings safely */
                process_cpr_data(&cmd_data[3], data_len);
            }
            else if (command == CMD_COMMAND_TIMEDATA) {
                LOG_INF("Command: Received Time Data");
                
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