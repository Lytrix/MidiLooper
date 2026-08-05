//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#pragma once

#include <cstdint>
#include <vector>

#include "EditPass.h"
#include "LoopPasses.h"
#include "MidiEvent.h"
#include "Utils/InternalHeapFirstAllocator.h"

/// Stable track identity for **EditorSelection**.
/// Invalid sentinel matches **LoopId** — **UINT32_MAX** (not **0**, which is a valid pool index).
using TrackId = uint32_t;
constexpr TrackId kInvalidTrackId = UINT32_MAX;

enum class NoteEditKind {
  Select,
  Add,
  Delete,
  Move,
  Pitch,
  Length,
};

struct EditorSelection {
  TrackId trackId = kInvalidTrackId;
  LoopId loopId = kInvalidLoopId;
  std::vector<NoteId, InternalHeapFirstAllocator<NoteId>> selectedNotes;
  NoteId primaryNote = kInvalidNoteId;
  /// Projected-interval select position [0, loopLength) — UIP **selectedTick**.
  uint32_t selectedTick = 0;
};

struct NoteEditSessionState {
  NoteEditKind kind = NoteEditKind::Select;
  EditorSelection selection{};
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

inline bool editorSelectionHasNote(const EditorSelection& sel) {
  return sel.primaryNote != kInvalidNoteId;
}

inline bool editorSelectionSameNoteTarget(const EditorSelection& sel, NoteId noteId) {
  if (!editorSelectionHasNote(sel)) {
    return false;
  }
  return sel.primaryNote == noteId;
}

/// True when the active focus driver matches the selected edit driver.
inline bool editorSelectionMatchesDriverNote(const EditorSelection& sel, NoteId driverNote) {
  return editorSelectionHasNote(sel) && sel.primaryNote == driverNote;
}

/// True when bracket, hasNote, or **NoteId** changed (not list index).
inline bool editorSelectionTargetChanged(const EditorSelection& prior, uint32_t nextBracket,
                                         NoteId nextPrimaryNote) {
  if (prior.selectedTick != nextBracket) {
    return true;
  }
  const bool priorHas = editorSelectionHasNote(prior);
  const bool nextHas = nextPrimaryNote != kInvalidNoteId;
  if (priorHas != nextHas) {
    return true;
  }
  if (nextHas && priorHas) {
    return prior.primaryNote != nextPrimaryNote;
  }
  return false;
}

/// After fader-1 targets a different note (or clears selection), the next geometry mutation
/// SHALL push a fresh session undo step even when kind stays Move.
inline bool shouldResetGeometryKindUndoOnSelectChange(const EditorSelection& prior,
                                                      NoteId nextPrimaryNote) {
  if (editorSelectionHasNote(prior) && nextPrimaryNote == kInvalidNoteId) {
    return true;
  }
  if (editorSelectionHasNote(prior) && nextPrimaryNote != kInvalidNoteId) {
    return !editorSelectionSameNoteTarget(prior, nextPrimaryNote);
  }
  return false;
}
