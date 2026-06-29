//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "RevisionLoadPolicy.h"

namespace RevisionLoadPolicy {

bool shouldHoldRevisionLoadRequest(bool workspaceDirty) {
  return workspaceDirty;
}

bool shouldDispatchRequestedLoadAfterCommitComplete(bool loadAfterRevisionCommitActive,
                                                    bool commitSucceeded) {
  return loadAfterRevisionCommitActive && commitSucceeded;
}

}  // namespace RevisionLoadPolicy
