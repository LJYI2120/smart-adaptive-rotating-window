#include "angle_feedback.h"

#include "board_config.h"
#include <Wire.h>

bool AngleFeedback::begin() {
  uint16_t raw = 0u;
  available_ = readRaw(raw);
  return available_;
}

bool AngleFeedback::readRaw(uint16_t &raw_angle) {
  raw_angle = 0u;
  if (!board::kEnableAs5600) {
    return false;
  }

  Wire.beginTransmission(board::kAs5600Address);
  Wire.write(static_cast<uint8_t>(0x0Cu));
  if (Wire.endTransmission(false) != 0u) {
    available_ = false;
    return false;
  }
  if (Wire.requestFrom(board::kAs5600Address, static_cast<uint8_t>(2u)) != 2u) {
    available_ = false;
    return false;
  }

  const uint8_t high = static_cast<uint8_t>(Wire.read());
  const uint8_t low = static_cast<uint8_t>(Wire.read());
  raw_angle = static_cast<uint16_t>(
      (static_cast<uint16_t>(high & 0x0Fu) << 8u) |
      static_cast<uint16_t>(low));
  available_ = true;
  return true;
}
