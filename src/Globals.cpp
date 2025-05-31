//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "Globals.h"
#include "ClockManager.h"

//uint8_t debugLevel = DEBUG_INFO;

// Runtime settings
float bpm = 120.0f;
uint32_t lastDisplayUpdate = 0;
uint32_t now = millis();

// --------------------
// Internal tracking
// --------------------
//static uint32_t lastBarTick = 0;


// --------------------
// Check if we're at the start of a new bar
// --------------------
bool isBarBoundary() {
  return (clockManager.getCurrentTick() % ticksPerBar) == 0;
}

// --------------------
// Setup function (reserved for future settings, EEPROM, etc.)
// --------------------
void setupGlobals() {
  // Initialize any runtime settings here
}

void loadConfig() {
  // TODO: Load configuration from EEPROM
}

void saveConfig() {
  // TODO: Save configuration to EEPROM
}
