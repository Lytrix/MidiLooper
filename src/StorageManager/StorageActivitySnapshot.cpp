//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "StorageActivitySnapshot.h"

SetBrowserOverlayPolicy::PersistencePhase resolvePersistencePhase(
    const StorageActivitySnapshot& snapshot) {
  using Phase = SetBrowserOverlayPolicy::PersistencePhase;
  if (snapshot.loadAfterRevisionCommit &&
      (snapshot.revisionCommitPending || snapshot.revisionCommitInProgress) &&
      !snapshot.revisionLoadPending && !snapshot.revisionLoadInProgress) {
    return Phase::AwaitingRevisionCommit;
  }
  if (snapshot.revisionLoadPending || snapshot.revisionLoadInProgress) {
    return Phase::RevisionLoadActive;
  }
  if (snapshot.revisionCommitOverlayBackground &&
      (snapshot.revisionCommitPending || snapshot.revisionCommitInProgress)) {
    return Phase::RevisionCommitActive;
  }
  return Phase::Idle;
}

bool isRevisionLoadDisplayPipelineActive(const StorageActivitySnapshot& snapshot) {
  return snapshot.loadAfterRevisionCommit || snapshot.revisionLoadPending ||
         snapshot.revisionLoadInProgress;
}

OverlayCatalogReadPolicy::OverlayCatalogReadInputs overlayCatalogReadInputsFromSnapshot(
    const StorageActivitySnapshot& snapshot) {
  OverlayCatalogReadPolicy::OverlayCatalogReadInputs inputs{};
  inputs.deferredSavePending = snapshot.deferredSavePending;
  inputs.deferredSaveInProgress = snapshot.deferredSaveInProgress;
  inputs.revisionCommitPending = snapshot.revisionCommitPending;
  inputs.revisionCommitInProgress = snapshot.revisionCommitInProgress;
  inputs.revisionLoadPending = snapshot.revisionLoadPending;
  inputs.revisionLoadInProgress = snapshot.revisionLoadInProgress;
  inputs.deferredSaveSdIoActive = snapshot.deferredSaveSdIoActive;
  inputs.revisionCommitSdIoActive = snapshot.revisionCommitSdIoActive;
  inputs.revisionLoadSdIoActive = snapshot.revisionLoadSdIoActive;
  return inputs;
}

bool isMinimalLoadingOverlayActive(const StorageActivitySnapshot& snapshot) {
  const SetBrowserOverlayPolicy::PersistencePhase phase = resolvePersistencePhase(snapshot);
  return SetBrowserOverlayPolicy::isMinimalLoadingOverlayActive(
      phase, snapshot.overlayOpen, snapshot.revisionCommitPending,
      snapshot.revisionCommitInProgress);
}

SetBrowserOverlayPolicy::Mode resolveOverlayMode(const StorageActivitySnapshot& snapshot) {
  return SetBrowserOverlayPolicy::resolveActiveMode(
      snapshot.navigation, snapshot.revisionLoadDirtyPromptActive,
      isMinimalLoadingOverlayActive(snapshot));
}

bool isOverlayCatalogReadAllowed(const StorageActivitySnapshot& snapshot) {
  return OverlayCatalogReadPolicy::isOverlayCatalogReadAllowed(
      overlayCatalogReadInputsFromSnapshot(snapshot));
}

bool isOverlayLoadRequestBlocked(const StorageActivitySnapshot& snapshot) {
  return SetBrowserOverlayPolicy::isOverlayLoadRequestBlocked(
      resolvePersistencePhase(snapshot), snapshot.revisionLoadPending,
      snapshot.revisionLoadInProgress);
}
