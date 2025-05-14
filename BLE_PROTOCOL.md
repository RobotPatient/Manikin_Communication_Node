# Manikin-iOS BLE Communication Protocol

This document specifies the Bluetooth Low Energy (BLE) communication protocol between the CPR training manikin device and an iOS application.

## Protocol Format

All commands and responses follow a standardized format:

```
START_BYTE + LENGTH_BYTE + COLON + MESSAGE + SEMICOLON + END_BYTE
```

### Protocol Constants

| Constant | Value | Description |
| --- | --- | --- |
| `BLE_COMMAND_BYTE_START` | `0x01` | Start of message marker |
| `BLE_COMMAND_MSG_COLON` | `0x3A` | Separator before message content |
| `BLE_COMMAND_MSG_SEMICOLON` | `0x3B` | Separator after message content |
| `BLE_COMMAND_MSG_END` | `0x17` | End of message marker |

### Message Structure

* **START_BYTE**: Always `0x01`
* **LENGTH_BYTE**: Length of the MESSAGE content (1 byte)
* **COLON**: Always `0x3A`
* **MESSAGE**: Command byte + optional payload
* **SEMICOLON**: Always `0x3B`
* **END_BYTE**: Always `0x17`

## Commands (iOS → Manikin)

### CPR Session Management

#### Start CPR Session

To start a CPR session, the iOS app sends a sequence of 4 commands in order:

**Send Instructor ID**
```
Command: CMD_COMMAND_DATA (0x04)
Payload: Instructor ID data
Format: 0x01 + [LENGTH] + 0x3A + 0x04 + [INSTRUCTOR_ID] + 0x3B + 0x17
```

**Send Trainee ID**
```
Command: CMD_COMMAND_DATA (0x04)
Payload: Trainee ID data
Format: 0x01 + [LENGTH] + 0x3A + 0x04 + [TRAINEE_ID] + 0x3B + 0x17
```

**Send Date/Time**
```
Command: CMD_COMMAND_TIMEDATA (0x05)
Payload: Date/time data
Format: 0x01 + [LENGTH] + 0x3A + 0x05 + [DATETIME] + 0x3B + 0x17
```

**Start CPR Command**
```
Command: CPR_CONTROL_START (0x02)
Payload: None
Format: 0x01 + 0x01 + 0x3A + 0x02 + 0x3B + 0x17
```

#### Stop CPR Session

**Stop CPR Command**
```
Command: CPR_COMMAND_STOP (0x03)
Payload: None
Format: 0x01 + 0x01 + 0x3A + 0x03 + 0x3B + 0x17
```

### Command Reference

| Command Name | Value | Description | Payload |
| --- | --- | --- | --- |
| `CPR_CONTROL_START` | `0x02` | Start CPR session | None |
| `CPR_COMMAND_STOP` | `0x03` | Stop CPR session | None |
| `CMD_COMMAND_DATA` | `0x04` | Send ID data | Instructor/Trainee ID |
| `CMD_COMMAND_TIMEDATA` | `0x05` | Send date/time | Date/time data |

## Responses (Manikin → iOS)

### Acknowledgment Messages

The manikin acknowledges each command by echoing back the same command value:

```
Format: 0x01 + 0x01 + 0x3A + [COMMAND_BYTE] + 0x3B + 0x17
```

#### Expected Acknowledgments

| Original Command | Expected Response | iOS Action |
| --- | --- | --- |
| `CPR_CONTROL_START (0x02)` | `0x01 0x01 0x3A 0x02 0x3B 0x17` | Set `isCPRStartAcknowledged = true`, update UI |
| `CPR_COMMAND_STOP (0x03)` | `0x01 0x01 0x3A 0x03 0x3B 0x17` | Set `isCPRStopAcknowledged = true`, reset session |
| `CMD_COMMAND_DATA (0x04)` | `0x01 0x01 0x3A 0x04 0x3B 0x17` | Acknowledge ID data received |
| `CMD_COMMAND_TIMEDATA (0x05)` | `0x01 0x01 0x3A 0x05 0x3B 0x17` | Acknowledge date/time received |

### Heartbeat Messages

The manikin sends periodic heartbeat messages with a special format:

```
Format: [START][TYPE=HEARTBEAT][VALUE]
```

* **START**: `0x01`
* **TYPE**: `0x01` (`MSG_TYPE_HEARTBEAT`)
* **VALUE**: Counter value (varies)

## Notification Message Types

The manikin can send various notification messages identified by their type:

| Message Type | Value | Description |
| --- | --- | --- |
| `MSG_TYPE_HEARTBEAT` | `0x01` | Heartbeat signals |
| `MSG_TYPE_LED_STATE` | `0x10` | LED state updates |
| `MSG_TYPE_TIME_DATA` | `0x20` | Time data notifications |
| `MSG_TYPE_CPR_TIME` | `0x30` | Session time progress |
| `MSG_TYPE_CPR_STATE` | `0x40` | CPR state changes |
| `MSG_TYPE_USER_ROLE` | `0x50` | User role information |
| `MSG_TYPE_CPR_CMD_ACK` | `0x60` | Command acknowledgments |

## Communication Flow

### CPR Session Start Flow

```
iOS → Manikin: Send Instructor ID (0x04)
Manikin → iOS: ACK Instructor ID (0x04)

iOS → Manikin: Send Trainee ID (0x04)
Manikin → iOS: ACK Trainee ID (0x04)

iOS → Manikin: Send Date/Time (0x05)
Manikin → iOS: ACK Date/Time (0x05)

iOS → Manikin: Start CPR (0x02)
Manikin → iOS: ACK Start CPR (0x02)
```

### CPR Session Stop Flow

```
iOS → Manikin: Stop CPR (0x03)
Manikin → iOS: ACK Stop CPR (0x03)
```

## Error Handling

### Timeout Behavior

* **Timeout Period**: 3 seconds for each command acknowledgment
* **Timeout Action**:
  * Log timeout message
  * Show warning status in UI
  * Continue with sequence for reliability

### Response Validation

The iOS app validates received BLE data through:
1. **Message Format Validation**: Verify protocol structure
2. **Command Extraction**: Parse command byte and payload
3. **Acknowledgment Matching**: Check against expected responses

## Example Messages

### Start CPR Command (No Payload)

```
Hex: 0x01 0x01 0x3A 0x02 0x3B 0x17
Breakdown:
0x01: START_BYTE
0x01: LENGTH_BYTE (1 byte message)
0x3A: COLON
0x02: CPR_CONTROL_START command
0x3B: SEMICOLON
0x17: END_BYTE
```

### Stop CPR Command (No Payload)

```
Hex: 0x01 0x01 0x3A 0x03 0x3B 0x17
Breakdown:
0x01: START_BYTE
0x01: LENGTH_BYTE (1 byte message)
0x3A: COLON
0x03: CPR_COMMAND_STOP command
0x3B: SEMICOLON
0x17: END_BYTE
```

## Implementation Notes

1. **Sequential Execution**: Commands for starting CPR must be sent in the specified order
2. **Acknowledgment Required**: Each command requires acknowledgment before proceeding
3. **Timeout Handling**: Implement proper timeout handling for reliability
4. **State Management**: Track acknowledgment states for UI updates
5. **Error Recovery**: Continue sequence even on timeouts for robustness

## Security Considerations

* Implement proper BLE pairing and encryption
* Validate all incoming data to prevent buffer overflows
* Implement rate limiting to prevent spam commands
* Secure storage of user IDs and session data