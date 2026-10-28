"""Application entry point."""

from __future__ import annotations

import sys


def main() -> int:
    from PySide6.QtWidgets import QApplication

    from .ui import MainWindow, apply_theme

    application = QApplication(sys.argv)
    application.setApplicationName("智能环境自适应旋转窗控制系统")
    application.setOrganizationName("SmartWindow")
    apply_theme(application)
    window = MainWindow()
    window.show()
    return application.exec()


if __name__ == "__main__":
    raise SystemExit(main())
