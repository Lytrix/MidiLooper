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
        slotRefs.pinCapturePass(entry.passId);
        break;
      case UndoEntryKind::OverdubPassAdded:
        slotRefs.pinCapturePass(entry.passId);
        for (EditPassId id : entry.editPassIds) {
          slotRefs.pinEditPass(id);
        }
        break;
      case UndoEntryKind::NoteEditPassClosed:
      case UndoEntryKind::ControlChangeEditPassClosed:
        for (EditPassId id : entry.editPassIds) {
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
        break;
    }
  }
}

bool overUndoMemoryPressure(const GlobalUndoStack& stack) {
  const bool chunkPressure =
      LoopEventStore::freeChunkCount() <= PassConfig::CHUNK_RESERVE;
  const bool heapPressure =
      MemoryMonitor::getInternalHeapFreeBytes() < Config::HEAP_RESERVE_BYTES;
  if (chunkPressure || heapPressure) {
    return true;
  }
  if (stack.entries.size() > Config::PREFERRED_UNDO_DEPTH && (chunkPressure || heapPressure)) {
    return true;
  }
  return false;
}

size_t trimGlobalUndoStackForMemory(GlobalUndoStack& stack) {
  size_t trimmed = 0;

  auto shouldTrim = [&]() {
    if (stack.entries.empty()) {
      return false;
    }
    if (stack.entries.size() > Config::ABSOLUTE_MAX_UNDO_ENTRIES) {
      return true;
    }
    if (stack.entries.size() <= Config::MIN_UNDO_DEPTH) {
      return false;
    }
    return overUndoMemoryPressure(stack);
  };

  while (shouldTrim()) {
    if (stack.cursor > 0) {
      stack.entries.erase(stack.entries.begin());
      --stack.cursor;
      ++trimmed;
      continue;
    }

    // cursor == 0: entire stack is the redo branch. Keep it unless absolute rail or pressure.
    if (stack.entries.size() > Config::ABSOLUTE_MAX_UNDO_ENTRIES) {
      stack.entries.erase(stack.entries.begin());
      ++trimmed;
      continue;
    }
    if (!overUndoMemoryPressure(stack)) {
      break;
    }
    stack.entries.erase(stack.entries.begin());
    ++trimmed;
  }

  return trimmed;
}
