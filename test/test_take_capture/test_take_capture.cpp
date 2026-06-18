//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include <unity.h>

#include "../../src/LoopEventStore.cpp"
#include "Take.h"
#include "LoopEventStore.h"
#include "MidiEvent.h"

void test_detach_chunks_moves_ownership() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  LoopEventStore capture;
  ChunkIdList detached;

  TEST_ASSERT_TRUE(capture.append(MidiEvent::NoteOn(10, 1, 60, 100)));
  capture.detachChunksTo(detached);

  TEST_ASSERT_TRUE(capture.empty());
  TEST_ASSERT_EQUAL(1u, detached.size());

  LoopEventStore reclaimed;
  reclaimed.adoptChunkIds(detached);
  TEST_ASSERT_TRUE(detached.empty());
  TEST_ASSERT_EQUAL(1u, reclaimed.size());
  TEST_ASSERT_EQUAL(10u, reclaimed.at(0).tick);
}

void test_adopt_chunk_ids_releases_pending_refs() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  LoopEventStore capture;
  ChunkIdList refs;

  TEST_ASSERT_TRUE(capture.append(MidiEvent::NoteOn(5, 1, 60, 100)));
  capture.detachChunksTo(refs);
  TEST_ASSERT_EQUAL(1u, refs.size());

  LoopEventStore staging;
  staging.adoptChunkIds(refs);
  staging.clear();
  TEST_ASSERT_TRUE(refs.empty());
}

void test_take_kind_maps_capture_phase() {
  TEST_ASSERT_EQUAL(static_cast<uint8_t>(TakeType::Record),
                    static_cast<uint8_t>(takeTypeForCapturePhase(CapturePhase::Record)));
  TEST_ASSERT_EQUAL(static_cast<uint8_t>(TakeType::Overdub),
                    static_cast<uint8_t>(takeTypeForCapturePhase(CapturePhase::Overdub)));
}

void test_append_flattened_chunk_ids_preserves_take_refs() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  LoopEventStore capture;
  ChunkIdList refs;

  TEST_ASSERT_TRUE(capture.append(MidiEvent::NoteOn(10, 1, 60, 100)));
  TEST_ASSERT_TRUE(capture.append(MidiEvent::NoteOn(20, 1, 64, 100)));
  capture.detachChunksTo(refs);

  MidiEventVec flat;
  LoopEventStore::appendFlattenedChunkIds(refs, flat);
  TEST_ASSERT_EQUAL(2u, flat.size());
  TEST_ASSERT_EQUAL(10u, flat[0].tick);
  TEST_ASSERT_EQUAL(20u, flat[1].tick);
  TEST_ASSERT_EQUAL(1u, refs.size());
}

void test_capture_store_spans_multiple_chunks() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  LoopEventStore capture;

  for (uint16_t i = 0; i < 300; ++i) {
    const uint32_t tick = static_cast<uint32_t>(i) * 48u;
    const bool ok = capture.append(
        MidiEvent::NoteOn(tick, 5, static_cast<uint8_t>(24 + (i % 16)), 98));
    if (!ok) {
      TEST_FAIL_MESSAGE("capture append failed before 300 events");
    }
  }
  TEST_ASSERT_EQUAL(300u, capture.size());
  TEST_ASSERT_EQUAL(48u * 299u, capture.at(299).tick);
}

int main(int /*argc*/, char** /*argv*/) {
  UNITY_BEGIN();
  RUN_TEST(test_detach_chunks_moves_ownership);
  RUN_TEST(test_adopt_chunk_ids_releases_pending_refs);
  RUN_TEST(test_take_kind_maps_capture_phase);
  RUN_TEST(test_append_flattened_chunk_ids_preserves_take_refs);
  RUN_TEST(test_capture_store_spans_multiple_chunks);
  return UNITY_END();
}
