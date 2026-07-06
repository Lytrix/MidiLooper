//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "EditPass.h"
#include "Globals.h"
#include "LoopEventBuffer.h"
#include "NoteEditSessionUndo.h"
#include "Utils/MemoryMonitor.h"
#include "NoteEditFocus.h"
#include "NoteEditSessionState.h"
#include "Utils/InternalHeapFirstAllocator.h"

enum class EditSessionType : uint8_t { Loop, Note, ControlChange };

inline EditPassType passTypeForSession(EditSessionType session) {
  switch (session) {
    case EditSessionType::Note:
      return EditPassType::Note;
    case EditSessionType::ControlChange:
      return EditPassType::ControlChange;
    case EditSessionType::Loop:
      break;
  }
  return EditPassType::Note;
}

/// In-session undo before saveEdit — edit pass rows + focus entries (not full store clones).
struct NoteEditSessionUndoStack {
  void clear() {
    entries_.clear();
    cursor_ = 0;
  }

  bool canUndo() const { return cursor_ > 0; }
  bool canRedo() const { return cursor_ < entries_.size(); }
  size_t undoCount() const { return cursor_; }
  size_t redoCount() const { return entries_.size() - cursor_; }

  bool pushEntry(const SessionUndoEntry& entry) {
    trimUntilCanAdmit(entry);
    if (!canHeapAdmitSessionUndoEntry(entry)) {
      return false;
    }
    dropRedoBranch();
    entries_.push_back(entry);
    cursor_ = entries_.size();
    trimHistory();
    return true;
  }

  SessionUndoEntry* popUndoTarget() {
    if (!canUndo()) {
      return nullptr;
    }
    --cursor_;
    return &entries_[cursor_];
  }

  SessionUndoEntry* popRedoTarget() {
    if (!canRedo()) {
      return nullptr;
    }
    SessionUndoEntry* target = &entries_[cursor_];
    ++cursor_;
    return target;
  }

  SessionUndoEntry* peekRedoTarget() {
    if (!canRedo()) {
      return nullptr;
    }
    return &entries_[cursor_];
  }

  void advanceRedoCursor() {
    if (canRedo()) {
      ++cursor_;
    }
  }

  void dropRedoBranch() {
    if (cursor_ >= entries_.size()) {
      return;
    }
    entries_.erase(entries_.begin() + static_cast<std::ptrdiff_t>(cursor_), entries_.end());
  }

 private:
  bool trimOneSessionUndoEntryForDepthCap() {
    if (entries_.empty()) {
      return false;
    }
    if (cursor_ > 0) {
      entries_.erase(entries_.begin());
      --cursor_;
      return true;
    }
    entries_.erase(entries_.begin());
    return true;
  }

  bool trimOneSessionUndoEntryForMemoryPressure() {
    if (entries_.empty()) {
      return false;
    }
    if (cursor_ > 0) {
      entries_.erase(entries_.begin());
      --cursor_;
      return true;
    }
    if (entries_.size() > Config::PREFERRED_SESSION_UNDO_DEPTH) {
      entries_.erase(entries_.begin());
      return true;
    }
    return false;
  }

  void trimHistory() {
    while (entries_.size() > Config::PREFERRED_SESSION_UNDO_DEPTH) {
      if (!trimOneSessionUndoEntryForDepthCap()) {
        break;
      }
    }
  }

  void trimUntilCanAdmit(const SessionUndoEntry& entry) {
    while (!entries_.empty() && !canHeapAdmitSessionUndoEntry(entry)) {
      if (!trimOneSessionUndoEntryForMemoryPressure() &&
          !trimOneSessionUndoEntryForDepthCap()) {
        break;
      }
    }
  }

  using EntryVec = std::vector<SessionUndoEntry, ExternalMemoryFirstAllocator<SessionUndoEntry>>;
  EntryVec entries_;
  size_t cursor_ = 0;
};

struct EditSession {
  EditSessionType sessionType = EditSessionType::Loop;
  CowLoopEventStore store;
  NoteEditSessionUndoStack undoStack;
  NoteEditFocus focus;
  uint8_t editPassIndex = 0;
  bool active = false;
  bool replaceEditPassOnClose = false;
  EditPassIdList editPassIds;

  bool isNote() const { return sessionType == EditSessionType::Note; }
};
