//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "StorageManager.h"
#include "Utils/BootLoopSlotRestore.h"
#include "TrackManager.h"
#include "Loop.h"
#include "Slot.h"
#include "SlotLoadSession.h"
#include "StorageLoopIo.h"
#include "CurrentSetStorage.h"
#include "CurrentWorkspaceStorage.h"
#include "PersistenceLayout.h"
#include "PersistenceBudget.h"
#include "LoadLoopBudget.h"
#include "LoadLoopSelectionPolicy.h"
#include "DeferredJobScheduler.h"
#include "PersistenceFailurePolicy.h"
#include "PersistenceQueue.h"
#include "SetRevisionCatalog.h"
#include "RevisionPackedBlob.h"
#include "RevisionCommitPolicy.h"
#include "RevisionLoadPolicy.h"
#include "OverlayCatalogReadPolicy.h"
#include "SetBrowserOverlayPolicy.h"
#include "StorageActivitySnapshot.h"
#include "StorageSession.h"
#include "StorageManagerInternal.h"
#include "StorageManagerInternal/PersistenceWorkQueue.h"
#include "BootRecoveryPolicy.h"
#include "PersistenceSchema.h"
#include "SavedSetCatalog.h"
#include "RtcTime.h"
#include "Globals.h"
#include "Logger.h"
#include "Utils/BootTelemetry.h"
#include "Utils/MemoryMonitor.h"
#include "Utils/PersistenceDiagnostics.h"
#include "Utils/DebugSessionCapture.h"
#include <SD.h>
#include <Arduino.h>
#include <utility>
#include "TrackUndo.h"
#include "EditManager.h"
#include "TrackDisplayState.h"
#include "Utils/MemoryPool.h"
#include "Utils/HotPathTelemetry.h"
#include "Utils/ExternalMemoryFirstAllocator.h"
#include <array>
#include <cstdio>
#include <cstring>
#include <vector>

using namespace StorageManagerInternal;

namespace {
#if defined(SESSION_CAPTURE)
#if defined(__IMXRT1062__)
#define CAPTURE_HITL_MEM FLASHMEM
#else
#define CAPTURE_HITL_MEM
#endif
#endif

bool setCurrentSetLoadedFromFolder(const char* folderName) {
    if (folderName == nullptr || folderName[0] == '\0') {
        clearCurrentSetLoadedFromFolder();
        return false;
    }
    const int written = std::snprintf(currentSetLoadedFromFolder,
                                      sizeof(currentSetLoadedFromFolder), "%s", folderName);
    if (written <= 0 ||
        static_cast<size_t>(written) >= sizeof(currentSetLoadedFromFolder)) {
        clearCurrentSetLoadedFromFolder();
        return false;
    }
    return true;
}

LoopId loopIdForPersistSlot(uint8_t trackIndex, uint8_t slotIndex) {
    if (trackIndex >= Config::NUM_TRACKS || slotIndex >= Config::MAX_LOOPS_PER_TRACK) {
        return kInvalidLoopId;
    }
    if (trackIndex >= trackManager.getTrackCount()) {
        return static_cast<LoopId>(slotIndex);
    }
    Track& track = trackManager.getTrack(trackIndex);
    if (!track.loopsAllocated()) {
        return static_cast<LoopId>(slotIndex);
    }
    return track.loopIdForSlot(slotIndex);
}

}  // namespace

namespace StorageManagerInternal {

void setRestoredSetBundlePath(const char* path) {
    if (path == nullptr) {
        restoredSetBundlePath_[0] = '\0';
        return;
    }
    snprintf(restoredSetBundlePath_, sizeof(restoredSetBundlePath_), "%s", path);
}

void resetBootUndoHydrateState() {
    undoSnapshotsPending_ = false;
    undoHydrateTrackIndex_ = 0;
    undoStackFileOffsets_.fill(0);
}

#if defined(SESSION_CAPTURE)
CAPTURE_HITL_MEM void quarantineCorruptRuntimeBundleOnSd() {
    char dest[96];
    const int written = snprintf(dest, sizeof(dest), "%s.bad.%lu",
                                 CurrentSetStorage::kCurrentRuntimeBundlePath,
                                 static_cast<unsigned long>(millis()));
    if (written <= 0 || static_cast<size_t>(written) >= sizeof(dest)) {
        return;
    }
    if (!SD.exists(CurrentSetStorage::kCurrentRuntimeBundlePath)) {
        return;
    }
    if (SD.rename(CurrentSetStorage::kCurrentRuntimeBundlePath, dest)) {
        Serial.print("[StorageManager] Quarantined: ");
        Serial.print(CurrentSetStorage::kCurrentRuntimeBundlePath);
        Serial.print(" -> ");
        Serial.println(dest);
    }
}
#endif

}  // namespace StorageManagerInternal

bool StorageManager::saveNewSet(char* savedSetFolderOut, size_t outSize) {
    return StorageManagerInternal::saveNewSetInternal(looperState.getLooperState(), savedSetFolderOut,
                                                      outSize);
}

bool StorageManager::loadSetIntoCurrent(const char* savedSetFolderName) {
    uint32_t sourceSequence = 0;
    if (!SavedSetCatalog::parseSavedSetFolderName(savedSetFolderName, sourceSequence, nullptr)) {
        return false;
    }
    char sourceSetDir[StorageManagerInternal::kSavedSetPathCapacity];
    if (!StorageManagerInternal::formatSavedSetDirectoryPath(savedSetFolderName, sourceSetDir,
                                                             sizeof(sourceSetDir)) ||
        !SD.exists(sourceSetDir)) {
        return false;
    }

    char autoSavedFolderName[16] = {};
    const bool shouldAutoSave =
        CurrentSetStorage::shouldAutoSaveBeforeLoadIntoCurrent(currentSetAnchorFields);
    if (shouldAutoSave &&
        !StorageManagerInternal::saveNewSetInternal(looperState.getLooperState(), autoSavedFolderName,
                                                    sizeof(autoSavedFolderName))) {
        return false;
    }

    if (!StorageManagerInternal::copySavedSetIntoCurrent(sourceSetDir) ||
        !loadCurrentSetFromDirectory(CurrentSetStorage::kCurrentSetDir,
                                     looperState.getLooperState())) {
        return false;
    }

    forceCurrentSetFullLoopWrite = false;
    syncCurrentSetDirtyTrackingFromLoadedState();
    setCurrentSetLoadedFromFolder(savedSetFolderName);
    CurrentSetStorage::applyLoadedSetAnchorFields(sourceSequence, currentSetAnchorFields);
    if (!StorageManagerInternal::patchCurrentSetAnchor()) {
        return false;
    }
    if (shouldAutoSave && autoSavedFolderName[0] != '\0') {
        const int written = std::snprintf(autoSaveBeforeLoadFolderPending,
                                          sizeof(autoSaveBeforeLoadFolderPending), "%s",
                                          autoSavedFolderName);
        autoSaveBeforeLoadFolderPendingValid =
            written > 0 &&
            static_cast<size_t>(written) < sizeof(autoSaveBeforeLoadFolderPending);
    } else {
        clearAutoSaveBeforeLoadFolderPending();
    }
    return true;
}

namespace StorageManagerInternal {

STORAGE_PERSIST_MEM void maybeAdmitDeferredWorkspaceFooter() {
#if BYPASS_STOP_UNDO_SAVE
    return;
#else
    if (!StorageManagerInternal::workspaceFooterPersistDeferred) {
        return;
    }
    if (isCaptureActiveForPersistence() || isTransportActiveForPersistence() ||
        anyTrackArmedOrPendingRecordForPersistence()) {
        return;
    }
    StorageManagerInternal::workspaceFooterPersistDeferred = false;
    StorageManager::admitWorkspaceFooter();
#endif
}

}  // namespace StorageManagerInternal

bool StorageManager::hasPendingLoopSlotRestore() {
    return StorageManagerInternal::pendingLoopSlotRestoreCount() > 0 || SlotLoadSession::isActive() ||
           StorageManagerInternal::anyLoadLoopJobActive();
}

bool STORAGE_PERSIST_MEM StorageManager::isFocusedLoopSlotRestoreWork() {
    if (trackManager.getTrackCount() == 0) {
        return false;
    }
    const uint8_t focusTrack = trackManager.getSelectedTrackIndex();
    const uint8_t focusSlot = trackManager.getSelectedSlotIndex(focusTrack);
    if (StorageManagerInternal::isActiveLoadLoopJobFor(focusTrack, focusSlot)) {
        return true;
    }
    if (StorageManagerInternal::isParkedLoadLoopJobFor(focusTrack, focusSlot)) {
        return true;
    }
    return StorageManagerInternal::isFocusDeferredLoopSlotRestorePending(focusTrack, focusSlot);
}

void STORAGE_PERSIST_MEM StorageManager::setBootTitleLoadDrain(bool enabled) {
    StorageManagerInternal::setBootTitleLoadDrain(enabled);
}

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

bool StorageManager::bootInteractiveReady() {
    // Playback-only boot: title + USB wait until the boot playback restore queue is empty
    // and any in-flight LoadLoopJob has committed.
    // Non-playback SD slots are not enqueued at boot (HEADER_READY metadata only).
    // Do not re-derive readiness from getActiveLoopIndex() after the footer —
    // loadTransportSlotIndices remaps active→selected while stopped.
    return StorageManagerInternal::pendingLoopSlotRestoreCount() == 0 && !SlotLoadSession::isActive() &&
           !StorageManagerInternal::anyLoadLoopJobActive();
}

void StorageManager::requestCommitRevision() {
#if BYPASS_STOP_UNDO_SAVE
    return;
#endif
    storageSession.revisionCommit.pending = true;
    SC_PERSIST("rev_request", 0, 0, 0, "queued");
}

bool StorageManager::saveState(const LooperState& state) {
    return StorageManagerInternal::drainPersistenceWorkBlocking(state);
}

namespace StorageManagerInternal {

void clearCurrentSetLoadedFromFolder() {
    currentSetLoadedFromFolder[0] = '\0';
}

void syncCurrentSetDirtyTrackingFromLoadedState() {
    for (uint8_t t = 0; t < Config::NUM_TRACKS; ++t) {
        for (uint8_t s = 0; s < Config::MAX_LOOPS_PER_TRACK; ++s) {
            currentSetLoopSlotDirty[t][s] = false;
        }
    }
}


void resetLoopSlotToEmpty(Loop& loop, uint8_t slotIndex) {
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


static STORAGE_PERSIST_MEM void markCommittedChunkIdsPersistedFromSdLoad(
    const CommittedChunkIdList& chunkIds) {
    for (uint16_t chunkId : chunkIds) {
        (void)PersistenceQueue::markChunkPersistedFromSdLoad(chunkId);
    }
}

STORAGE_PERSIST_MEM void markLoopCommittedChunksPersistedFromSdLoad(Loop& loop) {
    if (loop.passes.hasRecordPass()) {
        markCommittedChunkIdsPersistedFromSdLoad(loop.passes.recordPass.committedChunkIds);
    }
    for (const OverdubPass& pass : loop.passes.overdubPasses) {
        markCommittedChunkIdsPersistedFromSdLoad(pass.committedChunkIds);
    }
}

}  // namespace StorageManagerInternal

void STORAGE_PERSIST_MEM StorageManager::processDeferredUndoSnapshots() {
    if (!undoSnapshotsPending_ || undoHydrateTrackIndex_ >= Config::NUM_TRACKS) {
        return;
    }
    if (restoredSetBundlePath_[0] == '\0' || !SD.exists(restoredSetBundlePath_)) {
        undoSnapshotsPending_ = false;
        return;
    }
    File file = SD.open(restoredSetBundlePath_, FILE_READ);
    if (!file) {
        return;
    }
    const uint8_t trackIndex = undoHydrateTrackIndex_;
    if (!file.seek(undoStackFileOffsets_[trackIndex])) {
        file.close();
        undoHydrateTrackIndex_++;
        return;
    }
    if (!readGlobalUndoStackFromFile(file, trackManager.getTrack(trackIndex).getGlobalUndoStack())) {
        trackManager.getTrack(trackIndex).getGlobalUndoStack().clear();
    }
    file.close();
    undoHydrateTrackIndex_++;
    if (undoHydrateTrackIndex_ >= Config::NUM_TRACKS) {
        undoSnapshotsPending_ = false;
    }
}

void STORAGE_PERSIST_MEM StorageManager::restoreDeferredUndoSnapshotsBeforeUse() {
    while (undoSnapshotsPending_ && undoHydrateTrackIndex_ < Config::NUM_TRACKS) {
        processDeferredUndoSnapshots();
    }
}

void STORAGE_PERSIST_MEM StorageManager::requestLoopSlotRestoreFromSd(uint8_t trackIndex, uint8_t slotIndex) {
    // Phase 4: explicit requests enqueue only — never sync-load on the caller path.
    prioritizeLoopSlotRestoreForFocus(trackIndex, slotIndex);
}

void STORAGE_PERSIST_MEM StorageManager::reprioritizeDeferredLoopSlotRestore() {
    StorageManagerInternal::reprioritizeDeferredLoopSlotRestoreEntries();
}

void STORAGE_PERSIST_MEM StorageManager::prioritizeLoopSlotRestoreForFocus(uint8_t trackIndex,
                                                                           uint8_t slotIndex) {
    if (trackIndex >= Config::NUM_TRACKS || slotIndex >= Config::MAX_LOOPS_PER_TRACK) {
        return;
    }
    // Focus path: queue only the focused slot (+ immediate neighbors). Never dump the
    // full SD set here — that refilled the boot queue under the title (session_20260718_173340).
    // Full background fill is only via enqueueRemainingLoopSlotRestoresFromSd() after
    // bootInteractiveReady().
    auto queueIfNeeded = [](uint8_t t, uint8_t s) {
        if (!trackManager.getTrack(t).getLoop(s).hasCommittedPasses()) {
            StorageManagerInternal::queueDeferredLoopSlotRestore(t, s);
        }
    };
    queueIfNeeded(trackIndex, slotIndex);
    if (Config::MAX_LOOPS_PER_TRACK > 1) {
        const uint8_t left =
            static_cast<uint8_t>((slotIndex + Config::MAX_LOOPS_PER_TRACK - 1) %
                                 Config::MAX_LOOPS_PER_TRACK);
        const uint8_t right =
            static_cast<uint8_t>((slotIndex + 1) % Config::MAX_LOOPS_PER_TRACK);
        queueIfNeeded(trackIndex, left);
        queueIfNeeded(trackIndex, right);
    }
    StorageManagerInternal::reprioritizeDeferredLoopSlotRestoreEntries();
    if (!StorageManagerInternal::getBootTitleLoadDrain()) {
        StorageManagerInternal::demoteActiveLoadLoopJobForFocus(trackIndex, slotIndex);
        StorageManagerInternal::resumeParkedLoadLoopJobIfFocus(trackIndex, slotIndex);
    }
}

void STORAGE_PERSIST_MEM StorageManager::enqueueRemainingLoopSlotRestoresFromSd() {
    StorageManagerInternal::enqueueRemainingLoopSlotRestores();
}

bool StorageManager::loadCurrentWorkspaceFromSd(LooperState& state) {
    return loadCurrentSetFromDirectory(CurrentSetStorage::kCurrentSetDir, state);
}

bool StorageManager::loadCurrentSetFromSd(LooperState& state) {
    return loadCurrentWorkspaceAtBoot(state);
}


