//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "PersistenceSyncDrainBudget.h"

#include "LoopEventStore.h"
#include "MidiEvent.h"

namespace PersistenceSyncDrainBudget {
namespace {

constexpr uint32_t kLoopHeaderSliceSteps = 1;
constexpr uint32_t kLoopEditTailSliceSteps = 1;
constexpr uint32_t kLoopEmptyPersistSliceSteps = 3;
constexpr uint32_t kFinalizeSliceSteps = 1;
constexpr uint32_t kMidPassSliceStepsPerChunk = 1;
constexpr uint32_t kBundleGlobalMetaSliceSteps = 5;
constexpr uint32_t kBundleFooterSliceSteps = 6;
constexpr uint32_t kBundleUndoSliceStepsPerTrack = 12;
constexpr uint32_t kLoopPersistHeaderBytes = 512;
constexpr uint32_t kRuntimeBundleBaseBytes = 4096;
constexpr uint32_t kHeadroomNumerator = 3;
constexpr uint32_t kHeadroomDenominator = 2;
constexpr uint32_t kMinSlackSliceSteps = 32;
constexpr uint32_t kMinStuckIterations = 64;
constexpr uint32_t kMaxStuckIterations = 256;

uint32_t chunkListEventBytes(const CommittedChunkIdList& committedChunkIds) {
  return static_cast<uint32_t>(LoopEventStore::countEventsInChunkIds(committedChunkIds)) *
         static_cast<uint32_t>(sizeof(MidiEvent));
}

uint32_t passesChunkEventBytes(const LoopPasses& passes) {
  uint32_t bytes = 0;
  if (passes.hasRecordPass()) {
    bytes += chunkListEventBytes(passes.recordPass.committedChunkIds);
  }
  for (const OverdubPass& pass : passes.overdubPasses) {
    bytes += chunkListEventBytes(pass.committedChunkIds);
  }
  return bytes;
}

uint32_t capturePassSliceSteps(const CommittedChunkIdList& committedChunkIds) {
  return 1u + static_cast<uint32_t>(committedChunkIds.size());
}

}  // namespace

uint32_t estimateLoopPersistSliceSteps(const LoopPasses& passes) {
  if (passes.capturePassCount() == 0 && passes.editPasses.empty()) {
    return kLoopEmptyPersistSliceSteps;
  }

  uint32_t steps = kLoopHeaderSliceSteps + kLoopEditTailSliceSteps;
  if (passes.hasRecordPass()) {
    steps += capturePassSliceSteps(passes.recordPass.committedChunkIds);
  }
  for (const OverdubPass& pass : passes.overdubPasses) {
    steps += capturePassSliceSteps(pass.committedChunkIds);
  }
  return steps;
}

uint32_t estimateLoopPersistSdBytes(const LoopPasses& passes) {
  return kLoopPersistHeaderBytes + passesChunkEventBytes(passes);
}

uint32_t estimateRuntimeBundleSliceSteps(uint8_t numTracks, uint8_t slotsPerTrack) {
  const uint32_t trackHeaderSliceSteps = 2u;
  const uint32_t slotMetaSliceSteps = 3u;
  const uint32_t trackSliceSteps =
      trackHeaderSliceSteps + static_cast<uint32_t>(slotsPerTrack) * slotMetaSliceSteps;
  const uint32_t footerSliceSteps = kBundleFooterSliceSteps + static_cast<uint32_t>(numTracks) * 2u;
  const uint32_t undoSliceSteps = static_cast<uint32_t>(numTracks) * kBundleUndoSliceStepsPerTrack;
  return kBundleGlobalMetaSliceSteps + static_cast<uint32_t>(numTracks) * trackSliceSteps +
         footerSliceSteps + undoSliceSteps;
}

uint32_t estimateRuntimeBundleSdBytes(uint8_t numTracks, uint8_t slotsPerTrack) {
  const uint32_t trackMetaBytes = 16u;
  const uint32_t slotMetaBytes = 12u;
  return kRuntimeBundleBaseBytes +
         static_cast<uint32_t>(numTracks) * trackMetaBytes +
         static_cast<uint32_t>(numTracks) * static_cast<uint32_t>(slotsPerTrack) * slotMetaBytes;
}

uint32_t estimateMidPassSliceSteps(uint16_t chunkQueueDepth) {
  return static_cast<uint32_t>(chunkQueueDepth) * kMidPassSliceStepsPerChunk;
}

uint32_t estimateMidPassSdBytes(uint16_t chunkQueueDepth) {
  return static_cast<uint32_t>(chunkQueueDepth) * LoopEventStoreConfig::CHUNK_CAPACITY *
         static_cast<uint32_t>(sizeof(MidiEvent));
}

uint32_t estimateWorkItemSliceSteps(PersistWorkType type, uint8_t numTracks, uint8_t slotsPerTrack) {
  switch (type) {
    case PersistWorkType::LoopPersist:
      return kLoopEmptyPersistSliceSteps;
    case PersistWorkType::GlobalMeta:
    case PersistWorkType::TrackMeta:
    case PersistWorkType::SlotMeta:
    case PersistWorkType::WorkspaceFooter:
    case PersistWorkType::LoopUndoHistory:
      return estimateRuntimeBundleSliceSteps(numTracks, slotsPerTrack);
    case PersistWorkType::FinalizeWorkspace:
      return kFinalizeSliceSteps;
  }
  return 1;
}

uint32_t estimateWorkItemSdBytes(PersistWorkType type, const LoopPasses* loopPasses, uint8_t numTracks,
                                 uint8_t slotsPerTrack) {
  switch (type) {
    case PersistWorkType::LoopPersist:
      return loopPasses != nullptr ? estimateLoopPersistSdBytes(*loopPasses)
                                   : kLoopPersistHeaderBytes;
    case PersistWorkType::GlobalMeta:
    case PersistWorkType::TrackMeta:
    case PersistWorkType::SlotMeta:
    case PersistWorkType::WorkspaceFooter:
    case PersistWorkType::LoopUndoHistory:
      return estimateRuntimeBundleSdBytes(numTracks, slotsPerTrack);
    case PersistWorkType::FinalizeWorkspace:
      return 512;
  }
  return 0;
}

SyncDrainBudget buildSyncDrainBudget(const SyncDrainBudgetInput& input, uint32_t sliceBudgetUs) {
  SyncDrainBudget budget{};
  budget.expectedSliceSteps = input.queuedWorkSliceSteps + estimateMidPassSliceSteps(input.midPassQueueDepth) +
                              input.finalizeSliceSteps;
  budget.estimatedSdPayloadBytes =
      input.queuedWorkSdBytes + estimateMidPassSdBytes(input.midPassQueueDepth);
  budget.sliceBudgetUs = sliceBudgetUs;

  budget.maxSliceSteps =
      budget.expectedSliceSteps * kHeadroomNumerator / kHeadroomDenominator + kMinSlackSliceSteps;
  budget.maxStuckIterations = budget.maxSliceSteps / 4u;
  if (budget.maxStuckIterations < kMinStuckIterations) {
    budget.maxStuckIterations = kMinStuckIterations;
  }
  if (budget.maxStuckIterations > kMaxStuckIterations) {
    budget.maxStuckIterations = kMaxStuckIterations;
  }
  if (budget.maxStuckIterations > budget.maxSliceSteps) {
    budget.maxStuckIterations = budget.maxSliceSteps;
  }
  budget.maxWallClockUs = budget.maxSliceSteps * sliceBudgetUs;
  return budget;
}

bool madeSyncDrainProgress(const SyncDrainProgressSnapshot& before,
                           const SyncDrainProgressSnapshot& after) {
  if (after.workQueueDepth < before.workQueueDepth) {
    return true;
  }
  if (after.chunkQueueDepth < before.chunkQueueDepth) {
    return true;
  }
  if (after.writingWorkItems < before.writingWorkItems) {
    return true;
  }
  if (before.workItemActive && !after.workItemActive) {
    return true;
  }
  if (before.pendingFlush && !after.pendingFlush) {
    return true;
  }
  if (!after.workItemActive) {
    return false;
  }
  if (after.bundleStage != before.bundleStage) {
    return true;
  }
  if (after.loopStage != before.loopStage) {
    return true;
  }
  if (after.capturePassCursor != before.capturePassCursor) {
    return true;
  }
  if (after.chunkCursor != before.chunkCursor) {
    return true;
  }
  if (after.activeWorkType != before.activeWorkType) {
    return true;
  }
  return false;
}

}  // namespace PersistenceSyncDrainBudget
