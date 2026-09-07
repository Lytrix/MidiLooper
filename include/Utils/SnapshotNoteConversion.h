//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#pragma once

#include "LoopSnapshot.h"
#include "Utils/NoteUtils.h"

/// Snapshot-note → `DisplayNote` field map. Not display policy and not Loop-owned.
/// Paint (`gatherDrawSourceBars`) and occupy (`rebuildOverdubSourceSpanCache`) share this.
inline NoteUtils::DisplayNote displayNoteFromSnapshotNote(const LoopSnapshotNote& note) {
  NoteUtils::DisplayNote out{};
  out.noteId = note.noteId;
  out.note = note.pitch;
  out.velocity = note.velocity;
  out.startTick = note.startTick;
  out.endTick = note.endTick;
  return out;
}

inline void appendDisplayNoteUniqueById(NoteUtils::DisplayNoteVec& out,
                                        const NoteUtils::DisplayNote& note) {
  if (note.noteId != kInvalidNoteId) {
    for (const auto& existing : out) {
      if (existing.noteId == note.noteId) {
        return;
      }
    }
  }
  out.push_back(note);
}

inline void appendSnapshotNotesForBarRange(const LoopSnapshot& snapshot, uint32_t firstBar,
                                           uint32_t lastBar, NoteUtils::DisplayNoteVec& out) {
  if (firstBar > lastBar) {
    return;
  }
  for (uint32_t bar = firstBar; bar <= lastBar; ++bar) {
    for (uint16_t pitch = 0; pitch < kLoopSnapshotPitchCount; ++pitch) {
      const auto membership = snapshot.noteMembership(static_cast<uint8_t>(pitch), bar);
      forEachSnapshotMembershipReference(
          membership.get(), [&out](const LoopSnapshotNoteRef& reference) {
            if (!reference) {
              return;
            }
            appendDisplayNoteUniqueById(out, displayNoteFromSnapshotNote(*reference));
          });
    }
  }
}
