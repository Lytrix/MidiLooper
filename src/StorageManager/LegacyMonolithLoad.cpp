//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// Legacy v5/v6 monolith SD read and one-shot migration to current-set layout.

#include "StorageManager.h"
#include "StorageManagerInternal.h"

#include "CurrentSetStorage.h"
#include "GlobalUndoStack.h"
#include "Globals.h"
#include "Loop.h"
#include "StorageLoopIo.h"
#include "TrackManager.h"
#include "TrackUndo.h"
#include <Arduino.h>
#include <SD.h>
#include <vector>

namespace {

constexpr uint32_t kLegacyMonolithStorageVersion = 6;

#if defined(ARDUINO)
void quarantineLegacyMonolithFileOnSd() {
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
}
#endif

}  // namespace

// Cold v5 migration path — keep out of ITCM so DMAMEM StorageSession does not
// tip FlexRAM into a 14th code bank (steals a DTCM bank).
bool STORAGE_PERSIST_MEM StorageManager::loadV5MonolithIntoRam(LooperState& state) {
    Serial.println("[StorageManager] Loading state from SD card...");
    File file = SD.open(CurrentSetStorage::kLegacyMonolithPath, FILE_READ);
    if (!file) {
        Serial.print("[StorageManager] ERROR: Could not open file for reading: ");
        Serial.println(CurrentSetStorage::kLegacyMonolithPath);
        return false;
    }
    // Use temporary variables to avoid corrupting current state if file is bad
    uint32_t version = 0;
    if (!StorageManagerInternal::readRaw(file, &version, sizeof(version))) {
        Serial.println("[StorageManager] ERROR: Failed to read version");
        file.close();
        return false;
    }
    Serial.println("[StorageManager] Version read OK");
    if (version != kLegacyMonolithStorageVersion) {
        Serial.print("[StorageManager] ERROR: Unsupported legacy storage version. Found: ");
        Serial.println(version);
        file.close();
        return false;
    }

    float savedBpm = 0;
    if (!StorageManagerInternal::readRaw(file, &savedBpm, sizeof(savedBpm))) {
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
    if (!StorageManagerInternal::readRaw(file, &looperStateVal, sizeof(looperStateVal))) {
        Serial.println("[StorageManager] ERROR: Failed to read looper state");
        file.close();
        return false;
    }
    LooperState loadedLooperState =
        StorageManagerInternal::sanitizeLooperStateForPersistence(
            static_cast<LooperState>(looperStateVal));

    // Master loop length
    uint32_t masterLoopLength = 0;
    if (!StorageManagerInternal::readRaw(file, &masterLoopLength, sizeof(masterLoopLength))) {
        Serial.println("[StorageManager] ERROR: Failed to read master loop length");
        file.close();
        return false;
    }

    // Tracks
    uint8_t numTracks = 0;
    if (!StorageManagerInternal::readRaw(file, &numTracks, sizeof(numTracks))) {
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
#if defined(ARDUINO)
        quarantineLegacyMonolithFileOnSd();
#endif
        StorageManagerInternal::resetTracksAfterFailedLoad();
        return false;
    };

    for (uint8_t t = 0; t < numTracks; ++t) {
        Track& track = trackManager.getTrack(t);
        track.ensureLoopsAllocated();

        uint32_t trackStateRaw = 0;
        if (!StorageManagerInternal::readRaw(file, &trackStateRaw, sizeof(trackStateRaw))) {
            Serial.print("[StorageManager] ERROR: Failed to read trackState for track ");
            Serial.println(t);
            return failAfterPartialLoad();
        }
        TrackState loadedTrackState = static_cast<TrackState>(trackStateRaw);

        bool muted = false;
        if (!StorageManagerInternal::readRaw(file, &muted, sizeof(muted))) {
            Serial.print("[StorageManager] ERROR: Failed to read muted for track ");
            Serial.println(t);
            return failAfterPartialLoad();
        }

        bool anySlotHasEvents = false;

        for (uint8_t s = 0; s < Config::MAX_LOOPS_PER_TRACK; ++s) {
            bool slotEnabled = false;
            bool slotMuted = false;
            LoopId slotLoopId = kInvalidLoopId;
            if (!StorageManagerInternal::readRaw(file, &slotEnabled, sizeof(slotEnabled))) {
                Serial.print("[StorageManager] ERROR: Failed to read slotEnabled for track ");
                Serial.print(t);
                Serial.print(" slot ");
                Serial.println(s);
                return failAfterPartialLoad();
            }
            if (!StorageManagerInternal::readRaw(file, &slotMuted, sizeof(slotMuted))) {
                Serial.print("[StorageManager] ERROR: Failed to read slotMuted for track ");
                Serial.print(t);
                Serial.print(" slot ");
                Serial.println(s);
                return failAfterPartialLoad();
            }
            if (!StorageManagerInternal::readRaw(file, &slotLoopId, sizeof(slotLoopId))) {
                Serial.print("[StorageManager] ERROR: Failed to read slotLoopId for track ");
                Serial.print(t);
                Serial.print(" slot ");
                Serial.println(s);
                return failAfterPartialLoad();
            }

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

        const StorageIo loopIo = StorageManagerInternal::storageIoFromFileRead(file);
        for (uint8_t p = 0; p < Config::MAX_LOOPS_PER_TRACK; ++p) {
            Loop& loop = track.loopPool_.at(p);
            if (!readLoopPersisted(loopIo, loop)) {
                Serial.print("[StorageManager] ERROR: Failed to read loop pool entry track ");
                Serial.print(t);
                Serial.print(" pool ");
                Serial.println(p);
                return failAfterPartialLoad();
            }
            if (loop.hasCommittedPasses()) {
                anySlotHasEvents = true;
            }
        }

        StorageManagerInternal::applyLoadedTrackStateAfterLoopSlots(track, loadedTrackState,
                                                                    anySlotHasEvents, muted);
    }

    if (!StorageManagerInternal::readRaw(file, &selectedTrackIdx, sizeof(selectedTrackIdx))) {
        Serial.println("[StorageManager] ERROR: Failed to read selectedTrackIdx for legacy monolith");
        return failAfterPartialLoad();
    }

    std::vector<uint8_t> selectedSlotIndex(numTracks, 0);
    for (uint8_t t = 0; t < numTracks; ++t) {
        if (!StorageManagerInternal::readRaw(file, &activeLoopIndex[t], sizeof(activeLoopIndex[t]))) {
            Serial.println("[StorageManager] ERROR: Failed to read activeLoopIndex for legacy monolith");
            return failAfterPartialLoad();
        }
    }

    uint32_t footerToken = 0;
    if (!StorageManagerInternal::readRaw(file, &footerToken, sizeof(footerToken))) {
        Serial.println("[StorageManager] ERROR: Failed to read global undo stack token for legacy monolith");
        return failAfterPartialLoad();
    }
    if (footerToken == kFooterSelectedSlotExtensionToken) {
        for (uint8_t t = 0; t < numTracks; ++t) {
            if (!StorageManagerInternal::readRaw(file, &selectedSlotIndex[t],
                                                 sizeof(selectedSlotIndex[t]))) {
                Serial.println(
                    "[StorageManager] ERROR: Failed to read selectedSlotIndex for legacy monolith");
                return failAfterPartialLoad();
            }
        }
        if (!StorageManagerInternal::readRaw(file, &footerToken, sizeof(footerToken))) {
            Serial.println(
                "[StorageManager] ERROR: Failed to read global undo stack token for legacy monolith");
            return failAfterPartialLoad();
        }
    } else {
        for (uint8_t t = 0; t < numTracks; ++t) {
            selectedSlotIndex[t] = activeLoopIndex[t];
        }
    }
    if (footerToken != kGlobalUndoStackToken) {
        Serial.println("[StorageManager] ERROR: Global undo stack token mismatch for legacy monolith");
        return failAfterPartialLoad();
    }
    for (uint8_t t = 0; t < numTracks; ++t) {
        Track& track = trackManager.getTrack(t);
        if (!StorageManagerInternal::readGlobalUndoStackFromFile(file, track.getGlobalUndoStack())) {
            Serial.print("[StorageManager] ERROR: Failed to read global undo stack for track ");
            Serial.println(t);
            return failAfterPartialLoad();
        }
        TrackUndo::rebuildTrackFromLoopContent(track, selectedSlotIndex[t]);
    }

    uint32_t svokToken = 0;
    if (!StorageManagerInternal::readRaw(file, &svokToken, sizeof(svokToken))) {
        Serial.println(
            "[StorageManager] ERROR: Failed to read storage completion marker for legacy monolith");
        return failAfterPartialLoad();
    }
    if (svokToken != CurrentSetStorage::kSaveFileToken) {
        Serial.println("[StorageManager] ERROR: Storage completion marker mismatch for legacy monolith");
        return failAfterPartialLoad();
    }

    file.close();
    Serial.println("[StorageManager] Legacy monolith state loaded successfully (v5).");

    return StorageManagerInternal::applyLoadedTransportFooter(
        numTracks, activeLoopIndex, selectedSlotIndex, selectedTrackIdx, state, loadedLooperState,
        masterLoopLength);
}

bool StorageManager::migrateV5MonolithToCurrentSet(LooperState& state) {
    Serial.println("[StorageManager] Migrating v5 monolith to CurrentSet (deferred SD write)...");
    if (!loadV5MonolithIntoRam(state)) {
        return false;
    }
    StorageManagerInternal::currentSetAnchorFields = {};
    StorageManagerInternal::quarantineLegacyMonolithAfterSave = true;
    StorageManagerInternal::forceCurrentSetFullLoopWrite = true;
    StorageManagerInternal::markAllCurrentSetLoopSlotsDirtyForBootRecovery();
    requestDeferredSaveState(state);
    Serial.println("[StorageManager] v5 state loaded to RAM; CurrentSet write queued.");
    return true;
}
