//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#pragma once

#include <cstdint>

#include "Globals.h"
#include "LoopPasses.h"
#include "StorageManagerInternal/PersistenceWorkQueue.h"

enum class SyncDrainFailureReason : uint8_t {
  None = 0,
  Stuck,
  ExceededSliceBudget,
};

/// Upper-bound sync drain budget derived from queued SD work (not a fixed step cap).
struct SyncDrainBudget {
  uint32_t expectedSliceSteps = 0;
  uint32_t maxSliceSteps = 0;
  uint32_t maxStuckIterations = 0;
  uint32_t estimatedSdPayloadBytes = 0;
  uint32_t sliceBudgetUs = 0;
  uint32_t maxWallClockUs = 0;
};

/// Snapshot for progress / stuck detection during blocking saveState drain.
struct SyncDrainProgressSnapshot {
  uint16_t workQueueDepth = 0;
  uint16_t chunkQueueDepth = 0;
  uint16_t writingWorkItems = 0;
  bool workItemActive = false;
  PersistWorkType activeWorkType = PersistWorkType::GlobalMeta;
  uint8_t bundleStage = 0;
  uint8_t loopStage = 0;
  uint16_t capturePassCursor = 0;
  uint16_t chunkCursor = 0;
  bool pendingFlush = false;
};

namespace PersistenceSyncDrainBudget {

/// One deferred-save slice per loop header, capture-pass header, chunk batch, and edit tail.
uint32_t estimateLoopPersistSliceSteps(const LoopPasses& passes);

/// Payload bytes for loop persist (chunk events + small header allowance).
uint32_t estimateLoopPersistSdBytes(const LoopPasses& passes);

/// Runtime bundle rebuild without loop-slot bodies (meta + footer + undo stacks).
uint32_t estimateRuntimeBundleSliceSteps(uint8_t numTracks, uint8_t slotsPerTrack);

uint32_t estimateRuntimeBundleSdBytes(uint8_t numTracks, uint8_t slotsPerTrack);

uint32_t estimateMidPassSliceSteps(uint16_t chunkQueueDepth);

uint32_t estimateMidPassSdBytes(uint16_t chunkQueueDepth);

uint32_t estimateWorkItemSliceSteps(PersistWorkType type, uint8_t numTracks, uint8_t slotsPerTrack);

uint32_t estimateWorkItemSdBytes(PersistWorkType type, const LoopPasses* loopPasses, uint8_t numTracks,
                                 uint8_t slotsPerTrack);

struct SyncDrainBudgetInput {
  uint32_t queuedWorkSliceSteps = 0;
  uint32_t queuedWorkSdBytes = 0;
  uint16_t midPassQueueDepth = 0;
  uint16_t finalizeSliceSteps = 0;
};

SyncDrainBudget buildSyncDrainBudget(const SyncDrainBudgetInput& input, uint32_t sliceBudgetUs);

bool madeSyncDrainProgress(const SyncDrainProgressSnapshot& before,
                           const SyncDrainProgressSnapshot& after);

}  // namespace PersistenceSyncDrainBudget
