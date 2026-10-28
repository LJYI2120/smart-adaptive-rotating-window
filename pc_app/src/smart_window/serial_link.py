"""Serial transport and protocol dispatch for the desktop application."""

from __future__ import annotations

from dataclasses import dataclass
from typing import Any

from .protocol import (
    Frame,
    FrameParser,
    MessageType,
    OperatingMode,
    build_hello,
    build_mode,
    build_simple,
    build_target,
)


@dataclass(frozen=True, slots=True)
class PortInfo:
    device: str
    description: str
    hardware_id: str


def available_ports() -> list[PortInfo]:
    try:
        from serial.tools import list_ports
    except ImportError:
        return []
    return [
        PortInfo(port.device, port.description or "", port.hwid or "")
        for port in list_ports.comports()
    ]


def preferred_port(ports: list[PortInfo]) -> str | None:
    for candidate in ports:
        text = f"{candidate.description} {candidate.hardware_id}".lower()
        if any(
            marker in text
            for marker in ("ch340", "cp210", "esp32", "usb-serial", "303a:")
        ):
            return candidate.device
    return ports[0].device if ports else None


class SerialLink:
    """Non-blocking serial reader with sequence-aware command helpers."""

    def __init__(self, baud_rate: int = 115200) -> None:
        self.baud_rate = baud_rate
        self._serial: Any | None = None
        self._parser = FrameParser()
        self._sequence = 0

    @property
    def connected(self) -> bool:
        return bool(self._serial is not None and self._serial.is_open)

    @property
    def parse_errors(self) -> int:
        return self._parser.error_count

    def connect(self, port: str) -> None:
        try:
            import serial
        except ImportError as exc:
            raise RuntimeError("pyserial is not installed") from exc
        if not port:
            raise ValueError("serial port is required")
        self.disconnect()
        self._parser = FrameParser()
        self._serial = serial.Serial(
            port=port,
            baudrate=self.baud_rate,
            bytesize=serial.EIGHTBITS,
            parity=serial.PARITY_NONE,
            stopbits=serial.STOPBITS_ONE,
            timeout=0,
            write_timeout=0.25,
        )
        self._serial.reset_input_buffer()
        self._sequence = 0

    def disconnect(self) -> None:
        if self._serial is not None:
            try:
                self._serial.close()
            finally:
                self._serial = None

    def _next_sequence(self) -> int:
        value = self._sequence
        self._sequence = (self._sequence + 1) & 0xFF
        return value

    def _write(self, data: bytes) -> None:
        if not self.connected:
            raise ConnectionError("serial port is not connected")
        self._serial.write(data)

    def hello(self) -> int:
        sequence = self._next_sequence()
        self._write(build_hello(sequence))
        return sequence

    def set_target(self, angle_deg: float) -> int:
        sequence = self._next_sequence()
        self._write(build_target(sequence, angle_deg))
        return sequence

    def set_mode(self, mode: OperatingMode) -> int:
        sequence = self._next_sequence()
        self._write(build_mode(sequence, mode))
        return sequence

    def start(self) -> int:
        sequence = self._next_sequence()
        self._write(build_simple(MessageType.START, sequence))
        return sequence

    def stop(self) -> int:
        sequence = self._next_sequence()
        self._write(build_simple(MessageType.STOP, sequence))
        return sequence

    def clear_fault(self) -> int:
        sequence = self._next_sequence()
        self._write(build_simple(MessageType.CLEAR_FAULT, sequence))
        return sequence

    def heartbeat(self) -> int:
        sequence = self._next_sequence()
        self._write(build_simple(MessageType.HEARTBEAT, sequence))
        return sequence

    def poll(self, max_bytes: int = 4096) -> list[Frame]:
        if not self.connected:
            return []
        waiting = min(max_bytes, int(self._serial.in_waiting or 0))
        data = self._serial.read(waiting) if waiting > 0 else b""
        return self._parser.feed(data)
