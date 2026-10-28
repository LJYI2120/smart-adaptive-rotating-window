from __future__ import annotations

import pytest

from smart_window.controller import ControlSettings, TrackingController
from smart_window.protocol import OperatingMode, Telemetry
from smart_window.solar import SolarPosition


def telemetry(angle: float = 0.0, left: int = 100, right: int = 100) -> Telemetry:
    return Telemetry(
        sequence=1,
        angle_deg=angle,
        status=0,
        light_left=left,
        light_right=right,
        light_up=100,
        light_down=100,
        mode=OperatingMode.AUTO,
        sensor_mask=0x0F,
        uptime_s=1,
    )


def test_auto_tracking_combines_solar_and_light_gradient() -> None:
    controller = TrackingController(
        ControlSettings(
            window_azimuth_deg=180.0,
            light_kp=15.0,
            light_ki=0.0,
            light_kd=0.0,
            deadband_deg=0.0,
            max_slew_deg_per_s=100.0,
        )
    )
    sun = SolarPosition(
        elevation_deg=50.0,
        azimuth_deg=210.0,
        declination_deg=0.0,
        equation_of_time_min=0.0,
        hour_angle_deg=0.0,
    )
    decision = controller.update(
        OperatingMode.AUTO,
        telemetry(left=100, right=160),
        sun,
        manual_target_deg=0.0,
        elapsed_s=1.0,
    )
    assert decision.solar_angle_deg == 30.0
    assert decision.light_correction_deg == pytest.approx(1.8)
    assert decision.target_angle_deg == pytest.approx(31.8)


def test_slew_limit_is_applied() -> None:
    controller = TrackingController(
        ControlSettings(max_slew_deg_per_s=5.0, deadband_deg=0.0)
    )
    decision = controller.update(
        OperatingMode.MANUAL,
        telemetry(),
        None,
        manual_target_deg=60.0,
        elapsed_s=1.0,
    )
    assert decision.target_angle_deg == 5.0


def test_deadband_holds_target() -> None:
    controller = TrackingController(
        ControlSettings(deadband_deg=2.0, max_slew_deg_per_s=100.0)
    )
    controller.reset(20.0)
    decision = controller.update(
        OperatingMode.MANUAL,
        telemetry(),
        None,
        manual_target_deg=21.5,
        elapsed_s=1.0,
    )
    assert decision.target_angle_deg == 20.0


def test_target_is_clamped_to_mechanical_range() -> None:
    controller = TrackingController(
        ControlSettings(
            minimum_angle_deg=-45.0,
            maximum_angle_deg=45.0,
            max_slew_deg_per_s=1000.0,
            deadband_deg=0.0,
        )
    )
    decision = controller.update(
        OperatingMode.MANUAL,
        telemetry(),
        None,
        manual_target_deg=90.0,
        elapsed_s=1.0,
    )
    assert decision.target_angle_deg == 45.0
