"""Mechanical load estimates for the compact rotating-window prototype."""

from __future__ import annotations

from dataclasses import dataclass

STANDARD_GRAVITY = 9.80665


@dataclass(frozen=True, slots=True)
class StructureInputs:
    window_mass_kg: float = 2.5
    center_offset_mm: float = 10.0
    gear_ratio: float = 5.0
    motor_torque_nm: float = 0.4
    friction_safety_factor: float = 1.25
    transmission_efficiency: float = 0.85

    def validate(self) -> None:
        if self.window_mass_kg <= 0.0:
            raise ValueError("window mass must be positive")
        if self.center_offset_mm < 0.0:
            raise ValueError("center-of-gravity offset cannot be negative")
        if self.gear_ratio <= 0.0:
            raise ValueError("gear ratio must be positive")
        if self.motor_torque_nm <= 0.0:
            raise ValueError("motor torque must be positive")
        if self.friction_safety_factor <= 0.0:
            raise ValueError("friction factor must be positive")
        if not 0.0 < self.transmission_efficiency <= 1.0:
            raise ValueError("transmission efficiency must be in (0, 1]")


@dataclass(frozen=True, slots=True)
class StructureResult:
    gravity_torque_nm: float
    required_torque_nm: float
    available_torque_nm: float
    safety_margin: float
    direct_drive_margin: float
    recommendation: str


def evaluate_structure(inputs: StructureInputs) -> StructureResult:
    """Estimate holding torque with the shaft near the centre of gravity."""
    inputs.validate()
    lever_m = inputs.center_offset_mm / 1000.0
    gravity_torque = (
        inputs.window_mass_kg * STANDARD_GRAVITY * lever_m
    )
    required = gravity_torque * inputs.friction_safety_factor
    available = (
        inputs.motor_torque_nm
        * inputs.gear_ratio
        * inputs.transmission_efficiency
    )
    direct_available = inputs.motor_torque_nm * inputs.transmission_efficiency
    margin = available / required if required > 0.0 else float("inf")
    direct_margin = direct_available / required if required > 0.0 else float("inf")

    if margin < 1.5:
        recommendation = "传动余量不足：增加减速比或重新配重"
    elif margin < 2.0:
        recommendation = "余量偏低：建议使用 5:1 以上自锁减速机构"
    elif direct_margin < 2.0:
        recommendation = "建议减速驱动；同时将质心控制在转轴附近"
    else:
        recommendation = "力矩余量满足原型要求"

    return StructureResult(
        gravity_torque_nm=gravity_torque,
        required_torque_nm=required,
        available_torque_nm=available,
        safety_margin=margin,
        direct_drive_margin=direct_margin,
        recommendation=recommendation,
    )
