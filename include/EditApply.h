//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#pragma once

#include "EditPass.h"
#include "MidiEvent.h"
#include "LoopEventStore.h"
#include "LoopPasses.h"

/// Apply one note edit pass row to a flat MIDI list.
void applyNoteEditPass(MidiEventVec& events, const EditPass& editPass, uint32_t loopLengthTicks);

/// Apply ordered note edit pass rows with move/length identity tracking.
void applyNoteEditPassSequence(MidiEventVec& events, const EditPassVec& rows,
                               uint32_t loopLengthTicks);
void applyNoteEditPassSequence(SessionMidiEventVec& events, const EditPassVec& rows,
                               uint32_t loopLengthTicks);

/// Find note-on index by stable **NoteId**; returns -1 if not found.
int findNoteOnById(const MidiEventVec& events, NoteId noteId);

/// Commit-boundary persist identity for an **existing** capture note. Counts rematerialize
/// NoteOns at commitBaseline pitch+start. Not a general identity lookup. `applyNoteEditPass`
/// must not call this — it stays strictly `targetNoteId`-keyed.
enum class PersistIdentityResolveStatus : uint8_t {
  Unique,
  Unresolved,
  Ambiguous,
};

struct PersistIdentityResolve {
  PersistIdentityResolveStatus status = PersistIdentityResolveStatus::Unresolved;
  NoteId noteId = kInvalidNoteId;
  uint32_t matchCount = 0;
};

PersistIdentityResolve resolvePersistIdentityForExistingNote(
    const MidiEventVec& rematerializeEvents, uint8_t commitBaselinePitch,
    uint32_t commitBaselineStartTick);

enum class PersistIdentityReconcileStatus : uint8_t {
  AlreadyPresent,
  Unique,
  Unresolved,
  Ambiguous,
};

struct PersistIdentityReconcileResult {
  PersistIdentityReconcileStatus status = PersistIdentityReconcileStatus::Unresolved;
  NoteId persistNoteId = kInvalidNoteId;
  uint32_t matchCount = 0;
};

/// If `sessionNoteId` is already a rematerialize NoteOn, leave rows. Else retarget mover
/// Update rows only when `resolvePersistIdentityForExistingNote` is Unique.
PersistIdentityReconcileResult reconcileMoverPersistIdentity(
    const MidiEventVec& rematerializeEvents, NoteId sessionNoteId, uint8_t commitBaselinePitch,
    uint32_t commitBaselineStartTick, EditPassVec& rows);

/// Remove note-on/off pair for **noteId**.
void deleteNoteById(MidiEventVec& events, NoteId noteId);
