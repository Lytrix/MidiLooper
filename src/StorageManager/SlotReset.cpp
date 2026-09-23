//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// S2b.5: `resetLoopSlotToEmpty` split out of the StorageManager monolith. It is the destruction the load
// path performs on a slot whose payload is absent or invalid, so a suite proving that a live owner's slot is
// NOT destroyed has to be able to link it. Small and self-contained: it only calls Loop methods.
//
// FLASHMEM: cold path, and this tree sits on the RAM1 FlexRAM bank edge, so it must not consume ITCM.
// The post-link budget check (scripts/check_ram1_budget.py) enforces that.

#include "StorageManager.h"

#include "Globals.h"
#include "Loop.h"
#include "StorageManagerInternal.h"
#include <Arduino.h>

namespace StorageManagerInternal {

FLASHMEM void resetLoopSlotToEmpty(Loop& loop, uint8_t slotIndex) {
    loop.discardPendingCapturePass();
    loop.discardCapture();
    loop.resetPassTimeline();
    loop.loopId = static_cast<LoopId>(slotIndex);
    loop.startLoopTick = 0;
    loop.loopLengthTicks = 0;
    loop.loopStartTick = 0;
    loop.nextPassId_ = 1;
    loop.nextNoteId_ = 1;
    loop.nextMergeSequence_ = 0;
    loop.lastCommittedPassId_ = kInvalidPassId;
    loop.lastTickInLoop = 0;
    loop.nextEventIndex = 0;
    loop.clearEditStateDirty();
    loop.visualCache.clear();
    loop.capturePreview.clear();
    loop.pendingVisualDelta.clear();
    loop.invalidateCaches();
}

}  // namespace StorageManagerInternal
