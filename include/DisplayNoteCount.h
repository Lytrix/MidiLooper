//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#pragma once

#include "Utils/NoteUtils.h"

#include <cstddef>
#include <cstdint>

/// Compact inventory of an already-available DisplayNote list. No gather or reconstruct.
/// Device emit (`maybeLogStoredNoteCount`) is Gate 0 diagnosis and taxes MIDI runtime;
/// disable that path once later-stage overlap validation proofs exist.
namespace DisplayNoteCount {

struct Result {
  size_t notes = 0;
  size_t uniqueNoteIds = 0;
  size_t maxSamePitch = 0;
};

/// Count valid notes (noteId set, startTick != endTick). Wrap (end < start) is counted.
/// uniqueNoteIds is one per counted row. maxSamePitch is the largest count at one MIDI pitch.
template <typename NoteVec>
inline Result countDisplayNotes(const NoteVec& notes) {
  Result result{};
  uint16_t pitchCount[128] = {};
  for (const NoteUtils::DisplayNote& note : notes) {
    if (note.noteId == kInvalidNoteId || note.startTick == note.endTick) {
      continue;
    }
    ++result.notes;
    ++result.uniqueNoteIds;
    if (note.note < 128) {
      ++pitchCount[note.note];
      if (pitchCount[note.note] > result.maxSamePitch) {
        result.maxSamePitch = pitchCount[note.note];
      }
    }
  }
  return result;
}

}  // namespace DisplayNoteCount
