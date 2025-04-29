#pragma once

#include <Arduino.h>

namespace Clock {
  void setup();
  void update();
  void reset();
  
  bool isExternalClockActive();
}
