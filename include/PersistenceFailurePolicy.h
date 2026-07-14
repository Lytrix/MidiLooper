//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#pragma once

#include <cstdint>

#include "LoopPasses.h"

namespace PersistenceFailurePolicy {

/// Emit `#CAP,PERS,queue_alarm` when queue depth exceeds this during capture.
constexpr uint16_t kQueueDepthAlarmThreshold = 8;

enum class CapturePressureAction : uint8_t {
  None = 0,
  TelemetryOnly,
  PrioritizePersistence,
};

/// True when the mid-pass writer may take one cooperative slice (queue non-empty, no SD conflict).
bool shouldRunMidPassWriter(uint16_t queueDepth, bool otherSdIoActive);

/// True when the semantic work-item writer may take one cooperative slice.
bool shouldRunPersistenceWorkItemWriter(uint16_t queueDepth, uint16_t writingWorkItemCount,
                                        bool otherSdIoActive);

/// Policy when free chunks approach reserve during capture (native-testable).
CapturePressureAction evaluateCapturePressure(uint16_t freeChunks, uint16_t reserve,
                                              bool isRecording, bool isOverdubbing);

bool shouldEmitQueueDepthAlarm(uint16_t queueDepth, bool captureActive);

#if defined(SESSION_CAPTURE) && defined(ARDUINO)
void maybeEmitBackpressureTelemetry(bool captureActive);
#else
inline void maybeEmitBackpressureTelemetry(bool) {}
#endif

}  // namespace PersistenceFailurePolicy
