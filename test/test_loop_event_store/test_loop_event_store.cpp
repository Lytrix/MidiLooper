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

void test_merge_from_interleaves_across_chunks() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  LoopEventStore base;
  LoopEventStore overdub;

  for (uint16_t i = 0; i < 300; i += 2) {
    TEST_ASSERT_TRUE(base.append(MidiEvent::NoteOn(i, 1, 60, 100)));
  }
  for (uint16_t i = 1; i < 300; i += 2) {
    TEST_ASSERT_TRUE(overdub.append(MidiEvent::NoteOn(i, 1, 62, 90)));
  }

  base.mergeFrom(overdub);
  TEST_ASSERT_EQUAL(300u, base.size());
  for (uint16_t i = 0; i < 300; ++i) {
    TEST_ASSERT_EQUAL(i, base.at(i).tick);
  }
  TEST_ASSERT_TRUE(base.chunkIds().size() >= 2u);
}

void test_merge_from_stable_on_equal_tick() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  LoopEventStore base;
  LoopEventStore overdub;

  TEST_ASSERT_TRUE(base.append(MidiEvent::NoteOn(10, 1, 60, 100)));
  TEST_ASSERT_TRUE(overdub.append(MidiEvent::NoteOn(10, 1, 62, 90)));
  base.mergeFrom(overdub);

  TEST_ASSERT_EQUAL(2u, base.size());
  TEST_ASSERT_EQUAL(60, base.at(0).data.noteData.note);
  TEST_ASSERT_EQUAL(62, base.at(1).data.noteData.note);
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

void test_shift_all_ticks_bumps_negative() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  LoopEventStore store;

  TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOn(5, 1, 60, 100)));
  store.shiftAllTicks(-10);
  TEST_ASSERT_EQUAL(0u, store.at(0).tick);
}

void test_lower_bound_index_skips_prefix() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  LoopEventStore store;

  for (uint16_t i = 0; i < 300; ++i) {
    TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOn(i, 1, 60, 100)));
  }

  TEST_ASSERT_EQUAL(250u, store.lowerBoundIndex(250));
  TEST_ASSERT_EQUAL(300u, store.lowerBoundIndex(9999));
}

void test_undo_snapshot_ref_isolated_by_cow() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  CowLoopEventStore live;
  TEST_ASSERT_TRUE(live.mutStore().append(MidiEvent::NoteOn(1, 1, 60, 100)));
  const auto snap = live.shareForSnapshot();
  TEST_ASSERT_EQUAL(1u, snap->size());

  TEST_ASSERT_TRUE(live.mutStore().append(MidiEvent::NoteOn(2, 1, 62, 90)));
  TEST_ASSERT_EQUAL(2u, live.size());
  TEST_ASSERT_EQUAL(1u, snap->size());
}

void test_drop_events_at_or_beyond_tick_removes_overflow() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  LoopEventStore store;

  TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOn(1500, 1, 60, 100)));
  TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOff(1520, 1, 60, 0)));
  TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOn(1548, 1, 61, 100)));  // overflow
  TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOff(1590, 1, 61, 0)));   // overflow

  store.dropEventsAtOrBeyondTick(1536);
  TEST_ASSERT_EQUAL(2u, store.size());
  TEST_ASSERT_EQUAL(1500u, store.at(0).tick);
  TEST_ASSERT_EQUAL(1520u, store.at(1).tick);
}

void test_first_index_for_bar_uses_bar_index() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  LoopEventStore store;

  TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOn(0, 1, 60, 100)));      // bar 0
  TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOn(770, 1, 61, 100)));    // bar 1
  TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOn(1536, 1, 62, 100)));   // bar 2

  TEST_ASSERT_EQUAL(0u, store.firstIndexForBar(0));
  TEST_ASSERT_EQUAL(1u, store.firstIndexForBar(1));
  TEST_ASSERT_EQUAL(2u, store.firstIndexForBar(2));
  TEST_ASSERT_EQUAL(3u, store.firstIndexForBar(9));
}

void test_transfer_capture_chunk_ids_to_published() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  LoopEventStore store;
  TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOn(10, 1, 60, 100)));
  TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOn(20, 1, 64, 100)));

  CaptureChunkIdList captureIds;
  store.detachChunksTo(captureIds);
  TEST_ASSERT_EQUAL(1u, captureIds.size());
  TEST_ASSERT_TRUE(store.empty());

  PublishedChunkIdList publishedIds;
  TEST_ASSERT_TRUE(LoopEventStore::transferCaptureChunkIdsToPublished(publishedIds, captureIds));
  TEST_ASSERT_TRUE(captureIds.empty());
  TEST_ASSERT_EQUAL(1u, publishedIds.size());

  MidiEventVec flat;
  LoopEventStore::appendChunkRefEvents(publishedIds, flat);
  TEST_ASSERT_EQUAL(2u, flat.size());
  LoopEventStore::releaseChunkRefs(publishedIds);
}

void test_detach_chunks_to_published_seals_and_transfers() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  LoopEventStore store;
  TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOn(5, 1, 60, 100)));

  PublishedChunkIdList publishedIds;
  TEST_ASSERT_TRUE(store.detachChunksToPublished(publishedIds));
  TEST_ASSERT_TRUE(store.empty());
  TEST_ASSERT_EQUAL(1u, publishedIds.size());
  TEST_ASSERT_EQUAL(ChunkLifecycleState::Sealed,
                    LoopEventStore::chunkLifecycleState(publishedIds[0]));
  LoopEventStore::releaseChunkRefs(publishedIds);
}

void test_assign_missing_note_ids_in_chunks() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  LoopEventStore store;
  MidiEvent on = MidiEvent::NoteOn(10, 1, 60, 100);
  on.noteId = kInvalidNoteId;
  TEST_ASSERT_TRUE(store.append(on));
  TEST_ASSERT_EQUAL(1u, store.chunkIds().size());

  uint32_t nextId = 100;
  store.assignMissingNoteIdsToNoteOns([&nextId]() { return nextId++; });

  MidiEventVec flat;
  store.flatten(flat);
  TEST_ASSERT_EQUAL(1u, flat.size());
  TEST_ASSERT_EQUAL(100u, flat[0].noteId);
  TEST_ASSERT_EQUAL(1u, store.chunkIds().size());
}

void test_first_index_for_bar_skips_empty_bar() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  LoopEventStore store;

  TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOn(0, 1, 60, 100)));      // bar 0
  TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOn(1536, 1, 62, 100)));   // bar 2

  TEST_ASSERT_EQUAL(1u, store.firstIndexForBar(1));  // bar 1 empty -> next event is bar 2
}

int main(int /*argc*/, char** /*argv*/) {
  UNITY_BEGIN();
  RUN_TEST(test_append_fills_chunks_without_realloc_pattern);
  RUN_TEST(test_adopt_all_moves_chunks);
  RUN_TEST(test_merge_from_combines_sorted_events);
  RUN_TEST(test_merge_from_interleaves_across_chunks);
  RUN_TEST(test_merge_from_stable_on_equal_tick);
  RUN_TEST(test_restore_snapshot_deep_copies);
  RUN_TEST(test_shift_all_ticks_bumps_negative);
  RUN_TEST(test_lower_bound_index_skips_prefix);
  RUN_TEST(test_undo_snapshot_ref_isolated_by_cow);
  RUN_TEST(test_drop_events_at_or_beyond_tick_removes_overflow);
  RUN_TEST(test_first_index_for_bar_uses_bar_index);
  RUN_TEST(test_first_index_for_bar_skips_empty_bar);
  RUN_TEST(test_transfer_capture_chunk_ids_to_published);
  RUN_TEST(test_detach_chunks_to_published_seals_and_transfers);
  RUN_TEST(test_assign_missing_note_ids_in_chunks);
  return UNITY_END();
}
