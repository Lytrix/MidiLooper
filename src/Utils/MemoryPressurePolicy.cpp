//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "Utils/MemoryPressurePolicy.h"

#include <cstdio>

#include "Globals.h"

#include <cstdio>

const char* memoryPressureLevelName(MemoryPressureLevel level) {
  switch (level) {
    case MemoryPressureLevel::Normal:
      return "Normal";
    case MemoryPressureLevel::Low:
      return "Low";
    case MemoryPressureLevel::Critical:
      return "Critical";
  }
  return "Unknown";
}

namespace MemoryPressurePolicy {

namespace {

uint16_t chunkHeadroomThreshold() {
  return static_cast<uint16_t>(Config::CHUNK_POOL_RESERVE + Config::HEAP_PRESSURE_CHUNK_MARGIN);
}

bool isCriticalHeap(uint32_t heapFreeBytes) {
  return heapFreeBytes < Config::HEAP_RESERVE_BYTES;
}

bool isCriticalChunks(uint16_t chunksFree) {
  return chunksFree <= Config::CHUNK_POOL_RESERVE;
}

}  // namespace

bool chunkHeadroomOk(uint16_t chunksFree) {
  return chunksFree > chunkHeadroomThreshold();
}

MemoryPressureLevel classifyRawLevel(const Inputs& inputs) {
  if (inputs.captureAppendFailedLatch || isCriticalHeap(inputs.heapFreeBytes) ||
      isCriticalChunks(inputs.chunksFree)) {
    return MemoryPressureLevel::Critical;
  }
  if (inputs.heapFreeBytes >= Config::HEAP_PRESSURE_NORMAL_ENTER_BYTES &&
      chunkHeadroomOk(inputs.chunksFree)) {
    return MemoryPressureLevel::Normal;
  }
  return MemoryPressureLevel::Low;
}

MemoryPressureLevel stepLevel(MemoryPressureLevel current, const Inputs& inputs, uint32_t nowMs,
                              uint32_t& lowExitStableSinceMs) {
  const MemoryPressureLevel raw = classifyRawLevel(inputs);

  if (static_cast<uint8_t>(raw) > static_cast<uint8_t>(current)) {
    lowExitStableSinceMs = 0;
    return raw;
  }

  if (raw == current) {
    if (current == MemoryPressureLevel::Low &&
        inputs.heapFreeBytes >= Config::HEAP_PRESSURE_LOW_EXIT_BYTES && chunkHeadroomOk(inputs.chunksFree)) {
      if (lowExitStableSinceMs == 0) {
        lowExitStableSinceMs = nowMs;
      }
      if (nowMs - lowExitStableSinceMs >= Config::HEAP_PRESSURE_HYSTERESIS_MS) {
        lowExitStableSinceMs = 0;
        return MemoryPressureLevel::Normal;
      }
    } else if (current != MemoryPressureLevel::Normal) {
      lowExitStableSinceMs = 0;
    }
    return current;
  }

  // De-escalate one step (raw ordinal < current).
  if (current == MemoryPressureLevel::Critical) {
    if (inputs.heapFreeBytes >= Config::HEAP_PRESSURE_CRITICAL_EXIT_BYTES &&
        chunkHeadroomOk(inputs.chunksFree) && !inputs.captureAppendFailedLatch) {
      lowExitStableSinceMs = 0;
      return MemoryPressureLevel::Low;
    }
    return current;
  }

  if (current == MemoryPressureLevel::Low) {
    if (inputs.heapFreeBytes >= Config::HEAP_PRESSURE_LOW_EXIT_BYTES && chunkHeadroomOk(inputs.chunksFree)) {
      if (lowExitStableSinceMs == 0) {
        lowExitStableSinceMs = nowMs;
      }
      if (nowMs - lowExitStableSinceMs >= Config::HEAP_PRESSURE_HYSTERESIS_MS) {
        lowExitStableSinceMs = 0;
        return MemoryPressureLevel::Normal;
      }
    } else {
      lowExitStableSinceMs = 0;
    }
    return current;
  }

  return current;
}

void formatTransitionLabel(MemoryPressureLevel from, MemoryPressureLevel to, char* out,
                           size_t outSize) {
  if (out == nullptr || outSize == 0) {
    return;
  }
  std::snprintf(out, outSize, "%s->%s", memoryPressureLevelName(from), memoryPressureLevelName(to));
}

}  // namespace MemoryPressurePolicy
