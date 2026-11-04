//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#pragma once

/// C3.4 off the Clock (owner rulings 2026-11-04): the overdub wrap hands its interval to one of two FIFO obligation
/// slots in constant work; the discharge lane answers it against the interval's own source cut and seals it into its
/// own record. Design and proofs: `docs/Plans/2026-11-04-wrap-deadline-off-clock.md` (revision 2). Included by the
/// owner (`LoopOverdubSession.cpp`) and by the `Loop` lifetime unit, which owns the deleter.

#include "Loop.h"

#include <utility>
#include <vector>

struct Loop::WrapObligationQueue {
  static constexpr uint8_t kSlots = 2;
  struct Slot {
    enum class Phase : uint8_t { Free, Preparing, Answering, Sealing };
    Phase phase = Phase::Free;
    size_t record = 0;               // the interval's session record (P)
    uint8_t sessionIndex = 0;        // the session that record belongs to (record indices restart per session)
    PassId passId = kInvalidPassId;  // P's committed pass; rows from it on that are in no earlier record are later
    uint32_t loopLengthTicks = 0;
    PendingNoteChangeVec pending;    // P's own pending set: decided rows + owed `Add` rows (swapped, never copied)
    DeferredOverdubCompletion set[kDeferredOverdubCompletionCapacity]{};
    uint8_t setCount = 0;
    uint8_t setCursor = 0;
    size_t overflowCursor = 0;
    bool maskReady = false;
    bool membershipReady = false;
    uint32_t pitchMask[4] = {};
    /// Session-record membership at cut preparation: (row id, record index), sorted by id.
    std::vector<std::pair<uint32_t, uint32_t>> membership;
    LoopContentResolution::TilePrepCursor cut{};
    NoteUtils::DisplayNoteVec cutNotes;
    uint32_t cutSequence = 0;            // C2: the flip journal's sequence at the wrap
    PassId sessionFirstPassId = kInvalidPassId;  // rows below it are pre-session (journaled when they flip)
    CheckedCapacity::RefusalBackoff cutRefusal{};  // a refused cut growth: owed, retried after a bounded wait
  };
  Slot slots[kSlots];
  uint8_t head = 0;
  uint8_t count = 0;
  /// C2: bounded flip journal, cleared whenever the queue empties.
  static constexpr size_t kFlipJournalCapacity = 256;
  std::vector<Loop::WrapCutFlip> flips;
  uint32_t flipSequence = 0;
  uint32_t flipOverflows = 0;
  /// Cut growths refused at the allocation boundary (named; the obligation stays owed and is retried).
  uint32_t cutRefusals = 0;
};
