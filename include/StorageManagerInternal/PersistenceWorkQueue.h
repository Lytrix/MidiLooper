//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#pragma once

#include <cstdint>

#include "LoopPasses.h"

/// Semantic persistence work lifecycle (DEC-020 work-item queue).
enum class PersistWorkState : uint8_t {
  NotScheduled = 0,
  Queued,
  Writing,
  Persisted,
};

enum class PersistWorkType : uint8_t {
  GlobalMeta = 0,
  TrackMeta,
  SlotMeta,
  LoopPersist,
  LoopUndoHistory,
  WorkspaceFooter,
  FinalizeWorkspace,
};

enum class PersistKeyKind : uint8_t {
  Singleton = 0,
  LoopId,
  Track,
  Slot,
};

struct PersistKey {
  PersistKeyKind kind = PersistKeyKind::Singleton;
  LoopId loopId = kInvalidLoopId;
  uint8_t trackIndex = 0xFF;
  uint8_t slotIndex = 0xFF;
};

struct PersistWorkItem {
  PersistWorkType type = PersistWorkType::GlobalMeta;
  PersistKey key{};
};

bool persistKeysEqual(const PersistKey& left, const PersistKey& right);
bool persistWorkItemsEqual(const PersistWorkItem& left, const PersistWorkItem& right);

PersistKey persistKeySingleton();
PersistKey persistKeyForLoop(LoopId loopId);
PersistKey persistKeyForTrack(uint8_t trackIndex);
PersistKey persistKeyForSlot(uint8_t trackIndex, uint8_t slotIndex);

/// Internal to StorageManager — include only from StorageManager TU and native tests.
namespace PersistenceWorkQueue {

uint16_t queueDepth();

/// Items in Writing state (slice in progress).
uint16_t writingWorkItemCount();

PersistWorkState workState(PersistWorkType type, PersistKey key);

/// Admit stale work. Returns true when newly queued. Re-admit while Queued/Writing is a no-op (F2).
bool admitWork(PersistWorkType type, PersistKey key);

/// Next queued item in FIFO order. Returns false when empty.
bool beginWriteQueuedItem(PersistWorkItem& itemOut);

void markItemPersisted(const PersistWorkItem& item);

/// Return a Writing item to the queue head after a failed SD write.
void requeueWritingItem(const PersistWorkItem& item);

void resetForTests();

#if defined(PIO_UNIT_TEST_NATIVE)
size_t queuedWorkItems(PersistWorkItem* outItems, size_t maxCount);
#endif

}  // namespace PersistenceWorkQueue
