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

  SyncDrainProgressSnapshot afterPayloadSlice = before;
  afterPayloadSlice.workItemSliceCount = 1;
  TEST_ASSERT_TRUE(PersistenceSyncDrainBudget::madeSyncDrainProgress(before, afterPayloadSlice));

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

// ---------------------------------------------------------------------------
// Packet C / A3.3 — restore work in the drain's budget and progress accounting.
//
// `SyncDrainBudgetInput` and `SyncDrainProgressSnapshot` carried persistence fields only, so a large
// restore could exhaust `maxSliceSteps` or look stalled while legitimately progressing, and the stuck
// detector could abort a drain that was making real progress (design.md D4 rule 4, G2 finding 3).
// These cases are the pure-decision oracles for that accounting; the integration claim ("the drain
// advances the restore") is the A3.7 device gate per the Revision 5 split.
// ---------------------------------------------------------------------------

/// Restore work is charged in the drain's step budget and byte estimate, so `maxSliceSteps` covers it.
void test_drain_budget_includes_restore_work() {
  PersistenceSyncDrainBudget::SyncDrainBudgetInput withoutRestore{};
  withoutRestore.queuedWorkSliceSteps = 100;
  withoutRestore.queuedWorkSdBytes = 4096;

  PersistenceSyncDrainBudget::SyncDrainBudgetInput withRestore = withoutRestore;
  withRestore.restoreSliceSteps = 50;
  withRestore.restoreSdBytes = 2048;

  const SyncDrainBudget baseline =
      PersistenceSyncDrainBudget::buildSyncDrainBudget(withoutRestore, Config::maxPersistenceMicrosPerLoop);
  const SyncDrainBudget extended =
      PersistenceSyncDrainBudget::buildSyncDrainBudget(withRestore, Config::maxPersistenceMicrosPerLoop);

  TEST_ASSERT_GREATER_THAN(baseline.expectedSliceSteps, extended.expectedSliceSteps);
  TEST_ASSERT_GREATER_THAN(baseline.estimatedSdPayloadBytes, extended.estimatedSdPayloadBytes);
  TEST_ASSERT_GREATER_THAN(baseline.maxSliceSteps, extended.maxSliceSteps);
}

/// **Restore motion alone counts as drain progress.** Without this, a drain advancing a multi-slice
/// restore trips the stuck detector while making real progress.
void test_made_sync_drain_progress_counts_restore_motion() {
  SyncDrainProgressSnapshot before{};
  // No persistence work at all: the drain is waiting on the restore, which is the C5 shape.
  before.restoreProgressCounter = 10;

  SyncDrainProgressSnapshot afterSlice = before;
  afterSlice.restoreProgressCounter = 11;
  TEST_ASSERT_TRUE_MESSAGE(
      PersistenceSyncDrainBudget::madeSyncDrainProgress(before, afterSlice),
      "restore motion must count as drain progress");

  SyncDrainProgressSnapshot afterRestorePhase = before;
  afterRestorePhase.restorePhase = 1;
  TEST_ASSERT_TRUE_MESSAGE(
      PersistenceSyncDrainBudget::madeSyncDrainProgress(before, afterRestorePhase),
      "a restore phase change must count as drain progress");

  // A restore that advanced nothing is still not progress.
  TEST_ASSERT_FALSE(PersistenceSyncDrainBudget::madeSyncDrainProgress(before, before));
}

/// A restore-only drain is still bounded: the budget covers the restore work instead of being sized by
/// persistence alone, and its wall-clock cap remains finite.
void test_restore_only_drain_budget_is_bounded_not_zero() {
  PersistenceSyncDrainBudget::SyncDrainBudgetInput input{};
  input.restoreSliceSteps = 200;
  input.restoreSdBytes = 65536;
  input.restoreOnlyBlocker = true;

  const SyncDrainBudget budget =
      PersistenceSyncDrainBudget::buildSyncDrainBudget(input, Config::maxPersistenceMicrosPerLoop);

  TEST_ASSERT_GREATER_THAN(0u, budget.expectedSliceSteps);
  TEST_ASSERT_GREATER_THAN(0u, budget.maxSliceSteps);
  TEST_ASSERT_GREATER_THAN(0u, budget.maxWallClockUs);
  TEST_ASSERT_GREATER_THAN(0u, budget.estimatedSdPayloadBytes);
}

int main(int /*argc*/, char** /*argv*/) {
  UNITY_BEGIN();
  RUN_TEST(test_loop_persist_slice_steps_scales_with_chunks);
  RUN_TEST(test_build_sync_drain_budget_applies_headroom);
  RUN_TEST(test_made_sync_drain_progress_detects_queue_and_stage_changes);
  RUN_TEST(test_runtime_bundle_estimates_are_non_zero);
  // A3.3 — restore work in the drain budget and progress accounting.
  RUN_TEST(test_drain_budget_includes_restore_work);
  RUN_TEST(test_made_sync_drain_progress_counts_restore_motion);
  RUN_TEST(test_restore_only_drain_budget_is_bounded_not_zero);
  return UNITY_END();
}
