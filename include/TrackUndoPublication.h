// Copyright (c) 2026 Lytrix (Eelke Jager)
// Licensed under the PolyForm Noncommercial 1.0.0
#pragma once
#include "GlobalUndoStack.h"
#include "PassReclaim.h"

/// Track-owned protected stop transaction. Callers exclude all other stack
/// writers/readers while protected; the scalar visible counts remain available.
/// Redo resources remain recoverable until publishAtCommit. Physical retirement
/// happens only after successful canonical/audible/undo publication.
class TrackUndoPublication {
 public:
  enum class Phase : uint8_t {
    Idle, DetachRedo, Ready, Published, ReleaseRedo, AwaitReclaim, Trim, Complete, Restore
  };
  enum class Command : uint8_t { None, Undo, Redo };
  struct PendingCommand {
    Command kind = Command::None;
    uint8_t slot = Config::INVALID_LOOP_SLOT;
    LoopId loopId = kInvalidLoopId;
  };
  bool prewarm(size_t redoCapacity);
  bool begin(GlobalUndoStack& stack, UndoEntry& preparedEntry);
  bool stepPrepare(size_t maximumUnits);
  bool canPublish(const GlobalUndoStack& stack, LoopId loopId, PassId finalPassId) const;
  /// All fallible checks, including the joint audible/canonical proof, precede
  /// this call. The reserved append is the undo part of that same atomic turn.
  void publishAtCommit();
  bool beginRestore();
  bool stepRestore(size_t maximumUnits);
  bool recoverPreparedEntry(UndoEntry& emptyDestination);
  bool stepReleaseRedo(size_t maximumUnits);
  /// Reclaim dropped-redo canonical passes before checking trim pressure. The
  /// enclosing owner supplies its bounded reclaim continuation at this barrier.
  bool beginTrimAfterReclaim();
  bool stepTrim(size_t maximumUnits);
  bool finish();
  bool defer(Command command, uint8_t slot, LoopId loopId);
  PendingCommand takePendingCommand();
  bool protectedState() const { return phase_ != Phase::Idle; }
  bool published() const { return committedId_ != kInvalidUndoEntryId; }
  Phase phase() const { return phase_; }
  UndoEntryId committedId() const { return committedId_; }
  size_t visibleUndoCount() const { return visibleUndo_; }
  size_t visibleRedoCount() const { return visibleRedo_; }
  size_t trimmedCount() const { return trim_.trimmedCount(); }
 private:
  bool sourceCurrent() const;
  GlobalUndoStack* stack_ = nullptr;
  const UndoEntry* storage_ = nullptr;
  UndoEntry entry_;
  UndoEntryVec retiredRedo_;
  UndoEntryReleaseCursor release_;
  GlobalUndoStackTrimCursor trim_;
  PendingCommand pending_;
  size_t expectedSize_ = 0;
  size_t originalCursor_ = 0;
  size_t visibleUndo_ = 0, visibleRedo_ = 0;
  UndoEntryId nextId_ = 0, committedId_ = 0;
  Phase phase_ = Phase::Idle;
};
