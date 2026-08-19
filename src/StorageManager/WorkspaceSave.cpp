//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "StorageManagerInternal.h"

#include "CurrentSetStorage.h"
#include "CurrentWorkspaceStorage.h"
#include "GlobalUndoStack.h"
#include "Globals.h"
#include "Loop.h"
#include "LoopEventStore.h"
#include "PersistenceSchema.h"
#include "RtcTime.h"
#include "StorageManager.h"
#include "StorageLoopIo.h"
#include "TrackManager.h"
#include "TrackUndo.h"
#include "Utils/DebugSessionCapture.h"
#include <Arduino.h>
#include <cstdio>

namespace StorageManagerInternal {

STORAGE_PERSIST_MEM void quarantineLegacyMonolithStorageFile() {
#if defined(ARDUINO)
    if (!SD.exists(CurrentSetStorage::kLegacyMonolithPath)) {
        return;
    }
    char quarantineName[48];
    snprintf(quarantineName, sizeof(quarantineName), "/state.bad.%lu",
             static_cast<unsigned long>(millis()));
    if (SD.rename(CurrentSetStorage::kLegacyMonolithPath, quarantineName)) {
        Serial.print("[StorageManager] Quarantined storage file as ");
        Serial.println(quarantineName);
    }
#endif
}

STORAGE_PERSIST_MEM void clearCurrentSetLoopSlotDirty(uint8_t trackIndex, uint8_t slotIndex) {
    if (trackIndex >= Config::NUM_TRACKS || slotIndex >= Config::MAX_LOOPS_PER_TRACK) {
        return;
    }
    currentSetLoopSlotDirty[trackIndex][slotIndex] = false;
}

STORAGE_PERSIST_MEM void resetDeferredLoopFinalizeState() {
    storageSession.currentWorkspaceSave.loopFinalizeStage = DeferredLoopFinalizeStage::Idle;
}

STORAGE_PERSIST_MEM void beginDeferredLoopSlotFinalize() {
    storageSession.currentWorkspaceSave.loopFinalizeStage = DeferredLoopFinalizeStage::WriteToken;
    storageSession.currentWorkspaceSave.epochCrc = 0;
    storageSession.currentWorkspaceSave.epochCrcBodyOffset = 0;
    storageSession.currentWorkspaceSave.epochCrcBodySize = 0;
}

STORAGE_PERSIST_MEM bool deferredLoopSlotFinalizeInProgress() {
    return storageSession.currentWorkspaceSave.loopFinalizeStage != DeferredLoopFinalizeStage::Idle;
}

STORAGE_PERSIST_MEM void resetDeferredLoopWriteState() {
    storageSession.currentWorkspaceSave.loopWriteStage = DeferredLoopWriteStage::Header;
    storageSession.currentWorkspaceSave.capturePassCursor = 0;
    storageSession.currentWorkspaceSave.chunkCursor = 0;
    storageSession.currentWorkspaceSave.midiBatch.clear();
    resetDeferredLoopFinalizeState();
}

STORAGE_PERSIST_MEM void resetDeferredUndoWriteState() {
    storageSession.currentWorkspaceSave.undoWriteStage = DeferredUndoWriteStage::Header;
    storageSession.currentWorkspaceSave.undoEntryCursor = 0;
    resetDeferredLoopWriteState();
}


STORAGE_PERSIST_MEM void resetDeferredSaveJobState() {
    if (storageSession.currentWorkspaceSave.file) {
        storageSession.currentWorkspaceSave.file.close();
    }
    if (storageSession.currentWorkspaceSave.loopFile) {
        storageSession.currentWorkspaceSave.loopFile.close();
    }
    storageSession.currentWorkspaceSave.loopFileOpen = false;
    storageSession.currentWorkspaceSave.inProgress = false;
    storageSession.currentWorkspaceSave.sdIoActive = false;
    storageSession.currentWorkspaceSave.stage = DeferredSaveStage::Idle;
    storageSession.currentWorkspaceSave.globalHeaderStage = DeferredGlobalHeaderStage::Bpm;
    storageSession.currentWorkspaceSave.trackWriteStage = DeferredTrackWriteStage::TrackState;
    storageSession.currentWorkspaceSave.slotWriteStage = DeferredSlotWriteStage::SlotEnabled;
    storageSession.currentWorkspaceSave.footerWriteStage = DeferredFooterWriteStage::SelectedTrack;
    resetDeferredCompletionWriteState();
    storageSession.currentWorkspaceSave.numTracks = 0;
    storageSession.currentWorkspaceSave.trackCursor = 0;
    storageSession.currentWorkspaceSave.slotCursor = 0;
    storageSession.currentWorkspaceSave.poolCursor = 0;
    storageSession.currentWorkspaceSave.undoTrackCursor = 0;
    storageSession.currentWorkspaceSave.footerTrackCursor = 0;
    resetDeferredLoopWriteState();
    resetDeferredUndoWriteState();
    storageSession.currentWorkspaceSave.trackHeaderWritten = false;
    storageSession.currentWorkspaceSave.startedAtUs = 0;
    storageSession.currentWorkspaceSave.heapBefore = 0;
    storageSession.currentWorkspaceSave.admissionHeap = 0;
    storageSession.currentWorkspaceSave.heapFloorDeferred = false;
    storageSession.currentWorkspaceSave.urgentRequested = false;
    storageSession.currentWorkspaceSave.loopSlotsWritten = 0;
    storageSession.currentWorkspaceSave.loopSlotsSkipped = 0;
    storageSession.currentWorkspaceSave.displayBlockUs = 0;
    storageSession.currentWorkspaceSave.stateSnapshot = LOOPER_IDLE;
}


STORAGE_PERSIST_MEM bool writeCurrentSetMetaHeaderToOpenFile(File& file) {
    CurrentSetStorage::MetaHeader header{};
    header.containerVersion = CurrentSetStorage::CONTAINER_VERSION;
    header.lastActiveUnix = 0;
    header.anchor = currentSetAnchorFields;
    const StorageIo io = storageIoFromFileWrite(file);
    return CurrentSetStorage::writeMetaHeader(io, header);
}

STORAGE_PERSIST_MEM void closeDeferredMetaTempIfOpen() {
    if (storageSession.currentWorkspaceSave.file) {
        storageSession.currentWorkspaceSave.file.close();
    }
}

STORAGE_PERSIST_MEM bool finalizeDeferredMetaTempFile() {
    if (!writeRaw(storageSession.currentWorkspaceSave.file, &CurrentSetStorage::kSaveFileToken, sizeof(CurrentSetStorage::kSaveFileToken))) {
        return false;
    }
    storageSession.currentWorkspaceSave.file.close();
    if (!CurrentWorkspaceStorage::finalizeEpochFileHeaderCrc(
            CurrentSetStorage::kCurrentMetaTempPath)) {
        return false;
    }
    if (!CurrentSetStorage::verifySaveFileTokenAtPath(CurrentSetStorage::kCurrentMetaTempPath)) {
        return false;
    }
    return CurrentSetStorage::atomicRenameTempFile(CurrentSetStorage::kCurrentMetaTempPath,
                                                   CurrentSetStorage::kCurrentMetaPath);
}

STORAGE_PERSIST_MEM bool closeDeferredMetaTempForLoopWrites() {
    if (storageSession.currentWorkspaceSave.file) {
        storageSession.currentWorkspaceSave.file.close();
    }
    return true;
}

STORAGE_PERSIST_MEM bool reopenDeferredMetaTempForAppend() {
    storageSession.currentWorkspaceSave.file = SD.open(CurrentSetStorage::kCurrentMetaTempPath, FILE_WRITE);
    if (!storageSession.currentWorkspaceSave.file) {
        Serial.println("[StorageManager] ERROR: Could not reopen CurrentSet meta temp for append");
        return false;
    }
    if (!storageSession.currentWorkspaceSave.file.seek(storageSession.currentWorkspaceSave.file.size())) {
        storageSession.currentWorkspaceSave.file.close();
        return false;
    }
    return true;
}

STORAGE_PERSIST_MEM bool openDeferredLoopSlotTemp(uint8_t trackIndex, uint8_t slotIndex) {
    if (storageSession.currentWorkspaceSave.loopFileOpen) {
        storageSession.currentWorkspaceSave.loopFile.close();
        storageSession.currentWorkspaceSave.loopFileOpen = false;
    }
    char tempPath[48];
    if (!CurrentSetStorage::formatLoopSlotTempPath(tempPath, sizeof(tempPath), trackIndex,
                                                   slotIndex)) {
        return false;
    }
    storageSession.currentWorkspaceSave.loopFile = SD.open(tempPath, FILE_WRITE);
    if (!storageSession.currentWorkspaceSave.loopFile) {
        Serial.print("[StorageManager] ERROR: Could not open loop temp file: ");
        Serial.println(tempPath);
        return false;
    }
    storageSession.currentWorkspaceSave.loopFile.seek(0);
    if (!CurrentWorkspaceStorage::writeEpochHeaderPlaceholder(storageSession.currentWorkspaceSave.loopFile,
                                                              storageSession.currentWorkspaceSave.workspaceEpoch)) {
        storageSession.currentWorkspaceSave.loopFile.close();
        storageSession.currentWorkspaceSave.loopFileOpen = false;
        return false;
    }
    storageSession.currentWorkspaceSave.loopFileOpen = true;
    return true;
}

STORAGE_PERSIST_MEM bool stepFinalizeDeferredLoopSlotTemp(uint8_t trackIndex, uint8_t slotIndex,
                                                          bool& finalizeDoneOut) {
    finalizeDoneOut = false;
    CurrentWorkspaceSaveJob& job = storageSession.currentWorkspaceSave;
    char tempPath[48];
    char finalPath[48];
    if (!CurrentSetStorage::formatLoopSlotTempPath(tempPath, sizeof(tempPath), trackIndex,
                                                   slotIndex) ||
        !CurrentSetStorage::formatLoopSlotPath(finalPath, sizeof(finalPath), trackIndex,
                                               slotIndex)) {
        return false;
    }

    switch (job.loopFinalizeStage) {
        case DeferredLoopFinalizeStage::Idle:
            return false;

        case DeferredLoopFinalizeStage::WriteToken:
            if (!job.loopFileOpen) {
                return false;
            }
            if (!writeRaw(job.loopFile, &CurrentSetStorage::kSaveFileToken,
                          sizeof(CurrentSetStorage::kSaveFileToken))) {
                job.loopFile.close();
                job.loopFileOpen = false;
                return false;
            }
            job.loopFile.close();
            job.loopFileOpen = false;
            job.loopFinalizeStage = DeferredLoopFinalizeStage::EpochCrcBody;
            return true;

        case DeferredLoopFinalizeStage::EpochCrcBody: {
            if (!job.loopFile) {
                job.loopFile = SD.open(tempPath, FILE_READ);
                if (!job.loopFile) {
                    return false;
                }
                const size_t fileSize = job.loopFile.size();
                if (fileSize < CurrentWorkspaceStorage::kEpochFileHeaderByteSize) {
                    job.loopFile.close();
                    return false;
                }
                job.epochCrc = 0;
                job.epochCrcBodyOffset = 0;
                job.epochCrcBodySize =
                    static_cast<uint32_t>(fileSize - CurrentWorkspaceStorage::kEpochFileHeaderByteSize);
                if (!job.loopFile.seek(CurrentWorkspaceStorage::kEpochFileHeaderByteSize)) {
                    job.loopFile.close();
                    return false;
                }
                if (job.epochCrcBodySize == 0) {
                    job.loopFile.close();
                    job.loopFinalizeStage = DeferredLoopFinalizeStage::EpochCrcHeader;
                    return true;
                }
            }
            uint8_t buffer[CurrentWorkspaceStorage::kEpochFileCrcSliceBytes];
            const uint32_t remaining = job.epochCrcBodySize - job.epochCrcBodyOffset;
            const size_t toRead = remaining < CurrentWorkspaceStorage::kEpochFileCrcSliceBytes
                                      ? remaining
                                      : CurrentWorkspaceStorage::kEpochFileCrcSliceBytes;
            const int bytesRead = job.loopFile.read(buffer, toRead);
            if (bytesRead <= 0) {
                job.loopFile.close();
                return false;
            }
            job.epochCrc = PersistenceSchema::crc32Continue(job.epochCrc, buffer,
                                                            static_cast<size_t>(bytesRead));
            job.epochCrcBodyOffset += static_cast<uint32_t>(bytesRead);
            if (job.epochCrcBodyOffset >= job.epochCrcBodySize) {
                job.loopFile.close();
                job.loopFinalizeStage = DeferredLoopFinalizeStage::EpochCrcHeader;
            }
            return true;
        }

        case DeferredLoopFinalizeStage::EpochCrcHeader:
            if (!CurrentWorkspaceStorage::writeEpochFileHeaderCrc(tempPath, job.epochCrc)) {
                return false;
            }
            job.loopFinalizeStage = DeferredLoopFinalizeStage::VerifyAndRename;
            return true;

        case DeferredLoopFinalizeStage::VerifyAndRename:
            if (!CurrentSetStorage::verifySaveFileTokenAtPath(tempPath)) {
                return false;
            }
            if (!CurrentSetStorage::atomicRenameTempFile(tempPath, finalPath)) {
                return false;
            }
            (void)CurrentSetStorage::removeLoopSlotSealJournal(trackIndex, slotIndex);
            StorageManager::setLoopSlotPayloadOnSdInRam(trackIndex, slotIndex, true);
            resetDeferredLoopFinalizeState();
            finalizeDoneOut = true;
            return true;
    }
    return false;
}

STORAGE_PERSIST_MEM bool shouldWriteCurrentSetLoopSlot(uint8_t trackIndex, uint8_t slotIndex) {
    if (trackIndex >= Config::NUM_TRACKS || slotIndex >= Config::MAX_LOOPS_PER_TRACK) {
        return true;
    }
    return CurrentSetStorage::shouldWriteLoopPayloadForSlot(
        forceCurrentSetFullLoopWrite, currentSetLoopSlotDirty[trackIndex][slotIndex]);
}

STORAGE_PERSIST_MEM bool trackHasCurrentSetDirtyLoopSlot(uint8_t trackIndex) {
    if (forceCurrentSetFullLoopWrite) {
        return true;
    }
    if (trackIndex >= Config::NUM_TRACKS) {
        return false;
    }
    for (uint8_t slot = 0; slot < Config::MAX_LOOPS_PER_TRACK; ++slot) {
        if (currentSetLoopSlotDirty[trackIndex][slot]) {
            return true;
        }
    }
    return false;
}


STORAGE_PERSIST_MEM bool beginDeferredRuntimeBundleWrite(const LooperState& state) {
    storageSession.currentWorkspaceSave.workspaceEpoch = currentWorkspaceEpoch;

    if (!CurrentSetStorage::ensureDirectory(PersistenceLayout::kRoot) ||
        !CurrentSetStorage::ensureDirectory(CurrentSetStorage::kCurrentSetDir) ||
        !CurrentSetStorage::ensureDirectory(CurrentWorkspaceStorage::kCurrentTempDir) ||
        !CurrentSetStorage::ensureDirectory(CurrentSetStorage::kCurrentSlotsDir) ||
        !CurrentSetStorage::ensureDirectory(PersistenceLayout::kRecoveryRoot) ||
        !CurrentSetStorage::ensureDirectory(CurrentSetStorage::kCheckpointsDir)) {
        Serial.println("[StorageManager] ERROR: Could not create MidiLooper/current directories");
        return false;
    }

    closeDeferredMetaTempIfOpen();
    if (SD.exists(CurrentSetStorage::kCurrentMetaTempPath)) {
        (void)SD.remove(CurrentSetStorage::kCurrentMetaTempPath);
    }

    storageSession.currentWorkspaceSave.file = SD.open(CurrentSetStorage::kCurrentMetaTempPath, FILE_WRITE);
    if (!storageSession.currentWorkspaceSave.file) {
        Serial.println("[StorageManager] ERROR: Could not open CurrentSet meta temp file");
        return false;
    }
    storageSession.currentWorkspaceSave.file.seek(0);

    if (!CurrentWorkspaceStorage::writeEpochHeaderPlaceholder(storageSession.currentWorkspaceSave.file,
                                                              storageSession.currentWorkspaceSave.workspaceEpoch)) {
        Serial.println("[StorageManager] ERROR: Work item bundle write failed writing epoch header");
        storageSession.currentWorkspaceSave.file.close();
        return false;
    }

    if (!writeCurrentSetMetaHeaderToOpenFile(storageSession.currentWorkspaceSave.file)) {
        Serial.println("[StorageManager] ERROR: Work item bundle write failed writing CurrentSet meta header");
        storageSession.currentWorkspaceSave.file.close();
        return false;
    }

    storageSession.currentWorkspaceSave.stateSnapshot = state;
    storageSession.currentWorkspaceSave.numTracks = Config::NUM_TRACKS;
    storageSession.currentWorkspaceSave.globalHeaderStage = DeferredGlobalHeaderStage::Bpm;
    storageSession.currentWorkspaceSave.trackCursor = 0;
    if (storageSession.persistenceWorkItem.itemActive) {
        const PersistWorkItem& scopedItem = storageSession.persistenceWorkItem.item;
        if (scopedItem.type == PersistWorkType::TrackMeta &&
            scopedItem.key.kind == PersistKeyKind::Track) {
            storageSession.currentWorkspaceSave.trackCursor = scopedItem.key.trackIndex;
        }
    }
    storageSession.currentWorkspaceSave.slotCursor = 0;
    storageSession.currentWorkspaceSave.poolCursor = 0;
    storageSession.currentWorkspaceSave.undoTrackCursor = 0;
    storageSession.currentWorkspaceSave.footerTrackCursor = 0;
    storageSession.currentWorkspaceSave.trackHeaderWritten = false;
    storageSession.currentWorkspaceSave.trackWriteStage = DeferredTrackWriteStage::TrackState;
    storageSession.currentWorkspaceSave.slotWriteStage = DeferredSlotWriteStage::SlotEnabled;
    storageSession.currentWorkspaceSave.footerWriteStage = DeferredFooterWriteStage::SelectedTrack;
    resetDeferredCompletionWriteState();
    resetDeferredLoopWriteState();
    resetDeferredUndoWriteState();
    storageSession.currentWorkspaceSave.inProgress = true;
    storageSession.currentWorkspaceSave.stage = DeferredSaveStage::CurrentSetMeta;
    return true;
}


STORAGE_PERSIST_MEM bool beginDeferredSaveJob(const LooperState& state) {
    ++currentWorkspaceEpoch;
    storageSession.currentWorkspaceSave.workspaceEpoch = currentWorkspaceEpoch;

    if (!CurrentSetStorage::ensureDirectory(PersistenceLayout::kRoot) ||
        !CurrentSetStorage::ensureDirectory(CurrentSetStorage::kCurrentSetDir) ||
        !CurrentSetStorage::ensureDirectory(CurrentWorkspaceStorage::kCurrentTempDir) ||
        !CurrentSetStorage::ensureDirectory(CurrentSetStorage::kCurrentSlotsDir) ||
        !CurrentSetStorage::ensureDirectory(PersistenceLayout::kRecoveryRoot) ||
        !CurrentSetStorage::ensureDirectory(CurrentSetStorage::kCheckpointsDir)) {
        Serial.println("[StorageManager] ERROR: Could not create MidiLooper/current directories");
        return false;
    }

    closeDeferredMetaTempIfOpen();
    storageSession.currentWorkspaceSave.file = SD.open(CurrentSetStorage::kCurrentMetaTempPath, FILE_WRITE);
    if (!storageSession.currentWorkspaceSave.file) {
        Serial.println("[StorageManager] ERROR: Could not open CurrentSet meta temp file");
        return false;
    }
    storageSession.currentWorkspaceSave.file.seek(0);

    if (!CurrentWorkspaceStorage::writeEpochHeaderPlaceholder(storageSession.currentWorkspaceSave.file,
                                                              storageSession.currentWorkspaceSave.workspaceEpoch)) {
        Serial.println("[StorageManager] ERROR: Deferred save failed writing epoch header");
        storageSession.currentWorkspaceSave.file.close();
        return false;
    }

    if (!writeCurrentSetMetaHeaderToOpenFile(storageSession.currentWorkspaceSave.file)) {
        Serial.println("[StorageManager] ERROR: Deferred save failed writing CurrentSet meta header");
        storageSession.currentWorkspaceSave.file.close();
        return false;
    }

    storageSession.currentWorkspaceSave.stateSnapshot = state;
    storageSession.currentWorkspaceSave.numTracks = Config::NUM_TRACKS;
    storageSession.currentWorkspaceSave.globalHeaderStage = DeferredGlobalHeaderStage::Bpm;
    storageSession.currentWorkspaceSave.trackCursor = 0;
    storageSession.currentWorkspaceSave.slotCursor = 0;
    storageSession.currentWorkspaceSave.poolCursor = 0;
    storageSession.currentWorkspaceSave.undoTrackCursor = 0;
    storageSession.currentWorkspaceSave.footerTrackCursor = 0;
    storageSession.currentWorkspaceSave.trackHeaderWritten = false;
    storageSession.currentWorkspaceSave.trackWriteStage = DeferredTrackWriteStage::TrackState;
    storageSession.currentWorkspaceSave.slotWriteStage = DeferredSlotWriteStage::SlotEnabled;
    storageSession.currentWorkspaceSave.footerWriteStage = DeferredFooterWriteStage::SelectedTrack;
    resetDeferredCompletionWriteState();
    resetDeferredLoopWriteState();
    resetDeferredUndoWriteState();
    storageSession.currentWorkspaceSave.inProgress = true;
    storageSession.currentWorkspaceSave.stage = DeferredSaveStage::CurrentSetMeta;
    return true;
}

STORAGE_PERSIST_MEM bool selectDeferredCapturePass(const LoopPasses& passes, uint16_t cursor,
                               CapturePassSlotFileHeader& passHeader,
                               const CommittedChunkIdList*& committedChunkIds) {
    uint16_t overdubIndex = cursor;
    if (passes.hasRecordPass()) {
        if (cursor == 0) {
            passHeader.id = passes.recordPass.id;
            passHeader.mergeSequence = 0;
            passHeader.stateRaw = static_cast<uint8_t>(passes.recordPass.state);
            passHeader.typeRaw = 0;
            passHeader.sealedAtTick = passes.recordPass.sealedAtTick;
            committedChunkIds = &passes.recordPass.committedChunkIds;
            return true;
        }
        overdubIndex = cursor - 1;
    }

    if (overdubIndex >= passes.overdubPasses.size()) {
        committedChunkIds = nullptr;
        return false;
    }

    const OverdubPass& pass = passes.overdubPasses[overdubIndex];
    passHeader.id = pass.id;
    passHeader.mergeSequence = pass.mergeSequence;
    passHeader.stateRaw = static_cast<uint8_t>(pass.state);
    passHeader.typeRaw = 1;
    passHeader.sealedAtTick = pass.sealedAtTick;
    committedChunkIds = &pass.committedChunkIds;
    return true;
}

STORAGE_PERSIST_MEM bool writeDeferredLoopHeader(File& file, LoopId loopId, uint32_t startLoopTick,
                             uint32_t loopLengthTicks, uint32_t loopStartTick,
                             PassId nextPassId, NoteId nextNoteId, uint32_t nextMergeSequence,
                             PassId lastCommittedPassId, const LoopPasses& passes,
                             LoopPersistPayloadCrc crcMode = LoopPersistPayloadCrc::None) {
    if (!persistenceWriteRaw(file, &loopId, sizeof(loopId), crcMode)) return false;
    if (!persistenceWriteRaw(file, &startLoopTick, sizeof(startLoopTick), crcMode)) return false;
    if (!persistenceWriteRaw(file, &loopLengthTicks, sizeof(loopLengthTicks), crcMode)) {
        return false;
    }
    if (!persistenceWriteRaw(file, &loopStartTick, sizeof(loopStartTick), crcMode)) return false;
    if (!persistenceWriteRaw(file, &nextPassId, sizeof(nextPassId), crcMode)) return false;
    if (!persistenceWriteRaw(file, &nextNoteId, sizeof(nextNoteId), crcMode)) return false;
    if (!persistenceWriteRaw(file, &nextMergeSequence, sizeof(nextMergeSequence), crcMode)) {
        return false;
    }
    if (!persistenceWriteRaw(file, &lastCommittedPassId, sizeof(lastCommittedPassId), crcMode)) {
        return false;
    }

    const uint32_t persistedCount = static_cast<uint32_t>(passes.capturePassCount());
    return persistenceWriteRaw(file, &persistedCount, sizeof(persistedCount), crcMode);
}

STORAGE_PERSIST_MEM bool writeDeferredCapturePassHeader(File& file, const CapturePassSlotFileHeader& passHeader,
                                    const CommittedChunkIdList& committedChunkIds,
                                    LoopPersistPayloadCrc crcMode = LoopPersistPayloadCrc::None) {
    if (!persistenceWriteRaw(file, &passHeader.id, sizeof(passHeader.id), crcMode)) return false;
    if (!persistenceWriteRaw(file, &passHeader.mergeSequence, sizeof(passHeader.mergeSequence), crcMode)) {
        return false;
    }
    if (!persistenceWriteRaw(file, &passHeader.stateRaw, sizeof(passHeader.stateRaw), crcMode)) return false;
    if (!persistenceWriteRaw(file, &passHeader.typeRaw, sizeof(passHeader.typeRaw), crcMode)) return false;
    if (!persistenceWriteRaw(file, &passHeader.sealedAtTick, sizeof(passHeader.sealedAtTick), crcMode)) {
        return false;
    }

    const uint32_t midiCount =
        static_cast<uint32_t>(LoopEventStore::countEventsInChunkIds(committedChunkIds));
    return persistenceWriteRaw(file, &midiCount, sizeof(midiCount), crcMode);
}

STORAGE_PERSIST_MEM bool writeDeferredCapturePassChunk(File& file, uint16_t chunkId,
                                   LoopPersistPayloadCrc crcMode = LoopPersistPayloadCrc::None) {
    storageSession.currentWorkspaceSave.midiBatch.clear();
    LoopEventStore::appendChunkRefEvent(chunkId, storageSession.currentWorkspaceSave.midiBatch);
    if (storageSession.currentWorkspaceSave.midiBatch.empty()) {
        return true;
    }
    return persistenceWriteRaw(file, storageSession.currentWorkspaceSave.midiBatch.data(),
                              storageSession.currentWorkspaceSave.midiBatch.size() * sizeof(MidiEvent), crcMode);
}

STORAGE_PERSIST_MEM bool stepDeferredLoopPersist(File& file, const Loop& loop, bool& loopDone,
                              LoopPersistPayloadCrc crcMode) {
    loopDone = false;

    switch (storageSession.currentWorkspaceSave.loopWriteStage) {
        case DeferredLoopWriteStage::Header:
            if (!writeDeferredLoopHeader(file, loop.loopId, loop.startLoopTick,
                                         loop.reconcileLoopLengthWithCommittedPasses(loop.loopLengthTicks),
                                         loop.loopStartTick, loop.nextPassId_, loop.nextNoteId_,
                                         loop.nextMergeSequence_, loop.lastCommittedPassId_,
                                         loop.passes, crcMode)) {
                return false;
            }
            storageSession.currentWorkspaceSave.capturePassCursor = 0;
            storageSession.currentWorkspaceSave.chunkCursor = 0;
            storageSession.currentWorkspaceSave.loopWriteStage = DeferredLoopWriteStage::CapturePassHeader;
            return true;

        case DeferredLoopWriteStage::CapturePassHeader: {
            if (storageSession.currentWorkspaceSave.capturePassCursor >= loop.passes.capturePassCount()) {
                storageSession.currentWorkspaceSave.loopWriteStage = DeferredLoopWriteStage::EditTail;
                return true;
            }

            CapturePassSlotFileHeader passHeader{};
            const CommittedChunkIdList* committedChunkIds = nullptr;
            if (!selectDeferredCapturePass(loop.passes, storageSession.currentWorkspaceSave.capturePassCursor, passHeader, committedChunkIds) ||
                committedChunkIds == nullptr) {
                return false;
            }
            if (!writeDeferredCapturePassHeader(file, passHeader, *committedChunkIds, crcMode)) {
                return false;
            }
            storageSession.currentWorkspaceSave.chunkCursor = 0;
            storageSession.currentWorkspaceSave.loopWriteStage = DeferredLoopWriteStage::CapturePassChunk;
            return true;
        }

        case DeferredLoopWriteStage::CapturePassChunk: {
            CapturePassSlotFileHeader passHeader{};
            const CommittedChunkIdList* committedChunkIds = nullptr;
            if (!selectDeferredCapturePass(loop.passes, storageSession.currentWorkspaceSave.capturePassCursor, passHeader, committedChunkIds) ||
                committedChunkIds == nullptr) {
                return false;
            }
            (void)passHeader;

            if (storageSession.currentWorkspaceSave.chunkCursor < committedChunkIds->size()) {
                const uint16_t chunkId = (*committedChunkIds)[storageSession.currentWorkspaceSave.chunkCursor++];
                return writeDeferredCapturePassChunk(file, chunkId, crcMode);
            }

            ++storageSession.currentWorkspaceSave.capturePassCursor;
            storageSession.currentWorkspaceSave.loopWriteStage = DeferredLoopWriteStage::CapturePassHeader;
            return true;
        }

        case DeferredLoopWriteStage::EditTail: {
            const StorageIo io = crcMode == LoopPersistPayloadCrc::RevisionCommit
                                     ? storageIoFromFileWriteWithRevisionPayloadCrc(file)
                                     : storageIoFromFileWrite(file);
            if (!writePersistedEditsTail(io, loop.nextPassId_, loop.passes.editPasses,
                                         loop.passes.loopGeometries, loop.passes.overdubPasses)) {
                return false;
            }
            resetDeferredLoopWriteState();
            loopDone = true;
            return true;
        }
    }

    return false;
}

STORAGE_PERSIST_MEM bool stepDeferredEmptyLoopPersist(File& file, LoopId loopId, bool& loopDone) {
    loopDone = false;
    const LoopPasses emptyPasses{};

    switch (storageSession.currentWorkspaceSave.loopWriteStage) {
        case DeferredLoopWriteStage::Header:
            if (!writeDeferredLoopHeader(file, loopId, 0, 0, 0, 1, 1, 0, kInvalidPassId,
                                         emptyPasses)) {
                return false;
            }
            storageSession.currentWorkspaceSave.loopWriteStage = DeferredLoopWriteStage::EditTail;
            return true;

        case DeferredLoopWriteStage::CapturePassHeader:
        case DeferredLoopWriteStage::CapturePassChunk:
            return false;

        case DeferredLoopWriteStage::EditTail: {
            const StorageIo io = storageIoFromFileWrite(file);
            if (!writePersistedEditsTail(io, 1, emptyPasses.editPasses,
                                         emptyPasses.loopGeometries, emptyPasses.overdubPasses)) {
                return false;
            }
            resetDeferredLoopWriteState();
            loopDone = true;
            return true;
        }
    }

    return false;
}

STORAGE_PERSIST_MEM bool stepDeferredLoopSnapshotPersist(File& file, const PersistedLoopSnapshot& snapshot,
                                     bool& loopDone) {
    loopDone = false;

    switch (storageSession.currentWorkspaceSave.loopWriteStage) {
        case DeferredLoopWriteStage::Header:
            if (!writeDeferredLoopHeader(file, snapshot.loopId, snapshot.startLoopTick,
                                         snapshot.loopLengthTicks, snapshot.loopStartTick,
                                         snapshot.nextPassId, snapshot.nextNoteId,
                                         snapshot.nextMergeSequence, snapshot.lastCommittedPassId,
                                         snapshot.passes)) {
                return false;
            }
            storageSession.currentWorkspaceSave.capturePassCursor = 0;
            storageSession.currentWorkspaceSave.chunkCursor = 0;
            storageSession.currentWorkspaceSave.loopWriteStage = DeferredLoopWriteStage::CapturePassHeader;
            return true;

        case DeferredLoopWriteStage::CapturePassHeader: {
            if (storageSession.currentWorkspaceSave.capturePassCursor >= snapshot.passes.capturePassCount()) {
                storageSession.currentWorkspaceSave.loopWriteStage = DeferredLoopWriteStage::EditTail;
                return true;
            }

            CapturePassSlotFileHeader passHeader{};
            const CommittedChunkIdList* committedChunkIds = nullptr;
            if (!selectDeferredCapturePass(snapshot.passes, storageSession.currentWorkspaceSave.capturePassCursor, passHeader,
                                           committedChunkIds) ||
                committedChunkIds == nullptr) {
                return false;
            }
            if (!writeDeferredCapturePassHeader(file, passHeader, *committedChunkIds)) {
                return false;
            }
            storageSession.currentWorkspaceSave.chunkCursor = 0;
            storageSession.currentWorkspaceSave.loopWriteStage = DeferredLoopWriteStage::CapturePassChunk;
            return true;
        }

        case DeferredLoopWriteStage::CapturePassChunk: {
            CapturePassSlotFileHeader passHeader{};
            const CommittedChunkIdList* committedChunkIds = nullptr;
            if (!selectDeferredCapturePass(snapshot.passes, storageSession.currentWorkspaceSave.capturePassCursor, passHeader,
                                           committedChunkIds) ||
                committedChunkIds == nullptr) {
                return false;
            }
            (void)passHeader;

            if (storageSession.currentWorkspaceSave.chunkCursor < committedChunkIds->size()) {
                const uint16_t chunkId = (*committedChunkIds)[storageSession.currentWorkspaceSave.chunkCursor++];
                return writeDeferredCapturePassChunk(file, chunkId);
            }

            ++storageSession.currentWorkspaceSave.capturePassCursor;
            storageSession.currentWorkspaceSave.loopWriteStage = DeferredLoopWriteStage::CapturePassHeader;
            return true;
        }

        case DeferredLoopWriteStage::EditTail: {
            const StorageIo io = storageIoFromFileWrite(file);
            if (!writePersistedEditsTail(io, snapshot.nextPassId, snapshot.passes.editPasses,
                                         snapshot.passes.loopGeometries,
                                         snapshot.passes.overdubPasses)) {
                return false;
            }
            resetDeferredLoopWriteState();
            loopDone = true;
            return true;
        }
    }

    return false;
}

STORAGE_PERSIST_MEM bool writeDeferredUndoEntryHeader(File& file, const UndoEntry& entry) {
    const uint8_t kind = static_cast<uint8_t>(entry.kind);
    if (!writeRaw(file, &entry.id, sizeof(entry.id))) return false;
    if (!writeRaw(file, &kind, sizeof(kind))) return false;
    if (!writeRaw(file, &entry.slotIndex, sizeof(entry.slotIndex))) return false;
    if (!writeRaw(file, &entry.loopId, sizeof(entry.loopId))) return false;
    return writeRaw(file, &entry.passId, sizeof(entry.passId));
}

STORAGE_PERSIST_MEM bool writeDeferredUndoEntryTail(File& file, const UndoEntry& entry) {
    if (!writeRaw(file, &entry.beforeGeometry, sizeof(entry.beforeGeometry))) return false;
    if (!writeRaw(file, &entry.afterGeometry, sizeof(entry.afterGeometry))) return false;
    if (!writeRaw(file, &entry.beforeLoopStartTick, sizeof(entry.beforeLoopStartTick))) return false;
    if (!writeRaw(file, &entry.beforeLoopLengthTicks, sizeof(entry.beforeLoopLengthTicks))) return false;
    if (!writeRaw(file, &entry.afterLoopStartTick, sizeof(entry.afterLoopStartTick))) return false;
    if (!writeRaw(file, &entry.afterLoopLengthTicks, sizeof(entry.afterLoopLengthTicks))) return false;

    const uint32_t beforeTrackState = static_cast<uint32_t>(entry.beforeTrackState);
    const uint32_t afterTrackState = static_cast<uint32_t>(entry.afterTrackState);
    if (!writeRaw(file, &beforeTrackState, sizeof(beforeTrackState))) return false;
    if (!writeRaw(file, &afterTrackState, sizeof(afterTrackState))) return false;
    if (!writeRaw(file, &entry.hasTrackState, sizeof(entry.hasTrackState))) return false;
    return writeRaw(file, &entry.hasRedoPayload, sizeof(entry.hasRedoPayload));
}

STORAGE_PERSIST_MEM bool writeDeferredSnapshotPresence(File& file, const LoopSnapshotRef& snapshot) {
    const bool hasSnapshot = snapshot != nullptr;
    return writeRaw(file, &hasSnapshot, sizeof(hasSnapshot));
}

STORAGE_PERSIST_MEM bool stepDeferredUndoStackPersist(File& file, const GlobalUndoStack& stack, bool& stackDone) {
    stackDone = false;

    switch (storageSession.currentWorkspaceSave.undoWriteStage) {
        case DeferredUndoWriteStage::Header: {
            const uint32_t entryCount = static_cast<uint32_t>(stack.entries.size());
            const uint32_t cursor = static_cast<uint32_t>(stack.cursor);
            const uint32_t nextEntryId = stack.nextEntryId;
            if (!writeRaw(file, &entryCount, sizeof(entryCount))) return false;
            if (!writeRaw(file, &cursor, sizeof(cursor))) return false;
            if (!writeRaw(file, &nextEntryId, sizeof(nextEntryId))) return false;
            storageSession.currentWorkspaceSave.undoEntryCursor = 0;
            storageSession.currentWorkspaceSave.undoWriteStage = DeferredUndoWriteStage::EntryHeader;
            return true;
        }

        case DeferredUndoWriteStage::EntryHeader: {
            if (storageSession.currentWorkspaceSave.undoEntryCursor >= stack.entries.size()) {
                resetDeferredUndoWriteState();
                stackDone = true;
                return true;
            }
            const UndoEntry& entry = stack.entries[storageSession.currentWorkspaceSave.undoEntryCursor];
            if (!writeDeferredUndoEntryHeader(file, entry)) {
                return false;
            }
            storageSession.currentWorkspaceSave.undoWriteStage = DeferredUndoWriteStage::BeforeSnapshotPresence;
            return true;
        }

        case DeferredUndoWriteStage::BeforeSnapshotPresence: {
            const UndoEntry& entry = stack.entries[storageSession.currentWorkspaceSave.undoEntryCursor];
            if (!writeDeferredSnapshotPresence(file, entry.beforeSnapshot)) {
                return false;
            }
            if (entry.beforeSnapshot) {
                resetDeferredLoopWriteState();
                storageSession.currentWorkspaceSave.undoWriteStage = DeferredUndoWriteStage::BeforeSnapshotLoop;
            } else {
                storageSession.currentWorkspaceSave.undoWriteStage = DeferredUndoWriteStage::AfterSnapshotPresence;
            }
            return true;
        }

        case DeferredUndoWriteStage::BeforeSnapshotLoop: {
            const UndoEntry& entry = stack.entries[storageSession.currentWorkspaceSave.undoEntryCursor];
            if (!entry.beforeSnapshot) {
                return false;
            }
            bool snapshotDone = false;
            if (!stepDeferredLoopSnapshotPersist(file, *entry.beforeSnapshot, snapshotDone)) {
                return false;
            }
            if (snapshotDone) {
                storageSession.currentWorkspaceSave.undoWriteStage = DeferredUndoWriteStage::AfterSnapshotPresence;
            }
            return true;
        }

        case DeferredUndoWriteStage::AfterSnapshotPresence: {
            const UndoEntry& entry = stack.entries[storageSession.currentWorkspaceSave.undoEntryCursor];
            if (!writeDeferredSnapshotPresence(file, entry.afterSnapshot)) {
                return false;
            }
            if (entry.afterSnapshot) {
                resetDeferredLoopWriteState();
                storageSession.currentWorkspaceSave.undoWriteStage = DeferredUndoWriteStage::AfterSnapshotLoop;
            } else {
                storageSession.currentWorkspaceSave.undoWriteStage = DeferredUndoWriteStage::EntryTail;
            }
            return true;
        }

        case DeferredUndoWriteStage::AfterSnapshotLoop: {
            const UndoEntry& entry = stack.entries[storageSession.currentWorkspaceSave.undoEntryCursor];
            if (!entry.afterSnapshot) {
                return false;
            }
            bool snapshotDone = false;
            if (!stepDeferredLoopSnapshotPersist(file, *entry.afterSnapshot, snapshotDone)) {
                return false;
            }
            if (snapshotDone) {
                storageSession.currentWorkspaceSave.undoWriteStage = DeferredUndoWriteStage::EntryTail;
            }
            return true;
        }

        case DeferredUndoWriteStage::EntryTail: {
            const UndoEntry& entry = stack.entries[storageSession.currentWorkspaceSave.undoEntryCursor];
            if (!writeDeferredUndoEntryTail(file, entry)) {
                return false;
            }
            ++storageSession.currentWorkspaceSave.undoEntryCursor;
            storageSession.currentWorkspaceSave.undoWriteStage = DeferredUndoWriteStage::EntryHeader;
            return true;
        }
    }

    return false;
}


STORAGE_PERSIST_MEM bool stepDeferredSaveJob() {
    switch (storageSession.currentWorkspaceSave.stage) {
        case DeferredSaveStage::CurrentSetMeta:
            return stepDeferredSaveJobCurrentSetMeta();
        case DeferredSaveStage::TrackHeaderAndSlots:
            return stepDeferredSaveJobTrackHeaderAndSlots();
        case DeferredSaveStage::CurrentSetLoopSlot:
            return stepDeferredSaveJobCurrentSetLoopSlot();
        case DeferredSaveStage::Footer:
            return stepDeferredSaveJobFooter();
        case DeferredSaveStage::UndoStacks:
            return stepDeferredSaveJobUndoStacks();
        case DeferredSaveStage::CurrentSetCompletion:
            return stepDeferredSaveJobCurrentSetCompletion();
        case DeferredSaveStage::Idle:
        default:
            return true;
    }
}

STORAGE_PERSIST_MEM bool completeScopedRuntimeBundleWorkItemIfDone() {
    if (!storageSession.persistenceWorkItem.bundleWriteActive) {
        return false;
    }
    const PersistWorkItem& scopedItem = storageSession.persistenceWorkItem.item;
    if (scopedItem.type == PersistWorkType::TrackMeta &&
        scopedItem.key.kind == PersistKeyKind::Track) {
        const uint8_t scopedTrackIndex = scopedItem.key.trackIndex;
        if (storageSession.currentWorkspaceSave.stage == DeferredSaveStage::Footer ||
            (storageSession.currentWorkspaceSave.stage == DeferredSaveStage::TrackHeaderAndSlots &&
             storageSession.currentWorkspaceSave.trackCursor > scopedTrackIndex)) {
            if (!closeDeferredMetaTempForLoopWrites()) {
                return false;
            }
            storageSession.currentWorkspaceSave.stage = DeferredSaveStage::Idle;
            return true;
        }
    }
    return false;
}

STORAGE_PERSIST_MEM bool stepDeferredRuntimeBundleSlice(bool& bundleDoneOut) {
    bundleDoneOut = false;
    if (storageSession.currentWorkspaceSave.stage == DeferredSaveStage::Idle) {
        // Finalize closed the meta temp handle; only treat as done for an active bundle write item.
        if (storageSession.persistenceWorkItem.bundleWriteActive &&
            !storageSession.currentWorkspaceSave.file) {
            bundleDoneOut = true;
        }
        return true;
    }
    if (storageSession.currentWorkspaceSave.stage == DeferredSaveStage::CurrentSetLoopSlot) {
        storageSession.currentWorkspaceSave.stage = DeferredSaveStage::Footer;
        storageSession.currentWorkspaceSave.footerWriteStage = DeferredFooterWriteStage::SelectedTrack;
        storageSession.currentWorkspaceSave.footerTrackCursor = 0;
    }
    const bool stepOk = stepDeferredSaveJob();
    if (!stepOk) {
        return false;
    }
    if (completeScopedRuntimeBundleWorkItemIfDone()) {
        bundleDoneOut = true;
        return true;
    }
    if (storageSession.currentWorkspaceSave.stage == DeferredSaveStage::Idle) {
        bundleDoneOut = true;
    }
    return true;
}

STORAGE_PERSIST_MEM void resetDeferredCompletionWriteState() {
    storageSession.currentWorkspaceSave.completionWriteStage =
        DeferredCompletionWriteStage::PatchLastActiveUnix;
    storageSession.currentWorkspaceSave.epochCrc = 0;
    storageSession.currentWorkspaceSave.epochCrcBodyOffset = 0;
    storageSession.currentWorkspaceSave.epochCrcBodySize = 0;
}

STORAGE_PERSIST_MEM void beginCurrentSetCompletion() {
    storageSession.currentWorkspaceSave.stage = DeferredSaveStage::CurrentSetCompletion;
    resetDeferredCompletionWriteState();
}

STORAGE_PERSIST_MEM bool stepDeferredWorkspaceFinalizeSlice(bool& finalizeDoneOut) {
    finalizeDoneOut = false;
    if (storageSession.currentWorkspaceSave.stage != DeferredSaveStage::CurrentSetCompletion) {
        beginCurrentSetCompletion();
    }
    const bool stepOk = stepDeferredSaveJob();
    if (!stepOk) {
        return false;
    }
    finalizeDoneOut = storageSession.currentWorkspaceSave.stage == DeferredSaveStage::Idle;
    return true;
}

}  // namespace StorageManagerInternal
