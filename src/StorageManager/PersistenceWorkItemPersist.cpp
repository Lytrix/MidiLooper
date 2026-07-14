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
  job.item = PersistWorkItem{};
  job.trackIndex = 0xFF;
  job.slotIndex = 0xFF;
}

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

STORAGE_PERSIST_MEM bool stepScheduledWorkItem(PersistenceWorkItemJob& job) {
  switch (job.item.type) {
    case PersistWorkType::LoopPersist:
      return false;
    case PersistWorkType::LoopUndoHistory:
    case PersistWorkType::GlobalMeta:
    case PersistWorkType::TrackMeta:
    case PersistWorkType::SlotMeta:
    case PersistWorkType::WorkspaceFooter:
    case PersistWorkType::FinalizeWorkspace:
      return completePersistenceWorkItem(job, "sched", true);
  }
  return completePersistenceWorkItem(job, "sched", false);
}

STORAGE_PERSIST_MEM bool beginPersistenceWorkItem(PersistenceWorkItemJob& job) {
  if (!PersistenceWorkQueue::beginWriteQueuedItem(job.item)) {
    return true;
  }
  job.itemActive = true;
  emitPersistenceWorkTelemetry(job.item, "start", "ok");

  if (job.item.type != PersistWorkType::LoopPersist) {
    return true;
  }

  if (!resolveTrackSlotForLoopId(job.item.key.loopId, job.trackIndex, job.slotIndex)) {
    Serial.print("[StorageManager] ERROR: Work item could not resolve loopId ");
    Serial.println(static_cast<unsigned>(job.item.key.loopId));
    return completePersistenceWorkItem(job, "resolve", false);
  }

  if (!ensureWorkItemLoopDirectories()) {
    return completePersistenceWorkItem(job, "prepare", false);
  }

  storageSession.currentWorkspaceSave.workspaceEpoch = currentWorkspaceEpoch;
  resetDeferredLoopWriteState();
  return true;
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

}  // namespace

STORAGE_PERSIST_MEM void resetPersistenceWorkItemJobState() {
  PersistenceWorkItemJob& job = storageSession.persistenceWorkItem;
  if (job.itemActive) {
    PersistenceWorkQueue::requeueWritingItem(job.item);
  }
  job.sdIoActive = false;
  resetActivePersistenceWorkItem(job);
}

STORAGE_PERSIST_MEM bool stepPersistenceWorkItem() {
#if BYPASS_STOP_UNDO_SAVE
  return true;
#else
  if (PersistenceWorkQueue::queueDepth() == 0 && PersistenceWorkQueue::writingWorkItemCount() == 0 &&
      !storageSession.persistenceWorkItem.itemActive) {
    return true;
  }

  PersistenceWorkItemJob& job = storageSession.persistenceWorkItem;
  if (!job.itemActive) {
    if (!beginPersistenceWorkItem(job)) {
      return false;
    }
    if (!job.itemActive) {
      return true;
    }
    if (job.item.type != PersistWorkType::LoopPersist) {
      return stepScheduledWorkItem(job);
    }
  }

  return stepLoopPersistWorkItem(job);
#endif
}

}  // namespace StorageManagerInternal
