// Copyright (c) 2026 Lytrix (Eelke Jager)
// Licensed under the PolyForm Noncommercial 1.0.0
#include "TrackUndoPublication.h"
#include "Utils/TrackMem.h"

TRACK_COLD_MEM bool TrackUndoPublication::prewarm(size_t redoCapacity) {
  if (protectedState() || pending_.kind != Command::None || !retiredRedo_.empty() ||
      redoCapacity > Config::ABSOLUTE_MAX_UNDO_ENTRIES) return false;
  retiredRedo_.reserve(redoCapacity);
  return retiredRedo_.capacity() >= redoCapacity;
}

TRACK_COLD_MEM bool TrackUndoPublication::begin(GlobalUndoStack& stack, UndoEntry& preparedEntry) {
  if (protectedState() || pending_.kind != Command::None || !retiredRedo_.empty() ||
      stack.cursor > stack.entries.size() || stack.nextEntryId == 0 || stack.nextEntryId == UINT32_MAX ||
      stack.entries.size() - stack.cursor > retiredRedo_.capacity() ||
      stack.cursor >= stack.entries.capacity() ||
      preparedEntry.id != 0 || preparedEntry.kind != UndoEntryKind::OverdubPassAdded ||
      preparedEntry.passId == kInvalidPassId || preparedEntry.loopId == kInvalidLoopId ||
      preparedEntry.slotIndex >= Config::MAX_LOOPS_PER_TRACK) return false;
  entry_ = std::move(preparedEntry);
  stack_ = &stack;
  storage_ = stack.entries.data();
  expectedSize_ = stack.entries.size();
  originalCursor_ = stack.cursor;
  visibleUndo_ = stack.cursor;
  visibleRedo_ = stack.entries.size() - stack.cursor;
  nextId_ = stack.nextEntryId;
  committedId_ = 0;
  units_ = 0;
  phase_ = Phase::DetachRedo;
  return true;
}

TRACK_COLD_MEM bool TrackUndoPublication::sourceCurrent() const {
  return stack_ && stack_->entries.data() == storage_ &&
      stack_->entries.size() == expectedSize_ && stack_->cursor == originalCursor_ &&
      stack_->nextEntryId == nextId_;
}

TRACK_COLD_MEM bool TrackUndoPublication::stepPrepare(size_t maximumUnits) {
  if (!sourceCurrent() || (phase_ != Phase::DetachRedo && phase_ != Phase::Ready)) return false;
  for (size_t unit = 0; unit < maximumUnits && phase_ == Phase::DetachRedo; ++unit) {
    ++units_;
    if (stack_->entries.size() == originalCursor_) {
      phase_ = Phase::Ready;
    } else {
      retiredRedo_.push_back(std::move(stack_->entries.back()));
      stack_->entries.pop_back();
      --expectedSize_;
    }
  }
  return phase_ == Phase::Ready;
}

TRACK_COLD_MEM bool TrackUndoPublication::canPublish(
    const GlobalUndoStack& stack, LoopId loopId, PassId finalPassId) const {
  return phase_ == Phase::Ready && stack_ == &stack && sourceCurrent() &&
      entry_.loopId == loopId && entry_.passId == finalPassId &&
      stack.entries.size() < stack.entries.capacity();
}

TRACK_COLD_MEM void TrackUndoPublication::publishAtCommit() {
  entry_.id = nextId_;
  stack_->entries.push_back(std::move(entry_));
  stack_->nextEntryId = ++nextId_;
  stack_->cursor = ++originalCursor_;
  expectedSize_ = stack_->entries.size();
  committedId_ = nextId_ - 1;
  visibleUndo_ = stack_->cursor;
  visibleRedo_ = 0;
  phase_ = Phase::Published;
}

TRACK_COLD_MEM bool TrackUndoPublication::beginRestore() {
  if ((phase_ != Phase::DetachRedo && phase_ != Phase::Ready) || !sourceCurrent()) return false;
  phase_ = Phase::Restore;
  return true;
}

TRACK_COLD_MEM bool TrackUndoPublication::stepRestore(size_t maximumUnits) {
  if (phase_ != Phase::Restore || !sourceCurrent()) return false;
  for (size_t unit = 0; unit < maximumUnits && phase_ == Phase::Restore; ++unit) {
    ++units_;
    if (retiredRedo_.empty()) {
      // Keep the private entry intact so its reserved ID buffers and roots can
      // be reused by recovery; no prepared capture resource is discarded.
      phase_ = Phase::Complete;
    } else {
      stack_->entries.push_back(std::move(retiredRedo_.back()));
      retiredRedo_.pop_back();
      ++expectedSize_;
    }
  }
  return phase_ == Phase::Complete;
}

TRACK_COLD_MEM bool TrackUndoPublication::recoverPreparedEntry(UndoEntry& destination) {
  if (phase_ != Phase::Complete || published() || destination.id || destination.passId ||
      destination.passIds.capacity() || destination.editPassIds.capacity() ||
      destination.beforeSnapshot || destination.afterSnapshot ||
      destination.beforePublishedSnapshot || destination.afterPublishedSnapshot) return false;
  destination = std::move(entry_);
  entry_ = UndoEntry{};
  return true;
}

TRACK_COLD_MEM bool TrackUndoPublication::stepReleaseRedo(size_t maximumUnits) {
  if ((phase_ != Phase::Published && phase_ != Phase::ReleaseRedo) || !sourceCurrent()) return false;
  for (size_t unit = 0; unit < maximumUnits; ++unit) {
    ++units_;
    if (!release_.complete()) {
      release_.step(1);
    } else if (!retiredRedo_.empty()) {
      release_.begin(retiredRedo_.back());
      retiredRedo_.pop_back();
      phase_ = Phase::ReleaseRedo;
    } else {
      phase_ = published() ? Phase::AwaitReclaim : Phase::Complete;
      return true;
    }
  }
  return false;
}

TRACK_COLD_MEM bool TrackUndoPublication::beginTrimAfterReclaim() {
  if (phase_ != Phase::AwaitReclaim || !sourceCurrent() || !trim_.begin(*stack_)) return false;
  phase_ = Phase::Trim;
  return true;
}

TRACK_COLD_MEM bool TrackUndoPublication::stepTrim(size_t maximumUnits) {
  if (phase_ != Phase::Trim) return phase_ == Phase::Complete;
  const size_t before = trim_.completedUnits();
  const bool done = trim_.step(maximumUnits);
  units_ += trim_.completedUnits() - before;
  if (done) phase_ = Phase::Complete;
  return done;
}

TRACK_COLD_MEM bool TrackUndoPublication::finish() {
  if (phase_ != Phase::Complete || !release_.complete() || !retiredRedo_.empty() ||
      (!published() && entry_.passId != kInvalidPassId)) return false;
  visibleUndo_ = stack_->cursor;
  visibleRedo_ = stack_->entries.size() - stack_->cursor;
  stack_ = nullptr;
  storage_ = nullptr;
  phase_ = Phase::Idle;
  return true;
}

TRACK_COLD_MEM bool TrackUndoPublication::defer(Command command, uint8_t slot, LoopId loopId) {
  if (!protectedState() || (command != Command::Undo && command != Command::Redo) ||
      slot >= Config::MAX_LOOPS_PER_TRACK ||
      loopId == kInvalidLoopId) return false;
  pending_ = {command, slot, loopId};
  return true;
}

TRACK_COLD_MEM TrackUndoPublication::PendingCommand TrackUndoPublication::takePendingCommand() {
  if (protectedState()) return {};
  const PendingCommand command = pending_;
  pending_ = {};
  return command;
}
