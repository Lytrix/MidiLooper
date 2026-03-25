//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "StorageManager.h"
#include "TrackManager.h"
#include "Loop.h"
#include "Globals.h"
#include "Logger.h"
#include <SD.h>
#include <Arduino.h>
#include "TrackUndo.h"
#include "Utils/MemoryPool.h"
#include <array>

#define STORAGE_FILENAME "/midilooper_state.raw"
#define STORAGE_VERSION 3

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

bool StorageManager::saveState(const LooperState& state) {
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

            logger.log(CAT_STORAGE, LOG_DEBUG, "[StorageManager] v3 saving track=%u slot=%u", t, s);

            if (!writeRaw(file, &slotEnabled, sizeof(slotEnabled))) { Serial.print("[StorageManager] ERROR: Failed to write slotEnabled for track "); Serial.print(t); Serial.print(" slot "); Serial.println(s); file.close(); return false; }
            if (!writeRaw(file, &slotMuted, sizeof(slotMuted))) { Serial.print("[StorageManager] ERROR: Failed to write slotMuted for track "); Serial.print(t); Serial.print(" slot "); Serial.println(s); file.close(); return false; }

            Loop& loop = track.getLoop(s);

            // Loop core: startLoopTick is saved for completeness, but loading will normalize startLoopTick to 0.
            uint32_t startLoopTick = loop.startLoopTick;
            uint32_t loopLengthTicks = loop.loopLengthTicks;
            uint32_t loopStartTick = loop.loopStartTick;
            if (!writeRaw(file, &startLoopTick, sizeof(startLoopTick))) { Serial.print("[StorageManager] ERROR: Failed to write startLoopTick for track "); Serial.print(t); Serial.print(" slot "); Serial.println(s); file.close(); return false; }
            if (!writeRaw(file, &loopLengthTicks, sizeof(loopLengthTicks))) { Serial.print("[StorageManager] ERROR: Failed to write loopLengthTicks for track "); Serial.print(t); Serial.print(" slot "); Serial.println(s); file.close(); return false; }
            if (!writeRaw(file, &loopStartTick, sizeof(loopStartTick))) { Serial.print("[StorageManager] ERROR: Failed to write loopStartTick for track "); Serial.print(t); Serial.print(" slot "); Serial.println(s); file.close(); return false; }

            // MidiEvents
            const auto &midiEvents = loop.midiEvents;
            uint32_t midiCount = midiEvents.size();
            if (!writeRaw(file, &midiCount, sizeof(midiCount))) { Serial.print("[StorageManager] ERROR: Failed to write midiCount for track "); Serial.print(t); Serial.print(" slot "); Serial.println(s); file.close(); return false; }
            if (midiCount > 0 && !writeRaw(file, midiEvents.data(), midiCount * sizeof(MidiEvent))) { Serial.print("[StorageManager] ERROR: Failed to write midiEvents for track "); Serial.print(t); Serial.print(" slot "); Serial.println(s); file.close(); return false; }

            // Overdub undo history (midi + geom) - non-allocating access.
            const auto* midiHistoryPtr = loop.tryGetMidiHistory();
            const auto* geomHistoryPtr = loop.tryGetOverdubGeomHistory();

            // Clamp to avoid out-of-bounds if MIDI history and geom history diverge.
            const size_t midiHistCnt = midiHistoryPtr ? midiHistoryPtr->size() : 0;
            const size_t geomHistCnt = geomHistoryPtr ? geomHistoryPtr->size() : 0;
            uint32_t overdubUndoCount = static_cast<uint32_t>((midiHistCnt < geomHistCnt) ? midiHistCnt : geomHistCnt);

            logger.log(CAT_STORAGE, LOG_DEBUG, "[StorageManager] v3 overdubUndo track=%u slot=%u count=%lu",
                       t, s, static_cast<unsigned long>(overdubUndoCount));
            if (!writeRaw(file, &overdubUndoCount, sizeof(overdubUndoCount))) { Serial.print("[StorageManager] ERROR: Failed to write overdubUndoCount for track "); Serial.print(t); Serial.print(" slot "); Serial.println(s); file.close(); return false; }

            for (uint32_t u = 0; u < overdubUndoCount; ++u) {
                const auto &pooledSnapshot = (*midiHistoryPtr)[u];
                std::vector<MidiEvent> snapshot;
                snapshot.reserve(pooledSnapshot.size());
                for (const auto& eventPtr : pooledSnapshot) {
                    if (!eventPtr) continue; // Avoid null deref from partially-valid snapshots.
                    snapshot.push_back(*eventPtr);
                }
                uint32_t snapCount = snapshot.size();
                if (!writeRaw(file, &snapCount, sizeof(snapCount))) { Serial.print("[StorageManager] ERROR: Failed to write overdubUndo snapCount for track "); Serial.print(t); Serial.print(" slot "); Serial.println(s); file.close(); return false; }
                if (snapCount > 0 && !writeRaw(file, snapshot.data(), snapCount * sizeof(MidiEvent))) { Serial.print("[StorageManager] ERROR: Failed to write overdubUndo snapshot for track "); Serial.print(t); Serial.print(" slot "); Serial.println(s); file.close(); return false; }
                const OverdubGeomSnapshot &geom = (*geomHistoryPtr)[u];
                if (!writeRaw(file, &geom.loopLengthTicks, sizeof(geom.loopLengthTicks))) { Serial.print("[StorageManager] ERROR: Failed to write overdubUndo geom.loopLengthTicks for track "); Serial.print(t); Serial.print(" slot "); Serial.println(s); file.close(); return false; }
                if (!writeRaw(file, &geom.startLoopTick, sizeof(geom.startLoopTick))) { Serial.print("[StorageManager] ERROR: Failed to write overdubUndo geom.startLoopTick for track "); Serial.print(t); Serial.print(" slot "); Serial.println(s); file.close(); return false; }
                if (!writeRaw(file, &geom.loopStartTick, sizeof(geom.loopStartTick))) { Serial.print("[StorageManager] ERROR: Failed to write overdubUndo geom.loopStartTick for track "); Serial.print(t); Serial.print(" slot "); Serial.println(s); file.close(); return false; }

                // Keep the system responsive while serializing long stacks.
                if ((u & 0x7u) == 0u) yield();
            }

            // Overdub redo history (midi + geom) - non-allocating access.
            const auto* midiRedoHistoryPtr = loop.tryGetMidiRedoHistory();
            const auto* geomRedoHistoryPtr = loop.tryGetOverdubGeomRedoHistory();

            // Clamp to avoid out-of-bounds if redo MIDI and geom histories diverge.
            const size_t midiRedoCnt = midiRedoHistoryPtr ? midiRedoHistoryPtr->size() : 0;
            const size_t geomRedoCnt = geomRedoHistoryPtr ? geomRedoHistoryPtr->size() : 0;
            uint32_t overdubRedoCount = static_cast<uint32_t>((midiRedoCnt < geomRedoCnt) ? midiRedoCnt : geomRedoCnt);

            logger.log(CAT_STORAGE, LOG_DEBUG, "[StorageManager] v3 overdubRedo track=%u slot=%u count=%lu",
                       t, s, static_cast<unsigned long>(overdubRedoCount));
            if (!writeRaw(file, &overdubRedoCount, sizeof(overdubRedoCount))) { Serial.print("[StorageManager] ERROR: Failed to write overdubRedoCount for track "); Serial.print(t); Serial.print(" slot "); Serial.println(s); file.close(); return false; }

            for (uint32_t u = 0; u < overdubRedoCount; ++u) {
                const auto &pooledSnapshot = (*midiRedoHistoryPtr)[u];
                std::vector<MidiEvent> snapshot;
                snapshot.reserve(pooledSnapshot.size());
                for (const auto& eventPtr : pooledSnapshot) {
                    if (!eventPtr) continue;
                    snapshot.push_back(*eventPtr);
                }
                uint32_t snapCount = snapshot.size();
                if (!writeRaw(file, &snapCount, sizeof(snapCount))) { Serial.print("[StorageManager] ERROR: Failed to write overdubRedo snapCount for track "); Serial.print(t); Serial.print(" slot "); Serial.println(s); file.close(); return false; }
                if (snapCount > 0 && !writeRaw(file, snapshot.data(), snapCount * sizeof(MidiEvent))) { Serial.print("[StorageManager] ERROR: Failed to write overdubRedo snapshot for track "); Serial.print(t); Serial.print(" slot "); Serial.println(s); file.close(); return false; }
                const OverdubGeomSnapshot &geom = (*geomRedoHistoryPtr)[u];
                if (!writeRaw(file, &geom.loopLengthTicks, sizeof(geom.loopLengthTicks))) { Serial.print("[StorageManager] ERROR: Failed to write overdubRedo geom.loopLengthTicks for track "); Serial.print(t); Serial.print(" slot "); Serial.println(s); file.close(); return false; }
                if (!writeRaw(file, &geom.startLoopTick, sizeof(geom.startLoopTick))) { Serial.print("[StorageManager] ERROR: Failed to write overdubRedo geom.startLoopTick for track "); Serial.print(t); Serial.print(" slot "); Serial.println(s); file.close(); return false; }
                if (!writeRaw(file, &geom.loopStartTick, sizeof(geom.loopStartTick))) { Serial.print("[StorageManager] ERROR: Failed to write overdubRedo geom.loopStartTick for track "); Serial.print(t); Serial.print(" slot "); Serial.println(s); file.close(); return false; }

                if ((u & 0x7u) == 0u) yield();
            }

            // Clear undo history (midi + state + loop geometry) - non-allocating access.
            const auto* clearMidiHistoryPtr = loop.tryGetClearMidiHistory();
            const auto* clearStateHistoryPtr = loop.tryGetClearStateHistory();
            const auto* clearLengthHistoryPtr = loop.tryGetClearLengthHistory();
            const auto* clearStartHistoryPtr = loop.tryGetClearStartHistory();

            // Clamp to avoid out-of-bounds if clear stacks diverge.
            const size_t clearMidiCnt = clearMidiHistoryPtr ? clearMidiHistoryPtr->size() : 0;
            const size_t clearStateCnt = clearStateHistoryPtr ? clearStateHistoryPtr->size() : 0;
            const size_t clearLengthCnt = clearLengthHistoryPtr ? clearLengthHistoryPtr->size() : 0;
            const size_t clearStartCnt = clearStartHistoryPtr ? clearStartHistoryPtr->size() : 0;
            const size_t clearUndoCount = clearMidiCnt;
            size_t clearUndoCountClamped = clearUndoCount;
            if (clearStateCnt < clearUndoCountClamped) clearUndoCountClamped = clearStateCnt;
            if (clearLengthCnt < clearUndoCountClamped) clearUndoCountClamped = clearLengthCnt;
            if (clearStartCnt < clearUndoCountClamped) clearUndoCountClamped = clearStartCnt;
            uint32_t clearUndoCount32 = static_cast<uint32_t>(clearUndoCountClamped);

            logger.log(CAT_STORAGE, LOG_DEBUG, "[StorageManager] v3 clearUndo track=%u slot=%u count=%lu",
                       t, s, static_cast<unsigned long>(clearUndoCount32));
            if (!writeRaw(file, &clearUndoCount32, sizeof(clearUndoCount32))) { Serial.print("[StorageManager] ERROR: Failed to write clearUndoCount for track "); Serial.print(t); Serial.print(" slot "); Serial.println(s); file.close(); return false; }

            for (uint32_t u = 0; u < clearUndoCount32; ++u) {
                const auto &pooledSnapshot = (*clearMidiHistoryPtr)[u];
                std::vector<MidiEvent> snapshot;
                snapshot.reserve(pooledSnapshot.size());
                for (const auto& eventPtr : pooledSnapshot) {
                    if (!eventPtr) continue;
                    snapshot.push_back(*eventPtr);
                }
                uint32_t snapCount = snapshot.size();
                if (!writeRaw(file, &snapCount, sizeof(snapCount))) { Serial.print("[StorageManager] ERROR: Failed to write clearUndo snapCount for track "); Serial.print(t); Serial.print(" slot "); Serial.println(s); file.close(); return false; }
                if (snapCount > 0 && !writeRaw(file, snapshot.data(), snapCount * sizeof(MidiEvent))) { Serial.print("[StorageManager] ERROR: Failed to write clearUndo snapshot for track "); Serial.print(t); Serial.print(" slot "); Serial.println(s); file.close(); return false; }
                const TrackState snapState = (*clearStateHistoryPtr)[u];
                uint32_t snapStateRaw = static_cast<uint32_t>(snapState);
                if (!writeRaw(file, &snapStateRaw, sizeof(snapStateRaw))) { Serial.print("[StorageManager] ERROR: Failed to write clearUndo snapState for track "); Serial.print(t); Serial.print(" slot "); Serial.println(s); file.close(); return false; }
                const uint32_t snapLoopLengthTicks = (*clearLengthHistoryPtr)[u];
                const uint32_t snapLoopStartTick = (*clearStartHistoryPtr)[u];
                if (!writeRaw(file, &snapLoopLengthTicks, sizeof(snapLoopLengthTicks))) { Serial.print("[StorageManager] ERROR: Failed to write clearUndo snapLoopLengthTicks for track "); Serial.print(t); Serial.print(" slot "); Serial.println(s); file.close(); return false; }
                if (!writeRaw(file, &snapLoopStartTick, sizeof(snapLoopStartTick))) { Serial.print("[StorageManager] ERROR: Failed to write clearUndo snapLoopStartTick for track "); Serial.print(t); Serial.print(" slot "); Serial.println(s); file.close(); return false; }

                if ((u & 0x7u) == 0u) yield();
            }

            // Clear redo history - non-allocating access.
            const auto* clearMidiRedoHistoryPtr = loop.tryGetClearMidiRedoHistory();
            const auto* clearStateRedoHistoryPtr = loop.tryGetClearStateRedoHistory();
            const auto* clearLengthRedoHistoryPtr = loop.tryGetClearLengthRedoHistory();
            const auto* clearStartRedoHistoryPtr = loop.tryGetClearStartRedoHistory();

            // Clamp to avoid out-of-bounds if clear redo stacks diverge.
            const size_t clearMidiRedoCnt = clearMidiRedoHistoryPtr ? clearMidiRedoHistoryPtr->size() : 0;
            const size_t clearStateRedoCnt = clearStateRedoHistoryPtr ? clearStateRedoHistoryPtr->size() : 0;
            const size_t clearLengthRedoCnt = clearLengthRedoHistoryPtr ? clearLengthRedoHistoryPtr->size() : 0;
            const size_t clearStartRedoCnt = clearStartRedoHistoryPtr ? clearStartRedoHistoryPtr->size() : 0;
            const size_t clearRedoCount = clearMidiRedoCnt;
            size_t clearRedoCountClamped = clearRedoCount;
            if (clearStateRedoCnt < clearRedoCountClamped) clearRedoCountClamped = clearStateRedoCnt;
            if (clearLengthRedoCnt < clearRedoCountClamped) clearRedoCountClamped = clearLengthRedoCnt;
            if (clearStartRedoCnt < clearRedoCountClamped) clearRedoCountClamped = clearStartRedoCnt;
            uint32_t clearRedoCount32 = static_cast<uint32_t>(clearRedoCountClamped);

            logger.log(CAT_STORAGE, LOG_DEBUG, "[StorageManager] v3 clearRedo track=%u slot=%u count=%lu",
                       t, s, static_cast<unsigned long>(clearRedoCount32));
            if (!writeRaw(file, &clearRedoCount32, sizeof(clearRedoCount32))) { Serial.print("[StorageManager] ERROR: Failed to write clearRedoCount for track "); Serial.print(t); Serial.print(" slot "); Serial.println(s); file.close(); return false; }

            for (uint32_t u = 0; u < clearRedoCount32; ++u) {
                const auto &pooledSnapshot = (*clearMidiRedoHistoryPtr)[u];
                std::vector<MidiEvent> snapshot;
                snapshot.reserve(pooledSnapshot.size());
                for (const auto& eventPtr : pooledSnapshot) {
                    if (!eventPtr) continue;
                    snapshot.push_back(*eventPtr);
                }
                uint32_t snapCount = snapshot.size();
                if (!writeRaw(file, &snapCount, sizeof(snapCount))) { Serial.print("[StorageManager] ERROR: Failed to write clearRedo snapCount for track "); Serial.print(t); Serial.print(" slot "); Serial.println(s); file.close(); return false; }
                if (snapCount > 0 && !writeRaw(file, snapshot.data(), snapCount * sizeof(MidiEvent))) { Serial.print("[StorageManager] ERROR: Failed to write clearRedo snapshot for track "); Serial.print(t); Serial.print(" slot "); Serial.println(s); file.close(); return false; }
                const TrackState snapState = (*clearStateRedoHistoryPtr)[u];
                uint32_t snapStateRaw = static_cast<uint32_t>(snapState);
                if (!writeRaw(file, &snapStateRaw, sizeof(snapStateRaw))) { Serial.print("[StorageManager] ERROR: Failed to write clearRedo snapState for track "); Serial.print(t); Serial.print(" slot "); Serial.println(s); file.close(); return false; }
                const uint32_t snapLoopLengthTicks = (*clearLengthRedoHistoryPtr)[u];
                const uint32_t snapLoopStartTick = (*clearStartRedoHistoryPtr)[u];
                if (!writeRaw(file, &snapLoopLengthTicks, sizeof(snapLoopLengthTicks))) { Serial.print("[StorageManager] ERROR: Failed to write clearRedo snapLoopLengthTicks for track "); Serial.print(t); Serial.print(" slot "); Serial.println(s); file.close(); return false; }
                if (!writeRaw(file, &snapLoopStartTick, sizeof(snapLoopStartTick))) { Serial.print("[StorageManager] ERROR: Failed to write clearRedo snapLoopStartTick for track "); Serial.print(t); Serial.print(" slot "); Serial.println(s); file.close(); return false; }

                if ((u & 0x7u) == 0u) yield();
            }

            // Loop-start edit undo/redo (loopStartTick only) - non-allocating access.
            const auto* loopStartHistoryPtr = loop.tryGetLoopStartHistory();
            const auto* loopStartRedoHistoryPtr = loop.tryGetLoopStartRedoHistory();

            const size_t loopStartUndoCount = loopStartHistoryPtr ? loopStartHistoryPtr->size() : 0;
            uint32_t loopStartUndoCount32 = static_cast<uint32_t>(loopStartUndoCount);
            if (!writeRaw(file, &loopStartUndoCount32, sizeof(loopStartUndoCount32))) { Serial.print("[StorageManager] ERROR: Failed to write loopStartUndoCount for track "); Serial.print(t); Serial.print(" slot "); Serial.println(s); file.close(); return false; }
            for (uint32_t u = 0; u < loopStartUndoCount32; ++u) {
                uint32_t tick = (*loopStartHistoryPtr)[u];
                if (!writeRaw(file, &tick, sizeof(tick))) { Serial.print("[StorageManager] ERROR: Failed to write loopStartUndo tick for track "); Serial.print(t); Serial.print(" slot "); Serial.println(s); file.close(); return false; }
            }

            const size_t loopStartRedoCount = loopStartRedoHistoryPtr ? loopStartRedoHistoryPtr->size() : 0;
            uint32_t loopStartRedoCount32 = static_cast<uint32_t>(loopStartRedoCount);
            if (!writeRaw(file, &loopStartRedoCount32, sizeof(loopStartRedoCount32))) { Serial.print("[StorageManager] ERROR: Failed to write loopStartRedoCount for track "); Serial.print(t); Serial.print(" slot "); Serial.println(s); file.close(); return false; }
            for (uint32_t u = 0; u < loopStartRedoCount32; ++u) {
                uint32_t tick = (*loopStartRedoHistoryPtr)[u];
                if (!writeRaw(file, &tick, sizeof(tick))) { Serial.print("[StorageManager] ERROR: Failed to write loopStartRedo tick for track "); Serial.print(t); Serial.print(" slot "); Serial.println(s); file.close(); return false; }
            }

            // Yield once per slot to avoid watchdog issues during save.
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
    file.close();
    Serial.println("[StorageManager] State saved successfully.");
    return true;
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
    if (version != 1 && version != 2 && version != 3) {
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
        std::vector<MidiEvent> midiEvents;
        std::vector<std::vector<MidiEvent>> midiHistory;
    };
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

                // Reset loop
                loop.midiEvents.clear();
                loop.startLoopTick = 0;     // playback normalization
                loop.loopLengthTicks = 0;
                loop.loopStartTick = 0;
                loop.lastTickInLoop = 0;
                loop.nextEventIndex = 0;
                loop.playbackOrderDirty = true;
                loop.invalidateCaches();
                loop.clearAllUndoStacks();

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
                std::vector<MidiEvent> midiEvents(midiCount);
                if (midiCount > 0 && !readRaw(file, midiEvents.data(), midiCount * sizeof(MidiEvent))) { Serial.print("[StorageManager] ERROR: Failed to read midiEvents for track "); Serial.print(t); Serial.print(" slot "); Serial.println(s); file.close(); return false; }
                loop.midiEvents = std::move(midiEvents);
                if (loop.loopLengthTicks == 0 && !loop.midiEvents.empty()) {
                    uint32_t lastTick = 0;
                    for (const auto &evt : loop.midiEvents) lastTick = (evt.tick > lastTick) ? evt.tick : lastTick;
                    loop.loopLengthTicks = track.computeLoopLengthTicks(lastTick);
                }
                if (!loop.midiEvents.empty()) anySlotHasEvents = true;
                loop.invalidateCaches();

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
                    std::vector<MidiEvent> snapshot(snapCount);
                    if (snapCount > 0 && !readRaw(file, snapshot.data(), snapCount * sizeof(MidiEvent))) { Serial.print("[StorageManager] ERROR: Failed to read overdubUndo snapshot for track "); Serial.print(t); Serial.print(" slot "); Serial.println(s); file.close(); return false; }
                    OverdubGeomSnapshot geom;
                    uint32_t geomLoopLengthTicks = 0;
                    uint32_t geomStartLoopTick = 0;
                    uint32_t geomLoopStartTick = 0;
                    if (!readRaw(file, &geomLoopLengthTicks, sizeof(geomLoopLengthTicks))) { Serial.println("[StorageManager] ERROR: Failed to read overdubUndo geom.loopLengthTicks"); file.close(); return false; }
                    if (!readRaw(file, &geomStartLoopTick, sizeof(geomStartLoopTick))) { Serial.println("[StorageManager] ERROR: Failed to read overdubUndo geom.startLoopTick"); file.close(); return false; }
                    if (!readRaw(file, &geomLoopStartTick, sizeof(geomLoopStartTick))) { Serial.println("[StorageManager] ERROR: Failed to read overdubUndo geom.loopStartTick"); file.close(); return false; }
                    geom.loopLengthTicks = geomLoopLengthTicks;
                    geom.startLoopTick = 0; // playback normalization
                    geom.loopStartTick = geomLoopStartTick;

                    MemoryPool::PooledMidiEventVector pooledSnapshot(MemoryPool::globalMidiEventPool);
                    for (const auto &event : snapshot) pooledSnapshot.push_back(event);
                    loop.getMidiHistory().push_back(std::move(pooledSnapshot));
                    loop.getOverdubGeomHistory().push_back(geom);
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
                    std::vector<MidiEvent> snapshot(snapCount);
                    if (snapCount > 0 && !readRaw(file, snapshot.data(), snapCount * sizeof(MidiEvent))) { Serial.print("[StorageManager] ERROR: Failed to read overdubRedo snapshot for track "); Serial.print(t); Serial.print(" slot "); Serial.println(s); file.close(); return false; }
                    OverdubGeomSnapshot geom;
                    uint32_t geomLoopLengthTicks = 0;
                    uint32_t geomStartLoopTick = 0;
                    uint32_t geomLoopStartTick = 0;
                    if (!readRaw(file, &geomLoopLengthTicks, sizeof(geomLoopLengthTicks))) { Serial.println("[StorageManager] ERROR: Failed to read overdubRedo geom.loopLengthTicks"); file.close(); return false; }
                    if (!readRaw(file, &geomStartLoopTick, sizeof(geomStartLoopTick))) { Serial.println("[StorageManager] ERROR: Failed to read overdubRedo geom.startLoopTick"); file.close(); return false; }
                    if (!readRaw(file, &geomLoopStartTick, sizeof(geomLoopStartTick))) { Serial.println("[StorageManager] ERROR: Failed to read overdubRedo geom.loopStartTick"); file.close(); return false; }
                    geom.loopLengthTicks = geomLoopLengthTicks;
                    geom.startLoopTick = 0; // playback normalization
                    geom.loopStartTick = geomLoopStartTick;

                    MemoryPool::PooledMidiEventVector pooledSnapshot(MemoryPool::globalMidiEventPool);
                    for (const auto &event : snapshot) pooledSnapshot.push_back(event);
                    loop.getMidiRedoHistory().push_back(std::move(pooledSnapshot));
                    loop.getOverdubGeomRedoHistory().push_back(geom);
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
                    std::vector<MidiEvent> snapshot(snapCount);
                    if (snapCount > 0 && !readRaw(file, snapshot.data(), snapCount * sizeof(MidiEvent))) { Serial.print("[StorageManager] ERROR: Failed to read clearUndo snapshot for track "); Serial.print(t); Serial.print(" slot "); Serial.println(s); file.close(); return false; }
                    uint32_t snapStateRaw = 0;
                    uint32_t snapLoopLengthTicks = 0;
                    uint32_t snapLoopStartTick = 0;
                    if (!readRaw(file, &snapStateRaw, sizeof(snapStateRaw))) { Serial.println("[StorageManager] ERROR: Failed to read clearUndo snapState"); file.close(); return false; }
                    if (!readRaw(file, &snapLoopLengthTicks, sizeof(snapLoopLengthTicks))) { Serial.println("[StorageManager] ERROR: Failed to read clearUndo snapLoopLengthTicks"); file.close(); return false; }
                    if (!readRaw(file, &snapLoopStartTick, sizeof(snapLoopStartTick))) { Serial.println("[StorageManager] ERROR: Failed to read clearUndo snapLoopStartTick"); file.close(); return false; }
                    MemoryPool::PooledMidiEventVector pooledSnapshot(MemoryPool::globalMidiEventPool);
                    for (const auto &event : snapshot) pooledSnapshot.push_back(event);
                    loop.getClearMidiHistory().push_back(std::move(pooledSnapshot));
                    loop.getClearStateHistory().push_back((TrackState)snapStateRaw);
                    loop.getClearLengthHistory().push_back(snapLoopLengthTicks);
                    loop.getClearStartHistory().push_back(snapLoopStartTick);
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
                    std::vector<MidiEvent> snapshot(snapCount);
                    if (snapCount > 0 && !readRaw(file, snapshot.data(), snapCount * sizeof(MidiEvent))) { Serial.print("[StorageManager] ERROR: Failed to read clearRedo snapshot for track "); Serial.print(t); Serial.print(" slot "); Serial.println(s); file.close(); return false; }
                    uint32_t snapStateRaw = 0;
                    uint32_t snapLoopLengthTicks = 0;
                    uint32_t snapLoopStartTick = 0;
                    if (!readRaw(file, &snapStateRaw, sizeof(snapStateRaw))) { Serial.println("[StorageManager] ERROR: Failed to read clearRedo snapState"); file.close(); return false; }
                    if (!readRaw(file, &snapLoopLengthTicks, sizeof(snapLoopLengthTicks))) { Serial.println("[StorageManager] ERROR: Failed to read clearRedo snapLoopLengthTicks"); file.close(); return false; }
                    if (!readRaw(file, &snapLoopStartTick, sizeof(snapLoopStartTick))) { Serial.println("[StorageManager] ERROR: Failed to read clearRedo snapLoopStartTick"); file.close(); return false; }
                    MemoryPool::PooledMidiEventVector pooledSnapshot(MemoryPool::globalMidiEventPool);
                    for (const auto &event : snapshot) pooledSnapshot.push_back(event);
                    loop.getClearMidiRedoHistory().push_back(std::move(pooledSnapshot));
                    loop.getClearStateRedoHistory().push_back((TrackState)snapStateRaw);
                    loop.getClearLengthRedoHistory().push_back(snapLoopLengthTicks);
                    loop.getClearStartRedoHistory().push_back(snapLoopStartTick);
                }

                // Loop-start edit undo/redo
                uint32_t loopStartUndoCount = 0;
                if (!readRaw(file, &loopStartUndoCount, sizeof(loopStartUndoCount))) { Serial.print("[StorageManager] ERROR: Failed to read loopStartUndoCount for track "); Serial.print(t); Serial.print(" slot "); Serial.println(s); file.close(); return false; }
                for (uint32_t u = 0; u < loopStartUndoCount; ++u) {
                    uint32_t tick = 0;
                    if (!readRaw(file, &tick, sizeof(tick))) { Serial.println("[StorageManager] ERROR: Failed to read loopStartUndo tick"); file.close(); return false; }
                    loop.getLoopStartHistory().push_back(tick);
                }
                uint32_t loopStartRedoCount = 0;
                if (!readRaw(file, &loopStartRedoCount, sizeof(loopStartRedoCount))) { Serial.print("[StorageManager] ERROR: Failed to read loopStartRedoCount for track "); Serial.print(t); Serial.print(" slot "); Serial.println(s); file.close(); return false; }
                for (uint32_t u = 0; u < loopStartRedoCount; ++u) {
                    uint32_t tick = 0;
                    if (!readRaw(file, &tick, sizeof(tick))) { Serial.println("[StorageManager] ERROR: Failed to read loopStartRedo tick"); file.close(); return false; }
                    loop.getLoopStartRedoHistory().push_back(tick);
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

        file.close();
        Serial.println("[StorageManager] State loaded successfully (v3).");

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
        std::vector<MidiEvent> midiEvents(midiCount);
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
        std::vector<std::vector<MidiEvent>> midiHistory;
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
            std::vector<MidiEvent> snapshot(snapCount);
            if (snapCount > 0 && !readRaw(file, snapshot.data(), snapCount * sizeof(MidiEvent))) {
                Serial.print("[StorageManager] ERROR: Failed to read midiHistory snapshot for track "); Serial.println(t);
                file.close();
                return false;
            }
            midiHistory.push_back(snapshot);
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
        // Always restart loop timeline from zero after load to avoid negative-wrap playback math.
        loop.startLoopTick = 0;
        loop.midiEvents = tracksData[t].midiEvents;
        if (loop.loopLengthTicks == 0 && !loop.midiEvents.empty()) {
            // findLastEventTick uses the active loop; for v1/v2 we always load into slot 0
            uint32_t lastTick = track.findLastEventTick();
            loop.loopLengthTicks = track.computeLoopLengthTicks(lastTick);
        }
        auto &midiHistory = TrackUndo::getMidiHistory(track);
        midiHistory.clear();
        for (const auto& snapshot : tracksData[t].midiHistory) {
            // Convert regular vector to pooled vector
            MemoryPool::PooledMidiEventVector pooledSnapshot(MemoryPool::globalMidiEventPool);
            for (const auto& event : snapshot) {
                pooledSnapshot.push_back(event);
            }
            midiHistory.push_back(std::move(pooledSnapshot));
        }
        Serial.print("[StorageManager] Track "); Serial.print(t);
        Serial.print(" loaded: events="); Serial.print(tracksData[t].midiEvents.size());
        Serial.print(", undo_snapshots="); Serial.println(tracksData[t].midiHistory.size());
    }
    return true;
} 