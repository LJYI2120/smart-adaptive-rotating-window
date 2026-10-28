"""High-level mode selection and tracking target calculation."""

from __future__ import annotations

from dataclasses import dataclass

from .protocol import OperatingMode, Telemetry
from .solar import SolarPosition, solar_window_target


@dataclass(slots=True)
class ControlSettings:
    minimum_angle_deg: float = -90.0
    maximum_angle_deg: float = 90.0
    window_azimuth_deg: float = 180.0
    cooling_target_deg: float = 90.0
    light_kp: float = 15.0
    light_ki: float = 0.5
    light_kd: float = 0.2
    light_integral_limit_deg: float = 20.0
    light_correction_limit_deg: float = 25.0
    deadband_deg: float = 1.0
    max_slew_deg_per_s: float = 12.0

    def validate(self) -> None:
        if self.minimum_angle_deg >= self.maximum_angle_deg:
            raise ValueError("minimum angle must be below maximum angle")
        if min(self.light_kp, self.light_ki, self.light_kd) < 0.0:
            raise ValueError("light PID gains cannot be negative")
        if self.light_integral_limit_deg < 0.0:
            raise ValueError("light integral limit cannot be negative")
        if self.light_correction_limit_deg < 0.0:
            raise ValueError("light correction limit cannot be negative")
        if self.deadband_deg < 0.0:
            raise ValueError("deadband cannot be negative")
        if self.max_slew_deg_per_s <= 0.0:
            raise ValueError("slew limit must be positive")


@dataclass(frozen=True, slots=True)
class ControlDecision:
    target_angle_deg: float
    unconstrained_angle_deg: float
    solar_angle_deg: float
    light_correction_deg: float
    mode: OperatingMode


class TrackingController:
    """Convert solar position and measured light gradients to a safe target."""

    def __init__(self, settings: ControlSettings | None = None) -> None:
        self.settings = settings or ControlSettings()
        self.settings.validate()
        self._last_target = 0.0
        self._light_integral = 0.0
        self._last_light_error = 0.0

    def reset(self, current_angle_deg: float = 0.0) -> None:
        self._last_target = current_angle_deg
        self._light_integral = 0.0
        self._last_light_error = 0.0

    def _clamp(self, value: float) -> float:
        return max(
            self.settings.minimum_angle_deg,
            min(self.settings.maximum_angle_deg, value),
        )

    def _light_pid(self, right_minus_left: int, elapsed_s: float) -> float:
        """Normalized left/right light imbalance to a bounded angle offset."""
        error = right_minus_left / 500.0
        dt = max(0.05, elapsed_s)
        self._light_integral += error * dt
        self._light_integral = max(
            -self.settings.light_integral_limit_deg,
            min(self.settings.light_integral_limit_deg, self._light_integral),
        )
        derivative = (error - self._last_light_error) / dt
        self._last_light_error = error

        correction = (
            self.settings.light_kp * error
            + self.settings.light_ki * self._light_integral
            + self.settings.light_kd * derivative
        )
        return max(
            -self.settings.light_correction_limit_deg,
            min(self.settings.light_correction_limit_deg, correction),
        )

    def update(
        self,
        mode: OperatingMode,
        telemetry: Telemetry,
        solar_position: SolarPosition | None,
        manual_target_deg: float,
        elapsed_s: float,
    ) -> ControlDecision:
        if elapsed_s < 0.0:
            raise ValueError("elapsed time cannot be negative")
        self.settings.validate()

        solar_angle = 0.0
        correction = self._light_pid(
            telemetry.light_right - telemetry.light_left,
            elapsed_s,
        )

        if mode == OperatingMode.AUTO:
            if solar_position is not None:
                solar_angle = solar_window_target(
                    solar_position,
                    self.settings.window_azimuth_deg,
                    self.settings.minimum_angle_deg,
                    self.settings.maximum_angle_deg,
                )
            unconstrained = solar_angle + correction
        elif mode == OperatingMode.HARVEST:
            unconstrained = telemetry.angle_deg + correction
        elif mode == OperatingMode.COOLING:
            unconstrained = self.settings.cooling_target_deg
            correction = 0.0
            solar_angle = unconstrained
        elif mode == OperatingMode.MANUAL:
            unconstrained = manual_target_deg
            correction = 0.0
            solar_angle = unconstrained
        else:
            unconstrained = telemetry.angle_deg
            correction = 0.0
            self._light_integral = 0.0
            self._last_light_error = 0.0

        target = self._clamp(unconstrained)
        if abs(target - self._last_target) <= self.settings.deadband_deg:
            target = self._last_target
        else:
            max_change = self.settings.max_slew_deg_per_s * elapsed_s
            delta = max(-max_change, min(max_change, target - self._last_target))
            target = self._last_target + delta

        target = self._clamp(target)
        self._last_target = target
        return ControlDecision(
            target_angle_deg=target,
            unconstrained_angle_deg=unconstrained,
            solar_angle_deg=solar_angle,
            light_correction_deg=correction,
            mode=mode,
        )
