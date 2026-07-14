//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "StorageManagerInternal/PersistenceWorkQueue.h"

#include <cstring>
#include <new>

#if defined(ARDUINO)
#include <Arduino.h>
#endif

namespace {

constexpr uint16_t kMaxWorkEntries = 256;
constexpr uint16_t kInvalidEntryIndex = UINT16_MAX;

struct WorkEntry {
  PersistWorkItem item{};
  PersistWorkState state = PersistWorkState::NotScheduled;
};

struct QueueState {
  WorkEntry entries[kMaxWorkEntries] = {};
  uint16_t queueOrder[kMaxWorkEntries] = {};
  uint16_t queueHead = 0;
  uint16_t queueTail = 0;
  uint16_t writingEntryIndex = kInvalidEntryIndex;
};

QueueState* queueState() {
  static QueueState* state = nullptr;
  if (state != nullptr) {
    return state;
  }
#if defined(ARDUINO) && defined(__IMXRT1062__)
  state = static_cast<QueueState*>(extmem_malloc(sizeof(QueueState)));
  if (state != nullptr) {
    std::memset(state, 0, sizeof(QueueState));
    state->writingEntryIndex = kInvalidEntryIndex;
  }
#else
  static QueueState nativeState;
  state = &nativeState;
  state->writingEntryIndex = kInvalidEntryIndex;
#endif
  return state;
}

bool queueEmpty(const QueueState& state) { return state.queueHead == state.queueTail; }

bool queuePush(QueueState& state, uint16_t entryIndex) {
  const uint16_t nextTail = static_cast<uint16_t>((state.queueTail + 1) % kMaxWorkEntries);
  if (nextTail == state.queueHead) {
    return false;
  }
  state.queueOrder[state.queueTail] = entryIndex;
  state.queueTail = nextTail;
  return true;
}

bool queuePeek(const QueueState& state, uint16_t& entryIndexOut) {
  if (queueEmpty(state)) {
    return false;
  }
  entryIndexOut = state.queueOrder[state.queueHead];
  return true;
}

void queuePop(QueueState& state) {
  if (queueEmpty(state)) {
    return;
  }
  state.queueHead = static_cast<uint16_t>((state.queueHead + 1) % kMaxWorkEntries);
}

bool queuePushFront(QueueState& state, uint16_t entryIndex) {
  if (queueEmpty(state)) {
    return queuePush(state, entryIndex);
  }
  const uint16_t prevHead =
      static_cast<uint16_t>((state.queueHead + kMaxWorkEntries - 1) % kMaxWorkEntries);
  if (prevHead == state.queueTail) {
    return false;
  }
  state.queueHead = prevHead;
  state.queueOrder[state.queueHead] = entryIndex;
  return true;
}

uint16_t findEntryIndex(const QueueState& state, PersistWorkType type, PersistKey key) {
  for (uint16_t i = 0; i < kMaxWorkEntries; ++i) {
    if (state.entries[i].state == PersistWorkState::NotScheduled) {
      continue;
    }
    if (persistWorkItemsEqual(state.entries[i].item, PersistWorkItem{type, key})) {
      return i;
    }
  }
  return kInvalidEntryIndex;
}

uint16_t allocateEntryIndex(QueueState& state) {
  for (uint16_t i = 0; i < kMaxWorkEntries; ++i) {
    if (state.entries[i].state == PersistWorkState::NotScheduled) {
      return i;
    }
  }
  return kInvalidEntryIndex;
}

uint16_t countQueued(const QueueState& state) {
  uint16_t count = 0;
  for (uint16_t i = 0; i < kMaxWorkEntries; ++i) {
    if (state.entries[i].state == PersistWorkState::Queued) {
      ++count;
    }
  }
  return count;
}

}  // namespace

bool persistKeysEqual(const PersistKey& left, const PersistKey& right) {
  if (left.kind != right.kind) {
    return false;
  }
  switch (left.kind) {
    case PersistKeyKind::Singleton:
      return true;
    case PersistKeyKind::LoopId:
      return left.loopId == right.loopId;
    case PersistKeyKind::Track:
      return left.trackIndex == right.trackIndex;
    case PersistKeyKind::Slot:
      return left.trackIndex == right.trackIndex && left.slotIndex == right.slotIndex;
  }
  return false;
}

bool persistWorkItemsEqual(const PersistWorkItem& left, const PersistWorkItem& right) {
  return left.type == right.type && persistKeysEqual(left.key, right.key);
}

PersistKey persistKeySingleton() { return PersistKey{}; }

PersistKey persistKeyForLoop(LoopId loopId) {
  PersistKey key;
  key.kind = PersistKeyKind::LoopId;
  key.loopId = loopId;
  return key;
}

PersistKey persistKeyForTrack(uint8_t trackIndex) {
  PersistKey key;
  key.kind = PersistKeyKind::Track;
  key.trackIndex = trackIndex;
  return key;
}

PersistKey persistKeyForSlot(uint8_t trackIndex, uint8_t slotIndex) {
  PersistKey key;
  key.kind = PersistKeyKind::Slot;
  key.trackIndex = trackIndex;
  key.slotIndex = slotIndex;
  return key;
}

namespace PersistenceWorkQueue {

uint16_t queueDepth() {
  return countQueued(*queueState());
}

uint16_t writingWorkItemCount() {
  QueueState* state = queueState();
  if (state == nullptr || state->writingEntryIndex == kInvalidEntryIndex) {
    return 0;
  }
  return 1;
}

PersistWorkState workState(PersistWorkType type, PersistKey key) {
  const uint16_t index = findEntryIndex(*queueState(), type, key);
  if (index == kInvalidEntryIndex) {
    return PersistWorkState::NotScheduled;
  }
  return queueState()->entries[index].state;
}

bool admitWork(PersistWorkType type, PersistKey key) {
  QueueState* state = queueState();
  if (state == nullptr) {
    return false;
  }

  const PersistWorkItem item{type, key};
  uint16_t entryIndex = findEntryIndex(*state, type, key);

  if (entryIndex != kInvalidEntryIndex) {
    const PersistWorkState existing = state->entries[entryIndex].state;
    if (existing == PersistWorkState::Queued || existing == PersistWorkState::Writing) {
      return false;
    }
    if (existing == PersistWorkState::Persisted) {
      if (!queuePush(*state, entryIndex)) {
        return false;
      }
      state->entries[entryIndex].state = PersistWorkState::Queued;
      return true;
    }
    return false;
  }

  entryIndex = allocateEntryIndex(*state);
  if (entryIndex == kInvalidEntryIndex) {
    return false;
  }
  if (!queuePush(*state, entryIndex)) {
    return false;
  }
  state->entries[entryIndex].item = item;
  state->entries[entryIndex].state = PersistWorkState::Queued;
  return true;
}

bool beginWriteQueuedItem(PersistWorkItem& itemOut) {
  QueueState* state = queueState();
  if (state == nullptr) {
    return false;
  }

  uint16_t entryIndex = kInvalidEntryIndex;
  if (!queuePeek(*state, entryIndex)) {
    return false;
  }
  if (entryIndex >= kMaxWorkEntries ||
      state->entries[entryIndex].state != PersistWorkState::Queued) {
    queuePop(*state);
    return beginWriteQueuedItem(itemOut);
  }

  state->entries[entryIndex].state = PersistWorkState::Writing;
  state->writingEntryIndex = entryIndex;
  itemOut = state->entries[entryIndex].item;
  queuePop(*state);
  return true;
}

void markItemPersisted(const PersistWorkItem& item) {
  QueueState* state = queueState();
  if (state == nullptr) {
    return;
  }

  const uint16_t entryIndex = findEntryIndex(*state, item.type, item.key);
  if (entryIndex == kInvalidEntryIndex) {
    return;
  }
  if (state->entries[entryIndex].state != PersistWorkState::Writing) {
    return;
  }
  state->entries[entryIndex].state = PersistWorkState::Persisted;
  if (state->writingEntryIndex == entryIndex) {
    state->writingEntryIndex = kInvalidEntryIndex;
  }
}

void requeueWritingItem(const PersistWorkItem& item) {
  QueueState* state = queueState();
  if (state == nullptr) {
    return;
  }

  const uint16_t entryIndex = findEntryIndex(*state, item.type, item.key);
  if (entryIndex == kInvalidEntryIndex ||
      state->entries[entryIndex].state != PersistWorkState::Writing) {
    return;
  }
  state->entries[entryIndex].state = PersistWorkState::Queued;
  if (state->writingEntryIndex == entryIndex) {
    state->writingEntryIndex = kInvalidEntryIndex;
  }
  if (!queuePushFront(*state, entryIndex)) {
    state->entries[entryIndex].state = PersistWorkState::Writing;
    state->writingEntryIndex = entryIndex;
  }
}

void resetForTests() {
  QueueState* state = queueState();
  if (state == nullptr) {
    return;
  }
  std::memset(state, 0, sizeof(QueueState));
  state->writingEntryIndex = kInvalidEntryIndex;
}

#if defined(PIO_UNIT_TEST_NATIVE)
size_t queuedWorkItems(PersistWorkItem* outItems, size_t maxCount) {
  QueueState* state = queueState();
  if (state == nullptr || outItems == nullptr || maxCount == 0) {
    return 0;
  }

  size_t written = 0;
  uint16_t cursor = state->queueHead;
  while (cursor != state->queueTail && written < maxCount) {
    const uint16_t entryIndex = state->queueOrder[cursor];
    if (entryIndex < kMaxWorkEntries &&
        state->entries[entryIndex].state == PersistWorkState::Queued) {
      outItems[written++] = state->entries[entryIndex].item;
    }
    cursor = static_cast<uint16_t>((cursor + 1) % kMaxWorkEntries);
  }
  return written;
}
#endif

}  // namespace PersistenceWorkQueue

#if defined(ARDUINO) && defined(__IMXRT1062__)
extern "C" void* extmem_malloc(size_t size);
#endif
