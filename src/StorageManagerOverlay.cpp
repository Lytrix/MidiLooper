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
    snapshot.deferredSavePending = deferredSavePending;
    snapshot.deferredSaveInProgress = deferredSaveInProgress;
    snapshot.deferredSaveSdIoActive = deferredSaveSdIoActive;
    snapshot.revisionCommitPending = revisionCommitPending;
    snapshot.revisionCommitInProgress = revisionCommitInProgress;
    snapshot.revisionCommitSdIoActive = revisionCommitSdIoActive;
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

void StorageManager::resetSetBrowserOverlayNavigation() {
    SetBrowserOverlayPolicy::resetNavigation(storageSession.setBrowserNavigation);
}

void StorageManager::setSetBrowserOverlayEntryKind(SetBrowserOverlayEntryKind kind) {
    SetBrowserOverlayPolicy::setEntryKind(storageSession.setBrowserNavigation, kind);
}

bool StorageManager::openSetBrowserRevisionHistory(uint16_t setId, uint8_t listSelection,
                                                   uint8_t listScrollOffset) {
    if (setId == 0) {
        return false;
    }
    SetBrowserOverlayPolicy::openRevisionHistory(storageSession.setBrowserNavigation, setId, listSelection,
                                                 listScrollOffset);
    return true;
}

bool StorageManager::openSetBrowserLoopPick(uint16_t setId, uint8_t listSelection,
                                            uint8_t listScrollOffset) {
    if (setId == 0) {
        return false;
    }
    SetBrowserOverlayPolicy::openLoopPick(storageSession.setBrowserNavigation, setId, listSelection,
                                          listScrollOffset);
    return true;
}

bool StorageManager::navigateSetBrowserOverlayBack(uint8_t& outListSelection,
                                                   uint8_t& outListScrollOffset) {
    return SetBrowserOverlayPolicy::navigateBack(storageSession.setBrowserNavigation, outListSelection,
                                                 outListScrollOffset);
}

uint16_t StorageManager::getSetBrowserOverlayDrilledSetId() {
    return storageSession.setBrowserNavigation.drilledSetId;
}

bool StorageManager::isRevisionLoadDirtyPromptActive() {
    return storageSession.revisionLoad.heldForWorkspaceDirty;
}

uint8_t StorageManager::getRevisionLoadDirtyPromptSelection() {
    return static_cast<uint8_t>(storageSession.revisionLoad.confirmChoice);
}

void StorageManager::adjustRevisionLoadDirtyPromptSelection(int delta) {
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

void StorageManager::beginOverlaySaveRowCommit() {
#if BYPASS_STOP_UNDO_SAVE
    return;
#else
    storageSession.revisionCommit.overlayBackgroundCommit = true;
    requestCommitRevision();
#endif
}

bool StorageManager::isOverlayCatalogReadAllowed() {
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
        storageSession.revisionLoad.loadAfterRevisionCommit && revisionCommitInProgress;
    inputs.completedAtMs = revisionLoadCompletedAtMs;
    inputs.failedAtMs = revisionLoadFailedAtMs;
    return resolveDeferredLoadDisplayStatus(nowMs, inputs);
#endif
}

uint16_t StorageManager::getRevisionLoadDisplayTargetSetId() {
#if BYPASS_STOP_UNDO_SAVE
    return 0;
#else
    if (isRevisionLoadDisplayPipelineActive(buildStorageActivitySnapshot())) {
        if (revisionLoadSetId != 0) {
            return revisionLoadSetId;
        }
        return storageSession.revisionLoad.requestedSetId;
    }
    if (revisionLoadCompletedAtMs != 0 || revisionLoadFailedAtMs != 0) {
        return revisionLoadLastDisplaySetId;
    }
    return 0;
#endif
}

uint16_t StorageManager::getRevisionLoadDisplayTargetRevisionId() {
#if BYPASS_STOP_UNDO_SAVE
    return 0;
#else
    if (isRevisionLoadDisplayPipelineActive(buildStorageActivitySnapshot())) {
        if (revisionLoadRevisionId != 0) {
            return revisionLoadRevisionId;
        }
        return storageSession.revisionLoad.requestedRevisionId;
    }
    if (revisionLoadCompletedAtMs != 0 || revisionLoadFailedAtMs != 0) {
        return revisionLoadLastDisplayRevisionId;
    }
    return 0;
#endif
}

bool StorageManager::consumeRevisionLoadDisplayRefreshPending() {
    if (!revisionLoadDisplayRefreshPending) {
        return false;
    }
    revisionLoadDisplayRefreshPending = false;
    return true;
}

