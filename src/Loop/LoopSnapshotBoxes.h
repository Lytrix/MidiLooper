//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#pragma once

#include "LoopSnapshot.h"

#include <memory>
#include <vector>

struct PublishedSnapshotBox {
  ImmutableLoopSnapshotRef snapshot;
  uint32_t revision = 0;
};

struct PendingSnapshotPublishBox {
  bool sessionExitQueued = false;
  bool companionsApplied = false;
  bool applyingCompanions = false;
  CommitReason sessionExitReason = CommitReason::RecordStop;
  EditPassIdList companionIds;
  std::vector<PassId> queuedPassIds;
  std::vector<PassId> appliedPassIds;
  PassId constructionPassId = kInvalidPassId;
  std::unique_ptr<LoopSnapshotConstruction> construction;
  ImmutableLoopSnapshotRef unpublishedCandidate;
};
