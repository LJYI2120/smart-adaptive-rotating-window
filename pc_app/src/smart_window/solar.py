"""NOAA-style solar position calculations for a single-axis window."""

from __future__ import annotations

import math
from dataclasses import dataclass
from datetime import datetime


@dataclass(frozen=True, slots=True)
class SolarPosition:
    elevation_deg: float
    azimuth_deg: float
    declination_deg: float
    equation_of_time_min: float
    hour_angle_deg: float


def _normalize_degrees(value: float) -> float:
    return value % 360.0


def signed_relative_azimuth(azimuth_deg: float, reference_deg: float) -> float:
    """Return a reference-relative azimuth in the range [-180, 180)."""
    return ((azimuth_deg - reference_deg + 180.0) % 360.0) - 180.0


def calculate_solar_position(
    local_time: datetime,
    latitude_deg: float,
    longitude_deg: float,
    utc_offset_hours: float,
) -> SolarPosition:
    """Calculate solar elevation and azimuth for a local civil timestamp."""
    if not -90.0 <= latitude_deg <= 90.0:
        raise ValueError("latitude must be between -90 and 90 degrees")
    if not -180.0 <= longitude_deg <= 180.0:
        raise ValueError("longitude must be between -180 and 180 degrees")
    if not -14.0 <= utc_offset_hours <= 14.0:
        raise ValueError("UTC offset must be between -14 and 14 hours")

    day = local_time.timetuple().tm_yday
    hour = (
        local_time.hour
        + local_time.minute / 60.0
        + local_time.second / 3600.0
        + local_time.microsecond / 3_600_000_000.0
    )

    gamma = 2.0 * math.pi / 365.0 * (day - 1.0 + (hour - 12.0) / 24.0)
    equation_of_time = 229.18 * (
        0.000075
        + 0.001868 * math.cos(gamma)
        - 0.032077 * math.sin(gamma)
        - 0.014615 * math.cos(2.0 * gamma)
        - 0.040849 * math.sin(2.0 * gamma)
    )
    declination = (
        0.006918
        - 0.399912 * math.cos(gamma)
        + 0.070257 * math.sin(gamma)
        - 0.006758 * math.cos(2.0 * gamma)
        + 0.000907 * math.sin(2.0 * gamma)
        - 0.002697 * math.cos(3.0 * gamma)
        + 0.00148 * math.sin(3.0 * gamma)
    )

    time_offset = (
        equation_of_time
        + 4.0 * longitude_deg
        - 60.0 * utc_offset_hours
    )
    true_solar_time = hour * 60.0 + time_offset
    hour_angle = _normalize_degrees(true_solar_time / 4.0 - 180.0)
    if hour_angle > 180.0:
        hour_angle -= 360.0

    latitude = math.radians(latitude_deg)
    delta = declination
    hour_angle_rad = math.radians(hour_angle)

    cosine_zenith = (
        math.sin(latitude) * math.sin(delta)
        + math.cos(latitude) * math.cos(delta) * math.cos(hour_angle_rad)
    )
    cosine_zenith = max(-1.0, min(1.0, cosine_zenith))
    zenith = math.acos(cosine_zenith)
    elevation = 90.0 - math.degrees(zenith)

    azimuth = math.degrees(
        math.atan2(
            math.sin(hour_angle_rad),
            math.cos(hour_angle_rad) * math.sin(latitude)
            - math.tan(delta) * math.cos(latitude),
        )
    ) + 180.0

    return SolarPosition(
        elevation_deg=elevation,
        azimuth_deg=_normalize_degrees(azimuth),
        declination_deg=math.degrees(declination),
        equation_of_time_min=equation_of_time,
        hour_angle_deg=hour_angle,
    )


def solar_window_target(
    position: SolarPosition,
    window_azimuth_deg: float,
    minimum_angle_deg: float = -90.0,
    maximum_angle_deg: float = 90.0,
) -> float:
    """Map solar azimuth to the window's signed mechanical angle."""
    if not minimum_angle_deg <= maximum_angle_deg:
        raise ValueError("minimum angle must not exceed maximum angle")
    if position.elevation_deg <= 0.0:
        return 0.0

    relative = signed_relative_azimuth(
        position.azimuth_deg,
        window_azimuth_deg,
    )
    return max(minimum_angle_deg, min(maximum_angle_deg, relative))
