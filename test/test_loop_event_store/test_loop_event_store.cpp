//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include <unity.h>

#include "../../src/LoopEventStore.cpp"
#include "LoopEventStore.h"
#include "LoopEventBuffer.h"
#include "MidiEvent.h"

void test_append_fills_chunks_without_realloc_pattern() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  LoopEventStore store;

  for (uint16_t i = 0; i < 300; ++i) {
    TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOn(i, 1, 60, 100)));
  }
  TEST_ASSERT_EQUAL(300u, store.size());
  TEST_ASSERT_EQUAL(2u, store.chunkIds().size());
  TEST_ASSERT_EQUAL(256u, store.at(256).tick);
}

void test_adopt_all_moves_chunks() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  LoopEventStore capture;
  LoopEventStore committed;

  TEST_ASSERT_TRUE(capture.append(MidiEvent::NoteOn(10, 1, 60, 100)));
  committed.adoptAll(capture);
  TEST_ASSERT_TRUE(capture.empty());
  TEST_ASSERT_EQUAL(1u, committed.size());
  TEST_ASSERT_EQUAL(10u, committed.at(0).tick);
}

void test_merge_from_combines_sorted_events() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  LoopEventStore base;
  LoopEventStore overdub;

  TEST_ASSERT_TRUE(base.append(MidiEvent::NoteOn(10, 1, 60, 100)));
  TEST_ASSERT_TRUE(overdub.append(MidiEvent::NoteOn(20, 1, 62, 90)));
  base.mergeFrom(overdub);

  TEST_ASSERT_EQUAL(2u, base.size());
  TEST_ASSERT_EQUAL(10u, base.at(0).tick);
  TEST_ASSERT_EQUAL(20u, base.at(1).tick);
}

void test_restore_snapshot_deep_copies() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  CowLoopEventStore live;
  TEST_ASSERT_TRUE(live.mutStore().append(MidiEvent::NoteOn(1, 1, 60, 100)));
  const auto snap = live.shareForSnapshot();
  TEST_ASSERT_TRUE(live.mutStore().append(MidiEvent::NoteOn(2, 1, 62, 90)));
  TEST_ASSERT_EQUAL(2u, live.size());

  live.restoreFromSnapshot(snap);
  TEST_ASSERT_EQUAL(1u, live.size());
  TEST_ASSERT_EQUAL(60, live.at(0).data.noteData.note);

  TEST_ASSERT_TRUE(live.mutStore().append(MidiEvent::NoteOn(99, 1, 99, 99)));
  TEST_ASSERT_EQUAL(1u, snap->size());
  TEST_ASSERT_EQUAL(60, snap->at(0).data.noteData.note);
}

int main(int /*argc*/, char** /*argv*/) {
  UNITY_BEGIN();
  RUN_TEST(test_append_fills_chunks_without_realloc_pattern);
  RUN_TEST(test_adopt_all_moves_chunks);
  RUN_TEST(test_merge_from_combines_sorted_events);
  RUN_TEST(test_restore_snapshot_deep_copies);
  return UNITY_END();
}
