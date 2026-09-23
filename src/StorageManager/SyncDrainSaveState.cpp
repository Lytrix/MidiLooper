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

        // Terminal when a save is owed but no work item can carry it: the queue cannot admit the
        // finalize, so `hasPersistenceWorkPending()` is true on `pending` alone and no iteration can
        // change that. Fail fast with the cause instead of burning the whole stuck budget -- the
        // 2026-09-18 wedge reported `stuck after 33 iterations (max 33, expected ~1 slice steps,
        // ~0 SD bytes)` and refused CLEAR with no clue which of the two states it was.
        if (!maybeAdmitFinalizeWorkspaceAfterDrain()) {
            failureReason = SyncDrainFailureReason::AdmissionRefused;
            break;
        }
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
                // Name the gate that stopped the drain, in the same order `processDeferredSaveState`
                // checks them, so a blocking CLEAR reports *why* instead of the generic "stuck"
                // (2026-09-23 device session: the gate could not be told apart from any log).
                if (StorageManagerInternal::isCaptureRecordingForPersistence()) {
                    failureReason = SyncDrainFailureReason::BlockedByCapture;
                } else if (StorageManagerInternal::deferredSaveBlockedByActiveSlotLoadSd()) {
                    failureReason = SyncDrainFailureReason::BlockedBySlotLoad;
                } else if (StorageManagerInternal::deferredSaveBlockedByPostLoadCommitHoldoff()) {
                    failureReason = SyncDrainFailureReason::BlockedByPostLoadHoldoff;
                } else {
                    failureReason = SyncDrainFailureReason::Stuck;
                }
                break;
            }
        }
    }

    const bool completed = !hasPersistenceWorkPending();
    if (!completed) {
#if defined(SESSION_CAPTURE)
        const SyncDrainProgressSnapshot progress = captureSyncDrainProgressSnapshot();
#endif
        if (failureReason == SyncDrainFailureReason::AdmissionRefused) {
            const SyncDrainProgressSnapshot admissionProgress = captureSyncDrainProgressSnapshot();
            // PROGMEM: a plain literal here is RAM1-resident `.rodata`, and this tree sits on the
            // FlexRAM bank boundary.
            Serial.printf_P(
                PSTR("[StorageManager] ERROR: Persistence drain has no admittable work "
                     "(pending=%u queue=%u writing=%u active=%u steps=%u)\n"),
                storageSession.currentWorkspaceSave.pending ? 1u : 0u,
                static_cast<unsigned>(admissionProgress.workQueueDepth),
                static_cast<unsigned>(admissionProgress.writingWorkItems),
                admissionProgress.workItemActive ? 1u : 0u, static_cast<unsigned>(steps));
#if defined(SESSION_CAPTURE)
            SC_PERSIST_DRAIN_FAIL("admission_refused", steps, 0, admissionProgress.workQueueDepth,
                                  admissionProgress.chunkQueueDepth, drainBudget.expectedSliceSteps,
                                  drainBudget.estimatedSdPayloadBytes);
#endif
        } else if (failureReason == SyncDrainFailureReason::BlockedByCapture ||
                   failureReason == SyncDrainFailureReason::BlockedBySlotLoad ||
                   failureReason == SyncDrainFailureReason::BlockedByPostLoadHoldoff) {
            const char* gate = failureReason == SyncDrainFailureReason::BlockedByCapture
                                   ? "transport_active"
                                   : (failureReason == SyncDrainFailureReason::BlockedBySlotLoad
                                          ? "slot_load_active"
                                          : "post_load_holdoff");
            Serial.printf(
                "[StorageManager] ERROR: Persistence drain blocked by %s after %u iterations "
                "(max %u, expected ~%u slice steps, ~%u SD bytes)\n",
                gate, static_cast<unsigned>(stuckIterations),
                static_cast<unsigned>(drainBudget.maxStuckIterations),
                static_cast<unsigned>(drainBudget.expectedSliceSteps),
                static_cast<unsigned>(drainBudget.estimatedSdPayloadBytes));
#if defined(SESSION_CAPTURE)
            SC_PERSIST_DRAIN_FAIL(gate, steps, stuckIterations, progress.workQueueDepth,
                                  progress.chunkQueueDepth, drainBudget.expectedSliceSteps,
                                  drainBudget.estimatedSdPayloadBytes);
#endif
        } else if (failureReason == SyncDrainFailureReason::Stuck) {
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
