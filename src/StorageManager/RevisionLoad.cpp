//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "StorageManagerInternal.h"

#include "CurrentSetStorage.h"
#include "CurrentWorkspaceStorage.h"
#include "GlobalUndoStack.h"
#include "Globals.h"
#include "Loop.h"
#include "LooperState.h"
#include "PersistenceSchema.h"
#include "RevisionLoadPolicy.h"
#include "RevisionPackedBlob.h"
#include "RtcTime.h"
#include "SavedSetCatalog.h"
#include "SetRevisionCatalog.h"
#include "StorageLoopIo.h"
#include "TrackManager.h"
#include "TrackUndo.h"
#include "Utils/DebugSessionCapture.h"
#include <Arduino.h>
#include <SD.h>
#include <cstdio>
#include <cstring>
#include <vector>

namespace StorageManagerInternal {

STORAGE_PERSIST_MEM void clearRevisionLoadPromptAndPipelineState() {
    storageSession.revisionLoad.requested = false;
    storageSession.revisionLoad.requestedSetId = 0;
    storageSession.revisionLoad.requestedRevisionId = 0;
    storageSession.revisionLoad.heldForWorkspaceDirty = false;
    storageSession.revisionLoad.confirmChoice = RevisionLoadPolicy::DirtyPromptChoice::None;
    storageSession.revisionLoad.loadAfterRevisionCommit = false;
}

STORAGE_PERSIST_MEM void resetRevisionLoadReloadRamState() {
    if (storageSession.revisionLoad.reloadMetaFileOpen) {
        storageSession.revisionLoad.reloadMetaFile.close();
        storageSession.revisionLoad.reloadMetaFileOpen = false;
    }
    storageSession.revisionLoad.reloadRamStage = RevisionLoadReloadRamStage::WriteWorkspaceMeta;
    storageSession.revisionLoad.reloadTrackCursor = 0;
    storageSession.revisionLoad.reloadSlotCursor = 0;
    storageSession.revisionLoad.reloadNumTracks = 0;
    storageSession.revisionLoad.reloadActiveLoopIndex.clear();
    storageSession.revisionLoad.reloadSelectedTrackIdx = 0;
    storageSession.revisionLoad.reloadLooperState = LOOPER_IDLE;
    storageSession.revisionLoad.reloadMasterLoopLength = 0;
    for (uint8_t t = 0; t < Config::NUM_TRACKS; ++t) {
        storageSession.revisionLoad.reloadAnySlotHasEvents[t] = false;
        storageSession.revisionLoad.reloadLoadedTrackState[t] = TRACK_EMPTY;
        storageSession.revisionLoad.reloadMuted[t] = false;
    }
}

STORAGE_PERSIST_MEM void dispatchStagedRevisionLoad() {
    if (!storageSession.revisionLoad.requested) {
        return;
    }
    storageSession.revisionLoad.setId = storageSession.revisionLoad.requestedSetId;
    storageSession.revisionLoad.revisionId = storageSession.revisionLoad.requestedRevisionId;
    storageSession.revisionLoad.requested = false;
    storageSession.revisionLoad.heldForWorkspaceDirty = false;
    storageSession.revisionLoad.pending = true;
    SC_PERSIST("rev_load_request", 0, storageSession.revisionLoad.setId, storageSession.revisionLoad.revisionId, "queued");
}

STORAGE_PERSIST_MEM void resetRevisionLoadJobState() {
    if (storageSession.revisionLoad.sourceFileOpen) {
        storageSession.revisionLoad.sourceFile.close();
        storageSession.revisionLoad.sourceFileOpen = false;
    }
    if (storageSession.revisionLoad.destFileOpen) {
        storageSession.revisionLoad.destFile.close();
        storageSession.revisionLoad.destFileOpen = false;
    }
    storageSession.revisionLoad.inProgress = false;
    storageSession.revisionLoad.sdIoActive = false;
    storageSession.revisionLoad.stage = RevisionLoadStage::Idle;
    storageSession.revisionLoad.writeStage = RevisionLoadWriteStage::PrepareEpoch;
    storageSession.revisionLoad.setId = 0;
    storageSession.revisionLoad.revisionId = 0;
    storageSession.revisionLoad.sourcePath[0] = '\0';
    storageSession.revisionLoad.slotIndexCount = 0;
    storageSession.revisionLoad.workspaceEpoch = 0;
    storageSession.revisionLoad.transportFileOffset = 0;
    storageSession.revisionLoad.transportBodySize = 0;
    storageSession.revisionLoad.transportReadPos = 0;
    storageSession.revisionLoad.copyTrackCursor = 0;
    storageSession.revisionLoad.copySlotCursor = 0;
    storageSession.revisionLoad.slotBodyRemaining = 0;
    storageSession.revisionLoad.slotReadPos = 0;
    storageSession.revisionLoad.writingEmptySlot = false;
    storageSession.revisionLoad.loopWriteStage = DeferredLoopWriteStage::Header;
    storageSession.revisionLoad.lastBlockedLogAtMs = 0;
    storageSession.revisionLoad.usedDefaultTransport = false;
    storageSession.revisionLoad.displayRefreshPending = false;
    storageSession.revisionLoad.header = RevisionPackedBlob::RevisionHeader{};
    storageSession.revisionLoad.slotIndexCount = 0;
    for (uint16_t i = 0; i < kMaxRevisionLoopIndexEntries; ++i) {
        storageSession.revisionLoad.slotDirectoryEntries[i] = RevisionPackedBlob::RevisionLoopSlotDirectoryEntry{};
    }
    resetRevisionLoadReloadRamState();
    clearRevisionLoadPromptAndPipelineState();
}

STORAGE_PERSIST_MEM bool findRevisionLoadSlotEntry(uint8_t trackIndex, uint8_t slotIndex,
                               RevisionPackedBlob::RevisionLoopSlotDirectoryEntry& entryOut) {
    for (uint16_t i = 0; i < storageSession.revisionLoad.slotIndexCount; ++i) {
        if (storageSession.revisionLoad.slotDirectoryEntries[i].trackIndex == trackIndex &&
            storageSession.revisionLoad.slotDirectoryEntries[i].slotIndex == slotIndex) {
            entryOut = storageSession.revisionLoad.slotDirectoryEntries[i];
            return true;
        }
    }
    return false;
}

STORAGE_PERSIST_MEM uint32_t revisionLoadLoopSlotBodyFileOffset(const RevisionPackedBlob::RevisionLoopSlotDirectoryEntry& entry) {
    return RevisionPackedBlob::kRevisionHeaderByteSize + entry.chunkOffset +
           static_cast<uint32_t>(RevisionPackedBlob::kChunkHeaderByteSize);
}

STORAGE_PERSIST_MEM bool revisionLoadRevisionLoopSlotDirectoryEntryOccupied(uint8_t trackIndex, uint8_t slotIndex) {
    for (uint16_t index = 0; index < storageSession.revisionLoad.slotIndexCount; ++index) {
        const RevisionPackedBlob::RevisionLoopSlotDirectoryEntry& entry =
            storageSession.revisionLoad.slotDirectoryEntries[index];
        if (entry.trackIndex == trackIndex && entry.slotIndex == slotIndex) {
            return entry.occupied != 0;
        }
    }
    return false;
}

STORAGE_PERSIST_MEM uint32_t revisionLoadDefaultMasterLoopLength() {
    uint32_t maxLength = 0;
    for (uint16_t index = 0; index < storageSession.revisionLoad.slotIndexCount; ++index) {
        const RevisionPackedBlob::RevisionLoopSlotDirectoryEntry& entry =
            storageSession.revisionLoad.slotDirectoryEntries[index];
        if (entry.occupied != 0 && entry.loopLengthTicks > maxLength) {
            maxLength = entry.loopLengthTicks;
        }
    }
    return maxLength;
}

STORAGE_PERSIST_MEM uint8_t revisionLoadDefaultActiveLoopIndex(uint8_t trackIndex) {
    for (uint16_t index = 0; index < storageSession.revisionLoad.slotIndexCount; ++index) {
        const RevisionPackedBlob::RevisionLoopSlotDirectoryEntry& entry =
            storageSession.revisionLoad.slotDirectoryEntries[index];
        if (entry.trackIndex == trackIndex && entry.occupied != 0) {
            return entry.slotIndex;
        }
    }
    return 0;
}

STORAGE_PERSIST_MEM bool writeDefaultRevisionLoadTransportBody(File& file) {
    if (!writeCurrentSetMetaHeaderToOpenFile(file)) {
        return false;
    }

    const float savedBpm = bpm;
    if (!writeRaw(file, &savedBpm, sizeof(savedBpm))) {
        return false;
    }

    const uint32_t looperStateVal = persistedLooperStateRaw(LOOPER_IDLE);
    if (!writeRaw(file, &looperStateVal, sizeof(looperStateVal))) {
        return false;
    }

    const uint32_t masterLoopLength = revisionLoadDefaultMasterLoopLength();
    if (!writeRaw(file, &masterLoopLength, sizeof(masterLoopLength))) {
        return false;
    }

    const uint8_t numTracks = Config::NUM_TRACKS;
    if (!writeRaw(file, &numTracks, sizeof(numTracks))) {
        return false;
    }

    for (uint8_t trackIndex = 0; trackIndex < numTracks; ++trackIndex) {
        uint32_t trackState = static_cast<uint32_t>(TRACK_EMPTY);
        for (uint8_t slotIndex = 0; slotIndex < Config::MAX_LOOPS_PER_TRACK; ++slotIndex) {
            if (revisionLoadRevisionLoopSlotDirectoryEntryOccupied(trackIndex, slotIndex)) {
                trackState = static_cast<uint32_t>(TRACK_STOPPED);
                break;
            }
        }
        const bool muted = false;
        if (!writeRaw(file, &trackState, sizeof(trackState)) || !writeRaw(file, &muted, sizeof(muted))) {
            return false;
        }

        for (uint8_t slotIndex = 0; slotIndex < Config::MAX_LOOPS_PER_TRACK; ++slotIndex) {
            const bool slotEnabled = revisionLoadRevisionLoopSlotDirectoryEntryOccupied(trackIndex, slotIndex);
            const bool slotMuted = false;
            const LoopId slotLoopId = static_cast<LoopId>(slotIndex);
            if (!writeRaw(file, &slotEnabled, sizeof(slotEnabled)) ||
                !writeRaw(file, &slotMuted, sizeof(slotMuted)) ||
                !writeRaw(file, &slotLoopId, sizeof(slotLoopId))) {
                return false;
            }
        }
    }

    const uint8_t selectedTrackIdx = trackManager.getSelectedTrackIndex();
    if (!writeRaw(file, &selectedTrackIdx, sizeof(selectedTrackIdx))) {
        return false;
    }
    for (uint8_t trackIndex = 0; trackIndex < numTracks; ++trackIndex) {
        const uint8_t activeLoopIndex = revisionLoadDefaultActiveLoopIndex(trackIndex);
        if (!writeRaw(file, &activeLoopIndex, sizeof(activeLoopIndex))) {
            return false;
        }
    }

    if (!writeRaw(file, &kGlobalUndoStackToken, sizeof(kGlobalUndoStackToken))) {
        return false;
    }

    GlobalUndoStack emptyUndoStack{};
    emptyUndoStack.clear();
    for (uint8_t trackIndex = 0; trackIndex < numTracks; ++trackIndex) {
        if (!writeGlobalUndoStackToFile(file, emptyUndoStack)) {
            return false;
        }
    }
    return true;
}

STORAGE_PERSIST_MEM bool readRevisionChunkHeaderFromFile(File& file, uint32_t fileOffset,
                                     RevisionPackedBlob::ChunkHeader& chunkHeaderOut) {
    if (!file.seek(fileOffset)) {
        return false;
    }
    const StorageIo io = storageIoFromFileRead(file);
    return RevisionPackedBlob::readChunkHeader(io, chunkHeaderOut);
}

STORAGE_PERSIST_MEM bool findChunkBodyInRevisionFile(File& file, size_t fileSize,
                                 const RevisionPackedBlob::RevisionHeader& header,
                                 RevisionPackedBlob::ChunkType type, uint8_t trackIndex,
                                 uint8_t slotIndex, uint32_t& bodyOffsetInFileOut,
                                 uint32_t& bodyLengthOut) {
    bodyOffsetInFileOut = 0;
    bodyLengthOut = 0;
    if (header.payloadSize == 0) {
        return false;
    }
    const size_t payloadOffset = RevisionPackedBlob::kRevisionHeaderByteSize;
    if (payloadOffset + header.payloadSize > fileSize) {
        return false;
    }

    size_t cursor = 0;
    while (cursor + RevisionPackedBlob::kChunkHeaderByteSize <= header.payloadSize) {
        RevisionPackedBlob::ChunkHeader chunkHeader{};
        if (!readRevisionChunkHeaderFromFile(file,
                                             static_cast<uint32_t>(payloadOffset + cursor),
                                             chunkHeader)) {
            return false;
        }
        const size_t bodyStart = cursor + RevisionPackedBlob::kChunkHeaderByteSize;
        const size_t bodyEnd = bodyStart + static_cast<size_t>(chunkHeader.bodyLength);
        if (bodyEnd > header.payloadSize) {
            return false;
        }
        if (chunkHeader.type == static_cast<uint8_t>(type) &&
            chunkHeader.trackIndex == trackIndex && chunkHeader.slotIndex == slotIndex) {
            bodyOffsetInFileOut = static_cast<uint32_t>(payloadOffset + bodyStart);
            bodyLengthOut = chunkHeader.bodyLength;
            return true;
        }
        cursor = bodyEnd;
    }
    return false;
}

STORAGE_PERSIST_MEM bool readRevisionLoopSlotDirectoryEntryCountFromRevisionFile(File& file, size_t fileSize,
                                             const RevisionPackedBlob::RevisionHeader& header,
                                             uint16_t& entryCountOut) {
    entryCountOut = 0;
    uint32_t bodyOffsetInFile = 0;
    uint32_t bodyLength = 0;
    if (!findChunkBodyInRevisionFile(file, fileSize, header,
                                     RevisionPackedBlob::ChunkType::SlotIndex, 0, 0,
                                     bodyOffsetInFile, bodyLength)) {
        return false;
    }
    if (bodyLength < RevisionPackedBlob::kSlotIndexBodyPrefixByteSize) {
        return false;
    }
    if (!file.seek(bodyOffsetInFile) ||
        file.read(reinterpret_cast<uint8_t*>(&entryCountOut), sizeof(entryCountOut)) !=
            static_cast<int>(sizeof(entryCountOut))) {
        return false;
    }
    const size_t expectedBodySize =
        RevisionPackedBlob::kSlotIndexBodyPrefixByteSize +
        static_cast<size_t>(entryCountOut) * RevisionPackedBlob::kRevisionLoopSlotDirectoryEntryByteSize;
    return bodyLength >= expectedBodySize;
}

STORAGE_PERSIST_MEM bool readSlotIndexEntriesFromRevisionFile(
    File& file, size_t fileSize, const RevisionPackedBlob::RevisionHeader& header,
    RevisionPackedBlob::RevisionLoopSlotDirectoryEntry* entriesOut, uint16_t maxEntries,
    uint16_t& entryCountOut) {
    entryCountOut = 0;
    if (entriesOut == nullptr || maxEntries == 0) {
        return false;
    }

    uint16_t totalEntries = 0;
    if (!readRevisionLoopSlotDirectoryEntryCountFromRevisionFile(file, fileSize, header, totalEntries)) {
        return false;
    }

    uint32_t bodyOffsetInFile = 0;
    uint32_t bodyLength = 0;
    if (!findChunkBodyInRevisionFile(file, fileSize, header,
                                     RevisionPackedBlob::ChunkType::SlotIndex, 0, 0,
                                     bodyOffsetInFile, bodyLength)) {
        return false;
    }

    for (uint16_t index = 0; index < totalEntries; ++index) {
        if (index >= maxEntries) {
            return false;
        }
        const uint32_t entryOffset =
            bodyOffsetInFile + RevisionPackedBlob::kSlotIndexBodyPrefixByteSize +
            static_cast<uint32_t>(index) * RevisionPackedBlob::kRevisionLoopSlotDirectoryEntryByteSize;
        if (entryOffset + RevisionPackedBlob::kRevisionLoopSlotDirectoryEntryByteSize >
            bodyOffsetInFile + bodyLength) {
            return false;
        }
        if (!file.seek(entryOffset)) {
            return false;
        }
        const StorageIo io = storageIoFromFileRead(file);
        if (!RevisionPackedBlob::readRevisionLoopSlotDirectoryEntry(io, entriesOut[index])) {
            return false;
        }
        ++entryCountOut;
    }
    return true;
}

STORAGE_PERSIST_MEM bool validateRevisionLoadFileFooterFromSd(File& file, size_t fileSize,
                                          RevisionPackedBlob::RevisionHeader& headerOut) {
    if (fileSize < RevisionPackedBlob::kRevisionHeaderByteSize +
                        RevisionPackedBlob::kRevisionFooterByteSize) {
        return false;
    }
    uint8_t headerBytes[RevisionPackedBlob::kRevisionHeaderByteSize];
    if (!file.seek(0) ||
        file.read(headerBytes, sizeof(headerBytes)) != static_cast<int>(sizeof(headerBytes)) ||
        !RevisionPackedBlob::parseRevisionHeaderFromBytes(headerBytes, sizeof(headerBytes),
                                                          headerOut)) {
        return false;
    }

    const size_t payloadOffset = RevisionPackedBlob::kRevisionHeaderByteSize;
    const size_t payloadSize =
        fileSize - payloadOffset - RevisionPackedBlob::kRevisionFooterByteSize;
    if (payloadSize > UINT32_MAX) {
        return false;
    }

    uint32_t payloadCrc = 0;
    if (!computeRevisionCommitPayloadCrcFromFile(
            file, static_cast<uint32_t>(payloadOffset), static_cast<uint32_t>(payloadSize),
            payloadCrc)) {
        return false;
    }

    RevisionPackedBlob::RevisionFooter footer{};
    if (!file.seek(fileSize - RevisionPackedBlob::kRevisionFooterByteSize) ||
        !readRaw(file, &footer.svokToken, sizeof(footer.svokToken)) ||
        !readRaw(file, &footer.payloadCrc32, sizeof(footer.payloadCrc32)) ||
        !readRaw(file, &footer.fileSize, sizeof(footer.fileSize))) {
        return false;
    }

    if (footer.svokToken != RevisionPackedBlob::kRevisionSvokFileToken ||
        footer.fileSize != static_cast<uint32_t>(fileSize) ||
        footer.payloadCrc32 != payloadCrc) {
        Serial.print("[StorageManager] ERROR: Revision load SD footer mismatch footerCrc=");
        Serial.print(footer.payloadCrc32);
        Serial.print(" expectedCrc=");
        Serial.print(payloadCrc);
        Serial.print(" payloadSize=");
        Serial.println(static_cast<uint32_t>(payloadSize));
        return false;
    }

    if (headerOut.payloadSize != static_cast<uint32_t>(payloadSize)) {
        headerOut.payloadSize = static_cast<uint32_t>(payloadSize);
    }
    return true;
}

STORAGE_PERSIST_MEM bool beginRevisionLoadValidate() {
    if (!SetRevisionCatalog::formatRevisionPath(storageSession.revisionLoad.sourcePath,
                                                sizeof(storageSession.revisionLoad.sourcePath),
                                                storageSession.revisionLoad.setId, storageSession.revisionLoad.revisionId,
                                                false)) {
        return false;
    }
    if (!SD.exists(storageSession.revisionLoad.sourcePath)) {
        Serial.print("[StorageManager] ERROR: Revision file missing ");
        Serial.println(storageSession.revisionLoad.sourcePath);
        return false;
    }

    File file = SD.open(storageSession.revisionLoad.sourcePath, FILE_READ);
    if (!file) {
        return false;
    }
    const size_t fileSize = file.size();
    if (fileSize < RevisionPackedBlob::kRevisionHeaderByteSize +
                        RevisionPackedBlob::kRevisionFooterByteSize) {
        file.close();
        return false;
    }

    if (!validateRevisionLoadFileFooterFromSd(file, fileSize, storageSession.revisionLoad.header)) {
        file.close();
        return false;
    }

    storageSession.revisionLoad.slotIndexCount = 0;
    storageSession.revisionLoad.transportFileOffset = 0;
    storageSession.revisionLoad.transportBodySize = 0;
    storageSession.revisionLoad.usedDefaultTransport = false;

    if (!findChunkBodyInRevisionFile(file, fileSize, storageSession.revisionLoad.header,
                                     RevisionPackedBlob::ChunkType::Transport, 0, 0,
                                     storageSession.revisionLoad.transportFileOffset,
                                     storageSession.revisionLoad.transportBodySize) ||
        storageSession.revisionLoad.transportBodySize == 0) {
        storageSession.revisionLoad.transportFileOffset = 0;
        storageSession.revisionLoad.transportBodySize = 0;
        storageSession.revisionLoad.usedDefaultTransport = true;
        Serial.println(
            "[StorageManager] Revision load missing Transport chunk; using default transport");
    }

    if (!readSlotIndexEntriesFromRevisionFile(file, fileSize, storageSession.revisionLoad.header,
                                              storageSession.revisionLoad.slotDirectoryEntries,
                                              kMaxRevisionLoopIndexEntries,
                                              storageSession.revisionLoad.slotIndexCount)) {
        uint16_t slotIndexEntryCount = 0;
        const bool hasSlotIndexChunk = readRevisionLoopSlotDirectoryEntryCountFromRevisionFile(
            file, fileSize, storageSession.revisionLoad.header, slotIndexEntryCount);
        file.close();
        if (!hasSlotIndexChunk) {
            storageSession.revisionLoad.slotIndexCount = 0;
        } else {
            Serial.print("[StorageManager] ERROR: Revision load slot index parse failed fileSize=");
            Serial.print(static_cast<uint32_t>(fileSize));
            Serial.print(" hdrChunkCount=");
            Serial.print(storageSession.revisionLoad.header.chunkCount);
            Serial.print(" hdrPayloadSize=");
            Serial.print(storageSession.revisionLoad.header.payloadSize);
            Serial.print(" slotIndexEntryCount=");
            Serial.println(slotIndexEntryCount);
            return false;
        }
    } else {
        file.close();
    }

    storageSession.revisionLoad.writeStage = RevisionLoadWriteStage::PrepareEpoch;
    storageSession.revisionLoad.stage = RevisionLoadStage::Write;
    return true;
}

STORAGE_PERSIST_MEM bool openRevisionLoadLoopSlotTemp(uint8_t trackIndex, uint8_t slotIndex) {
    if (storageSession.revisionLoad.destFileOpen) {
        storageSession.revisionLoad.destFile.close();
        storageSession.revisionLoad.destFileOpen = false;
    }
    char tempPath[48];
    if (!CurrentSetStorage::formatLoopSlotTempPath(tempPath, sizeof(tempPath), trackIndex,
                                                   slotIndex)) {
        return false;
    }
    storageSession.revisionLoad.destFile = SD.open(tempPath, FILE_WRITE);
    if (!storageSession.revisionLoad.destFile) {
        return false;
    }
    storageSession.revisionLoad.destFile.seek(0);
    if (!CurrentWorkspaceStorage::writeEpochHeaderPlaceholder(storageSession.revisionLoad.destFile,
                                                              storageSession.revisionLoad.workspaceEpoch)) {
        storageSession.revisionLoad.destFile.close();
        return false;
    }
    storageSession.revisionLoad.destFileOpen = true;
    return true;
}

STORAGE_PERSIST_MEM bool finalizeRevisionLoadLoopSlotTemp(uint8_t trackIndex, uint8_t slotIndex) {
    if (!storageSession.revisionLoad.destFileOpen) {
        return false;
    }
    if (!writeRaw(storageSession.revisionLoad.destFile, &CurrentSetStorage::kSaveFileToken, sizeof(CurrentSetStorage::kSaveFileToken))) {
        storageSession.revisionLoad.destFile.close();
        storageSession.revisionLoad.destFileOpen = false;
        return false;
    }
    storageSession.revisionLoad.destFile.close();
    storageSession.revisionLoad.destFileOpen = false;

    char tempPath[48];
    char finalPath[48];
    if (!CurrentSetStorage::formatLoopSlotTempPath(tempPath, sizeof(tempPath), trackIndex,
                                                   slotIndex) ||
        !CurrentSetStorage::formatLoopSlotPath(finalPath, sizeof(finalPath), trackIndex,
                                               slotIndex)) {
        return false;
    }
    if (!CurrentWorkspaceStorage::finalizeEpochFileHeaderCrc(tempPath)) {
        return false;
    }
    if (!CurrentSetStorage::verifySaveFileTokenAtPath(tempPath)) {
        return false;
    }
    return CurrentSetStorage::atomicRenameTempFile(tempPath, finalPath);
}

STORAGE_PERSIST_MEM bool stepRevisionLoadWrite() {
    switch (storageSession.revisionLoad.writeStage) {
        case RevisionLoadWriteStage::PrepareEpoch:
            ++currentWorkspaceEpoch;
            storageSession.revisionLoad.workspaceEpoch = currentWorkspaceEpoch;
            if (!CurrentSetStorage::ensureDirectory(PersistenceLayout::kRoot) ||
                !CurrentSetStorage::ensureDirectory(CurrentSetStorage::kCurrentSetDir) ||
                !CurrentSetStorage::ensureDirectory(CurrentWorkspaceStorage::kCurrentTempDir) ||
                !CurrentSetStorage::ensureDirectory(CurrentSetStorage::kCurrentSlotsDir)) {
                return false;
            }
            storageSession.revisionLoad.writeStage = RevisionLoadWriteStage::OpenMetaTemp;
            return true;

        case RevisionLoadWriteStage::OpenMetaTemp:
            storageSession.revisionLoad.destFile = SD.open(CurrentSetStorage::kCurrentMetaTempPath, FILE_WRITE);
            if (!storageSession.revisionLoad.destFile) {
                return false;
            }
            storageSession.revisionLoad.destFile.seek(0);
            if (!CurrentWorkspaceStorage::writeEpochHeaderPlaceholder(storageSession.revisionLoad.destFile,
                                                                      storageSession.revisionLoad.workspaceEpoch)) {
                storageSession.revisionLoad.destFile.close();
                return false;
            }
            storageSession.revisionLoad.destFileOpen = true;
            storageSession.revisionLoad.transportReadPos = 0;
            storageSession.revisionLoad.writeStage = RevisionLoadWriteStage::CopyTransportBody;
            return true;

        case RevisionLoadWriteStage::CopyTransportBody:
            if (storageSession.revisionLoad.transportBodySize == 0) {
                if (!writeDefaultRevisionLoadTransportBody(storageSession.revisionLoad.destFile)) {
                    Serial.println(
                        "[StorageManager] ERROR: Revision load failed writing default transport");
                    storageSession.revisionLoad.destFile.close();
                    storageSession.revisionLoad.destFileOpen = false;
                    return false;
                }
                storageSession.revisionLoad.writeStage = RevisionLoadWriteStage::FinalizeMetaTemp;
                return true;
            }
            if (!storageSession.revisionLoad.sourceFileOpen) {
                storageSession.revisionLoad.sourceFile = SD.open(storageSession.revisionLoad.sourcePath, FILE_READ);
                if (!storageSession.revisionLoad.sourceFile) {
                    return false;
                }
                storageSession.revisionLoad.sourceFileOpen = true;
            }
            if (storageSession.revisionLoad.transportReadPos == 0) {
                storageSession.revisionLoad.transportReadPos = storageSession.revisionLoad.transportFileOffset;
            }
            {
                const uint32_t endPos =
                    storageSession.revisionLoad.transportFileOffset + storageSession.revisionLoad.transportBodySize;
                uint32_t remaining = endPos - storageSession.revisionLoad.transportReadPos;
                if (remaining > 0) {
                    if (!copyRevisionCommitChunk(
                            storageSession.revisionLoad.destFile, storageSession.revisionLoad.sourceFile,
                            storageSession.revisionLoad.transportReadPos, remaining,
                            static_cast<uint32_t>(storageSession.revisionCommit.copyBuffer.size()))) {
                        return false;
                    }
                    if (remaining > 0) {
                        return true;
                    }
                }
            }
            if (storageSession.revisionLoad.sourceFileOpen) {
                storageSession.revisionLoad.sourceFile.close();
                storageSession.revisionLoad.sourceFileOpen = false;
            }
            storageSession.revisionLoad.writeStage = RevisionLoadWriteStage::FinalizeMetaTemp;
            return true;

        case RevisionLoadWriteStage::FinalizeMetaTemp: {
            if (!storageSession.revisionLoad.destFileOpen) {
                return false;
            }
            if (!writeRaw(storageSession.revisionLoad.destFile, &CurrentSetStorage::kSaveFileToken,
                          sizeof(CurrentSetStorage::kSaveFileToken))) {
                storageSession.revisionLoad.destFile.close();
                storageSession.revisionLoad.destFileOpen = false;
                return false;
            }
            storageSession.revisionLoad.destFile.close();
            storageSession.revisionLoad.destFileOpen = false;
            if (!CurrentWorkspaceStorage::finalizeEpochFileHeaderCrc(
                    CurrentSetStorage::kCurrentMetaTempPath)) {
                return false;
            }
            if (!CurrentSetStorage::verifySaveFileTokenAtPath(CurrentSetStorage::kCurrentMetaTempPath)) {
                return false;
            }
            if (!CurrentSetStorage::atomicRenameTempFile(CurrentSetStorage::kCurrentMetaTempPath,
                                                           CurrentSetStorage::kCurrentMetaPath)) {
                return false;
            }
            storageSession.revisionLoad.copyTrackCursor = 0;
            storageSession.revisionLoad.copySlotCursor = 0;
            storageSession.revisionLoad.slotBodyRemaining = 0;
            storageSession.revisionLoad.writeStage = RevisionLoadWriteStage::WriteLoopSlots;
            return true;
        }

        case RevisionLoadWriteStage::WriteLoopSlots:
            while (storageSession.revisionLoad.copyTrackCursor < Config::NUM_TRACKS) {
                while (storageSession.revisionLoad.copySlotCursor < Config::MAX_LOOPS_PER_TRACK) {
                    const uint8_t trackIndex = storageSession.revisionLoad.copyTrackCursor;
                    const uint8_t slotIndex = storageSession.revisionLoad.copySlotCursor;
                    RevisionPackedBlob::RevisionLoopSlotDirectoryEntry slotEntry{};
                    const bool hasSlotEntry =
                        findRevisionLoadSlotEntry(trackIndex, slotIndex, slotEntry);

                    if (storageSession.revisionLoad.slotBodyRemaining == 0 && !storageSession.revisionLoad.destFileOpen &&
                        !storageSession.revisionLoad.writingEmptySlot) {
                        if (!openRevisionLoadLoopSlotTemp(trackIndex, slotIndex)) {
                            return false;
                        }
                        if (hasSlotEntry) {
                            storageSession.revisionLoad.writingEmptySlot = false;
                            storageSession.revisionLoad.slotReadPos =
                                revisionLoadLoopSlotBodyFileOffset(slotEntry);
                            storageSession.revisionLoad.slotBodyRemaining = slotEntry.bodyLength;
                            storageSession.revisionLoad.loopWriteStage = DeferredLoopWriteStage::Header;
                        } else {
                            storageSession.revisionLoad.writingEmptySlot = true;
                            storageSession.revisionLoad.loopWriteStage = DeferredLoopWriteStage::Header;
                        }
                    }

                    if (storageSession.revisionLoad.writingEmptySlot) {
                        bool loopDone = false;
                        if (!stepDeferredEmptyLoopPersist(
                                storageSession.revisionLoad.destFile, static_cast<LoopId>(slotIndex), loopDone)) {
                            return false;
                        }
                        if (!loopDone) {
                            return true;
                        }
                    } else if (storageSession.revisionLoad.slotBodyRemaining > 0) {
                        if (!storageSession.revisionLoad.sourceFileOpen) {
                            storageSession.revisionLoad.sourceFile = SD.open(storageSession.revisionLoad.sourcePath, FILE_READ);
                            if (!storageSession.revisionLoad.sourceFile) {
                                return false;
                            }
                            storageSession.revisionLoad.sourceFileOpen = true;
                        }
                        if (!copyRevisionCommitChunk(
                                storageSession.revisionLoad.destFile, storageSession.revisionLoad.sourceFile,
                                storageSession.revisionLoad.slotReadPos, storageSession.revisionLoad.slotBodyRemaining,
                                static_cast<uint32_t>(storageSession.revisionCommit.copyBuffer.size()))) {
                            return false;
                        }
                        if (storageSession.revisionLoad.slotBodyRemaining > 0) {
                            return true;
                        }
                        if (storageSession.revisionLoad.sourceFileOpen) {
                            storageSession.revisionLoad.sourceFile.close();
                            storageSession.revisionLoad.sourceFileOpen = false;
                        }
                    }

                    if (!finalizeRevisionLoadLoopSlotTemp(trackIndex, slotIndex)) {
                        return false;
                    }
                    storageSession.revisionLoad.writingEmptySlot = false;
                    storageSession.revisionLoad.slotBodyRemaining = 0;
                    ++storageSession.revisionLoad.copySlotCursor;
                }
                storageSession.revisionLoad.copySlotCursor = 0;
                ++storageSession.revisionLoad.copyTrackCursor;
            }
            storageSession.revisionLoad.stage = RevisionLoadStage::ReloadRam;
            return true;
    }
    return false;
}

STORAGE_PERSIST_MEM bool stepRevisionLoadReloadRam(LooperState& state) {
    switch (storageSession.revisionLoad.reloadRamStage) {
        case RevisionLoadReloadRamStage::WriteWorkspaceMeta:
            currentWorkspaceEpoch = storageSession.revisionLoad.workspaceEpoch;
            lastCommittedWorkspaceEpoch = storageSession.revisionLoad.workspaceEpoch;
            workspaceDerivedFromSetId = storageSession.revisionLoad.header.setId;
            workspaceDerivedFromRevisionId = storageSession.revisionLoad.header.revisionId;
            if (!writeWorkspaceMetaAfterDeferredSave()) {
                Serial.println("[StorageManager] ERROR: Revision load failed writing workspace.bin");
                return false;
            }
            resetTracksAfterFailedLoad();
            storageSession.revisionLoad.reloadMetaFile =
                SD.open(CurrentSetStorage::kCurrentRuntimeBundlePath, FILE_READ);
            if (!storageSession.revisionLoad.reloadMetaFile) {
                Serial.println("[StorageManager] ERROR: Revision load failed opening runtime bundle");
                return false;
            }
            storageSession.revisionLoad.reloadMetaFileOpen = true;
            storageSession.revisionLoad.reloadRamStage = RevisionLoadReloadRamStage::ReadMetaHeaders;
            return true;

        case RevisionLoadReloadRamStage::ReadMetaHeaders: {
            if (!readCurrentSetFilePreamble(storageSession.revisionLoad.reloadMetaFile, storageSession.revisionLoad.reloadLooperState,
                                            storageSession.revisionLoad.reloadMasterLoopLength,
                                            storageSession.revisionLoad.reloadNumTracks)) {
                return false;
            }
            storageSession.revisionLoad.reloadActiveLoopIndex.assign(storageSession.revisionLoad.reloadNumTracks, 0);
            for (uint8_t t = 0; t < storageSession.revisionLoad.reloadNumTracks; ++t) {
                Track& track = trackManager.getTrack(t);
                if (!readCurrentSetTrackSlotMetadata(storageSession.revisionLoad.reloadMetaFile, t, track,
                                                     storageSession.revisionLoad.reloadLoadedTrackState[t],
                                                     storageSession.revisionLoad.reloadMuted[t])) {
                    return false;
                }
                storageSession.revisionLoad.reloadAnySlotHasEvents[t] = false;
            }
            storageSession.revisionLoad.reloadTrackCursor = 0;
            storageSession.revisionLoad.reloadSlotCursor = 0;
            storageSession.revisionLoad.reloadRamStage = RevisionLoadReloadRamStage::LoadLoopSlot;
            return true;
        }

        case RevisionLoadReloadRamStage::LoadLoopSlot: {
            if (storageSession.revisionLoad.reloadTrackCursor >= storageSession.revisionLoad.reloadNumTracks) {
                storageSession.revisionLoad.reloadRamStage = RevisionLoadReloadRamStage::ReadFooter;
                return true;
            }
            Track& track = trackManager.getTrack(storageSession.revisionLoad.reloadTrackCursor);
            if (!loadLoopSlotFromCurrentSetSd(storageSession.revisionLoad.reloadTrackCursor,
                                              storageSession.revisionLoad.reloadSlotCursor, track,
                                              storageSession.revisionLoad.reloadAnySlotHasEvents
                                                  [storageSession.revisionLoad.reloadTrackCursor])) {
                return false;
            }
            ++storageSession.revisionLoad.reloadSlotCursor;
            if (storageSession.revisionLoad.reloadSlotCursor >= Config::MAX_LOOPS_PER_TRACK) {
                applyLoadedTrackStateAfterLoopSlots(
                    track, storageSession.revisionLoad.reloadLoadedTrackState[storageSession.revisionLoad.reloadTrackCursor],
                    storageSession.revisionLoad.reloadAnySlotHasEvents[storageSession.revisionLoad.reloadTrackCursor],
                    storageSession.revisionLoad.reloadMuted[storageSession.revisionLoad.reloadTrackCursor]);
                ++storageSession.revisionLoad.reloadTrackCursor;
                storageSession.revisionLoad.reloadSlotCursor = 0;
            }
            return true;
        }

        case RevisionLoadReloadRamStage::ReadFooter: {
            if (!readCurrentSetFileEpilogue(storageSession.revisionLoad.reloadMetaFile, storageSession.revisionLoad.reloadNumTracks,
                                            storageSession.revisionLoad.reloadActiveLoopIndex,
                                            storageSession.revisionLoad.reloadSelectedTrackIdx)) {
                return false;
            }
            if (storageSession.revisionLoad.reloadMetaFileOpen) {
                storageSession.revisionLoad.reloadMetaFile.close();
                storageSession.revisionLoad.reloadMetaFileOpen = false;
            }
            if (!applyLoadedTransportFooter(storageSession.revisionLoad.reloadNumTracks,
                                            storageSession.revisionLoad.reloadActiveLoopIndex,
                                            storageSession.revisionLoad.reloadSelectedTrackIdx, state,
                                            storageSession.revisionLoad.reloadLooperState,
                                            storageSession.revisionLoad.reloadMasterLoopLength)) {
                return false;
            }
            forceCurrentSetFullLoopWrite = false;
            syncCurrentSetDirtyTrackingFromLoadedState();
            clearCurrentSetLoadedFromFolder();
            currentSetAnchorFields = CurrentSetStorage::AnchorFields{};
            resetRevisionLoadReloadRamState();
            storageSession.revisionLoad.stage = RevisionLoadStage::Complete;
            return true;
        }
    }
    return false;
}

STORAGE_PERSIST_MEM bool stepRevisionLoadComplete() {
    Serial.print("[StorageManager] Revision load complete S");
    Serial.print(storageSession.revisionLoad.setId);
    Serial.print(" v");
    Serial.println(storageSession.revisionLoad.revisionId);
#if defined(SESSION_CAPTURE)
    char revLoadDetail[40];
    if (storageSession.revisionLoad.usedDefaultTransport) {
        std::snprintf(revLoadDetail, sizeof(revLoadDetail), "S%04u_v%04u,default_transport",
                      storageSession.revisionLoad.setId, storageSession.revisionLoad.revisionId);
    } else {
        std::snprintf(revLoadDetail, sizeof(revLoadDetail), "S%04u_v%04u", storageSession.revisionLoad.setId,
                      storageSession.revisionLoad.revisionId);
    }
    SC_PERSIST("rev_load_complete", 0, storageSession.revisionLoad.setId, storageSession.revisionLoad.revisionId, revLoadDetail);
#endif
    storageSession.revisionLoad.lastDisplaySetId = storageSession.revisionLoad.setId;
    storageSession.revisionLoad.lastDisplayRevisionId = storageSession.revisionLoad.revisionId;
    storageSession.revisionLoad.completedAtMs = millis();
    storageSession.revisionLoad.failedAtMs = 0;
    storageSession.revisionLoad.displayRefreshPending = true;
    storageSession.revisionLoad.stage = RevisionLoadStage::Idle;
    storageSession.revisionLoad.inProgress = false;
    if (looperState.isLoadSaveModeActive()) {
        looperState.exitLoadSaveMode();
    }
    return true;
}

STORAGE_PERSIST_MEM bool stepRevisionLoadJob(LooperState& state) {
    switch (storageSession.revisionLoad.stage) {
        case RevisionLoadStage::Idle:
            return true;

        case RevisionLoadStage::Validate:
            return beginRevisionLoadValidate();

        case RevisionLoadStage::Write:
            return stepRevisionLoadWrite();

        case RevisionLoadStage::ReloadRam:
            return stepRevisionLoadReloadRam(state);

        case RevisionLoadStage::Complete:
            return stepRevisionLoadComplete();
    }
    return false;
}

STORAGE_PERSIST_MEM bool readSetLatestRevisionIdFromSd(uint16_t setId, uint16_t& latestRevisionIdOut) {
    latestRevisionIdOut = 0;
    char setMetaPath[64];
    if (!SetRevisionCatalog::formatSetMetaPath(setMetaPath, sizeof(setMetaPath), setId)) {
        return false;
    }
    if (!SD.exists(setMetaPath)) {
        return false;
    }
    File file = SD.open(setMetaPath, FILE_READ);
    if (!file) {
        return false;
    }
    SetRevisionCatalog::SetMetaRecord meta{};
    const StorageIo io = storageIoFromFileRead(file);
    const bool ok = SetRevisionCatalog::readSetMetaRecord(io, meta);
    file.close();
    if (!ok || meta.latestRevisionId == 0) {
        return false;
    }
    latestRevisionIdOut = meta.latestRevisionId;
    return true;
}

}  // namespace StorageManagerInternal

