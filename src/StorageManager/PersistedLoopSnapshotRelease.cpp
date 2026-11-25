//  Copyright (c)  2026 Lytrix (Eelke Jager)
// Licensed under the PolyForm Noncommercial 1.0.0
#include "StorageLoopIo.h"
#include "Utils/LoopMem.h"

bool LOOP_COLD_MEM PersistedLoopSnapshotReleaseCursor::begin(LoopSnapshotRef& root) {
  if (!complete()) return false;
  phase_ = 0;
  units_ = 0;
  root_ = std::move(root);
  return true;
}

bool LOOP_COLD_MEM PersistedLoopSnapshotReleaseCursor::step(size_t maximumUnits) {
  for (size_t unit = 0; unit < maximumUnits && root_; ++unit) {
    ++units_;
    if (root_.use_count() > 1) {
      root_.reset();  // Another owner retains all payloads unchanged.
      continue;
    }
    auto& passes = root_->passes;
    switch (phase_) {
      case 0:
        if (!LoopEventStore::releaseLastCommittedChunkRef(passes.recordPass.committedChunkIds)) {
          passes.recordPass = RecordPass{};  // Empty vector: at most one free.
          ++phase_;
        }
        break;
      case 1:
        if (passes.overdubPasses.empty()) {
          CommittedOverdubPassVec{}.swap(passes.overdubPasses);
          ++phase_;
        } else if (!LoopEventStore::releaseLastCommittedChunkRef(
                       passes.overdubPasses.back().committedChunkIds)) {
          passes.overdubPasses.pop_back();  // Its chunk list is already empty.
        }
        break;
      case 2:
        if (passes.editPasses.empty()) {
          EditPassVec{}.swap(passes.editPasses);
          ++phase_;
        } else if (!passes.editPasses.back().addedEvents.empty()) {
          passes.editPasses.back().addedEvents.pop_back();
        } else {
          passes.editPasses.pop_back();
        }
        break;
      case 3:
        if (passes.loopGeometries.empty()) {
          LoopGeometryVec{}.swap(passes.loopGeometries);
          ++phase_;
        } else {
          passes.loopGeometries.pop_back();
        }
        break;
      default:
        root_.reset();  // All variable-size payloads and backing arrays drained.
        break;
    }
  }
  return complete();
}
