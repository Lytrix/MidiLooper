//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#pragma once

#include "OverlapNoteIdSet.h"
#include "Utils/IntervalProjection.h"
#include "Utils/NoteUtils.h"

#include <cstdint>

/// Test/diagnostic Gate 1 helper. Not a production source of truth.
/// Production overlap selection: normalized note geometry + [S, E) intersection
/// (`existingNoteOverlapsIncomingHold`). DisplayWindowUtils is not used.
///
/// Diagnostic membership for one incoming hold [startTick, endTick):
/// same-pitch notes already sounding at startTick (on tick < S, off not yet at S),
/// plus committed note-ons whose start tick t satisfies S <= t < E. Playback offs
/// do not erase.
namespace OverlapNoteIdObservation {

inline uint32_t incomingWindowLength(uint32_t startTick, uint32_t endTick) {
  return (endTick > startTick) ? (endTick - startTick) : 0u;
}

/// Linear sounding span: startTick < endTick. Wrap display (end < start) unwraps by loopLength.
/// startTick == endTick is invalid and rejected. Not a wrap and not an overlap case.
inline bool linearSoundingSpan(uint32_t startTick, uint32_t endTick, uint32_t loopLength,
                               uint32_t& linearStart, uint32_t& linearEnd) {
  if (loopLength == 0) {
    return false;
  }
  linearStart = IntervalProjection::tickPhaseInLoop(startTick, 0, loopLength);
  linearEnd = IntervalProjection::tickPhaseInLoop(endTick, 0, loopLength);
  if (linearEnd == linearStart) {
    return false;
  }
  if (linearEnd < linearStart) {
    linearEnd += loopLength;
  }
  return linearStart < linearEnd;
}

inline bool existingNoteOverlapsIncomingHold(uint32_t existingStart, uint32_t existingEnd,
                                             uint32_t incomingStart, uint32_t incomingEnd,
                                             uint32_t loopLength) {
  uint32_t existingLinearStart = 0;
  uint32_t existingLinearEnd = 0;
  uint32_t incomingLinearStart = 0;
  uint32_t incomingLinearEnd = 0;
  if (!linearSoundingSpan(existingStart, existingEnd, loopLength, existingLinearStart,
                          existingLinearEnd)) {
    return false;
  }
  if (!linearSoundingSpan(incomingStart, incomingEnd, loopLength, incomingLinearStart,
                          incomingLinearEnd)) {
    return false;
  }
  const bool direct =
      existingLinearStart < incomingLinearEnd && existingLinearEnd > incomingLinearStart;
  const bool existingShifted = existingLinearStart + loopLength < incomingLinearEnd &&
                               existingLinearEnd + loopLength > incomingLinearStart;
  const bool incomingShifted = existingLinearStart < incomingLinearEnd + loopLength &&
                               existingLinearEnd > incomingLinearStart + loopLength;
  return direct || existingShifted || incomingShifted;
}

inline bool tickInIncomingHold(uint32_t tick, uint32_t startTick, uint32_t endTick,
                               uint32_t loopLength) {
  const uint32_t windowLength = incomingWindowLength(startTick, endTick);
  if (loopLength == 0 || windowLength == 0) {
    return false;
  }
  const uint32_t rel = IntervalProjection::tickPhaseInLoop(tick, 0, loopLength);
  const uint32_t start = IntervalProjection::tickPhaseInLoop(startTick, 0, loopLength);
  const uint32_t end = IntervalProjection::tickPhaseInLoop(start + windowLength, 0, loopLength);
  if (windowLength >= loopLength) {
    return true;
  }
  if (start < end) {
    return rel >= start && rel < end;
  }
  return rel >= start || rel < end;
}

/// True when a committed note is already sounding at hold start S (on sent with t < S).
inline bool noteSoundingAtHoldStart(uint32_t noteStart, uint32_t noteEnd, uint32_t holdStart,
                                    uint32_t loopLength) {
  uint32_t linearStart = 0;
  uint32_t linearEnd = 0;
  if (!linearSoundingSpan(noteStart, noteEnd, loopLength, linearStart, linearEnd)) {
    return false;
  }
  const uint32_t s = IntervalProjection::tickPhaseInLoop(holdStart, 0, loopLength);
  const bool direct = linearStart < s && s < linearEnd;
  const bool shifted = linearStart < s + loopLength && s + loopLength < linearEnd;
  return direct || shifted;
}

inline void collectGeometryOverlapNoteIds(const NoteUtils::DisplayNoteVec& notes, uint8_t pitch,
                                          uint32_t startTick, uint32_t endTick, uint32_t loopLength,
                                          OverlapNoteIdSet& out) {
  out.clear();
  if (incomingWindowLength(startTick, endTick) == 0) {
    return;
  }
  for (const NoteUtils::DisplayNote& note : notes) {
    if (note.note != pitch || note.noteId == kInvalidNoteId) {
      continue;
    }
    if (existingNoteOverlapsIncomingHold(note.startTick, note.endTick, startTick, endTick,
                                         loopLength)) {
      (void)out.insert(note.noteId);
    }
  }
}

inline void collectObservedOverlapNoteIds(const NoteUtils::DisplayNoteVec& notes, uint8_t pitch,
                                          uint32_t startTick, uint32_t endTick, uint32_t loopLength,
                                          OverlapNoteIdSet& out) {
  out.clear();
  if (incomingWindowLength(startTick, endTick) == 0) {
    return;
  }
  for (const NoteUtils::DisplayNote& note : notes) {
    if (note.note != pitch || note.noteId == kInvalidNoteId) {
      continue;
    }
    uint32_t unusedStart = 0;
    uint32_t unusedEnd = 0;
    if (!linearSoundingSpan(note.startTick, note.endTick, loopLength, unusedStart, unusedEnd)) {
      continue;
    }
    const bool alreadySounding =
        noteSoundingAtHoldStart(note.startTick, note.endTick, startTick, loopLength);
    const bool onDuringHold =
        tickInIncomingHold(note.startTick, startTick, endTick, loopLength);
    if (alreadySounding || onDuringHold) {
      (void)out.insert(note.noteId);
    }
  }
}

}  // namespace OverlapNoteIdObservation
