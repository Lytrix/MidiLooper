//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>
#include <type_traits>

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
  enum class ClearResult : uint8_t { Idle, Progress, Complete, Superseded };

  /// Destructive departure only. Invalidates all previously borrowed entry pointers.
  /// Acceptance retains payload; a grant must carry the token for this live stack.
  uint32_t beginClear() {
    if (clearResult_ != ClearResult::Progress) {
      advanceClearToken();
      clearResult_ = ClearResult::Progress;
      cursor_ = 0;
    }
    return clearToken_;
  }
  ClearResult stepClear(uint32_t token);
  bool clearComplete(uint32_t token) const {
    return token != 0 && token == clearToken_ && clearResult_ == ClearResult::Complete;
  }

  /// Existing synchronous compatibility; not a bounded retirement grant.
  void clear() {
    advanceClearToken();
    clearResult_ = ClearResult::Idle;
    entries_.clear();
    cursor_ = 0;
  }

  bool canUndo() const { return clearResult_ != ClearResult::Progress && cursor_ > 0; }
  bool canRedo() const {
    return clearResult_ != ClearResult::Progress && cursor_ < entries_.size();
  }
  size_t undoCount() const { return clearResult_ == ClearResult::Progress ? 0 : cursor_; }
  size_t redoCount() const {
    return clearResult_ == ClearResult::Progress ? 0 : entries_.size() - cursor_;
  }
#if defined(PIO_UNIT_TEST_NATIVE)
  size_t clearEntryCountForTest() const { return entries_.size(); }
  size_t clearInventoryForTest() const;
#endif

  bool pushEntry(const SessionUndoEntry& entry) {
    if (clearResult_ == ClearResult::Progress) return false; // reject before nested copy
    SessionUndoEntry copy = entry;
    return pushEntry(std::move(copy));
  }

  bool pushEntry(SessionUndoEntry&& entry) {
    if (clearResult_ == ClearResult::Progress) return false; // preserve caller payload
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
    advanceClearToken();
    clearResult_ = ClearResult::Idle;
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
    if (clearResult_ == ClearResult::Progress) return;
    if (cursor_ >= entries_.size()) {
      return;
    }
    entries_.erase(entries_.begin() + static_cast<std::ptrdiff_t>(cursor_), entries_.end());
  }

 private:
  friend struct EditSession;
  friend class EditManager;
  static bool retireEntryUnit(SessionUndoEntry& entry);
  void advanceClearToken() {
    if (++clearToken_ == 0) ++clearToken_;
  }
  template <typename Map>
  static bool retireMapUnit(Map& map) {
    static_assert(std::is_trivially_destructible<typename Map::mapped_type>::value,
                  "a map retirement leaf must not own nested payload");
    if (!map.empty()) {
      // Global head has the before-begin predecessor, even in a collision chain.
      map.erase(map.begin());
      return true;
    }
    if (map.bucket_count() > 2) {
      // GCC's empty-map destructor still zeroes all buckets. Shrink only after
      // all nodes are gone; rehash then visits no nodes and creates <=2 buckets.
      map.max_load_factor(1.0f);
      map.rehash(0);
      return true;
    }
    return false;
  }
  static bool retireEditRowUnit(EditPassVec& rows) {
    static_assert(std::is_trivially_destructible<MidiEvent>::value,
                  "a MIDI retirement leaf must not own nested payload");
    if (rows.empty()) return false;
    if (!rows.back().addedEvents.empty()) rows.back().addedEvents.pop_back();
    else rows.pop_back();
    return true;
  }
  template <typename Vector>
  static bool retireScalarVectorUnit(Vector& values) {
    static_assert(std::is_trivially_destructible<typename Vector::value_type>::value,
                  "a retirement element must not hide nested payload destruction");
    if (values.empty()) return false;
    values.pop_back();
    return true;
  }
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
  uint32_t clearToken_ = 0;
  ClearResult clearResult_ = ClearResult::Idle;
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
  /// True when NOTE_EDIT opened on a current published root. Stays set across
  /// in-session `saveNoteEditPass` revision bumps so commit rematerialize keeps that root.
  bool openedOnCurrentSnapshot = false;
  uint32_t openedSnapshotRevision = 0;

  bool isNote() const { return sessionType == EditSessionType::Note; }

  enum class PayloadClearResult : uint8_t { Progress, Complete, Superseded };
  struct PayloadClearReceipt {
    const EditSession* owner = nullptr;
    uint32_t token = 0;
  };
  /// Destructive departure component, not an EditManager/session-FSM handoff.
  /// Caller excludes live use/borrowers/writes and qualifies Track/slot/source.
  /// Cancel before legacy reuse; receipts qualify only this same live object.
  PayloadClearReceipt beginPayloadClear();
  PayloadClearResult stepPayloadClear(const PayloadClearReceipt& receipt);
  PayloadClearResult payloadClearResult(const PayloadClearReceipt& receipt) const;
  void cancelPayloadClear() {
    advancePayloadClearToken();
    payloadClearPhase_ = PayloadClearPhase::Idle;
    // Payload stays owned; cancellation does not resurrect live edit authority.
  }
#if defined(PIO_UNIT_TEST_NATIVE)
  size_t payloadClearInventoryForTest() const;
#endif

 private:
  enum class PayloadClearPhase : uint8_t {
    Idle, Store, CurrentState, Undo, UndoStorage, Focus, EditIds, DurableIds,
    ApplyRows, Complete, Superseded
  };
  void advancePayloadClearToken() {
    if (++payloadClearToken_ == 0) ++payloadClearToken_;
  }
  uint32_t payloadClearToken_ = 0;
  uint32_t payloadClearUndoToken_ = 0;
  PayloadClearPhase payloadClearPhase_ = PayloadClearPhase::Idle;
};
