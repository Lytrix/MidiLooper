//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// Blocking persistence drain for StorageManager::saveState.

#include "StorageManager.h"
#include "StorageManagerInternal.h"

#include "PersistenceSyncDrainBudget.h"
#include "Utils/DebugSessionCapture.h"
#include "Utils/HotPathTelemetry.h"
#include <Arduino.h>

namespace StorageManagerInternal {

bool drainPersistenceWorkBlocking(const LooperState& state) {
#if BYPASS_STOP_UNDO_SAVE
    (void)state;
    Serial.println("[StorageManager] BYPASS_STOP_UNDO_SAVE: skip saveState");
    return true;
#else
    HotPathTelemetry::ScopedSaveState telemetryScope;
    Serial.println("[StorageManager] Draining persistence work queue to SD card...");

    storageSession.currentWorkspaceSave.admissionHeap = UINT32_MAX;
    storageSession.currentWorkspaceSave.urgentRequested = true;
    storageSession.currentWorkspaceSave.deferDispatchUntilMs = 0;
#if defined(SESSION_CAPTURE)
    const bool alreadyPending = storageSession.currentWorkspaceSave.pending;
#endif
    storageSession.currentWorkspaceSave.pending = true;
#if defined(SESSION_CAPTURE)
    SC_PERSIST("request", 0, 0, 0,
               alreadyPending ? "sync_drain_already_pending" : "sync_drain");
#endif

    storageSession.currentWorkspaceSave.lastCompletedOk = false;

    const SyncDrainBudget drainBudget = buildSyncDrainBudgetForSession();
    uint32_t steps = 0;
    uint32_t stuckIterations = 0;
    SyncDrainProgressSnapshot lastProgress = captureSyncDrainProgressSnapshot();
    SyncDrainFailureReason failureReason = SyncDrainFailureReason::None;

    while (hasPersistenceWorkPending()) {
        if (steps >= drainBudget.maxSliceSteps) {
            failureReason = SyncDrainFailureReason::ExceededSliceBudget;
            break;
        }

        maybeAdmitFinalizeWorkspaceAfterDrain();
        const SyncDrainProgressSnapshot beforeProgress = lastProgress;
        StorageManager::processDeferredSaveState(state);
        yield();
        steps++;

        const SyncDrainProgressSnapshot afterProgress = captureSyncDrainProgressSnapshot();
        if (PersistenceSyncDrainBudget::madeSyncDrainProgress(beforeProgress, afterProgress)) {
            stuckIterations = 0;
            lastProgress = afterProgress;
        } else {
            ++stuckIterations;
            if (stuckIterations >= drainBudget.maxStuckIterations) {
                failureReason = SyncDrainFailureReason::Stuck;
                break;
            }
        }
    }

    const bool completed = !hasPersistenceWorkPending();
    if (!completed) {
#if defined(SESSION_CAPTURE)
        const SyncDrainProgressSnapshot progress = captureSyncDrainProgressSnapshot();
#endif
        if (failureReason == SyncDrainFailureReason::Stuck) {
            Serial.printf(
                "[StorageManager] ERROR: Persistence drain stuck after %u iterations "
                "(max %u, expected ~%u slice steps, ~%u SD bytes)\n",
                static_cast<unsigned>(stuckIterations),
                static_cast<unsigned>(drainBudget.maxStuckIterations),
                static_cast<unsigned>(drainBudget.expectedSliceSteps),
                static_cast<unsigned>(drainBudget.estimatedSdPayloadBytes));
#if defined(SESSION_CAPTURE)
            SC_PERSIST_DRAIN_FAIL("stuck", steps, stuckIterations, progress.workQueueDepth,
                                  progress.chunkQueueDepth, drainBudget.expectedSliceSteps,
                                  drainBudget.estimatedSdPayloadBytes);
#endif
        } else {
            Serial.printf(
                "[StorageManager] ERROR: Persistence drain exceeded slice budget after %u steps "
                "(max %u, expected ~%u slice steps, ~%u SD bytes)\n",
                static_cast<unsigned>(steps), static_cast<unsigned>(drainBudget.maxSliceSteps),
                static_cast<unsigned>(drainBudget.expectedSliceSteps),
                static_cast<unsigned>(drainBudget.estimatedSdPayloadBytes));
#if defined(SESSION_CAPTURE)
            SC_PERSIST_DRAIN_FAIL("budget", steps, stuckIterations, progress.workQueueDepth,
                                  progress.chunkQueueDepth, drainBudget.expectedSliceSteps,
                                  drainBudget.estimatedSdPayloadBytes);
#endif
        }
        return false;
    }
    if (storageSession.currentWorkspaceSave.pending) {
        Serial.println("[StorageManager] ERROR: Persistence drain left flush pending");
#if defined(SESSION_CAPTURE)
        const SyncDrainProgressSnapshot progress = captureSyncDrainProgressSnapshot();
        SC_PERSIST_DRAIN_FAIL("flush_pending", steps, stuckIterations, progress.workQueueDepth,
                              progress.chunkQueueDepth, drainBudget.expectedSliceSteps,
                              drainBudget.estimatedSdPayloadBytes);
#endif
        return false;
    }
    if (!storageSession.currentWorkspaceSave.lastCompletedOk) {
        Serial.println("[StorageManager] ERROR: Persistence drain failed");
#if defined(SESSION_CAPTURE)
        const SyncDrainProgressSnapshot progress = captureSyncDrainProgressSnapshot();
        SC_PERSIST_DRAIN_FAIL("failed", steps, stuckIterations, progress.workQueueDepth,
                              progress.chunkQueueDepth, drainBudget.expectedSliceSteps,
                              drainBudget.estimatedSdPayloadBytes);
#endif
        return false;
    }
    Serial.println("[StorageManager] State saved successfully (v4).");
    telemetryScope.setOk(true);
    return true;
#endif
}

}  // namespace StorageManagerInternal
