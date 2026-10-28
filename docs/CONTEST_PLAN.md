# Competition Prototype Build Plan

## 1. Corrected hardware budget

| Category | Cost |
|---|---:|
| Electrical control and communications | 71 CNY |
| Motor and driver | 95 CNY |
| Mechanical frame and window sample | 88 CNY |
| Consumables | 11 CNY |
| Listed subtotal | 265 CNY |

The previously stated 332 CNY did not match the listed item prices. A practical
complete-prototype budget is approximately 330 CNY after adding:

| Safety/function item | Estimated cost |
|---|---:|
| TCA9548A I²C multiplexer | 8 CNY |
| AS5600 optional angle feedback | 15 CNY |
| Fuse, terminals and emergency stop | 12 CNY |
| Additional fasteners/3D-print material | 20 CNY |
| Shipping and spare consumables | 10 CNY |

Use a 24 V 3 A supply if the motor stalls at low speed or the driver resets.

## 2. Build order

1. Bench-test the ESP32, TCA9548A and four TSL2561 modules with the motor
   disconnected.
2. Verify all three NC inputs read LOW normally and HIGH when opened.
3. Set the TMC2209 current limit and test direction with the shaft uncoupled.
4. Install the minimum and maximum limits before connecting the window.
5. Run the PC application and confirm handshake, telemetry and heartbeat.
6. Test homing, stop, communication loss and physical emergency stop.
7. Install the acrylic window and radiation-cooling film.
8. Record solar tracking and light-gradient response at several times of day.

## 3. Demonstration story

1. Show the four relative-light curves and the calculated solar position.
2. Switch from automatic tracking to manual and demonstrate the target angle.
3. Cover one sensor to show the light-gradient correction.
4. Disconnect USB to demonstrate the ESP32 communication watchdog.
5. Press the physical emergency stop to demonstrate independent local safety.
6. Compare the cooling posture with the ordinary window orientation using the
   model window and a heat lamp.

## 4. Test evidence to capture

- Photo of wiring and emergency stop.
- Serial handshake and telemetry log.
- PC screenshot before and after a target change.
- Video or timed photos of homing and tracking.
- Limit-switch and disconnect tests.
- Energy/cooling comparison data if the radiation film is part of the entry.

## 5. Scope boundaries

The PC application computes high-level targets. It does not replace the
ESP32's local limit and emergency-stop logic. The current prototype uses
relative light counts; calibrated photometric lux measurement requires a
reference meter and per-sensor calibration.
