#ifndef CLOCK_H
#define CLOCK_H

#include <Arduino.h>

namespace Clock {
  void setup();
  void update();
  void reset();
  
  bool isExternalClockActive();
}

#endif // CLOCK_H
