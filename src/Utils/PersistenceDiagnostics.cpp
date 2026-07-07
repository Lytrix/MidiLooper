//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "Utils/PersistenceDiagnostics.h"

#if defined(SESSION_CAPTURE)

#include "LoopEventStore.h"
#include "LoopPasses.h"
#include "PersistenceQueue.h"
#include "Utils/DebugSessionCapture.h"
#include <Arduino.h>

namespace PersistenceDiagnostics {
namespace {

struct State {
  uint32_t transportBlockCount = 0;
  uint32_t heapFloorBlockCount = 0;
  uint32_t budgetBlockCount = 0;
  uint32_t sliceDoneCount = 0;
  uint32_t peakWriterLatencyUs = 0;
  uint32_t maxDeferredBacklog = 0;
  uint32_t dirtySaveRequestedAtMs = 0;
  uint32_t lastPeriodicEmitMs = 0;
  bool poolPressureWarnActive = false;
};

State state;

uint32_t dirtySaveAgeMs() {
  if (state.dirtySaveRequestedAtMs == 0) {
    return 0;
  }
  const uint32_t nowMs = millis();
  return nowMs >= state.dirtySaveRequestedAtMs ? nowMs - state.dirtySaveRequestedAtMs : 0;
}

void emitDiagnosticLine(bool captureOrTransportActive, bool savePending, bool saveInProgress,
                        bool sdIoActive) {
  const uint16_t freeChunks = LoopEventStore::freeChunkCount();
  const uint16_t usedChunks = LoopEventStore::usedChunkCount();
  const uint16_t reserve = PassConfig::CHUNK_RESERVE;
  const uint16_t queueDepth = PersistenceQueue::queueDepth();
  const uint16_t writingChunks = PersistenceQueue::writingChunkCount();
  const uint32_t backlog = queueDepth;
  if (backlog > state.maxDeferredBacklog) {
    state.maxDeferredBacklog = backlog;
  }

  DebugSessionCapture::persistenceDiagnostic(
      freeChunks, usedChunks, reserve, queueDepth, writingChunks, state.transportBlockCount,
      state.heapFloorBlockCount, state.budgetBlockCount, state.sliceDoneCount,
      state.peakWriterLatencyUs, dirtySaveAgeMs(), state.maxDeferredBacklog,
      savePending ? 1U : 0U, saveInProgress ? 1U : 0U, captureOrTransportActive ? 1U : 0U);
  state.lastPeriodicEmitMs = millis();
}

}  // namespace

void onDeferredSaveRequested() {
  if (state.dirtySaveRequestedAtMs == 0) {
    state.dirtySaveRequestedAtMs = millis();
  }
}

void onTransportGateBlock() {
  ++state.transportBlockCount;
}

void onHeapFloorBlock() {
  ++state.heapFloorBlockCount;
}

void onBudgetBlock() {
  ++state.budgetBlockCount;
}

void onSliceCompleted(uint32_t sliceLatencyUs) {
  ++state.sliceDoneCount;
  if (sliceLatencyUs > state.peakWriterLatencyUs) {
    state.peakWriterLatencyUs = sliceLatencyUs;
  }
}

void maybeEmitPoolPressureWarning() {
  const uint16_t freeChunks = LoopEventStore::freeChunkCount();
  const uint16_t warnThreshold =
      static_cast<uint16_t>(PassConfig::CHUNK_RESERVE + PassConfig::CHUNK_RESERVE);
  const bool underWarn = freeChunks <= warnThreshold;
  if (underWarn && !state.poolPressureWarnActive) {
    state.poolPressureWarnActive = true;
    DebugSessionCapture::persistencePoolPressure(freeChunks, PassConfig::CHUNK_RESERVE,
                                                 LoopEventStore::usedChunkCount());
  } else if (!underWarn && state.poolPressureWarnActive) {
    state.poolPressureWarnActive = false;
  }
}

void maybeEmitPeriodic(bool captureOrTransportActive, bool savePending, bool saveInProgress,
                       bool sdIoActive) {
  maybeEmitPoolPressureWarning();

  const bool hasDeferredWork = savePending || saveInProgress;
  if (!captureOrTransportActive && !hasDeferredWork) {
    if (!hasDeferredWork) {
      state.dirtySaveRequestedAtMs = 0;
    }
    return;
  }

  const uint32_t nowMs = millis();
  const uint32_t intervalMs = captureOrTransportActive ? 5000U : 10000U;
  if (state.lastPeriodicEmitMs != 0 && (nowMs - state.lastPeriodicEmitMs) < intervalMs) {
    return;
  }

  emitDiagnosticLine(captureOrTransportActive, savePending, saveInProgress, sdIoActive);

  if (!hasDeferredWork) {
    state.dirtySaveRequestedAtMs = 0;
  }
}

}  // namespace PersistenceDiagnostics

#endif  // SESSION_CAPTURE
