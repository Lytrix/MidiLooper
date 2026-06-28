//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "Utils/HotPathTelemetry.h"

#if defined(PERF_TELEMETRY)

#include <Arduino.h>
#include <cstring>

#if defined(__IMXRT1062__)
#define PERF_MEM_ATTR FLASHMEM
#define PERF_DATA_ATTR DMAMEM
#else
#define PERF_MEM_ATTR
#define PERF_DATA_ATTR
#endif

namespace HotPathTelemetry {

namespace {

constexpr uint32_t kOverdubStartBudgetUs = 1500;
constexpr uint32_t kUndoSnapshotBudgetUs = 2000;
constexpr uint32_t kDisplayUpdateBudgetUs = 30000;
constexpr uint32_t kSaveStateBudgetUs = 150000;

struct TelemetryState {
  Metric overdubStartMetric;
  Metric overdubSessionOpenMetric;
  Metric undoSnapshotMetric;
  Metric displayUpdateMetric;
  Metric saveStateMetric;
  uint32_t saveStateFailures = 0;
  uint32_t maxUndoDepth = 0;
  SnapshotStats snapshotStats;
  char deferredSummaryCheckpoint[32] = {};
  bool deferredSummaryPending = false;
};

PERF_DATA_ATTR TelemetryState telemetryState;

void updateMetric(Metric& metric, uint32_t elapsedUs, uint32_t budgetUs) {
  metric.samples++;
  metric.totalUs += elapsedUs;
  if (elapsedUs > metric.maxUs) metric.maxUs = elapsedUs;
  if (elapsedUs > budgetUs) metric.overBudget++;
}

uint32_t averageUs(const Metric& metric) {
  if (metric.samples == 0) return 0;
  return static_cast<uint32_t>(metric.totalUs / metric.samples);
}

}  // namespace

PERF_MEM_ATTR void reset() {
  telemetryState = {};
}

PERF_MEM_ATTR void recordOverdubStart(uint32_t elapsedUs, uint32_t sourceEvents, uint32_t undoDepth) {
  updateMetric(telemetryState.overdubStartMetric, elapsedUs, kOverdubStartBudgetUs);
  if (sourceEvents > telemetryState.snapshotStats.maxSourceEvents) {
    telemetryState.snapshotStats.maxSourceEvents = sourceEvents;
  }
  if (undoDepth > telemetryState.maxUndoDepth) {
    telemetryState.maxUndoDepth = undoDepth;
  }
}

PERF_MEM_ATTR void recordOverdubSessionOpen(uint32_t elapsedUs, uint32_t sourceEvents) {
  updateMetric(telemetryState.overdubSessionOpenMetric, elapsedUs, kOverdubStartBudgetUs);
  if (sourceEvents > telemetryState.snapshotStats.maxSourceEvents) {
    telemetryState.snapshotStats.maxSourceEvents = sourceEvents;
  }
}

PERF_MEM_ATTR void recordUndoSnapshot(uint32_t elapsedUs, uint32_t sourceEvents,
                                      uint32_t copiedEvents) {
  updateMetric(telemetryState.undoSnapshotMetric, elapsedUs, kUndoSnapshotBudgetUs);
  telemetryState.snapshotStats.samples++;
  if (sourceEvents > telemetryState.snapshotStats.maxSourceEvents) {
    telemetryState.snapshotStats.maxSourceEvents = sourceEvents;
  }
  if (copiedEvents > telemetryState.snapshotStats.maxCopiedEvents) {
    telemetryState.snapshotStats.maxCopiedEvents = copiedEvents;
  }
  if (copiedEvents < sourceEvents) {
    telemetryState.snapshotStats.droppedEvents += (sourceEvents - copiedEvents);
  }
}

PERF_MEM_ATTR void recordSaveState(uint32_t elapsedUs, bool ok) {
  updateMetric(telemetryState.saveStateMetric, elapsedUs, kSaveStateBudgetUs);
  if (!ok) telemetryState.saveStateFailures++;
}

PERF_MEM_ATTR void recordDisplayUpdate(uint32_t elapsedUs) {
  updateMetric(telemetryState.displayUpdateMetric, elapsedUs, kDisplayUpdateBudgetUs);
}

PERF_MEM_ATTR void emitSummary(const char* checkpoint) {
  Serial.printf(
      "PERF,%s,overdub_start[s=%lu,max=%lu,avg=%lu,over=%lu],"
      "undo_snapshot[s=%lu,max=%lu,avg=%lu,over=%lu,max_src=%lu,max_copied=%lu,dropped=%lu],"
      "save_state[s=%lu,max=%lu,avg=%lu,over=%lu,fail=%lu],"
      "display[s=%lu,max=%lu,avg=%lu,over=%lu],max_undo_depth=%lu\n",
      checkpoint ? checkpoint : "unknown",
      static_cast<unsigned long>(telemetryState.overdubStartMetric.samples),
      static_cast<unsigned long>(telemetryState.overdubStartMetric.maxUs),
      static_cast<unsigned long>(averageUs(telemetryState.overdubStartMetric)),
      static_cast<unsigned long>(telemetryState.overdubStartMetric.overBudget),
      static_cast<unsigned long>(telemetryState.undoSnapshotMetric.samples),
      static_cast<unsigned long>(telemetryState.undoSnapshotMetric.maxUs),
      static_cast<unsigned long>(averageUs(telemetryState.undoSnapshotMetric)),
      static_cast<unsigned long>(telemetryState.undoSnapshotMetric.overBudget),
      static_cast<unsigned long>(telemetryState.snapshotStats.maxSourceEvents),
      static_cast<unsigned long>(telemetryState.snapshotStats.maxCopiedEvents),
      static_cast<unsigned long>(telemetryState.snapshotStats.droppedEvents),
      static_cast<unsigned long>(telemetryState.saveStateMetric.samples),
      static_cast<unsigned long>(telemetryState.saveStateMetric.maxUs),
      static_cast<unsigned long>(averageUs(telemetryState.saveStateMetric)),
      static_cast<unsigned long>(telemetryState.saveStateMetric.overBudget),
      static_cast<unsigned long>(telemetryState.saveStateFailures),
      static_cast<unsigned long>(telemetryState.displayUpdateMetric.samples),
      static_cast<unsigned long>(telemetryState.displayUpdateMetric.maxUs),
      static_cast<unsigned long>(averageUs(telemetryState.displayUpdateMetric)),
      static_cast<unsigned long>(telemetryState.displayUpdateMetric.overBudget),
      static_cast<unsigned long>(telemetryState.maxUndoDepth));
}

PERF_MEM_ATTR void requestDeferredSummary(const char* checkpoint) {
  if (checkpoint == nullptr) {
    telemetryState.deferredSummaryCheckpoint[0] = '\0';
  } else {
    strncpy(telemetryState.deferredSummaryCheckpoint, checkpoint,
            sizeof(telemetryState.deferredSummaryCheckpoint) - 1);
    telemetryState.deferredSummaryCheckpoint[sizeof(telemetryState.deferredSummaryCheckpoint) - 1] =
        '\0';
  }
  telemetryState.deferredSummaryPending = true;
}

PERF_MEM_ATTR void processDeferredSummary() {
  if (!telemetryState.deferredSummaryPending) {
    return;
  }
  telemetryState.deferredSummaryPending = false;
  emitSummary(telemetryState.deferredSummaryCheckpoint[0] ? telemetryState.deferredSummaryCheckpoint
                                                          : "deferred");
}

PERF_MEM_ATTR ScopedSaveState::ScopedSaveState() : startUs(micros()), ok_(false) {}

PERF_MEM_ATTR ScopedSaveState::~ScopedSaveState() {
  const uint32_t elapsedUs = micros() - startUs;
  recordSaveState(elapsedUs, ok_);
}

PERF_MEM_ATTR void ScopedSaveState::setOk(bool ok) { ok_ = ok; }

}  // namespace HotPathTelemetry

#endif

