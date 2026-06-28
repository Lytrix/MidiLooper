//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "RevisionLoadPolicy.h"

#include "SetBrowserOverlayPolicy.h"

namespace RevisionLoadPolicy {

LoadRequestGate resolveLoadRequestGate(bool workspaceDirty) {
  return workspaceDirty ? LoadRequestGate::ShowDirtyPrompt
                        : LoadRequestGate::DispatchImmediately;
}

bool shouldDispatchStagedLoadAfterCommitComplete(bool saveThenLoadPipelineActive,
                                                 bool commitSucceeded) {
  return saveThenLoadPipelineActive && commitSucceeded;
}

bool isMinimalLoadingOverlayActive(bool pipelineActive, bool commitPending,
                                   bool commitInProgress, bool loadPending,
                                   bool loadInProgress) {
  (void)loadPending;
  (void)loadInProgress;
  if (!pipelineActive) {
    return false;
  }
  SetBrowserOverlayPolicy::PersistencePhase phase =
      SetBrowserOverlayPolicy::PersistencePhase::Idle;
  if (commitPending || commitInProgress) {
    phase = SetBrowserOverlayPolicy::PersistencePhase::AwaitingCommitThenLoad;
  } else {
    phase = SetBrowserOverlayPolicy::PersistencePhase::LoadInProgress;
  }
  return SetBrowserOverlayPolicy::isMinimalLoadingOverlayActive(phase, true, commitPending,
                                                                commitInProgress);
}

}  // namespace RevisionLoadPolicy
