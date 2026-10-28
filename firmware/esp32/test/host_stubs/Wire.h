#ifndef SMART_WINDOW_HOST_WIRE_H
#define SMART_WINDOW_HOST_WIRE_H

#include <cstddef>
#include <cstdint>

class HostWire {
 public:
  void begin(std::uint8_t sda, std::uint8_t scl);
  void beginTransmission(std::uint8_t address);
  std::size_t write(std::uint8_t value);
  std::uint8_t endTransmission(bool stop = true);
  std::uint8_t requestFrom(std::uint8_t address, std::uint8_t length);
  int read();

 private:
  std::size_t write_index_ = 0u;
};

extern HostWire Wire;

#endif
