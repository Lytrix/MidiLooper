//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "RevisionLoadPolicy.h"

namespace RevisionLoadPolicy {

LoadRequestGate resolveLoadRequestGate(bool workspaceDirty) {
  return workspaceDirty ? LoadRequestGate::ShowDirtyPrompt
                        : LoadRequestGate::DispatchImmediately;
}

bool shouldDispatchStagedLoadAfterCommitComplete(bool saveThenLoadPipelineActive,
                                                 bool commitSucceeded) {
  return saveThenLoadPipelineActive && commitSucceeded;
}

bool isMinimalLoadingOverlayActive(bool pipelineActive, bool commitInProgress,
                                   bool loadPending, bool loadInProgress) {
  if (!pipelineActive) {
    return false;
  }
  return commitInProgress || loadPending || loadInProgress;
}

}  // namespace RevisionLoadPolicy
