//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#pragma once

#include "OverlapNoteIdSet.h"
#include "Utils/NoteUtils.h"

/// Span lookup for overlap candidates. Empty set means no lookup and no gather/reconstruct.
namespace OverlapCandidateLookup {

inline bool shouldLookupSpans(const OverlapNoteIdSet& ids) {
  return ids.size() > 0;
}

/// Copy matching notes from an already-available list. Does not gather or reconstruct.
inline void appendNotesForIds(const NoteUtils::DisplayNoteVec& source, const OverlapNoteIdSet& ids,
                              NoteUtils::DisplayNoteVec& out) {
  out.clear();
  if (!shouldLookupSpans(ids)) {
    return;
  }
  for (const NoteUtils::DisplayNote& note : source) {
    if (note.noteId != kInvalidNoteId && ids.contains(note.noteId)) {
      out.push_back(note);
    }
  }
}

}  // namespace OverlapCandidateLookup
