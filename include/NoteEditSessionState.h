//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#pragma once

#include <cstdint>
#include "EditPass.h"

enum class NoteEditKind {
  Select,
  Add,
  Delete,
  Move,
  Pitch,
  Length,
};

struct NoteEditSelection {
  bool hasNote = false;
  NoteRef ref{};
  int displayIdx = -1;
  uint32_t bracketTick = 0;
};

struct NoteEditSessionState {
  NoteEditKind kind = NoteEditKind::Select;
  NoteEditSelection selection{};
};

inline bool isGeometryEditKind(NoteEditKind kind) {
  switch (kind) {
    case NoteEditKind::Add:
    case NoteEditKind::Delete:
    case NoteEditKind::Move:
    case NoteEditKind::Pitch:
    case NoteEditKind::Length:
      return true;
    default:
      return false;
  }
}

/// Encoder cycle order: Select → Move → Pitch → Length → Select.
NoteEditKind advanceEncoderCycleKind(NoteEditKind current);

/// True when a short press on the edit-mode button should cycle kind (overlay already active).
inline bool shouldCycleNoteEditTypeOnShortPress(bool editOverlayWasActive) {
  return editOverlayWasActive;
}

/// First encoder cycle press after fader-origin geometry anchors at Move.
NoteEditKind resolveEncoderCyclePress(NoteEditKind current, bool faderAnchorPending);

inline bool shouldPushGeometryKindUndo(NoteEditKind lastPushed, NoteEditKind mutationKind) {
  return isGeometryEditKind(mutationKind) && mutationKind != lastPushed;
}

inline bool noteEditSelectionSameNoteTarget(const NoteEditSelection& sel, const NoteRef& ref) {
  if (!sel.hasNote) {
    return false;
  }
  return sel.ref.channel == ref.channel && sel.ref.note == ref.note &&
         sel.ref.startTick == ref.startTick && sel.ref.endTick == ref.endTick;
}

/// After fader-1 targets a different note (or clears selection), the next geometry mutation
/// SHALL push a fresh session undo step even when kind stays Move.
inline bool shouldResetGeometryKindUndoOnSelectChange(const NoteEditSelection& prior,
                                                      bool nextHasNote, const NoteRef& nextRef) {
  if (prior.hasNote && !nextHasNote) {
    return true;
  }
  if (prior.hasNote && nextHasNote) {
    return !noteEditSelectionSameNoteTarget(prior, nextRef);
  }
  return false;
}
