#include "Arduino.h"
#include "Wire.h"

#include <array>

HardwareSerial Serial;
HostWire Wire;

namespace {
std::array<int, 64> gpio{};
std::array<std::uint8_t, 4> sensor_bytes{100u, 0u, 20u, 0u};
std::size_t sensor_index = 0u;
std::uint32_t virtual_micros = 0u;
}  // namespace

void pinMode(std::uint8_t, std::uint8_t) {}

void digitalWrite(std::uint8_t pin, std::uint8_t value) {
  if (pin < gpio.size()) {
    gpio[pin] = value;
  }
}

int digitalRead(std::uint8_t pin) {
  return pin < gpio.size() ? gpio[pin] : LOW;
}

std::uint32_t micros() {
  return virtual_micros;
}

std::uint32_t millis() {
  return virtual_micros / 1000u;
}

void delay(std::uint32_t milliseconds) {
  virtual_micros += milliseconds * 1000u;
}

void HardwareSerial::begin(std::uint32_t) {}

int HardwareSerial::available() {
  return 0;
}

int HardwareSerial::read() {
  return -1;
}

std::size_t HardwareSerial::write(const std::uint8_t *, std::size_t length) {
  return length;
}

void HostWire::begin(std::uint8_t, std::uint8_t) {}

void HostWire::beginTransmission(std::uint8_t) {
  write_index_ = 0u;
}

std::size_t HostWire::write(std::uint8_t) {
  ++write_index_;
  return 1u;
}

std::uint8_t HostWire::endTransmission(bool) {
  return 0u;
}

std::uint8_t HostWire::requestFrom(std::uint8_t, std::uint8_t length) {
  sensor_index = 0u;
  return length <= sensor_bytes.size()
             ? length
             : static_cast<std::uint8_t>(sensor_bytes.size());
}

int HostWire::read() {
  if (sensor_index >= sensor_bytes.size()) {
    return 0;
  }
  return sensor_bytes[sensor_index++];
}
