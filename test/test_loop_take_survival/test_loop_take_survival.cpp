//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include <algorithm>
#include <unity.h>

#include "../../src/Logger.cpp"
#include "../../src/Utils/NoteUtils.cpp"
#include "../../src/LoopEventStore.cpp"
#include "../../src/EditManager/EditApply.cpp"
#include "../../src/Loop/LoopPasses.cpp"
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
  CommittedChunkIdList committedChunkIds;
  TEST_ASSERT_TRUE(transferCaptureStoreToCommittedChunkIds(store, committedChunkIds));
  RecordPass pass{};
  pass.id = id;
  pass.state = CapturePassState::Active;
  pass.committedChunkIds = std::move(committedChunkIds);
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
  CommittedChunkIdList committedChunkIds;
  TEST_ASSERT_TRUE(transferCaptureStoreToCommittedChunkIds(odStore, committedChunkIds));
  OverdubPass overdub{};
  overdub.id = 2;
  overdub.mergeSequence = 1;
  overdub.state = CapturePassState::Active;
  overdub.committedChunkIds = std::move(committedChunkIds);
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

void drainPendingSnapshotPublish(Loop& loop) {
  size_t steps = 0;
  while (loop.hasPendingSnapshotPublish() && steps < 10000) {
    loop.stepPendingSnapshotPublish(1);
    ++steps;
  }
  TEST_ASSERT_FALSE(loop.hasPendingSnapshotPublish());
}

void recordLinearNote(Loop& loop, uint32_t onTick, uint32_t offTick, uint8_t pitch) {
  loop.beginCapture(CapturePhase::Record);
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(onTick, 1, pitch, 100)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(offTick, 1, pitch, 0)));
  TEST_ASSERT_EQUAL(CommitResult::Committed,
                    loop.commitCapturePass(CommitReason::RecordStop, offTick));
  drainPendingSnapshotPublish(loop);
}

void test_record_stop_publishes_complete_snapshot() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  loop.loopId = 7;
  loop.loopLengthTicks = 3072;
  loop.beginCapture(CapturePhase::Record);
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(100, 1, 60, 100)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(200, 1, 60, 0)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::ControlChange(120, 1, 7, 64)));
  TEST_ASSERT_EQUAL(CommitResult::Committed,
                    loop.commitCapturePass(CommitReason::RecordStop, 200));
  TEST_ASSERT_TRUE(loop.hasPendingSnapshotPublish());
  TEST_ASSERT_NULL(loop.publishedSnapshot().get());
  drainPendingSnapshotPublish(loop);

  const auto snapshot = loop.publishedSnapshot();
  TEST_ASSERT_NOT_NULL(snapshot.get());
  const auto notes = snapshot->noteMembership(60, 0);
  TEST_ASSERT_NOT_NULL(notes.get());
  TEST_ASSERT_EQUAL(1u, notes->references.size());
  TEST_ASSERT_EQUAL(100u, notes->references[0]->startTick);
  TEST_ASSERT_EQUAL(200u, notes->references[0]->endTick);
  const auto events = snapshot->eventMembership(0);
  TEST_ASSERT_NOT_NULL(events.get());
  TEST_ASSERT_EQUAL(3u, events->references.size());
  TEST_ASSERT_EQUAL(events->references[0]->canonicalOrder < events->references[1]->canonicalOrder,
                    true);
  TEST_ASSERT_TRUE(loop.passes.hasRecordPass());
}

void test_record_wrap_note_publishes_one_reference() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  loop.loopId = 7;
  loop.loopLengthTicks = 3072;
  loop.beginCapture(CapturePhase::Record);
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(2800, 1, 60, 100)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(100, 1, 60, 0)));
  TEST_ASSERT_EQUAL(CommitResult::Committed,
                    loop.commitCapturePass(CommitReason::RecordStop, 100));
  drainPendingSnapshotPublish(loop);

  const auto snapshot = loop.publishedSnapshot();
  TEST_ASSERT_NOT_NULL(snapshot.get());
  const auto tail = snapshot->noteMembership(60, 3);
  const auto head = snapshot->noteMembership(60, 0);
  TEST_ASSERT_NOT_NULL(tail.get());
  TEST_ASSERT_NOT_NULL(head.get());
  TEST_ASSERT_EQUAL(1u, tail->references.size());
  TEST_ASSERT_EQUAL(1u, head->references.size());
  TEST_ASSERT_EQUAL_PTR(tail->references[0].get(), head->references[0].get());
  TEST_ASSERT_TRUE(tail->references[0]->isWrap());
}

void test_overdub_wrap_does_not_publish_global_snapshot() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  loop.loopId = 7;
  loop.loopLengthTicks = 3072;
  recordLinearNote(loop, 100, 200, 60);
  const auto afterRecord = loop.publishedSnapshot();
  TEST_ASSERT_NOT_NULL(afterRecord.get());

  loop.beginCapture(CapturePhase::Overdub);
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(400, 1, 64, 90)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(480, 1, 64, 0)));
  TEST_ASSERT_EQUAL(CommitResult::Committed,
                    loop.commitCapturePass(CommitReason::OverdubWrap, 480));
  TEST_ASSERT_EQUAL_PTR(afterRecord.get(), loop.publishedSnapshot().get());
  TEST_ASSERT_EQUAL(1u, loop.passes.overdubPasses.size());
}

void test_overdub_stop_publishes_successor_from_parent() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  loop.loopId = 7;
  loop.loopLengthTicks = 3072;
  recordLinearNote(loop, 100, 200, 60);
  const auto afterRecord = loop.publishedSnapshot();
  TEST_ASSERT_NOT_NULL(afterRecord.get());

  loop.beginCapture(CapturePhase::Overdub);
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(400, 1, 64, 90)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(480, 1, 64, 0)));
  TEST_ASSERT_EQUAL(CommitResult::Committed,
                    loop.commitCapturePass(CommitReason::OverdubStop, 480));
  TEST_ASSERT_TRUE(loop.tryPublishSessionExitSnapshot(CommitReason::OverdubStop, {}));

  const auto afterOverdub = loop.publishedSnapshot();
  TEST_ASSERT_NOT_NULL(afterOverdub.get());
  TEST_ASSERT_TRUE(afterOverdub.get() != afterRecord.get());
  TEST_ASSERT_EQUAL(1u, afterRecord->noteMembership(60, 0)->references.size());
  TEST_ASSERT_NULL(afterRecord->noteMembership(64, 0).get());
  TEST_ASSERT_EQUAL(1u, afterOverdub->noteMembership(60, 0)->references.size());
  TEST_ASSERT_EQUAL(1u, afterOverdub->noteMembership(64, 0)->references.size());
  TEST_ASSERT_EQUAL_PTR(afterRecord->noteMembership(60, 0)->references[0].get(),
                        afterOverdub->noteMembership(60, 0)->references[0].get());
}

void test_note_edit_close_replaces_published_note() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  loop.loopId = 7;
  loop.loopLengthTicks = 3072;
  recordLinearNote(loop, 100, 200, 60);
  const auto afterRecord = loop.publishedSnapshot();
  const NoteId noteId = afterRecord->noteMembership(60, 0)->references[0]->noteId;

  const EditPassId rowId = loop.saveNoteEditPass(0, makePitchRow(noteId, 100, 200, 67));
  TEST_ASSERT_NOT_EQUAL(kInvalidEditPassId, rowId);
  EditPassIdList ids;
  ids.push_back(rowId);
  TEST_ASSERT_TRUE(loop.tryPublishNoteEditSnapshot(ids));

  const auto afterEdit = loop.publishedSnapshot();
  TEST_ASSERT_NOT_NULL(afterEdit.get());
  TEST_ASSERT_TRUE(afterEdit.get() != afterRecord.get());
  const auto oldPitch = afterEdit->noteMembership(60, 0);
  const size_t oldPitchCount = oldPitch ? oldPitch->references.size() : 0;
  TEST_ASSERT_EQUAL(0u, oldPitchCount);
  TEST_ASSERT_EQUAL(1u, afterEdit->noteMembership(67, 0)->references.size());
  TEST_ASSERT_EQUAL(noteId, afterEdit->noteMembership(67, 0)->references[0]->noteId);
}

void test_failed_edit_publish_preserves_parent_snapshot() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  loop.loopId = 7;
  loop.loopLengthTicks = 3072;
  recordLinearNote(loop, 100, 200, 60);
  const auto afterRecord = loop.publishedSnapshot();

  EditPassIdList missing;
  missing.push_back(999);
  TEST_ASSERT_FALSE(loop.tryPublishNoteEditSnapshot(missing));
  TEST_ASSERT_EQUAL_PTR(afterRecord.get(), loop.publishedSnapshot().get());
}

void sortEventsForCompare(SessionMidiEventVec& events) {
  std::sort(events.begin(), events.end(), [](const MidiEvent& left, const MidiEvent& right) {
    if (left.tick != right.tick) {
      return left.tick < right.tick;
    }
    if (left.type != right.type) {
      return static_cast<uint8_t>(left.type) < static_cast<uint8_t>(right.type);
    }
    if (left.isNoteOn() || left.isNoteOff()) {
      return left.data.noteData.note < right.data.noteData.note;
    }
    return false;
  });
}

void assertGatheredEventsMatch(const SessionMidiEventVec& snapshotEvents,
                               const SessionMidiEventVec& compatibilityEvents) {
  TEST_ASSERT_EQUAL(compatibilityEvents.size(), snapshotEvents.size());
  for (size_t i = 0; i < snapshotEvents.size(); ++i) {
    TEST_ASSERT_EQUAL(compatibilityEvents[i].tick, snapshotEvents[i].tick);
    TEST_ASSERT_EQUAL(static_cast<uint8_t>(compatibilityEvents[i].type),
                      static_cast<uint8_t>(snapshotEvents[i].type));
    if (snapshotEvents[i].isNoteOn() || snapshotEvents[i].isNoteOff()) {
      TEST_ASSERT_EQUAL(compatibilityEvents[i].data.noteData.note,
                        snapshotEvents[i].data.noteData.note);
      TEST_ASSERT_EQUAL(compatibilityEvents[i].data.noteData.velocity,
                        snapshotEvents[i].data.noteData.velocity);
    } else if (snapshotEvents[i].type == midi::ControlChange) {
      TEST_ASSERT_EQUAL(compatibilityEvents[i].data.ccData.cc, snapshotEvents[i].data.ccData.cc);
      TEST_ASSERT_EQUAL(compatibilityEvents[i].data.ccData.value, snapshotEvents[i].data.ccData.value);
    }
  }
}

void test_playback_gather_reads_published_snapshot_with_fallback() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  loop.loopId = 7;
  loop.loopLengthTicks = 3072;

  SessionMidiEventVec emptySnapshot;
  TEST_ASSERT_FALSE(loop.tryGatherPublishedSnapshotEventsInWindow(emptySnapshot, 0, 3072));
  TEST_ASSERT_NULL(loop.publishedSnapshotIdentity());

  loop.beginCapture(CapturePhase::Record);
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(100, 1, 60, 100)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(200, 1, 60, 0)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::ControlChange(120, 1, 7, 64)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(900, 1, 64, 90)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(980, 1, 64, 0)));
  TEST_ASSERT_EQUAL(CommitResult::Committed,
                    loop.commitCapturePass(CommitReason::RecordStop, 980));
  drainPendingSnapshotPublish(loop);

  const void* afterRecordIdentity = loop.publishedSnapshotIdentity();
  TEST_ASSERT_NOT_NULL(afterRecordIdentity);
  TEST_ASSERT_EQUAL_PTR(loop.publishedSnapshot().get(), afterRecordIdentity);

  SessionMidiEventVec snapshotFull;
  SessionMidiEventVec compatibilityFull;
  TEST_ASSERT_TRUE(loop.tryGatherPublishedSnapshotEventsInWindow(snapshotFull, 0, 3072));
  loop.gatherCommittedEventsInWindow(compatibilityFull, 0, 3072, false);
  sortEventsForCompare(snapshotFull);
  sortEventsForCompare(compatibilityFull);
  assertGatheredEventsMatch(snapshotFull, compatibilityFull);
  TEST_ASSERT_EQUAL(5u, snapshotFull.size());

  SessionMidiEventVec snapshotWindow;
  SessionMidiEventVec compatibilityWindow;
  TEST_ASSERT_TRUE(loop.tryGatherPublishedSnapshotEventsInWindow(snapshotWindow, 0, 768));
  loop.gatherCommittedEventsInWindow(compatibilityWindow, 0, 768, false);
  sortEventsForCompare(snapshotWindow);
  sortEventsForCompare(compatibilityWindow);
  assertGatheredEventsMatch(snapshotWindow, compatibilityWindow);
  TEST_ASSERT_EQUAL(3u, snapshotWindow.size());

  loop.beginCapture(CapturePhase::Overdub);
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(400, 1, 67, 80)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(460, 1, 67, 0)));
  TEST_ASSERT_EQUAL(CommitResult::Committed,
                    loop.commitCapturePass(CommitReason::OverdubStop, 460));
  TEST_ASSERT_TRUE(loop.tryPublishSessionExitSnapshot(CommitReason::OverdubStop, {}));
  TEST_ASSERT_TRUE(loop.publishedSnapshotIdentity() != afterRecordIdentity);

  SessionMidiEventVec snapshotAfterOverdub;
  SessionMidiEventVec compatibilityAfterOverdub;
  TEST_ASSERT_TRUE(
      loop.tryGatherPublishedSnapshotEventsInWindow(snapshotAfterOverdub, 0, 3072));
  loop.gatherCommittedEventsInWindow(compatibilityAfterOverdub, 0, 3072, false);
  sortEventsForCompare(snapshotAfterOverdub);
  sortEventsForCompare(compatibilityAfterOverdub);
  assertGatheredEventsMatch(snapshotAfterOverdub, compatibilityAfterOverdub);

  loop.resetPassTimeline();
  TEST_ASSERT_NULL(loop.publishedSnapshotIdentity());
  SessionMidiEventVec afterReset;
  TEST_ASSERT_FALSE(loop.tryGatherPublishedSnapshotEventsInWindow(afterReset, 0, 3072));
}

void test_stale_published_snapshot_gathers_window_without_full_compatibility() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  loop.loopId = 7;
  loop.loopLengthTicks = 3072;
  recordLinearNote(loop, 100, 200, 60);
  TEST_ASSERT_TRUE(loop.publishedSnapshotIsCurrent());
  SessionMidiEventVec fromSnapshot;
  TEST_ASSERT_TRUE(loop.tryGatherPublishedSnapshotEventsInWindow(fromSnapshot, 0, 3072));
  TEST_ASSERT_EQUAL(2u, fromSnapshot.size());

  ++loop.playbackRevision;
  TEST_ASSERT_FALSE(loop.publishedSnapshotIsCurrent());
  TEST_ASSERT_NULL(loop.publishedSnapshotIdentity());
  SessionMidiEventVec stale;
  TEST_ASSERT_TRUE(loop.tryGatherPublishedSnapshotEventsInWindow(stale, 0, 3072));
  sortEventsForCompare(stale);
  sortEventsForCompare(fromSnapshot);
  assertGatheredEventsMatch(stale, fromSnapshot);
}

void test_stale_snapshot_overlays_unpublished_overdub_pass() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  loop.loopId = 7;
  loop.loopLengthTicks = 3072;
  recordLinearNote(loop, 100, 200, 60);
  const auto afterRecord = loop.publishedSnapshot();
  TEST_ASSERT_NOT_NULL(afterRecord.get());

  loop.openOverdubSession(0);
  loop.beginCapture(CapturePhase::Overdub);
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(400, 1, 64, 90)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(480, 1, 64, 0)));
  TEST_ASSERT_EQUAL(CommitResult::Committed,
                    loop.commitCapturePass(CommitReason::OverdubWrap, 480));
  loop.pushOverdubSessionPass(loop.lastCommittedPassId(), {});
  TEST_ASSERT_EQUAL_PTR(afterRecord.get(), loop.publishedSnapshot().get());
  TEST_ASSERT_FALSE(loop.publishedSnapshotIsCurrent());

  SessionMidiEventVec snapshotWindow;
  SessionMidiEventVec compatibilityWindow;
  TEST_ASSERT_TRUE(loop.tryGatherPublishedSnapshotEventsInWindow(snapshotWindow, 0, 768));
  loop.gatherCommittedEventsInWindow(compatibilityWindow, 0, 768, false);
  sortEventsForCompare(snapshotWindow);
  sortEventsForCompare(compatibilityWindow);
  assertGatheredEventsMatch(snapshotWindow, compatibilityWindow);
  TEST_ASSERT_EQUAL(1, countNoteOns(snapshotWindow, 60));
  TEST_ASSERT_EQUAL(1, countNoteOns(snapshotWindow, 64));
}

void test_stale_snapshot_applies_unpublished_hide_companion() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  loop.loopId = 7;
  loop.loopLengthTicks = 3072;
  recordLinearNote(loop, 100, 200, 60);
  const auto membership = loop.publishedSnapshot()->noteMembership(60, 0);
  TEST_ASSERT_NOT_NULL(membership.get());
  TEST_ASSERT_FALSE(membership->references.empty());
  const NoteId hiddenNoteId = membership->references.front()->noteId;
  TEST_ASSERT_NOT_EQUAL(kInvalidNoteId, hiddenNoteId);

  loop.openOverdubSession(0);
  loop.beginCapture(CapturePhase::Overdub);
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(120, 1, 64, 90)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(180, 1, 64, 0)));
  TEST_ASSERT_EQUAL(CommitResult::Committed,
                    loop.commitCapturePass(CommitReason::OverdubWrap, 180));
  EditPass hide{};
  hide.passType = EditPassType::Note;
  hide.actionType = EditActionType::Delete;
  hide.targetNoteId = hiddenNoteId;
  hide.pitch = 60;
  hide.startTick = 100;
  hide.endTick = 200;
  const EditPassId hideId = loop.saveNoteEditPass(0, hide);
  TEST_ASSERT_NOT_EQUAL(kInvalidEditPassId, hideId);
  EditPassIdList companions;
  companions.push_back(hideId);
  loop.pushOverdubSessionPass(loop.lastCommittedPassId(), companions);
  TEST_ASSERT_FALSE(loop.publishedSnapshotIsCurrent());

  SessionMidiEventVec snapshotWindow;
  SessionMidiEventVec compatibilityWindow;
  TEST_ASSERT_TRUE(loop.tryGatherPublishedSnapshotEventsInWindow(snapshotWindow, 0, 768));
  loop.gatherCommittedEventsInWindow(compatibilityWindow, 0, 768, true);
  sortEventsForCompare(snapshotWindow);
  sortEventsForCompare(compatibilityWindow);
  assertGatheredEventsMatch(snapshotWindow, compatibilityWindow);
  TEST_ASSERT_EQUAL(0, countNoteOns(snapshotWindow, 60));
  TEST_ASSERT_EQUAL(1, countNoteOns(snapshotWindow, 64));
}

void test_overdub_publish_keeps_pass_events_when_companion_is_missing() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  loop.loopId = 7;
  loop.loopLengthTicks = 3072;
  recordLinearNote(loop, 100, 200, 60);

  loop.beginCapture(CapturePhase::Overdub);
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(400, 1, 64, 90)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(480, 1, 64, 0)));
  TEST_ASSERT_EQUAL(CommitResult::Committed,
                    loop.commitCapturePass(CommitReason::OverdubStop, 480));

  EditPass hide{};
  hide.passType = EditPassType::Note;
  hide.actionType = EditActionType::Delete;
  hide.targetNoteId = 999;
  hide.pitch = 60;
  hide.startTick = 100;
  hide.endTick = 200;
  const EditPassId hideId = loop.saveNoteEditPass(0, hide);
  TEST_ASSERT_NOT_EQUAL(kInvalidEditPassId, hideId);
  EditPassIdList companions;
  companions.push_back(hideId);
  TEST_ASSERT_TRUE(loop.tryPublishSessionExitSnapshot(CommitReason::OverdubStop, companions));
  TEST_ASSERT_TRUE(loop.publishedSnapshotIsCurrent());

  SessionMidiEventVec snapshotEvents;
  SessionMidiEventVec compatibilityEvents;
  TEST_ASSERT_TRUE(loop.tryGatherPublishedSnapshotEventsInWindow(snapshotEvents, 0, 3072));
  loop.gatherCommittedEventsInWindow(compatibilityEvents, 0, 3072, false);
  TEST_ASSERT_EQUAL(1, countNoteOns(snapshotEvents, 64));
  TEST_ASSERT_EQUAL(1, countNoteOns(compatibilityEvents, 64));
}

void test_queue_overdub_snapshot_waits_for_idle_steps() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  loop.loopId = 7;
  loop.loopLengthTicks = 3072;
  recordLinearNote(loop, 100, 200, 60);
  const auto afterRecord = loop.publishedSnapshot();
  TEST_ASSERT_NOT_NULL(afterRecord.get());

  loop.beginCapture(CapturePhase::Overdub);
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(400, 1, 64, 90)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(480, 1, 64, 0)));
  TEST_ASSERT_EQUAL(CommitResult::Committed,
                    loop.commitCapturePass(CommitReason::OverdubStop, 480));
  TEST_ASSERT_TRUE(loop.queueSessionExitSnapshot(CommitReason::OverdubStop, {}));
  TEST_ASSERT_TRUE(loop.hasPendingSnapshotPublish());
  TEST_ASSERT_EQUAL_PTR(afterRecord.get(), loop.publishedSnapshot().get());
  TEST_ASSERT_FALSE(loop.publishedSnapshotIsCurrent());

  drainPendingSnapshotPublish(loop);
  TEST_ASSERT_TRUE(loop.publishedSnapshotIsCurrent());
  TEST_ASSERT_TRUE(loop.publishedSnapshot().get() != afterRecord.get());
  TEST_ASSERT_EQUAL(1u, loop.publishedSnapshot()->noteMembership(64, 0)->references.size());
}

void test_live_record_pending_after_length_reaches_one_bar() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  loop.loopId = 7;
  loop.loopLengthTicks = 0;
  loop.beginCapture(CapturePhase::Record);
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(100, 1, 60, 100)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(200, 1, 60, 0)));
  TEST_ASSERT_FALSE(loop.hasPendingSnapshotPublish());
  loop.loopLengthTicks = Config::TICKS_PER_BAR;
  TEST_ASSERT_TRUE(loop.hasPendingSnapshotPublish());
}

void drainInFlightSnapshotPublish(Loop& loop) {
  size_t steps = 0;
  while (loop.hasInFlightSnapshotConstruction() && steps < 10000) {
    loop.stepPendingSnapshotPublish(1, false);
    ++steps;
  }
}

void test_live_record_does_not_start_in_progress_bar() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  loop.loopId = 7;
  loop.loopLengthTicks = Config::TICKS_PER_BAR * 4;
  loop.beginCapture(CapturePhase::Record);
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(100, 1, 60, 100)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(200, 1, 60, 0)));
  TEST_ASSERT_TRUE(loop.stepPendingSnapshotPublish(1, true, 0, Config::TICKS_PER_BAR));
  drainInFlightSnapshotPublish(loop);
  TEST_ASSERT_FALSE(loop.hasInFlightSnapshotConstruction());
  TEST_ASSERT_EQUAL(Config::TICKS_PER_BAR, loop.liveRecordAppliedCompletedTicks());

  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(Config::TICKS_PER_BAR + 100, 1, 64, 100)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(Config::TICKS_PER_BAR + 200, 1, 64, 0)));
  TEST_ASSERT_FALSE(loop.hasPendingSnapshotPublish());
  TEST_ASSERT_FALSE(
      loop.stepPendingSnapshotPublish(1, true, 0, Config::TICKS_PER_BAR));
  TEST_ASSERT_FALSE(loop.hasInFlightSnapshotConstruction());
}

void test_live_record_continues_next_completed_bar() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  loop.loopId = 7;
  loop.loopLengthTicks = Config::TICKS_PER_BAR * 4;
  loop.beginCapture(CapturePhase::Record);
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(100, 1, 60, 100)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(200, 1, 60, 0)));
  TEST_ASSERT_TRUE(loop.stepPendingSnapshotPublish(1, true, 0, Config::TICKS_PER_BAR));
  drainInFlightSnapshotPublish(loop);
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(Config::TICKS_PER_BAR + 100, 1, 64, 100)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(Config::TICKS_PER_BAR + 200, 1, 64, 0)));
  TEST_ASSERT_TRUE(
      loop.stepPendingSnapshotPublish(1, true, 0, Config::TICKS_PER_BAR * 2));
  TEST_ASSERT_TRUE(loop.hasInFlightSnapshotConstruction());
  drainInFlightSnapshotPublish(loop);
  TEST_ASSERT_EQUAL(Config::TICKS_PER_BAR * 2, loop.liveRecordAppliedCompletedTicks());
}

void test_live_record_starts_snapshot_before_session_exit() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  loop.loopId = 7;
  loop.loopLengthTicks = 3072;
  loop.beginCapture(CapturePhase::Record);
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(100, 1, 60, 100)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(200, 1, 60, 0)));
  TEST_ASSERT_TRUE(loop.hasPendingSnapshotPublish());
  TEST_ASSERT_TRUE(loop.stepPendingSnapshotPublish(1, true));
  TEST_ASSERT_TRUE(loop.hasInFlightSnapshotConstruction());
  TEST_ASSERT_NULL(loop.publishedSnapshot().get());

  size_t steps = 0;
  while (loop.hasInFlightSnapshotConstruction() && steps < 10000) {
    loop.stepPendingSnapshotPublish(1, false);
    ++steps;
  }
  TEST_ASSERT_FALSE(loop.hasInFlightSnapshotConstruction());
  TEST_ASSERT_NULL(loop.publishedSnapshot().get());

  TEST_ASSERT_EQUAL(CommitResult::Committed,
                    loop.commitCapturePass(CommitReason::RecordStop, 200));
  drainPendingSnapshotPublish(loop);
  TEST_ASSERT_TRUE(loop.publishedSnapshotIsCurrent());
  TEST_ASSERT_EQUAL(1u, loop.publishedSnapshot()->noteMembership(60, 0)->references.size());
}

void test_overdub_idle_advances_in_flight_construction_only() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  loop.loopId = 7;
  loop.loopLengthTicks = 3072;
  loop.beginCapture(CapturePhase::Record);
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(100, 1, 60, 100)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(200, 1, 60, 0)));
  TEST_ASSERT_EQUAL(CommitResult::Committed,
                    loop.commitCapturePass(CommitReason::RecordStop, 200));
  TEST_ASSERT_TRUE(loop.hasPendingSnapshotPublish());
  TEST_ASSERT_FALSE(loop.hasInFlightSnapshotConstruction());
  TEST_ASSERT_NULL(loop.publishedSnapshot().get());

  TEST_ASSERT_TRUE(loop.stepPendingSnapshotPublish(1, false));
  TEST_ASSERT_TRUE(loop.hasPendingSnapshotPublish());
  TEST_ASSERT_FALSE(loop.hasInFlightSnapshotConstruction());
  TEST_ASSERT_NULL(loop.publishedSnapshot().get());

  TEST_ASSERT_TRUE(loop.stepPendingSnapshotPublish(1, true));
  TEST_ASSERT_TRUE(loop.hasInFlightSnapshotConstruction());
  TEST_ASSERT_NULL(loop.publishedSnapshot().get());

  size_t steps = 0;
  while (loop.hasInFlightSnapshotConstruction() && steps < 10000) {
    loop.stepPendingSnapshotPublish(1, false);
    ++steps;
  }
  TEST_ASSERT_FALSE(loop.hasInFlightSnapshotConstruction());
  TEST_ASSERT_NOT_NULL(loop.publishedSnapshot().get());
  TEST_ASSERT_TRUE(loop.publishedSnapshotIsCurrent());
}

void test_overdub_wrap_ahead_does_not_publish_until_session_exit() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  loop.loopId = 7;
  loop.loopLengthTicks = 3072;
  recordLinearNote(loop, 100, 200, 60);
  const auto afterRecord = loop.publishedSnapshot();

  loop.openOverdubSession(0);
  loop.beginCapture(CapturePhase::Overdub);
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(400, 1, 64, 90)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(480, 1, 64, 0)));
  TEST_ASSERT_EQUAL(CommitResult::Committed,
                    loop.commitCapturePass(CommitReason::OverdubWrap, 480));
  loop.pushOverdubSessionPass(loop.lastCommittedPassId(), {});
  TEST_ASSERT_TRUE(loop.hasPendingSnapshotPublish());
  TEST_ASSERT_EQUAL_PTR(afterRecord.get(), loop.publishedSnapshot().get());
  TEST_ASSERT_FALSE(loop.publishedSnapshotIsCurrent());

  loop.beginCapture(CapturePhase::Overdub);
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(800, 1, 67, 80)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(860, 1, 67, 0)));
  TEST_ASSERT_EQUAL(CommitResult::Committed,
                    loop.commitCapturePass(CommitReason::OverdubStop, 860));
  TEST_ASSERT_TRUE(loop.queueSessionExitSnapshot(CommitReason::OverdubStop, {}));
  TEST_ASSERT_EQUAL_PTR(afterRecord.get(), loop.publishedSnapshot().get());
  drainPendingSnapshotPublish(loop);
  TEST_ASSERT_TRUE(loop.publishedSnapshotIsCurrent());
  SessionMidiEventVec snapshotEvents;
  TEST_ASSERT_TRUE(loop.tryGatherPublishedSnapshotEventsInWindow(snapshotEvents, 0, 3072));
  TEST_ASSERT_EQUAL(1, countNoteOns(snapshotEvents, 64));
  TEST_ASSERT_EQUAL(1, countNoteOns(snapshotEvents, 67));
}

void test_visual_cache_reads_published_snapshot_with_fallback() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  loop.loopId = 7;
  loop.loopLengthTicks = 3072;
  recordLinearNote(loop, 100, 200, 60);
  TEST_ASSERT_TRUE(loop.publishedSnapshotIsCurrent());

  SessionMidiEventVec snapshotEvents;
  TEST_ASSERT_TRUE(loop.tryGatherPublishedSnapshotEventsInWindow(snapshotEvents, 0, 3072));
  const NoteUtils::DisplayNoteVec expected =
      NoteUtils::reconstructDisplayNotes(snapshotEvents, 3072, false, false);
  TEST_ASSERT_EQUAL(1u, expected.size());

  loop.invalidateDisplayCaches();
  TEST_ASSERT_TRUE(loop.visualCacheDirty);
  loop.rebuildVisualCacheIdleSlice(4, 0);
  TEST_ASSERT_EQUAL(expected.size(), loop.visualCache.notes.size());
  TEST_ASSERT_EQUAL(expected[0].note, loop.visualCache.notes[0].note);
  TEST_ASSERT_EQUAL(expected[0].startTick, loop.visualCache.notes[0].startTick);
  TEST_ASSERT_EQUAL(expected[0].endTick, loop.visualCache.notes[0].endTick);

  loop.rebuildVisualCacheFromPasses();
  TEST_ASSERT_EQUAL(expected.size(), loop.visualCache.notes.size());
  TEST_ASSERT_EQUAL(expected[0].note, loop.visualCache.notes[0].note);
  TEST_ASSERT_EQUAL(expected[0].startTick, loop.visualCache.notes[0].startTick);
  TEST_ASSERT_EQUAL(expected[0].endTick, loop.visualCache.notes[0].endTick);

  Loop noSnapshot;
  seedCommittedPair(noSnapshot);
  SessionMidiEventVec missing;
  TEST_ASSERT_FALSE(noSnapshot.tryGatherPublishedSnapshotEventsInWindow(missing, 0, kLoopLen));
  noSnapshot.invalidateDisplayCaches();
  noSnapshot.rebuildVisualCacheIdleSlice(4, 0);
  TEST_ASSERT_FALSE(noSnapshot.visualCache.notes.empty());
}

void test_overdub_without_parent_does_not_invent_snapshot() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  loop.loopId = 7;
  loop.loopLengthTicks = 3072;
  loop.beginCapture(CapturePhase::Overdub);
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(400, 1, 64, 90)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(480, 1, 64, 0)));
  TEST_ASSERT_EQUAL(CommitResult::Committed,
                    loop.commitCapturePass(CommitReason::OverdubStop, 480));
  TEST_ASSERT_FALSE(loop.tryPublishSessionExitSnapshot(CommitReason::OverdubStop, {}));
  TEST_ASSERT_NULL(loop.publishedSnapshot().get());
}

void test_reclaim_disabled_overdub_releases_committed_chunks() {
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
  RUN_TEST(test_reclaim_disabled_overdub_releases_committed_chunks);
  RUN_TEST(test_record_stop_publishes_complete_snapshot);
  RUN_TEST(test_record_wrap_note_publishes_one_reference);
  RUN_TEST(test_overdub_wrap_does_not_publish_global_snapshot);
  RUN_TEST(test_overdub_stop_publishes_successor_from_parent);
  RUN_TEST(test_note_edit_close_replaces_published_note);
  RUN_TEST(test_failed_edit_publish_preserves_parent_snapshot);
  RUN_TEST(test_playback_gather_reads_published_snapshot_with_fallback);
  RUN_TEST(test_visual_cache_reads_published_snapshot_with_fallback);
  RUN_TEST(test_stale_published_snapshot_gathers_window_without_full_compatibility);
  RUN_TEST(test_stale_snapshot_overlays_unpublished_overdub_pass);
  RUN_TEST(test_stale_snapshot_applies_unpublished_hide_companion);
  RUN_TEST(test_overdub_publish_keeps_pass_events_when_companion_is_missing);
  RUN_TEST(test_queue_overdub_snapshot_waits_for_idle_steps);
  RUN_TEST(test_live_record_pending_after_length_reaches_one_bar);
  RUN_TEST(test_live_record_does_not_start_in_progress_bar);
  RUN_TEST(test_live_record_continues_next_completed_bar);
  RUN_TEST(test_live_record_starts_snapshot_before_session_exit);
  RUN_TEST(test_overdub_idle_advances_in_flight_construction_only);
  RUN_TEST(test_overdub_wrap_ahead_does_not_publish_until_session_exit);
  RUN_TEST(test_overdub_without_parent_does_not_invent_snapshot);
  return UNITY_END();
}
