//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

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
