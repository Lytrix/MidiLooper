//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "StorageManager.h"
#include "StorageManagerInternal.h"
#include "StorageActivitySnapshot.h"
#include "RevisionLoadPolicy.h"
#include "SetBrowserOverlayPolicy.h"
#include "DeferredSaveDisplayStatus.h"
#include "Globals.h"
#include "LooperState.h"
#include "Utils/DebugSessionCapture.h"
#include <Arduino.h>

using namespace StorageManagerInternal;

namespace StorageManagerInternal {

StorageActivitySnapshot buildStorageActivitySnapshot() {
    StorageActivitySnapshot snapshot{};
    snapshot.deferredSavePending = storageSession.currentWorkspaceSave.pending;
    snapshot.deferredSaveInProgress = storageSession.currentWorkspaceSave.inProgress;
    snapshot.deferredSaveSdIoActive = storageSession.currentWorkspaceSave.sdIoActive;
    snapshot.revisionCommitPending = storageSession.revisionCommit.pending;
    snapshot.revisionCommitInProgress = storageSession.revisionCommit.inProgress;
    snapshot.revisionCommitSdIoActive = storageSession.revisionCommit.sdIoActive;
    snapshot.revisionCommitOverlayBackground = storageSession.revisionCommit.overlayBackgroundCommit;
    snapshot.revisionLoadPending = storageSession.revisionLoad.pending;
    snapshot.revisionLoadInProgress = storageSession.revisionLoad.inProgress;
    snapshot.revisionLoadSdIoActive = storageSession.revisionLoad.sdIoActive;
    snapshot.revisionLoadDirtyPromptActive = storageSession.revisionLoad.heldForWorkspaceDirty;
    snapshot.loadAfterRevisionCommit = storageSession.revisionLoad.loadAfterRevisionCommit;
    snapshot.overlayOpen = looperState.isLoadSaveModeActive();
    snapshot.navigation = storageSession.setBrowserNavigation;
    return snapshot;
}

}  // namespace StorageManagerInternal


StorageManager::SetBrowserOverlayMode StorageManager::getSetBrowserOverlayMode() {
    return resolveOverlayMode(buildStorageActivitySnapshot());
}
SetBrowserOverlayPolicy::PersistencePhase StorageManager::getSetBrowserOverlayPersistencePhase() {
    return resolvePersistencePhase(buildStorageActivitySnapshot());
}

StorageManager::SetBrowserOverlayEntryKind StorageManager::getSetBrowserOverlayEntryKind() {
    return storageSession.setBrowserNavigation.entryKind;
}

STORAGE_PERSIST_MEM void StorageManager::resetSetBrowserOverlayNavigation() {
    SetBrowserOverlayPolicy::resetNavigation(storageSession.setBrowserNavigation);
}

STORAGE_PERSIST_MEM void StorageManager::setSetBrowserOverlayEntryKind(SetBrowserOverlayEntryKind kind) {
    SetBrowserOverlayPolicy::setEntryKind(storageSession.setBrowserNavigation, kind);
}

STORAGE_PERSIST_MEM bool StorageManager::openSetBrowserRevisionHistory(uint16_t setId, uint8_t listSelection,
                                                   uint8_t listScrollOffset) {
    if (setId == 0) {
        return false;
    }
    SetBrowserOverlayPolicy::openRevisionHistory(storageSession.setBrowserNavigation, setId, listSelection,
                                                 listScrollOffset);
    return true;
}

STORAGE_PERSIST_MEM bool StorageManager::openSetBrowserLoopPick(uint16_t setId, uint8_t listSelection,
                                            uint8_t listScrollOffset) {
    if (setId == 0) {
        return false;
    }
    SetBrowserOverlayPolicy::openLoopPick(storageSession.setBrowserNavigation, setId, listSelection,
                                          listScrollOffset);
    return true;
}

STORAGE_PERSIST_MEM bool StorageManager::navigateSetBrowserOverlayBack(uint8_t& outListSelection,
                                                   uint8_t& outListScrollOffset) {
    return SetBrowserOverlayPolicy::navigateBack(storageSession.setBrowserNavigation, outListSelection,
                                                 outListScrollOffset);
}

STORAGE_PERSIST_MEM uint16_t StorageManager::getSetBrowserOverlayDrilledSetId() {
    return storageSession.setBrowserNavigation.drilledSetId;
}

STORAGE_PERSIST_MEM bool StorageManager::isRevisionLoadHeldForWorkspaceDirty() {
    return storageSession.revisionLoad.heldForWorkspaceDirty;
}

STORAGE_PERSIST_MEM uint8_t StorageManager::getRevisionLoadDirtyPromptSelection() {
    return static_cast<uint8_t>(storageSession.revisionLoad.confirmChoice);
}

STORAGE_PERSIST_MEM void StorageManager::adjustRevisionLoadDirtyPromptSelection(int delta) {
    if (!storageSession.revisionLoad.heldForWorkspaceDirty || delta == 0) {
        return;
    }
    int next = static_cast<int>(storageSession.revisionLoad.confirmChoice) + delta;
    if (next < 0) {
        next = 0;
    } else if (next >= static_cast<int>(RevisionLoadPolicy::kDirtyPromptRowCount)) {
        next = static_cast<int>(RevisionLoadPolicy::kDirtyPromptRowCount) - 1;
    }
    storageSession.revisionLoad.confirmChoice =
        static_cast<RevisionLoadPolicy::DirtyPromptChoice>(next);
#if defined(SESSION_CAPTURE)
    SC_OVERLAY_SEL(1, static_cast<uint8_t>(storageSession.revisionLoad.confirmChoice));
#endif
}

STORAGE_PERSIST_MEM void StorageManager::beginOverlaySaveRowCommit() {
#if BYPASS_STOP_UNDO_SAVE
    return;
#else
    storageSession.revisionCommit.overlayBackgroundCommit = true;
    requestCommitRevision();
#endif
}

STORAGE_PERSIST_MEM bool StorageManager::isOverlayCatalogReadAllowed() {
#if BYPASS_STOP_UNDO_SAVE
    return true;
#else
    return ::isOverlayCatalogReadAllowed(buildStorageActivitySnapshot());
#endif
}

DeferredSaveDisplayStatus StorageManager::getDeferredLoadDisplayStatus(uint32_t nowMs) {
#if BYPASS_STOP_UNDO_SAVE
    (void)nowMs;
    return {};
#else
    DeferredLoadDisplayInputs inputs{};
    inputs.loadPending = storageSession.revisionLoad.pending;
    inputs.loadInProgress = storageSession.revisionLoad.inProgress;
    inputs.saveThenLoadCommitInProgress =
        storageSession.revisionLoad.loadAfterRevisionCommit && storageSession.revisionCommit.inProgress;
    inputs.completedAtMs = storageSession.revisionLoad.completedAtMs;
    inputs.failedAtMs = storageSession.revisionLoad.failedAtMs;
    return resolveDeferredLoadDisplayStatus(nowMs, inputs);
#endif
}

STORAGE_PERSIST_MEM uint16_t StorageManager::getRevisionLoadDisplayTargetSetId() {
#if BYPASS_STOP_UNDO_SAVE
    return 0;
#else
    if (isRevisionLoadDisplayPipelineActive(buildStorageActivitySnapshot())) {
        if (storageSession.revisionLoad.setId != 0) {
            return storageSession.revisionLoad.setId;
        }
        return storageSession.revisionLoad.requestedSetId;
    }
    if (storageSession.revisionLoad.completedAtMs != 0 || storageSession.revisionLoad.failedAtMs != 0) {
        return storageSession.revisionLoad.lastDisplaySetId;
    }
    return 0;
#endif
}

STORAGE_PERSIST_MEM uint16_t StorageManager::getRevisionLoadDisplayTargetRevisionId() {
#if BYPASS_STOP_UNDO_SAVE
    return 0;
#else
    if (isRevisionLoadDisplayPipelineActive(buildStorageActivitySnapshot())) {
        if (storageSession.revisionLoad.revisionId != 0) {
            return storageSession.revisionLoad.revisionId;
        }
        return storageSession.revisionLoad.requestedRevisionId;
    }
    if (storageSession.revisionLoad.completedAtMs != 0 || storageSession.revisionLoad.failedAtMs != 0) {
        return storageSession.revisionLoad.lastDisplayRevisionId;
    }
    return 0;
#endif
}

STORAGE_PERSIST_MEM bool StorageManager::consumeRevisionLoadDisplayRefreshPending() {
    if (!storageSession.revisionLoad.displayRefreshPending) {
        return false;
    }
    storageSession.revisionLoad.displayRefreshPending = false;
    return true;
}

STORAGE_PERSIST_MEM void StorageManager::confirmRevisionLoadAfterCommit() {
#if BYPASS_STOP_UNDO_SAVE
    return;
#else
    if (!storageSession.revisionLoad.heldForWorkspaceDirty || !storageSession.revisionLoad.requested) {
        return;
    }
    storageSession.revisionLoad.heldForWorkspaceDirty = false;
    storageSession.revisionLoad.loadAfterRevisionCommit = true;
    SC_PERSIST("rev_load_dirty_yes", 0, storageSession.revisionLoad.requestedSetId,
               storageSession.revisionLoad.requestedRevisionId, "save_then_load");
    requestCommitRevision();
#endif
}

STORAGE_PERSIST_MEM void StorageManager::confirmRevisionLoadDiscardWorkspace() {
#if BYPASS_STOP_UNDO_SAVE
    return;
#else
    if (!storageSession.revisionLoad.heldForWorkspaceDirty || !storageSession.revisionLoad.requested) {
        return;
    }
    storageSession.revisionLoad.heldForWorkspaceDirty = false;
    storageSession.revisionLoad.loadAfterRevisionCommit = false;
    SC_PERSIST("rev_load_dirty_no", 0, storageSession.revisionLoad.requestedSetId,
               storageSession.revisionLoad.requestedRevisionId, "discard_load");
    dispatchRequestedRevisionLoad();
#endif
}

STORAGE_PERSIST_MEM void StorageManager::cancelRevisionLoadRequest() {
#if BYPASS_STOP_UNDO_SAVE
    return;
#else
    if (!storageSession.revisionLoad.heldForWorkspaceDirty) {
        return;
    }
    SC_PERSIST("rev_load_dirty_cancel", 0, storageSession.revisionLoad.requestedSetId,
               storageSession.revisionLoad.requestedRevisionId, "cancel");
    clearRevisionLoadRequestState();
#endif
}

STORAGE_PERSIST_MEM void StorageManager::requestLoadRevision(uint16_t setId, uint16_t revisionId) {
#if BYPASS_STOP_UNDO_SAVE
    (void)setId;
    (void)revisionId;
    return;
#else
    if (setId == 0 || revisionId == 0) {
        SC_PERSIST("rev_load_skip", 0, setId, revisionId, "bad_id");
        return;
    }
    if (storageSession.revisionLoad.heldForWorkspaceDirty ||
        isOverlayLoadRequestBlocked(buildStorageActivitySnapshot())) {
        SC_PERSIST("rev_load_skip", 0, setId, revisionId, "pipeline_busy");
        return;
    }
    storageSession.revisionLoad.requestedSetId = setId;
    storageSession.revisionLoad.requestedRevisionId = revisionId;
    storageSession.revisionLoad.requested = true;
    storageSession.revisionLoad.confirmChoice = RevisionLoadPolicy::DirtyPromptChoice::None;

    if (RevisionLoadPolicy::shouldHoldRevisionLoadRequest(isCurrentWorkspaceDirty())) {
        storageSession.revisionLoad.heldForWorkspaceDirty = true;
        SC_PERSIST("rev_load_dirty_prompt", 0, setId, revisionId, "shown");
        return;
    }

    dispatchRequestedRevisionLoad();
#endif
}

STORAGE_PERSIST_MEM void StorageManager::requestLoadLatestRevisionForSet(uint16_t setId) {
#if BYPASS_STOP_UNDO_SAVE
    (void)setId;
    return;
#else
    if (setId == 0) {
        return;
    }
    uint16_t latestRevisionId = 0;
    if (!readSetLatestRevisionIdFromSd(setId, latestRevisionId)) {
        return;
    }
    requestLoadRevision(setId, latestRevisionId);
#endif
}

STORAGE_PERSIST_MEM bool StorageManager::hasRevisionLoadWork() {
#if BYPASS_STOP_UNDO_SAVE
    return false;
#else
    return storageSession.revisionLoad.pending || storageSession.revisionLoad.inProgress;
#endif
}

STORAGE_PERSIST_MEM bool StorageManager::isRevisionLoadActive() {
#if BYPASS_STOP_UNDO_SAVE
    return false;
#else
    return storageSession.revisionLoad.sdIoActive;
#endif
}

#if defined(SESSION_CAPTURE)
STORAGE_PERSIST_MEM void StorageManager::requestLoadRevisionForHitl(uint16_t setId, uint16_t revisionId) {
    requestLoadRevision(setId, revisionId);
    SC_PERSIST("rev_load_hitl_arm", 0, setId, revisionId, "armed");
}

STORAGE_PERSIST_MEM void StorageManager::confirmRevisionLoadAfterCommitForHitl() {
    confirmRevisionLoadAfterCommit();
}

STORAGE_PERSIST_MEM void StorageManager::confirmRevisionLoadDiscardWorkspaceForHitl() {
    confirmRevisionLoadDiscardWorkspace();
}

STORAGE_PERSIST_MEM void StorageManager::cancelRevisionLoadRequestForHitl() {
    cancelRevisionLoadRequest();
}
#endif

