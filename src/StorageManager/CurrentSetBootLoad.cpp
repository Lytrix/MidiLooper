//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// Sync current-set boot load: runtime bundle read, manifest scan, metadata hydrate,
// and boot-playback restore queue seeding. Distinct from deferred LoadLoopJob (runtime).

#include "StorageManager.h"
#include "StorageManagerInternal.h"

#include "CurrentSetStorage.h"
#include "CurrentWorkspaceStorage.h"
#include "Globals.h"
#include "Loop.h"
#include "LooperState.h"
#include "PersistenceQueue.h"
#include "SlotLoadSession.h"
#include "StorageLoopIo.h"
#include "TrackManager.h"
#include "Utils/BootLoopSlotRestore.h"
#include "Utils/BootTelemetry.h"
#include "Utils/MemoryMonitor.h"
#include <Arduino.h>
#include <SD.h>
#include <cstdio>
#include <cstring>
#include <vector>

namespace StorageManagerInternal {

void resetLoopSlotForBootManifest(Loop& loop, uint8_t slotIndex) {
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
    loop.lastCommittedPassId_ = kInvalidPassId;
    loop.lastTickInLoop = 0;
    loop.nextEventIndex = 0;
    loop.clearEditStateDirty();
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
        size_t clearedUndoEntries = 0;
        for (uint8_t t = 0; t < trackManager.getTrackCount(); ++t) {
            clearedUndoEntries +=
                trackManager.getTrack(t).getGlobalUndoStack().undoCount();
            trackManager.getTrack(t).getGlobalUndoStack().clear();
        }
        Serial.print("[StorageManager] Boot heap recovery: heap=");
        Serial.print(freeHeap / 1024u);
        Serial.print(" KB reserve=");
        Serial.print(Config::HEAP_RESERVE_BYTES / 1024u);
        Serial.print(" KB clearedUndoEntries=");
        Serial.println(clearedUndoEntries);
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

static STORAGE_PERSIST_MEM bool readLoopSlotMetadataFromCurrentSetFile(File& file,
                                                                       PersistedLoopSnapshot& snapshotOut) {
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
    snapshotOut = PersistedLoopSnapshot{};
    if (readPersistedLoopSnapshotHeader(bounded.io(), snapshotOut, false)) {
        return true;
    }
    if (!file.seek(payloadOffset)) {
        return false;
    }
    BoundedFileIo legacyBounded(file, payloadSize);
    return readPersistedLoopSnapshotHeader(legacyBounded.io(), snapshotOut, true);
}

bool STORAGE_PERSIST_MEM hydrateLoopSlotMetadataFromCurrentSetSd(uint8_t trackIndex, uint8_t slotIndex,
                                                                 Loop& loop) {
    char loopPath[64];
    if (!CurrentSetStorage::formatLoopSlotPath(loopPath, sizeof(loopPath), trackIndex, slotIndex)) {
        return false;
    }
    if (!SD.exists(loopPath) || !CurrentSetStorage::verifySaveFileTokenAtPath(loopPath)) {
        StorageManager::setLoopSlotPayloadOnSdInRam(trackIndex, slotIndex, false);
        return false;
    }
    File loopFile = SD.open(loopPath, FILE_READ);
    if (!loopFile) {
        return false;
    }
    PersistedLoopSnapshot metadata{};
    const bool readOk = readLoopSlotMetadataFromCurrentSetFile(loopFile, metadata);
    loopFile.close();
    if (!readOk) {
        return false;
    }
    applyLoopSlotMetadataToLoop(loop, metadata);
    StorageManager::setLoopSlotPayloadOnSdInRam(trackIndex, slotIndex, true);
    return true;
}

bool STORAGE_PERSIST_MEM loadLoopSlotFromCurrentSetSd(uint8_t trackIndex, uint8_t slotIndex, Track& track,
                                         bool& anySlotHasEventsOut) {
    char loopPath[64];
    if (!CurrentSetStorage::formatLoopSlotPath(loopPath, sizeof(loopPath), trackIndex, slotIndex)) {
        return false;
    }
    Loop& loop = track.getLoop(slotIndex);
    if (!SD.exists(loopPath)) {
        StorageManager::setLoopSlotPayloadOnSdInRam(trackIndex, slotIndex, false);
        resetLoopSlotToEmpty(loop, slotIndex);
        markLoopSlotRestoreAttempted(trackIndex, slotIndex);
        return true;
    }
    if (!CurrentSetStorage::verifySaveFileTokenAtPath(loopPath)) {
        Serial.print("[StorageManager] WARN: loop file incomplete, treating slot as empty ");
        Serial.println(loopPath);
        StorageManager::setLoopSlotPayloadOnSdInRam(trackIndex, slotIndex, false);
        resetLoopSlotToEmpty(loop, slotIndex);
        markLoopSlotRestoreAttempted(trackIndex, slotIndex);
        return true;
    }
    File loopFile = SD.open(loopPath, FILE_READ);
    if (!loopFile) {
        Serial.print("[StorageManager] WARN: could not open loop file, treating slot as empty ");
        Serial.println(loopPath);
        StorageManager::setLoopSlotPayloadOnSdInRam(trackIndex, slotIndex, false);
        resetLoopSlotToEmpty(loop, slotIndex);
        markLoopSlotRestoreAttempted(trackIndex, slotIndex);
        return true;
    }

    // Phase 3: session FSM advances between work units; boot/revision still sync-drain.
    SlotLoadSession session(trackIndex, slotIndex);
    (void)session.advanceAfterPhaseWork();  // Dequeued → Reading
    const bool readOk = readLoopFromCurrentSetFile(loopFile, loop);
    loopFile.close();
    if (!readOk) {
        session.fail();
        Serial.print("[StorageManager] WARN: loop read failed, treating slot as empty ");
        Serial.println(loopPath);
        StorageManager::setLoopSlotPayloadOnSdInRam(trackIndex, slotIndex, false);
        resetLoopSlotToEmpty(loop, slotIndex);
        markLoopSlotRestoreAttempted(trackIndex, slotIndex);
        return true;
    }

    (void)session.advanceAfterPhaseWork();  // Reading → Validating
    // readLoopFromCurrentSetFile already validated wire shape; destination still empty until adopt.
    (void)session.advanceAfterPhaseWork();  // Validating → Committing
    markLoopCommittedChunksPersistedFromSdLoad(loop);
    if (loop.hasCommittedPasses()) {
        anySlotHasEventsOut = true;
    }
    StorageManager::setLoopSlotPayloadOnSdInRam(trackIndex, slotIndex, true);
    (void)session.advanceAfterPhaseWork();  // Committing → Completed
    markLoopSlotRestoreAttempted(trackIndex, slotIndex);
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
        // Boot diagnostics: selected/active vs which slots are enabled for play.
        uint8_t enabledMask = 0;
        for (uint8_t s = 0; s < Config::MAX_LOOPS_PER_TRACK && s < 8; ++s) {
            if (trackManager.isSlotEnabled(t, s)) {
                enabledMask = static_cast<uint8_t>(enabledMask | (1u << s));
            }
        }
        Serial.print("[StorageManager] Boot transport t=");
        Serial.print(t);
        Serial.print(" active=");
        Serial.print(activeSlot);
        Serial.print(" selected=");
        Serial.print(selectedSlot);
        Serial.print(" playing=");
        Serial.print(trackManager.getActiveLoopIndex(t));
        Serial.print(" enabledMask=0x");
        Serial.println(enabledMask, HEX);
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
    StorageManagerInternal::clearPendingLoopSlotRestoresAtBoot();
    StorageManagerInternal::clearLoadLoopJob();
    StorageManagerInternal::resetAllLoopSlotRestoreAttempted();
    StorageManagerInternal::resetBootUndoHydrateState();

    LooperState loadedLooperState = LOOPER_IDLE;
    uint32_t masterLoopLength = 0;
    uint8_t numTracks = 0;
    if (!StorageManagerInternal::readCurrentSetFilePreamble(file, loadedLooperState, masterLoopLength, numTracks)) {
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
        if (!StorageManagerInternal::readCurrentSetTrackSlotMetadata(file, t, track, trackHeaders[t].state, trackHeaders[t].muted)) {
            return false;
        }
    }

    activeLoopIndex.assign(numTracks, 0);
    std::vector<uint8_t> selectedSlotIndex;
    if (!StorageManagerInternal::readCurrentSetFileEpilogue(file, numTracks, activeLoopIndex, selectedSlotIndex, selectedTrackIdx,
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

    // Boot restore pipeline: discover → hydrate metadata → enqueue boot playback set → sort → drain.
    // Boot playback set = isBootPlaybackSlot (per-track union of file selected + file active).
    for (uint8_t t = 0; t < numTracks; ++t) {
        Track& track = trackManager.getTrack(t);
        bool anySlotHasEvents = false;
        for (uint8_t s = 0; s < Config::MAX_LOOPS_PER_TRACK; ++s) {
            StorageManagerInternal::resetLoopSlotForBootManifest(track.getLoop(s), s);
            StorageManager::refreshLoopSlotPayloadOnSdInRam(t, s);
            if (!StorageManager::hasLoopSlotPayloadOnSdInRam(t, s)) {
                continue;
            }
            (void)StorageManagerInternal::hydrateLoopSlotMetadataFromCurrentSetSd(t, s, track.getLoop(s));
            anySlotHasEvents = true;
            if (!isBootPlaybackSlot(t, s, selectedTrackIdx, activeLoopIndex.data(),
                                   activeLoopIndex.size(), selectedSlotIndex.data(),
                                   selectedSlotIndex.size())) {
                continue;
            }
            const uint16_t restorePriority = computeBootRestorePriority(
                t, s, selectedTrackIdx, activeLoopIndex.data(), activeLoopIndex.size(),
                selectedSlotIndex.data(), selectedSlotIndex.size(), Config::NUM_TRACKS,
                Config::MAX_LOOPS_PER_TRACK);
            (void)StorageManagerInternal::appendBootLoopSlotRestore(t, s, restorePriority);
        }
        StorageManagerInternal::applyLoadedTrackStateAfterLoopSlots(track, trackHeaders[t].state, anySlotHasEvents,
                                            trackHeaders[t].muted);
        char trackMilestone[8];
        snprintf(trackMilestone, sizeof(trackMilestone), "t%u", static_cast<unsigned>(t));
        emitBootMilestone("scan", trackMilestone);
    }

    emitBootMilestone("scan", "done");

    StorageManagerInternal::sortPendingLoopSlotRestoreQueue();

    const uint8_t focusActive =
        selectedTrackIdx < activeLoopIndex.size() ? activeLoopIndex[selectedTrackIdx] : 0;
    const uint8_t focusSelected =
        selectedTrackIdx < selectedSlotIndex.size() ? selectedSlotIndex[selectedTrackIdx]
                                                   : focusActive;
    Serial.print("[StorageManager] Boot focus track=");
    Serial.print(selectedTrackIdx);
    Serial.print(" active=");
    Serial.print(focusActive);
    Serial.print(" selected=");
    Serial.println(focusSelected);

    // Boot playback slots only; main advances one LoadLoopJob step per loop() until
    // bootInteractiveReady(), then enqueues remaining SD slots for background fill.
    StorageManagerInternal::DeferredLoopSlotRestore first{};
    if (StorageManagerInternal::peekFirstPendingLoopSlotRestore(first)) {
        Serial.print("[StorageManager] Queuing boot playback loop slot restore ");
        Serial.print(StorageManagerInternal::pendingLoopSlotRestoreCount());
        Serial.print(" pending; first ");
        Serial.print(first.track);
        Serial.print('/');
        Serial.println(first.slot);
    }

    return StorageManagerInternal::applyLoadedTransportFooter(numTracks, activeLoopIndex, selectedSlotIndex, selectedTrackIdx,
                                      state, loadedLooperState, masterLoopLength);
}

bool StorageManager::loadCurrentSetFromDirectory(const char* setDir, LooperState& state) {
    char metaPath[80];
    if (setDir != nullptr && std::strcmp(setDir, CurrentSetStorage::kCurrentSetDir) == 0) {
        const int written = snprintf(metaPath, sizeof(metaPath), "%s",
                                        CurrentSetStorage::kCurrentRuntimeBundlePath);
        if (written <= 0 || static_cast<size_t>(written) >= sizeof(metaPath)) {
            return false;
        }
    } else {
        const int written = snprintf(metaPath, sizeof(metaPath), "%s/%s", setDir,
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
    StorageManagerInternal::setRestoredSetBundlePath(metaPath);
    std::vector<uint8_t> activeLoopIndex;
    uint8_t selectedTrackIdx = 0;
    const bool ok = loadCurrentSetBundleAndActiveLoopSlots(file, setDir, state, activeLoopIndex, selectedTrackIdx);
    if (file) {
        file.close();
    }
    if (!ok && setDir != nullptr && std::strcmp(setDir, CurrentSetStorage::kCurrentSetDir) == 0) {
        Serial.println("[StorageManager] Current runtime bundle unreadable; quarantining for recovery.");
        StorageManagerInternal::quarantineCorruptRuntimeBundleOnSd();
    }
    return ok;
}
