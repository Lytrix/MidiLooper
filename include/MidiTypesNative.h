//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

/**
 * @file MidiTypesNative.h
 * @brief Mirrors `midi::MidiType` from Teensyduino MIDI Library 5.x for host unit tests.
 *
 * Values must stay aligned with:
 * `.platformio/packages/framework-arduinoteensy/libraries/MIDI/src/midi_Defs.h`
 * (`enum MidiType : uint8_t`).
 */
#pragma once

#include <cstdint>

namespace midi {

// -----------------------------------------------------------------------------
// Duplicated from midi_Defs.h (Arduino MIDI Library) — keep numeric values identical.
// -----------------------------------------------------------------------------
enum MidiType : uint8_t {
    InvalidType          = 0x00,
    NoteOff              = 0x80,
    NoteOn               = 0x90,
    AfterTouchPoly       = 0xA0,
    ControlChange        = 0xB0,
    ProgramChange        = 0xC0,
    AfterTouchChannel    = 0xD0,
    PitchBend            = 0xE0,
    SystemExclusive      = 0xF0,
    SystemExclusiveStart = SystemExclusive,
    TimeCodeQuarterFrame = 0xF1,
    SongPosition         = 0xF2,
    SongSelect           = 0xF3,
    Undefined_F4         = 0xF4,
    Undefined_F5         = 0xF5,
    TuneRequest          = 0xF6,
    SystemExclusiveEnd   = 0xF7,
    Clock                = 0xF8,
    Undefined_F9         = 0xF9,
    Tick                 = Undefined_F9,
    Start                = 0xFA,
    Continue             = 0xFB,
    Stop                 = 0xFC,
    Undefined_FD         = 0xFD,
    ActiveSensing        = 0xFE,
    SystemReset          = 0xFF,
};

} // namespace midi
