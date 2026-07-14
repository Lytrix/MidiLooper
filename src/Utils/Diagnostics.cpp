//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "Utils/Diagnostics.h"

#ifdef SESSION_CAPTURE

#include <Arduino.h>
#include <cstring>

#include "EditManager.h"
#include "EditSession.h"
#include "LooperState.h"
#include "TrackManager.h"
#include "TrackState.h"
#include "Utils/DebugSessionCapture.h"
#include "Utils/MemoryMonitor.h"

#if defined(__IMXRT1062__)
extern "C" void* extmem_malloc(size_t size);
extern "C" void extmem_free(void* ptr);
#define DIAG_MEM_ATTR FLASHMEM
#else
#define DIAG_MEM_ATTR
#endif

namespace Diagnostics {

namespace {

DiagLastRecordSlot* sLastRecordSlot = nullptr;
uint32_t sCounters[static_cast<size_t>(Counter::Count)] = {};
uint32_t sTimingSumMicros[static_cast<size_t>(Timing::Count)] = {};
uint32_t sTimingSampleCount[static_cast<size_t>(Timing::Count)] = {};

constexpr const char* kCounterNames[] = {
    "PlaybackWindowRebuild",
    "PlaybackDeferredReuse",
    "PlaybackFullMaterialize",
    "LegacyMidiEvents",
    "DisplayFullRebuild",
    "DisplayIncrementalUpdate",
    "Materialize",
    "CacheInvalidateBroad",
    "CacheInvalidateScoped",
    "AllocatorFailure",
    "SessionUndoPush",
};

constexpr const char* kTimingNames[] = {
    "PlaybackBuildTime",
    "DisplayBuildTime",
};

#if defined(__IMXRT1062__)
uint32_t readMicros() { return micros(); }
#else
uint32_t readMicros() { return 0; }
#endif

bool ensureLastRecordSlot() {
  if (sLastRecordSlot != nullptr) {
    return true;
  }
#if defined(__IMXRT1062__)
  sLastRecordSlot =
      static_cast<DiagLastRecordSlot*>(extmem_malloc(sizeof(DiagLastRecordSlot)));
  if (sLastRecordSlot == nullptr) {
    return false;
  }
  std::memset(sLastRecordSlot, 0, sizeof(DiagLastRecordSlot));
  return true;
#else
  static DiagLastRecordSlot fallback{};
  sLastRecordSlot = &fallback;
  return true;
#endif
}

DIAG_MEM_ATTR DiagContextSnapshot captureContextSnapshot() {
  DiagContextSnapshot context{};
  const Track& track = trackManager.getSelectedTrack();
  context.trackState = static_cast<uint8_t>(track.getState());
  context.editSession = static_cast<uint8_t>(editManager.getEditSessionType());
  context.looperState = static_cast<uint8_t>(looperState.getLooperState());
  if (looperState.isLoadSaveModeActive()) {
    context.flags |= kFlagLoadSaveOverlay;
  }
  return context;
}

DIAG_MEM_ATTR void writeLastRecord(const DiagTraceRecord& record) {
  if (!ensureLastRecordSlot()) {
    return;
  }
  sLastRecordSlot->magic = kLastRecordMagic;
  sLastRecordSlot->record = record;
}

DIAG_MEM_ATTR void appendTraceRecord(const DiagTraceRecord& record) {
  writeLastRecord(record);
#if DIAG_LEVEL >= 2
  (void)DebugSessionCapture::appendDiagTraceRecord(&record, sizeof(record));
#endif
}

}  // namespace

DIAG_MEM_ATTR void init() {
  (void)ensureLastRecordSlot();
  DebugSessionCapture::initCaptureBuffer();
  std::memset(sCounters, 0, sizeof(sCounters));
  std::memset(sTimingSumMicros, 0, sizeof(sTimingSumMicros));
  std::memset(sTimingSampleCount, 0, sizeof(sTimingSampleCount));
}

void emitBootCheckpoint() {
  if (!ensureLastRecordSlot() || sLastRecordSlot->magic != kLastRecordMagic) {
    return;
  }
  DebugSessionCapture::emitDiagCheckpointLine(sLastRecordSlot->record);
  sLastRecordSlot->magic = 0;
}

void flushTraceRecords(size_t maxRecords) {
  DebugSessionCapture::flushCaptureBuffer(maxRecords);
}

DIAG_MEM_ATTR void recordEvent(uint16_t eventId, uint32_t payload, bool includeHeapSnapshot) {
  DiagTraceRecord record{};
  record.micros = readMicros();
  record.eventId = eventId;
  record.context = captureContextSnapshot();
  record.payload = payload;
  if (includeHeapSnapshot) {
    record.recordFlags |= kRecordHasHeapSnapshot;
    record.heapFree = MemoryMonitor::getInternalHeapFreeBytes();
    record.heapUsed = MemoryMonitor::getInternalHeapUsedBytes();
    record.extmemFree = MemoryMonitor::getExternalMemoryPoolFreeBytes();
  }
  appendTraceRecord(record);
}

DIAG_MEM_ATTR void recordMemorySnapshot(uint16_t eventId, uint32_t payload) {
  recordEvent(eventId, payload, true);
}

DIAG_MEM_ATTR void incrementCounter(Counter counter, uint32_t delta) {
  const size_t index = static_cast<size_t>(counter);
  if (index >= static_cast<size_t>(Counter::Count)) {
    return;
  }
  sCounters[index] += delta;
}

DIAG_MEM_ATTR uint32_t readCounter(Counter counter) {
  const size_t index = static_cast<size_t>(counter);
  if (index >= static_cast<size_t>(Counter::Count)) {
    return 0;
  }
  return sCounters[index];
}

DIAG_MEM_ATTR void recordTimingSample(Timing timing, uint32_t elapsedMicros) {
  const size_t index = static_cast<size_t>(timing);
  if (index >= static_cast<size_t>(Timing::Count)) {
    return;
  }
  sTimingSumMicros[index] += elapsedMicros;
  ++sTimingSampleCount[index];
}

DIAG_MEM_ATTR uint32_t readTimingSumMicros(Timing timing) {
  const size_t index = static_cast<size_t>(timing);
  if (index >= static_cast<size_t>(Timing::Count)) {
    return 0;
  }
  return sTimingSumMicros[index];
}

DIAG_MEM_ATTR uint32_t readTimingSampleCount(Timing timing) {
  const size_t index = static_cast<size_t>(timing);
  if (index >= static_cast<size_t>(Timing::Count)) {
    return 0;
  }
  return sTimingSampleCount[index];
}

DIAG_MEM_ATTR const char* counterName(Counter counter) {
  const size_t index = static_cast<size_t>(counter);
  if (index >= static_cast<size_t>(Counter::Count)) {
    return "Unknown";
  }
  return kCounterNames[index];
}

DIAG_MEM_ATTR const char* timingName(Timing timing) {
  const size_t index = static_cast<size_t>(timing);
  if (index >= static_cast<size_t>(Timing::Count)) {
    return "Unknown";
  }
  return kTimingNames[index];
}

DIAG_MEM_ATTR void emitArchitectureMetricsSnapshot() {
  for (size_t index = 0; index < static_cast<size_t>(Counter::Count); ++index) {
    const Counter counter = static_cast<Counter>(index);
    DebugSessionCapture::architectureCounter(counterName(counter), sCounters[index]);
  }
  for (size_t index = 0; index < static_cast<size_t>(Timing::Count); ++index) {
    const Timing timing = static_cast<Timing>(index);
    DebugSessionCapture::architectureTiming(timingName(timing), sTimingSumMicros[index],
                                            sTimingSampleCount[index]);
  }
}

}  // namespace Diagnostics

#endif  // SESSION_CAPTURE
