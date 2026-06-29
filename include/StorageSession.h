//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#pragma once

#include <cstdint>

#include "RevisionLoadPolicy.h"
#include "SetBrowserOverlayPolicy.h"

/// RAM aggregate for active persistence jobs on StorageManager (DEC-012).
struct RevisionCommitJob {
  bool overlayBackgroundCommit = false;
};

struct RevisionLoadJob {
  bool requested = false;
  uint16_t requestedSetId = 0;
  uint16_t requestedRevisionId = 0;
  bool heldForWorkspaceDirty = false;
  RevisionLoadPolicy::DirtyPromptChoice confirmChoice =
      RevisionLoadPolicy::DirtyPromptChoice::None;
  bool loadAfterRevisionCommit = false;
  bool pending = false;
  bool inProgress = false;
  bool sdIoActive = false;
};

struct StorageSession {
  RevisionCommitJob revisionCommit;
  RevisionLoadJob revisionLoad;
  SetBrowserOverlayPolicy::NavigationState setBrowserNavigation;
};
