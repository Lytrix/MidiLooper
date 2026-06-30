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
 * | Fader input   | ch16: pitchbend; ch14: pitchbend (fader2); ch15: CC 2,3 | —                                 |
 * | Fader output  | —                                 | ch16: pitchbend (fader1); ch14: pitchbend (fader2); ch15: CC 2,3 |
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
  constexpr uint8_t FADER = 15;              // Faders 3,4 (fine CC2, note-value CC3)
  constexpr uint8_t FADER_COARSE = 14;      // Fader 2 coarse pitchbend
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
  constexpr uint8_t SELECT_CHANNEL = 16;   // Fader 1, pitchbend (physical / DROID)
  constexpr uint8_t COARSE_CHANNEL = 14;   // Fader 2, pitchbend (physical / DROID)
#if defined(SWAP_FADER1_FADER2_TEST)
  /** Diagnostic build: firmware select↔coarse motor channels swapped for hardware test. */
  constexpr uint8_t SELECT_MOTOR_CHANNEL = 14;
  constexpr uint8_t COARSE_MOTOR_CHANNEL = 16;
#else
  constexpr uint8_t SELECT_MOTOR_CHANNEL = SELECT_CHANNEL;
  constexpr uint8_t COARSE_MOTOR_CHANNEL = COARSE_CHANNEL;
#endif
  constexpr uint8_t FINE_CHANNEL = 15;
  constexpr uint8_t FINE_CC = 2;
  constexpr uint8_t NOTE_VALUE_CHANNEL = 15;
  constexpr uint8_t NOTE_VALUE_CC = 3;
  /** DROID notegate1 on ch16/ch14 (fader1 select, fader2 coarse) → MIDI note 0. */
  constexpr uint8_t MOTOR_TRIGGER_NOTE = 0;
  /** DROID ch15 notegate2 (fader3 fine) → MIDI note 1. */
  constexpr uint8_t FINE_MOTOR_TRIGGER_NOTE = 1;
  /** DROID ch15 notegate3 (fader4 note value) → MIDI note 2. */
  constexpr uint8_t NOTE_VALUE_MOTOR_TRIGGER_NOTE = 2;
}

// --- Loop editing ---
namespace LoopEdit {
  constexpr uint8_t LENGTH_CC_CHANNEL = 16;
  constexpr uint8_t LENGTH_CC_NUMBER = 101;
  constexpr uint8_t START_PITCHBEND_CHANNEL = 16;
}

// --- Program change (edit session on ch16 → DROID _EDIT_NOTE_STATE) ---
constexpr uint8_t PROGRAM_CHANGE_CHANNEL = 16;
namespace SessionProgram {
  constexpr uint8_t LOOP_EDIT = 0;  // LoopEditor motorfaders (selectat=0)
  constexpr uint8_t NOTE_EDIT = 1;  // NoteEditor motorfaders (selectat=1)
}

// --- Bar/Step button input (16th notes + bars, any controller) ---
namespace BarStepButton {
  constexpr uint8_t CHANNEL = 16;
  constexpr uint8_t SIXTEENTH_BASE = 0;
  constexpr uint8_t SIXTEENTH_COUNT = 16;   // notes 0-15
  constexpr uint8_t BAR_BASE = 17;
  constexpr uint8_t BAR_COUNT = 8;          // notes 17-24
}

// --- Pitchbend (bipolar motorfader / fader logical range) ---
// Logical endpoints: -8192 (0 %) … +8192 (100 %). MIDI wire peaks at +8191 (unsigned 16383).
namespace Pitchbend {
  constexpr int16_t MIN = -8192;
  constexpr int16_t CENTER = 0;
  constexpr int16_t MAX = 8192;
  constexpr uint16_t UNSIGNED_MAX = 16383;

  inline int16_t clampLogical(int16_t value) {
    if (value < MIN) {
      return MIN;
    }
    if (value > MAX) {
      return MAX;
    }
    return value;
  }

  inline bool isValidLogical(int16_t value) {
    return value >= MIN && value <= MAX;
  }

  /** Encode logical pitchbend for 14-bit MIDI bytes (clamps +8192 → unsigned 16383). */
  inline uint16_t logicalToUnsigned(int16_t value) {
    const int32_t unsignedValue = static_cast<int32_t>(clampLogical(value)) + 8192;
    if (unsignedValue <= 0) {
      return 0;
    }
    if (unsignedValue >= static_cast<int32_t>(UNSIGNED_MAX)) {
      return UNSIGNED_MAX;
    }
    return static_cast<uint16_t>(unsignedValue);
  }

  inline int16_t unsignedToLogical(uint16_t unsignedValue) {
    if (unsignedValue > UNSIGNED_MAX) {
      unsignedValue = UNSIGNED_MAX;
    }
    return static_cast<int16_t>(static_cast<int32_t>(unsignedValue) - 8192);
  }

  /** Teensy MIDI sendPitchBend accepts -8192 … +8191; +8192 maps to +8191 on the wire. */
  inline int16_t logicalToWireSigned(int16_t value) {
    const int16_t logical = clampLogical(value);
    return logical == MAX ? static_cast<int16_t>(MAX - 1) : logical;
  }
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

// --- DROID USB host outbound (Teensy → DROID) ---
namespace DroidUsbHost {
  /** Minimum gap between consecutive USB-host packets (~1 kHz cap; reduces DROID MIDI loss). */
  constexpr uint16_t MIN_PACKET_GAP_MICROS = 1000;
  /** Max LED feedback packets drained per main-loop call (limits blocking). */
  constexpr uint8_t LED_DRAIN_MAX_PER_LOOP = 4;
  /** Pending LED queue capacity (coalesced per note before send). */
  constexpr uint8_t LED_PENDING_MAX = 64;
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
