/**
 * @file notification_manager.c
 * @brief Priority queue for BLE notifications to prevent ENOMEM errors
 */

#include <zephyr/kernel.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/logging/log.h>
#include <string.h>

#include "notification_manager.h"

LOG_MODULE_REGISTER(notif_manager, CONFIG_LOG_DEFAULT_LEVEL);


/* Size of notification queue from Kconfig */
#define QUEUE_SIZE CONFIG_CPR_MANIKIN_BLE_NOTIFICATION_QUEUE_SIZE
#define MAX_RETRIES CONFIG_CPR_MANIKIN_MAX_NOTIFICATION_RETRY
#define PRIORITY_HIGH CONFIG_CPR_MANIKIN_PRIORITY_HIGH_THRESHOLD
#define PRIORITY_MEDIUM CONFIG_CPR_MANIKIN_PRIORITY_MEDIUM_THRESHOLD

/* Notification item structure */
struct notification_item {
    uint8_t data[256];            /* Notification data buffer */
    uint16_t len;                 /* Length of data */
    uint8_t priority;             /* Priority (0-100) */
    bool critical;                /* Critical notification flag */
    uint8_t retry_count;          /* Number of retry attempts */
    bool in_use;                  /* Whether this slot is in use */
};

/* Queue for notifications */
static struct notification_item queue[QUEUE_SIZE];
static uint32_t queue_count = 0;

/* GATT characteristic to notify on */
static const struct bt_gatt_attr *notify_attr;

/* Memory error tracking */
#define ERROR_TRACKING_WINDOW_MS 10000  /* 10 second window */
static uint32_t memory_error_count = 0;
static uint32_t last_memory_error_time = 0;

/* Mutex for queue access */
K_MUTEX_DEFINE(queue_mutex);

/* Work for delayed notification sending */
static struct k_work_delayable notify_work;

/**
 * @brief Find the highest priority notification in the queue
 * 
 * @return Index of highest priority notification, or -1 if queue empty
 */
static int find_highest_priority(void)
{
    int highest_idx = -1;
    uint8_t highest_pri = 0;
    
    for (int i = 0; i < QUEUE_SIZE; i++) {
        if (queue[i].in_use && queue[i].priority > highest_pri) {
            highest_pri = queue[i].priority;
            highest_idx = i;
        }
    }
    
    return highest_idx;
}

/**
 * @brief Calculate backoff time for retry
 * 
 * @param retry_count Current retry count
 * @return Backoff time in milliseconds
 */
static uint32_t calculate_backoff(uint8_t retry_count)
{
    /* Base backoff (exponential) */
    uint32_t backoff = 100 * (1 << retry_count);
    
    /* Add memory pressure consideration */
    if (memory_error_count > 3) {
        backoff += memory_error_count * 100;
    }
    
    /* Add jitter */
    backoff += (sys_rand32_get() % 50);
    
    /* Cap backoff */
    if (backoff > 5000) {
        backoff = 5000;
    }
    
    return backoff;
}

/**
 * @brief Record a memory error 
 */
static void record_memory_error(void)
{
    uint32_t now = k_uptime_get_32();
    
    /* Reset count if outside window */
    if ((now - last_memory_error_time) > ERROR_TRACKING_WINDOW_MS) {
        memory_error_count = 0;
    }
    
    memory_error_count++;
    last_memory_error_time = now;
    
    LOG_WRN("BLE memory error recorded (count: %d)", memory_error_count);
}

/**
 * @brief Send a notification to the device
 * 
 * @param data Data buffer to send
 * @param len Length of the data
 * @return 0 on success, negative error code on failure
 */
static int send_notification(const uint8_t *data, uint16_t len)
{
    int err;
    
    /* Send notification */
    err = bt_gatt_notify(NULL, notify_attr, data, len);
    
    /* Handle errors */
    if (err == -ENOMEM) {
        record_memory_error();
        LOG_ERR("Notification failed (err %d): ENOMEM - Out of memory", err);
        
        if (memory_error_count >= 5) {
            LOG_WRN("Severe memory constraints (count %d), delaying acknowledgments", 
                  memory_error_count);
        }
    } else if (err) {
        LOG_ERR("Notification failed with error: %d", err);
    }
    
    return err;
}

/**
 * @brief Process the next notification in the queue
 * 
 * @return 0 on success, negative error code on failure
 */
static int process_next_notification(void)
{
    int idx, err;
    
    /* Lock the queue */
    k_mutex_lock(&queue_mutex, K_FOREVER);
    
    /* Find highest priority notification */
    idx = find_highest_priority();
    if (idx < 0) {
        /* No notifications to process */
        k_mutex_unlock(&queue_mutex);
        return 0;
    }
    
    /* Get notification data */
    uint8_t data[256];
    uint16_t len = queue[idx].len;
    uint8_t priority = queue[idx].priority;
    bool critical = queue[idx].critical;
    uint8_t retry_count = queue[idx].retry_count;
    
    /* Copy data to local buffer */
    memcpy(data, queue[idx].data, len);
    
    /* Unlock the queue during sending */
    k_mutex_unlock(&queue_mutex);
    
    /* Send the notification */
    err = send_notification(data, len);
    
    /* Handle errors */
    if (err == -ENOMEM) {
        /* Only retry high priority or critical notifications */
        if ((priority >= PRIORITY_HIGH || critical) && retry_count < MAX_RETRIES) {
            /* Lock queue for update */
            k_mutex_lock(&queue_mutex, K_FOREVER);
            
            /* Update retry count */
            queue[idx].retry_count++;
            
            /* Calculate backoff time */
            uint32_t backoff = calculate_backoff(retry_count);
            
            LOG_WRN("ENOMEM on %s notification - backing off for %d ms (attempt %d/%d)",
                   critical ? "critical" : "high priority",
                   backoff, retry_count + 1, MAX_RETRIES);
            
            /* Schedule delayed retry */
            k_work_schedule(&notify_work, K_MSEC(backoff));
            
            k_mutex_unlock(&queue_mutex);
            
            /* Sleep briefly to allow BLE stack to recover */
            uint32_t recovery_delay = 350;
            LOG_INF("Sleeping for %d ms to allow BLE stack to recover", recovery_delay);
            k_sleep(K_MSEC(recovery_delay));
        } else {
            /* Remove from queue */
            k_mutex_lock(&queue_mutex, K_FOREVER);
            queue[idx].in_use = false;
            queue_count--;
            k_mutex_unlock(&queue_mutex);
            
            if (retry_count >= MAX_RETRIES) {
                LOG_ERR("Failed to send notification after %d retries", retry_count);
            } else {
                LOG_WRN("Dropped low priority notification due to memory constraints");
            }
        }
    } else {
        /* Success or non-memory error - remove from queue */
        k_mutex_lock(&queue_mutex, K_FOREVER);
        queue[idx].in_use = false;
        queue_count--;
        k_mutex_unlock(&queue_mutex);
        
        if (err) {
            LOG_ERR("Failed to send notification: %d", err);
        }
    }
    
    return err;
}

/**
 * @brief Handler for the notification work
 */
static void notify_work_handler(struct k_work *work)
{
    /* Process next notification */
    process_next_notification();
    
    /* If still items in queue and no memory pressure, schedule again */
    k_mutex_lock(&queue_mutex, K_FOREVER);
    bool has_items = (queue_count > 0);
    k_mutex_unlock(&queue_mutex);
    
    if (has_items && memory_error_count < 3) {
        k_work_schedule(&notify_work, K_NO_WAIT);
    }
}

/**
 * @brief Initialize the notification manager
 * 
 * @param chr GATT characteristic to send notifications on
 * @return 0 on success, negative error code on failure
 */
int notification_manager_init(const struct bt_gatt_attr *chr)
{
    if (!chr) {
        return -EINVAL;
    }
    
    /* Store characteristic */
    notify_attr = chr;
    
    /* Initialize queue */
    k_mutex_lock(&queue_mutex, K_FOREVER);
    memset(queue, 0, sizeof(queue));
    queue_count = 0;
    k_mutex_unlock(&queue_mutex);
    
    /* Initialize work */
    k_work_init_delayable(&notify_work, notify_work_handler);
    
    LOG_INF("Notification manager initialized (queue size: %d)", QUEUE_SIZE);
  
    return 0;
}

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
                           uint8_t priority, bool critical)
{
    int idx = -1;
    
    if (!data || len == 0 || len > 255) {
        return -EINVAL;
    }
    
    k_mutex_lock(&queue_mutex, K_FOREVER);
    
    /* Find a free slot or lowest priority slot if full */
    if (queue_count < QUEUE_SIZE) {
        /* Find free slot */
        for (int i = 0; i < QUEUE_SIZE; i++) {
            if (!queue[i].in_use) {
                idx = i;
                break;
            }
        }
    } else {
        /* Queue full - find lowest priority item for replacement */
        uint8_t lowest_pri = 255;
        int lowest_idx = -1;
        
        for (int i = 0; i < QUEUE_SIZE; i++) {
            if (!queue[i].critical && queue[i].priority < lowest_pri) {
                lowest_pri = queue[i].priority;
                lowest_idx = i;
            }
        }
        
        /* Only replace if new item has higher priority */
        if (lowest_idx >= 0 && (priority > lowest_pri || critical)) {
            idx = lowest_idx;
            LOG_WRN("Queue full - replacing pri=%d with pri=%d%s", 
                   lowest_pri, priority, critical ? " (critical)" : "");
        }
    }
    
    /* If no slot found, return error */
    if (idx < 0) {
        k_mutex_unlock(&queue_mutex);
        LOG_WRN("Failed to add notification - queue full");
        return -ENOMEM;
    }
    
    /* Add to queue */
    memcpy(queue[idx].data, data, len);
    queue[idx].len = len;
    queue[idx].priority = priority;
    queue[idx].critical = critical;
    queue[idx].retry_count = 0;
    queue[idx].in_use = true;
    
    /* Update count */
    if (queue_count < QUEUE_SIZE) {
        queue_count++;
    }
    
    /* Check if this is the first item */
    bool was_empty = (queue_count == 1);
    
    k_mutex_unlock(&queue_mutex);
    
    /* Schedule processing if this was the first item */
    if (was_empty) {
        k_work_schedule(&notify_work, K_NO_WAIT);
    }
    
    return 0;
}

/**
 * @brief Process notifications in the queue
 * 
 * Should be called periodically to send queued notifications
 * 
 * @return 0 on success, negative error code on failure
 */
int notification_manager_process(void)
{
    k_mutex_lock(&queue_mutex, K_FOREVER);
    bool has_items = (queue_count > 0);
    k_mutex_unlock(&queue_mutex);
    
    if (!has_items) {
        return 0;
    }
    
    return process_next_notification();
}

/**
 * @brief Get the number of notifications in the queue
 * 
 * @return Number of notifications
 */
uint32_t notification_manager_get_count(void)
{
    uint32_t count;
    
    k_mutex_lock(&queue_mutex, K_FOREVER);
    count = queue_count;
    k_mutex_unlock(&queue_mutex);
    
    return count;
}

/**
 * @brief Clear the notification queue
 */
void notification_manager_clear(void)
{
    k_mutex_lock(&queue_mutex, K_FOREVER);
    
    /* Clear all items */
    memset(queue, 0, sizeof(queue));
    queue_count = 0;
    
    k_mutex_unlock(&queue_mutex);
    
    /* Cancel any pending work */
    k_work_cancel_delayable(&notify_work);
    
    LOG_INF("Notification queue cleared");
}

/**
 * @brief Check if we're currently experiencing memory pressure
 * 
 * @return true if memory errors are frequent
 */
bool notification_manager_has_memory_pressure(void)
{
    bool has_pressure;
    
    k_mutex_lock(&queue_mutex, K_FOREVER);
    has_pressure = (memory_error_count >= 3);
    k_mutex_unlock(&queue_mutex);
    
    return has_pressure;
}


