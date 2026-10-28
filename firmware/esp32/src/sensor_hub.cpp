#include "sensor_hub.h"

#include "board_config.h"
#include <Wire.h>

namespace {

constexpr uint8_t kCommandBit = 0x80u;
constexpr uint8_t kRegisterControl = 0x00u;
constexpr uint8_t kRegisterTiming = 0x01u;
constexpr uint8_t kRegisterData0Low = 0x0Cu;
constexpr uint8_t kControlPowerOn = 0x03u;
constexpr uint8_t kTimingGain16x101ms = 0x11u;
constexpr uint32_t kIntegrationReadyMs = 130u;

}  // namespace

bool SensorHub::selectChannel(uint8_t channel) {
  if (channel >= 8u) {
    return false;
  }
  Wire.beginTransmission(board::kTca9548Address);
  Wire.write(static_cast<uint8_t>(1u << channel));
  return Wire.endTransmission() == 0;
}

bool SensorHub::writeRegister(uint8_t device_address,
                              uint8_t register_address,
                              uint8_t value) {
  Wire.beginTransmission(device_address);
  Wire.write(static_cast<uint8_t>(kCommandBit | register_address));
  Wire.write(value);
  return Wire.endTransmission() == 0;
}

bool SensorHub::readSensorData(size_t index,
                               uint16_t &visible,
                               uint16_t &infrared) {
  visible = 0u;
  infrared = 0u;
  if ((index >= 4u) || !available_[index] ||
      !selectChannel(board::kLightSensorChannels[index])) {
    return false;
  }

  Wire.beginTransmission(board::kTsl2561Address);
  Wire.write(static_cast<uint8_t>(kCommandBit | kRegisterData0Low));
  if (Wire.endTransmission(false) != 0) {
    return false;
  }
  if (Wire.requestFrom(board::kTsl2561Address, static_cast<uint8_t>(4u)) != 4u) {
    return false;
  }

  const uint8_t data0_low = static_cast<uint8_t>(Wire.read());
  const uint8_t data0_high = static_cast<uint8_t>(Wire.read());
  const uint8_t data1_low = static_cast<uint8_t>(Wire.read());
  const uint8_t data1_high = static_cast<uint8_t>(Wire.read());
  visible = static_cast<uint16_t>(
      static_cast<uint16_t>(data0_low) |
      (static_cast<uint16_t>(data0_high) << 8u));
  infrared = static_cast<uint16_t>(
      static_cast<uint16_t>(data1_low) |
      (static_cast<uint16_t>(data1_high) << 8u));
  return true;
}

bool SensorHub::begin() {
  Wire.begin(board::kPinI2cSda, board::kPinI2cScl);
  bool any = false;
  for (size_t index = 0u; index < 4u; ++index) {
    available_[index] =
        selectChannel(board::kLightSensorChannels[index]) &&
        writeRegister(board::kTsl2561Address,
                      kRegisterControl,
                      kControlPowerOn) &&
        writeRegister(board::kTsl2561Address,
                      kRegisterTiming,
                      kTimingGain16x101ms);
    any = available_[index] || any;
  }
  return any;
}

bool SensorHub::startMeasurement(uint32_t now_ms) {
  if (measurement_active_) {
    return false;
  }
  for (size_t index = 0u; index < 4u; ++index) {
    if (available_[index]) {
      available_[index] =
          selectChannel(board::kLightSensorChannels[index]) &&
          writeRegister(board::kTsl2561Address,
                        kRegisterTiming,
                        kTimingGain16x101ms);
    }
  }
  measurement_started_ms_ = now_ms;
  measurement_active_ = true;
  return true;
}

void SensorHub::service(uint32_t now_ms, LightReading &reading) {
  if (!measurement_active_ ||
      ((now_ms - measurement_started_ms_) < kIntegrationReadyMs)) {
    reading = reading_;
    return;
  }

  uint16_t visible[4] = {0u, 0u, 0u, 0u};
  uint16_t infrared[4] = {0u, 0u, 0u, 0u};
  uint16_t relative_light[4] = {0u, 0u, 0u, 0u};
  for (size_t index = 0u; index < 4u; ++index) {
    if (available_[index] &&
        readSensorData(index, visible[index], infrared[index])) {
      /*
       * The visible channel includes IR; subtracting the IR channel gives a
       * stable relative daylight signal without claiming calibrated lux.
       */
      reading_.valid_mask = static_cast<uint8_t>(
          reading_.valid_mask | static_cast<uint8_t>(1u << index));
      relative_light[index] =
          visible[index] > infrared[index]
              ? static_cast<uint16_t>(visible[index] - infrared[index])
              : 0u;
    } else {
      available_[index] = false;
      reading_.valid_mask = static_cast<uint8_t>(
          reading_.valid_mask & static_cast<uint8_t>(~(1u << index)));
    }
  }
  reading_.left = relative_light[0];
  reading_.right = relative_light[1];
  reading_.up = relative_light[2];
  reading_.down = relative_light[3];
  measurement_active_ = false;
  reading = reading_;
}

bool SensorHub::available(size_t index) const {
  return index < 4u && available_[index];
}
