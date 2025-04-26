#include "Globals.h"
#include "Clock.h"

// Private state
namespace {
  volatile uint32_t tickCount = 0;
  bool externalClock = false;
}

// Public

void Clock::setup() {
  tickCount = 0;
  externalClock = false;
}

void Clock::update() {
  // (Optional: timeout if external clock disappears)
}

void Clock::onMidiClockPulse() {
  externalClock = true;
  tickCount++;
}

uint32_t Clock::getCurrentTick() {
  return tickCount;
}

void Clock::reset() {
  tickCount = 0;
}

bool Clock::isExternalClockActive() {
  return externalClock;
}
