# Notification Manager Integration Guide

This guide explains how to integrate the Notification Manager into your CPR Manikin firmware to fix ENOMEM issues with BLE notifications.

## Overview

The Notification Manager provides a priority-based queue for BLE notifications, with intelligent retry and backoff mechanisms to handle ENOMEM errors. Key features include:

- Priority-based queuing (critical messages sent first)
- Automatic retry with exponential backoff
- Memory pressure detection
- Selective dropping of low-priority messages under memory pressure

## Integration Steps

### 1. Initialize the Notification Manager

In your BLE service initialization, find where you register your GATT service and initialize the notification manager with your notify characteristic:

```c
// After BT GATT service registration
const struct bt_gatt_attr *notify_char = &my_service->attrs[NOTIFY_INDEX];
notification_manager_init(notify_char);
```

### 2. Replace Direct Notifications with Queue

Replace all calls to `bt_gatt_notify()` with `notification_manager_add()`:

```c
// Before:
bt_gatt_notify(NULL, &my_service->attrs[NOTIFY_INDEX], data, len);

// After:
notification_manager_add(data, len, PRIORITY, CRITICAL);
```

Use appropriate priority levels:
- `NOTIF_PRIORITY_LOW` (10) - For regular updates, can be dropped
- `NOTIF_PRIORITY_MEDIUM` (50) - For important updates 
- `NOTIF_PRIORITY_HIGH` (80) - For important status changes
- `NOTIF_PRIORITY_CRITICAL` (100) - For critical messages like CPR STOP acknowledgments

### 3. Set Critical Flag for Important Messages

For critical acknowledgments like CPR STOP, set the critical flag to true:

```c
// For CPR STOP acknowledgment
uint8_t priority = NOTIF_PRIORITY_CRITICAL;
bool critical = true;
notification_manager_add(ack_data, ack_len, priority, critical);
```

### 4. Process Queue in Main Loop

Add notification processing to your main loop:

```c
while (1) {
    // Process notifications if there are any in the queue
    if (notification_manager_get_count() > 0) {
        notification_manager_process();
    }
    
    // Rest of your main loop
    k_sleep(K_MSEC(10));
}
```

### 5. Handle Disconnection

When BLE disconnects, clear the notification queue:

```c
static void disconnected(struct bt_conn *conn, uint8_t reason)
{
    // Existing disconnect handling
    
    // Clear notification queue
    notification_manager_clear();
}
```

## Example for CPR STOP Acknowledgment

```c
void send_cpr_stop_ack(void)
{
    // Format your acknowledgment message with protocol format
    uint8_t ack_data[BUFFER_SIZE];
    uint16_t ack_len;
    
    // Format the message (with your protocol)
    ack_len = format_message(CMD_STOP, NULL, 0, ack_data, sizeof(ack_data));
    
    // Add to notification queue with highest priority and critical flag
    notification_manager_add(ack_data, ack_len, NOTIF_PRIORITY_CRITICAL, true);
    
    LOG_INF("CPR STOP acknowledgment queued with highest priority");
}
```

## Memory Error Handling

The notification manager automatically handles memory errors by:
1. Adding failed critical messages back to the queue
2. Implementing exponential backoff for retries
3. Adding delays to allow BLE stack to recover
4. Tracking memory pressure to adjust behavior

## Logging

The notification manager provides detailed logging for troubleshooting:
- Success/failure of notifications
- Backoff details when memory errors occur
- Queue operations (add, drop, replace)