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
