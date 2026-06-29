//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "StorageManagerInternal.h"

#include "CurrentWorkspaceStorage.h"
#include "Globals.h"
#include "Loop.h"
#include "LoopEventStore.h"
#include "PersistenceBudget.h"
#include "RtcTime.h"
#include "TrackManager.h"
#include "Utils/DebugSessionCapture.h"
#include <Arduino.h>
#include <cstdio>

namespace StorageManagerInternal {

bool deferredSavePending = false;
bool urgentEditSavePending = false;
uint32_t lastEditAutosaveMs = 0;
bool clearEditDirtyAfterDeferredSave = false;
bool quarantineLegacyMonolithAfterSave = false;
bool deferredSaveInProgress = false;
bool deferredSaveSdIoActive = false;
bool deferredSaveUrgentRequested = false;
DeferredSaveStage deferredSaveStage = DeferredSaveStage::Idle;
DeferredGlobalHeaderStage deferredGlobalHeaderStage = DeferredGlobalHeaderStage::Version;
DeferredTrackWriteStage deferredTrackWriteStage = DeferredTrackWriteStage::TrackState;
DeferredSlotWriteStage deferredSlotWriteStage = DeferredSlotWriteStage::SlotEnabled;
DeferredFooterWriteStage deferredFooterWriteStage = DeferredFooterWriteStage::SelectedTrack;
DeferredLoopWriteStage deferredLoopWriteStage = DeferredLoopWriteStage::Header;
DeferredUndoWriteStage deferredUndoWriteStage = DeferredUndoWriteStage::Header;
LooperState deferredSaveStateSnapshot = LOOPER_IDLE;
File deferredSaveFile;
File deferredSaveLoopFile;
bool deferredSaveLoopFileOpen = false;
CurrentSetStorage::AnchorFields currentSetAnchorFields{};
uint8_t deferredSaveNumTracks = 0;
uint8_t deferredSaveTrackCursor = 0;
uint8_t deferredSaveSlotCursor = 0;
uint8_t deferredSavePoolCursor = 0;
uint8_t deferredSaveUndoTrackCursor = 0;
uint16_t deferredSaveCapturePassCursor = 0;
uint16_t deferredSaveChunkCursor = 0;
uint32_t deferredSaveUndoEntryCursor = 0;
bool deferredSaveTrackHeaderWritten = false;
uint8_t deferredSaveFooterTrackCursor = 0;
uint32_t deferredSaveStartedAtUs = 0;
uint32_t deferredSaveHeapBefore = 0;
uint32_t deferredSaveAdmissionHeap = 0;
bool deferredSaveHeapFloorDeferred = false;
bool deferredSaveLastCompletedOk = false;
uint32_t deferredSaveCompletedAtMs = 0;
uint32_t deferredSaveFailedAtMs = 0;
uint32_t revisionLoadCompletedAtMs = 0;
uint32_t revisionLoadFailedAtMs = 0;
uint16_t revisionLoadLastDisplaySetId = 0;
uint16_t revisionLoadLastDisplayRevisionId = 0;
std::vector<MidiEvent, ExternalMemoryFirstAllocator<MidiEvent>> deferredSaveMidiBatch;
uint16_t deferredSaveLoopSlotsWritten = 0;
uint16_t deferredSaveLoopSlotsSkipped = 0;
uint32_t deferredSaveDisplayBlockUs = 0;
bool forceCurrentSetFullLoopWrite = true;
std::array<std::array<bool, Config::MAX_LOOPS_PER_TRACK>, Config::NUM_TRACKS>
    currentSetLoopSlotDirty{};
uint32_t currentSetLastActiveUnix = 0;
char currentSetLoadedFromFolder[16] = {};
uint32_t currentWorkspaceEpoch = 0;
uint32_t lastCommittedWorkspaceEpoch = 0;
uint16_t workspaceDerivedFromSetId = 0;
uint16_t workspaceDerivedFromRevisionId = 0;
uint16_t workspaceLastCommittedRevisionId = 0;
uint32_t deferredSaveWorkspaceEpoch = 0;
char autoSaveBeforeLoadFolderPending[16] = {};
bool autoSaveBeforeLoadFolderPendingValid = false;
bool revisionCommitPending = false;
bool revisionCommitInProgress = false;
bool revisionCommitSdIoActive = false;
RevisionCommitStage revisionCommitStage = RevisionCommitStage::Idle;
RevisionWriteStage revisionWriteStage = RevisionWriteStage::PrepareLayout;
uint32_t revisionCommitSourceEpoch = 0;
uint32_t revisionCommitWorkspaceEpochBeforeSnapshot = 0;
uint16_t revisionCommitSetId = 0;
uint16_t revisionCommitPendingRevisionId = 0;
char revisionCommitTempPath[80] = {};
char revisionCommitFinalPath[80] = {};
File revisionCommitFile;
SetRevisionCatalog::SetCatalogIndex revisionCommitCatalogIndex{};
SetRevisionCatalog::SetMetaRecord revisionCommitSetMeta{};
RevisionPackedBlob::RevisionHeader revisionCommitHeader{};
STORAGE_PERSIST_DATA RevisionPackedBlob::RevisionLoopSlotDirectoryEntry
    revisionCommitSlotEntries[kMaxRevisionLoopIndexEntries];
STORAGE_PERSIST_DATA RevisionPackedBlob::RevisionLoopSlotDirectoryEntry
    revisionLoadSlotDirectoryEntries[kMaxRevisionLoopIndexEntries];
uint16_t revisionCommitSlotIndexCount = 0;
uint16_t revisionCommitSlotIndexWriteCursor = 0;
uint32_t revisionCommitPayloadWriteOffset = 0;
uint16_t revisionCommitChunkCount = 0;
uint8_t revisionCommitCopyTrackCursor = 0;
uint8_t revisionCommitCopySlotCursor = 0;
uint32_t revisionCommitRuntimeBundleSize = 0;
uint32_t revisionCommitRuntimeBundleReadPos = 0;
uint32_t revisionCommitSlotReadPos = 0;
uint32_t revisionCommitSlotBodyRemaining = 0;
bool revisionCommitLoopSlotBodyActive = false;
File revisionCommitSourceFile;
bool revisionCommitSourceFileOpen = false;
STORAGE_PERSIST_DATA std::array<uint8_t, 512> revisionCommitCopyBuffer{};
uint32_t revisionCommitPayloadCrc = 0;
bool revisionCommitPayloadCrcSeeded = false;
bool revisionCommitAllocatedNewSet = false;
bool revisionCommitSlotIndexChunkWritten = false;
uint32_t lastRevisionCommitBlockedLogAtMs = 0;
RevisionLoadStage revisionLoadStage = RevisionLoadStage::Idle;
RevisionLoadWriteStage revisionLoadWriteStage = RevisionLoadWriteStage::PrepareEpoch;
uint16_t revisionLoadSetId = 0;
uint16_t revisionLoadRevisionId = 0;
char revisionLoadSourcePath[80] = {};
RevisionPackedBlob::RevisionHeader revisionLoadHeader{};
uint16_t revisionLoadSlotIndexCount = 0;
uint32_t revisionLoadWorkspaceEpoch = 0;
uint32_t revisionLoadTransportFileOffset = 0;
uint32_t revisionLoadTransportBodySize = 0;
uint32_t revisionLoadTransportReadPos = 0;
uint8_t revisionLoadCopyTrackCursor = 0;
uint8_t revisionLoadCopySlotCursor = 0;
uint32_t revisionLoadSlotBodyRemaining = 0;
uint32_t revisionLoadSlotReadPos = 0;
File revisionLoadSourceFile;
bool revisionLoadSourceFileOpen = false;
File revisionLoadDestFile;
bool revisionLoadDestFileOpen = false;
bool revisionLoadWritingEmptySlot = false;
DeferredLoopWriteStage revisionLoadLoopWriteStage = DeferredLoopWriteStage::Header;
uint32_t lastRevisionLoadBlockedLogAtMs = 0;
bool revisionLoadUsedDefaultTransport = false;
bool revisionLoadDisplayRefreshPending = false;
bool bootRevisionRecoveryPending = false;
uint16_t bootRevisionRecoverySetId = 0;
uint16_t bootRevisionRecoveryRevisionId = 0;
StorageSession storageSession{};
RevisionLoadReloadRamStage revisionLoadReloadRamStage = RevisionLoadReloadRamStage::WriteWorkspaceMeta;
File revisionLoadReloadMetaFile;
bool revisionLoadReloadMetaFileOpen = false;
uint8_t revisionLoadReloadTrackCursor = 0;
uint8_t revisionLoadReloadSlotCursor = 0;
uint8_t revisionLoadReloadNumTracks = 0;
bool revisionLoadReloadAnySlotHasEvents[Config::NUM_TRACKS] = {};
TrackState revisionLoadReloadLoadedTrackState[Config::NUM_TRACKS] = {};
bool revisionLoadReloadMuted[Config::NUM_TRACKS] = {};
std::vector<uint8_t> revisionLoadReloadActiveLoopIndex;
uint8_t revisionLoadReloadSelectedTrackIdx = 0;
LooperState revisionLoadReloadLooperState = LOOPER_IDLE;
uint32_t revisionLoadReloadMasterLoopLength = 0;

STORAGE_PERSIST_MEM void emitDeferredSaveSliceTelemetry(const char* phase) {
    char outcome[96];
    if (deferredSaveStage == DeferredSaveStage::CurrentSetLoopSlot) {
        snprintf(outcome, sizeof(outcome), "%s:%s:t%u:p%u:c%u:k%u", phase,
                 deferredLoopWriteStageName(deferredLoopWriteStage),
                 static_cast<unsigned>(deferredSaveTrackCursor),
                 static_cast<unsigned>(deferredSavePoolCursor),
                 static_cast<unsigned>(deferredSaveCapturePassCursor),
                 static_cast<unsigned>(deferredSaveChunkCursor));
    } else if (deferredSaveStage == DeferredSaveStage::UndoStacks) {
        snprintf(outcome, sizeof(outcome), "%s:%s:t%u:e%lu:%s", phase,
                 deferredSaveStageName(deferredSaveStage),
                 static_cast<unsigned>(deferredSaveUndoTrackCursor),
                 static_cast<unsigned long>(deferredSaveUndoEntryCursor),
                 deferredUndoWriteStageName(deferredUndoWriteStage));
    } else if (deferredSaveStage == DeferredSaveStage::CurrentSetMeta) {
        snprintf(outcome, sizeof(outcome), "%s:%s:%s", phase,
                 deferredSaveStageName(deferredSaveStage),
                 deferredGlobalHeaderStageName(deferredGlobalHeaderStage));
    } else if (deferredSaveStage == DeferredSaveStage::TrackHeaderAndSlots) {
        snprintf(outcome, sizeof(outcome), "%s:%s:t%u:s%u:%s:%s", phase,
                 deferredSaveStageName(deferredSaveStage),
                 static_cast<unsigned>(deferredSaveTrackCursor),
                 static_cast<unsigned>(deferredSaveSlotCursor),
                 deferredSaveTrackHeaderWritten ? "slot" : "track",
                 deferredSaveTrackHeaderWritten
                     ? deferredSlotWriteStageName(deferredSlotWriteStage)
                     : deferredTrackWriteStageName(deferredTrackWriteStage));
    } else if (deferredSaveStage == DeferredSaveStage::Footer) {
        snprintf(outcome, sizeof(outcome), "%s:%s:t%u:%s", phase,
                 deferredSaveStageName(deferredSaveStage),
                 static_cast<unsigned>(deferredSaveFooterTrackCursor),
                 deferredFooterWriteStageName(deferredFooterWriteStage));
    } else {
        snprintf(outcome, sizeof(outcome), "%s:%s:t%u:s%u:p%u", phase,
                 deferredSaveStageName(deferredSaveStage),
                 static_cast<unsigned>(deferredSaveTrackCursor),
                 static_cast<unsigned>(deferredSaveSlotCursor),
                 static_cast<unsigned>(deferredSavePoolCursor));
    }
    SC_PERSIST("slice", micros() - deferredSaveStartedAtUs, deferredSaveHeapBefore,
               deferredSaveHeapBefore, outcome);
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

STORAGE_PERSIST_MEM uint32_t resolvePersistenceSliceBudgetUs(const LooperState& state) {
    return PersistenceBudget::resolvePersistenceSliceBudgetUs(
        isCaptureActiveForPersistence(),
        state == LOOPER_PLAYING || state == LOOPER_OVERDUBBING || state == LOOPER_RECORDING);
}

}  // namespace StorageManagerInternal
