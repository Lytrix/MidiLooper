//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#pragma once

#include <cstdint>
#include <vector>

#include "EditPass.h"
#include "GlobalUndoStack.h"
#include "Globals.h"
#include "LoopPasses.h"
#include "StorageLoopIo.h"
#include "Utils/InternalHeapFirstAllocator.h"

struct SlotPassReferences {
  std::vector<PassId, InternalHeapFirstAllocator<PassId>> capturePassIds;
  std::vector<EditPassId, InternalHeapFirstAllocator<EditPassId>> editPassIds;

  void pinCapturePass(PassId id) {
    if (id == kInvalidPassId) {
      return;
    }
    for (PassId pinned : capturePassIds) {
      if (pinned == id) {
        return;
      }
    }
    capturePassIds.push_back(id);
  }

  void pinEditPass(EditPassId id) {
    if (id == kInvalidEditPassId) {
      return;
    }
    for (EditPassId pinned : editPassIds) {
      if (pinned == id) {
        return;
      }
    }
    editPassIds.push_back(id);
  }

  bool referencesCapturePass(PassId id) const {
    for (PassId pinned : capturePassIds) {
      if (pinned == id) {
        return true;
      }
    }
    return false;
  }

  bool referencesEditPass(EditPassId id) const {
    for (EditPassId pinned : editPassIds) {
      if (pinned == id) {
        return true;
      }
    }
    return false;
  }
};

struct PassReferenceSet {
  SlotPassReferences slots[Config::MAX_LOOPS_PER_TRACK];
};

void pinPassesFromLoopPasses(const LoopPasses& passes, SlotPassReferences& refs);
void collectReferencedPasses(const GlobalUndoStack& stack, PassReferenceSet& out);

bool overUndoMemoryPressure(const GlobalUndoStack& stack);

/// Trim global undo entries under depth/pressure rails. Prefers dropping entries before
/// cursor (undo history) so the redo branch at cursor..end stays intact when cursor > 0.
size_t trimGlobalUndoStackForMemory(GlobalUndoStack& stack);

struct PassReclaimStats {
  uint16_t passesReclaimed = 0;
  uint16_t chunksReleased = 0;
  uint16_t chunksFreeBefore = 0;
  uint16_t chunksFreeAfter = 0;
  uint32_t durationUs = 0;
};

/// Feedback trimming of an exclusively owned, unpublished stack. The enclosing
/// Track transaction must hide it from readers/mutators and cross its no-fail
/// point before begin: retirement cannot be rolled back. No pass reclaim occurs
/// here; the transaction schedules it after prepared canonical proofs are retired.
class GlobalUndoStackTrimCursor {
 public:
  bool begin(GlobalUndoStack& privateStack);
  bool step(size_t maximumUnits);
  bool complete() const { return stack_ == nullptr; }
  size_t trimmedCount() const { return trimmed_; }
  size_t completedUnits() const { return units_; }
 private:
  GlobalUndoStack* stack_ = nullptr;
  UndoEntryReleaseCursor release_;
  size_t shift_ = 0;
  size_t trimmed_ = 0;
  size_t units_ = 0;
  uint8_t phase_ = 0;
};
