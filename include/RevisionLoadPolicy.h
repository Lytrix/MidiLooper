//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#pragma once

#include <cstdint>

namespace RevisionLoadPolicy {

enum class LoadRequestGate : uint8_t {
  DispatchImmediately = 0,
  ShowDirtyPrompt,
};

enum class DirtyPromptChoice : uint8_t {
  None = 0,
  SaveThenLoad,
  DiscardLoad,
  Cancel,
};

constexpr uint8_t kDirtyPromptRowCount = 3;

/// Whether a revision load request should run immediately or wait for dirty prompt.
LoadRequestGate resolveLoadRequestGate(bool workspaceDirty);

/// After revision commit COMPLETE during save-then-load pipeline.
bool shouldDispatchStagedLoadAfterCommitComplete(bool saveThenLoadPipelineActive,
                                                 bool commitSucceeded);

/// Overlay minimal spinner until load (and optional preceding commit) finish.
bool isMinimalLoadingOverlayActive(bool pipelineActive, bool commitPending,
                                   bool commitInProgress, bool loadPending,
                                   bool loadInProgress);

}  // namespace RevisionLoadPolicy
