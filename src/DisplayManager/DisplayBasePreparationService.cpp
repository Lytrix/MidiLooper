//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// DEC-237 Slice 3a — the staged committed display base's producer service.
//
// This TU is deliberately separate from `DisplayManager.cpp`: that object is ITCM-routed, and the
// producer's tile/pass state machine (with its identity indexes) must stay in flash. The candidate
// itself lives in DMAMEM here, never in the manager's RAM1 image.
//
// The candidate is placement-constructed on first use rather than declared as a global object: a
// global `Prep` constructor would be emitted in the ITCM `.text` of this TU even though the object
// lives in DMAMEM, and its three identity indexes are far too large for the ITCM budget.

#include "DisplayManager.h"

#include "DisplayManagerInternal.h"
#include "TrackManagerInternal.h"

#include <new>

#if defined(__IMXRT1062__)
#define DISP_COLD_MEM FLASHMEM
#else
#define DISP_COLD_MEM
#endif

namespace {
DMAMEM alignas(DisplayBasePreparation::Prep)
    uint8_t gStagedDisplayBaseStorage[sizeof(DisplayBasePreparation::Prep)];
DMAMEM DisplayBasePreparation::Prep* gStagedDisplayBasePrep = nullptr;

DISP_COLD_MEM DisplayBasePreparation::Prep& stagedDisplayBasePrepStorage() {
    if (gStagedDisplayBasePrep == nullptr) {
        gStagedDisplayBasePrep = new (gStagedDisplayBaseStorage) DisplayBasePreparation::Prep();
    }
    return *gStagedDisplayBasePrep;
}

DISP_COLD_MEM bool stagedDisplayBaseKeyFor(const Track& track, uint8_t displaySlot,
                                           DisplayBasePreparation::Key& key) {
    const Loop& loop = track.getLoop(displaySlot);
    if (loop.loopLengthTicks == 0 || !loop.hasCommittedPasses()) {
        return false;
    }
    key.trackIndex = DisplayManagerInternal::resolveTrackIndex(track);
    key.displaySlot = displaySlot;
    key.loopId = loop.loopId;
    key.playbackRevision = loop.playbackRevision;
    key.loopLengthTicks = loop.loopLengthTicks;
    key.snapshotCurrent = loop.publishedSnapshotIsCurrent();
    key.publishedIdentity = key.snapshotCurrent ? loop.publishedSnapshotIdentity() : nullptr;
    key.rangeStart = 0;
    key.rangeLength = loop.loopLengthTicks;
    return true;
}
}  // namespace

DISP_COLD_MEM bool DisplayManager::stagedDisplayBaseOwed(const Track& track,
                                                         uint8_t displaySlot) const {
    DisplayBasePreparation::Key key;
    if (!stagedDisplayBaseKeyFor(track, displaySlot, key)) {
        return false;
    }
    if (gStagedDisplayBasePrep == nullptr) {
        return true;
    }
    if (!gStagedDisplayBasePrep->ready) {
        return true;
    }
    return !DisplayBasePreparation::keyMatches(gStagedDisplayBasePrep->key, key);
}

DISP_COLD_MEM bool DisplayManager::serviceStagedDisplayBaseTurn(const Track& track,
                                                                uint8_t displaySlot,
                                                                uint32_t currentTick,
                                                                size_t eventBudget) {
    (void)currentTick;
    const Loop& loop = track.getLoop(displaySlot);
    DisplayBasePreparation::Key key;
    if (!stagedDisplayBaseKeyFor(track, displaySlot, key)) {
        cancelStagedDisplayBase();
        return false;
    }
    DisplayBasePreparation::Prep& prep = stagedDisplayBasePrepStorage();
    if (!prep.active || !DisplayBasePreparation::keyMatches(prep.key, key)) {
        if (!DisplayBasePreparation::start(prep, loop, key, Config::TICKS_PER_BAR)) {
            return false;
        }
    }
    if (prep.ready) {
        return true;
    }
    return DisplayBasePreparation::step(prep, loop, eventBudget);
}

DISP_COLD_MEM bool DisplayManager::stagedDisplayBaseReady() const {
    return gStagedDisplayBasePrep != nullptr && gStagedDisplayBasePrep->ready;
}

DISP_COLD_MEM const DisplayBasePreparation::Prep& DisplayManager::stagedDisplayBasePrepForTest()
    const {
    return stagedDisplayBasePrepStorage();
}

DISP_COLD_MEM void DisplayManager::cancelStagedDisplayBase() {
    DisplayBasePreparation::cancel(stagedDisplayBasePrepStorage());
}
