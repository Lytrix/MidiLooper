#pragma once

#include <Arduino.h>

namespace Clock {
  void setup();
  void update();
  void onMidiClockPulse();
  uint32_t getCurrentTick();
  void reset();
  
  bool isExternalClockActive();
}
