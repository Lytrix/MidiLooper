//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "Loop.h"

#include "Utils/LoopMem.h"

#include <utility>

struct PublishedSnapshotBox {
  ImmutableLoopSnapshotRef snapshot;
};

LOOP_COLD_MEM Loop::Loop() {}

LOOP_COLD_MEM Loop::~Loop() {}

LOOP_COLD_MEM ImmutableLoopSnapshotRef Loop::publishedSnapshot() const {
  return publishedSnapshotBox_ ? publishedSnapshotBox_->snapshot
                               : ImmutableLoopSnapshotRef{};
}

LOOP_COLD_MEM void Loop::clearPublishedSnapshot() {
  publishedSnapshotBox_.reset();
}

LOOP_COLD_MEM void Loop::storePublishedSnapshot(ImmutableLoopSnapshotRef snapshot) {
  if (!snapshot) {
    publishedSnapshotBox_.reset();
    return;
  }
  if (!publishedSnapshotBox_) {
    publishedSnapshotBox_ = std::make_unique<PublishedSnapshotBox>();
  }
  publishedSnapshotBox_->snapshot = std::move(snapshot);
}
