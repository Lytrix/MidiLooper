//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#pragma once

#include <cstdint>

namespace PersistenceDiagnostics {

/// Work-queue + sync-drain estimate snapshot for periodic #CAP,PERS,backlog lines.
struct BacklogSnapshot {
  uint16_t workQueueDepth = 0;
  uint16_t writingWorkItems = 0;
  uint16_t chunkQueueDepth = 0;
  uint32_t estSliceSteps = 0;
  uint32_t estSdBytes = 0;
  uint8_t urgentRequested = 0;
};

/// The declared persistence bar: the same 50 ms one-shot alarm `loop_rem` carries. A single step that
/// exceeds it cannot be bounded by the between-step budget and must be **named** (report-only).
constexpr uint32_t kPersistenceStepBarUs = 50000u;

/// True when a single step must be named: it exceeded the active slice budget (the between-step bound)
/// or the declared bar. Naming changes no admission, budget or order.
inline bool persistenceStepBreachExceeded(uint32_t stepUs, uint32_t budgetUs,
                                          uint32_t barUs = kPersistenceStepBarUs) {
  return stepUs > budgetUs || stepUs > barUs;
}

/// The indivisible steps the deferred save scheduler can execute between budget checks.
enum class PersistenceStep : uint8_t {
  RevisionCommit = 0,
  RevisionLoad = 1,
  MidPassChunk = 2,
  WorkItem = 3,
  Count
};

const char* persistenceStepName(PersistenceStep step);

/// The row cadence: a step is named at most once per 5 s. `lastMs` is per-step state (0 = never named).
/// A within-budget step never emits. This is the exact "one row per step per interval, not per step"
/// rule the scheduler's report uses.
constexpr uint32_t kPersistenceStepRowIntervalMs = 5000u;
inline bool persistenceStepShouldEmit(uint32_t stepUs, uint32_t budgetUs, uint32_t nowMs,
                                      uint32_t& lastMs) {
  if (!persistenceStepBreachExceeded(stepUs, budgetUs)) {
    return false;
  }
  if (lastMs != 0 && nowMs - lastMs < kPersistenceStepRowIntervalMs) {
    return false;
  }
  lastMs = nowMs;
  return true;
}

/// Report one step's duration when it breached the budget or the bar (one row per step per 5 s).
/// Report-only: never allocates, never runs on the stop path, and never changes the step.
void onPersistenceStep(PersistenceStep step, uint32_t stepUs, uint32_t budgetUs, bool indivisible);

/// Mark first deferred-save request while work remains outstanding (dirty-save age proxy).
void onDeferredSaveRequested();

/// Deferred writer returned without dispatching because capture/overdub is active.
void onTransportGateBlock();

/// Deferred writer deferred dispatch because internal heap is below the safety floor.
void onHeapFloorBlock();

/// Slice loop exited with deferred work remaining because the time budget was exhausted.
void onBudgetBlock();

/// One deferred-save finite-state-machine sub-step completed.
void onSliceCompleted(uint32_t sliceLatencyUs);

/// Emit pool-pressure warning when free chunks approach reserve (rate-limited).
void maybeEmitPoolPressureWarning();

/**
 * Periodic persistence diagnostic line when capture is active or deferred save is pending.
 * Phase 0: queue depth proxies deferred-save backlog (chunk queue arrives in Phase 2).
 */
void maybeEmitPeriodic(bool captureOrTransportActive, bool savePending, bool saveInProgress,
                       bool sdIoActive, const BacklogSnapshot* backlog = nullptr);

#if !defined(SESSION_CAPTURE)
#if defined(PIO_UNIT_TEST_NATIVE)
/// Task 3.3's state-machine seam: a native fixture can force the duration the scheduler reports for a
/// step (the measured duration stays real) and read back every report the scheduler made. Firmware
/// builds never see either symbol.
struct PersistenceStepReportForTest {
  PersistenceStep step = PersistenceStep::RevisionCommit;
  uint32_t stepUs = 0;
  uint32_t budgetUs = 0;
  bool indivisible = false;
};

inline constexpr uint8_t kPersistenceStepReportCapacityForTest = 16;
inline PersistenceStepReportForTest sPersistenceStepReportsForTest[kPersistenceStepReportCapacityForTest]{};
inline uint8_t sPersistenceStepReportCountForTest = 0;
inline uint32_t sPersistenceStepDurationOverrideUsForTest[static_cast<uint8_t>(PersistenceStep::Count)] = {};

inline void resetPersistenceStepReportsForTest() { sPersistenceStepReportCountForTest = 0; }
inline uint8_t persistenceStepReportCountForTest() { return sPersistenceStepReportCountForTest; }
inline const PersistenceStepReportForTest& persistenceStepReportForTest(uint8_t index) {
  return sPersistenceStepReportsForTest[index];
}
inline uint32_t& persistenceStepDurationOverrideUsForTest(PersistenceStep step) {
  return sPersistenceStepDurationOverrideUsForTest[static_cast<uint8_t>(step)];
}
#endif
inline const char* persistenceStepName(PersistenceStep) { return "none"; }
inline void onPersistenceStep(PersistenceStep step, uint32_t stepUs, uint32_t budgetUs,
                              bool indivisible) {
#if defined(PIO_UNIT_TEST_NATIVE)
  if (sPersistenceStepReportCountForTest < kPersistenceStepReportCapacityForTest) {
    sPersistenceStepReportsForTest[sPersistenceStepReportCountForTest++] = {step, stepUs, budgetUs,
                                                                           indivisible};
  }
#else
  (void)step;
  (void)stepUs;
  (void)budgetUs;
  (void)indivisible;
#endif
}
inline void onDeferredSaveRequested() {}
inline void onTransportGateBlock() {}
inline void onHeapFloorBlock() {}
inline void onBudgetBlock() {}
inline void onSliceCompleted(uint32_t) {}
inline void maybeEmitPoolPressureWarning() {}
inline void maybeEmitPeriodic(bool, bool, bool, bool, const BacklogSnapshot*) {}
#endif

}  // namespace PersistenceDiagnostics
