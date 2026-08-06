//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// Inline status-query bodies for StorageManager (included from StorageManager.h).

#pragma once

#include "StorageManagerInternal.h"

inline bool StorageManager::isDeferredSaveActive() {
#if BYPASS_STOP_UNDO_SAVE
    return false;
#else
    return StorageManagerInternal::storageSession.currentWorkspaceSave.sdIoActive ||
           StorageManagerInternal::storageSession.persistenceWorkItem.sdIoActive;
#endif
}

inline bool StorageManager::hasDeferredSaveWork() {
#if BYPASS_STOP_UNDO_SAVE
    return false;
#else
    return StorageManagerInternal::hasPersistenceWorkPending();
#endif
}

inline bool StorageManager::hasRevisionCommitWork() {
#if BYPASS_STOP_UNDO_SAVE
    return false;
#else
    return StorageManagerInternal::storageSession.revisionCommit.pending ||
           StorageManagerInternal::storageSession.revisionCommit.inProgress;
#endif
}

inline bool StorageManager::isRevisionCommitActive() {
#if BYPASS_STOP_UNDO_SAVE
    return false;
#else
    return StorageManagerInternal::storageSession.revisionCommit.sdIoActive;
#endif
}

inline DeferredSaveDisplayStatus StorageManager::getDeferredSaveDisplayStatus(uint32_t nowMs) {
#if BYPASS_STOP_UNDO_SAVE
    (void)nowMs;
    return {};
#else
    DeferredSaveDisplayInputs inputs{};
    inputs.savePending = StorageManagerInternal::storageSession.currentWorkspaceSave.pending ||
                         PersistenceWorkQueue::queueDepth() > 0;
    inputs.saveInProgress =
        StorageManagerInternal::storageSession.persistenceWorkItem.itemActive ||
        PersistenceWorkQueue::writingWorkItemCount() > 0 ||
        StorageManagerInternal::storageSession.persistenceWorkItem.sdIoActive;
    inputs.revisionCommitPending = StorageManagerInternal::storageSession.revisionCommit.pending;
    inputs.revisionCommitInProgress =
        StorageManagerInternal::storageSession.revisionCommit.inProgress;
    inputs.completedAtMs = StorageManagerInternal::storageSession.currentWorkspaceSave.completedAtMs;
    inputs.failedAtMs = StorageManagerInternal::storageSession.currentWorkspaceSave.failedAtMs;
    return resolveDeferredSaveDisplayStatus(nowMs, inputs);
#endif
}
