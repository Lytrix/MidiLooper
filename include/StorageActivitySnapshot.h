//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#pragma once

#include "OverlayCatalogReadPolicy.h"
#include "SetBrowserOverlayPolicy.h"

/// Read-only aggregate of persistence job flags and overlay coordination inputs.
/// Populated by `buildStorageActivitySnapshot()` in StorageManager.cpp on device; tests
/// build instances directly.
struct StorageActivitySnapshot {
  bool deferredSavePending = false;
  bool deferredSaveInProgress = false;
  bool deferredSaveSdIoActive = false;
  bool revisionCommitPending = false;
  bool revisionCommitInProgress = false;
  bool revisionCommitSdIoActive = false;
  bool revisionCommitOverlayBackground = false;
  bool revisionLoadPending = false;
  bool revisionLoadInProgress = false;
  bool revisionLoadSdIoActive = false;
  bool revisionLoadDirtyPromptActive = false;
  bool loadAfterRevisionCommit = false;
  bool overlayOpen = false;
  SetBrowserOverlayPolicy::NavigationState navigation{};
};

SetBrowserOverlayPolicy::PersistencePhase resolvePersistencePhase(
    const StorageActivitySnapshot& snapshot);

bool isRevisionLoadDisplayPipelineActive(const StorageActivitySnapshot& snapshot);

OverlayCatalogReadPolicy::OverlayCatalogReadInputs overlayCatalogReadInputsFromSnapshot(
    const StorageActivitySnapshot& snapshot);

bool isMinimalLoadingOverlayActive(const StorageActivitySnapshot& snapshot);

SetBrowserOverlayPolicy::Mode resolveOverlayMode(const StorageActivitySnapshot& snapshot);

bool isOverlayCatalogReadAllowed(const StorageActivitySnapshot& snapshot);

bool isOverlayLoadRequestBlocked(const StorageActivitySnapshot& snapshot);
