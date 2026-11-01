//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include <unity.h>

#include "../../src/LoopEventStore.cpp"
#include "../../src/StorageManager/SlotLoadSession.cpp"

#include "LoopEventStore.h"
#include "MidiEvent.h"
#include "PersistenceQueue.h"
#include "SlotLoadSession.h"

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

void test_sd_load_staging_marks_persisted_without_queue() {
  resetPool();
  LoopEventStore::enterSdLoadStaging();
  LoopEventStore store;

  for (uint16_t i = 0; i < LoopEventStoreConfig::CHUNK_CAPACITY + 5; ++i) {
    TEST_ASSERT_TRUE(store.append(noteOn(i)));
  }
  CaptureChunkIdList refs;
  store.detachChunksTo(refs);
  TEST_ASSERT_EQUAL(2u, refs.size());
  TEST_ASSERT_EQUAL(0u, PersistenceQueue::queueDepth());
  for (uint16_t id : refs) {
    TEST_ASSERT_EQUAL(static_cast<int>(ChunkPersistenceState::Persisted),
                      static_cast<int>(PersistenceQueue::chunkState(id)));
  }
  LoopEventStore::leaveSdLoadStaging();
}


/// Device wedge 2026-11-01 (`DIAG,order_deny,reason=5,…,persist=3`, three chains on `cb4694f5`/`975e0577`/`909c930d`):
/// the Record take's mid-pass writer logged `queue=0, admit_refused=0` for the whole take while the take's own chunk
/// read `Persisted`, i.e. its chunks were sealed through the SD-load branch. A load job keeps its `SlotLoadSession`
/// alive across main-loop turns (it pauses while capture runs), so a live session must not turn an unrelated capture
/// seal into "already on SD": that chunk is never written and the Record stop's private-order preflight denies it.
void test_capture_seal_while_a_slot_load_session_is_alive_is_queued_not_persisted() {
  resetPool();
  PersistenceQueue::resetForTests();
  SlotLoadSession otherSlotLoading(1, 0);  // a boot-sweep load of another slot, parked across turns
  LoopEventStore capture;
  for (uint16_t i = 0; i < LoopEventStoreConfig::CHUNK_CAPACITY + 5; ++i) {
    TEST_ASSERT_TRUE(capture.append(noteOn(i)));
  }
  const uint16_t first = capture.chunkIds().front();
  TEST_ASSERT_EQUAL_MESSAGE(static_cast<int>(ChunkPersistenceState::Queued),
                            static_cast<int>(PersistenceQueue::chunkState(first)),
                            "a capture chunk sealed while a slot load is in flight must be admitted to persistence");
  TEST_ASSERT_TRUE(PersistenceQueue::queueDepth() >= 1u);
}

void test_ephemeral_seal_does_not_enqueue_mid_pass() {
  resetPool();
  LoopEventStore::enterEphemeralSeal();
  LoopEventStore store;

  for (uint16_t i = 0; i < LoopEventStoreConfig::CHUNK_CAPACITY + 5; ++i) {
    TEST_ASSERT_TRUE(store.append(noteOn(i)));
  }
  CaptureChunkIdList refs;
  store.detachChunksTo(refs);
  TEST_ASSERT_EQUAL(2u, refs.size());
  TEST_ASSERT_EQUAL(0u, PersistenceQueue::queueDepth());
  for (uint16_t id : refs) {
    TEST_ASSERT_EQUAL(static_cast<int>(ChunkPersistenceState::NotScheduled),
                      static_cast<int>(PersistenceQueue::chunkState(id)));
  }
  LoopEventStore::leaveEphemeralSeal();
}

void test_mark_chunk_persisted_from_sd_load_purges_queued() {
  resetPool();
  LoopEventStore store;

  for (uint16_t i = 0; i < LoopEventStoreConfig::CHUNK_CAPACITY; ++i) {
    TEST_ASSERT_TRUE(store.append(noteOn(i)));
  }
  const uint16_t sealedId = store.chunkIds().front();
  TEST_ASSERT_EQUAL(1u, PersistenceQueue::queueDepth());
  TEST_ASSERT_TRUE(PersistenceQueue::markChunkPersistedFromSdLoad(sealedId));
  TEST_ASSERT_EQUAL(0u, PersistenceQueue::queueDepth());
  TEST_ASSERT_EQUAL(static_cast<int>(ChunkPersistenceState::Persisted),
                    static_cast<int>(PersistenceQueue::chunkState(sealedId)));
  TEST_ASSERT_FALSE(PersistenceQueue::admitSealedChunk(sealedId));
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
  RUN_TEST(test_sd_load_staging_marks_persisted_without_queue);
  RUN_TEST(test_capture_seal_while_a_slot_load_session_is_alive_is_queued_not_persisted);
  RUN_TEST(test_ephemeral_seal_does_not_enqueue_mid_pass);
  RUN_TEST(test_mark_chunk_persisted_from_sd_load_purges_queued);
  RUN_TEST(test_chunk_freed_resets_persistence_state);
  return UNITY_END();
}
