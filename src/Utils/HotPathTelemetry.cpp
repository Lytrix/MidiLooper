//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "Utils/HotPathTelemetry.h"

#if defined(PERF_TELEMETRY)

#include <Arduino.h>
#include <cstring>

namespace HotPathTelemetry {

namespace {

constexpr uint32_t kOverdubStartBudgetUs = 1500;
constexpr uint32_t kUndoSnapshotBudgetUs = 2000;
constexpr uint32_t kDisplayUpdateBudgetUs = 30000;
constexpr uint32_t kSaveStateBudgetUs = 150000;

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

void reset() {
  overdubStartMetric = {};
  overdubSessionOpenMetric = {};
  undoSnapshotMetric = {};
  displayUpdateMetric = {};
  saveStateMetric = {};
  saveStateFailures = 0;
  maxUndoDepth = 0;
  snapshotStats = {};
  deferredSummaryCheckpoint[0] = '\0';
  deferredSummaryPending = false;
}

void recordOverdubStart(uint32_t elapsedUs, uint32_t sourceEvents, uint32_t undoDepth) {
  updateMetric(overdubStartMetric, elapsedUs, kOverdubStartBudgetUs);
  if (sourceEvents > snapshotStats.maxSourceEvents) {
    snapshotStats.maxSourceEvents = sourceEvents;
  }
  if (undoDepth > maxUndoDepth) {
    maxUndoDepth = undoDepth;
  }
}

void recordOverdubSessionOpen(uint32_t elapsedUs, uint32_t sourceEvents) {
  updateMetric(overdubSessionOpenMetric, elapsedUs, kOverdubStartBudgetUs);
  if (sourceEvents > snapshotStats.maxSourceEvents) {
    snapshotStats.maxSourceEvents = sourceEvents;
  }
}

void recordUndoSnapshot(uint32_t elapsedUs, uint32_t sourceEvents, uint32_t copiedEvents) {
  updateMetric(undoSnapshotMetric, elapsedUs, kUndoSnapshotBudgetUs);
  snapshotStats.samples++;
  if (sourceEvents > snapshotStats.maxSourceEvents) {
    snapshotStats.maxSourceEvents = sourceEvents;
  }
  if (copiedEvents > snapshotStats.maxCopiedEvents) {
    snapshotStats.maxCopiedEvents = copiedEvents;
  }
  if (copiedEvents < sourceEvents) {
    snapshotStats.droppedEvents += (sourceEvents - copiedEvents);
  }
}

void recordSaveState(uint32_t elapsedUs, bool ok) {
  updateMetric(saveStateMetric, elapsedUs, kSaveStateBudgetUs);
  if (!ok) saveStateFailures++;
}

void recordDisplayUpdate(uint32_t elapsedUs) {
  updateMetric(displayUpdateMetric, elapsedUs, kDisplayUpdateBudgetUs);
}

void emitSummary(const char* checkpoint) {
  Serial.printf(
      "PERF,%s,overdub_start[s=%lu,max=%lu,avg=%lu,over=%lu],"
      "undo_snapshot[s=%lu,max=%lu,avg=%lu,over=%lu,max_src=%lu,max_copied=%lu,dropped=%lu],"
      "save_state[s=%lu,max=%lu,avg=%lu,over=%lu,fail=%lu],"
      "display[s=%lu,max=%lu,avg=%lu,over=%lu],max_undo_depth=%lu\n",
      checkpoint ? checkpoint : "unknown",
      static_cast<unsigned long>(overdubStartMetric.samples),
      static_cast<unsigned long>(overdubStartMetric.maxUs),
      static_cast<unsigned long>(averageUs(overdubStartMetric)),
      static_cast<unsigned long>(overdubStartMetric.overBudget),
      static_cast<unsigned long>(undoSnapshotMetric.samples),
      static_cast<unsigned long>(undoSnapshotMetric.maxUs),
      static_cast<unsigned long>(averageUs(undoSnapshotMetric)),
      static_cast<unsigned long>(undoSnapshotMetric.overBudget),
      static_cast<unsigned long>(snapshotStats.maxSourceEvents),
      static_cast<unsigned long>(snapshotStats.maxCopiedEvents),
      static_cast<unsigned long>(snapshotStats.droppedEvents),
      static_cast<unsigned long>(saveStateMetric.samples),
      static_cast<unsigned long>(saveStateMetric.maxUs),
      static_cast<unsigned long>(averageUs(saveStateMetric)),
      static_cast<unsigned long>(saveStateMetric.overBudget),
      static_cast<unsigned long>(saveStateFailures),
      static_cast<unsigned long>(displayUpdateMetric.samples),
      static_cast<unsigned long>(displayUpdateMetric.maxUs),
      static_cast<unsigned long>(averageUs(displayUpdateMetric)),
      static_cast<unsigned long>(displayUpdateMetric.overBudget),
      static_cast<unsigned long>(maxUndoDepth));
}

void requestDeferredSummary(const char* checkpoint) {
  if (checkpoint == nullptr) {
    deferredSummaryCheckpoint[0] = '\0';
  } else {
    strncpy(deferredSummaryCheckpoint, checkpoint, sizeof(deferredSummaryCheckpoint) - 1);
    deferredSummaryCheckpoint[sizeof(deferredSummaryCheckpoint) - 1] = '\0';
  }
  deferredSummaryPending = true;
}

void processDeferredSummary() {
  if (!deferredSummaryPending) {
    return;
  }
  deferredSummaryPending = false;
  emitSummary(deferredSummaryCheckpoint[0] ? deferredSummaryCheckpoint : "deferred");
}

ScopedSaveState::ScopedSaveState() : startUs(micros()), ok_(false) {}

ScopedSaveState::~ScopedSaveState() {
  const uint32_t elapsedUs = micros() - startUs;
  recordSaveState(elapsedUs, ok_);
}

void ScopedSaveState::setOk(bool ok) { ok_ = ok; }

}  // namespace HotPathTelemetry

#endif

