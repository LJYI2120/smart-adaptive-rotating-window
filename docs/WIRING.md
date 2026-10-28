# Prototype Wiring and Power

## 1. Default pin map

| ESP32 pin | Connected signal | Notes |
|---:|---|---|
| GPIO21 | I²C SDA | TCA9548A and optional LCD/AS5600 |
| GPIO22 | I²C SCL | TCA9548A and optional LCD/AS5600 |
| GPIO26 | TMC2209 STEP | Step pulse |
| GPIO27 | TMC2209 DIR | Direction |
| GPIO25 | TMC2209 EN | Active-low enable |
| GPIO32 | Minimum-angle limit | NC contact, input pull-up |
| GPIO33 | Maximum-angle limit | NC contact, input pull-up |
| GPIO14 | Emergency stop | NC contact, input pull-up |
| GPIO34 | TMC2209 DIAG | Optional StallGuard input |

The defaults are in `firmware/esp32/include/board_config.h`.

## 2. Power

```text
24 V DC
  ├─ TMC2209 VM / GND
  └─ LM2596 input
         └─ 5 V -> ESP32 development-board 5V pin

ESP32 3.3 V -> TCA9548A VIN / TSL2561 modules
All logic components share GND with the 24 V supply.
```

Use a 24 V 3 A supply where possible. The original 2 A supply has little
margin for NEMA17 startup and holding current. Set the TMC2209 current limit
for the actual motor before connecting the shaft.

Add a fuse on the 24 V input and a physical emergency-stop switch that
disconnects driver enable or motor power. The software stop button is not a
replacement for the physical switch.

## 3. Four TSL2561 sensors

The four sensors share the same fixed I²C address. Connect them to separate
channels of a TCA9548A:

| TCA9548A channel | Position |
|---:|---|
| 0 | Left |
| 1 | Right |
| 2 | Up |
| 3 | Down |

The firmware uses TSL2561 channel 0 minus channel 1 as a relative daylight
signal. These values are linearized sensor counts, not laboratory-calibrated
lux values. The PC chart labels them as relative light intensity.

## 4. Normally closed limits

Wire each limit switch as normally closed between the GPIO and GND:

```text
GPIO32/33/14 ---- NC contact ---- GND
```

With `INPUT_PULLUP`, a healthy unactuated switch reads LOW. A triggered switch,
broken wire or disconnected connector reads HIGH. The firmware treats HIGH as
triggered/faulted.

## 5. Stepper configuration

- Motor: NEMA17 42BYGH40, 1.5 A per phase.
- Driver: TMC2209 in STEP/DIR mode.
- Default scale: 200 full steps per revolution, 16 microsteps, direct drive.
- Equivalent scale: `3200 / 360 = 8.8889` microsteps per degree.
- If a reduction gearbox is installed, update `kStepsPerDegreeMilli`.

The current firmware homes to the minimum limit at startup. Do not enable the
motor until the limit direction and emergency-stop input have been tested with
the shaft uncoupled.

## 6. Optional improvements

- Add an AS5600 magnetic encoder to verify step position and detect lost
  steps. Set `kEnableAs5600` after wiring and calibration. The firmware uses
  raw-angle change as an optional no-motion detector; step counts remain the
  primary position estimate.
- Use a self-locking worm gear or brake if the window is not gravity-balanced.
- Add a TCA9548A-level reset line so a stuck I²C multiplexer can be recovered.
- Add an isolated USB module when the PC and 24 V supply are far apart or the
  electrical environment is noisy.
