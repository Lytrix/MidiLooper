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
  const uint32_t queuedAgeMs = PersistenceQueue::oldestQueuedChunkAgeMs();
  if (state.dirtySaveRequestedAtMs == 0) {
    return queuedAgeMs;
  }
  const uint32_t nowMs = millis();
  const uint32_t saveAgeMs =
      nowMs >= state.dirtySaveRequestedAtMs ? nowMs - state.dirtySaveRequestedAtMs : 0;
  return saveAgeMs > queuedAgeMs ? saveAgeMs : queuedAgeMs;
}

void emitDiagnosticLine(bool captureOrTransportActive, bool savePending, bool saveInProgress,
                        bool sdIoActive, const BacklogSnapshot* backlog) {
  const uint16_t freeChunks = LoopEventStore::freeChunkCount();
  const uint16_t usedChunks = LoopEventStore::usedChunkCount();
  const uint16_t reserve = PassConfig::CHUNK_RESERVE;
  const uint16_t queueDepth = PersistenceQueue::queueDepth();
  const uint16_t writingChunks = PersistenceQueue::writingChunkCount();
  const uint32_t backlogDepth = queueDepth;
  if (backlogDepth > state.maxDeferredBacklog) {
    state.maxDeferredBacklog = backlogDepth;
  }
  const uint32_t dirtyAgeMs = dirtySaveAgeMs();

  DebugSessionCapture::persistenceDiagnostic(
      freeChunks, usedChunks, reserve, queueDepth, writingChunks, state.transportBlockCount,
      state.heapFloorBlockCount, state.budgetBlockCount, state.sliceDoneCount,
      state.peakWriterLatencyUs, dirtyAgeMs, state.maxDeferredBacklog,
      savePending ? 1U : 0U, saveInProgress ? 1U : 0U, captureOrTransportActive ? 1U : 0U);

  if (backlog != nullptr) {
    DebugSessionCapture::persistenceBacklog(
        backlog->workQueueDepth, backlog->writingWorkItems, backlog->chunkQueueDepth, dirtyAgeMs,
        backlog->estSliceSteps, backlog->estSdBytes, savePending ? 1U : 0U,
        backlog->urgentRequested, state.transportBlockCount, state.budgetBlockCount,
        state.heapFloorBlockCount);
  }

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
                       bool sdIoActive, const BacklogSnapshot* backlog) {
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

  emitDiagnosticLine(captureOrTransportActive, savePending, saveInProgress, sdIoActive, backlog);

  if (!hasDeferredWork) {
    state.dirtySaveRequestedAtMs = 0;
  }
}

}  // namespace PersistenceDiagnostics

#endif  // SESSION_CAPTURE
