/* main.c - Application main entry point */

/*
 * Copyright (c) 2015-2016 Intel Corporation
 * Copyright (c) 2019 Marcio Montenegro <mtuxpe@gmail.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/types.h>
#include <stddef.h>
#include <string.h>
#include <errno.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/fs/fs.h>
#include <zephyr/fs/fs_interface.h>
#include <zephyr/storage/disk_access.h>
#include "button_svc.h"
#include "led_svc.h"
#include <stm32h7xx.h> // Include STM32 headers manually if needed

LOG_MODULE_REGISTER(main);

#include <ff.h>

/*
 *  Note the fatfs library is able to mount only strings inside _VOLUME_STRS
 *  in ffconf.h
 */
#define DISK_DRIVE_NAME "SD"

#define DISK_MOUNT_PT "/"DISK_DRIVE_NAME":"

static FATFS fat_fs;
/* mounting info */
static struct fs_mount_t mp = {
	.type = FS_FATFS,
	.fs_data = &fat_fs,
};

#define FS_RET_OK FR_OK


#define MAX_PATH 128
#define SOME_FILE_NAME "some.dat"
#define SOME_DIR_NAME "some"
#define SOME_REQUIRED_LEN MAX(sizeof(SOME_FILE_NAME), sizeof(SOME_DIR_NAME))


static bool create_some_entries(const char *base_path)
{
	char path[MAX_PATH];
	struct fs_file_t file;
	int base = strlen(base_path);

	fs_file_t_init(&file);

	if (base >= (sizeof(path) - SOME_REQUIRED_LEN)) {
		LOG_ERR("Not enough concatenation buffer to create file paths");
		return false;
	}

	LOG_INF("Creating some dir entries in %s", base_path);
	strncpy(path, base_path, sizeof(path));

	path[base++] = '/';
	path[base] = 0;
	strcat(&path[base], SOME_FILE_NAME);

	if (fs_open(&file, path, FS_O_CREATE) != 0) {
		LOG_ERR("Failed to create file %s", path);
		return false;
	}
	fs_close(&file);

	path[base] = 0;
	strcat(&path[base], SOME_DIR_NAME);

	if (fs_mkdir(path) != 0) {
		LOG_ERR("Failed to create dir %s", path);
		/* If code gets here, it has at least successes to create the
		 * file so allow function to return true.
		 */
	}
	return true;
}


static const char *disk_mount_pt = DISK_MOUNT_PT;

/* Button value. */
static uint16_t but_val;

/* Prototype */
static ssize_t recv(struct bt_conn *conn,
		    const struct bt_gatt_attr *attr, const void *buf,
		    uint16_t len, uint16_t offset, uint8_t flags);

/* ST Custom Service  */
static const struct bt_uuid_128 st_service_uuid = BT_UUID_INIT_128(
	BT_UUID_128_ENCODE(0x0000fe40, 0xcc7a, 0x482a, 0x984a, 0x7f2ed5b3e58f));

/* ST LED service */
static const struct bt_uuid_128 led_char_uuid = BT_UUID_INIT_128(
	BT_UUID_128_ENCODE(0x0000fe41, 0x8e22, 0x4541, 0x9d4c, 0x21edae82ed19));

/* ST Notify button service */
static const struct bt_uuid_128 but_notif_uuid = BT_UUID_INIT_128(
	BT_UUID_128_ENCODE(0x0000fe42, 0x8e22, 0x4541, 0x9d4c, 0x21edae82ed19));

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

static const struct bt_data ad[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
	BT_DATA(BT_DATA_NAME_COMPLETE, DEVICE_NAME, DEVICE_NAME_LEN),
	BT_DATA(BT_DATA_MANUFACTURER_DATA, manuf_data, ADV_LEN)
};

/* BLE connection */
struct bt_conn *ble_conn;
/* Notification state */
volatile bool notify_enable;

static void mpu_ccc_cfg_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
	ARG_UNUSED(attr);
	notify_enable = (value == BT_GATT_CCC_NOTIFY);
	LOG_INF("Notification %s", notify_enable ? "enabled" : "disabled");
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
);

static ssize_t recv(struct bt_conn *conn,
		    const struct bt_gatt_attr *attr, const void *buf,
		    uint16_t len, uint16_t offset, uint8_t flags)
{
	led_update();

	return 0;
}

static void button_callback(const struct device *gpiob, struct gpio_callback *cb,
		     uint32_t pins)
{
	int err;

	LOG_INF("Button pressed");
	if (ble_conn) {
		if (notify_enable) {
			err = bt_gatt_notify(NULL, &stsensor_svc.attrs[4],
					     &but_val, sizeof(but_val));
			if (err) {
				LOG_ERR("Notify error: %d", err);
			} else {
				LOG_INF("Send notify ok");
				but_val = (but_val == 0) ? 0x100 : 0;
			}
		} else {
			LOG_INF("Notify not enabled");
		}
	} else {
		LOG_INF("BLE not connected");
	}
}

static void bt_ready(int err)
{
	if (err) {
		LOG_ERR("Bluetooth init failed (err %d)", err);
		return;
	}
	LOG_INF("Bluetooth initialized");
	/* Start advertising */
	err = bt_le_adv_start(BT_LE_ADV_CONN_FAST_1, ad, ARRAY_SIZE(ad), NULL, 0);
	if (err) {
		LOG_ERR("Advertising failed to start (err %d)", err);
		return;
	}

	LOG_INF("Configuration mode: waiting connections...");
}

static void connected(struct bt_conn *connected, uint8_t err)
{
	if (err) {
		LOG_ERR("Connection failed (err %u)", err);
	} else {
		LOG_INF("Connected");
		if (!ble_conn) {
			ble_conn = bt_conn_ref(connected);
		}
	}
}

static void disconnected(struct bt_conn *disconn, uint8_t reason)
{
	if (ble_conn) {
		bt_conn_unref(ble_conn);
		ble_conn = NULL;
	}

	LOG_INF("Disconnected, reason %u %s", reason, bt_hci_err_to_str(reason));
}

BT_CONN_CB_DEFINE(conn_callbacks) = {
	.connected = connected,
	.disconnected = disconnected,
};

int main(void)
{/* raw disk i/o */
	
	do {
		static const char *disk_pdrv = DISK_DRIVE_NAME;
		uint64_t memory_size_mb;
		uint32_t block_count;
		uint32_t block_size;

		if (disk_access_ioctl(disk_pdrv,
				DISK_IOCTL_CTRL_INIT, NULL) != 0) {
			LOG_ERR("Storage init ERROR!");
			break;
		}

		if (disk_access_ioctl(disk_pdrv,
				DISK_IOCTL_GET_SECTOR_COUNT, &block_count)) {
			LOG_ERR("Unable to get sector count");
			break;
		}
		LOG_INF("Block count %u", block_count);

		if (disk_access_ioctl(disk_pdrv,
				DISK_IOCTL_GET_SECTOR_SIZE, &block_size)) {
			LOG_ERR("Unable to get sector size");
			break;
		}
		printk("Sector size %u\n", block_size);

		memory_size_mb = (uint64_t)block_count * block_size;
		printk("Memory Size(MB) %u\n", (uint32_t)(memory_size_mb >> 20));

		if (disk_access_ioctl(disk_pdrv,
				DISK_IOCTL_CTRL_DEINIT, NULL) != 0) {
			LOG_ERR("Storage deinit ERROR!");
			break;
		}
	} while (0);
	int err;



	err = button_init(button_callback);
	if (err) {
		return 0;
	}

	err = led_init();
	if (err) {
		return 0;
	}

	/* Initialize the Bluetooth Subsystem */
	err = bt_enable(bt_ready);
	if (err) {
		LOG_ERR("Bluetooth init failed (err %d)", err);
	}
	return 0;
}
