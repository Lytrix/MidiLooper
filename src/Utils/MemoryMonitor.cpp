//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "Utils/MemoryMonitor.h"
#if defined(__IMXRT1062__)
#include "Utils/ExtMemAllocator.h"
#include "Utils/MemoryPool.h"
#include "Logger.h"
#include <Arduino.h>

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

uint32_t getFreeHeap() {
  int32_t freeBytes = reinterpret_cast<char*>(&_heap_end) - __brkval;
  return freeBytes > 0 ? static_cast<uint32_t>(freeBytes) : 0;
}

uint32_t getTotalHeap() {
  return static_cast<uint32_t>(reinterpret_cast<char*>(&_heap_end) -
                               reinterpret_cast<char*>(&_heap_start));
}

uint32_t getUsedHeap() {
  uint32_t total = getTotalHeap();
  uint32_t free = getFreeHeap();
  return total > free ? total - free : 0;
}

bool isPsramAvailable() {
  return external_psram_size > 0 && extmem_smalloc_pool.pool_size > 0 &&
         extmem_smalloc_pool.pool != nullptr;
}

uint32_t getPsramTotalBytes() {
  return static_cast<uint32_t>(extmem_smalloc_pool.pool_size);
}

uint32_t getPsramFreeBytes() {
  size_t freeBytes = 0;
  getPsramStats(nullptr, &freeBytes);
  return static_cast<uint32_t>(freeBytes);
}

uint32_t getPsramUsedBytes() {
  size_t usedBytes = 0;
  getPsramStats(&usedBytes, nullptr);
  return static_cast<uint32_t>(usedBytes);
}

bool isLowMemory(uint32_t thresholdBytes) {
  return getFreeHeap() < thresholdBytes;
}

void logStatus() {
  const uint32_t freeK = getFreeHeap() / 1024;
  const uint32_t totalK = getTotalHeap() / 1024;
  const uint32_t usedK = getUsedHeap() / 1024;
  logger.log(CAT_GENERAL, LOG_INFO,
             "[Memory] heap free=%lu used=%lu total=%lu KB",
             (unsigned long)freeK, (unsigned long)usedK, (unsigned long)totalK);
  if (isPsramAvailable()) {
    logger.log(CAT_GENERAL, LOG_INFO,
               "[Memory] psram chip=%u MB free=%lu used=%lu pool=%lu KB",
               (unsigned)external_psram_size,
               (unsigned long)(getPsramFreeBytes() / 1024),
               (unsigned long)(getPsramUsedBytes() / 1024),
               (unsigned long)(getPsramTotalBytes() / 1024));
  } else {
    logger.log(CAT_GENERAL, LOG_INFO, "[Memory] psram unavailable");
  }
  if (isLowMemory(20 * 1024)) {
    logger.log(CAT_GENERAL, LOG_WARNING, "[Memory] Low heap - consider reducing undo/loops");
  }
}

void logStatusAtAddedNotes(uint32_t addedNoteOns, size_t loopEventCount,
                           const void* loopEventsData) {
  const uint32_t freeK = getFreeHeap() / 1024;
  const uint32_t totalK = getTotalHeap() / 1024;
  const uint32_t usedK = getUsedHeap() / 1024;

  const auto poolStats = MemoryPool::globalMidiEventPool.getStats();
  const char* loopStorage = "empty";
  if (loopEventCount > 0 && loopEventsData != nullptr) {
    loopStorage = isInPsram(loopEventsData) ? "psram" : "heap";
  }

  logger.log(CAT_GENERAL, LOG_INFO,
             "[Memory] notes=%lu heap free=%lu used=%lu total=%lu KB",
             (unsigned long)addedNoteOns,
             (unsigned long)freeK, (unsigned long)usedK, (unsigned long)totalK);

  // NOTE: do NOT call getPsramFreeBytes()/getPsramUsedBytes() here. They walk the
  // entire PSRAM smalloc pool header chain (sm_malloc_stats_pool), which on an 8 MB
  // pool stalls the loop for hundreds of ms. This runs on the record/overdub note-on
  // hot path, so only O(1) fields are reported. Full free/used PSRAM stats are logged
  // off the hot path in logStatus() (boot/setup). pool=KB below is pool_size (O(1)).
  if (isPsramAvailable()) {
    logger.log(CAT_GENERAL, LOG_INFO,
               "[Memory] notes=%lu psram chip=%u MB pool=%lu KB "
               "midi_pool=%zu/%zu loop_events=%zu loop_buf=%s",
               (unsigned long)addedNoteOns,
               (unsigned)external_psram_size,
               (unsigned long)(getPsramTotalBytes() / 1024),
               poolStats.first, poolStats.second, loopEventCount, loopStorage);
  } else {
    logger.log(CAT_GENERAL, LOG_INFO,
               "[Memory] notes=%lu psram unavailable midi_pool=%zu/%zu loop_events=%zu loop_buf=%s",
               (unsigned long)addedNoteOns,
               poolStats.first, poolStats.second, loopEventCount, loopStorage);
  }

  if (isLowMemory(20 * 1024)) {
    logger.log(CAT_GENERAL, LOG_WARNING, "[Memory] Low heap - consider reducing undo/loops");
  }
}

}  // namespace MemoryMonitor

#elif defined(PIO_UNIT_TEST_NATIVE)

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

uint32_t getFreeHeap() {
  return g_nativeTestHeapOverride ? g_nativeTestFreeHeap : UINT32_MAX;
}
uint32_t getTotalHeap() { return UINT32_MAX; }
uint32_t getUsedHeap() { return 0; }
bool isPsramAvailable() { return false; }
uint32_t getPsramTotalBytes() { return 0; }
uint32_t getPsramFreeBytes() { return 0; }
uint32_t getPsramUsedBytes() { return 0; }
bool isLowMemory(uint32_t) { return false; }
void logStatus() {}
void logStatusAtAddedNotes(uint32_t, size_t, const void*) {}

}  // namespace MemoryMonitor

#else

namespace MemoryMonitor {

uint32_t getFreeHeap() { return 0; }
uint32_t getTotalHeap() { return 0; }
uint32_t getUsedHeap() { return 0; }
bool isPsramAvailable() { return false; }
uint32_t getPsramTotalBytes() { return 0; }
uint32_t getPsramFreeBytes() { return 0; }
uint32_t getPsramUsedBytes() { return 0; }
bool isLowMemory(uint32_t) { return false; }
void logStatus() {}
void logStatusAtAddedNotes(uint32_t, size_t, const void*) {}

}  // namespace MemoryMonitor

#endif
