# Smart Window Serial Protocol

## 1. Scope

The PC application owns solar-position calculation, tracking decisions, mode
selection, charts and logs. The ESP32 owns step generation, limit checks,
emergency-stop handling, command watchdog and telemetry.

The link is a binary byte stream over USB serial at `115200 8N1`.

## 2. Frame format

All multi-byte integers are little-endian.

| Offset | Size | Field | Description |
|---:|---:|---|---|
| 0 | 1 | `SYNC0` | `0xAA` |
| 1 | 1 | `SYNC1` | `0x55` |
| 2 | 1 | `VERSION` | Protocol version, currently `0x01` |
| 3 | 1 | `TYPE` | Message type |
| 4 | 1 | `SEQUENCE` | Rolling sequence, `0..255` |
| 5 | 1 | `LENGTH` | Payload length, `0..64` |
| 6 | N | `PAYLOAD` | Message-specific bytes |
| 6+N | 2 | `CRC16` | CRC-16/CCITT-FALSE, little-endian |

The CRC uses polynomial `0x1021`, initial value `0xFFFF`, refin/refout false
and xorout `0x0000`. It covers `VERSION` through the end of `PAYLOAD`.

The parser discards bytes before the synchronization pattern. Invalid lengths,
versions or CRCs cause a one-byte resynchronization attempt.

## 3. Message types

### PC to ESP32

| Value | Name | Payload |
|---:|---|---|
| `0x01` | `HELLO` | ASCII `SWC1` |
| `0x10` | `SET_TARGET` | signed `int16` angle in 0.01 degrees |
| `0x11` | `SET_MODE` | `uint8` mode |
| `0x12` | `START` | empty |
| `0x13` | `STOP` | empty |
| `0x14` | `CLEAR_FAULT` | empty |
| `0x15` | `HEARTBEAT` | empty |

### ESP32 to PC

| Value | Name | Payload |
|---:|---|---|
| `0x81` | `HELLO_ACK` | `SWC1` + major + minor + patch |
| `0x82` | `TELEMETRY` | fixed 18 bytes |
| `0x83` | `ACK` | acked sequence + command + result |
| `0x84` | `FAULT` | fault code + detail |

## 4. Modes

| Value | Mode |
|---:|---|
| `0` | Automatic solar tracking |
| `1` | Illuminance harvesting |
| `2` | Radiative-cooling posture |
| `3` | Manual target angle |
| `4` | Stopped |

## 5. Status bits

| Bit | Name | Meaning |
|---:|---|---|
| 0 | `ENABLED` | Driver output enabled |
| 1 | `HOMING` | Homing sequence active |
| 2 | `MOVING` | Step generator active |
| 3 | `LIMIT_MIN` | Minimum-angle switch triggered |
| 4 | `LIMIT_MAX` | Maximum-angle switch triggered |
| 5 | `EMERGENCY_STOP` | Physical emergency stop active |
| 6 | `COMM_TIMEOUT` | PC command watchdog expired |
| 7 | `SENSOR_FAULT` | One or more light sensors unavailable |
| 8 | `MOTOR_FAULT` | Driver diagnostic/stall indication |
| 9 | `HOMED` | Mechanical reference known |

## 6. Command results

| Value | Name |
|---:|---|
| `0` | Success |
| `1` | Rejected |
| `2` | Invalid payload |
| `3` | Unknown command |
| `4` | Not homed |
| `5` | Emergency stop active |
| `6` | Motor fault |
| `7` | Limit reached |

## 7. Telemetry payload

| Offset | Size | Field |
|---:|---:|---|
| 0 | 2 | Signed current angle in 0.01 degrees |
| 2 | 2 | Status bit mask |
| 4 | 2 | Left illuminance |
| 6 | 2 | Right illuminance |
| 8 | 2 | Up illuminance |
| 10 | 2 | Down illuminance |
| 12 | 1 | Current mode |
| 13 | 1 | Sensor-valid bit mask |
| 14 | 4 | ESP32 uptime in seconds |

The firmware sends telemetry every `200 ms`. The PC sends a heartbeat every
`250 ms`. Once started, the ESP32 requires a valid command or heartbeat at
least every `1500 ms`; otherwise it disables the driver and reports
`COMM_TIMEOUT`.

## 8. Reference vector

`SET_TARGET(-12.34°, sequence 7)` encodes as:

```text
AA 55 01 10 07 02 2E FB 3A 0B
```

This vector is asserted by both the Python and C++ protocol tests.

## 9. Safety invariants

1. The physical emergency stop and limit inputs are evaluated by the ESP32.
2. The PC cannot override hardware limits or clear an active emergency stop.
3. Loss of the PC link stops motion locally.
4. Target angles are constrained to the configured mechanical range by both
   endpoints.
5. All frames are length-checked and CRC-checked before state changes.
