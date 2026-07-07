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

void test_seal_on_capacity_creates_new_recording_tail() {
  resetPool();
  LoopEventStore store;

  for (uint16_t i = 0; i < LoopEventStoreConfig::CHUNK_CAPACITY; ++i) {
    TEST_ASSERT_TRUE(store.append(noteOn(i)));
  }
  TEST_ASSERT_EQUAL(1u, store.chunkIds().size());
  const uint16_t firstId = store.chunkIds().front();
  TEST_ASSERT_EQUAL(static_cast<int>(ChunkLifecycleState::Sealed),
                    static_cast<int>(LoopEventStore::chunkLifecycleState(firstId)));

  TEST_ASSERT_TRUE(store.append(noteOn(LoopEventStoreConfig::CHUNK_CAPACITY)));
  TEST_ASSERT_EQUAL(2u, store.chunkIds().size());
  TEST_ASSERT_EQUAL(static_cast<int>(ChunkLifecycleState::Sealed),
                    static_cast<int>(LoopEventStore::chunkLifecycleState(firstId)));
  const uint16_t tailId = store.chunkIds().back();
  TEST_ASSERT_EQUAL(static_cast<int>(ChunkLifecycleState::Recording),
                    static_cast<int>(LoopEventStore::chunkLifecycleState(tailId)));
}

void test_single_recording_tail_during_append() {
  resetPool();
  LoopEventStore store;

  for (uint16_t i = 0; i < LoopEventStoreConfig::CHUNK_CAPACITY + 10; ++i) {
    TEST_ASSERT_TRUE(store.append(noteOn(i)));
  }

  uint16_t recordingCount = 0;
  for (uint16_t id : store.chunkIds()) {
    if (LoopEventStore::chunkLifecycleState(id) == ChunkLifecycleState::Recording) {
      ++recordingCount;
    }
  }
  TEST_ASSERT_EQUAL(1u, recordingCount);
}

void test_sealed_chunk_immutable_under_shift() {
  resetPool();
  LoopEventStore store;

  for (uint16_t i = 0; i < LoopEventStoreConfig::CHUNK_CAPACITY; ++i) {
    TEST_ASSERT_TRUE(store.append(noteOn(i)));
  }
  TEST_ASSERT_TRUE(store.append(noteOn(500)));

  const uint16_t sealedId = store.chunkIds().front();
  const uint32_t sealedFirstTick = store.at(0).tick;

  store.shiftAllTicks(100);
  TEST_ASSERT_EQUAL(sealedFirstTick, store.at(0).tick);
  TEST_ASSERT_EQUAL(static_cast<int>(ChunkLifecycleState::Sealed),
                    static_cast<int>(LoopEventStore::chunkLifecycleState(sealedId)));
  TEST_ASSERT_EQUAL(500u + 100u, store.at(LoopEventStoreConfig::CHUNK_CAPACITY).tick);
}

void test_detach_chunks_to_seals_all_chunks() {
  resetPool();
  LoopEventStore store;

  for (uint16_t i = 0; i < LoopEventStoreConfig::CHUNK_CAPACITY + 1; ++i) {
    TEST_ASSERT_TRUE(store.append(noteOn(i)));
  }

  ChunkIdList refs;
  store.detachChunksTo(refs);
  TEST_ASSERT_TRUE(store.empty());
  TEST_ASSERT_EQUAL(2u, refs.size());

  for (uint16_t id : refs) {
    TEST_ASSERT_EQUAL(static_cast<int>(ChunkLifecycleState::Sealed),
                      static_cast<int>(LoopEventStore::chunkLifecycleState(id)));
    TEST_ASSERT_EQUAL(1u, LoopEventStore::chunkReferenceCount(id));
  }
}

void test_reference_released_on_clear_reclaims_chunk() {
  resetPool();
  LoopEventStore store;

  TEST_ASSERT_TRUE(store.append(noteOn(1)));
  const uint16_t chunkId = store.chunkIds().front();
  TEST_ASSERT_EQUAL(1u, LoopEventStore::chunkReferenceCount(chunkId));

  store.clear();
  TEST_ASSERT_EQUAL(0u, LoopEventStore::chunkReferenceCount(chunkId));
  TEST_ASSERT_EQUAL(static_cast<int>(ChunkLifecycleState::Free),
                    static_cast<int>(LoopEventStore::chunkLifecycleState(chunkId)));
  TEST_ASSERT_EQUAL(0u, LoopEventStore::usedChunkCount());
}

void test_runtime_sealed_independent_of_persistence_proxy() {
  resetPool();
  LoopEventStore store;

  for (uint16_t i = 0; i < LoopEventStoreConfig::CHUNK_CAPACITY; ++i) {
    TEST_ASSERT_TRUE(store.append(noteOn(i)));
  }

  const uint16_t sealedId = store.chunkIds().front();
  TEST_ASSERT_EQUAL(static_cast<int>(ChunkLifecycleState::Sealed),
                    static_cast<int>(LoopEventStore::chunkLifecycleState(sealedId)));
  TEST_ASSERT_EQUAL(static_cast<int>(ChunkPersistenceState::Queued),
                    static_cast<int>(PersistenceQueue::chunkState(sealedId)));

  TEST_ASSERT_TRUE(store.append(noteOn(999)));
  TEST_ASSERT_EQUAL(static_cast<int>(ChunkLifecycleState::Sealed),
                    static_cast<int>(LoopEventStore::chunkLifecycleState(sealedId)));
}

int main(int /*argc*/, char** /*argv*/) {
  UNITY_BEGIN();
  RUN_TEST(test_seal_on_capacity_creates_new_recording_tail);
  RUN_TEST(test_single_recording_tail_during_append);
  RUN_TEST(test_sealed_chunk_immutable_under_shift);
  RUN_TEST(test_detach_chunks_to_seals_all_chunks);
  RUN_TEST(test_reference_released_on_clear_reclaims_chunk);
  RUN_TEST(test_runtime_sealed_independent_of_persistence_proxy);
  return UNITY_END();
}
