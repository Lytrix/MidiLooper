//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "PersistenceFailurePolicy.h"

#if defined(SESSION_CAPTURE) && defined(ARDUINO)
#include "LoopEventStore.h"
#include "PersistenceQueue.h"
#include "Utils/DebugSessionCapture.h"
#include "Utils/PersistenceDiagnostics.h"
#include <Arduino.h>
#include <cstdio>
#endif

namespace PersistenceFailurePolicy {

bool shouldRunMidPassWriter(uint16_t queueDepth, bool otherSdIoActive) {
  return queueDepth > 0 && !otherSdIoActive;
}

bool shouldRunPersistenceWorkItemWriter(uint16_t queueDepth, uint16_t writingWorkItemCount,
                                        bool otherSdIoActive, bool monolithInProgress) {
  return (queueDepth > 0 || writingWorkItemCount > 0) && !otherSdIoActive && !monolithInProgress;
}

CapturePressureAction evaluateCapturePressure(uint16_t freeChunks, uint16_t reserve,
                                              bool isRecording, bool isOverdubbing) {
  (void)isRecording;
  (void)isOverdubbing;
  const uint16_t warnThreshold = static_cast<uint16_t>(reserve + reserve);
  if (freeChunks <= reserve) {
    // At reserve: chunk admission is gated by canAllocChunkWithReserve; prioritize writer.
    return CapturePressureAction::PrioritizePersistence;
  }
  if (freeChunks <= warnThreshold) {
    return CapturePressureAction::TelemetryOnly;
  }
  return CapturePressureAction::None;
}

bool shouldEmitQueueDepthAlarm(uint16_t queueDepth, bool captureActive) {
  return captureActive && queueDepth >= kQueueDepthAlarmThreshold;
}

#if defined(SESSION_CAPTURE) && defined(ARDUINO)
namespace {

uint32_t lastQueueAlarmMs = 0;

}  // namespace

void maybeEmitBackpressureTelemetry(bool captureActive) {
  const uint16_t queueDepth = PersistenceQueue::queueDepth();
  const uint16_t freeChunks = LoopEventStore::freeChunkCount();
  const uint16_t reserve = PassConfig::CHUNK_RESERVE;

  PersistenceDiagnostics::maybeEmitPoolPressureWarning();

  if (shouldEmitQueueDepthAlarm(queueDepth, captureActive)) {
    const uint32_t nowMs = millis();
    if (nowMs - lastQueueAlarmMs >= 5000U) {
      lastQueueAlarmMs = nowMs;
      const uint32_t dirtyAgeMs = PersistenceQueue::oldestQueuedChunkAgeMs();
      char outcome[24];
      std::snprintf(outcome, sizeof(outcome), "q%u", static_cast<unsigned>(queueDepth));
      SC_PERSIST("queue_alarm", dirtyAgeMs, queueDepth, 0, outcome);
    }
  }

  if (freeChunks <= static_cast<uint16_t>(reserve + reserve)) {
    const CapturePressureAction action = evaluateCapturePressure(freeChunks, reserve, true, true);
    if (action == CapturePressureAction::PrioritizePersistence) {
      DebugSessionCapture::persistencePoolPressure(freeChunks, reserve,
                                                   LoopEventStore::usedChunkCount());
    }
  }
}
#endif

}  // namespace PersistenceFailurePolicy
