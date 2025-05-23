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
#define DEBUG_ALL      0xFF

extern uint8_t debugLevel;  // Set in Globals.cpp

// --------------------
// Hardware Configuration
// --------------------
// LCD Display Configuration
namespace LCD {
  const int RS     = 28;    // Register Select
  const int ENABLE = 27;    // Enable
  const int D4     = 32;    // Data 4
  const int D5     = 31;    // Data 5
  const int D6     = 30;    // Data 6
  const int D7     = 29;    // Data 7
  const uint32_t DISPLAY_UPDATE_INTERVAL = 30 ; // in ms (approx. 333Hz)
}

// Button Configuration
namespace Buttons {
  const int RECORD = 37;      // Record/Overdub button
  const int PLAY   = 36;      // Play/Stop button
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
void setupGlobals();                       // Initialize system configuration
bool isBarBoundary();                      // Check if current tick is on bar boundary
void loadConfig();                         // Load configuration from persistent storage
void saveConfig();                         // Save configuration to persistent storage

#endif // GLOBALS_H
