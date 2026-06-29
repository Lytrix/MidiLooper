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
    if (revisionLoadReloadMetaFileOpen) {
        revisionLoadReloadMetaFile.close();
        revisionLoadReloadMetaFileOpen = false;
    }
    revisionLoadReloadRamStage = RevisionLoadReloadRamStage::WriteWorkspaceMeta;
    revisionLoadReloadTrackCursor = 0;
    revisionLoadReloadSlotCursor = 0;
    revisionLoadReloadNumTracks = 0;
    revisionLoadReloadActiveLoopIndex.clear();
    revisionLoadReloadSelectedTrackIdx = 0;
    revisionLoadReloadLooperState = LOOPER_IDLE;
    revisionLoadReloadMasterLoopLength = 0;
    for (uint8_t t = 0; t < Config::NUM_TRACKS; ++t) {
        revisionLoadReloadAnySlotHasEvents[t] = false;
        revisionLoadReloadLoadedTrackState[t] = TRACK_EMPTY;
        revisionLoadReloadMuted[t] = false;
    }
}

STORAGE_PERSIST_MEM void dispatchStagedRevisionLoad() {
    if (!storageSession.revisionLoad.requested) {
        return;
    }
    revisionLoadSetId = storageSession.revisionLoad.requestedSetId;
    revisionLoadRevisionId = storageSession.revisionLoad.requestedRevisionId;
    storageSession.revisionLoad.requested = false;
    storageSession.revisionLoad.heldForWorkspaceDirty = false;
    storageSession.revisionLoad.pending = true;
    SC_PERSIST("rev_load_request", 0, revisionLoadSetId, revisionLoadRevisionId, "queued");
}

STORAGE_PERSIST_MEM void resetRevisionLoadJobState() {
    if (revisionLoadSourceFileOpen) {
        revisionLoadSourceFile.close();
        revisionLoadSourceFileOpen = false;
    }
    if (revisionLoadDestFileOpen) {
        revisionLoadDestFile.close();
        revisionLoadDestFileOpen = false;
    }
    storageSession.revisionLoad.inProgress = false;
    storageSession.revisionLoad.sdIoActive = false;
    revisionLoadStage = RevisionLoadStage::Idle;
    revisionLoadWriteStage = RevisionLoadWriteStage::PrepareEpoch;
    revisionLoadSetId = 0;
    revisionLoadRevisionId = 0;
    revisionLoadSourcePath[0] = '\0';
    revisionLoadSlotIndexCount = 0;
    revisionLoadWorkspaceEpoch = 0;
    revisionLoadTransportFileOffset = 0;
    revisionLoadTransportBodySize = 0;
    revisionLoadTransportReadPos = 0;
    revisionLoadCopyTrackCursor = 0;
    revisionLoadCopySlotCursor = 0;
    revisionLoadSlotBodyRemaining = 0;
    revisionLoadSlotReadPos = 0;
    revisionLoadWritingEmptySlot = false;
    revisionLoadLoopWriteStage = DeferredLoopWriteStage::Header;
    lastRevisionLoadBlockedLogAtMs = 0;
    revisionLoadUsedDefaultTransport = false;
    revisionLoadDisplayRefreshPending = false;
    revisionLoadHeader = RevisionPackedBlob::RevisionHeader{};
    revisionLoadSlotIndexCount = 0;
    for (uint16_t i = 0; i < kMaxRevisionLoopIndexEntries; ++i) {
        revisionLoadSlotDirectoryEntries[i] = RevisionPackedBlob::RevisionLoopSlotDirectoryEntry{};
    }
    resetRevisionLoadReloadRamState();
    clearRevisionLoadPromptAndPipelineState();
}

STORAGE_PERSIST_MEM bool findRevisionLoadSlotEntry(uint8_t trackIndex, uint8_t slotIndex,
                               RevisionPackedBlob::RevisionLoopSlotDirectoryEntry& entryOut) {
    for (uint16_t i = 0; i < revisionLoadSlotIndexCount; ++i) {
        if (revisionLoadSlotDirectoryEntries[i].trackIndex == trackIndex &&
            revisionLoadSlotDirectoryEntries[i].slotIndex == slotIndex) {
            entryOut = revisionLoadSlotDirectoryEntries[i];
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
    for (uint16_t index = 0; index < revisionLoadSlotIndexCount; ++index) {
        const RevisionPackedBlob::RevisionLoopSlotDirectoryEntry& entry =
            revisionLoadSlotDirectoryEntries[index];
        if (entry.trackIndex == trackIndex && entry.slotIndex == slotIndex) {
            return entry.occupied != 0;
        }
    }
    return false;
}

STORAGE_PERSIST_MEM uint32_t revisionLoadDefaultMasterLoopLength() {
    uint32_t maxLength = 0;
    for (uint16_t index = 0; index < revisionLoadSlotIndexCount; ++index) {
        const RevisionPackedBlob::RevisionLoopSlotDirectoryEntry& entry =
            revisionLoadSlotDirectoryEntries[index];
        if (entry.occupied != 0 && entry.loopLengthTicks > maxLength) {
            maxLength = entry.loopLengthTicks;
        }
    }
    return maxLength;
}

STORAGE_PERSIST_MEM uint8_t revisionLoadDefaultActiveLoopIndex(uint8_t trackIndex) {
    for (uint16_t index = 0; index < revisionLoadSlotIndexCount; ++index) {
        const RevisionPackedBlob::RevisionLoopSlotDirectoryEntry& entry =
            revisionLoadSlotDirectoryEntries[index];
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
    if (!SetRevisionCatalog::formatRevisionPath(revisionLoadSourcePath,
                                                sizeof(revisionLoadSourcePath),
                                                revisionLoadSetId, revisionLoadRevisionId,
                                                false)) {
        return false;
    }
    if (!SD.exists(revisionLoadSourcePath)) {
        Serial.print("[StorageManager] ERROR: Revision file missing ");
        Serial.println(revisionLoadSourcePath);
        return false;
    }

    File file = SD.open(revisionLoadSourcePath, FILE_READ);
    if (!file) {
        return false;
    }
    const size_t fileSize = file.size();
    if (fileSize < RevisionPackedBlob::kRevisionHeaderByteSize +
                        RevisionPackedBlob::kRevisionFooterByteSize) {
        file.close();
        return false;
    }

    if (!validateRevisionLoadFileFooterFromSd(file, fileSize, revisionLoadHeader)) {
        file.close();
        return false;
    }

    revisionLoadSlotIndexCount = 0;
    revisionLoadTransportFileOffset = 0;
    revisionLoadTransportBodySize = 0;
    revisionLoadUsedDefaultTransport = false;

    if (!findChunkBodyInRevisionFile(file, fileSize, revisionLoadHeader,
                                     RevisionPackedBlob::ChunkType::Transport, 0, 0,
                                     revisionLoadTransportFileOffset,
                                     revisionLoadTransportBodySize) ||
        revisionLoadTransportBodySize == 0) {
        revisionLoadTransportFileOffset = 0;
        revisionLoadTransportBodySize = 0;
        revisionLoadUsedDefaultTransport = true;
        Serial.println(
            "[StorageManager] Revision load missing Transport chunk; using default transport");
    }

    if (!readSlotIndexEntriesFromRevisionFile(file, fileSize, revisionLoadHeader,
                                              revisionLoadSlotDirectoryEntries,
                                              kMaxRevisionLoopIndexEntries,
                                              revisionLoadSlotIndexCount)) {
        uint16_t slotIndexEntryCount = 0;
        const bool hasSlotIndexChunk = readRevisionLoopSlotDirectoryEntryCountFromRevisionFile(
            file, fileSize, revisionLoadHeader, slotIndexEntryCount);
        file.close();
        if (!hasSlotIndexChunk) {
            revisionLoadSlotIndexCount = 0;
        } else {
            Serial.print("[StorageManager] ERROR: Revision load slot index parse failed fileSize=");
            Serial.print(static_cast<uint32_t>(fileSize));
            Serial.print(" hdrChunkCount=");
            Serial.print(revisionLoadHeader.chunkCount);
            Serial.print(" hdrPayloadSize=");
            Serial.print(revisionLoadHeader.payloadSize);
            Serial.print(" slotIndexEntryCount=");
            Serial.println(slotIndexEntryCount);
            return false;
        }
    } else {
        file.close();
    }

    revisionLoadWriteStage = RevisionLoadWriteStage::PrepareEpoch;
    revisionLoadStage = RevisionLoadStage::Write;
    return true;
}

STORAGE_PERSIST_MEM bool openRevisionLoadLoopSlotTemp(uint8_t trackIndex, uint8_t slotIndex) {
    if (revisionLoadDestFileOpen) {
        revisionLoadDestFile.close();
        revisionLoadDestFileOpen = false;
    }
    char tempPath[48];
    if (!CurrentSetStorage::formatLoopSlotTempPath(tempPath, sizeof(tempPath), trackIndex,
                                                   slotIndex)) {
        return false;
    }
    revisionLoadDestFile = SD.open(tempPath, FILE_WRITE);
    if (!revisionLoadDestFile) {
        return false;
    }
    revisionLoadDestFile.seek(0);
    if (!CurrentWorkspaceStorage::writeEpochHeaderPlaceholder(revisionLoadDestFile,
                                                              revisionLoadWorkspaceEpoch)) {
        revisionLoadDestFile.close();
        return false;
    }
    revisionLoadDestFileOpen = true;
    return true;
}

STORAGE_PERSIST_MEM bool finalizeRevisionLoadLoopSlotTemp(uint8_t trackIndex, uint8_t slotIndex) {
    if (!revisionLoadDestFileOpen) {
        return false;
    }
    if (!writeRaw(revisionLoadDestFile, &CurrentSetStorage::kSaveFileToken, sizeof(CurrentSetStorage::kSaveFileToken))) {
        revisionLoadDestFile.close();
        revisionLoadDestFileOpen = false;
        return false;
    }
    revisionLoadDestFile.close();
    revisionLoadDestFileOpen = false;

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
    switch (revisionLoadWriteStage) {
        case RevisionLoadWriteStage::PrepareEpoch:
            ++currentWorkspaceEpoch;
            revisionLoadWorkspaceEpoch = currentWorkspaceEpoch;
            if (!CurrentSetStorage::ensureDirectory(PersistenceLayout::kRoot) ||
                !CurrentSetStorage::ensureDirectory(CurrentSetStorage::kCurrentSetDir) ||
                !CurrentSetStorage::ensureDirectory(CurrentWorkspaceStorage::kCurrentTempDir) ||
                !CurrentSetStorage::ensureDirectory(CurrentSetStorage::kCurrentSlotsDir)) {
                return false;
            }
            revisionLoadWriteStage = RevisionLoadWriteStage::OpenMetaTemp;
            return true;

        case RevisionLoadWriteStage::OpenMetaTemp:
            revisionLoadDestFile = SD.open(CurrentSetStorage::kCurrentMetaTempPath, FILE_WRITE);
            if (!revisionLoadDestFile) {
                return false;
            }
            revisionLoadDestFile.seek(0);
            if (!CurrentWorkspaceStorage::writeEpochHeaderPlaceholder(revisionLoadDestFile,
                                                                      revisionLoadWorkspaceEpoch)) {
                revisionLoadDestFile.close();
                return false;
            }
            revisionLoadDestFileOpen = true;
            revisionLoadTransportReadPos = 0;
            revisionLoadWriteStage = RevisionLoadWriteStage::CopyTransportBody;
            return true;

        case RevisionLoadWriteStage::CopyTransportBody:
            if (revisionLoadTransportBodySize == 0) {
                if (!writeDefaultRevisionLoadTransportBody(revisionLoadDestFile)) {
                    Serial.println(
                        "[StorageManager] ERROR: Revision load failed writing default transport");
                    revisionLoadDestFile.close();
                    revisionLoadDestFileOpen = false;
                    return false;
                }
                revisionLoadWriteStage = RevisionLoadWriteStage::FinalizeMetaTemp;
                return true;
            }
            if (!revisionLoadSourceFileOpen) {
                revisionLoadSourceFile = SD.open(revisionLoadSourcePath, FILE_READ);
                if (!revisionLoadSourceFile) {
                    return false;
                }
                revisionLoadSourceFileOpen = true;
            }
            if (revisionLoadTransportReadPos == 0) {
                revisionLoadTransportReadPos = revisionLoadTransportFileOffset;
            }
            {
                const uint32_t endPos =
                    revisionLoadTransportFileOffset + revisionLoadTransportBodySize;
                uint32_t remaining = endPos - revisionLoadTransportReadPos;
                if (remaining > 0) {
                    if (!copyRevisionCommitChunk(
                            revisionLoadDestFile, revisionLoadSourceFile,
                            revisionLoadTransportReadPos, remaining,
                            static_cast<uint32_t>(revisionCommitCopyBuffer.size()))) {
                        return false;
                    }
                    if (remaining > 0) {
                        return true;
                    }
                }
            }
            if (revisionLoadSourceFileOpen) {
                revisionLoadSourceFile.close();
                revisionLoadSourceFileOpen = false;
            }
            revisionLoadWriteStage = RevisionLoadWriteStage::FinalizeMetaTemp;
            return true;

        case RevisionLoadWriteStage::FinalizeMetaTemp: {
            if (!revisionLoadDestFileOpen) {
                return false;
            }
            if (!writeRaw(revisionLoadDestFile, &CurrentSetStorage::kSaveFileToken,
                          sizeof(CurrentSetStorage::kSaveFileToken))) {
                revisionLoadDestFile.close();
                revisionLoadDestFileOpen = false;
                return false;
            }
            revisionLoadDestFile.close();
            revisionLoadDestFileOpen = false;
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
            revisionLoadCopyTrackCursor = 0;
            revisionLoadCopySlotCursor = 0;
            revisionLoadSlotBodyRemaining = 0;
            revisionLoadWriteStage = RevisionLoadWriteStage::WriteLoopSlots;
            return true;
        }

        case RevisionLoadWriteStage::WriteLoopSlots:
            while (revisionLoadCopyTrackCursor < Config::NUM_TRACKS) {
                while (revisionLoadCopySlotCursor < Config::MAX_LOOPS_PER_TRACK) {
                    const uint8_t trackIndex = revisionLoadCopyTrackCursor;
                    const uint8_t slotIndex = revisionLoadCopySlotCursor;
                    RevisionPackedBlob::RevisionLoopSlotDirectoryEntry slotEntry{};
                    const bool hasSlotEntry =
                        findRevisionLoadSlotEntry(trackIndex, slotIndex, slotEntry);

                    if (revisionLoadSlotBodyRemaining == 0 && !revisionLoadDestFileOpen &&
                        !revisionLoadWritingEmptySlot) {
                        if (!openRevisionLoadLoopSlotTemp(trackIndex, slotIndex)) {
                            return false;
                        }
                        if (hasSlotEntry) {
                            revisionLoadWritingEmptySlot = false;
                            revisionLoadSlotReadPos =
                                revisionLoadLoopSlotBodyFileOffset(slotEntry);
                            revisionLoadSlotBodyRemaining = slotEntry.bodyLength;
                            revisionLoadLoopWriteStage = DeferredLoopWriteStage::Header;
                        } else {
                            revisionLoadWritingEmptySlot = true;
                            revisionLoadLoopWriteStage = DeferredLoopWriteStage::Header;
                        }
                    }

                    if (revisionLoadWritingEmptySlot) {
                        bool loopDone = false;
                        if (!stepDeferredEmptyLoopPersist(
                                revisionLoadDestFile, static_cast<LoopId>(slotIndex), loopDone)) {
                            return false;
                        }
                        if (!loopDone) {
                            return true;
                        }
                    } else if (revisionLoadSlotBodyRemaining > 0) {
                        if (!revisionLoadSourceFileOpen) {
                            revisionLoadSourceFile = SD.open(revisionLoadSourcePath, FILE_READ);
                            if (!revisionLoadSourceFile) {
                                return false;
                            }
                            revisionLoadSourceFileOpen = true;
                        }
                        if (!copyRevisionCommitChunk(
                                revisionLoadDestFile, revisionLoadSourceFile,
                                revisionLoadSlotReadPos, revisionLoadSlotBodyRemaining,
                                static_cast<uint32_t>(revisionCommitCopyBuffer.size()))) {
                            return false;
                        }
                        if (revisionLoadSlotBodyRemaining > 0) {
                            return true;
                        }
                        if (revisionLoadSourceFileOpen) {
                            revisionLoadSourceFile.close();
                            revisionLoadSourceFileOpen = false;
                        }
                    }

                    if (!finalizeRevisionLoadLoopSlotTemp(trackIndex, slotIndex)) {
                        return false;
                    }
                    revisionLoadWritingEmptySlot = false;
                    revisionLoadSlotBodyRemaining = 0;
                    ++revisionLoadCopySlotCursor;
                }
                revisionLoadCopySlotCursor = 0;
                ++revisionLoadCopyTrackCursor;
            }
            revisionLoadStage = RevisionLoadStage::ReloadRam;
            return true;
    }
    return false;
}

STORAGE_PERSIST_MEM bool stepRevisionLoadReloadRam(LooperState& state) {
    switch (revisionLoadReloadRamStage) {
        case RevisionLoadReloadRamStage::WriteWorkspaceMeta:
            currentWorkspaceEpoch = revisionLoadWorkspaceEpoch;
            lastCommittedWorkspaceEpoch = revisionLoadWorkspaceEpoch;
            workspaceDerivedFromSetId = revisionLoadHeader.setId;
            workspaceDerivedFromRevisionId = revisionLoadHeader.revisionId;
            if (!writeWorkspaceMetaAfterDeferredSave()) {
                Serial.println("[StorageManager] ERROR: Revision load failed writing workspace.bin");
                return false;
            }
            resetTracksAfterFailedLoad();
            revisionLoadReloadMetaFile =
                SD.open(CurrentSetStorage::kCurrentRuntimeBundlePath, FILE_READ);
            if (!revisionLoadReloadMetaFile) {
                Serial.println("[StorageManager] ERROR: Revision load failed opening runtime bundle");
                return false;
            }
            revisionLoadReloadMetaFileOpen = true;
            revisionLoadReloadRamStage = RevisionLoadReloadRamStage::ReadMetaHeaders;
            return true;

        case RevisionLoadReloadRamStage::ReadMetaHeaders: {
            if (!readCurrentSetFilePreamble(revisionLoadReloadMetaFile, revisionLoadReloadLooperState,
                                            revisionLoadReloadMasterLoopLength,
                                            revisionLoadReloadNumTracks)) {
                return false;
            }
            revisionLoadReloadActiveLoopIndex.assign(revisionLoadReloadNumTracks, 0);
            for (uint8_t t = 0; t < revisionLoadReloadNumTracks; ++t) {
                Track& track = trackManager.getTrack(t);
                if (!readCurrentSetTrackSlotMetadata(revisionLoadReloadMetaFile, t, track,
                                                     revisionLoadReloadLoadedTrackState[t],
                                                     revisionLoadReloadMuted[t])) {
                    return false;
                }
                revisionLoadReloadAnySlotHasEvents[t] = false;
            }
            revisionLoadReloadTrackCursor = 0;
            revisionLoadReloadSlotCursor = 0;
            revisionLoadReloadRamStage = RevisionLoadReloadRamStage::LoadLoopSlot;
            return true;
        }

        case RevisionLoadReloadRamStage::LoadLoopSlot: {
            if (revisionLoadReloadTrackCursor >= revisionLoadReloadNumTracks) {
                revisionLoadReloadRamStage = RevisionLoadReloadRamStage::ReadFooter;
                return true;
            }
            Track& track = trackManager.getTrack(revisionLoadReloadTrackCursor);
            if (!loadLoopSlotFromCurrentSetSd(revisionLoadReloadTrackCursor,
                                              revisionLoadReloadSlotCursor, track,
                                              revisionLoadReloadAnySlotHasEvents
                                                  [revisionLoadReloadTrackCursor])) {
                return false;
            }
            ++revisionLoadReloadSlotCursor;
            if (revisionLoadReloadSlotCursor >= Config::MAX_LOOPS_PER_TRACK) {
                applyLoadedTrackStateAfterLoopSlots(
                    track, revisionLoadReloadLoadedTrackState[revisionLoadReloadTrackCursor],
                    revisionLoadReloadAnySlotHasEvents[revisionLoadReloadTrackCursor],
                    revisionLoadReloadMuted[revisionLoadReloadTrackCursor]);
                ++revisionLoadReloadTrackCursor;
                revisionLoadReloadSlotCursor = 0;
            }
            return true;
        }

        case RevisionLoadReloadRamStage::ReadFooter: {
            if (!readCurrentSetFileEpilogue(revisionLoadReloadMetaFile, revisionLoadReloadNumTracks,
                                            revisionLoadReloadActiveLoopIndex,
                                            revisionLoadReloadSelectedTrackIdx)) {
                return false;
            }
            if (revisionLoadReloadMetaFileOpen) {
                revisionLoadReloadMetaFile.close();
                revisionLoadReloadMetaFileOpen = false;
            }
            if (!applyLoadedTransportFooter(revisionLoadReloadNumTracks,
                                            revisionLoadReloadActiveLoopIndex,
                                            revisionLoadReloadSelectedTrackIdx, state,
                                            revisionLoadReloadLooperState,
                                            revisionLoadReloadMasterLoopLength)) {
                return false;
            }
            forceCurrentSetFullLoopWrite = false;
            syncCurrentSetDirtyTrackingFromLoadedState();
            clearCurrentSetLoadedFromFolder();
            currentSetAnchorFields = CurrentSetStorage::AnchorFields{};
            resetRevisionLoadReloadRamState();
            revisionLoadStage = RevisionLoadStage::Complete;
            return true;
        }
    }
    return false;
}

STORAGE_PERSIST_MEM bool stepRevisionLoadComplete() {
    Serial.print("[StorageManager] Revision load complete S");
    Serial.print(revisionLoadSetId);
    Serial.print(" v");
    Serial.println(revisionLoadRevisionId);
#if defined(SESSION_CAPTURE)
    char revLoadDetail[40];
    if (revisionLoadUsedDefaultTransport) {
        std::snprintf(revLoadDetail, sizeof(revLoadDetail), "S%04u_v%04u,default_transport",
                      revisionLoadSetId, revisionLoadRevisionId);
    } else {
        std::snprintf(revLoadDetail, sizeof(revLoadDetail), "S%04u_v%04u", revisionLoadSetId,
                      revisionLoadRevisionId);
    }
    SC_PERSIST("rev_load_complete", 0, revisionLoadSetId, revisionLoadRevisionId, revLoadDetail);
#endif
    revisionLoadLastDisplaySetId = revisionLoadSetId;
    revisionLoadLastDisplayRevisionId = revisionLoadRevisionId;
    revisionLoadCompletedAtMs = millis();
    revisionLoadFailedAtMs = 0;
    revisionLoadDisplayRefreshPending = true;
    revisionLoadStage = RevisionLoadStage::Idle;
    storageSession.revisionLoad.inProgress = false;
    if (looperState.isLoadSaveModeActive()) {
        looperState.exitLoadSaveMode();
    }
    return true;
}

STORAGE_PERSIST_MEM bool stepRevisionLoadJob(LooperState& state) {
    switch (revisionLoadStage) {
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

