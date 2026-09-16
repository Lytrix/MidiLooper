// Copyright (c) 2026 Lytrix (Eelke Jager)
// Licensed under the PolyForm Noncommercial 1.0.0
#include <unity.h>
#include "../../src/LoopEventStore.cpp"

void setUp() { LoopEventStore::resetPoolForTests(); }
void tearDown() {}

void test_reserved_canonical_output_preserves_sealed_raw_under_full_pool_pressure() {
  LoopEventStore producer, output, pressure;
  LoopEventStore::CaptureRange raw;
  LoopEventStore::ChunkReservation credits;
  TEST_ASSERT_TRUE(producer.reserveCaptureTail());
  TEST_ASSERT_TRUE(raw.reserveTail());
  TEST_ASSERT_TRUE(output.prewarmCaptureDirectory());
  TEST_ASSERT_TRUE(credits.reserveChunks(2));
  for (size_t i = 0; i < 257; ++i)
    TEST_ASSERT_TRUE(producer.append(MidiEvent::ControlChange(256 - i, 1, 7, i % 128)));
  TEST_ASSERT_TRUE(producer.sealCaptureRange(raw));
  const auto receipt = raw.generation();
  uint16_t writing = UINT16_MAX;
  TEST_ASSERT_TRUE(PersistenceQueue::beginWriteQueuedChunk(writing));
  TEST_ASSERT_EQUAL(ChunkPersistenceState::Writing, PersistenceQueue::chunkState(writing));
  while (pressure.append(MidiEvent::ControlChange(500, 1, 9, 1))) {}
  TEST_ASSERT_EQUAL(0, LoopEventStore::freeChunkCount());
  const auto used = LoopEventStore::usedChunkCount();
  const auto capacity = output.chunkIds().capacity();
  // Equivalent to the head's ordered-row writer: every output row uses a
  // separately reserved physical chunk; a writing raw chunk stays untouched.
  for (size_t i = 0; i < 257; ++i)
    TEST_ASSERT_TRUE(output.appendReserved(MidiEvent::ControlChange(i, 1, 7, (256 - i) % 128), credits));
  TEST_ASSERT_EQUAL(used, LoopEventStore::usedChunkCount());
  TEST_ASSERT_EQUAL(capacity, output.chunkIds().capacity());
  TEST_ASSERT_EQUAL(0, credits.availableChunks());
  TEST_ASSERT_EQUAL(257, output.size());
  for (size_t i = 0; i < 257; ++i) {
    MidiEvent event; bool end = true;
    TEST_ASSERT_TRUE(raw.read(receipt, i / 256, i % 256, event, end));
    TEST_ASSERT_FALSE(end);
    TEST_ASSERT_EQUAL(256 - i, event.tick);
    TEST_ASSERT_EQUAL(i, output.at(i).tick);
  }
  MidiEvent writingEvent; bool atEnd = true;
  TEST_ASSERT_TRUE(LoopEventStore::readSealedChunkEvent(writing, 0, writingEvent, atEnd));
  TEST_ASSERT_FALSE(atEnd);
  TEST_ASSERT_EQUAL(256, writingEvent.tick);
  TEST_ASSERT_EQUAL(ChunkPersistenceState::Writing, PersistenceQueue::chunkState(writing));
}

void test_failed_reservation_growth_rolls_back_only_new_credits() {
  LoopEventStore output;
  TEST_ASSERT_TRUE(output.prewarmCaptureDirectory());
  LoopEventStore::ChunkReservation retained, pressure;
  TEST_ASSERT_TRUE(retained.reserveChunks(1));
  TEST_ASSERT_TRUE(pressure.reserveChunks(510));
  TEST_ASSERT_EQUAL(1, LoopEventStore::freeChunkCount());
  TEST_ASSERT_FALSE(retained.reserveChunks(3));
  TEST_ASSERT_EQUAL(1, retained.availableChunks());
  TEST_ASSERT_EQUAL(1, LoopEventStore::freeChunkCount());
  TEST_ASSERT_TRUE(output.appendReserved(MidiEvent::NoteOff(100, 1, 60, 0), retained));
  TEST_ASSERT_EQUAL(0, retained.availableChunks());
  TEST_ASSERT_EQUAL(1, output.size());
}

void test_reserved_append_denies_before_mutation_without_physical_credit() {
  LoopEventStore output;
  LoopEventStore::ChunkReservation credits;
  TEST_ASSERT_TRUE(output.prewarmCaptureDirectory());
  TEST_ASSERT_TRUE(credits.reserveChunks(1));
  for (size_t i = 0; i < 256; ++i)
    TEST_ASSERT_TRUE(output.appendReserved(MidiEvent::ControlChange(i, 1, 7, 1), credits));
  const auto free = LoopEventStore::freeChunkCount();
  TEST_ASSERT_FALSE(output.appendReserved(MidiEvent::NoteOn(257, 1, 60, 100), credits));
  TEST_ASSERT_EQUAL(256, output.size());
  TEST_ASSERT_EQUAL(free, LoopEventStore::freeChunkCount());
  TEST_ASSERT_EQUAL(255, output.at(255).tick);
}

void test_stale_reservation_cannot_write_or_release_reused_chunks() {
  LoopEventStore oldOutput;
  LoopEventStore::ChunkReservation stale;
  TEST_ASSERT_TRUE(oldOutput.prewarmCaptureDirectory());
  TEST_ASSERT_TRUE(stale.reserveChunks(2));
  LoopEventStore::resetPoolForTests();
  LoopEventStore replacement;
  TEST_ASSERT_TRUE(replacement.append(MidiEvent::NoteOn(10, 1, 64, 100)));
  const auto used = LoopEventStore::usedChunkCount();
  TEST_ASSERT_FALSE(oldOutput.appendReserved(MidiEvent::NoteOff(20, 1, 60, 0), stale));
  TEST_ASSERT_FALSE(stale.reserveChunks(3));
  TEST_ASSERT_FALSE(stale.releaseGrain(1));
  TEST_ASSERT_EQUAL(used, LoopEventStore::usedChunkCount());
  TEST_ASSERT_TRUE(stale.releaseGrain(1));
  TEST_ASSERT_EQUAL(used, LoopEventStore::usedChunkCount());
  TEST_ASSERT_EQUAL(10, replacement.at(0).tick);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_reserved_canonical_output_preserves_sealed_raw_under_full_pool_pressure);
  RUN_TEST(test_failed_reservation_growth_rolls_back_only_new_credits);
  RUN_TEST(test_reserved_append_denies_before_mutation_without_physical_credit);
  RUN_TEST(test_stale_reservation_cannot_write_or_release_reused_chunks);
  return UNITY_END();
}
