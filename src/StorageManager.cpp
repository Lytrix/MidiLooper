//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "StorageManager.h"
#include "TrackManager.h"
#include "Loop.h"
#include "Slot.h"
#include "StorageLoopIo.h"
#include "Globals.h"
#include "Logger.h"
#include "Utils/DebugSessionCapture.h"
#include "Utils/MemoryMonitor.h"
#include <SD.h>
#include <Arduino.h>
#include "TrackUndo.h"
#include "Utils/MemoryPool.h"
#include "Utils/HotPathTelemetry.h"
#include "Utils/PsramFirstAllocator.h"
#include <array>
#include <cstdio>
#include <vector>

#define STORAGE_FILENAME "/midilooper_state.raw"
#define STORAGE_VERSION 4
static constexpr uint32_t GLOBAL_UNDO_MAGIC = 0x33535547UL;  // "GUS3"
static constexpr uint32_t STORAGE_COMPLETE_MAGIC = 0x45564153UL;  // "SAVE"

// Helper to write raw data
static bool writeRaw(File &file, const void *data, size_t size) {
    return file.write((const uint8_t*)data, size) == size;
}
// Helper to read raw data
static bool readRaw(File &file, void *data, size_t size) {
    // Check if enough bytes remain
    if ((file.size() - file.position()) < size) {
        Serial.print("[StorageManager] readRaw: Not enough bytes left in file. Needed: ");
        Serial.print(size);
        Serial.print(", available: ");
        Serial.println(file.size() - file.position());
        return false;
    }
    int bytesRead = file.read((uint8_t*)data, size);
    if (bytesRead != (int)size) {
        Serial.print("[StorageManager] readRaw: expected ");
        Serial.print(size);
        Serial.print(" bytes, got ");
        Serial.println(bytesRead);
        return false;
    }
    return true;
}

static LooperState sanitizeLoadedLooperState(LooperState state) {
    switch (state) {
        case LOOPER_RECORDING:
        case LOOPER_PLAYING:
        case LOOPER_OVERDUBBING:
            return LOOPER_IDLE;
        default:
            return state;
    }
}

static uint32_t persistedLooperStateRaw(LooperState state) {
    return static_cast<uint32_t>(sanitizeLoadedLooperState(state));
}

static StorageIo storageIoFromFileWrite(File& file) {
    return StorageIo{
        [&file](const void* data, size_t size) { return writeRaw(file, data, size); },
        nullptr,
    };
}

static StorageIo storageIoFromFileRead(File& file) {
    return StorageIo{
        nullptr,
        [&file](void* data, size_t size) { return readRaw(file, data, size); },
    };
}

static bool writeLoopSnapshot(File& file, const LoopSnapshotRef& snapshot) {
    bool hasSnapshot = snapshot != nullptr;
    if (!writeRaw(file, &hasSnapshot, sizeof(hasSnapshot))) return false;
    if (!hasSnapshot) return true;
    const StorageIo io = storageIoFromFileWrite(file);
    return writePersistedLoopSnapshot(io, *snapshot);
}

static bool readLoopSnapshot(File& file, LoopSnapshotRef& snapshot) {
    bool hasSnapshot = false;
    if (!readRaw(file, &hasSnapshot, sizeof(hasSnapshot))) return false;
    if (!hasSnapshot) {
        snapshot.reset();
        return true;
    }
    auto loaded = std::make_shared<PersistedLoopSnapshot>();
    const StorageIo io = storageIoFromFileRead(file);
    if (!readPersistedLoopSnapshot(io, *loaded)) {
        return false;
    }
    snapshot = std::move(loaded);
    return true;
}

static bool writeGlobalUndoStack(File& file, const GlobalUndoStack& stack) {
    uint32_t entryCount = static_cast<uint32_t>(stack.entries.size());
    uint32_t cursor = static_cast<uint32_t>(stack.cursor);
    uint32_t nextEntryId = stack.nextEntryId;
    if (!writeRaw(file, &entryCount, sizeof(entryCount))) return false;
    if (!writeRaw(file, &cursor, sizeof(cursor))) return false;
    if (!writeRaw(file, &nextEntryId, sizeof(nextEntryId))) return false;

    for (const UndoEntry& entry : stack.entries) {
        uint8_t kind = static_cast<uint8_t>(entry.kind);
        if (!writeRaw(file, &entry.id, sizeof(entry.id))) return false;
        if (!writeRaw(file, &kind, sizeof(kind))) return false;
        if (!writeRaw(file, &entry.slotIndex, sizeof(entry.slotIndex))) return false;
        if (!writeRaw(file, &entry.loopId, sizeof(entry.loopId))) return false;
        if (!writeRaw(file, &entry.passId, sizeof(entry.passId))) return false;

        if (!writeLoopSnapshot(file, entry.beforeSnapshot)) return false;
        if (!writeLoopSnapshot(file, entry.afterSnapshot)) return false;

        if (!writeRaw(file, &entry.beforeGeometry, sizeof(entry.beforeGeometry))) return false;
        if (!writeRaw(file, &entry.afterGeometry, sizeof(entry.afterGeometry))) return false;
        if (!writeRaw(file, &entry.beforeLoopStartTick, sizeof(entry.beforeLoopStartTick))) return false;
        if (!writeRaw(file, &entry.beforeLoopLengthTicks, sizeof(entry.beforeLoopLengthTicks))) return false;
        if (!writeRaw(file, &entry.afterLoopStartTick, sizeof(entry.afterLoopStartTick))) return false;
        if (!writeRaw(file, &entry.afterLoopLengthTicks, sizeof(entry.afterLoopLengthTicks))) return false;

        uint32_t beforeTrackState = static_cast<uint32_t>(entry.beforeTrackState);
        uint32_t afterTrackState = static_cast<uint32_t>(entry.afterTrackState);
        if (!writeRaw(file, &beforeTrackState, sizeof(beforeTrackState))) return false;
        if (!writeRaw(file, &afterTrackState, sizeof(afterTrackState))) return false;
        if (!writeRaw(file, &entry.hasTrackState, sizeof(entry.hasTrackState))) return false;
        if (!writeRaw(file, &entry.hasRedoPayload, sizeof(entry.hasRedoPayload))) return false;
    }

    return true;
}

static bool readGlobalUndoStack(File& file, GlobalUndoStack& stack) {
    uint32_t entryCount = 0;
    uint32_t cursor = 0;
    uint32_t nextEntryId = 1;
    if (!readRaw(file, &entryCount, sizeof(entryCount))) return false;
    if (!readRaw(file, &cursor, sizeof(cursor))) return false;
    if (!readRaw(file, &nextEntryId, sizeof(nextEntryId))) return false;

    stack.clear();
    stack.nextEntryId = nextEntryId;
    stack.entries.reserve(entryCount);

    for (uint32_t i = 0; i < entryCount; ++i) {
        UndoEntry entry;
        uint8_t kindRaw = 0;
        uint32_t beforeTrackStateRaw = 0;
        uint32_t afterTrackStateRaw = 0;
        if (!readRaw(file, &entry.id, sizeof(entry.id))) return false;
        if (!readRaw(file, &kindRaw, sizeof(kindRaw))) return false;
        entry.kind = static_cast<UndoEntryKind>(kindRaw);
        if (!readRaw(file, &entry.slotIndex, sizeof(entry.slotIndex))) return false;
        if (!readRaw(file, &entry.loopId, sizeof(entry.loopId))) return false;
        if (!readRaw(file, &entry.passId, sizeof(entry.passId))) return false;

        if (!readLoopSnapshot(file, entry.beforeSnapshot)) return false;
        if (!readLoopSnapshot(file, entry.afterSnapshot)) return false;

        if (!readRaw(file, &entry.beforeGeometry, sizeof(entry.beforeGeometry))) return false;
        if (!readRaw(file, &entry.afterGeometry, sizeof(entry.afterGeometry))) return false;
        if (!readRaw(file, &entry.beforeLoopStartTick, sizeof(entry.beforeLoopStartTick))) return false;
        if (!readRaw(file, &entry.beforeLoopLengthTicks, sizeof(entry.beforeLoopLengthTicks))) return false;
        if (!readRaw(file, &entry.afterLoopStartTick, sizeof(entry.afterLoopStartTick))) return false;
        if (!readRaw(file, &entry.afterLoopLengthTicks, sizeof(entry.afterLoopLengthTicks))) return false;
        if (!readRaw(file, &beforeTrackStateRaw, sizeof(beforeTrackStateRaw))) return false;
        if (!readRaw(file, &afterTrackStateRaw, sizeof(afterTrackStateRaw))) return false;
        if (!readRaw(file, &entry.hasTrackState, sizeof(entry.hasTrackState))) return false;
        if (!readRaw(file, &entry.hasRedoPayload, sizeof(entry.hasRedoPayload))) return false;

        entry.beforeTrackState = static_cast<TrackState>(beforeTrackStateRaw);
        entry.afterTrackState = static_cast<TrackState>(afterTrackStateRaw);
        stack.entries.push_back(std::move(entry));
    }

    stack.cursor = (cursor <= stack.entries.size()) ? cursor : stack.entries.size();
    if (stack.nextEntryId == 0) {
        stack.nextEntryId = 1;
    }
    return true;
}

bool StorageManager::saveState(const LooperState& state) {
#if BYPASS_STOP_UNDO_SAVE
    (void)state;
    Serial.println("[StorageManager] BYPASS_STOP_UNDO_SAVE: skip saveState");
    return true;
#endif
    HotPathTelemetry::ScopedSaveState telemetryScope;
    Serial.println("[StorageManager] Saving state to SD card...");
    File file = SD.open(STORAGE_FILENAME, FILE_WRITE);
    if (!file) {
        Serial.print("[StorageManager] ERROR: Could not open file for writing: ");
        Serial.println(STORAGE_FILENAME);
        return false;
    }
    file.seek(0); // Overwrite

    uint32_t version = STORAGE_VERSION;
    if (!writeRaw(file, &version, sizeof(version))) { Serial.println("[StorageManager] ERROR: Failed to write version"); file.close(); return false; }

    float savedBpm = bpm;
    if (!writeRaw(file, &savedBpm, sizeof(savedBpm))) { Serial.println("[StorageManager] ERROR: Failed to write BPM"); file.close(); return false; }

    // Save looper state
    uint32_t looperStateVal = persistedLooperStateRaw(state);
    if (!writeRaw(file, &looperStateVal, sizeof(looperStateVal))) { Serial.println("[StorageManager] ERROR: Failed to write looper state"); file.close(); return false; }

    // Save master loop length
    uint32_t masterLoopLength = trackManager.getMasterLoopLength();
    if (!writeRaw(file, &masterLoopLength, sizeof(masterLoopLength))) { Serial.println("[StorageManager] ERROR: Failed to write master loop length"); file.close(); return false; }

    // Save all tracks
    uint8_t numTracks = Config::NUM_TRACKS;
    if (!writeRaw(file, &numTracks, sizeof(numTracks))) { Serial.println("[StorageManager] ERROR: Failed to write numTracks"); file.close(); return false; }
    for (uint8_t t = 0; t < numTracks; ++t) {
        Track &track = trackManager.getTrack(t);
        // Track state
        TrackState stateToSave = track.getState();
        // Ensure we save the state as TRACK_PLAYING when still in overdubbing to avoid state machine corruption
        if (stateToSave == TRACK_OVERDUBBING) stateToSave = TRACK_PLAYING;
        uint32_t trackState = (uint32_t)stateToSave;
        if (!writeRaw(file, &trackState, sizeof(trackState))) { Serial.print("[StorageManager] ERROR: Failed to write trackState for track "); Serial.println(t); file.close(); return false; }
        // Muted
        bool muted = track.isMuted();
        if (!writeRaw(file, &muted, sizeof(muted))) { Serial.print("[StorageManager] ERROR: Failed to write muted for track "); Serial.println(t); file.close(); return false; }

        for (uint8_t s = 0; s < Config::MAX_LOOPS_PER_TRACK; ++s) {
            const bool slotEnabled = trackManager.isSlotEnabled(t, s);
            const bool slotMuted = trackManager.isSlotMuted(t, s);
            const LoopId slotLoopId = track.slotRef(s).loopId;

            logger.log(CAT_STORAGE, LOG_DEBUG, "[StorageManager] v4 saving track=%u slot=%u loopId=%lu",
                       t, s, static_cast<unsigned long>(slotLoopId));

            if (!writeRaw(file, &slotEnabled, sizeof(slotEnabled))) { Serial.print("[StorageManager] ERROR: Failed to write slotEnabled for track "); Serial.print(t); Serial.print(" slot "); Serial.println(s); file.close(); return false; }
            if (!writeRaw(file, &slotMuted, sizeof(slotMuted))) { Serial.print("[StorageManager] ERROR: Failed to write slotMuted for track "); Serial.print(t); Serial.print(" slot "); Serial.println(s); file.close(); return false; }
            if (!writeRaw(file, &slotLoopId, sizeof(slotLoopId))) { Serial.print("[StorageManager] ERROR: Failed to write slotLoopId for track "); Serial.print(t); Serial.print(" slot "); Serial.println(s); file.close(); return false; }
        }

        track.ensureLoopsAllocated();
        const StorageIo loopIo = storageIoFromFileWrite(file);
        for (uint8_t p = 0; p < Config::MAX_LOOPS_PER_TRACK; ++p) {
            Loop& loop = track.loopPool_.at(p);
            logger.log(CAT_STORAGE, LOG_DEBUG, "[StorageManager] v4 saving track=%u pool=%u loopId=%lu takes=%u",
                       t, p, static_cast<unsigned long>(loop.loopId),
                       static_cast<unsigned>(loop.passes.capturePassCount()));
            if (!writeLoopPersisted(loopIo, loop)) {
                Serial.print("[StorageManager] ERROR: Failed to write loop pool entry track ");
                Serial.print(t);
                Serial.print(" pool ");
                Serial.println(p);
                file.close();
                return false;
            }
            yield();
        }
    }
    // Save selected track index
    uint8_t selectedTrackIdx = trackManager.getSelectedTrackIndex();
    if (!file.write(&selectedTrackIdx, sizeof(selectedTrackIdx))) {
        Serial.println("[StorageManager] ERROR: Failed to write selected track index");
        file.close();
        return false;
    }

    // Save active loop slot index per track
    for (uint8_t t = 0; t < numTracks; ++t) {
        uint8_t activeIdx = trackManager.getActiveLoopIndex(t);
        if (!file.write(&activeIdx, sizeof(activeIdx))) {
            Serial.println("[StorageManager] ERROR: Failed to write activeLoopIndex for track");
            file.close();
            return false;
        }
    }

    // Optional v3 extension tail: per-track GlobalUndoStack (M3).
    if (!writeRaw(file, &GLOBAL_UNDO_MAGIC, sizeof(GLOBAL_UNDO_MAGIC))) {
        Serial.println("[StorageManager] ERROR: Failed to write global undo magic");
        file.close();
        return false;
    }
    for (uint8_t t = 0; t < numTracks; ++t) {
        const Track& track = trackManager.getTrack(t);
        if (!writeGlobalUndoStack(file, track.getGlobalUndoStack())) {
            Serial.print("[StorageManager] ERROR: Failed to write global undo stack for track ");
            Serial.println(t);
            file.close();
            return false;
        }
    }
    if (!writeRaw(file, &STORAGE_COMPLETE_MAGIC, sizeof(STORAGE_COMPLETE_MAGIC))) {
        Serial.println("[StorageManager] ERROR: Failed to write storage completion marker");
        file.close();
        return false;
    }
    file.close();
    Serial.println("[StorageManager] State saved successfully (v4).");
    telemetryScope.setOk(true);
    return true;
}

namespace {
bool deferredSavePending = false;
bool urgentEditSavePending = false;
uint32_t lastEditAutosaveMs = 0;
bool clearEditDirtyAfterDeferredSave = false;

enum class DeferredSaveStage : uint8_t {
    Idle = 0,
    GlobalHeader,
    TrackHeaderAndSlots,
    LoopPool,
    Footer,
    UndoStacks,
    CompletionMarker,
};

enum class DeferredGlobalHeaderStage : uint8_t {
    Version = 0,
    Bpm,
    LooperState,
    MasterLoopLength,
    TrackCount,
};

enum class DeferredTrackWriteStage : uint8_t {
    TrackState = 0,
    Muted,
};

enum class DeferredSlotWriteStage : uint8_t {
    SlotEnabled = 0,
    SlotMuted,
    SlotLoopId,
};

enum class DeferredFooterWriteStage : uint8_t {
    SelectedTrack = 0,
    ActiveLoopIndex,
    UndoMagic,
};

enum class DeferredLoopWriteStage : uint8_t {
    Header = 0,
    CapturePassHeader,
    CapturePassChunk,
    EditTail,
};

enum class DeferredUndoWriteStage : uint8_t {
    Header = 0,
    EntryHeader,
    BeforeSnapshotPresence,
    BeforeSnapshotLoop,
    AfterSnapshotPresence,
    AfterSnapshotLoop,
    EntryTail,
};

bool deferredSaveInProgress = false;
DeferredSaveStage deferredSaveStage = DeferredSaveStage::Idle;
DeferredGlobalHeaderStage deferredGlobalHeaderStage = DeferredGlobalHeaderStage::Version;
DeferredTrackWriteStage deferredTrackWriteStage = DeferredTrackWriteStage::TrackState;
DeferredSlotWriteStage deferredSlotWriteStage = DeferredSlotWriteStage::SlotEnabled;
DeferredFooterWriteStage deferredFooterWriteStage = DeferredFooterWriteStage::SelectedTrack;
DeferredLoopWriteStage deferredLoopWriteStage = DeferredLoopWriteStage::Header;
DeferredUndoWriteStage deferredUndoWriteStage = DeferredUndoWriteStage::Header;
LooperState deferredSaveStateSnapshot = LOOPER_IDLE;
File deferredSaveFile;
uint8_t deferredSaveNumTracks = 0;
uint8_t deferredSaveTrackCursor = 0;
uint8_t deferredSaveSlotCursor = 0;
uint8_t deferredSavePoolCursor = 0;
uint8_t deferredSaveUndoTrackCursor = 0;
uint16_t deferredSaveCapturePassCursor = 0;
uint16_t deferredSaveChunkCursor = 0;
uint32_t deferredSaveUndoEntryCursor = 0;
bool deferredSaveTrackHeaderWritten = false;
uint8_t deferredSaveFooterTrackCursor = 0;
uint32_t deferredSaveStartedAtUs = 0;
uint32_t deferredSaveHeapBefore = 0;
uint32_t deferredSaveAdmissionHeap = 0;
bool deferredSaveHeapFloorDeferred = false;
std::vector<MidiEvent, PsramFirstAllocator<MidiEvent>> deferredSaveMidiBatch;

bool anyAllocatedLoopEditStateDirty() {
    for (uint8_t t = 0; t < trackManager.getTrackCount(); ++t) {
        Track& track = trackManager.getTrack(t);
        if (!track.loopsAllocated()) {
            continue;
        }
        for (uint8_t s = 0; s < Config::MAX_LOOPS_PER_TRACK; ++s) {
            if (track.getLoop(s).isEditStateDirty()) {
                return true;
            }
        }
    }
    return false;
}

void clearAllocatedLoopEditStateDirty() {
    for (uint8_t t = 0; t < trackManager.getTrackCount(); ++t) {
        Track& track = trackManager.getTrack(t);
        if (!track.loopsAllocated()) {
            continue;
        }
        for (uint8_t s = 0; s < Config::MAX_LOOPS_PER_TRACK; ++s) {
            track.getLoop(s).clearEditStateDirty();
        }
    }
}

static void quarantineStorageFile() {
#if defined(ARDUINO)
    if (!SD.exists(STORAGE_FILENAME)) {
        return;
    }
    char quarantineName[48];
    snprintf(quarantineName, sizeof(quarantineName), "/state.bad.%lu",
             static_cast<unsigned long>(millis()));
    if (SD.rename(STORAGE_FILENAME, quarantineName)) {
        Serial.print("[StorageManager] Quarantined storage file as ");
        Serial.println(quarantineName);
    }
#endif
}

static void stabilizeBootMemoryAfterLoad() {
    uint32_t freeHeap = MemoryMonitor::getFreeHeap();
    if (freeHeap < Config::HEAP_RESERVE_BYTES) {
        Serial.print("[StorageManager] Boot heap below reserve after load (");
        Serial.print(freeHeap);
        Serial.println(" B); clearing undo stacks");
        for (uint8_t t = 0; t < trackManager.getTrackCount(); ++t) {
            trackManager.getTrack(t).getGlobalUndoStack().clear();
        }
        freeHeap = MemoryMonitor::getFreeHeap();
    }

    if (freeHeap < Config::HEAP_RESERVE_BYTES) {
        Serial.print("[StorageManager] WARN: boot heap still low (");
        Serial.print(freeHeap);
        Serial.println(" B); skipping playback prewarm");
        return;
    }

    trackManager.prewarmPlaybackRuntime();
}

void resetTracksAfterFailedLoad() {
    trackManager.setMasterLoopLength(0);
    for (uint8_t t = 0; t < trackManager.getTrackCount(); ++t) {
        Track& track = trackManager.getTrack(t);
        track.ensureLoopsAllocated();
        track.forceSetState(TRACK_EMPTY);
        track.getGlobalUndoStack().clear();
        track.setActiveLoopIndex(0);
        trackManager.setSelectedSlotIndex(t, 0);
        for (uint8_t s = 0; s < Config::MAX_LOOPS_PER_TRACK; ++s) {
            trackManager.setSlotEnabled(t, s, false);
            trackManager.setSlotMuted(t, s, false);
            Loop& loop = track.getLoop(s);
            loop.discardPendingCapturePass();
            loop.discardCapture();
            loop.resetPassTimeline();
            loop.loopId = static_cast<LoopId>(s);
            loop.startLoopTick = 0;
            loop.loopLengthTicks = 0;
            loop.loopStartTick = 0;
            loop.nextPassId_ = 1;
            loop.nextMergeSequence_ = 0;
            loop.lastPublishedPassId_ = kInvalidPassId;
            loop.lastTickInLoop = 0;
            loop.nextEventIndex = 0;
            loop.clearEditStateDirty();
            loop.visualCache.clear();
            loop.capturePreview.clear();
            loop.pendingVisualDelta.clear();
            loop.invalidateCaches();
        }
    }
    trackManager.setSelectedTrack(0);
}

const char* deferredSaveStageName(DeferredSaveStage stage) {
    switch (stage) {
        case DeferredSaveStage::Idle: return "idle";
        case DeferredSaveStage::GlobalHeader: return "global_header";
        case DeferredSaveStage::TrackHeaderAndSlots: return "track_header_slots";
        case DeferredSaveStage::LoopPool: return "loop_pool";
        case DeferredSaveStage::Footer: return "footer";
        case DeferredSaveStage::UndoStacks: return "undo_stacks";
        case DeferredSaveStage::CompletionMarker: return "completion_marker";
    }
    return "unknown";
}

const char* deferredGlobalHeaderStageName(DeferredGlobalHeaderStage stage) {
    switch (stage) {
        case DeferredGlobalHeaderStage::Version: return "version";
        case DeferredGlobalHeaderStage::Bpm: return "bpm";
        case DeferredGlobalHeaderStage::LooperState: return "looper_state";
        case DeferredGlobalHeaderStage::MasterLoopLength: return "master_loop_length";
        case DeferredGlobalHeaderStage::TrackCount: return "track_count";
    }
    return "unknown";
}

const char* deferredTrackWriteStageName(DeferredTrackWriteStage stage) {
    switch (stage) {
        case DeferredTrackWriteStage::TrackState: return "track_state";
        case DeferredTrackWriteStage::Muted: return "muted";
    }
    return "unknown";
}

const char* deferredSlotWriteStageName(DeferredSlotWriteStage stage) {
    switch (stage) {
        case DeferredSlotWriteStage::SlotEnabled: return "slot_enabled";
        case DeferredSlotWriteStage::SlotMuted: return "slot_muted";
        case DeferredSlotWriteStage::SlotLoopId: return "slot_loop_id";
    }
    return "unknown";
}

const char* deferredFooterWriteStageName(DeferredFooterWriteStage stage) {
    switch (stage) {
        case DeferredFooterWriteStage::SelectedTrack: return "selected_track";
        case DeferredFooterWriteStage::ActiveLoopIndex: return "active_loop_index";
        case DeferredFooterWriteStage::UndoMagic: return "undo_magic";
    }
    return "unknown";
}

const char* deferredLoopWriteStageName(DeferredLoopWriteStage stage) {
    switch (stage) {
        case DeferredLoopWriteStage::Header: return "loop_header";
        case DeferredLoopWriteStage::CapturePassHeader: return "capture_pass_header";
        case DeferredLoopWriteStage::CapturePassChunk: return "capture_pass_chunk";
        case DeferredLoopWriteStage::EditTail: return "edit_tail";
    }
    return "unknown";
}

const char* deferredUndoWriteStageName(DeferredUndoWriteStage stage) {
    switch (stage) {
        case DeferredUndoWriteStage::Header: return "undo_header";
        case DeferredUndoWriteStage::EntryHeader: return "entry_header";
        case DeferredUndoWriteStage::BeforeSnapshotPresence: return "before_snapshot_presence";
        case DeferredUndoWriteStage::BeforeSnapshotLoop: return "before_snapshot_loop";
        case DeferredUndoWriteStage::AfterSnapshotPresence: return "after_snapshot_presence";
        case DeferredUndoWriteStage::AfterSnapshotLoop: return "after_snapshot_loop";
        case DeferredUndoWriteStage::EntryTail: return "entry_tail";
    }
    return "unknown";
}

void emitDeferredSaveSliceTelemetry(const char* phase) {
    char outcome[96];
    if (deferredSaveStage == DeferredSaveStage::LoopPool) {
        snprintf(outcome, sizeof(outcome), "%s:%s:t%u:p%u:c%u:k%u", phase,
                 deferredLoopWriteStageName(deferredLoopWriteStage),
                 static_cast<unsigned>(deferredSaveTrackCursor),
                 static_cast<unsigned>(deferredSavePoolCursor),
                 static_cast<unsigned>(deferredSaveCapturePassCursor),
                 static_cast<unsigned>(deferredSaveChunkCursor));
    } else if (deferredSaveStage == DeferredSaveStage::UndoStacks) {
        snprintf(outcome, sizeof(outcome), "%s:%s:t%u:e%lu:%s", phase,
                 deferredSaveStageName(deferredSaveStage),
                 static_cast<unsigned>(deferredSaveUndoTrackCursor),
                 static_cast<unsigned long>(deferredSaveUndoEntryCursor),
                 deferredUndoWriteStageName(deferredUndoWriteStage));
    } else if (deferredSaveStage == DeferredSaveStage::GlobalHeader) {
        snprintf(outcome, sizeof(outcome), "%s:%s:%s", phase,
                 deferredSaveStageName(deferredSaveStage),
                 deferredGlobalHeaderStageName(deferredGlobalHeaderStage));
    } else if (deferredSaveStage == DeferredSaveStage::TrackHeaderAndSlots) {
        snprintf(outcome, sizeof(outcome), "%s:%s:t%u:s%u:%s:%s", phase,
                 deferredSaveStageName(deferredSaveStage),
                 static_cast<unsigned>(deferredSaveTrackCursor),
                 static_cast<unsigned>(deferredSaveSlotCursor),
                 deferredSaveTrackHeaderWritten ? "slot" : "track",
                 deferredSaveTrackHeaderWritten
                     ? deferredSlotWriteStageName(deferredSlotWriteStage)
                     : deferredTrackWriteStageName(deferredTrackWriteStage));
    } else if (deferredSaveStage == DeferredSaveStage::Footer) {
        snprintf(outcome, sizeof(outcome), "%s:%s:t%u:%s", phase,
                 deferredSaveStageName(deferredSaveStage),
                 static_cast<unsigned>(deferredSaveFooterTrackCursor),
                 deferredFooterWriteStageName(deferredFooterWriteStage));
    } else {
        snprintf(outcome, sizeof(outcome), "%s:%s:t%u:s%u:p%u", phase,
                 deferredSaveStageName(deferredSaveStage),
                 static_cast<unsigned>(deferredSaveTrackCursor),
                 static_cast<unsigned>(deferredSaveSlotCursor),
                 static_cast<unsigned>(deferredSavePoolCursor));
    }
    SC_PERSIST("slice", micros() - deferredSaveStartedAtUs, deferredSaveHeapBefore,
               deferredSaveHeapBefore, outcome);
}

void resetDeferredLoopWriteState() {
    deferredLoopWriteStage = DeferredLoopWriteStage::Header;
    deferredSaveCapturePassCursor = 0;
    deferredSaveChunkCursor = 0;
    deferredSaveMidiBatch.clear();
}

void resetDeferredUndoWriteState() {
    deferredUndoWriteStage = DeferredUndoWriteStage::Header;
    deferredSaveUndoEntryCursor = 0;
    resetDeferredLoopWriteState();
}

void resetDeferredSaveJobState() {
    if (deferredSaveFile) {
        deferredSaveFile.close();
    }
    deferredSaveInProgress = false;
    deferredSaveStage = DeferredSaveStage::Idle;
    deferredGlobalHeaderStage = DeferredGlobalHeaderStage::Version;
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
    deferredSaveStateSnapshot = LOOPER_IDLE;
}

bool beginDeferredSaveJob(const LooperState& state) {
    deferredSaveFile = SD.open(STORAGE_FILENAME, FILE_WRITE);
    if (!deferredSaveFile) {
        Serial.print("[StorageManager] ERROR: Could not open file for deferred write: ");
        Serial.println(STORAGE_FILENAME);
        return false;
    }
    deferredSaveFile.seek(0);  // Overwrite

    deferredSaveStateSnapshot = state;
    deferredSaveNumTracks = Config::NUM_TRACKS;
    deferredGlobalHeaderStage = DeferredGlobalHeaderStage::Version;
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
    deferredSaveStage = DeferredSaveStage::GlobalHeader;
    return true;
}

bool selectDeferredCapturePass(const LoopPasses& passes, uint16_t cursor,
                               PersistedCapturePassWire& wire,
                               const ChunkIdList*& chunkRefs) {
    uint16_t overdubIndex = cursor;
    if (passes.hasRecordPass()) {
        if (cursor == 0) {
            wire.id = passes.recordPass.id;
            wire.mergeSequence = 0;
            wire.stateRaw = static_cast<uint8_t>(passes.recordPass.state);
            wire.typeRaw = 0;
            wire.sealedAtTick = passes.recordPass.sealedAtTick;
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
    wire.id = pass.id;
    wire.mergeSequence = pass.mergeSequence;
    wire.stateRaw = static_cast<uint8_t>(pass.state);
    wire.typeRaw = 1;
    wire.sealedAtTick = pass.sealedAtTick;
    chunkRefs = &pass.chunkRefs;
    return true;
}

bool writeDeferredLoopHeader(File& file, LoopId loopId, uint32_t startLoopTick,
                             uint32_t loopLengthTicks, uint32_t loopStartTick,
                             PassId nextPassId, uint32_t nextMergeSequence,
                             PassId lastPublishedPassId, const LoopPasses& passes) {
    if (!writeRaw(file, &loopId, sizeof(loopId))) return false;
    if (!writeRaw(file, &startLoopTick, sizeof(startLoopTick))) return false;
    if (!writeRaw(file, &loopLengthTicks, sizeof(loopLengthTicks))) return false;
    if (!writeRaw(file, &loopStartTick, sizeof(loopStartTick))) return false;
    if (!writeRaw(file, &nextPassId, sizeof(nextPassId))) return false;
    if (!writeRaw(file, &nextMergeSequence, sizeof(nextMergeSequence))) return false;
    if (!writeRaw(file, &lastPublishedPassId, sizeof(lastPublishedPassId))) {
        return false;
    }

    const uint32_t persistedCount = static_cast<uint32_t>(passes.capturePassCount());
    return writeRaw(file, &persistedCount, sizeof(persistedCount));
}

bool writeDeferredCapturePassHeader(File& file, const PersistedCapturePassWire& wire,
                                    const ChunkIdList& chunkRefs) {
    if (!writeRaw(file, &wire.id, sizeof(wire.id))) return false;
    if (!writeRaw(file, &wire.mergeSequence, sizeof(wire.mergeSequence))) return false;
    if (!writeRaw(file, &wire.stateRaw, sizeof(wire.stateRaw))) return false;
    if (!writeRaw(file, &wire.typeRaw, sizeof(wire.typeRaw))) return false;
    if (!writeRaw(file, &wire.sealedAtTick, sizeof(wire.sealedAtTick))) return false;

    const uint32_t midiCount =
        static_cast<uint32_t>(LoopEventStore::countEventsInChunkIds(chunkRefs));
    return writeRaw(file, &midiCount, sizeof(midiCount));
}

bool writeDeferredCapturePassChunk(File& file, uint16_t chunkId) {
    deferredSaveMidiBatch.clear();
    LoopEventStore::appendFlattenedChunkId(chunkId, deferredSaveMidiBatch);
    if (deferredSaveMidiBatch.empty()) {
        return true;
    }
    return writeRaw(file, deferredSaveMidiBatch.data(),
                    deferredSaveMidiBatch.size() * sizeof(MidiEvent));
}

bool stepDeferredLoopPersist(File& file, const Loop& loop, bool& loopDone) {
    loopDone = false;

    switch (deferredLoopWriteStage) {
        case DeferredLoopWriteStage::Header:
            if (!writeDeferredLoopHeader(file, loop.loopId, loop.startLoopTick, loop.loopLengthTicks,
                                         loop.loopStartTick, loop.nextPassId_,
                                         loop.nextMergeSequence_, loop.lastPublishedPassId_,
                                         loop.passes)) {
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

            PersistedCapturePassWire wire{};
            const ChunkIdList* chunkRefs = nullptr;
            if (!selectDeferredCapturePass(loop.passes, deferredSaveCapturePassCursor, wire, chunkRefs) ||
                chunkRefs == nullptr) {
                return false;
            }
            if (!writeDeferredCapturePassHeader(file, wire, *chunkRefs)) {
                return false;
            }
            deferredSaveChunkCursor = 0;
            deferredLoopWriteStage = DeferredLoopWriteStage::CapturePassChunk;
            return true;
        }

        case DeferredLoopWriteStage::CapturePassChunk: {
            PersistedCapturePassWire wire{};
            const ChunkIdList* chunkRefs = nullptr;
            if (!selectDeferredCapturePass(loop.passes, deferredSaveCapturePassCursor, wire, chunkRefs) ||
                chunkRefs == nullptr) {
                return false;
            }
            (void)wire;

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

bool stepDeferredEmptyLoopPersist(File& file, LoopId loopId, bool& loopDone) {
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

bool stepDeferredLoopSnapshotPersist(File& file, const PersistedLoopSnapshot& snapshot,
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

            PersistedCapturePassWire wire{};
            const ChunkIdList* chunkRefs = nullptr;
            if (!selectDeferredCapturePass(snapshot.passes, deferredSaveCapturePassCursor, wire,
                                           chunkRefs) ||
                chunkRefs == nullptr) {
                return false;
            }
            if (!writeDeferredCapturePassHeader(file, wire, *chunkRefs)) {
                return false;
            }
            deferredSaveChunkCursor = 0;
            deferredLoopWriteStage = DeferredLoopWriteStage::CapturePassChunk;
            return true;
        }

        case DeferredLoopWriteStage::CapturePassChunk: {
            PersistedCapturePassWire wire{};
            const ChunkIdList* chunkRefs = nullptr;
            if (!selectDeferredCapturePass(snapshot.passes, deferredSaveCapturePassCursor, wire,
                                           chunkRefs) ||
                chunkRefs == nullptr) {
                return false;
            }
            (void)wire;

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

bool writeDeferredUndoEntryHeader(File& file, const UndoEntry& entry) {
    const uint8_t kind = static_cast<uint8_t>(entry.kind);
    if (!writeRaw(file, &entry.id, sizeof(entry.id))) return false;
    if (!writeRaw(file, &kind, sizeof(kind))) return false;
    if (!writeRaw(file, &entry.slotIndex, sizeof(entry.slotIndex))) return false;
    if (!writeRaw(file, &entry.loopId, sizeof(entry.loopId))) return false;
    return writeRaw(file, &entry.passId, sizeof(entry.passId));
}

bool writeDeferredUndoEntryTail(File& file, const UndoEntry& entry) {
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

bool writeDeferredSnapshotPresence(File& file, const LoopSnapshotRef& snapshot) {
    const bool hasSnapshot = snapshot != nullptr;
    return writeRaw(file, &hasSnapshot, sizeof(hasSnapshot));
}

bool stepDeferredUndoStackPersist(File& file, const GlobalUndoStack& stack, bool& stackDone) {
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

bool stepDeferredSaveJob() {
    switch (deferredSaveStage) {
        case DeferredSaveStage::GlobalHeader:
            switch (deferredGlobalHeaderStage) {
                case DeferredGlobalHeaderStage::Version: {
                    const uint32_t version = STORAGE_VERSION;
                    if (!writeRaw(deferredSaveFile, &version, sizeof(version))) {
                        Serial.println("[StorageManager] ERROR: Deferred save failed writing version");
                        return false;
                    }
                    deferredGlobalHeaderStage = DeferredGlobalHeaderStage::Bpm;
                    return true;
                }

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

            deferredSavePoolCursor = 0;
            resetDeferredLoopWriteState();
            deferredSaveStage = DeferredSaveStage::LoopPool;
            return true;
        }

        case DeferredSaveStage::LoopPool: {
            Track& track = trackManager.getTrack(deferredSaveTrackCursor);
            bool loopDone = false;
            const bool loopWriteOk = track.loopsAllocated()
                                         ? stepDeferredLoopPersist(
                                               deferredSaveFile,
                                               track.getLoop(deferredSavePoolCursor), loopDone)
                                         : stepDeferredEmptyLoopPersist(
                                               deferredSaveFile,
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
                deferredSaveStage = DeferredSaveStage::TrackHeaderAndSlots;
                return true;
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
                    deferredFooterWriteStage = DeferredFooterWriteStage::UndoMagic;
                    return true;

                case DeferredFooterWriteStage::UndoMagic:
                    if (!writeRaw(deferredSaveFile, &GLOBAL_UNDO_MAGIC,
                                  sizeof(GLOBAL_UNDO_MAGIC))) {
                        Serial.println("[StorageManager] ERROR: Deferred save failed writing global undo magic");
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

            deferredSaveStage = DeferredSaveStage::CompletionMarker;
            return true;
        }

        case DeferredSaveStage::CompletionMarker:
            if (!writeRaw(deferredSaveFile, &STORAGE_COMPLETE_MAGIC,
                          sizeof(STORAGE_COMPLETE_MAGIC))) {
                Serial.println("[StorageManager] ERROR: Deferred save failed writing storage completion marker");
                return false;
            }
            deferredSaveFile.close();
            Serial.println("[StorageManager] State saved successfully (v4 deferred slices).");
            deferredSaveInProgress = false;
            deferredSaveStage = DeferredSaveStage::Idle;
            return true;

        case DeferredSaveStage::Idle:
        default:
            return true;
    }
}
}  // namespace

void StorageManager::requestUrgentEditSave() {
#if BYPASS_STOP_UNDO_SAVE
    return;
#endif
    urgentEditSavePending = true;
}

void StorageManager::processEditAutosave(const LooperState& state) {
#if BYPASS_STOP_UNDO_SAVE
    (void)state;
    urgentEditSavePending = false;
    return;
#endif
    const uint32_t nowMs = millis();
    if (urgentEditSavePending) {
        urgentEditSavePending = false;
        clearEditDirtyAfterDeferredSave = true;
        requestDeferredSaveState(state);
        lastEditAutosaveMs = nowMs;
        return;
    }

    bool captureActive = false;
    for (uint8_t t = 0; t < trackManager.getTrackCount(); ++t) {
        Track& track = trackManager.getTrack(t);
        if (track.isRecording() || track.isOverdubbing()) {
            captureActive = true;
        }
    }
    const bool anyDirty = anyAllocatedLoopEditStateDirty();
    if (!anyDirty || captureActive) {
        return;
    }
    if (nowMs - lastEditAutosaveMs < Config::autosaveIntervalMs) {
        return;
    }
    clearEditDirtyAfterDeferredSave = true;
    requestDeferredSaveState(state);
    lastEditAutosaveMs = nowMs;
}

void StorageManager::requestDeferredSaveState(const LooperState& /*state*/, uint32_t admissionHeap) {
#if BYPASS_STOP_UNDO_SAVE
    return;
#endif
    const bool alreadyPending = deferredSavePending;
    if (admissionHeap != UINT32_MAX || deferredSaveAdmissionHeap == 0) {
        deferredSaveAdmissionHeap = admissionHeap;
    }
    const uint32_t reportedHeap = deferredSaveAdmissionHeap == UINT32_MAX
                                      ? 0
                                      : deferredSaveAdmissionHeap;
    deferredSavePending = true;
    SC_PERSIST("request", 0, reportedHeap, reportedHeap,
               alreadyPending ? "already_pending" : "queued");
}

bool StorageManager::isDeferredSaveActive() {
#if BYPASS_STOP_UNDO_SAVE
    return false;
#else
    // Block display only while SD writes are in flight — not while a save is merely queued.
    return deferredSaveInProgress;
#endif
}

void StorageManager::processDeferredSaveState(const LooperState& state) {
#if BYPASS_STOP_UNDO_SAVE
    (void)state;
    deferredSavePending = false;
    resetDeferredSaveJobState();
    return;
#endif
    if (!deferredSavePending && !deferredSaveInProgress) {
        return;
    }

    // Do not start or continue deferred save while capture is active.
    for (uint8_t t = 0; t < trackManager.getTrackCount(); ++t) {
        const Track& track = trackManager.getTrack(t);
        if (track.isRecording() || track.isOverdubbing()) {
            return;
        }
    }

    // Admission uses a caller-provided heap sample when available; do not call getFreeHeap() here.
    // Once dispatch has started, run every slice to completion without re-gating.
    if (!deferredSaveInProgress) {
        if (deferredSaveAdmissionHeap != UINT32_MAX &&
            !LoopEventStore::hasRam2HeadroomForNonCriticalWork(deferredSaveAdmissionHeap)) {
            if (!deferredSaveHeapFloorDeferred) {
                SC_PERSIST("defer", 0, deferredSaveAdmissionHeap, deferredSaveAdmissionHeap,
                           "heap_floor");
                deferredSaveHeapFloorDeferred = true;
            }
            return;
        }
        deferredSaveHeapFloorDeferred = false;
    }

    if (!deferredSaveInProgress && deferredSavePending) {
        const uint32_t dispatchHeap = deferredSaveAdmissionHeap == UINT32_MAX
                                          ? 0
                                          : deferredSaveAdmissionHeap;
        SC_PERSIST("dispatch", 0, dispatchHeap, dispatchHeap, "run");
        deferredSavePending = false;
        deferredSaveStartedAtUs = micros();
        deferredSaveHeapBefore = dispatchHeap;
        deferredSaveInProgress = true;
        if (!beginDeferredSaveJob(state)) {
            const uint32_t saveDurationUs = micros() - deferredSaveStartedAtUs;
            SC_PERSIST("result", saveDurationUs, deferredSaveHeapBefore, deferredSaveHeapBefore,
                       "failed");
            resetDeferredSaveJobState();
            return;
        }
        return;
    }

    if (!deferredSaveInProgress) {
        return;
    }

    emitDeferredSaveSliceTelemetry("start");
    const bool stepOk = stepDeferredSaveJob();
    emitDeferredSaveSliceTelemetry(stepOk ? "done" : "failed");
    if (!stepOk) {
        deferredSaveInProgress = false;
    }
    if (deferredSaveInProgress) {
        return;
    }

    const uint32_t saveDurationUs = micros() - deferredSaveStartedAtUs;
    SC_PERSIST("result", saveDurationUs, deferredSaveHeapBefore, deferredSaveHeapBefore,
               stepOk ? "ok" : "failed");
    if (stepOk && clearEditDirtyAfterDeferredSave) {
        clearAllocatedLoopEditStateDirty();
        clearEditDirtyAfterDeferredSave = false;
    }
    if (!stepOk) {
        resetDeferredSaveJobState();
    }
}

bool StorageManager::loadState(LooperState& state) {
    Serial.println("[StorageManager] Loading state from SD card...");
    File file = SD.open(STORAGE_FILENAME, FILE_READ);
    if (!file) {
        Serial.print("[StorageManager] ERROR: Could not open file for reading: ");
        Serial.println(STORAGE_FILENAME);
        return false;
    }
    // Use temporary variables to avoid corrupting current state if file is bad
    uint32_t version = 0;
    if (!readRaw(file, &version, sizeof(version))) {
        Serial.println("[StorageManager] ERROR: Failed to read version");
        file.close();
        return false;
    }
    Serial.println("[StorageManager] Version read OK");
    if (version != 4) {
        Serial.print("[StorageManager] ERROR: Unsupported legacy storage version. Found: ");
        Serial.println(version);
        file.close();
        return false;
    }

    float savedBpm = 0;
    if (!readRaw(file, &savedBpm, sizeof(savedBpm))) {
        Serial.println("[StorageManager] ERROR: Failed to read BPM");
        file.close();
        return false;
    }
    if (savedBpm >= 20.0f && savedBpm <= 300.0f) {
        bpm = savedBpm;
        Serial.print("[StorageManager] Restored BPM: ");
        Serial.println(savedBpm);
    }

    // Looper state
    uint32_t looperStateVal = 0;
    if (!readRaw(file, &looperStateVal, sizeof(looperStateVal))) {
        Serial.println("[StorageManager] ERROR: Failed to read looper state");
        file.close();
        return false;
    }
    LooperState loadedLooperState = sanitizeLoadedLooperState((LooperState)looperStateVal);

    // Master loop length
    uint32_t masterLoopLength = 0;
    if (!readRaw(file, &masterLoopLength, sizeof(masterLoopLength))) {
        Serial.println("[StorageManager] ERROR: Failed to read master loop length");
        file.close();
        return false;
    }

    // Tracks
    uint8_t numTracks = 0;
    if (!readRaw(file, &numTracks, sizeof(numTracks))) {
        Serial.println("[StorageManager] ERROR: Failed to read numTracks");
        file.close();
        return false;
    }
    if (numTracks != Config::NUM_TRACKS) {
        Serial.print("[StorageManager] ERROR: numTracks mismatch. Found: ");
        Serial.println(numTracks);
        file.close();
        return false;
    }

    std::vector<uint8_t> activeLoopIndex(numTracks, 0);
        uint8_t selectedTrackIdx = 0;
        auto failAfterPartialLoad = [&file]() {
            file.close();
            quarantineStorageFile();
            resetTracksAfterFailedLoad();
            return false;
        };

        for (uint8_t t = 0; t < numTracks; ++t) {
            Track& track = trackManager.getTrack(t);
            track.ensureLoopsAllocated();

            uint32_t trackStateRaw = 0;
            if (!readRaw(file, &trackStateRaw, sizeof(trackStateRaw))) {
                Serial.print("[StorageManager] ERROR: Failed to read trackState for track "); Serial.println(t);
                return failAfterPartialLoad();
            }
            TrackState loadedTrackState = static_cast<TrackState>(trackStateRaw);

            bool muted = false;
            if (!readRaw(file, &muted, sizeof(muted))) {
                Serial.print("[StorageManager] ERROR: Failed to read muted for track "); Serial.println(t);
                return failAfterPartialLoad();
            }

            bool anySlotHasEvents = false;

            for (uint8_t s = 0; s < Config::MAX_LOOPS_PER_TRACK; ++s) {
                bool slotEnabled = false;
                bool slotMuted = false;
                LoopId slotLoopId = kInvalidLoopId;
                if (!readRaw(file, &slotEnabled, sizeof(slotEnabled))) { Serial.print("[StorageManager] ERROR: Failed to read slotEnabled for track "); Serial.print(t); Serial.print(" slot "); Serial.println(s); return failAfterPartialLoad(); }
                if (!readRaw(file, &slotMuted, sizeof(slotMuted))) { Serial.print("[StorageManager] ERROR: Failed to read slotMuted for track "); Serial.print(t); Serial.print(" slot "); Serial.println(s); return failAfterPartialLoad(); }
                if (!readRaw(file, &slotLoopId, sizeof(slotLoopId))) { Serial.print("[StorageManager] ERROR: Failed to read slotLoopId for track "); Serial.print(t); Serial.print(" slot "); Serial.println(s); return failAfterPartialLoad(); }

                if (slotLoopId == kInvalidLoopId || slotLoopId >= Config::MAX_LOOPS_PER_TRACK) {
                    Serial.print("[StorageManager] WARNING: Invalid slotLoopId ");
                    Serial.print(static_cast<unsigned long>(slotLoopId));
                    Serial.print(" for track ");
                    Serial.print(t);
                    Serial.print(" slot ");
                    Serial.print(s);
                    Serial.print(" — repairing to ");
                    Serial.println(s);
                    slotLoopId = static_cast<LoopId>(s);
                }

                trackManager.setSlotEnabled(t, s, slotEnabled);
                trackManager.setSlotMuted(t, s, slotMuted);
                track.slots_[s].loopId = slotLoopId;
            }

            const StorageIo loopIo = storageIoFromFileRead(file);
            for (uint8_t p = 0; p < Config::MAX_LOOPS_PER_TRACK; ++p) {
                Loop& loop = track.loopPool_.at(p);
                if (!readLoopPersisted(loopIo, loop)) {
                    Serial.print("[StorageManager] ERROR: Failed to read loop pool entry track ");
                    Serial.print(t);
                    Serial.print(" pool ");
                    Serial.println(p);
                    return failAfterPartialLoad();
                }
                if (loop.hasPublishedEvents()) {
                    anySlotHasEvents = true;
                }
            }

            if (loadedTrackState == TRACK_RECORDING || loadedTrackState == TRACK_ARMED ||
                loadedTrackState == TRACK_STOPPED_RECORDING || loadedTrackState == TRACK_PLAYING ||
                loadedTrackState == TRACK_OVERDUBBING) {
                loadedTrackState = anySlotHasEvents ? TRACK_STOPPED : TRACK_EMPTY;
            }
            if (loadedTrackState == TRACK_EMPTY && anySlotHasEvents) {
                loadedTrackState = TRACK_STOPPED;
            }

            track.forceSetState(loadedTrackState);
            if (muted != track.isMuted()) track.toggleMuteTrack();
        }

        if (!readRaw(file, &selectedTrackIdx, sizeof(selectedTrackIdx))) {
            Serial.println("[StorageManager] ERROR: Failed to read selectedTrackIdx for v4");
            return failAfterPartialLoad();
        }

        for (uint8_t t = 0; t < numTracks; ++t) {
            if (!readRaw(file, &activeLoopIndex[t], sizeof(activeLoopIndex[t]))) {
                Serial.println("[StorageManager] ERROR: Failed to read activeLoopIndex for v4");
                return failAfterPartialLoad();
            }
        }

        uint32_t undoMagic = 0;
        if (!readRaw(file, &undoMagic, sizeof(undoMagic))) {
            Serial.println("[StorageManager] ERROR: Failed to read global undo magic for v4");
            return failAfterPartialLoad();
        }
        if (undoMagic != GLOBAL_UNDO_MAGIC) {
            Serial.println("[StorageManager] ERROR: Global undo magic mismatch for v4");
            return failAfterPartialLoad();
        }
        for (uint8_t t = 0; t < numTracks; ++t) {
            Track& track = trackManager.getTrack(t);
            if (!readGlobalUndoStack(file, track.getGlobalUndoStack())) {
                Serial.print("[StorageManager] ERROR: Failed to read global undo stack for track ");
                Serial.println(t);
                return failAfterPartialLoad();
            }
        }

        uint32_t completeMagic = 0;
        if (!readRaw(file, &completeMagic, sizeof(completeMagic))) {
            Serial.println("[StorageManager] ERROR: Failed to read storage completion marker for v4");
            return failAfterPartialLoad();
        }
        if (completeMagic != STORAGE_COMPLETE_MAGIC) {
            Serial.println("[StorageManager] ERROR: Storage completion marker mismatch for v4");
            return failAfterPartialLoad();
        }

        file.close();
        Serial.println("[StorageManager] State loaded successfully (v4).");

        state = loadedLooperState;
        trackManager.setMasterLoopLength(masterLoopLength);
        for (uint8_t t = 0; t < numTracks; ++t) {
            trackManager.getTrack(t).setActiveLoopIndex(activeLoopIndex[t]);
            trackManager.setSelectedSlotIndex(t, activeLoopIndex[t]);
        }
        if (selectedTrackIdx < Config::NUM_TRACKS) {
            trackManager.setSelectedTrack(selectedTrackIdx);
        } else {
            trackManager.setSelectedTrack(0);
        }
        stabilizeBootMemoryAfterLoad();
    return true;
}
