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
  /// A save was owed (`currentWorkspaceSave.pending`) but no work item could be admitted to carry
  /// it, so the drain has nothing to step. Terminal and immediate: spinning the full stuck budget
  /// on a state that cannot change only hides the cause. See `admitWork`'s fixed-table exhaustion.
  AdmissionRefused,
  /// The drain can step nothing because `StorageManager::processDeferredSaveState` returns early on a
  /// gate, not because the work itself stalled. These name the gate.
  ///
  /// Without them the blocking CLEAR drain reported the generic `Stuck` while the actual cause was a
  /// gate the caller could have waited out or reported differently: the 2026-09-23 device session
  /// could not tell a transport gate from a slot-load gate from a post-load holdoff from any log, and
  /// so could not choose a fix on evidence.
  BlockedByCapture,
  /// **A3.2:** a restore holds the save gate. The verdict carries the request's own **named wait**, so
  /// a blocking CLEAR reports *why* instead of collapsing every restore block into `BlockedBySlotLoad`.
  BlockedByRestore,
  /// Retained for a slot load that is active but whose request names no wait, so no existing report
  /// regresses to a less specific label.
  BlockedBySlotLoad,
  BlockedByPostLoadHoldoff,
};

/// The restore wait reasons the drain can report, as small integer codes. Deliberately not a second enum
/// type: a duplicate vocabulary cost more ITCM than the problem needs (measured in the first attempt).
namespace SyncDrainWait {
constexpr uint8_t kNone = 0;
constexpr uint8_t kSlotOwned = 1;
constexpr uint8_t kCapture = 2;
constexpr uint8_t kCommitGate = 3;
constexpr uint8_t kJobCapacity = 4;
constexpr uint8_t kRetry = 5;
}  // namespace SyncDrainWait

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
  uint32_t workItemSliceCount = 0;
  uint16_t capturePassCursor = 0;
  uint16_t chunkCursor = 0;
  bool pendingFlush = false;
  /// A3.3: restore motion, so a drain advancing a restore is not misreported as stuck. The counter is
  /// bumped per serviced grain, so it subsumes byte/parse-position terms; the phase term catches a
  /// transition that lands between two increments.
  uint8_t restorePhase = 255;
  uint32_t restoreProgressCounter = 0;
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
  /// A3.3 (D4 rule 4): the restore work the drain may have to advance. The drain's step budget is
  /// derived from queued persistence work, so without these terms a large restore could exhaust
  /// `maxSliceSteps` while making real progress.
  uint32_t restoreSliceSteps = 0;
  uint32_t restoreSdBytes = 0;
  /// True when an owed save is blocked **only** by the restore, which is the C5 shape: the drain then
  /// needs enough budget for the restore rather than for the save.
  bool restoreOnlyBlocker = false;
};

SyncDrainBudget buildSyncDrainBudget(const SyncDrainBudgetInput& input, uint32_t sliceBudgetUs);

/// **A3.2 — the classified drain verdict**, pairing the reason with the name to report. Returning both
/// from one call keeps the drain's report site branch-free: the classification runs once, on the FLASH
/// side, instead of as a nested conditional in the ITCM drain.
struct SyncDrainVerdict {
  SyncDrainFailureReason reason = SyncDrainFailureReason::None;
  /// `transport_active` / a `waiting_*` name / `slot_load_active` / `post_load_holdoff` / `unexplained`.
  const char* gate = "none";
};

/// **A3.2 — the pure drain-verdict classifier.** Names *why* the drain could not advance, from the three
/// gate predicates it already tests plus the restore request's wait code (`SyncDrainWait`). Order matters
/// and matches the drain's own check order: capture first (a recording track defers the load itself),
/// then the slot-load gate, then the post-load holdoff. `Stuck` is reserved for the case where no gate
/// explains it.
SyncDrainVerdict classifySyncDrainVerdict(bool captureRecording, bool slotLoadActive,
                                          bool postLoadHoldoff, uint8_t restoreWait);
/// The `waiting_*` name for a restore wait code, or `nullptr` for `kNone`.
const char* syncDrainRestoreWaitName(uint8_t wait);

bool madeSyncDrainProgress(const SyncDrainProgressSnapshot& before,
                           const SyncDrainProgressSnapshot& after);

}  // namespace PersistenceSyncDrainBudget
