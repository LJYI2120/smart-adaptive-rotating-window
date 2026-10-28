# Third-Party Notices

The C99 control core does not use third-party runtime libraries. The optional
PC application uses the following dependencies:

| Component | Version used | License | Use |
|---|---|---|---|
| Qt for Python / PySide6 | 6.12 | LGPL-3.0-only, GPL-2.0/3.0, or commercial | Desktop UI |
| pyqtgraph | 0.14 | MIT | Real-time chart |
| pyserial | 3.5 | BSD-3-Clause | USB serial |
| NumPy | 2.5 | BSD-3-Clause | pyqtgraph dependency |
| PyInstaller | 6.22 | GPL with bootloader exception | Windows packaging |

The ESP32 firmware is built with the Arduino-ESP32 framework:

| Component | License | Use |
|---|---|---|
| Arduino-ESP32 | LGPL-2.1-or-later | ESP32 board support |
| TSL2561 register interface | no bundled library | Direct I2C access |

Before distributing an executable outside the project team, preserve the
license notices supplied by the exact dependency versions. In particular,
PySide6/Qt has LGPL and commercial licensing options; confirm that the chosen
distribution model satisfies those terms. This notice is not legal advice.

The project source itself does not grant an open-source license. See the
repository README for the author's current usage terms.
