//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#pragma once

#include "OverlapNoteIdObservation.h"
#include "OverlapNoteIdSet.h"
#include "Utils/NoteUtils.h"

#include <cstdint>

/// Hold-duration candidate collection for PendingNote.overlapNoteIds.
/// Selection stays geometry + [S, E). This only records ids.
namespace OverlapHoldCandidates {

/// Same-pitch notes occupying hold start S (`[start, end)`). Includes a note
/// that starts at S. Does not clear `out`.
inline void snapshotSoundingAtHoldStart(const NoteUtils::DisplayNoteVec& notes, uint8_t pitch,
                                        uint32_t holdStartTick, uint32_t loopLength,
                                        OverlapNoteIdSet& out) {
  if (loopLength == 0) {
    return;
  }
  for (const NoteUtils::DisplayNote& note : notes) {
    if (note.note != pitch || note.noteId == kInvalidNoteId) {
      continue;
    }
    uint32_t linearStart = 0;
    uint32_t linearEnd = 0;
    if (!OverlapNoteIdObservation::linearSoundingSpan(note.startTick, note.endTick, loopLength,
                                                      linearStart, linearEnd)) {
      continue;
    }
    const uint32_t s = IntervalProjection::tickPhaseInLoop(holdStartTick, 0, loopLength);
    const bool direct = linearStart <= s && s < linearEnd;
    const bool shifted = linearStart <= s + loopLength && s + loopLength < linearEnd;
    if (direct || shifted) {
      (void)out.insert(note.noteId);
    }
  }
}

/// Playback note-on while the incoming hold is open. Offs do not erase. Invalid id rejected.
inline bool considerPlaybackNoteOn(OverlapNoteIdSet& out, NoteId noteId, uint8_t eventPitch,
                                   uint8_t holdPitch) {
  if (noteId == kInvalidNoteId || eventPitch != holdPitch) {
    return false;
  }
  return out.insert(noteId);
}

}  // namespace OverlapHoldCandidates
