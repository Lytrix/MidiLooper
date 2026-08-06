//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// Saved-set and set-revision browser metadata readers (SD catalog / history).

#include "StorageManager.h"
#include "StorageManagerInternal.h"

#include "CurrentSetStorage.h"
#include "Globals.h"
#include "RevisionPackedBlob.h"
#include "SavedSetCatalog.h"
#include "SetRevisionCatalog.h"
#include "StorageSession.h"
#include <Arduino.h>
#include <SD.h>
#include <cstdio>
#include <cstring>

using namespace StorageManagerInternal;

namespace {

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
