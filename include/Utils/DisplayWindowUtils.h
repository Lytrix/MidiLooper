//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#pragma once

#include <cstdint>
#include <vector>

#include "Utils/NoteUtils.h"

namespace DisplayWindowUtils {

constexpr uint32_t kMaxDetailedWindowBars = 16;

uint32_t chooseBarsPerSegment(uint32_t loopBars, uint32_t segmentCount);

bool noteIntersectsWindow(uint32_t startTick, uint32_t endTick, uint32_t windowStart,
                          uint32_t windowLength, uint32_t loopLength);

NoteUtils::DisplayNoteVec filterDisplayNotesToWindow(const NoteUtils::DisplayNoteVec& notes,
                                                     uint32_t windowStart, uint32_t windowLength,
                                                     uint32_t loopLength);

/// Window inclusion filter for edit/motor/nav inventory — keeps storage ticks unchanged.
NoteUtils::DisplayNoteVec filterDisplayNotesByWindowInclusion(
    const NoteUtils::DisplayNoteVec& notes, uint32_t windowStart, uint32_t windowLength,
    uint32_t loopLength);

bool segmentHasNotes(const NoteUtils::DisplayNoteVec& notes, uint32_t loopLength,
                     uint32_t segStartTick, uint32_t segEndTick);

/// Place detailed window so playheadTick sits near the horizontal center (clamped to loop).
uint32_t resolveCenteredWindowStart(uint32_t playheadTick, uint32_t windowLength,
                                    uint32_t loopLength);

}  // namespace DisplayWindowUtils
