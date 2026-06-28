//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "OverlayCatalogReadPolicy.h"

namespace OverlayCatalogReadPolicy {

bool isOverlayCatalogReadAllowed(const OverlayCatalogReadInputs& inputs) {
  // Block while persistence holds SD files open (reentrancy hazard).
  if (inputs.deferredSaveSdIoActive || inputs.revisionCommitSdIoActive ||
      inputs.revisionLoadSdIoActive) {
    return false;
  }
  // Block while a writer job is actively advancing (may open SD any slice).
  if (inputs.deferredSaveInProgress || inputs.revisionCommitInProgress ||
      inputs.revisionLoadInProgress) {
    return false;
  }
  // Queued (*Pending) alone is allowed: display update runs before persistence slices
  // in main loop, so catalog reads can complete between writer steps.
  return true;
}

}  // namespace OverlayCatalogReadPolicy
