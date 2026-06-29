//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#pragma once

#include <cstdint>

namespace RevisionLoadPolicy {

enum class DirtyPromptChoice : uint8_t {
  None = 0,
  SaveThenLoad,
  DiscardLoad,
  Cancel,
};

constexpr uint8_t kDirtyPromptRowCount = 3;

/// When true, a revision load request is held for dirty-workspace confirmation.
bool shouldHoldRevisionLoadRequest(bool workspaceDirty);

/// After revision commit COMPLETE during save-then-load pipeline.
bool shouldDispatchRequestedLoadAfterCommitComplete(bool loadAfterRevisionCommitActive,
                                                    bool commitSucceeded);

}  // namespace RevisionLoadPolicy
