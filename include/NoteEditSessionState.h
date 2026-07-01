//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#pragma once

#include <cstdint>
#include "EditPass.h"
#include "EntityIds.h"

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

inline bool noteRefSameTarget(const NoteRef& a, const NoteRef& b) {
  return a.channel == b.channel && a.note == b.note && a.startTick == b.startTick &&
         a.endTick == b.endTick;
}

inline bool noteEditSelectionSameNoteTarget(const NoteEditSelection& sel, const NoteRef& ref) {
  if (!sel.hasNote) {
    return false;
  }
  return noteRefSameTarget(sel.ref, ref);
}

/// True when bracket, hasNote, or NoteRef identity changed (not list index).
inline bool noteEditSelectionTargetChanged(const NoteEditSelection& prior, uint32_t nextBracket,
                                           bool nextHasNote, const NoteRef& nextRef) {
  if (prior.bracketTick != nextBracket) {
    return true;
  }
  if (prior.hasNote != nextHasNote) {
    return true;
  }
  if (nextHasNote && prior.hasNote) {
    return !noteEditSelectionSameNoteTarget(prior, nextRef);
  }
  return false;
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
