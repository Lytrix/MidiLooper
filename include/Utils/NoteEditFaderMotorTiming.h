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

/** F1 idle before flushing coalesced select-dependent motor sync (live note select). */
constexpr uint32_t kSelectFaderMotorIdleMs = 300;

constexpr uint32_t ticksToMilliseconds(uint16_t ticks) {
    return static_cast<uint32_t>(
        (static_cast<uint64_t>(ticks) * 60000ULL) /
        (static_cast<uint64_t>(kReferenceTempoBpm) * kReferenceTicksPerBeat));
}

constexpr uint32_t kInterPositionGapMs = ticksToMilliseconds(kInterPositionGapTicks);
constexpr uint32_t kMotorNoteDurationMs = ticksToMilliseconds(kMotorNoteDurationTicks);

inline bool shouldFlushSelectDependentMotorSync(uint32_t nowMs, uint32_t lastSelectFaderTimeMs,
                                                bool pendingValid) {
    if (!pendingValid) {
        return false;
    }
    if (lastSelectFaderTimeMs == 0) {
        return true;
    }
    return (nowMs - lastSelectFaderTimeMs) >= kSelectFaderMotorIdleMs;
}

inline bool noteEditDisplayPaintedForMotorSync(uint32_t paintedEpoch, uint32_t requiredPaintEpoch) {
    return paintedEpoch >= requiredPaintEpoch;
}

inline bool selectDependentSettleExpired(uint32_t nowMs, uint32_t settleUntilMs) {
    return settleUntilMs == 0 || nowMs >= settleUntilMs;
}

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

/**
 * Parallel interleaved burst for multiple dependent faders: each round sends all enabled
 * positions back-to-back, then one inter-position gap (same total gap count as single-fader burst).
 */
template <typename Slot0, typename Slot1, typename Slot2>
inline void runParallelMotorFaderBursts(Slot0& slot0, Slot1& slot1, Slot2& slot2) {
    for (uint8_t round = 0; round < kPositionSendCount; ++round) {
        if (slot0.enabled) {
            slot0.sendPosition();
        }
        if (slot1.enabled) {
            slot1.sendPosition();
        }
        if (slot2.enabled) {
            slot2.sendPosition();
        }
        if (round + 1 < kPositionSendCount) {
#ifdef ARDUINO
            delay(kInterPositionGapMs);
#endif
        }
    }
    if (slot0.enabled) {
        slot0.sendNoteOn();
    }
    if (slot1.enabled) {
        slot1.sendNoteOn();
    }
    if (slot2.enabled) {
        slot2.sendNoteOn();
    }
#ifdef ARDUINO
    delay(kMotorNoteDurationMs);
#endif
    if (slot0.enabled) {
        slot0.sendNoteOff();
    }
    if (slot1.enabled) {
        slot1.sendNoteOff();
    }
    if (slot2.enabled) {
        slot2.sendNoteOff();
    }
}

}  // namespace NoteEditFaderMotorTiming
