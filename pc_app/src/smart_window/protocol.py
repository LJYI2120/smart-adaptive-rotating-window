"""Binary serial protocol shared with the ESP32 firmware."""

from __future__ import annotations

import struct
from dataclasses import dataclass
from enum import IntEnum
from typing import Iterable

SYNC = b"\xAA\x55"
PROTOCOL_VERSION = 0x01
MAX_PAYLOAD = 64
FRAME_OVERHEAD = 8


class MessageType(IntEnum):
    HELLO = 0x01
    SET_TARGET = 0x10
    SET_MODE = 0x11
    START = 0x12
    STOP = 0x13
    CLEAR_FAULT = 0x14
    HEARTBEAT = 0x15
    HELLO_ACK = 0x81
    TELEMETRY = 0x82
    ACK = 0x83
    FAULT = 0x84


class OperatingMode(IntEnum):
    AUTO = 0
    HARVEST = 1
    COOLING = 2
    MANUAL = 3
    STOPPED = 4


class CommandResult(IntEnum):
    SUCCESS = 0
    REJECTED = 1
    INVALID_PAYLOAD = 2
    UNKNOWN_COMMAND = 3
    NOT_HOMED = 4
    EMERGENCY_STOP = 5
    MOTOR_FAULT = 6
    LIMIT_REACHED = 7


class StatusFlag(IntEnum):
    ENABLED = 1 << 0
    HOMING = 1 << 1
    MOVING = 1 << 2
    LIMIT_MIN = 1 << 3
    LIMIT_MAX = 1 << 4
    EMERGENCY_STOP = 1 << 5
    COMM_TIMEOUT = 1 << 6
    SENSOR_FAULT = 1 << 7
    MOTOR_FAULT = 1 << 8
    HOMED = 1 << 9


class FaultCode(IntEnum):
    NONE = 0
    BAD_FRAME = 1
    COMM_TIMEOUT = 2
    LIMIT = 3
    EMERGENCY_STOP = 4
    SENSOR = 5
    MOTOR = 6
    HOMING_TIMEOUT = 7
    INTERNAL = 8


@dataclass(frozen=True, slots=True)
class Frame:
    message_type: int
    sequence: int
    payload: bytes
    version: int = PROTOCOL_VERSION


@dataclass(frozen=True, slots=True)
class Telemetry:
    sequence: int
    angle_deg: float
    status: int
    light_left: int
    light_right: int
    light_up: int
    light_down: int
    mode: OperatingMode
    sensor_mask: int
    uptime_s: int

    def has_status(self, flag: StatusFlag) -> bool:
        return bool(self.status & int(flag))

    @property
    def average_light(self) -> float:
        return (
            self.light_left
            + self.light_right
            + self.light_up
            + self.light_down
        ) / 4.0


@dataclass(frozen=True, slots=True)
class Acknowledgement:
    sequence: int
    acked_sequence: int
    command: int
    result: CommandResult


def crc16_ccitt(data: bytes | bytearray | memoryview) -> int:
    """CRC-16/CCITT-FALSE."""
    crc = 0xFFFF
    for value in data:
        crc ^= int(value) << 8
        for _ in range(8):
            if crc & 0x8000:
                crc = ((crc << 1) ^ 0x1021) & 0xFFFF
            else:
                crc = (crc << 1) & 0xFFFF
    return crc


def build_frame(message_type: int, sequence: int, payload: bytes = b"") -> bytes:
    if not 0 <= int(message_type) <= 0xFF:
        raise ValueError("message type must fit in one byte")
    if not 0 <= int(sequence) <= 0xFF:
        raise ValueError("sequence must fit in one byte")
    if len(payload) > MAX_PAYLOAD:
        raise ValueError(f"payload exceeds {MAX_PAYLOAD} bytes")

    body = bytes(
        (
            PROTOCOL_VERSION,
            int(message_type),
            int(sequence),
            len(payload),
        )
    ) + payload
    crc = crc16_ccitt(body)
    return SYNC + body + struct.pack("<H", crc)


def build_hello(sequence: int) -> bytes:
    return build_frame(MessageType.HELLO, sequence, b"SWC1")


def build_target(sequence: int, angle_deg: float) -> bytes:
    centidegrees = round(float(angle_deg) * 100.0)
    if not -32768 <= centidegrees <= 32767:
        raise ValueError("angle cannot be represented")
    return build_frame(
        MessageType.SET_TARGET,
        sequence,
        struct.pack("<h", centidegrees),
    )


def build_mode(sequence: int, mode: OperatingMode) -> bytes:
    return build_frame(MessageType.SET_MODE, sequence, bytes((int(mode),)))


def build_simple(message_type: MessageType, sequence: int) -> bytes:
    return build_frame(message_type, sequence)


def parse_telemetry(frame: Frame) -> Telemetry:
    if frame.message_type != MessageType.TELEMETRY or len(frame.payload) != 18:
        raise ValueError("invalid telemetry frame")

    (
        angle_cdeg,
        status,
        light_left,
        light_right,
        light_up,
        light_down,
        mode_value,
        sensor_mask,
        uptime_s,
    ) = struct.unpack("<hHHHHHBBI", frame.payload)

    try:
        mode = OperatingMode(mode_value)
    except ValueError as exc:
        raise ValueError(f"unknown operating mode {mode_value}") from exc

    return Telemetry(
        sequence=frame.sequence,
        angle_deg=angle_cdeg / 100.0,
        status=status,
        light_left=light_left,
        light_right=light_right,
        light_up=light_up,
        light_down=light_down,
        mode=mode,
        sensor_mask=sensor_mask,
        uptime_s=uptime_s,
    )


def parse_ack(frame: Frame) -> Acknowledgement:
    if frame.message_type != MessageType.ACK or len(frame.payload) != 3:
        raise ValueError("invalid acknowledgement frame")
    acked_sequence, command, result_value = frame.payload
    try:
        result = CommandResult(result_value)
    except ValueError:
        result = CommandResult.REJECTED
    return Acknowledgement(
        sequence=frame.sequence,
        acked_sequence=acked_sequence,
        command=command,
        result=result,
    )


class FrameParser:
    """Incremental parser tolerant of fragmented reads and leading noise."""

    def __init__(self) -> None:
        self._buffer = bytearray()
        self.error_count = 0

    def feed(self, data: bytes | bytearray | memoryview) -> list[Frame]:
        if data:
            self._buffer.extend(data)

        frames: list[Frame] = []
        while True:
            sync_index = self._buffer.find(SYNC)
            if sync_index < 0:
                self._discard_noise()
                break
            if sync_index > 0:
                del self._buffer[:sync_index]
            if len(self._buffer) < 6:
                break

            version = self._buffer[2]
            payload_length = self._buffer[5]
            if version != PROTOCOL_VERSION or payload_length > MAX_PAYLOAD:
                self.error_count += 1
                del self._buffer[0]
                continue

            total_length = FRAME_OVERHEAD + payload_length
            if len(self._buffer) < total_length:
                break

            candidate = bytes(self._buffer[:total_length])
            expected_crc = int.from_bytes(candidate[-2:], "little")
            actual_crc = crc16_ccitt(candidate[2:-2])
            if expected_crc != actual_crc:
                self.error_count += 1
                del self._buffer[0]
                continue

            frames.append(
                Frame(
                    version=version,
                    message_type=candidate[3],
                    sequence=candidate[4],
                    payload=candidate[6:-2],
                )
            )
            del self._buffer[:total_length]

        return frames

    def _discard_noise(self) -> None:
        if self._buffer.endswith(b"\xAA"):
            self._buffer[:] = b"\xAA"
        else:
            self._buffer.clear()


def iter_encoded_frames(frames: Iterable[Frame]) -> bytes:
    return b"".join(
        build_frame(frame.message_type, frame.sequence, frame.payload)
        for frame in frames
    )
