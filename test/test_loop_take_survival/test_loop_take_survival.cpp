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
#include "LoopInternal.h"
#include "LoopSnapshot.h"
#include "../test_support/CommittedChunkIdTestHelpers.h"
#include "../test_support/NoteIdTestFixtures.h"
#include "EditPass.h"
#include "MidiEvent.h"
#include "PassReclaim.h"
#include "StorageLoopIo.h"
#include "Utils/DisplayWindowUtils.h"

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

const NoteUtils::DisplayNote* findDisplayNoteById(const NoteUtils::DisplayNoteVec& notes,
                                                  NoteId noteId) {
  for (const NoteUtils::DisplayNote& note : notes) {
    if (note.noteId == noteId) {
      return &note;
    }
  }
  return nullptr;
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

NoteId lastCaptureNoteOnId(const Loop& loop) {
  for (size_t i = loop.capture.store.size(); i > 0; --i) {
    const MidiEvent& evt = loop.capture.store.at(i - 1);
    if (evt.isNoteOn()) {
      return evt.noteId;
    }
  }
  return kInvalidNoteId;
}

void pushClosedPendingAdd(Loop& loop, NoteId noteId, uint8_t pitch, uint32_t startTick,
                          uint32_t endTick) {
  PendingNoteChange add{};
  add.kind = PendingNoteChangeKind::Add;
  add.noteId = noteId;
  add.channel = 1;
  add.pitch = pitch;
  add.velocity = 90;
  add.startTick = startTick;
  add.endTick = endTick;
  loop.pushPendingNoteChangeForTest(add);
}

void commitOverdubWrapLikeDevice(Loop& loop, uint32_t sealedAtTick, bool requireWrapJournal) {
  const uint32_t prior = loop.playbackRevision;
  TEST_ASSERT_EQUAL(CommitResult::Committed,
                    loop.commitCapturePass(CommitReason::OverdubWrap, sealedAtTick));
  const PassId passId = loop.lastCommittedPassId();
  const bool queued = loop.queueOverdubWrapSnapshotJournal(passId);
  if (requireWrapJournal) {
    TEST_ASSERT_TRUE(queued);
  }
  // Pairing fixture only: journal first, then drop live pending the way wrap seal does.
  // Do not call sealPendingNoteChangesToEditPasses here — this suite does not link that TU.
  loop.clearPendingNoteChanges();
  loop.pushOverdubSessionPass(passId, {});
  loop.recordLastOverdubSeal(passId, {}, prior);
  loop.beginCapture(CapturePhase::Overdub);
}

void assertClosedNotesDidNotPairAcrossPasses(const Loop& loop, NoteId idA, NoteId idB,
                                             uint32_t endA, uint32_t endB) {
  TEST_ASSERT_NOT_EQUAL(kInvalidNoteId, idA);
  TEST_ASSERT_NOT_EQUAL(kInvalidNoteId, idB);
  TEST_ASSERT_NOT_EQUAL(idA, idB);

  const auto snapshot = loop.publishedSnapshot();
  TEST_ASSERT_TRUE(static_cast<bool>(snapshot));
  const LoopSnapshotNoteRef noteA = snapshot->findNote(idA, 64);
  const LoopSnapshotNoteRef noteB = snapshot->findNote(idB, 64);
  TEST_ASSERT_TRUE(static_cast<bool>(noteA));
  TEST_ASSERT_TRUE(static_cast<bool>(noteB));
  TEST_ASSERT_EQUAL_UINT32(2304u, noteA->startTick);
  TEST_ASSERT_EQUAL_UINT32(endA, noteA->endTick);
  TEST_ASSERT_EQUAL_UINT32(768u, noteB->startTick);
  TEST_ASSERT_EQUAL_UINT32(endB, noteB->endTick);
  TEST_ASSERT_FALSE(noteA->isWrap(snapshot->loopLengthTicks()));
  TEST_ASSERT_FALSE(noteB->isWrap(snapshot->loopLengthTicks()));
  TEST_ASSERT_EQUAL(idA, noteA->noteId);
  TEST_ASSERT_EQUAL(idB, noteB->noteId);

  SessionMidiEventVec gathered;
  TEST_ASSERT_TRUE(loop.tryGatherPublishedSnapshotEventsInWindow(gathered, 0, 3072));
  int onA = 0;
  int offA = 0;
  int onB = 0;
  int offB = 0;
  for (const MidiEvent& evt : gathered) {
    if (evt.noteId == idA && evt.isNoteOn() && evt.data.noteData.note == 64) {
      TEST_ASSERT_EQUAL_UINT32(2304u, evt.tick);
      ++onA;
    }
    if (evt.noteId == idA && evt.isNoteOff() && evt.data.noteData.note == 64) {
      TEST_ASSERT_EQUAL_UINT32(endA, evt.tick);
      ++offA;
    }
    if (evt.noteId == idB && evt.isNoteOn() && evt.data.noteData.note == 64) {
      TEST_ASSERT_EQUAL_UINT32(768u, evt.tick);
      ++onB;
    }
    if (evt.noteId == idB && evt.isNoteOff() && evt.data.noteData.note == 64) {
      TEST_ASSERT_EQUAL_UINT32(endB, evt.tick);
      ++offB;
    }
  }
  TEST_ASSERT_EQUAL(1, onA);
  TEST_ASSERT_EQUAL(1, offA);
  TEST_ASSERT_EQUAL(1, onB);
  TEST_ASSERT_EQUAL(1, offB);
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
  TEST_ASSERT_EQUAL(1u, notes->count);
  TEST_ASSERT_EQUAL(100u, notes->references[0]->startTick);
  TEST_ASSERT_EQUAL(200u, notes->references[0]->endTick);
  const auto events = snapshot->eventMembership(0);
  TEST_ASSERT_NOT_NULL(events.get());
  TEST_ASSERT_EQUAL(3u, events->count);
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
  TEST_ASSERT_EQUAL(1u, tail->count);
  TEST_ASSERT_EQUAL(1u, head->count);
  TEST_ASSERT_EQUAL_PTR(tail->references[0].get(), head->references[0].get());
  TEST_ASSERT_TRUE(tail->references[0]->isWrap(snapshot->loopLengthTicks()));
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
  TEST_ASSERT_EQUAL(1u, afterRecord->noteMembership(60, 0)->count);
  TEST_ASSERT_NULL(afterRecord->noteMembership(64, 0).get());
  TEST_ASSERT_EQUAL(1u, afterOverdub->noteMembership(60, 0)->count);
  TEST_ASSERT_EQUAL(1u, afterOverdub->noteMembership(64, 0)->count);
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
  const size_t oldPitchCount = oldPitch ? oldPitch->count : 0;
  TEST_ASSERT_EQUAL(0u, oldPitchCount);
  TEST_ASSERT_EQUAL(1u, afterEdit->noteMembership(67, 0)->count);
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
  Loop::SnapshotWindowGatherPath emptyPath = Loop::SnapshotWindowGatherPath::Snapshot;
  TEST_ASSERT_FALSE(
      loop.tryGatherPublishedSnapshotEventsInWindow(emptySnapshot, 0, 3072, &emptyPath));
  TEST_ASSERT_EQUAL(static_cast<int>(Loop::SnapshotWindowGatherPath::None),
                    static_cast<int>(emptyPath));
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
  Loop::SnapshotWindowGatherPath currentPath = Loop::SnapshotWindowGatherPath::None;
  TEST_ASSERT_TRUE(
      loop.tryGatherPublishedSnapshotEventsInWindow(fromSnapshot, 0, 3072, &currentPath));
  TEST_ASSERT_EQUAL(static_cast<int>(Loop::SnapshotWindowGatherPath::Snapshot),
                    static_cast<int>(currentPath));
  TEST_ASSERT_EQUAL(2u, fromSnapshot.size());

  ++loop.playbackRevision;
  TEST_ASSERT_FALSE(loop.publishedSnapshotIsCurrent());
  TEST_ASSERT_NULL(loop.publishedSnapshotIdentity());
  SessionMidiEventVec stale;
  Loop::SnapshotWindowGatherPath stalePath = Loop::SnapshotWindowGatherPath::None;
  TEST_ASSERT_TRUE(loop.tryGatherPublishedSnapshotEventsInWindow(stale, 0, 3072, &stalePath));
  TEST_ASSERT_EQUAL(static_cast<int>(Loop::SnapshotWindowGatherPath::Overlay),
                    static_cast<int>(stalePath));
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
  TEST_ASSERT_TRUE(snapshotMembershipCount(membership.get()) > 0);
  const NoteId hiddenNoteId = membership->references[0]->noteId;
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
  TEST_ASSERT_EQUAL(1u, loop.publishedSnapshot()->noteMembership(64, 0)->count);
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

void test_live_record_does_not_rebuild_when_parent_has_no_room() {
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
  TEST_ASSERT_EQUAL(Config::TICKS_PER_BAR, loop.liveRecordAppliedCompletedTicks());
  TEST_ASSERT_TRUE(loop.liveRecordCanApplyCompletedTicks(Config::TICKS_PER_BAR * 17));
  TEST_ASSERT_FALSE(loop.liveRecordCanApplyCompletedTicks(Config::TICKS_PER_BAR * 18));

  const uint32_t pastParent = Config::TICKS_PER_BAR * 18;
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(pastParent + 100, 1, 67, 100)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(pastParent + 200, 1, 67, 0)));
  TEST_ASSERT_FALSE(loop.stepPendingSnapshotPublish(1, true, 0, pastParent));
  TEST_ASSERT_FALSE(loop.hasInFlightSnapshotConstruction());
  TEST_ASSERT_EQUAL(Config::TICKS_PER_BAR, loop.liveRecordAppliedCompletedTicks());
}

void test_live_record_second_bar_start_changes_are_bar_two_only() {
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
  TEST_ASSERT_EQUAL(Config::TICKS_PER_BAR, loop.liveRecordAppliedCompletedTicks());
  TEST_ASSERT_EQUAL(2u, loop.liveRecordAppliedEventIndex());

  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(Config::TICKS_PER_BAR + 100, 1, 64, 100)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(Config::TICKS_PER_BAR + 200, 1, 64, 0)));
  TEST_ASSERT_TRUE(
      loop.stepPendingSnapshotPublish(1, true, 0, Config::TICKS_PER_BAR * 2));
  const LoopSnapshotChangeSet* changes = loop.pendingSnapshotConstructionChanges();
  TEST_ASSERT_NOT_NULL(changes);
  TEST_ASSERT_TRUE(!changes->events.empty());
  for (const SnapshotEventChange& change : changes->events) {
    TEST_ASSERT_TRUE(change.next.value.tick >= Config::TICKS_PER_BAR);
    TEST_ASSERT_TRUE(change.next.value.tick < Config::TICKS_PER_BAR * 2);
  }
  for (const SnapshotNoteChange& change : changes->notes) {
    TEST_ASSERT_TRUE(change.next.startTick >= Config::TICKS_PER_BAR);
    TEST_ASSERT_TRUE(change.next.startTick < Config::TICKS_PER_BAR * 2);
  }
  drainInFlightSnapshotPublish(loop);
  TEST_ASSERT_EQUAL(Config::TICKS_PER_BAR * 2, loop.liveRecordAppliedCompletedTicks());
}

void test_live_record_held_note_pairs_on_next_bar_off() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  loop.loopId = 7;
  loop.loopLengthTicks = Config::TICKS_PER_BAR * 4;
  loop.beginCapture(CapturePhase::Record);
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(100, 1, 60, 100)));
  TEST_ASSERT_TRUE(loop.stepPendingSnapshotPublish(1, true, 0, Config::TICKS_PER_BAR));
  drainInFlightSnapshotPublish(loop);
  TEST_ASSERT_EQUAL(Config::TICKS_PER_BAR, loop.liveRecordAppliedCompletedTicks());
  TEST_ASSERT_EQUAL(1u, loop.liveRecordOpenNoteCount());
  TEST_ASSERT_NOT_NULL(loop.unpublishedSnapshot().get());
  TEST_ASSERT_NULL(loop.unpublishedSnapshot()->noteMembership(60, 0).get());

  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(Config::TICKS_PER_BAR + 200, 1, 60, 0)));
  TEST_ASSERT_TRUE(
      loop.stepPendingSnapshotPublish(1, true, 0, Config::TICKS_PER_BAR * 2));
  drainInFlightSnapshotPublish(loop);
  TEST_ASSERT_EQUAL(0u, loop.liveRecordOpenNoteCount());
  TEST_ASSERT_EQUAL(Config::TICKS_PER_BAR * 2, loop.liveRecordAppliedCompletedTicks());
  TEST_ASSERT_EQUAL(1u, loop.unpublishedSnapshot()->noteMembership(60, 0)->count);
}

void test_live_record_dense_bar_keeps_cursor_inside_bar() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  loop.loopId = 7;
  loop.loopLengthTicks = Config::TICKS_PER_BAR * 4;
  loop.beginCapture(CapturePhase::Record);
  constexpr size_t kPairs = 20;
  for (size_t i = 0; i < kPairs; ++i) {
    const uint32_t onTick = 10 + static_cast<uint32_t>(i) * 20;
    TEST_ASSERT_TRUE(loop.appendCaptureEvent(
        MidiEvent::NoteOn(onTick, 1, static_cast<uint8_t>(40 + i), 100)));
    TEST_ASSERT_TRUE(loop.appendCaptureEvent(
        MidiEvent::NoteOff(onTick + 10, 1, static_cast<uint8_t>(40 + i), 0)));
  }
  TEST_ASSERT_TRUE(loop.stepPendingSnapshotPublish(1, true, 0, Config::TICKS_PER_BAR));
  TEST_ASSERT_EQUAL(0u, loop.liveRecordAppliedCompletedTicks());
  const size_t afterFirst = loop.liveRecordAppliedEventIndex();
  TEST_ASSERT_TRUE(afterFirst > 0);
  TEST_ASSERT_TRUE(afterFirst < kPairs * 2);
  const LoopSnapshotChangeSet* changes = loop.pendingSnapshotConstructionChanges();
  TEST_ASSERT_NOT_NULL(changes);
  for (const SnapshotEventChange& change : changes->events) {
    TEST_ASSERT_TRUE(change.next.value.tick < Config::TICKS_PER_BAR);
  }
  drainInFlightSnapshotPublish(loop);
  TEST_ASSERT_EQUAL(0u, loop.liveRecordAppliedCompletedTicks());
  TEST_ASSERT_EQUAL(afterFirst, loop.liveRecordAppliedEventIndex());

  size_t steps = 0;
  while (loop.liveRecordAppliedCompletedTicks() < Config::TICKS_PER_BAR && steps < 16) {
    TEST_ASSERT_TRUE(loop.stepPendingSnapshotPublish(1, true, 0, Config::TICKS_PER_BAR));
    drainInFlightSnapshotPublish(loop);
    ++steps;
  }
  TEST_ASSERT_EQUAL(Config::TICKS_PER_BAR, loop.liveRecordAppliedCompletedTicks());
  TEST_ASSERT_EQUAL(kPairs * 2, loop.liveRecordAppliedEventIndex());
}

void test_live_record_equal_boundary_event_waits_for_next_window() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  loop.loopId = 7;
  loop.loopLengthTicks = Config::TICKS_PER_BAR * 4;
  loop.beginCapture(CapturePhase::Record);
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(100, 1, 60, 100)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(200, 1, 60, 0)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(
      MidiEvent::NoteOn(Config::TICKS_PER_BAR, 1, 64, 100)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(
      MidiEvent::NoteOff(Config::TICKS_PER_BAR + 100, 1, 64, 0)));
  TEST_ASSERT_TRUE(loop.stepPendingSnapshotPublish(1, true, 0, Config::TICKS_PER_BAR));
  drainInFlightSnapshotPublish(loop);
  TEST_ASSERT_EQUAL(Config::TICKS_PER_BAR, loop.liveRecordAppliedCompletedTicks());
  TEST_ASSERT_EQUAL(2u, loop.liveRecordAppliedEventIndex());
  TEST_ASSERT_EQUAL(1u, loop.unpublishedSnapshot()->noteMembership(60, 0)->count);
  TEST_ASSERT_NULL(loop.unpublishedSnapshot()->noteMembership(64, 1).get());

  TEST_ASSERT_TRUE(
      loop.stepPendingSnapshotPublish(1, true, 0, Config::TICKS_PER_BAR * 2));
  const LoopSnapshotChangeSet* changes = loop.pendingSnapshotConstructionChanges();
  TEST_ASSERT_NOT_NULL(changes);
  bool sawBoundaryOn = false;
  for (const SnapshotEventChange& change : changes->events) {
    TEST_ASSERT_TRUE(change.next.value.tick >= Config::TICKS_PER_BAR);
    if (change.next.value.tick == Config::TICKS_PER_BAR && change.next.value.isNoteOn()) {
      sawBoundaryOn = true;
    }
  }
  TEST_ASSERT_TRUE(sawBoundaryOn);
  drainInFlightSnapshotPublish(loop);
  TEST_ASSERT_EQUAL(Config::TICKS_PER_BAR * 2, loop.liveRecordAppliedCompletedTicks());
  TEST_ASSERT_EQUAL(1u, loop.unpublishedSnapshot()->noteMembership(64, 1)->count);
}

void test_live_record_same_pitch_second_bar_does_not_duplicate_first() {
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
  TEST_ASSERT_EQUAL(1u, loop.unpublishedSnapshot()->noteMembership(60, 0)->count);

  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(Config::TICKS_PER_BAR + 100, 1, 60, 100)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(Config::TICKS_PER_BAR + 200, 1, 60, 0)));
  TEST_ASSERT_TRUE(
      loop.stepPendingSnapshotPublish(1, true, 0, Config::TICKS_PER_BAR * 2));
  const LoopSnapshotChangeSet* changes = loop.pendingSnapshotConstructionChanges();
  TEST_ASSERT_NOT_NULL(changes);
  TEST_ASSERT_EQUAL(1u, changes->notes.size());
  TEST_ASSERT_TRUE(changes->notes[0].next.startTick >= Config::TICKS_PER_BAR);
  TEST_ASSERT_TRUE(changes->notes[0].next.startTick < Config::TICKS_PER_BAR * 2);
  drainInFlightSnapshotPublish(loop);
  TEST_ASSERT_EQUAL(1u, loop.unpublishedSnapshot()->noteMembership(60, 0)->count);
  TEST_ASSERT_EQUAL(1u, loop.unpublishedSnapshot()->noteMembership(60, 1)->count);
}

void test_session_exit_pass_change_skips_note_already_on_parent() {
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
  TEST_ASSERT_EQUAL(1u, loop.unpublishedSnapshot()->noteMembership(60, 0)->count);

  TEST_ASSERT_EQUAL(CommitResult::Committed,
                    loop.commitCapturePass(CommitReason::RecordStop, 200));
  TEST_ASSERT_TRUE(loop.pendingSnapshotSessionExitQueued());
  loop.stepPendingSnapshotPublish(1, true);
  const LoopSnapshotChangeSet* changes = loop.pendingSnapshotConstructionChanges();
  if (changes != nullptr) {
    for (const SnapshotNoteChange& change : changes->notes) {
      TEST_ASSERT_FALSE(change.kind == SnapshotChangeKind::Add && change.next.pitch == 60);
    }
  }
  if (loop.hasPendingSnapshotPublish()) {
    drainPendingSnapshotPublish(loop);
  }
  TEST_ASSERT_TRUE(loop.publishedSnapshotIsCurrent());
  TEST_ASSERT_EQUAL(1u, loop.publishedSnapshot()->noteMembership(60, 0)->count);
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
  TEST_ASSERT_EQUAL(1u, loop.publishedSnapshot()->noteMembership(60, 0)->count);
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

/// Task 3.5: a settled wrap with no session exit queued publishes its candidate and
/// the published snapshot is current for the sealed revision, so display and playback
/// can read the snapshot tier mid-overdub instead of falling back to a lagging cache.
void test_settled_overdub_wrap_publishes_current_snapshot() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  loop.loopId = 7;
  loop.loopLengthTicks = 3072;
  recordLinearNote(loop, 100, 200, 60);
  TEST_ASSERT_TRUE(loop.publishedSnapshotIsCurrent());

  loop.openOverdubSession(0);
  loop.beginCapture(CapturePhase::Overdub);
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(400, 1, 72, 90)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(480, 1, 72, 0)));
  const uint32_t priorRevision = loop.playbackRevision;
  TEST_ASSERT_EQUAL(CommitResult::Committed,
                    loop.commitCapturePass(CommitReason::OverdubWrap, 480));
  const PassId wrapPassId = loop.lastCommittedPassId();
  PendingNoteChange wrapAdd{};
  wrapAdd.kind = PendingNoteChangeKind::Add;
  wrapAdd.noteId = 10;
  wrapAdd.channel = 1;
  wrapAdd.pitch = 72;
  wrapAdd.velocity = 90;
  wrapAdd.startTick = 400;
  wrapAdd.endTick = 480;
  loop.pushPendingNoteChangeForTest(wrapAdd);
  TEST_ASSERT_TRUE(loop.queueOverdubWrapSnapshotJournal(wrapPassId));
  loop.pushOverdubSessionPass(wrapPassId, {});
  loop.recordLastOverdubSeal(wrapPassId, {}, priorRevision);
  loop.beginCapture(CapturePhase::Overdub);

  size_t steps = 0;
  while (loop.hasPendingWrapJournal() && steps < 10000) {
    loop.stepPendingSnapshotPublish(1, true);
    ++steps;
  }
  TEST_ASSERT_FALSE(loop.hasPendingWrapJournal());
  TEST_ASSERT_TRUE(loop.publishedSnapshotIsCurrent());

  SessionMidiEventVec gathered;
  TEST_ASSERT_TRUE(loop.tryGatherPublishedSnapshotEventsInWindow(gathered, 0, 3072));
  TEST_ASSERT_EQUAL(1, countNoteOns(gathered, 60));
  TEST_ASSERT_EQUAL(1, countNoteOns(gathered, 72));
}

/// The candidate must not be selectable for a revision it does not contain: without
/// the post-seal stamp the wrap journal drains but the snapshot stays unselected.
void test_unstamped_wrap_journal_leaves_snapshot_unselected() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  loop.loopId = 7;
  loop.loopLengthTicks = 3072;
  recordLinearNote(loop, 100, 200, 60);

  loop.openOverdubSession(0);
  loop.beginCapture(CapturePhase::Overdub);
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(400, 1, 72, 90)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(480, 1, 72, 0)));
  TEST_ASSERT_EQUAL(CommitResult::Committed,
                    loop.commitCapturePass(CommitReason::OverdubWrap, 480));
  PendingNoteChange wrapAdd{};
  wrapAdd.kind = PendingNoteChangeKind::Add;
  wrapAdd.noteId = 10;
  wrapAdd.channel = 1;
  wrapAdd.pitch = 72;
  wrapAdd.velocity = 90;
  wrapAdd.startTick = 400;
  wrapAdd.endTick = 480;
  loop.pushPendingNoteChangeForTest(wrapAdd);
  TEST_ASSERT_TRUE(loop.queueOverdubWrapSnapshotJournal(loop.lastCommittedPassId()));
  loop.pushOverdubSessionPass(loop.lastCommittedPassId(), {});
  loop.beginCapture(CapturePhase::Overdub);

  size_t steps = 0;
  while (loop.hasPendingWrapJournal() && steps < 10000) {
    loop.stepPendingSnapshotPublish(1, true);
    ++steps;
  }
  TEST_ASSERT_FALSE(loop.hasPendingWrapJournal());
  TEST_ASSERT_FALSE(loop.publishedSnapshotIsCurrent());
}

/// 150650 ordering: the sealed pass reached neither committed tier when the frame was painted,
/// so the frame fell back to the previous pass. Retained rows must survive the seal and only be
/// released once the representation the painter selects proves coverage.
void test_seal_retains_capture_rows_until_snapshot_covers_revision() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  loop.loopId = 7;
  loop.loopLengthTicks = 1536;
  recordLinearNote(loop, 0, 40, 64);
  TEST_ASSERT_TRUE(loop.publishedSnapshotIsCurrent());

  loop.openOverdubSession(0);
  loop.beginCapture(CapturePhase::Overdub);
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(0, 1, 64, 90)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(624, 1, 64, 0)));
  const uint32_t priorRevision = loop.playbackRevision;
  TEST_ASSERT_EQUAL(CommitResult::Committed,
                    loop.commitCapturePass(CommitReason::OverdubWrap, 624));
  const PassId wrapPassId = loop.lastCommittedPassId();

  // Frame: neither tier carries the seal yet, so the rows must still be available.
  TEST_ASSERT_TRUE(loop.hasSealedCapturePreview());
  TEST_ASSERT_FALSE(loop.publishedSnapshotIsCurrent());
  TEST_ASSERT_FALSE(loop.sealedCapturePreviewIsCovered());
  TEST_ASSERT_EQUAL(1u, loop.sealedCapturePreviewNotes().size());
  TEST_ASSERT_EQUAL(624u, loop.sealedCapturePreviewNotes()[0].endTick);

  // Repeated frames during the seam must not re-copy or re-derive the retained vector.
  const NoteUtils::DisplayNote* retainedData = loop.sealedCapturePreviewNotes().data();
  for (int frame = 0; frame < 4; ++frame) {
    TEST_ASSERT_FALSE(loop.sealedCapturePreviewIsCovered());
    loop.releaseSealedCapturePreviewIfCovered();
  }
  TEST_ASSERT_EQUAL_PTR(retainedData, loop.sealedCapturePreviewNotes().data());
  TEST_ASSERT_EQUAL(1u, loop.sealedCapturePreviewNotes().size());

  PendingNoteChange wrapAdd{};
  wrapAdd.kind = PendingNoteChangeKind::Add;
  wrapAdd.noteId = 21;
  wrapAdd.channel = 1;
  wrapAdd.pitch = 64;
  wrapAdd.velocity = 90;
  wrapAdd.startTick = 0;
  wrapAdd.endTick = 624;
  loop.pushPendingNoteChangeForTest(wrapAdd);
  TEST_ASSERT_TRUE(loop.queueOverdubWrapSnapshotJournal(wrapPassId));
  loop.pushOverdubSessionPass(wrapPassId, {});
  loop.recordLastOverdubSeal(wrapPassId, {}, priorRevision);
  loop.beginCapture(CapturePhase::Overdub);
  // The wrap beginCapture clears capturePreview; retention must outlive it.
  TEST_ASSERT_TRUE(loop.hasSealedCapturePreview());

  size_t steps = 0;
  while (loop.hasPendingWrapJournal() && steps < 10000) {
    loop.stepPendingSnapshotPublish(1, true);
    ++steps;
  }
  TEST_ASSERT_TRUE(loop.publishedSnapshotIsCurrent());

  // A current snapshot is proof on its own: the painter reads snapshot membership for every
  // committed bar, so the still-dirty cache cannot contribute a stale row.
  TEST_ASSERT_TRUE(loop.sealedCapturePreviewIsCovered());
  loop.releaseSealedCapturePreviewIfCovered();
  TEST_ASSERT_FALSE(loop.hasSealedCapturePreview());
}

/// With a stale snapshot the cache is what paints, so only `visualCache.builtFromRevision` can
/// release. A cache one revision behind must keep the retained rows alive.
void test_stale_snapshot_release_requires_visual_cache_coverage() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  loop.loopId = 7;
  loop.loopLengthTicks = 1536;
  recordLinearNote(loop, 0, 40, 64);

  loop.openOverdubSession(0);
  loop.beginCapture(CapturePhase::Overdub);
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(768, 1, 64, 90)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(1392, 1, 64, 0)));
  TEST_ASSERT_EQUAL(CommitResult::Committed,
                    loop.commitCapturePass(CommitReason::OverdubWrap, 1392));
  const uint32_t sealedRevision = loop.playbackRevision;
  TEST_ASSERT_FALSE(loop.publishedSnapshotIsCurrent());
  TEST_ASSERT_TRUE(loop.hasSealedCapturePreview());

  // Cache clean but one revision behind — this is the 150650 frame.
  loop.visualCacheDirty = false;
  loop.visualCache.builtFromRevision = sealedRevision - 1;
  TEST_ASSERT_FALSE(loop.sealedCapturePreviewIsCovered());
  loop.releaseSealedCapturePreviewIfCovered();
  TEST_ASSERT_TRUE(loop.hasSealedCapturePreview());

  // A cache at the sealed revision but still dirty cannot prove loop-wide coverage.
  loop.visualCache.builtFromRevision = sealedRevision;
  loop.visualCacheDirty = true;
  TEST_ASSERT_FALSE(loop.sealedCapturePreviewIsCovered());

  loop.visualCacheDirty = false;
  TEST_ASSERT_TRUE(loop.sealedCapturePreviewIsCovered());
  loop.releaseSealedCapturePreviewIfCovered();
  TEST_ASSERT_FALSE(loop.hasSealedCapturePreview());
}

/// 153517 RC1: OverlayRejected fallback must not stamp builtFromRevision to playback 10
/// while the published snapshot is still 8. Retention stays alive.
void test_overlay_rejected_slice_does_not_claim_playback_coverage() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  loop.loopId = 7;
  loop.loopLengthTicks = 3072;
  recordLinearNote(loop, 0, 40, 64);
  TEST_ASSERT_TRUE(loop.publishedSnapshotIsCurrent());
  const ImmutableLoopSnapshotRef recordSnapshot = loop.publishedSnapshot();
  TEST_ASSERT_TRUE(static_cast<bool>(recordSnapshot));

  loop.openOverdubSession(0);
  loop.beginCapture(CapturePhase::Overdub);
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(2304, 1, 64, 90)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(2784, 1, 64, 0)));
  // commitPendingCapturePass increments then retains, so 9 → sealed revision 10.
  loop.playbackRevision = 9;
  TEST_ASSERT_EQUAL(CommitResult::Committed,
                    loop.commitCapturePass(CommitReason::OverdubWrap, 2784));
  loop.storePublishedSnapshotForTest(recordSnapshot, 8);

  TEST_ASSERT_EQUAL_UINT32(8u, loop.publishedSnapshotRevision());
  TEST_ASSERT_EQUAL_UINT32(10u, loop.playbackRevision);
  TEST_ASSERT_FALSE(loop.publishedSnapshotIsCurrent());
  TEST_ASSERT_TRUE(loop.hasSealedCapturePreview());

  loop.visualCache.builtFromRevision = 8;
  loop.visualCacheDirty = true;
  loop.visualCache.dirtyBars.clear();
  loop.rebuildVisualCacheIdleSlice(4, 0, UINT32_MAX, false, false, 0);

  TEST_ASSERT_TRUE(loop.lastVisualCacheIdleOverlayRejected());
  TEST_ASSERT_NOT_EQUAL(10u, loop.visualCache.builtFromRevision);
  TEST_ASSERT_EQUAL_UINT32(8u, loop.visualCache.builtFromRevision);
  TEST_ASSERT_FALSE(loop.sealedCapturePreviewIsCovered());
  loop.releaseSealedCapturePreviewIfCovered();
  TEST_ASSERT_TRUE(loop.hasSealedCapturePreview());
}

/// RC5b (181116): after OverdubPassAdded undo, a complete Active-only gather must
/// replace the larger pre-undo cache. slice_keep must not retain Disabled-pass notes.
void test_pass_state_refresh_replaces_cache_when_selected_active_gather_is_complete() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  loop.loopId = 7;
  loop.loopLengthTicks = 3072;
  recordLinearNote(loop, 100, 200, 60);
  const ImmutableLoopSnapshotRef recordSnapshot = loop.publishedSnapshot();
  TEST_ASSERT_TRUE(static_cast<bool>(recordSnapshot));
  const uint32_t recordRevision = loop.publishedSnapshotRevision();

  loop.openOverdubSession(0);
  loop.beginCapture(CapturePhase::Overdub);
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(400, 1, 64, 90)));
  const NoteId overdubNoteId = lastCaptureNoteOnId(loop);
  TEST_ASSERT_NOT_EQUAL(kInvalidNoteId, overdubNoteId);
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(480, 1, 64, 0)));
  TEST_ASSERT_EQUAL(CommitResult::Committed,
                    loop.commitCapturePass(CommitReason::OverdubWrap, 480));
  const PassId overdubPassId = loop.lastCommittedPassId();
  loop.storePublishedSnapshotForTest(recordSnapshot, recordRevision);
  loop.rebuildVisualCacheFromPasses();

  TEST_ASSERT_TRUE(loop.visualCache.notes.size() >= 2u);
  TEST_ASSERT_NOT_NULL(findDisplayNoteById(loop.visualCache.notes, overdubNoteId));
  NoteId recordNoteId = kInvalidNoteId;
  for (const NoteUtils::DisplayNote& note : loop.visualCache.notes) {
    if (note.noteId != kInvalidNoteId && note.noteId != overdubNoteId) {
      recordNoteId = note.noteId;
      break;
    }
  }
  TEST_ASSERT_NOT_EQUAL(kInvalidNoteId, recordNoteId);
  TEST_ASSERT_NOT_NULL(findDisplayNoteById(loop.visualCache.notes, recordNoteId));

  TEST_ASSERT_TRUE(loop.setCapturePassState(overdubPassId, CapturePassState::Disabled));

  SessionMidiEventVec selectedFlat;
  loop.gatherCommittedEvents(selectedFlat);
  const NoteUtils::DisplayNoteVec selectedNotes =
      NoteUtils::reconstructDisplayNotes(selectedFlat, loop.loopLengthTicks, false, false);
  TEST_ASSERT_FALSE(selectedNotes.empty());
  TEST_ASSERT_NOT_NULL(findDisplayNoteById(selectedNotes, recordNoteId));
  TEST_ASSERT_NULL(findDisplayNoteById(selectedNotes, overdubNoteId));
  TEST_ASSERT_TRUE(selectedNotes.size() < loop.visualCache.notes.size());

  loop.refreshVisualCacheAfterPassStateChange();
  TEST_ASSERT_FALSE(loop.visualCacheDirty);
  TEST_ASSERT_NOT_EQUAL(static_cast<int>(Loop::VisualCacheIdleGatherSource::None),
                        static_cast<int>(loop.lastVisualCacheIdleGatherSource()));
  TEST_ASSERT_NOT_NULL(findDisplayNoteById(loop.visualCache.notes, recordNoteId));
  TEST_ASSERT_NULL(findDisplayNoteById(loop.visualCache.notes, overdubNoteId));
  TEST_ASSERT_EQUAL(selectedNotes.size(), loop.visualCache.notes.size());
}

/// RC2 pin-first: a later same-pitch NoteOff must not close a prior pass's closed note.
/// Wrap-journal path uses the capture-assigned NoteIds as the match key.
void test_wrap_journal_closed_notes_do_not_pair_across_passes() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  loop.loopId = 7;
  loop.loopLengthTicks = 3072;
  recordLinearNote(loop, 100, 200, 60);

  loop.openOverdubSession(0);
  loop.beginCapture(CapturePhase::Overdub);
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(2304, 1, 64, 90)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(2784, 1, 64, 0)));
  const NoteId idA = lastCaptureNoteOnId(loop);
  TEST_ASSERT_NOT_EQUAL(kInvalidNoteId, idA);
  pushClosedPendingAdd(loop, idA, 64, 2304, 2784);
  commitOverdubWrapLikeDevice(loop, 2784, true);

  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(768, 1, 64, 90)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(864, 1, 64, 0)));
  const NoteId idB = lastCaptureNoteOnId(loop);
  TEST_ASSERT_NOT_EQUAL(idA, idB);
  pushClosedPendingAdd(loop, idB, 64, 768, 864);
  commitOverdubWrapLikeDevice(loop, 864, true);

  drainPendingSnapshotPublish(loop);
  TEST_ASSERT_TRUE(loop.tryPublishSessionExitSnapshot(CommitReason::OverdubStop));
  assertClosedNotesDidNotPairAcrossPasses(loop, idA, idB, 2784, 864);
}

/// RC2 pin-first: session-exit pass-change must use the same NoteId key. No pending Adds.
const LoopSnapshotNote* findSnapshotNote(const LoopSnapshot& snapshot, NoteId noteId,
                                         uint8_t pitch) {
  const LoopSnapshotNoteRef note = snapshot.findNote(noteId, pitch);
  return note ? note.get() : nullptr;
}

void assertClosedNoteKeepsLinearEnd(const Loop& loop, NoteId noteId, uint8_t pitch,
                                    uint32_t startTick, uint32_t endTick) {
  const auto snapshot = loop.publishedSnapshot();
  TEST_ASSERT_TRUE(static_cast<bool>(snapshot));
  const LoopSnapshotNote* snapNote = findSnapshotNote(*snapshot, noteId, pitch);
  TEST_ASSERT_NOT_NULL(snapNote);
  TEST_ASSERT_EQUAL_UINT32(startTick, snapNote->startTick);
  TEST_ASSERT_EQUAL_UINT32(endTick, snapNote->endTick);
  TEST_ASSERT_FALSE(snapNote->isWrap(snapshot->loopLengthTicks()));

  SessionMidiEventVec gathered;
  TEST_ASSERT_TRUE(loop.tryGatherPublishedSnapshotEventsInWindow(gathered, 0, 3072));
  uint32_t gatheredOff = 0;
  for (const MidiEvent& evt : gathered) {
    if (evt.noteId == noteId && evt.isNoteOff() && evt.data.noteData.note == pitch) {
      gatheredOff = evt.tick;
    }
  }
  TEST_ASSERT_EQUAL_UINT32(endTick, gatheredOff);

  const NoteUtils::DisplayNoteVec reconstructed =
      NoteUtils::reconstructDisplayNotes(gathered, 3072, false, false);
  const NoteUtils::DisplayNote* reconstructedNote = findDisplayNoteById(reconstructed, noteId);
  TEST_ASSERT_NOT_NULL(reconstructedNote);
  TEST_ASSERT_EQUAL_UINT32(startTick, reconstructedNote->startTick);
  TEST_ASSERT_EQUAL_UINT32(endTick, reconstructedNote->endTick);
  TEST_ASSERT_FALSE(reconstructedNote->endTick < reconstructedNote->startTick);

  const NoteUtils::DisplayNoteVec wrapPaired =
      NoteUtils::reconstructDisplayNotes(gathered, 3072, false, false, true);
  const NoteUtils::DisplayNote* wrapPairedNote = findDisplayNoteById(wrapPaired, noteId);
  TEST_ASSERT_NOT_NULL(wrapPairedNote);
  TEST_ASSERT_EQUAL_UINT32(startTick, wrapPairedNote->startTick);
  TEST_ASSERT_EQUAL_UINT32(endTick, wrapPairedNote->endTick);
  TEST_ASSERT_FALSE(wrapPairedNote->endTick < wrapPairedNote->startTick);
}

MidiEvent noteOffWithNoteId(uint32_t tick, uint8_t channel, uint8_t pitch, NoteId noteId) {
  MidiEvent evt = MidiEvent::NoteOff(tick, channel, pitch, 0);
  evt.noteId = noteId;
  return evt;
}

void applyEventsToPreview(CapturePreview& preview, const MidiEventVec& events,
                          uint32_t loopLength) {
  for (const MidiEvent& evt : events) {
    applyCaptureEventToPreview(preview, evt, Config::TICKS_PER_BAR, loopLength);
  }
}

void assertSamePassClosedNoteKeepsLinearEnd(const NoteUtils::DisplayNoteVec& notes, NoteId idA,
                                            uint32_t closedEnd, uint32_t forbiddenSiblingEnd,
                                            uint32_t loopLength) {
  const NoteUtils::DisplayNote* noteA = findDisplayNoteById(notes, idA);
  TEST_ASSERT_NOT_NULL(noteA);
  TEST_ASSERT_EQUAL_UINT32(2304u, noteA->startTick);
  TEST_ASSERT_EQUAL_UINT32(closedEnd, noteA->endTick);
  TEST_ASSERT_NOT_EQUAL(forbiddenSiblingEnd, noteA->endTick);
  TEST_ASSERT_NOT_EQUAL(loopLength - 1u, noteA->endTick);
  TEST_ASSERT_FALSE(noteA->endTick < noteA->startTick);
}

NoteUtils::DisplayNote displayNoteFromSnapshotNote(const LoopSnapshotNote& note) {
  NoteUtils::DisplayNote out{};
  out.noteId = note.noteId;
  out.note = note.pitch;
  out.velocity = note.velocity;
  out.startTick = note.startTick;
  out.endTick = note.endTick;
  return out;
}

void appendDisplayNoteUniqueById(NoteUtils::DisplayNoteVec& out, const NoteUtils::DisplayNote& note) {
  if (note.noteId != kInvalidNoteId) {
    for (const auto& existing : out) {
      if (existing.noteId == note.noteId) {
        return;
      }
    }
  }
  out.push_back(note);
}

void upsertDisplayNoteAtPitchStartFrom(NoteUtils::DisplayNoteVec& notes, size_t suffixBegin,
                                       const NoteUtils::DisplayNote& row) {
  if (row.endTick >= row.startTick) {
    for (size_t i = suffixBegin; i < notes.size(); ++i) {
      if (notes[i].note == row.note && notes[i].startTick == row.startTick) {
        notes[i] = row;
        return;
      }
    }
  }
  notes.push_back(row);
}

bool previewNoteIntersectsBar(const NoteUtils::DisplayNote& note, uint32_t bar,
                              uint32_t loopLength) {
  const uint32_t barStart = bar * Config::TICKS_PER_BAR;
  return DisplayWindowUtils::noteIntersectsWindow(note.startTick, note.endTick, barStart,
                                                  Config::TICKS_PER_BAR, loopLength);
}

void appendSnapshotNotesForBar(const LoopSnapshot& snapshot, uint32_t bar, uint8_t pitchLow,
                               uint8_t pitchHigh, NoteUtils::DisplayNoteVec& out) {
  const uint8_t lastPitch =
      pitchHigh < kLoopSnapshotPitchCount ? pitchHigh
                                          : static_cast<uint8_t>(kLoopSnapshotPitchCount - 1);
  for (uint16_t pitch = pitchLow; pitch <= lastPitch; ++pitch) {
    const auto membership = snapshot.noteMembership(static_cast<uint8_t>(pitch), bar);
    forEachSnapshotMembershipReference(membership.get(), [&out](const LoopSnapshotNoteRef& reference) {
      if (!reference) {
        return;
      }
      appendDisplayNoteUniqueById(out, displayNoteFromSnapshotNote(*reference));
    });
  }
}

void appendNotesIntersectingBar(const NoteUtils::DisplayNoteVec& source, uint32_t bar,
                                uint32_t loopLength, NoteUtils::DisplayNoteVec& out) {
  for (const NoteUtils::DisplayNote& note : source) {
    if (previewNoteIntersectsBar(note, bar, loopLength)) {
      appendDisplayNoteUniqueById(out, note);
    }
  }
}

/// Same committed+capture bar union as `DisplayManager::resolveDisplayNotesByDrawSource`
/// before playhead tails. Host-reachable without Track/DisplayManager.
void gatherDrawSourceDisplayNotes(const Loop& loop, uint32_t firstBar, uint32_t lastBar,
                                  uint32_t playheadBar, uint8_t pitchLow, uint8_t pitchHigh,
                                  NoteUtils::DisplayNoteVec& out, size_t* committedEnd = nullptr) {
  out.clear();
  const uint32_t loopLength = loop.loopLengthTicks;
  if (loopLength == 0 || firstBar > lastBar) {
    if (committedEnd != nullptr) {
      *committedEnd = 0;
    }
    return;
  }
  const ImmutableLoopSnapshotRef snapshot = loop.publishedSnapshot();
  const bool captureActive = loop.captureActive();
  const bool usePublishedSnapshot = DisplayWindowUtils::shouldGatherPublishedSnapshotForDrawSource(
      static_cast<bool>(snapshot), captureActive, !loop.visualCache.notes.empty());
  const uint8_t resolvedPitchHigh = pitchHigh < pitchLow ? pitchLow : pitchHigh;

  for (uint32_t bar = firstBar; bar <= lastBar; ++bar) {
    if (usePublishedSnapshot) {
      appendSnapshotNotesForBar(*snapshot, bar, pitchLow, resolvedPitchHigh, out);
    } else {
      appendNotesIntersectingBar(loop.visualCache.notes, bar, loopLength, out);
    }
  }
  const size_t prefixEnd = out.size();
  if (committedEnd != nullptr) {
    *committedEnd = prefixEnd;
  }
  if (loop.hasSealedCapturePreview() && !loop.sealedCapturePreviewIsCovered()) {
    for (const NoteUtils::DisplayNote& sealedRow : loop.sealedCapturePreviewNotes()) {
      for (uint32_t bar = firstBar; bar <= lastBar; ++bar) {
        if (!previewNoteIntersectsBar(sealedRow, bar, loopLength)) {
          continue;
        }
        upsertDisplayNoteAtPitchStartFrom(out, prefixEnd, sealedRow);
        break;
      }
    }
  }
  if (captureActive) {
    for (const NoteUtils::DisplayNote& previewNote : loop.capturePreview.notes) {
      bool intersectsWindow = false;
      for (uint32_t bar = firstBar; bar <= lastBar; ++bar) {
        if (previewNoteIntersectsBar(previewNote, bar, loopLength)) {
          intersectsWindow = true;
          break;
        }
      }
      if (!intersectsWindow) {
        continue;
      }
      if (DisplayWindowUtils::capturePreviewNoteIsOpenHold(previewNote.startTick,
                                                           previewNote.endTick)) {
        appendDisplayNoteUniqueById(out, previewNote);
      } else {
        upsertDisplayNoteAtPitchStartFrom(out, prefixEnd, previewNote);
      }
    }
  }
  (void)playheadBar;
}

uint32_t clampOpenNoteCloseTick(uint32_t closeTick, uint32_t loopLength) {
  if (loopLength == 0) {
    return 0;
  }
  if (closeTick >= loopLength) {
    return loopLength - 1;
  }
  return closeTick;
}

bool updateDisplayNoteEndFrom(NoteUtils::DisplayNoteVec& notes, size_t regionStart, uint8_t pitch,
                              uint32_t startTick, uint32_t endTick) {
  for (size_t i = regionStart; i < notes.size(); ++i) {
    if (notes[i].note == pitch && notes[i].startTick == startTick) {
      notes[i].endTick = endTick;
      return true;
    }
  }
  return false;
}

void appendWrapHeldOpenNoteDisplay(NoteUtils::DisplayNoteVec& notes, size_t regionStart,
                                   const NoteUtils::OpenNoteOn& open, uint32_t tailEnd,
                                   const NoteUtils::WrapHeadSegment& headSegment) {
  if (!updateDisplayNoteEndFrom(notes, regionStart, open.note, open.tick, tailEnd)) {
    NoteUtils::DisplayNote tailSeg;
    tailSeg.note = open.note;
    tailSeg.velocity = open.velocity;
    tailSeg.startTick = open.tick;
    tailSeg.endTick = tailEnd;
    notes.push_back(tailSeg);
  }
  if (!headSegment.visible) {
    return;
  }
  NoteUtils::DisplayNote headSeg;
  headSeg.note = open.note;
  headSeg.velocity = open.velocity;
  headSeg.startTick = headSegment.startTick;
  headSeg.endTick = headSegment.endTickInclusive;
  notes.push_back(headSeg);
}

NoteUtils::WrapHeadSegment resolveWrapOpenHeadSegment(uint32_t loopLength,
                                                      const NoteUtils::OpenNoteOn& open,
                                                      uint32_t closeTick, bool hasCommittedHeadOff,
                                                      uint32_t headOffTick) {
  if (hasCommittedHeadOff) {
    return NoteUtils::resolveWrapHeadSegment(
        loopLength, headOffTick, NoteUtils::WrapHeadSegmentContext::CommittedHeadOff);
  }
  if (closeTick < open.tick) {
    return NoteUtils::resolveWrapHeadSegment(loopLength, closeTick,
                                             NoteUtils::WrapHeadSegmentContext::LivePlayhead, 768,
                                             open.tick);
  }
  return {};
}

bool shouldSplitLiveWrapOpenNoteDisplay(const NoteUtils::OpenNoteOn& open, uint32_t loopLength,
                                        uint32_t closeTick, bool extendHeldNotesToPlayhead,
                                        bool isWrapHeld) {
  return isWrapHeld ||
         NoteUtils::isLiveWrapHeadContinuationDisplay(open.tick, closeTick, loopLength,
                                                      extendHeldNotesToPlayhead);
}

/// Same body as `DisplayManagerInternal::applyCapturePlayheadTails`. Overdub paint passes
/// `allowWrapContinuation=false`.
void applyCapturePlayheadTails(const CapturePreview& preview, uint32_t loopLength,
                               uint32_t closeTick, size_t captureRegionStart,
                               NoteUtils::DisplayNoteVec& notes, bool allowWrapContinuation) {
  const uint32_t clampedCloseTick = clampOpenNoteCloseTick(closeTick, loopLength);
  for (const uint32_t previewNoteIndex : preview.openNoteIndices) {
    if (previewNoteIndex >= preview.notes.size() ||
        previewNoteIndex >= preview.noteStates.size()) {
      continue;
    }
    const NoteUtils::DisplayNote& previewNote = preview.notes[previewNoteIndex];
    const CapturePreviewNoteState& state = preview.noteStates[previewNoteIndex];
    if (!state.open) {
      continue;
    }
    const NoteUtils::OpenNoteOn open{previewNote.note, previewNote.velocity, previewNote.startTick};
    const bool wrapHeldForSplit = allowWrapContinuation && state.wrapHeld;
    if (allowWrapContinuation &&
        shouldSplitLiveWrapOpenNoteDisplay(open, loopLength, clampedCloseTick, true,
                                           wrapHeldForSplit)) {
      uint32_t tailEnd = loopLength - 1;
      if (clampedCloseTick >= open.tick) {
        tailEnd = std::min(clampedCloseTick, loopLength - 1);
      }
      const NoteUtils::WrapHeadSegment head = resolveWrapOpenHeadSegment(
          loopLength, open, clampedCloseTick, state.hasPreferredHeadOff, state.preferredHeadOffTick);
      appendWrapHeldOpenNoteDisplay(notes, captureRegionStart, open, tailEnd, head);
      continue;
    }
    uint32_t noteStartTick = open.tick;
    uint32_t playheadEndTick = std::max(noteStartTick, clampedCloseTick);
    DisplayWindowUtils::clampNonWrapDisplayNoteBarTicks(noteStartTick, playheadEndTick, loopLength);
    bool updated = false;
    for (size_t i = captureRegionStart; i < notes.size(); ++i) {
      if (notes[i].note == open.note && notes[i].startTick == open.tick) {
        notes[i].startTick = noteStartTick;
        notes[i].endTick = playheadEndTick;
        updated = true;
        break;
      }
    }
    if (!updated) {
      NoteUtils::DisplayNote liveNote;
      liveNote.note = open.note;
      liveNote.velocity = open.velocity;
      liveNote.startTick = noteStartTick;
      liveNote.endTick = playheadEndTick;
      notes.push_back(liveNote);
    }
  }
}

/// RC2b (181116 / 190847): same-pass Off closes A. Wrap-head reconstruction must not
/// rewrite that NoteId to the bar-1 sibling Off or loopLength-1. Open C may be wrap-held.
void test_same_pass_closed_note_keeps_linear_end_beside_bar_one_sibling() {
  constexpr uint32_t kLoopLength = 3072;
  const NoteId idA = 74;
  const NoteId idB = 79;
  const NoteId idC = 85;
  MidiEventVec events;
  events.push_back(NoteIdTestFixtures::noteOnWithNoteId(2304, 1, 64, 90, idA));
  events.push_back(noteOffWithNoteId(2880, 1, 64, idA));
  events.push_back(NoteIdTestFixtures::noteOnWithNoteId(768, 1, 64, 90, idB));
  events.push_back(noteOffWithNoteId(1008, 1, 64, idB));
  events.push_back(NoteIdTestFixtures::noteOnWithNoteId(2304, 1, 64, 90, idC));

  CapturePreview preview;
  applyEventsToPreview(preview, events, kLoopLength);
  bool previewKeptA = false;
  for (const NoteUtils::DisplayNote& note : preview.notes) {
    if (note.note != 64 || note.startTick != 2304u) {
      continue;
    }
    if (note.endTick == 2880u) {
      previewKeptA = true;
    }
    TEST_ASSERT_NOT_EQUAL(1008u, note.endTick);
    TEST_ASSERT_NOT_EQUAL(kLoopLength - 1u, note.endTick);
  }
  TEST_ASSERT_TRUE(previewKeptA);

  const NoteUtils::DisplayNoteVec reconstructed =
      NoteUtils::reconstructDisplayNotes(events, kLoopLength, false, false, true);
  assertSamePassClosedNoteKeepsLinearEnd(reconstructed, idA, 2880u, 1008u, kLoopLength);
  const NoteUtils::DisplayNote* noteC = findDisplayNoteById(reconstructed, idC);
  if (noteC != nullptr) {
    TEST_ASSERT_EQUAL_UINT32(2304u, noteC->startTick);
  }
}

/// RC2b device ticks from 190847: compose 2304→2976, drawn 2304→576.
void test_same_pass_closed_bar_four_keeps_end_beside_hold_head() {
  constexpr uint32_t kLoopLength = 3072;
  const NoteId idA = 77;
  const NoteId idB = 79;
  const NoteId idC = 85;
  MidiEventVec events;
  events.push_back(NoteIdTestFixtures::noteOnWithNoteId(2304, 1, 64, 90, idA));
  events.push_back(noteOffWithNoteId(2976, 1, 64, idA));
  events.push_back(NoteIdTestFixtures::noteOnWithNoteId(0, 1, 64, 90, idB));
  events.push_back(noteOffWithNoteId(576, 1, 64, idB));
  events.push_back(NoteIdTestFixtures::noteOnWithNoteId(2304, 1, 64, 90, idC));

  CapturePreview preview;
  applyEventsToPreview(preview, events, kLoopLength);
  bool previewKeptA = false;
  for (const NoteUtils::DisplayNote& note : preview.notes) {
    if (note.note != 64 || note.startTick != 2304u) {
      continue;
    }
    if (note.endTick == 2976u) {
      previewKeptA = true;
    }
    TEST_ASSERT_NOT_EQUAL(576u, note.endTick);
    TEST_ASSERT_NOT_EQUAL(kLoopLength - 1u, note.endTick);
  }
  TEST_ASSERT_TRUE(previewKeptA);

  const NoteUtils::DisplayNoteVec reconstructed =
      NoteUtils::reconstructDisplayNotes(events, kLoopLength, false, false, true);
  assertSamePassClosedNoteKeepsLinearEnd(reconstructed, idA, 2976u, 576u, kLoopLength);
  const NoteUtils::DisplayNote* noteC = findDisplayNoteById(reconstructed, idC);
  if (noteC != nullptr) {
    TEST_ASSERT_EQUAL_UINT32(2304u, noteC->startTick);
  }
}

/// RC2b (190847): one wrap pass contains bar-4 close and bar-1 sibling. Snapshot paint
/// reads membership; compose reconstruct does not. A must stay 2304→2976 in the snapshot.
void test_same_pass_snapshot_keeps_closed_bar_four_end() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  loop.loopId = 7;
  loop.loopLengthTicks = 3072;
  recordLinearNote(loop, 100, 200, 60);

  loop.openOverdubSession(0);
  loop.beginCapture(CapturePhase::Overdub);
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(2304, 1, 64, 90)));
  const NoteId idA = lastCaptureNoteOnId(loop);
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(2976, 1, 64, 0)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(0, 1, 64, 90)));
  const NoteId idB = lastCaptureNoteOnId(loop);
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(576, 1, 64, 0)));
  TEST_ASSERT_NOT_EQUAL(kInvalidNoteId, idA);
  TEST_ASSERT_NOT_EQUAL(idA, idB);
  pushClosedPendingAdd(loop, idA, 64, 2304, 2976);
  pushClosedPendingAdd(loop, idB, 64, 0, 576);
  commitOverdubWrapLikeDevice(loop, 2976, true);
  drainPendingSnapshotPublish(loop);

  const auto snapshot = loop.publishedSnapshot();
  TEST_ASSERT_TRUE(static_cast<bool>(snapshot));
  const LoopSnapshotNoteRef noteA = snapshot->findNote(idA, 64);
  TEST_ASSERT_TRUE(static_cast<bool>(noteA));
  TEST_ASSERT_EQUAL_UINT32(2304u, noteA->startTick);
  TEST_ASSERT_EQUAL_UINT32(2976u, noteA->endTick);
  TEST_ASSERT_FALSE(noteA->isWrap(3072));
  TEST_ASSERT_NOT_EQUAL(576u, noteA->endTick);
  TEST_ASSERT_NOT_EQUAL(3071u, noteA->endTick);
}

/// RC2b (190847): `resolveDisplayNotesByDrawSource` paints `id 77` as `2304→576`
/// while compose keeps `2304→2976`. This pin runs that gather's committed+capture
/// union on the same same-pass wrap. A must stay linear; no row of that NoteId
/// may become the bar-1 Off or `loopLength-1`.
void test_draw_source_upserts_closed_capture_preview_on_already_played_bar() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  loop.loopId = 7;
  loop.loopLengthTicks = 3072;
  recordLinearNote(loop, 100, 200, 60);
  TEST_ASSERT_TRUE(loop.publishedSnapshotIsCurrent());
  const auto membership = loop.publishedSnapshot()->noteMembership(60, 0);
  TEST_ASSERT_NOT_NULL(membership.get());
  TEST_ASSERT_TRUE(snapshotMembershipCount(membership.get()) > 0);
  const NoteId recordId = membership->references[0]->noteId;
  const size_t visualBefore = loop.visualCache.notes.size();

  loop.openOverdubSession(0);
  loop.beginCapture(CapturePhase::Overdub);
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(100, 1, 60, 90)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(400, 1, 60, 0)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(300, 1, 72, 90)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(420, 1, 72, 0)));

  NoteUtils::DisplayNoteVec drawn;
  size_t committedEnd = 0;
  gatherDrawSourceDisplayNotes(loop, 0, 3, 2, 0, 127, drawn, &committedEnd);
  bool closedAfterPrefix = false;
  for (size_t i = 0; i < drawn.size(); ++i) {
    if (drawn[i].note == 60 && drawn[i].startTick == 100 && drawn[i].endTick == 400) {
      TEST_ASSERT_TRUE(i >= committedEnd);
      closedAfterPrefix = true;
    }
  }
  TEST_ASSERT_TRUE(closedAfterPrefix);

  NoteUtils::DisplayNoteVec afterHide;
  for (size_t i = 0; i < drawn.size(); ++i) {
    if (i < committedEnd && drawn[i].noteId == recordId) {
      continue;
    }
    afterHide.push_back(drawn[i]);
  }

  uint32_t pitch60At100 = 0;
  uint32_t pitch60End = 0;
  bool found72 = false;
  bool recordRemains = false;
  for (const NoteUtils::DisplayNote& note : afterHide) {
    if (note.note == 60 && note.startTick == 100) {
      ++pitch60At100;
      pitch60End = note.endTick;
    }
    if (note.note == 72 && note.startTick == 300 && note.endTick == 420) {
      found72 = true;
    }
    if (note.noteId == recordId && note.endTick == 200) {
      recordRemains = true;
    }
  }
  TEST_ASSERT_EQUAL_UINT32(1u, pitch60At100);
  TEST_ASSERT_EQUAL_UINT32(400u, pitch60End);
  TEST_ASSERT_TRUE(found72);
  TEST_ASSERT_FALSE(recordRemains);
  TEST_ASSERT_EQUAL(visualBefore, loop.visualCache.notes.size());
}

/// [`001647`] occupy Hide of 77@336→384. Open hold at 336 must grow to playhead 360,
/// not stay end==start and not keep the record 384.
void test_draw_source_open_hold_grows_to_playhead_after_occupy_hide() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  loop.loopId = 7;
  loop.loopLengthTicks = 3072;
  recordLinearNote(loop, 336, 384, 77);
  TEST_ASSERT_TRUE(loop.publishedSnapshotIsCurrent());
  const auto membership = loop.publishedSnapshot()->noteMembership(77, 0);
  TEST_ASSERT_NOT_NULL(membership.get());
  TEST_ASSERT_TRUE(snapshotMembershipCount(membership.get()) > 0);
  const NoteId recordId = membership->references[0]->noteId;
  TEST_ASSERT_NOT_EQUAL(kInvalidNoteId, recordId);
  TEST_ASSERT_EQUAL_UINT32(336u, membership->references[0]->startTick);
  TEST_ASSERT_EQUAL_UINT32(384u, membership->references[0]->endTick);

  loop.openOverdubSession(0);
  loop.beginCapture(CapturePhase::Overdub);
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(336, 1, 77, 90)));
  TEST_ASSERT_FALSE(loop.capturePreview.openNoteIndices.empty());

  NoteUtils::DisplayNoteVec drawn;
  size_t committedEnd = 0;
  gatherDrawSourceDisplayNotes(loop, 0, 3, 0, 0, 127, drawn, &committedEnd);
  TEST_ASSERT_TRUE(committedEnd < drawn.size());
  // This suite stubs Loop::applyPendingNoteChangesToDisplayNotes. Occupy Hide of the
  // record prefix is applied here the same way the device prefix walk does.
  NoteUtils::DisplayNoteVec afterHide;
  afterHide.reserve(drawn.size());
  size_t hiddenPrefix = 0;
  for (size_t i = 0; i < drawn.size(); ++i) {
    if (i < committedEnd && drawn[i].noteId == recordId) {
      ++hiddenPrefix;
      continue;
    }
    afterHide.push_back(drawn[i]);
  }
  TEST_ASSERT_EQUAL_UINT32(1u, static_cast<uint32_t>(hiddenPrefix));
  committedEnd -= hiddenPrefix;
  drawn.swap(afterHide);
  applyCapturePlayheadTails(loop.capturePreview, 3072u, 360u, committedEnd, drawn, false);

  uint32_t holdEnd = 0;
  uint32_t holdCount = 0;
  bool recordRemains = false;
  bool zeroLengthHold = false;
  for (const NoteUtils::DisplayNote& note : drawn) {
    if (note.note != 77 || note.startTick != 336) {
      continue;
    }
    if (note.noteId == recordId && note.endTick == 384) {
      recordRemains = true;
    }
    if (note.endTick == 336) {
      zeroLengthHold = true;
    }
    if (note.endTick == 360) {
      ++holdCount;
      holdEnd = note.endTick;
    }
  }
  TEST_ASSERT_FALSE(recordRemains);
  TEST_ASSERT_FALSE(zeroLengthHold);
  TEST_ASSERT_EQUAL_UINT32(1u, holdCount);
  TEST_ASSERT_EQUAL_UINT32(360u, holdEnd);
}

void test_draw_source_unions_sealed_preview_after_wrap_before_snapshot() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  loop.loopId = 7;
  loop.loopLengthTicks = 3072;
  recordLinearNote(loop, 100, 200, 60);
  const size_t visualBefore = loop.visualCache.notes.size();

  loop.openOverdubSession(0);
  loop.beginCapture(CapturePhase::Overdub);
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(768, 1, 64, 90)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(900, 1, 64, 0)));
  TEST_ASSERT_EQUAL(CommitResult::Committed,
                    loop.commitCapturePass(CommitReason::OverdubWrap, 900));
  loop.beginCapture(CapturePhase::Overdub);

  TEST_ASSERT_TRUE(loop.hasSealedCapturePreview());
  TEST_ASSERT_FALSE(loop.publishedSnapshotIsCurrent());
  TEST_ASSERT_FALSE(loop.sealedCapturePreviewIsCovered());

  NoteUtils::DisplayNoteVec drawn;
  gatherDrawSourceDisplayNotes(loop, 0, 3, 0, 0, 127, drawn);
  bool foundSealed = false;
  for (const NoteUtils::DisplayNote& note : drawn) {
    if (note.note == 64 && note.startTick == 768 && note.endTick == 900) {
      foundSealed = true;
    }
  }
  TEST_ASSERT_TRUE(foundSealed);
  TEST_ASSERT_EQUAL(visualBefore, loop.visualCache.notes.size());
}

/// [`003558`] wrap 2/3: sealed sits after committedEnd so prefix Hide cannot drop it.
void test_draw_source_sealed_row_survives_prefix_hide() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  loop.loopId = 7;
  loop.loopLengthTicks = 3072;
  recordLinearNote(loop, 768, 960, 64);
  TEST_ASSERT_TRUE(loop.publishedSnapshotIsCurrent());
  const auto membership = loop.publishedSnapshot()->noteMembership(64, 1);
  TEST_ASSERT_NOT_NULL(membership.get());
  TEST_ASSERT_TRUE(snapshotMembershipCount(membership.get()) > 0);
  const NoteId recordId = membership->references[0]->noteId;

  loop.openOverdubSession(0);
  loop.beginCapture(CapturePhase::Overdub);
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(768, 1, 64, 90)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(900, 1, 64, 0)));
  TEST_ASSERT_EQUAL(CommitResult::Committed,
                    loop.commitCapturePass(CommitReason::OverdubWrap, 900));
  loop.beginCapture(CapturePhase::Overdub);
  TEST_ASSERT_TRUE(loop.hasSealedCapturePreview());
  TEST_ASSERT_FALSE(loop.sealedCapturePreviewIsCovered());

  NoteUtils::DisplayNoteVec drawn;
  size_t committedEnd = 0;
  gatherDrawSourceDisplayNotes(loop, 0, 3, 0, 0, 127, drawn, &committedEnd);
  bool sealedAfterPrefix = false;
  for (size_t i = 0; i < drawn.size(); ++i) {
    if (drawn[i].note == 64 && drawn[i].startTick == 768 && drawn[i].endTick == 900) {
      TEST_ASSERT_TRUE(i >= committedEnd);
      sealedAfterPrefix = true;
    }
  }
  TEST_ASSERT_TRUE(sealedAfterPrefix);

  NoteUtils::DisplayNoteVec afterHide;
  for (size_t i = 0; i < drawn.size(); ++i) {
    if (i < committedEnd && drawn[i].noteId == recordId) {
      continue;
    }
    afterHide.push_back(drawn[i]);
  }
  bool sealedRemains = false;
  bool recordRemains = false;
  for (const NoteUtils::DisplayNote& note : afterHide) {
    if (note.note == 64 && note.startTick == 768 && note.endTick == 900) {
      sealedRemains = true;
    }
    if (note.noteId == recordId && note.endTick == 960) {
      recordRemains = true;
    }
  }
  TEST_ASSERT_TRUE(sealedRemains);
  TEST_ASSERT_FALSE(recordRemains);
}

/// [`004351`] wrap 2: last published snapshot still paints after snap goes stale.
void test_draw_source_keeps_stale_snapshot_after_second_wrap() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  loop.loopId = 7;
  loop.loopLengthTicks = 3072;
  recordLinearNote(loop, 100, 200, 60);
  TEST_ASSERT_TRUE(loop.publishedSnapshotIsCurrent());
  const size_t visualBefore = loop.visualCache.notes.size();

  loop.openOverdubSession(0);
  loop.beginCapture(CapturePhase::Overdub);
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(400, 1, 72, 90)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(480, 1, 72, 0)));
  const uint32_t priorRevision = loop.playbackRevision;
  TEST_ASSERT_EQUAL(CommitResult::Committed,
                    loop.commitCapturePass(CommitReason::OverdubWrap, 480));
  const PassId wrapPassId = loop.lastCommittedPassId();
  PendingNoteChange wrapAdd{};
  wrapAdd.kind = PendingNoteChangeKind::Add;
  wrapAdd.noteId = 10;
  wrapAdd.channel = 1;
  wrapAdd.pitch = 72;
  wrapAdd.velocity = 90;
  wrapAdd.startTick = 400;
  wrapAdd.endTick = 480;
  loop.pushPendingNoteChangeForTest(wrapAdd);
  TEST_ASSERT_TRUE(loop.queueOverdubWrapSnapshotJournal(wrapPassId));
  loop.pushOverdubSessionPass(wrapPassId, {});
  loop.recordLastOverdubSeal(wrapPassId, {}, priorRevision);
  loop.beginCapture(CapturePhase::Overdub);

  size_t steps = 0;
  while (loop.hasPendingWrapJournal() && steps < 10000) {
    loop.stepPendingSnapshotPublish(1, true);
    ++steps;
  }
  TEST_ASSERT_FALSE(loop.hasPendingWrapJournal());
  TEST_ASSERT_TRUE(loop.publishedSnapshotIsCurrent());
  const LoopSnapshotNoteRef wrapOne = loop.publishedSnapshot()->findNote(10, 72);
  TEST_ASSERT_TRUE(static_cast<bool>(wrapOne));
  TEST_ASSERT_EQUAL_UINT32(400u, wrapOne->startTick);
  TEST_ASSERT_EQUAL_UINT32(480u, wrapOne->endTick);

  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(768, 1, 64, 90)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(900, 1, 64, 0)));
  TEST_ASSERT_EQUAL(CommitResult::Committed,
                    loop.commitCapturePass(CommitReason::OverdubWrap, 900));
  loop.beginCapture(CapturePhase::Overdub);
  TEST_ASSERT_FALSE(loop.publishedSnapshotIsCurrent());
  TEST_ASSERT_TRUE(loop.hasSealedCapturePreview());

  NoteUtils::DisplayNoteVec drawn;
  gatherDrawSourceDisplayNotes(loop, 0, 3, 0, 0, 127, drawn);
  bool foundWrapOne = false;
  bool foundSealedLapTwo = false;
  for (const NoteUtils::DisplayNote& note : drawn) {
    if (note.note == 72 && note.startTick == 400 && note.endTick == 480) {
      foundWrapOne = true;
    }
    if (note.note == 64 && note.startTick == 768 && note.endTick == 900) {
      foundSealedLapTwo = true;
    }
  }
  TEST_ASSERT_TRUE(foundWrapOne);
  TEST_ASSERT_TRUE(foundSealedLapTwo);
  TEST_ASSERT_EQUAL(visualBefore, loop.visualCache.notes.size());
}

/// [`010217`] after stop: successor snapshot wrap-shaped 2640→0 must not replace
/// adopt_partial linear 2640→2784/2792.
void test_draw_source_after_stop_keeps_adopted_linear_ends() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  loop.loopId = 7;
  loop.loopLengthTicks = 3072;
  recordLinearNote(loop, 100, 200, 60);

  loop.openOverdubSession(0);
  loop.beginCapture(CapturePhase::Overdub);
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(400, 1, 72, 90)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(480, 1, 72, 0)));
  const uint32_t priorRevision = loop.playbackRevision;
  TEST_ASSERT_EQUAL(CommitResult::Committed,
                    loop.commitCapturePass(CommitReason::OverdubWrap, 480));
  const PassId wrapPassId = loop.lastCommittedPassId();
  const struct {
    NoteId noteId;
    uint8_t pitch;
    uint32_t linearEnd;
  } chord[] = {{10, 45, 2792}, {11, 51, 2792}, {12, 39, 2784}};
  for (const auto& row : chord) {
    PendingNoteChange wrapAdd{};
    wrapAdd.kind = PendingNoteChangeKind::Add;
    wrapAdd.noteId = row.noteId;
    wrapAdd.channel = 1;
    wrapAdd.pitch = row.pitch;
    wrapAdd.velocity = 90;
    wrapAdd.startTick = 2640;
    wrapAdd.endTick = 0;
    loop.pushPendingNoteChangeForTest(wrapAdd);
  }
  TEST_ASSERT_TRUE(loop.queueOverdubWrapSnapshotJournal(wrapPassId));
  loop.clearPendingNoteChanges();
  loop.pushOverdubSessionPass(wrapPassId, {});
  loop.recordLastOverdubSeal(wrapPassId, {}, priorRevision);
  TEST_ASSERT_FALSE(loop.captureActive());

  size_t steps = 0;
  while (loop.hasPendingWrapJournal() && steps < 10000) {
    loop.stepPendingSnapshotPublish(1, true);
    ++steps;
  }
  TEST_ASSERT_FALSE(loop.hasPendingWrapJournal());
  TEST_ASSERT_TRUE(static_cast<bool>(loop.publishedSnapshot()));
  for (const auto& row : chord) {
    const LoopSnapshotNoteRef snapNote = loop.publishedSnapshot()->findNote(row.noteId, row.pitch);
    TEST_ASSERT_TRUE(static_cast<bool>(snapNote));
    TEST_ASSERT_EQUAL_UINT32(2640u, snapNote->startTick);
    TEST_ASSERT_EQUAL_UINT32(0u, snapNote->endTick);
    TEST_ASSERT_TRUE(snapNote->isWrap(3072));
  }

  NoteUtils::DisplayNoteVec adopted;
  for (const auto& row : chord) {
    NoteUtils::DisplayNote note{};
    note.noteId = row.noteId;
    note.note = row.pitch;
    note.velocity = 90;
    note.startTick = 2640;
    note.endTick = row.linearEnd;
    adopted.push_back(note);
  }
  loop.adoptComposedDisplayNotesFromViewport(adopted);
  TEST_ASSERT_FALSE(loop.visualCache.notes.empty());
  TEST_ASSERT_FALSE(loop.captureActive());

  NoteUtils::DisplayNoteVec drawn;
  gatherDrawSourceDisplayNotes(loop, 0, 3, 3, 0, 127, drawn);
  for (const auto& row : chord) {
    bool foundLinear = false;
    for (const NoteUtils::DisplayNote& note : drawn) {
      if (note.note != row.pitch || note.startTick != 2640u) {
        continue;
      }
      TEST_ASSERT_FALSE(note.endTick < note.startTick);
      TEST_ASSERT_EQUAL_UINT32(row.linearEnd, note.endTick);
      foundLinear = true;
    }
    TEST_ASSERT_TRUE(foundLinear);
  }
}

void test_same_pass_draw_source_keeps_closed_bar_four_end() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  loop.loopId = 7;
  loop.loopLengthTicks = 3072;
  recordLinearNote(loop, 100, 200, 60);

  loop.openOverdubSession(0);
  loop.beginCapture(CapturePhase::Overdub);
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(2304, 1, 64, 90)));
  const NoteId idA = lastCaptureNoteOnId(loop);
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(2976, 1, 64, 0)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(0, 1, 64, 90)));
  const NoteId idB = lastCaptureNoteOnId(loop);
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(576, 1, 64, 0)));
  TEST_ASSERT_NOT_EQUAL(kInvalidNoteId, idA);
  TEST_ASSERT_NOT_EQUAL(idA, idB);
  pushClosedPendingAdd(loop, idA, 64, 2304, 2976);
  pushClosedPendingAdd(loop, idB, 64, 0, 576);
  commitOverdubWrapLikeDevice(loop, 2976, true);
  drainPendingSnapshotPublish(loop);
  loop.rebuildVisualCacheFromPasses();

  NoteUtils::DisplayNoteVec drawn;
  gatherDrawSourceDisplayNotes(loop, 0, 3, 0, 0, 127, drawn);
  assertSamePassClosedNoteKeepsLinearEnd(drawn, idA, 2976u, 576u, 3072u);
  for (const NoteUtils::DisplayNote& note : drawn) {
    if (note.noteId != idA) {
      continue;
    }
    TEST_ASSERT_EQUAL_UINT32(2304u, note.startTick);
    TEST_ASSERT_EQUAL_UINT32(2976u, note.endTick);
    TEST_ASSERT_FALSE(note.endTick < note.startTick);
  }

  TEST_ASSERT_TRUE(loop.publishedSnapshotIsCurrent());
  NoteUtils::DisplayNoteVec cacheDrawn;
  for (uint32_t bar = 0; bar <= 3; ++bar) {
    appendNotesIntersectingBar(loop.visualCache.notes, bar, 3072u, cacheDrawn);
  }
  assertSamePassClosedNoteKeepsLinearEnd(cacheDrawn, idA, 2976u, 576u, 3072u);
}

/// RC2b (192435): overdub `applyCapturePlayheadTails` (`allowWrapContinuation=false`).
/// Compose keeps `id 74` `2296→2976`; device drawn is `2296→672`. Host result:
/// with A in the capture suffix and open C at the same pitch+start, tails rewrite
/// A to playhead `2296`, not `672`. A must stay `2976`.
void test_same_pass_playhead_tails_keep_closed_bar_four_end() {
  constexpr uint32_t kLoopLength = 3072;
  constexpr uint32_t kPlayheadTick = 1720;
  const NoteId idA = 74;
  const NoteId idB = 76;
  const NoteId idC = 85;
  MidiEventVec events;
  events.push_back(NoteIdTestFixtures::noteOnWithNoteId(2296, 1, 63, 90, idA));
  events.push_back(noteOffWithNoteId(2976, 1, 63, idA));
  events.push_back(NoteIdTestFixtures::noteOnWithNoteId(0, 1, 63, 90, idB));
  events.push_back(noteOffWithNoteId(672, 1, 63, idB));
  events.push_back(NoteIdTestFixtures::noteOnWithNoteId(2296, 1, 63, 90, idC));

  CapturePreview preview;
  for (const MidiEvent& evt : events) {
    applyCaptureEventToPreview(preview, evt, Config::TICKS_PER_BAR, kLoopLength);
    if (evt.isNoteOn() && !preview.notes.empty()) {
      preview.notes.back().noteId = evt.noteId;
    }
  }

  NoteUtils::DisplayNoteVec drawn;
  for (const NoteUtils::DisplayNote& note : preview.notes) {
    drawn.push_back(note);
  }
  TEST_ASSERT_TRUE(DisplayWindowUtils::shouldApplyLiveCapturePlayheadTails(
      false, true, false, false, !preview.openNoteIndices.empty()));
  applyCapturePlayheadTails(preview, kLoopLength, kPlayheadTick, 0, drawn, false);

  const NoteUtils::DisplayNote* noteA = findDisplayNoteById(drawn, idA);
  TEST_ASSERT_NOT_NULL(noteA);
  TEST_ASSERT_EQUAL_UINT32(2296u, noteA->startTick);
  TEST_ASSERT_EQUAL_UINT32(2976u, noteA->endTick);
  TEST_ASSERT_NOT_EQUAL(672u, noteA->endTick);
  TEST_ASSERT_NOT_EQUAL(kLoopLength - 1u, noteA->endTick);
  TEST_ASSERT_FALSE(noteA->endTick < noteA->startTick);
  for (const NoteUtils::DisplayNote& note : drawn) {
    if (note.noteId != idA) {
      continue;
    }
    TEST_ASSERT_EQUAL_UINT32(2296u, note.startTick);
    TEST_ASSERT_EQUAL_UINT32(2976u, note.endTick);
  }
}

/// RC2b (172532): closed 2304→2496 same NoteId must not become wrap-held 2304→432
/// after wrap-2 journal drain / gather. Sibling 240→432 is the Off draw_src stole.
void test_closed_same_note_id_keeps_linear_end_after_wrap_two_gather() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  loop.loopId = 7;
  loop.loopLengthTicks = 3072;
  recordLinearNote(loop, 100, 200, 60);

  loop.openOverdubSession(0);
  loop.beginCapture(CapturePhase::Overdub);
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(2304, 1, 64, 90)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(2496, 1, 64, 0)));
  const NoteId idClosed = lastCaptureNoteOnId(loop);
  TEST_ASSERT_NOT_EQUAL(kInvalidNoteId, idClosed);
  pushClosedPendingAdd(loop, idClosed, 64, 2304, 2496);
  commitOverdubWrapLikeDevice(loop, 2496, true);
  drainPendingSnapshotPublish(loop);
  assertClosedNoteKeepsLinearEnd(loop, idClosed, 64, 2304, 2496);

  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(240, 1, 64, 90)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(432, 1, 64, 0)));
  const NoteId idSibling = lastCaptureNoteOnId(loop);
  TEST_ASSERT_NOT_EQUAL(idClosed, idSibling);
  pushClosedPendingAdd(loop, idSibling, 64, 240, 432);
  commitOverdubWrapLikeDevice(loop, 432, true);
  drainPendingSnapshotPublish(loop);
  TEST_ASSERT_TRUE(loop.tryPublishSessionExitSnapshot(CommitReason::OverdubStop));

  assertClosedNoteKeepsLinearEnd(loop, idClosed, 64, 2304, 2496);
  loop.rebuildVisualCacheFromPasses();
  const NoteUtils::DisplayNote* cacheNote = findDisplayNoteById(loop.visualCache.notes, idClosed);
  TEST_ASSERT_NOT_NULL(cacheNote);
  TEST_ASSERT_EQUAL_UINT32(2304u, cacheNote->startTick);
  TEST_ASSERT_EQUAL_UINT32(2496u, cacheNote->endTick);
  TEST_ASSERT_FALSE(cacheNote->endTick < cacheNote->startTick);
}

void test_session_exit_closed_notes_do_not_pair_across_passes() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  loop.loopId = 7;
  loop.loopLengthTicks = 3072;
  recordLinearNote(loop, 100, 200, 60);

  loop.openOverdubSession(0);
  loop.beginCapture(CapturePhase::Overdub);
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(2304, 1, 64, 90)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(2784, 1, 64, 0)));
  const NoteId idA = lastCaptureNoteOnId(loop);
  TEST_ASSERT_NOT_EQUAL(kInvalidNoteId, idA);
  commitOverdubWrapLikeDevice(loop, 2784, false);

  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(768, 1, 64, 90)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(864, 1, 64, 0)));
  const NoteId idB = lastCaptureNoteOnId(loop);
  TEST_ASSERT_NOT_EQUAL(idA, idB);
  commitOverdubWrapLikeDevice(loop, 864, false);

  TEST_ASSERT_TRUE(loop.tryPublishSessionExitSnapshot(CommitReason::OverdubStop));
  assertClosedNotesDidNotPairAcrossPasses(loop, idA, idB, 2784, 864);
}

/// Open holds keep sounding into the next pass and the live Capture tier still paints them.
void test_seal_retention_excludes_open_capture_holds() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  loop.loopId = 7;
  loop.loopLengthTicks = 1536;
  recordLinearNote(loop, 0, 40, 64);

  loop.openOverdubSession(0);
  loop.beginCapture(CapturePhase::Overdub);
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(0, 1, 64, 90)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(624, 1, 64, 0)));
  // Still held at the seal: no NoteOff, so this hold continues into the next pass.
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(480, 1, 67, 90)));
  TEST_ASSERT_FALSE(loop.capturePreview.openNoteIndices.empty());
  TEST_ASSERT_EQUAL(CommitResult::Committed,
                    loop.commitCapturePass(CommitReason::OverdubWrap, 624));

  TEST_ASSERT_EQUAL(1u, loop.sealedCapturePreviewNotes().size());
  TEST_ASSERT_EQUAL(64u, loop.sealedCapturePreviewNotes()[0].note);
}

void test_wrap_journal_drain_keeps_first_record_while_session_exit_pending() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  loop.loopId = 7;
  loop.loopLengthTicks = 3072;
  recordLinearNote(loop, 100, 200, 60);
  TEST_ASSERT_TRUE(loop.publishedSnapshotIsCurrent());
  TEST_ASSERT_EQUAL(1u, loop.publishedSnapshot()->noteMembership(60, 0)->count);

  loop.openOverdubSession(0);
  loop.beginCapture(CapturePhase::Overdub);
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(400, 1, 72, 90)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(480, 1, 72, 0)));
  TEST_ASSERT_EQUAL(CommitResult::Committed,
                    loop.commitCapturePass(CommitReason::OverdubWrap, 480));
  PendingNoteChange wrapAdd{};
  wrapAdd.kind = PendingNoteChangeKind::Add;
  wrapAdd.noteId = 10;
  wrapAdd.channel = 1;
  wrapAdd.pitch = 72;
  wrapAdd.velocity = 90;
  wrapAdd.startTick = 400;
  wrapAdd.endTick = 480;
  loop.pushPendingNoteChangeForTest(wrapAdd);
  TEST_ASSERT_TRUE(loop.queueOverdubWrapSnapshotJournal(loop.lastCommittedPassId()));
  loop.pushOverdubSessionPass(loop.lastCommittedPassId(), {});

  loop.beginCapture(CapturePhase::Overdub);
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(800, 1, 64, 80)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(860, 1, 64, 0)));
  TEST_ASSERT_EQUAL(CommitResult::Committed,
                    loop.commitCapturePass(CommitReason::OverdubStop, 860));
  loop.pushOverdubSessionPass(loop.lastCommittedPassId(), {});
  TEST_ASSERT_TRUE(loop.queueSessionExitSnapshot(CommitReason::OverdubStop, {}));
  TEST_ASSERT_TRUE(loop.hasPendingWrapJournal());
  TEST_ASSERT_TRUE(loop.pendingSnapshotQueuedPassCount() > 0);

  size_t steps = 0;
  while (loop.hasPendingWrapJournal() && steps < 10000) {
    loop.stepPendingSnapshotPublish(1, true);
    ++steps;
  }
  TEST_ASSERT_FALSE(loop.hasPendingWrapJournal());
  TEST_ASSERT_TRUE(loop.hasPendingSnapshotPublish());
  TEST_ASSERT_FALSE(loop.publishedSnapshotIsCurrent());

  SessionMidiEventVec gathered;
  if (!loop.tryGatherPublishedSnapshotEventsInWindow(gathered, 0, 3072)) {
    loop.gatherCommittedEventsInWindow(gathered, 0, 3072, false);
  }
  TEST_ASSERT_EQUAL(1, countNoteOns(gathered, 60));

  drainPendingSnapshotPublish(loop);
  TEST_ASSERT_TRUE(loop.publishedSnapshotIsCurrent());
  SessionMidiEventVec after;
  TEST_ASSERT_TRUE(loop.tryGatherPublishedSnapshotEventsInWindow(after, 0, 3072));
  TEST_ASSERT_EQUAL(1, countNoteOns(after, 60));
  TEST_ASSERT_EQUAL(1, countNoteOns(after, 72));
  TEST_ASSERT_EQUAL(1, countNoteOns(after, 64));
}

void applyLiveRecordCompletedTicks(Loop& loop, uint32_t completedTicks) {
  size_t steps = 0;
  while (loop.liveRecordAppliedCompletedTicks() < completedTicks && steps < 64) {
    const uint32_t before = loop.liveRecordAppliedCompletedTicks();
    loop.stepPendingSnapshotPublish(1, true, 0, completedTicks);
    drainInFlightSnapshotPublish(loop);
    TEST_ASSERT_TRUE(loop.liveRecordAppliedCompletedTicks() > before);
    ++steps;
  }
  TEST_ASSERT_EQUAL(completedTicks, loop.liveRecordAppliedCompletedTicks());
}

void test_session_exit_handoff_when_live_record_notes_fit_sealed_length() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  loop.loopId = 7;
  constexpr uint32_t kSealedLength = Config::TICKS_PER_BAR * 12;
  constexpr uint32_t kProvisionalLength =
      kSealedLength + 16u * Config::TICKS_PER_BAR;
  loop.loopLengthTicks = kSealedLength;
  loop.beginCapture(CapturePhase::Record);
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(100, 1, 60, 100)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(200, 1, 60, 0)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(9000, 1, 64, 100)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(9048, 1, 64, 0)));
  applyLiveRecordCompletedTicks(loop, kSealedLength);
  TEST_ASSERT_EQUAL(kProvisionalLength, loop.pendingSnapshotCandidateLengthTicks());
  TEST_ASSERT_NULL(loop.publishedSnapshot().get());

  TEST_ASSERT_EQUAL(CommitResult::Committed,
                    loop.commitCapturePass(CommitReason::RecordStop, 9048));
  TEST_ASSERT_TRUE(loop.pendingSnapshotSessionExitQueued());
  TEST_ASSERT_EQUAL(kSealedLength, loop.loopLengthTicks);
  loop.stepPendingSnapshotPublish(1, true);
  if (loop.hasPendingSnapshotPublish()) {
    TEST_ASSERT_EQUAL(kSealedLength, loop.pendingSnapshotCandidateLengthTicks());
    TEST_ASSERT_EQUAL(kSealedLength, loop.liveRecordAppliedCompletedTicks());
    TEST_ASSERT_NULL(loop.publishedSnapshot().get());
    drainPendingSnapshotPublish(loop);
  }
  TEST_ASSERT_TRUE(loop.publishedSnapshotIsCurrent());
  TEST_ASSERT_EQUAL(kSealedLength, loop.publishedSnapshot()->loopLengthTicks());
  SessionMidiEventVec snapshotEvents;
  TEST_ASSERT_TRUE(
      loop.tryGatherPublishedSnapshotEventsInWindow(snapshotEvents, 0, kSealedLength));
  TEST_ASSERT_EQUAL(1, countNoteOns(snapshotEvents, 60));
  TEST_ASSERT_EQUAL(1, countNoteOns(snapshotEvents, 64));
}

void test_session_exit_discards_when_applied_ticks_exceed_sealed_length() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  loop.loopId = 7;
  constexpr uint32_t kAppliedLength = Config::TICKS_PER_BAR * 2;
  constexpr uint32_t kSealedLength = Config::TICKS_PER_BAR;
  loop.loopLengthTicks = kAppliedLength;
  loop.beginCapture(CapturePhase::Record);
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(100, 1, 60, 100)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(200, 1, 60, 0)));
  TEST_ASSERT_TRUE(
      loop.appendCaptureEvent(MidiEvent::NoteOn(kSealedLength + 100, 1, 64, 100)));
  TEST_ASSERT_TRUE(
      loop.appendCaptureEvent(MidiEvent::NoteOff(kSealedLength + 200, 1, 64, 0)));
  applyLiveRecordCompletedTicks(loop, kAppliedLength);
  TEST_ASSERT_TRUE(loop.pendingSnapshotCandidateLengthTicks() > kSealedLength);
  TEST_ASSERT_EQUAL(CommitResult::Committed,
                    loop.commitCapturePass(CommitReason::RecordStop, kAppliedLength));
  TEST_ASSERT_TRUE(loop.pendingSnapshotSessionExitQueued());
  TEST_ASSERT_EQUAL(kAppliedLength, loop.liveRecordAppliedCompletedTicks());
  TEST_ASSERT_TRUE(loop.passes.hasRecordPass());
  TEST_ASSERT_FALSE(loop.passes.recordPass.committedChunkIds.empty());
  TEST_ASSERT_TRUE(loop.pendingSnapshotQueuedPassCount() > 0);

  loop.loopLengthTicks = kSealedLength;
  loop.stepPendingSnapshotPublish(1, true);
  TEST_ASSERT_EQUAL(0u, loop.liveRecordAppliedCompletedTicks());
  TEST_ASSERT_TRUE(loop.pendingSnapshotCandidateLengthTicks() <= kSealedLength);
  TEST_ASSERT_NULL(loop.publishedSnapshot().get());
}

void test_session_exit_pass_starts_as_journal_capped_fragments() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  loop.loopId = 7;
  loop.loopLengthTicks = 3072;
  loop.beginCapture(CapturePhase::Record);
  constexpr uint8_t kNoteCount = 20;
  for (uint8_t i = 0; i < kNoteCount; ++i) {
    const uint32_t onTick = 40u + static_cast<uint32_t>(i) * 80u;
    TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(onTick, 1, 60 + i, 100)));
    TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(onTick + 40u, 1, 60 + i, 0)));
  }
  TEST_ASSERT_EQUAL(CommitResult::Committed,
                    loop.commitCapturePass(CommitReason::RecordStop, 1600));
  TEST_ASSERT_TRUE(loop.hasPendingSnapshotPublish());
  TEST_ASSERT_TRUE(loop.stepPendingSnapshotPublish(1, true));
  TEST_ASSERT_TRUE(loop.hasPendingSnapshotPublish());
  TEST_ASSERT_NULL(loop.publishedSnapshot().get());

  drainPendingSnapshotPublish(loop);
  TEST_ASSERT_TRUE(loop.publishedSnapshotIsCurrent());
  SessionMidiEventVec snapshotEvents;
  TEST_ASSERT_TRUE(loop.tryGatherPublishedSnapshotEventsInWindow(snapshotEvents, 0, 3072));
  uint8_t foundNoteOns = 0;
  for (uint8_t i = 0; i < kNoteCount; ++i) {
    TEST_ASSERT_EQUAL(1, countNoteOns(snapshotEvents, static_cast<uint8_t>(60 + i)));
    ++foundNoteOns;
  }
  TEST_ASSERT_EQUAL(kNoteCount, foundNoteOns);
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
  RUN_TEST(test_live_record_does_not_rebuild_when_parent_has_no_room);
  RUN_TEST(test_live_record_second_bar_start_changes_are_bar_two_only);
  RUN_TEST(test_live_record_held_note_pairs_on_next_bar_off);
  RUN_TEST(test_live_record_dense_bar_keeps_cursor_inside_bar);
  RUN_TEST(test_live_record_equal_boundary_event_waits_for_next_window);
  RUN_TEST(test_live_record_same_pitch_second_bar_does_not_duplicate_first);
  RUN_TEST(test_session_exit_pass_change_skips_note_already_on_parent);
  RUN_TEST(test_live_record_starts_snapshot_before_session_exit);
  RUN_TEST(test_overdub_idle_advances_in_flight_construction_only);
  RUN_TEST(test_overdub_wrap_ahead_does_not_publish_until_session_exit);
  RUN_TEST(test_settled_overdub_wrap_publishes_current_snapshot);
  RUN_TEST(test_unstamped_wrap_journal_leaves_snapshot_unselected);
  RUN_TEST(test_seal_retains_capture_rows_until_snapshot_covers_revision);
  RUN_TEST(test_stale_snapshot_release_requires_visual_cache_coverage);
  RUN_TEST(test_seal_retention_excludes_open_capture_holds);
  RUN_TEST(test_overlay_rejected_slice_does_not_claim_playback_coverage);
  RUN_TEST(test_pass_state_refresh_replaces_cache_when_selected_active_gather_is_complete);
  RUN_TEST(test_wrap_journal_closed_notes_do_not_pair_across_passes);
  RUN_TEST(test_same_pass_closed_note_keeps_linear_end_beside_bar_one_sibling);
  RUN_TEST(test_same_pass_closed_bar_four_keeps_end_beside_hold_head);
  RUN_TEST(test_same_pass_snapshot_keeps_closed_bar_four_end);
  RUN_TEST(test_draw_source_upserts_closed_capture_preview_on_already_played_bar);
  RUN_TEST(test_draw_source_open_hold_grows_to_playhead_after_occupy_hide);
  RUN_TEST(test_draw_source_unions_sealed_preview_after_wrap_before_snapshot);
  RUN_TEST(test_draw_source_sealed_row_survives_prefix_hide);
  RUN_TEST(test_draw_source_keeps_stale_snapshot_after_second_wrap);
  RUN_TEST(test_draw_source_after_stop_keeps_adopted_linear_ends);
  RUN_TEST(test_same_pass_draw_source_keeps_closed_bar_four_end);
  RUN_TEST(test_same_pass_playhead_tails_keep_closed_bar_four_end);
  RUN_TEST(test_closed_same_note_id_keeps_linear_end_after_wrap_two_gather);
  RUN_TEST(test_session_exit_closed_notes_do_not_pair_across_passes);
  RUN_TEST(test_wrap_journal_drain_keeps_first_record_while_session_exit_pending);
  RUN_TEST(test_session_exit_handoff_when_live_record_notes_fit_sealed_length);
  RUN_TEST(test_session_exit_discards_when_applied_ticks_exceed_sealed_length);
  RUN_TEST(test_session_exit_pass_starts_as_journal_capped_fragments);
  RUN_TEST(test_overdub_without_parent_does_not_invent_snapshot);
  return UNITY_END();
}
