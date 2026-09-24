//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include <unity.h>

#include "../../src/LoopEventStore.cpp"
#include "../../src/PersistenceSyncDrainBudget.cpp"

#include "LoopPasses.h"
#include "PersistenceDrainOutcome.h"
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

// ---------------------------------------------------------------------------
// Packet C / A3.2 — the named drain verdict.
//
// The drain collapsed "a restore holds the save gate" into one generic `BlockedBySlotLoad`, so a blocking
// CLEAR could not report *why*. The classification is a pure decision over the gate predicates plus the
// restore request's named wait, so it is testable natively even though the real drain cannot link
// (REVIEW.md A1.7). The verdict carries the name as well as the reason, so the ITCM drain's report site
// needs no branching.
// ---------------------------------------------------------------------------

using PersistenceSyncDrainBudget::SyncDrainVerdict;

/// Capture is its own reason and wins over the slot-load gate, matching the drain's own check order (a
/// recording track defers the load itself).
void test_drain_verdict_names_capture_before_slot_load() {
  const SyncDrainVerdict v = PersistenceSyncDrainBudget::classifySyncDrainVerdict(
      /*captureRecording=*/true, /*slotLoadActive=*/true, /*postLoadHoldoff=*/false,
      SyncDrainWait::kNone);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(SyncDrainFailureReason::BlockedByCapture),
                        static_cast<int>(v.reason));
  TEST_ASSERT_EQUAL_STRING("transport_active", v.gate);
}

/// A restore holding the save gate reports the request's **named wait**, and the verdict carries that
/// name so the drain reports *why*.
void test_drain_verdict_names_the_restore_wait_reason() {
  struct Case {
    uint8_t wait;
    const char* expected;
  };
  const Case cases[] = {
      {SyncDrainWait::kSlotOwned, "waiting_slot_owned"},
      {SyncDrainWait::kCapture, "waiting_capture"},
      {SyncDrainWait::kCommitGate, "waiting_commit_gate"},
      {SyncDrainWait::kJobCapacity, "waiting_job_capacity"},
      {SyncDrainWait::kRetry, "waiting_retry"},
  };
  for (const Case& c : cases) {
    const SyncDrainVerdict v = PersistenceSyncDrainBudget::classifySyncDrainVerdict(
        /*captureRecording=*/false, /*slotLoadActive=*/true, /*postLoadHoldoff=*/false, c.wait);
    TEST_ASSERT_EQUAL_INT_MESSAGE(static_cast<int>(SyncDrainFailureReason::BlockedByRestore),
                                  static_cast<int>(v.reason), c.expected);
    TEST_ASSERT_EQUAL_STRING_MESSAGE(c.expected, v.gate, c.expected);
  }
}

/// A slot load that is active but names **no** wait keeps the historical `BlockedBySlotLoad`, so no
/// existing report regresses to a less specific label.
void test_drain_verdict_keeps_slot_load_when_no_wait_is_named() {
  const SyncDrainVerdict v = PersistenceSyncDrainBudget::classifySyncDrainVerdict(
      false, true, false, SyncDrainWait::kNone);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(SyncDrainFailureReason::BlockedBySlotLoad),
                        static_cast<int>(v.reason));
  TEST_ASSERT_EQUAL_STRING("slot_load_active", v.gate);
}

/// The post-load holdoff is named, and is checked only after the slot-load gate.
void test_drain_verdict_names_the_post_load_holdoff() {
  const SyncDrainVerdict holdoff = PersistenceSyncDrainBudget::classifySyncDrainVerdict(
      false, false, true, SyncDrainWait::kNone);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(SyncDrainFailureReason::BlockedByPostLoadHoldoff),
                        static_cast<int>(holdoff.reason));
  TEST_ASSERT_EQUAL_STRING("post_load_holdoff", holdoff.gate);

  const SyncDrainVerdict slotLoadWins = PersistenceSyncDrainBudget::classifySyncDrainVerdict(
      false, true, true, SyncDrainWait::kNone);
  TEST_ASSERT_EQUAL_INT_MESSAGE(static_cast<int>(SyncDrainFailureReason::BlockedBySlotLoad),
                                static_cast<int>(slotLoadWins.reason),
                                "an active slot load outranks the holdoff, as the drain checks it");
}

/// `Stuck` is reserved for the case where **no** gate explains the lack of progress; nothing here may
/// collapse a named gate into it.
void test_drain_verdict_reserves_stuck_for_an_unexplained_gate() {
  const SyncDrainVerdict unexplained = PersistenceSyncDrainBudget::classifySyncDrainVerdict(
      false, false, false, SyncDrainWait::kNone);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(SyncDrainFailureReason::Stuck),
                        static_cast<int>(unexplained.reason));
  TEST_ASSERT_EQUAL_STRING("unexplained", unexplained.gate);

  const SyncDrainVerdict named[] = {
      PersistenceSyncDrainBudget::classifySyncDrainVerdict(true, false, false, SyncDrainWait::kNone),
      PersistenceSyncDrainBudget::classifySyncDrainVerdict(false, true, false, SyncDrainWait::kNone),
      PersistenceSyncDrainBudget::classifySyncDrainVerdict(false, false, true, SyncDrainWait::kNone),
      PersistenceSyncDrainBudget::classifySyncDrainVerdict(false, true, false, SyncDrainWait::kRetry),
  };
  for (const SyncDrainVerdict& v : named) {
    TEST_ASSERT_TRUE_MESSAGE(v.reason != SyncDrainFailureReason::Stuck,
                             "a named gate must never be reported as generic Stuck");
    TEST_ASSERT_TRUE_MESSAGE(v.gate != nullptr && v.gate[0] != '\0',
                             "every verdict must carry a reportable name");
  }
}

// ---------------------------------------------------------------------------
// Packet C / A3.4 — the caller contract.
//
// `saveState` returned only bool, and every `false` was treated as failure at both call sites, so a named
// *temporary* wait could not coexist with "the operation reported success". The drain now returns
// completed / temporarily blocked / failed, and the rule that reads it is a pure function, so it is
// pinned exactly here. The real drain's blocked classification comes from the A3.2 verdict (tested
// above), and the drain-integration/device half is A3.7.
// ---------------------------------------------------------------------------

/// **Only `Completed` proceeds.** A blocked drain is an explicit refusal — never silent success, never a
/// destructive license — and a failure is reported as a failure.
void test_a34_only_completed_licenses_the_destructive_operation() {
  TEST_ASSERT_EQUAL_INT(static_cast<int>(SyncDrainCallerAction::Proceed),
                        static_cast<int>(resolveDrainCallerAction(SyncDrainOutcome::Completed)));
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(SyncDrainCallerAction::RefuseBlocked),
      static_cast<int>(resolveDrainCallerAction(SyncDrainOutcome::TemporarilyBlocked)));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(SyncDrainCallerAction::ReportFailure),
                        static_cast<int>(resolveDrainCallerAction(SyncDrainOutcome::Failed)));

  TEST_ASSERT_FALSE_MESSAGE(drainOutcomeLeavesTargetUntouched(SyncDrainOutcome::Completed),
                            "only Completed may touch the target");
  TEST_ASSERT_TRUE_MESSAGE(drainOutcomeLeavesTargetUntouched(SyncDrainOutcome::TemporarilyBlocked),
                           "a blocked CLEAR must leave the slot untouched and the save owed");
  TEST_ASSERT_TRUE_MESSAGE(drainOutcomeLeavesTargetUntouched(SyncDrainOutcome::Failed),
                           "a failed drain must not license the destructive operation either");
}

/// The blocked wait and a real failure stay **distinguishable**, which is what the two caller log lines
/// rely on (a named refusal is an expected outcome; an abort is not).
void test_a34_blocked_and_failed_are_distinguishable() {
  TEST_ASSERT_TRUE(resolveDrainCallerAction(SyncDrainOutcome::TemporarilyBlocked) !=
                   resolveDrainCallerAction(SyncDrainOutcome::Failed));
  TEST_ASSERT_TRUE(drainOutcomeLeavesTargetUntouched(SyncDrainOutcome::TemporarilyBlocked));
  TEST_ASSERT_TRUE(drainOutcomeLeavesTargetUntouched(SyncDrainOutcome::Failed));
}

/// The blocked/failed split is driven by the **A3.2** named classification, so the verdict and the caller
/// contract cannot drift: every named gate is a temporary block, and `Stuck` is the failure side.
void test_a34_named_gates_are_temporary_blocks_not_failures() {
  const uint8_t waits[] = {SyncDrainWait::kSlotOwned, SyncDrainWait::kCapture,
                           SyncDrainWait::kCommitGate, SyncDrainWait::kJobCapacity,
                           SyncDrainWait::kRetry};
  for (uint8_t wait : waits) {
    const PersistenceSyncDrainBudget::SyncDrainVerdict v =
        PersistenceSyncDrainBudget::classifySyncDrainVerdict(false, true, false, wait);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(SyncDrainFailureReason::BlockedByRestore),
                          static_cast<int>(v.reason));
    TEST_ASSERT_TRUE_MESSAGE(v.gate != nullptr && v.gate[0] != '\0',
                             "a blocked verdict must name its reason for the refusal log");
  }
  TEST_ASSERT_EQUAL_INT(static_cast<int>(SyncDrainFailureReason::Stuck),
                        static_cast<int>(PersistenceSyncDrainBudget::classifySyncDrainVerdict(
                                             false, false, false, SyncDrainWait::kNone)
                                             .reason));
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
  // A3.2 — the named drain verdict.
  RUN_TEST(test_drain_verdict_names_capture_before_slot_load);
  RUN_TEST(test_drain_verdict_names_the_restore_wait_reason);
  RUN_TEST(test_drain_verdict_keeps_slot_load_when_no_wait_is_named);
  RUN_TEST(test_drain_verdict_names_the_post_load_holdoff);
  RUN_TEST(test_drain_verdict_reserves_stuck_for_an_unexplained_gate);
  // A3.4 — the caller contract.
  RUN_TEST(test_a34_only_completed_licenses_the_destructive_operation);
  RUN_TEST(test_a34_blocked_and_failed_are_distinguishable);
  RUN_TEST(test_a34_named_gates_are_temporary_blocks_not_failures);
  return UNITY_END();
}
