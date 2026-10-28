"""PySide6 desktop interface for the rotating-window prototype."""

from __future__ import annotations

import time
from collections import deque
from datetime import datetime
from typing import Deque

import pyqtgraph as pg
from PySide6.QtCore import QElapsedTimer, Qt, QTimer
from PySide6.QtGui import QCloseEvent, QFont, QFontDatabase
from PySide6.QtWidgets import (
    QApplication,
    QCheckBox,
    QComboBox,
    QDoubleSpinBox,
    QFormLayout,
    QFrame,
    QGridLayout,
    QGroupBox,
    QHBoxLayout,
    QLabel,
    QMainWindow,
    QMessageBox,
    QPlainTextEdit,
    QPushButton,
    QSplitter,
    QVBoxLayout,
    QWidget,
)

from .app_config import AppConfig, load_config, save_config
from .controller import ControlSettings, TrackingController
from .protocol import (
    FaultCode,
    Frame,
    MessageType,
    OperatingMode,
    StatusFlag,
    parse_ack,
    parse_telemetry,
)
from .serial_link import SerialLink, available_ports, preferred_port
from .solar import calculate_solar_position

MODE_LABELS = (
    ("自动天文追光", OperatingMode.AUTO),
    ("光强寻优", OperatingMode.HARVEST),
    ("辐射制冷姿态", OperatingMode.COOLING),
    ("手动角度", OperatingMode.MANUAL),
)


class MetricCard(QFrame):
    def __init__(self, title: str, value: str = "--") -> None:
        super().__init__()
        self.setObjectName("metricCard")
        layout = QVBoxLayout(self)
        layout.setContentsMargins(12, 8, 12, 8)
        self.title_label = QLabel(title)
        self.title_label.setObjectName("metricTitle")
        self.value_label = QLabel(value)
        self.value_label.setObjectName("metricValue")
        self.value_label.setAlignment(Qt.AlignmentFlag.AlignRight)
        layout.addWidget(self.title_label)
        layout.addWidget(self.value_label)

    def set_value(self, value: str) -> None:
        self.value_label.setText(value)


class MainWindow(QMainWindow):
    def __init__(self, config: AppConfig | None = None) -> None:
        super().__init__()
        self.config = config or load_config()
        self.serial_link = SerialLink(self.config.baud_rate)
        self.controller = TrackingController(self._controller_settings())
        self.running = False
        self.device_ready = False
        self.last_telemetry = None
        self._last_control = QElapsedTimer()
        self._last_control.start()
        self._samples: Deque[tuple[float, int, int, int, int]] = deque(maxlen=240)

        self.setWindowTitle("智能环境自适应旋转窗控制系统")
        self.setMinimumSize(1200, 800)
        self.resize(1440, 900)

        self._build_ui()
        self._bind_config()
        self._connect_signals()
        self._create_timers()
        self.refresh_ports()
        self._log("软件已启动，选择串口后连接设备。")

    def _controller_settings(self) -> ControlSettings:
        return ControlSettings(
            window_azimuth_deg=self.config.window_azimuth_deg,
            cooling_target_deg=self.config.cooling_target_deg,
            light_kp=self.config.light_kp,
            light_ki=self.config.light_ki,
            light_kd=self.config.light_kd,
            light_integral_limit_deg=self.config.light_integral_limit_deg,
            light_correction_limit_deg=self.config.light_correction_limit_deg,
            deadband_deg=self.config.deadband_deg,
            max_slew_deg_per_s=self.config.max_slew_deg_per_s,
        )

    def _build_ui(self) -> None:
        central = QWidget()
        self.setCentralWidget(central)
        root = QVBoxLayout(central)
        root.setContentsMargins(18, 16, 18, 16)
        root.setSpacing(12)

        header = QHBoxLayout()
        title = QLabel("智能旋转窗控制台")
        title.setObjectName("windowTitle")
        subtitle = QLabel("太阳方位 · 光强闭环 · 串口执行")
        subtitle.setObjectName("subtitle")
        header.addWidget(title)
        header.addWidget(subtitle)
        header.addStretch(1)
        self.connection_badge = QLabel("未连接")
        self.connection_badge.setObjectName("connectionBadge")
        header.addWidget(self.connection_badge)
        root.addLayout(header)

        root.addWidget(self._build_connection_group())
        root.addLayout(self._build_metrics())

        splitter = QSplitter(Qt.Orientation.Horizontal)
        splitter.addWidget(self._build_control_panel())
        splitter.addWidget(self._build_plot_panel())
        splitter.setStretchFactor(0, 1)
        splitter.setStretchFactor(1, 3)
        splitter.setSizes([380, 1020])
        root.addWidget(splitter, 1)
        root.addWidget(self._build_log_panel())

    def _build_connection_group(self) -> QGroupBox:
        group = QGroupBox("设备连接")
        layout = QHBoxLayout(group)

        layout.addWidget(QLabel("串口"))
        self.port_combo = QComboBox()
        self.port_combo.setMinimumWidth(230)
        layout.addWidget(self.port_combo)

        self.refresh_button = QPushButton("刷新")
        self.auto_connect_button = QPushButton("自动连接")
        self.connect_button = QPushButton("连接设备")
        self.connect_button.setObjectName("primaryButton")
        layout.addWidget(self.refresh_button)
        layout.addWidget(self.auto_connect_button)
        layout.addWidget(self.connect_button)
        layout.addStretch(1)

        self.connection_label = QLabel("USB 串口 115200 8N1")
        self.connection_label.setObjectName("secondaryText")
        layout.addWidget(self.connection_label)
        return group

    def _build_metrics(self) -> QGridLayout:
        layout = QGridLayout()
        layout.setHorizontalSpacing(10)
        self.angle_card = MetricCard("当前角度", "-- °")
        self.target_card = MetricCard("目标角度", "-- °")
        self.solar_card = MetricCard("太阳方位 / 高度", "--")
        self.status_card = MetricCard("运行状态", "待连接")
        for index, card in enumerate(
            (
                self.angle_card,
                self.target_card,
                self.solar_card,
                self.status_card,
            )
        ):
            layout.addWidget(card, 0, index)
        return layout

    def _build_control_panel(self) -> QWidget:
        panel = QWidget()
        panel.setMinimumWidth(370)
        layout = QVBoxLayout(panel)
        layout.setContentsMargins(0, 0, 0, 0)

        mode_group = QGroupBox("运行控制")
        mode_layout = QVBoxLayout(mode_group)
        self.mode_combo = QComboBox()
        for label, mode in MODE_LABELS:
            self.mode_combo.addItem(label, int(mode))
        mode_layout.addWidget(self.mode_combo)

        buttons = QHBoxLayout()
        self.start_button = QPushButton("启动追踪")
        self.start_button.setObjectName("primaryButton")
        self.stop_button = QPushButton("停止运动")
        self.emergency_button = QPushButton("立即停止")
        self.emergency_button.setObjectName("dangerButton")
        buttons.addWidget(self.start_button)
        buttons.addWidget(self.stop_button)
        buttons.addWidget(self.emergency_button)
        mode_layout.addLayout(buttons)

        self.clear_fault_button = QPushButton("清除故障")
        mode_layout.addWidget(self.clear_fault_button)
        layout.addWidget(mode_group)

        location_group = QGroupBox("太阳位置参数")
        form = QFormLayout(location_group)
        form.setVerticalSpacing(7)
        self.latitude_spin = self._double_spin(-90.0, 90.0, 4)
        self.longitude_spin = self._double_spin(-180.0, 180.0, 4)
        self.timezone_spin = self._double_spin(-14.0, 14.0, 1)
        self.window_azimuth_spin = self._double_spin(0.0, 360.0, 1)
        form.addRow("纬度 (°)", self.latitude_spin)
        form.addRow("经度 (°)", self.longitude_spin)
        form.addRow("UTC 时区", self.timezone_spin)
        form.addRow("窗口朝向 (°)", self.window_azimuth_spin)
        layout.addWidget(location_group)

        target_group = QGroupBox("目标与增益")
        target_form = QFormLayout(target_group)
        target_form.setVerticalSpacing(7)
        self.manual_target_spin = self._double_spin(-90.0, 90.0, 1)
        self.cooling_target_spin = self._double_spin(-90.0, 90.0, 1)
        self.light_gain_spin = self._double_spin(0.0, 50.0, 2)
        self.deadband_spin = self._double_spin(0.0, 10.0, 1)
        self.slew_spin = self._double_spin(0.5, 60.0, 1)
        target_form.addRow("手动角度 (°)", self.manual_target_spin)
        target_form.addRow("制冷姿态 (°)", self.cooling_target_spin)
        target_form.addRow("光差 P 增益", self.light_gain_spin)
        target_form.addRow("角度死区 (°)", self.deadband_spin)
        target_form.addRow("最大速度 (°/s)", self.slew_spin)
        layout.addWidget(target_group)

        self.auto_heartbeat_check = QCheckBox("保持通信心跳")
        self.auto_heartbeat_check.setChecked(True)
        layout.addWidget(self.auto_heartbeat_check)
        layout.addStretch(1)
        return panel

    def _build_plot_panel(self) -> QWidget:
        panel = QWidget()
        layout = QVBoxLayout(panel)
        layout.setContentsMargins(0, 0, 0, 0)

        title_row = QHBoxLayout()
        title = QLabel("四向相对光强")
        title.setObjectName("sectionTitle")
        self.average_label = QLabel("平均光强 --")
        self.average_label.setObjectName("secondaryText")
        title_row.addWidget(title)
        title_row.addStretch(1)
        title_row.addWidget(self.average_label)
        layout.addLayout(title_row)

        self.plot_widget = pg.PlotWidget(axisItems={"bottom": pg.DateAxisItem()})
        self.plot_widget.setBackground("#111827")
        self.plot_widget.showGrid(x=True, y=True, alpha=0.2)
        self.plot_widget.setLabel("left", "相对光强")
        self.plot_widget.setLabel("bottom", "时间")
        self.plot_widget.addLegend(offset=(12, 10))
        self.curves = {
            "左": self.plot_widget.plot(pen=pg.mkPen("#38BDF8", width=2), name="左"),
            "右": self.plot_widget.plot(pen=pg.mkPen("#FBBF24", width=2), name="右"),
            "上": self.plot_widget.plot(pen=pg.mkPen("#34D399", width=2), name="上"),
            "下": self.plot_widget.plot(pen=pg.mkPen("#F472B6", width=2), name="下"),
        }
        layout.addWidget(self.plot_widget, 1)
        return panel

    def _build_log_panel(self) -> QWidget:
        group = QGroupBox("运行日志")
        layout = QVBoxLayout(group)
        self.log_view = QPlainTextEdit()
        self.log_view.setReadOnly(True)
        self.log_view.setMaximumBlockCount(600)
        self.log_view.setMaximumHeight(130)
        layout.addWidget(self.log_view)
        return group

    def _double_spin(
        self,
        minimum: float,
        maximum: float,
        decimals: int,
    ) -> QDoubleSpinBox:
        spin = QDoubleSpinBox()
        spin.setRange(minimum, maximum)
        spin.setDecimals(decimals)
        spin.setSingleStep(0.1 if decimals <= 2 else 0.0001)
        spin.setMinimumWidth(120)
        spin.setMinimumHeight(30)
        return spin

    def _bind_config(self) -> None:
        self.latitude_spin.setValue(self.config.latitude_deg)
        self.longitude_spin.setValue(self.config.longitude_deg)
        self.timezone_spin.setValue(self.config.utc_offset_hours)
        self.window_azimuth_spin.setValue(self.config.window_azimuth_deg)
        self.cooling_target_spin.setValue(self.config.cooling_target_deg)
        self.light_gain_spin.setValue(self.config.light_kp)
        self.deadband_spin.setValue(self.config.deadband_deg)
        self.slew_spin.setValue(self.config.max_slew_deg_per_s)
        self.manual_target_spin.setValue(self.config.manual_target_deg)
        index = self.mode_combo.findData(int(self.config.mode))
        if index >= 0:
            self.mode_combo.setCurrentIndex(index)

    def _connect_signals(self) -> None:
        self.refresh_button.clicked.connect(self.refresh_ports)
        self.auto_connect_button.clicked.connect(self.auto_connect)
        self.connect_button.clicked.connect(self.toggle_connection)
        self.start_button.clicked.connect(self.start_control)
        self.stop_button.clicked.connect(self.stop_control)
        self.emergency_button.clicked.connect(self.stop_control)
        self.clear_fault_button.clicked.connect(self.clear_fault)
        self.manual_target_spin.valueChanged.connect(self._manual_target_changed)
        self.mode_combo.currentIndexChanged.connect(self._mode_changed)

    def _create_timers(self) -> None:
        self.poll_timer = QTimer(self)
        self.poll_timer.setInterval(20)
        self.poll_timer.timeout.connect(self.poll_serial)

        self.heartbeat_timer = QTimer(self)
        self.heartbeat_timer.setInterval(250)
        self.heartbeat_timer.timeout.connect(self.send_heartbeat)

        self.control_timer = QTimer(self)
        self.control_timer.setInterval(500)
        self.control_timer.timeout.connect(self.update_control_target)

    def refresh_ports(self) -> None:
        ports = available_ports()
        selected = self.port_combo.currentData()
        self.port_combo.clear()
        for port in ports:
            self.port_combo.addItem(f"{port.device} · {port.description}", port.device)
            self.port_combo.setItemData(
                self.port_combo.count() - 1,
                port.hardware_id,
                Qt.ItemDataRole.ToolTipRole,
            )
        preferred = preferred_port(ports)
        if preferred:
            index = self.port_combo.findData(preferred)
            if index >= 0:
                self.port_combo.setCurrentIndex(index)
        elif selected:
            index = self.port_combo.findData(selected)
            if index >= 0:
                self.port_combo.setCurrentIndex(index)
        self._log(f"检测到 {len(ports)} 个串口。")

    def auto_connect(self) -> None:
        self.refresh_ports()
        port = self.port_combo.currentData()
        if port:
            self._connect_port(str(port))
        else:
            QMessageBox.warning(self, "未找到串口", "请检查 USB 数据线和 CH340 驱动。")

    def toggle_connection(self) -> None:
        if self.serial_link.connected:
            self._disconnect_port()
            return
        port = self.port_combo.currentData()
        if not port:
            QMessageBox.warning(self, "未选择串口", "请选择一个 USB 串口。")
            return
        self._connect_port(str(port))

    def _connect_port(self, port: str) -> None:
        try:
            self.serial_link.connect(port)
            self.serial_link.hello()
        except Exception as exc:
            self._log(f"串口连接失败：{exc}")
            QMessageBox.critical(self, "连接失败", str(exc))
            return

        self.device_ready = False
        self.connection_badge.setText(f"已连接 {port}")
        self.connection_badge.setProperty("connected", True)
        self.connection_badge.style().unpolish(self.connection_badge)
        self.connection_badge.style().polish(self.connection_badge)
        self.connect_button.setText("断开")
        self.config.port = port
        self.poll_timer.start()
        self.heartbeat_timer.start()
        self.control_timer.start()
        self._log(f"已打开 {port}，等待设备握手。")

    def _disconnect_port(self) -> None:
        if self.running:
            self.stop_control()
        self.poll_timer.stop()
        self.heartbeat_timer.stop()
        self.control_timer.stop()
        self.serial_link.disconnect()
        self.device_ready = False
        self.connection_badge.setText("未连接")
        self.connection_badge.setProperty("connected", False)
        self.connection_badge.style().unpolish(self.connection_badge)
        self.connection_badge.style().polish(self.connection_badge)
        self.connect_button.setText("连接设备")
        self._log("串口已断开。")

    def _current_config(self) -> AppConfig:
        self.config.latitude_deg = self.latitude_spin.value()
        self.config.longitude_deg = self.longitude_spin.value()
        self.config.utc_offset_hours = self.timezone_spin.value()
        self.config.window_azimuth_deg = self.window_azimuth_spin.value()
        self.config.cooling_target_deg = self.cooling_target_spin.value()
        self.config.light_kp = self.light_gain_spin.value()
        self.config.deadband_deg = self.deadband_spin.value()
        self.config.max_slew_deg_per_s = self.slew_spin.value()
        self.config.manual_target_deg = self.manual_target_spin.value()
        self.config.mode = OperatingMode(self.mode_combo.currentData())
        self.config.validate()
        self.controller.settings = self._controller_settings()
        return self.config

    def send_heartbeat(self) -> None:
        if self.serial_link.connected and self.auto_heartbeat_check.isChecked():
            try:
                self.serial_link.heartbeat()
            except Exception as exc:
                self._log(f"心跳发送失败：{exc}")
                self._disconnect_port()

    def start_control(self) -> None:
        if not self.serial_link.connected:
            QMessageBox.warning(self, "未连接", "请先连接控制设备。")
            return
        if not self.device_ready:
            QMessageBox.warning(self, "等待握手", "尚未收到设备握手响应。")
            return
        try:
            config = self._current_config()
            self.serial_link.set_mode(config.mode)
            if self.last_telemetry is not None:
                self.controller.reset(self.last_telemetry.angle_deg)
            self.serial_link.set_target(
                self.last_telemetry.angle_deg if self.last_telemetry else 0.0
            )
            self.serial_link.start()
            self.running = True
            self._last_control.restart()
            self.status_card.set_value("启动中")
            self._log("已发送启动命令，设备将先执行限位归零。")
        except Exception as exc:
            self._log(f"启动失败：{exc}")

    def stop_control(self) -> None:
        self.running = False
        if self.serial_link.connected:
            try:
                self.serial_link.stop()
            except Exception as exc:
                self._log(f"停止命令发送失败：{exc}")
        self.status_card.set_value("已停止")
        self._log("已发送停止命令。")

    def clear_fault(self) -> None:
        if not self.serial_link.connected:
            return
        try:
            self.serial_link.clear_fault()
            self._log("已发送故障复位命令。")
        except Exception as exc:
            self._log(f"故障复位失败：{exc}")

    def _manual_target_changed(self, value: float) -> None:
        if (
            self.serial_link.connected
            and self.device_ready
            and self.mode_combo.currentData() == int(OperatingMode.MANUAL)
        ):
            try:
                self.serial_link.set_target(value)
            except Exception as exc:
                self._log(f"手动角度发送失败：{exc}")

    def _mode_changed(self) -> None:
        if self.serial_link.connected and self.device_ready:
            try:
                self.serial_link.set_mode(OperatingMode(self.mode_combo.currentData()))
            except Exception as exc:
                self._log(f"模式发送失败：{exc}")

    def update_control_target(self) -> None:
        if not self.running or not self.serial_link.connected:
            return
        try:
            config = self._current_config()
            now = datetime.now()
            solar = calculate_solar_position(
                now,
                config.latitude_deg,
                config.longitude_deg,
                config.utc_offset_hours,
            )
            self.solar_card.set_value(
                f"{solar.azimuth_deg:.1f}° / {solar.elevation_deg:.1f}°"
            )
            if self.last_telemetry is None:
                return
            elapsed_s = max(0.05, self._last_control.restart() / 1000.0)
            decision = self.controller.update(
                config.mode,
                self.last_telemetry,
                solar,
                config.manual_target_deg,
                elapsed_s,
            )
            self.serial_link.set_target(decision.target_angle_deg)
            self.target_card.set_value(f"{decision.target_angle_deg:.1f} °")
        except Exception as exc:
            self._log(f"追光计算失败：{exc}")
            self.stop_control()

    def poll_serial(self) -> None:
        try:
            frames = self.serial_link.poll()
        except Exception as exc:
            self._log(f"串口读取失败：{exc}")
            self._disconnect_port()
            return
        for frame in frames:
            self._handle_frame(frame)

    def _handle_frame(self, frame: Frame) -> None:
        if frame.message_type == MessageType.HELLO_ACK:
            if frame.payload[:4] == b"SWC1":
                self.device_ready = True
                version = ".".join(str(value) for value in frame.payload[4:7])
                self._log(f"设备握手成功，固件 {version}。")
            return

        if frame.message_type == MessageType.TELEMETRY:
            try:
                telemetry = parse_telemetry(frame)
            except ValueError as exc:
                self._log(f"遥测解析失败：{exc}")
                return
            self._handle_telemetry_frame(telemetry)
            return

        if frame.message_type == MessageType.ACK:
            acknowledgement = parse_ack(frame)
            if int(acknowledgement.result) != 0:
                self._log(
                    f"设备拒绝命令 0x{acknowledgement.command:02X}，"
                    f"结果 {int(acknowledgement.result)}。"
                )
            return

        if frame.message_type == MessageType.FAULT and len(frame.payload) >= 2:
            code = frame.payload[0]
            try:
                name = FaultCode(code).name
            except ValueError:
                name = f"UNKNOWN_{code}"
            self.status_card.set_value(f"故障 {name}")
            self._log(f"设备故障：{name}，详情 0x{frame.payload[1]:02X}。")

    def _handle_telemetry_frame(self, telemetry) -> None:
        self.last_telemetry = telemetry
        self.angle_card.set_value(f"{telemetry.angle_deg:.1f} °")
        self.average_label.setText(f"平均光强 {telemetry.average_light:.0f}")

        status_labels = {
            StatusFlag.ENABLED: "已使能",
            StatusFlag.HOMING: "归零中",
            StatusFlag.MOVING: "运动中",
            StatusFlag.LIMIT_MIN: "下限位",
            StatusFlag.LIMIT_MAX: "上限位",
            StatusFlag.EMERGENCY_STOP: "急停",
            StatusFlag.COMM_TIMEOUT: "通信超时",
            StatusFlag.SENSOR_FAULT: "传感器故障",
            StatusFlag.MOTOR_FAULT: "电机故障",
            StatusFlag.HOMED: "已归零",
        }
        status_names = [
            label
            for flag, label in status_labels.items()
            if telemetry.has_status(flag)
        ]
        self.status_card.set_value(", ".join(status_names) or "待机")

        if (
            telemetry.has_status(StatusFlag.EMERGENCY_STOP)
            or telemetry.has_status(StatusFlag.MOTOR_FAULT)
            or telemetry.has_status(StatusFlag.COMM_TIMEOUT)
        ):
            self.running = False

        timestamp = time.time()
        self._samples.append(
            (
                timestamp,
                telemetry.light_left,
                telemetry.light_right,
                telemetry.light_up,
                telemetry.light_down,
            )
        )
        if len(self._samples) < 2:
            return
        x_values = [sample[0] for sample in self._samples]
        for index, key in enumerate(("左", "右", "上", "下"), start=1):
            y_values = [sample[index] for sample in self._samples]
            self.curves[key].setData(x_values, y_values)

    def _log(self, message: str) -> None:
        stamp = datetime.now().strftime("%H:%M:%S")
        self.log_view.appendPlainText(f"[{stamp}] {message}")

    def closeEvent(self, event: QCloseEvent) -> None:
        try:
            self._current_config()
            save_config(self.config)
        except Exception:
            pass
        if self.serial_link.connected:
            try:
                self.serial_link.stop()
            except Exception:
                pass
            self.serial_link.disconnect()
        event.accept()


def apply_theme(app: QApplication) -> None:
    app.setStyle("Fusion")
    families = set(QFontDatabase.families())
    preferred_fonts = (
        "Microsoft YaHei UI",
        "Microsoft YaHei",
        "Noto Sans SC",
        "Noto Sans CJK SC",
        "Source Han Sans SC",
        "WenQuanYi Micro Hei",
    )
    font_family = next(
        (family for family in preferred_fonts if family in families),
        app.font().family(),
    )
    app.setFont(QFont(font_family, 10))
    app.setStyleSheet(
        """
        QMainWindow, QWidget {
            background: #0B1120;
            color: #E5E7EB;
        }
        QGroupBox {
            border: 1px solid #263247;
            border-radius: 10px;
            margin-top: 12px;
            padding-top: 14px;
            font-weight: 600;
        }
        QGroupBox::title {
            subcontrol-origin: margin;
            left: 12px;
            padding: 0 6px;
            color: #93C5FD;
        }
        QPushButton {
            background: #1E293B;
            border: 1px solid #334155;
            border-radius: 7px;
            padding: 8px 14px;
        }
        QPushButton:hover {
            background: #263449;
            border-color: #60A5FA;
        }
        QPushButton#primaryButton {
            background: #2563EB;
            border-color: #3B82F6;
            font-weight: 600;
        }
        QPushButton#primaryButton:hover {
            background: #1D4ED8;
        }
        QPushButton#dangerButton {
            background: #991B1B;
            border-color: #EF4444;
            font-weight: 700;
        }
        QComboBox, QDoubleSpinBox, QPlainTextEdit {
            background: #111827;
            border: 1px solid #334155;
            border-radius: 6px;
            padding: 6px;
            selection-background-color: #2563EB;
        }
        QComboBox, QDoubleSpinBox {
            min-height: 24px;
        }
        QLabel#windowTitle {
            font-size: 24px;
            font-weight: 700;
            color: #F8FAFC;
        }
        QLabel#subtitle, QLabel#secondaryText, QLabel#metricTitle {
            color: #94A3B8;
        }
        QLabel#sectionTitle {
            font-size: 16px;
            font-weight: 600;
        }
        QLabel#connectionBadge {
            background: #451A1A;
            color: #FCA5A5;
            border-radius: 12px;
            padding: 5px 12px;
        }
        QLabel#connectionBadge[connected="true"] {
            background: #064E3B;
            color: #6EE7B7;
        }
        QFrame#metricCard {
            background: #111827;
            border: 1px solid #263247;
            border-radius: 10px;
        }
        QLabel#metricValue {
            font-size: 21px;
            font-weight: 700;
            color: #F8FAFC;
        }
        QSplitter::handle {
            background: #1E293B;
            width: 2px;
        }
        """
    )
