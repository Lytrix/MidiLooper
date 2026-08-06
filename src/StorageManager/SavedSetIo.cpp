//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// Saved-set archive I/O: set index, folder paths, copy current-set → saved-set.

#include "StorageManager.h"
#include "StorageManagerInternal.h"

#include "CurrentSetStorage.h"
#include "CurrentWorkspaceStorage.h"
#include "Globals.h"
#include "PersistenceLayout.h"
#include "RtcTime.h"
#include "SavedSetCatalog.h"
#include "TrackManager.h"
#include <Arduino.h>
#include <SD.h>
#include <cstdio>
#include <cstring>

namespace StorageManagerInternal {

bool parseSavedSetSequence(const char* folderName, uint32_t& sequence) {
    return SavedSetCatalog::parseSavedSetFolderName(folderName, sequence, nullptr);
}

namespace {

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
    if (std::snprintf(destinationTempPath, sizeof(destinationTempPath), "%s/%s", savedSetDir,
                      CurrentSetStorage::kSetBinTempFileName) <= 0 ||
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
             writeRaw(destination, &CurrentSetStorage::kSaveFileToken,
                      sizeof(CurrentSetStorage::kSaveFileToken));
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

}  // namespace

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

bool buildSavedSetMetadata(uint32_t sequence, SavedSetCatalog::FolderNamingMode namingMode,
                           uint32_t createdAtUnix, SavedSetCatalog::SavedSetMetadata& metadata) {
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
    metadata.filledSlotCount = static_cast<uint8_t>(filledTotal > 255 ? 255 : filledTotal);
    metadata.userLabel[0] = '\0';
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
        !copyCurrentSetLoopsToSavedSet(savedSetDir) || !writeSetIndexToSd(index)) {
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

}  // namespace StorageManagerInternal
