#ifndef SMART_WINDOW_HOST_ARDUINO_H
#define SMART_WINDOW_HOST_ARDUINO_H

#include <algorithm>
#include <cstddef>
#include <cstdint>

constexpr std::uint8_t LOW = 0u;
constexpr std::uint8_t HIGH = 1u;
constexpr std::uint8_t INPUT = 0u;
constexpr std::uint8_t OUTPUT = 1u;
constexpr std::uint8_t INPUT_PULLUP = 2u;

void pinMode(std::uint8_t pin, std::uint8_t mode);
void digitalWrite(std::uint8_t pin, std::uint8_t value);
int digitalRead(std::uint8_t pin);
std::uint32_t micros();
std::uint32_t millis();
void delay(std::uint32_t milliseconds);

class HardwareSerial {
 public:
  void begin(std::uint32_t baud);
  int available();
  int read();
  std::size_t write(const std::uint8_t *data, std::size_t length);
};

extern HardwareSerial Serial;

template <typename T>
const T &max(const T &left, const T &right) {
  return std::max(left, right);
}

template <typename T>
const T &min(const T &left, const T &right) {
  return std::min(left, right);
}

#endif
