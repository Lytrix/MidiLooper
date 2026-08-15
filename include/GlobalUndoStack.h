#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "LoopPasses.h"
#include "EditPass.h"
#include "StorageLoopIo.h"
#include "TrackState.h"
#include "UndoLoopGeometry.h"
#include "Utils/InternalHeapFirstAllocator.h"
#include "Utils/ExternalMemoryFirstAllocator.h"

using UndoEntryId = uint32_t;

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

/// Move one slot's entries to the stack tip so `canUndoForLoop` works after content rebuild.
inline void repositionGlobalUndoStackTipForSlot(GlobalUndoStack& stack, uint8_t slotIndex) {
  if (stack.entries.empty()) {
    stack.cursor = 0;
    return;
  }
  UndoEntryVec otherEntries;
  UndoEntryVec slotEntries;
  otherEntries.reserve(stack.entries.size());
  slotEntries.reserve(stack.entries.size());
  for (UndoEntry& entry : stack.entries) {
    if (entry.slotIndex == slotIndex) {
      slotEntries.push_back(std::move(entry));
    } else {
      otherEntries.push_back(std::move(entry));
    }
  }
  if (slotEntries.empty()) {
    stack.cursor = stack.entries.size();
    return;
  }
  stack.entries.clear();
  for (UndoEntry& entry : otherEntries) {
    stack.entries.push_back(std::move(entry));
  }
  for (UndoEntry& entry : slotEntries) {
    stack.entries.push_back(std::move(entry));
  }
  stack.cursor = stack.entries.size();
}
