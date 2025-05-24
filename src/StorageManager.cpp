#include "StorageManager.h"
#include "TrackManager.h"
#include "Globals.h"
#include <SD.h>
#include <Arduino.h>

#define STORAGE_FILENAME "/midilooper_state.raw"
#define STORAGE_VERSION 1

// Helper to write raw data
static bool writeRaw(File &file, const void *data, size_t size) {
    return file.write((const uint8_t*)data, size) == size;
}
// Helper to read raw data
static bool readRaw(File &file, void *data, size_t size) {
    return file.read((uint8_t*)data, size) == size;
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
        // NoteEvents
        const auto &noteEvents = track.getNoteEvents();
        uint32_t noteCount = noteEvents.size();
        if (!writeRaw(file, &noteCount, sizeof(noteCount))) { Serial.print("[StorageManager] ERROR: Failed to write noteCount for track "); Serial.println(t); file.close(); return false; }
        if (noteCount > 0 && !writeRaw(file, noteEvents.data(), noteCount * sizeof(NoteEvent))) { Serial.print("[StorageManager] ERROR: Failed to write noteEvents for track "); Serial.println(t); file.close(); return false; }
        // Undo history (midi)
        const auto &midiHistory = track.getMidiHistory();
        uint32_t undoCount = midiHistory.size();
        if (!writeRaw(file, &undoCount, sizeof(undoCount))) { Serial.print("[StorageManager] ERROR: Failed to write undoCount for track "); Serial.println(t); file.close(); return false; }
        for (const auto &snapshot : midiHistory) {
            uint32_t snapCount = snapshot.size();
            if (!writeRaw(file, &snapCount, sizeof(snapCount))) { Serial.print("[StorageManager] ERROR: Failed to write midiHistory snapCount for track "); Serial.println(t); file.close(); return false; }
            if (snapCount > 0 && !writeRaw(file, snapshot.data(), snapCount * sizeof(MidiEvent))) { Serial.print("[StorageManager] ERROR: Failed to write midiHistory snapshot for track "); Serial.println(t); file.close(); return false; }
        }
        // Undo history (note)
        const auto &noteHistory = track.getNoteHistory();
        uint32_t noteUndoCount = noteHistory.size();
        if (!writeRaw(file, &noteUndoCount, sizeof(noteUndoCount))) { Serial.print("[StorageManager] ERROR: Failed to write noteUndoCount for track "); Serial.println(t); file.close(); return false; }
        for (const auto &snapshot : noteHistory) {
            uint32_t snapCount = snapshot.size();
            if (!writeRaw(file, &snapCount, sizeof(snapCount))) { Serial.print("[StorageManager] ERROR: Failed to write noteHistory snapCount for track "); Serial.println(t); file.close(); return false; }
            if (snapCount > 0 && !writeRaw(file, snapshot.data(), snapCount * sizeof(NoteEvent))) { Serial.print("[StorageManager] ERROR: Failed to write noteHistory snapshot for track "); Serial.println(t); file.close(); return false; }
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
    uint32_t version = 0;
    if (!readRaw(file, &version, sizeof(version))) { Serial.println("[StorageManager] ERROR: Failed to read version"); file.close(); return false; }
    if (version != STORAGE_VERSION) { Serial.print("[StorageManager] ERROR: Version mismatch. Found: "); Serial.println(version); file.close(); return false; }

    // Looper state
    uint32_t looperStateVal = 0;
    if (!readRaw(file, &looperStateVal, sizeof(looperStateVal))) { Serial.println("[StorageManager] ERROR: Failed to read looper state"); file.close(); return false; }
    state = (LooperState)looperStateVal;

    // Master loop length
    uint32_t masterLoopLength = 0;
    if (!readRaw(file, &masterLoopLength, sizeof(masterLoopLength))) { Serial.println("[StorageManager] ERROR: Failed to read master loop length"); file.close(); return false; }
    trackManager.setMasterLoopLength(masterLoopLength);

    // Tracks
    uint8_t numTracks = 0;
    if (!readRaw(file, &numTracks, sizeof(numTracks))) { Serial.println("[StorageManager] ERROR: Failed to read numTracks"); file.close(); return false; }
    for (uint8_t t = 0; t < numTracks; ++t) {
        Track &track = trackManager.getTrack(t);
        // Track state
        uint32_t trackState = 0;
        if (!readRaw(file, &trackState, sizeof(trackState))) { Serial.print("[StorageManager] ERROR: Failed to read trackState for track "); Serial.println(t); file.close(); return false; }
        // Force set state to avoid state machine corruption instead of setState
        track.forceSetState((TrackState)trackState);
        // Muted
        bool muted = false;
        if (!readRaw(file, &muted, sizeof(muted))) { Serial.print("[StorageManager] ERROR: Failed to read muted for track "); Serial.println(t); file.close(); return false; }
        if (muted != track.isMuted()) track.toggleMuteTrack();
        // Timing
        uint32_t startLoopTick = 0, loopLengthTicks = 0;
        if (!readRaw(file, &startLoopTick, sizeof(startLoopTick))) { Serial.print("[StorageManager] ERROR: Failed to read startLoopTick for track "); Serial.println(t); file.close(); return false; }
        if (!readRaw(file, &loopLengthTicks, sizeof(loopLengthTicks))) { Serial.print("[StorageManager] ERROR: Failed to read loopLengthTicks for track "); Serial.println(t); file.close(); return false; }
        track.setLength(loopLengthTicks);
        // MidiEvents
        uint32_t midiCount = 0;
        if (!readRaw(file, &midiCount, sizeof(midiCount))) { Serial.print("[StorageManager] ERROR: Failed to read midiCount for track "); Serial.println(t); file.close(); return false; }
        std::vector<MidiEvent> midiEvents(midiCount);
        if (midiCount > 0 && !readRaw(file, midiEvents.data(), midiCount * sizeof(MidiEvent))) { Serial.print("[StorageManager] ERROR: Failed to read midiEvents for track "); Serial.println(t); file.close(); return false; }
        track.getMidiEvents() = midiEvents;
        // NoteEvents
        uint32_t noteCount = 0;
        if (!readRaw(file, &noteCount, sizeof(noteCount))) { Serial.print("[StorageManager] ERROR: Failed to read noteCount for track "); Serial.println(t); file.close(); return false; }
        std::vector<NoteEvent> noteEvents(noteCount);
        if (noteCount > 0 && !readRaw(file, noteEvents.data(), noteCount * sizeof(NoteEvent))) { Serial.print("[StorageManager] ERROR: Failed to read noteEvents for track "); Serial.println(t); file.close(); return false; }
        track.getNoteEvents() = noteEvents;
        // Undo history (midi)
        uint32_t undoCount = 0;
        if (!readRaw(file, &undoCount, sizeof(undoCount))) { Serial.print("[StorageManager] ERROR: Failed to read undoCount for track "); Serial.println(t); file.close(); return false; }
        auto &midiHistory = track.getMidiHistory();
        midiHistory.clear();
        for (uint32_t u = 0; u < undoCount; ++u) {
            uint32_t snapCount = 0;
            if (!readRaw(file, &snapCount, sizeof(snapCount))) { Serial.print("[StorageManager] ERROR: Failed to read midiHistory snapCount for track "); Serial.println(t); file.close(); return false; }
            std::vector<MidiEvent> snapshot(snapCount);
            if (snapCount > 0 && !readRaw(file, snapshot.data(), snapCount * sizeof(MidiEvent))) { Serial.print("[StorageManager] ERROR: Failed to read midiHistory snapshot for track "); Serial.println(t); file.close(); return false; }
            midiHistory.push_back(snapshot);
        }
        // Undo history (note)
        uint32_t noteUndoCount = 0;
        if (!readRaw(file, &noteUndoCount, sizeof(noteUndoCount))) { Serial.print("[StorageManager] ERROR: Failed to read noteUndoCount for track "); Serial.println(t); file.close(); return false; }
        auto &noteHistory = track.getNoteHistory();
        noteHistory.clear();
        for (uint32_t u = 0; u < noteUndoCount; ++u) {
            uint32_t snapCount = 0;
            if (!readRaw(file, &snapCount, sizeof(snapCount))) { Serial.print("[StorageManager] ERROR: Failed to read noteHistory snapCount for track "); Serial.println(t); file.close(); return false; }
            std::vector<NoteEvent> snapshot(snapCount);
            if (snapCount > 0 && !readRaw(file, snapshot.data(), snapCount * sizeof(NoteEvent))) { Serial.print("[StorageManager] ERROR: Failed to read noteHistory snapshot for track "); Serial.println(t); file.close(); return false; }
            noteHistory.push_back(snapshot);
        }
    }
    file.close();
    Serial.println("[StorageManager] State loaded successfully.");
    return true;
} 