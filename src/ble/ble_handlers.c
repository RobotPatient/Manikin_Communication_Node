#include "ble_handlers.h"
#include <zephyr/drivers/can.h>

LOG_MODULE_REGISTER(ble_handlers);

uint8_t ble_cmd_buffer[BLE_BUFFER_SIZE];

/* LED control flags - for safe LED control from BLE thread */
bool led_request_pending = false;
bool led_requested_state = false;

/* Define a work item for delayed advertising restart */
static struct k_work_delayable adv_work;

/* Button value. */
static uint16_t but_val;

/* User identification data */
static char instructor_id[BLE_BUFFER_SIZE/2];
static char trainee_id[BLE_BUFFER_SIZE/2];
static uint8_t current_user_role;

/* Command buffer for receiving data from iOS */
static uint8_t ios_cmd_buffer[20];

/* Prototypes */
static ssize_t recv(struct bt_conn *conn,
					const struct bt_gatt_attr *attr, const void *buf,
					uint16_t len, uint16_t offset, uint8_t flags);
                    
static ssize_t ios_cmd_recv(struct bt_conn *conn,
					const struct bt_gatt_attr *attr, const void *buf,
					uint16_t len, uint16_t offset, uint8_t flags);

/* ST Custom Service */
static const struct bt_uuid_128 st_service_uuid = BT_UUID_INIT_128(
	BT_UUID_128_ENCODE(0x0000fe40, 0xcc7a, 0x482a, 0x984a, 0x7f2ed5b3e58f));

/* ST LED service */
static const struct bt_uuid_128 led_char_uuid = BT_UUID_INIT_128(
	BT_UUID_128_ENCODE(0x0000fe41, 0x8e22, 0x4541, 0x9d4c, 0x21edae82ed19));

/* ST Notify button service */
static const struct bt_uuid_128 but_notif_uuid = BT_UUID_INIT_128(
	BT_UUID_128_ENCODE(0x0000fe42, 0x8e22, 0x4541, 0x9d4c, 0x21edae82ed19));

/* Data stream characteristic UUID */
static const struct bt_uuid_128 data_stream_char_uuid = BT_UUID_INIT_128(
	BT_UUID_128_ENCODE(0x0000fe43, 0x8e22, 0x4541, 0x9d4c, 0x21edae82ed19));

/* iOS command characteristic UUID */
static const struct bt_uuid_128 ios_cmd_char_uuid = BT_UUID_INIT_128(
	BT_UUID_128_ENCODE(0x0000fe44, 0x8e22, 0x4541, 0x9d4c, 0x21edae82ed19));

#define DEVICE_NAME CONFIG_BT_DEVICE_NAME
#define DEVICE_NAME_LEN (sizeof(DEVICE_NAME) - 1)
#define ADV_LEN 12

/* Advertising data */
static uint8_t manuf_data[ADV_LEN] = {
	0x01 /*SKD version */,
	0x83 /* STM32WB - P2P Server 1 */,
	0x00 /* GROUP A Feature  */,
	0x00 /* GROUP A Feature */,
	0x00 /* GROUP B Feature */,
	0x00 /* GROUP B Feature */,
	0x00, /* BLE MAC start -MSB */
	0x00,
	0x00,
	0x00,
	0x00,
	0x00, /* BLE MAC stop */
};

/* Buffer to hold our streaming data */
static uint8_t stream_data[20];
/* Data stream notification state */
static volatile bool stream_notify_enable;

/* Forward declaration of timer handler */
static void ble_tx_timer_handler(struct k_timer *timer);

/* Timer for periodic BLE transmission of CAN data */
K_TIMER_DEFINE(ble_tx_timer, ble_tx_timer_handler, NULL);

/* Mutex to protect the buffer during concurrent access */
K_MUTEX_DEFINE(can_data_mutex);

static const struct bt_data ad[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
	BT_DATA(BT_DATA_NAME_COMPLETE, DEVICE_NAME, DEVICE_NAME_LEN),
	BT_DATA(BT_DATA_MANUFACTURER_DATA, manuf_data, ADV_LEN)};

/* BLE connection */
struct bt_conn *ble_conn;
/* Button notification state */
volatile bool notify_enable;

void mpu_ccc_cfg_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
	ARG_UNUSED(attr);
	notify_enable = (value == BT_GATT_CCC_NOTIFY);
	LOG_INF("Button notification %s", notify_enable ? "enabled" : "disabled");
}

/* Data stream CCC configuration callback */
void stream_ccc_cfg_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
	ARG_UNUSED(attr);
	stream_notify_enable = (value == BT_GATT_CCC_NOTIFY);
	LOG_INF("Data stream notifications %s", stream_notify_enable ? "enabled" : "disabled");
	
	if (stream_notify_enable) {
		LOG_INF("Starting CAN data streaming");
		k_timer_start(&ble_tx_timer, K_NO_WAIT, K_MSEC(50));  /* 20Hz transmission rate */
	} else {
		LOG_INF("Stopping CAN data streaming");
		k_timer_stop(&ble_tx_timer);
	}
}

/* The embedded board is acting as GATT server.
 * The ST BLE Android app is the BLE GATT client.
 */

/* ST BLE Sensor GATT services and characteristic */
BT_GATT_SERVICE_DEFINE(stsensor_svc,
					   BT_GATT_PRIMARY_SERVICE(&st_service_uuid),
					   BT_GATT_CHARACTERISTIC(&led_char_uuid.uuid,
											  BT_GATT_CHRC_READ | BT_GATT_CHRC_WRITE_WITHOUT_RESP,
											  BT_GATT_PERM_WRITE, NULL, recv, (void *)1),
					   BT_GATT_CHARACTERISTIC(&but_notif_uuid.uuid, BT_GATT_CHRC_NOTIFY,
											  BT_GATT_PERM_READ, NULL, NULL, &but_val),
					   BT_GATT_CCC(mpu_ccc_cfg_changed, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
					   
					   /* CAN data streaming characteristic */
					   BT_GATT_CHARACTERISTIC(&data_stream_char_uuid.uuid, BT_GATT_CHRC_NOTIFY,
											  BT_GATT_PERM_READ, NULL, NULL, &stream_data),
					   BT_GATT_CCC(stream_ccc_cfg_changed, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
                       
                       /* iOS command characteristic */
                       BT_GATT_CHARACTERISTIC(&ios_cmd_char_uuid.uuid,
                                              BT_GATT_CHRC_WRITE | BT_GATT_CHRC_WRITE_WITHOUT_RESP,
                                              BT_GATT_PERM_WRITE, NULL, ios_cmd_recv, ios_cmd_buffer),
					  );

ssize_t recv(struct bt_conn *conn,
const struct bt_gatt_attr *attr, const void *buf,
uint16_t len, uint16_t offset, uint8_t flags)
{
	/* Check if we have a valid buffer */
	if (!buf || len == 0) {
		return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
	}

	/* Prevent buffer overflow */
	if (offset + len > BLE_BUFFER_SIZE) {
		return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
	}

	/* Copy the data to our buffer */
	memcpy(ble_cmd_buffer + offset, buf, len);

	LOG_INF("Received data, %d bytes", len);

	/* Check if this is a single-byte command (for backward compatibility) */
	if (len == 1) {
	uint8_t cmd = ble_cmd_buffer[0];
	LOG_INF("Single-byte command: 0x%02x", cmd);

	/* Handle simple command */
	switch (cmd) {
		case CPR_CONTROL_LED_OFF:
			LOG_INF("Command: LED OFF");
			led_off();
			break;

		case CPR_CONTROL_LED_ON:
			LOG_INF("Command: LED ON");
			led_on();
			break;

		case CPR_CONTROL_START:
			LOG_INF("Command: Start CPR");
			/* Add CPR start implementation here */
			break;

		case CPR_COMMAND_STOP:
			LOG_INF("Command: Stop CPR");
			/* Add CPR stop implementation here */
			break;

		default:
			LOG_WRN("Unknown single-byte command: 0x%02x", cmd);
			break;
		}

		return len;
	}

	/* For multi-byte commands, try to process using the structured protocol */
	process_ble_command(ble_cmd_buffer, len);

	return len;
}

/**
 * detect_and_print_user_role() 
 *   scans cmd_data[3..] for the "in:" or "tr:" prefix,
 *   extracts the rest as the user‐ID,
 *   and prints both role and ID.
 * 
 * @param cmd_data: full buffer (header + payload)
 * @param data_len: length of payload starting at cmd_data[3]
 * @param len:      total length of cmd_data[]
 * @return USER_ROLE_INSTRUCTOR or USER_ROLE_TRAINEE on success, 0 on failure
 */
int detect_and_print_user_role(const uint8_t *cmd_data, size_t data_len, size_t len)
{
    return 0;
	/* 1) Print raw payload in hex */

    LOG_INF("CPR payload (hex):");
    for (size_t i = 0; i < data_len && (i + 3) < len; i++) {
        printk("0x%02x ", cmd_data[3 + i]);
    }
    printk("\n");

    /* Check if we actually have data to process */
    if (data_len == 0 || len <= 3) {
        LOG_WRN("Payload too short to analyze");
        return 0;
    }

    /* 2) Point to ASCII payload and cap its length */
    const char *payload = (const char *)&cmd_data[3];
    size_t payload_len = data_len;
    if (payload_len > len - 3)
        payload_len = len - 3;

    /* 3) Check prefix */
    int role = 0;
    size_t prefix_len = 0;
    size_t instr_prefix_len = strlen(USER_ROLE_INSTRUCTOR_PREFIX);
    size_t train_prefix_len = strlen(USER_ROLE_TRAINEE_PREFIX);
    
    if (payload_len >= instr_prefix_len &&
        strncmp(payload, USER_ROLE_INSTRUCTOR_PREFIX, instr_prefix_len) == 0) {
        role = USER_ROLE_INSTRUCTOR;
        prefix_len = instr_prefix_len;
    }
    else if (payload_len >= train_prefix_len &&
             strncmp(payload, USER_ROLE_TRAINEE_PREFIX, train_prefix_len) == 0) {
        role = USER_ROLE_TRAINEE;
        prefix_len = train_prefix_len;
    }
    else {
        LOG_WRN("Unknown role prefix in payload");
        return 0;
    }

    /* Ensure we have an ID component */
    if (payload_len <= prefix_len) {
        LOG_WRN("Role prefix found but no ID present");
        return role;
    }

    /* 4) Extract ID string */
    size_t id_len = payload_len - prefix_len;
    if (id_len > 15) id_len = 15;  // avoid large strings in logging

    char user_id[16];  // Smaller buffer
    if (id_len > 0) {
        memcpy(user_id, payload + prefix_len, id_len);
        user_id[id_len] = '\0';
    } else {
        user_id[0] = '\0';
    }

    /* 5) Log result */
    if (role == USER_ROLE_INSTRUCTOR) {
        LOG_INF("Detected role: INSTRUCTOR, ID=\"%s\"", user_id);
    } else {
        LOG_INF("Detected role: TRAINEE, ID=\"%s\"", user_id);
    }

    /* 6) Store the ID in the appropriate buffer */
    if (role == USER_ROLE_INSTRUCTOR) {
        memset(instructor_id, 0, sizeof(instructor_id));
        strncpy(instructor_id, user_id, sizeof(instructor_id) - 1);
        instructor_id[sizeof(instructor_id) - 1] = '\0';
        current_user_role = USER_ROLE_INSTRUCTOR;
    } else {
        memset(trainee_id, 0, sizeof(trainee_id));
        strncpy(trainee_id, user_id, sizeof(trainee_id) - 1);
        trainee_id[sizeof(trainee_id) - 1] = '\0';
        current_user_role = USER_ROLE_TRAINEE;
    }

    return role;
}

/* Process commands received via BLE */
void process_ble_command(uint8_t *cmd_data, uint16_t len)
{
	/* If not an ID message, proceed with structured protocol processing */
	/* Validate protocol structure:
	 * [START][LENGTH][COLON][DATA...][SEMICOLON][END]
	 * Minimum size: 5 bytes (START, LENGTH, COLON, SEMICOLON, END with no data)
	 */
	if (len < 5) {
		LOG_WRN("Command too short, %d bytes", len);
		return;
	}

	/* Check start byte */
	if (cmd_data[0] != BLE_COMMAND_BYTE_START) {
		LOG_WRN("Invalid start byte: 0x%02x, expected 0x%02x",
				cmd_data[0], BLE_COMMAND_BYTE_START);
		return;
	}

	/* Safely retrieve and validate the data length */
	uint8_t data_len = 0;
	if (len > 1) {
		data_len = cmd_data[1];
	} else {
		LOG_WRN("Buffer too short to read data length");
		return;
	}
	
	/* Basic sanity check for data length */
	if (data_len > BLE_BUFFER_SIZE) {
		LOG_WRN("Data length value too large: %d", data_len);
		return;
	}
	
	/* Calculate total expected length based on protocol format */
	uint16_t expected_total_len = 5 + data_len; // START + LEN + COLON + data + SEMICOLON + END
	
	/* Ensure the buffer contains enough bytes for the claimed data length */
	if (expected_total_len > len) {
		LOG_WRN("Data length mismatch: expected total %d bytes, got %d",
				expected_total_len, len);
		return;
	}

	/* Check colon byte */
	if (len > 2 && cmd_data[2] != BLE_COMMAND_MSG_COLON) {
		LOG_WRN("Invalid colon byte: 0x%02x, expected 0x%02x",
				cmd_data[2], BLE_COMMAND_MSG_COLON);
		return;
	}

	/* Check end structure - with bounds checking */
	uint16_t semicolon_pos = 3 + data_len;
	if (semicolon_pos < len && cmd_data[semicolon_pos] != BLE_COMMAND_MSG_SEMICOLON) {
		LOG_WRN("Invalid semicolon byte at position %d: 0x%02x", 
			   semicolon_pos, cmd_data[semicolon_pos]);
		return;
	}

	uint16_t end_pos = 4 + data_len;
	if (end_pos < len && cmd_data[end_pos] != BLE_COMMAND_MSG_END) {
		LOG_WRN("Invalid end byte at position %d: 0x%02x", 
			   end_pos, cmd_data[end_pos]);
		return;
	}

	/* Message is valid, process the data */
	LOG_INF("Valid message received, data length: %d", data_len);

	/* Process the command - data starts at index 3 */
	if (data_len > 0) {
		/* Extra safety check to ensure data is within bounds */
		if (3 + data_len <= len) {
			uint8_t command = cmd_data[3]; /* First byte of data */
			
			LOG_INF("Processing command byte: 0x%02x", command);

			/* Extremely simplified handling to avoid stack usage */
			if (command == CPR_CONTROL_LED_OFF) {
				LOG_INF("Command: LED OFF");
				/* Request LED off via flags instead of direct call */
				led_requested_state = false;
				led_request_pending = true;
			}
			else if (command == CPR_CONTROL_LED_ON) {
				LOG_INF("Command: LED ON");
				/* Request LED on via flags instead of direct call */
				led_requested_state = true;
				led_request_pending = true;
			}
			else if (command == CPR_CONTROL_START) {
				LOG_INF("Command: Start CPR");
				/* Request LED on via flags instead of direct call */
				led_requested_state = true;
				led_request_pending = true;
			}
			else if (command == CPR_COMMAND_STOP) {
				LOG_INF("Command: Stop CPR");
				/* Request LED off via flags instead of direct call */
				led_requested_state = false;
				led_request_pending = true;
			}
			else if (command == CPR_COMMAND_DATA) {
				LOG_INF("Command: Received CPR Init Data");
				/* Special handling for CPR data - just turn on LED for now */
				//led_on();
				
				/* If data starts with in: or tr: - set the role but don't try to extract the whole ID */

				
				if (data_len >= 4 && cmd_data[4] == 'i' && cmd_data[5] == 'n' && cmd_data[6] == ':') {
					current_user_role = USER_ROLE_INSTRUCTOR;
					LOG_INF("Detected instructor role");
				}
				else if (data_len >= 4 && cmd_data[4] == 't' && cmd_data[5] == 'r' && cmd_data[6] == ':') {
					current_user_role = USER_ROLE_TRAINEE;
					LOG_INF("Detected trainee role");
				}
			
			}
			else {
				LOG_WRN("Unknown command: 0x%02x", command);
			}
		} else {
			LOG_ERR("Data length exceeds buffer bounds: data_len=%d, buffer_len=%d", 
				data_len, len);
		}
	} else {
		LOG_WRN("Valid message structure but no data");
	}
}

/* Handler for iOS app commands */
static ssize_t ios_cmd_recv(struct bt_conn *conn,
					const struct bt_gatt_attr *attr, const void *buf,
					uint16_t len, uint16_t offset, uint8_t flags)
{
    /* Check if we have a valid buffer */
    if (!buf || len == 0) {
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
    }
    
    /* Prevent buffer overflow */
    if (offset + len > sizeof(ios_cmd_buffer)) {
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
    }
    
    /* Copy the data to our buffer */
    memcpy(ios_cmd_buffer + offset, buf, len);
    
    LOG_INF("Received command from iOS app, %d bytes", len);
    
    /* Process the command */
    process_ios_command(ios_cmd_buffer, len);
    
    return len;
}

/* Process commands received from the iOS app */
void process_ios_command(uint8_t *cmd_data, uint16_t len)
{
    /* Check if this is a text-based ID message */
    if (len >= 3) {
        /* Check for instructor ID prefix (without using large buffer) */
        size_t instr_prefix_len = strlen(USER_ROLE_INSTRUCTOR_PREFIX);
        if (len > instr_prefix_len && 
            memcmp(cmd_data, USER_ROLE_INSTRUCTOR_PREFIX, instr_prefix_len) == 0) {
            
            /* Calculate available ID length (with bounds check) */
            size_t max_id_len = sizeof(instructor_id) - 1;
            size_t avail_data = len - instr_prefix_len;
            size_t id_len = (avail_data < max_id_len) ? avail_data : max_id_len;
            
            if (id_len > 0) {
                /* Safely copy ID without large buffer */
                memset(instructor_id, 0, sizeof(instructor_id));
                memcpy(instructor_id, cmd_data + instr_prefix_len, id_len);
                instructor_id[id_len] = '\0';  /* Ensure null-termination */
                current_user_role = USER_ROLE_INSTRUCTOR;
                
                LOG_INF("Set instructor ID: %s", instructor_id);
                /* Request LED on via flags instead of direct call */
                led_requested_state = true;
                led_request_pending = true;
                return;
            }
        }
        
        /* Check for trainee ID prefix (without using large buffer) */
        size_t train_prefix_len = strlen(USER_ROLE_TRAINEE_PREFIX);
        if (len > train_prefix_len && 
            memcmp(cmd_data, USER_ROLE_TRAINEE_PREFIX, train_prefix_len) == 0) {
            
            /* Calculate available ID length (with bounds check) */
            size_t max_id_len = sizeof(trainee_id) - 1;
            size_t avail_data = len - train_prefix_len;
            size_t id_len = (avail_data < max_id_len) ? avail_data : max_id_len;
            
            if (id_len > 0) {
                /* Safely copy ID without large buffer */
                memset(trainee_id, 0, sizeof(trainee_id));
                memcpy(trainee_id, cmd_data + train_prefix_len, id_len);
                trainee_id[id_len] = '\0';  /* Ensure null-termination */
                current_user_role = USER_ROLE_TRAINEE;
                
                LOG_INF("Set trainee ID: %s", trainee_id);
                /* Request LED on via flags instead of direct call */
                led_requested_state = true;
                led_request_pending = true;
                return;
            }
        }
    }

    /* Simple command processor - adjust based on your protocol */
    if (len < 1) {
        LOG_WRN("Command too short");
        return;
    }
    
    uint8_t cmd_type = cmd_data[0];
    
    switch (cmd_type) {
        case 0x01:  /* Example: Start streaming command */
            LOG_INF("Command: Start streaming");
            if (!stream_notify_enable) {
                /* Only start if not already streaming */
                stream_notify_enable = true;
                k_timer_start(&ble_tx_timer, K_NO_WAIT, K_MSEC(50));
            }
            break;
            
        case 0x02:  /* Example: Stop streaming command */
            LOG_INF("Command: Stop streaming");
            if (stream_notify_enable) {
                stream_notify_enable = false;
                k_timer_stop(&ble_tx_timer);
            }
            break;
            
        case 0x03:  /* Example: Send CAN message */
            if (len >= 6) {  /* Minimum: cmd(1) + can_id(4) + len(1) */
                uint32_t can_id = 
                    ((uint32_t)cmd_data[1] << 24) |
                    ((uint32_t)cmd_data[2] << 16) |
                    ((uint32_t)cmd_data[3] << 8) |
                    (uint32_t)cmd_data[4];
                    
                uint8_t data_len = cmd_data[5];
                
                /* Validate CAN data length */
                if (len >= 6 + data_len && data_len <= 8) {
                    LOG_INF("Command: Send CAN message, ID: 0x%08x, len: %d", can_id, data_len);
                    
                    /* Send CAN message */
                    send_can_message(can_id, &cmd_data[6], data_len);
                } else {
                    LOG_WRN("Invalid CAN message length");
                }
            } else {
                LOG_WRN("Command too short for CAN message");
            }
            break;
            
        default:
            LOG_WRN("Unknown command: 0x%02x", cmd_type);
            break;
    }
}

/* Function to send a CAN message */
void send_can_message(uint32_t can_id, uint8_t *data, uint8_t len)
{
    LOG_INF("DUMMY CAN TX - ID: 0x%08x, Length: %d", can_id, len);
    
    /* Print the data bytes if available */
    if (data != NULL && len > 0) {
        LOG_INF("DUMMY CAN TX - Data:");
        for (int i = 0; i < len && i < 8; i++) {
            printk("0x%02x ", data[i]);
        }
        printk("\n");
    }
    
    /* Just a placeholder for now */
    /* We'll implement actual CAN transmission later */
}

/* Circular buffer for CAN data */
#define CAN_BUFFER_SIZE 10  /* Number of CAN frames to buffer */
static struct {
	uint8_t data[8];  /* CAN data (adjust size as needed) */
	uint8_t len;      /* Data length */
	uint32_t id;      /* CAN ID */
} can_circular_buffer[CAN_BUFFER_SIZE];

static volatile uint8_t can_buffer_head = 0;
static volatile uint8_t can_buffer_tail = 0;
static volatile bool can_buffer_full = false;

/* Add CAN frame to circular buffer */
void can_buffer_add(const void *frame)
{
	k_mutex_lock(&can_data_mutex, K_FOREVER);
	
#ifdef CONFIG_CAN_API_USE_ZCAN
	/* For older Zephyr versions using zcan API */
	const struct zcan_frame *zframe = (const struct zcan_frame *)frame;
	memcpy(can_circular_buffer[can_buffer_head].data, zframe->data, zframe->dlc);
	can_circular_buffer[can_buffer_head].len = zframe->dlc;
	can_circular_buffer[can_buffer_head].id = zframe->id;
#else
	/* For newer Zephyr CAN API */
	const struct can_frame *cframe = (const struct can_frame *)frame;
	memcpy(can_circular_buffer[can_buffer_head].data, cframe->data, cframe->dlc);
	can_circular_buffer[can_buffer_head].len = cframe->dlc;
	can_circular_buffer[can_buffer_head].id = cframe->id;
#endif
	
	/* Update head pointer */
	can_buffer_head = (can_buffer_head + 1) % CAN_BUFFER_SIZE;
	
	/* Check if buffer is full */
	if (can_buffer_head == can_buffer_tail) {
		can_buffer_full = true;
	}
	
	k_mutex_unlock(&can_data_mutex);
}

/* Timer handler to send buffered CAN data over BLE */
static void ble_tx_timer_handler(struct k_timer *timer)
{
	/* Only process if BLE is connected and notifications are enabled */
	if (!ble_conn || !stream_notify_enable) {
		return;
	}
	
	k_mutex_lock(&can_data_mutex, K_FOREVER);
	
	/* Check if there's data to send */
	if (can_buffer_head != can_buffer_tail || can_buffer_full) {
		/* Format data for BLE transmission */
		uint8_t index = 0;
		
		/* For example, include one CAN frame per BLE packet */
		if (can_buffer_tail != can_buffer_head || can_buffer_full) {
			/* Add CAN ID (4 bytes) */
			stream_data[index++] = (can_circular_buffer[can_buffer_tail].id >> 24) & 0xFF;
			stream_data[index++] = (can_circular_buffer[can_buffer_tail].id >> 16) & 0xFF;
			stream_data[index++] = (can_circular_buffer[can_buffer_tail].id >> 8) & 0xFF;
			stream_data[index++] = can_circular_buffer[can_buffer_tail].id & 0xFF;
			
			/* Add data length (1 byte) */
			stream_data[index++] = can_circular_buffer[can_buffer_tail].len;
			
			/* Add data (up to 8 bytes) */
			for (int i = 0; i < can_circular_buffer[can_buffer_tail].len; i++) {
				stream_data[index++] = can_circular_buffer[can_buffer_tail].data[i];
			}
			
			/* Update tail pointer */
			can_buffer_tail = (can_buffer_tail + 1) % CAN_BUFFER_SIZE;
			can_buffer_full = false;
			
			/* Send notification */
			int err = bt_gatt_notify(NULL, &stsensor_svc.attrs[7], stream_data, index);
			if (err) {
				LOG_ERR("Stream notify error: %d", err);
			}
		}
	}
	
	k_mutex_unlock(&can_data_mutex);
}

/* CAN frame reception callback */
void can_rx_callback(const struct device *dev, struct can_frame *frame,
					void *user_data)
{
#ifdef CONFIG_CAN_API_USE_ZCAN
	/* For older Zephyr versions using zcan API */
	can_buffer_add(frame);
#else
	/* For newer Zephyr CAN API */
	struct can_frame can_frame_data;
	memcpy(&can_frame_data, frame, sizeof(struct can_frame));
	can_buffer_add(&can_frame_data);
#endif
}

void button_callback(const struct device *gpiob, struct gpio_callback *cb,
							uint32_t pins)
{
	int err;

	LOG_INF("Button pressed");
	if (ble_conn)
	{
		if (notify_enable)
		{
			err = bt_gatt_notify(NULL, &stsensor_svc.attrs[4],
								 &but_val, sizeof(but_val));
			if (err)
			{
				LOG_ERR("Notify error: %d", err);
			}
			else
			{
				LOG_INF("Send notify ok");
				but_val = (but_val == 0) ? 0x100 : 0;
			}
		}
		else
		{
			LOG_INF("Notify not enabled");
		}
	}
	else
	{
		LOG_INF("BLE not connected");
	}
}

/* Updated bt_ready function with work queue initialization */
void bt_ready(int err)
{
    if (err) {
        LOG_ERR("Bluetooth init failed (err %d)", err);
        return;
    }
    
    LOG_INF("Bluetooth initialized");
    
    /* Initialize the advertising restart work queue */
    k_work_init_delayable(&adv_work, restart_advertising);
    
    /* Make sure there are no active connections */
    if (ble_conn) {
        bt_conn_unref(ble_conn);
        ble_conn = NULL;
    }
    
    /* Stop any active advertising first */
    err = bt_le_adv_stop();
    if (err && err != -EALREADY) {
        LOG_WRN("Failed to stop advertising (err %d)", err);
        /* Continue anyway */
    }
    
    /* Start advertising */
    err = bt_le_adv_start(BT_LE_ADV_CONN_FAST_1, ad, ARRAY_SIZE(ad), NULL, 0);
    if (err) {
        LOG_ERR("Advertising failed to start (err %d)", err);
        return;
    }

    LOG_INF("Advertising started, waiting for connections");
}

void connected(struct bt_conn *connected, uint8_t err)
{
	if (err)
	{
		LOG_ERR("Connection failed (err %u)", err);
	}
	else
	{
		LOG_INF("Connected");
		if (!ble_conn)
		{
			ble_conn = bt_conn_ref(connected);
		}
	}
}

/* Updated disconnected function with work queue for reconnection */
void disconnected(struct bt_conn *disconn, uint8_t reason)
{
    if (ble_conn) {
        LOG_INF("Disconnected, reason %u %s", reason, bt_hci_err_to_str(reason));
        
        /* Clean up connection resources */
        bt_conn_unref(ble_conn);
        ble_conn = NULL;
        
        /* Stop data streaming when disconnected */
        k_timer_stop(&ble_tx_timer);
        stream_notify_enable = false;
        
        /* Reset other state variables */
        notify_enable = false;
        
        /* Schedule advertising restart with a short delay to allow resource cleanup */
        k_work_schedule(&adv_work, K_MSEC(500));
    }
}

/* Get currently set user IDs */
void get_user_ids(char *instr_buf, size_t instr_size, char *train_buf, size_t train_size)
{
    if (instr_buf && instr_size > 0) {
        strncpy(instr_buf, instructor_id, instr_size - 1);
        instr_buf[instr_size - 1] = '\0';
    }
    
    if (train_buf && train_size > 0) {
        strncpy(train_buf, trainee_id, train_size - 1);
        train_buf[train_size - 1] = '\0';
    }
}

/* Get current user role */
uint8_t get_current_user_role(void)
{
    return current_user_role;
}

/* Work handler function to restart advertising */
void restart_advertising(struct k_work *work)
{
    int err;
    
    LOG_INF("Attempting to restart advertising");
    
    /* First make sure advertising is fully stopped */
    err = bt_le_adv_stop();
    if (err && err != -EALREADY) {
        LOG_WRN("Failed to stop advertising (err %d)", err);
        /* Continue anyway */
    }
    
    /* Wait a moment to ensure resources are released */
    k_sleep(K_MSEC(100));
    
    /* Restart advertising */
    err = bt_le_adv_start(BT_LE_ADV_CONN_FAST_1, ad, ARRAY_SIZE(ad), NULL, 0);
    if (err) {
        LOG_ERR("Advertising failed to restart (err %d)", err);
        
        if (err == -ENOMEM) {
            /* Schedule another attempt after a longer delay */
            LOG_WRN("Out of memory, retrying in 1 second...");
            k_work_schedule(&adv_work, K_SECONDS(1));
        }
    } else {
        LOG_INF("Advertising restarted successfully");
    }
}

BT_CONN_CB_DEFINE(conn_callbacks) = {
	.connected = connected,
	.disconnected = disconnected,
};