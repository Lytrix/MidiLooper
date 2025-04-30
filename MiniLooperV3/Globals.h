#ifndef GLOBALS_H
#define GLOBALS_H

#pragma once
#include <Arduino.h>

// --------------------
// Debug flags (set in Globals.cpp)
// --------------------
extern bool DEBUG;
extern bool DEBUG_MIDI;
extern bool DEBUG_NOTES;
extern bool DEBUG_BUTTONS;
extern bool DEBUG_DISPLAY;

// --------------------
// Track and timing constants
// --------------------
#define NUM_TRACKS 4               // Number of looper tracks
#define INTERNAL_PPQN 192          // Internal resolution for timing
#define MIDI_CLOCK_PPQN 24         // Standard MIDI clock pulses per quarter note
#define TICKS_PER_CLOCK (INTERNAL_PPQN / MIDI_CLOCK_PPQN)  // 8 ticks per MIDI clock pulse

// --------------------
// LCD Pin definitions
// --------------------
const int LCD_RS     = 12;
const int LCD_ENABLE = 11;
const int LCD_D4     = 32;
const int LCD_D5     = 31;
const int LCD_D6     = 30;
const int LCD_D7     = 29;

// --------------------
// Musical timing settings (set in Globals.cpp)
// --------------------
extern float bpm;
extern uint32_t ticksPerQuarterNote;   // MIDI resolution (24 for external MIDI)
extern uint32_t quartersPerBar;        // Normally 4 for 4/4 time
extern const uint32_t ticksPerBar;     // Computed as ticksPerQuarterNote * quartersPerBar

// --------------------
// Setup and helpers
// --------------------
void setupGlobals();                   // Optional future config loader
bool isBarBoundary();                  // Used for quantized transitions

#endif // GLOBALS_H
