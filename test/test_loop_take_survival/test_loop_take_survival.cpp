//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include <unity.h>

#include "../../src/Logger.cpp"
#include "../../src/Utils/NoteUtils.cpp"
#include "../../src/LoopEventStore.cpp"
#include "../../src/EditApply.cpp"
#include "../../src/LoopPasses.cpp"
#include "../test_support/MemoryMonitorNativeDeps.cpp"
#include "../../src/Utils/LoopEventValidation.cpp"
#include "../../src/Loop.cpp"
#include "../test_support/LoopCaptureTestDeps.cpp"

#include "Loop.h"
#include "../test_support/CommittedChunkIdTestHelpers.h"
#include "../test_support/NoteIdTestFixtures.h"
#include "EditPass.h"
#include "MidiEvent.h"
#include "PassReclaim.h"
#include "StorageLoopIo.h"

namespace {

using namespace NoteIdTestFixtures;

constexpr uint32_t kLoopLen = Config::TICKS_PER_BAR * 8;

RecordPass makeRecordPassWithNote(PassId id, uint32_t tick, uint8_t channel = 1) {
  LoopEventStore store;
  TEST_ASSERT_TRUE(storeAppendNoteOn(store, tick, channel, 60, 100, 1));
  TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOff(tick + 10, channel, 60, 0)));
  CommittedChunkIdList publishedIds;
  TEST_ASSERT_TRUE(transferCaptureStoreToCommittedChunkIds(store, publishedIds));
  RecordPass pass{};
  pass.id = id;
  pass.state = CapturePassState::Active;
  pass.committedChunkIds = std::move(publishedIds);
  return pass;
}

EditPass makePitchRow(NoteId targetNoteId, uint32_t start, uint32_t end, uint8_t pitch) {
  EditPass row{};
  row.passType = EditPassType::Note;
  row.actionType = EditActionType::Update;
  row.propertyType = EditPropertyType::Pitch;
  row.targetNoteId = targetNoteId;
  row.startTick = start;
  row.endTick = end;
  row.pitch = pitch;
  return row;
}

template <typename EventVec>
int countNoteOns(const EventVec& flat, uint8_t pitch) {
  int count = 0;
  for (const MidiEvent& evt : flat) {
    if (evt.isNoteOn() && evt.data.noteData.note == pitch) {
      ++count;
    }
  }
  return count;
}

void seedCommittedPair(Loop& loop) {
  LoopEventStore store;
  TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOn(10, 1, 60, 100)));
  TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOff(58, 1, 60, 0)));
  loop.seedRecordPassFromStore(store);
  loop.loopLengthTicks = kLoopLen;
}

void simulateOverdubSealAndPublish(Loop& loop) {
  loop.beginCapture(CapturePhase::Overdub);
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(200, 1, 64, 90)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(248, 1, 64, 0)));
  TEST_ASSERT_EQUAL(SealOutcome::Ok, loop.sealCapture(0));
  TEST_ASSERT_TRUE(loop.commitPendingCapturePass());
  loop.rebuildVisualCacheFromPasses();
  loop.invalidateCaches();
}

size_t snapshotEventCount(const LoopSnapshotRef& snapshot) {
  TEST_ASSERT_NOT_NULL(snapshot.get());
  MidiEventVec flat;
  snapshot->passes.materializeToEventVector(flat, snapshot->loopLengthTicks);
  return flat.size();
}

}  // namespace

void test_imported_takes_survive_invalidateCaches() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  seedCommittedPair(loop);
  TEST_ASSERT_EQUAL(2u, loop.nativeTestLiveEventCount());

  for (int i = 0; i < 5; ++i) {
    loop.invalidateCaches();
  }
  TEST_ASSERT_TRUE(loop.hasCommittedPasses());
  TEST_ASSERT_EQUAL(2u, loop.nativeTestLiveEventCount());
  TEST_ASSERT_EQUAL(kLoopLen, loop.loopLengthTicks);
}

void test_discard_materialization_preserves_takes() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  seedCommittedPair(loop);
  loop.discardPassesMaterializedCache();
  const size_t before = loop.nativeTestLiveEventCount();
  (void)loop.midiEvents();
  loop.discardPassesMaterializedCache();

  TEST_ASSERT_TRUE(loop.hasCommittedPasses());
  TEST_ASSERT_EQUAL(before, loop.nativeTestLiveEventCount());
  TEST_ASSERT_EQUAL(kLoopLen, loop.loopLengthTicks);
}

void test_restore_empty_pass_snapshot_clears_takes() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  seedCommittedPair(loop);

  PersistedLoopSnapshot empty{};
  loop.restorePassesSnapshot(empty);

  TEST_ASSERT_FALSE(loop.hasCommittedPasses());
  TEST_ASSERT_EQUAL(0u, loop.nativeTestLiveEventCount());
}

void test_readonly_flat_access_preserves_takes() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  seedCommittedPair(loop);
  simulateOverdubSealAndPublish(loop);
  TEST_ASSERT_EQUAL(4u, loop.nativeTestLiveEventCount());

  loop.discardPassesMaterializedCache();
  TEST_ASSERT_EQUAL(4u, loop.midiEvents().size());
  loop.invalidateCaches();

  TEST_ASSERT_EQUAL(4u, loop.nativeTestLiveEventCount());
  TEST_ASSERT_TRUE(loop.hasCommittedPasses());
}

void test_post_overdub_stop_path_preserves_takes() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  seedCommittedPair(loop);
  simulateOverdubSealAndPublish(loop);
  TEST_ASSERT_EQUAL(4u, loop.nativeTestLiveEventCount());

  TEST_ASSERT_TRUE(loop.hasCommittedPasses());
  TEST_ASSERT_EQUAL(4u, loop.nativeTestLiveEventCount());
  TEST_ASSERT_EQUAL(kLoopLen, loop.loopLengthTicks);
  TEST_ASSERT_FALSE(loop.visualCache.notes.empty());
  TEST_ASSERT_TRUE(loop.passes.hasRecordPass());
  TEST_ASSERT_EQUAL(1u, loop.passes.overdubPasses.size());
  TEST_ASSERT_EQUAL(1u, loop.passes.recordPass.id);
  TEST_ASSERT_EQUAL(2u, loop.passes.overdubPasses[0].id);
}

void test_commit_stop_finalize_empty_merged_preserves_takes() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  seedCommittedPair(loop);

  LoopEventStore emptyMerged;
  loop.commitStopFinalizeFromStore(emptyMerged);

  TEST_ASSERT_TRUE(loop.hasCommittedPasses());
  TEST_ASSERT_EQUAL(2u, loop.nativeTestLiveEventCount());
}

void test_multi_take_flatten_matches_live_event_count() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  seedCommittedPair(loop);

  LoopEventStore odStore;
  TEST_ASSERT_TRUE(odStore.append(MidiEvent::NoteOn(200, 1, 64, 90)));
  TEST_ASSERT_TRUE(odStore.append(MidiEvent::NoteOff(248, 1, 64, 0)));
  CommittedChunkIdList publishedIds;
  TEST_ASSERT_TRUE(transferCaptureStoreToCommittedChunkIds(odStore, publishedIds));
  OverdubPass overdub{};
  overdub.id = 2;
  overdub.mergeSequence = 1;
  overdub.state = CapturePassState::Active;
  overdub.committedChunkIds = std::move(publishedIds);
  loop.passes.overdubPasses.push_back(std::move(overdub));

  TEST_ASSERT_EQUAL(4u, loop.nativeTestLiveEventCount());
  MidiEventVec flat;
  loop.mergeActiveCapturePasses(flat);
  TEST_ASSERT_EQUAL(4u, flat.size());
}

void test_pass_snapshot_ignores_derived_flat_mutation() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  seedCommittedPair(loop);
  const auto before = loop.sharePassesSnapshot();
  TEST_ASSERT_EQUAL(2u, snapshotEventCount(before));

  loop.midiEvents().push_back(MidiEvent::NoteOn(48, 1, 60, 80));
  loop.midiEvents().push_back(MidiEvent::NoteOff(72, 1, 60, 0));

  const auto after = loop.sharePassesSnapshot();
  TEST_ASSERT_EQUAL(2u, snapshotEventCount(after));
  TEST_ASSERT_EQUAL(2u, loop.nativeTestLiveEventCount());
}

void test_seal_overdub_preserves_record_pass() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  seedCommittedPair(loop);
  simulateOverdubSealAndPublish(loop);

  TEST_ASSERT_TRUE(loop.setCapturePassState(2, CapturePassState::Disabled));
  TEST_ASSERT_TRUE(loop.passes.hasRecordPass());
  TEST_ASSERT_EQUAL(2u, loop.nativeTestLiveEventCount());
}

void test_invalidateCaches_marks_materialize_stale_after_loop_length_change() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  loop.loopLengthTicks = 768;
  loop.passes.recordPass = makeRecordPassWithNote(1, 10);
  loop.nextPassId_ = 2;
  (void)loop.saveNoteEditPass(0, makePitchRow(1, 10, 20, 67));

  (void)loop.midiEvents();
  TEST_ASSERT_TRUE(loop.isPassesMaterializedStoreFresh());

  loop.loopLengthTicks = 384;
  loop.invalidateCaches();
  TEST_ASSERT_FALSE(loop.isPassesMaterializedStoreFresh());

  MidiEventVec expected;
  loop.passes.materializeToEventVector(expected, loop.loopLengthTicks);
  SessionMidiEventVec gathered;
  loop.gatherCommittedEvents(gathered);
  TEST_ASSERT_EQUAL(expected.size(), gathered.size());
  TEST_ASSERT_EQUAL(1, countNoteOns(expected, 67));
  TEST_ASSERT_EQUAL(1, countNoteOns(gathered, 67));
}

void test_reclaim_disabled_overdub_releases_published_chunks() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  seedCommittedPair(loop);
  simulateOverdubSealAndPublish(loop);
  TEST_ASSERT_EQUAL(2u, LoopEventStore::usedChunkCount());

  TEST_ASSERT_TRUE(loop.setCapturePassState(2, CapturePassState::Disabled));
  SlotPassReferences refs{};
  loop.reclaimUnreferencedDisabledPasses(refs);

  TEST_ASSERT_EQUAL(0u, loop.passes.overdubPasses.size());
  TEST_ASSERT_EQUAL(1u, LoopEventStore::usedChunkCount());
  TEST_ASSERT_EQUAL(2u, loop.nativeTestLiveEventCount());
}

int main(int /*argc*/, char** /*argv*/) {
  UNITY_BEGIN();
  RUN_TEST(test_imported_takes_survive_invalidateCaches);
  RUN_TEST(test_discard_materialization_preserves_takes);
  RUN_TEST(test_restore_empty_pass_snapshot_clears_takes);
  RUN_TEST(test_readonly_flat_access_preserves_takes);
  RUN_TEST(test_post_overdub_stop_path_preserves_takes);
  RUN_TEST(test_seal_overdub_preserves_record_pass);
  RUN_TEST(test_commit_stop_finalize_empty_merged_preserves_takes);
  RUN_TEST(test_multi_take_flatten_matches_live_event_count);
  RUN_TEST(test_pass_snapshot_ignores_derived_flat_mutation);
  RUN_TEST(test_invalidateCaches_marks_materialize_stale_after_loop_length_change);
  RUN_TEST(test_reclaim_disabled_overdub_releases_published_chunks);
  return UNITY_END();
}
