//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include <unity.h>

#include "../../src/Logger.cpp"
#include "../../src/Utils/NoteUtils.cpp"
#include "../../src/LoopEventStore.cpp"
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

void appendOverdubEpoch(Loop& loop) {
  LoopEventStore odStore;
  TEST_ASSERT_TRUE(odStore.append(MidiEvent::NoteOn(200, 1, 64, 90)));
  TEST_ASSERT_TRUE(odStore.append(MidiEvent::NoteOff(248, 1, 64, 0)));
  ChunkIdList refs;
  odStore.detachChunksTo(refs);
  TEST_ASSERT_FALSE(refs.empty());

  Epoch od{};
  od.id = 2;
  od.mergeSequence = 1;
  od.state = EpochState::Active;
  od.kind = EpochKind::Overdub;
  od.chunkRefs = std::move(refs);
  loop.epochs.push_back(od);
}

void simulatePostOverdubStopPath(Loop& loop) {
  loop.discardEditFlatMaterialization();
  loop.invalidateCaches();
  loop.invalidateCaches();

  MidiEventVec flat;
  loop.flattenActiveEpochs(flat);
  LoopEventStore merged;
  merged.loadFromFlat(flat);
  loop.commitStopFinalizeFromStore(merged);
  loop.rebuildVisualCacheFromEpochs();
  loop.invalidateCaches();
}

}  // namespace

void test_imported_epochs_survive_invalidateCaches() {
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

void test_accidental_empty_sync_preserves_epochs() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  seedPublishedPair(loop);
  loop.discardEditFlatMaterialization();

  loop.nativeTestSyncEditFlatToEpochs(false);

  TEST_ASSERT_TRUE(loop.hasPublishedEvents());
  TEST_ASSERT_EQUAL(2u, loop.nativeTestLiveEventCount());
  TEST_ASSERT_EQUAL(kLoopLen, loop.loopLengthTicks);
}

void test_intentional_empty_restore_clears_epochs() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  seedPublishedPair(loop);

  const MidiSnapshotRef empty = std::make_shared<LoopEventStore>();
  loop.restoreEditSnapshot(empty);

  TEST_ASSERT_FALSE(loop.hasPublishedEvents());
  TEST_ASSERT_EQUAL(0u, loop.nativeTestLiveEventCount());
}

void test_readonly_flat_access_preserves_epochs() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  seedPublishedPair(loop);
  appendOverdubEpoch(loop);
  TEST_ASSERT_EQUAL(4u, loop.nativeTestLiveEventCount());

  loop.discardEditFlatMaterialization();
  TEST_ASSERT_EQUAL(4u, loop.midiEvents().size());
  loop.invalidateCaches();
  loop.flushEditStoreToEpochs();

  TEST_ASSERT_EQUAL(4u, loop.nativeTestLiveEventCount());
  TEST_ASSERT_TRUE(loop.hasPublishedEvents());
}

void test_post_overdub_stop_path_preserves_epochs() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  seedPublishedPair(loop);
  appendOverdubEpoch(loop);
  TEST_ASSERT_EQUAL(4u, loop.nativeTestLiveEventCount());

  simulatePostOverdubStopPath(loop);

  TEST_ASSERT_TRUE(loop.hasPublishedEvents());
  TEST_ASSERT_EQUAL(4u, loop.nativeTestLiveEventCount());
  TEST_ASSERT_EQUAL(kLoopLen, loop.loopLengthTicks);
  TEST_ASSERT_FALSE(loop.visualCache.notes.empty());
}

void test_commit_stop_finalize_empty_merged_preserves_epochs() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  seedPublishedPair(loop);

  LoopEventStore emptyMerged;
  loop.commitStopFinalizeFromStore(emptyMerged);

  TEST_ASSERT_TRUE(loop.hasPublishedEvents());
  TEST_ASSERT_EQUAL(2u, loop.nativeTestLiveEventCount());
}

void test_flush_without_dirty_does_not_wipe_epochs() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  seedPublishedPair(loop);
  loop.discardEditFlatMaterialization();

  loop.flushEditStoreToEpochs();
  loop.nativeTestSyncEditFlatToEpochs(false);

  TEST_ASSERT_EQUAL(2u, loop.nativeTestLiveEventCount());
}

void test_multi_epoch_flatten_matches_live_event_count() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  seedPublishedPair(loop);

  LoopEventStore odStore;
  TEST_ASSERT_TRUE(odStore.append(MidiEvent::NoteOn(200, 1, 64, 90)));
  TEST_ASSERT_TRUE(odStore.append(MidiEvent::NoteOff(248, 1, 64, 0)));
  ChunkIdList refs;
  odStore.detachChunksTo(refs);
  Epoch od{};
  od.id = 2;
  od.mergeSequence = 1;
  od.state = EpochState::Active;
  od.kind = EpochKind::Overdub;
  od.chunkRefs = std::move(refs);
  loop.epochs.push_back(od);

  TEST_ASSERT_EQUAL(4u, loop.nativeTestLiveEventCount());
  MidiEventVec flat;
  loop.flattenActiveEpochs(flat);
  TEST_ASSERT_EQUAL(4u, flat.size());
}

int main(int /*argc*/, char** /*argv*/) {
  UNITY_BEGIN();
  RUN_TEST(test_imported_epochs_survive_invalidateCaches);
  RUN_TEST(test_accidental_empty_sync_preserves_epochs);
  RUN_TEST(test_intentional_empty_restore_clears_epochs);
  RUN_TEST(test_readonly_flat_access_preserves_epochs);
  RUN_TEST(test_post_overdub_stop_path_preserves_epochs);
  RUN_TEST(test_commit_stop_finalize_empty_merged_preserves_epochs);
  RUN_TEST(test_flush_without_dirty_does_not_wipe_epochs);
  RUN_TEST(test_multi_epoch_flatten_matches_live_event_count);
  return UNITY_END();
}
