# Software Registration Material Map

## 1. Product scope

Register the PC application and ESP32 execution firmware as one coherent
software product only if the submitted manual describes the serial protocol,
PC operator workflow and MCU safety behavior as parts of the same system.

The user manual should use the product name:

```text
智能环境自适应旋转窗控制系统 V1.0
```

The source version stored in this repository is the fixed Git commit used to
generate the PDF and executable. Do not mix files from different tags.

## 2. Manual chapter map

| Manual chapter | Source or evidence |
|---|---|
| 1. Product overview | `README.md` system architecture |
| 2. Hardware composition | `docs/WIRING.md` |
| 3. Installation and connection | `docs/OPERATIONS.md` |
| 4. Main interface | `docs/images/pc_app.png` |
| 5. Serial communication | `protocol/PROTOCOL.md` |
| 6. Solar tracking | `pc_app/src/smart_window/solar.py` |
| 7. Light PID | `pc_app/src/smart_window/controller.py` |
| 8. ESP32 execution | `firmware/esp32/src/main.cpp` |
| 9. Safety and fault handling | `firmware/esp32/src/motion_controller.cpp` |
| 10. Test and acceptance | `docs/CONTEST_PLAN.md` |

## 3. Recommended source order

### First material section

1. PC entry and main window construction.
2. Serial frame encoding/parsing and CRC.
3. Serial connection and command sequence.
4. Solar-position calculation.
5. Tracking PID and mode selection.
6. Persistent configuration.

### Last material section

1. ESP32 frame parser and telemetry.
2. TCA9548A four-sensor acquisition.
3. STEP/DIR pulse generator and homing.
4. Limit, emergency stop and communication watchdog.
5. C99 safety reference core, if included in the same registration.

Cut pages only at complete function or class boundaries. Include source page
headers with the product name, version and page number.

## 4. Evidence checklist

- [ ] Fixed Git commit and release tag recorded.
- [ ] Windows executable built from the same commit.
- [ ] PC source and ESP32 source counted in the declared scope.
- [ ] Manual screenshots match the released UI.
- [ ] Serial protocol version in the executable and firmware match.
- [ ] Actual hardware photographed and wiring reviewed.
- [ ] Homing, limit, emergency stop and communication-loss tests recorded.
- [ ] No credentials, local absolute paths or personal data in submitted files.

## 5. Important boundary

The generated UI screenshot in `docs/images/pc_app.png` is illustrative test
data. It should be supplemented by a screenshot from the actual connected
hardware before being presented as a hardware acceptance result.
