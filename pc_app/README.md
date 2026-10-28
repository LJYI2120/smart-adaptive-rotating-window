# Smart Window PC Controller

Windows desktop application for the rotating-window prototype. The PC owns
solar-position calculation, tracking decisions, mode selection, data charts
and operator controls. The ESP32 owns local safety and step generation.

## Features

- Automatic solar tracking using date, time, longitude, latitude and window
  orientation.
- Normalized illuminance PID correction on top of the solar target.
- Manual angle, illuminance-harvesting and radiative-cooling posture modes.
- One-click serial discovery/connection with CH340 and CP210x preference.
- CRC-protected serial protocol with heartbeats and acknowledgements.
- Four-channel relative-light chart and live angle/status display.
- Automatic fault and communication-timeout indication.
- Standalone Windows build through PyInstaller.

## Run from source

From the repository root:

```powershell
python -m venv pc_app\.venv
.\pc_app\.venv\Scripts\python.exe -m pip install -r pc_app\requirements.txt
$env:PYTHONPATH="$PWD\pc_app\src"
.\pc_app\.venv\Scripts\python.exe -m smart_window
```

## Test

```powershell
$env:QT_QPA_PLATFORM="offscreen"
.\pc_app\.venv\Scripts\python.exe -m pytest -c pc_app\pyproject.toml pc_app\tests
```

## Build the Windows application

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\pc_app\build_windows.ps1
```

The distributable directory is:

```text
pc_app/dist/SmartWindowController/
```

Run `SmartWindowController.exe` from that directory. The whole directory is
required because Qt runtime libraries are bundled beside the executable.

## Operating sequence

1. Install the CH340/CP210x USB serial driver if Windows did not detect the
   controller automatically.
2. Connect the ESP32 and select the serial port.
3. Click `连接设备` and wait for the firmware handshake.
4. Configure latitude, longitude, UTC offset and window orientation.
5. Select a mode and click `启动追踪`.
6. On first startup, the ESP32 homes to the minimum limit before accepting
   motion.
7. Use `停止运动` for normal stop and the physical emergency stop for an
   actual hazardous condition.

The application sends a heartbeat every 250 ms. The ESP32 stops and latches a
communication fault if valid commands are absent for 1500 ms after startup.

## Important hardware note

The PC is not part of the safety chain. Limit inputs, emergency stop, motor
enable, command timeout and step generation remain on the ESP32.
