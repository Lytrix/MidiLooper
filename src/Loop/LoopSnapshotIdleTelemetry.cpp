//  Copyright (c)  2026 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// Owner of the SESSION_CAPTURE-gated snapshot idle/unit telemetry moved out of
// `LoopSnapshotPublish.cpp`. The TU name matches the linker flash allow-list
// (`*LoopSnapshot*.o(.text .text.*)` in `linker/imxrt1062_t41_lcr.ld`), so every
// function here stays in FLASH exactly where the publish TU's code already was and
// none of them needs its own cold marker to stay out of ITCM.

#include "LoopSnapshotIdleTelemetry.h"

#include "Loop.h"
#include "LoopSnapshotBoxes.h"

#include "Utils/LoopMem.h"
#include "Utils/MemoryMonitor.h"

#if defined(SESSION_CAPTURE) && defined(ARDUINO)
#include "Utils/DebugSessionCapture.h"

#include <cstdio>
#endif

#if defined(SESSION_CAPTURE) && defined(ARDUINO)
LOOP_COLD_MEM __attribute__((noinline)) void emitSnapshotUnitCapture(
    const Loop& loop, const char* kind, uint32_t heapBefore) {
  const uint32_t heapAfter = MemoryMonitor::getInternalHeapFreeBytes();
  char line[192];
  snprintf(line, sizeof(line),
           "#CAP,%lu,DIAG,snapshot_unit,kind=%s,ph=%u,ld=%u,p=%u,q=%lu,a=%lu,"
           "if=%u,hb=%lu,ha=%lu,psram=%lu,n=%lu,b=%lu,s=%lu",
           static_cast<unsigned long>(micros()), kind != nullptr ? kind : "none",
           static_cast<unsigned>(loop.loadedSnapshotHydrationPhase()),
           loop.pendingSnapshotFromLoadedPasses() ? 1u : 0u,
           loop.hasPendingSnapshotPublish() ? 1u : 0u,
           static_cast<unsigned long>(loop.pendingSnapshotQueuedPassCount()),
           static_cast<unsigned long>(loop.pendingSnapshotAppliedPassCount()),
           loop.hasInFlightSnapshotConstruction() ? 1u : 0u,
           static_cast<unsigned long>(heapBefore),
           static_cast<unsigned long>(heapAfter),
           static_cast<unsigned long>(DebugSessionCapture::kUnsampledHeapBytes),
           static_cast<unsigned long>(snapshotAllocNodeCount()),
           static_cast<unsigned long>(snapshotAllocByteCount()),
           static_cast<unsigned long>(snapshotAllocSlabCount()));
  DebugSessionCapture::appendCaptureTextLine(line);
}

// `step` dominates snapshot_unit volume (`033502`: 4,991 of 5,134 lines) while
// start / finish / publish are the rare decisive events. Report steps on an interval
// so a construction that stops advancing is still visible without one line per unit.
constexpr uint32_t kSnapshotUnitStepReportIntervalMs = 25u;

LOOP_COLD_MEM __attribute__((noinline)) void emitSnapshotUnitStepCapture(const Loop& loop,
                                                                        uint32_t heapBefore) {
  static uint32_t sLastStepReportMs = 0;
  const uint32_t nowMs = millis();
  if (sLastStepReportMs != 0 && (nowMs - sLastStepReportMs) < kSnapshotUnitStepReportIntervalMs) {
    return;
  }
  sLastStepReportMs = nowMs;
  emitSnapshotUnitCapture(loop, "step", heapBefore);
}
#endif

namespace {

const char* snapshotFailureReasonName(LoopSnapshotFailureReason reason) {
  switch (reason) {
    case LoopSnapshotFailureReason::MembershipCapacity:
      return "membership_capacity";
    case LoopSnapshotFailureReason::Allocation:
      return "allocation";
    case LoopSnapshotFailureReason::Injected:
      return "injected";
    default:
      return "none";
  }
}

}  // namespace

#if defined(SESSION_CAPTURE) && defined(ARDUINO)
LOOP_COLD_MEM void emitSnapshotFragmentFailure(const PendingSnapshotPublishBox& box,
                                               LoopSnapshotFailureReason reason) {
  char line[176];
  snprintf(line, sizeof(line),
           "#CAP,%lu,DIAG,snapshot_fragment,fail,reason=%s,n=%u,pass=%u,next=%u,blocked=%u",
           static_cast<unsigned long>(micros()), snapshotFailureReasonName(reason),
           static_cast<unsigned>(box.consecutiveFragmentFailures),
           static_cast<unsigned>(box.constructionPassId),
           static_cast<unsigned>(box.passChangeProgress.nextEventId),
           box.fragmentBlocked ? 1u : 0u);
  DebugSessionCapture::appendCaptureTextLine(line);
}
#endif

#if defined(SESSION_CAPTURE)
void resetSnapshotIdleChildTimes() {
  snapshotIdleChildTimes = SnapshotIdleChildTimes{};
  const uint32_t probeA = micros();
  const uint32_t probeB = micros();
  snapshotIdleChildTimes.probeUs = probeB - probeA;
}
#endif

LOOP_COLD_MEM uint32_t Loop::lastSnapshotIdleStartUs() const {
#if defined(SESSION_CAPTURE)
  return snapshotIdleChildTimes.startUs;
#else
  return 0;
#endif
}

LOOP_COLD_MEM uint32_t Loop::lastSnapshotIdlePassChangeUs() const {
#if defined(SESSION_CAPTURE)
  return snapshotIdleChildTimes.passChangeUs;
#else
  return 0;
#endif
}

LOOP_COLD_MEM uint32_t Loop::lastSnapshotIdleStepUs() const {
#if defined(SESSION_CAPTURE)
  return snapshotIdleChildTimes.stepUs;
#else
  return 0;
#endif
}

LOOP_COLD_MEM uint32_t Loop::lastSnapshotIdleFinishUs() const {
#if defined(SESSION_CAPTURE)
  return snapshotIdleChildTimes.finishUs;
#else
  return 0;
#endif
}

LOOP_COLD_MEM uint32_t Loop::lastSnapshotIdlePublishUs() const {
#if defined(SESSION_CAPTURE)
  return snapshotIdleChildTimes.publishUs;
#else
  return 0;
#endif
}

LOOP_COLD_MEM uint32_t Loop::lastSnapshotIdleProbeUs() const {
#if defined(SESSION_CAPTURE)
  return snapshotIdleChildTimes.probeUs;
#else
  return 0;
#endif
}

LOOP_COLD_MEM uint32_t Loop::lastSnapshotIdleEnqueueUs() const {
#if defined(SESSION_CAPTURE)
  return snapshotIdleChildTimes.enqueueUs;
#else
  return 0;
#endif
}

LOOP_COLD_MEM uint32_t Loop::lastSnapshotIdleDiscardUs() const {
#if defined(SESSION_CAPTURE)
  return snapshotIdleChildTimes.discardUs;
#else
  return 0;
#endif
}

LOOP_COLD_MEM uint32_t Loop::lastSnapshotIdleEmitUs() const {
#if defined(SESSION_CAPTURE)
  return snapshotIdleChildTimes.emitUs;
#else
  return 0;
#endif
}

LOOP_COLD_MEM uint32_t Loop::lastSnapshotIdleRefillUs() const {
#if defined(SESSION_CAPTURE)
  return snapshotIdleChildTimes.refillUs;
#else
  return 0;
#endif
}

LOOP_COLD_MEM uint32_t Loop::lastSnapshotIdleEventScanUs() const {
#if defined(SESSION_CAPTURE)
  return snapshotIdleChildTimes.eventScanUs;
#else
  return 0;
#endif
}

LOOP_COLD_MEM uint32_t Loop::lastSnapshotIdleNoteLookupUs() const {
#if defined(SESSION_CAPTURE)
  return snapshotIdleChildTimes.noteLookupUs;
#else
  return 0;
#endif
}

LOOP_COLD_MEM uint32_t Loop::lastSnapshotIdleChangeBuildUs() const {
#if defined(SESSION_CAPTURE)
  return snapshotIdleChildTimes.changeBuildUs;
#else
  return 0;
#endif
}

LOOP_COLD_MEM uint32_t Loop::lastSnapshotIdleConstructUs() const {
#if defined(SESSION_CAPTURE)
  return snapshotIdleChildTimes.constructUs;
#else
  return 0;
#endif
}

LOOP_COLD_MEM void Loop::addSnapshotIdleEmitUs(uint32_t durationUs) {
#if defined(SESSION_CAPTURE)
  snapshotIdleChildTimes.emitUs += durationUs;
#else
  (void)durationUs;
#endif
}

