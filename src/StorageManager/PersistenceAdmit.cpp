//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// Persistence work-queue admission, current-set dirty tracking, and save dispatch requests.

#include "StorageManager.h"
#include "StorageManagerInternal.h"
#include "StorageManagerInternal/PersistenceWorkQueue.h"

#include "EditManager.h"
#include "Globals.h"
#include "Loop.h"
#include "PersistenceQueue.h"
#include "RtcTime.h"
#include "TrackManager.h"
#include "Utils/DebugSessionCapture.h"
#include "Utils/PersistenceDiagnostics.h"
#include <Arduino.h>
#include <cstdio>

using namespace StorageManagerInternal;

namespace StorageManagerInternal {

bool workspaceFooterPersistDeferred = false;

void markCurrentSetMaterialChange() {
    currentSetAnchorFields.hasMaterialChangesSinceAnchor = 1;
    const uint32_t nowUnix = RtcTime::getUnixTime();
    if (nowUnix != 0) {
        currentSetAnchorFields.lastMaterialChangeUnix = nowUnix;
    }
}

void markCurrentSetLoopSlotDirtyInternal(uint8_t trackIndex, uint8_t slotIndex,
                                         bool markMaterialChange) {
    if (trackIndex >= Config::NUM_TRACKS || slotIndex >= Config::MAX_LOOPS_PER_TRACK) {
        return;
    }
    currentSetLoopSlotDirty[trackIndex][slotIndex] = true;
    if (markMaterialChange) {
        markCurrentSetMaterialChange();
    }
}

void markCurrentSetTrackDirtyInternal(uint8_t trackIndex, bool markMaterialChange) {
    if (trackIndex >= Config::NUM_TRACKS) {
        return;
    }
    for (uint8_t slot = 0; slot < Config::MAX_LOOPS_PER_TRACK; ++slot) {
        currentSetLoopSlotDirty[trackIndex][slot] = true;
    }
    if (markMaterialChange) {
        markCurrentSetMaterialChange();
    }
}

void markAllCurrentSetLoopSlotsDirtyInternal(bool markMaterialChange) {
    for (uint8_t track = 0; track < Config::NUM_TRACKS; ++track) {
        markCurrentSetTrackDirtyInternal(track, false);
    }
    if (markMaterialChange) {
        markCurrentSetMaterialChange();
    }
}

void clearCurrentSetLoopSlotDirtyInternal(uint8_t trackIndex, uint8_t slotIndex) {
    if (trackIndex >= Config::NUM_TRACKS || slotIndex >= Config::MAX_LOOPS_PER_TRACK) {
        return;
    }
    currentSetLoopSlotDirty[trackIndex][slotIndex] = false;
}

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

void markAllCurrentSetLoopSlotsDirtyForBootRecovery() {
    markAllCurrentSetLoopSlotsDirtyInternal(false);
}

}  // namespace StorageManagerInternal

void StorageManager::requestUrgentEditSave() {
#if BYPASS_STOP_UNDO_SAVE
    return;
#endif
    urgentEditSavePending = true;
}

void StorageManager::admitLoopPersist(LoopId loopId) {
#if BYPASS_STOP_UNDO_SAVE
    (void)loopId;
    return;
#else
    if (loopId == kInvalidLoopId) {
        return;
    }
    PersistenceWorkQueue::admitWork(PersistWorkType::LoopPersist, persistKeyForLoop(loopId));
#endif
}

void StorageManager::admitLoopUndoHistory(uint8_t trackIndex, uint8_t slotIndex) {
#if BYPASS_STOP_UNDO_SAVE
    (void)trackIndex;
    (void)slotIndex;
    return;
#else
    if (trackIndex >= Config::NUM_TRACKS || slotIndex >= Config::MAX_LOOPS_PER_TRACK) {
        return;
    }
    PersistenceWorkQueue::admitWork(PersistWorkType::LoopUndoHistory,
                                    persistKeyForSlot(trackIndex, slotIndex));
#endif
}

void StorageManager::admitSlotMeta(uint8_t trackIndex, uint8_t slotIndex) {
#if BYPASS_STOP_UNDO_SAVE
    (void)trackIndex;
    (void)slotIndex;
    return;
#else
    if (trackIndex >= Config::NUM_TRACKS || slotIndex >= Config::MAX_LOOPS_PER_TRACK) {
        return;
    }
    PersistenceWorkQueue::admitWork(PersistWorkType::SlotMeta,
                                  persistKeyForSlot(trackIndex, slotIndex));
#endif
}

void StorageManager::admitTrackMeta(uint8_t trackIndex) {
#if BYPASS_STOP_UNDO_SAVE
    (void)trackIndex;
    return;
#else
    if (trackIndex >= Config::NUM_TRACKS) {
        return;
    }
    PersistenceWorkQueue::admitWork(PersistWorkType::TrackMeta, persistKeyForTrack(trackIndex));
#endif
}

void StorageManager::admitWorkspaceFooter() {
#if BYPASS_STOP_UNDO_SAVE
    return;
#else
    StorageManagerInternal::workspaceFooterPersistDeferred = false;
    PersistenceWorkQueue::admitWork(PersistWorkType::WorkspaceFooter, persistKeySingleton());
#endif
}

void StorageManager::requestWorkspaceFooterPersistWhenSafe() {
#if BYPASS_STOP_UNDO_SAVE
    return;
#else
    StorageManagerInternal::workspaceFooterPersistDeferred = true;
#endif
}

void StorageManager::admitGlobalMeta() {
#if BYPASS_STOP_UNDO_SAVE
    return;
#else
    StorageManagerInternal::workspaceFooterPersistDeferred = false;
    PersistenceWorkQueue::admitWork(PersistWorkType::GlobalMeta, persistKeySingleton());
#endif
}

void StorageManager::markLoopSlotMaterialDirty(uint8_t trackIndex, uint8_t slotIndex) {
#if BYPASS_STOP_UNDO_SAVE
    (void)trackIndex;
    (void)slotIndex;
    return;
#endif
    StorageManagerInternal::markCurrentSetLoopSlotDirtyInternal(trackIndex, slotIndex);
}

void StorageManager::markTrackSlotsMaterialDirty(uint8_t trackIndex) {
#if BYPASS_STOP_UNDO_SAVE
    (void)trackIndex;
    return;
#endif
    StorageManagerInternal::markCurrentSetTrackDirtyInternal(trackIndex);
}

void StorageManager::admitLoopSlotPersist(uint8_t trackIndex, uint8_t slotIndex) {
#if BYPASS_STOP_UNDO_SAVE
    (void)trackIndex;
    (void)slotIndex;
    return;
#else
    admitSlotMeta(trackIndex, slotIndex);
    PersistenceWorkQueue::admitWork(PersistWorkType::LoopPersist,
                                    persistKeyForSlot(trackIndex, slotIndex));
#endif
}

void StorageManager::admitTrackSlotPersistence(uint8_t trackIndex) {
#if BYPASS_STOP_UNDO_SAVE
    (void)trackIndex;
    return;
#else
    admitTrackMeta(trackIndex);
    for (uint8_t slot = 0; slot < Config::MAX_LOOPS_PER_TRACK; ++slot) {
        admitLoopSlotPersist(trackIndex, slot);
    }
#endif
}

void StorageManager::markCurrentSetLoopSlotDirty(uint8_t trackIndex, uint8_t slotIndex) {
#if BYPASS_STOP_UNDO_SAVE
    (void)trackIndex;
    (void)slotIndex;
    return;
#endif
    markLoopSlotMaterialDirty(trackIndex, slotIndex);
    admitLoopSlotPersist(trackIndex, slotIndex);
}

void StorageManager::markCurrentSetTrackDirty(uint8_t trackIndex) {
#if BYPASS_STOP_UNDO_SAVE
    (void)trackIndex;
    return;
#endif
    markTrackSlotsMaterialDirty(trackIndex);
    admitTrackSlotPersistence(trackIndex);
}

void StorageManager::markAllCurrentSetLoopSlotsDirty() {
#if BYPASS_STOP_UNDO_SAVE
    return;
#endif
    StorageManagerInternal::markAllCurrentSetLoopSlotsDirtyInternal();
    for (uint8_t track = 0; track < Config::NUM_TRACKS; ++track) {
        admitTrackSlotPersistence(track);
    }
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
        if (editManager.isNoteEditActive()) {
            editManager.markCurrentEditBatchDurable(trackManager.getSelectedTrack());
        }
        for (uint8_t t = 0; t < trackManager.getTrackCount(); ++t) {
            Track& track = trackManager.getTrack(t);
            if (!track.loopsAllocated()) {
                continue;
            }
            for (uint8_t s = 0; s < Config::MAX_LOOPS_PER_TRACK; ++s) {
                if (track.getLoop(s).isEditStateDirty()) {
                    const uint8_t trackIndex = t;
                    const uint8_t slotIndex = s;
                    StorageManager::markLoopSlotMaterialDirty(trackIndex, slotIndex);
                    StorageManager::admitLoopSlotPersist(trackIndex, slotIndex);
                }
            }
        }
        requestDeferredSaveState(state, UINT32_MAX, true);
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
    const bool anyDirty = StorageManagerInternal::anyAllocatedLoopEditStateDirty();
    if (!anyDirty || captureActive) {
        return;
    }
    if (nowMs - lastEditAutosaveMs < Config::autosaveIntervalMs) {
        return;
    }
    clearEditDirtyAfterDeferredSave = true;
    if (editManager.isNoteEditActive()) {
        editManager.markCurrentEditBatchDurable(trackManager.getSelectedTrack());
    }
    for (uint8_t t = 0; t < trackManager.getTrackCount(); ++t) {
        Track& track = trackManager.getTrack(t);
        if (!track.loopsAllocated()) {
            continue;
        }
        for (uint8_t s = 0; s < Config::MAX_LOOPS_PER_TRACK; ++s) {
            if (track.getLoop(s).isEditStateDirty()) {
                const uint8_t trackIndex = t;
                const uint8_t slotIndex = s;
                StorageManager::markLoopSlotMaterialDirty(trackIndex, slotIndex);
                StorageManager::admitLoopSlotPersist(trackIndex, slotIndex);
            }
        }
    }
    requestDeferredSaveState(state);
    lastEditAutosaveMs = nowMs;
}

void StorageManager::deferWorkspaceSaveDispatchDuringPlayback(uint32_t graceMs) {
#if BYPASS_STOP_UNDO_SAVE
    (void)graceMs;
    return;
#else
    const uint32_t untilMs = millis() + graceMs;
    if (untilMs > storageSession.currentWorkspaceSave.deferDispatchUntilMs) {
        storageSession.currentWorkspaceSave.deferDispatchUntilMs = untilMs;
    }
#if defined(SESSION_CAPTURE)
    char outcome[24];
    std::snprintf(outcome, sizeof(outcome), "until_%lu", static_cast<unsigned long>(untilMs));
    SC_PERSIST("defer_playback", 0, 0, 0, outcome);
#endif
#endif
}

void StorageManager::requestDeferredSaveState(const LooperState& /*state*/, uint32_t admissionHeap,
                                              bool isUrgentRequest) {
#if BYPASS_STOP_UNDO_SAVE
    (void)isUrgentRequest;
    return;
#endif
    if (isUrgentRequest) {
        storageSession.currentWorkspaceSave.deferDispatchUntilMs = 0;
    }
    const bool alreadyPending = storageSession.currentWorkspaceSave.pending;
    if (admissionHeap != UINT32_MAX || storageSession.currentWorkspaceSave.admissionHeap == 0) {
        storageSession.currentWorkspaceSave.admissionHeap = admissionHeap;
    }
    const uint32_t reportedHeap = storageSession.currentWorkspaceSave.admissionHeap == UINT32_MAX
                                      ? 0
                                      : storageSession.currentWorkspaceSave.admissionHeap;
    storageSession.currentWorkspaceSave.urgentRequested =
        storageSession.currentWorkspaceSave.urgentRequested || isUrgentRequest;
    storageSession.currentWorkspaceSave.pending = true;
    if (!alreadyPending) {
        PersistenceDiagnostics::onDeferredSaveRequested();
    }
    SC_PERSIST("request", 0, reportedHeap, reportedHeap,
               alreadyPending ? "already_pending" : "queued");
}
