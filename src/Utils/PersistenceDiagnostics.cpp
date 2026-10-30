//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "Utils/PersistenceDiagnostics.h"

#include "Utils/TrackMem.h"

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

#if defined(__IMXRT1062__)
/// Task 3.1's per-step row rate limit. RAM2 (DMAMEM) so the named-step report cannot cross the DTCM
/// align edge; 4 entries, one per indivisible step.
DMAMEM uint32_t sLastStepRowMs[static_cast<uint8_t>(PersistenceStep::Count)] = {};
#else
uint32_t sLastStepRowMs[static_cast<uint8_t>(PersistenceStep::Count)] = {};
#endif

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

const char* persistenceStepName(PersistenceStep step) {
  switch (step) {
    case PersistenceStep::RevisionCommit:
      return "revision_commit";
    case PersistenceStep::RevisionLoad:
      return "revision_load";
    case PersistenceStep::MidPassChunk:
      return "mid_pass_chunk";
    case PersistenceStep::WorkItem:
      return "work_item";
    default:
      return "none";
  }
}

// Task 3.1: name the **indivisible** step when it breaches the active budget or the declared bar. One row
// per step per 5 s; report-only (no allocation, no admission/budget/order change, never the stop path).
TRACK_COLD_MEM void onPersistenceStep(PersistenceStep step, uint32_t stepUs, uint32_t budgetUs,
                                      bool indivisible) {
  const uint8_t index = static_cast<uint8_t>(step);
  if (index >= static_cast<uint8_t>(PersistenceStep::Count)) {
    return;
  }
  if (!persistenceStepShouldEmit(stepUs, budgetUs, millis(), sLastStepRowMs[index])) {
    return;
  }
  char line[192];
  snprintf(line, sizeof(line),
           "#CAP,%lu,DIAG,persist_save,step,%s,us=%lu,budget=%lu,bar=%lu,indivisible=%u",
           static_cast<unsigned long>(micros()), persistenceStepName(step),
           static_cast<unsigned long>(stepUs), static_cast<unsigned long>(budgetUs),
           static_cast<unsigned long>(kPersistenceStepBarUs), indivisible ? 1u : 0u);
  DebugSessionCapture::appendCaptureTextLine(line);
}

// FLASHMEM: periodic idle diagnostic, called from the deferred save scheduler, never from
// the MIDI edge. Moved out of ITCM to keep the S4f.17 Record admission's held-note receipt
// transfer (an out-of-line `unordered_map::swap`) inside the RAM1 floor.
TRACK_COLD_MEM void maybeEmitPeriodic(bool captureOrTransportActive, bool savePending,
                                      bool saveInProgress, bool sdIoActive,
                                      const BacklogSnapshot* backlog) {
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
