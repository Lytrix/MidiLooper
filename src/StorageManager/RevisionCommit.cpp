//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "StorageManagerInternal.h"

#include "CurrentSetStorage.h"
#include "CurrentWorkspaceStorage.h"
#include "Globals.h"
#include "Loop.h"
#include "LoopEventStore.h"
#include "PersistenceBudget.h"
#include "PersistenceSchema.h"
#include "RevisionCommitPolicy.h"
#include "RevisionLoadPolicy.h"
#include "RevisionPackedBlob.h"
#include "RtcTime.h"
#include "SetRevisionCatalog.h"
#include "StorageLoopIo.h"
#include "TrackManager.h"
#include "Utils/DebugSessionCapture.h"
#include <Arduino.h>
#include <SD.h>
#include <cstdio>
#include <cstring>

namespace StorageManagerInternal {

STORAGE_PERSIST_MEM void resetRevisionCommitJobState() {
    if (storageSession.revisionCommit.workspaceEpochBeforeSnapshot != 0) {
        currentWorkspaceEpoch = storageSession.revisionCommit.workspaceEpochBeforeSnapshot;
        storageSession.revisionCommit.workspaceEpochBeforeSnapshot = 0;
    }
    if (storageSession.revisionCommit.file) {
        storageSession.revisionCommit.file.close();
    }
    if (storageSession.revisionCommit.sourceFileOpen) {
        storageSession.revisionCommit.sourceFile.close();
        storageSession.revisionCommit.sourceFileOpen = false;
    }
    storageSession.revisionCommit.inProgress = false;
    storageSession.revisionCommit.sdIoActive = false;
    storageSession.revisionCommit.stage = RevisionCommitStage::Idle;
    storageSession.revisionCommit.writeStage = RevisionWriteStage::PrepareLayout;
    storageSession.revisionCommit.sourceEpoch = 0;
    storageSession.revisionCommit.setId = 0;
    storageSession.revisionCommit.pendingRevisionId = 0;
    storageSession.revisionCommit.tempPath[0] = '\0';
    storageSession.revisionCommit.finalPath[0] = '\0';
    storageSession.revisionCommit.slotIndexCount = 0;
    storageSession.revisionCommit.slotIndexWriteCursor = 0;
    storageSession.revisionCommit.payloadWriteOffset = 0;
    storageSession.revisionCommit.chunkCount = 0;
    storageSession.revisionCommit.copyTrackCursor = 0;
    storageSession.revisionCommit.copySlotCursor = 0;
    storageSession.revisionCommit.runtimeBundleSize = 0;
    storageSession.revisionCommit.runtimeBundleReadPos = 0;
    storageSession.revisionCommit.slotReadPos = 0;
    storageSession.revisionCommit.slotBodyRemaining = 0;
    storageSession.revisionCommit.loopSlotBodyActive = false;
    storageSession.revisionCommit.payloadCrc = 0;
    storageSession.revisionCommit.payloadCrcSeeded = false;
    storageSession.revisionCommit.allocatedNewSet = false;
    storageSession.revisionCommit.slotIndexChunkWritten = false;
    storageSession.revisionCommit.lastBlockedLogAtMs = 0;
    resetDeferredLoopWriteState();
    storageSession.revisionCommit.header = RevisionPackedBlob::RevisionHeader{};
    storageSession.revisionCommit.catalogIndex = SetRevisionCatalog::SetCatalogIndex{};
    storageSession.revisionCommit.setMeta = SetRevisionCatalog::SetMetaRecord{};
    for (uint16_t i = 0; i < kMaxRevisionLoopIndexEntries; ++i) {
        storageSession.revisionCommit.slotEntries[i] = RevisionPackedBlob::RevisionLoopSlotDirectoryEntry{};
    }
    if (storageSession.revisionLoad.loadAfterRevisionCommit) {
        clearRevisionLoadRequestState();
    }
}

STORAGE_PERSIST_MEM uint32_t resolveMaxPersistenceMicros(const LooperState& state) {
    return PersistenceBudget::resolveMaxPersistenceMicros(
        isCaptureActiveForPersistence(),
        state == LOOPER_PLAYING || state == LOOPER_OVERDUBBING || state == LOOPER_RECORDING);
}

STORAGE_PERSIST_MEM uint32_t resolveRevisionCommitSourceEpoch() {
    return CurrentWorkspaceStorage::resolveCompletedWorkspaceEpochForRevisionSnapshot(
        currentWorkspaceEpoch, storageSession.currentWorkspaceSave.workspaceEpoch, storageSession.currentWorkspaceSave.inProgress);
}

/// Loop slots are written only when dirty; unchanged slots keep an older epoch on SD but still
/// belong to the workspace snapshot at sourceEpoch. Include any non-empty slot with epoch <= max.
STORAGE_PERSIST_MEM bool slotSourceFileReadableForRevisionCommit(const char* path, uint32_t maxEpoch,
                                             uint32_t& bodySizeOut) {
    bodySizeOut = 0;
    if (path == nullptr || !SD.exists(path)) {
        return false;
    }
    File file = SD.open(path, FILE_READ);
    if (!file) {
        return false;
    }
    const size_t fileSize = file.size();
    size_t payloadOffset = 0;
    if (CurrentWorkspaceStorage::fileStartsWithEpochHeader(file)) {
        CurrentWorkspaceStorage::EpochFileHeader epochHeader{};
        const StorageIo epochIo = storageIoFromFileRead(file);
        if (!CurrentWorkspaceStorage::readEpochFileHeader(epochIo, epochHeader)) {
            file.close();
            return false;
        }
        if (epochHeader.epoch > maxEpoch) {
            file.close();
            return false;
        }
        payloadOffset = CurrentWorkspaceStorage::kEpochFileHeaderByteSize;
    } else if (maxEpoch != 0) {
        file.close();
        return false;
    }
    if (fileSize < payloadOffset + sizeof(CurrentSetStorage::kSaveFileToken)) {
        file.close();
        return false;
    }
    bodySizeOut = static_cast<uint32_t>(fileSize - payloadOffset - sizeof(CurrentSetStorage::kSaveFileToken));
    file.close();
    if (bodySizeOut == 0) {
        return false;
    }
    return CurrentSetStorage::verifySaveFileTokenAtPath(path);
}

STORAGE_PERSIST_MEM bool prepareRevisionCommitLayout() {
    storageSession.revisionCommit.slotIndexCount = 0;
    storageSession.revisionCommit.runtimeBundleSize = 0;
    uint32_t bundleBodySize = 0;
    if (slotSourceFileReadableForRevisionCommit(CurrentSetStorage::kCurrentMetaPath,
                                                storageSession.revisionCommit.sourceEpoch, bundleBodySize)) {
        storageSession.revisionCommit.runtimeBundleSize = bundleBodySize;
    }

    for (uint8_t trackIndex = 0; trackIndex < Config::NUM_TRACKS; ++trackIndex) {
        for (uint8_t slotIndex = 0; slotIndex < Config::MAX_LOOPS_PER_TRACK; ++slotIndex) {
            const Loop& loop = trackManager.getTrack(trackIndex).getLoop(slotIndex);
            if (!loop.hasCommittedPasses() && !loop.passes.hasRecordPass() &&
                loop.passes.overdubPasses.empty()) {
                continue;
            }
            const uint32_t bodySize = measureLoopSlotFileBytes(loop);
            if (bodySize == 0) {
                continue;
            }
            if (storageSession.revisionCommit.slotIndexCount >= kMaxRevisionLoopIndexEntries) {
                return false;
            }
            RevisionPackedBlob::RevisionLoopSlotDirectoryEntry& entry =
                storageSession.revisionCommit.slotEntries[storageSession.revisionCommit.slotIndexCount];
            entry = RevisionPackedBlob::RevisionLoopSlotDirectoryEntry{};
            entry.trackIndex = trackIndex;
            entry.slotIndex = slotIndex;
            entry.occupied = 1;
            entry.bodyLength = bodySize;
            entry.loopLengthTicks = loop.loopLengthTicks;
            const uint32_t ticksPerBar = Track::getTicksPerBar();
            if (loop.loopLengthTicks > 0 && ticksPerBar > 0) {
                entry.bars = static_cast<uint16_t>(loop.loopLengthTicks / ticksPerBar);
            }
            size_t eventCount = 0;
            if (loop.passes.hasRecordPass()) {
                eventCount +=
                    LoopEventStore::countEventsInChunkIds(loop.passes.recordPass.committedChunkIds);
            }
            for (const OverdubPass& pass : loop.passes.overdubPasses) {
                eventCount += LoopEventStore::countEventsInChunkIds(pass.committedChunkIds);
            }
            entry.noteCount =
                static_cast<uint16_t>(eventCount > UINT16_MAX ? UINT16_MAX : eventCount / 2U);
            ++storageSession.revisionCommit.slotIndexCount;
        }
    }
    return true;
}

STORAGE_PERSIST_MEM bool beginRevisionCommitSnapshot() {
    storageSession.revisionCommit.sourceEpoch = resolveRevisionCommitSourceEpoch();
    if (storageSession.revisionCommit.sourceEpoch == 0) {
        Serial.println("[StorageManager] ERROR: Revision commit has no completed source epoch");
        return false;
    }

    if (!CurrentSetStorage::ensureDirectory(PersistenceLayout::kRoot) ||
        !CurrentSetStorage::ensureDirectory(SetRevisionCatalog::kSetsRoot)) {
        return false;
    }

    File indexFile = SD.open(SetRevisionCatalog::kSetIndexPath, FILE_READ);
    if (indexFile) {
        const StorageIo indexIo = storageIoFromFileRead(indexFile);
        (void)SetRevisionCatalog::readSetCatalogIndex(indexIo, storageSession.revisionCommit.catalogIndex);
        indexFile.close();
    }

    storageSession.revisionCommit.setId = workspaceDerivedFromSetId;
    storageSession.revisionCommit.allocatedNewSet = false;
    if (storageSession.revisionCommit.setId == 0) {
        storageSession.revisionCommit.setId = SetRevisionCatalog::allocateNextSetId(storageSession.revisionCommit.catalogIndex);
        storageSession.revisionCommit.allocatedNewSet = true;
        storageSession.revisionCommit.setMeta = SetRevisionCatalog::SetMetaRecord{};
        storageSession.revisionCommit.setMeta.setId = storageSession.revisionCommit.setId;
    } else {
        char setMetaPath[64];
        if (!SetRevisionCatalog::formatSetMetaPath(setMetaPath, sizeof(setMetaPath),
                                                   storageSession.revisionCommit.setId)) {
            return false;
        }
        File setMetaFile = SD.open(setMetaPath, FILE_READ);
        if (!setMetaFile) {
            Serial.println(
                "[StorageManager] WARN: Derived set missing on SD; allocating new set for commit");
            storageSession.revisionCommit.setId = SetRevisionCatalog::allocateNextSetId(storageSession.revisionCommit.catalogIndex);
            storageSession.revisionCommit.allocatedNewSet = true;
            storageSession.revisionCommit.setMeta = SetRevisionCatalog::SetMetaRecord{};
            storageSession.revisionCommit.setMeta.setId = storageSession.revisionCommit.setId;
        } else {
            const StorageIo setMetaIo = storageIoFromFileRead(setMetaFile);
            const bool metaOk =
                SetRevisionCatalog::readSetMetaRecord(setMetaIo, storageSession.revisionCommit.setMeta);
            setMetaFile.close();
            if (!metaOk) {
                Serial.println(
                    "[StorageManager] WARN: Derived set meta invalid; allocating new set for commit");
                storageSession.revisionCommit.setId =
                    SetRevisionCatalog::allocateNextSetId(storageSession.revisionCommit.catalogIndex);
                storageSession.revisionCommit.allocatedNewSet = true;
                storageSession.revisionCommit.setMeta = SetRevisionCatalog::SetMetaRecord{};
                storageSession.revisionCommit.setMeta.setId = storageSession.revisionCommit.setId;
            }
        }
    }

    storageSession.revisionCommit.pendingRevisionId =
        SetRevisionCatalog::peekNextRevisionId(storageSession.revisionCommit.setMeta);

    char setFolderPath[48];
    char revisionsDirPath[56];
    if (!SetRevisionCatalog::formatSetFolderPath(setFolderPath, sizeof(setFolderPath),
                                                 storageSession.revisionCommit.setId) ||
        !SetRevisionCatalog::formatRevisionPath(storageSession.revisionCommit.tempPath,
                                                sizeof(storageSession.revisionCommit.tempPath),
                                                storageSession.revisionCommit.setId,
                                                storageSession.revisionCommit.pendingRevisionId, true) ||
        !SetRevisionCatalog::formatRevisionPath(storageSession.revisionCommit.finalPath,
                                                sizeof(storageSession.revisionCommit.finalPath),
                                                storageSession.revisionCommit.setId,
                                                storageSession.revisionCommit.pendingRevisionId, false)) {
        return false;
    }
    const int revisionsWritten = std::snprintf(
        revisionsDirPath, sizeof(revisionsDirPath), "%s/revisions", setFolderPath);
    if (revisionsWritten <= 0 ||
        static_cast<size_t>(revisionsWritten) >= sizeof(revisionsDirPath)) {
        return false;
    }
    if (!CurrentSetStorage::ensureDirectory(setFolderPath) ||
        !CurrentSetStorage::ensureDirectory(revisionsDirPath)) {
        return false;
    }

    storageSession.revisionCommit.header = RevisionPackedBlob::RevisionHeader{};
    std::memcpy(storageSession.revisionCommit.header.magic, RevisionPackedBlob::kRevisionMagic,
                sizeof(storageSession.revisionCommit.header.magic));
    storageSession.revisionCommit.header.revisionId = 0;
    storageSession.revisionCommit.header.setId = storageSession.revisionCommit.setId;
    storageSession.revisionCommit.header.sourceEpoch = storageSession.revisionCommit.sourceEpoch;
    storageSession.revisionCommit.header.createdUnix = static_cast<uint64_t>(RtcTime::getUnixTime());

    if (!prepareRevisionCommitLayout()) {
        return false;
    }

    storageSession.revisionCommit.workspaceEpochBeforeSnapshot = currentWorkspaceEpoch;
    currentWorkspaceEpoch = CurrentWorkspaceStorage::workspaceEpochAfterRevisionSnapshot(
        storageSession.revisionCommit.sourceEpoch);

    storageSession.revisionCommit.writeStage = RevisionWriteStage::OpenTempFile;
    storageSession.revisionCommit.stage = RevisionCommitStage::Write;
    return true;
}

STORAGE_PERSIST_MEM bool computeRevisionCommitPayloadCrcFromFile(File& file, uint32_t payloadOffset,
                                             uint32_t payloadSize, uint32_t& crcOut) {
    if (payloadSize == 0) {
        crcOut = 0;
        return true;
    }
    if (!file.seek(payloadOffset)) {
        return false;
    }
    uint32_t remaining = payloadSize;
    uint32_t crc = 0;
    bool seeded = false;
    while (remaining > 0) {
        const size_t chunkSize =
            remaining > storageSession.revisionCommit.copyBuffer.size() ? storageSession.revisionCommit.copyBuffer.size() : remaining;
        const int bytesRead =
            file.read(storageSession.revisionCommit.copyBuffer.data(), static_cast<size_t>(chunkSize));
        if (bytesRead <= 0) {
            return false;
        }
        const size_t written = static_cast<size_t>(bytesRead);
        if (!seeded) {
            crc = PersistenceSchema::crc32(storageSession.revisionCommit.copyBuffer.data(), written);
            seeded = true;
        } else {
            crc = PersistenceSchema::crc32Continue(crc, storageSession.revisionCommit.copyBuffer.data(), written);
        }
        remaining -= static_cast<uint32_t>(written);
    }
    crcOut = crc;
    return true;
}

STORAGE_PERSIST_MEM bool writeRevisionCommitChunkHeader(RevisionPackedBlob::ChunkType type, uint8_t trackIndex,
                                    uint8_t slotIndex, uint32_t bodyLength) {
    RevisionPackedBlob::ChunkHeader chunkHeader{};
    chunkHeader.type = static_cast<uint8_t>(type);
    chunkHeader.trackIndex = trackIndex;
    chunkHeader.slotIndex = slotIndex;
    chunkHeader.bodyLength = bodyLength;
    const StorageIo io = storageIoFromFileWrite(storageSession.revisionCommit.file);
    if (!RevisionPackedBlob::writeChunkHeader(io, chunkHeader)) {
        return false;
    }
    uint8_t chunkHeaderBytes[RevisionPackedBlob::kChunkHeaderByteSize];
    if (!RevisionPackedBlob::revisionChunkHeaderFileBytes(chunkHeader, chunkHeaderBytes, sizeof(chunkHeaderBytes))) {
        return false;
    }
    if (!appendRevisionCommitPayloadCrc(chunkHeaderBytes, sizeof(chunkHeaderBytes))) {
        return false;
    }
    storageSession.revisionCommit.payloadWriteOffset +=
        static_cast<uint32_t>(RevisionPackedBlob::kChunkHeaderByteSize);
    return true;
}

STORAGE_PERSIST_MEM bool writeRevisionCommitSlotIndexChunkFromFooter() {
    if (storageSession.revisionCommit.slotIndexChunkWritten) {
        return true;
    }
    if (!storageSession.revisionCommit.file) {
        return false;
    }
    const uint32_t slotIndexBodySize = static_cast<uint32_t>(
        RevisionPackedBlob::slotIndexChunkBodySize(storageSession.revisionCommit.slotIndexCount));
    if (!writeRevisionCommitChunkHeader(RevisionPackedBlob::ChunkType::SlotIndex, 0, 0,
                                        slotIndexBodySize)) {
        return false;
    }
    ++storageSession.revisionCommit.chunkCount;
    const uint16_t entryCount = storageSession.revisionCommit.slotIndexCount;
    const uint16_t reservedPrefix = 0;
    if (storageSession.revisionCommit.file.write(reinterpret_cast<const uint8_t*>(&entryCount),
                                 sizeof(entryCount)) != sizeof(entryCount) ||
        storageSession.revisionCommit.file.write(reinterpret_cast<const uint8_t*>(&reservedPrefix),
                                 sizeof(reservedPrefix)) != sizeof(reservedPrefix)) {
        return false;
    }
    uint8_t slotIndexPrefixBytes[RevisionPackedBlob::kSlotIndexBodyPrefixByteSize];
    std::memcpy(slotIndexPrefixBytes, &entryCount, sizeof(entryCount));
    std::memcpy(slotIndexPrefixBytes + sizeof(entryCount), &reservedPrefix, sizeof(reservedPrefix));
    if (!appendRevisionCommitPayloadCrc(slotIndexPrefixBytes, sizeof(slotIndexPrefixBytes))) {
        return false;
    }
    storageSession.revisionCommit.payloadWriteOffset +=
        static_cast<uint32_t>(RevisionPackedBlob::kSlotIndexBodyPrefixByteSize);
    const StorageIo io = storageIoFromFileWrite(storageSession.revisionCommit.file);
    for (uint16_t i = 0; i < storageSession.revisionCommit.slotIndexCount; ++i) {
        if (!RevisionPackedBlob::writeRevisionLoopSlotDirectoryEntry(io, storageSession.revisionCommit.slotEntries[i])) {
            return false;
        }
        uint8_t directoryEntryBytes[RevisionPackedBlob::kRevisionLoopSlotDirectoryEntryByteSize];
        if (!RevisionPackedBlob::revisionLoopSlotDirectoryEntryFileBytes(storageSession.revisionCommit.slotEntries[i], directoryEntryBytes,
                                                         sizeof(directoryEntryBytes))) {
            return false;
        }
        if (!appendRevisionCommitPayloadCrc(directoryEntryBytes, sizeof(directoryEntryBytes))) {
            return false;
        }
        storageSession.revisionCommit.payloadWriteOffset +=
            static_cast<uint32_t>(RevisionPackedBlob::kRevisionLoopSlotDirectoryEntryByteSize);
    }
    storageSession.revisionCommit.slotIndexWriteCursor = storageSession.revisionCommit.slotIndexCount;
    storageSession.revisionCommit.slotIndexChunkWritten = true;
    return true;
}

STORAGE_PERSIST_MEM bool copyRevisionCommitChunk(File& dest, File& src, uint32_t& readPos, uint32_t& bytesRemaining,
                             uint32_t chunkSize) {
    if (bytesRemaining == 0) {
        return true;
    }
    const uint32_t toRead = bytesRemaining < chunkSize ? bytesRemaining : chunkSize;
    if (!src.seek(readPos)) {
        return false;
    }
    const int bytesRead = src.read(storageSession.revisionCommit.copyBuffer.data(), toRead);
    if (bytesRead <= 0) {
        return false;
    }
    const auto written = static_cast<size_t>(bytesRead);
    if (dest.write(storageSession.revisionCommit.copyBuffer.data(), written) != written) {
        return false;
    }
    readPos += written;
    bytesRemaining -= static_cast<uint32_t>(written);
    if (written > 0 && storageSession.revisionCommit.inProgress) {
        if (!appendRevisionCommitPayloadCrc(storageSession.revisionCommit.copyBuffer.data(), written)) {
            return false;
        }
        storageSession.revisionCommit.payloadWriteOffset += static_cast<uint32_t>(written);
    }
    return true;
}

STORAGE_PERSIST_MEM bool stepRevisionCommitWrite() {
    switch (storageSession.revisionCommit.writeStage) {
        case RevisionWriteStage::PrepareLayout:
            return false;

        case RevisionWriteStage::OpenTempFile:
            storageSession.revisionCommit.file = SD.open(storageSession.revisionCommit.tempPath, FILE_WRITE);
            if (!storageSession.revisionCommit.file) {
                Serial.println("[StorageManager] ERROR: Could not open revision temp file");
                return false;
            }
            storageSession.revisionCommit.file.seek(0);
            storageSession.revisionCommit.writeStage = RevisionWriteStage::WriteHeader;
            return true;

        case RevisionWriteStage::WriteHeader: {
            storageSession.revisionCommit.payloadCrc = 0;
            storageSession.revisionCommit.payloadCrcSeeded = false;
            storageSession.revisionCommit.payloadWriteOffset = 0;
            storageSession.revisionCommit.chunkCount = 0;
            const StorageIo io = storageIoFromFileWrite(storageSession.revisionCommit.file);
            if (!RevisionPackedBlob::writeRevisionHeader(io, storageSession.revisionCommit.header)) {
                return false;
            }
            storageSession.revisionCommit.writeStage = RevisionWriteStage::WriteTransportChunk;
            return true;
        }

        case RevisionWriteStage::WriteTransportChunk:
            if (storageSession.revisionCommit.runtimeBundleSize == 0) {
                Serial.println("[StorageManager] ERROR: Revision commit missing runtime bundle body");
                return false;
            }
            if (storageSession.revisionCommit.runtimeBundleReadPos == 0) {
                if (!writeRevisionCommitChunkHeader(RevisionPackedBlob::ChunkType::Transport, 0, 0,
                                                    storageSession.revisionCommit.runtimeBundleSize)) {
                    return false;
                }
                ++storageSession.revisionCommit.chunkCount;
                storageSession.revisionCommit.runtimeBundleReadPos =
                    CurrentWorkspaceStorage::kEpochFileHeaderByteSize;
            }
            if (!storageSession.revisionCommit.sourceFileOpen) {
                storageSession.revisionCommit.sourceFile = SD.open(CurrentSetStorage::kCurrentMetaPath, FILE_READ);
                if (!storageSession.revisionCommit.sourceFile) {
                    return false;
                }
                storageSession.revisionCommit.sourceFileOpen = true;
            }
            {
                const uint32_t endPos = CurrentWorkspaceStorage::kEpochFileHeaderByteSize +
                                        storageSession.revisionCommit.runtimeBundleSize;
                uint32_t remaining = endPos - storageSession.revisionCommit.runtimeBundleReadPos;
                if (remaining > 0) {
                    if (!copyRevisionCommitChunk(
                            storageSession.revisionCommit.file, storageSession.revisionCommit.sourceFile,
                            storageSession.revisionCommit.runtimeBundleReadPos, remaining,
                            static_cast<uint32_t>(storageSession.revisionCommit.copyBuffer.size()))) {
                        return false;
                    }
                    if (remaining > 0) {
                        return true;
                    }
                }
            }
            if (storageSession.revisionCommit.sourceFileOpen) {
                storageSession.revisionCommit.sourceFile.close();
                storageSession.revisionCommit.sourceFileOpen = false;
            }
            storageSession.revisionCommit.copyTrackCursor = 0;
            storageSession.revisionCommit.copySlotCursor = 0;
            storageSession.revisionCommit.writeStage = RevisionWriteStage::WriteLoopSlotChunks;
            return true;

        case RevisionWriteStage::WriteLoopSlotChunks:
            while (storageSession.revisionCommit.copyTrackCursor < Config::NUM_TRACKS) {
                while (storageSession.revisionCommit.copySlotCursor < Config::MAX_LOOPS_PER_TRACK) {
                    const uint8_t trackIndex = storageSession.revisionCommit.copyTrackCursor;
                    const uint8_t slotIndex = storageSession.revisionCommit.copySlotCursor;
                    uint16_t slotEntryIndex = 0;
                    bool foundEntry = false;
                    for (uint16_t i = 0; i < storageSession.revisionCommit.slotIndexCount; ++i) {
                        if (storageSession.revisionCommit.slotEntries[i].trackIndex == trackIndex &&
                            storageSession.revisionCommit.slotEntries[i].slotIndex == slotIndex) {
                            slotEntryIndex = i;
                            foundEntry = true;
                            break;
                        }
                    }
                    if (!foundEntry) {
                        ++storageSession.revisionCommit.copySlotCursor;
                        continue;
                    }
                    const Loop& loop = trackManager.getTrack(trackIndex).getLoop(slotIndex);
                    if (!storageSession.revisionCommit.loopSlotBodyActive) {
                        storageSession.revisionCommit.slotEntries[slotEntryIndex].chunkOffset =
                            storageSession.revisionCommit.payloadWriteOffset;
                        if (!writeRevisionCommitChunkHeader(RevisionPackedBlob::ChunkType::LoopSlot,
                                                            trackIndex, slotIndex,
                                                            storageSession.revisionCommit.slotEntries[slotEntryIndex]
                                                                .bodyLength)) {
                            return false;
                        }
                        ++storageSession.revisionCommit.chunkCount;
                        resetDeferredLoopWriteState();
                        storageSession.revisionCommit.loopSlotBodyActive = true;
                    }
                    bool loopDone = false;
                    if (!stepDeferredLoopPersist(storageSession.revisionCommit.file, loop, loopDone,
                                                 LoopPersistPayloadCrc::RevisionCommit)) {
                        return false;
                    }
                    if (!loopDone) {
                        return true;
                    }
                    storageSession.revisionCommit.loopSlotBodyActive = false;
                    ++storageSession.revisionCommit.copySlotCursor;
                }
                storageSession.revisionCommit.copySlotCursor = 0;
                ++storageSession.revisionCommit.copyTrackCursor;
            }
            storageSession.revisionCommit.slotIndexWriteCursor = 0;
            storageSession.revisionCommit.writeStage = RevisionWriteStage::WriteSlotIndexChunk;
            return true;

        case RevisionWriteStage::WriteSlotIndexChunk: {
            if (storageSession.revisionCommit.slotIndexWriteCursor == 0) {
                const uint32_t slotIndexBodySize = static_cast<uint32_t>(
                    RevisionPackedBlob::slotIndexChunkBodySize(storageSession.revisionCommit.slotIndexCount));
                if (!writeRevisionCommitChunkHeader(RevisionPackedBlob::ChunkType::SlotIndex, 0, 0,
                                                    slotIndexBodySize)) {
                    return false;
                }
                ++storageSession.revisionCommit.chunkCount;
                const uint16_t entryCount = storageSession.revisionCommit.slotIndexCount;
                const uint16_t reservedPrefix = 0;
                if (storageSession.revisionCommit.file.write(reinterpret_cast<const uint8_t*>(&entryCount),
                                             sizeof(entryCount)) != sizeof(entryCount) ||
                    storageSession.revisionCommit.file.write(reinterpret_cast<const uint8_t*>(&reservedPrefix),
                                             sizeof(reservedPrefix)) != sizeof(reservedPrefix)) {
                    return false;
                }
                uint8_t slotIndexPrefixBytes[RevisionPackedBlob::kSlotIndexBodyPrefixByteSize];
                std::memcpy(slotIndexPrefixBytes, &entryCount, sizeof(entryCount));
                std::memcpy(slotIndexPrefixBytes + sizeof(entryCount), &reservedPrefix,
                            sizeof(reservedPrefix));
                if (!appendRevisionCommitPayloadCrc(slotIndexPrefixBytes, sizeof(slotIndexPrefixBytes))) {
                    return false;
                }
                storageSession.revisionCommit.payloadWriteOffset +=
                    static_cast<uint32_t>(RevisionPackedBlob::kSlotIndexBodyPrefixByteSize);
                storageSession.revisionCommit.slotIndexChunkWritten = true;
            }
            if (storageSession.revisionCommit.slotIndexWriteCursor < storageSession.revisionCommit.slotIndexCount) {
                const StorageIo io = storageIoFromFileWrite(storageSession.revisionCommit.file);
                if (!RevisionPackedBlob::writeRevisionLoopSlotDirectoryEntry(
                        io, storageSession.revisionCommit.slotEntries[storageSession.revisionCommit.slotIndexWriteCursor])) {
                    return false;
                }
                const RevisionPackedBlob::RevisionLoopSlotDirectoryEntry& entry =
                    storageSession.revisionCommit.slotEntries[storageSession.revisionCommit.slotIndexWriteCursor];
                uint8_t directoryEntryBytes[RevisionPackedBlob::kRevisionLoopSlotDirectoryEntryByteSize];
                if (!RevisionPackedBlob::revisionLoopSlotDirectoryEntryFileBytes(entry, directoryEntryBytes,
                                                                 sizeof(directoryEntryBytes))) {
                    return false;
                }
                if (!appendRevisionCommitPayloadCrc(directoryEntryBytes, sizeof(directoryEntryBytes))) {
                    return false;
                }
                storageSession.revisionCommit.payloadWriteOffset +=
                    static_cast<uint32_t>(RevisionPackedBlob::kRevisionLoopSlotDirectoryEntryByteSize);
                ++storageSession.revisionCommit.slotIndexWriteCursor;
                return true;
            }
            storageSession.revisionCommit.writeStage = RevisionWriteStage::WriteFooter;
            return true;
        }

        case RevisionWriteStage::WriteFooter: {
            if (!storageSession.revisionCommit.file) {
                return false;
            }
            if (!writeRevisionCommitSlotIndexChunkFromFooter()) {
                return false;
            }
            storageSession.revisionCommit.header.chunkCount = storageSession.revisionCommit.chunkCount;
            storageSession.revisionCommit.header.payloadSize = storageSession.revisionCommit.payloadWriteOffset;
            uint32_t payloadCrc = 0;
            if (!computeRevisionCommitPayloadCrcFromFile(
                    storageSession.revisionCommit.file, static_cast<uint32_t>(RevisionPackedBlob::kRevisionHeaderByteSize),
                    storageSession.revisionCommit.header.payloadSize, payloadCrc)) {
                return false;
            }
            RevisionPackedBlob::RevisionFooter footer{};
            footer.svokToken = RevisionPackedBlob::kRevisionSvokFileToken;
            footer.payloadCrc32 =
                storageSession.revisionCommit.header.payloadSize == 0U ? 0U : payloadCrc;
            footer.fileSize =
                static_cast<uint32_t>(storageSession.revisionCommit.file.size()) +
                static_cast<uint32_t>(RevisionPackedBlob::kRevisionFooterByteSize);
            const StorageIo footerIo = storageIoFromFileWrite(storageSession.revisionCommit.file);
            if (!RevisionPackedBlob::writeRevisionFooter(footerIo, footer)) {
                return false;
            }
            storageSession.revisionCommit.file.flush();
            storageSession.revisionCommit.file.close();
            storageSession.revisionCommit.header.revisionId = storageSession.revisionCommit.pendingRevisionId;
            storageSession.revisionCommit.header.headerCrc32 =
                RevisionPackedBlob::computeRevisionHeaderChecksum(storageSession.revisionCommit.header);
            storageSession.revisionCommit.stage = RevisionCommitStage::Validate;
            return true;
        }
    }
    return false;
}

STORAGE_PERSIST_MEM bool stepRevisionCommitValidate() {
    File file = SD.open(storageSession.revisionCommit.tempPath, FILE_READ);
    if (!file) {
        return false;
    }
    const size_t fileSize = file.size();
    if (fileSize < RevisionPackedBlob::kRevisionHeaderByteSize +
                        RevisionPackedBlob::kRevisionFooterByteSize) {
        file.close();
        SD.remove(storageSession.revisionCommit.tempPath);
        return false;
    }

    uint8_t headerBytes[RevisionPackedBlob::kRevisionHeaderByteSize];
    if (file.read(headerBytes, sizeof(headerBytes)) != static_cast<int>(sizeof(headerBytes))) {
        file.close();
        SD.remove(storageSession.revisionCommit.tempPath);
        return false;
    }

    RevisionPackedBlob::RevisionHeader header{};
    if (!RevisionPackedBlob::parseRevisionHeaderFromBytes(headerBytes, sizeof(headerBytes),
                                                          header)) {
        file.close();
        SD.remove(storageSession.revisionCommit.tempPath);
        return false;
    }

    const size_t payloadOffset = RevisionPackedBlob::kRevisionHeaderByteSize;
    const size_t payloadSize =
        fileSize - payloadOffset - RevisionPackedBlob::kRevisionFooterByteSize;
    if (payloadSize > UINT32_MAX) {
        file.close();
        SD.remove(storageSession.revisionCommit.tempPath);
        return false;
    }
    if (payloadOffset + payloadSize + RevisionPackedBlob::kRevisionFooterByteSize > fileSize) {
        file.close();
        SD.remove(storageSession.revisionCommit.tempPath);
        return false;
    }

    uint32_t payloadCrc = 0;
    bool payloadCrcSeeded = false;
    if (payloadSize > 0) {
        if (!file.seek(payloadOffset)) {
            file.close();
            SD.remove(storageSession.revisionCommit.tempPath);
            return false;
        }
        uint32_t remaining = static_cast<uint32_t>(payloadSize);
        while (remaining > 0) {
            const size_t chunkSize =
                remaining > storageSession.revisionCommit.copyBuffer.size() ? storageSession.revisionCommit.copyBuffer.size()
                                                            : remaining;
            const int bytesRead =
                file.read(storageSession.revisionCommit.copyBuffer.data(), static_cast<size_t>(chunkSize));
            if (bytesRead <= 0) {
                file.close();
                SD.remove(storageSession.revisionCommit.tempPath);
                return false;
            }
            const size_t written = static_cast<size_t>(bytesRead);
            if (!payloadCrcSeeded) {
                payloadCrc = PersistenceSchema::crc32(storageSession.revisionCommit.copyBuffer.data(), written);
                payloadCrcSeeded = true;
            } else {
                payloadCrc = PersistenceSchema::crc32Continue(payloadCrc,
                                                              storageSession.revisionCommit.copyBuffer.data(),
                                                              written);
            }
            remaining -= static_cast<uint32_t>(written);
        }
    }

    RevisionPackedBlob::RevisionFooter footer{};
    if (!file.seek(fileSize - RevisionPackedBlob::kRevisionFooterByteSize) ||
        !readRaw(file, &footer.svokToken, sizeof(footer.svokToken)) ||
        !readRaw(file, &footer.payloadCrc32, sizeof(footer.payloadCrc32)) ||
        !readRaw(file, &footer.fileSize, sizeof(footer.fileSize))) {
        file.close();
        SD.remove(storageSession.revisionCommit.tempPath);
        return false;
    }
    file.close();

    if (footer.svokToken != RevisionPackedBlob::kRevisionSvokFileToken ||
        footer.fileSize != static_cast<uint32_t>(fileSize) ||
        footer.payloadCrc32 != payloadCrc) {
        Serial.print("[StorageManager] ERROR: Revision validate footer magic=");
        Serial.print(footer.svokToken, HEX);
        Serial.print(" fileSize=");
        Serial.print(footer.fileSize);
        Serial.print(" expectedCrc=");
        Serial.print(payloadCrc);
        Serial.print(" footerCrc=");
        Serial.println(footer.payloadCrc32);
        SD.remove(storageSession.revisionCommit.tempPath);
        return false;
    }

    header.revisionId = storageSession.revisionCommit.pendingRevisionId;
    header.chunkCount = storageSession.revisionCommit.chunkCount;
    header.payloadSize = static_cast<uint32_t>(payloadSize);
    header.headerCrc32 = RevisionPackedBlob::computeRevisionHeaderChecksum(header);
    storageSession.revisionCommit.file = SD.open(storageSession.revisionCommit.tempPath, FILE_WRITE);
    if (!storageSession.revisionCommit.file) {
        return false;
    }
    storageSession.revisionCommit.file.seek(0);
    const StorageIo headerIo = storageIoFromFileWrite(storageSession.revisionCommit.file);
    if (!RevisionPackedBlob::writeRevisionHeader(headerIo, header)) {
        storageSession.revisionCommit.file.close();
        return false;
    }
    storageSession.revisionCommit.file.close();

    if (!CurrentSetStorage::atomicRenameTempFile(storageSession.revisionCommit.tempPath,
                                                 storageSession.revisionCommit.finalPath)) {
        return false;
    }
    storageSession.revisionCommit.header = header;
    storageSession.revisionCommit.stage = RevisionCommitStage::CatalogUpdate;
    return true;
}

STORAGE_PERSIST_MEM bool stepRevisionCommitCatalogUpdate() {
    const uint64_t updatedUnix = static_cast<uint64_t>(RtcTime::getUnixTime());
    SetRevisionCatalog::applyValidatedRevisionToSetMeta(storageSession.revisionCommit.setMeta,
                                                       storageSession.revisionCommit.pendingRevisionId,
                                                       updatedUnix);

    char setMetaPath[64];
    char setMetaTempPath[72];
    if (!SetRevisionCatalog::formatSetMetaPath(setMetaPath, sizeof(setMetaPath),
                                               storageSession.revisionCommit.setId)) {
        return false;
    }
    const int tempWritten = std::snprintf(setMetaTempPath, sizeof(setMetaTempPath), "%s.tmp",
                                          setMetaPath);
    if (tempWritten <= 0 || static_cast<size_t>(tempWritten) >= sizeof(setMetaTempPath)) {
        return false;
    }

    File setMetaTemp = SD.open(setMetaTempPath, FILE_WRITE);
    if (!setMetaTemp) {
        return false;
    }
    const StorageIo setMetaIo = storageIoFromFileWrite(setMetaTemp);
    if (!SetRevisionCatalog::writeSetMetaRecord(setMetaIo, storageSession.revisionCommit.setMeta)) {
        setMetaTemp.close();
        return false;
    }
    setMetaTemp.close();
    if (!CurrentSetStorage::atomicRenameTempFile(setMetaTempPath, setMetaPath)) {
        return false;
    }

    storageSession.revisionCommit.catalogIndex.catalogChecksum =
        SetRevisionCatalog::computeSetCatalogIndexChecksum(storageSession.revisionCommit.catalogIndex);
    File indexTemp = SD.open(SetRevisionCatalog::kSetIndexTempPath, FILE_WRITE);
    if (!indexTemp) {
        return false;
    }
    const StorageIo indexIo = storageIoFromFileWrite(indexTemp);
    if (!SetRevisionCatalog::writeSetCatalogIndex(indexIo, storageSession.revisionCommit.catalogIndex)) {
        indexTemp.close();
        return false;
    }
    indexTemp.close();
    if (!CurrentSetStorage::atomicRenameTempFile(SetRevisionCatalog::kSetIndexTempPath,
                                                 SetRevisionCatalog::kSetIndexPath)) {
        return false;
    }

    storageSession.revisionCommit.stage = RevisionCommitStage::Complete;
    return true;
}

STORAGE_PERSIST_MEM bool writeSetCatalogIndexFile(const SetRevisionCatalog::SetCatalogIndex& index) {
    File indexTemp = SD.open(SetRevisionCatalog::kSetIndexTempPath, FILE_WRITE);
    if (!indexTemp) {
        return false;
    }
    const StorageIo indexIo = storageIoFromFileWrite(indexTemp);
    if (!SetRevisionCatalog::writeSetCatalogIndex(indexIo, index)) {
        indexTemp.close();
        return false;
    }
    indexTemp.close();
    return CurrentSetStorage::atomicRenameTempFile(SetRevisionCatalog::kSetIndexTempPath,
                                                   SetRevisionCatalog::kSetIndexPath);
}

STORAGE_PERSIST_MEM bool writeSetMetaRecordFile(uint16_t setId, const SetRevisionCatalog::SetMetaRecord& record) {
    char setMetaPath[64];
    char setMetaTempPath[72];
    if (!SetRevisionCatalog::formatSetMetaPath(setMetaPath, sizeof(setMetaPath), setId)) {
        return false;
    }
    const int tempWritten = std::snprintf(setMetaTempPath, sizeof(setMetaTempPath), "%s.tmp",
                                          setMetaPath);
    if (tempWritten <= 0 || static_cast<size_t>(tempWritten) >= sizeof(setMetaTempPath)) {
        return false;
    }
    File setMetaTemp = SD.open(setMetaTempPath, FILE_WRITE);
    if (!setMetaTemp) {
        return false;
    }
    const StorageIo setMetaIo = storageIoFromFileWrite(setMetaTemp);
    if (!SetRevisionCatalog::writeSetMetaRecord(setMetaIo, record)) {
        setMetaTemp.close();
        return false;
    }
    setMetaTemp.close();
    return CurrentSetStorage::atomicRenameTempFile(setMetaTempPath, setMetaPath);
}

STORAGE_PERSIST_MEM bool stepRevisionCommitComplete() {
    lastCommittedWorkspaceEpoch =
        CurrentWorkspaceStorage::syncLastCommittedEpochAfterRevisionCommitComplete(
            currentWorkspaceEpoch);
    storageSession.revisionCommit.workspaceEpochBeforeSnapshot = 0;
    workspaceDerivedFromSetId = storageSession.revisionCommit.setId;
    workspaceDerivedFromRevisionId = storageSession.revisionCommit.pendingRevisionId;
    workspaceLastCommittedRevisionId = storageSession.revisionCommit.pendingRevisionId;
    if (!writeWorkspaceMetaAfterDeferredSave()) {
        return false;
    }
    Serial.print("[StorageManager] Revision commit complete S");
    Serial.print(storageSession.revisionCommit.setId);
    Serial.print(" v");
    Serial.println(storageSession.revisionCommit.pendingRevisionId);
    const int toastWritten = std::snprintf(
        autoSaveBeforeLoadFolderPending, sizeof(autoSaveBeforeLoadFolderPending), "S%04u_v%04u",
        storageSession.revisionCommit.setId, storageSession.revisionCommit.pendingRevisionId);
    autoSaveBeforeLoadFolderPendingValid =
        toastWritten > 0 &&
        static_cast<size_t>(toastWritten) < sizeof(autoSaveBeforeLoadFolderPending);
#if defined(SESSION_CAPTURE)
    {
        char revCompleteDetail[24];
        std::snprintf(revCompleteDetail, sizeof(revCompleteDetail), "S%04u_v%04u",
                      storageSession.revisionCommit.setId, storageSession.revisionCommit.pendingRevisionId);
        SC_PERSIST("rev_complete", 0, storageSession.revisionCommit.setId, storageSession.revisionCommit.pendingRevisionId,
                   revCompleteDetail);
    }
    if (hitlRevisionCommitBackup.armed) {
        hitlRevisionCommitBackup.committedSetId = storageSession.revisionCommit.setId;
        hitlRevisionCommitBackup.committedRevisionId = storageSession.revisionCommit.pendingRevisionId;
        hitlRevisionCommitBackup.createdNewSetFolder = storageSession.revisionCommit.allocatedNewSet;
        const int folderWritten = std::snprintf(hitlRevisionCommitBackup.setFolderPath,
                                                sizeof(hitlRevisionCommitBackup.setFolderPath),
                                                "%s/S%04u", SetRevisionCatalog::kSetsRoot,
                                                storageSession.revisionCommit.setId);
        const int revisionWritten = std::snprintf(
            hitlRevisionCommitBackup.revisionFinalPath,
            sizeof(hitlRevisionCommitBackup.revisionFinalPath), "%s",
            storageSession.revisionCommit.finalPath);
        if (folderWritten <= 0 ||
            static_cast<size_t>(folderWritten) >= sizeof(hitlRevisionCommitBackup.setFolderPath) ||
            revisionWritten <= 0 ||
            static_cast<size_t>(revisionWritten) >=
                sizeof(hitlRevisionCommitBackup.revisionFinalPath)) {
            return false;
        }
    }
#endif
    storageSession.revisionCommit.stage = RevisionCommitStage::Idle;
    storageSession.revisionCommit.inProgress = false;
    storageSession.currentWorkspaceSave.completedAtMs = millis();
    storageSession.revisionCommit.overlayBackgroundCommit = false;
    return true;
}

STORAGE_PERSIST_MEM bool appendRevisionCommitPayloadCrc(const uint8_t* data, size_t size) {
    if (data == nullptr || size == 0) {
        return true;
    }
    if (!storageSession.revisionCommit.payloadCrcSeeded) {
        storageSession.revisionCommit.payloadCrc = PersistenceSchema::crc32(data, size);
        storageSession.revisionCommit.payloadCrcSeeded = true;
    } else {
        storageSession.revisionCommit.payloadCrc =
            PersistenceSchema::crc32Continue(storageSession.revisionCommit.payloadCrc, data, size);
    }
    return true;
}

STORAGE_PERSIST_MEM bool persistenceWriteRaw(File& file, const void* data, size_t size,
                           LoopPersistPayloadCrc crcMode) {
    if (!writeRaw(file, data, size)) {
        return false;
    }
    if (crcMode == LoopPersistPayloadCrc::RevisionCommit) {
        if (!appendRevisionCommitPayloadCrc(static_cast<const uint8_t*>(data), size)) {
            return false;
        }
        storageSession.revisionCommit.payloadWriteOffset += static_cast<uint32_t>(size);
        return true;
    }
    return true;
}

STORAGE_PERSIST_MEM StorageIo storageIoFromFileWriteWithRevisionPayloadCrc(File& file) {
    return StorageIo{
        [&file](const void* data, size_t size) -> bool {
            return persistenceWriteRaw(file, data, size, LoopPersistPayloadCrc::RevisionCommit);
        },
        nullptr,
    };
}

STORAGE_PERSIST_MEM bool stepRevisionCommitJob() {
    switch (storageSession.revisionCommit.stage) {
        case RevisionCommitStage::Idle:
            return true;

        case RevisionCommitStage::Snapshot:
            return beginRevisionCommitSnapshot();

        case RevisionCommitStage::Write:
            return stepRevisionCommitWrite();

        case RevisionCommitStage::Validate:
            return stepRevisionCommitValidate();

        case RevisionCommitStage::CatalogUpdate:
            return stepRevisionCommitCatalogUpdate();

        case RevisionCommitStage::Complete:
            return stepRevisionCommitComplete();
    }
    return false;
}

}  // namespace StorageManagerInternal

