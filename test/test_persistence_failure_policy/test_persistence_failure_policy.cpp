//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include <unity.h>

#include "../../src/PersistenceFailurePolicy.cpp"
#include "../../src/LoopEventStore.cpp"
#include "LoopPasses.h"
#include "PersistenceFailurePolicy.h"
#include "PersistenceQueue.h"

namespace {

void resetPool() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  PersistenceQueue::resetForTests();
  PersistenceQueue::persistenceQueueTestNowMs = 0;
}

MidiEvent noteOn(uint32_t tick) { return MidiEvent::NoteOn(tick, 1, 60, 100); }

uint16_t sealOneChunk(LoopEventStore& store) {
  const uint16_t before = static_cast<uint16_t>(store.chunkIds().size());
  for (uint16_t i = 0; i < LoopEventStoreConfig::CHUNK_CAPACITY; ++i) {
    TEST_ASSERT_TRUE(store.append(noteOn(i)));
  }
  TEST_ASSERT_EQUAL(static_cast<uint16_t>(before + 1), store.chunkIds().size());
  return store.chunkIds().back();
}

}  // namespace

void test_should_run_mid_pass_writer_when_queue_non_empty() {
  TEST_ASSERT_TRUE(PersistenceFailurePolicy::shouldRunMidPassWriter(1, false));
  TEST_ASSERT_FALSE(PersistenceFailurePolicy::shouldRunMidPassWriter(0, false));
  TEST_ASSERT_FALSE(PersistenceFailurePolicy::shouldRunMidPassWriter(3, true));
}

void test_should_run_persistence_work_item_writer() {
  TEST_ASSERT_TRUE(PersistenceFailurePolicy::shouldRunPersistenceWorkItemWriter(1, 0, false));
  TEST_ASSERT_TRUE(PersistenceFailurePolicy::shouldRunPersistenceWorkItemWriter(0, 1, false));
  TEST_ASSERT_FALSE(PersistenceFailurePolicy::shouldRunPersistenceWorkItemWriter(0, 0, false));
  TEST_ASSERT_FALSE(PersistenceFailurePolicy::shouldRunPersistenceWorkItemWriter(2, 0, true));
}

void test_has_persistence_slice_headroom() {
  const uint32_t floor = LoopEventStoreConfig::INTERNAL_HEAP_SAFETY_FLOOR_BYTES;
  const uint32_t belowFloor = floor - 4096U;

  TEST_ASSERT_TRUE(
      PersistenceFailurePolicy::hasPersistenceSliceHeadroom(floor, false, false));
  TEST_ASSERT_FALSE(
      PersistenceFailurePolicy::hasPersistenceSliceHeadroom(belowFloor, false, false));
  TEST_ASSERT_TRUE(
      PersistenceFailurePolicy::hasPersistenceSliceHeadroom(belowFloor, false, true));
  TEST_ASSERT_TRUE(
      PersistenceFailurePolicy::hasPersistenceSliceHeadroom(belowFloor, true, false));
}

void test_capture_pressure_prioritizes_persistence_at_reserve() {
  const uint16_t reserve = PassConfig::CHUNK_RESERVE;
  TEST_ASSERT_EQUAL(static_cast<int>(PersistenceFailurePolicy::CapturePressureAction::PrioritizePersistence),
                    static_cast<int>(PersistenceFailurePolicy::evaluateCapturePressure(reserve, reserve, true,
                                                                                        false)));
  TEST_ASSERT_EQUAL(static_cast<int>(PersistenceFailurePolicy::CapturePressureAction::TelemetryOnly),
                    static_cast<int>(PersistenceFailurePolicy::evaluateCapturePressure(
                        static_cast<uint16_t>(reserve + 1), reserve, true, true)));
  TEST_ASSERT_EQUAL(static_cast<int>(PersistenceFailurePolicy::CapturePressureAction::None),
                    static_cast<int>(PersistenceFailurePolicy::evaluateCapturePressure(
                        static_cast<uint16_t>(reserve + reserve + 1), reserve, true, true)));
}

void test_queue_depth_alarm_threshold() {
  TEST_ASSERT_FALSE(PersistenceFailurePolicy::shouldEmitQueueDepthAlarm(
      PersistenceFailurePolicy::kQueueDepthAlarmThreshold - 1, true));
  TEST_ASSERT_TRUE(PersistenceFailurePolicy::shouldEmitQueueDepthAlarm(
      PersistenceFailurePolicy::kQueueDepthAlarmThreshold, true));
  TEST_ASSERT_FALSE(PersistenceFailurePolicy::shouldEmitQueueDepthAlarm(
      PersistenceFailurePolicy::kQueueDepthAlarmThreshold, false));
}

void test_oldest_queued_chunk_age_tracks_admit_order() {
  resetPool();
  PersistenceQueue::persistenceQueueTestNowMs = 1000;
  LoopEventStore store;

  (void)sealOneChunk(store);
  TEST_ASSERT_EQUAL(1u, PersistenceQueue::queueDepth());

  PersistenceQueue::persistenceQueueTestNowMs = 2500;
  (void)sealOneChunk(store);
  TEST_ASSERT_EQUAL(2u, PersistenceQueue::queueDepth());

  TEST_ASSERT_EQUAL(1500U, PersistenceQueue::oldestQueuedChunkAgeMs());
}

void test_requeue_writing_chunk_returns_to_queue_head() {
  resetPool();
  LoopEventStore store;

  const uint16_t firstId = sealOneChunk(store);
  const uint16_t secondId = sealOneChunk(store);

  uint16_t drained = 0;
  TEST_ASSERT_TRUE(PersistenceQueue::beginWriteQueuedChunk(drained));
  TEST_ASSERT_EQUAL(firstId, drained);
  PersistenceQueue::requeueWritingChunk(drained);

  uint16_t ids[2] = {};
  TEST_ASSERT_EQUAL(2u, PersistenceQueue::queuedChunkIds(ids, 2));
  TEST_ASSERT_EQUAL(firstId, ids[0]);
  TEST_ASSERT_EQUAL(secondId, ids[1]);
}

int main(int argc, char** argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_should_run_mid_pass_writer_when_queue_non_empty);
  RUN_TEST(test_should_run_persistence_work_item_writer);
  RUN_TEST(test_has_persistence_slice_headroom);
  RUN_TEST(test_capture_pressure_prioritizes_persistence_at_reserve);
  RUN_TEST(test_queue_depth_alarm_threshold);
  RUN_TEST(test_oldest_queued_chunk_age_tracks_admit_order);
  RUN_TEST(test_requeue_writing_chunk_returns_to_queue_head);
  return UNITY_END();
}
