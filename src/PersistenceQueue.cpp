//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "PersistenceQueue.h"

#include <cstring>
#include <new>

#if defined(ARDUINO)
#include <Arduino.h>
#endif

namespace PersistenceQueue {
namespace {

struct QueueState {
  ChunkPersistenceState chunkState[LoopEventStoreConfig::POOL_CHUNK_COUNT] = {};
  uint16_t sealSequence[LoopEventStoreConfig::POOL_CHUNK_COUNT] = {};
  uint16_t queueOrder[LoopEventStoreConfig::POOL_CHUNK_COUNT] = {};
  uint32_t nextSealSequence = 1;
  uint16_t queueHead = 0;
  uint16_t queueTail = 0;
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
    state->nextSealSequence = 1;
  }
#else
  static QueueState nativeState;
  state = &nativeState;
#endif
  return state;
}

constexpr uint16_t kInvalidChunkId = UINT16_MAX;

bool queueEmpty(const QueueState& state) { return state.queueHead == state.queueTail; }

bool queuePush(QueueState& state, uint16_t chunkId) {
  const uint16_t nextTail =
      static_cast<uint16_t>((state.queueTail + 1) % LoopEventStoreConfig::POOL_CHUNK_COUNT);
  if (nextTail == state.queueHead) {
    return false;
  }
  state.queueOrder[state.queueTail] = chunkId;
  state.queueTail = nextTail;
  return true;
}

bool queuePeek(const QueueState& state, uint16_t& chunkIdOut) {
  if (queueEmpty(state)) {
    return false;
  }
  chunkIdOut = state.queueOrder[state.queueHead];
  return true;
}

void queuePop(QueueState& state) {
  if (queueEmpty(state)) {
    return;
  }
  state.queueHead =
      static_cast<uint16_t>((state.queueHead + 1) % LoopEventStoreConfig::POOL_CHUNK_COUNT);
}

uint16_t countChunksInState(const QueueState& state, ChunkPersistenceState target) {
  uint16_t count = 0;
  for (uint16_t i = 0; i < LoopEventStoreConfig::POOL_CHUNK_COUNT; ++i) {
    if (state.chunkState[i] == target) {
      ++count;
    }
  }
  return count;
}

}  // namespace

uint16_t queueDepth() {
  QueueState* state = queueState();
  return state != nullptr ? countChunksInState(*state, ChunkPersistenceState::Queued) : 0;
}

uint16_t writingChunkCount() {
  QueueState* state = queueState();
  return state != nullptr ? countChunksInState(*state, ChunkPersistenceState::Writing) : 0;
}

ChunkPersistenceState chunkState(uint16_t chunkId) {
  QueueState* state = queueState();
  if (state == nullptr || chunkId >= LoopEventStoreConfig::POOL_CHUNK_COUNT) {
    return ChunkPersistenceState::NotScheduled;
  }
  return state->chunkState[chunkId];
}

bool admitSealedChunk(uint16_t chunkId) {
  QueueState* state = queueState();
  if (state == nullptr || chunkId >= LoopEventStoreConfig::POOL_CHUNK_COUNT) {
    return false;
  }
  if (LoopEventStore::chunkLifecycleState(chunkId) != ChunkLifecycleState::Sealed) {
    return false;
  }
  const ChunkPersistenceState existing = state->chunkState[chunkId];
  if (existing == ChunkPersistenceState::Queued || existing == ChunkPersistenceState::Writing ||
      existing == ChunkPersistenceState::Persisted) {
    return false;
  }
  if (!queuePush(*state, chunkId)) {
    return false;
  }
  state->chunkState[chunkId] = ChunkPersistenceState::Queued;
  state->sealSequence[chunkId] = static_cast<uint16_t>(state->nextSealSequence++);
  return true;
}

bool beginWriteQueuedChunk(uint16_t& chunkIdOut) {
  QueueState* state = queueState();
  if (state == nullptr) {
    return false;
  }
  uint16_t candidate = kInvalidChunkId;
  if (!queuePeek(*state, candidate)) {
    return false;
  }
  if (state->chunkState[candidate] != ChunkPersistenceState::Queued) {
    queuePop(*state);
    return beginWriteQueuedChunk(chunkIdOut);
  }
  state->chunkState[candidate] = ChunkPersistenceState::Writing;
  queuePop(*state);
  chunkIdOut = candidate;
  return true;
}

void markChunkPersisted(uint16_t chunkId) {
  QueueState* state = queueState();
  if (state == nullptr || chunkId >= LoopEventStoreConfig::POOL_CHUNK_COUNT) {
    return;
  }
  if (state->chunkState[chunkId] == ChunkPersistenceState::Writing) {
    state->chunkState[chunkId] = ChunkPersistenceState::Persisted;
  }
}

void onChunkFreed(uint16_t chunkId) {
  QueueState* state = queueState();
  if (state == nullptr || chunkId >= LoopEventStoreConfig::POOL_CHUNK_COUNT) {
    return;
  }
  state->chunkState[chunkId] = ChunkPersistenceState::NotScheduled;
  state->sealSequence[chunkId] = 0;
}

void resetForTests() {
  QueueState* state = queueState();
  if (state == nullptr) {
    return;
  }
  std::memset(state, 0, sizeof(QueueState));
  state->nextSealSequence = 1;
}

#if defined(PIO_UNIT_TEST_NATIVE)
uint32_t sealSequenceForChunk(uint16_t chunkId) {
  QueueState* state = queueState();
  if (state == nullptr || chunkId >= LoopEventStoreConfig::POOL_CHUNK_COUNT) {
    return 0;
  }
  return state->sealSequence[chunkId];
}

size_t queuedChunkIds(uint16_t* outIds, size_t maxCount) {
  QueueState* state = queueState();
  if (state == nullptr || outIds == nullptr || maxCount == 0) {
    return 0;
  }
  size_t written = 0;
  uint16_t cursor = state->queueHead;
  while (cursor != state->queueTail && written < maxCount) {
    outIds[written++] = state->queueOrder[cursor];
    cursor = static_cast<uint16_t>((cursor + 1) % LoopEventStoreConfig::POOL_CHUNK_COUNT);
  }
  return written;
}
#endif

}  // namespace PersistenceQueue

#if defined(ARDUINO) && defined(__IMXRT1062__)
extern "C" void* extmem_malloc(size_t size);
#endif
