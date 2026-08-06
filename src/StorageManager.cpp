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
#define CAPTURE_HITL_DATA DMAMEM
#else
#define CAPTURE_HITL_MEM
#define CAPTURE_HITL_DATA
#endif
#endif


char restoredSetBundlePath_[80] = {};

std::array<uint32_t, Config::NUM_TRACKS> undoStackFileOffsets_{};
uint8_t undoHydrateTrackIndex_ = 0;
bool undoSnapshotsPending_ = false;

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

void clearAutoSaveBeforeLoadFolderPending() {
    autoSaveBeforeLoadFolderPending[0] = '\0';
    autoSaveBeforeLoadFolderPendingValid = false;
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


#if defined(SESSION_CAPTURE)
namespace StorageManagerInternal {

CAPTURE_HITL_DATA HitlRevisionCommitBackup hitlRevisionCommitBackup{};

}  // namespace StorageManagerInternal
#endif


namespace {

}  // namespace




bool parseRevisionSetFolderEntryName(const char* name, uint16_t& setIdOut);

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

uint32_t StorageManager::getCurrentSetLastActiveUnix() {
    return currentSetLastActiveUnix;
}

bool StorageManager::isCurrentWorkspaceDirty() {
    return CurrentWorkspaceStorage::isWorkspaceDirty(currentWorkspaceEpoch,
                                                     lastCommittedWorkspaceEpoch);
}

bool StorageManager::shouldQueueCurrentWorkspaceSave() {
#if BYPASS_STOP_UNDO_SAVE
    return false;
#else
    if (forceCurrentSetFullLoopWrite) {
        return true;
    }
    if (anyCurrentSetLoopSlotDirty()) {
        return true;
    }
    if (StorageManagerInternal::anyAllocatedLoopEditStateDirty()) {
        return true;
    }
    return CurrentSetStorage::shouldAutoSaveBeforeLoadIntoCurrent(currentSetAnchorFields);
#endif
}

uint32_t StorageManager::getCurrentWorkspaceEpoch() {
    return currentWorkspaceEpoch;
}

uint32_t StorageManager::getLastCommittedWorkspaceEpoch() {
    return lastCommittedWorkspaceEpoch;
}

uint16_t StorageManager::getCurrentWorkspaceDerivedSetId() {
    return workspaceDerivedFromSetId;
}

uint16_t StorageManager::getCurrentWorkspaceDerivedRevisionId() {
    return workspaceDerivedFromRevisionId;
}

bool StorageManager::copyCurrentSetLoadedFromFolder(char* out, size_t outSize) {
    if (out == nullptr || outSize == 0 || currentSetLoadedFromFolder[0] == '\0') {
        return false;
    }
    const int written = std::snprintf(out, outSize, "%s", currentSetLoadedFromFolder);
    return written > 0 && static_cast<size_t>(written) < outSize;
}

bool StorageManager::consumeAutoSaveBeforeLoadFolder(char* out, size_t outSize) {
    if (out == nullptr || outSize == 0 || !autoSaveBeforeLoadFolderPendingValid) {
        return false;
    }
    const int written =
        std::snprintf(out, outSize, "%s", autoSaveBeforeLoadFolderPending);
    const bool copied =
        written > 0 && static_cast<size_t>(written) < outSize;
    clearAutoSaveBeforeLoadFolderPending();
    return copied;
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

bool StorageManager::hasPendingUndoSnapshotHydrate() {
    return undoSnapshotsPending_;
}


void StorageManager::requestCommitRevision() {
#if BYPASS_STOP_UNDO_SAVE
    return;
#endif
    storageSession.revisionCommit.pending = true;
    SC_PERSIST("rev_request", 0, 0, 0, "queued");
}


#if defined(SESSION_CAPTURE)
CAPTURE_HITL_MEM bool removeEmptyDirectoryIfPresent(const char* path) {
    if (path == nullptr || path[0] == '\0' || !SD.exists(path)) {
        return true;
    }
    File dir = SD.open(path);
    if (!dir || !dir.isDirectory()) {
        if (dir) {
            dir.close();
        }
        return false;
    }
    while (true) {
        File entry = dir.openNextFile();
        if (!entry) {
            break;
        }
        entry.close();
        return false;
    }
    dir.close();
    return SD.rmdir(path);
}

CAPTURE_HITL_MEM bool parseRevisionSetFolderEntryName(const char* name, uint16_t& setIdOut) {
    if (name == nullptr || name[0] == '\0') {
        return false;
    }
    const char* base = name;
    if (const char* slash = std::strrchr(name, '/')) {
        base = slash + 1;
    }
    if (base[0] != 'S') {
        return false;
    }
    unsigned setId = 0;
    for (const char* cursor = base + 1; *cursor != '\0'; ++cursor) {
        if (*cursor < '0' || *cursor > '9') {
            return false;
        }
        setId = setId * 10U + static_cast<unsigned>(*cursor - '0');
    }
    if (setId == 0U || setId > 0xFFFFU) {
        return false;
    }
    setIdOut = static_cast<uint16_t>(setId);
    return true;
}

CAPTURE_HITL_MEM bool removeHitlSetFolderTree(const char* setFolderPath) {
    if (setFolderPath == nullptr || setFolderPath[0] == '\0') {
        return false;
    }
    char revisionsDir[56];
    const int revisionsWritten =
        std::snprintf(revisionsDir, sizeof(revisionsDir), "%s/revisions", setFolderPath);
    if (revisionsWritten <= 0 ||
        static_cast<size_t>(revisionsWritten) >= sizeof(revisionsDir)) {
        return false;
    }
    if (SD.exists(revisionsDir)) {
        File revisions = SD.open(revisionsDir);
        if (revisions && revisions.isDirectory()) {
            while (true) {
                File entry = revisions.openNextFile();
                if (!entry) {
                    break;
                }
                char entryPath[96];
                const char* name = entry.name();
                entry.close();
                const int entryWritten =
                    std::snprintf(entryPath, sizeof(entryPath), "%s/%s", revisionsDir, name);
                if (entryWritten <= 0 ||
                    static_cast<size_t>(entryWritten) >= sizeof(entryPath)) {
                    return false;
                }
                if (!SD.remove(entryPath)) {
                    return false;
                }
            }
            revisions.close();
        } else if (revisions) {
            revisions.close();
        }
        if (!removeEmptyDirectoryIfPresent(revisionsDir)) {
            return false;
        }
    }

    char setMetaPath[64];
    const int metaWritten =
        std::snprintf(setMetaPath, sizeof(setMetaPath), "%s/set.bin", setFolderPath);
    if (metaWritten > 0 && static_cast<size_t>(metaWritten) < sizeof(setMetaPath) &&
        SD.exists(setMetaPath)) {
        if (!SD.remove(setMetaPath)) {
            return false;
        }
    }
    char setMetaTempPath[68];
    const int metaTempWritten =
        std::snprintf(setMetaTempPath, sizeof(setMetaTempPath), "%s/set.bin.tmp", setFolderPath);
    if (metaTempWritten > 0 && static_cast<size_t>(metaTempWritten) < sizeof(setMetaTempPath) &&
        SD.exists(setMetaTempPath)) {
        (void)SD.remove(setMetaTempPath);
    }
    return removeEmptyDirectoryIfPresent(setFolderPath);
}

CAPTURE_HITL_MEM bool StorageManager::nukeHitlSetsCatalog() {
#if BYPASS_STOP_UNDO_SAVE
    return false;
#else
    if (storageSession.revisionCommit.pending || storageSession.revisionCommit.inProgress || storageSession.revisionLoad.pending ||
        storageSession.revisionLoad.inProgress) {
        SC_PERSIST("rev_nuke_sets", 0, 0, 0, "active_job");
        return false;
    }

    SC_PERSIST("rev_nuke_sets", 0, 0, 0, "started");
    hitlRevisionCommitBackup = HitlRevisionCommitBackup{};

    constexpr uint8_t kMaxSetFolders = 32;
    char setFolderPaths[kMaxSetFolders][52];
    uint8_t setFolderCount = 0;

    if (SD.exists(SetRevisionCatalog::kSetsRoot)) {
        File setsDir = SD.open(SetRevisionCatalog::kSetsRoot);
        if (setsDir) {
            while (true) {
                File entry = setsDir.openNextFile();
                if (!entry) {
                    break;
                }
                const bool isDirectory = entry.isDirectory();
                const char* name = entry.name();
                entry.close();
                if (!isDirectory || setFolderCount >= kMaxSetFolders) {
                    continue;
                }
                uint16_t setId = 0;
                if (!parseRevisionSetFolderEntryName(name, setId)) {
                    continue;
                }
                if (!SetRevisionCatalog::formatSetFolderPath(setFolderPaths[setFolderCount],
                                                              sizeof(setFolderPaths[0]), setId)) {
                    continue;
                }
                ++setFolderCount;
            }
            setsDir.close();
        }
    }

    bool ok = true;
    for (uint8_t i = 0; i < setFolderCount; ++i) {
        if (!removeHitlSetFolderTree(setFolderPaths[i])) {
            ok = false;
        }
    }

    if (SD.exists(SetRevisionCatalog::kSetIndexPath)) {
        ok = SD.remove(SetRevisionCatalog::kSetIndexPath) && ok;
    }
    if (SD.exists(SetRevisionCatalog::kSetIndexTempPath)) {
        (void)SD.remove(SetRevisionCatalog::kSetIndexTempPath);
    }

    workspaceDerivedFromSetId = 0;
    workspaceDerivedFromRevisionId = 0;
    workspaceLastCommittedRevisionId = 0;
    if (!writeWorkspaceMetaAfterDeferredSave()) {
        ok = false;
    }

    SC_PERSIST("rev_nuke_sets", 0, setFolderCount, 0, ok ? "ok" : "failed");
    Serial.print("[StorageManager] HITL sets catalog nuke removed ");
    Serial.print(setFolderCount);
    Serial.println(ok ? " folders" : " folders (partial failure)");
    return ok;
#endif
}

CAPTURE_HITL_MEM void StorageManager::requestCommitRevisionForHitl() {
#if BYPASS_STOP_UNDO_SAVE
    return;
#else
    hitlRevisionCommitBackup = HitlRevisionCommitBackup{};
    hitlRevisionCommitBackup.armed = true;
    hitlRevisionCommitBackup.workspaceCurrentEpoch = currentWorkspaceEpoch;
    hitlRevisionCommitBackup.workspaceLastCommittedEpoch = lastCommittedWorkspaceEpoch;
    hitlRevisionCommitBackup.workspaceDerivedFromSetId = workspaceDerivedFromSetId;
    hitlRevisionCommitBackup.workspaceDerivedFromRevisionId = workspaceDerivedFromRevisionId;
    hitlRevisionCommitBackup.workspaceLastCommittedRevisionId = workspaceLastCommittedRevisionId;

    if (SD.exists(SetRevisionCatalog::kSetIndexPath)) {
        File indexFile = SD.open(SetRevisionCatalog::kSetIndexPath, FILE_READ);
        if (indexFile) {
            const StorageIo indexIo = storageIoFromFileRead(indexFile);
            hitlRevisionCommitBackup.hadIndexOnSd =
                SetRevisionCatalog::readSetCatalogIndex(indexIo,
                                                        hitlRevisionCommitBackup.catalogIndex);
            indexFile.close();
        }
    }

    if (workspaceDerivedFromSetId != 0) {
        char setMetaPath[64];
        if (SetRevisionCatalog::formatSetMetaPath(setMetaPath, sizeof(setMetaPath),
                                                  workspaceDerivedFromSetId)) {
            File setMetaFile = SD.open(setMetaPath, FILE_READ);
            if (setMetaFile) {
                const StorageIo setMetaIo = storageIoFromFileRead(setMetaFile);
                hitlRevisionCommitBackup.hadSetMetaOnSd = SetRevisionCatalog::readSetMetaRecord(
                    setMetaIo, hitlRevisionCommitBackup.setMeta);
                setMetaFile.close();
            }
        }
    }

    requestCommitRevision();
    SC_PERSIST("rev_hitl_arm", 0, 0, 0, "armed");
#endif
}

CAPTURE_HITL_MEM bool StorageManager::cleanupHitlRevisionCommit() {
#if BYPASS_STOP_UNDO_SAVE
    return false;
#else
    if (!hitlRevisionCommitBackup.armed) {
        SC_PERSIST("rev_cleanup", 0, 0, 0, "not_armed");
        return false;
    }

    bool ok = true;
    if (hitlRevisionCommitBackup.revisionFinalPath[0] != '\0' &&
        SD.exists(hitlRevisionCommitBackup.revisionFinalPath)) {
        ok = SD.remove(hitlRevisionCommitBackup.revisionFinalPath) && ok;
    }
    char revisionTempPath[84];
    const int tempWritten = std::snprintf(revisionTempPath, sizeof(revisionTempPath), "%s.tmp",
                                          hitlRevisionCommitBackup.revisionFinalPath);
    if (tempWritten > 0 && static_cast<size_t>(tempWritten) < sizeof(revisionTempPath) &&
        SD.exists(revisionTempPath)) {
        (void)SD.remove(revisionTempPath);
    }

    if (hitlRevisionCommitBackup.createdNewSetFolder) {
        ok = removeHitlSetFolderTree(hitlRevisionCommitBackup.setFolderPath) && ok;
        if (hitlRevisionCommitBackup.hadIndexOnSd) {
            ok = writeSetCatalogIndexFile(hitlRevisionCommitBackup.catalogIndex) && ok;
        } else if (SD.exists(SetRevisionCatalog::kSetIndexPath)) {
            ok = SD.remove(SetRevisionCatalog::kSetIndexPath) && ok;
        }
    } else if (hitlRevisionCommitBackup.hadSetMetaOnSd) {
        ok = writeSetMetaRecordFile(hitlRevisionCommitBackup.committedSetId,
                                    hitlRevisionCommitBackup.setMeta) &&
             ok;
    }

    currentWorkspaceEpoch = hitlRevisionCommitBackup.workspaceCurrentEpoch;
    lastCommittedWorkspaceEpoch = hitlRevisionCommitBackup.workspaceLastCommittedEpoch;
    workspaceDerivedFromSetId = hitlRevisionCommitBackup.workspaceDerivedFromSetId;
    workspaceDerivedFromRevisionId = hitlRevisionCommitBackup.workspaceDerivedFromRevisionId;
    workspaceLastCommittedRevisionId = hitlRevisionCommitBackup.workspaceLastCommittedRevisionId;
    if (!writeWorkspaceMetaAfterDeferredSave()) {
        ok = false;
    }

    hitlRevisionCommitBackup = HitlRevisionCommitBackup{};
    SC_PERSIST("rev_cleanup", 0, 0, 0, ok ? "ok" : "failed");
    return ok;
#endif
}

namespace {
// Quarantine helpers (HITL serial dispatch lives in StorageManagerHitlSerial.cpp).

CAPTURE_HITL_MEM bool renamePathOnSdIfPresent(const char* src, const char* dest) {
    if (src == nullptr || dest == nullptr || src[0] == '\0' || dest[0] == '\0') {
        return false;
    }
    if (!SD.exists(src)) {
        return true;
    }
    if (SD.rename(src, dest)) {
        Serial.print("[StorageManager] Quarantined: ");
        Serial.print(src);
        Serial.print(" -> ");
        Serial.println(dest);
        return true;
    }
    Serial.print("[StorageManager] ERROR: quarantine rename failed: ");
    Serial.println(src);
    return false;
}

CAPTURE_HITL_MEM void quarantineCorruptRuntimeBundleOnSd() {
    char dest[96];
    const int written = std::snprintf(
        dest, sizeof(dest), "%s.bad.%lu", CurrentSetStorage::kCurrentRuntimeBundlePath,
        static_cast<unsigned long>(millis()));
    if (written <= 0 || static_cast<size_t>(written) >= sizeof(dest)) {
        return;
    }
    (void)renamePathOnSdIfPresent(CurrentSetStorage::kCurrentRuntimeBundlePath, dest);
}

CAPTURE_HITL_MEM bool quarantineCurrentWorkspaceOnSdImpl() {
    const unsigned long stamp = static_cast<unsigned long>(millis());
    char dest[72];
    bool ok = true;

    int written = std::snprintf(dest, sizeof(dest), "%s.bad.%lu", PersistenceLayout::kCurrentRoot,
                                stamp);
    if (written > 0 && static_cast<size_t>(written) < sizeof(dest)) {
        ok = renamePathOnSdIfPresent(PersistenceLayout::kCurrentRoot, dest) && ok;
    } else {
        ok = false;
    }

    written = std::snprintf(dest, sizeof(dest), "%s/checkpoints.bad.%lu",
                            PersistenceLayout::kRecoveryRoot, stamp);
    if (written > 0 && static_cast<size_t>(written) < sizeof(dest)) {
        ok = renamePathOnSdIfPresent(CurrentSetStorage::kCheckpointsDir, dest) && ok;
    } else {
        ok = false;
    }

    if (SD.exists(CurrentSetStorage::kLegacyMonolithPath)) {
        written = std::snprintf(dest, sizeof(dest), "/state.bad.%lu", stamp);
        if (written > 0 && static_cast<size_t>(written) < sizeof(dest)) {
            ok = renamePathOnSdIfPresent(CurrentSetStorage::kLegacyMonolithPath, dest) && ok;
        } else {
            ok = false;
        }
    }

    Serial.println(ok ? "[StorageManager] Workspace quarantine complete."
                      : "[StorageManager] Workspace quarantine incomplete.");
    return ok;
}

CAPTURE_HITL_MEM void pollBootQuarantineLineFromSerial(uint32_t listenMs) {
    Serial.println(
        "[StorageManager] Boot: send !QUARANTINE_WORKSPACE now to quarantine SD workspace "
        "(current + recovery checkpoints)");
    char line[48];
    size_t len = 0;
    bool sawByte = false;
    const uint32_t startMs = millis();
    const uint32_t deadlineMs = startMs + listenMs;
    while (millis() < deadlineMs) {
        if (Serial.available() == 0) {
            if (!sawByte && millis() - startMs >= 250) {
                return;
            }
            delay(1);
            continue;
        }
        sawByte = true;
        const char ch = static_cast<char>(Serial.read());
        if (ch == '\r') {
            continue;
        }
        if (ch == '\n') {
            line[len] = '\0';
            if (StorageManagerInternal::handleHitlQuarantineCommandLine(line)) {
                return;
            }
            len = 0;
            continue;
        }
        if (len + 1 < sizeof(line)) {
            line[len++] = ch;
        }
    }
}
}  // namespace

CAPTURE_HITL_MEM bool StorageManagerInternal::handleHitlQuarantineCommandLine(const char* line) {
    if (line == nullptr || std::strcmp(line, "!QUARANTINE_WORKSPACE") != 0) {
        return false;
    }
    (void)StorageManager::quarantineCurrentWorkspaceOnSd();
    return true;
}

CAPTURE_HITL_MEM bool StorageManager::quarantineCurrentWorkspaceOnSd() {
    return quarantineCurrentWorkspaceOnSdImpl();
}

CAPTURE_HITL_MEM void StorageManager::pollBootQuarantineWorkspaceBeforeLoad(uint32_t listenMs) {
    pollBootQuarantineLineFromSerial(listenMs);
}

#endif

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


bool readCurrentSetFilePreamble(File& file, LooperState& loadedLooperStateOut,
                                       uint32_t& masterLoopLengthOut, uint8_t& numTracksOut) {
    if (!CurrentWorkspaceStorage::fileStartsWithEpochHeader(file)) {
        if (!file.seek(0)) {
            return false;
        }
    } else {
        CurrentWorkspaceStorage::EpochFileHeader epochHeader{};
        const StorageIo epochIo = storageIoFromFileRead(file);
        if (!CurrentWorkspaceStorage::readEpochFileHeader(epochIo, epochHeader)) {
            Serial.println("[StorageManager] ERROR: Failed to read Current epoch header");
            return false;
        }
        currentWorkspaceEpoch = epochHeader.epoch;
        storageSession.currentWorkspaceSave.workspaceEpoch = epochHeader.epoch;
    }

    CurrentSetStorage::MetaHeader metaHeader{};
    const StorageIo metaIo = storageIoFromFileRead(file);
    if (!CurrentSetStorage::readMetaHeader(metaIo, metaHeader)) {
        Serial.println("[StorageManager] ERROR: Failed to read CurrentSet meta header");
        return false;
    }
    if (metaHeader.containerVersion != CurrentSetStorage::CONTAINER_VERSION) {
        Serial.print("[StorageManager] ERROR: Unsupported CurrentSet version ");
        Serial.println(metaHeader.containerVersion);
        return false;
    }
    currentSetLastActiveUnix = metaHeader.lastActiveUnix;
    currentSetAnchorFields = metaHeader.anchor;
    if (currentSetAnchorFields.loadedFromSequence != 0) {
        if (!StorageManagerInternal::resolveSavedSetFolderNameBySequence(
                currentSetAnchorFields.loadedFromSequence,
                                                 currentSetLoadedFromFolder,
                                                 sizeof(currentSetLoadedFromFolder))) {
            std::snprintf(currentSetLoadedFromFolder, sizeof(currentSetLoadedFromFolder),
                          "%05lu",
                          static_cast<unsigned long>(currentSetAnchorFields.loadedFromSequence));
        }
    } else {
        clearCurrentSetLoadedFromFolder();
    }

    float savedBpm = 0;
    if (!readRaw(file, &savedBpm, sizeof(savedBpm))) {
        return false;
    }
    if (savedBpm >= 20.0f && savedBpm <= 300.0f) {
        bpm = savedBpm;
    }

    uint32_t looperStateVal = 0;
    if (!readRaw(file, &looperStateVal, sizeof(looperStateVal))) {
        return false;
    }
    loadedLooperStateOut = sanitizeLooperStateForPersistence(static_cast<LooperState>(looperStateVal));

    uint32_t masterLoopLength = 0;
    if (!readRaw(file, &masterLoopLength, sizeof(masterLoopLength))) {
        return false;
    }
    masterLoopLengthOut = masterLoopLength;

    uint8_t numTracks = 0;
    if (!readRaw(file, &numTracks, sizeof(numTracks)) || numTracks != Config::NUM_TRACKS) {
        return false;
    }
    numTracksOut = numTracks;
    return true;
}

bool readCurrentSetFileEpilogue(File& file, uint8_t numTracks,
                                       std::vector<uint8_t>& activeLoopIndex,
                                       std::vector<uint8_t>& selectedSlotIndex,
                                       uint8_t& selectedTrackIdxOut,
                                       bool deferUndoSnapshotBodies) {
    if (!readRaw(file, &selectedTrackIdxOut, sizeof(selectedTrackIdxOut))) {
        return false;
    }
    activeLoopIndex.assign(numTracks, 0);
    selectedSlotIndex.assign(numTracks, 0);
    for (uint8_t t = 0; t < numTracks; ++t) {
        if (!readRaw(file, &activeLoopIndex[t], sizeof(activeLoopIndex[t]))) {
            return false;
        }
    }

    uint32_t footerToken = 0;
    if (!readRaw(file, &footerToken, sizeof(footerToken))) {
        return false;
    }
    if (footerToken == kFooterSelectedSlotExtensionToken) {
        for (uint8_t t = 0; t < numTracks; ++t) {
            if (!readRaw(file, &selectedSlotIndex[t], sizeof(selectedSlotIndex[t]))) {
                return false;
            }
        }
        if (!readRaw(file, &footerToken, sizeof(footerToken))) {
            return false;
        }
    } else {
        for (uint8_t t = 0; t < numTracks; ++t) {
            selectedSlotIndex[t] = activeLoopIndex[t];
        }
    }
    if (footerToken != kGlobalUndoStackToken) {
        Serial.print("[StorageManager] ERROR: CurrentSet runtime bundle footer token mismatch (got=0x");
        Serial.print(footerToken, HEX);
        Serial.print(" expected=0x");
        Serial.print(kGlobalUndoStackToken, HEX);
        Serial.print(" pos=");
        Serial.println(static_cast<unsigned long>(file.position()));
        return false;
    }
    undoSnapshotsPending_ = deferUndoSnapshotBodies;
    undoHydrateTrackIndex_ = 0;
    undoStackFileOffsets_.fill(0);
    for (uint8_t t = 0; t < numTracks; ++t) {
        undoStackFileOffsets_[t] = static_cast<uint32_t>(file.position());
        if (deferUndoSnapshotBodies) {
            if (!readGlobalUndoStackMetadataFromFile(file,
                                                     trackManager.getTrack(t).getGlobalUndoStack())) {
                return false;
            }
        } else if (!readGlobalUndoStackFromFile(file,
                                                trackManager.getTrack(t).getGlobalUndoStack())) {
            return false;
        }
    }

    uint32_t tailMarker = 0;
    if (!readRaw(file, &tailMarker, sizeof(tailMarker))) {
        return false;
    }
    if (tailMarker == SavedSetCatalog::kSavedSetMetaTrailerMagic) {
        if (!file.seek(file.position() - sizeof(uint32_t))) {
            return false;
        }
        SavedSetCatalog::SavedSetMetadata ignoredMetadata{};
        const StorageIo trailerIo = storageIoFromFileRead(file);
        if (!SavedSetCatalog::readSavedSetMetadataTrailer(trailerIo, ignoredMetadata)) {
            return false;
        }
        if (!readRaw(file, &tailMarker, sizeof(tailMarker))) {
            return false;
        }
    }
    if (tailMarker != CurrentSetStorage::kSaveFileToken) {
        Serial.println("[StorageManager] ERROR: CurrentSet meta completion marker mismatch");
        return false;
    }
    return true;
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


