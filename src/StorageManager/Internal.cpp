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
STORAGE_PERSIST_DATA std::array<std::array<bool, Config::MAX_LOOPS_PER_TRACK>, Config::NUM_TRACKS>
    currentSetLoopSlotDirty{};
uint32_t currentSetLastActiveUnix = 0;
STORAGE_PERSIST_DATA char currentSetLoadedFromFolder[16] = {};
uint32_t currentWorkspaceEpoch = 0;
uint32_t lastCommittedWorkspaceEpoch = 0;
uint16_t workspaceDerivedFromSetId = 0;
uint16_t workspaceDerivedFromRevisionId = 0;
uint16_t workspaceLastCommittedRevisionId = 0;
STORAGE_PERSIST_DATA char autoSaveBeforeLoadFolderPending[16] = {};
bool autoSaveBeforeLoadFolderPendingValid = false;
STORAGE_PERSIST_DATA char restoredSetBundlePath_[80] = {};
STORAGE_PERSIST_DATA std::array<uint32_t, Config::NUM_TRACKS> undoStackFileOffsets_{};
uint8_t undoHydrateTrackIndex_ = 0;
bool undoSnapshotsPending_ = false;

void clearAutoSaveBeforeLoadFolderPending() {
    autoSaveBeforeLoadFolderPending[0] = '\0';
    autoSaveBeforeLoadFolderPendingValid = false;
}
// Cold persistence job aggregate -- OCRAM/RAM2. Not on the MIDI clock / ISR hot path.
// Size ~5.5 KB; DTCM is reserved for midiHandler / trackManager / playback.
DMAMEM StorageSession storageSession{};

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
    } else if (job.stage == DeferredSaveStage::CurrentSetCompletion) {
        snprintf(outcome, sizeof(outcome), "%s:%s:%s", phase, deferredSaveStageName(job.stage),
                 deferredCompletionWriteStageName(job.completionWriteStage));
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
            eventCount += LoopEventStore::countEventsInChunkIds(loop.passes.recordPass.committedChunkIds);
        }
        for (const OverdubPass& pass : loop.passes.overdubPasses) {
            eventCount += LoopEventStore::countEventsInChunkIds(pass.committedChunkIds);
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
        if (track.hasLiveCaptureInterval() || track.hasStopInFlight()) {
            return true;
        }
    }
    return false;
}

/// A track actively **recording** parks the full deferred-save drain; audible **playback** does not.
///
/// `PROJECT_INTENT` §1 -- *"record MIDI the moment the idea happens; the music never stops"* --
/// supersedes the older "every track silent" gate this replaces (DEC-081). A playing track must never
/// be stopped, nor held up, so that persistence can run. That gate is what made CLEAR wait for every
/// other track to fall silent, and then abort when one kept playing.
///
/// The live-capture case still parks the multi-frame bundle writers; the bounded
/// one-chunk-per-turn flush in `DeferredSaveScheduler` is the deliberate exception that keeps a take
/// durable *while* it is recorded.
STORAGE_PERSIST_MEM bool isCaptureRecordingForPersistence() {
    for (uint8_t trackIndex = 0; trackIndex < trackManager.getTrackCount(); ++trackIndex) {
        if (trackManager.getTrack(trackIndex).isRecording()) {
            return true;
        }
    }
    return false;
}

STORAGE_PERSIST_MEM bool anyTrackArmedOrPendingRecordForPersistence() {
    for (uint8_t trackIndex = 0; trackIndex < trackManager.getTrackCount(); ++trackIndex) {
        const Track& track = trackManager.getTrack(trackIndex);
        if (track.isArmed() || trackManager.hasQueuedRecordingTrack(trackIndex)) {
            return true;
        }
    }
    return false;
}

/// True while any track is audibly playing.
///
/// Kept as its own predicate because the **slice budget** still needs it even though the *gate* no
/// longer does: persistence may now run while tracks play (DEC-081), but it must stay inside the same
/// bounded active budget, or the longer idle-sized bursts would reintroduce the display stalls the
/// active budget exists to cap.
STORAGE_PERSIST_MEM bool anyTrackAudibleForPersistence() {
    for (uint8_t trackIndex = 0; trackIndex < trackManager.getTrackCount(); ++trackIndex) {
        if (trackManager.getTrack(trackIndex).isAudiblePlaybackActive()) {
            return true;
        }
    }
    return false;
}

STORAGE_PERSIST_MEM uint32_t resolvePersistenceSliceBudgetUs(const LooperState& state) {
    // `transportBudgetActive` keeps the small active budget whenever anything is sounding or being
    // recorded; only a genuinely quiet device gets the larger idle budget.
    return PersistenceBudget::resolvePersistenceSliceBudgetUs(
        isCaptureActiveForPersistence(),
        isCaptureRecordingForPersistence() || anyTrackAudibleForPersistence());
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
