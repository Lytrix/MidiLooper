#include "StorageManager.h"
#include "TrackManager.h"
#include "Globals.h"
#include <SD.h>
#include <Arduino.h>
#include "TrackUndo.h"

#define STORAGE_FILENAME "/midilooper_state.raw"
#define STORAGE_VERSION 1

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
    if (bytesRead != size) {
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
        // Timing
        uint32_t startLoopTick = track.getStartLoopTick();
        uint32_t loopLengthTicks = track.getLength();
        if (!writeRaw(file, &startLoopTick, sizeof(startLoopTick))) { Serial.print("[StorageManager] ERROR: Failed to write startLoopTick for track "); Serial.println(t); file.close(); return false; }
        if (!writeRaw(file, &loopLengthTicks, sizeof(loopLengthTicks))) { Serial.print("[StorageManager] ERROR: Failed to write loopLengthTicks for track "); Serial.println(t); file.close(); return false; }
        // MidiEvents
        const auto &midiEvents = track.getEvents();
        uint32_t midiCount = midiEvents.size();
        if (!writeRaw(file, &midiCount, sizeof(midiCount))) { Serial.print("[StorageManager] ERROR: Failed to write midiCount for track "); Serial.println(t); file.close(); return false; }
        if (midiCount > 0 && !writeRaw(file, midiEvents.data(), midiCount * sizeof(MidiEvent))) { Serial.print("[StorageManager] ERROR: Failed to write midiEvents for track "); Serial.println(t); file.close(); return false; }
        // Undo history (midi)
        const auto &midiHistory = TrackUndo::getMidiHistory(track);
        uint32_t undoCount = midiHistory.size();
        if (!writeRaw(file, &undoCount, sizeof(undoCount))) { Serial.print("[StorageManager] ERROR: Failed to write undoCount for track "); Serial.println(t); file.close(); return false; }
        for (const auto &snapshot : midiHistory) {
            uint32_t snapCount = snapshot.size();
            if (!writeRaw(file, &snapCount, sizeof(snapCount))) { Serial.print("[StorageManager] ERROR: Failed to write midiHistory snapCount for track "); Serial.println(t); file.close(); return false; }
            if (snapCount > 0 && !writeRaw(file, snapshot.data(), snapCount * sizeof(MidiEvent))) { Serial.print("[StorageManager] ERROR: Failed to write midiHistory snapshot for track "); Serial.println(t); file.close(); return false; }
        }
    }
    // Save selected track index
    uint8_t selectedTrackIdx = trackManager.getSelectedTrackIndex();
    if (!file.write(&selectedTrackIdx, sizeof(selectedTrackIdx))) {
        Serial.println("[StorageManager] ERROR: Failed to write selected track index");
        file.close();
        return false;
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
    if (version != STORAGE_VERSION) {
        Serial.print("[StorageManager] ERROR: Version mismatch. Found: ");
        Serial.println(version);
        file.close();
        return false;
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
        track.forceSetState(tracksData[t].state);
        if (tracksData[t].muted != track.isMuted()) track.toggleMuteTrack();
        track.setLength(tracksData[t].loopLengthTicks);
        track.getMidiEvents() = tracksData[t].midiEvents;
        auto &midiHistory = TrackUndo::getMidiHistory(track);
        midiHistory.clear();
        for (const auto& snapshot : tracksData[t].midiHistory) {
            midiHistory.push_back(snapshot);
        }
    }
    return true;
} 