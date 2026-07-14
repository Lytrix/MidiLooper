//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#pragma once

#include <cstddef>
#include <cstdint>

#include "Utils/MemoryPressureLevel.h"

namespace MemoryPressurePolicy {

struct Inputs {
  uint32_t heapFreeBytes = 0;
  uint16_t chunksFree = 0;
  uint16_t persistQueueDepth = 0;
  bool captureAppendFailedLatch = false;
};

bool chunkHeadroomOk(uint16_t chunksFree);

/** Instant classification without hysteresis (escalation / telemetry). */
MemoryPressureLevel classifyRawLevel(const Inputs& inputs);

/**
 * Apply hysteresis when de-escalating; escalate immediately when raw > current.
 * @p lowExitStableSinceMs 0 when not counting; set/ cleared by this function.
 */
MemoryPressureLevel stepLevel(MemoryPressureLevel current, const Inputs& inputs, uint32_t nowMs,
                              uint32_t& lowExitStableSinceMs);

void formatTransitionLabel(MemoryPressureLevel from, MemoryPressureLevel to, char* out,
                           size_t outSize);

}  // namespace MemoryPressurePolicy
