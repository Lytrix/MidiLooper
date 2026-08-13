//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#pragma once

#include "OverlapNoteIdSet.h"
#include "Utils/DisplayWindowUtils.h"
#include "Utils/IntervalProjection.h"
#include "Utils/NoteUtils.h"

#include <cstdint>

/// Gate 1 observation membership for one incoming hold [startTick, endTick).
/// Snapshot: same-pitch notes already sounding at startTick (on tick < S, off not yet at S).
/// Plus committed note-ons whose start tick t satisfies S <= t < E. Playback offs do not erase.
namespace OverlapNoteIdObservation {

inline uint32_t incomingWindowLength(uint32_t startTick, uint32_t endTick) {
  return (endTick > startTick) ? (endTick - startTick) : 0u;
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
  if (loopLength == 0) {
    return false;
  }
  const uint32_t start = IntervalProjection::tickPhaseInLoop(noteStart, 0, loopLength);
  const uint32_t end = IntervalProjection::tickPhaseInLoop(noteEnd, 0, loopLength);
  const uint32_t s = IntervalProjection::tickPhaseInLoop(holdStart, 0, loopLength);
  if (end > start) {
    return start < s && s < end;
  }
  if (end == start) {
    return false;
  }
  return s > start || s < end;
}

inline void collectGeometryOverlapNoteIds(const NoteUtils::DisplayNoteVec& notes, uint8_t pitch,
                                          uint32_t startTick, uint32_t endTick, uint32_t loopLength,
                                          OverlapNoteIdSet& out) {
  out.clear();
  const uint32_t windowLength = incomingWindowLength(startTick, endTick);
  if (windowLength == 0) {
    return;
  }
  for (const NoteUtils::DisplayNote& note : notes) {
    if (note.note != pitch || note.noteId == kInvalidNoteId) {
      continue;
    }
    if (DisplayWindowUtils::noteIntersectsWindow(note.startTick, note.endTick, startTick,
                                                 windowLength, loopLength)) {
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
