//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

/**
 * @file MidiConfig.h
 * @brief Centralized MIDI channels, note numbers, and CC numbers.
 * DROID ini and routing must match these values.
 *
 * CONFIG SUMMARY
 * ---------------
 * Buttons and LEDs (notes):
 * | Role              | Button press note (ch 16) | LED feedback note (ch 15) |
 * |-------------------|---------------------------|---------------------------|
 * | Main controls     | 35-39                     | 39                        |
 * | Extended transport| 40-48                     | —                         |
 * | Track select      | 60-67                     | 60-67                     |
 * | Loop select       | 50-57                     | 50-57                     |
 * | Bar select        | 17-24                     | 40-47                     |
 * | 16th select       | 0-15                      | 0-15                      |
 * | Current position  | —                         | 16-31                     |
 * | Loop edit         | CC 100,101, pitchbend     | —                         |
 *
 * Faders (pitchbend/CC, two channels for high-resolution position/move):
 * | Role          | Fader input (ch)                 | Fader output (ch)                 |
 * |---------------|----------------------------------|-----------------------------------|
 * | Fader input   | ch16: pitchbend; ch15: pb, CC 2,3| —                                 |
 * | Fader output  | —                                 | ch16: pitchbend; ch15: pb, CC 2,3 |
 *
 * Record exclusion: channel 16 not recorded. See RECORD_EXCLUDE_*.
 * See docs/Guides/MIDI_CONFIG_GUIDE.md for remap instructions.
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
  constexpr uint8_t DEFAULT = 1;             // Legacy default
  constexpr uint8_t TRACK_SELECT = 16;       // DROID track row (notes 60-67)
  constexpr uint8_t LED_FEEDBACK = 15;       // All LEDs (16th, tick, bar, main controls)
  constexpr uint8_t MAIN_CONTROLS_LED = 15;  // Play/stop LED same channel as other LEDs
  constexpr uint8_t TRANSPORT = 15;          // Alias for MAIN_CONTROLS_LED (play/stop LED)
  constexpr uint8_t FADER = 15;              // Faders 2,3,4 (coarse, fine, note value)
  constexpr uint8_t SELECT = 16;             // Fader 1 (select), buttons, loop length
}

// --- Record exclusion (channel 16 = buttons, not recorded) ---
constexpr uint8_t RECORD_EXCLUDE_MIN = 16;
constexpr uint8_t RECORD_EXCLUDE_MAX = 16;

// --- LED feedback (excluded from All Notes Off) ---
constexpr uint8_t LED_CHANNEL_MIN = 15;
constexpr uint8_t LED_CHANNEL_MAX = 15;

// --- LED notes (channel 15) ---
namespace Led {
  constexpr uint8_t CHANNEL = 15;
  constexpr uint8_t CONTENT_BASE = 0;       // 16th step content: notes 0-15
  constexpr uint8_t CONTENT_COUNT = 16;
  constexpr uint8_t TICK_OFFSET = 16;       // Current position: notes 16-31
  constexpr uint8_t TICK_COUNT = 16;
  constexpr uint8_t BAR_BASE = 40;          // 8 bar LEDs: notes 40-47
  constexpr uint8_t BAR_COUNT = 8;
  constexpr uint8_t TRACK_SELECT_LED_BASE = 60;   // Track row LEDs: notes 60-67 (ch15)
  constexpr uint8_t TRACK_SELECT_LED_COUNT = 8;
  constexpr uint8_t LOOP_SELECT_LED_BASE = 50;    // Loop row LEDs: notes 50-57 (ch15)
  constexpr uint8_t LOOP_SELECT_LED_COUNT = 8;
  constexpr uint8_t MAIN_CONTROLS_NOTE = 39;  // Play/stop LED
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
  constexpr uint8_t NOTE_EDIT_MODE = 38;  // D2, Button C — note edit mode cycle (B2.31)
  constexpr uint8_t NOTE_REDO = 39;     // D#2, Button D
}

// --- Length edit (DROID B2.32 NOTELEN) ---
namespace LengthEdit {
  constexpr uint8_t NOTE = 35;  // Matches DROID ini, groups with main controls 36-39
}

// --- LFO pulse (armed/recording/overdub feedback on Droid) ---
namespace LfoPulse {
  constexpr uint8_t ARM_CHANNEL = 16;
  constexpr uint8_t ARM_NOTE = 70;
  constexpr uint8_t SLOT_CC = 80;   // 0-7 = which loop LED receives pulse, 127 = none
}

// --- Track select row (DROID notes 60+) ---
namespace TrackSelect {
  constexpr uint8_t NOTE_BASE = 60;
  constexpr uint8_t NOTE_COUNT = 8;
}

// --- Extended transport (ch16, notes 40-48; Play/Stop uses 40 to avoid conflict with 39) ---
namespace ExtendedTransport {
  constexpr uint8_t NOTE_PLAY_STOP = 40;      // Play/Stop (39 = Global Transport)
  constexpr uint8_t NOTE_SET_LOOP_START = 41;
  constexpr uint8_t NOTE_SET_LOOP_END = 42;
  constexpr uint8_t NOTE_QUANTIZE = 43;
  constexpr uint8_t NOTE_COPY_PASTE = 44;
  constexpr uint8_t NOTE_MOVE_BACK_BEAT = 45;
  constexpr uint8_t NOTE_MOVE_FORWARD_BEAT = 46;
  constexpr uint8_t NOTE_MOVE_BACK_16TH = 47;
  constexpr uint8_t NOTE_DELETE = 48;         // Delete Note (loadExtended)
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
