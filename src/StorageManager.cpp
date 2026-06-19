//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "StorageManager.h"
#include "TrackManager.h"
#include "Loop.h"
#include "Slot.h"
#include "StorageLoopIo.h"
#include "Globals.h"
#include "Logger.h"
#include <SD.h>
#include <Arduino.h>
#include "TrackUndo.h"
#include "Utils/MemoryPool.h"
#include "Utils/ExtMemAllocator.h"
#include "Utils/HotPathTelemetry.h"
#include <array>

#define STORAGE_FILENAME "/midilooper_state.raw"
#define STORAGE_VERSION 4
static constexpr uint32_t GLOBAL_UNDO_MAGIC = 0x33535547UL;  // "GUS3"

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

static void migrateLoadedLoopsToTakeTimeline(Track& track) {
    track.ensureLoopsAllocated();
    for (uint8_t s = 0; s < Config::MAX_LOOPS_PER_TRACK; ++s) {
        Loop& loop = track.loopForSlot(s);
        if (loop.hasPublishedEvents()) {
            loop.rebuildVisualCacheFromTakes();
        }
    }
}

static bool writeMidiSnapshot(File& file, const MidiSnapshotRef& snapshot) {
    bool hasSnapshot = snapshot != nullptr;
    if (!writeRaw(file, &hasSnapshot, sizeof(hasSnapshot))) return false;
    if (!hasSnapshot) return true;

    MidiEventVec flat;
    snapshot->flatten(flat);
    uint32_t count = static_cast<uint32_t>(flat.size());
    if (!writeRaw(file, &count, sizeof(count))) return false;
    if (count > 0 && !writeRaw(file, flat.data(), count * sizeof(MidiEvent))) return false;
    return true;
}

static bool skipRawBytes(File& file, size_t size) {
    if (size == 0) {
        return true;
    }
    if ((file.size() - file.position()) < size) {
        return false;
    }
    return file.seek(file.position() + size);
}

static bool loadCommittedEventsFromFile(File& file, LoopEventStore& store, uint32_t midiCount) {
    store.clear();
    for (uint32_t i = 0; i < midiCount; ++i) {
        MidiEvent evt;
        if (!readRaw(file, &evt, sizeof(evt))) {
            return false;
        }
        if (!store.append(evt)) {
            Serial.println("[StorageManager] ERROR: chunk pool full while loading committed events");
            return false;
        }
    }
    return true;
}

static uint32_t lastEventTickInStore(const LoopEventStore& store) {
    uint32_t lastTick = 0;
    const size_t count = store.size();
    for (size_t i = 0; i < count; ++i) {
        const uint32_t tick = store.at(i).tick;
        if (tick > lastTick) {
            lastTick = tick;
        }
    }
    return lastTick;
}

static bool readMidiSnapshot(File& file, MidiSnapshotRef& snapshot) {
    bool hasSnapshot = false;
    if (!readRaw(file, &hasSnapshot, sizeof(hasSnapshot))) return false;
    if (!hasSnapshot) {
        snapshot.reset();
        return true;
    }

    uint32_t count = 0;
    if (!readRaw(file, &count, sizeof(count))) return false;
    const size_t bytesNeeded = static_cast<size_t>(count) * sizeof(MidiEvent);
    if ((file.size() - file.position()) < bytesNeeded) return false;

    auto store = std::make_shared<LoopEventStore>();
    if (!loadCommittedEventsFromFile(file, *store, count)) {
        return false;
    }
    snapshot = store;
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
        if (!writeRaw(file, &entry.takeId, sizeof(entry.takeId))) return false;

        if (!writeMidiSnapshot(file, entry.beforeSnapshot)) return false;
        if (!writeMidiSnapshot(file, entry.afterSnapshot)) return false;

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
        if (!readRaw(file, &entry.takeId, sizeof(entry.takeId))) return false;

        if (!readMidiSnapshot(file, entry.beforeSnapshot)) return false;
        if (!readMidiSnapshot(file, entry.afterSnapshot)) return false;

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
    uint32_t looperStateVal = (uint32_t)state;
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
                       static_cast<unsigned>(loop.takes.size()));
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
    file.close();
    Serial.println("[StorageManager] State saved successfully (v4).");
    telemetryScope.setOk(true);
    return true;
}

namespace {
bool deferredSavePending = false;
bool urgentEditSavePending = false;
uint32_t lastEditAutosaveMs = 0;
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
        for (uint8_t t = 0; t < trackManager.getTrackCount(); ++t) {
            Track& track = trackManager.getTrack(t);
            for (uint8_t s = 0; s < Config::MAX_LOOPS_PER_TRACK; ++s) {
                track.getLoop(s).clearEditStateDirty();
            }
        }
        saveState(state);
        lastEditAutosaveMs = nowMs;
        return;
    }

    bool anyDirty = false;
    bool captureActive = false;
    for (uint8_t t = 0; t < trackManager.getTrackCount(); ++t) {
        Track& track = trackManager.getTrack(t);
        if (track.isRecording() || track.isOverdubbing()) {
            captureActive = true;
        }
        for (uint8_t s = 0; s < Config::MAX_LOOPS_PER_TRACK; ++s) {
            if (track.getLoop(s).isEditStateDirty()) {
                anyDirty = true;
            }
        }
    }
    if (!anyDirty || captureActive) {
        return;
    }
    if (nowMs - lastEditAutosaveMs < Config::autosaveIntervalMs) {
        return;
    }
    for (uint8_t t = 0; t < trackManager.getTrackCount(); ++t) {
        Track& track = trackManager.getTrack(t);
        for (uint8_t s = 0; s < Config::MAX_LOOPS_PER_TRACK; ++s) {
            track.getLoop(s).clearEditStateDirty();
        }
    }
    saveState(state);
    lastEditAutosaveMs = nowMs;
}

void StorageManager::requestDeferredSaveState(const LooperState& /*state*/) {
#if BYPASS_STOP_UNDO_SAVE
    return;
#endif
    deferredSavePending = true;
}

void StorageManager::processDeferredSaveState(const LooperState& state) {
#if BYPASS_STOP_UNDO_SAVE
    (void)state;
    deferredSavePending = false;
    return;
#endif
    if (!deferredSavePending) {
        return;
    }
    deferredSavePending = false;
    saveState(state);
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
    if (version != 1 && version != 2 && version != 3 && version != 4) {
        Serial.print("[StorageManager] ERROR: Version mismatch. Found: ");
        Serial.println(version);
        file.close();
        return false;
    }

    if (version >= 2) {
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
    }

    // Looper state
    uint32_t looperStateVal = 0;
    if (!readRaw(file, &looperStateVal, sizeof(looperStateVal))) {
        Serial.println("[StorageManager] ERROR: Failed to read looper state");
        file.close();
        return false;
    }
    LooperState loadedLooperState = (LooperState)looperStateVal;

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

    // Prepare temporary storage for all track data
    struct TrackLoadData {
        TrackState state;
        bool muted;
        uint32_t startLoopTick;
        uint32_t loopLengthTicks;
        MidiEventVec midiEvents;
        std::vector<MidiEventVec, ExtMemAllocator<MidiEventVec>> midiHistory;
    };
    if (version == 4) {
        std::vector<uint8_t> activeLoopIndex(numTracks, 0);
        uint8_t selectedTrackIdx = 0;

        for (uint8_t t = 0; t < numTracks; ++t) {
            Track& track = trackManager.getTrack(t);
            track.ensureLoopsAllocated();

            uint32_t trackStateRaw = 0;
            if (!readRaw(file, &trackStateRaw, sizeof(trackStateRaw))) {
                Serial.print("[StorageManager] ERROR: Failed to read trackState for track "); Serial.println(t);
                file.close();
                return false;
            }
            TrackState loadedTrackState = static_cast<TrackState>(trackStateRaw);

            bool muted = false;
            if (!readRaw(file, &muted, sizeof(muted))) {
                Serial.print("[StorageManager] ERROR: Failed to read muted for track "); Serial.println(t);
                file.close();
                return false;
            }

            bool anySlotHasEvents = false;

            for (uint8_t s = 0; s < Config::MAX_LOOPS_PER_TRACK; ++s) {
                bool slotEnabled = false;
                bool slotMuted = false;
                LoopId slotLoopId = kInvalidLoopId;
                if (!readRaw(file, &slotEnabled, sizeof(slotEnabled))) { Serial.print("[StorageManager] ERROR: Failed to read slotEnabled for track "); Serial.print(t); Serial.print(" slot "); Serial.println(s); file.close(); return false; }
                if (!readRaw(file, &slotMuted, sizeof(slotMuted))) { Serial.print("[StorageManager] ERROR: Failed to read slotMuted for track "); Serial.print(t); Serial.print(" slot "); Serial.println(s); file.close(); return false; }
                if (!readRaw(file, &slotLoopId, sizeof(slotLoopId))) { Serial.print("[StorageManager] ERROR: Failed to read slotLoopId for track "); Serial.print(t); Serial.print(" slot "); Serial.println(s); file.close(); return false; }

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
                    file.close();
                    return false;
                }
                if (loop.hasPublishedEvents()) {
                    anySlotHasEvents = true;
                }
            }

            if (loadedTrackState == TRACK_RECORDING || loadedTrackState == TRACK_ARMED || loadedTrackState == TRACK_STOPPED_RECORDING) {
                loadedTrackState = anySlotHasEvents ? TRACK_STOPPED : TRACK_EMPTY;
            }
            if (loadedTrackState == TRACK_OVERDUBBING) loadedTrackState = TRACK_PLAYING;
            if (loadedTrackState == TRACK_EMPTY && anySlotHasEvents) {
                loadedTrackState = TRACK_STOPPED;
            }

            track.forceSetState(loadedTrackState);
            if (muted != track.isMuted()) track.toggleMuteTrack();
        }

        if (!readRaw(file, &selectedTrackIdx, sizeof(selectedTrackIdx))) {
            Serial.println("[StorageManager] ERROR: Failed to read selectedTrackIdx for v4");
            file.close();
            return false;
        }

        for (uint8_t t = 0; t < numTracks; ++t) {
            if (!readRaw(file, &activeLoopIndex[t], sizeof(activeLoopIndex[t]))) {
                Serial.println("[StorageManager] ERROR: Failed to read activeLoopIndex for v4");
                file.close();
                return false;
            }
        }

        uint32_t undoMagic = 0;
        if (!readRaw(file, &undoMagic, sizeof(undoMagic))) {
            Serial.println("[StorageManager] ERROR: Failed to read global undo magic for v4");
            file.close();
            return false;
        }
        if (undoMagic != GLOBAL_UNDO_MAGIC) {
            Serial.println("[StorageManager] ERROR: Global undo magic mismatch for v4");
            file.close();
            return false;
        }
        for (uint8_t t = 0; t < numTracks; ++t) {
            Track& track = trackManager.getTrack(t);
            if (!readGlobalUndoStack(file, track.getGlobalUndoStack())) {
                Serial.print("[StorageManager] ERROR: Failed to read global undo stack for track ");
                Serial.println(t);
                file.close();
                return false;
            }
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
        return true;
    }

    if (version == 3) {
        // v3: load all loop slots per track + per-slot enable/mute + undo/redo stacks.
        std::vector<uint8_t> activeLoopIndex(numTracks, 0);
        uint8_t selectedTrackIdx = 0;

        for (uint8_t t = 0; t < numTracks; ++t) {
            Track &track = trackManager.getTrack(t);

            // Track state
            uint32_t trackStateRaw = 0;
            if (!readRaw(file, &trackStateRaw, sizeof(trackStateRaw))) {
                Serial.print("[StorageManager] ERROR: Failed to read trackState for track "); Serial.println(t);
                file.close();
                return false;
            }
            TrackState loadedTrackState = (TrackState)trackStateRaw;

            // Muted
            bool muted = false;
            if (!readRaw(file, &muted, sizeof(muted))) {
                Serial.print("[StorageManager] ERROR: Failed to read muted for track "); Serial.println(t);
                file.close();
                return false;
            }

            bool anySlotHasEvents = false;

            for (uint8_t s = 0; s < Config::MAX_LOOPS_PER_TRACK; ++s) {
                bool slotEnabled = false;
                bool slotMuted = false;
                if (!readRaw(file, &slotEnabled, sizeof(slotEnabled))) { Serial.print("[StorageManager] ERROR: Failed to read slotEnabled for track "); Serial.print(t); Serial.print(" slot "); Serial.println(s); file.close(); return false; }
                if (!readRaw(file, &slotMuted, sizeof(slotMuted))) { Serial.print("[StorageManager] ERROR: Failed to read slotMuted for track "); Serial.print(t); Serial.print(" slot "); Serial.println(s); file.close(); return false; }
                trackManager.setSlotEnabled(t, s, slotEnabled);
                trackManager.setSlotMuted(t, s, slotMuted);

                Loop& loop = track.getLoop(s);

                loop.startLoopTick = 0;
                loop.loopLengthTicks = 0;
                loop.loopStartTick = 0;
                loop.lastTickInLoop = 0;
                loop.nextEventIndex = 0;
                loop.playbackOrderDirty = true;
                loop.resetTakeTimeline();
                loop.markDisplayCachesStale();

                // Loop core
                uint32_t storedStartLoopTick = 0;
                uint32_t loopLengthTicks = 0;
                uint32_t storedLoopStartTick = 0;
                if (!readRaw(file, &storedStartLoopTick, sizeof(storedStartLoopTick))) { Serial.print("[StorageManager] ERROR: Failed to read startLoopTick for track "); Serial.print(t); Serial.print(" slot "); Serial.println(s); file.close(); return false; }
                if (!readRaw(file, &loopLengthTicks, sizeof(loopLengthTicks))) { Serial.print("[StorageManager] ERROR: Failed to read loopLengthTicks for track "); Serial.print(t); Serial.print(" slot "); Serial.println(s); file.close(); return false; }
                if (!readRaw(file, &storedLoopStartTick, sizeof(storedLoopStartTick))) { Serial.print("[StorageManager] ERROR: Failed to read loopStartTick for track "); Serial.print(t); Serial.print(" slot "); Serial.println(s); file.close(); return false; }
                (void)storedStartLoopTick; // normalized to 0
                // Recover invalid wrapped lengths caused by previous underflowed recording stop.
                if (loopLengthTicks >= 0x80000000u) loopLengthTicks = 0;
                loop.loopLengthTicks = loopLengthTicks;
                loop.loopStartTick = storedLoopStartTick;

                // MidiEvents
                uint32_t midiCount = 0;
                if (!readRaw(file, &midiCount, sizeof(midiCount))) { Serial.print("[StorageManager] ERROR: Failed to read midiCount for track "); Serial.print(t); Serial.print(" slot "); Serial.println(s); file.close(); return false; }
                size_t midiBytesNeeded = midiCount * sizeof(MidiEvent);
                if ((file.size() - file.position()) < midiBytesNeeded) {
                    Serial.print("[StorageManager] ERROR: Not enough bytes for midiEvents (v3). Track "); Serial.print(t); Serial.print(" slot "); Serial.print(s); Serial.print(". Expected ");
                    Serial.print(midiBytesNeeded); Serial.print(" but remaining ");
                    Serial.println(file.size() - file.position());
                    file.close();
                    return false;
                }
                LoopEventStore loadedStore;
                if (!loadCommittedEventsFromFile(file, loadedStore, midiCount)) {
                    Serial.print("[StorageManager] ERROR: Failed to load committed events for track "); Serial.print(t); Serial.print(" slot "); Serial.println(s);
                    file.close();
                    return false;
                }
                loop.importPublishedStore(loadedStore);
                if (loop.loopLengthTicks == 0 && loop.hasPublishedEvents()) {
                    const uint32_t lastTick =
                        lastEventTickInStore(loop.readEditStore());
                    loop.loopLengthTicks = track.computeLoopLengthTicks(lastTick);
                }
                if (loop.hasPublishedEvents()) anySlotHasEvents = true;
                loop.markDisplayCachesStale();

                // Legacy per-slot undo snapshots are skipped on load — GlobalUndoStack tail
                // (GUS3) holds authoritative undo after M3. Skipping avoids 2×–3× RAM peak.

                // Overdub undo history (midi + geom)
                uint32_t overdubUndoCount = 0;
                if (!readRaw(file, &overdubUndoCount, sizeof(overdubUndoCount))) { Serial.print("[StorageManager] ERROR: Failed to read overdubUndoCount for track "); Serial.print(t); Serial.print(" slot "); Serial.println(s); file.close(); return false; }
                for (uint32_t u = 0; u < overdubUndoCount; ++u) {
                    uint32_t snapCount = 0;
                    if (!readRaw(file, &snapCount, sizeof(snapCount))) { Serial.print("[StorageManager] ERROR: Failed to read overdubUndo snapCount for track "); Serial.print(t); Serial.print(" slot "); Serial.println(s); file.close(); return false; }
                    size_t snapBytesNeeded = snapCount * sizeof(MidiEvent);
                    if ((file.size() - file.position()) < snapBytesNeeded) {
                        Serial.println("[StorageManager] ERROR: Not enough bytes for overdubUndo snapshot (v3)");
                        file.close();
                        return false;
                    }
                    if (!skipRawBytes(file, snapBytesNeeded)) { Serial.print("[StorageManager] ERROR: Failed to skip overdubUndo snapshot for track "); Serial.print(t); Serial.print(" slot "); Serial.println(s); file.close(); return false; }
                    uint32_t geomLoopLengthTicks = 0;
                    uint32_t geomStartLoopTick = 0;
                    uint32_t geomLoopStartTick = 0;
                    if (!readRaw(file, &geomLoopLengthTicks, sizeof(geomLoopLengthTicks))) { Serial.println("[StorageManager] ERROR: Failed to read overdubUndo geom.loopLengthTicks"); file.close(); return false; }
                    if (!readRaw(file, &geomStartLoopTick, sizeof(geomStartLoopTick))) { Serial.println("[StorageManager] ERROR: Failed to read overdubUndo geom.startLoopTick"); file.close(); return false; }
                    if (!readRaw(file, &geomLoopStartTick, sizeof(geomLoopStartTick))) { Serial.println("[StorageManager] ERROR: Failed to read overdubUndo geom.loopStartTick"); file.close(); return false; }
                    (void)geomLoopLengthTicks;
                    (void)geomStartLoopTick;
                    (void)geomLoopStartTick;
                }

                // Overdub redo history (midi + geom)
                uint32_t overdubRedoCount = 0;
                if (!readRaw(file, &overdubRedoCount, sizeof(overdubRedoCount))) { Serial.print("[StorageManager] ERROR: Failed to read overdubRedoCount for track "); Serial.print(t); Serial.print(" slot "); Serial.println(s); file.close(); return false; }
                for (uint32_t u = 0; u < overdubRedoCount; ++u) {
                    uint32_t snapCount = 0;
                    if (!readRaw(file, &snapCount, sizeof(snapCount))) { Serial.print("[StorageManager] ERROR: Failed to read overdubRedo snapCount for track "); Serial.print(t); Serial.print(" slot "); Serial.println(s); file.close(); return false; }
                    size_t snapBytesNeeded = snapCount * sizeof(MidiEvent);
                    if ((file.size() - file.position()) < snapBytesNeeded) {
                        Serial.println("[StorageManager] ERROR: Not enough bytes for overdubRedo snapshot (v3)");
                        file.close();
                        return false;
                    }
                    if (!skipRawBytes(file, snapBytesNeeded)) { Serial.print("[StorageManager] ERROR: Failed to skip overdubRedo snapshot for track "); Serial.print(t); Serial.print(" slot "); Serial.println(s); file.close(); return false; }
                    uint32_t geomLoopLengthTicks = 0;
                    uint32_t geomStartLoopTick = 0;
                    uint32_t geomLoopStartTick = 0;
                    if (!readRaw(file, &geomLoopLengthTicks, sizeof(geomLoopLengthTicks))) { Serial.println("[StorageManager] ERROR: Failed to read overdubRedo geom.loopLengthTicks"); file.close(); return false; }
                    if (!readRaw(file, &geomStartLoopTick, sizeof(geomStartLoopTick))) { Serial.println("[StorageManager] ERROR: Failed to read overdubRedo geom.startLoopTick"); file.close(); return false; }
                    if (!readRaw(file, &geomLoopStartTick, sizeof(geomLoopStartTick))) { Serial.println("[StorageManager] ERROR: Failed to read overdubRedo geom.loopStartTick"); file.close(); return false; }
                    (void)geomLoopLengthTicks;
                    (void)geomStartLoopTick;
                    (void)geomLoopStartTick;
                }

                // Clear undo history
                uint32_t clearUndoCount = 0;
                if (!readRaw(file, &clearUndoCount, sizeof(clearUndoCount))) { Serial.print("[StorageManager] ERROR: Failed to read clearUndoCount for track "); Serial.print(t); Serial.print(" slot "); Serial.println(s); file.close(); return false; }
                for (uint32_t u = 0; u < clearUndoCount; ++u) {
                    uint32_t snapCount = 0;
                    if (!readRaw(file, &snapCount, sizeof(snapCount))) { Serial.print("[StorageManager] ERROR: Failed to read clearUndo snapCount for track "); Serial.print(t); Serial.print(" slot "); Serial.println(s); file.close(); return false; }
                    size_t snapBytesNeeded = snapCount * sizeof(MidiEvent);
                    if ((file.size() - file.position()) < snapBytesNeeded) {
                        Serial.println("[StorageManager] ERROR: Not enough bytes for clearUndo snapshot (v3)");
                        file.close();
                        return false;
                    }
                    if (!skipRawBytes(file, snapBytesNeeded)) { Serial.print("[StorageManager] ERROR: Failed to skip clearUndo snapshot for track "); Serial.print(t); Serial.print(" slot "); Serial.println(s); file.close(); return false; }
                    uint32_t snapStateRaw = 0;
                    uint32_t snapLoopLengthTicks = 0;
                    uint32_t snapLoopStartTick = 0;
                    if (!readRaw(file, &snapStateRaw, sizeof(snapStateRaw))) { Serial.println("[StorageManager] ERROR: Failed to read clearUndo snapState"); file.close(); return false; }
                    if (!readRaw(file, &snapLoopLengthTicks, sizeof(snapLoopLengthTicks))) { Serial.println("[StorageManager] ERROR: Failed to read clearUndo snapLoopLengthTicks"); file.close(); return false; }
                    if (!readRaw(file, &snapLoopStartTick, sizeof(snapLoopStartTick))) { Serial.println("[StorageManager] ERROR: Failed to read clearUndo snapLoopStartTick"); file.close(); return false; }
                    (void)snapStateRaw;
                    (void)snapLoopLengthTicks;
                    (void)snapLoopStartTick;
                }

                // Clear redo history
                uint32_t clearRedoCount = 0;
                if (!readRaw(file, &clearRedoCount, sizeof(clearRedoCount))) { Serial.print("[StorageManager] ERROR: Failed to read clearRedoCount for track "); Serial.print(t); Serial.print(" slot "); Serial.println(s); file.close(); return false; }
                for (uint32_t u = 0; u < clearRedoCount; ++u) {
                    uint32_t snapCount = 0;
                    if (!readRaw(file, &snapCount, sizeof(snapCount))) { Serial.print("[StorageManager] ERROR: Failed to read clearRedo snapCount for track "); Serial.print(t); Serial.print(" slot "); Serial.println(s); file.close(); return false; }
                    size_t snapBytesNeeded = snapCount * sizeof(MidiEvent);
                    if ((file.size() - file.position()) < snapBytesNeeded) {
                        Serial.println("[StorageManager] ERROR: Not enough bytes for clearRedo snapshot (v3)");
                        file.close();
                        return false;
                    }
                    if (!skipRawBytes(file, snapBytesNeeded)) { Serial.print("[StorageManager] ERROR: Failed to skip clearRedo snapshot for track "); Serial.print(t); Serial.print(" slot "); Serial.println(s); file.close(); return false; }
                    uint32_t snapStateRaw = 0;
                    uint32_t snapLoopLengthTicks = 0;
                    uint32_t snapLoopStartTick = 0;
                    if (!readRaw(file, &snapStateRaw, sizeof(snapStateRaw))) { Serial.println("[StorageManager] ERROR: Failed to read clearRedo snapState"); file.close(); return false; }
                    if (!readRaw(file, &snapLoopLengthTicks, sizeof(snapLoopLengthTicks))) { Serial.println("[StorageManager] ERROR: Failed to read clearRedo snapLoopLengthTicks"); file.close(); return false; }
                    if (!readRaw(file, &snapLoopStartTick, sizeof(snapLoopStartTick))) { Serial.println("[StorageManager] ERROR: Failed to read clearRedo snapLoopStartTick"); file.close(); return false; }
                    (void)snapStateRaw;
                    (void)snapLoopLengthTicks;
                    (void)snapLoopStartTick;
                }

                // Loop-start edit undo/redo (legacy — skipped; GlobalUndoStack is authoritative)
                uint32_t loopStartUndoCount = 0;
                if (!readRaw(file, &loopStartUndoCount, sizeof(loopStartUndoCount))) { Serial.print("[StorageManager] ERROR: Failed to read loopStartUndoCount for track "); Serial.print(t); Serial.print(" slot "); Serial.println(s); file.close(); return false; }
                for (uint32_t u = 0; u < loopStartUndoCount; ++u) {
                    uint32_t tick = 0;
                    if (!readRaw(file, &tick, sizeof(tick))) { Serial.println("[StorageManager] ERROR: Failed to read loopStartUndo tick"); file.close(); return false; }
                    (void)tick;
                }
                uint32_t loopStartRedoCount = 0;
                if (!readRaw(file, &loopStartRedoCount, sizeof(loopStartRedoCount))) { Serial.print("[StorageManager] ERROR: Failed to read loopStartRedoCount for track "); Serial.print(t); Serial.print(" slot "); Serial.println(s); file.close(); return false; }
                for (uint32_t u = 0; u < loopStartRedoCount; ++u) {
                    uint32_t tick = 0;
                    if (!readRaw(file, &tick, sizeof(tick))) { Serial.println("[StorageManager] ERROR: Failed to read loopStartRedo tick"); file.close(); return false; }
                    (void)tick;
                }
            }

            // Never resume volatile capture states after reboot.
            if (loadedTrackState == TRACK_RECORDING || loadedTrackState == TRACK_ARMED || loadedTrackState == TRACK_STOPPED_RECORDING) {
                loadedTrackState = anySlotHasEvents ? TRACK_STOPPED : TRACK_EMPTY;
            }
            if (loadedTrackState == TRACK_OVERDUBBING) loadedTrackState = TRACK_PLAYING;
            // SD / bug history can leave EMPTY while slots still contain notes — unusable until reconciled.
            if (loadedTrackState == TRACK_EMPTY && anySlotHasEvents) {
                loadedTrackState = TRACK_STOPPED;
            }

            track.forceSetState(loadedTrackState);
            if (muted != track.isMuted()) track.toggleMuteTrack();
        }

        // Tail: selected track idx + activeLoopIndex per track
        if (!readRaw(file, &selectedTrackIdx, sizeof(selectedTrackIdx))) {
            Serial.println("[StorageManager] ERROR: Failed to read selectedTrackIdx for v3");
            file.close();
            return false;
        }

        for (uint8_t t = 0; t < numTracks; ++t) {
            if (!readRaw(file, &activeLoopIndex[t], sizeof(activeLoopIndex[t]))) {
                Serial.println("[StorageManager] ERROR: Failed to read activeLoopIndex for v3");
                file.close();
                return false;
            }
        }

        // Optional v3 extension tail: GlobalUndoStack per track.
        bool hasGlobalUndoTail = false;
        if ((file.size() - file.position()) >= sizeof(uint32_t)) {
            uint32_t magic = 0;
            if (!readRaw(file, &magic, sizeof(magic))) {
                file.close();
                return false;
            }
            hasGlobalUndoTail = (magic == GLOBAL_UNDO_MAGIC);
            if (!hasGlobalUndoTail) {
                file.seek(file.position() - static_cast<uint32_t>(sizeof(magic)));
            }
        }

        if (hasGlobalUndoTail) {
            for (uint8_t t = 0; t < numTracks; ++t) {
                Track& track = trackManager.getTrack(t);
                if (!readGlobalUndoStack(file, track.getGlobalUndoStack())) {
                    Serial.print("[StorageManager] ERROR: Failed to read global undo stack for track ");
                    Serial.println(t);
                    file.close();
                    return false;
                }
            }
            Serial.println("[StorageManager] GlobalUndoStack tail loaded (legacy per-slot undo snapshots skipped).");
        } else {
            for (uint8_t t = 0; t < numTracks; ++t) {
                trackManager.getTrack(t).getGlobalUndoStack().clear();
            }
            Serial.println("[StorageManager] WARN: No GlobalUndoStack tail; legacy per-slot undo snapshots were skipped on load.");
        }

        file.close();
        Serial.println("[StorageManager] State loaded successfully (v3).");

        for (uint8_t t = 0; t < numTracks; ++t) {
            migrateLoadedLoopsToTakeTimeline(trackManager.getTrack(t));
        }

        // Apply header state
        state = loadedLooperState;
        trackManager.setMasterLoopLength(masterLoopLength);

        // Restore active slot focus + playback anchor
        for (uint8_t t = 0; t < numTracks; ++t) {
            trackManager.getTrack(t).setActiveLoopIndex(activeLoopIndex[t]);
            trackManager.setSelectedSlotIndex(t, activeLoopIndex[t]);
        }
        if (selectedTrackIdx < Config::NUM_TRACKS) {
            trackManager.setSelectedTrack(selectedTrackIdx);
        } else {
            trackManager.setSelectedTrack(0);
        }
        return true;
    }

    // v1/v2: keep existing slot0-only behavior
    std::vector<TrackLoadData> tracksData(numTracks);

    for (uint8_t t = 0; t < numTracks; ++t) {
        // Track state
        uint32_t trackState = 0;
        if (!readRaw(file, &trackState, sizeof(trackState))) {
            Serial.print("[StorageManager] ERROR: Failed to read trackState for track "); Serial.println(t);
            file.close();
            return false;
        }
        TrackState loadedTrackState = (TrackState)trackState;
        // Muted
        bool muted = false;
        if (!readRaw(file, &muted, sizeof(muted))) {
            Serial.print("[StorageManager] ERROR: Failed to read muted for track "); Serial.println(t);
            file.close();
            return false;
        }
        // Timing
        uint32_t startLoopTick = 0, loopLengthTicks = 0;
        if (!readRaw(file, &startLoopTick, sizeof(startLoopTick))) {
            Serial.print("[StorageManager] ERROR: Failed to read startLoopTick for track "); Serial.println(t);
            file.close();
            return false;
        }
        if (!readRaw(file, &loopLengthTicks, sizeof(loopLengthTicks))) {
            Serial.print("[StorageManager] ERROR: Failed to read loopLengthTicks for track "); Serial.println(t);
            file.close();
            return false;
        }
        // MidiEvents
        uint32_t midiCount = 0;
        if (!readRaw(file, &midiCount, sizeof(midiCount))) {
            Serial.print("[StorageManager] ERROR: Failed to read midiCount for track "); Serial.println(t);
            file.close();
            return false;
        }
        // Check for struct size mismatch or corrupt file
        size_t midiBytesNeeded = midiCount * sizeof(MidiEvent);
        if ((file.size() - file.position()) < midiBytesNeeded) {
            Serial.print("[StorageManager] ERROR: Not enough bytes for midiEvents. Expected ");
            Serial.print(midiBytesNeeded);
            Serial.print(" bytes, but only ");
            Serial.print(file.size() - file.position());
            Serial.println(" available. This may indicate a MidiEvent struct size mismatch.");
            file.close();
            return false;
        }
        MidiEventVec midiEvents(midiCount);
        if (midiCount > 0 && !readRaw(file, midiEvents.data(), midiCount * sizeof(MidiEvent))) {
            Serial.print("[StorageManager] ERROR: Failed to read midiEvents for track "); Serial.println(t);
            file.close();
            return false;
        }
        // Undo history (midi)
        uint32_t undoCount = 0;
        if (!readRaw(file, &undoCount, sizeof(undoCount))) {
            Serial.print("[StorageManager] ERROR: Failed to read undoCount for track "); Serial.println(t);
            file.close();
            return false;
        }
        std::vector<MidiEventVec, ExtMemAllocator<MidiEventVec>> midiHistory;
        for (uint32_t u = 0; u < undoCount; ++u) {
            uint32_t snapCount = 0;
            if (!readRaw(file, &snapCount, sizeof(snapCount))) {
                Serial.print("[StorageManager] ERROR: Failed to read midiHistory snapCount for track "); Serial.println(t);
                file.close();
                return false;
            }
            // Check for struct size mismatch or corrupt file for snapshot
            size_t snapBytesNeeded = snapCount * sizeof(MidiEvent);
            if ((file.size() - file.position()) < snapBytesNeeded) {
                Serial.print("[StorageManager] ERROR: Not enough bytes for midiHistory snapshot. Expected ");
                Serial.print(snapBytesNeeded);
                Serial.print(" bytes, but only ");
                Serial.print(file.size() - file.position());
                Serial.println(" available. This may indicate a MidiEvent struct size mismatch.");
                file.close();
                return false;
            }
            MidiEventVec snapshot(snapCount);
            if (snapCount > 0 && !readRaw(file, snapshot.data(), snapCount * sizeof(MidiEvent))) {
                Serial.print("[StorageManager] ERROR: Failed to read midiHistory snapshot for track "); Serial.println(t);
                file.close();
                return false;
            }
            midiHistory.push_back(std::move(snapshot));
        }
        // Store loaded data for this track
        tracksData[t] = {loadedTrackState, muted, startLoopTick, loopLengthTicks, midiEvents, midiHistory};
    }

    // Try to read selected track index (if present)
    uint8_t selectedTrackIdx = 0;
    if ((file.size() - file.position()) >= sizeof(selectedTrackIdx)) {
        if (!readRaw(file, &selectedTrackIdx, sizeof(selectedTrackIdx))) {
            Serial.println("[StorageManager] ERROR: Failed to read selected track index");
            file.close();
            return false;
        }
        trackManager.setSelectedTrack(selectedTrackIdx);
    } else {
        // Backward compatibility: default to track 0
        trackManager.setSelectedTrack(0);
    }
    file.close();
    Serial.println("[StorageManager] State loaded successfully.");

    // Only apply loaded data if everything succeeded
    state = loadedLooperState;
    trackManager.setMasterLoopLength(masterLoopLength);
    for (uint8_t t = 0; t < numTracks; ++t) {
        Track &track = trackManager.getTrack(t);
        track.getGlobalUndoStack().clear();
        track.setActiveLoopIndex(0);  // v1/v2: load into slot 0
        TrackState loadedState = tracksData[t].state;
        // Never resume volatile capture states after reboot.
        if (loadedState == TRACK_RECORDING || loadedState == TRACK_ARMED || loadedState == TRACK_STOPPED_RECORDING) {
            loadedState = tracksData[t].midiEvents.empty() ? TRACK_EMPTY : TRACK_STOPPED;
        }
        if (loadedState == TRACK_EMPTY && !tracksData[t].midiEvents.empty()) {
            loadedState = TRACK_STOPPED;
        }
        track.forceSetState(loadedState);
        if (tracksData[t].muted != track.isMuted()) track.toggleMuteTrack();
        uint32_t loopLength = tracksData[t].loopLengthTicks;
        // Recover invalid wrapped lengths caused by previous underflowed recording stop.
        if (loopLength >= 0x80000000u) {
            loopLength = 0;
        }
        track.setLoopLength(loopLength);
        Loop& loop = track.getLoop(0);
        loop.startLoopTick = 0;
        LoopEventStore loadedStore;
        loadedStore.loadFromFlat(tracksData[t].midiEvents);
        loop.importPublishedStore(loadedStore);
        if (loop.loopLengthTicks == 0 && loop.hasPublishedEvents()) {
            uint32_t lastTick = track.findLastEventTick();
            loop.loopLengthTicks = track.computeLoopLengthTicks(lastTick);
        }
        track.validateAndCleanupMidiEvents();
        Serial.print("[StorageManager] Track "); Serial.print(t);
        Serial.print(" loaded: events="); Serial.print(tracksData[t].midiEvents.size());
        Serial.println(", legacy undo snapshots skipped");
    }
    return true;
} 