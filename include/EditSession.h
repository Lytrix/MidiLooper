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
#include "NoteEditCurrentState.h"
#include "NoteEditSessionState.h"
#include "Utils/InternalHeapFirstAllocator.h"

#if defined(SESSION_CAPTURE)
#include <Arduino.h>
#endif

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
    SessionUndoEntry copy = entry;
    return pushEntry(std::move(copy));
  }

  bool pushEntry(SessionUndoEntry&& entry) {
#if defined(SESSION_CAPTURE)
    const uint32_t totalStartUs = micros();
    uint32_t phaseStartUs = totalStartUs;
    const size_t stackSizeBefore = entries_.size();
    const size_t entryBaselineCount = entry.focus.baselineMap.size();
    const size_t entryEditRowCount = entry.editRows.size();
#endif
    trimUntilCanAdmit(entry);
#if defined(SESSION_CAPTURE)
    logUndoPushPhase("trim_until_admit", phaseStartUs, stackSizeBefore, cursor_);
    phaseStartUs = micros();
#endif
    if (!canHeapAdmitSessionUndoEntry(entry)) {
#if defined(SESSION_CAPTURE)
      logUndoPushPhase("admit_fail", phaseStartUs, entries_.size(), cursor_, entryBaselineCount);
      logUndoPushPhase("total", totalStartUs, entries_.size(), cursor_, entryEditRowCount);
#endif
      return false;
    }
#if defined(SESSION_CAPTURE)
    logUndoPushPhase("admit_ok", phaseStartUs, entries_.size(), cursor_, entryBaselineCount);
    phaseStartUs = micros();
#endif
    dropRedoBranch();
#if defined(SESSION_CAPTURE)
    logUndoPushPhase("drop_redo", phaseStartUs, entries_.size(), cursor_);
    phaseStartUs = micros();
#endif
    entries_.push_back(std::move(entry));
#if defined(SESSION_CAPTURE)
    logUndoPushPhase("push_back", phaseStartUs, entries_.size(), cursor_);
    phaseStartUs = micros();
#endif
    cursor_ = entries_.size();
    trimHistory();
#if defined(SESSION_CAPTURE)
    logUndoPushPhase("trim_history", phaseStartUs, entries_.size(), cursor_);
    logUndoPushPhase("total", totalStartUs, entries_.size(), cursor_, entryEditRowCount);
#endif
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
  NoteEditCurrentState noteEditCurrentState;
  NoteEditSessionUndoStack undoStack;
  NoteEditFocus focus;
  uint8_t editPassIndex = 0;
  bool active = false;
  bool replaceEditPassOnClose = false;
  EditPassIdList editPassIds;
  /// Global undo (U:) already pushed for these committed **editPass** ids while session stays open.
  EditPassIdList durableCheckpointedEditPassIds;
  bool durableCheckpointCreated = false;
  /// Rows accumulated by applyEditSessionActions for the open noteEditPass batch.
  EditPassVec applyOwnedEditPassRows;

  bool isNote() const { return sessionType == EditSessionType::Note; }
};
