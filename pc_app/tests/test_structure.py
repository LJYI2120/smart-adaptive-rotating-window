from __future__ import annotations

import pytest

from smart_window.structure import StructureInputs, evaluate_structure


def test_geared_drive_has_healthy_margin() -> None:
    result = evaluate_structure(
        StructureInputs(
            window_mass_kg=2.5,
            center_offset_mm=10.0,
            gear_ratio=5.0,
            motor_torque_nm=0.4,
        )
    )
    assert result.required_torque_nm == pytest.approx(0.3065, abs=0.001)
    assert result.available_torque_nm == pytest.approx(1.7, abs=0.01)
    assert result.safety_margin > 5.0
    assert "质心" in result.recommendation


def test_direct_drive_is_flagged_near_three_kilograms() -> None:
    result = evaluate_structure(
        StructureInputs(
            window_mass_kg=3.0,
            center_offset_mm=10.0,
            gear_ratio=1.0,
        )
    )
    assert result.safety_margin < 1.5
    assert "不足" in result.recommendation


def test_large_center_offset_increases_required_torque() -> None:
    small = evaluate_structure(StructureInputs(center_offset_mm=5.0))
    large = evaluate_structure(StructureInputs(center_offset_mm=20.0))
    assert large.required_torque_nm == pytest.approx(
        small.required_torque_nm * 4.0
    )
