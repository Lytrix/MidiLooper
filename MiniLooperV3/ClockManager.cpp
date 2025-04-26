// ClockManager.cpp
#include "Globals.h"
#include "Clock.h"
#include "ClockManager.h"
#include <IntervalTimer.h>

IntervalTimer clockTimer;

ClockSource clockSource = CLOCK_INTERNAL;

volatile uint32_t lastMidiClockTime = 0;
volatile uint32_t lastInternalTickTime = 0;
uint32_t microsPerTick = 0;

void setupClock() {
  microsPerTick = 60000000 / (bpm * ticksPerQuarterNote);
  clockTimer.begin(updateInternalClock, microsPerTick);
}

void updateInternalClock() {
  if (clockSource == CLOCK_INTERNAL) {
    currentTick++;
    lastInternalTickTime = micros();
  }
}

void onMidiClockPulse() {
  currentTick++;
  lastMidiClockTime = micros();
  clockSource = CLOCK_EXTERNAL;
}

void checkClockSource() {
  uint32_t now = micros();
  if (clockSource == CLOCK_EXTERNAL) {
    if (now - lastMidiClockTime > midiClockTimeout) {
      clockSource = CLOCK_INTERNAL;
    }
  }
}