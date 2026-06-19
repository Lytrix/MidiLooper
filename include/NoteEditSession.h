//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include "Edit.h"
#include "LoopEventBuffer.h"
#include "LoopEventStore.h"
#include "NoteEditFocus.h"
#include "Utils/ExtMemAllocator.h"

/// In-session undo before saveEdit — RAM store snapshots only.
struct NoteEditSessionUndoStack {
  static constexpr size_t kMaxDepth = 32;

  void clear() {
    entries_.clear();
    cursor_ = 0;
  }

  bool canUndo() const { return cursor_ > 0; }
  bool canRedo() const { return cursor_ < entries_.size(); }
  size_t undoCount() const { return cursor_; }
  size_t redoCount() const { return entries_.size() - cursor_; }

  void pushBeforeMutation(const LoopEventStore& store) {
    dropRedoBranch();
    entries_.push_back(store.cloneShared());
    cursor_ = entries_.size();
    trimHistory();
  }

  std::shared_ptr<const LoopEventStore> popUndoSnapshot() {
    if (!canUndo()) {
      return nullptr;
    }
    --cursor_;
    return entries_[cursor_];
  }

  std::shared_ptr<const LoopEventStore> popRedoSnapshot() {
    if (!canRedo()) {
      return nullptr;
    }
    const auto snap = entries_[cursor_];
    ++cursor_;
    return snap;
  }

  void dropRedoBranch() {
    if (cursor_ >= entries_.size()) {
      return;
    }
    entries_.erase(entries_.begin() + static_cast<std::ptrdiff_t>(cursor_), entries_.end());
  }

 private:
  void trimHistory() {
    while (entries_.size() > kMaxDepth) {
      entries_.erase(entries_.begin());
      if (cursor_ > 0) {
        --cursor_;
      }
    }
  }

  using SnapshotVec = std::vector<std::shared_ptr<const LoopEventStore>, ExtMemAllocator<std::shared_ptr<const LoopEventStore>>>;
  SnapshotVec entries_;
  size_t cursor_ = 0;
};

struct NoteEditSession {
  CowLoopEventStore store;
  NoteEditSessionUndoStack undoStack;
  NoteEditFocus focus;
  uint8_t spanIndex = 0;
  bool active = false;
  EditIdList spanEditIds;
  EditChangeList pendingChanges;
};
