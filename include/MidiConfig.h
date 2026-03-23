//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

/**
 * @file MidiConfig.h
 * @brief Centralized MIDI channels, note numbers, and CC numbers.
 * DROID ini and routing must match these values.
 *
 * CONFIG SUMMARY (custom controller remap)
 * ---------------------------------------
 * | Role              | Channel | Notes/CC         | File reference      |
 * |-------------------|---------|------------------|---------------------|
 * | Main buttons      | 16      | 36-39, 3, etc.   | MidiButtonConfig    |
 * | Track select      | 2 or 16 | 48-63 (ch2)      | MidiButtonConfig    |
 * | Bar/16th buttons  | 16      | 0-15, 17-24      | BarStepButton       |
 * | Fader select      | 16      | pitchbend        | Fader SELECT_CHANNEL|
 * | Fader 2,3,4       | 15      | pitchbend, CC 2,3| Fader COARSE/FINE   |
 * | Loop length       | 16      | CC 101           | LoopEdit            |
 * | Loop start/end    | 16      | pitchbend, CC100,101 | droid ini       |
 * | LED feedback out  | 3       | notes 0-31, 40-47| Led namespace       |
 * | Transport LED in  | 4       | note 39          | droid ini           |
 * | Record exclusion  | 13-16   | (not recorded)   | RECORD_EXCLUDE_*    |
 * See docs/MIDI_CONFIG_GUIDE.md for remap instructions.
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

// --- Bar/Step button input (16th notes + bars, any controller) ---
namespace BarStepButton {
  constexpr uint8_t CHANNEL = 16;
  constexpr uint8_t SIXTEENTH_BASE = 0;
  constexpr uint8_t SIXTEENTH_COUNT = 16;   // notes 0-15
  constexpr uint8_t BAR_BASE = 17;
  constexpr uint8_t BAR_COUNT = 8;          // notes 17-24
}

// --- Pitchbend (standard MIDI range) ---
namespace Pitchbend {
  constexpr int16_t MIN = -8192;
  constexpr int16_t CENTER = 0;
  constexpr int16_t MAX = 8191;
}

// --- Transport / main buttons (4-button layout: C2-D#2) ---
namespace Transport {
  constexpr uint8_t NOTE_RECORD = 36;   // C2, Button A
  constexpr uint8_t NOTE_PLAY = 37;     // C#2, Button B
  constexpr uint8_t NOTE_UNDO = 38;     // D2, Button C
  constexpr uint8_t NOTE_REDO = 39;     // D#2, Button D
}

// --- Encoder defaults ---
namespace Encoder {
  constexpr uint8_t DEFAULT_CHANNEL = Channels::DEFAULT;
  constexpr uint8_t DEFAULT_CC = 4;
  constexpr uint8_t UP_VALUE = 127;
  constexpr uint8_t DOWN_VALUE = 0;
}

} // namespace MidiConfig

#endif // MIDI_CONFIG_H
