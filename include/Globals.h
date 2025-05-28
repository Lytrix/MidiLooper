/**
 * @file Globals.h
 * @brief Global configuration, hardware pin definitions, timing constants, and runtime state for the MIDI looper.
 *
 * Defines:
 *  - Debug levels (DEBUG_* flags) and global debugLevel variable.
 *  - LCD, button, and encoder hardware pin assignments in LCD and Buttons namespaces.
 *  - Default MIDI channel and PPQN in MidiConfig namespace.
 *  - Track count, internal PPQN, time signature, and loop timing constants in Config namespace.
 *  - Runtime settings: bpm, ticksPerQuarterNote, quartersPerBar, ticksPerBar, and display timing.
 *  - System helper functions: setupGlobals(), isBarBoundary(), loadConfig(), saveConfig().
 */
#ifndef GLOBALS_H
#define GLOBALS_H

#pragma once
#include <Arduino.h>

// --------------------
// Debug Configuration
// --------------------
// Debug levels (can be combined using bitwise OR)
#define DEBUG_NONE     0x00
#define DEBUG_ERROR    0x01
#define DEBUG_WARNING  0x02
#define DEBUG_INFO     0x04
#define DEBUG_MIDI     0x08
#define DEBUG_NOTES    0x10
#define DEBUG_BUTTONS  0x20
#define DEBUG_DISPLAY  0x40
#define DEBUG_STATE    0x80
#define DEBUG_MOVE_NOTES 0x100
#define DEBUG_ALL      0xFF

extern uint8_t debugLevel;  // Set in Globals.cpp

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
  const int ENCODER_PIN_A = 30;
  const int ENCODER_PIN_B = 29;
  const int ENCODER_BUTTON_PIN = 31;
}

// MIDI Configuration
namespace MidiConfig {
  const int CHANNEL = 1;      // Default MIDI channel
  const int PPQN = 24;        // MIDI clock pulses per quarter note
  const int CHANNEL_OMNI = 0; // Channel for listening to all MIDI channels
}

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
