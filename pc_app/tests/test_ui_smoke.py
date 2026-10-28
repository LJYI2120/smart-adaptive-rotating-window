from __future__ import annotations

import os

os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")

import pytest

QtWidgets = pytest.importorskip("PySide6.QtWidgets")

from smart_window.app_config import AppConfig
from smart_window.ui import MainWindow


def test_main_window_constructs_offscreen() -> None:
    application = QtWidgets.QApplication.instance()
    if application is None:
        application = QtWidgets.QApplication([])
    window = MainWindow(AppConfig())
    assert window.windowTitle() == "智能环境自适应旋转窗控制系统"
    assert not window.running
    window.close()
