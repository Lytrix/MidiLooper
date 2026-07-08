#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "LoopPasses.h"
#include "EditPass.h"
#include "StorageLoopIo.h"
#include "TrackState.h"
#include "Utils/InternalHeapFirstAllocator.h"
#include "Utils/ExternalMemoryFirstAllocator.h"

using UndoEntryId = uint32_t;

enum class UndoEntryKind : uint8_t {
  RecordPassAdded = 0,
  OverdubPassAdded = 1,
  ClearSlot = 2,
  LoopBoundaryChange = 3,
  NoteEditPassClosed = 4,
  ControlChangeEditPassClosed = 5,
};

struct UndoLoopGeometry {
  uint32_t loopLengthTicks = 0;
  uint32_t startLoopTick = 0;
  uint32_t loopStartTick = 0;
};

struct UndoEntry {
  UndoEntryId id = 0;
  UndoEntryKind kind = UndoEntryKind::RecordPassAdded;
  uint8_t slotIndex = 0;
  LoopId loopId = kInvalidLoopId;
  PassId passId = kInvalidPassId;

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
  bool hasRedoPayload = false;

  EditPassIdList editPassIds;
  uint8_t editPassIndex = 0;
  EditPassType editPassType = EditPassType::Note;
};

using UndoEntryVec = std::vector<UndoEntry, ExternalMemoryFirstAllocator<UndoEntry>>;

/// On-disk token before per-track undo stacks in `runtime.bundle.bin` footer (wire bytes: "GUS3").
constexpr uint32_t kGlobalUndoStackToken = 0x33535547UL;
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
