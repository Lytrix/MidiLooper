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
#include "StorageLoopIo.h"
#include "TrackManager.h"
#include "TrackUndo.h"
#include "Utils/DebugSessionCapture.h"
#include <Arduino.h>
#include <cstdio>

namespace StorageManagerInternal {

static void quarantineStorageFile() {
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

static void clearCurrentSetLoopSlotDirtyInternal(uint8_t trackIndex, uint8_t slotIndex) {
    if (trackIndex >= Config::NUM_TRACKS || slotIndex >= Config::MAX_LOOPS_PER_TRACK) {
        return;
    }
    currentSetLoopSlotDirty[trackIndex][slotIndex] = false;
}

STORAGE_PERSIST_MEM void resetDeferredLoopWriteState() {
    storageSession.currentWorkspaceSave.loopWriteStage = DeferredLoopWriteStage::Header;
    storageSession.currentWorkspaceSave.capturePassCursor = 0;
    storageSession.currentWorkspaceSave.chunkCursor = 0;
    storageSession.currentWorkspaceSave.midiBatch.clear();
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

STORAGE_PERSIST_MEM bool finalizeDeferredLoopSlotTemp(uint8_t trackIndex, uint8_t slotIndex) {
    if (!storageSession.currentWorkspaceSave.loopFileOpen) {
        return false;
    }
    if (!writeRaw(storageSession.currentWorkspaceSave.loopFile, &CurrentSetStorage::kSaveFileToken, sizeof(CurrentSetStorage::kSaveFileToken))) {
        storageSession.currentWorkspaceSave.loopFile.close();
        storageSession.currentWorkspaceSave.loopFileOpen = false;
        return false;
    }
    storageSession.currentWorkspaceSave.loopFile.close();
    storageSession.currentWorkspaceSave.loopFileOpen = false;

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
    resetDeferredLoopWriteState();
    resetDeferredUndoWriteState();
    storageSession.currentWorkspaceSave.stage = DeferredSaveStage::CurrentSetMeta;
    return true;
}


STORAGE_PERSIST_MEM bool selectDeferredCapturePass(const LoopPasses& passes, uint16_t cursor,
                               CapturePassSlotFileHeader& passHeader,
                               const ChunkIdList*& chunkRefs) {
    uint16_t overdubIndex = cursor;
    if (passes.hasRecordPass()) {
        if (cursor == 0) {
            passHeader.id = passes.recordPass.id;
            passHeader.mergeSequence = 0;
            passHeader.stateRaw = static_cast<uint8_t>(passes.recordPass.state);
            passHeader.typeRaw = 0;
            passHeader.sealedAtTick = passes.recordPass.sealedAtTick;
            chunkRefs = &passes.recordPass.chunkRefs;
            return true;
        }
        overdubIndex = cursor - 1;
    }

    if (overdubIndex >= passes.overdubPasses.size()) {
        chunkRefs = nullptr;
        return false;
    }

    const OverdubPass& pass = passes.overdubPasses[overdubIndex];
    passHeader.id = pass.id;
    passHeader.mergeSequence = pass.mergeSequence;
    passHeader.stateRaw = static_cast<uint8_t>(pass.state);
    passHeader.typeRaw = 1;
    passHeader.sealedAtTick = pass.sealedAtTick;
    chunkRefs = &pass.chunkRefs;
    return true;
}

STORAGE_PERSIST_MEM bool writeDeferredLoopHeader(File& file, LoopId loopId, uint32_t startLoopTick,
                             uint32_t loopLengthTicks, uint32_t loopStartTick,
                             PassId nextPassId, uint32_t nextMergeSequence,
                             PassId lastPublishedPassId, const LoopPasses& passes,
                             LoopPersistPayloadCrc crcMode = LoopPersistPayloadCrc::None) {
    if (!persistenceWriteRaw(file, &loopId, sizeof(loopId), crcMode)) return false;
    if (!persistenceWriteRaw(file, &startLoopTick, sizeof(startLoopTick), crcMode)) return false;
    if (!persistenceWriteRaw(file, &loopLengthTicks, sizeof(loopLengthTicks), crcMode)) {
        return false;
    }
    if (!persistenceWriteRaw(file, &loopStartTick, sizeof(loopStartTick), crcMode)) return false;
    if (!persistenceWriteRaw(file, &nextPassId, sizeof(nextPassId), crcMode)) return false;
    if (!persistenceWriteRaw(file, &nextMergeSequence, sizeof(nextMergeSequence), crcMode)) {
        return false;
    }
    if (!persistenceWriteRaw(file, &lastPublishedPassId, sizeof(lastPublishedPassId), crcMode)) {
        return false;
    }

    const uint32_t persistedCount = static_cast<uint32_t>(passes.capturePassCount());
    return persistenceWriteRaw(file, &persistedCount, sizeof(persistedCount), crcMode);
}

STORAGE_PERSIST_MEM bool writeDeferredCapturePassHeader(File& file, const CapturePassSlotFileHeader& passHeader,
                                    const ChunkIdList& chunkRefs,
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
        static_cast<uint32_t>(LoopEventStore::countEventsInChunkIds(chunkRefs));
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
                              LoopPersistPayloadCrc crcMode = LoopPersistPayloadCrc::None) {
    loopDone = false;

    switch (storageSession.currentWorkspaceSave.loopWriteStage) {
        case DeferredLoopWriteStage::Header:
            if (!writeDeferredLoopHeader(file, loop.loopId, loop.startLoopTick, loop.loopLengthTicks,
                                         loop.loopStartTick, loop.nextPassId_,
                                         loop.nextMergeSequence_, loop.lastPublishedPassId_,
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
            const ChunkIdList* chunkRefs = nullptr;
            if (!selectDeferredCapturePass(loop.passes, storageSession.currentWorkspaceSave.capturePassCursor, passHeader, chunkRefs) ||
                chunkRefs == nullptr) {
                return false;
            }
            if (!writeDeferredCapturePassHeader(file, passHeader, *chunkRefs, crcMode)) {
                return false;
            }
            storageSession.currentWorkspaceSave.chunkCursor = 0;
            storageSession.currentWorkspaceSave.loopWriteStage = DeferredLoopWriteStage::CapturePassChunk;
            return true;
        }

        case DeferredLoopWriteStage::CapturePassChunk: {
            CapturePassSlotFileHeader passHeader{};
            const ChunkIdList* chunkRefs = nullptr;
            if (!selectDeferredCapturePass(loop.passes, storageSession.currentWorkspaceSave.capturePassCursor, passHeader, chunkRefs) ||
                chunkRefs == nullptr) {
                return false;
            }
            (void)passHeader;

            if (storageSession.currentWorkspaceSave.chunkCursor < chunkRefs->size()) {
                const uint16_t chunkId = (*chunkRefs)[storageSession.currentWorkspaceSave.chunkCursor++];
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
            if (!writePersistedEditsTail(io, loop.nextPassId_, loop.passes.editPasses)) {
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
            if (!writeDeferredLoopHeader(file, loopId, 0, 0, 0, 1, 0, kInvalidPassId,
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
            if (!writePersistedEditsTail(io, 1, emptyPasses.editPasses)) {
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
                                         snapshot.nextPassId, snapshot.nextMergeSequence,
                                         snapshot.lastPublishedPassId, snapshot.passes)) {
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
            const ChunkIdList* chunkRefs = nullptr;
            if (!selectDeferredCapturePass(snapshot.passes, storageSession.currentWorkspaceSave.capturePassCursor, passHeader,
                                           chunkRefs) ||
                chunkRefs == nullptr) {
                return false;
            }
            if (!writeDeferredCapturePassHeader(file, passHeader, *chunkRefs)) {
                return false;
            }
            storageSession.currentWorkspaceSave.chunkCursor = 0;
            storageSession.currentWorkspaceSave.loopWriteStage = DeferredLoopWriteStage::CapturePassChunk;
            return true;
        }

        case DeferredLoopWriteStage::CapturePassChunk: {
            CapturePassSlotFileHeader passHeader{};
            const ChunkIdList* chunkRefs = nullptr;
            if (!selectDeferredCapturePass(snapshot.passes, storageSession.currentWorkspaceSave.capturePassCursor, passHeader,
                                           chunkRefs) ||
                chunkRefs == nullptr) {
                return false;
            }
            (void)passHeader;

            if (storageSession.currentWorkspaceSave.chunkCursor < chunkRefs->size()) {
                const uint16_t chunkId = (*chunkRefs)[storageSession.currentWorkspaceSave.chunkCursor++];
                return writeDeferredCapturePassChunk(file, chunkId);
            }

            ++storageSession.currentWorkspaceSave.capturePassCursor;
            storageSession.currentWorkspaceSave.loopWriteStage = DeferredLoopWriteStage::CapturePassHeader;
            return true;
        }

        case DeferredLoopWriteStage::EditTail: {
            const StorageIo io = storageIoFromFileWrite(file);
            if (!writePersistedEditsTail(io, snapshot.nextPassId, snapshot.passes.editPasses)) {
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
            switch (storageSession.currentWorkspaceSave.globalHeaderStage) {
                case DeferredGlobalHeaderStage::Version:
                    storageSession.currentWorkspaceSave.globalHeaderStage = DeferredGlobalHeaderStage::Bpm;
                    return true;

                case DeferredGlobalHeaderStage::Bpm: {
                    const float savedBpm = bpm;
                    if (!writeRaw(storageSession.currentWorkspaceSave.file, &savedBpm, sizeof(savedBpm))) {
                        Serial.println("[StorageManager] ERROR: Deferred save failed writing BPM");
                        return false;
                    }
                    storageSession.currentWorkspaceSave.globalHeaderStage = DeferredGlobalHeaderStage::LooperState;
                    return true;
                }

                case DeferredGlobalHeaderStage::LooperState: {
                    const uint32_t looperStateVal =
                        persistedLooperStateRaw(storageSession.currentWorkspaceSave.stateSnapshot);
                    if (!writeRaw(storageSession.currentWorkspaceSave.file, &looperStateVal, sizeof(looperStateVal))) {
                        Serial.println("[StorageManager] ERROR: Deferred save failed writing looper state");
                        return false;
                    }
                    storageSession.currentWorkspaceSave.globalHeaderStage = DeferredGlobalHeaderStage::MasterLoopLength;
                    return true;
                }

                case DeferredGlobalHeaderStage::MasterLoopLength: {
                    const uint32_t masterLoopLength = trackManager.getMasterLoopLength();
                    if (!writeRaw(storageSession.currentWorkspaceSave.file, &masterLoopLength, sizeof(masterLoopLength))) {
                        Serial.println("[StorageManager] ERROR: Deferred save failed writing master loop length");
                        return false;
                    }
                    storageSession.currentWorkspaceSave.globalHeaderStage = DeferredGlobalHeaderStage::TrackCount;
                    return true;
                }

                case DeferredGlobalHeaderStage::TrackCount:
                    if (!writeRaw(storageSession.currentWorkspaceSave.file, &storageSession.currentWorkspaceSave.numTracks,
                                  sizeof(storageSession.currentWorkspaceSave.numTracks))) {
                        Serial.println("[StorageManager] ERROR: Deferred save failed writing track count");
                        return false;
                    }
                    storageSession.currentWorkspaceSave.trackCursor = 0;
                    storageSession.currentWorkspaceSave.slotCursor = 0;
                    storageSession.currentWorkspaceSave.poolCursor = 0;
                    storageSession.currentWorkspaceSave.trackHeaderWritten = false;
                    storageSession.currentWorkspaceSave.trackWriteStage = DeferredTrackWriteStage::TrackState;
                    storageSession.currentWorkspaceSave.slotWriteStage = DeferredSlotWriteStage::SlotEnabled;
                    storageSession.currentWorkspaceSave.stage = DeferredSaveStage::TrackHeaderAndSlots;
                    return true;
            }
            return false;

        case DeferredSaveStage::TrackHeaderAndSlots: {
            Track& track = trackManager.getTrack(storageSession.currentWorkspaceSave.trackCursor);
            if (!storageSession.currentWorkspaceSave.trackHeaderWritten) {
                switch (storageSession.currentWorkspaceSave.trackWriteStage) {
                    case DeferredTrackWriteStage::TrackState: {
                        TrackState stateToSave = track.getState();
                        if (stateToSave == TRACK_OVERDUBBING) {
                            stateToSave = TRACK_PLAYING;
                        }
                        const uint32_t trackState = static_cast<uint32_t>(stateToSave);
                        if (!writeRaw(storageSession.currentWorkspaceSave.file, &trackState, sizeof(trackState))) {
                            Serial.print("[StorageManager] ERROR: Deferred save failed writing trackState for track ");
                            Serial.println(storageSession.currentWorkspaceSave.trackCursor);
                            return false;
                        }
                        storageSession.currentWorkspaceSave.trackWriteStage = DeferredTrackWriteStage::Muted;
                        return true;
                    }

                    case DeferredTrackWriteStage::Muted: {
                        const bool muted = track.isMuted();
                        if (!writeRaw(storageSession.currentWorkspaceSave.file, &muted, sizeof(muted))) {
                            Serial.print("[StorageManager] ERROR: Deferred save failed writing muted for track ");
                            Serial.println(storageSession.currentWorkspaceSave.trackCursor);
                            return false;
                        }
                        storageSession.currentWorkspaceSave.trackHeaderWritten = true;
                        storageSession.currentWorkspaceSave.trackWriteStage = DeferredTrackWriteStage::TrackState;
                        storageSession.currentWorkspaceSave.slotWriteStage = DeferredSlotWriteStage::SlotEnabled;
                        return true;
                    }
                }
                return false;
            }

            if (storageSession.currentWorkspaceSave.slotCursor < Config::MAX_LOOPS_PER_TRACK) {
                const uint8_t slot = storageSession.currentWorkspaceSave.slotCursor;
                switch (storageSession.currentWorkspaceSave.slotWriteStage) {
                    case DeferredSlotWriteStage::SlotEnabled: {
                        const bool slotEnabled =
                            trackManager.isSlotEnabled(storageSession.currentWorkspaceSave.trackCursor, slot);
                        if (!writeRaw(storageSession.currentWorkspaceSave.file, &slotEnabled, sizeof(slotEnabled))) {
                            Serial.print("[StorageManager] ERROR: Deferred save failed writing slotEnabled for track ");
                            Serial.print(storageSession.currentWorkspaceSave.trackCursor);
                            Serial.print(" slot ");
                            Serial.println(slot);
                            return false;
                        }
                        storageSession.currentWorkspaceSave.slotWriteStage = DeferredSlotWriteStage::SlotMuted;
                        return true;
                    }

                    case DeferredSlotWriteStage::SlotMuted: {
                        const bool slotMuted =
                            trackManager.isSlotMuted(storageSession.currentWorkspaceSave.trackCursor, slot);
                        if (!writeRaw(storageSession.currentWorkspaceSave.file, &slotMuted, sizeof(slotMuted))) {
                            Serial.print("[StorageManager] ERROR: Deferred save failed writing slotMuted for track ");
                            Serial.print(storageSession.currentWorkspaceSave.trackCursor);
                            Serial.print(" slot ");
                            Serial.println(slot);
                            return false;
                        }
                        storageSession.currentWorkspaceSave.slotWriteStage = DeferredSlotWriteStage::SlotLoopId;
                        return true;
                    }

                    case DeferredSlotWriteStage::SlotLoopId: {
                        const LoopId slotLoopId = track.slotRef(slot).loopId;
                        if (!writeRaw(storageSession.currentWorkspaceSave.file, &slotLoopId, sizeof(slotLoopId))) {
                            Serial.print("[StorageManager] ERROR: Deferred save failed writing slotLoopId for track ");
                            Serial.print(storageSession.currentWorkspaceSave.trackCursor);
                            Serial.print(" slot ");
                            Serial.println(slot);
                            return false;
                        }
                        storageSession.currentWorkspaceSave.slotCursor++;
                        storageSession.currentWorkspaceSave.slotWriteStage = DeferredSlotWriteStage::SlotEnabled;
                        return true;
                    }
                }
                return false;
            }

            if (!trackHasCurrentSetDirtyLoopSlot(storageSession.currentWorkspaceSave.trackCursor)) {
                storageSession.currentWorkspaceSave.loopSlotsSkipped += Config::MAX_LOOPS_PER_TRACK;
                storageSession.currentWorkspaceSave.trackCursor++;
                storageSession.currentWorkspaceSave.slotCursor = 0;
                storageSession.currentWorkspaceSave.poolCursor = 0;
                storageSession.currentWorkspaceSave.trackHeaderWritten = false;
                storageSession.currentWorkspaceSave.trackWriteStage = DeferredTrackWriteStage::TrackState;
                storageSession.currentWorkspaceSave.slotWriteStage = DeferredSlotWriteStage::SlotEnabled;
                if (storageSession.currentWorkspaceSave.trackCursor < storageSession.currentWorkspaceSave.numTracks) {
                    return true;
                }
                storageSession.currentWorkspaceSave.stage = DeferredSaveStage::Footer;
                storageSession.currentWorkspaceSave.footerWriteStage = DeferredFooterWriteStage::SelectedTrack;
                storageSession.currentWorkspaceSave.footerTrackCursor = 0;
                return true;
            }

            storageSession.currentWorkspaceSave.poolCursor = 0;
            resetDeferredLoopWriteState();
            if (!closeDeferredMetaTempForLoopWrites()) {
                return false;
            }
            storageSession.currentWorkspaceSave.stage = DeferredSaveStage::CurrentSetLoopSlot;
            return true;
        }

        case DeferredSaveStage::CurrentSetLoopSlot: {
            const uint8_t trackIndex = storageSession.currentWorkspaceSave.trackCursor;
            const uint8_t slotIndex = storageSession.currentWorkspaceSave.poolCursor;
            if (!shouldWriteCurrentSetLoopSlot(trackIndex, slotIndex)) {
                ++storageSession.currentWorkspaceSave.loopSlotsSkipped;
                storageSession.currentWorkspaceSave.poolCursor++;
                if (storageSession.currentWorkspaceSave.poolCursor < Config::MAX_LOOPS_PER_TRACK) {
                    return true;
                }
                storageSession.currentWorkspaceSave.trackCursor++;
                if (storageSession.currentWorkspaceSave.trackCursor < storageSession.currentWorkspaceSave.numTracks) {
                    storageSession.currentWorkspaceSave.slotCursor = 0;
                    storageSession.currentWorkspaceSave.poolCursor = 0;
                    resetDeferredLoopWriteState();
                    storageSession.currentWorkspaceSave.trackHeaderWritten = false;
                    storageSession.currentWorkspaceSave.trackWriteStage = DeferredTrackWriteStage::TrackState;
                    storageSession.currentWorkspaceSave.slotWriteStage = DeferredSlotWriteStage::SlotEnabled;
                    if (!reopenDeferredMetaTempForAppend()) {
                        return false;
                    }
                    storageSession.currentWorkspaceSave.stage = DeferredSaveStage::TrackHeaderAndSlots;
                    return true;
                }
                if (!reopenDeferredMetaTempForAppend()) {
                    return false;
                }
                storageSession.currentWorkspaceSave.stage = DeferredSaveStage::Footer;
                storageSession.currentWorkspaceSave.footerWriteStage = DeferredFooterWriteStage::SelectedTrack;
                storageSession.currentWorkspaceSave.footerTrackCursor = 0;
                return true;
            }

            if (!storageSession.currentWorkspaceSave.loopFileOpen &&
                !openDeferredLoopSlotTemp(trackIndex, slotIndex)) {
                return false;
            }
            Track& track = trackManager.getTrack(storageSession.currentWorkspaceSave.trackCursor);
            bool loopDone = false;
            const bool loopWriteOk = track.loopsAllocated()
                                         ? stepDeferredLoopPersist(
                                               storageSession.currentWorkspaceSave.loopFile,
                                               track.getLoop(storageSession.currentWorkspaceSave.poolCursor), loopDone)
                                         : stepDeferredEmptyLoopPersist(
                                               storageSession.currentWorkspaceSave.loopFile,
                                               static_cast<LoopId>(storageSession.currentWorkspaceSave.poolCursor),
                                               loopDone);
            if (!loopWriteOk) {
                Serial.print("[StorageManager] ERROR: Deferred save failed writing loop pool entry track ");
                Serial.print(storageSession.currentWorkspaceSave.trackCursor);
                Serial.print(" pool ");
                Serial.println(storageSession.currentWorkspaceSave.poolCursor);
                return false;
            }
            if (!loopDone) {
                return true;
            }

            if (!finalizeDeferredLoopSlotTemp(trackIndex, slotIndex)) {
                Serial.print("[StorageManager] ERROR: Deferred save failed finalizing loop slot track ");
                Serial.print(trackIndex);
                Serial.print(" slot ");
                Serial.println(slotIndex);
                return false;
            }
            ++storageSession.currentWorkspaceSave.loopSlotsWritten;
            clearCurrentSetLoopSlotDirtyInternal(trackIndex, slotIndex);

            storageSession.currentWorkspaceSave.poolCursor++;
            if (storageSession.currentWorkspaceSave.poolCursor < Config::MAX_LOOPS_PER_TRACK) {
                resetDeferredLoopWriteState();
                return true;
            }

            storageSession.currentWorkspaceSave.trackCursor++;
            if (storageSession.currentWorkspaceSave.trackCursor < storageSession.currentWorkspaceSave.numTracks) {
                storageSession.currentWorkspaceSave.slotCursor = 0;
                storageSession.currentWorkspaceSave.poolCursor = 0;
                resetDeferredLoopWriteState();
                storageSession.currentWorkspaceSave.trackHeaderWritten = false;
                storageSession.currentWorkspaceSave.trackWriteStage = DeferredTrackWriteStage::TrackState;
                storageSession.currentWorkspaceSave.slotWriteStage = DeferredSlotWriteStage::SlotEnabled;
                if (!reopenDeferredMetaTempForAppend()) {
                    return false;
                }
                storageSession.currentWorkspaceSave.stage = DeferredSaveStage::TrackHeaderAndSlots;
                return true;
            }

            if (!reopenDeferredMetaTempForAppend()) {
                return false;
            }
            storageSession.currentWorkspaceSave.stage = DeferredSaveStage::Footer;
            storageSession.currentWorkspaceSave.footerWriteStage = DeferredFooterWriteStage::SelectedTrack;
            storageSession.currentWorkspaceSave.footerTrackCursor = 0;
            return true;
        }

        case DeferredSaveStage::Footer: {
            switch (storageSession.currentWorkspaceSave.footerWriteStage) {
                case DeferredFooterWriteStage::SelectedTrack: {
                    const uint8_t selectedTrackIdx = trackManager.getSelectedTrackIndex();
                    if (!writeRaw(storageSession.currentWorkspaceSave.file, &selectedTrackIdx,
                                  sizeof(selectedTrackIdx))) {
                        Serial.println("[StorageManager] ERROR: Deferred save failed writing selected track index");
                        return false;
                    }
                    storageSession.currentWorkspaceSave.footerTrackCursor = 0;
                    storageSession.currentWorkspaceSave.footerWriteStage = DeferredFooterWriteStage::ActiveLoopIndex;
                    return true;
                }

                case DeferredFooterWriteStage::ActiveLoopIndex:
                    if (storageSession.currentWorkspaceSave.footerTrackCursor < storageSession.currentWorkspaceSave.numTracks) {
                        const uint8_t activeIdx =
                            trackManager.getActiveLoopIndex(storageSession.currentWorkspaceSave.footerTrackCursor);
                        if (!writeRaw(storageSession.currentWorkspaceSave.file, &activeIdx, sizeof(activeIdx))) {
                            Serial.print("[StorageManager] ERROR: Deferred save failed writing activeLoopIndex for track ");
                            Serial.println(storageSession.currentWorkspaceSave.footerTrackCursor);
                            return false;
                        }
                        ++storageSession.currentWorkspaceSave.footerTrackCursor;
                        return true;
                    }
                    storageSession.currentWorkspaceSave.footerWriteStage = DeferredFooterWriteStage::GlobalUndoStackToken;
                    return true;

                case DeferredFooterWriteStage::GlobalUndoStackToken:
                    if (!writeRaw(storageSession.currentWorkspaceSave.file, &kGlobalUndoStackToken,
                                  sizeof(kGlobalUndoStackToken))) {
                        Serial.println("[StorageManager] ERROR: Deferred save failed writing global undo stack token");
                        return false;
                    }
                    storageSession.currentWorkspaceSave.undoTrackCursor = 0;
                    resetDeferredUndoWriteState();
                    storageSession.currentWorkspaceSave.stage = DeferredSaveStage::UndoStacks;
                    return true;
            }
            return false;
        }

        case DeferredSaveStage::UndoStacks: {
            if (storageSession.currentWorkspaceSave.undoTrackCursor < storageSession.currentWorkspaceSave.numTracks) {
                const Track& track = trackManager.getTrack(storageSession.currentWorkspaceSave.undoTrackCursor);
                bool stackDone = false;
                if (!stepDeferredUndoStackPersist(storageSession.currentWorkspaceSave.file, track.getGlobalUndoStack(),
                                                  stackDone)) {
                    Serial.print("[StorageManager] ERROR: Deferred save failed writing global undo stack for track ");
                    Serial.println(storageSession.currentWorkspaceSave.undoTrackCursor);
                    return false;
                }
                if (!stackDone) {
                    return true;
                }
                storageSession.currentWorkspaceSave.undoTrackCursor++;
                resetDeferredUndoWriteState();
                return true;
            }

            if (!finalizeDeferredMetaTempFile()) {
                Serial.println("[StorageManager] ERROR: Deferred save failed finalizing CurrentSet meta");
                return false;
            }
            storageSession.currentWorkspaceSave.stage = DeferredSaveStage::CurrentSetCompletion;
            return true;
        }

        case DeferredSaveStage::CurrentSetCompletion: {
            const uint32_t lastActiveUnix = RtcTime::getUnixTime();
            if (!CurrentSetStorage::patchLastActiveUnix(CurrentSetStorage::kCurrentMetaPath,
                                                        lastActiveUnix)) {
                Serial.println("[StorageManager] ERROR: Deferred save failed patching lastActiveUnix");
                return false;
            }
            currentSetLastActiveUnix = lastActiveUnix;
            if (!writeWorkspaceMetaAfterDeferredSave()) {
                Serial.println("[StorageManager] ERROR: Deferred save failed writing workspace.bin");
                return false;
            }
            if (quarantineLegacyMonolithAfterSave) {
                quarantineStorageFile();
                quarantineLegacyMonolithAfterSave = false;
                Serial.println("[StorageManager] v5 monolith quarantined after CurrentSet save.");
            }
            forceCurrentSetFullLoopWrite = false;
            Serial.println("[StorageManager] CurrentSet saved successfully (v6 deferred slices).");
            storageSession.currentWorkspaceSave.inProgress = false;
            storageSession.currentWorkspaceSave.stage = DeferredSaveStage::Idle;
            return true;
        }

        case DeferredSaveStage::Idle:
        default:
            return true;
    }
}

}  // namespace StorageManagerInternal
