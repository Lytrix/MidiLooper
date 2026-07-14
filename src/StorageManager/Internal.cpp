//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "StorageManagerInternal.h"

#include "CurrentWorkspaceStorage.h"
#include "Globals.h"
#include "Loop.h"
#include "LoopEventStore.h"
#include "PersistenceBudget.h"
#include "RtcTime.h"
#include "SetBrowserOverlayPolicy.h"
#include "TrackManager.h"
#include "Utils/DebugSessionCapture.h"
#include <Arduino.h>
#include <cstdio>

namespace StorageManagerInternal {

bool urgentEditSavePending = false;
uint32_t lastEditAutosaveMs = 0;
bool clearEditDirtyAfterDeferredSave = false;
bool quarantineLegacyMonolithAfterSave = false;
CurrentSetStorage::AnchorFields currentSetAnchorFields{};
// Incremental loop writes by default; migration/recovery paths set true explicitly.
bool forceCurrentSetFullLoopWrite = false;
std::array<std::array<bool, Config::MAX_LOOPS_PER_TRACK>, Config::NUM_TRACKS>
    currentSetLoopSlotDirty{};
uint32_t currentSetLastActiveUnix = 0;
char currentSetLoadedFromFolder[16] = {};
uint32_t currentWorkspaceEpoch = 0;
uint32_t lastCommittedWorkspaceEpoch = 0;
uint16_t workspaceDerivedFromSetId = 0;
uint16_t workspaceDerivedFromRevisionId = 0;
uint16_t workspaceLastCommittedRevisionId = 0;
char autoSaveBeforeLoadFolderPending[16] = {};
bool autoSaveBeforeLoadFolderPendingValid = false;
StorageSession storageSession{};

STORAGE_PERSIST_MEM void resetStorageSessionJobs() {
    storageSession.currentWorkspaceSave.pending = false;
    resetDeferredSaveJobState();
    resetMidPassChunkPersistState();
    resetPersistenceWorkItemJobState();
    resetRevisionCommitJobState();
    resetRevisionLoadJobState();
    storageSession.bootRecovery = BootRecoveryJob{};
    storageSession.revisionCommit.overlayBackgroundCommit = false;
    SetBrowserOverlayPolicy::resetNavigation(storageSession.setBrowserNavigation);
}

STORAGE_PERSIST_MEM void emitDeferredSaveSliceTelemetry(const char* phase) {
    CurrentWorkspaceSaveJob& job = storageSession.currentWorkspaceSave;
    char outcome[96];
    if (job.stage == DeferredSaveStage::CurrentSetLoopSlot) {
        snprintf(outcome, sizeof(outcome), "%s:%s:t%u:p%u:c%u:k%u", phase,
                 deferredLoopWriteStageName(job.loopWriteStage),
                 static_cast<unsigned>(job.trackCursor),
                 static_cast<unsigned>(job.poolCursor),
                 static_cast<unsigned>(job.capturePassCursor),
                 static_cast<unsigned>(job.chunkCursor));
    } else if (job.stage == DeferredSaveStage::UndoStacks) {
        snprintf(outcome, sizeof(outcome), "%s:%s:t%u:e%lu:%s", phase,
                 deferredSaveStageName(job.stage),
                 static_cast<unsigned>(job.undoTrackCursor),
                 static_cast<unsigned long>(job.undoEntryCursor),
                 deferredUndoWriteStageName(job.undoWriteStage));
    } else if (job.stage == DeferredSaveStage::CurrentSetMeta) {
        snprintf(outcome, sizeof(outcome), "%s:%s:%s", phase, deferredSaveStageName(job.stage),
                 deferredGlobalHeaderStageName(job.globalHeaderStage));
    } else if (job.stage == DeferredSaveStage::TrackHeaderAndSlots) {
        snprintf(outcome, sizeof(outcome), "%s:%s:t%u:s%u:%s:%s", phase,
                 deferredSaveStageName(job.stage), static_cast<unsigned>(job.trackCursor),
                 static_cast<unsigned>(job.slotCursor), job.trackHeaderWritten ? "slot" : "track",
                 job.trackHeaderWritten ? deferredSlotWriteStageName(job.slotWriteStage)
                                      : deferredTrackWriteStageName(job.trackWriteStage));
    } else if (job.stage == DeferredSaveStage::Footer) {
        snprintf(outcome, sizeof(outcome), "%s:%s:t%u:%s", phase, deferredSaveStageName(job.stage),
                 static_cast<unsigned>(job.footerTrackCursor),
                 deferredFooterWriteStageName(job.footerWriteStage));
    } else {
        snprintf(outcome, sizeof(outcome), "%s:%s:t%u:s%u:p%u", phase,
                 deferredSaveStageName(job.stage), static_cast<unsigned>(job.trackCursor),
                 static_cast<unsigned>(job.slotCursor), static_cast<unsigned>(job.poolCursor));
    }
    SC_PERSIST("slice", micros() - job.startedAtUs, job.heapBefore, job.heapBefore, outcome);
}

STORAGE_PERSIST_MEM void fillSlotSummariesForTrack(uint8_t trackIndex, const Track& track,
                               CurrentWorkspaceStorage::SlotSummary* summaries,
                               size_t summaryCount) {
    if (summaries == nullptr || summaryCount == 0) {
        return;
    }
    const uint32_t ticksPerBar = Track::getTicksPerBar();
    const uint8_t slotLimit = static_cast<uint8_t>(
        summaryCount < Config::MAX_LOOPS_PER_TRACK ? summaryCount : Config::MAX_LOOPS_PER_TRACK);
    for (uint8_t slot = 0; slot < slotLimit; ++slot) {
        const Loop& loop = track.getLoop(slot);
        CurrentWorkspaceStorage::SlotSummary& summary = summaries[slot];
        summary = CurrentWorkspaceStorage::SlotSummary{};
        summary.muted = trackManager.isSlotMuted(trackIndex, slot) ? 1 : 0;
        const bool occupied = loop.passes.hasRecordPass() || !loop.passes.overdubPasses.empty() ||
                              loop.hasPendingCapturePass();
        summary.occupied = occupied ? 1 : 0;
        if (!occupied || loop.loopLengthTicks == 0 || ticksPerBar == 0) {
            continue;
        }
        size_t eventCount = 0;
        if (loop.passes.hasRecordPass()) {
            eventCount += LoopEventStore::countEventsInChunkIds(loop.passes.recordPass.chunkRefs);
        }
        for (const OverdubPass& pass : loop.passes.overdubPasses) {
            eventCount += LoopEventStore::countEventsInChunkIds(pass.chunkRefs);
        }
        summary.noteCount =
            static_cast<uint16_t>(eventCount > UINT16_MAX ? UINT16_MAX : eventCount / 2U);
        summary.bars = static_cast<uint16_t>(loop.loopLengthTicks / ticksPerBar);
    }
}

STORAGE_PERSIST_MEM bool writeWorkspaceMetaAfterDeferredSave() {
    CurrentWorkspaceStorage::WorkspaceMetaRecord record{};
    record.currentEpoch = currentWorkspaceEpoch;
    record.lastCommittedEpoch = lastCommittedWorkspaceEpoch;
    record.derivedFromSetId = workspaceDerivedFromSetId;
    record.derivedFromRevisionId = workspaceDerivedFromRevisionId;
    record.lastCommittedRevisionId = workspaceLastCommittedRevisionId;
    record.updatedUnix = static_cast<uint64_t>(RtcTime::getUnixTime());
    const uint8_t selectedTrack = trackManager.getSelectedTrackIndex();
    if (selectedTrack < trackManager.getTrackCount()) {
        fillSlotSummariesForTrack(selectedTrack, trackManager.getTrack(selectedTrack),
                                  record.slotSummary, CurrentWorkspaceStorage::kSlotSummaryCount);
    }
    return CurrentWorkspaceStorage::writeWorkspaceMetaFile(record);
}

STORAGE_PERSIST_MEM bool isCaptureActiveForPersistence() {
    for (uint8_t trackIndex = 0; trackIndex < trackManager.getTrackCount(); ++trackIndex) {
        const Track& track = trackManager.getTrack(trackIndex);
        if (track.isRecording() || track.isOverdubbing()) {
            return true;
        }
    }
    return false;
}

STORAGE_PERSIST_MEM bool isTransportActiveForPersistence() {
    for (uint8_t trackIndex = 0; trackIndex < trackManager.getTrackCount(); ++trackIndex) {
        const Track& track = trackManager.getTrack(trackIndex);
        if (track.isPlaying() || track.isOverdubbing() || track.isRecording()) {
            return true;
        }
    }
    return false;
}

STORAGE_PERSIST_MEM uint32_t resolvePersistenceSliceBudgetUs(const LooperState& state) {
    return PersistenceBudget::resolvePersistenceSliceBudgetUs(
        isCaptureActiveForPersistence(),
        isTransportActiveForPersistence());
}

STORAGE_PERSIST_MEM bool anyCurrentSetLoopSlotDirty() {
    for (uint8_t track = 0; track < Config::NUM_TRACKS; ++track) {
        for (uint8_t slot = 0; slot < Config::MAX_LOOPS_PER_TRACK; ++slot) {
            if (currentSetLoopSlotDirty[track][slot]) {
                return true;
            }
        }
    }
    return false;
}

}  // namespace StorageManagerInternal
