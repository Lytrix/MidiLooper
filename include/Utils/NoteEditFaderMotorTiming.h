//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#pragma once

#include <cstdint>

#ifdef ARDUINO
#include <Arduino.h>
#endif

namespace NoteEditFaderMotorTiming {

/** Reference tempo for converting MIDI tick gaps to milliseconds (Ableton export). */
constexpr uint16_t kReferenceTicksPerBeat = 96;
constexpr uint16_t kReferenceTempoBpm = 120;

/** Gaps from test/test_faders/fader{2,3,4}_*.mid (track offset ignored). */
constexpr uint16_t kInterPositionGapTicks = 12;
constexpr uint16_t kMotorNoteDurationTicks = 24;
constexpr uint8_t kPositionSendCount = 3;

constexpr uint32_t ticksToMilliseconds(uint16_t ticks) {
    return static_cast<uint32_t>(
        (static_cast<uint64_t>(ticks) * 60000ULL) /
        (static_cast<uint64_t>(kReferenceTempoBpm) * kReferenceTicksPerBeat));
}

constexpr uint32_t kInterPositionGapMs = ticksToMilliseconds(kInterPositionGapTicks);
constexpr uint32_t kMotorNoteDurationMs = ticksToMilliseconds(kMotorNoteDurationTicks);

/**
 * DROID motorfader burst: three position sends (12-tick gaps), notegate on with the third,
 * short note (24 ticks) for fast motor refresh. Matches Ableton-validated fader2/3/4 clips.
 */
template <typename SendPosition, typename SendNoteOn, typename SendNoteOff>
inline void runMotorFaderBurst(SendPosition sendPosition, SendNoteOn sendNoteOn,
                               SendNoteOff sendNoteOff) {
    sendPosition();
#ifdef ARDUINO
    delay(kInterPositionGapMs);
#endif
    sendPosition();
#ifdef ARDUINO
    delay(kInterPositionGapMs);
#endif
    sendPosition();
    sendNoteOn();
#ifdef ARDUINO
    delay(kMotorNoteDurationMs);
#endif
    sendNoteOff();
}

}  // namespace NoteEditFaderMotorTiming
