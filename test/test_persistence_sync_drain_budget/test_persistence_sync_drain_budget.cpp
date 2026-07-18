//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include <unity.h>

#include "../../src/LoopEventStore.cpp"
#include "../../src/PersistenceSyncDrainBudget.cpp"

#include "LoopPasses.h"
#include "PersistenceSyncDrainBudget.h"

namespace {

LoopPasses makePassesWithCaptureChunks(size_t recordChunks, size_t overdubChunks) {
  LoopPasses passes;
  if (recordChunks > 0) {
    passes.recordPass.id = 1;
    for (size_t i = 0; i < recordChunks; ++i) {
      passes.recordPass.committedChunkIds.push_back(static_cast<uint16_t>(i + 1));
    }
  }
  if (overdubChunks > 0) {
    OverdubPass overdub;
    overdub.id = 2;
    for (size_t i = 0; i < overdubChunks; ++i) {
      overdub.committedChunkIds.push_back(static_cast<uint16_t>(100 + i));
    }
    passes.overdubPasses.push_back(std::move(overdub));
  }
  return passes;
}

}  // namespace

void test_loop_persist_slice_steps_scales_with_chunks() {
  const LoopPasses empty = makePassesWithCaptureChunks(0, 0);
  const LoopPasses oneRecord = makePassesWithCaptureChunks(2, 0);
  const LoopPasses recordAndOverdub = makePassesWithCaptureChunks(1, 3);

  const uint32_t emptySteps = PersistenceSyncDrainBudget::estimateLoopPersistSliceSteps(empty);
  const uint32_t oneRecordSteps = PersistenceSyncDrainBudget::estimateLoopPersistSliceSteps(oneRecord);
  const uint32_t mixedSteps =
      PersistenceSyncDrainBudget::estimateLoopPersistSliceSteps(recordAndOverdub);

  TEST_ASSERT_LESS_THAN(oneRecordSteps, emptySteps);
  TEST_ASSERT_GREATER_THAN(0u, oneRecordSteps);
  TEST_ASSERT_LESS_THAN(mixedSteps, oneRecordSteps);
}

void test_build_sync_drain_budget_applies_headroom() {
  PersistenceSyncDrainBudget::SyncDrainBudgetInput input{};
  input.queuedWorkSliceSteps = 100;
  input.queuedWorkSdBytes = 4096;
  input.midPassQueueDepth = 4;
  input.finalizeSliceSteps = 1;

  const SyncDrainBudget budget =
      PersistenceSyncDrainBudget::buildSyncDrainBudget(input, Config::maxPersistenceMicrosPerLoop);

  TEST_ASSERT_EQUAL(105u, budget.expectedSliceSteps);
  TEST_ASSERT_EQUAL(189u, budget.maxSliceSteps);
  TEST_ASSERT_EQUAL(64u, budget.maxStuckIterations);
  TEST_ASSERT_EQUAL(567000u, budget.maxWallClockUs);
}

void test_made_sync_drain_progress_detects_queue_and_stage_changes() {
  SyncDrainProgressSnapshot before{};
  before.workQueueDepth = 2;
  before.bundleStage = 0;
  before.loopStage = 0;
  before.capturePassCursor = 0;
  before.pendingFlush = true;

  before.workItemActive = true;

  SyncDrainProgressSnapshot afterQueueDrain = before;
  afterQueueDrain.workQueueDepth = 1;
  TEST_ASSERT_TRUE(PersistenceSyncDrainBudget::madeSyncDrainProgress(before, afterQueueDrain));

  SyncDrainProgressSnapshot afterStage = before;
  afterStage.loopStage = 1;
  TEST_ASSERT_TRUE(PersistenceSyncDrainBudget::madeSyncDrainProgress(before, afterStage));

  SyncDrainProgressSnapshot afterPendingClear = before;
  afterPendingClear.pendingFlush = false;
  TEST_ASSERT_TRUE(PersistenceSyncDrainBudget::madeSyncDrainProgress(before, afterPendingClear));

  SyncDrainProgressSnapshot noChange = before;
  TEST_ASSERT_FALSE(PersistenceSyncDrainBudget::madeSyncDrainProgress(before, noChange));
}

void test_runtime_bundle_estimates_are_non_zero() {
  const uint32_t sliceSteps = PersistenceSyncDrainBudget::estimateRuntimeBundleSliceSteps(
      Config::NUM_TRACKS, Config::MAX_LOOPS_PER_TRACK);
  const uint32_t sdBytes = PersistenceSyncDrainBudget::estimateRuntimeBundleSdBytes(
      Config::NUM_TRACKS, Config::MAX_LOOPS_PER_TRACK);

  TEST_ASSERT_GREATER_THAN(0u, sliceSteps);
  TEST_ASSERT_GREATER_THAN(0u, sdBytes);
}

int main(int /*argc*/, char** /*argv*/) {
  UNITY_BEGIN();
  RUN_TEST(test_loop_persist_slice_steps_scales_with_chunks);
  RUN_TEST(test_build_sync_drain_budget_applies_headroom);
  RUN_TEST(test_made_sync_drain_progress_detects_queue_and_stage_changes);
  RUN_TEST(test_runtime_bundle_estimates_are_non_zero);
  return UNITY_END();
}
