//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "StorageManager.h"
#include "Utils/BootLoopSlotRestore.h"
#include "TrackManager.h"
#include "Loop.h"
#include "Slot.h"
#include "StorageLoopIo.h"
#include "CurrentSetStorage.h"
#include "CurrentWorkspaceStorage.h"
#include "PersistenceLayout.h"
#include "PersistenceBudget.h"
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
#include "BootRecoveryPolicy.h"
#include "PersistenceSchema.h"
#include "SavedSetCatalog.h"
#include "RtcTime.h"
#include "Globals.h"
#include "Logger.h"
#include "Utils/DebugSessionCapture.h"
#include "Utils/BootTelemetry.h"
#include "Utils/MemoryMonitor.h"
#include "Utils/PersistenceDiagnostics.h"
#include <SD.h>
#include <Arduino.h>
#include "TrackUndo.h"
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

constexpr size_t kSavedSetPathCapacity = 64;

struct DeferredLoopSlotRestore {
    uint8_t track = 0;
    uint8_t slot = 0;
    uint8_t restorePriority = 3;
};

struct PendingLoopSlotRestoreQueue {
    static constexpr uint16_t kCapacity =
        static_cast<uint16_t>(Config::NUM_TRACKS * Config::MAX_LOOPS_PER_TRACK);
    DeferredLoopSlotRestore entries[kCapacity];
    uint16_t count = 0;
};

PendingLoopSlotRestoreQueue pendingLoopSlotRestores_{};
char restoredSetBundlePath_[80] = {};
std::array<uint32_t, Config::NUM_TRACKS> undoStackFileOffsets_{};
uint8_t undoHydrateTrackIndex_ = 0;
bool undoSnapshotsPending_ = false;

bool STORAGE_PERSIST_MEM StorageManager::loopSlotHasPayloadOnSd(uint8_t trackIndex, uint8_t slotIndex) {
    char loopPath[64];
    if (!CurrentSetStorage::formatLoopSlotPath(loopPath, sizeof(loopPath), trackIndex, slotIndex)) {
        return false;
    }
    return SD.exists(loopPath) && CurrentSetStorage::verifySaveFileTokenAtPath(loopPath);
}

bool loopSlotManifestExistsOnSd(uint8_t trackIndex, uint8_t slotIndex) {
    char loopPath[64];
    if (!CurrentSetStorage::formatLoopSlotPath(loopPath, sizeof(loopPath), trackIndex, slotIndex)) {
        return false;
    }
    return SD.exists(loopPath);
}

void STORAGE_PERSIST_MEM removeDeferredLoopSlotRestore(uint8_t trackIndex, uint8_t slotIndex) {
    for (uint16_t i = 0; i < pendingLoopSlotRestores_.count;) {
        if (pendingLoopSlotRestores_.entries[i].track == trackIndex &&
            pendingLoopSlotRestores_.entries[i].slot == slotIndex) {
            for (uint16_t j = i + 1; j < pendingLoopSlotRestores_.count; ++j) {
                pendingLoopSlotRestores_.entries[j - 1] = pendingLoopSlotRestores_.entries[j];
            }
            --pendingLoopSlotRestores_.count;
        } else {
            ++i;
        }
    }
}

void STORAGE_PERSIST_MEM sortPendingLoopSlotRestoresByPriority() {
    for (uint16_t i = 1; i < pendingLoopSlotRestores_.count; ++i) {
        const DeferredLoopSlotRestore item = pendingLoopSlotRestores_.entries[i];
        uint16_t j = i;
        while (j > 0 &&
               pendingLoopSlotRestores_.entries[j - 1].restorePriority > item.restorePriority) {
            pendingLoopSlotRestores_.entries[j] = pendingLoopSlotRestores_.entries[j - 1];
            --j;
        }
        pendingLoopSlotRestores_.entries[j] = item;
    }
}

void STORAGE_PERSIST_MEM queueDeferredLoopSlotRestore(uint8_t trackIndex, uint8_t slotIndex) {
    if (!StorageManager::loopSlotHasPayloadOnSd(trackIndex, slotIndex)) {
        return;
    }
    for (uint16_t i = 0; i < pendingLoopSlotRestores_.count; ++i) {
        const DeferredLoopSlotRestore& pending = pendingLoopSlotRestores_.entries[i];
        if (pending.track == trackIndex && pending.slot == slotIndex) {
            return;
        }
    }
    if (pendingLoopSlotRestores_.count >= PendingLoopSlotRestoreQueue::kCapacity) {
        return;
    }
    pendingLoopSlotRestores_.entries[pendingLoopSlotRestores_.count++] = {trackIndex, slotIndex, 3};
}

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

bool writeSetIndexToSd(const SavedSetCatalog::SetIndex& index) {
    File file = SD.open(CurrentSetStorage::kSetIndexTempPath, FILE_WRITE);
    if (!file) {
        return false;
    }
    const StorageIo io = storageIoFromFileWrite(file);
    if (!SavedSetCatalog::writeSetIndex(io, index) ||
        !writeRaw(file, &CurrentSetStorage::kSaveFileToken, sizeof(CurrentSetStorage::kSaveFileToken))) {
        file.close();
        return false;
    }
    file.close();
    if (!CurrentSetStorage::verifySaveFileTokenAtPath(CurrentSetStorage::kSetIndexTempPath)) {
        return false;
    }
    return CurrentSetStorage::atomicRenameTempFile(CurrentSetStorage::kSetIndexTempPath,
                                                   CurrentSetStorage::kSetIndexPath);
}

bool readSetIndexFromSd(SavedSetCatalog::SetIndex& index) {
    if (!SD.exists(CurrentSetStorage::kSetIndexPath)) {
        index.nextSequence = 1;
        return true;
    }

    File file = SD.open(CurrentSetStorage::kSetIndexPath, FILE_READ);
    if (!file) {
        return false;
    }
    const StorageIo io = storageIoFromFileRead(file);
    if (!SavedSetCatalog::readSetIndex(io, index)) {
        file.close();
        return false;
    }
    uint32_t svokToken = 0;
    const bool ok = readRaw(file, &svokToken, sizeof(svokToken)) &&
                    svokToken == CurrentSetStorage::kSaveFileToken;
    file.close();
    return ok;
}

bool parseSavedSetSequence(const char* folderName, uint32_t& sequence) {
    return SavedSetCatalog::parseSavedSetFolderName(folderName, sequence, nullptr);
}

bool formatSavedSetDirectoryPath(const char* folderName, char* out, size_t outSize) {
    if (folderName == nullptr || folderName[0] == '\0' || out == nullptr || outSize == 0) {
        return false;
    }
    const int written = std::snprintf(out, outSize, "%s/%s", CurrentSetStorage::kSetsArchiveDir,
                                      folderName);
    return written > 0 && static_cast<size_t>(written) < outSize;
}

bool resolveSavedSetFolderNameBySequence(uint32_t sequence, char* out, size_t outSize) {
    if (out == nullptr || outSize == 0 || sequence == 0 ||
        !SD.exists(CurrentSetStorage::kSetsArchiveDir)) {
        return false;
    }
    File dir = SD.open(CurrentSetStorage::kSetsArchiveDir);
    if (!dir) {
        return false;
    }
    bool found = false;
    while (true) {
        File entry = dir.openNextFile();
        if (!entry) {
            break;
        }
        const bool isDirectory = entry.isDirectory();
        const char* name = entry.name();
        entry.close();
        uint32_t parsedSequence = 0;
        if (!isDirectory ||
            !SavedSetCatalog::parseSavedSetFolderName(name, parsedSequence, nullptr) ||
            parsedSequence != sequence) {
            continue;
        }
        const char* baseName = std::strrchr(name, '/');
        if (baseName != nullptr) {
            baseName += 1;
        } else {
            baseName = name;
        }
        const int written = std::snprintf(out, outSize, "%s", baseName);
        found = written > 0 && static_cast<size_t>(written) < outSize;
        break;
    }
    dir.close();
    return found;
}

uint32_t scanHighestSavedSetSequenceOnSd() {
    if (!SD.exists(CurrentSetStorage::kSetsArchiveDir)) {
        return 0;
    }
    File dir = SD.open(CurrentSetStorage::kSetsArchiveDir);
    if (!dir) {
        return 0;
    }
    uint32_t highest = 0;
    while (true) {
        File entry = dir.openNextFile();
        if (!entry) {
            break;
        }
        const bool isDirectory = entry.isDirectory();
        const char* name = entry.name();
        uint32_t sequence = 0;
        if (isDirectory && parseSavedSetSequence(name, sequence) && sequence > highest) {
            highest = sequence;
        }
        entry.close();
    }
    dir.close();
    return highest;
}

bool reconcileSetIndexOnSd(SavedSetCatalog::SetIndex& index) {
    if (!readSetIndexFromSd(index)) {
        return false;
    }
    const uint32_t reconciled =
        SavedSetCatalog::reconcileNextSequence(index.nextSequence,
                                               scanHighestSavedSetSequenceOnSd());
    if (reconciled != index.nextSequence || !SD.exists(CurrentSetStorage::kSetIndexPath)) {
        index.nextSequence = reconciled;
        if (!writeSetIndexToSd(index)) {
            return false;
        }
    }
    return true;
}

bool copyFileBinary(const char* sourcePath, const char* destinationPath) {
    File source = SD.open(sourcePath, FILE_READ);
    if (!source) {
        return false;
    }
    File destination = SD.open(destinationPath, FILE_WRITE);
    if (!destination) {
        source.close();
        return false;
    }

    uint8_t buffer[512];
    bool ok = true;
    while (source.available() > 0) {
        const int bytesRead = source.read(buffer, sizeof(buffer));
        if (bytesRead <= 0) {
            ok = false;
            break;
        }
        if (destination.write(buffer, static_cast<size_t>(bytesRead)) !=
            static_cast<size_t>(bytesRead)) {
            ok = false;
            break;
        }
    }
    source.close();
    destination.close();
    return ok;
}

bool buildSavedSetMetadata(uint32_t sequence, SavedSetCatalog::FolderNamingMode namingMode,
                           uint32_t createdAtUnix,
                           SavedSetCatalog::SavedSetMetadata& metadata) {
    metadata = {};
    metadata.sequence = sequence;
    metadata.folderNamingMode = namingMode;
    metadata.createdAtUnix = createdAtUnix;
    metadata.masterLoopBars = static_cast<uint16_t>(
        trackManager.getMasterLoopLength() / Config::TICKS_PER_BAR);

    uint16_t filledTotal = 0;
    uint8_t filledTracks = 0;
    for (uint8_t trackIndex = 0; trackIndex < Config::NUM_TRACKS; ++trackIndex) {
        Track& track = trackManager.getTrack(trackIndex);
        uint8_t filledSlots = 0;
        if (track.loopsAllocated()) {
            for (uint8_t slotIndex = 0; slotIndex < Config::MAX_LOOPS_PER_TRACK; ++slotIndex) {
                if (track.hasDataInSlot(slotIndex)) {
                    ++filledSlots;
                }
            }
        }
        metadata.perTrackFilledSlots[trackIndex] = filledSlots;
        if (filledSlots > 0) {
            ++filledTracks;
            filledTotal += filledSlots;
        }
    }
    metadata.trackCount = filledTracks;
    metadata.filledSlotCount =
        static_cast<uint8_t>(filledTotal > 255 ? 255 : filledTotal);
    metadata.userLabel[0] = '\0';
    return true;
}

bool copyCurrentSetMetaToSavedSet(const char* savedSetDir,
                                  const SavedSetCatalog::SavedSetMetadata& metadata) {
    if (!CurrentSetStorage::verifySaveFileTokenAtPath(CurrentSetStorage::kCurrentMetaPath)) {
        return false;
    }
    File source = SD.open(CurrentSetStorage::kCurrentMetaPath, FILE_READ);
    if (!source) {
        return false;
    }
    const size_t sourceSize = source.size();
    if (sourceSize < sizeof(CurrentSetStorage::kSaveFileToken)) {
        source.close();
        return false;
    }
    const size_t payloadSize = sourceSize - sizeof(CurrentSetStorage::kSaveFileToken);

    char destinationTempPath[kSavedSetPathCapacity];
    char destinationPath[kSavedSetPathCapacity];
    if (std::snprintf(destinationTempPath, sizeof(destinationTempPath), "%s/%s",
                      savedSetDir, CurrentSetStorage::kSetBinTempFileName) <= 0 ||
        std::snprintf(destinationPath, sizeof(destinationPath), "%s/%s", savedSetDir,
                      CurrentSetStorage::kSetBinFileName) <= 0) {
        source.close();
        return false;
    }

    File destination = SD.open(destinationTempPath, FILE_WRITE);
    if (!destination) {
        source.close();
        return false;
    }

    uint8_t buffer[512];
    size_t remaining = payloadSize;
    bool ok = true;
    while (remaining > 0) {
        const size_t chunk = remaining > sizeof(buffer) ? sizeof(buffer) : remaining;
        const int bytesRead = source.read(buffer, chunk);
        if (bytesRead != static_cast<int>(chunk) ||
            destination.write(buffer, chunk) != chunk) {
            ok = false;
            break;
        }
        remaining -= chunk;
    }

    if (ok) {
        const StorageIo destinationIo = storageIoFromFileWrite(destination);
        ok = SavedSetCatalog::writeSavedSetMetadataTrailer(destinationIo, metadata) &&
             writeRaw(destination, &CurrentSetStorage::kSaveFileToken, sizeof(CurrentSetStorage::kSaveFileToken));
    }
    source.close();
    destination.close();
    if (!ok) {
        return false;
    }
    if (!CurrentSetStorage::verifySaveFileTokenAtPath(destinationTempPath)) {
        return false;
    }
    return CurrentSetStorage::atomicRenameTempFile(destinationTempPath, destinationPath);
}

bool copyCurrentSetLoopsToSavedSet(const char* savedSetDir) {
    for (uint8_t trackIndex = 0; trackIndex < Config::NUM_TRACKS; ++trackIndex) {
        for (uint8_t slotIndex = 0; slotIndex < Config::MAX_LOOPS_PER_TRACK; ++slotIndex) {
            char sourcePath[64];
            char destinationPath[kSavedSetPathCapacity];
            if (!CurrentSetStorage::formatLoopSlotPath(sourcePath, sizeof(sourcePath), trackIndex,
                                                       slotIndex) ||
                std::snprintf(destinationPath, sizeof(destinationPath), "%s/loop_%02u_%02u.bin",
                              savedSetDir, static_cast<unsigned>(trackIndex),
                              static_cast<unsigned>(slotIndex)) <= 0) {
                return false;
            }
            if (!copyFileBinary(sourcePath, destinationPath)) {
                return false;
            }
        }
    }
    return true;
}

bool patchCurrentSetAnchor() {
    return CurrentSetStorage::patchAnchorFields(CurrentSetStorage::kCurrentMetaPath,
                                                currentSetAnchorFields);
}

bool saveNewSetInternal(const LooperState& state, char* savedSetFolderOut, size_t outSize) {
    if (!CurrentSetStorage::ensureDirectory(PersistenceLayout::kRoot) ||
        !CurrentSetStorage::ensureDirectory(CurrentSetStorage::kSetsRoot) ||
        !CurrentSetStorage::ensureDirectory(CurrentSetStorage::kSetsArchiveDir) ||
        !CurrentSetStorage::ensureDirectory(CurrentSetStorage::kCurrentSetDir) ||
        !CurrentSetStorage::ensureDirectory(CurrentWorkspaceStorage::kCurrentTempDir) ||
        !CurrentSetStorage::ensureDirectory(CurrentSetStorage::kCurrentSlotsDir)) {
        return false;
    }
    if (!SD.exists(CurrentSetStorage::kCurrentMetaPath)) {
        return false;
    }
    if (StorageManager::hasDeferredSaveWork() && !StorageManager::saveState(state)) {
        return false;
    }

    SavedSetCatalog::SetIndex index{};
    if (!reconcileSetIndexOnSd(index)) {
        return false;
    }

    const uint32_t createdAtUnix = RtcTime::getUnixTime();
    const bool hasValidDateFolder = RtcTime::hasValidDateForFolderNaming();
    const uint32_t sequence = SavedSetCatalog::allocateNextSequence(index);
    SavedSetCatalog::FolderNamingMode namingMode = SavedSetCatalog::FolderNamingMode::Unknown;
    char folderName[16];
    if (!SavedSetCatalog::formatSavedSetFolderName(sequence, createdAtUnix, hasValidDateFolder,
                                                   folderName, sizeof(folderName), &namingMode)) {
        return false;
    }
    char savedSetDir[kSavedSetPathCapacity];
    if (!formatSavedSetDirectoryPath(folderName, savedSetDir, sizeof(savedSetDir))) {
        return false;
    }
    if (SD.exists(savedSetDir) || !CurrentSetStorage::ensureDirectory(savedSetDir)) {
        return false;
    }

    SavedSetCatalog::SavedSetMetadata metadata{};
    if (!buildSavedSetMetadata(sequence, namingMode, createdAtUnix, metadata) ||
        !copyCurrentSetMetaToSavedSet(savedSetDir, metadata) ||
        !copyCurrentSetLoopsToSavedSet(savedSetDir) ||
        !writeSetIndexToSd(index)) {
        return false;
    }

    currentSetAnchorFields.lastAnchoredSequence = sequence;
    currentSetAnchorFields.hasMaterialChangesSinceAnchor = 0;
    if (!patchCurrentSetAnchor()) {
        return false;
    }

    if (savedSetFolderOut != nullptr && outSize > 0) {
        std::snprintf(savedSetFolderOut, outSize, "%s", folderName);
    }
    return true;
}

bool copySavedSetIntoCurrent(const char* sourceSetDir) {
    if (!CurrentSetStorage::ensureDirectory(PersistenceLayout::kRoot) ||
        !CurrentSetStorage::ensureDirectory(CurrentSetStorage::kCurrentSetDir) ||
        !CurrentSetStorage::ensureDirectory(CurrentWorkspaceStorage::kCurrentTempDir) ||
        !CurrentSetStorage::ensureDirectory(CurrentSetStorage::kCurrentSlotsDir)) {
        return false;
    }

    char sourceMetaPath[kSavedSetPathCapacity];
    if (std::snprintf(sourceMetaPath, sizeof(sourceMetaPath), "%s/%s", sourceSetDir,
                      CurrentSetStorage::kSetBinFileName) <= 0) {
        return false;
    }
    if (!copyFileBinary(sourceMetaPath, CurrentSetStorage::kCurrentMetaTempPath)) {
        return false;
    }
    if (!CurrentSetStorage::verifySaveFileTokenAtPath(CurrentSetStorage::kCurrentMetaTempPath) ||
        !CurrentSetStorage::atomicRenameTempFile(CurrentSetStorage::kCurrentMetaTempPath,
                                                 CurrentSetStorage::kCurrentMetaPath)) {
        return false;
    }

    for (uint8_t trackIndex = 0; trackIndex < Config::NUM_TRACKS; ++trackIndex) {
        for (uint8_t slotIndex = 0; slotIndex < Config::MAX_LOOPS_PER_TRACK; ++slotIndex) {
            char sourceLoopPath[kSavedSetPathCapacity];
            char destinationLoopPath[64];
            char destinationTempPath[68];
            if (std::snprintf(sourceLoopPath, sizeof(sourceLoopPath), "%s/loop_%02u_%02u.bin",
                              sourceSetDir, static_cast<unsigned>(trackIndex),
                              static_cast<unsigned>(slotIndex)) <= 0 ||
                !CurrentSetStorage::formatLoopSlotPath(destinationLoopPath,
                                                       sizeof(destinationLoopPath), trackIndex,
                                                       slotIndex) ||
                !CurrentSetStorage::formatLoopSlotTempPath(destinationTempPath,
                                                           sizeof(destinationTempPath), trackIndex,
                                                           slotIndex)) {
                return false;
            }
            if (!copyFileBinary(sourceLoopPath, destinationTempPath) ||
                !CurrentSetStorage::verifySaveFileTokenAtPath(destinationTempPath) ||
                !CurrentSetStorage::atomicRenameTempFile(destinationTempPath,
                                                         destinationLoopPath)) {
                return false;
            }
        }
    }
    return true;
}


}  // namespace


#if defined(SESSION_CAPTURE)
namespace StorageManagerInternal {

CAPTURE_HITL_DATA HitlRevisionCommitBackup hitlRevisionCommitBackup{};

}  // namespace StorageManagerInternal
#endif


namespace {

void loadWorkspaceMetaCountersFromSd() {
    CurrentWorkspaceStorage::WorkspaceMetaRecord record{};
    if (!CurrentWorkspaceStorage::readWorkspaceMetaFile(record)) {
        return;
    }
    currentWorkspaceEpoch = record.currentEpoch;
    lastCommittedWorkspaceEpoch = record.lastCommittedEpoch;
    workspaceDerivedFromSetId = record.derivedFromSetId;
    workspaceDerivedFromRevisionId = record.derivedFromRevisionId;
    workspaceLastCommittedRevisionId = record.lastCommittedRevisionId;
}


}  // namespace




bool parseRevisionSetFolderEntryName(const char* name, uint16_t& setIdOut);

void StorageManager::requestUrgentEditSave() {
#if BYPASS_STOP_UNDO_SAVE
    return;
#endif
    urgentEditSavePending = true;
}

bool StorageManager::saveNewSet(char* savedSetFolderOut, size_t outSize) {
    return saveNewSetInternal(looperState.getLooperState(), savedSetFolderOut, outSize);
}

bool StorageManager::loadSetIntoCurrent(const char* savedSetFolderName) {
    uint32_t sourceSequence = 0;
    if (!SavedSetCatalog::parseSavedSetFolderName(savedSetFolderName, sourceSequence, nullptr)) {
        return false;
    }
    char sourceSetDir[kSavedSetPathCapacity];
    if (!formatSavedSetDirectoryPath(savedSetFolderName, sourceSetDir,
                                     sizeof(sourceSetDir)) ||
        !SD.exists(sourceSetDir)) {
        return false;
    }

    char autoSavedFolderName[16] = {};
    const bool shouldAutoSave =
        CurrentSetStorage::shouldAutoSaveBeforeLoadIntoCurrent(currentSetAnchorFields);
    if (shouldAutoSave &&
        !saveNewSetInternal(looperState.getLooperState(), autoSavedFolderName,
                            sizeof(autoSavedFolderName))) {
        return false;
    }

    if (!copySavedSetIntoCurrent(sourceSetDir) ||
        !loadCurrentSetFromDirectory(CurrentSetStorage::kCurrentSetDir,
                                     looperState.getLooperState())) {
        return false;
    }

    forceCurrentSetFullLoopWrite = false;
    syncCurrentSetDirtyTrackingFromLoadedState();
    setCurrentSetLoadedFromFolder(savedSetFolderName);
    CurrentSetStorage::applyLoadedSetAnchorFields(sourceSequence, currentSetAnchorFields);
    if (!patchCurrentSetAnchor()) {
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
        if (!isDirectory || !parseSavedSetSequence(name, sequence) || sequence == 0) {
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

void stepWallClockFromSdCatalogSync(uint8_t maxSetsPerSlice) {
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
    char metaPath[kSavedSetPathCapacity];
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
    if (!buildSavedSetMetadata(sequence != 0 ? sequence : 1,
                               SavedSetCatalog::FolderNamingMode::Unknown,
                               currentSetLastActiveUnix, metadata)) {
        return false;
    }
    metadata.sequence = sequence;
    metadata.createdAtUnix = currentSetLastActiveUnix;
    metadata.userLabel[0] = '\0';
    return true;
}

void StorageManager::markCurrentSetLoopSlotDirty(uint8_t trackIndex, uint8_t slotIndex) {
#if BYPASS_STOP_UNDO_SAVE
    (void)trackIndex;
    (void)slotIndex;
    return;
#endif
    markCurrentSetLoopSlotDirtyInternal(trackIndex, slotIndex);
}

void StorageManager::markCurrentSetTrackDirty(uint8_t trackIndex) {
#if BYPASS_STOP_UNDO_SAVE
    (void)trackIndex;
    return;
#endif
    markCurrentSetTrackDirtyInternal(trackIndex);
}

void StorageManager::markAllCurrentSetLoopSlotsDirty() {
#if BYPASS_STOP_UNDO_SAVE
    return;
#endif
    markAllCurrentSetLoopSlotsDirtyInternal();
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
        for (uint8_t t = 0; t < trackManager.getTrackCount(); ++t) {
            Track& track = trackManager.getTrack(t);
            if (!track.loopsAllocated()) {
                continue;
            }
            for (uint8_t s = 0; s < Config::MAX_LOOPS_PER_TRACK; ++s) {
                if (track.getLoop(s).isEditStateDirty()) {
                    markCurrentSetLoopSlotDirtyInternal(t, s);
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
    for (uint8_t t = 0; t < trackManager.getTrackCount(); ++t) {
        Track& track = trackManager.getTrack(t);
        if (!track.loopsAllocated()) {
            continue;
        }
        for (uint8_t s = 0; s < Config::MAX_LOOPS_PER_TRACK; ++s) {
            if (track.getLoop(s).isEditStateDirty()) {
                markCurrentSetLoopSlotDirtyInternal(t, s);
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

bool StorageManager::isDeferredSaveActive() {
#if BYPASS_STOP_UNDO_SAVE
    return false;
#else
    // Active only during the SD-write section of a save slice.
    return storageSession.currentWorkspaceSave.sdIoActive;
#endif
}

bool StorageManager::hasDeferredSaveWork() {
#if BYPASS_STOP_UNDO_SAVE
    return false;
#else
    return storageSession.currentWorkspaceSave.pending || storageSession.currentWorkspaceSave.inProgress;
#endif
}

bool StorageManager::hasPendingLoopSlotRestore() {
    return pendingLoopSlotRestores_.count > 0;
}

bool StorageManager::hasPendingUndoSnapshotHydrate() {
    return undoSnapshotsPending_;
}

void StorageManager::processDeferredSaveState(const LooperState& state) {
#if BYPASS_STOP_UNDO_SAVE
    (void)state;
    storageSession.currentWorkspaceSave.pending = false;
    resetDeferredSaveJobState();
    resetRevisionCommitJobState();
    resetRevisionLoadJobState();
    return;
#endif
    storageSession.currentWorkspaceSave.sdIoActive = false;
    storageSession.revisionCommit.sdIoActive = false;
    storageSession.revisionLoad.sdIoActive = false;

    const bool captureActiveForScheduler = isCaptureActiveForPersistence();
    const uint32_t sliceBudgetUs = resolvePersistenceSliceBudgetUs(state);
    const uint32_t sliceStartUs = micros();

    auto sliceBudgetExhausted = [&]() {
        return PersistenceBudget::persistenceSliceBudgetExhausted(sliceBudgetUs,
                                                                  micros() - sliceStartUs);
    };

    if (storageSession.bootRecovery.pending && !storageSession.revisionLoad.pending && !storageSession.revisionLoad.inProgress &&
        !storageSession.revisionCommit.pending && !storageSession.revisionCommit.inProgress && !storageSession.currentWorkspaceSave.pending &&
        !storageSession.currentWorkspaceSave.inProgress) {
        storageSession.bootRecovery.pending = false;
        storageSession.revisionLoad.setId = storageSession.bootRecovery.setId;
        storageSession.revisionLoad.revisionId = storageSession.bootRecovery.revisionId;
        storageSession.revisionLoad.pending = true;
        Serial.print("[StorageManager] Boot recovery: queued revision load S");
        Serial.print(storageSession.revisionLoad.setId);
        Serial.print(" v");
        Serial.println(storageSession.revisionLoad.revisionId);
    }

    stepWallClockFromSdCatalogSync(2);

    while (!sliceBudgetExhausted()) {
        const bool revisionBlockedByDeferredSave =
            storageSession.revisionCommit.pending && (storageSession.currentWorkspaceSave.pending || storageSession.currentWorkspaceSave.inProgress);
        const bool revisionBlockedByLoad =
            storageSession.revisionCommit.pending && (storageSession.revisionLoad.pending || storageSession.revisionLoad.inProgress);
        if (revisionBlockedByDeferredSave || revisionBlockedByLoad) {
            const uint32_t nowMs = millis();
            if (nowMs - storageSession.revisionCommit.lastBlockedLogAtMs >= 5000U) {
                storageSession.revisionCommit.lastBlockedLogAtMs = nowMs;
                SC_PERSIST("rev_blocked", 0, storageSession.currentWorkspaceSave.pending ? 1U : 0U,
                           storageSession.currentWorkspaceSave.inProgress ? 1U : 0U,
                           revisionBlockedByLoad ? "load_active" : "deferred_save_active");
            }
        } else if (storageSession.revisionCommit.inProgress || storageSession.revisionCommit.pending) {
            if (!storageSession.revisionCommit.inProgress && storageSession.revisionCommit.pending) {
                storageSession.revisionCommit.pending = false;
                storageSession.revisionCommit.inProgress = true;
                storageSession.revisionCommit.stage = RevisionCommitStage::Snapshot;
                SC_PERSIST("rev_dispatch", 0, 0, 0, "run");
            }

            const uint32_t ioStartUs = micros();
            storageSession.revisionCommit.sdIoActive = true;
            const bool revStepOk = stepRevisionCommitJob();
            storageSession.revisionCommit.sdIoActive = false;
            storageSession.currentWorkspaceSave.displayBlockUs += micros() - ioStartUs;

            if (!revStepOk) {
                Serial.print("[StorageManager] ERROR: Revision commit failed at stage ");
                Serial.println(static_cast<uint8_t>(storageSession.revisionCommit.stage));
                resetRevisionCommitJobState();
                break;
            }
            if (storageSession.revisionCommit.inProgress) {
                break;
            }
            if (RevisionLoadPolicy::shouldDispatchRequestedLoadAfterCommitComplete(
                    storageSession.revisionLoad.loadAfterRevisionCommit, true)) {
                storageSession.revisionLoad.loadAfterRevisionCommit = false;
                dispatchRequestedRevisionLoad();
            }
            if (captureActiveForScheduler) {
                break;
            }
            continue;
        }

        const bool revisionLoadBlockedByDeferredSave =
            storageSession.revisionLoad.pending && (storageSession.currentWorkspaceSave.pending || storageSession.currentWorkspaceSave.inProgress);
        const bool revisionLoadBlockedByCommit =
            storageSession.revisionLoad.pending && (storageSession.revisionCommit.pending || storageSession.revisionCommit.inProgress);
        if (revisionLoadBlockedByDeferredSave || revisionLoadBlockedByCommit) {
            const uint32_t nowMs = millis();
            if (nowMs - storageSession.revisionLoad.lastBlockedLogAtMs >= 5000U) {
                storageSession.revisionLoad.lastBlockedLogAtMs = nowMs;
                SC_PERSIST("rev_load_blocked", 0,
                           revisionLoadBlockedByDeferredSave ? 1U : 0U,
                           revisionLoadBlockedByCommit ? 1U : 0U,
                           revisionLoadBlockedByDeferredSave ? "deferred_save_active"
                                                               : "commit_active");
            }
        } else if (storageSession.revisionLoad.inProgress || storageSession.revisionLoad.pending) {
            if (!storageSession.revisionLoad.inProgress && storageSession.revisionLoad.pending) {
                storageSession.revisionLoad.pending = false;
                storageSession.revisionLoad.inProgress = true;
                storageSession.revisionLoad.stage = RevisionLoadStage::Validate;
                SC_PERSIST("rev_load_dispatch", 0, storageSession.revisionLoad.setId, storageSession.revisionLoad.revisionId,
                           "run");
            }

            const uint32_t ioStartUs = micros();
            storageSession.revisionLoad.sdIoActive = true;
            const bool revLoadStepOk =
                stepRevisionLoadJob(const_cast<LooperState&>(state));
            storageSession.revisionLoad.sdIoActive = false;
            storageSession.currentWorkspaceSave.displayBlockUs += micros() - ioStartUs;

            if (!revLoadStepOk) {
                Serial.print("[StorageManager] ERROR: Revision load failed at stage ");
                Serial.println(static_cast<uint8_t>(storageSession.revisionLoad.stage));
                storageSession.revisionLoad.lastDisplaySetId =
                    storageSession.revisionLoad.setId != 0 ? storageSession.revisionLoad.setId : storageSession.revisionLoad.requestedSetId;
                storageSession.revisionLoad.lastDisplayRevisionId = storageSession.revisionLoad.revisionId != 0
                                                          ? storageSession.revisionLoad.revisionId
                                                          : storageSession.revisionLoad.requestedRevisionId;
                storageSession.revisionLoad.failedAtMs = millis();
                storageSession.revisionLoad.completedAtMs = 0;
                resetRevisionLoadJobState();
                break;
            }
            if (storageSession.revisionLoad.inProgress) {
                break;
            }
            if (captureActiveForScheduler) {
                break;
            }
            continue;
        }

        const bool otherSdIoActive =
            storageSession.currentWorkspaceSave.sdIoActive || storageSession.revisionCommit.sdIoActive ||
            storageSession.revisionLoad.sdIoActive || storageSession.midPassChunkPersist.sdIoActive;
        if (PersistenceFailurePolicy::shouldRunMidPassWriter(PersistenceQueue::queueDepth(),
                                                               otherSdIoActive)) {
            const uint32_t currentHeap = MemoryMonitor::getInternalHeapFreeBytes();
            if (!LoopEventStore::hasInternalHeapHeadroomForNonCriticalWork(currentHeap)) {
                PersistenceDiagnostics::onHeapFloorBlock();
                break;
            }
            PersistenceFailurePolicy::maybeEmitBackpressureTelemetry(captureActiveForScheduler);
            const uint32_t ioStartUs = micros();
            storageSession.midPassChunkPersist.sdIoActive = true;
            const bool midOk = stepMidPassChunkPersist();
            const uint32_t sliceLatencyUs = micros() - ioStartUs;
            storageSession.midPassChunkPersist.sdIoActive = false;
            PersistenceDiagnostics::onSliceCompleted(sliceLatencyUs);
            if (!midOk) {
                SC_PERSIST("mid_pass", sliceLatencyUs, 0, 0, "failed");
                break;
            }
            break;
        }

        if (!storageSession.currentWorkspaceSave.pending && !storageSession.currentWorkspaceSave.inProgress) {
            break;
        }

        if (!storageSession.currentWorkspaceSave.inProgress) {
            const uint32_t currentHeap = MemoryMonitor::getInternalHeapFreeBytes();
            if (!LoopEventStore::hasInternalHeapHeadroomForNonCriticalWork(currentHeap)) {
                if (!storageSession.currentWorkspaceSave.heapFloorDeferred) {
                    const uint32_t admissionHeap =
                        storageSession.currentWorkspaceSave.admissionHeap == UINT32_MAX
                            ? currentHeap
                            : storageSession.currentWorkspaceSave.admissionHeap;
                    SC_PERSIST("defer", 0, admissionHeap, currentHeap, "heap_floor");
                    storageSession.currentWorkspaceSave.heapFloorDeferred = true;
                }
                PersistenceDiagnostics::onHeapFloorBlock();
                break;
            }
            storageSession.currentWorkspaceSave.heapFloorDeferred = false;
        }

        if (!storageSession.currentWorkspaceSave.inProgress && storageSession.currentWorkspaceSave.pending) {
            const uint32_t nowMs = millis();
            if (isTransportActiveForPersistence() &&
                nowMs < storageSession.currentWorkspaceSave.deferDispatchUntilMs) {
                break;
            }
            const uint32_t dispatchHeap = MemoryMonitor::getInternalHeapFreeBytes();
            const uint32_t admissionHeap =
                storageSession.currentWorkspaceSave.admissionHeap == UINT32_MAX ? dispatchHeap
                                                                                : storageSession.currentWorkspaceSave.admissionHeap;
            SC_PERSIST("dispatch", 0, admissionHeap, dispatchHeap, "run");
            storageSession.currentWorkspaceSave.pending = false;
            storageSession.currentWorkspaceSave.startedAtUs = micros();
            storageSession.currentWorkspaceSave.heapBefore = dispatchHeap;
            storageSession.currentWorkspaceSave.loopSlotsWritten = 0;
            storageSession.currentWorkspaceSave.loopSlotsSkipped = 0;
            storageSession.currentWorkspaceSave.displayBlockUs = 0;
            storageSession.currentWorkspaceSave.inProgress = true;
            const uint32_t ioStartUs = micros();
            storageSession.currentWorkspaceSave.sdIoActive = true;
            const bool beginOk = beginDeferredSaveJob(state);
            storageSession.currentWorkspaceSave.displayBlockUs += micros() - ioStartUs;
            storageSession.currentWorkspaceSave.sdIoActive = false;
            if (!beginOk) {
                const uint32_t saveDurationUs = micros() - storageSession.currentWorkspaceSave.startedAtUs;
                SC_PERSIST("result", saveDurationUs, storageSession.currentWorkspaceSave.heapBefore, storageSession.currentWorkspaceSave.heapBefore,
                           "failed");
                storageSession.currentWorkspaceSave.lastCompletedOk = false;
                storageSession.currentWorkspaceSave.failedAtMs = millis();
                storageSession.currentWorkspaceSave.completedAtMs = 0;
                resetDeferredSaveJobState();
                break;
            }
            storageSession.currentWorkspaceSave.urgentRequested = false;
            break;
        }

        if (!storageSession.currentWorkspaceSave.inProgress) {
            break;
        }

        emitDeferredSaveSliceTelemetry("start");
        const uint32_t ioStartUs = micros();
        storageSession.currentWorkspaceSave.sdIoActive = true;
        const bool stepOk = stepDeferredSaveJob();
        const uint32_t sliceLatencyUs = micros() - ioStartUs;
        storageSession.currentWorkspaceSave.displayBlockUs += sliceLatencyUs;
        storageSession.currentWorkspaceSave.sdIoActive = false;
        emitDeferredSaveSliceTelemetry(stepOk ? "done" : "failed");
        PersistenceDiagnostics::onSliceCompleted(sliceLatencyUs);
        if (!stepOk) {
            storageSession.currentWorkspaceSave.inProgress = false;
        }
        if (storageSession.currentWorkspaceSave.inProgress) {
            break;
        }

        const uint32_t saveDurationUs = micros() - storageSession.currentWorkspaceSave.startedAtUs;
        const char* resultOutcome = stepOk ? "ok" : "failed";
        SC_PERSIST("result", saveDurationUs, storageSession.currentWorkspaceSave.heapBefore, storageSession.currentWorkspaceSave.heapBefore,
                   resultOutcome);
        char resultStats[72];
        std::snprintf(resultStats, sizeof(resultStats), "w%u_s%u_db%lu",
                      static_cast<unsigned>(storageSession.currentWorkspaceSave.loopSlotsWritten),
                      static_cast<unsigned>(storageSession.currentWorkspaceSave.loopSlotsSkipped),
                      static_cast<unsigned long>(storageSession.currentWorkspaceSave.displayBlockUs));
        SC_PERSIST("result_stats", saveDurationUs, storageSession.currentWorkspaceSave.heapBefore, storageSession.currentWorkspaceSave.heapBefore,
                   resultStats);
        storageSession.currentWorkspaceSave.lastCompletedOk = stepOk;
        const uint32_t resultAtMs = millis();
        if (stepOk) {
            storageSession.currentWorkspaceSave.completedAtMs = resultAtMs;
            storageSession.currentWorkspaceSave.failedAtMs = 0;
        } else {
            storageSession.currentWorkspaceSave.failedAtMs = resultAtMs;
            storageSession.currentWorkspaceSave.completedAtMs = 0;
        }
        if (stepOk && clearEditDirtyAfterDeferredSave) {
            clearAllocatedLoopEditStateDirty();
            clearEditDirtyAfterDeferredSave = false;
        }
        storageSession.currentWorkspaceSave.urgentRequested = false;
        if (!stepOk) {
            resetDeferredSaveJobState();
        }
        break;
    }

#if defined(SESSION_CAPTURE)
    if ((storageSession.currentWorkspaceSave.pending || storageSession.currentWorkspaceSave.inProgress) &&
        PersistenceBudget::persistenceSliceBudgetExhausted(sliceBudgetUs, micros() - sliceStartUs)) {
        PersistenceDiagnostics::onBudgetBlock();
    }
    PersistenceDiagnostics::maybeEmitPeriodic(
        isCaptureActiveForPersistence(), storageSession.currentWorkspaceSave.pending,
        storageSession.currentWorkspaceSave.inProgress, storageSession.currentWorkspaceSave.sdIoActive);
#endif
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
    inputs.savePending = storageSession.currentWorkspaceSave.pending;
    inputs.saveInProgress = storageSession.currentWorkspaceSave.inProgress;
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
    Serial.println("[StorageManager] Draining deferred save to SD card...");

    if (!storageSession.currentWorkspaceSave.inProgress) {
        storageSession.currentWorkspaceSave.admissionHeap = UINT32_MAX;
        storageSession.currentWorkspaceSave.urgentRequested = true;
        const bool alreadyPending = storageSession.currentWorkspaceSave.pending;
        storageSession.currentWorkspaceSave.pending = true;
        SC_PERSIST("request", 0, 0, 0,
                   alreadyPending ? "sync_drain_already_pending" : "sync_drain");
    }

    storageSession.currentWorkspaceSave.lastCompletedOk = false;
    constexpr uint32_t kMaxDrainSteps = 200000u;
    uint32_t steps = 0;
    while ((storageSession.currentWorkspaceSave.pending || storageSession.currentWorkspaceSave.inProgress) && steps < kMaxDrainSteps) {
        processDeferredSaveState(state);
        yield();
        steps++;
    }

    const bool completed = !storageSession.currentWorkspaceSave.pending && !storageSession.currentWorkspaceSave.inProgress;
    if (!completed) {
        Serial.println("[StorageManager] ERROR: Deferred save drain exceeded step limit");
        return false;
    }
    if (!storageSession.currentWorkspaceSave.lastCompletedOk) {
        Serial.println("[StorageManager] ERROR: Deferred save drain failed");
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

static void resetLoopSlotForBootManifest(Loop& loop, uint8_t slotIndex) {
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
    loop.lastPublishedPassId_ = kInvalidPassId;
    loop.lastTickInLoop = 0;
    loop.nextEventIndex = 0;
    loop.clearEditStateDirty();
}

static void resetLoopSlotToEmpty(Loop& loop, uint8_t slotIndex) {
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
    loop.lastPublishedPassId_ = kInvalidPassId;
    loop.lastTickInLoop = 0;
    loop.nextEventIndex = 0;
    loop.clearEditStateDirty();
    loop.visualCache.clear();
    loop.capturePreview.clear();
    loop.pendingVisualDelta.clear();
    loop.invalidateCaches();
}

void resetTracksAfterFailedLoad() {
    trackManager.setMasterLoopLength(0);
    for (uint8_t t = 0; t < trackManager.getTrackCount(); ++t) {
        Track& track = trackManager.getTrack(t);
        track.ensureLoopsAllocated();
        track.forceSetState(TRACK_EMPTY);
        track.getGlobalUndoStack().clear();
        trackManager.loadTransportSlotIndices(t, 0, 0);
        for (uint8_t s = 0; s < Config::MAX_LOOPS_PER_TRACK; ++s) {
            trackManager.setSlotEnabled(t, s, false);
            trackManager.setSlotMuted(t, s, false);
            resetLoopSlotToEmpty(track.getLoop(s), s);
        }
    }
    trackManager.setSelectedTrack(0);
}

static STORAGE_PERSIST_MEM void stabilizeBootMemoryAfterLoad() {
    uint32_t freeHeap = MemoryMonitor::getInternalHeapFreeBytes();
    if (freeHeap < Config::HEAP_RESERVE_BYTES) {
        Serial.print("[StorageManager] Boot heap below reserve after load (");
        Serial.print(freeHeap);
        Serial.println(" B); clearing undo stacks");
        for (uint8_t t = 0; t < trackManager.getTrackCount(); ++t) {
            trackManager.getTrack(t).getGlobalUndoStack().clear();
        }
        freeHeap = MemoryMonitor::getInternalHeapFreeBytes();
    }

    if (freeHeap < Config::HEAP_RESERVE_BYTES) {
        Serial.print("[StorageManager] WARN: boot heap still low (");
        Serial.print(freeHeap);
        Serial.println(" B); skipping playback prewarm");
        return;
    }

    trackManager.prewarmPlaybackRuntime();
}

static STORAGE_PERSIST_MEM bool readLoopFromCurrentSetFile(File& file, Loop& loop) {
    const size_t fileSize = file.size();
    if (fileSize < sizeof(CurrentSetStorage::kSaveFileToken)) {
        return false;
    }
    size_t payloadOffset = 0;
    if (CurrentWorkspaceStorage::fileStartsWithEpochHeader(file)) {
        CurrentWorkspaceStorage::EpochFileHeader epochHeader{};
        const StorageIo epochIo = storageIoFromFileRead(file);
        if (!CurrentWorkspaceStorage::readEpochFileHeader(epochIo, epochHeader)) {
            return false;
        }
        payloadOffset = CurrentWorkspaceStorage::kEpochFileHeaderByteSize;
    }
    if (fileSize < payloadOffset + sizeof(CurrentSetStorage::kSaveFileToken)) {
        return false;
    }
    const size_t payloadSize = fileSize - payloadOffset - sizeof(CurrentSetStorage::kSaveFileToken);
    if (!file.seek(payloadOffset)) {
        return false;
    }

    class BoundedFileIo {
     public:
        BoundedFileIo(File& f, size_t limit) : file_(f), limit_(limit), pos_(0) {}

        StorageIo io() {
            return StorageIo{
                [this](const void*, size_t) { return false; },
                [this](void* data, size_t size) { return read(data, size); },
            };
        }

     private:
        bool read(void* data, size_t size) {
            if (pos_ + size > limit_) {
                return false;
            }
            const int bytesRead = file_.read(static_cast<uint8_t*>(data), size);
            if (bytesRead != static_cast<int>(size)) {
                return false;
            }
            pos_ += size;
            return true;
        }

        File& file_;
        size_t limit_;
        size_t pos_;
    };

    BoundedFileIo bounded(file, payloadSize);
    PersistedLoopSnapshot snapshot{};
    if (!readPersistedLoopSnapshot(bounded.io(), snapshot, false)) {
        if (!file.seek(payloadOffset)) {
            Serial.println("[StorageManager] ERROR: readLoopPersisted seek retry failed");
            return false;
        }
        BoundedFileIo legacyBounded(file, payloadSize);
        if (!readPersistedLoopSnapshot(legacyBounded.io(), snapshot, true)) {
            Serial.println("[StorageManager] ERROR: readLoopPersisted failed for current loop file");
            return false;
        }
        Serial.println("[StorageManager] WARN: loop slot read used legacy deferred header (no nextNoteId)");
    }
    applySnapshotToLoop(loop, snapshot);
    uint32_t magic = 0;
    return readRaw(file, &magic, sizeof(magic)) && magic == CurrentSetStorage::kSaveFileToken;
}

bool STORAGE_PERSIST_MEM loadLoopSlotFromCurrentSetSd(uint8_t trackIndex, uint8_t slotIndex, Track& track,
                                         bool& anySlotHasEventsOut) {
    char loopPath[64];
    if (!CurrentSetStorage::formatLoopSlotPath(loopPath, sizeof(loopPath), trackIndex, slotIndex)) {
        return false;
    }
    Loop& loop = track.getLoop(slotIndex);
    if (!SD.exists(loopPath)) {
        resetLoopSlotToEmpty(loop, slotIndex);
        return true;
    }
    if (!CurrentSetStorage::verifySaveFileTokenAtPath(loopPath)) {
        Serial.print("[StorageManager] WARN: loop file incomplete, treating slot as empty ");
        Serial.println(loopPath);
        resetLoopSlotToEmpty(loop, slotIndex);
        return true;
    }
    File loopFile = SD.open(loopPath, FILE_READ);
    if (!loopFile) {
        Serial.print("[StorageManager] WARN: could not open loop file, treating slot as empty ");
        Serial.println(loopPath);
        resetLoopSlotToEmpty(loop, slotIndex);
        return true;
    }
    const bool readOk = readLoopFromCurrentSetFile(loopFile, loop);
    loopFile.close();
    if (!readOk) {
        Serial.print("[StorageManager] WARN: loop read failed, treating slot as empty ");
        Serial.println(loopPath);
        resetLoopSlotToEmpty(loop, slotIndex);
        return true;
    }
    if (loop.hasPublishedEvents()) {
        anySlotHasEventsOut = true;
    }
    return true;
}

bool readCurrentSetTrackSlotMetadata(File& file, uint8_t trackIndex, Track& track,
                                            TrackState& loadedTrackStateOut, bool& mutedOut) {
    track.ensureLoopsAllocated();

    uint32_t trackStateRaw = 0;
    if (!readRaw(file, &trackStateRaw, sizeof(trackStateRaw))) {
        return false;
    }
    loadedTrackStateOut = static_cast<TrackState>(trackStateRaw);

    bool muted = false;
    if (!readRaw(file, &muted, sizeof(muted))) {
        return false;
    }
    mutedOut = muted;

    for (uint8_t s = 0; s < Config::MAX_LOOPS_PER_TRACK; ++s) {
        bool slotEnabled = false;
        bool slotMuted = false;
        LoopId slotLoopId = kInvalidLoopId;
        if (!readRaw(file, &slotEnabled, sizeof(slotEnabled)) ||
            !readRaw(file, &slotMuted, sizeof(slotMuted)) ||
            !readRaw(file, &slotLoopId, sizeof(slotLoopId))) {
            return false;
        }
        if (slotLoopId == kInvalidLoopId || slotLoopId >= Config::MAX_LOOPS_PER_TRACK) {
            slotLoopId = static_cast<LoopId>(s);
        }
        trackManager.setSlotEnabled(trackIndex, s, slotEnabled);
        trackManager.setSlotMuted(trackIndex, s, slotMuted);
        track.getLoop(s).loopId = slotLoopId;
    }
    return true;
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
        if (!resolveSavedSetFolderNameBySequence(currentSetAnchorFields.loadedFromSequence,
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

bool STORAGE_PERSIST_MEM applyLoadedTransportFooter(uint8_t numTracks, const std::vector<uint8_t>& activeLoopIndex,
                                       const std::vector<uint8_t>& selectedSlotIndex,
                                       uint8_t selectedTrackIdx, LooperState& state,
                                       LooperState loadedLooperState, uint32_t masterLoopLength) {
    state = loadedLooperState;
    trackManager.setMasterLoopLength(masterLoopLength);
    for (uint8_t t = 0; t < numTracks; ++t) {
        const uint8_t activeSlot = t < activeLoopIndex.size() ? activeLoopIndex[t] : 0;
        const uint8_t selectedSlot =
            t < selectedSlotIndex.size() ? selectedSlotIndex[t] : activeSlot;
        trackManager.loadTransportSlotIndices(t, activeSlot, selectedSlot);
    }
    if (selectedTrackIdx < Config::NUM_TRACKS) {
        trackManager.setSelectedTrack(selectedTrackIdx);
    } else {
        trackManager.setSelectedTrack(0);
    }
    stabilizeBootMemoryAfterLoad();
    return true;
}

}  // namespace StorageManagerInternal

bool StorageManager::loadCurrentSetBundleAndActiveLoopSlots(File& file, const char* setDir, LooperState& state,
                                        std::vector<uint8_t>& activeLoopIndex,
                                        uint8_t& selectedTrackIdx) {
    (void)setDir;
    pendingLoopSlotRestores_.count = 0;
    undoSnapshotsPending_ = false;
    undoHydrateTrackIndex_ = 0;
    undoStackFileOffsets_.fill(0);

    LooperState loadedLooperState = LOOPER_IDLE;
    uint32_t masterLoopLength = 0;
    uint8_t numTracks = 0;
    if (!readCurrentSetFilePreamble(file, loadedLooperState, masterLoopLength, numTracks)) {
        return false;
    }
    if (numTracks > Config::NUM_TRACKS) {
        return false;
    }

    struct LoadedTrackHeader {
        TrackState state = TRACK_EMPTY;
        bool muted = false;
    };
    LoadedTrackHeader trackHeaders[Config::NUM_TRACKS]{};

    for (uint8_t t = 0; t < numTracks; ++t) {
        Track& track = trackManager.getTrack(t);
        if (!readCurrentSetTrackSlotMetadata(file, t, track, trackHeaders[t].state, trackHeaders[t].muted)) {
            return false;
        }
    }

    activeLoopIndex.assign(numTracks, 0);
    std::vector<uint8_t> selectedSlotIndex;
    if (!readCurrentSetFileEpilogue(file, numTracks, activeLoopIndex, selectedSlotIndex, selectedTrackIdx,
                                    true)) {
        return false;
    }

    file.close();

    Serial.println("[StorageManager] Scanning loop slot manifests on SD...");
    {
        char heapDetail[16];
        snprintf(heapDetail, sizeof(heapDetail), "%lu",
                 static_cast<unsigned long>(MemoryMonitor::getInternalHeapFreeBytes()));
        emitBootMilestone("ram1", heapDetail);
    }
    emitBootMilestone("scan", "start");

    // Boot restore pipeline: discover → build queue (exhaustive) → sort → process incrementally.
    for (uint8_t t = 0; t < numTracks; ++t) {
        Track& track = trackManager.getTrack(t);
        bool anySlotHasEvents = false;
        for (uint8_t s = 0; s < Config::MAX_LOOPS_PER_TRACK; ++s) {
            resetLoopSlotForBootManifest(track.getLoop(s), s);
            if (!StorageManager::loopSlotHasPayloadOnSd(t, s)) {
                continue;
            }
            anySlotHasEvents = true;
            const uint8_t restorePriority = computeBootRestorePriority(
                t, s, selectedTrackIdx, activeLoopIndex.data(), activeLoopIndex.size(),
                selectedSlotIndex.data(), selectedSlotIndex.size());
            if (pendingLoopSlotRestores_.count < PendingLoopSlotRestoreQueue::kCapacity) {
                pendingLoopSlotRestores_.entries[pendingLoopSlotRestores_.count++] = {t, s,
                                                                                      restorePriority};
            }
        }
        applyLoadedTrackStateAfterLoopSlots(track, trackHeaders[t].state, anySlotHasEvents,
                                            trackHeaders[t].muted);
        char trackMilestone[8];
        snprintf(trackMilestone, sizeof(trackMilestone), "t%u", static_cast<unsigned>(t));
        emitBootMilestone("scan", trackMilestone);
    }

    emitBootMilestone("scan", "done");

    sortPendingLoopSlotRestoresByPriority();

    if (pendingLoopSlotRestores_.count > 0) {
        const DeferredLoopSlotRestore& first = pendingLoopSlotRestores_.entries[0];
        Serial.print("[StorageManager] Queuing loop slot restore ");
        Serial.print(pendingLoopSlotRestores_.count);
        Serial.print(" pending; first ");
        Serial.print(first.track);
        Serial.print('/');
        Serial.println(first.slot);
    }

    return applyLoadedTransportFooter(numTracks, activeLoopIndex, selectedSlotIndex, selectedTrackIdx,
                                      state, loadedLooperState, masterLoopLength);
}

bool StorageManager::loadCurrentSetFromDirectory(const char* setDir, LooperState& state) {
    char metaPath[80];
    if (setDir != nullptr && std::strcmp(setDir, CurrentSetStorage::kCurrentSetDir) == 0) {
        const int written = std::snprintf(metaPath, sizeof(metaPath), "%s",
                                        CurrentSetStorage::kCurrentRuntimeBundlePath);
        if (written <= 0 || static_cast<size_t>(written) >= sizeof(metaPath)) {
            return false;
        }
    } else {
        const int written = std::snprintf(metaPath, sizeof(metaPath), "%s/%s", setDir,
                                          CurrentSetStorage::kSetBinFileName);
        if (written <= 0 || static_cast<size_t>(written) >= sizeof(metaPath)) {
            return false;
        }
    }
    if (!SD.exists(metaPath)) {
        Serial.print("[StorageManager] ERROR: CurrentSet meta missing ");
        Serial.println(metaPath);
        return false;
    }
    if (!CurrentSetStorage::verifySaveFileTokenAtPath(metaPath)) {
        Serial.print("[StorageManager] ERROR: CurrentSet meta incomplete ");
        Serial.println(metaPath);
        return false;
    }
    File file = SD.open(metaPath, FILE_READ);
    if (!file) {
        Serial.print("[StorageManager] ERROR: Could not open CurrentSet meta ");
        Serial.println(metaPath);
        return false;
    }
    Serial.print("[StorageManager] Reading runtime bundle ");
    Serial.println(metaPath);
    std::snprintf(restoredSetBundlePath_, sizeof(restoredSetBundlePath_), "%s", metaPath);
    std::vector<uint8_t> activeLoopIndex;
    uint8_t selectedTrackIdx = 0;
    const bool ok = loadCurrentSetBundleAndActiveLoopSlots(file, setDir, state, activeLoopIndex, selectedTrackIdx);
    if (file) {
        file.close();
    }
    if (!ok && setDir != nullptr && std::strcmp(setDir, CurrentSetStorage::kCurrentSetDir) == 0) {
        Serial.println("[StorageManager] Current runtime bundle unreadable; quarantining for recovery.");
        quarantineCorruptRuntimeBundleOnSd();
    }
    return ok;
}

void STORAGE_PERSIST_MEM StorageManager::processDeferredLoopSlotRestore() {
    if (pendingLoopSlotRestores_.count == 0) {
        return;
    }
    const DeferredLoopSlotRestore next = pendingLoopSlotRestores_.entries[0];
    for (uint16_t i = 1; i < pendingLoopSlotRestores_.count; ++i) {
        pendingLoopSlotRestores_.entries[i - 1] = pendingLoopSlotRestores_.entries[i];
    }
    --pendingLoopSlotRestores_.count;
    Track& track = trackManager.getTrack(next.track);
    bool anySlotHasEvents = false;
    Serial.print("[StorageManager] Deferred restore loop slot ");
    Serial.print(next.track);
    Serial.print('/');
    Serial.println(next.slot);
    loadLoopSlotFromCurrentSetSd(next.track, next.slot, track, anySlotHasEvents);
}

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
    removeDeferredLoopSlotRestore(trackIndex, slotIndex);
    Track& track = trackManager.getTrack(trackIndex);
    if (track.getLoop(slotIndex).hasPublishedEvents()) {
        return;
    }
    if (!StorageManager::loopSlotHasPayloadOnSd(trackIndex, slotIndex)) {
        return;
    }
    bool anySlotHasEvents = false;
    loadLoopSlotFromCurrentSetSd(trackIndex, slotIndex, track, anySlotHasEvents);
}

bool StorageManager::loadCurrentWorkspaceFromSd(LooperState& state) {
    return loadCurrentSetFromDirectory(CurrentSetStorage::kCurrentSetDir, state);
}

bool StorageManager::loadV5MonolithIntoRam(LooperState& state) {
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
                if (loop.hasPublishedEvents()) {
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

void queueBootRevisionRecovery(uint16_t setId, uint16_t revisionId) {
    if (setId == 0 || revisionId == 0) {
        return;
    }
    storageSession.bootRecovery.setId = setId;
    storageSession.bootRecovery.revisionId = revisionId;
    storageSession.bootRecovery.pending = true;
}

void discardIncompleteCurrentWorkspaceTempFilesOnSd() {
    uint16_t removedCount = 0;
    if (SD.exists(CurrentSetStorage::kCurrentRuntimeBundleTempPath)) {
        if (SD.remove(CurrentSetStorage::kCurrentRuntimeBundleTempPath)) {
            ++removedCount;
            Serial.println("[StorageManager] Boot hygiene: removed incomplete runtime bundle temp");
        }
    }
    for (uint8_t trackIndex = 0; trackIndex < Config::NUM_TRACKS; ++trackIndex) {
        for (uint8_t slotIndex = 0; slotIndex < Config::MAX_LOOPS_PER_TRACK; ++slotIndex) {
            char tempPath[48];
            if (!CurrentSetStorage::formatLoopSlotTempPath(tempPath, sizeof(tempPath), trackIndex,
                                                           slotIndex)) {
                continue;
            }
            if (!SD.exists(tempPath)) {
                continue;
            }
            if (SD.remove(tempPath)) {
                ++removedCount;
            }
        }
    }
    if (removedCount > 0) {
        Serial.print("[StorageManager] Boot hygiene: discarded ");
        Serial.print(removedCount);
        Serial.println(" incomplete current-workspace temp file(s)");
    }
}

void discardIncompleteRevisionTempFilesOnSd() {
    if (!SD.exists(SetRevisionCatalog::kSetsRoot)) {
        return;
    }
    File setsDir = SD.open(SetRevisionCatalog::kSetsRoot);
    if (!setsDir) {
        return;
    }
    uint16_t removedCount = 0;
    while (true) {
        File setEntry = setsDir.openNextFile();
        if (!setEntry) {
            break;
        }
        const bool isSetDirectory = setEntry.isDirectory();
        char setFolderName[16] = {};
        const char* setName = setEntry.name();
        if (setName != nullptr) {
            std::snprintf(setFolderName, sizeof(setFolderName), "%s", setName);
        }
        setEntry.close();
        if (!isSetDirectory || setFolderName[0] != 'S') {
            continue;
        }

        char revisionsDir[48];
        if (std::snprintf(revisionsDir, sizeof(revisionsDir), "%s/%s/revisions",
                          SetRevisionCatalog::kSetsRoot, setFolderName) <= 0) {
            continue;
        }
        if (!SD.exists(revisionsDir)) {
            continue;
        }
        File revisions = SD.open(revisionsDir);
        if (!revisions) {
            continue;
        }
        while (true) {
            File revisionEntry = revisions.openNextFile();
            if (!revisionEntry) {
                break;
            }
            char revisionName[24] = {};
            const char* revisionFileName = revisionEntry.name();
            revisionEntry.close();
            if (revisionFileName == nullptr) {
                continue;
            }
            std::snprintf(revisionName, sizeof(revisionName), "%s", revisionFileName);
            const size_t nameLen = std::strlen(revisionName);
            const size_t tempSuffixLen = std::strlen(SetRevisionCatalog::kRevisionTempSuffix);
            if (nameLen <= tempSuffixLen ||
                std::strcmp(revisionName + nameLen - tempSuffixLen,
                            SetRevisionCatalog::kRevisionTempSuffix) != 0) {
                continue;
            }
            char tempPath[72];
            if (std::snprintf(tempPath, sizeof(tempPath), "%s/%s", revisionsDir, revisionName) <=
                0) {
                continue;
            }
            if (SD.remove(tempPath)) {
                ++removedCount;
                Serial.print("[StorageManager] Boot hygiene: removed incomplete revision ");
                Serial.println(tempPath);
            }
        }
        revisions.close();
    }
    setsDir.close();
    if (removedCount > 0) {
        Serial.print("[StorageManager] Boot hygiene: discarded ");
        Serial.print(removedCount);
        Serial.println(" incomplete revision temp file(s)");
    }
}

bool StorageManager::loadCurrentWorkspaceAtBoot(LooperState& state) {
    Serial.println("[StorageManager] Loading Current workspace from SD...");
    loadWorkspaceMetaCountersFromSd();
    const bool ok = loadCurrentSetFromDirectory(CurrentSetStorage::kCurrentSetDir, state);
    if (ok) {
        Serial.println("[StorageManager] Current workspace loaded successfully.");
    } else {
        Serial.println("[StorageManager] Current workspace load failed.");
    }
    return ok;
}

bool StorageManager::loadCurrentSetFromSd(LooperState& state) {
    return loadCurrentWorkspaceAtBoot(state);
}

bool StorageManager::tryLoadLatestRecoveryPoint(LooperState& state) {
    if (!SD.exists(CurrentSetStorage::kCheckpointsDir)) {
        return false;
    }
    File dir = SD.open(CurrentSetStorage::kCheckpointsDir);
    if (!dir) {
        return false;
    }
    char latestName[32] = {};
    while (true) {
        File entry = dir.openNextFile();
        if (!entry) {
            break;
        }
        const char* name = entry.name();
        entry.close();
        if (name == nullptr || name[0] != '_') {
            continue;
        }
        if (latestName[0] == '\0' || std::strcmp(name, latestName) > 0) {
            std::snprintf(latestName, sizeof(latestName), "%s", name);
        }
    }
    dir.close();
    if (latestName[0] == '\0') {
        return false;
    }
    char recoveryDir[72];
    std::snprintf(recoveryDir, sizeof(recoveryDir), "%s/%s",
                  CurrentSetStorage::kCheckpointsDir, latestName);
    Serial.print("[StorageManager] Boot recovery: trying RecoveryPoint ");
    Serial.println(recoveryDir);
    if (!loadCurrentSetFromDirectory(recoveryDir, state)) {
        return false;
    }
    forceCurrentSetFullLoopWrite = true;
    markAllCurrentSetLoopSlotsDirtyInternal(false);
    requestDeferredSaveState(state);
    return true;
}

bool StorageManager::tryLoadNewestSavedSet(LooperState& state) {
    if (!SD.exists(CurrentSetStorage::kSetsRoot)) {
        return false;
    }
    File dir = SD.open(CurrentSetStorage::kSetsRoot);
    if (!dir) {
        return false;
    }
    char newestName[32] = {};
    uint32_t newestSequence = 0;
    while (true) {
        File entry = dir.openNextFile();
        if (!entry) {
            break;
        }
        const bool isDirectory = entry.isDirectory();
        const char* name = entry.name();
        entry.close();
        uint32_t sequence = 0;
        if (!isDirectory || !SavedSetCatalog::parseSavedSetFolderName(name, sequence, nullptr)) {
            continue;
        }
        if (newestName[0] == '\0' || sequence > newestSequence) {
            newestSequence = sequence;
            std::snprintf(newestName, sizeof(newestName), "%s", name);
        }
    }
    dir.close();
    if (newestName[0] == '\0') {
        return false;
    }
    char savedSetDir[48];
    std::snprintf(savedSetDir, sizeof(savedSetDir), "%s/%s", CurrentSetStorage::kSetsRoot,
                  newestName);
    Serial.print("[StorageManager] Boot recovery: trying SavedSet ");
    Serial.println(savedSetDir);
    if (!loadCurrentSetFromDirectory(savedSetDir, state)) {
        return false;
    }
    forceCurrentSetFullLoopWrite = true;
    markAllCurrentSetLoopSlotsDirtyInternal(false);
    requestDeferredSaveState(state);
    return true;
}

bool StorageManager::attemptBootRecoveryChain(LooperState& state) {
    CurrentWorkspaceStorage::WorkspaceMetaRecord workspaceMeta{};
    if (!CurrentWorkspaceStorage::readWorkspaceMetaFile(workspaceMeta)) {
        workspaceMeta.derivedFromSetId = workspaceDerivedFromSetId;
        workspaceMeta.derivedFromRevisionId = workspaceDerivedFromRevisionId;
    }

    const BootRecoveryPolicy::RevisionRecoveryPlan plan =
        BootRecoveryPolicy::buildRevisionRecoveryPlan(
            workspaceMeta.derivedFromSetId, workspaceMeta.derivedFromRevisionId, 0);

    if (plan.setId != 0) {
        uint16_t catalogLatestRevisionId = 0;
        readSetLatestRevisionIdFromSd(plan.setId, catalogLatestRevisionId);
        const BootRecoveryPolicy::RevisionRecoveryPlan resolvedPlan =
            BootRecoveryPolicy::buildRevisionRecoveryPlan(
                plan.setId, plan.derivedRevisionId, catalogLatestRevisionId);

        if (resolvedPlan.derivedRevisionId != 0) {
            Serial.print("[StorageManager] Boot recovery: queue derived revision S");
            Serial.print(resolvedPlan.setId);
            Serial.print(" v");
            Serial.println(resolvedPlan.derivedRevisionId);
            queueBootRevisionRecovery(resolvedPlan.setId, resolvedPlan.derivedRevisionId);
            return true;
        }

        const uint16_t latestFallback = BootRecoveryPolicy::resolveLatestRevisionFallback(
            resolvedPlan.derivedRevisionId, resolvedPlan.latestRevisionId);
        if (latestFallback != 0) {
            Serial.print("[StorageManager] Boot recovery: queue latest revision S");
            Serial.print(resolvedPlan.setId);
            Serial.print(" v");
            Serial.println(latestFallback);
            queueBootRevisionRecovery(resolvedPlan.setId, latestFallback);
            return true;
        }
    }

    if (tryLoadLatestRecoveryPoint(state)) {
        Serial.println("[StorageManager] Boot recovered from recovery checkpoint.");
        return true;
    }
    Serial.println("[StorageManager] Boot recovery chain exhausted; starting empty.");
    forceCurrentSetFullLoopWrite = false;
    syncCurrentSetDirtyTrackingFromLoadedState();
    return false;
}

bool StorageManager::loadState(LooperState& state) {
    trackManager.beginBootLoad();
    emitBootMilestone("load", "start");
    struct BootLoadScope {
        ~BootLoadScope() { trackManager.endBootLoad(); }
    } bootLoadScope;

    resetStorageSessionJobs();
    RtcTime::init();
    syncWallClockFromSdTimestampsQuick();
    discardIncompleteRevisionTempFilesOnSd();
    discardIncompleteCurrentWorkspaceTempFilesOnSd();

    const bool hasCurrentWorkspace =
        SD.exists(CurrentSetStorage::kCurrentMetaPath) ||
        SD.exists(CurrentWorkspaceStorage::kWorkspaceMetaPath);
    if (hasCurrentWorkspace) {
        if (loadCurrentWorkspaceAtBoot(state)) {
            SavedSetCatalog::SetIndex index{};
            if (!reconcileSetIndexOnSd(index)) {
                Serial.println("[StorageManager] WARN: could not reconcile MidiLooper/sets/index.bin");
            }
            forceCurrentSetFullLoopWrite = false;
            syncCurrentSetDirtyTrackingFromLoadedState();
            emitBootMilestone("load", "ok");
            return true;
        }
        Serial.println("[StorageManager] Current workspace load failed; attempting boot recovery chain.");
        resetTracksAfterFailedLoad();
        if (attemptBootRecoveryChain(state)) {
            emitBootMilestone("load", "ok");
            return true;
        }
        forceCurrentSetFullLoopWrite = false;
        syncCurrentSetDirtyTrackingFromLoadedState();
        emitBootMilestone("load", "fail");
        return false;
    }
    if (SD.exists(STORAGE_FILENAME)) {
        const bool migrated = migrateV5MonolithToCurrentSet(state);
        emitBootMilestone("load", migrated ? "ok" : "fail");
        return migrated;
    }
    const bool recovered = attemptBootRecoveryChain(state);
    emitBootMilestone("load", recovered ? "ok" : "fail");
    return recovered;
}
