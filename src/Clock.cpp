#include "Clock.h"

volatile uint32_t tickCount = 0;
bool externalClock = true;

void Clock::setup() {
  tickCount = 0;
  externalClock = true;
}

void Clock::update() {
  // (Optional: timeout if external clock disappears)
}

void Clock::reset() {
  tickCount = 0;
}

bool Clock::isExternalClockActive() {
  return externalClock;
}
