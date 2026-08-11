//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "MidiEvent.h"
#include "Utils/IntervalProjection.h"
#include "Utils/NoteUtils.h"

namespace DisplayWindowUtils {

constexpr uint32_t kMaxDetailedWindowBars = 16;

uint32_t chooseBarsPerSegment(uint32_t loopBars, uint32_t segmentCount);

bool noteIntersectsWindow(const TickInterval& noteSpan, const TickInterval& viewport,
                          uint32_t loopLength);

bool noteIntersectsWindow(uint32_t startTick, uint32_t endTick, uint32_t windowStart,
                          uint32_t windowLength, uint32_t loopLength);

NoteUtils::DisplayNoteVec filterDisplayNotesToWindow(const NoteUtils::DisplayNoteVec& notes,
                                                     const TickInterval& viewport,
                                                     uint32_t loopLength);

NoteUtils::DisplayNoteVec filterDisplayNotesToWindow(const NoteUtils::DisplayNoteVec& notes,
                                                     uint32_t windowStart, uint32_t windowLength,
                                                     uint32_t loopLength);

/// Window inclusion filter for edit/motor/nav inventory — keeps storage ticks unchanged.
NoteUtils::DisplayNoteVec filterDisplayNotesByWindowInclusion(
    const NoteUtils::DisplayNoteVec& notes, const TickInterval& viewport, uint32_t loopLength);

NoteUtils::DisplayNoteVec filterDisplayNotesByWindowInclusion(
    const NoteUtils::DisplayNoteVec& notes, uint32_t windowStart, uint32_t windowLength,
    uint32_t loopLength);

bool segmentHasNotes(const NoteUtils::DisplayNoteVec& notes, uint32_t loopLength,
                     const TickInterval& segment);

bool segmentHasNotes(const NoteUtils::DisplayNoteVec& notes, uint32_t loopLength,
                     uint32_t segStartTick, uint32_t segEndTick);

/// Place detailed window so playheadTick sits near the horizontal center (clamped to loop).
uint32_t resolveCenteredWindowStart(uint32_t playheadTick, uint32_t windowLength,
                                    uint32_t loopLength);

/// Post-stop handoff: keep only the committed compose prefix (drop capture suffix / tails).
inline size_t clampPreservedDisplayNoteCount(size_t liveNoteCount, size_t committedBaseCount) {
    return committedBaseCount < liveNoteCount ? committedBaseCount : liveNoteCount;
}

/// Overdub-stop handoff (RC5a): drop temporary playhead-tail rows only — never strip the
/// capture-preview suffix. When compose base bookkeeping is cold (0), keep the full frame.
inline size_t preservedOverdubStopDisplayNoteCount(size_t liveNoteCount, size_t composeBaseCount) {
    if (composeBaseCount == 0) {
        return liveNoteCount;
    }
    return composeBaseCount < liveNoteCount ? composeBaseCount : liveNoteCount;
}

/// Clean visualCache may authorize committed display; dirty/stale cache must not (RC5b).
inline bool committedDisplayVisualCacheAuthoritative(bool visualCacheDirty,
                                                     bool visualCacheNonempty) {
    return !visualCacheDirty && visualCacheNonempty;
}

/// Overdub committed-window reuse: never treat committedCount==0 as a hit (would resize empty).
inline bool overdubCommittedWindowCacheReusable(size_t committedNoteCount, size_t liveNoteCount) {
  return committedNoteCount > 0 && committedNoteCount <= liveNoteCount;
}

/// Overdub committed layer must switch from window gather to full visualCache when idle
/// slices finish — otherwise wrap/auto-follow keeps painting a stale ~18-bar gather until stop.
inline bool shouldPromoteOverdubCommittedToFullVisualCache(bool overdubbing, bool visualCacheDirty,
                                                           bool visualCacheNonempty,
                                                           bool committedFromWindowGather) {
  return overdubbing && !visualCacheDirty && visualCacheNonempty && committedFromWindowGather;
}

/// True when the paint window lies entirely inside a previously gathered tick range.
inline bool paintWindowInsideGather(uint32_t windowStart, uint32_t windowLength,
                                    uint32_t gatherStart, uint32_t gatherLength) {
    if (windowLength == 0 || gatherLength == 0 || windowStart < gatherStart) {
        return false;
    }
    return (windowStart - gatherStart) + windowLength <= gatherLength;
}

TickInterval makeViewportInterval(uint32_t windowStart, uint32_t windowLength);

/// Copy MIDI events whose tick lies in the half-open loop window [start, start + length).
void filterMidiEventsToWindow(const MidiEventVec& events, MidiEventVec& out, uint32_t windowStart,
                              uint32_t windowLength, uint32_t loopLength);

void filterMidiEventsToWindow(const SessionMidiEventVec& events, SessionMidiEventVec& out,
                              uint32_t windowStart, uint32_t windowLength, uint32_t loopLength);

/// Info-strip LEN field: `" --"` when empty; otherwise 3-digit bar count capped at 999.
void formatLoopLengthBars(char* out, size_t outSize, uint32_t loopLengthTicks,
                          uint32_t ticksPerBar);

}  // namespace DisplayWindowUtils
