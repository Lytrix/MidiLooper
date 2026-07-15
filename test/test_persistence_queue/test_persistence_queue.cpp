//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include <unity.h>

#include "../../src/LoopEventStore.cpp"

#include "LoopEventStore.h"
#include "MidiEvent.h"
#include "PersistenceQueue.h"

namespace {

void resetPool() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
}

MidiEvent noteOn(uint32_t tick) { return MidiEvent::NoteOn(tick, 1, 60, 100); }

}  // namespace

void test_admit_on_capacity_preserves_seal_order() {
  resetPool();
  LoopEventStore store;

  for (uint16_t i = 0; i < LoopEventStoreConfig::CHUNK_CAPACITY * 2; ++i) {
    TEST_ASSERT_TRUE(store.append(noteOn(i)));
  }
  TEST_ASSERT_EQUAL(2u, PersistenceQueue::queueDepth());
  const uint16_t firstId = store.chunkIds().front();
  const uint16_t secondId = store.chunkIds()[1];
  TEST_ASSERT_TRUE(PersistenceQueue::sealSequenceForChunk(secondId) >
                   PersistenceQueue::sealSequenceForChunk(firstId));

  uint16_t queued[4] = {};
  TEST_ASSERT_EQUAL(2u, PersistenceQueue::queuedChunkIds(queued, 4));
  TEST_ASSERT_EQUAL(firstId, queued[0]);
  TEST_ASSERT_EQUAL(secondId, queued[1]);
}

void test_pass_close_tail_admitted_once() {
  resetPool();
  LoopEventStore store;

  for (uint16_t i = 0; i < LoopEventStoreConfig::CHUNK_CAPACITY + 5; ++i) {
    TEST_ASSERT_TRUE(store.append(noteOn(i)));
  }

  CaptureChunkIdList refs;
  store.detachChunksTo(refs);
  TEST_ASSERT_EQUAL(2u, refs.size());
  TEST_ASSERT_EQUAL(2u, PersistenceQueue::queueDepth());
  for (uint16_t id : refs) {
    TEST_ASSERT_EQUAL(static_cast<int>(ChunkPersistenceState::Queued),
                      static_cast<int>(PersistenceQueue::chunkState(id)));
  }
}

void test_exactly_once_admission() {
  resetPool();
  LoopEventStore store;

  for (uint16_t i = 0; i < LoopEventStoreConfig::CHUNK_CAPACITY; ++i) {
    TEST_ASSERT_TRUE(store.append(noteOn(i)));
  }
  const uint16_t sealedId = store.chunkIds().front();
  TEST_ASSERT_EQUAL(1u, PersistenceQueue::queueDepth());
  TEST_ASSERT_FALSE(PersistenceQueue::admitSealedChunk(sealedId));
  TEST_ASSERT_EQUAL(1u, PersistenceQueue::queueDepth());
}

void test_drain_order_matches_seal_order() {
  resetPool();
  LoopEventStore store;

  for (uint16_t i = 0; i < LoopEventStoreConfig::CHUNK_CAPACITY * 3; ++i) {
    TEST_ASSERT_TRUE(store.append(noteOn(i)));
  }
  TEST_ASSERT_EQUAL(3u, PersistenceQueue::queueDepth());

  uint16_t expected[3] = {};
  TEST_ASSERT_EQUAL(3u, PersistenceQueue::queuedChunkIds(expected, 3));

  uint16_t drained[3] = {};
  for (size_t i = 0; i < 3; ++i) {
    TEST_ASSERT_TRUE(PersistenceQueue::beginWriteQueuedChunk(drained[i]));
    TEST_ASSERT_EQUAL(expected[i], drained[i]);
    PersistenceQueue::markChunkPersisted(drained[i]);
  }
  TEST_ASSERT_EQUAL(0u, PersistenceQueue::queueDepth());
  for (size_t i = 0; i < 3; ++i) {
    TEST_ASSERT_EQUAL(static_cast<int>(ChunkPersistenceState::Persisted),
                      static_cast<int>(PersistenceQueue::chunkState(drained[i])));
  }
}

void test_persisted_chunk_not_re_enqueued() {
  resetPool();
  LoopEventStore store;

  for (uint16_t i = 0; i < LoopEventStoreConfig::CHUNK_CAPACITY; ++i) {
    TEST_ASSERT_TRUE(store.append(noteOn(i)));
  }
  const uint16_t sealedId = store.chunkIds().front();
  uint16_t writeId = 0;
  TEST_ASSERT_TRUE(PersistenceQueue::beginWriteQueuedChunk(writeId));
  TEST_ASSERT_EQUAL(sealedId, writeId);
  PersistenceQueue::markChunkPersisted(writeId);
  TEST_ASSERT_FALSE(PersistenceQueue::admitSealedChunk(sealedId));
  TEST_ASSERT_EQUAL(static_cast<int>(ChunkPersistenceState::Persisted),
                    static_cast<int>(PersistenceQueue::chunkState(sealedId)));
}

void test_chunk_freed_resets_persistence_state() {
  resetPool();
  LoopEventStore store;

  for (uint16_t i = 0; i < LoopEventStoreConfig::CHUNK_CAPACITY; ++i) {
    TEST_ASSERT_TRUE(store.append(noteOn(i)));
  }
  const uint16_t chunkId = store.chunkIds().front();
  TEST_ASSERT_EQUAL(1u, PersistenceQueue::queueDepth());
  store.clear();
  TEST_ASSERT_EQUAL(static_cast<int>(ChunkPersistenceState::NotScheduled),
                    static_cast<int>(PersistenceQueue::chunkState(chunkId)));

  for (uint16_t i = 0; i < LoopEventStoreConfig::CHUNK_CAPACITY; ++i) {
    TEST_ASSERT_TRUE(store.append(noteOn(i + 100)));
  }
  const uint16_t reallocatedId = store.chunkIds().front();
  TEST_ASSERT_EQUAL(chunkId, reallocatedId);
  TEST_ASSERT_EQUAL(1u, PersistenceQueue::queueDepth());
}

int main(int /*argc*/, char** /*argv*/) {
  UNITY_BEGIN();
  RUN_TEST(test_admit_on_capacity_preserves_seal_order);
  RUN_TEST(test_pass_close_tail_admitted_once);
  RUN_TEST(test_exactly_once_admission);
  RUN_TEST(test_drain_order_matches_seal_order);
  RUN_TEST(test_persisted_chunk_not_re_enqueued);
  RUN_TEST(test_chunk_freed_resets_persistence_state);
  return UNITY_END();
}
