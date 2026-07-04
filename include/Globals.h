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
#if defined(PIO_UNIT_TEST_NATIVE)
#include <cstdint>
#else
#include <Arduino.h>
#endif

#ifndef BYPASS_STOP_UNDO_SAVE
#define BYPASS_STOP_UNDO_SAVE 0
#endif

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

// GPIO button and encoder pin map (Teensy 4.1 base module).
// MIDI transport notes 36–39 align numerically by convention; namespaces differ (MIDI vs pinMode).
namespace Buttons {
  constexpr int ENCODER_PIN_A = 29;
  constexpr int ENCODER_PIN_B = 30;
  constexpr int ENCODER_BUTTON_PIN = 31;
  constexpr int BUTTON_A_PIN = 36;  // Record / play / delete action family
  constexpr int BUTTON_B_PIN = 37;  // Track select / mute / delete action family
  constexpr int BUTTON_C_PIN = 38;  // Loop / note edit mode switch (NOTE_EDIT_MODE)
  constexpr int BUTTON_D_PIN = 39;  // Play / stop transport action family
  // Legacy aliases (same pins as BUTTON_A/B)
  const int PLAY   = BUTTON_A_PIN;
  const int RECORD = BUTTON_B_PIN;
}

// MIDI Configuration (channels, CCs, record exclusion, LED feedback)
#include "MidiConfig.h"

// --------------------
// Track and Timing Configuration
// --------------------
namespace Config {
  constexpr uint8_t  NUM_TRACKS = 8;                                   // Number of looper tracks
  constexpr uint8_t  MAX_LOOPS_PER_TRACK = 8;
  /// Not a valid track index (255). Use for: no selected track cache / invalid track sentinel.
  constexpr uint8_t  INVALID_TRACK_INDEX = 0xFF;
  /// Not a valid loop slot index (255). Use for: no slot / no pending queue / no recording focus /
  /// queued-record phase reference "use global bar boundary only" (not a loop wrap).
  constexpr uint8_t  INVALID_LOOP_SLOT = 0xFF;
  constexpr uint8_t  INTERNAL_PPQN = 192;                              // Internal resolution for timing
  constexpr uint8_t  QUARTERS_PER_BAR = 4;                             // Time signature numerator (4/4 time) 
  constexpr uint8_t  TICKS_PER_QUARTER_NOTE = INTERNAL_PPQN;           // For Musical Time naming consistency
  constexpr uint8_t  TICKS_PER_CLOCK = (INTERNAL_PPQN / 24);           // 8 ticks per MIDI clock pulse (24 PPQN)
  constexpr uint32_t TICKS_PER_BAR = INTERNAL_PPQN * QUARTERS_PER_BAR; // 768 or your default value (ticksPerQuarterNote * quartersPerBar)
  constexpr uint32_t TICKS_PER_16TH_STEP = INTERNAL_PPQN / 4;          // 192 / 4 = 48 Ticks
  constexpr uint32_t DUPLICATE_TICK_TOLERANCE = TICKS_PER_16TH_STEP / 4;  // 12 ticks = 1/64th note; events within this are treated as duplicates
  /// Default minimum completed note pair length on capture hot stop (Q16). User override: noteMinLengthTicks.
  constexpr uint32_t DEFAULT_NOTE_MIN_LENGTH_TICKS = DUPLICATE_TICK_TOLERANCE;
  /// Default: remove short pairs on capture hot stop. User may disable via noteMinLengthRemoveEnabled.
  constexpr bool DEFAULT_NOTE_MIN_LENGTH_REMOVE_ENABLED = true;
  /// Target undo depth per track when memory is not under pressure.
  constexpr uint16_t PREFERRED_UNDO_DEPTH = 99;
  /// Try to keep at least this many undo entries when trimming under pressure.
  constexpr uint16_t MIN_UNDO_DEPTH = 8;
  /// Hard safety rail — trim oldest entries when exceeded regardless of pressure.
  constexpr uint16_t ABSOLUTE_MAX_UNDO_ENTRIES = 512;
  /// Target in-session undo depth when memory is not under pressure.
  constexpr uint16_t PREFERRED_SESSION_UNDO_DEPTH = 32;
  /// Try to keep at least this many session undo entries when trimming under pressure.
  constexpr uint16_t MIN_SESSION_UNDO_DEPTH = 4;
  /// Minimum free heap (bytes) held back for edit vectors and undo metadata.
  constexpr uint32_t HEAP_RESERVE_BYTES = 32 * 1024;
  constexpr uint8_t  PLAYBACK_WINDOW_MIN_BARS = 2;
  constexpr uint8_t  PLAYBACK_WINDOW_MAX_BARS = 8;
  /// Above this event count, overdub undo still stores O(1) refs but logs a degraded-undo warning.
  constexpr size_t SNAPSHOT_DEGRADED_UNDO_EVENT_THRESHOLD = 4000;
  constexpr uint32_t autosaveIntervalMs = 300000;
  /// Wall-clock max wait before deferred full validate runs while track is PLAYING-only.
  constexpr uint32_t deferredValidateMaxDelayMs = 60000;
  /// Max SD persistence slice duration while capture or playback is active (revision-commit spec).
  constexpr uint32_t maxPersistenceMicrosActive = 300;
  /// Unbounded persistence slice when transport idle and capture inactive.
  constexpr uint32_t maxPersistenceMicrosIdle = UINT32_MAX;
  /// Cap each main loop() persistence drain so display/MIDI stay responsive when idle.
  constexpr uint32_t maxPersistenceMicrosPerLoop = 3000;
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
/// User-settable minimum completed note pair length (ticks) removed on capture hot stop. Default: DEFAULT_NOTE_MIN_LENGTH_TICKS (12).
extern uint32_t noteMinLengthTicks;
/// When false, hot stop skips removePairsShorterThanNoteMinLength (keep flams/grace notes). Default: true.
extern bool noteMinLengthRemoveEnabled;

// --------------------
// System Functions
// --------------------
bool isBarBoundary();                      // Check if current tick is on bar boundary

// Still to be implemented on EEPROM
void setupGlobals();                       // Initialize system configuration
void loadConfig();                         // Load configuration from persistent storage
void saveConfig();                         // Save configuration to persistent storage

#endif // GLOBALS_H
