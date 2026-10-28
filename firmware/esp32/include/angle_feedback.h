#ifndef SMART_WINDOW_ANGLE_FEEDBACK_H
#define SMART_WINDOW_ANGLE_FEEDBACK_H

#include <Arduino.h>

class AngleFeedback {
 public:
  bool begin();
  bool readRaw(uint16_t &raw_angle);

 private:
  bool available_ = false;
};

#endif
