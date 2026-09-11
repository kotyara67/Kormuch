#pragma once

#include <Arduino.h>

class MotorDriver {
 public:
  void begin();
  void on();
  void off();
  bool isOn() const { return on_; }

 private:
  bool on_ = false;
};

