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

#define STORAGE_FILENAME CurrentSetStorage::kLegacyMonolithPath
#define STORAGE_VERSION 6

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


bool workspaceFooterPersistDeferred = false;
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

void markCurrentSetMaterialChange() {
    currentSetAnchorFields.hasMaterialChangesSinceAnchor = 1;
    const uint32_t nowUnix = RtcTime::getUnixTime();
    if (nowUnix != 0) {
        currentSetAnchorFields.lastMaterialChangeUnix = nowUnix;
    }
}

void markCurrentSetLoopSlotDirtyInternal(uint8_t trackIndex, uint8_t slotIndex,
                                         bool markMaterialChange = true) {
    if (trackIndex >= Config::NUM_TRACKS || slotIndex >= Config::MAX_LOOPS_PER_TRACK) {
        return;
    }
    currentSetLoopSlotDirty[trackIndex][slotIndex] = true;
    if (markMaterialChange) {
        markCurrentSetMaterialChange();
    }
}

void markCurrentSetTrackDirtyInternal(uint8_t trackIndex,
                                      bool markMaterialChange = true) {
    if (trackIndex >= Config::NUM_TRACKS) {
        return;
    }
    for (uint8_t slot = 0; slot < Config::MAX_LOOPS_PER_TRACK; ++slot) {
        currentSetLoopSlotDirty[trackIndex][slot] = true;
    }
    if (markMaterialChange) {
        markCurrentSetMaterialChange();
    }
}

void markAllCurrentSetLoopSlotsDirtyInternal(bool markMaterialChange = true) {
    for (uint8_t track = 0; track < Config::NUM_TRACKS; ++track) {
        markCurrentSetTrackDirtyInternal(track, false);
    }
    if (markMaterialChange) {
        markCurrentSetMaterialChange();
    }
}

void clearCurrentSetLoopSlotDirtyInternal(uint8_t trackIndex, uint8_t slotIndex) {
    if (trackIndex >= Config::NUM_TRACKS || slotIndex >= Config::MAX_LOOPS_PER_TRACK) {
        return;
    }
    currentSetLoopSlotDirty[trackIndex][slotIndex] = false;
}

bool anyAllocatedLoopEditStateDirty() {
    for (uint8_t t = 0; t < trackManager.getTrackCount(); ++t) {
        Track& track = trackManager.getTrack(t);
        if (!track.loopsAllocated()) {
            continue;
        }
        for (uint8_t s = 0; s < Config::MAX_LOOPS_PER_TRACK; ++s) {
            if (track.getLoop(s).isEditStateDirty()) {
                return true;
            }
        }
    }
    return false;
}

void clearAllocatedLoopEditStateDirty() {
    for (uint8_t t = 0; t < trackManager.getTrackCount(); ++t) {
        Track& track = trackManager.getTrack(t);
        if (!track.loopsAllocated()) {
            continue;
        }
        for (uint8_t s = 0; s < Config::MAX_LOOPS_PER_TRACK; ++s) {
            track.getLoop(s).clearEditStateDirty();
        }
    }
}

static void quarantineStorageFile() {
#if defined(ARDUINO)
    if (!SD.exists(STORAGE_FILENAME)) {
        return;
    }
    char quarantineName[48];
    snprintf(quarantineName, sizeof(quarantineName), "/state.bad.%lu",
             static_cast<unsigned long>(millis()));
    if (SD.rename(STORAGE_FILENAME, quarantineName)) {
        Serial.print("[StorageManager] Quarantined storage file as ");
        Serial.println(quarantineName);
    }
#endif
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

void StorageManager::requestUrgentEditSave() {
#if BYPASS_STOP_UNDO_SAVE
    return;
#endif
    urgentEditSavePending = true;
}

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
    if (anyAllocatedLoopEditStateDirty()) {
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

bool readSavedSetMetadataFromMetaPath(const char* metaPath,
                                      SavedSetCatalog::SavedSetMetadata& metadata) {
    File file = SD.open(metaPath, FILE_READ);
    if (!file) {
        return false;
    }
    const size_t fileSize = file.size();
    const size_t trailerTailSize =
        SavedSetCatalog::kSavedSetMetadataTrailerByteSize + sizeof(CurrentSetStorage::kSaveFileToken);
    if (fileSize < trailerTailSize) {
        file.close();
        return false;
    }
    if (!file.seek(fileSize - trailerTailSize)) {
        file.close();
        return false;
    }
    const StorageIo trailerIo = storageIoFromFileRead(file);
    const bool ok = SavedSetCatalog::readSavedSetMetadataTrailer(trailerIo, metadata);
    file.close();
    return ok;
}

size_t StorageManager::listSavedSetFolderEntries(SavedSetCatalog::SavedSetFolderListEntry* entries,
                                                 size_t maxEntries) {
    if (entries == nullptr || maxEntries == 0 ||
        !SD.exists(CurrentSetStorage::kSetsArchiveDir)) {
        return 0;
    }

    size_t count = 0;
    File dir = SD.open(CurrentSetStorage::kSetsArchiveDir);
    if (!dir) {
        return 0;
    }
    while (true) {
        File entry = dir.openNextFile();
        if (!entry) {
            break;
        }
        const bool isDirectory = entry.isDirectory();
        const char* name = entry.name();
        entry.close();
        uint32_t sequence = 0;
        if (!isDirectory || !StorageManagerInternal::parseSavedSetSequence(name, sequence) ||
            sequence == 0) {
            continue;
        }
        const char* baseName = std::strrchr(name, '/');
        if (baseName != nullptr) {
            baseName += 1;
        } else {
            baseName = name;
        }
        if (count < maxEntries) {
            const int written = std::snprintf(entries[count].folderName,
                                              sizeof(entries[count].folderName), "%s", baseName);
            if (written <= 0 ||
                static_cast<size_t>(written) >= sizeof(entries[count].folderName)) {
                continue;
            }
            entries[count].sequence = sequence;
        }
        ++count;
    }
    dir.close();

    const size_t sortCount = count < maxEntries ? count : maxEntries;
    for (size_t i = 0; i + 1 < sortCount; ++i) {
        for (size_t j = i + 1; j < sortCount; ++j) {
            if (entries[j].sequence > entries[i].sequence) {
                const SavedSetCatalog::SavedSetFolderListEntry tmp = entries[i];
                entries[i] = entries[j];
                entries[j] = tmp;
            }
        }
    }
    return count < maxEntries ? count : maxEntries;
}

size_t StorageManager::listSetRevisionBrowserEntries(SetRevisionCatalog::SetBrowserListEntry* entries,
                                                     size_t maxEntries) {
#if BYPASS_STOP_UNDO_SAVE
    (void)entries;
    (void)maxEntries;
    return 0;
#else
    if (entries == nullptr || maxEntries == 0 || !SD.exists(SetRevisionCatalog::kSetsRoot)) {
        return 0;
    }

    SetRevisionCatalog::SetBrowserListEntry scratch[16];
    constexpr size_t kScratchCapacity = 16;
    const size_t capacity = maxEntries < kScratchCapacity ? maxEntries : kScratchCapacity;
    size_t count = 0;

    if (SD.exists(SetRevisionCatalog::kSetIndexPath)) {
        File indexFile = SD.open(SetRevisionCatalog::kSetIndexPath, FILE_READ);
        if (indexFile) {
            SetRevisionCatalog::SetCatalogIndex catalogIndex{};
            const StorageIo indexIo = storageIoFromFileRead(indexFile);
            const bool indexOk = SetRevisionCatalog::readSetCatalogIndex(indexIo, catalogIndex);
            indexFile.close();
            if (indexOk && catalogIndex.nextSetId > 1U) {
                for (uint32_t candidateSetId = 1;
                     candidateSetId < catalogIndex.nextSetId && count < capacity; ++candidateSetId) {
                    const uint16_t setId = static_cast<uint16_t>(candidateSetId);
                    char setMetaPath[64];
                    if (!SetRevisionCatalog::formatSetMetaPath(setMetaPath, sizeof(setMetaPath),
                                                               setId) ||
                        !SD.exists(setMetaPath)) {
                        continue;
                    }
                    File metaFile = SD.open(setMetaPath, FILE_READ);
                    if (!metaFile) {
                        continue;
                    }
                    SetRevisionCatalog::SetMetaRecord meta{};
                    const StorageIo metaIo = storageIoFromFileRead(metaFile);
                    const bool readOk = SetRevisionCatalog::readSetMetaRecord(metaIo, meta);
                    metaFile.close();
                    if (!readOk || meta.setId != setId || meta.latestRevisionId == 0) {
                        continue;
                    }
                    SetRevisionCatalog::SetBrowserListEntry& row = scratch[count];
                    const int folderWritten =
                        std::snprintf(row.folderName, sizeof(row.folderName), "S%04u", setId);
                    if (folderWritten <= 0 ||
                        static_cast<size_t>(folderWritten) >= sizeof(row.folderName)) {
                        continue;
                    }
                    row.setId = setId;
                    row.latestRevisionId = meta.latestRevisionId;
                    row.updatedUnix = meta.updatedUnix;
                    row.favorite = meta.favorite;
                    ++count;
                }
            }
        }
    }

    SetRevisionCatalog::sortSetBrowserListEntriesByUpdatedUnixDesc(scratch, count);
    for (size_t i = 0; i < count; ++i) {
        entries[i] = scratch[i];
    }
    return count;
#endif
}

bool StorageManager::readSetRevisionCatalogMetaForFolder(const char* folderName,
                                                           SetRevisionCatalog::SetMetaRecord& meta) {
#if BYPASS_STOP_UNDO_SAVE
    (void)folderName;
    (void)meta;
    return false;
#else
    if (folderName == nullptr || folderName[0] == '\0') {
        return false;
    }
    uint16_t setId = 0;
    if (!SetRevisionCatalog::parseSetIdFromFolderName(folderName, setId)) {
        return false;
    }
    char setMetaPath[64];
    if (!SetRevisionCatalog::formatSetMetaPath(setMetaPath, sizeof(setMetaPath), setId) ||
        !SD.exists(setMetaPath)) {
        return false;
    }
    File file = SD.open(setMetaPath, FILE_READ);
    if (!file) {
        return false;
    }
    const StorageIo io = storageIoFromFileRead(file);
    const bool ok = SetRevisionCatalog::readSetMetaRecord(io, meta);
    file.close();
    return ok && meta.setId == setId;
#endif
}

namespace {

void considerSdWallClockFloor(uint32_t& maxFloor, uint64_t unixCandidate) {
    if (unixCandidate == 0) {
        return;
    }
    const uint32_t floor = unixCandidate > UINT32_MAX ? UINT32_MAX
                                                      : static_cast<uint32_t>(unixCandidate);
    if (floor > maxFloor) {
        maxFloor = floor;
    }
}

bool wallClockSdCatalogSyncPending = false;
uint32_t wallClockSdCatalogSyncNextSetId = 1;
uint32_t wallClockSdCatalogSyncEndSetId = 0;

void applySdWallClockFloor(uint32_t maxFloor) {
    if (maxFloor > 0) {
        RtcTime::raiseWallClockToAtLeast(maxFloor);
    }
}

void collectSdWallClockFloorFromWorkspaceAndCurrent(uint32_t& maxFloor) {
    CurrentWorkspaceStorage::WorkspaceMetaRecord workspace{};
    if (CurrentWorkspaceStorage::readWorkspaceMetaFile(workspace)) {
        considerSdWallClockFloor(maxFloor, workspace.updatedUnix);
    }

    if (SD.exists(CurrentSetStorage::kCurrentMetaPath)) {
        File file = SD.open(CurrentSetStorage::kCurrentMetaPath, FILE_READ);
        if (file) {
            const StorageIo io = storageIoFromFileRead(file);
            CurrentWorkspaceStorage::EpochFileHeader epochHeader{};
            if (CurrentWorkspaceStorage::readEpochFileHeader(io, epochHeader)) {
                CurrentSetStorage::MetaHeader metaHeader{};
                if (CurrentSetStorage::readMetaHeader(io, metaHeader)) {
                    considerSdWallClockFloor(maxFloor, metaHeader.lastActiveUnix);
                    considerSdWallClockFloor(maxFloor, metaHeader.anchor.lastMaterialChangeUnix);
                }
            }
            file.close();
        }
    }
}

void queueWallClockSdCatalogSync() {
#if defined(ARDUINO)
#if BYPASS_STOP_UNDO_SAVE
    return;
#else
    wallClockSdCatalogSyncPending = false;
    wallClockSdCatalogSyncNextSetId = 1;
    wallClockSdCatalogSyncEndSetId = 0;
    if (!SD.exists(SetRevisionCatalog::kSetIndexPath)) {
        return;
    }
    File indexFile = SD.open(SetRevisionCatalog::kSetIndexPath, FILE_READ);
    if (!indexFile) {
        return;
    }
    SetRevisionCatalog::SetCatalogIndex catalogIndex{};
    const StorageIo indexIo = storageIoFromFileRead(indexFile);
    const bool indexOk = SetRevisionCatalog::readSetCatalogIndex(indexIo, catalogIndex);
    indexFile.close();
    if (!indexOk || catalogIndex.nextSetId <= 1U) {
        return;
    }
    wallClockSdCatalogSyncNextSetId = 1;
    wallClockSdCatalogSyncEndSetId = catalogIndex.nextSetId;
    wallClockSdCatalogSyncPending = true;
#endif
#endif
}

void syncWallClockFromSdTimestampsQuick() {
#if defined(ARDUINO)
#if BYPASS_STOP_UNDO_SAVE
    return;
#else
    uint32_t maxFloor = 0;
    collectSdWallClockFloorFromWorkspaceAndCurrent(maxFloor);
    applySdWallClockFloor(maxFloor);
    queueWallClockSdCatalogSync();
#endif
#endif
}

void stepWallClockFromSdCatalogSyncAnon(uint8_t maxSetsPerSlice) {
#if defined(ARDUINO)
#if BYPASS_STOP_UNDO_SAVE
    (void)maxSetsPerSlice;
    return;
#else
    if (!wallClockSdCatalogSyncPending || maxSetsPerSlice == 0) {
        return;
    }
    uint32_t maxFloor = 0;
    uint8_t scanned = 0;
    while (wallClockSdCatalogSyncNextSetId < wallClockSdCatalogSyncEndSetId &&
           scanned < maxSetsPerSlice) {
        const uint16_t setId = static_cast<uint16_t>(wallClockSdCatalogSyncNextSetId++);
        ++scanned;
        char setMetaPath[64];
        if (!SetRevisionCatalog::formatSetMetaPath(setMetaPath, sizeof(setMetaPath), setId) ||
            !SD.exists(setMetaPath)) {
            continue;
        }
        File metaFile = SD.open(setMetaPath, FILE_READ);
        if (!metaFile) {
            continue;
        }
        SetRevisionCatalog::SetMetaRecord meta{};
        const StorageIo metaIo = storageIoFromFileRead(metaFile);
        const bool readOk = SetRevisionCatalog::readSetMetaRecord(metaIo, meta);
        metaFile.close();
        if (!readOk || meta.setId != setId) {
            continue;
        }
        considerSdWallClockFloor(maxFloor, meta.updatedUnix);
        considerSdWallClockFloor(maxFloor, meta.createdUnix);
    }
    applySdWallClockFloor(maxFloor);
    if (wallClockSdCatalogSyncNextSetId >= wallClockSdCatalogSyncEndSetId) {
        wallClockSdCatalogSyncPending = false;
    }
#endif
#endif
}

void applyRevisionSlotDirectoryToSavedSetMetadata(
    const RevisionPackedBlob::RevisionLoopSlotDirectoryEntry* entries, uint16_t entryCount,
    SavedSetCatalog::SavedSetMetadata& metadata) {
    metadata.trackCount = 0;
    metadata.filledSlotCount = 0;
    metadata.masterLoopBars = 0;
    for (uint8_t trackIndex = 0; trackIndex < Config::NUM_TRACKS; ++trackIndex) {
        metadata.perTrackFilledSlots[trackIndex] = 0;
    }
    uint16_t maxBars = 0;
    for (uint16_t index = 0; index < entryCount; ++index) {
        const RevisionPackedBlob::RevisionLoopSlotDirectoryEntry& entry = entries[index];
        if (entry.occupied == 0) {
            continue;
        }
        if (entry.trackIndex >= Config::NUM_TRACKS || entry.slotIndex >= Config::MAX_LOOPS_PER_TRACK) {
            continue;
        }
        ++metadata.perTrackFilledSlots[entry.trackIndex];
        if (entry.bars > maxBars) {
            maxBars = entry.bars;
        }
    }
    uint16_t filledTotal = 0;
    for (uint8_t trackIndex = 0; trackIndex < Config::NUM_TRACKS; ++trackIndex) {
        if (metadata.perTrackFilledSlots[trackIndex] > 0) {
            ++metadata.trackCount;
            filledTotal += metadata.perTrackFilledSlots[trackIndex];
        }
    }
    metadata.filledSlotCount =
        static_cast<uint8_t>(filledTotal > 255 ? 255 : filledTotal);
    metadata.masterLoopBars = maxBars;
}

bool readRevisionHeaderFromRevisionFile(File& file, RevisionPackedBlob::RevisionHeader& headerOut) {
    uint8_t headerBytes[RevisionPackedBlob::kRevisionHeaderByteSize];
    if (!file.seek(0) ||
        file.read(headerBytes, sizeof(headerBytes)) != static_cast<int>(sizeof(headerBytes))) {
        return false;
    }
    return RevisionPackedBlob::parseRevisionHeaderFromBytes(headerBytes, sizeof(headerBytes),
                                                          headerOut);
}

}  // namespace

bool StorageManager::readSetRevisionCatalogBrowserMetadata(
    const char* folderName, SavedSetCatalog::SavedSetMetadata& metadata, uint16_t& setIdOut,
    uint16_t& revisionIdOut, uint32_t& updatedUnixOut) {
#if BYPASS_STOP_UNDO_SAVE
    (void)folderName;
    (void)metadata;
    (void)setIdOut;
    (void)revisionIdOut;
    (void)updatedUnixOut;
    return false;
#else
    SetRevisionCatalog::SetMetaRecord meta{};
    if (!readSetRevisionCatalogMetaForFolder(folderName, meta)) {
        return false;
    }
    setIdOut = meta.setId;
    revisionIdOut = meta.latestRevisionId;
    updatedUnixOut = static_cast<uint32_t>(meta.updatedUnix);
    metadata = {};
    metadata.createdAtUnix = updatedUnixOut;

    if (meta.latestRevisionId == 0) {
        return true;
    }

    char revisionPath[80];
    if (!SetRevisionCatalog::formatRevisionPath(revisionPath, sizeof(revisionPath), meta.setId,
                                                meta.latestRevisionId, false) ||
        !SD.exists(revisionPath)) {
        return true;
    }
    File revisionFile = SD.open(revisionPath, FILE_READ);
    if (!revisionFile) {
        return true;
    }
    const size_t fileSize = revisionFile.size();
    RevisionPackedBlob::RevisionHeader header{};
    if (!readRevisionHeaderFromRevisionFile(revisionFile, header)) {
        revisionFile.close();
        return true;
    }
    if (header.createdUnix != 0) {
        updatedUnixOut = static_cast<uint32_t>(header.createdUnix);
        metadata.createdAtUnix = updatedUnixOut;
    }
    uint16_t entryCount = 0;
    if (readSlotIndexEntriesFromRevisionFile(revisionFile, fileSize, header,
                                             storageSession.revisionLoad.slotDirectoryEntries,
                                             kMaxRevisionLoopIndexEntries, entryCount)) {
        applyRevisionSlotDirectoryToSavedSetMetadata(storageSession.revisionLoad.slotDirectoryEntries, entryCount,
                                                     metadata);
    }
    revisionFile.close();
    return true;
#endif
}

size_t StorageManager::listSetRevisionHistoryEntries(
    uint16_t setId, SetRevisionCatalog::RevisionBrowserListEntry* entries, size_t maxEntries) {
#if BYPASS_STOP_UNDO_SAVE
    (void)setId;
    (void)entries;
    (void)maxEntries;
    return 0;
#else
    if (setId == 0 || entries == nullptr || maxEntries == 0) {
        return 0;
    }

    char setMetaPath[64];
    if (!SetRevisionCatalog::formatSetMetaPath(setMetaPath, sizeof(setMetaPath), setId) ||
        !SD.exists(setMetaPath)) {
        return 0;
    }
    File metaFile = SD.open(setMetaPath, FILE_READ);
    if (!metaFile) {
        return 0;
    }
    SetRevisionCatalog::SetMetaRecord meta{};
    const StorageIo metaIo = storageIoFromFileRead(metaFile);
    const bool metaOk = SetRevisionCatalog::readSetMetaRecord(metaIo, meta);
    metaFile.close();
    if (!metaOk || meta.setId != setId || meta.latestRevisionId == 0) {
        return 0;
    }

    SetRevisionCatalog::RevisionBrowserListEntry scratch[16];
    constexpr size_t kScratchCapacity = 16;
    const size_t capacity = maxEntries < kScratchCapacity ? maxEntries : kScratchCapacity;
    size_t count = 0;

    for (uint16_t revisionId = meta.latestRevisionId;
         revisionId > 0 && count < capacity; --revisionId) {
        char revisionPath[80];
        if (!SetRevisionCatalog::formatRevisionPath(revisionPath, sizeof(revisionPath), setId,
                                                    revisionId, false) ||
            !SD.exists(revisionPath)) {
            continue;
        }
        File revisionFile = SD.open(revisionPath, FILE_READ);
        if (!revisionFile) {
            continue;
        }
        RevisionPackedBlob::RevisionHeader header{};
        const bool headerOk = readRevisionHeaderFromRevisionFile(revisionFile, header);
        revisionFile.close();
        if (!headerOk || header.revisionId != revisionId) {
            continue;
        }
        SetRevisionCatalog::RevisionBrowserListEntry& row = scratch[count];
        row.revisionId = revisionId;
        row.createdUnix = header.createdUnix;
        ++count;
    }

    SetRevisionCatalog::sortRevisionBrowserListEntriesByCreatedUnixDesc(scratch, count);
    for (size_t i = 0; i < count; ++i) {
        entries[i] = scratch[i];
    }
    return count;
#endif
}

bool StorageManager::readSetRevisionHistoryBrowserMetadata(
    uint16_t setId, uint16_t revisionId, SavedSetCatalog::SavedSetMetadata& metadata,
    uint32_t& createdUnixOut) {
#if BYPASS_STOP_UNDO_SAVE
    (void)setId;
    (void)revisionId;
    (void)metadata;
    (void)createdUnixOut;
    return false;
#else
    if (setId == 0 || revisionId == 0) {
        return false;
    }
    metadata = {};
    createdUnixOut = 0;

    char revisionPath[80];
    if (!SetRevisionCatalog::formatRevisionPath(revisionPath, sizeof(revisionPath), setId,
                                                revisionId, false) ||
        !SD.exists(revisionPath)) {
        return false;
    }
    File revisionFile = SD.open(revisionPath, FILE_READ);
    if (!revisionFile) {
        return false;
    }
    const size_t fileSize = revisionFile.size();
    RevisionPackedBlob::RevisionHeader header{};
    if (!readRevisionHeaderFromRevisionFile(revisionFile, header)) {
        revisionFile.close();
        return false;
    }
    if (header.createdUnix != 0) {
        createdUnixOut = static_cast<uint32_t>(header.createdUnix);
        metadata.createdAtUnix = createdUnixOut;
    }
    uint16_t entryCount = 0;
    if (readSlotIndexEntriesFromRevisionFile(revisionFile, fileSize, header,
                                             storageSession.revisionLoad.slotDirectoryEntries,
                                             kMaxRevisionLoopIndexEntries, entryCount)) {
        applyRevisionSlotDirectoryToSavedSetMetadata(storageSession.revisionLoad.slotDirectoryEntries, entryCount,
                                                     metadata);
    }
    revisionFile.close();
    return true;
#endif
}

bool StorageManager::readSavedSetMetadataForFolder(const char* folderName,
                                                   SavedSetCatalog::SavedSetMetadata& metadata) {
    if (folderName == nullptr || folderName[0] == '\0') {
        return false;
    }
    char metaPath[StorageManagerInternal::kSavedSetPathCapacity];
    const int written =
        std::snprintf(metaPath, sizeof(metaPath), "%s/%s/%s", CurrentSetStorage::kSetsRoot,
                      folderName, CurrentSetStorage::kSetBinFileName);
    if (written <= 0 || static_cast<size_t>(written) >= sizeof(metaPath)) {
        return false;
    }
    return readSavedSetMetadataFromMetaPath(metaPath, metadata);
}

bool StorageManager::readCurrentSetBrowserMetadata(SavedSetCatalog::SavedSetMetadata& metadata) {
    const uint32_t sequence = currentSetAnchorFields.loadedFromSequence;
    if (!StorageManagerInternal::buildSavedSetMetadata(sequence != 0 ? sequence : 1,
                               SavedSetCatalog::FolderNamingMode::Unknown,
                               currentSetLastActiveUnix, metadata)) {
        return false;
    }
    metadata.sequence = sequence;
    metadata.createdAtUnix = currentSetLastActiveUnix;
    metadata.userLabel[0] = '\0';
    return true;
}

void StorageManager::admitLoopPersist(LoopId loopId) {
#if BYPASS_STOP_UNDO_SAVE
    (void)loopId;
    return;
#else
    if (loopId == kInvalidLoopId) {
        return;
    }
    PersistenceWorkQueue::admitWork(PersistWorkType::LoopPersist, persistKeyForLoop(loopId));
#endif
}

void StorageManager::admitLoopUndoHistory(uint8_t trackIndex, uint8_t slotIndex) {
#if BYPASS_STOP_UNDO_SAVE
    (void)trackIndex;
    (void)slotIndex;
    return;
#else
    if (trackIndex >= Config::NUM_TRACKS || slotIndex >= Config::MAX_LOOPS_PER_TRACK) {
        return;
    }
    PersistenceWorkQueue::admitWork(PersistWorkType::LoopUndoHistory,
                                    persistKeyForSlot(trackIndex, slotIndex));
#endif
}

void StorageManager::admitSlotMeta(uint8_t trackIndex, uint8_t slotIndex) {
#if BYPASS_STOP_UNDO_SAVE
    (void)trackIndex;
    (void)slotIndex;
    return;
#else
    if (trackIndex >= Config::NUM_TRACKS || slotIndex >= Config::MAX_LOOPS_PER_TRACK) {
        return;
    }
    PersistenceWorkQueue::admitWork(PersistWorkType::SlotMeta,
                                  persistKeyForSlot(trackIndex, slotIndex));
#endif
}

void StorageManager::admitTrackMeta(uint8_t trackIndex) {
#if BYPASS_STOP_UNDO_SAVE
    (void)trackIndex;
    return;
#else
    if (trackIndex >= Config::NUM_TRACKS) {
        return;
    }
    PersistenceWorkQueue::admitWork(PersistWorkType::TrackMeta, persistKeyForTrack(trackIndex));
#endif
}

void StorageManager::admitWorkspaceFooter() {
#if BYPASS_STOP_UNDO_SAVE
    return;
#else
    workspaceFooterPersistDeferred = false;
    PersistenceWorkQueue::admitWork(PersistWorkType::WorkspaceFooter, persistKeySingleton());
#endif
}

void StorageManager::requestWorkspaceFooterPersistWhenSafe() {
#if BYPASS_STOP_UNDO_SAVE
    return;
#else
    workspaceFooterPersistDeferred = true;
#endif
}

void StorageManager::admitGlobalMeta() {
#if BYPASS_STOP_UNDO_SAVE
    return;
#else
    workspaceFooterPersistDeferred = false;
    PersistenceWorkQueue::admitWork(PersistWorkType::GlobalMeta, persistKeySingleton());
#endif
}

void StorageManager::markCurrentSetLoopSlotDirty(uint8_t trackIndex, uint8_t slotIndex) {
#if BYPASS_STOP_UNDO_SAVE
    (void)trackIndex;
    (void)slotIndex;
    return;
#endif
    markCurrentSetLoopSlotDirtyInternal(trackIndex, slotIndex);
    admitSlotMeta(trackIndex, slotIndex);
    PersistenceWorkQueue::admitWork(PersistWorkType::LoopPersist,
                                  persistKeyForSlot(trackIndex, slotIndex));
}

void StorageManager::markCurrentSetTrackDirty(uint8_t trackIndex) {
#if BYPASS_STOP_UNDO_SAVE
    (void)trackIndex;
    return;
#endif
    markCurrentSetTrackDirtyInternal(trackIndex);
    admitTrackMeta(trackIndex);
    for (uint8_t slot = 0; slot < Config::MAX_LOOPS_PER_TRACK; ++slot) {
        PersistenceWorkQueue::admitWork(PersistWorkType::LoopPersist,
                                        persistKeyForSlot(trackIndex, slot));
        admitSlotMeta(trackIndex, slot);
    }
}

void StorageManager::markAllCurrentSetLoopSlotsDirty() {
#if BYPASS_STOP_UNDO_SAVE
    return;
#endif
    markAllCurrentSetLoopSlotsDirtyInternal();
    for (uint8_t track = 0; track < Config::NUM_TRACKS; ++track) {
        admitTrackMeta(track);
        for (uint8_t slot = 0; slot < Config::MAX_LOOPS_PER_TRACK; ++slot) {
            PersistenceWorkQueue::admitWork(PersistWorkType::LoopPersist,
                                            persistKeyForSlot(track, slot));
            admitSlotMeta(track, slot);
        }
    }
}

void StorageManager::processEditAutosave(const LooperState& state) {
#if BYPASS_STOP_UNDO_SAVE
    (void)state;
    urgentEditSavePending = false;
    return;
#endif
    const uint32_t nowMs = millis();
    if (urgentEditSavePending) {
        urgentEditSavePending = false;
        clearEditDirtyAfterDeferredSave = true;
        if (editManager.isNoteEditActive()) {
            editManager.markCurrentEditBatchDurable(trackManager.getSelectedTrack());
        }
        for (uint8_t t = 0; t < trackManager.getTrackCount(); ++t) {
            Track& track = trackManager.getTrack(t);
            if (!track.loopsAllocated()) {
                continue;
            }
            for (uint8_t s = 0; s < Config::MAX_LOOPS_PER_TRACK; ++s) {
                if (track.getLoop(s).isEditStateDirty()) {
                    StorageManager::markCurrentSetLoopSlotDirty(t, s);
                }
            }
        }
        requestDeferredSaveState(state, UINT32_MAX, true);
        // Runtime policy: urgent NOTE_EDIT save requests stay deferred to avoid
        // blocking playback timing on synchronous SD drain.
        lastEditAutosaveMs = nowMs;
        return;
    }

    bool captureActive = false;
    for (uint8_t t = 0; t < trackManager.getTrackCount(); ++t) {
        Track& track = trackManager.getTrack(t);
        if (track.isRecording() || track.isOverdubbing()) {
            captureActive = true;
        }
    }
    const bool anyDirty = anyAllocatedLoopEditStateDirty();
    if (!anyDirty || captureActive) {
        return;
    }
    if (nowMs - lastEditAutosaveMs < Config::autosaveIntervalMs) {
        return;
    }
    clearEditDirtyAfterDeferredSave = true;
    if (editManager.isNoteEditActive()) {
        editManager.markCurrentEditBatchDurable(trackManager.getSelectedTrack());
    }
    for (uint8_t t = 0; t < trackManager.getTrackCount(); ++t) {
        Track& track = trackManager.getTrack(t);
        if (!track.loopsAllocated()) {
            continue;
        }
        for (uint8_t s = 0; s < Config::MAX_LOOPS_PER_TRACK; ++s) {
            if (track.getLoop(s).isEditStateDirty()) {
                StorageManager::markCurrentSetLoopSlotDirty(t, s);
            }
        }
    }
    requestDeferredSaveState(state);
    lastEditAutosaveMs = nowMs;
}

void StorageManager::deferWorkspaceSaveDispatchDuringPlayback(uint32_t graceMs) {
#if BYPASS_STOP_UNDO_SAVE
    (void)graceMs;
    return;
#else
    const uint32_t untilMs = millis() + graceMs;
    if (untilMs > storageSession.currentWorkspaceSave.deferDispatchUntilMs) {
        storageSession.currentWorkspaceSave.deferDispatchUntilMs = untilMs;
    }
#if defined(SESSION_CAPTURE)
    char outcome[24];
    std::snprintf(outcome, sizeof(outcome), "until_%lu", static_cast<unsigned long>(untilMs));
    SC_PERSIST("defer_playback", 0, 0, 0, outcome);
#endif
#endif
}

void StorageManager::requestDeferredSaveState(const LooperState& /*state*/, uint32_t admissionHeap,
                                              bool isUrgentRequest) {
#if BYPASS_STOP_UNDO_SAVE
    (void)isUrgentRequest;
    return;
#endif
    if (isUrgentRequest) {
        storageSession.currentWorkspaceSave.deferDispatchUntilMs = 0;
    }
    const bool alreadyPending = storageSession.currentWorkspaceSave.pending;
    if (admissionHeap != UINT32_MAX || storageSession.currentWorkspaceSave.admissionHeap == 0) {
        storageSession.currentWorkspaceSave.admissionHeap = admissionHeap;
    }
    const uint32_t reportedHeap = storageSession.currentWorkspaceSave.admissionHeap == UINT32_MAX
                                      ? 0
                                      : storageSession.currentWorkspaceSave.admissionHeap;
    storageSession.currentWorkspaceSave.urgentRequested = storageSession.currentWorkspaceSave.urgentRequested || isUrgentRequest;
    storageSession.currentWorkspaceSave.pending = true;
    if (!alreadyPending) {
        PersistenceDiagnostics::onDeferredSaveRequested();
    }
    SC_PERSIST("request", 0, reportedHeap, reportedHeap,
               alreadyPending ? "already_pending" : "queued");
}

namespace StorageManagerInternal {

STORAGE_PERSIST_MEM void maybeAdmitDeferredWorkspaceFooter() {
#if BYPASS_STOP_UNDO_SAVE
    return;
#else
    if (!workspaceFooterPersistDeferred) {
        return;
    }
    if (isCaptureActiveForPersistence() || isTransportActiveForPersistence() ||
        anyTrackArmedOrPendingRecordForPersistence()) {
        return;
    }
    workspaceFooterPersistDeferred = false;
    StorageManager::admitWorkspaceFooter();
#endif
}

bool hasPersistenceWorkPending() {
#if BYPASS_STOP_UNDO_SAVE
    return false;
#else
    return storageSession.currentWorkspaceSave.pending ||
           PersistenceWorkQueue::queueDepth() > 0 || PersistenceWorkQueue::writingWorkItemCount() > 0 ||
           storageSession.persistenceWorkItem.itemActive;
#endif
}

void stepWallClockFromSdCatalogSync(uint8_t maxSetsPerSlice) {
    stepWallClockFromSdCatalogSyncAnon(maxSetsPerSlice);
}

void syncWallClockFromSdTimestampsQuickForBootLoad() {
    syncWallClockFromSdTimestampsQuick();
}

void markAllCurrentSetLoopSlotsDirtyForBootRecovery() {
    markAllCurrentSetLoopSlotsDirtyInternal(false);
}

}  // namespace StorageManagerInternal

bool StorageManager::isDeferredSaveActive() {
#if BYPASS_STOP_UNDO_SAVE
    return false;
#else
    return storageSession.currentWorkspaceSave.sdIoActive ||
           storageSession.persistenceWorkItem.sdIoActive;
#endif
}

bool StorageManager::hasDeferredSaveWork() {
#if BYPASS_STOP_UNDO_SAVE
    return false;
#else
    return hasPersistenceWorkPending();
#endif
}

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

bool StorageManager::hasRevisionCommitWork() {
#if BYPASS_STOP_UNDO_SAVE
    return false;
#else
    return storageSession.revisionCommit.pending || storageSession.revisionCommit.inProgress;
#endif
}

bool StorageManager::isRevisionCommitActive() {
#if BYPASS_STOP_UNDO_SAVE
    return false;
#else
    return storageSession.revisionCommit.sdIoActive;
#endif
}

bool StorageManager::toggleSetRevisionCatalogFavorite(uint16_t setId) {
#if BYPASS_STOP_UNDO_SAVE
    (void)setId;
    return false;
#else
    if (setId == 0) {
        return false;
    }
    char setMetaPath[64];
    if (!SetRevisionCatalog::formatSetMetaPath(setMetaPath, sizeof(setMetaPath), setId) ||
        !SD.exists(setMetaPath)) {
        return false;
    }
    File file = SD.open(setMetaPath, FILE_READ);
    if (!file) {
        return false;
    }
    SetRevisionCatalog::SetMetaRecord meta{};
    const StorageIo readIo = storageIoFromFileRead(file);
    const bool readOk = SetRevisionCatalog::readSetMetaRecord(readIo, meta);
    file.close();
    if (!readOk || meta.setId != setId) {
        return false;
    }
    meta.favorite = meta.favorite != 0 ? 0 : 1;
    return writeSetMetaRecordFile(setId, meta);
#endif
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

    if (SD.exists(STORAGE_FILENAME)) {
        written = std::snprintf(dest, sizeof(dest), "/state.bad.%lu", stamp);
        if (written > 0 && static_cast<size_t>(written) < sizeof(dest)) {
            ok = renamePathOnSdIfPresent(STORAGE_FILENAME, dest) && ok;
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

DeferredSaveDisplayStatus StorageManager::getDeferredSaveDisplayStatus(uint32_t nowMs) {
#if BYPASS_STOP_UNDO_SAVE
    (void)nowMs;
    return {};
#else
    DeferredSaveDisplayInputs inputs{};
    inputs.savePending = storageSession.currentWorkspaceSave.pending ||
                       PersistenceWorkQueue::queueDepth() > 0;
    inputs.saveInProgress = storageSession.persistenceWorkItem.itemActive ||
                            PersistenceWorkQueue::writingWorkItemCount() > 0 ||
                            storageSession.persistenceWorkItem.sdIoActive;
    inputs.revisionCommitPending = storageSession.revisionCommit.pending;
    inputs.revisionCommitInProgress = storageSession.revisionCommit.inProgress;
    inputs.completedAtMs = storageSession.currentWorkspaceSave.completedAtMs;
    inputs.failedAtMs = storageSession.currentWorkspaceSave.failedAtMs;
    return resolveDeferredSaveDisplayStatus(nowMs, inputs);
#endif
}

bool StorageManager::saveState(const LooperState& state) {
#if BYPASS_STOP_UNDO_SAVE
    (void)state;
    Serial.println("[StorageManager] BYPASS_STOP_UNDO_SAVE: skip saveState");
    return true;
#endif
    HotPathTelemetry::ScopedSaveState telemetryScope;
    Serial.println("[StorageManager] Draining persistence work queue to SD card...");

    storageSession.currentWorkspaceSave.admissionHeap = UINT32_MAX;
    storageSession.currentWorkspaceSave.urgentRequested = true;
    storageSession.currentWorkspaceSave.deferDispatchUntilMs = 0;
    const bool alreadyPending = storageSession.currentWorkspaceSave.pending;
    storageSession.currentWorkspaceSave.pending = true;
    SC_PERSIST("request", 0, 0, 0,
               alreadyPending ? "sync_drain_already_pending" : "sync_drain");

    storageSession.currentWorkspaceSave.lastCompletedOk = false;

    const SyncDrainBudget drainBudget = buildSyncDrainBudgetForSession();
    uint32_t steps = 0;
    uint32_t stuckIterations = 0;
    SyncDrainProgressSnapshot lastProgress = captureSyncDrainProgressSnapshot();
    SyncDrainFailureReason failureReason = SyncDrainFailureReason::None;

    while (hasPersistenceWorkPending()) {
        if (steps >= drainBudget.maxSliceSteps) {
            failureReason = SyncDrainFailureReason::ExceededSliceBudget;
            break;
        }

        maybeAdmitFinalizeWorkspaceAfterDrain();
        const SyncDrainProgressSnapshot beforeProgress = lastProgress;
        processDeferredSaveState(state);
        yield();
        steps++;

        const SyncDrainProgressSnapshot afterProgress = captureSyncDrainProgressSnapshot();
        if (PersistenceSyncDrainBudget::madeSyncDrainProgress(beforeProgress, afterProgress)) {
            stuckIterations = 0;
            lastProgress = afterProgress;
        } else {
            ++stuckIterations;
            if (stuckIterations >= drainBudget.maxStuckIterations) {
                failureReason = SyncDrainFailureReason::Stuck;
                break;
            }
        }
    }

    const bool completed = !hasPersistenceWorkPending();
    if (!completed) {
        if (failureReason == SyncDrainFailureReason::Stuck) {
            Serial.printf(
                "[StorageManager] ERROR: Persistence drain stuck after %u iterations "
                "(max %u, expected ~%u slice steps, ~%u SD bytes)\n",
                static_cast<unsigned>(stuckIterations),
                static_cast<unsigned>(drainBudget.maxStuckIterations),
                static_cast<unsigned>(drainBudget.expectedSliceSteps),
                static_cast<unsigned>(drainBudget.estimatedSdPayloadBytes));
        } else {
            Serial.printf(
                "[StorageManager] ERROR: Persistence drain exceeded slice budget after %u steps "
                "(max %u, expected ~%u slice steps, ~%u SD bytes)\n",
                static_cast<unsigned>(steps), static_cast<unsigned>(drainBudget.maxSliceSteps),
                static_cast<unsigned>(drainBudget.expectedSliceSteps),
                static_cast<unsigned>(drainBudget.estimatedSdPayloadBytes));
        }
        return false;
    }
    if (storageSession.currentWorkspaceSave.pending) {
        Serial.println("[StorageManager] ERROR: Persistence drain left flush pending");
        return false;
    }
    if (!storageSession.currentWorkspaceSave.lastCompletedOk) {
        Serial.println("[StorageManager] ERROR: Persistence drain failed");
        return false;
    }
    Serial.println("[StorageManager] State saved successfully (v4).");
    telemetryScope.setOk(true);
    return true;
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

// Cold v5 migration path — keep out of ITCM so DMAMEM StorageSession does not
// tip FlexRAM into a 14th code bank (steals a DTCM bank).
bool STORAGE_PERSIST_MEM StorageManager::loadV5MonolithIntoRam(LooperState& state) {
    Serial.println("[StorageManager] Loading state from SD card...");
    File file = SD.open(STORAGE_FILENAME, FILE_READ);
    if (!file) {
        Serial.print("[StorageManager] ERROR: Could not open file for reading: ");
        Serial.println(STORAGE_FILENAME);
        return false;
    }
    // Use temporary variables to avoid corrupting current state if file is bad
    uint32_t version = 0;
    if (!readRaw(file, &version, sizeof(version))) {
        Serial.println("[StorageManager] ERROR: Failed to read version");
        file.close();
        return false;
    }
    Serial.println("[StorageManager] Version read OK");
    if (version != 6) {
        Serial.print("[StorageManager] ERROR: Unsupported legacy storage version. Found: ");
        Serial.println(version);
        file.close();
        return false;
    }

    float savedBpm = 0;
    if (!readRaw(file, &savedBpm, sizeof(savedBpm))) {
        Serial.println("[StorageManager] ERROR: Failed to read BPM");
        file.close();
        return false;
    }
    if (savedBpm >= 20.0f && savedBpm <= 300.0f) {
        bpm = savedBpm;
        Serial.print("[StorageManager] Restored BPM: ");
        Serial.println(savedBpm);
    }

    // Looper state
    uint32_t looperStateVal = 0;
    if (!readRaw(file, &looperStateVal, sizeof(looperStateVal))) {
        Serial.println("[StorageManager] ERROR: Failed to read looper state");
        file.close();
        return false;
    }
    LooperState loadedLooperState = sanitizeLooperStateForPersistence(static_cast<LooperState>(looperStateVal));

    // Master loop length
    uint32_t masterLoopLength = 0;
    if (!readRaw(file, &masterLoopLength, sizeof(masterLoopLength))) {
        Serial.println("[StorageManager] ERROR: Failed to read master loop length");
        file.close();
        return false;
    }

    // Tracks
    uint8_t numTracks = 0;
    if (!readRaw(file, &numTracks, sizeof(numTracks))) {
        Serial.println("[StorageManager] ERROR: Failed to read numTracks");
        file.close();
        return false;
    }
    if (numTracks != Config::NUM_TRACKS) {
        Serial.print("[StorageManager] ERROR: numTracks mismatch. Found: ");
        Serial.println(numTracks);
        file.close();
        return false;
    }

    std::vector<uint8_t> activeLoopIndex(numTracks, 0);
        uint8_t selectedTrackIdx = 0;
        auto failAfterPartialLoad = [&file]() {
            file.close();
            quarantineStorageFile();
            resetTracksAfterFailedLoad();
            return false;
        };

        for (uint8_t t = 0; t < numTracks; ++t) {
            Track& track = trackManager.getTrack(t);
            track.ensureLoopsAllocated();

            uint32_t trackStateRaw = 0;
            if (!readRaw(file, &trackStateRaw, sizeof(trackStateRaw))) {
                Serial.print("[StorageManager] ERROR: Failed to read trackState for track "); Serial.println(t);
                return failAfterPartialLoad();
            }
            TrackState loadedTrackState = static_cast<TrackState>(trackStateRaw);

            bool muted = false;
            if (!readRaw(file, &muted, sizeof(muted))) {
                Serial.print("[StorageManager] ERROR: Failed to read muted for track "); Serial.println(t);
                return failAfterPartialLoad();
            }

            bool anySlotHasEvents = false;

            for (uint8_t s = 0; s < Config::MAX_LOOPS_PER_TRACK; ++s) {
                bool slotEnabled = false;
                bool slotMuted = false;
                LoopId slotLoopId = kInvalidLoopId;
                if (!readRaw(file, &slotEnabled, sizeof(slotEnabled))) { Serial.print("[StorageManager] ERROR: Failed to read slotEnabled for track "); Serial.print(t); Serial.print(" slot "); Serial.println(s); return failAfterPartialLoad(); }
                if (!readRaw(file, &slotMuted, sizeof(slotMuted))) { Serial.print("[StorageManager] ERROR: Failed to read slotMuted for track "); Serial.print(t); Serial.print(" slot "); Serial.println(s); return failAfterPartialLoad(); }
                if (!readRaw(file, &slotLoopId, sizeof(slotLoopId))) { Serial.print("[StorageManager] ERROR: Failed to read slotLoopId for track "); Serial.print(t); Serial.print(" slot "); Serial.println(s); return failAfterPartialLoad(); }

                if (slotLoopId == kInvalidLoopId || slotLoopId >= Config::MAX_LOOPS_PER_TRACK) {
                    Serial.print("[StorageManager] WARNING: Invalid slotLoopId ");
                    Serial.print(static_cast<unsigned long>(slotLoopId));
                    Serial.print(" for track ");
                    Serial.print(t);
                    Serial.print(" slot ");
                    Serial.print(s);
                    Serial.print(" — repairing to ");
                    Serial.println(s);
                    slotLoopId = static_cast<LoopId>(s);
                }

                trackManager.setSlotEnabled(t, s, slotEnabled);
                trackManager.setSlotMuted(t, s, slotMuted);
                track.slots_[s].loopId = slotLoopId;
            }

            const StorageIo loopIo = storageIoFromFileRead(file);
            for (uint8_t p = 0; p < Config::MAX_LOOPS_PER_TRACK; ++p) {
                Loop& loop = track.loopPool_.at(p);
                if (!readLoopPersisted(loopIo, loop)) {
                    Serial.print("[StorageManager] ERROR: Failed to read loop pool entry track ");
                    Serial.print(t);
                    Serial.print(" pool ");
                    Serial.println(p);
                    return failAfterPartialLoad();
                }
                if (loop.hasCommittedPasses()) {
                    anySlotHasEvents = true;
                }
            }

            applyLoadedTrackStateAfterLoopSlots(track, loadedTrackState, anySlotHasEvents, muted);
        }

        if (!readRaw(file, &selectedTrackIdx, sizeof(selectedTrackIdx))) {
            Serial.println("[StorageManager] ERROR: Failed to read selectedTrackIdx for legacy monolith");
            return failAfterPartialLoad();
        }

        std::vector<uint8_t> selectedSlotIndex(numTracks, 0);
        for (uint8_t t = 0; t < numTracks; ++t) {
            if (!readRaw(file, &activeLoopIndex[t], sizeof(activeLoopIndex[t]))) {
                Serial.println("[StorageManager] ERROR: Failed to read activeLoopIndex for legacy monolith");
                return failAfterPartialLoad();
            }
        }

        uint32_t footerToken = 0;
        if (!readRaw(file, &footerToken, sizeof(footerToken))) {
            Serial.println("[StorageManager] ERROR: Failed to read global undo stack token for legacy monolith");
            return failAfterPartialLoad();
        }
        if (footerToken == kFooterSelectedSlotExtensionToken) {
            for (uint8_t t = 0; t < numTracks; ++t) {
                if (!readRaw(file, &selectedSlotIndex[t], sizeof(selectedSlotIndex[t]))) {
                    Serial.println("[StorageManager] ERROR: Failed to read selectedSlotIndex for legacy monolith");
                    return failAfterPartialLoad();
                }
            }
            if (!readRaw(file, &footerToken, sizeof(footerToken))) {
                Serial.println("[StorageManager] ERROR: Failed to read global undo stack token for legacy monolith");
                return failAfterPartialLoad();
            }
        } else {
            for (uint8_t t = 0; t < numTracks; ++t) {
                selectedSlotIndex[t] = activeLoopIndex[t];
            }
        }
        if (footerToken != kGlobalUndoStackToken) {
            Serial.println("[StorageManager] ERROR: Global undo stack token mismatch for legacy monolith");
            return failAfterPartialLoad();
        }
        for (uint8_t t = 0; t < numTracks; ++t) {
            Track& track = trackManager.getTrack(t);
            if (!readGlobalUndoStackFromFile(file, track.getGlobalUndoStack())) {
                Serial.print("[StorageManager] ERROR: Failed to read global undo stack for track ");
                Serial.println(t);
                return failAfterPartialLoad();
            }
        }

        uint32_t svokToken = 0;
        if (!readRaw(file, &svokToken, sizeof(svokToken))) {
            Serial.println("[StorageManager] ERROR: Failed to read storage completion marker for legacy monolith");
            return failAfterPartialLoad();
        }
        if (svokToken != CurrentSetStorage::kSaveFileToken) {
            Serial.println("[StorageManager] ERROR: Storage completion marker mismatch for legacy monolith");
            return failAfterPartialLoad();
        }

        file.close();
        Serial.println("[StorageManager] Legacy monolith state loaded successfully (v5).");

        applyLoadedTransportFooter(numTracks, activeLoopIndex, selectedSlotIndex, selectedTrackIdx,
                                   state, loadedLooperState, masterLoopLength);
        return true;
}

bool StorageManager::migrateV5MonolithToCurrentSet(LooperState& state) {
    Serial.println("[StorageManager] Migrating v5 monolith to CurrentSet (deferred SD write)...");
    if (!loadV5MonolithIntoRam(state)) {
        return false;
    }
    currentSetAnchorFields = {};
    quarantineLegacyMonolithAfterSave = true;
    forceCurrentSetFullLoopWrite = true;
    markAllCurrentSetLoopSlotsDirtyInternal(false);
    requestDeferredSaveState(state);
    Serial.println("[StorageManager] v5 state loaded to RAM; CurrentSet write queued.");
    return true;
}

bool StorageManager::loadCurrentSetFromSd(LooperState& state) {
    return loadCurrentWorkspaceAtBoot(state);
}


