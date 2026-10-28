#include "motion_controller.h"

#include "board_config.h"
#include "sw_protocol.h"
#include <limits.h>

namespace {

bool inputTriggered(uint8_t pin) {
  return digitalRead(pin) == board::kInputTriggeredLevel;
}

int32_t clampTargetSteps(int32_t steps) {
  const int32_t maximum =
      static_cast<int32_t>(((board::kAngleMaximumCentiDeg -
                             board::kAngleMinimumCentiDeg) *
                            board::kStepsPerDegreeMilli) /
                           100000);
  return max<int32_t>(0, min<int32_t>(maximum, steps));
}

}  // namespace

void MotionController::begin(uint32_t now_ms) {
  pinMode(board::kPinStep, OUTPUT);
  pinMode(board::kPinDirection, OUTPUT);
  pinMode(board::kPinDriverEnable, OUTPUT);
  pinMode(board::kPinLimitMinimum, INPUT_PULLUP);
  pinMode(board::kPinLimitMaximum, INPUT_PULLUP);
  pinMode(board::kPinEmergencyStop, INPUT_PULLUP);
  pinMode(board::kPinStallDiagnostic, INPUT);

  digitalWrite(board::kPinStep, LOW);
  digitalWrite(board::kPinDirection, LOW);
  enableDriver(false);
  last_command_ms_ = now_ms;
}

int32_t MotionController::angleToSteps(int16_t angle_centideg) const {
  const int32_t relative_centideg =
      static_cast<int32_t>(angle_centideg) -
      board::kAngleMinimumCentiDeg;
  const int64_t numerator =
      static_cast<int64_t>(relative_centideg) *
      board::kStepsPerDegreeMilli;
  return clampTargetSteps(static_cast<int32_t>(numerator / 100000));
}

void MotionController::enableDriver(bool enabled) {
  enabled_ = enabled;
  digitalWrite(
      board::kPinDriverEnable,
      enabled ? board::kDriverEnableLevel
              : static_cast<uint8_t>(!board::kDriverEnableLevel));
}

void MotionController::noteCommand(uint32_t now_ms) {
  last_command_ms_ = now_ms;
}

void MotionController::markSensorFault(bool active) {
  sensor_fault_ = active;
}

void MotionController::markMotorFault() {
  setFault(static_cast<uint8_t>(smart_window::FaultCode::kMotor));
}

void MotionController::setFault(uint8_t code) {
  fault_code_ = code;
  stopPulseGenerator();
  enableDriver(false);
  start_pending_ = false;
}

bool MotionController::emergencyStopActive() const {
  return inputTriggered(board::kPinEmergencyStop);
}

MotionStartResult MotionController::start(uint32_t now_ms) {
  if (emergencyStopActive()) {
    emergency_latched_ = true;
    setFault(static_cast<uint8_t>(smart_window::FaultCode::kEmergencyStop));
    return MotionStartResult::kEmergencyStop;
  }
  if ((fault_code_ != 0u) || emergency_latched_) {
    return MotionStartResult::kFault;
  }

  start_pending_ = true;
  last_command_ms_ = now_ms;
  if (!homed_) {
    homing_ = true;
    homing_started_ms_ = now_ms;
    moving_ = true;
    enableDriver(true);
    digitalWrite(board::kPinDirection, LOW);
  } else {
    moving_ = position_steps_ != target_steps_;
    enableDriver(true);
  }
  return MotionStartResult::kStarted;
}

void MotionController::stopPulseGenerator() {
  moving_ = false;
  pulse_high_ = false;
  digitalWrite(board::kPinStep, LOW);
}

void MotionController::stop() {
  stopPulseGenerator();
  homing_ = false;
  start_pending_ = false;
  /*
   * STOP removes motion but leaves the stepper enabled to preserve holding
   * torque. Emergency stop and faults call enableDriver(false).
   */
}

bool MotionController::clearFault(uint32_t now_ms) {
  if (emergencyStopActive()) {
    return false;
  }
  if (emergency_latched_) {
    emergency_latched_ = false;
  }
  fault_code_ = 0u;
  sensor_fault_ = false;
  last_command_ms_ = now_ms;
  return true;
}

bool MotionController::setTargetCentideg(int16_t angle_centideg) {
  if ((angle_centideg < board::kAngleMinimumCentiDeg) ||
      (angle_centideg > board::kAngleMaximumCentiDeg)) {
    return false;
  }
  target_steps_ = angleToSteps(angle_centideg);
  if (homed_ && enabled_ && (fault_code_ == 0u)) {
    moving_ = position_steps_ != target_steps_;
  }
  return true;
}

void MotionController::startPulse(uint32_t now_ms) {
  if (homing_) {
    digitalWrite(board::kPinDirection, LOW);
  } else {
    digitalWrite(
        board::kPinDirection,
        target_steps_ < position_steps_ ? LOW : HIGH);
  }
  digitalWrite(board::kPinStep, HIGH);
  pulse_high_ = true;
  pulse_started_us_ = micros();
  last_step_us_ = pulse_started_us_;
  if (homing_) {
    --position_steps_;
  } else if (target_steps_ > position_steps_) {
    ++position_steps_;
  } else if (target_steps_ < position_steps_) {
    --position_steps_;
  }
  (void)now_ms;
}

void MotionController::finishPulse(uint32_t now_ms) {
  digitalWrite(board::kPinStep, LOW);
  pulse_high_ = false;
  if (!homing_ && (position_steps_ == target_steps_)) {
    moving_ = false;
  }
  (void)now_ms;
}

void MotionController::service(uint32_t now_ms) {
  const bool minimum_triggered = inputTriggered(board::kPinLimitMinimum);
  const bool maximum_triggered = inputTriggered(board::kPinLimitMaximum);

  if (emergencyStopActive()) {
    emergency_latched_ = true;
    setFault(static_cast<uint8_t>(smart_window::FaultCode::kEmergencyStop));
    return;
  }

  if (minimum_triggered && maximum_triggered) {
    setFault(static_cast<uint8_t>(smart_window::FaultCode::kSensor));
    return;
  }

  if (homing_) {
    if (minimum_triggered) {
      position_steps_ = 0;
      homed_ = true;
      homing_ = false;
      moving_ = position_steps_ != target_steps_;
      digitalWrite(board::kPinStep, LOW);
      /*
       * A START command retains its most recent target request. The PC sends
       * the target repeatedly, so the following target frame starts motion.
       */
    } else if ((now_ms - homing_started_ms_) >= board::kHomingTimeoutMs) {
      setFault(static_cast<uint8_t>(smart_window::FaultCode::kHomingTimeout));
      return;
    } else {
      moving_ = true;
    }
  }

  if (enabled_ && start_pending_ &&
      ((now_ms - last_command_ms_) >= board::kCommunicationTimeoutMs)) {
    setFault(static_cast<uint8_t>(smart_window::FaultCode::kCommTimeout));
    return;
  }

  if (board::kEnableStallDiagnostic &&
      enabled_ && moving_ &&
      digitalRead(board::kPinStallDiagnostic) ==
          board::kStallDiagnosticActiveLevel) {
    setFault(static_cast<uint8_t>(smart_window::FaultCode::kMotor));
    return;
  }

  if (!enabled_ || (!moving_ && !homing_)) {
    return;
  }

  if (pulse_high_) {
    if ((micros() - pulse_started_us_) >= board::kStepPulseWidthUs) {
      finishPulse(now_ms);
    }
    return;
  }

  if ((micros() - last_step_us_) < board::kStepIntervalUs) {
    return;
  }

  if (!homing_ && minimum_triggered &&
      (target_steps_ <= position_steps_)) {
    stopPulseGenerator();
    return;
  }
  if (!homing_ && maximum_triggered &&
      (target_steps_ >= position_steps_)) {
    stopPulseGenerator();
    return;
  }

  if (!homing_ && (position_steps_ == target_steps_)) {
    stopPulseGenerator();
    return;
  }

  startPulse(now_ms);
}

int16_t MotionController::currentAngleCentideg() const {
  const int32_t relative =
      static_cast<int32_t>(position_steps_) * 100000 /
      board::kStepsPerDegreeMilli;
  const int32_t angle = board::kAngleMinimumCentiDeg + relative;
  return static_cast<int16_t>(max<int32_t>(
      board::kAngleMinimumCentiDeg,
      min<int32_t>(board::kAngleMaximumCentiDeg, angle)));
}

uint16_t MotionController::statusBits() const {
  uint16_t status = 0u;
  if (enabled_) {
    status |= smart_window::kStatusEnabled;
  }
  if (homing_) {
    status |= smart_window::kStatusHoming;
  }
  if (moving_) {
    status |= smart_window::kStatusMoving;
  }
  if (inputTriggered(board::kPinLimitMinimum)) {
    status |= smart_window::kStatusLimitMin;
  }
  if (inputTriggered(board::kPinLimitMaximum)) {
    status |= smart_window::kStatusLimitMax;
  }
  if (emergency_latched_ || emergencyStopActive()) {
    status |= smart_window::kStatusEmergencyStop;
  }
  if (fault_code_ ==
      static_cast<uint8_t>(smart_window::FaultCode::kCommTimeout)) {
    status |= smart_window::kStatusCommTimeout;
  }
  if (sensor_fault_) {
    status |= smart_window::kStatusSensorFault;
  }
  if (fault_code_ ==
      static_cast<uint8_t>(smart_window::FaultCode::kMotor)) {
    status |= smart_window::kStatusMotorFault;
  }
  if (homed_) {
    status |= smart_window::kStatusHomed;
  }
  return status;
}

uint8_t MotionController::faultCode() const {
  return fault_code_;
}

bool MotionController::isHomed() const {
  return homed_;
}

bool MotionController::isMoving() const {
  return moving_;
}
