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
    if (revisionCommitWorkspaceEpochBeforeSnapshot != 0) {
        currentWorkspaceEpoch = revisionCommitWorkspaceEpochBeforeSnapshot;
        revisionCommitWorkspaceEpochBeforeSnapshot = 0;
    }
    if (revisionCommitFile) {
        revisionCommitFile.close();
    }
    if (revisionCommitSourceFileOpen) {
        revisionCommitSourceFile.close();
        revisionCommitSourceFileOpen = false;
    }
    revisionCommitInProgress = false;
    revisionCommitSdIoActive = false;
    revisionCommitStage = RevisionCommitStage::Idle;
    revisionWriteStage = RevisionWriteStage::PrepareLayout;
    revisionCommitSourceEpoch = 0;
    revisionCommitSetId = 0;
    revisionCommitPendingRevisionId = 0;
    revisionCommitTempPath[0] = '\0';
    revisionCommitFinalPath[0] = '\0';
    revisionCommitSlotIndexCount = 0;
    revisionCommitSlotIndexWriteCursor = 0;
    revisionCommitPayloadWriteOffset = 0;
    revisionCommitChunkCount = 0;
    revisionCommitCopyTrackCursor = 0;
    revisionCommitCopySlotCursor = 0;
    revisionCommitRuntimeBundleSize = 0;
    revisionCommitRuntimeBundleReadPos = 0;
    revisionCommitSlotReadPos = 0;
    revisionCommitSlotBodyRemaining = 0;
    revisionCommitLoopSlotBodyActive = false;
    revisionCommitPayloadCrc = 0;
    revisionCommitPayloadCrcSeeded = false;
    revisionCommitAllocatedNewSet = false;
    revisionCommitSlotIndexChunkWritten = false;
    lastRevisionCommitBlockedLogAtMs = 0;
    resetDeferredLoopWriteState();
    revisionCommitHeader = RevisionPackedBlob::RevisionHeader{};
    revisionCommitCatalogIndex = SetRevisionCatalog::SetCatalogIndex{};
    revisionCommitSetMeta = SetRevisionCatalog::SetMetaRecord{};
    for (uint16_t i = 0; i < kMaxRevisionLoopIndexEntries; ++i) {
        revisionCommitSlotEntries[i] = RevisionPackedBlob::RevisionLoopSlotDirectoryEntry{};
    }
    if (storageSession.revisionLoad.loadAfterRevisionCommit) {
        clearRevisionLoadPromptAndPipelineState();
    }
}

STORAGE_PERSIST_MEM uint32_t resolveMaxPersistenceMicros(const LooperState& state) {
    return PersistenceBudget::resolveMaxPersistenceMicros(
        isCaptureActiveForPersistence(),
        state == LOOPER_PLAYING || state == LOOPER_OVERDUBBING || state == LOOPER_RECORDING);
}

STORAGE_PERSIST_MEM uint32_t resolveRevisionCommitSourceEpoch() {
    return CurrentWorkspaceStorage::resolveCompletedWorkspaceEpochForRevisionSnapshot(
        currentWorkspaceEpoch, deferredSaveWorkspaceEpoch, deferredSaveInProgress);
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
    revisionCommitSlotIndexCount = 0;
    revisionCommitRuntimeBundleSize = 0;
    uint32_t bundleBodySize = 0;
    if (slotSourceFileReadableForRevisionCommit(CurrentSetStorage::kCurrentMetaPath,
                                                revisionCommitSourceEpoch, bundleBodySize)) {
        revisionCommitRuntimeBundleSize = bundleBodySize;
    }

    for (uint8_t trackIndex = 0; trackIndex < Config::NUM_TRACKS; ++trackIndex) {
        for (uint8_t slotIndex = 0; slotIndex < Config::MAX_LOOPS_PER_TRACK; ++slotIndex) {
            const Loop& loop = trackManager.getTrack(trackIndex).getLoop(slotIndex);
            if (!loop.hasPublishedEvents() && !loop.passes.hasRecordPass() &&
                loop.passes.overdubPasses.empty()) {
                continue;
            }
            const uint32_t bodySize = measureLoopSlotFileBytes(loop);
            if (bodySize == 0) {
                continue;
            }
            if (revisionCommitSlotIndexCount >= kMaxRevisionLoopIndexEntries) {
                return false;
            }
            RevisionPackedBlob::RevisionLoopSlotDirectoryEntry& entry =
                revisionCommitSlotEntries[revisionCommitSlotIndexCount];
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
                    LoopEventStore::countEventsInChunkIds(loop.passes.recordPass.chunkRefs);
            }
            for (const OverdubPass& pass : loop.passes.overdubPasses) {
                eventCount += LoopEventStore::countEventsInChunkIds(pass.chunkRefs);
            }
            entry.noteCount =
                static_cast<uint16_t>(eventCount > UINT16_MAX ? UINT16_MAX : eventCount / 2U);
            ++revisionCommitSlotIndexCount;
        }
    }
    return true;
}

STORAGE_PERSIST_MEM bool beginRevisionCommitSnapshot() {
    revisionCommitSourceEpoch = resolveRevisionCommitSourceEpoch();
    if (revisionCommitSourceEpoch == 0) {
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
        (void)SetRevisionCatalog::readSetCatalogIndex(indexIo, revisionCommitCatalogIndex);
        indexFile.close();
    }

    revisionCommitSetId = workspaceDerivedFromSetId;
    revisionCommitAllocatedNewSet = false;
    if (revisionCommitSetId == 0) {
        revisionCommitSetId = SetRevisionCatalog::allocateNextSetId(revisionCommitCatalogIndex);
        revisionCommitAllocatedNewSet = true;
        revisionCommitSetMeta = SetRevisionCatalog::SetMetaRecord{};
        revisionCommitSetMeta.setId = revisionCommitSetId;
    } else {
        char setMetaPath[64];
        if (!SetRevisionCatalog::formatSetMetaPath(setMetaPath, sizeof(setMetaPath),
                                                   revisionCommitSetId)) {
            return false;
        }
        File setMetaFile = SD.open(setMetaPath, FILE_READ);
        if (!setMetaFile) {
            Serial.println(
                "[StorageManager] WARN: Derived set missing on SD; allocating new set for commit");
            revisionCommitSetId = SetRevisionCatalog::allocateNextSetId(revisionCommitCatalogIndex);
            revisionCommitAllocatedNewSet = true;
            revisionCommitSetMeta = SetRevisionCatalog::SetMetaRecord{};
            revisionCommitSetMeta.setId = revisionCommitSetId;
        } else {
            const StorageIo setMetaIo = storageIoFromFileRead(setMetaFile);
            const bool metaOk =
                SetRevisionCatalog::readSetMetaRecord(setMetaIo, revisionCommitSetMeta);
            setMetaFile.close();
            if (!metaOk) {
                Serial.println(
                    "[StorageManager] WARN: Derived set meta invalid; allocating new set for commit");
                revisionCommitSetId =
                    SetRevisionCatalog::allocateNextSetId(revisionCommitCatalogIndex);
                revisionCommitAllocatedNewSet = true;
                revisionCommitSetMeta = SetRevisionCatalog::SetMetaRecord{};
                revisionCommitSetMeta.setId = revisionCommitSetId;
            }
        }
    }

    revisionCommitPendingRevisionId =
        SetRevisionCatalog::peekNextRevisionId(revisionCommitSetMeta);

    char setFolderPath[48];
    char revisionsDirPath[56];
    if (!SetRevisionCatalog::formatSetFolderPath(setFolderPath, sizeof(setFolderPath),
                                                 revisionCommitSetId) ||
        !SetRevisionCatalog::formatRevisionPath(revisionCommitTempPath,
                                                sizeof(revisionCommitTempPath),
                                                revisionCommitSetId,
                                                revisionCommitPendingRevisionId, true) ||
        !SetRevisionCatalog::formatRevisionPath(revisionCommitFinalPath,
                                                sizeof(revisionCommitFinalPath),
                                                revisionCommitSetId,
                                                revisionCommitPendingRevisionId, false)) {
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

    revisionCommitHeader = RevisionPackedBlob::RevisionHeader{};
    std::memcpy(revisionCommitHeader.magic, RevisionPackedBlob::kRevisionMagic,
                sizeof(revisionCommitHeader.magic));
    revisionCommitHeader.revisionId = 0;
    revisionCommitHeader.setId = revisionCommitSetId;
    revisionCommitHeader.sourceEpoch = revisionCommitSourceEpoch;
    revisionCommitHeader.createdUnix = static_cast<uint64_t>(RtcTime::getUnixTime());

    if (!prepareRevisionCommitLayout()) {
        return false;
    }

    revisionCommitWorkspaceEpochBeforeSnapshot = currentWorkspaceEpoch;
    currentWorkspaceEpoch = CurrentWorkspaceStorage::workspaceEpochAfterRevisionSnapshot(
        revisionCommitSourceEpoch);

    revisionWriteStage = RevisionWriteStage::OpenTempFile;
    revisionCommitStage = RevisionCommitStage::Write;
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
            remaining > revisionCommitCopyBuffer.size() ? revisionCommitCopyBuffer.size() : remaining;
        const int bytesRead =
            file.read(revisionCommitCopyBuffer.data(), static_cast<size_t>(chunkSize));
        if (bytesRead <= 0) {
            return false;
        }
        const size_t written = static_cast<size_t>(bytesRead);
        if (!seeded) {
            crc = PersistenceSchema::crc32(revisionCommitCopyBuffer.data(), written);
            seeded = true;
        } else {
            crc = PersistenceSchema::crc32Continue(crc, revisionCommitCopyBuffer.data(), written);
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
    const StorageIo io = storageIoFromFileWrite(revisionCommitFile);
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
    revisionCommitPayloadWriteOffset +=
        static_cast<uint32_t>(RevisionPackedBlob::kChunkHeaderByteSize);
    return true;
}

STORAGE_PERSIST_MEM bool writeRevisionCommitSlotIndexChunkFromFooter() {
    if (revisionCommitSlotIndexChunkWritten) {
        return true;
    }
    if (!revisionCommitFile) {
        return false;
    }
    const uint32_t slotIndexBodySize = static_cast<uint32_t>(
        RevisionPackedBlob::slotIndexChunkBodySize(revisionCommitSlotIndexCount));
    if (!writeRevisionCommitChunkHeader(RevisionPackedBlob::ChunkType::SlotIndex, 0, 0,
                                        slotIndexBodySize)) {
        return false;
    }
    ++revisionCommitChunkCount;
    const uint16_t entryCount = revisionCommitSlotIndexCount;
    const uint16_t reservedPrefix = 0;
    if (revisionCommitFile.write(reinterpret_cast<const uint8_t*>(&entryCount),
                                 sizeof(entryCount)) != sizeof(entryCount) ||
        revisionCommitFile.write(reinterpret_cast<const uint8_t*>(&reservedPrefix),
                                 sizeof(reservedPrefix)) != sizeof(reservedPrefix)) {
        return false;
    }
    uint8_t slotIndexPrefixBytes[RevisionPackedBlob::kSlotIndexBodyPrefixByteSize];
    std::memcpy(slotIndexPrefixBytes, &entryCount, sizeof(entryCount));
    std::memcpy(slotIndexPrefixBytes + sizeof(entryCount), &reservedPrefix, sizeof(reservedPrefix));
    if (!appendRevisionCommitPayloadCrc(slotIndexPrefixBytes, sizeof(slotIndexPrefixBytes))) {
        return false;
    }
    revisionCommitPayloadWriteOffset +=
        static_cast<uint32_t>(RevisionPackedBlob::kSlotIndexBodyPrefixByteSize);
    const StorageIo io = storageIoFromFileWrite(revisionCommitFile);
    for (uint16_t i = 0; i < revisionCommitSlotIndexCount; ++i) {
        if (!RevisionPackedBlob::writeRevisionLoopSlotDirectoryEntry(io, revisionCommitSlotEntries[i])) {
            return false;
        }
        uint8_t directoryEntryBytes[RevisionPackedBlob::kRevisionLoopSlotDirectoryEntryByteSize];
        if (!RevisionPackedBlob::revisionLoopSlotDirectoryEntryFileBytes(revisionCommitSlotEntries[i], directoryEntryBytes,
                                                         sizeof(directoryEntryBytes))) {
            return false;
        }
        if (!appendRevisionCommitPayloadCrc(directoryEntryBytes, sizeof(directoryEntryBytes))) {
            return false;
        }
        revisionCommitPayloadWriteOffset +=
            static_cast<uint32_t>(RevisionPackedBlob::kRevisionLoopSlotDirectoryEntryByteSize);
    }
    revisionCommitSlotIndexWriteCursor = revisionCommitSlotIndexCount;
    revisionCommitSlotIndexChunkWritten = true;
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
    const int bytesRead = src.read(revisionCommitCopyBuffer.data(), toRead);
    if (bytesRead <= 0) {
        return false;
    }
    const auto written = static_cast<size_t>(bytesRead);
    if (dest.write(revisionCommitCopyBuffer.data(), written) != bytesRead) {
        return false;
    }
    readPos += written;
    bytesRemaining -= static_cast<uint32_t>(written);
    if (written > 0 && revisionCommitInProgress) {
        if (!appendRevisionCommitPayloadCrc(revisionCommitCopyBuffer.data(), written)) {
            return false;
        }
        revisionCommitPayloadWriteOffset += static_cast<uint32_t>(written);
    }
    return true;
}

STORAGE_PERSIST_MEM bool stepRevisionCommitWrite() {
    switch (revisionWriteStage) {
        case RevisionWriteStage::PrepareLayout:
            return false;

        case RevisionWriteStage::OpenTempFile:
            revisionCommitFile = SD.open(revisionCommitTempPath, FILE_WRITE);
            if (!revisionCommitFile) {
                Serial.println("[StorageManager] ERROR: Could not open revision temp file");
                return false;
            }
            revisionCommitFile.seek(0);
            revisionWriteStage = RevisionWriteStage::WriteHeader;
            return true;

        case RevisionWriteStage::WriteHeader: {
            revisionCommitPayloadCrc = 0;
            revisionCommitPayloadCrcSeeded = false;
            revisionCommitPayloadWriteOffset = 0;
            revisionCommitChunkCount = 0;
            const StorageIo io = storageIoFromFileWrite(revisionCommitFile);
            if (!RevisionPackedBlob::writeRevisionHeader(io, revisionCommitHeader)) {
                return false;
            }
            revisionWriteStage = RevisionWriteStage::WriteTransportChunk;
            return true;
        }

        case RevisionWriteStage::WriteTransportChunk:
            if (revisionCommitRuntimeBundleSize == 0) {
                Serial.println("[StorageManager] ERROR: Revision commit missing runtime bundle body");
                return false;
            }
            if (revisionCommitRuntimeBundleReadPos == 0) {
                if (!writeRevisionCommitChunkHeader(RevisionPackedBlob::ChunkType::Transport, 0, 0,
                                                    revisionCommitRuntimeBundleSize)) {
                    return false;
                }
                ++revisionCommitChunkCount;
                revisionCommitRuntimeBundleReadPos =
                    CurrentWorkspaceStorage::kEpochFileHeaderByteSize;
            }
            if (!revisionCommitSourceFileOpen) {
                revisionCommitSourceFile = SD.open(CurrentSetStorage::kCurrentMetaPath, FILE_READ);
                if (!revisionCommitSourceFile) {
                    return false;
                }
                revisionCommitSourceFileOpen = true;
            }
            {
                const uint32_t endPos = CurrentWorkspaceStorage::kEpochFileHeaderByteSize +
                                        revisionCommitRuntimeBundleSize;
                uint32_t remaining = endPos - revisionCommitRuntimeBundleReadPos;
                if (remaining > 0) {
                    if (!copyRevisionCommitChunk(
                            revisionCommitFile, revisionCommitSourceFile,
                            revisionCommitRuntimeBundleReadPos, remaining,
                            static_cast<uint32_t>(revisionCommitCopyBuffer.size()))) {
                        return false;
                    }
                    if (remaining > 0) {
                        return true;
                    }
                }
            }
            if (revisionCommitSourceFileOpen) {
                revisionCommitSourceFile.close();
                revisionCommitSourceFileOpen = false;
            }
            revisionCommitCopyTrackCursor = 0;
            revisionCommitCopySlotCursor = 0;
            revisionWriteStage = RevisionWriteStage::WriteLoopSlotChunks;
            return true;

        case RevisionWriteStage::WriteLoopSlotChunks:
            while (revisionCommitCopyTrackCursor < Config::NUM_TRACKS) {
                while (revisionCommitCopySlotCursor < Config::MAX_LOOPS_PER_TRACK) {
                    const uint8_t trackIndex = revisionCommitCopyTrackCursor;
                    const uint8_t slotIndex = revisionCommitCopySlotCursor;
                    uint16_t slotEntryIndex = 0;
                    bool foundEntry = false;
                    for (uint16_t i = 0; i < revisionCommitSlotIndexCount; ++i) {
                        if (revisionCommitSlotEntries[i].trackIndex == trackIndex &&
                            revisionCommitSlotEntries[i].slotIndex == slotIndex) {
                            slotEntryIndex = i;
                            foundEntry = true;
                            break;
                        }
                    }
                    if (!foundEntry) {
                        ++revisionCommitCopySlotCursor;
                        continue;
                    }
                    const Loop& loop = trackManager.getTrack(trackIndex).getLoop(slotIndex);
                    if (!revisionCommitLoopSlotBodyActive) {
                        revisionCommitSlotEntries[slotEntryIndex].chunkOffset =
                            revisionCommitPayloadWriteOffset;
                        if (!writeRevisionCommitChunkHeader(RevisionPackedBlob::ChunkType::LoopSlot,
                                                            trackIndex, slotIndex,
                                                            revisionCommitSlotEntries[slotEntryIndex]
                                                                .bodyLength)) {
                            return false;
                        }
                        ++revisionCommitChunkCount;
                        resetDeferredLoopWriteState();
                        revisionCommitLoopSlotBodyActive = true;
                    }
                    bool loopDone = false;
                    if (!stepDeferredLoopPersist(revisionCommitFile, loop, loopDone,
                                                 LoopPersistPayloadCrc::RevisionCommit)) {
                        return false;
                    }
                    if (!loopDone) {
                        return true;
                    }
                    revisionCommitLoopSlotBodyActive = false;
                    ++revisionCommitCopySlotCursor;
                }
                revisionCommitCopySlotCursor = 0;
                ++revisionCommitCopyTrackCursor;
            }
            revisionCommitSlotIndexWriteCursor = 0;
            revisionWriteStage = RevisionWriteStage::WriteSlotIndexChunk;
            return true;

        case RevisionWriteStage::WriteSlotIndexChunk: {
            if (revisionCommitSlotIndexWriteCursor == 0) {
                const uint32_t slotIndexBodySize = static_cast<uint32_t>(
                    RevisionPackedBlob::slotIndexChunkBodySize(revisionCommitSlotIndexCount));
                if (!writeRevisionCommitChunkHeader(RevisionPackedBlob::ChunkType::SlotIndex, 0, 0,
                                                    slotIndexBodySize)) {
                    return false;
                }
                ++revisionCommitChunkCount;
                const uint16_t entryCount = revisionCommitSlotIndexCount;
                const uint16_t reservedPrefix = 0;
                if (revisionCommitFile.write(reinterpret_cast<const uint8_t*>(&entryCount),
                                             sizeof(entryCount)) != sizeof(entryCount) ||
                    revisionCommitFile.write(reinterpret_cast<const uint8_t*>(&reservedPrefix),
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
                revisionCommitPayloadWriteOffset +=
                    static_cast<uint32_t>(RevisionPackedBlob::kSlotIndexBodyPrefixByteSize);
                revisionCommitSlotIndexChunkWritten = true;
            }
            if (revisionCommitSlotIndexWriteCursor < revisionCommitSlotIndexCount) {
                const StorageIo io = storageIoFromFileWrite(revisionCommitFile);
                if (!RevisionPackedBlob::writeRevisionLoopSlotDirectoryEntry(
                        io, revisionCommitSlotEntries[revisionCommitSlotIndexWriteCursor])) {
                    return false;
                }
                const RevisionPackedBlob::RevisionLoopSlotDirectoryEntry& entry =
                    revisionCommitSlotEntries[revisionCommitSlotIndexWriteCursor];
                uint8_t directoryEntryBytes[RevisionPackedBlob::kRevisionLoopSlotDirectoryEntryByteSize];
                if (!RevisionPackedBlob::revisionLoopSlotDirectoryEntryFileBytes(entry, directoryEntryBytes,
                                                                 sizeof(directoryEntryBytes))) {
                    return false;
                }
                if (!appendRevisionCommitPayloadCrc(directoryEntryBytes, sizeof(directoryEntryBytes))) {
                    return false;
                }
                revisionCommitPayloadWriteOffset +=
                    static_cast<uint32_t>(RevisionPackedBlob::kRevisionLoopSlotDirectoryEntryByteSize);
                ++revisionCommitSlotIndexWriteCursor;
                return true;
            }
            revisionWriteStage = RevisionWriteStage::WriteFooter;
            return true;
        }

        case RevisionWriteStage::WriteFooter: {
            if (!revisionCommitFile) {
                return false;
            }
            if (!writeRevisionCommitSlotIndexChunkFromFooter()) {
                return false;
            }
            revisionCommitHeader.chunkCount = revisionCommitChunkCount;
            revisionCommitHeader.payloadSize = revisionCommitPayloadWriteOffset;
            uint32_t payloadCrc = 0;
            if (!computeRevisionCommitPayloadCrcFromFile(
                    revisionCommitFile, static_cast<uint32_t>(RevisionPackedBlob::kRevisionHeaderByteSize),
                    revisionCommitHeader.payloadSize, payloadCrc)) {
                return false;
            }
            RevisionPackedBlob::RevisionFooter footer{};
            footer.svokToken = RevisionPackedBlob::kRevisionSvokFileToken;
            footer.payloadCrc32 =
                revisionCommitHeader.payloadSize == 0U ? 0U : payloadCrc;
            footer.fileSize =
                static_cast<uint32_t>(revisionCommitFile.size()) +
                static_cast<uint32_t>(RevisionPackedBlob::kRevisionFooterByteSize);
            const StorageIo footerIo = storageIoFromFileWrite(revisionCommitFile);
            if (!RevisionPackedBlob::writeRevisionFooter(footerIo, footer)) {
                return false;
            }
            revisionCommitFile.flush();
            revisionCommitFile.close();
            revisionCommitHeader.revisionId = revisionCommitPendingRevisionId;
            revisionCommitHeader.headerCrc32 =
                RevisionPackedBlob::computeRevisionHeaderChecksum(revisionCommitHeader);
            revisionCommitStage = RevisionCommitStage::Validate;
            return true;
        }
    }
    return false;
}

STORAGE_PERSIST_MEM bool stepRevisionCommitValidate() {
    File file = SD.open(revisionCommitTempPath, FILE_READ);
    if (!file) {
        return false;
    }
    const size_t fileSize = file.size();
    if (fileSize < RevisionPackedBlob::kRevisionHeaderByteSize +
                        RevisionPackedBlob::kRevisionFooterByteSize) {
        file.close();
        SD.remove(revisionCommitTempPath);
        return false;
    }

    uint8_t headerBytes[RevisionPackedBlob::kRevisionHeaderByteSize];
    if (file.read(headerBytes, sizeof(headerBytes)) != static_cast<int>(sizeof(headerBytes))) {
        file.close();
        SD.remove(revisionCommitTempPath);
        return false;
    }

    RevisionPackedBlob::RevisionHeader header{};
    if (!RevisionPackedBlob::parseRevisionHeaderFromBytes(headerBytes, sizeof(headerBytes),
                                                          header)) {
        file.close();
        SD.remove(revisionCommitTempPath);
        return false;
    }

    const size_t payloadOffset = RevisionPackedBlob::kRevisionHeaderByteSize;
    const size_t payloadSize =
        fileSize - payloadOffset - RevisionPackedBlob::kRevisionFooterByteSize;
    if (payloadSize > UINT32_MAX) {
        file.close();
        SD.remove(revisionCommitTempPath);
        return false;
    }
    if (payloadOffset + payloadSize + RevisionPackedBlob::kRevisionFooterByteSize > fileSize) {
        file.close();
        SD.remove(revisionCommitTempPath);
        return false;
    }

    uint32_t payloadCrc = 0;
    bool payloadCrcSeeded = false;
    if (payloadSize > 0) {
        if (!file.seek(payloadOffset)) {
            file.close();
            SD.remove(revisionCommitTempPath);
            return false;
        }
        uint32_t remaining = static_cast<uint32_t>(payloadSize);
        while (remaining > 0) {
            const size_t chunkSize =
                remaining > revisionCommitCopyBuffer.size() ? revisionCommitCopyBuffer.size()
                                                            : remaining;
            const int bytesRead =
                file.read(revisionCommitCopyBuffer.data(), static_cast<size_t>(chunkSize));
            if (bytesRead <= 0) {
                file.close();
                SD.remove(revisionCommitTempPath);
                return false;
            }
            const size_t written = static_cast<size_t>(bytesRead);
            if (!payloadCrcSeeded) {
                payloadCrc = PersistenceSchema::crc32(revisionCommitCopyBuffer.data(), written);
                payloadCrcSeeded = true;
            } else {
                payloadCrc = PersistenceSchema::crc32Continue(payloadCrc,
                                                              revisionCommitCopyBuffer.data(),
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
        SD.remove(revisionCommitTempPath);
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
        SD.remove(revisionCommitTempPath);
        return false;
    }

    header.revisionId = revisionCommitPendingRevisionId;
    header.chunkCount = revisionCommitChunkCount;
    header.payloadSize = static_cast<uint32_t>(payloadSize);
    header.headerCrc32 = RevisionPackedBlob::computeRevisionHeaderChecksum(header);
    revisionCommitFile = SD.open(revisionCommitTempPath, FILE_WRITE);
    if (!revisionCommitFile) {
        return false;
    }
    revisionCommitFile.seek(0);
    const StorageIo headerIo = storageIoFromFileWrite(revisionCommitFile);
    if (!RevisionPackedBlob::writeRevisionHeader(headerIo, header)) {
        revisionCommitFile.close();
        return false;
    }
    revisionCommitFile.close();

    if (!CurrentSetStorage::atomicRenameTempFile(revisionCommitTempPath,
                                                 revisionCommitFinalPath)) {
        return false;
    }
    revisionCommitHeader = header;
    revisionCommitStage = RevisionCommitStage::CatalogUpdate;
    return true;
}

STORAGE_PERSIST_MEM bool stepRevisionCommitCatalogUpdate() {
    const uint64_t updatedUnix = static_cast<uint64_t>(RtcTime::getUnixTime());
    SetRevisionCatalog::applyValidatedRevisionToSetMeta(revisionCommitSetMeta,
                                                       revisionCommitPendingRevisionId,
                                                       updatedUnix);

    char setMetaPath[64];
    char setMetaTempPath[72];
    if (!SetRevisionCatalog::formatSetMetaPath(setMetaPath, sizeof(setMetaPath),
                                               revisionCommitSetId)) {
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
    if (!SetRevisionCatalog::writeSetMetaRecord(setMetaIo, revisionCommitSetMeta)) {
        setMetaTemp.close();
        return false;
    }
    setMetaTemp.close();
    if (!CurrentSetStorage::atomicRenameTempFile(setMetaTempPath, setMetaPath)) {
        return false;
    }

    revisionCommitCatalogIndex.catalogChecksum =
        SetRevisionCatalog::computeSetCatalogIndexChecksum(revisionCommitCatalogIndex);
    File indexTemp = SD.open(SetRevisionCatalog::kSetIndexTempPath, FILE_WRITE);
    if (!indexTemp) {
        return false;
    }
    const StorageIo indexIo = storageIoFromFileWrite(indexTemp);
    if (!SetRevisionCatalog::writeSetCatalogIndex(indexIo, revisionCommitCatalogIndex)) {
        indexTemp.close();
        return false;
    }
    indexTemp.close();
    if (!CurrentSetStorage::atomicRenameTempFile(SetRevisionCatalog::kSetIndexTempPath,
                                                 SetRevisionCatalog::kSetIndexPath)) {
        return false;
    }

    revisionCommitStage = RevisionCommitStage::Complete;
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
    revisionCommitWorkspaceEpochBeforeSnapshot = 0;
    workspaceDerivedFromSetId = revisionCommitSetId;
    workspaceDerivedFromRevisionId = revisionCommitPendingRevisionId;
    workspaceLastCommittedRevisionId = revisionCommitPendingRevisionId;
    if (!writeWorkspaceMetaAfterDeferredSave()) {
        return false;
    }
    Serial.print("[StorageManager] Revision commit complete S");
    Serial.print(revisionCommitSetId);
    Serial.print(" v");
    Serial.println(revisionCommitPendingRevisionId);
    const int toastWritten = std::snprintf(
        autoSaveBeforeLoadFolderPending, sizeof(autoSaveBeforeLoadFolderPending), "S%04u_v%04u",
        revisionCommitSetId, revisionCommitPendingRevisionId);
    autoSaveBeforeLoadFolderPendingValid =
        toastWritten > 0 &&
        static_cast<size_t>(toastWritten) < sizeof(autoSaveBeforeLoadFolderPending);
#if defined(SESSION_CAPTURE)
    {
        char revCompleteDetail[24];
        std::snprintf(revCompleteDetail, sizeof(revCompleteDetail), "S%04u_v%04u",
                      revisionCommitSetId, revisionCommitPendingRevisionId);
        SC_PERSIST("rev_complete", 0, revisionCommitSetId, revisionCommitPendingRevisionId,
                   revCompleteDetail);
    }
    if (hitlRevisionCommitBackup.armed) {
        hitlRevisionCommitBackup.committedSetId = revisionCommitSetId;
        hitlRevisionCommitBackup.committedRevisionId = revisionCommitPendingRevisionId;
        hitlRevisionCommitBackup.createdNewSetFolder = revisionCommitAllocatedNewSet;
        const int folderWritten = std::snprintf(hitlRevisionCommitBackup.setFolderPath,
                                                sizeof(hitlRevisionCommitBackup.setFolderPath),
                                                "%s/S%04u", SetRevisionCatalog::kSetsRoot,
                                                revisionCommitSetId);
        const int revisionWritten = std::snprintf(
            hitlRevisionCommitBackup.revisionFinalPath,
            sizeof(hitlRevisionCommitBackup.revisionFinalPath), "%s",
            revisionCommitFinalPath);
        if (folderWritten <= 0 ||
            static_cast<size_t>(folderWritten) >= sizeof(hitlRevisionCommitBackup.setFolderPath) ||
            revisionWritten <= 0 ||
            static_cast<size_t>(revisionWritten) >=
                sizeof(hitlRevisionCommitBackup.revisionFinalPath)) {
            return false;
        }
    }
#endif
    revisionCommitStage = RevisionCommitStage::Idle;
    revisionCommitInProgress = false;
    deferredSaveCompletedAtMs = millis();
    storageSession.revisionCommit.overlayBackgroundCommit = false;
    return true;
}

STORAGE_PERSIST_MEM bool appendRevisionCommitPayloadCrc(const uint8_t* data, size_t size) {
    if (data == nullptr || size == 0) {
        return true;
    }
    if (!revisionCommitPayloadCrcSeeded) {
        revisionCommitPayloadCrc = PersistenceSchema::crc32(data, size);
        revisionCommitPayloadCrcSeeded = true;
    } else {
        revisionCommitPayloadCrc =
            PersistenceSchema::crc32Continue(revisionCommitPayloadCrc, data, size);
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
        revisionCommitPayloadWriteOffset += static_cast<uint32_t>(size);
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
    switch (revisionCommitStage) {
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

