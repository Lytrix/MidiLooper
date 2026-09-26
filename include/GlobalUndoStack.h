#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "LoopPasses.h"
#include "EditPass.h"
#include "LoopSnapshot.h"
#include "StorageLoopIo.h"
#include "TrackState.h"
#include "UndoLoopGeometry.h"
#include "Utils/InternalHeapFirstAllocator.h"
#include "Utils/ExternalMemoryFirstAllocator.h"

using UndoEntryId = uint32_t;
/// `GlobalUndoStack::nextEntryId` starts at 1; zero never names a live entry.
constexpr UndoEntryId kInvalidUndoEntryId = 0;

/// Reserved `GlobalUndoStack::entries` capacity for the protected deferred-stop publication. The
/// transaction must never grow the stack at its atomic publish (`TrackUndoPublication::begin`
/// rejects a cursor that has reached capacity), so any rebuild that replaces the vector has to
/// preserve this reserve -- otherwise a later stop silently publishes no undo entry while its pass
/// still commits.
constexpr size_t kUndoStackProtectedCapacity = 32;

enum class UndoEntryKind : uint8_t {
  RecordPassAdded = 0,
  OverdubPassAdded = 1,
  ClearSlot = 2,
  LoopBoundaryChange = 3,
  /// Scoped edit batch is independently undoable on the global stack (U:). Does **not** mean
  /// the NOTE_EDIT UI session ended — checkpoints may occur mid-session when edits become durable.
  NoteEditPassClosed = 4,
  ControlChangeEditPassClosed = 5,
};

struct UndoEntry {
  UndoEntryId id = 0;
  UndoEntryKind kind = UndoEntryKind::RecordPassAdded;
  uint8_t slotIndex = 0;
  LoopId loopId = kInvalidLoopId;
  PassId passId = kInvalidPassId;
  /// Extra wrap ids for one OverdubPassAdded session (DEC-038 038.2). Empty = legacy
  /// single `passId` (STK2). When non-empty, `passId` is the last wrap.
  PassIdList passIds;

  LoopSnapshotRef beforeSnapshot;
  LoopSnapshotRef afterSnapshot;
  /// Published immutable roots retained while this entry is on the live GUS
  /// branch (OpenSpec 5.1). Distinct from `beforeSnapshot`/`afterSnapshot`
  /// (`PersistedLoopSnapshot` compatibility DTOs). RAM-only — not in STK wire.
  ImmutableLoopSnapshotRef beforePublishedSnapshot;
  ImmutableLoopSnapshotRef afterPublishedSnapshot;
  /// DEC-073: the committed-pass revision each retained root was captured at. Selecting an entry's
  /// root during undo/redo stores it with this coverage, so a root from an older committed-pass
  /// state can never be stamped current by the selection itself.
  uint32_t beforePublishedCoverage = kSnapshotCoverageUnset;
  uint32_t afterPublishedCoverage = kSnapshotCoverageUnset;
  UndoLoopGeometry beforeGeometry;
  UndoLoopGeometry afterGeometry;

  uint32_t beforeLoopStartTick = 0;
  uint32_t beforeLoopLengthTicks = 0;
  uint32_t afterLoopStartTick = 0;
  uint32_t afterLoopLengthTicks = 0;

  TrackState beforeTrackState = TRACK_EMPTY;
  TrackState afterTrackState = TRACK_EMPTY;
  bool hasTrackState = false;
  bool beforeSlotEnabled = false;
  bool beforeSlotMuted = false;
  bool hasSlotFlags = false;
  bool hasRedoPayload = false;

  EditPassIdList editPassIds;
  uint8_t editPassIndex = 0;
  EditPassType editPassType = EditPassType::Note;
};


/// Final-reference destruction of one privately retired entry. Begin only beyond
/// the transaction's no-fail point; keep alive and drain before active teardown.
/// No public stack reader may observe the moved-from entry during retirement.
class UndoEntryReleaseCursor {
 public:
  UndoEntryReleaseCursor() = default;
  UndoEntryReleaseCursor(const UndoEntryReleaseCursor&) = delete;
  UndoEntryReleaseCursor& operator=(const UndoEntryReleaseCursor&) = delete;
  bool begin(UndoEntry& entry);
  bool step(size_t maximumUnits);
  bool complete() const { return !active_; }
  size_t completedUnits() const { return units_; }
 private:
  UndoEntry entry_;
  LoopSnapshot::ReleaseCursor published_;
  PersistedLoopSnapshotReleaseCursor persisted_;
  size_t units_ = 0;
  uint8_t phase_ = 0;
  bool active_ = false;
};

using UndoEntryVec = std::vector<UndoEntry, ExternalMemoryFirstAllocator<UndoEntry>>;

/// On-disk token before per-track undo stacks in `runtime.bundle.bin` footer (wire bytes: "GUS3").
constexpr uint32_t kGlobalUndoStackToken = 0x33535547UL;
/// Optional undo-stack header extension before entries (wire bytes: "STK1") — scoped-edit fields
/// on **NoteEditPassClosed** / **ControlChangeEditPassClosed** rows. Legacy stacks omit this token.
constexpr uint32_t kGlobalUndoStackScopedEditExtensionToken = 0x314B5453UL;
/// STK2 = STK1 plus OverdubPassAdded companion editPassIds (wire bytes: "STK2").
constexpr uint32_t kGlobalUndoStackOverdubCompanionExtensionToken = 0x324B5453UL;
/// STK3 = STK2 plus OverdubPassAdded wrap passIds (wire bytes: "STK3").
constexpr uint32_t kGlobalUndoStackOverdubPassIdsExtensionToken = 0x334B5453UL;
/// Optional footer extension before `selectedSlotIndex[]` (wire bytes: "SLOT").
constexpr uint32_t kFooterSelectedSlotExtensionToken = 0x534C4F54UL;

struct GlobalUndoStack {
  UndoEntryVec entries;
  UndoEntryId nextEntryId = 1;
  size_t cursor = 0;

  void clear() {
    entries.clear();
    nextEntryId = 1;
    cursor = 0;
  }

  bool canUndo() const { return cursor > 0; }
  bool canRedo() const { return cursor < entries.size(); }
  size_t undoCount() const { return cursor; }
  size_t redoCount() const { return entries.size() - cursor; }
};

/// Capture-pass ids for one OverdubPassAdded. Legacy STK2 rows use `passId` only.
inline void appendOverdubCapturePassIds(const UndoEntry& entry, PassIdList& out) {
  if (!entry.passIds.empty()) {
    for (const PassId id : entry.passIds) {
      if (id != kInvalidPassId) {
        out.push_back(id);
      }
    }
    return;
  }
  if (entry.passId != kInvalidPassId) {
    out.push_back(entry.passId);
  }
}

/// Pass/edit/loop-boundary undo — excludes ClearSlot (sidebar U: depth).
inline bool isPassUndoEntryKind(UndoEntryKind kind) {
  switch (kind) {
    case UndoEntryKind::RecordPassAdded:
    case UndoEntryKind::OverdubPassAdded:
    case UndoEntryKind::NoteEditPassClosed:
    case UndoEntryKind::ControlChangeEditPassClosed:
    case UndoEntryKind::LoopBoundaryChange:
      return true;
    case UndoEntryKind::ClearSlot:
      return false;
  }
  return false;
}

/// Applied undo entries for one loop slot (index < cursor).
inline size_t countAppliedUndoEntriesForSlot(const GlobalUndoStack& stack, uint8_t slotIndex) {
  size_t count = 0;
  const size_t limit = stack.cursor < stack.entries.size() ? stack.cursor : stack.entries.size();
  for (size_t i = 0; i < limit; ++i) {
    if (stack.entries[i].slotIndex == slotIndex) {
      ++count;
    }
  }
  return count;
}

/// Applied pass/edit undo depth for sidebar U: (ClearSlot excluded).
inline size_t countAppliedPassUndoEntriesForSlot(const GlobalUndoStack& stack, uint8_t slotIndex) {
  size_t count = 0;
  const size_t limit = stack.cursor < stack.entries.size() ? stack.cursor : stack.entries.size();
  for (size_t i = 0; i < limit; ++i) {
    if (stack.entries[i].slotIndex == slotIndex &&
        isPassUndoEntryKind(stack.entries[i].kind)) {
      ++count;
    }
  }
  return count;
}

/// Redo-branch entries for one loop slot (index >= cursor).
inline size_t countRedoEntriesForSlot(const GlobalUndoStack& stack, uint8_t slotIndex) {
  size_t count = 0;
  for (size_t i = stack.cursor; i < stack.entries.size(); ++i) {
    if (stack.entries[i].slotIndex == slotIndex) {
      ++count;
    }
  }
  return count;
}

/// Focus a slot's applied/redo tips without changing which actions are undone.
inline void repositionGlobalUndoStackTipForSlot(GlobalUndoStack& stack, uint8_t slotIndex) {
  UndoEntryVec otherEntries;
  UndoEntryVec slotEntries;
  otherEntries.reserve(stack.entries.size());
  slotEntries.reserve(stack.entries.size());
  const size_t applied = std::min(stack.cursor, stack.entries.size());
  size_t otherApplied = 0;
  size_t slotApplied = 0;
  for (size_t i = 0; i < stack.entries.size(); ++i) {
    UndoEntry& entry = stack.entries[i];
    if (entry.slotIndex == slotIndex) {
      slotEntries.push_back(std::move(entry));
      if (i < applied) ++slotApplied;
    } else {
      otherEntries.push_back(std::move(entry));
      if (i < applied) ++otherApplied;
    }
  }
  // Applied focus entries last (Undo); focus redo entries first (Redo).
  // Rebuild even when focus is absent: the vectors now own the moved payloads.
  stack.entries.clear();
  for (size_t i = 0; i < otherApplied; ++i)
    stack.entries.push_back(std::move(otherEntries[i]));
  for (size_t i = 0; i < slotApplied; ++i)
    stack.entries.push_back(std::move(slotEntries[i]));
  stack.cursor = stack.entries.size();
  for (size_t i = slotApplied; i < slotEntries.size(); ++i)
    stack.entries.push_back(std::move(slotEntries[i]));
  for (size_t i = otherApplied; i < otherEntries.size(); ++i)
    stack.entries.push_back(std::move(otherEntries[i]));
}
