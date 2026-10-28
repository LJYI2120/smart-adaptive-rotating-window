"""Render a deterministic UI preview for repository and manual screenshots."""

from __future__ import annotations

import math
import sys
from pathlib import Path

from PySide6.QtGui import QFontDatabase
from PySide6.QtWidgets import QApplication

from smart_window.app_config import AppConfig
from smart_window.protocol import OperatingMode, StatusFlag, Telemetry
from smart_window.ui import MainWindow, apply_theme


def main() -> int:
    app = QApplication(sys.argv)
    for font_path in (
        Path("C:/Windows/Fonts/msyh.ttc"),
        Path("C:/Windows/Fonts/NotoSansSC-VF.ttf"),
    ):
        if font_path.exists():
            QFontDatabase.addApplicationFont(str(font_path))
    apply_theme(app)
    window = MainWindow(AppConfig(port="COM7"))
    window.resize(1440, 900)
    window.show()

    for index in range(48):
        left = 920 + int(120 * math.sin(index / 6))
        right = 760 + int(80 * math.sin(index / 7))
        up = 840 + int(60 * math.sin(index / 8))
        down = 680 + int(70 * math.sin(index / 5))
        telemetry = Telemetry(
            sequence=index & 0xFF,
            angle_deg=18.0 + index * 0.25,
            status=int(
                StatusFlag.ENABLED
                | StatusFlag.MOVING
                | StatusFlag.HOMED
            ),
            light_left=left,
            light_right=right,
            light_up=up,
            light_down=down,
            mode=OperatingMode.AUTO,
            sensor_mask=0x0F,
            uptime_s=index,
        )
        window._handle_telemetry_frame(telemetry)
        app.processEvents()

    window.connection_badge.setText("界面预览")
    window.target_card.set_value("32.0 °")
    window.solar_card.set_value("214.8° / 51.2°")
    window._log("演示数据：四路光强与追光状态实时刷新。")
    app.processEvents()

    output = Path(__file__).resolve().parents[2] / "docs" / "images" / "pc_app.png"
    output.parent.mkdir(parents=True, exist_ok=True)
    if not window.grab().save(str(output), "PNG"):
        raise RuntimeError(f"failed to save screenshot: {output}")
    print(output)
    window.close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
