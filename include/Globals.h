//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

/**
 * @file Globals.h
 * @brief Global configuration, hardware pin definitions, timing constants, and runtime state for the MIDI looper.
 *
 * Defines:
 *  - LCD, button, and encoder hardware pin assignments in LCD and Buttons namespaces.
 *  - MIDI channels and CCs in MidiConfig.h.
 *  - Track count, internal PPQN, time signature, and loop timing constants in Config namespace.
 *  - Runtime settings: bpm, ticksPerQuarterNote, quartersPerBar, ticksPerBar, and display timing.
 *  - System helper functions: setupGlobals(), isBarBoundary(), loadConfig(), saveConfig().
 */
#ifndef GLOBALS_H
#define GLOBALS_H

#pragma once
#include <Arduino.h>

// --------------------
// Hardware Configuration
// --------------------
// LCD Display Configuration deactived, so no pins set
namespace LCD {
  const int RS     = 255;    // Register Select
  const int ENABLE = 255;    // Enable
  const int D4     = 255;    // Data 4
  const int D5     = 255;    // Data 5
  const int D6     = 255;    // Data 6
  const int D7     = 255;    // Data 7
  const uint32_t DISPLAY_UPDATE_INTERVAL = 30 ; // in ms (approx. 333Hz)
}

// Button Configuration
namespace Buttons {
  const int RECORD = 37;      // Record/Overdub button
  const int PLAY   = 36;      // Play/Stop button
  const int ENCODER_PIN_A = 29;
  const int ENCODER_PIN_B = 30;
  const int ENCODER_BUTTON_PIN = 31;
}

// MIDI Configuration (channels, CCs, record exclusion, LED feedback)
#include "MidiConfig.h"

// --------------------
// Track and Timing Configuration
// --------------------
namespace Config {
  constexpr uint8_t  NUM_TRACKS = 4;                                   // Number of looper tracks
  constexpr uint8_t  INTERNAL_PPQN = 192;                              // Internal resolution for timing
  constexpr uint8_t  QUARTERS_PER_BAR = 4;                             // Time signature numerator (4/4 time) 
  constexpr uint8_t  TICKS_PER_QUARTER_NOTE = INTERNAL_PPQN;           // For Musical Time naming consistency
  constexpr uint8_t  TICKS_PER_CLOCK = (INTERNAL_PPQN / 24);           // 8 ticks per MIDI clock pulse (24 PPQN)
  constexpr uint32_t TICKS_PER_BAR = INTERNAL_PPQN * QUARTERS_PER_BAR; // 768 or your default value (ticksPerQuarterNote * quartersPerBar)
  constexpr uint32_t TICKS_PER_16TH_STEP = INTERNAL_PPQN / 4;          // 192 / 4 = 48 Ticks
  constexpr uint8_t  MAX_UNDO_HISTORY = 99;
}
 
// --------------------
// Runtime Settings
// --------------------
extern float bpm;                          // Current tempo
extern uint32_t ticksPerQuarterNote;       // MIDI resolution
extern uint32_t quartersPerBar;            // Time signature numerator
extern const uint32_t ticksPerBar;         // Computed as ticksPerQuarterNote * quartersPerBar
extern uint32_t now;                       // Current time
extern uint32_t lastDisplayUpdate;    

// --------------------
// System Functions
// --------------------
bool isBarBoundary();                      // Check if current tick is on bar boundary

// Still to be implemented on EEPROM
void setupGlobals();                       // Initialize system configuration
void loadConfig();                         // Load configuration from persistent storage
void saveConfig();                         // Save configuration to persistent storage

#endif // GLOBALS_H
