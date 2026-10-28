from __future__ import annotations

from smart_window.app_config import AppConfig, load_config, save_config
from smart_window.protocol import OperatingMode
from smart_window.serial_link import PortInfo, preferred_port


def test_config_round_trip(tmp_path) -> None:
    path = tmp_path / "config.json"
    expected = AppConfig(
        port="COM7",
        latitude_deg=31.2,
        mode=OperatingMode.MANUAL,
    )
    save_config(expected, path)
    assert load_config(path) == expected


def test_invalid_config_falls_back_to_defaults(tmp_path) -> None:
    path = tmp_path / "config.json"
    path.write_text("{not json", encoding="utf-8")
    assert load_config(path) == AppConfig()


def test_preferred_serial_port_favors_controller() -> None:
    ports = [
        PortInfo("COM1", "Communications Port", "ACPI"),
        PortInfo("COM7", "USB-SERIAL CH340", "USB VID:PID=1A86:7523"),
    ]
    assert preferred_port(ports) == "COM7"
