//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include <unity.h>

#include "../../src/Logger.cpp"
#include "../../src/Utils/NoteUtils.cpp"
#include "../../src/LoopEventStore.cpp"
#include "../../src/EditApply.cpp"
#include "../../src/LoopPasses.cpp"
#include "../../src/Loop.cpp"

#include "Loop.h"
#include "LoopEventStore.h"
#include "MidiEvent.h"
#include "StorageLoopIo.h"

namespace {

constexpr uint32_t kLoopLen = Config::TICKS_PER_BAR * 8;

void seedPublishedPair(Loop& loop) {
  LoopEventStore store;
  TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOn(10, 1, 60, 100)));
  TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOff(58, 1, 60, 0)));
  loop.seedRecordPassFromStore(store);
  loop.loopLengthTicks = kLoopLen;
}

void appendOverdubPass(Loop& loop) {
  LoopEventStore odStore;
  TEST_ASSERT_TRUE(odStore.append(MidiEvent::NoteOn(200, 1, 64, 90)));
  TEST_ASSERT_TRUE(odStore.append(MidiEvent::NoteOff(248, 1, 64, 0)));
  ChunkIdList refs;
  odStore.detachChunksTo(refs);
  TEST_ASSERT_FALSE(refs.empty());

  OverdubPass overdub{};
  overdub.id = 2;
  overdub.mergeSequence = 1;
  overdub.state = CapturePassState::Active;
  overdub.chunkRefs = std::move(refs);
  loop.passes.overdubPasses.push_back(std::move(overdub));
}

void simulatePostOverdubStopPath(Loop& loop) {
  loop.discardEditFlatMaterialization();
  loop.invalidateCaches();
  loop.invalidateCaches();

  MidiEventVec flat;
  loop.flattenActiveCapturePasses(flat);
  LoopEventStore merged;
  merged.loadFromFlat(flat);
  loop.commitStopFinalizeFromStore(merged);
  loop.rebuildVisualCacheFromPasses();
  loop.invalidateCaches();
}

size_t snapshotEventCount(const LoopSnapshotRef& snapshot) {
  TEST_ASSERT_NOT_NULL(snapshot.get());
  MidiEventVec flat;
  snapshot->passes.materializeToFlat(flat, snapshot->loopLengthTicks);
  return flat.size();
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

void test_discard_materialization_preserves_takes() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  seedPublishedPair(loop);
  loop.discardEditFlatMaterialization();
  const size_t before = loop.nativeTestLiveEventCount();
  (void)loop.midiEvents();
  loop.discardEditFlatMaterialization();

  TEST_ASSERT_TRUE(loop.hasPublishedEvents());
  TEST_ASSERT_EQUAL(before, loop.nativeTestLiveEventCount());
  TEST_ASSERT_EQUAL(kLoopLen, loop.loopLengthTicks);
}

void test_restore_empty_pass_snapshot_clears_takes() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  seedPublishedPair(loop);

  PersistedLoopSnapshot empty{};
  loop.restorePassesSnapshot(empty);

  TEST_ASSERT_FALSE(loop.hasPublishedEvents());
  TEST_ASSERT_EQUAL(0u, loop.nativeTestLiveEventCount());
}

void test_readonly_flat_access_preserves_takes() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  seedPublishedPair(loop);
  appendOverdubPass(loop);
  TEST_ASSERT_EQUAL(4u, loop.nativeTestLiveEventCount());

  loop.discardEditFlatMaterialization();
  TEST_ASSERT_EQUAL(4u, loop.midiEvents().size());
  loop.invalidateCaches();

  TEST_ASSERT_EQUAL(4u, loop.nativeTestLiveEventCount());
  TEST_ASSERT_TRUE(loop.hasPublishedEvents());
}

void test_post_overdub_stop_path_preserves_takes() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  seedPublishedPair(loop);
  appendOverdubPass(loop);
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
  OverdubPass overdub{};
  overdub.id = 2;
  overdub.mergeSequence = 1;
  overdub.state = CapturePassState::Active;
  overdub.chunkRefs = std::move(refs);
  loop.passes.overdubPasses.push_back(std::move(overdub));

  TEST_ASSERT_EQUAL(4u, loop.nativeTestLiveEventCount());
  MidiEventVec flat;
  loop.flattenActiveCapturePasses(flat);
  TEST_ASSERT_EQUAL(4u, flat.size());
}

void test_pass_snapshot_ignores_derived_flat_mutation() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  seedPublishedPair(loop);
  const auto before = loop.sharePassesSnapshot();
  TEST_ASSERT_EQUAL(2u, snapshotEventCount(before));

  loop.midiEvents().push_back(MidiEvent::NoteOn(48, 1, 60, 80));
  loop.midiEvents().push_back(MidiEvent::NoteOff(72, 1, 60, 0));

  const auto after = loop.sharePassesSnapshot();
  TEST_ASSERT_EQUAL(2u, snapshotEventCount(after));
  TEST_ASSERT_EQUAL(2u, loop.nativeTestLiveEventCount());
}

int main(int /*argc*/, char** /*argv*/) {
  UNITY_BEGIN();
  RUN_TEST(test_imported_takes_survive_invalidateCaches);
  RUN_TEST(test_discard_materialization_preserves_takes);
  RUN_TEST(test_restore_empty_pass_snapshot_clears_takes);
  RUN_TEST(test_readonly_flat_access_preserves_takes);
  RUN_TEST(test_post_overdub_stop_path_preserves_takes);
  RUN_TEST(test_commit_stop_finalize_empty_merged_preserves_takes);
  RUN_TEST(test_multi_take_flatten_matches_live_event_count);
  RUN_TEST(test_pass_snapshot_ignores_derived_flat_mutation);
  return UNITY_END();
}
