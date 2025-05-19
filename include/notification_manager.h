/**
 * @file notification_manager.h
 * @brief Priority queue for BLE notifications to prevent ENOMEM errors
 */

#ifndef NOTIFICATION_MANAGER_H
#define NOTIFICATION_MANAGER_H

#include <zephyr/types.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>

/**
 * @brief Priority levels for messages
 */
#define NOTIF_PRIORITY_LOW    10    /* Low priority, can be dropped */
#define NOTIF_PRIORITY_MEDIUM 50    /* Medium priority */
#define NOTIF_PRIORITY_HIGH   80    /* High priority, should be sent */
#define NOTIF_PRIORITY_CRITICAL 100 /* Critical priority, must be sent */

/**
 * @brief Initialize the notification manager
 * 
 * @param chr GATT characteristic to send notifications on
 * @return 0 on success, negative error code on failure
 */
int notification_manager_init(const struct bt_gatt_attr *chr);

/**
 * @brief Add a notification to the queue
 * 
 * @param data Data buffer to send
 * @param len Length of data buffer
 * @param priority Priority of notification (0-100)
 * @param critical Whether this is a critical message
 * @return 0 on success, negative error code on failure
 */
int notification_manager_add(const uint8_t *data, uint16_t len, 
                           uint8_t priority, bool critical);

/**
 * @brief Process notifications in the queue
 * 
 * Should be called periodically to send queued notifications
 * 
 * @return 0 on success, negative error code on failure
 */
int notification_manager_process(void);

/**
 * @brief Get the number of notifications in the queue
 * 
 * @return Number of notifications
 */
uint32_t notification_manager_get_count(void);

/**
 * @brief Clear the notification queue
 */
void notification_manager_clear(void);

/**
 * @brief Check if we're currently experiencing memory pressure
 * 
 * @return true if memory errors are frequent
 */
bool notification_manager_has_memory_pressure(void);

#endif /* NOTIFICATION_MANAGER_H */