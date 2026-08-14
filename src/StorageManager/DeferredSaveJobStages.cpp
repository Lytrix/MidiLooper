//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "StorageManagerInternal.h"

#include "GlobalUndoStack.h"
#include "Globals.h"
#include "Loop.h"
#include "RtcTime.h"
#include "StorageManager.h"
#include "TrackManager.h"
#include <Arduino.h>

namespace StorageManagerInternal {

STORAGE_PERSIST_MEM bool stepDeferredSaveJobCurrentSetMeta() {
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
                    if (storageSession.persistenceWorkItem.bundleWriteActive &&
                        storageSession.persistenceWorkItem.item.type == PersistWorkType::GlobalMeta) {
                        if (!closeDeferredMetaTempForLoopWrites()) {
                            return false;
                        }
                        storageSession.currentWorkspaceSave.stage = DeferredSaveStage::Idle;
                        return true;
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

}

STORAGE_PERSIST_MEM bool stepDeferredSaveJobTrackHeaderAndSlots() {

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

            if (storageSession.persistenceWorkItem.skipLoopSlotStage ||
                !trackHasCurrentSetDirtyLoopSlot(storageSession.currentWorkspaceSave.trackCursor)) {
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

STORAGE_PERSIST_MEM bool stepDeferredSaveJobCurrentSetLoopSlot() {

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
            clearCurrentSetLoopSlotDirty(trackIndex, slotIndex);

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

STORAGE_PERSIST_MEM bool stepDeferredSaveJobFooter() {

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
                    storageSession.currentWorkspaceSave.footerWriteStage = DeferredFooterWriteStage::SelectedSlotExtensionToken;
                    return true;

                case DeferredFooterWriteStage::SelectedSlotExtensionToken:
                    if (!writeRaw(storageSession.currentWorkspaceSave.file,
                                  &kFooterSelectedSlotExtensionToken,
                                  sizeof(kFooterSelectedSlotExtensionToken))) {
                        Serial.println("[StorageManager] ERROR: Deferred save failed writing selected slot extension token");
                        return false;
                    }
                    storageSession.currentWorkspaceSave.footerTrackCursor = 0;
                    storageSession.currentWorkspaceSave.footerWriteStage =
                        DeferredFooterWriteStage::SelectedSlotIndex;
                    return true;

                case DeferredFooterWriteStage::SelectedSlotIndex:
                    if (storageSession.currentWorkspaceSave.footerTrackCursor <
                        storageSession.currentWorkspaceSave.numTracks) {
                        const uint8_t selectedIdx = trackManager.getSelectedSlotIndex(
                            storageSession.currentWorkspaceSave.footerTrackCursor);
                        if (!writeRaw(storageSession.currentWorkspaceSave.file, &selectedIdx,
                                      sizeof(selectedIdx))) {
                            Serial.print("[StorageManager] ERROR: Deferred save failed writing selectedSlotIndex for track ");
                            Serial.println(storageSession.currentWorkspaceSave.footerTrackCursor);
                            return false;
                        }
                        ++storageSession.currentWorkspaceSave.footerTrackCursor;
                        return true;
                    }
                    storageSession.currentWorkspaceSave.footerWriteStage =
                        DeferredFooterWriteStage::GlobalUndoStackToken;
                    return true;

                case DeferredFooterWriteStage::GlobalUndoStackToken:
                    if (!writeRaw(storageSession.currentWorkspaceSave.file, &kGlobalUndoStackToken,
                                  sizeof(kGlobalUndoStackToken))) {
                        Serial.println("[StorageManager] ERROR: Deferred save failed writing global undo stack token");
                        return false;
                    }
                    // Stage 3: persist empty GUS headers only. Load fills GUS from Loop content.
                    {
                        GlobalUndoStack emptyUndoStack{};
                        emptyUndoStack.clear();
                        for (uint8_t trackIndex = 0;
                             trackIndex < storageSession.currentWorkspaceSave.numTracks;
                             ++trackIndex) {
                            if (!writeGlobalUndoStackToFile(
                                    storageSession.currentWorkspaceSave.file, emptyUndoStack)) {
                                Serial.println(
                                    "[StorageManager] ERROR: Deferred save failed writing empty undo stack");
                                return false;
                            }
                        }
                    }
                    if (!finalizeDeferredMetaTempFile()) {
                        Serial.println("[StorageManager] ERROR: Deferred save failed finalizing CurrentSet meta");
                        return false;
                    }
                    if (storageSession.persistenceWorkItem.bundleWriteActive) {
                        storageSession.currentWorkspaceSave.stage = DeferredSaveStage::Idle;
                        return true;
                    }
                    storageSession.currentWorkspaceSave.stage = DeferredSaveStage::CurrentSetCompletion;
                    return true;
            }
            return false;
        
}

STORAGE_PERSIST_MEM bool stepDeferredSaveJobUndoStacks() {
            // Stage 3: in-flight saves that still enter this stage write empty headers only.
            if (storageSession.currentWorkspaceSave.undoTrackCursor <
                storageSession.currentWorkspaceSave.numTracks) {
                GlobalUndoStack emptyUndoStack{};
                emptyUndoStack.clear();
                if (!writeGlobalUndoStackToFile(storageSession.currentWorkspaceSave.file,
                                                emptyUndoStack)) {
                    Serial.print("[StorageManager] ERROR: Deferred save failed writing empty undo stack for track ");
                    Serial.println(storageSession.currentWorkspaceSave.undoTrackCursor);
                    return false;
                }
                storageSession.currentWorkspaceSave.undoTrackCursor++;
                resetDeferredUndoWriteState();
                return true;
            }

            if (!finalizeDeferredMetaTempFile()) {
                Serial.println("[StorageManager] ERROR: Deferred save failed finalizing CurrentSet meta");
                return false;
            }
            if (storageSession.persistenceWorkItem.bundleWriteActive) {
                storageSession.currentWorkspaceSave.stage = DeferredSaveStage::Idle;
                return true;
            }
            storageSession.currentWorkspaceSave.stage = DeferredSaveStage::CurrentSetCompletion;
            return true;
        
}

STORAGE_PERSIST_MEM bool stepDeferredSaveJobCurrentSetCompletion() {

            const uint32_t lastActiveUnix = RtcTime::getUnixTime();
            if (!CurrentSetStorage::patchLastActiveUnix(CurrentSetStorage::kCurrentMetaPath,
                                                        lastActiveUnix)) {
                Serial.println("[StorageManager] ERROR: Deferred save failed patching lastActiveUnix");
                return false;
            }
            if (!CurrentWorkspaceStorage::finalizeEpochFileHeaderCrc(
                    CurrentSetStorage::kCurrentMetaPath)) {
                Serial.println("[StorageManager] ERROR: Deferred save failed refreshing runtime bundle epoch CRC");
                return false;
            }
            currentSetLastActiveUnix = lastActiveUnix;
            if (!writeWorkspaceMetaAfterDeferredSave()) {
                Serial.println("[StorageManager] ERROR: Deferred save failed writing workspace.bin");
                return false;
            }
            if (quarantineLegacyMonolithAfterSave) {
                quarantineLegacyMonolithStorageFile();
                quarantineLegacyMonolithAfterSave = false;
                Serial.println("[StorageManager] v5 monolith quarantined after CurrentSet save.");
            }
            forceCurrentSetFullLoopWrite = false;
            Serial.println("[StorageManager] CurrentSet saved successfully (v6 deferred slices).");
            storageSession.currentWorkspaceSave.inProgress = false;
            storageSession.currentWorkspaceSave.stage = DeferredSaveStage::Idle;
            return true;
        
}

}  // namespace StorageManagerInternal
