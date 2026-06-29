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
    deferredLoopWriteStage = DeferredLoopWriteStage::Header;
    deferredSaveCapturePassCursor = 0;
    deferredSaveChunkCursor = 0;
    deferredSaveMidiBatch.clear();
}

STORAGE_PERSIST_MEM void resetDeferredUndoWriteState() {
    deferredUndoWriteStage = DeferredUndoWriteStage::Header;
    deferredSaveUndoEntryCursor = 0;
    resetDeferredLoopWriteState();
}


STORAGE_PERSIST_MEM void resetDeferredSaveJobState() {
    if (deferredSaveFile) {
        deferredSaveFile.close();
    }
    if (deferredSaveLoopFile) {
        deferredSaveLoopFile.close();
    }
    deferredSaveLoopFileOpen = false;
    deferredSaveInProgress = false;
    deferredSaveSdIoActive = false;
    deferredSaveStage = DeferredSaveStage::Idle;
    deferredGlobalHeaderStage = DeferredGlobalHeaderStage::Bpm;
    deferredTrackWriteStage = DeferredTrackWriteStage::TrackState;
    deferredSlotWriteStage = DeferredSlotWriteStage::SlotEnabled;
    deferredFooterWriteStage = DeferredFooterWriteStage::SelectedTrack;
    deferredSaveNumTracks = 0;
    deferredSaveTrackCursor = 0;
    deferredSaveSlotCursor = 0;
    deferredSavePoolCursor = 0;
    deferredSaveUndoTrackCursor = 0;
    deferredSaveFooterTrackCursor = 0;
    resetDeferredLoopWriteState();
    resetDeferredUndoWriteState();
    deferredSaveTrackHeaderWritten = false;
    deferredSaveStartedAtUs = 0;
    deferredSaveHeapBefore = 0;
    deferredSaveAdmissionHeap = 0;
    deferredSaveHeapFloorDeferred = false;
    deferredSaveUrgentRequested = false;
    deferredSaveLoopSlotsWritten = 0;
    deferredSaveLoopSlotsSkipped = 0;
    deferredSaveDisplayBlockUs = 0;
    deferredSaveStateSnapshot = LOOPER_IDLE;
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
    if (!writeRaw(deferredSaveFile, &CurrentSetStorage::kSaveFileToken, sizeof(CurrentSetStorage::kSaveFileToken))) {
        return false;
    }
    deferredSaveFile.close();
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
    if (deferredSaveFile) {
        deferredSaveFile.close();
    }
    return true;
}

STORAGE_PERSIST_MEM bool reopenDeferredMetaTempForAppend() {
    deferredSaveFile = SD.open(CurrentSetStorage::kCurrentMetaTempPath, FILE_WRITE);
    if (!deferredSaveFile) {
        Serial.println("[StorageManager] ERROR: Could not reopen CurrentSet meta temp for append");
        return false;
    }
    if (!deferredSaveFile.seek(deferredSaveFile.size())) {
        deferredSaveFile.close();
        return false;
    }
    return true;
}

STORAGE_PERSIST_MEM bool openDeferredLoopSlotTemp(uint8_t trackIndex, uint8_t slotIndex) {
    if (deferredSaveLoopFileOpen) {
        deferredSaveLoopFile.close();
        deferredSaveLoopFileOpen = false;
    }
    char tempPath[48];
    if (!CurrentSetStorage::formatLoopSlotTempPath(tempPath, sizeof(tempPath), trackIndex,
                                                   slotIndex)) {
        return false;
    }
    deferredSaveLoopFile = SD.open(tempPath, FILE_WRITE);
    if (!deferredSaveLoopFile) {
        Serial.print("[StorageManager] ERROR: Could not open loop temp file: ");
        Serial.println(tempPath);
        return false;
    }
    deferredSaveLoopFile.seek(0);
    if (!CurrentWorkspaceStorage::writeEpochHeaderPlaceholder(deferredSaveLoopFile,
                                                              deferredSaveWorkspaceEpoch)) {
        deferredSaveLoopFile.close();
        deferredSaveLoopFileOpen = false;
        return false;
    }
    deferredSaveLoopFileOpen = true;
    return true;
}

STORAGE_PERSIST_MEM bool finalizeDeferredLoopSlotTemp(uint8_t trackIndex, uint8_t slotIndex) {
    if (!deferredSaveLoopFileOpen) {
        return false;
    }
    if (!writeRaw(deferredSaveLoopFile, &CurrentSetStorage::kSaveFileToken, sizeof(CurrentSetStorage::kSaveFileToken))) {
        deferredSaveLoopFile.close();
        deferredSaveLoopFileOpen = false;
        return false;
    }
    deferredSaveLoopFile.close();
    deferredSaveLoopFileOpen = false;

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
    deferredSaveWorkspaceEpoch = currentWorkspaceEpoch;

    if (!CurrentSetStorage::ensureDirectory(PersistenceLayout::kRoot) ||
        !CurrentSetStorage::ensureDirectory(CurrentSetStorage::kCurrentSetDir) ||
        !CurrentSetStorage::ensureDirectory(CurrentWorkspaceStorage::kCurrentTempDir) ||
        !CurrentSetStorage::ensureDirectory(CurrentSetStorage::kCurrentSlotsDir) ||
        !CurrentSetStorage::ensureDirectory(PersistenceLayout::kRecoveryRoot) ||
        !CurrentSetStorage::ensureDirectory(CurrentSetStorage::kCheckpointsDir)) {
        Serial.println("[StorageManager] ERROR: Could not create MidiLooper/current directories");
        return false;
    }

    deferredSaveFile = SD.open(CurrentSetStorage::kCurrentMetaTempPath, FILE_WRITE);
    if (!deferredSaveFile) {
        Serial.println("[StorageManager] ERROR: Could not open CurrentSet meta temp file");
        return false;
    }
    deferredSaveFile.seek(0);

    if (!CurrentWorkspaceStorage::writeEpochHeaderPlaceholder(deferredSaveFile,
                                                              deferredSaveWorkspaceEpoch)) {
        Serial.println("[StorageManager] ERROR: Deferred save failed writing epoch header");
        deferredSaveFile.close();
        return false;
    }

    if (!writeCurrentSetMetaHeaderToOpenFile(deferredSaveFile)) {
        Serial.println("[StorageManager] ERROR: Deferred save failed writing CurrentSet meta header");
        deferredSaveFile.close();
        return false;
    }

    deferredSaveStateSnapshot = state;
    deferredSaveNumTracks = Config::NUM_TRACKS;
    deferredGlobalHeaderStage = DeferredGlobalHeaderStage::Bpm;
    deferredSaveTrackCursor = 0;
    deferredSaveSlotCursor = 0;
    deferredSavePoolCursor = 0;
    deferredSaveUndoTrackCursor = 0;
    deferredSaveFooterTrackCursor = 0;
    deferredSaveTrackHeaderWritten = false;
    deferredTrackWriteStage = DeferredTrackWriteStage::TrackState;
    deferredSlotWriteStage = DeferredSlotWriteStage::SlotEnabled;
    deferredFooterWriteStage = DeferredFooterWriteStage::SelectedTrack;
    resetDeferredLoopWriteState();
    resetDeferredUndoWriteState();
    deferredSaveStage = DeferredSaveStage::CurrentSetMeta;
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
    deferredSaveMidiBatch.clear();
    LoopEventStore::appendChunkRefEvent(chunkId, deferredSaveMidiBatch);
    if (deferredSaveMidiBatch.empty()) {
        return true;
    }
    return persistenceWriteRaw(file, deferredSaveMidiBatch.data(),
                              deferredSaveMidiBatch.size() * sizeof(MidiEvent), crcMode);
}

STORAGE_PERSIST_MEM bool stepDeferredLoopPersist(File& file, const Loop& loop, bool& loopDone,
                              LoopPersistPayloadCrc crcMode = LoopPersistPayloadCrc::None) {
    loopDone = false;

    switch (deferredLoopWriteStage) {
        case DeferredLoopWriteStage::Header:
            if (!writeDeferredLoopHeader(file, loop.loopId, loop.startLoopTick, loop.loopLengthTicks,
                                         loop.loopStartTick, loop.nextPassId_,
                                         loop.nextMergeSequence_, loop.lastPublishedPassId_,
                                         loop.passes, crcMode)) {
                return false;
            }
            deferredSaveCapturePassCursor = 0;
            deferredSaveChunkCursor = 0;
            deferredLoopWriteStage = DeferredLoopWriteStage::CapturePassHeader;
            return true;

        case DeferredLoopWriteStage::CapturePassHeader: {
            if (deferredSaveCapturePassCursor >= loop.passes.capturePassCount()) {
                deferredLoopWriteStage = DeferredLoopWriteStage::EditTail;
                return true;
            }

            CapturePassSlotFileHeader passHeader{};
            const ChunkIdList* chunkRefs = nullptr;
            if (!selectDeferredCapturePass(loop.passes, deferredSaveCapturePassCursor, passHeader, chunkRefs) ||
                chunkRefs == nullptr) {
                return false;
            }
            if (!writeDeferredCapturePassHeader(file, passHeader, *chunkRefs, crcMode)) {
                return false;
            }
            deferredSaveChunkCursor = 0;
            deferredLoopWriteStage = DeferredLoopWriteStage::CapturePassChunk;
            return true;
        }

        case DeferredLoopWriteStage::CapturePassChunk: {
            CapturePassSlotFileHeader passHeader{};
            const ChunkIdList* chunkRefs = nullptr;
            if (!selectDeferredCapturePass(loop.passes, deferredSaveCapturePassCursor, passHeader, chunkRefs) ||
                chunkRefs == nullptr) {
                return false;
            }
            (void)passHeader;

            if (deferredSaveChunkCursor < chunkRefs->size()) {
                const uint16_t chunkId = (*chunkRefs)[deferredSaveChunkCursor++];
                return writeDeferredCapturePassChunk(file, chunkId, crcMode);
            }

            ++deferredSaveCapturePassCursor;
            deferredLoopWriteStage = DeferredLoopWriteStage::CapturePassHeader;
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

    switch (deferredLoopWriteStage) {
        case DeferredLoopWriteStage::Header:
            if (!writeDeferredLoopHeader(file, loopId, 0, 0, 0, 1, 0, kInvalidPassId,
                                         emptyPasses)) {
                return false;
            }
            deferredLoopWriteStage = DeferredLoopWriteStage::EditTail;
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

    switch (deferredLoopWriteStage) {
        case DeferredLoopWriteStage::Header:
            if (!writeDeferredLoopHeader(file, snapshot.loopId, snapshot.startLoopTick,
                                         snapshot.loopLengthTicks, snapshot.loopStartTick,
                                         snapshot.nextPassId, snapshot.nextMergeSequence,
                                         snapshot.lastPublishedPassId, snapshot.passes)) {
                return false;
            }
            deferredSaveCapturePassCursor = 0;
            deferredSaveChunkCursor = 0;
            deferredLoopWriteStage = DeferredLoopWriteStage::CapturePassHeader;
            return true;

        case DeferredLoopWriteStage::CapturePassHeader: {
            if (deferredSaveCapturePassCursor >= snapshot.passes.capturePassCount()) {
                deferredLoopWriteStage = DeferredLoopWriteStage::EditTail;
                return true;
            }

            CapturePassSlotFileHeader passHeader{};
            const ChunkIdList* chunkRefs = nullptr;
            if (!selectDeferredCapturePass(snapshot.passes, deferredSaveCapturePassCursor, passHeader,
                                           chunkRefs) ||
                chunkRefs == nullptr) {
                return false;
            }
            if (!writeDeferredCapturePassHeader(file, passHeader, *chunkRefs)) {
                return false;
            }
            deferredSaveChunkCursor = 0;
            deferredLoopWriteStage = DeferredLoopWriteStage::CapturePassChunk;
            return true;
        }

        case DeferredLoopWriteStage::CapturePassChunk: {
            CapturePassSlotFileHeader passHeader{};
            const ChunkIdList* chunkRefs = nullptr;
            if (!selectDeferredCapturePass(snapshot.passes, deferredSaveCapturePassCursor, passHeader,
                                           chunkRefs) ||
                chunkRefs == nullptr) {
                return false;
            }
            (void)passHeader;

            if (deferredSaveChunkCursor < chunkRefs->size()) {
                const uint16_t chunkId = (*chunkRefs)[deferredSaveChunkCursor++];
                return writeDeferredCapturePassChunk(file, chunkId);
            }

            ++deferredSaveCapturePassCursor;
            deferredLoopWriteStage = DeferredLoopWriteStage::CapturePassHeader;
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

    switch (deferredUndoWriteStage) {
        case DeferredUndoWriteStage::Header: {
            const uint32_t entryCount = static_cast<uint32_t>(stack.entries.size());
            const uint32_t cursor = static_cast<uint32_t>(stack.cursor);
            const uint32_t nextEntryId = stack.nextEntryId;
            if (!writeRaw(file, &entryCount, sizeof(entryCount))) return false;
            if (!writeRaw(file, &cursor, sizeof(cursor))) return false;
            if (!writeRaw(file, &nextEntryId, sizeof(nextEntryId))) return false;
            deferredSaveUndoEntryCursor = 0;
            deferredUndoWriteStage = DeferredUndoWriteStage::EntryHeader;
            return true;
        }

        case DeferredUndoWriteStage::EntryHeader: {
            if (deferredSaveUndoEntryCursor >= stack.entries.size()) {
                resetDeferredUndoWriteState();
                stackDone = true;
                return true;
            }
            const UndoEntry& entry = stack.entries[deferredSaveUndoEntryCursor];
            if (!writeDeferredUndoEntryHeader(file, entry)) {
                return false;
            }
            deferredUndoWriteStage = DeferredUndoWriteStage::BeforeSnapshotPresence;
            return true;
        }

        case DeferredUndoWriteStage::BeforeSnapshotPresence: {
            const UndoEntry& entry = stack.entries[deferredSaveUndoEntryCursor];
            if (!writeDeferredSnapshotPresence(file, entry.beforeSnapshot)) {
                return false;
            }
            if (entry.beforeSnapshot) {
                resetDeferredLoopWriteState();
                deferredUndoWriteStage = DeferredUndoWriteStage::BeforeSnapshotLoop;
            } else {
                deferredUndoWriteStage = DeferredUndoWriteStage::AfterSnapshotPresence;
            }
            return true;
        }

        case DeferredUndoWriteStage::BeforeSnapshotLoop: {
            const UndoEntry& entry = stack.entries[deferredSaveUndoEntryCursor];
            if (!entry.beforeSnapshot) {
                return false;
            }
            bool snapshotDone = false;
            if (!stepDeferredLoopSnapshotPersist(file, *entry.beforeSnapshot, snapshotDone)) {
                return false;
            }
            if (snapshotDone) {
                deferredUndoWriteStage = DeferredUndoWriteStage::AfterSnapshotPresence;
            }
            return true;
        }

        case DeferredUndoWriteStage::AfterSnapshotPresence: {
            const UndoEntry& entry = stack.entries[deferredSaveUndoEntryCursor];
            if (!writeDeferredSnapshotPresence(file, entry.afterSnapshot)) {
                return false;
            }
            if (entry.afterSnapshot) {
                resetDeferredLoopWriteState();
                deferredUndoWriteStage = DeferredUndoWriteStage::AfterSnapshotLoop;
            } else {
                deferredUndoWriteStage = DeferredUndoWriteStage::EntryTail;
            }
            return true;
        }

        case DeferredUndoWriteStage::AfterSnapshotLoop: {
            const UndoEntry& entry = stack.entries[deferredSaveUndoEntryCursor];
            if (!entry.afterSnapshot) {
                return false;
            }
            bool snapshotDone = false;
            if (!stepDeferredLoopSnapshotPersist(file, *entry.afterSnapshot, snapshotDone)) {
                return false;
            }
            if (snapshotDone) {
                deferredUndoWriteStage = DeferredUndoWriteStage::EntryTail;
            }
            return true;
        }

        case DeferredUndoWriteStage::EntryTail: {
            const UndoEntry& entry = stack.entries[deferredSaveUndoEntryCursor];
            if (!writeDeferredUndoEntryTail(file, entry)) {
                return false;
            }
            ++deferredSaveUndoEntryCursor;
            deferredUndoWriteStage = DeferredUndoWriteStage::EntryHeader;
            return true;
        }
    }

    return false;
}


STORAGE_PERSIST_MEM bool stepDeferredSaveJob() {
    switch (deferredSaveStage) {
        case DeferredSaveStage::CurrentSetMeta:
            switch (deferredGlobalHeaderStage) {
                case DeferredGlobalHeaderStage::Version:
                    deferredGlobalHeaderStage = DeferredGlobalHeaderStage::Bpm;
                    return true;

                case DeferredGlobalHeaderStage::Bpm: {
                    const float savedBpm = bpm;
                    if (!writeRaw(deferredSaveFile, &savedBpm, sizeof(savedBpm))) {
                        Serial.println("[StorageManager] ERROR: Deferred save failed writing BPM");
                        return false;
                    }
                    deferredGlobalHeaderStage = DeferredGlobalHeaderStage::LooperState;
                    return true;
                }

                case DeferredGlobalHeaderStage::LooperState: {
                    const uint32_t looperStateVal =
                        persistedLooperStateRaw(deferredSaveStateSnapshot);
                    if (!writeRaw(deferredSaveFile, &looperStateVal, sizeof(looperStateVal))) {
                        Serial.println("[StorageManager] ERROR: Deferred save failed writing looper state");
                        return false;
                    }
                    deferredGlobalHeaderStage = DeferredGlobalHeaderStage::MasterLoopLength;
                    return true;
                }

                case DeferredGlobalHeaderStage::MasterLoopLength: {
                    const uint32_t masterLoopLength = trackManager.getMasterLoopLength();
                    if (!writeRaw(deferredSaveFile, &masterLoopLength, sizeof(masterLoopLength))) {
                        Serial.println("[StorageManager] ERROR: Deferred save failed writing master loop length");
                        return false;
                    }
                    deferredGlobalHeaderStage = DeferredGlobalHeaderStage::TrackCount;
                    return true;
                }

                case DeferredGlobalHeaderStage::TrackCount:
                    if (!writeRaw(deferredSaveFile, &deferredSaveNumTracks,
                                  sizeof(deferredSaveNumTracks))) {
                        Serial.println("[StorageManager] ERROR: Deferred save failed writing track count");
                        return false;
                    }
                    deferredSaveTrackCursor = 0;
                    deferredSaveSlotCursor = 0;
                    deferredSavePoolCursor = 0;
                    deferredSaveTrackHeaderWritten = false;
                    deferredTrackWriteStage = DeferredTrackWriteStage::TrackState;
                    deferredSlotWriteStage = DeferredSlotWriteStage::SlotEnabled;
                    deferredSaveStage = DeferredSaveStage::TrackHeaderAndSlots;
                    return true;
            }
            return false;

        case DeferredSaveStage::TrackHeaderAndSlots: {
            Track& track = trackManager.getTrack(deferredSaveTrackCursor);
            if (!deferredSaveTrackHeaderWritten) {
                switch (deferredTrackWriteStage) {
                    case DeferredTrackWriteStage::TrackState: {
                        TrackState stateToSave = track.getState();
                        if (stateToSave == TRACK_OVERDUBBING) {
                            stateToSave = TRACK_PLAYING;
                        }
                        const uint32_t trackState = static_cast<uint32_t>(stateToSave);
                        if (!writeRaw(deferredSaveFile, &trackState, sizeof(trackState))) {
                            Serial.print("[StorageManager] ERROR: Deferred save failed writing trackState for track ");
                            Serial.println(deferredSaveTrackCursor);
                            return false;
                        }
                        deferredTrackWriteStage = DeferredTrackWriteStage::Muted;
                        return true;
                    }

                    case DeferredTrackWriteStage::Muted: {
                        const bool muted = track.isMuted();
                        if (!writeRaw(deferredSaveFile, &muted, sizeof(muted))) {
                            Serial.print("[StorageManager] ERROR: Deferred save failed writing muted for track ");
                            Serial.println(deferredSaveTrackCursor);
                            return false;
                        }
                        deferredSaveTrackHeaderWritten = true;
                        deferredTrackWriteStage = DeferredTrackWriteStage::TrackState;
                        deferredSlotWriteStage = DeferredSlotWriteStage::SlotEnabled;
                        return true;
                    }
                }
                return false;
            }

            if (deferredSaveSlotCursor < Config::MAX_LOOPS_PER_TRACK) {
                const uint8_t slot = deferredSaveSlotCursor;
                switch (deferredSlotWriteStage) {
                    case DeferredSlotWriteStage::SlotEnabled: {
                        const bool slotEnabled =
                            trackManager.isSlotEnabled(deferredSaveTrackCursor, slot);
                        if (!writeRaw(deferredSaveFile, &slotEnabled, sizeof(slotEnabled))) {
                            Serial.print("[StorageManager] ERROR: Deferred save failed writing slotEnabled for track ");
                            Serial.print(deferredSaveTrackCursor);
                            Serial.print(" slot ");
                            Serial.println(slot);
                            return false;
                        }
                        deferredSlotWriteStage = DeferredSlotWriteStage::SlotMuted;
                        return true;
                    }

                    case DeferredSlotWriteStage::SlotMuted: {
                        const bool slotMuted =
                            trackManager.isSlotMuted(deferredSaveTrackCursor, slot);
                        if (!writeRaw(deferredSaveFile, &slotMuted, sizeof(slotMuted))) {
                            Serial.print("[StorageManager] ERROR: Deferred save failed writing slotMuted for track ");
                            Serial.print(deferredSaveTrackCursor);
                            Serial.print(" slot ");
                            Serial.println(slot);
                            return false;
                        }
                        deferredSlotWriteStage = DeferredSlotWriteStage::SlotLoopId;
                        return true;
                    }

                    case DeferredSlotWriteStage::SlotLoopId: {
                        const LoopId slotLoopId = track.slotRef(slot).loopId;
                        if (!writeRaw(deferredSaveFile, &slotLoopId, sizeof(slotLoopId))) {
                            Serial.print("[StorageManager] ERROR: Deferred save failed writing slotLoopId for track ");
                            Serial.print(deferredSaveTrackCursor);
                            Serial.print(" slot ");
                            Serial.println(slot);
                            return false;
                        }
                        deferredSaveSlotCursor++;
                        deferredSlotWriteStage = DeferredSlotWriteStage::SlotEnabled;
                        return true;
                    }
                }
                return false;
            }

            if (!trackHasCurrentSetDirtyLoopSlot(deferredSaveTrackCursor)) {
                deferredSaveLoopSlotsSkipped += Config::MAX_LOOPS_PER_TRACK;
                deferredSaveTrackCursor++;
                deferredSaveSlotCursor = 0;
                deferredSavePoolCursor = 0;
                deferredSaveTrackHeaderWritten = false;
                deferredTrackWriteStage = DeferredTrackWriteStage::TrackState;
                deferredSlotWriteStage = DeferredSlotWriteStage::SlotEnabled;
                if (deferredSaveTrackCursor < deferredSaveNumTracks) {
                    return true;
                }
                deferredSaveStage = DeferredSaveStage::Footer;
                deferredFooterWriteStage = DeferredFooterWriteStage::SelectedTrack;
                deferredSaveFooterTrackCursor = 0;
                return true;
            }

            deferredSavePoolCursor = 0;
            resetDeferredLoopWriteState();
            if (!closeDeferredMetaTempForLoopWrites()) {
                return false;
            }
            deferredSaveStage = DeferredSaveStage::CurrentSetLoopSlot;
            return true;
        }

        case DeferredSaveStage::CurrentSetLoopSlot: {
            const uint8_t trackIndex = deferredSaveTrackCursor;
            const uint8_t slotIndex = deferredSavePoolCursor;
            if (!shouldWriteCurrentSetLoopSlot(trackIndex, slotIndex)) {
                ++deferredSaveLoopSlotsSkipped;
                deferredSavePoolCursor++;
                if (deferredSavePoolCursor < Config::MAX_LOOPS_PER_TRACK) {
                    return true;
                }
                deferredSaveTrackCursor++;
                if (deferredSaveTrackCursor < deferredSaveNumTracks) {
                    deferredSaveSlotCursor = 0;
                    deferredSavePoolCursor = 0;
                    resetDeferredLoopWriteState();
                    deferredSaveTrackHeaderWritten = false;
                    deferredTrackWriteStage = DeferredTrackWriteStage::TrackState;
                    deferredSlotWriteStage = DeferredSlotWriteStage::SlotEnabled;
                    if (!reopenDeferredMetaTempForAppend()) {
                        return false;
                    }
                    deferredSaveStage = DeferredSaveStage::TrackHeaderAndSlots;
                    return true;
                }
                if (!reopenDeferredMetaTempForAppend()) {
                    return false;
                }
                deferredSaveStage = DeferredSaveStage::Footer;
                deferredFooterWriteStage = DeferredFooterWriteStage::SelectedTrack;
                deferredSaveFooterTrackCursor = 0;
                return true;
            }

            if (!deferredSaveLoopFileOpen &&
                !openDeferredLoopSlotTemp(trackIndex, slotIndex)) {
                return false;
            }
            Track& track = trackManager.getTrack(deferredSaveTrackCursor);
            bool loopDone = false;
            const bool loopWriteOk = track.loopsAllocated()
                                         ? stepDeferredLoopPersist(
                                               deferredSaveLoopFile,
                                               track.getLoop(deferredSavePoolCursor), loopDone)
                                         : stepDeferredEmptyLoopPersist(
                                               deferredSaveLoopFile,
                                               static_cast<LoopId>(deferredSavePoolCursor),
                                               loopDone);
            if (!loopWriteOk) {
                Serial.print("[StorageManager] ERROR: Deferred save failed writing loop pool entry track ");
                Serial.print(deferredSaveTrackCursor);
                Serial.print(" pool ");
                Serial.println(deferredSavePoolCursor);
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
            ++deferredSaveLoopSlotsWritten;
            clearCurrentSetLoopSlotDirtyInternal(trackIndex, slotIndex);

            deferredSavePoolCursor++;
            if (deferredSavePoolCursor < Config::MAX_LOOPS_PER_TRACK) {
                resetDeferredLoopWriteState();
                return true;
            }

            deferredSaveTrackCursor++;
            if (deferredSaveTrackCursor < deferredSaveNumTracks) {
                deferredSaveSlotCursor = 0;
                deferredSavePoolCursor = 0;
                resetDeferredLoopWriteState();
                deferredSaveTrackHeaderWritten = false;
                deferredTrackWriteStage = DeferredTrackWriteStage::TrackState;
                deferredSlotWriteStage = DeferredSlotWriteStage::SlotEnabled;
                if (!reopenDeferredMetaTempForAppend()) {
                    return false;
                }
                deferredSaveStage = DeferredSaveStage::TrackHeaderAndSlots;
                return true;
            }

            if (!reopenDeferredMetaTempForAppend()) {
                return false;
            }
            deferredSaveStage = DeferredSaveStage::Footer;
            deferredFooterWriteStage = DeferredFooterWriteStage::SelectedTrack;
            deferredSaveFooterTrackCursor = 0;
            return true;
        }

        case DeferredSaveStage::Footer: {
            switch (deferredFooterWriteStage) {
                case DeferredFooterWriteStage::SelectedTrack: {
                    const uint8_t selectedTrackIdx = trackManager.getSelectedTrackIndex();
                    if (!writeRaw(deferredSaveFile, &selectedTrackIdx,
                                  sizeof(selectedTrackIdx))) {
                        Serial.println("[StorageManager] ERROR: Deferred save failed writing selected track index");
                        return false;
                    }
                    deferredSaveFooterTrackCursor = 0;
                    deferredFooterWriteStage = DeferredFooterWriteStage::ActiveLoopIndex;
                    return true;
                }

                case DeferredFooterWriteStage::ActiveLoopIndex:
                    if (deferredSaveFooterTrackCursor < deferredSaveNumTracks) {
                        const uint8_t activeIdx =
                            trackManager.getActiveLoopIndex(deferredSaveFooterTrackCursor);
                        if (!writeRaw(deferredSaveFile, &activeIdx, sizeof(activeIdx))) {
                            Serial.print("[StorageManager] ERROR: Deferred save failed writing activeLoopIndex for track ");
                            Serial.println(deferredSaveFooterTrackCursor);
                            return false;
                        }
                        ++deferredSaveFooterTrackCursor;
                        return true;
                    }
                    deferredFooterWriteStage = DeferredFooterWriteStage::GlobalUndoStackToken;
                    return true;

                case DeferredFooterWriteStage::GlobalUndoStackToken:
                    if (!writeRaw(deferredSaveFile, &kGlobalUndoStackToken,
                                  sizeof(kGlobalUndoStackToken))) {
                        Serial.println("[StorageManager] ERROR: Deferred save failed writing global undo stack token");
                        return false;
                    }
                    deferredSaveUndoTrackCursor = 0;
                    resetDeferredUndoWriteState();
                    deferredSaveStage = DeferredSaveStage::UndoStacks;
                    return true;
            }
            return false;
        }

        case DeferredSaveStage::UndoStacks: {
            if (deferredSaveUndoTrackCursor < deferredSaveNumTracks) {
                const Track& track = trackManager.getTrack(deferredSaveUndoTrackCursor);
                bool stackDone = false;
                if (!stepDeferredUndoStackPersist(deferredSaveFile, track.getGlobalUndoStack(),
                                                  stackDone)) {
                    Serial.print("[StorageManager] ERROR: Deferred save failed writing global undo stack for track ");
                    Serial.println(deferredSaveUndoTrackCursor);
                    return false;
                }
                if (!stackDone) {
                    return true;
                }
                deferredSaveUndoTrackCursor++;
                resetDeferredUndoWriteState();
                return true;
            }

            if (!finalizeDeferredMetaTempFile()) {
                Serial.println("[StorageManager] ERROR: Deferred save failed finalizing CurrentSet meta");
                return false;
            }
            deferredSaveStage = DeferredSaveStage::CurrentSetCompletion;
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
            deferredSaveInProgress = false;
            deferredSaveStage = DeferredSaveStage::Idle;
            return true;
        }

        case DeferredSaveStage::Idle:
        default:
            return true;
    }
}

}  // namespace StorageManagerInternal
