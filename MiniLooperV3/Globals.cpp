// Globals.cpp
#include "Globals.h"

volatile uint32_t currentTick = 0; // Define it once.

float bpm = 120.0;
uint32_t ticksPerQuarterNote = 24;
uint32_t quartersPerBar = 4;
const uint32_t ticksPerBar = ticksPerQuarterNote * quartersPerBar; // 4/4

static uint32_t lastBarTick = 0;

bool isBarBoundary() {
  uint32_t ticksSinceLastBar = currentTick - lastBarTick;

  if (ticksSinceLastBar >= ticksPerBar) {
    lastBarTick = currentTick;
    return true;
  }
  return false;
}

void setupGlobals() {
  // Load settings if needed
}
