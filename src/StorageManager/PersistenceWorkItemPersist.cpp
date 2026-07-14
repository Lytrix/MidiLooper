//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "StorageManagerInternal.h"

#include "CurrentSetStorage.h"
#include "CurrentWorkspaceStorage.h"
#include "Loop.h"
#include "PersistenceLayout.h"
#include "StorageManagerInternal/PersistenceWorkQueue.h"
#include "TrackManager.h"
#include "Utils/DebugSessionCapture.h"
#include "Utils/MemoryMonitor.h"
#include <Arduino.h>
#include <cstdio>

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

STORAGE_PERSIST_MEM void emitPersistenceWorkTelemetry(const PersistWorkItem& item, const char* phase,
                                    const char* outcome) {
#if defined(SESSION_CAPTURE)
  char detail[64];
  char keyLabel[24];
  formatPersistKeyLabel(item.key, keyLabel, sizeof(keyLabel));
  std::snprintf(detail, sizeof(detail), "%s,%s,%s,%s", persistWorkTypeName(item.type), keyLabel,
                phase, outcome);
  SC_PERSIST("work", 0, 0, 0, detail);
#else
  (void)item;
  (void)phase;
  (void)outcome;
#endif
}

STORAGE_PERSIST_MEM void resetActivePersistenceWorkItem(PersistenceWorkItemJob& job) {
  job.itemActive = false;
  job.bundleWriteActive = false;
  job.skipLoopSlotStage = false;
  job.item = PersistWorkItem{};
  job.trackIndex = 0xFF;
  job.slotIndex = 0xFF;
}

STORAGE_PERSIST_MEM bool deferFlushForTransport() {
  if (storageSession.currentWorkspaceSave.urgentRequested) {
    return false;
  }
  if (!storageSession.currentWorkspaceSave.pending) {
    return false;
  }
  const uint32_t nowMs = millis();
  return isTransportActiveForPersistence() &&
         nowMs < storageSession.currentWorkspaceSave.deferDispatchUntilMs;
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

STORAGE_PERSIST_MEM bool isRuntimeBundleWorkType(PersistWorkType type) {
  switch (type) {
    case PersistWorkType::GlobalMeta:
    case PersistWorkType::TrackMeta:
    case PersistWorkType::SlotMeta:
    case PersistWorkType::WorkspaceFooter:
    case PersistWorkType::LoopUndoHistory:
      return true;
    default:
      return false;
  }
}

STORAGE_PERSIST_MEM bool beginPersistenceWorkItem(PersistenceWorkItemJob& job,
                                const LooperState& state) {
  if (!PersistenceWorkQueue::beginWriteQueuedItem(job.item)) {
    return true;
  }
  job.itemActive = true;
  job.stateSnapshot = state;
  emitPersistenceWorkTelemetry(job.item, "start", "ok");

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
    resetDeferredLoopWriteState();
    return true;
  }

  if (isRuntimeBundleWorkType(job.item.type)) {
    if (deferFlushForTransport()) {
      PersistenceWorkQueue::requeueWritingItem(job.item);
      resetActivePersistenceWorkItem(job);
      return true;
    }
    job.bundleWriteActive = true;
    job.skipLoopSlotStage = true;
    if (!beginDeferredRuntimeBundleWrite(state)) {
      return completePersistenceWorkItem(job, "bundle_begin", false);
    }
    return true;
  }

  if (job.item.type == PersistWorkType::FinalizeWorkspace) {
    if (deferFlushForTransport()) {
      PersistenceWorkQueue::requeueWritingItem(job.item);
      resetActivePersistenceWorkItem(job);
      return true;
    }
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
  if (job.trackIndex >= Config::NUM_TRACKS || job.slotIndex >= Config::MAX_LOOPS_PER_TRACK) {
    return completePersistenceWorkItem(job, "resolve", false);
  }

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
    return completePersistenceWorkItem(job, "slice", false);
  }
  if (!loopDone) {
    emitPersistenceWorkTelemetry(job.item, "slice", "ok");
    return true;
  }

  if (!finalizeDeferredLoopSlotTemp(job.trackIndex, job.slotIndex)) {
    return completePersistenceWorkItem(job, "finalize", false);
  }

  clearLoopSlotDirty(job.trackIndex, job.slotIndex);
  return completePersistenceWorkItem(job, "done", true);
}

STORAGE_PERSIST_MEM bool stepRuntimeBundleWorkItem(PersistenceWorkItemJob& job) {
  if (!job.itemActive || !isRuntimeBundleWorkType(job.item.type)) {
    return true;
  }

  bool bundleDone = false;
  if (!stepDeferredRuntimeBundleSlice(bundleDone)) {
    return completePersistenceWorkItem(job, "bundle_slice", false);
  }
  if (!bundleDone) {
    emitPersistenceWorkTelemetry(job.item, "bundle_slice", "ok");
    return true;
  }

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
    SC_PERSIST("result", saveDurationUs, job.flushHeapBefore, job.flushHeapBefore, "failed");
    return completePersistenceWorkItem(job, "finalize", false);
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
  SC_PERSIST("result", saveDurationUs, job.flushHeapBefore, job.flushHeapBefore, "ok");
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

  const uint8_t trackCount = trackManager.getTrackCount();
  for (uint8_t trackIndex = 0; trackIndex < trackCount; ++trackIndex) {
    Track& track = trackManager.getTrack(trackIndex);
    for (uint8_t slotIndex = 0; slotIndex < Config::MAX_LOOPS_PER_TRACK; ++slotIndex) {
      if (track.loopIdForSlot(slotIndex) == loopId) {
        trackIndexOut = trackIndex;
        slotIndexOut = slotIndex;
        return true;
      }
    }
  }

  if (static_cast<uint8_t>(loopId) < Config::MAX_LOOPS_PER_TRACK && trackCount > 0) {
    trackIndexOut = 0;
    slotIndexOut = static_cast<uint8_t>(loopId);
    return true;
  }
  return false;
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

STORAGE_PERSIST_MEM void maybeAdmitFinalizeWorkspaceAfterDrain() {
  if (PersistenceWorkQueue::queueDepth() > 0 || PersistenceWorkQueue::writingWorkItemCount() > 0 ||
      storageSession.persistenceWorkItem.itemActive) {
    return;
  }
  if (!storageSession.currentWorkspaceSave.pending) {
    return;
  }
  PersistenceWorkQueue::admitWork(PersistWorkType::FinalizeWorkspace, persistKeySingleton());
}

STORAGE_PERSIST_MEM void resetPersistenceWorkItemJobState() {
  PersistenceWorkItemJob& job = storageSession.persistenceWorkItem;
  if (job.itemActive) {
    PersistenceWorkQueue::requeueWritingItem(job.item);
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
  maybeAdmitFinalizeWorkspaceAfterDrain();

  if (PersistenceWorkQueue::queueDepth() == 0 && PersistenceWorkQueue::writingWorkItemCount() == 0 &&
      !storageSession.persistenceWorkItem.itemActive) {
    return true;
  }

  PersistenceWorkItemJob& job = storageSession.persistenceWorkItem;
  if (!job.itemActive) {
    if (!beginPersistenceWorkItem(job, state)) {
      return false;
    }
    if (!job.itemActive) {
      return true;
    }
  }

  switch (job.item.type) {
    case PersistWorkType::LoopPersist:
      return stepLoopPersistWorkItem(job);
    case PersistWorkType::GlobalMeta:
    case PersistWorkType::TrackMeta:
    case PersistWorkType::SlotMeta:
    case PersistWorkType::WorkspaceFooter:
    case PersistWorkType::LoopUndoHistory:
      return stepRuntimeBundleWorkItem(job);
    case PersistWorkType::FinalizeWorkspace:
      return stepFinalizeWorkspaceWorkItem(job);
  }
  return completePersistenceWorkItem(job, "sched", false);
#endif
}

}  // namespace StorageManagerInternal
