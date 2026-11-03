//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// Session-scoped logical note changes (Add / Shorten / Hide). Not a timeline pass.
// Shared resolution vocabulary for overdub (and later NOTE_EDIT) pending deltas.

#pragma once

#include <cstdint>
#include <vector>

#include "MidiEvent.h"
#include "Utils/InternalHeapFirstAllocator.h"

enum class PendingNoteChangeKind : uint8_t {
  Add = 0,
  Shorten = 1,
  Hide = 2,
};

/// Reserved editPassIndex for overdub-sealed Shorten/Hide rows (not a NOTE_EDIT session index).
constexpr uint8_t kOverdubCompanionEditPassIndex = 255;

/// One canonical geometry outcome held until session commit.
struct PendingNoteChange {
  PendingNoteChangeKind kind = PendingNoteChangeKind::Add;
  NoteId noteId = kInvalidNoteId;
  uint8_t channel = 0;
  uint8_t pitch = 0;
  uint8_t velocity = 0;
  uint32_t startTick = 0;
  uint32_t endTick = 0;
  /// `Add` rows only: this note's overlap consequence is still owed because the bounded deferred completion
  /// set was full when its span closed. The worker promotes it into the set as capacity frees, and the seal
  /// deadline (CONTRACTS.md C3.4) answers any remainder, so the MIDI path never derives it. Trailing, so the
  /// positional initializers above keep their meaning.
  bool consequenceOwed = false;
};

using PendingNoteChangeVec =
    std::vector<PendingNoteChange, InternalHeapFirstAllocator<PendingNoteChange>>;
