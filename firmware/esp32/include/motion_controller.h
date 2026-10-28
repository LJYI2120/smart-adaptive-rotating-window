#ifndef SMART_WINDOW_MOTION_CONTROLLER_H
#define SMART_WINDOW_MOTION_CONTROLLER_H

#include <Arduino.h>

enum class MotionStartResult : uint8_t {
  kStarted = 0,
  kEmergencyStop = 1,
  kFault = 2,
};

class MotionController {
 public:
  void begin(uint32_t now_ms);
  void service(uint32_t now_ms);
  MotionStartResult start(uint32_t now_ms);
  void stop();
  bool clearFault(uint32_t now_ms);
  bool setTargetCentideg(int16_t angle_centideg);
  void noteCommand(uint32_t now_ms);
  void markSensorFault(bool active);
  void markMotorFault();

  int16_t currentAngleCentideg() const;
  uint16_t statusBits() const;
  uint8_t faultCode() const;
  bool emergencyStopActive() const;
  bool isHomed() const;
  bool isMoving() const;

 private:
  int32_t angleToSteps(int16_t angle_centideg) const;
  void enableDriver(bool enabled);
  void startPulse(uint32_t now_ms);
  void finishPulse(uint32_t now_ms);
  void setFault(uint8_t code);
  void stopPulseGenerator();

  int32_t position_steps_ = 0;
  int32_t target_steps_ = 0;
  uint32_t last_step_us_ = 0u;
  uint32_t pulse_started_us_ = 0u;
  uint32_t last_command_ms_ = 0u;
  uint32_t homing_started_ms_ = 0u;
  bool enabled_ = false;
  bool homed_ = false;
  bool homing_ = false;
  bool moving_ = false;
  bool pulse_high_ = false;
  bool start_pending_ = false;
  bool sensor_fault_ = false;
  bool emergency_latched_ = false;
  uint8_t fault_code_ = 0u;
};

#endif
