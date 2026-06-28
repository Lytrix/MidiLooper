//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#pragma once

namespace OverlayCatalogReadPolicy {

struct OverlayCatalogReadInputs {
  bool deferredSavePending = false;
  bool deferredSaveInProgress = false;
  bool revisionCommitPending = false;
  bool revisionCommitInProgress = false;
  bool revisionLoadPending = false;
  bool revisionLoadInProgress = false;
  bool deferredSaveSdIoActive = false;
  bool revisionCommitSdIoActive = false;
  bool revisionLoadSdIoActive = false;
};

/// False while persistence may use SD — overlay catalog/metadata reads must defer.
bool isOverlayCatalogReadAllowed(const OverlayCatalogReadInputs& inputs);

}  // namespace OverlayCatalogReadPolicy
