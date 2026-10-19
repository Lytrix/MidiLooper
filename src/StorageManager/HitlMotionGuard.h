// Read-only scalar composition shared by the capture serial owner and native fixture.
#pragma once
#include <cstdint>

namespace StorageManagerInternal {
// Getter calls are an observation seam, not permission to execute storage work.
template <class Storage>
inline bool physicalMotionStorageIdleForHitl(bool playbackPrewarm) {
  return !Storage::isDeferredSaveActive() && !Storage::hasDeferredSaveWork() &&
      !Storage::hasActivePersistenceWorkItem() && !Storage::hasQueuedCaptureChunksToPersist() &&
      !Storage::hasPendingLoopSlotRestore() && !Storage::hasPendingUndoSnapshotHydrate() &&
      !Storage::isRevisionCommitActive() && !Storage::isRevisionLoadActive() && !playbackPrewarm;
}
inline uint32_t physicalMotionGuardBitsForHitl(uint8_t trackBits, bool pendingStart, bool storageIdle) {
  return trackBits | (storageIdle ? 4u : 0u) | (!pendingStart ? 8u : 0u);
}
}  // namespace StorageManagerInternal
