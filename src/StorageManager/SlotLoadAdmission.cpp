//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// S2b.5: the load-admission decision, split out of the StorageManager monolith so a native suite can link
// it -- and therefore the load decision -- without the whole persistence layer. It is a pure predicate
// over slot/capture state: no I/O, no allocation, and no mutation.
//
// FLASHMEM: this is a cold predicate, and this tree sits on the RAM1 FlexRAM bank edge, so it must not
// consume ITCM. The post-link budget check (scripts/check_ram1_budget.py) enforces that.

#include "StorageManager.h"

#include "Globals.h"
#include "Loop.h"
#include "SlotLoadSession.h"
#include "StorageManagerInternal.h"
#include "TrackManager.h"
#include <Arduino.h>

bool StorageManager::needsSlotLoad(uint8_t trackIndex, uint8_t slotIndex) {
    if (trackIndex >= Config::NUM_TRACKS || slotIndex >= Config::MAX_LOOPS_PER_TRACK) {
        return false;
    }
    if (StorageManagerInternal::isLoopSlotRestoreAttempted(trackIndex, slotIndex)) {
        return false;
    }
    if (SlotLoadSession::isActiveFor(trackIndex, slotIndex)) {
        return false;
    }
    if (StorageManagerInternal::isActiveLoadLoopJobFor(trackIndex, slotIndex)) {
        return false;
    }
    if (StorageManagerInternal::isParkedLoadLoopJobFor(trackIndex, slotIndex)) {
        return false;
    }
    if (trackIndex < trackManager.getTrackCount()) {
        Track& track = trackManager.getTrack(trackIndex);
        if (track.loopsAllocated() && track.getLoop(slotIndex).hasCommittedPasses()) {
            return false;
        }
    }
    if (StorageManagerInternal::isDeferredLoopSlotRestoreQueued(trackIndex, slotIndex)) {
        return false;
    }
    return true;
}
