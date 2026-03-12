//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

/**
 * @file MidiConfig.h
 * @brief Centralized MIDI channels, note numbers, and CC numbers.
 * DROID ini and routing must match these values.
 */
#ifndef MIDI_CONFIG_H
#define MIDI_CONFIG_H

#include <cstdint>

namespace MidiConfig {

// --- Ports ---
// Serial8 = DIN MIDI, usbMIDI = USB device, usbHost = USB Host (DROID)

// Channel for listening to all MIDI channels (DIN MIDI input)
constexpr int CHANNEL_OMNI = 0;

// --- Channels ---
namespace Channels {
  constexpr uint8_t DEFAULT = 1;           // Legacy default
  constexpr uint8_t TRACK_SELECT = 2;
  constexpr uint8_t LED_FEEDBACK = 3;      // DROID LED (ch3 works on DROID 1.7)
  constexpr uint8_t TRANSPORT = 4;
  constexpr uint8_t FADER = 15;            // Faders 2,3,4 (coarse, fine, note value)
  constexpr uint8_t SELECT = 16;           // Fader 1 (select), buttons, loop length
}

// --- Record exclusion (channels not recorded) ---
constexpr uint8_t RECORD_EXCLUDE_MIN = 13;
constexpr uint8_t RECORD_EXCLUDE_MAX = 16;

// --- LED feedback (excluded from All Notes Off) ---
constexpr uint8_t LED_CHANNEL_MIN = 1;
constexpr uint8_t LED_CHANNEL_MAX = 4;

// --- LED notes (channel 3) ---
namespace Led {
  constexpr uint8_t CHANNEL = 3;
  constexpr uint8_t CONTENT_BASE = 0;      // 16th step content: notes 0-15
  constexpr uint8_t TICK_OFFSET = 16;      // Tick indicator: notes 16-31
  constexpr uint8_t BAR_BASE = 40;         // 8 bar LEDs: notes 40-47
}

// --- Fader / CC ---
namespace Fader {
  constexpr uint8_t SELECT_CHANNEL = 16;   // Fader 1, pitchbend
  constexpr uint8_t COARSE_CHANNEL = 15;   // Fader 2, pitchbend
  constexpr uint8_t FINE_CHANNEL = 15;
  constexpr uint8_t FINE_CC = 2;
  constexpr uint8_t NOTE_VALUE_CHANNEL = 15;
  constexpr uint8_t NOTE_VALUE_CC = 3;
}

// --- Loop editing ---
namespace LoopEdit {
  constexpr uint8_t LENGTH_CC_CHANNEL = 16;
  constexpr uint8_t LENGTH_CC_NUMBER = 101;
  constexpr uint8_t START_PITCHBEND_CHANNEL = 16;
}

// --- Program change (mode switching) ---
constexpr uint8_t PROGRAM_CHANGE_CHANNEL = 16;

} // namespace MidiConfig

#endif // MIDI_CONFIG_H
