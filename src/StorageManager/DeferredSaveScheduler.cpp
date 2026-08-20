//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "StorageManager.h"
#include "StorageManagerInternal.h"

#include "Globals.h"
#include "LoopEventStore.h"
#include "PersistenceBudget.h"
#include "PersistenceFailurePolicy.h"
#include "PersistenceQueue.h"
#include "RevisionLoadPolicy.h"
#include "SlotLoadSession.h"
#include "Utils/DebugSessionCapture.h"
#include "Utils/PersistenceDiagnostics.h"
#include "Utils/MemoryMonitor.h"
#include <Arduino.h>

using namespace StorageManagerInternal;

void StorageManager::processDeferredSaveState(const LooperState& state) {
#if BYPASS_STOP_UNDO_SAVE
    (void)state;
    storageSession.currentWorkspaceSave.pending = false;
    resetDeferredSaveJobState();
    resetPersistenceWorkItemJobState();
    resetRevisionCommitJobState();
    resetRevisionLoadJobState();
    return;
#endif
    // LoadLoopJob owns the SD session through Parsing/Committing (file may be closed
    // between grains). Catalog sync / revision / work-item must not run until Commit
    // finishes — overlapping SD during parse hung after edits grain (235129).
    if (deferredSaveBlockedByActiveSlotLoadSd()) {
        return;
    }
    // Same-frame after Commit deletes SlotLoadSession — save must still wait for prewarm
    // (000659: hang after commit_prewarm_q before prewarm_enter).
    if (deferredSaveBlockedByPostLoadCommitHoldoff()) {
        return;
    }
    storageSession.currentWorkspaceSave.sdIoActive = false;
    storageSession.revisionCommit.sdIoActive = false;
    storageSession.revisionLoad.sdIoActive = false;

    const bool captureActiveForScheduler = isCaptureActiveForPersistence();
    const uint32_t sliceBudgetUs = resolvePersistenceSliceBudgetUs(state);
    const uint32_t sliceStartUs = micros();

    auto sliceBudgetExhausted = [&]() {
        return PersistenceBudget::persistenceSliceBudgetExhausted(sliceBudgetUs,
                                                                  micros() - sliceStartUs);
    };

    if (storageSession.bootRecovery.pending && !storageSession.revisionLoad.pending && !storageSession.revisionLoad.inProgress &&
        !storageSession.revisionCommit.pending && !storageSession.revisionCommit.inProgress && !hasPersistenceWorkPending()) {
        storageSession.bootRecovery.pending = false;
        storageSession.revisionLoad.setId = storageSession.bootRecovery.setId;
        storageSession.revisionLoad.revisionId = storageSession.bootRecovery.revisionId;
        storageSession.revisionLoad.pending = true;
        Serial.print("[StorageManager] Boot recovery: queued revision load S");
        Serial.print(storageSession.revisionLoad.setId);
        Serial.print(" v");
        Serial.println(storageSession.revisionLoad.revisionId);
    }

    stepWallClockFromSdCatalogSync(2);

    while (!sliceBudgetExhausted()) {
        maybeAdmitDeferredWorkspaceFooter();

        const bool revisionBlockedByDeferredSave =
            storageSession.revisionCommit.pending && hasPersistenceWorkPending();
        const bool revisionBlockedByLoad =
            storageSession.revisionCommit.pending && (storageSession.revisionLoad.pending || storageSession.revisionLoad.inProgress);
        if (revisionBlockedByDeferredSave || revisionBlockedByLoad) {
            const uint32_t nowMs = millis();
            if (nowMs - storageSession.revisionCommit.lastBlockedLogAtMs >= 5000U) {
                storageSession.revisionCommit.lastBlockedLogAtMs = nowMs;
                SC_PERSIST("rev_blocked", 0, storageSession.currentWorkspaceSave.pending ? 1U : 0U,
                           hasPersistenceWorkPending() ? 1U : 0U,
                           revisionBlockedByLoad ? "load_active" : "deferred_save_active");
            }
        } else if (storageSession.revisionCommit.inProgress || storageSession.revisionCommit.pending) {
            if (!storageSession.revisionCommit.inProgress && storageSession.revisionCommit.pending) {
                storageSession.revisionCommit.pending = false;
                storageSession.revisionCommit.inProgress = true;
                storageSession.revisionCommit.stage = RevisionCommitStage::Snapshot;
                SC_PERSIST("rev_dispatch", 0, 0, 0, "run");
            }

            const uint32_t ioStartUs = micros();
            storageSession.revisionCommit.sdIoActive = true;
            const bool revStepOk = stepRevisionCommitJob();
            storageSession.revisionCommit.sdIoActive = false;
            storageSession.currentWorkspaceSave.displayBlockUs += micros() - ioStartUs;

            if (!revStepOk) {
                Serial.print("[StorageManager] ERROR: Revision commit failed at stage ");
                Serial.println(static_cast<uint8_t>(storageSession.revisionCommit.stage));
                resetRevisionCommitJobState();
                break;
            }
            if (storageSession.revisionCommit.inProgress) {
                break;
            }
            if (RevisionLoadPolicy::shouldDispatchRequestedLoadAfterCommitComplete(
                    storageSession.revisionLoad.loadAfterRevisionCommit, true)) {
                storageSession.revisionLoad.loadAfterRevisionCommit = false;
                dispatchRequestedRevisionLoad();
            }
            if (captureActiveForScheduler) {
                break;
            }
            continue;
        }

        const bool revisionLoadBlockedByDeferredSave =
            storageSession.revisionLoad.pending && hasPersistenceWorkPending();
        const bool revisionLoadBlockedByCommit =
            storageSession.revisionLoad.pending && (storageSession.revisionCommit.pending || storageSession.revisionCommit.inProgress);
        if (revisionLoadBlockedByDeferredSave || revisionLoadBlockedByCommit) {
            const uint32_t nowMs = millis();
            if (nowMs - storageSession.revisionLoad.lastBlockedLogAtMs >= 5000U) {
                storageSession.revisionLoad.lastBlockedLogAtMs = nowMs;
                SC_PERSIST("rev_load_blocked", 0,
                           revisionLoadBlockedByDeferredSave ? 1U : 0U,
                           revisionLoadBlockedByCommit ? 1U : 0U,
                           revisionLoadBlockedByDeferredSave ? "deferred_save_active"
                                                               : "commit_active");
            }
        } else if (storageSession.revisionLoad.inProgress || storageSession.revisionLoad.pending) {
            if (!storageSession.revisionLoad.inProgress && storageSession.revisionLoad.pending) {
                storageSession.revisionLoad.pending = false;
                storageSession.revisionLoad.inProgress = true;
                storageSession.revisionLoad.stage = RevisionLoadStage::Validate;
                SC_PERSIST("rev_load_dispatch", 0, storageSession.revisionLoad.setId, storageSession.revisionLoad.revisionId,
                           "run");
            }

            const uint32_t ioStartUs = micros();
            storageSession.revisionLoad.sdIoActive = true;
            const bool revLoadStepOk =
                stepRevisionLoadJob(const_cast<LooperState&>(state));
            storageSession.revisionLoad.sdIoActive = false;
            storageSession.currentWorkspaceSave.displayBlockUs += micros() - ioStartUs;

            if (!revLoadStepOk) {
                Serial.print("[StorageManager] ERROR: Revision load failed at stage ");
                Serial.println(static_cast<uint8_t>(storageSession.revisionLoad.stage));
                storageSession.revisionLoad.lastDisplaySetId =
                    storageSession.revisionLoad.setId != 0 ? storageSession.revisionLoad.setId : storageSession.revisionLoad.requestedSetId;
                storageSession.revisionLoad.lastDisplayRevisionId = storageSession.revisionLoad.revisionId != 0
                                                          ? storageSession.revisionLoad.revisionId
                                                          : storageSession.revisionLoad.requestedRevisionId;
                storageSession.revisionLoad.failedAtMs = millis();
                storageSession.revisionLoad.completedAtMs = 0;
                resetRevisionLoadJobState();
                break;
            }
            if (storageSession.revisionLoad.inProgress) {
                break;
            }
            if (captureActiveForScheduler) {
                break;
            }
            continue;
        }

        if (storageSession.currentWorkspaceSave.pending) {
            maybeAdmitFinalizeWorkspaceAfterDrain();
        }

        const bool otherSdIoActive =
            storageSession.currentWorkspaceSave.sdIoActive || storageSession.revisionCommit.sdIoActive ||
            storageSession.revisionLoad.sdIoActive || storageSession.midPassChunkPersist.sdIoActive ||
            storageSession.persistenceWorkItem.sdIoActive || SlotLoadSession::isActive();
        const uint16_t workQueueDepth = PersistenceWorkQueue::queueDepth();
        const uint16_t writingWorkItemCount = PersistenceWorkQueue::writingWorkItemCount();
        const bool deferMidPassForWorkspaceSave = PersistenceFailurePolicy::shouldDeferMidPassForWorkspaceSave(
            storageSession.currentWorkspaceSave.pending,
            storageSession.currentWorkspaceSave.urgentRequested, workQueueDepth,
            writingWorkItemCount, storageSession.persistenceWorkItem.itemActive);
        if (!deferMidPassForWorkspaceSave &&
            PersistenceFailurePolicy::shouldRunMidPassWriter(PersistenceQueue::queueDepth(),
                                                             otherSdIoActive)) {
            const uint32_t currentHeap = MemoryMonitor::getInternalHeapFreeBytes();
            if (!LoopEventStore::hasInternalHeapHeadroomForNonCriticalWork(currentHeap)) {
                PersistenceDiagnostics::onHeapFloorBlock();
                break;
            }
            PersistenceFailurePolicy::maybeEmitBackpressureTelemetry(captureActiveForScheduler);
#if defined(SESSION_CAPTURE)
            const uint32_t ioStartUs = micros();
#endif
            storageSession.midPassChunkPersist.sdIoActive = true;
            const bool midOk = stepMidPassChunkPersist();
            storageSession.midPassChunkPersist.sdIoActive = false;
#if defined(SESSION_CAPTURE)
            const uint32_t sliceLatencyUs = micros() - ioStartUs;
            PersistenceDiagnostics::onSliceCompleted(sliceLatencyUs);
#endif
            if (!midOk) {
#if defined(SESSION_CAPTURE)
                SC_PERSIST("mid_pass", sliceLatencyUs, 0, 0, "failed");
#endif
                break;
            }
            break;
        }

        if (PersistenceFailurePolicy::shouldRunPersistenceWorkItemWriter(
                workQueueDepth, writingWorkItemCount, otherSdIoActive)) {
            const uint32_t currentHeap = MemoryMonitor::getInternalHeapFreeBytes();
            if (!PersistenceFailurePolicy::hasPersistenceSliceHeadroom(
                    currentHeap, storageSession.persistenceWorkItem.itemActive,
                    storageSession.currentWorkspaceSave.urgentRequested)) {
                PersistenceDiagnostics::onHeapFloorBlock();
                break;
            }
            PersistenceFailurePolicy::maybeEmitBackpressureTelemetry(captureActiveForScheduler);
#if defined(SESSION_CAPTURE)
            const uint32_t ioStartUs = micros();
#endif
            storageSession.persistenceWorkItem.sdIoActive = true;
            storageSession.currentWorkspaceSave.sdIoActive = true;
            const bool workOk = stepPersistenceWorkItem(state);
            storageSession.persistenceWorkItem.sdIoActive = false;
            storageSession.currentWorkspaceSave.sdIoActive = false;
#if defined(SESSION_CAPTURE)
            const uint32_t sliceLatencyUs = micros() - ioStartUs;
            PersistenceDiagnostics::onSliceCompleted(sliceLatencyUs);
#endif
            if (!workOk) {
#if defined(SESSION_CAPTURE)
                SC_PERSIST("work", sliceLatencyUs, 0, 0, "failed");
#endif
                break;
            }
            break;
        }

        break;
    }

#if defined(SESSION_CAPTURE)
    if (hasPersistenceWorkPending() &&
        PersistenceBudget::persistenceSliceBudgetExhausted(sliceBudgetUs, micros() - sliceStartUs)) {
        PersistenceDiagnostics::onBudgetBlock();
    }
    PersistenceDiagnostics::BacklogSnapshot backlog{};
    const SyncDrainProgressSnapshot progress = captureSyncDrainProgressSnapshot();
    const SyncDrainBudget drainBudget = buildSyncDrainBudgetForSession();
    backlog.workQueueDepth = progress.workQueueDepth;
    backlog.writingWorkItems = progress.writingWorkItems;
    backlog.chunkQueueDepth = progress.chunkQueueDepth;
    backlog.estSliceSteps = drainBudget.expectedSliceSteps;
    backlog.estSdBytes = drainBudget.estimatedSdPayloadBytes;
    backlog.urgentRequested = storageSession.currentWorkspaceSave.urgentRequested ? 1U : 0U;
    PersistenceDiagnostics::maybeEmitPeriodic(
        isCaptureActiveForPersistence(), storageSession.currentWorkspaceSave.pending,
        storageSession.persistenceWorkItem.itemActive, storageSession.persistenceWorkItem.sdIoActive,
        &backlog);
#endif
}
