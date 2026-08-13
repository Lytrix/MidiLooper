//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#pragma once

#include "OverlapNoteIdSet.h"
#include "Utils/NoteUtils.h"

#include <cstddef>

/// Span lookup for overlap candidates. Empty set means no lookup and no gather/reconstruct.
namespace OverlapCandidateLookup {

inline bool shouldLookupSpans(const OverlapNoteIdSet& ids) {
  return ids.size() > 0;
}

/// Copy matching notes from an already-available list. One pass; stops when every id is found.
/// Does not gather, reconstruct, or call findLinearNoteSpanForNoteId.
inline void appendNotesForIds(const NoteUtils::DisplayNoteVec& source, const OverlapNoteIdSet& ids,
                              NoteUtils::DisplayNoteVec& out, size_t* notesExamined = nullptr) {
  out.clear();
  if (notesExamined != nullptr) {
    *notesExamined = 0;
  }
  if (!shouldLookupSpans(ids)) {
    return;
  }
  size_t examined = 0;
  for (const NoteUtils::DisplayNote& note : source) {
    ++examined;
    if (note.noteId != kInvalidNoteId && ids.contains(note.noteId)) {
      out.push_back(note);
      if (out.size() == ids.size()) {
        break;
      }
    }
  }
  if (notesExamined != nullptr) {
    *notesExamined = examined;
  }
}

}  // namespace OverlapCandidateLookup
