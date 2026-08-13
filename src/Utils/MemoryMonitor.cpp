//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "Utils/MemoryMonitor.h"
#if defined(__IMXRT1062__)
#include "Globals.h"
#include "Utils/InternalHeapFirstAllocator.h"
#include "Utils/MemoryPool.h"
#include "Logger.h"
#include <Arduino.h>

#include "LoopEventStore.h"
#include "PersistenceQueue.h"
#include "Utils/DebugSessionCapture.h"
#include "Utils/MemoryPressurePolicy.h"

#include <smalloc.h>

// Linker symbols from imxrt1062_t41.ld
extern "C" {
  extern char *__brkval;
  extern unsigned long _heap_start;
  extern unsigned long _heap_end;
  extern uint8_t external_psram_size;  // MB detected at startup (Teensy core)
}

namespace MemoryMonitor {

namespace {

uint32_t sMinEverFreeBytes = UINT32_MAX;

void updateInternalHeapWatermark(uint32_t freeBytes) {
  if (freeBytes < sMinEverFreeBytes) {
    sMinEverFreeBytes = freeBytes;
  }
}

void getPsramStats(size_t* totalUsed, size_t* totalFree) {
  if (totalUsed) *totalUsed = 0;
  if (totalFree) *totalFree = 0;
  if (extmem_smalloc_pool.pool_size == 0 || extmem_smalloc_pool.pool == nullptr) {
    return;
  }
  // sm_malloc_stats_pool always writes *free (and uses *total) even when those
  // out-params are null — pass stack locals and copy out selectively.
  size_t total = 0;
  size_t userUsed = 0;
  size_t freeBytes = 0;
  int blocks = 0;
  // sm_malloc_stats_pool returns 1 when blocks exist, 0 when pool is empty but valid,
  // -1 on verify failure. Either non-negative rc fills freeBytes (= pool_size - total).
  const int rc = sm_malloc_stats_pool(&extmem_smalloc_pool, &total, &userUsed, &freeBytes, &blocks);
  if (rc >= 0) {
    if (totalUsed) *totalUsed = total;
    if (totalFree) *totalFree = freeBytes;
  }
  (void)userUsed;
  (void)blocks;
}

}  // namespace

uint32_t getInternalHeapFreeBytes() {
  int32_t freeBytes = reinterpret_cast<char*>(&_heap_end) - __brkval;
  const uint32_t free = freeBytes > 0 ? static_cast<uint32_t>(freeBytes) : 0;
  updateInternalHeapWatermark(free);
  return free;
}

uint32_t getInternalHeapMinEverFreeBytes() {
  if (sMinEverFreeBytes == UINT32_MAX) {
    return getInternalHeapFreeBytes();
  }
  return sMinEverFreeBytes;
}

void resetInternalHeapWatermark() {
  sMinEverFreeBytes = UINT32_MAX;
  (void)getInternalHeapFreeBytes();
}

uint32_t getInternalHeapTotalBytes() {
  return static_cast<uint32_t>(reinterpret_cast<char*>(&_heap_end) -
                               reinterpret_cast<char*>(&_heap_start));
}

uint32_t getInternalHeapUsedBytes() {
  uint32_t total = getInternalHeapTotalBytes();
  uint32_t free = getInternalHeapFreeBytes();
  return total > free ? total - free : 0;
}

bool isExternalMemoryPoolAvailable() {
  return external_psram_size > 0 && extmem_smalloc_pool.pool_size > 0 &&
         extmem_smalloc_pool.pool != nullptr;
}

uint32_t getExternalMemoryPoolTotalBytes() {
  return static_cast<uint32_t>(extmem_smalloc_pool.pool_size);
}

uint32_t getExternalMemoryPoolFreeBytes() {
  size_t freeBytes = 0;
  getPsramStats(nullptr, &freeBytes);
  return static_cast<uint32_t>(freeBytes);
}

uint32_t getExternalMemoryPoolUsedBytes() {
  size_t usedBytes = 0;
  getPsramStats(&usedBytes, nullptr);
  return static_cast<uint32_t>(usedBytes);
}

bool isLowMemory(uint32_t thresholdBytes) {
  return getInternalHeapFreeBytes() < thresholdBytes;
}

void logStatus(bool includeExternalPoolUsage) {
  const uint32_t freeK = getInternalHeapFreeBytes() / 1024;
  const uint32_t totalK = getInternalHeapTotalBytes() / 1024;
  const uint32_t usedK = getInternalHeapUsedBytes() / 1024;
  const uint32_t minEverK = getInternalHeapMinEverFreeBytes() / 1024;
  logger.log(CAT_GENERAL, LOG_INFO,
             "[Memory] heap free=%lu used=%lu total=%lu KB min_ever=%lu KB",
             (unsigned long)freeK, (unsigned long)usedK, (unsigned long)totalK,
             (unsigned long)minEverK);
  if (!isExternalMemoryPoolAvailable()) {
    logger.log(CAT_GENERAL, LOG_INFO, "[Memory] psram unavailable");
  } else if (includeExternalPoolUsage) {
    logger.log(CAT_GENERAL, LOG_INFO,
               "[Memory] psram chip=%u MB free=%lu used=%lu pool=%lu KB",
               (unsigned)external_psram_size,
               (unsigned long)(getExternalMemoryPoolFreeBytes() / 1024),
               (unsigned long)(getExternalMemoryPoolUsedBytes() / 1024),
               (unsigned long)(getExternalMemoryPoolTotalBytes() / 1024));
  } else {
    // pool_size only — free/used walk the header chain and must not run once the
    // transport or an external clock can be live (141815: 593 ms, MIDI clock lost).
    logger.log(CAT_GENERAL, LOG_INFO, "[Memory] psram chip=%u MB pool=%lu KB",
               (unsigned)external_psram_size,
               (unsigned long)(getExternalMemoryPoolTotalBytes() / 1024));
  }
  if (isLowMemory(20 * 1024)) {
    logger.log(CAT_GENERAL, LOG_WARNING, "[Memory] Low heap - consider reducing undo/loops");
  }
}

void logStatusAtAddedNotes(uint32_t addedNoteOns, size_t loopEventCount,
                           const void* loopEventsData, size_t loopChunkRefCount,
                           bool loopChunkBacked) {
  const uint32_t freeK = getInternalHeapFreeBytes() / 1024;
  const uint32_t totalK = getInternalHeapTotalBytes() / 1024;
  const uint32_t usedK = getInternalHeapUsedBytes() / 1024;

  const auto poolStats = MemoryPool::globalMidiEventPool.getStats();
  const char* loopStorage = "empty";
  if (loopChunkBacked && loopChunkRefCount > 0) {
    loopStorage = "chunk_refs";
  } else if (loopEventCount > 0 && loopEventsData != nullptr) {
    loopStorage = isInExternalMemoryPool(loopEventsData) ? "psram" : "heap";
  }

  logger.log(CAT_GENERAL, LOG_INFO,
             "[Memory] notes=%lu heap free=%lu used=%lu total=%lu KB",
             (unsigned long)addedNoteOns,
             (unsigned long)freeK, (unsigned long)usedK, (unsigned long)totalK);

  // NOTE: do NOT call getExternalMemoryPoolFreeBytes()/getExternalMemoryPoolUsedBytes() here.
  // They walk the
  // entire PSRAM smalloc pool header chain (sm_malloc_stats_pool), which on an 8 MB
  // pool stalls the loop for hundreds of ms. This runs on the record/overdub note-on
  // hot path, so only O(1) fields are reported. Full free/used PSRAM stats are logged
  // off the hot path in logStatus() (boot/setup). pool=KB below is pool_size (O(1)).
  if (isExternalMemoryPoolAvailable()) {
    logger.log(CAT_GENERAL, LOG_INFO,
               "[Memory] notes=%lu psram chip=%u MB pool=%lu KB "
               "midi_pool=%zu/%zu loop_events=%zu loop_chunks=%zu loop_buf=%s",
               (unsigned long)addedNoteOns,
               (unsigned)external_psram_size,
               (unsigned long)(getExternalMemoryPoolTotalBytes() / 1024),
               poolStats.first, poolStats.second,
               loopEventCount, loopChunkRefCount, loopStorage);
  } else {
    logger.log(CAT_GENERAL, LOG_INFO,
               "[Memory] notes=%lu psram unavailable midi_pool=%zu/%zu "
               "loop_events=%zu loop_chunks=%zu loop_buf=%s",
               (unsigned long)addedNoteOns,
               poolStats.first, poolStats.second,
               loopEventCount, loopChunkRefCount, loopStorage);
  }

  if (isLowMemory(20 * 1024)) {
    logger.log(CAT_GENERAL, LOG_WARNING, "[Memory] Low heap - consider reducing undo/loops");
  }
}

#define PRESSURE_MEM FLASHMEM

namespace {

MemoryPressureLevel sAdvisoryLevel = MemoryPressureLevel::Normal;
uint32_t sLowExitStableSinceMs = 0;
bool sCaptureAppendFailedLatch = false;
uint32_t sCaptureAppendFailedLatchSinceMs = 0;

MemoryPressurePolicy::Inputs gatherPressureInputs(uint32_t nowMs) {
  MemoryPressurePolicy::Inputs inputs{};
  inputs.heapFreeBytes = MemoryMonitor::getInternalHeapFreeBytes();
  inputs.chunksFree = LoopEventStore::freeChunkCount();
  inputs.persistQueueDepth = PersistenceQueue::queueDepth();
  (void)nowMs;
  if (sCaptureAppendFailedLatch) {
    inputs.captureAppendFailedLatch = true;
  }
  return inputs;
}

void maybeClearCaptureAppendFailedLatch(const MemoryPressurePolicy::Inputs& inputs,
                                        uint32_t nowMs) {
  if (!sCaptureAppendFailedLatch) {
    return;
  }
  const bool headroomRecovered =
      MemoryPressurePolicy::chunkHeadroomOk(inputs.chunksFree) &&
      inputs.heapFreeBytes >= Config::HEAP_PRESSURE_CRITICAL_EXIT_BYTES;
  const bool latchTimedOut =
      sCaptureAppendFailedLatchSinceMs != 0 &&
      nowMs - sCaptureAppendFailedLatchSinceMs >= Config::HEAP_PRESSURE_CAPTURE_APPEND_LATCH_MS;
  if (headroomRecovered || latchTimedOut) {
    sCaptureAppendFailedLatch = false;
    sCaptureAppendFailedLatchSinceMs = 0;
  }
}

PRESSURE_MEM void emitPressureTransition(MemoryPressureLevel from, MemoryPressureLevel to,
                                         const MemoryPressurePolicy::Inputs& inputs) {
  char transition[32];
  MemoryPressurePolicy::formatTransitionLabel(from, to, transition, sizeof(transition));
  SC_MEMORY_PRESSURE(transition, inputs.heapFreeBytes, inputs.chunksFree,
                     inputs.persistQueueDepth);
}

}  // namespace

PRESSURE_MEM MemoryPressureLevel getAdvisoryPressureLevel() { return sAdvisoryLevel; }

PRESSURE_MEM void notifyCaptureAppendFailed(uint32_t nowMs) {
  const bool wasLatched = sCaptureAppendFailedLatch;
  sCaptureAppendFailedLatch = true;
  sCaptureAppendFailedLatchSinceMs = nowMs;
#if defined(SESSION_CAPTURE)
  if (!wasLatched) {
    MemoryPressurePolicy::Inputs inputs = gatherPressureInputs(nowMs);
    SC_MEMORY_PRESSURE("latch,Critical", inputs.heapFreeBytes, inputs.chunksFree,
                       inputs.persistQueueDepth);
  }
#endif
}

PRESSURE_MEM void updateAdvisoryPressureLevel(uint32_t nowMs) {
  MemoryPressurePolicy::Inputs inputs = gatherPressureInputs(nowMs);
  maybeClearCaptureAppendFailedLatch(inputs, nowMs);
  inputs = gatherPressureInputs(nowMs);

  const MemoryPressureLevel previous = sAdvisoryLevel;
  sAdvisoryLevel =
      MemoryPressurePolicy::stepLevel(previous, inputs, nowMs, sLowExitStableSinceMs);

  if (sAdvisoryLevel != previous) {
    emitPressureTransition(previous, sAdvisoryLevel, inputs);
  }
}

#undef PRESSURE_MEM

}  // namespace MemoryMonitor

#elif defined(PIO_UNIT_TEST_NATIVE)

#include "Globals.h"
#include "Utils/MemoryPressurePolicy.h"

namespace MemoryMonitor {

uint32_t g_nativeTestFreeHeap = UINT32_MAX;
bool g_nativeTestHeapOverride = false;

void setNativeTestFreeHeap(uint32_t bytes) {
  g_nativeTestFreeHeap = bytes;
  g_nativeTestHeapOverride = true;
}

void resetNativeTestFreeHeap() {
  g_nativeTestHeapOverride = false;
  g_nativeTestFreeHeap = UINT32_MAX;
}

uint32_t getInternalHeapFreeBytes() {
  return g_nativeTestHeapOverride ? g_nativeTestFreeHeap : UINT32_MAX;
}
uint32_t getInternalHeapTotalBytes() { return UINT32_MAX; }
uint32_t getInternalHeapUsedBytes() { return 0; }
bool isExternalMemoryPoolAvailable() { return false; }
uint32_t getExternalMemoryPoolTotalBytes() { return 0; }
uint32_t getExternalMemoryPoolFreeBytes() { return 0; }
uint32_t getExternalMemoryPoolUsedBytes() { return 0; }
bool isLowMemory(uint32_t) { return false; }
uint32_t getInternalHeapMinEverFreeBytes() { return getInternalHeapFreeBytes(); }
void resetInternalHeapWatermark() {}
void logStatus(bool) {}
void logStatusAtAddedNotes(uint32_t, size_t, const void*, size_t, bool) {}

namespace {

MemoryPressureLevel sAdvisoryLevel = MemoryPressureLevel::Normal;
uint32_t sLowExitStableSinceMs = 0;
bool sCaptureAppendFailedLatch = false;
uint32_t sCaptureAppendFailedLatchSinceMs = 0;
uint16_t g_nativeTestChunksFree = 512;
uint16_t g_nativeTestPersistQueueDepth = 0;
bool g_nativeTestPersistQueueOverride = false;

MemoryPressurePolicy::Inputs gatherPressureInputs(uint32_t nowMs) {
  MemoryPressurePolicy::Inputs inputs{};
  inputs.heapFreeBytes = getInternalHeapFreeBytes();
  inputs.chunksFree = g_nativeTestChunksFree;
  inputs.persistQueueDepth =
      g_nativeTestPersistQueueOverride ? g_nativeTestPersistQueueDepth : 0;
  (void)nowMs;
  if (sCaptureAppendFailedLatch) {
    inputs.captureAppendFailedLatch = true;
  }
  return inputs;
}

void maybeClearCaptureAppendFailedLatch(const MemoryPressurePolicy::Inputs& inputs,
                                        uint32_t nowMs) {
  if (!sCaptureAppendFailedLatch) {
    return;
  }
  const bool headroomRecovered =
      MemoryPressurePolicy::chunkHeadroomOk(inputs.chunksFree) &&
      inputs.heapFreeBytes >= Config::HEAP_PRESSURE_CRITICAL_EXIT_BYTES;
  const bool latchTimedOut =
      sCaptureAppendFailedLatchSinceMs != 0 &&
      nowMs - sCaptureAppendFailedLatchSinceMs >= Config::HEAP_PRESSURE_CAPTURE_APPEND_LATCH_MS;
  if (headroomRecovered || latchTimedOut) {
    sCaptureAppendFailedLatch = false;
    sCaptureAppendFailedLatchSinceMs = 0;
  }
}

}  // namespace

MemoryPressureLevel getAdvisoryPressureLevel() { return sAdvisoryLevel; }

void notifyCaptureAppendFailed(uint32_t nowMs) {
  sCaptureAppendFailedLatch = true;
  sCaptureAppendFailedLatchSinceMs = nowMs;
}

void updateAdvisoryPressureLevel(uint32_t nowMs) {
  MemoryPressurePolicy::Inputs inputs = gatherPressureInputs(nowMs);
  maybeClearCaptureAppendFailedLatch(inputs, nowMs);
  inputs = gatherPressureInputs(nowMs);

  const MemoryPressureLevel previous = sAdvisoryLevel;
  sAdvisoryLevel =
      MemoryPressurePolicy::stepLevel(previous, inputs, nowMs, sLowExitStableSinceMs);
}

void setNativeTestChunksFree(uint16_t chunksFree) { g_nativeTestChunksFree = chunksFree; }

void setNativeTestPersistQueueDepth(uint16_t depth) {
  g_nativeTestPersistQueueDepth = depth;
  g_nativeTestPersistQueueOverride = true;
}

void resetNativeTestPressureInputs() {
  g_nativeTestChunksFree = 512;
  g_nativeTestPersistQueueDepth = 0;
  g_nativeTestPersistQueueOverride = false;
  sAdvisoryLevel = MemoryPressureLevel::Normal;
  sLowExitStableSinceMs = 0;
  sCaptureAppendFailedLatch = false;
  sCaptureAppendFailedLatchSinceMs = 0;
}

}  // namespace MemoryMonitor

#else

namespace MemoryMonitor {

uint32_t getInternalHeapFreeBytes() { return 0; }
uint32_t getInternalHeapTotalBytes() { return 0; }
uint32_t getInternalHeapUsedBytes() { return 0; }
bool isExternalMemoryPoolAvailable() { return false; }
uint32_t getExternalMemoryPoolTotalBytes() { return 0; }
uint32_t getExternalMemoryPoolFreeBytes() { return 0; }
uint32_t getExternalMemoryPoolUsedBytes() { return 0; }
bool isLowMemory(uint32_t) { return false; }
uint32_t getInternalHeapMinEverFreeBytes() { return 0; }
void resetInternalHeapWatermark() {}
void logStatus(bool) {}
void logStatusAtAddedNotes(uint32_t, size_t, const void*, size_t, bool) {}

}  // namespace MemoryMonitor

#endif
