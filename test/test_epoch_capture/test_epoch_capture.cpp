//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include <unity.h>

#include "../../src/LoopEventStore.cpp"
#include "Epoch.h"
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

void test_epoch_kind_maps_capture_phase() {
  TEST_ASSERT_EQUAL(static_cast<uint8_t>(EpochKind::Record),
                    static_cast<uint8_t>(epochKindForCapturePhase(CapturePhase::Record)));
  TEST_ASSERT_EQUAL(static_cast<uint8_t>(EpochKind::Overdub),
                    static_cast<uint8_t>(epochKindForCapturePhase(CapturePhase::Overdub)));
}

int main(int /*argc*/, char** /*argv*/) {
  UNITY_BEGIN();
  RUN_TEST(test_detach_chunks_moves_ownership);
  RUN_TEST(test_adopt_chunk_ids_releases_pending_refs);
  RUN_TEST(test_epoch_kind_maps_capture_phase);
  return UNITY_END();
}
