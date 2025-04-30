#include "Globals.h"
#include "ClockManager.h"

// --------------------
// Debug flags
// --------------------
bool DEBUG = true;
bool DEBUG_MIDI = false;
bool DEBUG_NOTES = true;
bool DEBUG_BUTTONS = false;
bool DEBUG_DISPLAY = false;

// --------------------
// Timing setup
// --------------------
float bpm = 120.0;
uint32_t ticksPerQuarterNote = 24;        // External MIDI clock
uint32_t quartersPerBar = 4;              // Standard 4/4
const uint32_t ticksPerBar = ticksPerQuarterNote * quartersPerBar;

// --------------------
// Internal tracking
// --------------------
static uint32_t lastBarTick = 0;

// --------------------
// Check if we're at the start of a new bar
// --------------------
bool isBarBoundary() {
  uint32_t currentTick = clockManager.getCurrentTick();
  uint32_t ticksSinceLastBar = currentTick - lastBarTick;

  if (ticksSinceLastBar >= ticksPerBar) {
    lastBarTick = currentTick;
    return true;
  }
  return false;
}

// --------------------
// Setup function (reserved for future settings, EEPROM, etc.)
// --------------------
void setupGlobals() {
  // Load saved settings or apply defaults (if needed)
}
