#include "ble_handlers.h"
#include <zephyr/drivers/can.h>
#include "ble_can_interface.h"

LOG_MODULE_REGISTER(ble_handlers);

uint8_t ble_cmd_buffer[BLE_BUFFER_SIZE];

/* LED control flags - for safe LED control from BLE thread */
bool led_request_pending = false;
bool led_requested_state = false;

/* Define a work item for delayed advertising restart */
static struct k_work_delayable adv_work;

/* Button value. */
static uint16_t but_val;

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

	/* Minimal logging showing just the length */
	LOG_INF("BLE data received, length: %d bytes", len);

	/* Check if this is a single-byte command (for backward compatibility) */
	if (len == 1) {
		uint8_t cmd = ble_cmd_buffer[0];
		/* Forward to the message processor - no logging */
		submit_direct_command(cmd);
		return len;
	}

	/* For multi-byte commands, forward to the message processor */
	submit_command(ble_cmd_buffer, len);

	return len;
}

/* Process commands received via BLE - this now delegates to the message processor */
void process_ble_command(uint8_t *cmd_data, uint16_t len)
{
    /* Pass through to the message processor using the submit API */
    submit_command(cmd_data, len);
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
    /* First try to process using message processor for standard commands */
    int ret = submit_command(cmd_data, len);
    if (ret == 0) {
        /* Command was successfully processed by message processor */
        return;
    }
    
    /* If message processor didn't handle it, try the BLE-specific commands */
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

/* 
 * The send_can_message function has been moved to can_wrapper_mock.c
 * to centralize all CAN-related functionality.
 */

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

/* 
 * STUB bt_ready function that does nothing 
 * (BLE functionality disabled for debugging)
 */
void bt_ready(int err)
{
    /* This function is a stub - BLE is disabled */
    LOG_INF("BT_READY stub called - BLE is disabled");
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