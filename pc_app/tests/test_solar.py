from __future__ import annotations

from datetime import datetime

import pytest

from smart_window.solar import (
    calculate_solar_position,
    signed_relative_azimuth,
    solar_window_target,
)


def test_equatorial_noon_is_near_zenith() -> None:
    position = calculate_solar_position(
        datetime(2026, 3, 20, 12, 0),
        latitude_deg=0.0,
        longitude_deg=0.0,
        utc_offset_hours=0.0,
    )
    assert position.elevation_deg > 87.5


def test_shanghai_daytime_position_is_plausible() -> None:
    position = calculate_solar_position(
        datetime(2026, 6, 21, 14, 0),
        latitude_deg=30.5728,
        longitude_deg=104.0668,
        utc_offset_hours=8.0,
    )
    assert 30.0 < position.elevation_deg < 90.0
    assert 180.0 < position.azimuth_deg < 300.0


def test_signed_relative_azimuth_wraps_across_north() -> None:
    assert signed_relative_azimuth(350.0, 10.0) == pytest.approx(-20.0)
    assert signed_relative_azimuth(10.0, 350.0) == pytest.approx(20.0)


def test_night_target_returns_stow_angle() -> None:
    position = calculate_solar_position(
        datetime(2026, 12, 21, 1, 0),
        latitude_deg=30.0,
        longitude_deg=104.0,
        utc_offset_hours=8.0,
    )
    assert solar_window_target(position, 180.0) == 0.0
