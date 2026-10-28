"""Persistent application configuration."""

from __future__ import annotations

import json
from dataclasses import asdict, dataclass, fields
from pathlib import Path
from typing import Any

from .protocol import OperatingMode


@dataclass(slots=True)
class AppConfig:
    port: str = ""
    baud_rate: int = 115200
    latitude_deg: float = 30.5728
    longitude_deg: float = 104.0668
    utc_offset_hours: float = 8.0
    window_azimuth_deg: float = 180.0
    cooling_target_deg: float = 90.0
    light_kp: float = 15.0
    light_ki: float = 0.5
    light_kd: float = 0.2
    light_integral_limit_deg: float = 20.0
    light_correction_limit_deg: float = 25.0
    deadband_deg: float = 1.0
    max_slew_deg_per_s: float = 12.0
    manual_target_deg: float = 0.0
    mode: OperatingMode = OperatingMode.AUTO

    def validate(self) -> None:
        if self.baud_rate <= 0:
            raise ValueError("baud rate must be positive")
        if not -90.0 <= self.latitude_deg <= 90.0:
            raise ValueError("invalid latitude")
        if not -180.0 <= self.longitude_deg <= 180.0:
            raise ValueError("invalid longitude")
        if not -14.0 <= self.utc_offset_hours <= 14.0:
            raise ValueError("invalid UTC offset")
        if not -90.0 <= self.manual_target_deg <= 90.0:
            raise ValueError("invalid manual target")

    def to_dict(self) -> dict[str, Any]:
        data = asdict(self)
        data["mode"] = int(self.mode)
        return data

    @classmethod
    def from_dict(cls, data: dict[str, Any]) -> "AppConfig":
        known = {field.name for field in fields(cls)}
        filtered = {key: value for key, value in data.items() if key in known}
        if "mode" in filtered:
            filtered["mode"] = OperatingMode(int(filtered["mode"]))
        config = cls(**filtered)
        config.validate()
        return config


def default_config_path() -> Path:
    return Path.home() / ".smart_window" / "config.json"


def load_config(path: Path | None = None) -> AppConfig:
    config_path = path or default_config_path()
    if not config_path.exists():
        return AppConfig()
    try:
        with config_path.open("r", encoding="utf-8") as stream:
            return AppConfig.from_dict(json.load(stream))
    except (OSError, ValueError, TypeError, json.JSONDecodeError):
        return AppConfig()


def save_config(config: AppConfig, path: Path | None = None) -> None:
    config.validate()
    config_path = path or default_config_path()
    config_path.parent.mkdir(parents=True, exist_ok=True)
    temporary = config_path.with_suffix(".tmp")
    with temporary.open("w", encoding="utf-8") as stream:
        json.dump(config.to_dict(), stream, ensure_ascii=False, indent=2)
    temporary.replace(config_path)
