//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#pragma once

#include <cstdint>

#include "Globals.h"

namespace DeferredValidatePolicy {

/// True when a queued deferred full validate may run (PLAYING-only, age exceeded).
inline bool shouldRunDeferredFullValidate(bool queued, bool isRecording, bool isOverdubbing,
                                          uint32_t queuedAtMs, uint32_t nowMs) {
  if (!queued || isRecording || isOverdubbing) {
    return false;
  }
  if (queuedAtMs == 0) {
    return false;
  }
  return (nowMs - queuedAtMs) >= Config::deferredValidateMaxDelayMs;
}

}  // namespace
