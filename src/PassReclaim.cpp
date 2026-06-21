//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "PassReclaim.h"

#include "LoopEventStore.h"
#include "Utils/MemoryMonitor.h"

void pinPassesFromLoopPasses(const LoopPasses& passes, SlotPassReferences& refs) {
  if (passes.hasRecordPass() && passes.recordPass.id != kInvalidPassId) {
    refs.pinCapturePass(passes.recordPass.id);
  }
  for (const OverdubPass& pass : passes.overdubPasses) {
    if (pass.id != kInvalidPassId) {
      refs.pinCapturePass(pass.id);
    }
  }
  for (const EditPass& editPass : passes.editPasses) {
    if (editPass.id != kInvalidEditPassId) {
      refs.pinEditPass(editPass.id);
    }
  }
}

void collectReferencedPasses(const GlobalUndoStack& stack, PassReferenceSet& out) {
  for (const UndoEntry& entry : stack.entries) {
    if (entry.slotIndex >= Config::MAX_LOOPS_PER_TRACK) {
      continue;
    }
    SlotPassReferences& slotRefs = out.slots[entry.slotIndex];
    switch (entry.kind) {
      case UndoEntryKind::RecordPassAdded:
      case UndoEntryKind::OverdubPassAdded:
        slotRefs.pinCapturePass(entry.passId);
        break;
      case UndoEntryKind::NoteEditPassClosed:
        for (EditPassId id : entry.noteEditPassIds) {
          slotRefs.pinEditPass(id);
        }
        break;
      case UndoEntryKind::ClearSlot:
        if (entry.beforeSnapshot) {
          pinPassesFromLoopPasses(entry.beforeSnapshot->passes, slotRefs);
        }
        if (entry.afterSnapshot) {
          pinPassesFromLoopPasses(entry.afterSnapshot->passes, slotRefs);
        }
        break;
      case UndoEntryKind::LoopBoundaryChange:
      case UndoEntryKind::ControlChangeEditPassClosed:
        break;
    }
  }
}

bool overUndoMemoryPressure(const GlobalUndoStack& stack) {
  const bool chunkPressure =
      LoopEventStore::freeChunkCount() <= PassConfig::CHUNK_RESERVE;
  const bool heapPressure = MemoryMonitor::getFreeHeap() < Config::HEAP_RESERVE_BYTES;
  if (chunkPressure || heapPressure) {
    return true;
  }
  if (stack.entries.size() > Config::PREFERRED_UNDO_DEPTH && (chunkPressure || heapPressure)) {
    return true;
  }
  return false;
}
