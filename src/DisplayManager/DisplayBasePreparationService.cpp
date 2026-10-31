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

#include "Utils/DebugSessionCapture.h"

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
DMAMEM uint32_t gStagedDisplayBaseStartUs = 0;
DMAMEM bool gStagedDisplayBaseReported = false;

DISP_COLD_MEM DisplayBasePreparation::Prep& stagedDisplayBasePrepStorage() {
    if (gStagedDisplayBasePrep == nullptr) {
        gStagedDisplayBasePrep = &DisplayBasePreparation::constructFlash(gStagedDisplayBaseStorage);
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
        if (!DisplayBasePreparation::startFlash(prep, loop, key, Config::TICKS_PER_BAR)) {
            return false;
        }
        gStagedDisplayBaseStartUs = micros();
        gStagedDisplayBaseReported = false;
    }
    if (prep.ready) {
        return true;
    }
    const bool ready = DisplayBasePreparation::stepFlash(prep, loop, eventBudget);
#if defined(SESSION_CAPTURE)
    if (ready && !gStagedDisplayBaseReported) {
        char line[176];
        snprintf_P(line, sizeof(line),
                   PSTR("#CAP,%lu,DIAG,disp_base_prep,site=idle,steps=%lu,tiles=%u,wrap=%u,us=%lu,ready=1"),
                   static_cast<unsigned long>(micros()),
                   static_cast<unsigned long>(prep.steps),
                   static_cast<unsigned>(prep.tileCount),
                   static_cast<unsigned>(prep.wrapPassCount),
                   static_cast<unsigned long>(micros() - gStagedDisplayBaseStartUs));
        DebugSessionCapture::appendCaptureTextLine(line);
        gStagedDisplayBaseReported = true;
    }
#endif
    return ready;
}

DISP_COLD_MEM bool DisplayManager::stagedDisplayBaseReady() const {
    return gStagedDisplayBasePrep != nullptr && gStagedDisplayBasePrep->ready;
}

DISP_COLD_MEM const DisplayBasePreparation::Prep& DisplayManager::stagedDisplayBasePrepForTest()
    const {
    return stagedDisplayBasePrepStorage();
}

DISP_COLD_MEM void DisplayManager::cancelStagedDisplayBase() {
    DisplayBasePreparation::cancelFlash(stagedDisplayBasePrepStorage());
}
