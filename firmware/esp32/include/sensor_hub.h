#ifndef SMART_WINDOW_SENSOR_HUB_H
#define SMART_WINDOW_SENSOR_HUB_H

#include <Arduino.h>

struct LightReading {
  uint16_t left = 0;
  uint16_t right = 0;
  uint16_t up = 0;
  uint16_t down = 0;
  uint8_t valid_mask = 0;
};

class SensorHub {
 public:
  bool begin();
  bool startMeasurement(uint32_t now_ms);
  void service(uint32_t now_ms, LightReading &reading);
  const LightReading &reading() const { return reading_; }
  bool available(size_t index) const;

 private:
  bool selectChannel(uint8_t channel);
  bool writeRegister(uint8_t device_address,
                     uint8_t register_address,
                     uint8_t value);
  bool readSensorData(size_t index, uint16_t &visible, uint16_t &infrared);

  bool available_[4] = {false, false, false, false};
  bool measurement_active_ = false;
  uint32_t measurement_started_ms_ = 0;
  LightReading reading_;
};

#endif
