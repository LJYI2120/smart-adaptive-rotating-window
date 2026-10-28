#ifndef SMART_WINDOW_BOARD_CONFIG_H
#define SMART_WINDOW_BOARD_CONFIG_H

#include <Arduino.h>

namespace board {

constexpr uint8_t kPinStep = 26;
constexpr uint8_t kPinDirection = 27;
constexpr uint8_t kPinDriverEnable = 25;
constexpr uint8_t kPinLimitMinimum = 32;
constexpr uint8_t kPinLimitMaximum = 33;
constexpr uint8_t kPinEmergencyStop = 14;
constexpr uint8_t kPinStallDiagnostic = 34;
constexpr uint8_t kPinI2cSda = 21;
constexpr uint8_t kPinI2cScl = 22;

constexpr uint8_t kTca9548Address = 0x70;
constexpr uint8_t kAs5600Address = 0x36;
constexpr bool kEnableAs5600 = false;
constexpr uint8_t kLightSensorChannels[4] = {0u, 1u, 2u, 3u};
constexpr uint8_t kTsl2561Address = 0x39;

constexpr bool kLimitsAreNormallyClosed = true;
constexpr uint8_t kInputTriggeredLevel = HIGH;
constexpr uint8_t kDriverEnableLevel = LOW;

constexpr int16_t kAngleMinimumCentiDeg = -9000;
constexpr int16_t kAngleMaximumCentiDeg = 9000;
constexpr int32_t kStepsPerDegreeMilli = 8889;
constexpr uint16_t kStepIntervalUs = 900u;
constexpr uint8_t kStepPulseWidthUs = 5u;

constexpr uint32_t kCommunicationTimeoutMs = 1500u;
constexpr uint32_t kHomingTimeoutMs = 15000u;
constexpr uint32_t kTelemetryPeriodMs = 200u;
constexpr uint32_t kAngleFeedbackPeriodMs = 100u;
constexpr uint32_t kNoMotionFaultMs = 1500u;
constexpr uint16_t kNoMotionRawDelta = 3u;

constexpr bool kEnableStallDiagnostic = false;
constexpr uint8_t kStallDiagnosticActiveLevel = HIGH;

constexpr char kFrameMagic[] = "SWC1";
constexpr uint8_t kFirmwareMajor = 0u;
constexpr uint8_t kFirmwareMinor = 1u;
constexpr uint8_t kFirmwarePatch = 0u;

}  // namespace board

#endif
