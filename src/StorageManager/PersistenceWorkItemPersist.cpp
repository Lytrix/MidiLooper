//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "StorageManagerInternal.h"

#include "LoadLoopBudget.h"
#include "StorageManager.h"
#include "CurrentSetStorage.h"
#include "CurrentWorkspaceStorage.h"
#include "Loop.h"
#include "PersistenceFailurePolicy.h"
#include "PersistenceLayout.h"
#include "StorageManagerInternal/PersistenceWorkQueue.h"
#include "TrackManager.h"
#include "Utils/DebugSessionCapture.h"
#include "Utils/TelemetryCadence.h"
#include "Utils/MemoryMonitor.h"
#include <Arduino.h>
#include <cstdio>
#include <cstring>

namespace StorageManagerInternal {
namespace {

STORAGE_PERSIST_MEM const char* persistWorkTypeName(PersistWorkType type) {
  switch (type) {
    case PersistWorkType::GlobalMeta:
      return "GlobalMeta";
    case PersistWorkType::TrackMeta:
      return "TrackMeta";
    case PersistWorkType::SlotMeta:
      return "SlotMeta";
    case PersistWorkType::LoopPersist:
      return "LoopPersist";
    case PersistWorkType::LoopUndoHistory:
      return "LoopUndoHistory";
    case PersistWorkType::WorkspaceFooter:
      return "WorkspaceFooter";
    case PersistWorkType::FinalizeWorkspace:
      return "FinalizeWorkspace";
  }
  return "Unknown";
}

STORAGE_PERSIST_MEM void formatPersistKeyLabel(const PersistKey& key, char* out, size_t outSize) {
  if (out == nullptr || outSize == 0) {
    return;
  }
  switch (key.kind) {
    case PersistKeyKind::Singleton:
      std::snprintf(out, outSize, "singleton");
      break;
    case PersistKeyKind::LoopId:
      std::snprintf(out, outSize, "loop:%u", static_cast<unsigned>(key.loopId));
      break;
    case PersistKeyKind::Track:
      std::snprintf(out, outSize, "track:%u", static_cast<unsigned>(key.trackIndex));
      break;
    case PersistKeyKind::Slot:
      std::snprintf(out, outSize, "slot:%u:%u", static_cast<unsigned>(key.trackIndex),
                    static_cast<unsigned>(key.slotIndex));
      break;
  }
}

#if defined(SESSION_CAPTURE)
STORAGE_PERSIST_MEM const char* deferredLoopWriteStageName(DeferredLoopWriteStage stage) {
  switch (stage) {
    case DeferredLoopWriteStage::Header:
      return "Header";
    case DeferredLoopWriteStage::CapturePassHeader:
      return "CapturePassHeader";
    case DeferredLoopWriteStage::CapturePassChunk:
      return "CapturePassChunk";
    case DeferredLoopWriteStage::EditTail:
      return "EditTail";
    case DeferredLoopWriteStage::LoopSnapshotTail:
      return "LoopSnapshotTail";
  }
  return "Unknown";
}

STORAGE_PERSIST_MEM const char* deferredLoopFinalizeStageName(DeferredLoopFinalizeStage stage) {
  switch (stage) {
    case DeferredLoopFinalizeStage::Idle:
      return "Idle";
    case DeferredLoopFinalizeStage::WriteToken:
      return "WriteToken";
    case DeferredLoopFinalizeStage::EpochCrcBody:
      return "EpochCrcBody";
    case DeferredLoopFinalizeStage::EpochCrcHeader:
      return "EpochCrcHeader";
    case DeferredLoopFinalizeStage::VerifyAndRename:
      return "VerifyAndRename";
  }
  return "Unknown";
}

// Wall-clock start of the current processDeferredSaveState step, so every PERS,work line carries
// the elapsed microseconds of the slice that produced it. Without this, a 60 ms persist_save
// outlier could not be attributed to one of thousands of identically labelled slices.
uint32_t persistenceSliceStartUs = 0;
#endif

/// Per-phase slice-log cadence for the item currently being written. **OCRAM, not DTCM:** these are mutable
/// telemetry counters, and the RAM1 budget's own note records the rule that such state must not grow DTCM (it would
/// come out of the local allowance). Placing them in `DMAMEM` keeps RAM1 variables unchanged at 96,192 B.
/// The states are reset whenever the item's key
/// changes, so a new item always emits the first slice of each phase (the acceptance surface asserts phase
/// presence, and `deferred_persist_large_loop_bounds` requires `payload_slice` and `finalize_slice`).
DMAMEM TelemetryCadence::SliceLogState loopPersistPayloadSliceLog;
DMAMEM TelemetryCadence::SliceLogState loopPersistFinalizeSliceLog;
DMAMEM PersistKey loopPersistSliceLogKey{};
DMAMEM bool loopPersistSliceLogKeyValid = false;

STORAGE_PERSIST_MEM void noteLoopPersistItemForKey(const PersistKey& key) {
  if (loopPersistSliceLogKeyValid && persistKeysEqual(loopPersistSliceLogKey, key)) {
    return;
  }
  loopPersistSliceLogKey = key;
  loopPersistSliceLogKeyValid = true;
  TelemetryCadence::reset(loopPersistPayloadSliceLog);
  TelemetryCadence::reset(loopPersistFinalizeSliceLog);
}

#if defined(SESSION_CAPTURE)
STORAGE_PERSIST_MEM void emitPersistenceWorkTelemetry(const PersistWorkItem& item, const char* phase,
                                    const char* outcome, uint32_t durationUs) {
  if (strcmp(phase, "bundle_slice") == 0) {
    return;
  }
  char detail[96];
  char keyLabel[24];
  formatPersistKeyLabel(item.key, keyLabel, sizeof(keyLabel));
  if (item.type == PersistWorkType::LoopPersist) {
    std::snprintf(detail, sizeof(detail), "%s,%s,%s,%s,write=%s,finalize=%s",
                  persistWorkTypeName(item.type), keyLabel, phase, outcome,
                  deferredLoopWriteStageName(storageSession.currentWorkspaceSave.loopWriteStage),
                  deferredLoopFinalizeStageName(
                      storageSession.currentWorkspaceSave.loopFinalizeStage));
  } else {
    std::snprintf(detail, sizeof(detail), "%s,%s,%s,%s", persistWorkTypeName(item.type), keyLabel,
                  phase, outcome);
  }
  SC_PERSIST("work", durationUs, 0, 0, detail);
}

STORAGE_PERSIST_MEM void emitPersistenceWorkTelemetry(const PersistWorkItem& item, const char* phase,
                                    const char* outcome) {
  emitPersistenceWorkTelemetry(item, phase, outcome, micros() - persistenceSliceStartUs);
}

/// A LoopPersist slice row: one row per `kSliceLogInterval` slices, carrying the **running maximum** slice
/// duration so a bound check over these rows stays a faithful upper bound (see `Utils/TelemetryCadence.h`).
STORAGE_PERSIST_MEM void emitLoopPersistSliceTelemetry(const PersistWorkItem& item, const char* phase,
                                    TelemetryCadence::SliceLogState& state) {
  uint32_t carriedUs = 0;
  const uint32_t sliceUs = micros() - persistenceSliceStartUs;
  if (!TelemetryCadence::shouldLogSlice(state, sliceUs, TelemetryCadence::kSliceLogInterval,
                                        carriedUs)) {
    return;
  }
  emitPersistenceWorkTelemetry(item, phase, "ok", carriedUs);
}
#else
STORAGE_PERSIST_MEM void emitPersistenceWorkTelemetry(const PersistWorkItem& item, const char* phase,
                                    const char* outcome) {
  (void)item;
  (void)phase;
  (void)outcome;
}

STORAGE_PERSIST_MEM void emitLoopPersistSliceTelemetry(const PersistWorkItem& item, const char* phase,
                                    TelemetryCadence::SliceLogState& state) {
  (void)item;
  (void)phase;
  (void)state;
}
#endif

STORAGE_PERSIST_MEM bool recoverPersistenceWorkQueueStall(PersistenceWorkItemJob& job) {
  if (job.itemActive || PersistenceWorkQueue::writingWorkItemCount() == 0) {
    return false;
  }
  PersistWorkItem stalled{};
  if (!PersistenceWorkQueue::tryGetWritingItem(stalled)) {
    return false;
  }
  if (!PersistenceWorkQueue::recoverOrphanedWritingItem()) {
    return false;
  }
  emitPersistenceWorkTelemetry(stalled, "work_queue_stall", "recover");
  return true;
}

STORAGE_PERSIST_MEM void resetActivePersistenceWorkItem(PersistenceWorkItemJob& job) {
  job.itemActive = false;
  job.bundleWriteActive = false;
  job.skipLoopSlotStage = false;
  job.bundleStartedAtUs = 0;
  job.workItemSliceCount = 0;
  job.bundleSliceCount = 0;
  job.bundleMaxSliceUs = 0;
  job.bundleUndoEntryCount = 0;
  job.bundleSnapshotCount = 0;
  job.item = PersistWorkItem{};
  job.trackIndex = 0xFF;
  job.slotIndex = 0xFF;
}

STORAGE_PERSIST_MEM bool deferFlushForTransport() {
  return PersistenceFailurePolicy::shouldDeferTransportFlush(
      storageSession.currentWorkspaceSave.pending, isCaptureRecordingForPersistence(), millis(),
      storageSession.currentWorkspaceSave.deferDispatchUntilMs);
}

STORAGE_PERSIST_MEM bool ensureWorkItemLoopDirectories() {
  return CurrentSetStorage::ensureDirectory(PersistenceLayout::kRoot) &&
         CurrentSetStorage::ensureDirectory(CurrentSetStorage::kCurrentSetDir) &&
         CurrentSetStorage::ensureDirectory(CurrentWorkspaceStorage::kCurrentTempDir) &&
         CurrentSetStorage::ensureDirectory(CurrentSetStorage::kCurrentSlotsDir);
}

STORAGE_PERSIST_MEM void clearLoopSlotDirty(uint8_t trackIndex, uint8_t slotIndex) {
  if (trackIndex >= Config::NUM_TRACKS || slotIndex >= Config::MAX_LOOPS_PER_TRACK) {
    return;
  }
  currentSetLoopSlotDirty[trackIndex][slotIndex] = false;
}

STORAGE_PERSIST_MEM bool completePersistenceWorkItem(PersistenceWorkItemJob& job, const char* phase,
                                   bool ok) {
  emitPersistenceWorkTelemetry(job.item, phase, ok ? "ok" : "failed");
  if (ok) {
    PersistenceWorkQueue::markItemPersisted(job.item);
  } else {
    PersistenceWorkQueue::requeueWritingItem(job.item);
  }
  resetActivePersistenceWorkItem(job);
  return ok;
}

/// DEC-091: abandon a work item whose step **cannot** succeed on a retry in the same turn sequence, and
/// report that at the call site. `completePersistenceWorkItem(job, phase, false)` requeues to the queue
/// **head**, which is right for a transient SD write that the next slice may complete, and wrong for the
/// workspace completion: its sub-steps (`patchLastActiveUnix`, the bundle CRC refresh, `workspace.bin`) are
/// deterministic, so the retry re-enters the same step forever and every other item waits behind it while
/// `hasDeferredSaveWork()` stays true -- which refuses the save pre-flight and the read-only slot-payload
/// observation for the rest of the boot. The owed work is not lost: the per-slot dirty set is left exactly
/// as the failure found it, which is the retry the persistence contract names.
STORAGE_PERSIST_MEM bool abandonPersistenceWorkItem(PersistenceWorkItemJob& job, const char* phase) {
  emitPersistenceWorkTelemetry(job.item, phase, "failed");
  // The queue's terminal state is the tombstone `markItemPersisted` writes: not Queued, not Writing, and
  // reclaimable by a later admission of the same key. It is reached here for an **abandoned** item rather
  // than a persisted one, and no production code reads `workState()`, so the name is the only mismatch --
  // which is why this decision is recorded (DEC-091) instead of resting on the call site.
  PersistenceWorkQueue::markItemPersisted(job.item);
  resetActivePersistenceWorkItem(job);
  // True: the turn did what it owed (record the failure, release the queue). The failure itself is
  // reported through `PERS,result,...,failed` and the job's `failedAtMs`, not as a slice error.
  return true;
}

STORAGE_PERSIST_MEM bool isRuntimeBundleWorkType(PersistWorkType type) {
  switch (type) {
    case PersistWorkType::GlobalMeta:
    case PersistWorkType::TrackMeta:
    case PersistWorkType::SlotMeta:
    case PersistWorkType::WorkspaceFooter:
      return true;
    case PersistWorkType::LoopUndoHistory:
      return false;
    default:
      return false;
  }
}

STORAGE_PERSIST_MEM bool beginPersistenceWorkItem(PersistenceWorkItemJob& job,
                                const LooperState& state) {
  if (!PersistenceWorkQueue::beginWriteQueuedItem(job.item)) {
    return true;
  }

  const bool deferBeginForTransport =
      deferFlushForTransport() &&
      (isRuntimeBundleWorkType(job.item.type) ||
       job.item.type == PersistWorkType::FinalizeWorkspace);
  if (deferBeginForTransport) {
    PersistenceWorkQueue::requeueWritingItem(job.item);
    resetActivePersistenceWorkItem(job);
    return true;
  }

  job.itemActive = true;
  job.workItemSliceCount = 0;
  job.stateSnapshot = state;
  emitPersistenceWorkTelemetry(job.item, "start", "ok");

  if (job.item.type == PersistWorkType::LoopUndoHistory) {
    return completePersistenceWorkItem(job, "skip_stage3", true);
  }

  if (job.item.type == PersistWorkType::LoopPersist) {
    if (!resolvePersistKeyToTrackSlot(job.item.key, job.trackIndex, job.slotIndex)) {
      Serial.print("[StorageManager] ERROR: Work item could not resolve loop persist key kind ");
      Serial.println(static_cast<unsigned>(job.item.key.kind));
      return completePersistenceWorkItem(job, "resolve", false);
    }
    if (!ensureWorkItemLoopDirectories()) {
      return completePersistenceWorkItem(job, "prepare", false);
    }
    storageSession.currentWorkspaceSave.workspaceEpoch = currentWorkspaceEpoch;
    if (!loopPersistTailWriteResumeActive(job.item.key)) {
      resetDeferredLoopWriteState();
    } else {
      resetDeferredLoopFinalizeState();
    }
    return true;
  }

  if (isRuntimeBundleWorkType(job.item.type)) {
    job.bundleWriteActive = true;
    job.skipLoopSlotStage = true;
    if (!beginDeferredRuntimeBundleWrite(state)) {
      return completePersistenceWorkItem(job, "bundle_begin", false);
    }
    job.bundleStartedAtUs = micros();
    for (uint8_t trackIndex = 0; trackIndex < Config::NUM_TRACKS; ++trackIndex) {
      const GlobalUndoStack& stack = trackManager.getTrack(trackIndex).getGlobalUndoStack();
      job.bundleUndoEntryCount += static_cast<uint32_t>(stack.entries.size());
      for (const UndoEntry& entry : stack.entries) {
        job.bundleSnapshotCount += entry.beforeSnapshot != nullptr ? 1u : 0u;
        job.bundleSnapshotCount += entry.afterSnapshot != nullptr ? 1u : 0u;
      }
    }
    return true;
  }

  if (job.item.type == PersistWorkType::FinalizeWorkspace) {
    job.flushStartedAtUs = micros();
    job.flushHeapBefore = MemoryMonitor::getInternalHeapFreeBytes();
    storageSession.currentWorkspaceSave.startedAtUs = job.flushStartedAtUs;
    storageSession.currentWorkspaceSave.heapBefore = job.flushHeapBefore;
    ++currentWorkspaceEpoch;
    storageSession.currentWorkspaceSave.workspaceEpoch = currentWorkspaceEpoch;
    return true;
  }

  return completePersistenceWorkItem(job, "sched", false);
}

STORAGE_PERSIST_MEM bool stepLoopPersistWorkItem(PersistenceWorkItemJob& job) {
  if (!job.itemActive || job.item.type != PersistWorkType::LoopPersist) {
    return true;
  }
  noteLoopPersistItemForKey(job.item.key);
  if (job.trackIndex >= Config::NUM_TRACKS || job.slotIndex >= Config::MAX_LOOPS_PER_TRACK) {
    return completePersistenceWorkItem(job, "resolve", false);
  }

  if (!deferredLoopSlotFinalizeInProgress()) {
    if (!storageSession.currentWorkspaceSave.loopFileOpen &&
        !openDeferredLoopSlotTemp(job.trackIndex, job.slotIndex)) {
      return completePersistenceWorkItem(job, "open", false);
    }

    Track& track = trackManager.getTrack(job.trackIndex);
    bool loopDone = false;
    const bool loopWriteOk = track.loopsAllocated()
                                 ? stepDeferredLoopPersist(storageSession.currentWorkspaceSave.loopFile,
                                                           track.getLoop(job.slotIndex), loopDone)
                                 : stepDeferredEmptyLoopPersist(storageSession.currentWorkspaceSave.loopFile,
                                                                static_cast<LoopId>(job.slotIndex),
                                                                loopDone);
    if (!loopWriteOk) {
      return completePersistenceWorkItem(job, "payload_slice", false);
    }
    if (!loopDone) {
      emitLoopPersistSliceTelemetry(job.item, "payload_slice", loopPersistPayloadSliceLog);
      return true;
    }
    beginDeferredLoopSlotFinalize();
    emitPersistenceWorkTelemetry(job.item, "payload_done", "ok");
    return true;
  }

  bool finalizeDone = false;
  if (!stepFinalizeDeferredLoopSlotTemp(job.trackIndex, job.slotIndex, finalizeDone)) {
    return completePersistenceWorkItem(job, "finalize", false);
  }
  if (!finalizeDone) {
    emitLoopPersistSliceTelemetry(job.item, "finalize_slice", loopPersistFinalizeSliceLog);
    return true;
  }

  clearLoopSlotDirty(job.trackIndex, job.slotIndex);
  return completePersistenceWorkItem(job, "done", true);
}

STORAGE_PERSIST_MEM bool stepRuntimeBundleWorkItem(PersistenceWorkItemJob& job) {
  if (!job.itemActive || !isRuntimeBundleWorkType(job.item.type)) {
    return true;
  }

  const uint32_t sliceStartUs = micros();
  bool bundleDone = false;
  if (!stepDeferredRuntimeBundleSlice(bundleDone)) {
    return completePersistenceWorkItem(job, "bundle_slice", false);
  }
  const uint32_t sliceDurationUs = micros() - sliceStartUs;
  ++job.bundleSliceCount;
  if (sliceDurationUs > job.bundleMaxSliceUs) {
    job.bundleMaxSliceUs = sliceDurationUs;
  }
  if (!bundleDone) {
    emitPersistenceWorkTelemetry(job.item, "bundle_slice", "ok");
    return true;
  }

  SC_PERSIST_BUNDLE(persistWorkTypeName(job.item.type),
                    micros() - job.bundleStartedAtUs, job.bundleMaxSliceUs,
                    job.bundleSliceCount, job.bundleUndoEntryCount,
                    job.bundleSnapshotCount);
  job.bundleWriteActive = false;
  job.skipLoopSlotStage = false;
  return completePersistenceWorkItem(job, "bundle_done", true);
}

STORAGE_PERSIST_MEM bool stepFinalizeWorkspaceWorkItem(PersistenceWorkItemJob& job) {
  if (!job.itemActive || job.item.type != PersistWorkType::FinalizeWorkspace) {
    return true;
  }

  bool finalizeDone = false;
  if (!stepDeferredWorkspaceFinalizeSlice(finalizeDone)) {
    storageSession.currentWorkspaceSave.pending = false;
    storageSession.currentWorkspaceSave.lastCompletedOk = false;
    storageSession.currentWorkspaceSave.failedAtMs = millis();
    storageSession.currentWorkspaceSave.completedAtMs = 0;
    const uint32_t saveDurationUs = micros() - job.flushStartedAtUs;
    SC_PERSIST_RESULT(saveDurationUs, job.flushHeapBefore, job.flushHeapBefore, "failed");
    // DEC-091: discharge, do not requeue. The dirty set is untouched, so the next save trigger re-writes
    // everything owed.
    return abandonPersistenceWorkItem(job, "finalize");
  }
  if (!finalizeDone) {
    emitPersistenceWorkTelemetry(job.item, "finalize_slice", "ok");
    return true;
  }

  storageSession.currentWorkspaceSave.pending = false;
  storageSession.currentWorkspaceSave.urgentRequested = false;
  storageSession.currentWorkspaceSave.lastCompletedOk = true;
  storageSession.currentWorkspaceSave.completedAtMs = millis();
  storageSession.currentWorkspaceSave.failedAtMs = 0;
  const uint32_t saveDurationUs = micros() - job.flushStartedAtUs;
  SC_PERSIST_RESULT(saveDurationUs, job.flushHeapBefore, job.flushHeapBefore, "ok");
  if (clearEditDirtyAfterDeferredSave) {
    for (uint8_t t = 0; t < trackManager.getTrackCount(); ++t) {
      Track& track = trackManager.getTrack(t);
      if (!track.loopsAllocated()) {
        continue;
      }
      for (uint8_t s = 0; s < Config::MAX_LOOPS_PER_TRACK; ++s) {
        track.getLoop(s).clearEditStateDirty();
      }
    }
    clearEditDirtyAfterDeferredSave = false;
  }
  return completePersistenceWorkItem(job, "done", true);
}

}  // namespace

STORAGE_PERSIST_MEM bool resolveTrackSlotForLoopId(LoopId loopId, uint8_t& trackIndexOut,
                                                   uint8_t& slotIndexOut) {
  if (loopId == kInvalidLoopId) {
    return false;
  }

  bool found = false;
  uint8_t resolvedTrack = 0;
  uint8_t resolvedSlot = 0;
  const uint8_t trackCount = trackManager.getTrackCount();
  for (uint8_t trackIndex = 0; trackIndex < trackCount; ++trackIndex) {
    Track& track = trackManager.getTrack(trackIndex);
    for (uint8_t slotIndex = 0; slotIndex < Config::MAX_LOOPS_PER_TRACK; ++slotIndex) {
      if (track.loopIdForSlot(slotIndex) != loopId) {
        continue;
      }
      if (found) {
        return false;
      }
      found = true;
      resolvedTrack = trackIndex;
      resolvedSlot = slotIndex;
    }
  }

  if (!found) {
    return false;
  }
  trackIndexOut = resolvedTrack;
  slotIndexOut = resolvedSlot;
  return true;
}

STORAGE_PERSIST_MEM bool resolvePersistKeyToTrackSlot(const PersistKey& key, uint8_t& trackIndexOut,
                                                      uint8_t& slotIndexOut) {
  if (key.kind == PersistKeyKind::Slot) {
    if (key.trackIndex >= Config::NUM_TRACKS || key.slotIndex >= Config::MAX_LOOPS_PER_TRACK) {
      return false;
    }
    trackIndexOut = key.trackIndex;
    slotIndexOut = key.slotIndex;
    return true;
  }
  if (key.kind == PersistKeyKind::LoopId) {
    return resolveTrackSlotForLoopId(key.loopId, trackIndexOut, slotIndexOut);
  }
  return false;
}

STORAGE_PERSIST_MEM bool maybeAdmitFinalizeWorkspaceAfterDrain() {
  if (PersistenceWorkQueue::queueDepth() > 0 || PersistenceWorkQueue::writingWorkItemCount() > 0 ||
      storageSession.persistenceWorkItem.itemActive) {
    return true;
  }
  if (!storageSession.currentWorkspaceSave.pending) {
    return false;
  }
  // Checked, not discarded: a refused admission here is the one state in which an owed save has
  // nothing to step, and the blocking drain must fail fast on it rather than spin. See
  // `reclaimTerminalEntry` for the fixed-table exhaustion that used to make this permanent.
  return PersistenceWorkQueue::admitWork(PersistWorkType::FinalizeWorkspace, persistKeySingleton());
}

STORAGE_PERSIST_MEM void resetPersistenceWorkItemJobState() {
  PersistenceWorkItemJob& job = storageSession.persistenceWorkItem;
  if (job.itemActive) {
    PersistenceWorkQueue::requeueWritingItem(job.item);
  } else {
    (void)recoverPersistenceWorkQueueStall(job);
  }
  job.sdIoActive = false;
  job.bundleWriteActive = false;
  job.skipLoopSlotStage = false;
  resetActivePersistenceWorkItem(job);
}

STORAGE_PERSIST_MEM bool stepPersistenceWorkItem(const LooperState& state) {
#if BYPASS_STOP_UNDO_SAVE
  (void)state;
  return true;
#else
#if defined(SESSION_CAPTURE)
  persistenceSliceStartUs = micros();
#endif
  maybeAdmitFinalizeWorkspaceAfterDrain();

  PersistenceWorkItemJob& job = storageSession.persistenceWorkItem;
  (void)recoverPersistenceWorkQueueStall(job);

  if (PersistenceWorkQueue::queueDepth() == 0 && PersistenceWorkQueue::writingWorkItemCount() == 0 &&
      !job.itemActive) {
    return true;
  }
  if (deferFlushForTransport() && !job.itemActive &&
      !StorageManager::shouldPrioritizeWorkspaceSaveDrain()) {
    return true;
  }
  if (!job.itemActive) {
    if (!beginPersistenceWorkItem(job, state)) {
      return false;
    }
    if (!job.itemActive) {
      return true;
    }
  }

  bool sliceOk = false;
  switch (job.item.type) {
    case PersistWorkType::LoopPersist:
      sliceOk = stepLoopPersistWorkItem(job);
      break;
    case PersistWorkType::GlobalMeta:
    case PersistWorkType::TrackMeta:
    case PersistWorkType::SlotMeta:
    case PersistWorkType::WorkspaceFooter:
      sliceOk = stepRuntimeBundleWorkItem(job);
      break;
    case PersistWorkType::LoopUndoHistory:
      sliceOk = completePersistenceWorkItem(job, "skip_stage3", true);
      break;
    case PersistWorkType::FinalizeWorkspace:
      sliceOk = stepFinalizeWorkspaceWorkItem(job);
      break;
  }
  // A loop payload can require hundreds of bounded slices while retaining the
  // same public stage/cursor. Record each successful active slice so the
  // synchronous drain can distinguish that work from a writer stall.
  if (sliceOk && job.itemActive) {
    ++job.workItemSliceCount;
  }
  return sliceOk;
#endif
}

}  // namespace StorageManagerInternal
