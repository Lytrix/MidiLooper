//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#pragma once

#include <cstdint>

namespace NoteMovementUtils {

/// Wrap a signed or out-of-range tick offset into [0, loopLength). Same behaviour as
/// NoteMovementUtils.cpp before extraction — used from firmware and host unit tests.
inline uint32_t wrapPosition(int32_t position, uint32_t loopLength) {
    if (position < 0) {
        position = static_cast<int32_t>(loopLength) + position;
        while (position < 0) {
            position += static_cast<int32_t>(loopLength);
        }
    } else if (position >= static_cast<int32_t>(loopLength)) {
        position = position % static_cast<int32_t>(loopLength);
    }
    return static_cast<uint32_t>(position);
}

/// Linear canonical off tick: NoteOn.tick + span length (may exceed loopLength).
inline uint32_t linearStorageOffTickForSpanEnd(uint32_t startTick, uint32_t noteLen) {
    return startTick + noteLen;
}

/// Length in ticks from start to end, handling wrap when end < start inside a loop.
inline uint32_t calculateNoteLength(uint32_t start, uint32_t end, uint32_t loopLength) {
    if (end >= start) {
        return end - start;
    }
    return (loopLength - start) + end;
}

/// True when [noteStart, noteEnd] lies within movingNoteStart..movingNoteEnd (display-unwrapped end).
inline bool isNoteWithinMovingNoteRange(uint32_t noteStart, uint32_t noteEnd,
                                        uint32_t movingNoteStart, uint32_t movingNoteEnd,
                                        uint32_t loopLength) {
    if (loopLength == 0) {
        return false;
    }
    const uint32_t displayMovingNoteEnd =
        (movingNoteEnd >= loopLength) ? (movingNoteEnd % loopLength) : movingNoteEnd;
    if (noteEnd < noteStart) {
        return false;
    }
    if (displayMovingNoteEnd >= movingNoteStart) {
        return noteStart >= movingNoteStart && noteEnd <= displayMovingNoteEnd;
    }
    return noteStart >= movingNoteStart || noteEnd <= displayMovingNoteEnd;
}

} // namespace NoteMovementUtils
