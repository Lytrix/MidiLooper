//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include <unity.h>

#include "../../src/Logger.cpp"
#include "../../src/Utils/NoteUtils.cpp"
#include "../../src/LoopEventStore.cpp"
#include "../../src/EditApply.cpp"
#include "../../src/Loop.cpp"

#include "Loop.h"
#include "LoopEventStore.h"
#include "MidiEvent.h"

namespace {

constexpr uint32_t kLoopLen = Config::TICKS_PER_BAR * 8;

void seedPublishedPair(Loop& loop) {
  LoopEventStore store;
  TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOn(10, 1, 60, 100)));
  TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOff(58, 1, 60, 0)));
  loop.importPublishedStore(store);
  loop.loopLengthTicks = kLoopLen;
}

void appendOverdubTake(Loop& loop) {
  LoopEventStore odStore;
  TEST_ASSERT_TRUE(odStore.append(MidiEvent::NoteOn(200, 1, 64, 90)));
  TEST_ASSERT_TRUE(odStore.append(MidiEvent::NoteOff(248, 1, 64, 0)));
  ChunkIdList refs;
  odStore.detachChunksTo(refs);
  TEST_ASSERT_FALSE(refs.empty());

  Take od{};
  od.id = 2;
  od.mergeSequence = 1;
  od.state = TakeState::Active;
  od.type = TakeType::Overdub;
  od.chunkRefs = std::move(refs);
  loop.takes.push_back(od);
}

void simulatePostOverdubStopPath(Loop& loop) {
  loop.discardEditFlatMaterialization();
  loop.invalidateCaches();
  loop.invalidateCaches();

  MidiEventVec flat;
  loop.flattenActiveTakes(flat);
  LoopEventStore merged;
  merged.loadFromFlat(flat);
  loop.commitStopFinalizeFromStore(merged);
  loop.rebuildVisualCacheFromTakes();
  loop.invalidateCaches();
}

}  // namespace

void test_imported_takes_survive_invalidateCaches() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  seedPublishedPair(loop);
  TEST_ASSERT_EQUAL(2u, loop.nativeTestLiveEventCount());

  for (int i = 0; i < 5; ++i) {
    loop.invalidateCaches();
  }
  TEST_ASSERT_TRUE(loop.hasPublishedEvents());
  TEST_ASSERT_EQUAL(2u, loop.nativeTestLiveEventCount());
  TEST_ASSERT_EQUAL(kLoopLen, loop.loopLengthTicks);
}

void test_accidental_empty_sync_preserves_takes() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  seedPublishedPair(loop);
  loop.discardEditFlatMaterialization();

  loop.nativeTestCommitMaterializedStoreToTakes(false);

  TEST_ASSERT_TRUE(loop.hasPublishedEvents());
  TEST_ASSERT_EQUAL(2u, loop.nativeTestLiveEventCount());
  TEST_ASSERT_EQUAL(kLoopLen, loop.loopLengthTicks);
}

void test_intentional_empty_restore_clears_takes() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  seedPublishedPair(loop);

  const MidiSnapshotRef empty = std::make_shared<LoopEventStore>();
  loop.restoreEditSnapshot(empty);

  TEST_ASSERT_FALSE(loop.hasPublishedEvents());
  TEST_ASSERT_EQUAL(0u, loop.nativeTestLiveEventCount());
}

void test_readonly_flat_access_preserves_takes() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  seedPublishedPair(loop);
  appendOverdubTake(loop);
  TEST_ASSERT_EQUAL(4u, loop.nativeTestLiveEventCount());

  loop.discardEditFlatMaterialization();
  TEST_ASSERT_EQUAL(4u, loop.midiEvents().size());
  loop.invalidateCaches();
  loop.nativeTestCommitMaterializedStoreToTakes(false);

  TEST_ASSERT_EQUAL(4u, loop.nativeTestLiveEventCount());
  TEST_ASSERT_TRUE(loop.hasPublishedEvents());
}

void test_post_overdub_stop_path_preserves_takes() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  seedPublishedPair(loop);
  appendOverdubTake(loop);
  TEST_ASSERT_EQUAL(4u, loop.nativeTestLiveEventCount());

  simulatePostOverdubStopPath(loop);

  TEST_ASSERT_TRUE(loop.hasPublishedEvents());
  TEST_ASSERT_EQUAL(4u, loop.nativeTestLiveEventCount());
  TEST_ASSERT_EQUAL(kLoopLen, loop.loopLengthTicks);
  TEST_ASSERT_FALSE(loop.visualCache.notes.empty());
}

void test_commit_stop_finalize_empty_merged_preserves_takes() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  seedPublishedPair(loop);

  LoopEventStore emptyMerged;
  loop.commitStopFinalizeFromStore(emptyMerged);

  TEST_ASSERT_TRUE(loop.hasPublishedEvents());
  TEST_ASSERT_EQUAL(2u, loop.nativeTestLiveEventCount());
}

void test_flush_without_dirty_does_not_wipe_takes() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  seedPublishedPair(loop);
  loop.discardEditFlatMaterialization();

  loop.commitMaterializedStoreToTakes();
  loop.nativeTestCommitMaterializedStoreToTakes(false);

  TEST_ASSERT_EQUAL(2u, loop.nativeTestLiveEventCount());
}

void test_multi_take_flatten_matches_live_event_count() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  seedPublishedPair(loop);

  LoopEventStore odStore;
  TEST_ASSERT_TRUE(odStore.append(MidiEvent::NoteOn(200, 1, 64, 90)));
  TEST_ASSERT_TRUE(odStore.append(MidiEvent::NoteOff(248, 1, 64, 0)));
  ChunkIdList refs;
  odStore.detachChunksTo(refs);
  Take od{};
  od.id = 2;
  od.mergeSequence = 1;
  od.state = TakeState::Active;
  od.type = TakeType::Overdub;
  od.chunkRefs = std::move(refs);
  loop.takes.push_back(od);

  TEST_ASSERT_EQUAL(4u, loop.nativeTestLiveEventCount());
  MidiEventVec flat;
  loop.flattenActiveTakes(flat);
  TEST_ASSERT_EQUAL(4u, flat.size());
}

void test_share_edit_snapshot_includes_dirty_flat() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  seedPublishedPair(loop);

  loop.midiEvents().push_back(MidiEvent::NoteOn(48, 1, 60, 80));
  loop.midiEvents().push_back(MidiEvent::NoteOff(72, 1, 60, 0));
  loop.markEditFlatDirty();

  const auto snap = loop.shareEditSnapshot();
  TEST_ASSERT_NOT_NULL(snap.get());
  MidiEventVec snapFlat;
  snap->flatten(snapFlat);
  TEST_ASSERT_EQUAL(4u, snapFlat.size());
  TEST_ASSERT_EQUAL(48u, snapFlat[2].tick);
}

int main(int /*argc*/, char** /*argv*/) {
  UNITY_BEGIN();
  RUN_TEST(test_imported_takes_survive_invalidateCaches);
  RUN_TEST(test_accidental_empty_sync_preserves_takes);
  RUN_TEST(test_intentional_empty_restore_clears_takes);
  RUN_TEST(test_readonly_flat_access_preserves_takes);
  RUN_TEST(test_post_overdub_stop_path_preserves_takes);
  RUN_TEST(test_commit_stop_finalize_empty_merged_preserves_takes);
  RUN_TEST(test_flush_without_dirty_does_not_wipe_takes);
  RUN_TEST(test_multi_take_flatten_matches_live_event_count);
  RUN_TEST(test_share_edit_snapshot_includes_dirty_flat);
  return UNITY_END();
}
