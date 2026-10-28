from __future__ import annotations

import struct

import pytest

from smart_window.protocol import (
    Frame,
    FrameParser,
    MessageType,
    OperatingMode,
    build_frame,
    build_hello,
    build_target,
    crc16_ccitt,
    parse_telemetry,
)


def telemetry_frame(
    angle_cdeg: int = 1234,
    status: int = 0x0207,
    sequence: int = 9,
) -> Frame:
    payload = struct.pack(
        "<hHHHHHBBI",
        angle_cdeg,
        status,
        120,
        240,
        360,
        480,
        int(OperatingMode.AUTO),
        0x0F,
        42,
    )
    return Frame(MessageType.TELEMETRY, sequence, payload)


def test_crc16_known_vector() -> None:
    assert crc16_ccitt(b"123456789") == 0x29B1


def test_frame_round_trip_fragmented_and_noisy() -> None:
    encoded = build_hello(3)
    parser = FrameParser()

    assert parser.feed(b"noise" + encoded[:2]) == []
    assert parser.feed(encoded[2:7]) == []
    frames = parser.feed(encoded[7:])

    assert len(frames) == 1
    assert frames[0].message_type == MessageType.HELLO
    assert frames[0].sequence == 3
    assert frames[0].payload == b"SWC1"


def test_corrupt_frame_is_rejected_and_parser_recovers() -> None:
    bad = bytearray(build_target(1, 25.0))
    bad[-1] ^= 0x80
    parser = FrameParser()

    assert parser.feed(bytes(bad) + build_target(2, -10.0)) != []
    assert parser.error_count >= 1


def test_target_encoding_uses_centidegrees() -> None:
    encoded = build_target(7, -12.34)
    assert encoded.hex() == "aa55011007022efb3a0b"
    frame = FrameParser().feed(encoded)[0]
    assert frame.message_type == MessageType.SET_TARGET
    value = struct.unpack("<h", frame.payload)[0]
    assert value == -1234


def test_telemetry_parsing() -> None:
    frame = telemetry_frame()
    parsed = parse_telemetry(frame)

    assert parsed.angle_deg == pytest.approx(12.34)
    assert parsed.light_left == 120
    assert parsed.light_right == 240
    assert parsed.average_light == pytest.approx(300.0)
    assert parsed.mode == OperatingMode.AUTO
    assert parsed.sensor_mask == 0x0F
    assert parsed.uptime_s == 42


def test_payload_limit() -> None:
    with pytest.raises(ValueError):
        build_frame(MessageType.HELLO, 0, b"x" * 65)
