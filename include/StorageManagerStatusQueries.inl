//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// Inline status-query bodies for StorageManager (included from StorageManager.h).

#pragma once

#include "StorageManagerInternal.h"
#include <cstdio>

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

inline uint32_t StorageManager::getCurrentSetLastActiveUnix() {
    return StorageManagerInternal::currentSetLastActiveUnix;
}

inline bool StorageManager::isCurrentWorkspaceDirty() {
    return CurrentWorkspaceStorage::isWorkspaceDirty(
        StorageManagerInternal::currentWorkspaceEpoch,
        StorageManagerInternal::lastCommittedWorkspaceEpoch);
}

inline bool StorageManager::shouldQueueCurrentWorkspaceSave() {
#if BYPASS_STOP_UNDO_SAVE
    return false;
#else
    if (StorageManagerInternal::forceCurrentSetFullLoopWrite) {
        return true;
    }
    if (StorageManagerInternal::anyCurrentSetLoopSlotDirty()) {
        return true;
    }
    if (StorageManagerInternal::anyAllocatedLoopEditStateDirty()) {
        return true;
    }
    return CurrentSetStorage::shouldAutoSaveBeforeLoadIntoCurrent(
        StorageManagerInternal::currentSetAnchorFields);
#endif
}

inline uint32_t StorageManager::getCurrentWorkspaceEpoch() {
    return StorageManagerInternal::currentWorkspaceEpoch;
}

inline uint32_t StorageManager::getLastCommittedWorkspaceEpoch() {
    return StorageManagerInternal::lastCommittedWorkspaceEpoch;
}

inline uint16_t StorageManager::getCurrentWorkspaceDerivedSetId() {
    return StorageManagerInternal::workspaceDerivedFromSetId;
}

inline uint16_t StorageManager::getCurrentWorkspaceDerivedRevisionId() {
    return StorageManagerInternal::workspaceDerivedFromRevisionId;
}

inline bool StorageManager::copyCurrentSetLoadedFromFolder(char* out, size_t outSize) {
    if (out == nullptr || outSize == 0 ||
        StorageManagerInternal::currentSetLoadedFromFolder[0] == '\0') {
        return false;
    }
    const int written =
        std::snprintf(out, outSize, "%s", StorageManagerInternal::currentSetLoadedFromFolder);
    return written > 0 && static_cast<size_t>(written) < outSize;
}

inline bool StorageManager::consumeAutoSaveBeforeLoadFolder(char* out, size_t outSize) {
    if (out == nullptr || outSize == 0 ||
        !StorageManagerInternal::autoSaveBeforeLoadFolderPendingValid) {
        return false;
    }
    const int written = std::snprintf(
        out, outSize, "%s", StorageManagerInternal::autoSaveBeforeLoadFolderPending);
    const bool copied = written > 0 && static_cast<size_t>(written) < outSize;
    StorageManagerInternal::clearAutoSaveBeforeLoadFolderPending();
    return copied;
}

inline bool StorageManager::hasPendingUndoSnapshotHydrate() {
    return StorageManagerInternal::undoSnapshotsPending_;
}
