//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "Utils/MemoryMonitor.h"
#include "Logger.h"
#include <Arduino.h>

#if defined(__IMXRT1062__)  // Teensy 4.x

// Linker symbols from imxrt1062_t41.ld
extern "C" {
  extern char *__brkval;
  extern unsigned long _heap_start;
  extern unsigned long _heap_end;
}

namespace MemoryMonitor {

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

bool isLowMemory(uint32_t thresholdBytes) {
  return getFreeHeap() < thresholdBytes;
}

void logStatus() {
  uint32_t freeK = getFreeHeap() / 1024;
  uint32_t totalK = getTotalHeap() / 1024;
  uint32_t usedK = getUsedHeap() / 1024;
  logger.log(CAT_GENERAL, LOG_INFO,
             "[Memory] free=%lu KB used=%lu KB total=%lu KB",
             (unsigned long)freeK, (unsigned long)usedK, (unsigned long)totalK);
  if (isLowMemory(20 * 1024)) {
    logger.log(CAT_GENERAL, LOG_WARNING, "[Memory] Low heap - consider reducing undo/loops");
  }
}

}  // namespace MemoryMonitor

#else

namespace MemoryMonitor {

uint32_t getFreeHeap() { return 0; }
uint32_t getTotalHeap() { return 0; }
uint32_t getUsedHeap() { return 0; }
bool isLowMemory(uint32_t) { return false; }
void logStatus() {}

}  // namespace MemoryMonitor

#endif
