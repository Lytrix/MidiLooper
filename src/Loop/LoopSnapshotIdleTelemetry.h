//  Copyright (c)  2026 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// Shared surface of the snapshot idle/unit telemetry whose implementation lives in
// `LoopSnapshotIdleTelemetry.cpp`: the publish path accumulates the idle children
// and the telemetry TU resets, emits and exposes them.
//
// Snapshot idle/unit instrumentation is deliberately gated on the broad `SESSION_CAPTURE` flag, not
// on `SESSION_CAPTURE_SNAPSHOT_IDLE`: the probes must be available in any capture build without
// changing the env of a running session, and their volume is bounded by the step-report interval and
// the phase/stall/signature conditions in the telemetry TU. DEC-076 records the decision; the
// reader stays narrow.

#pragma once

#include "LoopSnapshot.h"

#if defined(SESSION_CAPTURE)
#include <cstdint>

#if defined(ARDUINO)
#include <Arduino.h>
#endif

struct SnapshotIdleChildTimes {
  uint32_t startUs = 0;
  uint32_t passChangeUs = 0;
  uint32_t stepUs = 0;
  uint32_t finishUs = 0;
  uint32_t publishUs = 0;
  uint32_t probeUs = 0;
  uint32_t enqueueUs = 0;
  uint32_t discardUs = 0;
  uint32_t emitUs = 0;
  uint32_t refillUs = 0;
  uint32_t eventScanUs = 0;
  uint32_t noteLookupUs = 0;
  uint32_t changeBuildUs = 0;
  uint32_t constructUs = 0;
};

/// One instance shared by both TUs: the publish path accumulates one child per timed region and the
/// telemetry TU resets, emits and reads the totals.
inline SnapshotIdleChildTimes snapshotIdleChildTimes;

/// Scoped accumulator for one idle child region: adds the elapsed microseconds to its field on
/// destruction.
struct SnapshotIdleChildAdd {
  uint32_t& field;
  uint32_t startUs;
  explicit SnapshotIdleChildAdd(uint32_t& target) : field(target), startUs(micros()) {}
  ~SnapshotIdleChildAdd() { field += micros() - startUs; }
};

/// Zero every child and leave the one-shot `micros()` resolution probe in place.
void resetSnapshotIdleChildTimes();
#endif

#if defined(SESSION_CAPTURE) && defined(ARDUINO)
struct Loop;
struct PendingSnapshotPublishBox;

void emitSnapshotUnitCapture(const Loop& loop, const char* kind, uint32_t heapBefore);
void emitSnapshotUnitStepCapture(const Loop& loop, uint32_t heapBefore);
void emitSnapshotFragmentFailure(const PendingSnapshotPublishBox& box,
                                 LoopSnapshotFailureReason reason);
#endif
