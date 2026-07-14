//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "StorageManagerInternal.h"

#include "Loop.h"
#include "PersistenceQueue.h"
#include "PersistenceSyncDrainBudget.h"
#include "StorageManagerInternal/PersistenceWorkQueue.h"
#include "TrackManager.h"

namespace StorageManagerInternal {
namespace {

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

struct SyncDrainBudgetAccum {
  uint32_t sliceSteps = 0;
  uint32_t sdBytes = 0;
};

STORAGE_PERSIST_MEM void accumulateWorkItemBudget(const PersistWorkItem& item, void* context) {
  auto* accum = static_cast<SyncDrainBudgetAccum*>(context);
  const LoopPasses* loopPasses = nullptr;
  if (item.type == PersistWorkType::LoopPersist) {
    uint8_t trackIndex = 0;
    uint8_t slotIndex = 0;
    if (resolveTrackSlotForLoopId(item.key.loopId, trackIndex, slotIndex)) {
      loopPasses = &trackManager.getTrack(trackIndex).getLoop(slotIndex).passes;
      accum->sliceSteps += PersistenceSyncDrainBudget::estimateLoopPersistSliceSteps(*loopPasses);
      accum->sdBytes += PersistenceSyncDrainBudget::estimateLoopPersistSdBytes(*loopPasses);
      return;
    }
  }

  accum->sliceSteps += PersistenceSyncDrainBudget::estimateWorkItemSliceSteps(
      item.type, Config::NUM_TRACKS, Config::MAX_LOOPS_PER_TRACK);
  accum->sdBytes += PersistenceSyncDrainBudget::estimateWorkItemSdBytes(
      item.type, loopPasses, Config::NUM_TRACKS, Config::MAX_LOOPS_PER_TRACK);
}

}  // namespace

STORAGE_PERSIST_MEM SyncDrainProgressSnapshot captureSyncDrainProgressSnapshot() {
  SyncDrainProgressSnapshot snapshot{};
  snapshot.workQueueDepth = PersistenceWorkQueue::queueDepth();
  snapshot.chunkQueueDepth = PersistenceQueue::queueDepth();
  snapshot.writingWorkItems = PersistenceWorkQueue::writingWorkItemCount();
  snapshot.workItemActive = storageSession.persistenceWorkItem.itemActive;
  snapshot.activeWorkType = storageSession.persistenceWorkItem.item.type;
  snapshot.bundleStage = static_cast<uint8_t>(storageSession.currentWorkspaceSave.stage);
  snapshot.loopStage = static_cast<uint8_t>(storageSession.currentWorkspaceSave.loopWriteStage);
  snapshot.capturePassCursor = storageSession.currentWorkspaceSave.capturePassCursor;
  snapshot.chunkCursor = storageSession.currentWorkspaceSave.chunkCursor;
  snapshot.pendingFlush = storageSession.currentWorkspaceSave.pending;
  return snapshot;
}

STORAGE_PERSIST_MEM SyncDrainBudget buildSyncDrainBudgetForSession() {
  SyncDrainBudgetAccum accum{};
  PersistenceWorkQueue::visitScheduledWorkItems(accumulateWorkItemBudget, &accum);

  PersistenceSyncDrainBudget::SyncDrainBudgetInput input{};
  input.queuedWorkSliceSteps = accum.sliceSteps;
  input.queuedWorkSdBytes = accum.sdBytes;
  input.midPassQueueDepth = PersistenceQueue::queueDepth();
  if (storageSession.currentWorkspaceSave.pending && PersistenceWorkQueue::queueDepth() == 0 &&
      PersistenceWorkQueue::writingWorkItemCount() == 0 &&
      !storageSession.persistenceWorkItem.itemActive) {
    input.finalizeSliceSteps = 1;
  }

  const uint32_t sliceBudgetUs = Config::maxPersistenceMicrosPerLoop;
  return PersistenceSyncDrainBudget::buildSyncDrainBudget(input, sliceBudgetUs);
}

}  // namespace StorageManagerInternal
