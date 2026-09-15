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
#include "../../src/Utils/CommittedMidiScratch.cpp"

#include "Loop.h"
#include "LoopInternal.h"
#include "LoopSnapshot.h"
#include "GlobalUndoStack.h"
#include "Utils/CommittedMidiScratch.h"
#include "../test_support/CommittedChunkIdTestHelpers.h"
#include "../test_support/NoteIdTestFixtures.h"
#include "Utils/MidiServiceDrain.h"
#include "EditPass.h"
#include "MidiEvent.h"
#include "PassReclaim.h"
#include "StorageLoopIo.h"
#include "LoopContentResolution.h"
#include "Utils/DisplayWindowUtils.h"
#include "Utils/SnapshotNoteConversion.h"

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

EditPass makeNoteRangeRow(NoteId targetNoteId, uint32_t newStart, uint32_t newEnd) {
  EditPass row{};
  row.passType = EditPassType::Note;
  row.actionType = EditActionType::Update;
  row.propertyType = EditPropertyType::NoteRange;
  row.targetNoteId = targetNoteId;
  row.startTick = newStart;
  row.endTick = newEnd;
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

NoteUtils::DisplayNote makeAdoptedNote(NoteId noteId, uint8_t pitch, uint32_t startTick,
                                       uint32_t endTick);

void test_prepared_capture_transfer_moves_pass_without_vector_growth() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();

  Loop loop;
  seedCommittedPair(loop);
  loop.beginCapture(CapturePhase::Overdub);
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(200, 1, 64, 90)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(248, 1, 64, 0)));
  TEST_ASSERT_EQUAL(SealOutcome::Ok,
                    loop.stagePreparedCapturePass(248, CommitReason::OverdubStop));
  TEST_ASSERT_TRUE(loop.hasPendingCapturePass());
  // Seal preparation reserves canonical pass storage. The handoff itself may
  // only transfer that prepared candidate without a vector growth.
  TEST_ASSERT_TRUE(loop.canTransferPendingCapturePassWithoutAllocation());
  const size_t preparedCapacity = loop.passes.overdubPasses.capacity();
  const uint32_t revisionBeforeTransfer = loop.playbackRevision;
  PreparedCapturePassTransfer transferred{};
  TEST_ASSERT_TRUE(loop.commitPreparedCapturePassAtomic(transferred));

  TEST_ASSERT_FALSE(loop.hasPendingCapturePass());
  TEST_ASSERT_EQUAL_UINT32(preparedCapacity, loop.passes.overdubPasses.capacity());
  TEST_ASSERT_EQUAL_UINT32(revisionBeforeTransfer + 1u, loop.playbackRevision);
  TEST_ASSERT_EQUAL(CapturePassPhase::Overdub, transferred.phase);
  TEST_ASSERT_EQUAL_UINT32(1u, loop.passes.overdubPasses.size());
  TEST_ASSERT_EQUAL(transferred.id, loop.passes.overdubPasses.back().id);
  TEST_ASSERT_FALSE(loop.passes.overdubPasses.back().committedChunkIds.empty());
  TEST_ASSERT_FALSE(loop.captureActive());
  // The atomic handoff must leave content-sized display/source work for the
  // deferred owner; clearing this preview belongs to the next continuation.
  TEST_ASSERT_FALSE(loop.capturePreview.notes.empty());
  loop.completePreparedCapturePassDeferredCleanup();
  TEST_ASSERT_TRUE(loop.capturePreview.notes.empty());
}

void test_active_overdub_transfer_uses_prewarmed_capacity() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();

  Loop loop;
  seedCommittedPair(loop);
  TEST_ASSERT_TRUE(loop.prewarmActiveOverdubTransferHorizon(2));
  const size_t passCapacity = loop.passes.overdubPasses.capacity();

  loop.beginCapture(CapturePhase::Overdub);
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(200, 1, 64, 90)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(248, 1, 64, 0)));
  TEST_ASSERT_TRUE(loop.hasActiveOverdubTransferAdmission());
  TEST_ASSERT_EQUAL(SealOutcome::Ok,
                    loop.stagePreparedCapturePassFromActiveAdmission(
                        248, CommitReason::OverdubStop));
  TEST_ASSERT_TRUE(loop.canTransferPendingCapturePassWithoutAllocation());

  PreparedCapturePassTransfer transferred{};
  TEST_ASSERT_TRUE(loop.commitPreparedCapturePassAtomic(transferred));
  TEST_ASSERT_EQUAL_UINT32(passCapacity, loop.passes.overdubPasses.capacity());
  TEST_ASSERT_EQUAL_UINT32(1u, loop.passes.overdubPasses.size());
  // One row remains from the two-pass admission horizon.
  TEST_ASSERT_TRUE(loop.hasActiveOverdubTransferAdmission());
}

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

void test_gather_committed_preserves_takes() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  seedCommittedPair(loop);
  const size_t before = loop.nativeTestLiveEventCount();
  SessionMidiEventVec gathered;
  loop.copyEffectiveCommittedEventsInRange(gathered, 0, kLoopLen);

  TEST_ASSERT_TRUE(loop.hasCommittedPasses());
  TEST_ASSERT_EQUAL(before, loop.nativeTestLiveEventCount());
  TEST_ASSERT_EQUAL(2u, gathered.size());
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

  SessionMidiEventVec gathered;
  loop.copyEffectiveCommittedEventsInRange(gathered, 0, kLoopLen);
  TEST_ASSERT_EQUAL(4u, gathered.size());
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

  SessionMidiEventVec gathered;
  loop.copyEffectiveCommittedEventsInRange(gathered, 0, kLoopLen);
  gathered.push_back(MidiEvent::NoteOn(48, 1, 60, 80));
  gathered.push_back(MidiEvent::NoteOff(72, 1, 60, 0));

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

void test_invalidateCaches_gather_reflects_loop_length_change() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  loop.loopLengthTicks = 768;
  loop.passes.recordPass = makeRecordPassWithNote(1, 10);
  loop.nextPassId_ = 2;
  (void)loop.saveNoteEditPass(0, makePitchRow(1, 10, 20, 67));

  loop.loopLengthTicks = 384;
  loop.invalidateCaches();

  MidiEventVec expected;
  loop.passes.materializeToEventVector(expected, loop.loopLengthTicks);
  SessionMidiEventVec gathered;
  loop.copyEffectiveCommittedEventsInRange(gathered, 0, loop.loopLengthTicks, true);
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

void pushClosedPendingShorten(Loop& loop, NoteId noteId, uint8_t pitch, uint32_t startTick,
                               uint32_t endTick) {
  PendingNoteChange shorten{};
  shorten.kind = PendingNoteChangeKind::Shorten;
  shorten.noteId = noteId;
  shorten.channel = 1;
  shorten.pitch = pitch;
  shorten.velocity = 90;
  shorten.startTick = startTick;
  shorten.endTick = endTick;
  loop.pushPendingNoteChangeForTest(shorten);
}

void pushClosedPendingHide(Loop& loop, NoteId noteId, uint8_t pitch, uint32_t startTick,
                            uint32_t endTick) {
  PendingNoteChange hide{};
  hide.kind = PendingNoteChangeKind::Hide;
  hide.noteId = noteId;
  hide.channel = 1;
  hide.pitch = pitch;
  hide.velocity = 90;
  hide.startTick = startTick;
  hide.endTick = endTick;
  loop.pushPendingNoteChangeForTest(hide);
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

void drainWrapJournalsUntilCurrent(Loop& loop) {
  size_t steps = 0;
  while ((loop.hasPendingWrapJournal() || loop.hasPendingSnapshotPublish()) && steps < 10000) {
    loop.stepPendingSnapshotPublish(1, true);
    ++steps;
  }
  TEST_ASSERT_FALSE(loop.hasPendingWrapJournal());
  TEST_ASSERT_TRUE(loop.publishedSnapshotIsCurrent());
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

void test_note_edit_close_note_range_reuses_published_pitch() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  loop.loopId = 7;
  loop.loopLengthTicks = 3072;
  recordLinearNote(loop, 100, 200, 60);
  const auto afterRecord = loop.publishedSnapshot();
  const NoteId noteId = afterRecord->noteMembership(60, 0)->references[0]->noteId;

  const EditPassId rowId = loop.saveNoteEditPass(0, makeNoteRangeRow(noteId, 400, 500));
  TEST_ASSERT_NOT_EQUAL(kInvalidEditPassId, rowId);
  EditPassIdList ids;
  ids.push_back(rowId);
  TEST_ASSERT_TRUE(loop.tryPublishNoteEditSnapshot(ids));

  const auto afterEdit = loop.publishedSnapshot();
  TEST_ASSERT_NOT_NULL(afterEdit.get());
  const auto pitchZero = afterEdit->noteMembership(0, 0);
  TEST_ASSERT_EQUAL(0u, pitchZero ? pitchZero->count : 0);
  const LoopSnapshotNoteRef moved = afterEdit->findNote(noteId, 60);
  TEST_ASSERT_TRUE(static_cast<bool>(moved));
  TEST_ASSERT_EQUAL_UINT8(60, moved->pitch);
  TEST_ASSERT_EQUAL_UINT32(400u, moved->startTick);
  TEST_ASSERT_EQUAL_UINT32(500u, moved->endTick);
}

void test_note_edit_close_pitch_then_note_range_keeps_updated_pitch() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  loop.loopId = 7;
  loop.loopLengthTicks = 3072;
  recordLinearNote(loop, 100, 200, 60);
  const auto afterRecord = loop.publishedSnapshot();
  const NoteId noteId = afterRecord->noteMembership(60, 0)->references[0]->noteId;

  const EditPassId pitchId = loop.saveNoteEditPass(0, makePitchRow(noteId, 100, 200, 67));
  const EditPassId rangeId = loop.saveNoteEditPass(0, makeNoteRangeRow(noteId, 400, 500));
  TEST_ASSERT_NOT_EQUAL(kInvalidEditPassId, pitchId);
  TEST_ASSERT_NOT_EQUAL(kInvalidEditPassId, rangeId);
  EditPassIdList ids;
  ids.push_back(pitchId);
  ids.push_back(rangeId);
  TEST_ASSERT_TRUE(loop.tryPublishNoteEditSnapshot(ids));

  const auto afterEdit = loop.publishedSnapshot();
  TEST_ASSERT_NOT_NULL(afterEdit.get());
  const auto pitchZero = afterEdit->noteMembership(0, 0);
  TEST_ASSERT_EQUAL(0u, pitchZero ? pitchZero->count : 0);
  const auto oldPitch = afterEdit->noteMembership(60, 0);
  TEST_ASSERT_EQUAL(0u, oldPitch ? oldPitch->count : 0);
  const LoopSnapshotNoteRef moved = afterEdit->findNote(noteId, 67);
  TEST_ASSERT_TRUE(static_cast<bool>(moved));
  TEST_ASSERT_EQUAL_UINT8(67, moved->pitch);
  TEST_ASSERT_EQUAL_UINT32(400u, moved->startTick);
  TEST_ASSERT_EQUAL_UINT32(500u, moved->endTick);
}

void test_stale_root_save_admits_edit_pass_to_pending_companions() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  loop.loopId = 7;
  loop.loopLengthTicks = 3072;
  recordLinearNote(loop, 100, 200, 60);
  const auto afterRecord = loop.publishedSnapshot();
  TEST_ASSERT_NOT_NULL(afterRecord.get());
  const NoteId noteId = afterRecord->noteMembership(60, 0)->references[0]->noteId;
  const uint32_t rootRev = loop.publishedSnapshotRevision();

  loop.openOverdubSession(0);
  loop.beginCapture(CapturePhase::Overdub, 0);
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(400, 1, 64, 90)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(480, 1, 64, 0)));
  TEST_ASSERT_EQUAL(CommitResult::Committed,
                    loop.commitCapturePass(CommitReason::OverdubStop, 480));
  TEST_ASSERT_FALSE(loop.publishedSnapshotIsCurrent());
  TEST_ASSERT_EQUAL(rootRev, loop.publishedSnapshotRevision());

  const EditPassId rowId = loop.saveNoteEditPass(0, makePitchRow(noteId, 100, 200, 67));
  TEST_ASSERT_NOT_EQUAL(kInvalidEditPassId, rowId);
  TEST_ASSERT_EQUAL(rootRev, loop.publishedSnapshotRevision());
  TEST_ASSERT_FALSE(loop.publishedSnapshotIsCurrent());

  const EditPassIdList* companions = loop.pendingSnapshotCompanionIds();
  TEST_ASSERT_NOT_NULL(companions);
  bool admitted = false;
  for (const EditPassId id : *companions) {
    if (id == rowId) {
      admitted = true;
      break;
    }
  }
  TEST_ASSERT_TRUE(admitted);

  Loop::SnapshotWindowGatherPath path = Loop::SnapshotWindowGatherPath::None;
  SessionMidiEventVec gathered;
  TEST_ASSERT_TRUE(loop.tryGatherPublishedSnapshotEventsInWindow(gathered, 0, 3072, &path));
  TEST_ASSERT_EQUAL(static_cast<int>(Loop::SnapshotWindowGatherPath::Overlay),
                    static_cast<int>(path));
  TEST_ASSERT_EQUAL(1, countNoteOns(gathered, 67));
  TEST_ASSERT_EQUAL(0, countNoteOns(gathered, 60));

  (void)loop.rebuildOverdubSourceSpanCacheForTest(0, 3072);
  bool sourceHasPitched = false;
  for (const NoteUtils::DisplayNote& note : loop.overdubSourceSpanCacheNotesForTest()) {
    if (note.noteId == noteId && note.note == 67) {
      sourceHasPitched = true;
      break;
    }
  }
  TEST_ASSERT_TRUE(sourceHasPitched);
}

void test_current_root_save_does_not_admit_companion_and_close_publish_holds() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  loop.loopId = 7;
  loop.loopLengthTicks = 3072;
  recordLinearNote(loop, 100, 200, 60);
  const auto afterRecord = loop.publishedSnapshot();
  TEST_ASSERT_TRUE(loop.publishedSnapshotIsCurrent());
  const NoteId noteId = afterRecord->noteMembership(60, 0)->references[0]->noteId;

  const EditPassId rowId = loop.saveNoteEditPass(0, makePitchRow(noteId, 100, 200, 67));
  TEST_ASSERT_NOT_EQUAL(kInvalidEditPassId, rowId);
  const EditPassIdList* companions = loop.pendingSnapshotCompanionIds();
  if (companions != nullptr) {
    for (const EditPassId id : *companions) {
      TEST_ASSERT_NOT_EQUAL(rowId, id);
    }
  }

  EditPassIdList ids;
  ids.push_back(rowId);
  TEST_ASSERT_TRUE(loop.tryPublishNoteEditSnapshot(ids));
  const auto afterEdit = loop.publishedSnapshot();
  TEST_ASSERT_NOT_NULL(afterEdit.get());
  const auto oldPitch = afterEdit->noteMembership(60, 0);
  TEST_ASSERT_EQUAL(0u, oldPitch ? oldPitch->count : 0);
  TEST_ASSERT_EQUAL(1u, afterEdit->noteMembership(67, 0)->count);
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
  loop.copyEffectiveCommittedEventsInRange(compatibilityFull, 0, 3072, false);
  sortEventsForCompare(snapshotFull);
  sortEventsForCompare(compatibilityFull);
  assertGatheredEventsMatch(snapshotFull, compatibilityFull);
  TEST_ASSERT_EQUAL(5u, snapshotFull.size());

  SessionMidiEventVec snapshotWindow;
  SessionMidiEventVec compatibilityWindow;
  TEST_ASSERT_TRUE(loop.tryGatherPublishedSnapshotEventsInWindow(snapshotWindow, 0, 768));
  loop.copyEffectiveCommittedEventsInRange(compatibilityWindow, 0, 768, false);
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
  loop.copyEffectiveCommittedEventsInRange(compatibilityAfterOverdub, 0, 3072, false);
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
  loop.copyEffectiveCommittedEventsInRange(compatibilityWindow, 0, 768, false);
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
  loop.copyEffectiveCommittedEventsInRange(compatibilityWindow, 0, 768, true);
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
  loop.copyEffectiveCommittedEventsInRange(compatibilityEvents, 0, 3072, false);
  TEST_ASSERT_EQUAL(1, countNoteOns(snapshotEvents, 64));
  TEST_ASSERT_EQUAL(1, countNoteOns(compatibilityEvents, 64));
}

void test_prepared_stop_handoff_preserves_restart_before_and_after_publication() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  loop.loopId = 7;
  loop.loopLengthTicks = 3072;
  recordLinearNote(loop, 100, 200, 60);
  TEST_ASSERT_FALSE(loop.canHandoffSessionExitSnapshot());
  TEST_ASSERT_TRUE(loop.prewarmActiveOverdubTransferHorizon(2));
  loop.openOverdubSession(0);
  loop.beginCapture(CapturePhase::Overdub);
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(400, 1, 64, 90)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(480, 1, 64, 0)));
  TEST_ASSERT_EQUAL(SealOutcome::Ok, loop.stagePreparedCapturePassFromActiveAdmission(
      480, CommitReason::OverdubStopToStopped));
  PreparedCapturePassTransfer transfer{};
  TEST_ASSERT_TRUE(loop.commitPreparedCapturePassAtomic(transfer));
  TEST_ASSERT_TRUE(loop.handoffSessionExitSnapshot(CommitReason::OverdubStopToStopped));
  TEST_ASSERT_FALSE(loop.canHandoffSessionExitSnapshot());
  TEST_ASSERT_FALSE(loop.handoffSessionExitSnapshot(CommitReason::OverdubStopToStopped));
  loop.completePreparedCapturePassDeferredCleanup();
  loop.closeOverdubSession();
  TEST_ASSERT_FALSE(loop.hasOverdubSession());
  TEST_ASSERT_TRUE(loop.pendingSnapshotSessionExitQueued());
  TEST_ASSERT_TRUE(loop.hasPendingSnapshotPublish());
  // No idle queue assembly is permitted in a construction-disabled turn.
  TEST_ASSERT_TRUE(loop.stepPendingSnapshotPublish(1, false));
  TEST_ASSERT_FALSE(loop.canHandoffSessionExitSnapshot());
  SessionMidiEventVec events;
  TEST_ASSERT_TRUE(loop.tryGatherPublishedSnapshotEventsInWindow(events, 0, 768));
  TEST_ASSERT_EQUAL(1, countNoteOns(events, 60));
  TEST_ASSERT_EQUAL(1, countNoteOns(events, 64));
  drainPendingSnapshotPublish(loop);
  TEST_ASSERT_TRUE(loop.publishedSnapshotIsCurrent());
  TEST_ASSERT_TRUE(loop.canHandoffSessionExitSnapshot());
  TEST_ASSERT_TRUE(loop.tryGatherPublishedSnapshotEventsInWindow(events, 0, 768));
  TEST_ASSERT_EQUAL(1, countNoteOns(events, 60));
  TEST_ASSERT_EQUAL(1, countNoteOns(events, 64));
}

void test_stop_handoff_retains_session_companions_and_clear_cancels_receipt() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  loop.loopId = 7;
  loop.loopLengthTicks = 3072;
  recordLinearNote(loop, 100, 200, 60);
  const auto membership = loop.publishedSnapshot()->noteMembership(60, 0);
  TEST_ASSERT_TRUE(loop.prewarmActiveOverdubTransferHorizon(2));
  loop.openOverdubSession(0);
  loop.beginCapture(CapturePhase::Overdub);
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(400, 1, 64, 90)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(480, 1, 64, 0)));
  TEST_ASSERT_EQUAL(CommitResult::Committed,
                    loop.commitCapturePass(CommitReason::OverdubWrap, 480));
  EditPass hide{};
  hide.passType = EditPassType::Note;
  hide.actionType = EditActionType::Delete;
  hide.targetNoteId = membership->references[0]->noteId;
  hide.pitch = 60;
  hide.startTick = 100;
  hide.endTick = 200;
  const EditPassId hideId = loop.saveNoteEditPass(0, hide);
  TEST_ASSERT_NOT_EQUAL(kInvalidEditPassId, hideId);
  loop.pushOverdubSessionPass(loop.lastCommittedPassId(), {hideId});
  TEST_ASSERT_TRUE(loop.handoffSessionExitSnapshot(CommitReason::OverdubStop));
  loop.closeOverdubSession();
  SessionMidiEventVec events;
  TEST_ASSERT_TRUE(loop.tryGatherPublishedSnapshotEventsInWindow(events, 0, 768));
  TEST_ASSERT_EQUAL(0, countNoteOns(events, 60));
  TEST_ASSERT_EQUAL(1, countNoteOns(events, 64));
  drainPendingSnapshotPublish(loop);
  TEST_ASSERT_TRUE(loop.publishedSnapshotIsCurrent());
  TEST_ASSERT_TRUE(loop.tryGatherPublishedSnapshotEventsInWindow(events, 0, 768));
  TEST_ASSERT_NULL(loop.publishedSnapshot()->findNote(hide.targetNoteId, 60).get());
  TEST_ASSERT_EQUAL(0, countNoteOns(events, 60));
  TEST_ASSERT_EQUAL(1, countNoteOns(events, 64));
  loop.openOverdubSession(0);
  TEST_ASSERT_TRUE(loop.handoffSessionExitSnapshot(CommitReason::OverdubStop));
  loop.closeOverdubSession();
  loop.resetPassTimeline();
  TEST_ASSERT_FALSE(loop.pendingSnapshotSessionExitQueued());
  TEST_ASSERT_FALSE(loop.hasPendingSnapshotPublish());
}

void test_empty_stop_handoff_does_not_duplicate_published_record() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  loop.loopId = 7;
  loop.loopLengthTicks = 3072;
  recordLinearNote(loop, 100, 200, 60);
  TEST_ASSERT_TRUE(loop.prewarmActiveOverdubTransferHorizon(2));
  loop.openOverdubSession(0);
  TEST_ASSERT_TRUE(loop.handoffSessionExitSnapshot(CommitReason::OverdubStopToStopped));
  loop.closeOverdubSession();
  drainPendingSnapshotPublish(loop);
  TEST_ASSERT_TRUE(loop.canHandoffSessionExitSnapshot());
  SessionMidiEventVec events;
  TEST_ASSERT_TRUE(loop.tryGatherPublishedSnapshotEventsInWindow(events, 0, 768));
  TEST_ASSERT_EQUAL(1, countNoteOns(events, 60));
  TEST_ASSERT_EQUAL_UINT32(2, events.size());
}

void test_stop_handoff_keeps_inflight_wrap_and_final_pass_once() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  loop.loopId = 7;
  loop.loopLengthTicks = 3072;
  recordLinearNote(loop, 100, 200, 60);
  TEST_ASSERT_TRUE(loop.prewarmActiveOverdubTransferHorizon(2));
  loop.openOverdubSession(0);
  loop.beginCapture(CapturePhase::Overdub);
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(300, 1, 62, 90)));
  const NoteId wrappedId = lastCaptureNoteOnId(loop);
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(360, 1, 62, 0)));
  pushClosedPendingAdd(loop, wrappedId, 62, 300, 360);
  commitOverdubWrapLikeDevice(loop, 360, true);
  TEST_ASSERT_TRUE(loop.hasPendingWrapJournal());
  TEST_ASSERT_TRUE(loop.stepPendingSnapshotPublish(1));
  TEST_ASSERT_TRUE(loop.hasInFlightSnapshotConstruction());
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(400, 1, 64, 90)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(480, 1, 64, 0)));
  TEST_ASSERT_EQUAL(SealOutcome::Ok, loop.stagePreparedCapturePassFromActiveAdmission(
      480, CommitReason::OverdubStopToStopped));
  PreparedCapturePassTransfer transfer{};
  TEST_ASSERT_TRUE(loop.commitPreparedCapturePassAtomic(transfer));
  TEST_ASSERT_TRUE(loop.handoffSessionExitSnapshot(CommitReason::OverdubStopToStopped));
  loop.completePreparedCapturePassDeferredCleanup();
  loop.closeOverdubSession();
  TEST_ASSERT_TRUE(loop.hasInFlightSnapshotConstruction());
  drainPendingSnapshotPublish(loop);
  TEST_ASSERT_TRUE(loop.publishedSnapshotIsCurrent());
  SessionMidiEventVec events;
  TEST_ASSERT_TRUE(loop.tryGatherPublishedSnapshotEventsInWindow(events, 0, 768));
  TEST_ASSERT_EQUAL(1, countNoteOns(events, 60));
  TEST_ASSERT_EQUAL(1, countNoteOns(events, 62));
  TEST_ASSERT_EQUAL(1, countNoteOns(events, 64));
  TEST_ASSERT_EQUAL_UINT32(6, events.size());
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

void applyLiveRecordCompletedTicks(Loop& loop, uint32_t completedTicks);

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

void test_live_record_extends_candidate_past_seventeen_bars() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  loop.loopId = 7;
  loop.loopLengthTicks = Config::TICKS_PER_BAR * 30;
  loop.beginCapture(CapturePhase::Record);
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(100, 1, 60, 100)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(200, 1, 60, 0)));
  TEST_ASSERT_TRUE(loop.stepPendingSnapshotPublish(1, true, 0, Config::TICKS_PER_BAR));
  drainInFlightSnapshotPublish(loop);
  TEST_ASSERT_EQUAL(Config::TICKS_PER_BAR, loop.liveRecordAppliedCompletedTicks());
  TEST_ASSERT_TRUE(loop.liveRecordCanApplyCompletedTicks(Config::TICKS_PER_BAR * 18));

  const uint32_t pastParent = Config::TICKS_PER_BAR * 18;
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(pastParent + 100, 1, 67, 100)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(pastParent + 200, 1, 67, 0)));
  applyLiveRecordCompletedTicks(loop, pastParent);
  TEST_ASSERT_EQUAL(pastParent, loop.liveRecordAppliedCompletedTicks());
  TEST_ASSERT_GREATER_THAN_UINT32(Config::TICKS_PER_BAR * 17,
                                  loop.pendingSnapshotCandidateLengthTicks());
}

void test_live_record_scales_applied_frontier_past_seventeen_bars() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  loop.loopId = 7;
  constexpr uint32_t kBars = 25;
  loop.loopLengthTicks = Config::TICKS_PER_BAR * kBars;
  loop.beginCapture(CapturePhase::Record);
  for (uint32_t bar = 0; bar < kBars; ++bar) {
    const uint32_t tick = bar * Config::TICKS_PER_BAR;
    TEST_ASSERT_TRUE(loop.appendCaptureEvent(
        MidiEvent::NoteOn(tick + 100, 1, static_cast<uint8_t>(60 + bar), 100)));
    TEST_ASSERT_TRUE(loop.appendCaptureEvent(
        MidiEvent::NoteOff(tick + 200, 1, static_cast<uint8_t>(60 + bar), 0)));
  }
  applyLiveRecordCompletedTicks(loop, Config::TICKS_PER_BAR * kBars);
  TEST_ASSERT_EQUAL(Config::TICKS_PER_BAR * kBars, loop.liveRecordAppliedCompletedTicks());
  TEST_ASSERT_GREATER_THAN_UINT32(Config::TICKS_PER_BAR * 17,
                                  loop.pendingSnapshotCandidateLengthTicks());
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

void test_session_exit_keeps_bar_crossing_live_record_open_note() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  loop.loopId = 7;
  loop.loopLengthTicks = Config::TICKS_PER_BAR * 4;
  loop.beginCapture(CapturePhase::Record);

  constexpr uint8_t kPitch = 60;
  constexpr uint32_t kOnTick = 100;
  const uint32_t offTick = Config::TICKS_PER_BAR + 200;

  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(kOnTick, 1, kPitch, 100)));
  TEST_ASSERT_TRUE(loop.stepPendingSnapshotPublish(1, true, 0, Config::TICKS_PER_BAR));
  drainInFlightSnapshotPublish(loop);
  TEST_ASSERT_EQUAL(Config::TICKS_PER_BAR, loop.liveRecordAppliedCompletedTicks());
  TEST_ASSERT_EQUAL(1u, loop.liveRecordOpenNoteCount());
  TEST_ASSERT_NOT_NULL(loop.unpublishedSnapshot().get());
  TEST_ASSERT_NULL(loop.unpublishedSnapshot()->noteMembership(kPitch, 0).get());

  const NoteId noteId = lastCaptureNoteOnId(loop);
  TEST_ASSERT_NOT_EQUAL(kInvalidNoteId, noteId);

  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(offTick, 1, kPitch, 0)));
  TEST_ASSERT_EQUAL(1u, loop.liveRecordOpenNoteCount());

  TEST_ASSERT_EQUAL(CommitResult::Committed,
                    loop.commitCapturePass(CommitReason::RecordStop, offTick));
  TEST_ASSERT_EQUAL(1u, loop.liveRecordOpenNoteCount());
  drainPendingSnapshotPublish(loop);
  TEST_ASSERT_TRUE(loop.publishedSnapshotIsCurrent());
  const auto published = loop.publishedSnapshot();
  TEST_ASSERT_NOT_NULL(published.get());

  const LoopSnapshotNoteRef note = published->findNote(noteId, kPitch);
  TEST_ASSERT_NOT_NULL(note.get());
  TEST_ASSERT_EQUAL_UINT32(kOnTick, note->startTick);
  TEST_ASSERT_EQUAL_UINT32(offTick, note->endTick);

  const auto bar0 = published->noteMembership(kPitch, 0);
  const auto bar1 = published->noteMembership(kPitch, 1);
  TEST_ASSERT_NOT_NULL(bar0.get());
  TEST_ASSERT_NOT_NULL(bar1.get());
  TEST_ASSERT_TRUE(bar0->count >= 1);
  TEST_ASSERT_TRUE(bar1->count >= 1);
  TEST_ASSERT_EQUAL(noteId, bar0->references[0]->noteId);
  TEST_ASSERT_EQUAL(noteId, bar1->references[0]->noteId);

  SessionMidiEventVec gathered;
  TEST_ASSERT_TRUE(
      loop.tryGatherPublishedSnapshotEventsInWindow(gathered, 0, loop.loopLengthTicks));
  TEST_ASSERT_EQUAL(1, countNoteOns(gathered, kPitch));
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
  TEST_ASSERT_FALSE(loop.pendingSnapshotFromLoadedPasses());
  TEST_ASSERT_EQUAL(static_cast<int>(LoadedSnapshotHydrationPhase::None),
                    static_cast<int>(loop.loadedSnapshotHydrationPhase()));
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

void test_session_exit_restarts_construction_when_overdub_would_block_start() {
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
  TEST_ASSERT_TRUE(loop.pendingSnapshotSessionExitQueued());
  TEST_ASSERT_TRUE(loop.stepPendingSnapshotPublish(1, true));
  TEST_ASSERT_TRUE(loop.hasInFlightSnapshotConstruction());
  while (loop.hasInFlightSnapshotConstruction()) {
    loop.stepPendingSnapshotPublish(1, false);
  }
  TEST_ASSERT_TRUE(loop.hasPendingSnapshotPublish());
  TEST_ASSERT_NULL(loop.publishedSnapshot().get());
  TEST_ASSERT_TRUE(MidiServiceDrain::allowSnapshotConstructionStart(
      true, true, false, false, loop.pendingSnapshotSessionExitQueued()));
  TEST_ASSERT_TRUE(loop.stepPendingSnapshotPublish(1, true));
  TEST_ASSERT_TRUE(loop.hasInFlightSnapshotConstruction());
}

void test_playback_session_exit_publication_gap_predicate() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  loop.loopId = 7;
  loop.loopLengthTicks = 3072;
  TEST_ASSERT_FALSE(loop.isPlaybackSessionExitPublicationGap());

  loop.beginCapture(CapturePhase::Record);
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(100, 1, 60, 100)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(200, 1, 60, 0)));
  TEST_ASSERT_EQUAL(CommitResult::Committed,
                    loop.commitCapturePass(CommitReason::RecordStop, 200));
  TEST_ASSERT_NULL(loop.publishedSnapshot().get());
  TEST_ASSERT_TRUE(loop.pendingSnapshotSessionExitQueued());
  TEST_ASSERT_TRUE(loop.hasPendingSnapshotPublish());
  TEST_ASSERT_TRUE(loop.isPlaybackSessionExitPublicationGap());

  drainPendingSnapshotPublish(loop);
  TEST_ASSERT_TRUE(loop.publishedSnapshotIsCurrent());
  TEST_ASSERT_FALSE(loop.isPlaybackSessionExitPublicationGap());

  Loop passOnly;
  passOnly.loopId = 8;
  passOnly.loopLengthTicks = 3072;
  LoopEventStore store;
  TEST_ASSERT_TRUE(storeAppendNoteOn(store, 100, 1, 60, 100, 1));
  TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOff(200, 1, 60, 0)));
  passOnly.seedRecordPassFromStore(store);
  TEST_ASSERT_NULL(passOnly.publishedSnapshot().get());
  TEST_ASSERT_FALSE(passOnly.isPlaybackSessionExitPublicationGap());
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
  TEST_ASSERT_TRUE(loop.lastVisualCacheIdleCopiedSnapshotNotes());
  TEST_ASSERT_EQUAL(static_cast<int>(Loop::VisualCacheIdleGatherSource::Snapshot),
                    static_cast<int>(loop.lastVisualCacheIdleGatherSource()));
  TEST_ASSERT_EQUAL(expected.size(), loop.visualCache.notes.size());
  TEST_ASSERT_EQUAL(expected[0].note, loop.visualCache.notes[0].note);
  TEST_ASSERT_EQUAL(expected[0].startTick, loop.visualCache.notes[0].startTick);
  TEST_ASSERT_EQUAL(expected[0].endTick, loop.visualCache.notes[0].endTick);

  NoteUtils::DisplayNoteVec resolved;
  LoopContentResolution::resolveDisplayNotes(*loop.publishedSnapshot(), resolved);
  loop.rebuildVisualCacheFromPasses();
  TEST_ASSERT_TRUE(loop.publishedSnapshotIsCurrent());
  TEST_ASSERT_EQUAL(loop.publishedSnapshotRevision(), loop.visualCache.builtFromRevision);
  TEST_ASSERT_EQUAL_PTR(loop.publishedSnapshotIdentity(),
                        loop.visualCache.builtFromSnapshotIdentity);
  TEST_ASSERT_EQUAL(resolved.size(), loop.visualCache.notes.size());
  TEST_ASSERT_EQUAL(resolved[0].note, loop.visualCache.notes[0].note);
  TEST_ASSERT_EQUAL(resolved[0].startTick, loop.visualCache.notes[0].startTick);
  TEST_ASSERT_EQUAL(resolved[0].endTick, loop.visualCache.notes[0].endTick);
  TEST_ASSERT_EQUAL(resolved[0].noteId, loop.visualCache.notes[0].noteId);

  Loop noSnapshot;
  seedCommittedPair(noSnapshot);
  SessionMidiEventVec missing;
  TEST_ASSERT_FALSE(noSnapshot.tryGatherPublishedSnapshotEventsInWindow(missing, 0, kLoopLen));
  noSnapshot.invalidateDisplayCaches();
  noSnapshot.rebuildVisualCacheIdleSlice(4, 0);
  TEST_ASSERT_FALSE(noSnapshot.lastVisualCacheIdleCopiedSnapshotNotes());
  TEST_ASSERT_FALSE(noSnapshot.visualCache.notes.empty());
  TEST_ASSERT_NULL(noSnapshot.visualCache.builtFromSnapshotIdentity);
}

void test_rebuild_visual_cache_from_passes_uses_snapshot_resolve_notes() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  loop.loopId = 7;
  loop.loopLengthTicks = 3072;
  recordLinearNote(loop, 100, 200, 60);
  TEST_ASSERT_TRUE(loop.publishedSnapshotIsCurrent());
  const ImmutableLoopSnapshotRef snapshot = loop.publishedSnapshot();
  TEST_ASSERT_NOT_NULL(snapshot.get());
  TEST_ASSERT_EQUAL_PTR(loop.publishedSnapshotIdentity(), snapshot.get());

  NoteUtils::DisplayNoteVec resolved;
  LoopContentResolution::resolveDisplayNotes(*snapshot, resolved);
  NoteUtils::DisplayNoteVec fromSpans;
  appendSnapshotNotesForBarRange(*snapshot, 0, 3, fromSpans);
  TEST_ASSERT_EQUAL(1u, resolved.size());
  TEST_ASSERT_EQUAL(fromSpans.size(), resolved.size());
  TEST_ASSERT_EQUAL(fromSpans[0].noteId, resolved[0].noteId);
  TEST_ASSERT_EQUAL(fromSpans[0].startTick, resolved[0].startTick);
  TEST_ASSERT_EQUAL(fromSpans[0].endTick, resolved[0].endTick);

  loop.invalidateDisplayCaches();
  loop.rebuildVisualCacheFromPasses();
  TEST_ASSERT_FALSE(loop.visualCacheDirty);
  TEST_ASSERT_TRUE(loop.publishedSnapshotIsCurrent());
  TEST_ASSERT_EQUAL_PTR(loop.publishedSnapshotIdentity(), loop.publishedSnapshot().get());
  TEST_ASSERT_EQUAL(loop.publishedSnapshotRevision(), loop.visualCache.builtFromRevision);
  TEST_ASSERT_EQUAL_PTR(loop.publishedSnapshotIdentity(),
                        loop.visualCache.builtFromSnapshotIdentity);
  TEST_ASSERT_EQUAL(resolved.size(), loop.visualCache.notes.size());
  TEST_ASSERT_EQUAL(resolved[0].noteId, loop.visualCache.notes[0].noteId);
  TEST_ASSERT_EQUAL(resolved[0].note, loop.visualCache.notes[0].note);
  TEST_ASSERT_EQUAL(resolved[0].startTick, loop.visualCache.notes[0].startTick);
  TEST_ASSERT_EQUAL(resolved[0].endTick, loop.visualCache.notes[0].endTick);
}

/// OpenSpec 4.6 Stage 1: same playbackRevision with a replaced published root must
/// not treat VisualCache as snapshot-current. Nullptr is never current.
void test_visual_cache_identity_stale_when_published_root_replaced_same_revision() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  loop.loopId = 7;
  loop.loopLengthTicks = 3072;
  recordLinearNote(loop, 100, 200, 60);
  TEST_ASSERT_TRUE(loop.publishedSnapshotIsCurrent());
  const uint32_t revisionR = loop.playbackRevision;
  const void* identityA = loop.publishedSnapshotIdentity();
  TEST_ASSERT_NOT_NULL(identityA);

  loop.invalidateDisplayCaches();
  loop.rebuildVisualCacheFromPasses();
  TEST_ASSERT_EQUAL_UINT32(revisionR, loop.visualCache.builtFromRevision);
  TEST_ASSERT_EQUAL_PTR(identityA, loop.visualCache.builtFromSnapshotIdentity);
  TEST_ASSERT_TRUE(DisplayWindowUtils::visualCacheIsCurrentPublishedSnapshot(
      loop.visualCache.builtFromRevision, loop.playbackRevision,
      loop.visualCache.builtFromSnapshotIdentity, loop.publishedSnapshotIdentity()));

  auto empty = LoopSnapshot::createEmpty(loop.loopId, loop.loopLengthTicks, 0, 8, 8);
  LoopSnapshotChangeSet changes;
  LoopSnapshotConstruction construction(empty, changes);
  while (!construction.complete()) {
    TEST_ASSERT_TRUE(construction.step(128));
  }
  const ImmutableLoopSnapshotRef snapshotB = construction.freeze();
  TEST_ASSERT_NOT_NULL(snapshotB.get());
  TEST_ASSERT_TRUE(snapshotB.get() != identityA);
  loop.storePublishedSnapshotForTest(snapshotB, revisionR);
  TEST_ASSERT_EQUAL_UINT32(revisionR, loop.playbackRevision);
  TEST_ASSERT_TRUE(loop.publishedSnapshotIsCurrent());
  const void* identityB = loop.publishedSnapshotIdentity();
  TEST_ASSERT_EQUAL_PTR(snapshotB.get(), identityB);
  TEST_ASSERT_TRUE(identityB != identityA);

  TEST_ASSERT_FALSE(DisplayWindowUtils::visualCacheIsCurrentPublishedSnapshot(
      loop.visualCache.builtFromRevision, loop.playbackRevision,
      loop.visualCache.builtFromSnapshotIdentity, loop.publishedSnapshotIdentity()));
  TEST_ASSERT_FALSE(DisplayWindowUtils::visualCacheIsCurrentPublishedSnapshot(
      revisionR, revisionR, nullptr, nullptr));
  TEST_ASSERT_FALSE(DisplayWindowUtils::visualCacheIsCurrentPublishedSnapshot(
      revisionR, revisionR, nullptr, identityB));
}

ImmutableLoopSnapshotRef buildLegacyScratchSnapshot(LoopId loopId, uint32_t loopLength,
                                                    NoteId noteId, uint8_t pitch,
                                                    uint32_t startTick, uint32_t endTick) {
  auto empty = std::make_shared<const LoopSnapshot>(LoopSnapshot(loopId, loopLength, 0, 8, 8));
  LoopSnapshotChangeSet changes;
  SnapshotNoteChange add{};
  add.kind = SnapshotChangeKind::Add;
  add.next.loopId = loopId;
  add.next.noteId = noteId;
  add.next.pitch = pitch;
  add.next.velocity = 100;
  add.next.startTick = startTick;
  add.next.endTick = endTick;
  changes.notes.push_back(add);
  LoopSnapshotConstruction construction(empty, changes);
  while (!construction.complete()) {
    TEST_ASSERT_TRUE(construction.step(128));
  }
  const ImmutableLoopSnapshotRef snapshot = construction.freeze();
  TEST_ASSERT_NOT_NULL(snapshot.get());
  return snapshot;
}

bool scratchHasNoteOffAt(const MidiEventVec& events, uint8_t pitch, uint32_t tick) {
  for (const MidiEvent& evt : events) {
    if (evt.isNoteOff() && evt.data.noteData.note == pitch && evt.tick == tick) {
      return true;
    }
  }
  return false;
}

/// OpenSpec 4.6 Stage 5: reuse, A→B@R source=B, revision rebuild, pass fallback.
void test_legacy_committed_scratch_snapshot_identity_freshness() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  loop.loopId = 7;
  loop.loopLengthTicks = 3072;
  recordLinearNote(loop, 100, 200, 60);
  TEST_ASSERT_TRUE(loop.publishedSnapshotIsCurrent());
  const uint32_t revisionR = loop.playbackRevision;

  MidiEventVec scratch;
  uint32_t scratchRevision = UINT32_MAX;
  const void* scratchIdentity = nullptr;

  TEST_ASSERT_EQUAL(
      static_cast<int>(CommittedMidiScratch::RefreshKind::FromSnapshot),
      static_cast<int>(CommittedMidiScratch::refresh(scratch, scratchRevision, scratchIdentity,
                                                     loop)));
  const void* identityA = loop.publishedSnapshotIdentity();
  TEST_ASSERT_EQUAL_PTR(identityA, scratchIdentity);
  TEST_ASSERT_EQUAL_UINT32(revisionR, scratchRevision);
  TEST_ASSERT_TRUE(scratchHasNoteOffAt(scratch, 60, 200));
  const MidiEventVec eventsFromA = scratch;
  const size_t scratchSizeA = scratch.size();

  TEST_ASSERT_EQUAL(
      static_cast<int>(CommittedMidiScratch::RefreshKind::Reused),
      static_cast<int>(CommittedMidiScratch::refresh(scratch, scratchRevision, scratchIdentity,
                                                     loop)));
  TEST_ASSERT_EQUAL_PTR(identityA, scratchIdentity);
  TEST_ASSERT_EQUAL(scratchSizeA, scratch.size());

  const ImmutableLoopSnapshotRef snapshotB =
      buildLegacyScratchSnapshot(loop.loopId, loop.loopLengthTicks, 1, 60, 100, 176);
  TEST_ASSERT_TRUE(snapshotB.get() != identityA);
  loop.storePublishedSnapshotForTest(snapshotB, revisionR);
  TEST_ASSERT_EQUAL_UINT32(revisionR, loop.playbackRevision);
  TEST_ASSERT_TRUE(loop.publishedSnapshotIsCurrent());
  const void* identityB = loop.publishedSnapshotIdentity();
  TEST_ASSERT_EQUAL_PTR(snapshotB.get(), identityB);

  TEST_ASSERT_EQUAL(
      static_cast<int>(CommittedMidiScratch::RefreshKind::FromSnapshot),
      static_cast<int>(CommittedMidiScratch::refresh(scratch, scratchRevision, scratchIdentity,
                                                     loop)));
  TEST_ASSERT_EQUAL_PTR(identityB, scratchIdentity);
  TEST_ASSERT_EQUAL_UINT32(revisionR, scratchRevision);
  SessionMidiEventVec expectedFromB;
  const void* observedB = nullptr;
  TEST_ASSERT_TRUE(loop.tryCopyCurrentSnapshotEditViewEvents(expectedFromB, observedB));
  TEST_ASSERT_EQUAL_PTR(identityB, observedB);
  TEST_ASSERT_EQUAL(expectedFromB.size(), scratch.size());
  TEST_ASSERT_TRUE(scratch.size() != eventsFromA.size() ||
                   scratch[0].tick != eventsFromA[0].tick ||
                   !scratchHasNoteOffAt(scratch, 60, 200));
  for (size_t i = 0; i < scratch.size(); ++i) {
    TEST_ASSERT_EQUAL_UINT32(expectedFromB[i].tick, scratch[i].tick);
    TEST_ASSERT_EQUAL(static_cast<int>(expectedFromB[i].type), static_cast<int>(scratch[i].type));
    if (expectedFromB[i].isNoteOn() || expectedFromB[i].isNoteOff()) {
      TEST_ASSERT_EQUAL_UINT8(expectedFromB[i].data.noteData.note, scratch[i].data.noteData.note);
    }
  }
  TEST_ASSERT_FALSE(scratchHasNoteOffAt(scratch, 60, 200));

  ++loop.playbackRevision;
  TEST_ASSERT_FALSE(loop.publishedSnapshotIsCurrent());
  TEST_ASSERT_EQUAL(
      static_cast<int>(CommittedMidiScratch::RefreshKind::HoldEmpty),
      static_cast<int>(CommittedMidiScratch::refresh(scratch, scratchRevision, scratchIdentity,
                                                     loop)));
  TEST_ASSERT_NULL(scratchIdentity);
  TEST_ASSERT_EQUAL_UINT32(loop.playbackRevision, scratchRevision);
  TEST_ASSERT_TRUE(scratch.empty());

  CommittedMidiScratch::invalidate(scratch, scratchRevision, scratchIdentity);
  TEST_ASSERT_TRUE(scratch.empty());
  TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, scratchRevision);
  TEST_ASSERT_NULL(scratchIdentity);
}

void test_committed_scratch_hold_empty_without_snapshot_or_c1_a() {
  Loop loop;
  loop.loopId = 7;
  loop.loopLengthTicks = 3072;

  MidiEventVec scratch;
  uint32_t scratchRevision = UINT32_MAX;
  const void* scratchIdentity = nullptr;
  CommittedMidiScratch::invalidate(scratch, scratchRevision, scratchIdentity);

  const CommittedMidiScratch::RefreshKind refresh =
      CommittedMidiScratch::refresh(scratch, scratchRevision, scratchIdentity, loop);
  TEST_ASSERT_EQUAL(CommittedMidiScratch::RefreshKind::HoldEmpty, refresh);
  TEST_ASSERT_TRUE(scratch.empty());
  TEST_ASSERT_EQUAL_UINT32(loop.playbackRevision, scratchRevision);
  TEST_ASSERT_NULL(scratchIdentity);
}

void test_committed_scratch_hold_empty_during_session_exit_gap() {
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
  TEST_ASSERT_TRUE(loop.isPlaybackSessionExitPublicationGap());

  MidiEventVec scratch;
  uint32_t scratchRevision = UINT32_MAX;
  const void* scratchIdentity = nullptr;
  const CommittedMidiScratch::RefreshKind refresh =
      CommittedMidiScratch::refresh(scratch, scratchRevision, scratchIdentity, loop);
  TEST_ASSERT_EQUAL(CommittedMidiScratch::RefreshKind::HoldEmpty, refresh);
  TEST_ASSERT_TRUE(scratch.empty());
  TEST_ASSERT_EQUAL_UINT32(loop.playbackRevision, scratchRevision);
  TEST_ASSERT_NULL(scratchIdentity);
}

void test_keep_committed_mover_in_visual_cache_232309() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  loop.loopId = 7;
  loop.loopLengthTicks = 1536;
  loop.beginCapture(CapturePhase::Record);
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(0, 1, 64, 100)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(48, 1, 64, 0)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(192, 1, 67, 100)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(240, 1, 67, 0)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(240, 1, 55, 100)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(289, 1, 55, 0)));
  TEST_ASSERT_EQUAL(CommitResult::Committed,
                    loop.commitCapturePass(CommitReason::RecordStop, 289));
  drainPendingSnapshotPublish(loop);
  TEST_ASSERT_TRUE(loop.publishedSnapshotIsCurrent());
  loop.invalidateDisplayCaches();
  loop.rebuildVisualCacheFromPasses();
  TEST_ASSERT_EQUAL(3u, loop.visualCache.notes.size());
  NoteId moverId = kInvalidNoteId;
  for (const NoteUtils::DisplayNote& note : loop.visualCache.notes) {
    if (note.note == 55 && note.startTick == 240) {
      moverId = note.noteId;
      break;
    }
  }
  TEST_ASSERT_NOT_EQUAL(kInvalidNoteId, moverId);
  TEST_ASSERT_NOT_NULL(loop.publishedSnapshot()->findNote(moverId, 55).get());

  auto newEnd = std::remove_if(loop.visualCache.notes.begin(), loop.visualCache.notes.end(),
                               [moverId](const NoteUtils::DisplayNote& note) {
                                 return note.noteId == moverId;
                               });
  loop.visualCache.notes.erase(newEnd, loop.visualCache.notes.end());
  TEST_ASSERT_EQUAL(2u, loop.visualCache.notes.size());
  TEST_ASSERT_NULL(findDisplayNoteById(loop.visualCache.notes, moverId));

  loop.keepCommittedMoverInVisualCache(moverId, 55);
  TEST_ASSERT_EQUAL(3u, loop.visualCache.notes.size());
  const NoteUtils::DisplayNote* kept = findDisplayNoteById(loop.visualCache.notes, moverId);
  TEST_ASSERT_NOT_NULL(kept);
  TEST_ASSERT_EQUAL_UINT8(55, kept->note);
  TEST_ASSERT_EQUAL_UINT32(240u, kept->startTick);
}

void test_rebuild_visual_cache_from_passes_skips_wrap_held_append_on_snapshot_spans() {
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
  TEST_ASSERT_TRUE(loop.publishedSnapshotIsCurrent());
  const ImmutableLoopSnapshotRef snapshot = loop.publishedSnapshot();
  TEST_ASSERT_NOT_NULL(snapshot.get());

  NoteUtils::DisplayNoteVec spans;
  LoopContentResolution::resolveDisplayNotes(*snapshot, spans);
  TEST_ASSERT_EQUAL(1u, spans.size());
  TEST_ASSERT_TRUE(spans[0].endTick < spans[0].startTick);

  NoteUtils::DisplayNoteVec withWrapHeld = spans;
  loop.appendOverdubPassDisplayNotes(withWrapHeld);
  TEST_ASSERT_GREATER_THAN(spans.size(), withWrapHeld.size());

  loop.invalidateDisplayCaches();
  loop.rebuildVisualCacheFromPasses();
  TEST_ASSERT_EQUAL(spans.size(), loop.visualCache.notes.size());
  TEST_ASSERT_EQUAL(spans[0].noteId, loop.visualCache.notes[0].noteId);
  TEST_ASSERT_EQUAL(spans[0].startTick, loop.visualCache.notes[0].startTick);
  TEST_ASSERT_EQUAL(spans[0].endTick, loop.visualCache.notes[0].endTick);
}

void test_rematerialize_edit_view_uses_snapshot_span_note_ids() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  loop.loopId = 1;
  loop.loopLengthTicks = Config::TICKS_PER_BAR;
  loop.playbackRevision = 1;

  auto empty = LoopSnapshot::createEmpty(1, loop.loopLengthTicks, 0, 8, 8);
  LoopSnapshotChangeSet changes;

  SnapshotNoteChange noteChange{};
  noteChange.kind = SnapshotChangeKind::Add;
  noteChange.next.loopId = 1;
  noteChange.next.noteId = 7;
  noteChange.next.pitch = 60;
  noteChange.next.velocity = 100;
  noteChange.next.startTick = 100;
  noteChange.next.endTick = 200;
  changes.notes.push_back(noteChange);

  MidiEvent noteOn = MidiEvent::NoteOn(100, 1, 60, 100);
  noteOn.noteId = 7;
  SnapshotEventChange noteOnChange{};
  noteOnChange.kind = SnapshotChangeKind::Add;
  noteOnChange.next = {1, 1, 0, noteOn};
  changes.events.push_back(noteOnChange);

  MidiEvent noteOff = MidiEvent::NoteOff(200, 1, 60);
  noteOff.noteId = 7;
  SnapshotEventChange noteOffChange{};
  noteOffChange.kind = SnapshotChangeKind::Add;
  noteOffChange.next = {1, 2, 1, noteOff};
  changes.events.push_back(noteOffChange);

  MidiEvent onlyStoreOn = MidiEvent::NoteOn(400, 1, 69, 100);
  onlyStoreOn.noteId = 64;
  SnapshotEventChange onlyStoreOnChange{};
  onlyStoreOnChange.kind = SnapshotChangeKind::Add;
  onlyStoreOnChange.next = {1, 3, 2, onlyStoreOn};
  changes.events.push_back(onlyStoreOnChange);

  LoopSnapshotConstruction construction(empty, changes);
  while (!construction.complete()) {
    TEST_ASSERT_TRUE(construction.step(128));
  }
  const ImmutableLoopSnapshotRef frozen = construction.freeze();
  TEST_ASSERT_NOT_NULL(frozen.get());
  loop.storePublishedSnapshotForTest(frozen, loop.playbackRevision);
  TEST_ASSERT_TRUE(loop.publishedSnapshotIsCurrent());

  NoteUtils::DisplayNoteVec spanNotes;
  LoopContentResolution::resolveDisplayNotes(*frozen, spanNotes);
  TEST_ASSERT_EQUAL_UINT32(1u, spanNotes.size());
  TEST_ASSERT_EQUAL_UINT32(7u, spanNotes.front().noteId);

  SessionMidiEventVec windowEvents;
  LoopContentResolution::resolveWindow(*frozen, 0, loop.loopLengthTicks, windowEvents);
  uint32_t windowNoteOns = 0;
  bool windowHasOnlyStore = false;
  for (const MidiEvent& evt : windowEvents) {
    if (!evt.isNoteOn()) {
      continue;
    }
    ++windowNoteOns;
    if (evt.noteId == 64) {
      windowHasOnlyStore = true;
    }
  }
  TEST_ASSERT_EQUAL_UINT32(2u, windowNoteOns);
  TEST_ASSERT_TRUE(windowHasOnlyStore);

  LoopEventStore store;
  loop.rematerializeEditView(store);
  uint32_t storeNoteOns = 0;
  bool storeHasSpanId = false;
  bool storeHasOnlyStore = false;
  for (size_t i = 0; i < store.size(); ++i) {
    const MidiEvent& evt = store.at(i);
    if (!evt.isNoteOn()) {
      continue;
    }
    ++storeNoteOns;
    if (evt.noteId == 7) {
      storeHasSpanId = true;
      TEST_ASSERT_EQUAL_UINT8(1u, evt.channel);
      TEST_ASSERT_EQUAL_UINT8(60u, evt.data.noteData.note);
    }
    if (evt.noteId == 64) {
      storeHasOnlyStore = true;
    }
  }
  TEST_ASSERT_EQUAL_UINT32(1u, storeNoteOns);
  TEST_ASSERT_TRUE(storeHasSpanId);
  TEST_ASSERT_FALSE(storeHasOnlyStore);
}

void test_copy_note_edit_commit_base_uses_snapshot_span_note_ids() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  loop.loopId = 1;
  loop.loopLengthTicks = Config::TICKS_PER_BAR;
  loop.playbackRevision = 1;

  auto empty = LoopSnapshot::createEmpty(1, loop.loopLengthTicks, 0, 8, 8);
  LoopSnapshotChangeSet changes;

  SnapshotNoteChange noteChange{};
  noteChange.kind = SnapshotChangeKind::Add;
  noteChange.next.loopId = 1;
  noteChange.next.noteId = 7;
  noteChange.next.pitch = 60;
  noteChange.next.velocity = 100;
  noteChange.next.startTick = 100;
  noteChange.next.endTick = 200;
  changes.notes.push_back(noteChange);

  MidiEvent noteOn = MidiEvent::NoteOn(100, 1, 60, 100);
  noteOn.noteId = 7;
  SnapshotEventChange noteOnChange{};
  noteOnChange.kind = SnapshotChangeKind::Add;
  noteOnChange.next = {1, 1, 0, noteOn};
  changes.events.push_back(noteOnChange);

  MidiEvent noteOff = MidiEvent::NoteOff(200, 1, 60);
  noteOff.noteId = 7;
  SnapshotEventChange noteOffChange{};
  noteOffChange.kind = SnapshotChangeKind::Add;
  noteOffChange.next = {1, 2, 1, noteOff};
  changes.events.push_back(noteOffChange);

  MidiEvent onlyStoreOn = MidiEvent::NoteOn(400, 1, 69, 100);
  onlyStoreOn.noteId = 64;
  SnapshotEventChange onlyStoreOnChange{};
  onlyStoreOnChange.kind = SnapshotChangeKind::Add;
  onlyStoreOnChange.next = {1, 3, 2, onlyStoreOn};
  changes.events.push_back(onlyStoreOnChange);

  LoopSnapshotConstruction construction(empty, changes);
  while (!construction.complete()) {
    TEST_ASSERT_TRUE(construction.step(128));
  }
  const ImmutableLoopSnapshotRef frozen = construction.freeze();
  TEST_ASSERT_NOT_NULL(frozen.get());
  loop.storePublishedSnapshotForTest(frozen, loop.playbackRevision);
  TEST_ASSERT_TRUE(loop.publishedSnapshotIsCurrent());

  const uint32_t openedSnapshotRevision = loop.publishedSnapshotRevision();

  SessionMidiEventVec commitBase;
  loop.copyNoteEditCommitBase(EditPassIdList{}, commitBase, true, openedSnapshotRevision);
  uint32_t baseNoteOns = 0;
  bool baseHasSpanId = false;
  bool baseHasOnlyStore = false;
  for (const MidiEvent& evt : commitBase) {
    if (!evt.isNoteOn()) {
      continue;
    }
    ++baseNoteOns;
    if (evt.noteId == 7) {
      baseHasSpanId = true;
      TEST_ASSERT_EQUAL_UINT8(60u, evt.data.noteData.note);
    }
    if (evt.noteId == 64) {
      baseHasOnlyStore = true;
    }
  }
  TEST_ASSERT_EQUAL_UINT32(1u, baseNoteOns);
  TEST_ASSERT_TRUE(baseHasSpanId);
  TEST_ASSERT_FALSE(baseHasOnlyStore);

  LoopEventStore store;
  loop.rematerializeEditView(store);
  uint32_t storeNoteOns = 0;
  bool storeHasOnlyStore = false;
  for (size_t i = 0; i < store.size(); ++i) {
    const MidiEvent& evt = store.at(i);
    if (!evt.isNoteOn()) {
      continue;
    }
    ++storeNoteOns;
    if (evt.noteId == 64) {
      storeHasOnlyStore = true;
    }
  }
  TEST_ASSERT_EQUAL_UINT32(baseNoteOns, storeNoteOns);
  TEST_ASSERT_FALSE(storeHasOnlyStore);

  EditPass pitchRow;
  pitchRow.actionType = EditActionType::Update;
  pitchRow.propertyType = EditPropertyType::Pitch;
  pitchRow.targetNoteId = 7;
  pitchRow.startTick = 100;
  pitchRow.endTick = 200;
  pitchRow.pitch = 72;
  const EditPassId savedId = loop.saveNoteEditPass(0, pitchRow);
  TEST_ASSERT_NOT_EQUAL(kInvalidEditPassId, savedId);
  EditPassIdList sessionIds;
  sessionIds.push_back(savedId);

  TEST_ASSERT_FALSE(loop.publishedSnapshotIsCurrent());
  TEST_ASSERT_EQUAL_UINT32(openedSnapshotRevision, loop.publishedSnapshotRevision());

  SessionMidiEventVec sessionBase;
  loop.copyNoteEditCommitBase(sessionIds, sessionBase, true, openedSnapshotRevision);
  uint32_t sessionNoteOns = 0;
  bool sessionHasSpanId = false;
  bool sessionHasOnlyStore = false;
  uint8_t sessionPitch = 0;
  for (const MidiEvent& evt : sessionBase) {
    if (!evt.isNoteOn()) {
      continue;
    }
    ++sessionNoteOns;
    if (evt.noteId == 7) {
      sessionHasSpanId = true;
      sessionPitch = evt.data.noteData.note;
    }
    if (evt.noteId == 64) {
      sessionHasOnlyStore = true;
    }
  }
  TEST_ASSERT_EQUAL_UINT32(1u, sessionNoteOns);
  TEST_ASSERT_TRUE(sessionHasSpanId);
  TEST_ASSERT_EQUAL_UINT8(72u, sessionPitch);
  TEST_ASSERT_FALSE(sessionHasOnlyStore);

  loop.playbackRevision++;
  TEST_ASSERT_FALSE(loop.publishedSnapshotIsCurrent());
  SessionMidiEventVec fallback;
  loop.copyNoteEditCommitBase(EditPassIdList{}, fallback);
  SessionMidiEventVec fromPasses;
  loop.passes.materializeToEventVector(fromPasses, loop.loopLengthTicks);
  TEST_ASSERT_EQUAL_UINT32(fromPasses.size(), fallback.size());
}

void test_try_copy_note_edit_opened_snapshot_events_uses_span_note_ids() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  loop.loopId = 1;
  loop.loopLengthTicks = Config::TICKS_PER_BAR;
  loop.playbackRevision = 1;

  auto empty = LoopSnapshot::createEmpty(1, loop.loopLengthTicks, 0, 8, 8);
  LoopSnapshotChangeSet changes;

  SnapshotNoteChange noteChange{};
  noteChange.kind = SnapshotChangeKind::Add;
  noteChange.next.loopId = 1;
  noteChange.next.noteId = 7;
  noteChange.next.pitch = 60;
  noteChange.next.velocity = 100;
  noteChange.next.startTick = 100;
  noteChange.next.endTick = 200;
  changes.notes.push_back(noteChange);

  MidiEvent noteOn = MidiEvent::NoteOn(100, 1, 60, 100);
  noteOn.noteId = 7;
  SnapshotEventChange noteOnChange{};
  noteOnChange.kind = SnapshotChangeKind::Add;
  noteOnChange.next = {1, 1, 0, noteOn};
  changes.events.push_back(noteOnChange);

  MidiEvent noteOff = MidiEvent::NoteOff(200, 1, 60);
  noteOff.noteId = 7;
  SnapshotEventChange noteOffChange{};
  noteOffChange.kind = SnapshotChangeKind::Add;
  noteOffChange.next = {1, 2, 1, noteOff};
  changes.events.push_back(noteOffChange);

  MidiEvent onlyStoreOn = MidiEvent::NoteOn(400, 1, 69, 100);
  onlyStoreOn.noteId = 64;
  SnapshotEventChange onlyStoreOnChange{};
  onlyStoreOnChange.kind = SnapshotChangeKind::Add;
  onlyStoreOnChange.next = {1, 3, 2, onlyStoreOn};
  changes.events.push_back(onlyStoreOnChange);

  LoopSnapshotConstruction construction(empty, changes);
  while (!construction.complete()) {
    TEST_ASSERT_TRUE(construction.step(128));
  }
  const ImmutableLoopSnapshotRef frozen = construction.freeze();
  TEST_ASSERT_NOT_NULL(frozen.get());
  loop.storePublishedSnapshotForTest(frozen, loop.playbackRevision);
  TEST_ASSERT_TRUE(loop.publishedSnapshotIsCurrent());

  const uint32_t openedSnapshotRevision = loop.publishedSnapshotRevision();
  SessionMidiEventVec opened;
  TEST_ASSERT_TRUE(loop.tryCopyNoteEditOpenedSnapshotEvents(true, openedSnapshotRevision, opened));
  uint32_t openedNoteOns = 0;
  bool openedHasSpanId = false;
  bool openedHasOnlyStore = false;
  for (const MidiEvent& evt : opened) {
    if (!evt.isNoteOn()) {
      continue;
    }
    ++openedNoteOns;
    if (evt.noteId == 7) {
      openedHasSpanId = true;
    }
    if (evt.noteId == 64) {
      openedHasOnlyStore = true;
    }
  }
  TEST_ASSERT_EQUAL_UINT32(1u, openedNoteOns);
  TEST_ASSERT_TRUE(openedHasSpanId);
  TEST_ASSERT_FALSE(openedHasOnlyStore);

  loop.playbackRevision++;
  SessionMidiEventVec miss;
  TEST_ASSERT_FALSE(loop.tryCopyNoteEditOpenedSnapshotEvents(false, 0, miss));
  TEST_ASSERT_EQUAL_UINT32(0u, miss.size());
}

void test_copy_idle_validate_events_uses_snapshot_span_note_ids() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  loop.loopId = 1;
  loop.loopLengthTicks = Config::TICKS_PER_BAR;
  loop.playbackRevision = 1;

  auto empty = LoopSnapshot::createEmpty(1, loop.loopLengthTicks, 0, 8, 8);
  LoopSnapshotChangeSet changes;

  SnapshotNoteChange noteChange{};
  noteChange.kind = SnapshotChangeKind::Add;
  noteChange.next.loopId = 1;
  noteChange.next.noteId = 7;
  noteChange.next.pitch = 60;
  noteChange.next.velocity = 100;
  noteChange.next.startTick = 100;
  noteChange.next.endTick = 200;
  changes.notes.push_back(noteChange);

  MidiEvent noteOn = MidiEvent::NoteOn(100, 1, 60, 100);
  noteOn.noteId = 7;
  SnapshotEventChange noteOnChange{};
  noteOnChange.kind = SnapshotChangeKind::Add;
  noteOnChange.next = {1, 1, 0, noteOn};
  changes.events.push_back(noteOnChange);

  MidiEvent noteOff = MidiEvent::NoteOff(200, 1, 60);
  noteOff.noteId = 7;
  SnapshotEventChange noteOffChange{};
  noteOffChange.kind = SnapshotChangeKind::Add;
  noteOffChange.next = {1, 2, 1, noteOff};
  changes.events.push_back(noteOffChange);

  MidiEvent onlyStoreOn = MidiEvent::NoteOn(400, 1, 69, 100);
  onlyStoreOn.noteId = 64;
  SnapshotEventChange onlyStoreOnChange{};
  onlyStoreOnChange.kind = SnapshotChangeKind::Add;
  onlyStoreOnChange.next = {1, 3, 2, onlyStoreOn};
  changes.events.push_back(onlyStoreOnChange);

  LoopSnapshotConstruction construction(empty, changes);
  while (!construction.complete()) {
    TEST_ASSERT_TRUE(construction.step(128));
  }
  const ImmutableLoopSnapshotRef frozen = construction.freeze();
  TEST_ASSERT_NOT_NULL(frozen.get());
  loop.storePublishedSnapshotForTest(frozen, loop.playbackRevision);
  TEST_ASSERT_TRUE(loop.publishedSnapshotIsCurrent());

  MidiEventVec currentRoot;
  loop.copyIdleValidateEvents(currentRoot);
  uint32_t currentNoteOns = 0;
  bool currentHasSpanId = false;
  bool currentHasOnlyStore = false;
  for (const MidiEvent& evt : currentRoot) {
    if (!evt.isNoteOn()) {
      continue;
    }
    ++currentNoteOns;
    if (evt.noteId == 7) {
      currentHasSpanId = true;
    }
    if (evt.noteId == 64) {
      currentHasOnlyStore = true;
    }
  }
  TEST_ASSERT_EQUAL_UINT32(1u, currentNoteOns);
  TEST_ASSERT_TRUE(currentHasSpanId);
  TEST_ASSERT_FALSE(currentHasOnlyStore);

  MidiEventVec mergeEmpty;
  loop.mergeActiveCapturePasses(mergeEmpty);
  TEST_ASSERT_EQUAL_UINT32(0u, mergeEmpty.size());
  TEST_ASSERT_TRUE(currentRoot.size() > mergeEmpty.size());

  loop.playbackRevision++;
  TEST_ASSERT_FALSE(loop.publishedSnapshotIsCurrent());
  MidiEventVec staleRoot;
  loop.copyIdleValidateEvents(staleRoot);
  MidiEventVec mergeStale;
  loop.mergeActiveCapturePasses(mergeStale);
  TEST_ASSERT_EQUAL_UINT32(mergeStale.size(), staleRoot.size());

  Loop passLoop;
  passLoop.loopId = 7;
  passLoop.loopLengthTicks = 3072;
  recordLinearNote(passLoop, 100, 200, 60);
  TEST_ASSERT_TRUE(passLoop.publishedSnapshotIsCurrent());
  passLoop.playbackRevision++;
  TEST_ASSERT_FALSE(passLoop.publishedSnapshotIsCurrent());
  MidiEventVec fallback;
  passLoop.copyIdleValidateEvents(fallback);
  MidiEventVec mergeFallback;
  passLoop.mergeActiveCapturePasses(mergeFallback);
  TEST_ASSERT_TRUE(mergeFallback.size() > 0);
  TEST_ASSERT_EQUAL_UINT32(mergeFallback.size(), fallback.size());
}

void test_try_copy_current_snapshot_display_notes_in_window_uses_span_note_ids() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  loop.loopId = 1;
  loop.loopLengthTicks = Config::TICKS_PER_BAR;
  loop.playbackRevision = 1;

  auto empty = LoopSnapshot::createEmpty(1, loop.loopLengthTicks, 0, 8, 8);
  LoopSnapshotChangeSet changes;

  SnapshotNoteChange noteChange{};
  noteChange.kind = SnapshotChangeKind::Add;
  noteChange.next.loopId = 1;
  noteChange.next.noteId = 7;
  noteChange.next.pitch = 60;
  noteChange.next.velocity = 100;
  noteChange.next.startTick = 100;
  noteChange.next.endTick = 200;
  changes.notes.push_back(noteChange);

  MidiEvent noteOn = MidiEvent::NoteOn(100, 1, 60, 100);
  noteOn.noteId = 7;
  SnapshotEventChange noteOnChange{};
  noteOnChange.kind = SnapshotChangeKind::Add;
  noteOnChange.next = {1, 1, 0, noteOn};
  changes.events.push_back(noteOnChange);

  MidiEvent noteOff = MidiEvent::NoteOff(200, 1, 60);
  noteOff.noteId = 7;
  SnapshotEventChange noteOffChange{};
  noteOffChange.kind = SnapshotChangeKind::Add;
  noteOffChange.next = {1, 2, 1, noteOff};
  changes.events.push_back(noteOffChange);

  MidiEvent onlyStoreOn = MidiEvent::NoteOn(400, 1, 69, 100);
  onlyStoreOn.noteId = 64;
  SnapshotEventChange onlyStoreOnChange{};
  onlyStoreOnChange.kind = SnapshotChangeKind::Add;
  onlyStoreOnChange.next = {1, 3, 2, onlyStoreOn};
  changes.events.push_back(onlyStoreOnChange);

  LoopSnapshotConstruction construction(empty, changes);
  while (!construction.complete()) {
    TEST_ASSERT_TRUE(construction.step(128));
  }
  const ImmutableLoopSnapshotRef frozen = construction.freeze();
  TEST_ASSERT_NOT_NULL(frozen.get());
  loop.storePublishedSnapshotForTest(frozen, loop.playbackRevision);
  TEST_ASSERT_TRUE(loop.publishedSnapshotIsCurrent());

  NoteUtils::DisplayNoteVec windowNotes;
  TEST_ASSERT_TRUE(
      loop.tryCopyCurrentSnapshotDisplayNotesInWindow(windowNotes, 0, loop.loopLengthTicks));
  TEST_ASSERT_EQUAL_UINT32(1u, windowNotes.size());
  TEST_ASSERT_EQUAL_UINT32(7u, windowNotes[0].noteId);

  NoteUtils::DisplayNoteVec outside;
  TEST_ASSERT_TRUE(loop.tryCopyCurrentSnapshotDisplayNotesInWindow(outside, 400, 80));
  TEST_ASSERT_EQUAL_UINT32(0u, outside.size());

  loop.playbackRevision++;
  TEST_ASSERT_FALSE(loop.publishedSnapshotIsCurrent());
  NoteUtils::DisplayNoteVec stale;
  TEST_ASSERT_FALSE(
      loop.tryCopyCurrentSnapshotDisplayNotesInWindow(stale, 0, loop.loopLengthTicks));
  TEST_ASSERT_EQUAL_UINT32(0u, stale.size());
}

void test_visual_cache_idle_slice_copies_current_snapshot_notes() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  loop.loopId = 7;
  loop.loopLengthTicks = 3072;
  recordLinearNote(loop, 100, 200, 60);
  TEST_ASSERT_TRUE(loop.publishedSnapshotIsCurrent());
  const ImmutableLoopSnapshotRef snapshot = loop.publishedSnapshot();
  TEST_ASSERT_NOT_NULL(snapshot.get());
  NoteUtils::DisplayNoteVec fromSnapshot;
  appendSnapshotNotesForBarRange(*snapshot, 0, 3, fromSnapshot);
  TEST_ASSERT_EQUAL(1u, fromSnapshot.size());

  loop.invalidateDisplayCaches();
  TEST_ASSERT_TRUE(loop.visualCacheDirty);
  loop.rebuildVisualCacheIdleSlice(4, 0);
  TEST_ASSERT_TRUE(loop.lastVisualCacheIdleCopiedSnapshotNotes());
  TEST_ASSERT_EQUAL(static_cast<int>(Loop::VisualCacheIdleGatherSource::Snapshot),
                    static_cast<int>(loop.lastVisualCacheIdleGatherSource()));
  TEST_ASSERT_EQUAL(fromSnapshot.size(), loop.visualCache.notes.size());
  TEST_ASSERT_FALSE(loop.visualCacheDirty);
  TEST_ASSERT_EQUAL_UINT32(loop.playbackRevision, loop.visualCache.builtFromRevision);
  TEST_ASSERT_EQUAL_PTR(loop.publishedSnapshotIdentity(),
                        loop.visualCache.builtFromSnapshotIdentity);
}

void test_loaded_visual_cache_falls_back_until_current_snapshot() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop source;
  source.loopId = 7;
  source.loopLengthTicks = 3072;
  recordLinearNote(source, 100, 200, 60);
  TEST_ASSERT_TRUE(source.publishedSnapshotIsCurrent());
  LoopSnapshotRef shared = source.sharePassesSnapshot();
  TEST_ASSERT_NOT_NULL(shared.get());

  Loop dest;
  dest.adoptPersistedSnapshot(*shared);
  TEST_ASSERT_FALSE(dest.pendingSnapshotFromLoadedPasses());
  dest.invalidateDisplayCaches();
  dest.rebuildVisualCacheIdleSlice(4, 0);
  TEST_ASSERT_FALSE(dest.lastVisualCacheIdleCopiedSnapshotNotes());
  TEST_ASSERT_NOT_EQUAL(static_cast<int>(Loop::VisualCacheIdleGatherSource::None),
                        static_cast<int>(dest.lastVisualCacheIdleGatherSource()));
  TEST_ASSERT_NOT_EQUAL(static_cast<int>(Loop::VisualCacheIdleGatherSource::Snapshot),
                        static_cast<int>(dest.lastVisualCacheIdleGatherSource()));
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

/// Yield skips `stepPendingSnapshotPublish`; the previous complete root stays
/// selected and wrap work remains. Retry then publishes only when complete.
void test_snapshot_yield_retains_then_retries_to_publish() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  loop.loopId = 7;
  loop.loopLengthTicks = 3072;
  recordLinearNote(loop, 100, 200, 60);
  const auto afterRecord = loop.publishedSnapshot();
  const uint32_t afterRecordRev = loop.publishedSnapshotRevision();
  TEST_ASSERT_TRUE(static_cast<bool>(afterRecord));
  TEST_ASSERT_TRUE(loop.publishedSnapshotIsCurrent());

  loop.openOverdubSession(0);
  loop.beginCapture(CapturePhase::Overdub);
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(400, 1, 72, 90)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(480, 1, 72, 0)));
  PendingNoteChange wrapAdd{};
  wrapAdd.kind = PendingNoteChangeKind::Add;
  wrapAdd.noteId = 10;
  wrapAdd.channel = 1;
  wrapAdd.pitch = 72;
  wrapAdd.velocity = 90;
  wrapAdd.startTick = 400;
  wrapAdd.endTick = 480;
  loop.pushPendingNoteChangeForTest(wrapAdd);
  commitOverdubWrapLikeDevice(loop, 480, true);

  TEST_ASSERT_TRUE(loop.hasPendingWrapJournal());
  TEST_ASSERT_TRUE(loop.hasPendingSnapshotPublish());
  TEST_ASSERT_EQUAL_PTR(afterRecord.get(), loop.publishedSnapshot().get());
  TEST_ASSERT_EQUAL_UINT32(afterRecordRev, loop.publishedSnapshotRevision());
  TEST_ASSERT_FALSE(loop.publishedSnapshotIsCurrent());

  drainWrapJournalsUntilCurrent(loop);
  TEST_ASSERT_FALSE(loop.hasPendingWrapJournal());
  TEST_ASSERT_FALSE(loop.hasPendingSnapshotPublish());
  TEST_ASSERT_TRUE(loop.publishedSnapshotIsCurrent());
  TEST_ASSERT_NOT_EQUAL(afterRecordRev, loop.publishedSnapshotRevision());
  TEST_ASSERT_NOT_EQUAL(afterRecord.get(), loop.publishedSnapshot().get());

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

/// Task 3.6: two wrap Shortens of the same NoteId publish one latest geometry each time.
/// Older published roots keep the previous end.
void test_wrap_journal_repeated_same_note_id_replace() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  loop.loopId = 7;
  loop.loopLengthTicks = 3072;
  recordLinearNote(loop, 100, 200, 60);
  const auto afterRecord = loop.publishedSnapshot();
  TEST_ASSERT_TRUE(static_cast<bool>(afterRecord));
  const NoteId recordId = afterRecord->noteMembership(60, 0)->references[0]->noteId;
  TEST_ASSERT_NOT_EQUAL(kInvalidNoteId, recordId);
  TEST_ASSERT_EQUAL_UINT32(200u, afterRecord->findNote(recordId, 60)->endTick);

  loop.openOverdubSession(0);
  loop.beginCapture(CapturePhase::Overdub);
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(400, 1, 72, 90)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(480, 1, 72, 0)));
  pushClosedPendingShorten(loop, recordId, 60, 100, 160);
  pushClosedPendingAdd(loop, 10, 72, 400, 480);
  commitOverdubWrapLikeDevice(loop, 480, true);
  drainWrapJournalsUntilCurrent(loop);

  const auto afterFirst = loop.publishedSnapshot();
  TEST_ASSERT_TRUE(static_cast<bool>(afterFirst));
  TEST_ASSERT_TRUE(afterFirst.get() != afterRecord.get());
  TEST_ASSERT_EQUAL_UINT32(200u, afterRecord->findNote(recordId, 60)->endTick);
  TEST_ASSERT_EQUAL_UINT32(160u, afterFirst->findNote(recordId, 60)->endTick);
  TEST_ASSERT_EQUAL(recordId, afterFirst->findNote(recordId, 60)->noteId);

  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(800, 1, 67, 80)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(860, 1, 67, 0)));
  pushClosedPendingShorten(loop, recordId, 60, 100, 140);
  pushClosedPendingAdd(loop, 11, 67, 800, 860);
  commitOverdubWrapLikeDevice(loop, 860, true);
  drainWrapJournalsUntilCurrent(loop);

  const auto afterSecond = loop.publishedSnapshot();
  TEST_ASSERT_TRUE(static_cast<bool>(afterSecond));
  TEST_ASSERT_EQUAL_UINT32(200u, afterRecord->findNote(recordId, 60)->endTick);
  TEST_ASSERT_EQUAL_UINT32(160u, afterFirst->findNote(recordId, 60)->endTick);
  TEST_ASSERT_EQUAL_UINT32(140u, afterSecond->findNote(recordId, 60)->endTick);
  TEST_ASSERT_EQUAL(recordId, afterSecond->findNote(recordId, 60)->noteId);
}

/// Task 3.6: wrap-journal Shorten replaces wrap NoteOn/NoteOff objects and keeps NoteId.
void test_wrap_journal_replaces_wrap_note_on_off_pair() {
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

  const auto afterRecord = loop.publishedSnapshot();
  TEST_ASSERT_TRUE(static_cast<bool>(afterRecord));
  const LoopSnapshotNoteRef parentNote = afterRecord->findNote(
      afterRecord->noteMembership(60, 3)->references[0]->noteId, 60);
  TEST_ASSERT_TRUE(static_cast<bool>(parentNote));
  const NoteId wrapId = parentNote->noteId;
  TEST_ASSERT_TRUE(parentNote->isWrap(3072));
  TEST_ASSERT_EQUAL_UINT32(2800u, parentNote->startTick);
  TEST_ASSERT_EQUAL_UINT32(100u, parentNote->endTick);

  loop.openOverdubSession(0);
  loop.beginCapture(CapturePhase::Overdub);
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(400, 1, 72, 90)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(480, 1, 72, 0)));
  pushClosedPendingShorten(loop, wrapId, 60, 2800, 50);
  pushClosedPendingAdd(loop, 10, 72, 400, 480);
  commitOverdubWrapLikeDevice(loop, 480, true);
  drainWrapJournalsUntilCurrent(loop);

  const auto afterWrap = loop.publishedSnapshot();
  TEST_ASSERT_TRUE(static_cast<bool>(afterWrap));
  const LoopSnapshotNoteRef replaced = afterWrap->findNote(wrapId, 60);
  TEST_ASSERT_TRUE(static_cast<bool>(replaced));
  TEST_ASSERT_TRUE(replaced.get() != parentNote.get());
  TEST_ASSERT_EQUAL(wrapId, replaced->noteId);
  TEST_ASSERT_EQUAL_UINT32(2800u, replaced->startTick);
  TEST_ASSERT_EQUAL_UINT32(50u, replaced->endTick);
  TEST_ASSERT_TRUE(replaced->isWrap(3072));
  TEST_ASSERT_EQUAL_PTR(afterWrap->noteMembership(60, 3)->references[0].get(),
                        afterWrap->noteMembership(60, 0)->references[0].get());
  TEST_ASSERT_EQUAL_UINT32(100u, afterRecord->findNote(wrapId, 60)->endTick);

  SessionMidiEventVec gathered;
  TEST_ASSERT_TRUE(loop.tryGatherPublishedSnapshotEventsInWindow(gathered, 0, 3072));
  int onCount = 0;
  int offCount = 0;
  for (const MidiEvent& evt : gathered) {
    if (evt.noteId != wrapId || evt.data.noteData.note != 60) {
      continue;
    }
    if (evt.isNoteOn()) {
      TEST_ASSERT_EQUAL_UINT32(2800u, evt.tick);
      ++onCount;
    }
    if (evt.isNoteOff()) {
      TEST_ASSERT_EQUAL_UINT32(50u, evt.tick);
      ++offCount;
    }
  }
  TEST_ASSERT_EQUAL(1, onCount);
  TEST_ASSERT_EQUAL(1, offCount);
}

/// Task 3.6: wrap-journal Hide removes the NoteId from the successor and keeps the parent.
void test_wrap_journal_hide_removes_published_note() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  loop.loopId = 7;
  loop.loopLengthTicks = 3072;
  recordLinearNote(loop, 100, 200, 60);
  const auto afterRecord = loop.publishedSnapshot();
  TEST_ASSERT_TRUE(static_cast<bool>(afterRecord));
  const NoteId recordId = afterRecord->noteMembership(60, 0)->references[0]->noteId;

  loop.openOverdubSession(0);
  loop.beginCapture(CapturePhase::Overdub);
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(400, 1, 72, 90)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(480, 1, 72, 0)));
  pushClosedPendingHide(loop, recordId, 60, 100, 200);
  pushClosedPendingAdd(loop, 10, 72, 400, 480);
  commitOverdubWrapLikeDevice(loop, 480, true);
  drainWrapJournalsUntilCurrent(loop);

  const auto afterHide = loop.publishedSnapshot();
  TEST_ASSERT_TRUE(static_cast<bool>(afterHide));
  TEST_ASSERT_TRUE(static_cast<bool>(afterRecord->findNote(recordId, 60)));
  TEST_ASSERT_FALSE(static_cast<bool>(afterHide->findNote(recordId, 60)));
  TEST_ASSERT_TRUE(static_cast<bool>(afterHide->findNote(10, 72)));

  SessionMidiEventVec gathered;
  TEST_ASSERT_TRUE(loop.tryGatherPublishedSnapshotEventsInWindow(gathered, 0, 3072));
  TEST_ASSERT_EQUAL(0, countNoteOns(gathered, 60));
  TEST_ASSERT_EQUAL(1, countNoteOns(gathered, 72));
}

/// Task 3.6: wrap-journal non-note events keep EventId and equal-tick canonical order.
void test_wrap_journal_equal_tick_non_note_event_order() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  loop.loopId = 7;
  loop.loopLengthTicks = 3072;
  recordLinearNote(loop, 100, 200, 60);

  loop.openOverdubSession(0);
  loop.beginCapture(CapturePhase::Overdub);
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(400, 1, 72, 90)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::ControlChange(400, 1, 7, 64)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(480, 1, 72, 0)));
  pushClosedPendingAdd(loop, 10, 72, 400, 480);
  commitOverdubWrapLikeDevice(loop, 480, true);
  drainWrapJournalsUntilCurrent(loop);

  const auto snapshot = loop.publishedSnapshot();
  TEST_ASSERT_TRUE(static_cast<bool>(snapshot));
  const auto membership = snapshot->eventMembership(0);
  TEST_ASSERT_NOT_NULL(membership.get());
  uint32_t noteOnOrder = 0;
  uint32_t ccOrder = 0;
  EventId ccEventId = kInvalidEventId;
  uint32_t prevOrder = 0;
  bool sawPrev = false;
  forEachSnapshotMembershipReference(membership.get(), [&](const LoopSnapshotEventRef& ref) {
    if (!ref) {
      return;
    }
    if (sawPrev) {
      TEST_ASSERT_TRUE(prevOrder <= ref->canonicalOrder);
    }
    prevOrder = ref->canonicalOrder;
    sawPrev = true;
    if (ref->value.isNoteOn() && ref->value.tick == 400 &&
        ref->value.data.noteData.note == 72) {
      noteOnOrder = ref->canonicalOrder;
    }
    if (ref->value.type == midi::ControlChange && ref->value.tick == 400) {
      ccOrder = ref->canonicalOrder;
      ccEventId = ref->eventId;
    }
  });
  TEST_ASSERT_TRUE(noteOnOrder != 0);
  TEST_ASSERT_TRUE(ccOrder != 0);
  TEST_ASSERT_TRUE(noteOnOrder < ccOrder);
  TEST_ASSERT_NOT_EQUAL(kInvalidEventId, ccEventId);

  SessionMidiEventVec gathered;
  TEST_ASSERT_TRUE(loop.tryGatherPublishedSnapshotEventsInWindow(gathered, 0, 3072));
  int ccAtOnTick = 0;
  bool sawNoteOnAt400 = false;
  for (const MidiEvent& evt : gathered) {
    if (evt.tick != 400) {
      continue;
    }
    if (evt.isNoteOn() && evt.data.noteData.note == 72) {
      TEST_ASSERT_EQUAL(0, ccAtOnTick);
      sawNoteOnAt400 = true;
    }
    if (evt.type == midi::ControlChange) {
      TEST_ASSERT_TRUE(sawNoteOnAt400);
      ++ccAtOnTick;
    }
  }
  TEST_ASSERT_EQUAL(1, ccAtOnTick);
}

/// [`094930`] skipped overdub stop closed the session without queueing session-exit, which
/// discarded in-flight wrap journals and left `root_rev` at the first record.
void test_skipped_overdub_stop_retains_in_flight_wrap_journals() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  loop.loopId = 7;
  loop.loopLengthTicks = 3072;
  recordLinearNote(loop, 100, 200, 60);
  const uint32_t recordRevision = loop.publishedSnapshotRevision();

  loop.openOverdubSession(0);
  loop.beginCapture(CapturePhase::Overdub);
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(400, 1, 72, 90)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(480, 1, 72, 0)));
  pushClosedPendingAdd(loop, 10, 72, 400, 480);
  commitOverdubWrapLikeDevice(loop, 480, true);
  TEST_ASSERT_TRUE(loop.hasPendingWrapJournal());
  TEST_ASSERT_TRUE(loop.stepPendingSnapshotPublish(1, true));
  TEST_ASSERT_TRUE(loop.hasPendingWrapJournal());
  TEST_ASSERT_EQUAL(recordRevision, loop.publishedSnapshotRevision());

  loop.closeOverdubSession();
  TEST_ASSERT_FALSE(loop.hasOverdubSession());
  TEST_ASSERT_TRUE(loop.hasPendingWrapJournal());
  TEST_ASSERT_TRUE(loop.hasPendingSnapshotPublish());

  drainWrapJournalsUntilCurrent(loop);
  TEST_ASSERT_TRUE(loop.publishedSnapshotRevision() > recordRevision);
  SessionMidiEventVec gathered;
  TEST_ASSERT_TRUE(loop.tryGatherPublishedSnapshotEventsInWindow(gathered, 0, 3072));
  TEST_ASSERT_EQUAL(1, countNoteOns(gathered, 60));
  TEST_ASSERT_EQUAL(1, countNoteOns(gathered, 72));
}

/// Wrap pass is already journaled, so session-exit has no new pass id. Keep-alive must
/// retain the journals instead of clearing the box.
void test_queue_session_exit_keeps_wrap_journals_without_new_pass() {
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
  pushClosedPendingAdd(loop, 10, 72, 400, 480);
  commitOverdubWrapLikeDevice(loop, 480, true);
  TEST_ASSERT_TRUE(loop.hasPendingWrapJournal());
  TEST_ASSERT_TRUE(loop.queueSessionExitSnapshot(CommitReason::OverdubStop, {}));
  TEST_ASSERT_TRUE(loop.hasPendingWrapJournal());
  loop.closeOverdubSession();
  TEST_ASSERT_TRUE(loop.hasPendingWrapJournal());
  drainWrapJournalsUntilCurrent(loop);
  SessionMidiEventVec gathered;
  TEST_ASSERT_TRUE(loop.tryGatherPublishedSnapshotEventsInWindow(gathered, 0, 3072));
  TEST_ASSERT_EQUAL(1, countNoteOns(gathered, 60));
  TEST_ASSERT_EQUAL(1, countNoteOns(gathered, 72));
}

void test_bounded_session_close_keeps_snapshot_work_and_limits_each_turn() {
  using Phase = Loop::SessionClose::Phase;
  for (const size_t quantum : {size_t(1), size_t(7), size_t(32)}) {
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
    pushClosedPendingAdd(loop, 10, 72, 400, 480);
    commitOverdubWrapLikeDevice(loop, 480, true);
    TEST_ASSERT_TRUE(loop.queueSessionExitSnapshot(CommitReason::OverdubStop, {}));
    for (PassId id = 100; id < 105; ++id)
      loop.pushOverdubSessionPass(id, EditPassIdList{10, 11, 12});
    Loop::SessionClose work;
    TEST_ASSERT_TRUE(loop.beginCloseOverdubSession(work));
    TEST_ASSERT_FALSE(loop.beginCloseOverdubSession(work));
    TEST_ASSERT_EQUAL(Phase::Denied, loop.stepCloseOverdubSession(work, 0));
    TEST_ASSERT_EQUAL(Phase::Passes, work.phase);
    const auto revision = loop.playbackRevision;
    const auto storage = loop.canonicalStorageGeneration;
    for (size_t turn = 0; work.phase != Phase::Complete && turn < 1000; ++turn) {
      const size_t before = work.units;
      TEST_ASSERT_TRUE(loop.hasOverdubSession());
      TEST_ASSERT_NOT_EQUAL(Phase::Denied, loop.stepCloseOverdubSession(work, quantum));
      TEST_ASSERT_TRUE(work.units - before <= quantum);
    }
    TEST_ASSERT_EQUAL(Phase::Complete, work.phase);
    TEST_ASSERT_FALSE(loop.hasOverdubSession());
    TEST_ASSERT_EQUAL(0, loop.overdubSessionUndoDepth());
    TEST_ASSERT_EQUAL(0, loop.overdubSessionRedoDepth());
    TEST_ASSERT_FALSE(loop.hasOverdubSourceView());
    TEST_ASSERT_EQUAL(revision, loop.playbackRevision);
    TEST_ASSERT_EQUAL(storage, loop.canonicalStorageGeneration);
    TEST_ASSERT_TRUE(loop.hasPendingWrapJournal());
    drainWrapJournalsUntilCurrent(loop);
    SessionMidiEventVec gathered;
    TEST_ASSERT_TRUE(loop.tryGatherPublishedSnapshotEventsInWindow(gathered, 0, 3072));
    TEST_ASSERT_EQUAL(1, countNoteOns(gathered, 60));
    TEST_ASSERT_EQUAL(1, countNoteOns(gathered, 72));
  }
}

void test_bounded_session_close_rejects_a_changed_session() {
  Loop loop;
  loop.openOverdubSession(0);
  loop.pushOverdubSessionPass(1, EditPassIdList{10, 11});
  Loop::SessionClose work;
  TEST_ASSERT_TRUE(loop.beginCloseOverdubSession(work));
  loop.pushOverdubSessionPass(2, {});
  TEST_ASSERT_EQUAL(Loop::SessionClose::Phase::Denied, loop.stepCloseOverdubSession(work, 1));
  TEST_ASSERT_EQUAL(0, work.units);
  TEST_ASSERT_TRUE(loop.hasOverdubSession());
  TEST_ASSERT_EQUAL(2, loop.overdubSessionUndoDepth());
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

  // `012714` wrap 2: snapshot current is not membership proof. Visual is still
  // behind, so sealed rows stay until the cache consumes the sealed revision.
  TEST_ASSERT_FALSE(loop.sealedCapturePreviewIsCovered());
  loop.releaseSealedCapturePreviewIfCovered();
  TEST_ASSERT_TRUE(loop.hasSealedCapturePreview());
  TEST_ASSERT_EQUAL(1u, loop.sealedCapturePreviewNotes().size());
}

/// `120321` wrap 2: retain must keep uncovered lap-1 sealed and merge lap-2 closed preview.
/// `clear()` then copy would leave only the latest lap (`sealed=78` → `85`).
void test_retain_at_seal_merges_uncovered_laps() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  loop.loopId = 7;
  loop.loopLengthTicks = 1536;
  recordLinearNote(loop, 0, 40, 60);

  loop.openOverdubSession(0);
  loop.beginCapture(CapturePhase::Overdub);
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(0, 1, 64, 90)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(624, 1, 64, 0)));
  TEST_ASSERT_EQUAL(CommitResult::Committed,
                    loop.commitCapturePass(CommitReason::OverdubWrap, 624));
  TEST_ASSERT_EQUAL(1u, loop.sealedCapturePreviewNotes().size());
  TEST_ASSERT_EQUAL(64u, loop.sealedCapturePreviewNotes()[0].note);
  TEST_ASSERT_EQUAL(624u, loop.sealedCapturePreviewNotes()[0].endTick);
  TEST_ASSERT_FALSE(loop.sealedCapturePreviewIsCovered());

  loop.beginCapture(CapturePhase::Overdub);
  TEST_ASSERT_TRUE(loop.hasSealedCapturePreview());
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(768, 1, 67, 90)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(1392, 1, 67, 0)));
  TEST_ASSERT_EQUAL(CommitResult::Committed,
                    loop.commitCapturePass(CommitReason::OverdubWrap, 1392));

  TEST_ASSERT_FALSE(loop.sealedCapturePreviewIsCovered());
  loop.releaseSealedCapturePreviewIfCovered();
  TEST_ASSERT_EQUAL(2u, loop.sealedCapturePreviewNotes().size());
  bool foundLap1 = false;
  bool foundLap2 = false;
  for (const NoteUtils::DisplayNote& note : loop.sealedCapturePreviewNotes()) {
    if (note.note == 64 && note.startTick == 0 && note.endTick == 624) {
      foundLap1 = true;
    }
    if (note.note == 67 && note.startTick == 768 && note.endTick == 1392) {
      foundLap2 = true;
    }
  }
  TEST_ASSERT_TRUE(foundLap1);
  TEST_ASSERT_TRUE(foundLap2);

  loop.beginCapture(CapturePhase::Overdub);
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(0, 1, 64, 90)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(480, 1, 64, 0)));
  TEST_ASSERT_EQUAL(CommitResult::Committed,
                    loop.commitCapturePass(CommitReason::OverdubWrap, 480));
  TEST_ASSERT_EQUAL(2u, loop.sealedCapturePreviewNotes().size());
  bool foundReplacedLap1 = false;
  foundLap2 = false;
  for (const NoteUtils::DisplayNote& note : loop.sealedCapturePreviewNotes()) {
    if (note.note == 64 && note.startTick == 0 && note.endTick == 480) {
      foundReplacedLap1 = true;
    }
    if (note.note == 64 && note.endTick == 624) {
      TEST_FAIL_MESSAGE("pitch+start upsert must not keep the replaced sealed geometry");
    }
    if (note.note == 67 && note.startTick == 768 && note.endTick == 1392) {
      foundLap2 = true;
    }
  }
  TEST_ASSERT_TRUE(foundReplacedLap1);
  TEST_ASSERT_TRUE(foundLap2);
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

  // Clean + matching revision is still not coverage until the sealed rows are present.
  loop.visualCacheDirty = false;
  TEST_ASSERT_FALSE(loop.sealedCapturePreviewIsCovered());
  for (const NoteUtils::DisplayNote& sealedRow : loop.sealedCapturePreviewNotes()) {
    loop.visualCache.notes.push_back(sealedRow);
  }
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
  loop.copyEffectiveCommittedEventsInRange(selectedFlat, 0, loop.loopLengthTicks);
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

void upsertDisplayNoteAtPitchStartFrom(NoteUtils::DisplayNoteVec& notes, size_t suffixBegin,
                                       const NoteUtils::DisplayNote& row,
                                       bool captureActive = true) {
  DisplayWindowUtils::upsertDisplayNoteAtPitchStartFrom(notes, suffixBegin, row, captureActive);
}

bool previewNoteIntersectsBar(const NoteUtils::DisplayNote& note, uint32_t bar,
                              uint32_t loopLength) {
  const uint32_t barStart = bar * Config::TICKS_PER_BAR;
  return DisplayWindowUtils::noteIntersectsWindow(note.startTick, note.endTick, barStart,
                                                  Config::TICKS_PER_BAR, loopLength);
}

struct DrawSourceGatherWalkCounts {
  uint32_t committedWalks = 0;
  uint32_t sealedWalks = 0;
  uint32_t previewWalks = 0;
  bool pendingApplied = false;
};

bool displayFrameCoversBar(const NoteUtils::DisplayNoteVec& frame, uint32_t bar,
                           uint32_t loopLength) {
  for (const NoteUtils::DisplayNote& note : frame) {
    if (previewNoteIntersectsBar(note, bar, loopLength)) {
      return true;
    }
  }
  return false;
}

void appendSnapshotNotesForBar(const LoopSnapshot& snapshot, uint32_t bar, uint8_t pitchLow,
                               uint8_t pitchHigh, NoteUtils::DisplayNoteVec& out,
                               uint32_t* walkCount = nullptr) {
  const uint8_t lastPitch =
      pitchHigh < kLoopSnapshotPitchCount ? pitchHigh
                                          : static_cast<uint8_t>(kLoopSnapshotPitchCount - 1);
  for (uint16_t pitch = pitchLow; pitch <= lastPitch; ++pitch) {
    const auto membership = snapshot.noteMembership(static_cast<uint8_t>(pitch), bar);
    forEachSnapshotMembershipReference(membership.get(),
                                       [&out, walkCount](const LoopSnapshotNoteRef& reference) {
      if (!reference) {
        return;
      }
      if (walkCount != nullptr) {
        ++(*walkCount);
      }
      appendDisplayNoteUniqueById(out, displayNoteFromSnapshotNote(*reference));
    });
  }
}

/// Host twin of `supplementDrawSourceFromUnpublishedSnapshotForUncoveredBars` (GAP-8).
void supplementDrawSourceFromUnpublishedSnapshotForUncoveredBars(
    const Loop& loop, uint32_t firstBar, uint32_t lastBar, uint8_t pitchLow, uint8_t pitchHigh,
    NoteUtils::DisplayNoteVec& frame, size_t& committedDisplayEnd) {
  const ImmutableLoopSnapshotRef unpublished = loop.unpublishedSnapshot();
  if (!DisplayWindowUtils::shouldSupplementDrawSourceFromUnpublishedSnapshotDuringPublicationGap(
          loop.isPlaybackSessionExitPublicationGap(), static_cast<bool>(unpublished))) {
    return;
  }
  const uint32_t loopLength = loop.loopLengthTicks;
  if (loopLength == 0 || firstBar > lastBar) {
    return;
  }
  const uint8_t resolvedPitchHigh = pitchHigh < pitchLow ? pitchLow : pitchHigh;
  for (uint32_t bar = firstBar; bar <= lastBar; ++bar) {
    if (displayFrameCoversBar(frame, bar, loopLength)) {
      continue;
    }
    appendSnapshotNotesForBar(*unpublished, bar, pitchLow, resolvedPitchHigh, frame);
  }
  if (committedDisplayEnd < frame.size()) {
    committedDisplayEnd = frame.size();
  }
}

void appendNotesIntersectingBar(const NoteUtils::DisplayNoteVec& source, uint32_t bar,
                                uint32_t loopLength, NoteUtils::DisplayNoteVec& out,
                                uint32_t* walkCount = nullptr) {
  for (const NoteUtils::DisplayNote& note : source) {
    if (walkCount != nullptr) {
      ++(*walkCount);
    }
    if (previewNoteIntersectsBar(note, bar, loopLength)) {
      appendDisplayNoteUniqueById(out, note);
    }
  }
}

void applyDrawSourceDisplayOverlays(const Loop& loop, uint32_t firstBar, uint32_t lastBar,
                                    NoteUtils::DisplayNoteVec& out, size_t prefixEnd,
                                    DrawSourceGatherWalkCounts* walks) {
  const uint32_t loopLength = loop.loopLengthTicks;
  const bool captureActive = loop.captureActive();
  if (DisplayWindowUtils::shouldUnionSealedCapturePreviewForDrawSource(
          loop.hasSealedCapturePreview(), loop.sealedCapturePreviewIsCovered(), captureActive,
          loop.hasOverdubSession())) {
    for (const NoteUtils::DisplayNote& sealedRow : loop.sealedCapturePreviewNotes()) {
      if (walks != nullptr) {
        ++walks->sealedWalks;
      }
      for (uint32_t bar = firstBar; bar <= lastBar; ++bar) {
        if (!previewNoteIntersectsBar(sealedRow, bar, loopLength)) {
          continue;
        }
        upsertDisplayNoteAtPitchStartFrom(out, 0, sealedRow, captureActive);
        break;
      }
    }
  }
  if (!captureActive && !loop.hasOverdubSession()) {
    return;
  }
  for (const NoteUtils::DisplayNote& previewNote : loop.capturePreview.notes) {
    if (walks != nullptr) {
      ++walks->previewWalks;
    }
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

/// Same committed+capture bar union as `DisplayManager::resolveDisplayNotesByDrawSource`
/// before playhead tails. Host-reachable without Track/DisplayManager.
void gatherDrawSourceDisplayNotes(const Loop& loop, uint32_t firstBar, uint32_t lastBar,
                                  uint32_t playheadBar, uint8_t pitchLow, uint8_t pitchHigh,
                                  NoteUtils::DisplayNoteVec& out, size_t* committedEnd = nullptr,
                                  DrawSourceGatherWalkCounts* walks = nullptr,
                                  bool reuseCommittedPrefix = false, bool applyPending = false) {
  const uint32_t loopLength = loop.loopLengthTicks;
  if (loopLength == 0 || firstBar > lastBar) {
    if (!reuseCommittedPrefix) {
      out.clear();
    }
    if (committedEnd != nullptr) {
      *committedEnd = out.size();
    }
    return;
  }
  size_t prefixEnd = 0;
  if (reuseCommittedPrefix) {
    prefixEnd = out.size();
  } else {
    out.clear();
    const ImmutableLoopSnapshotRef snapshot = loop.publishedSnapshot();
    const bool captureActive = loop.captureActive();
    const uint32_t windowStart = firstBar * Config::TICKS_PER_BAR;
    const uint32_t windowEnd = (lastBar + 1u) * Config::TICKS_PER_BAR;
    const uint32_t windowLength = windowEnd > windowStart ? windowEnd - windowStart : 0;
    const bool coversWindow =
        visualCacheCoversWindow(loop.visualCacheDirty, loop.visualCache.dirtyBars, windowStart,
                                windowLength, loopLength, Config::TICKS_PER_BAR);
    const bool uncoveredSealed =
        (loop.captureActive() || loop.hasOverdubSession()) &&
        loop.hasSealedCapturePreview() && !loop.sealedCapturePreviewIsCovered();
    const bool cacheReusable = DisplayWindowUtils::visualCacheReusableAsCommittedDrawSource(
        loop.visualCacheDirty, !loop.visualCache.notes.empty(), coversWindow, uncoveredSealed);
    const bool usePublishedSnapshot =
        DisplayWindowUtils::shouldGatherPublishedSnapshotForCompleteDrawSource(
            static_cast<bool>(snapshot), captureActive, loop.hasOverdubSession(),
            loop.publishedSnapshotIsCurrent(), cacheReusable, true);
    const uint8_t resolvedPitchHigh = pitchHigh < pitchLow ? pitchLow : pitchHigh;
    uint32_t* committedWalks = walks != nullptr ? &walks->committedWalks : nullptr;
    for (uint32_t bar = firstBar; bar <= lastBar; ++bar) {
      if (usePublishedSnapshot && snapshot) {
        appendSnapshotNotesForBar(*snapshot, bar, pitchLow, resolvedPitchHigh, out, committedWalks);
      } else {
        appendNotesIntersectingBar(loop.visualCache.notes, bar, loopLength, out, committedWalks);
      }
    }
    if (DisplayWindowUtils::shouldUnionSealedCapturePreviewForDrawSource(
            loop.hasSealedCapturePreview(), loop.sealedCapturePreviewIsCovered(), captureActive,
            loop.hasOverdubSession())) {
      for (const NoteUtils::DisplayNote& sealedRow : loop.sealedCapturePreviewNotes()) {
        for (uint32_t bar = firstBar; bar <= lastBar; ++bar) {
          if (!previewNoteIntersectsBar(sealedRow, bar, loopLength)) {
            continue;
          }
          upsertDisplayNoteAtPitchStartFrom(out, 0, sealedRow);
          break;
        }
      }
    }
    prefixEnd = out.size();
    if (DisplayWindowUtils::shouldRetainUnpublishedVisualCacheOverStaleGather(
            loop.publishedSnapshotIsCurrent(), captureActive || loop.hasOverdubSession(),
            loop.visualCache.notes.size(), prefixEnd,
            loop.hasSealedCapturePreview() && !loop.sealedCapturePreviewIsCovered())) {
      out.assign(loop.visualCache.notes.begin(), loop.visualCache.notes.end());
      prefixEnd = out.size();
    }
  }
  if (committedEnd != nullptr) {
    *committedEnd = prefixEnd;
  }
  applyDrawSourceDisplayOverlays(loop, firstBar, lastBar, out, prefixEnd, walks);
  if (applyPending && (loop.hasPendingNoteChanges() || loop.hasSealedCompanionOverlay())) {
    loop.applyPendingNoteChangesToDisplayNotes(out, prefixEnd);
    if (walks != nullptr) {
      walks->pendingApplied = true;
    }
  }
  (void)playheadBar;
}

uint32_t previewComposeMatchHost(const NoteUtils::DisplayNoteVec& compose,
                                 const NoteUtils::DisplayNote& preview) {
  const NoteUtils::DisplayNote* byId = findDisplayNoteById(compose, preview.noteId);
  if (byId != nullptr) {
    return (byId->note == preview.note && byId->startTick == preview.startTick &&
            byId->endTick == preview.endTick)
               ? 1u
               : 2u;
  }
  for (const NoteUtils::DisplayNote& note : compose) {
    if (note.note == preview.note && note.startTick == preview.startTick) {
      return 2u;
    }
  }
  return 0u;
}

void seedPublishedRecordGridHost(Loop& loop, uint32_t bars) {
  loop.loopLengthTicks = Config::TICKS_PER_BAR * bars;
  loop.beginCapture(CapturePhase::Record);
  for (uint32_t bar = 0; bar < bars; ++bar) {
    const uint32_t tick = bar * Config::TICKS_PER_BAR;
    const uint8_t pitch = static_cast<uint8_t>(84 - bar);
    TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(tick + 10, 1, pitch, 100)));
    TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(tick + 50, 1, pitch, 0)));
  }
  TEST_ASSERT_EQUAL(CommitResult::Committed,
                    loop.commitCapturePass(CommitReason::RecordStop, loop.loopLengthTicks - 48));
  drainPendingSnapshotPublish(loop);
  TEST_ASSERT_TRUE(loop.publishedSnapshotIsCurrent());
}

void pushClosedCapturePreviewRowHost(Loop& loop, NoteId noteId, uint8_t pitch, uint32_t startTick,
                                     uint32_t endTick) {
  loop.capturePreview.notes.push_back(makeAdoptedNote(noteId, pitch, startTick, endTick));
  CapturePreviewNoteState state{};
  state.open = false;
  loop.capturePreview.noteStates.push_back(state);
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

int findCaptureTailTargetIndex(const NoteUtils::DisplayNoteVec& notes, size_t regionStart,
                               const NoteUtils::DisplayNote& previewNote) {
  if (regionStart >= notes.size()) {
    return -1;
  }
  if (previewNote.noteId != kInvalidNoteId) {
    for (size_t i = regionStart; i < notes.size(); ++i) {
      if (notes[i].noteId == previewNote.noteId) {
        return static_cast<int>(i);
      }
    }
  }
  int openHoldMatch = -1;
  int invalidIdMatch = -1;
  int anyMatch = -1;
  for (size_t i = regionStart; i < notes.size(); ++i) {
    if (notes[i].note != previewNote.note || notes[i].startTick != previewNote.startTick) {
      continue;
    }
    if (DisplayWindowUtils::capturePreviewNoteIsOpenHold(notes[i].startTick, notes[i].endTick)) {
      openHoldMatch = static_cast<int>(i);
      break;
    }
    if (invalidIdMatch < 0 && notes[i].noteId == kInvalidNoteId) {
      invalidIdMatch = static_cast<int>(i);
    }
    if (anyMatch < 0) {
      anyMatch = static_cast<int>(i);
    }
  }
  if (openHoldMatch >= 0) {
    return openHoldMatch;
  }
  if (invalidIdMatch >= 0) {
    return invalidIdMatch;
  }
  return anyMatch;
}

/// Same body as `DisplayManagerInternal::applyCapturePlayheadTails`. Callers pass
/// `shouldAllowCapturePlayheadWrapContinuation` (OVERDUBBING+PLAYING → true).
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
    const int targetIndex = findCaptureTailTargetIndex(notes, captureRegionStart, previewNote);
    if (targetIndex >= 0) {
      notes[static_cast<size_t>(targetIndex)].startTick = noteStartTick;
      notes[static_cast<size_t>(targetIndex)].endTick = playheadEndTick;
    } else {
      NoteUtils::DisplayNote liveNote;
      liveNote.noteId = previewNote.noteId;
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
  TEST_ASSERT_FALSE(loop.hasSealedCapturePreview());
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

/// [`003558`] / [`170537`]: sealed replacement joins the committed prefix. Hide of the
/// old record id must not drop that replacement.
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
  bool sealedInPrefix = false;
  for (size_t i = 0; i < committedEnd; ++i) {
    if (drawn[i].note == 64 && drawn[i].startTick == 768 && drawn[i].endTick == 900) {
      TEST_ASSERT_TRUE(drawn[i].noteId != recordId);
      sealedInPrefix = true;
    }
  }
  TEST_ASSERT_TRUE(sealedInPrefix);

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
  TEST_ASSERT_TRUE(loop.visualCacheDirty);
}

NoteUtils::DisplayNote makeAdoptedNote(NoteId noteId, uint8_t pitch, uint32_t startTick,
                                       uint32_t endTick) {
  NoteUtils::DisplayNote note{};
  note.noteId = noteId;
  note.note = pitch;
  note.velocity = 90;
  note.startTick = startTick;
  note.endTick = endTick;
  return note;
}

/// 214900: NOTE_EDIT close while overdub dirties the cache; this gather is the
/// stale published snapshot (56). Retain the unpublished visual compose (65).
void test_draw_source_retains_unpublished_visual_cache_over_stale_gather_214900() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  loop.loopId = 7;
  loop.loopLengthTicks = 2304;
  loop.beginCapture(CapturePhase::Record);
  for (uint32_t i = 0; i < 56; ++i) {
    TEST_ASSERT_TRUE(loop.appendCaptureEvent(
        MidiEvent::NoteOn(i * 24u, 1, static_cast<uint8_t>(20 + (i % 20)), 100)));
    TEST_ASSERT_TRUE(loop.appendCaptureEvent(
        MidiEvent::NoteOff(i * 24u + 12u, 1, static_cast<uint8_t>(20 + (i % 20)), 0)));
  }
  TEST_ASSERT_EQUAL(CommitResult::Committed,
                    loop.commitCapturePass(CommitReason::RecordStop, 55u * 24u + 12u));
  drainPendingSnapshotPublish(loop);
  TEST_ASSERT_TRUE(loop.publishedSnapshotIsCurrent());

  NoteUtils::DisplayNoteVec unpublished;
  unpublished.reserve(65);
  for (uint32_t i = 1; i <= 65; ++i) {
    unpublished.push_back(makeAdoptedNote(static_cast<NoteId>(i), static_cast<uint8_t>(20 + (i % 30)),
                                          (i - 1u) * 24u, (i - 1u) * 24u + 12u));
  }
  for (uint32_t offset = 0; offset < 8; ++offset) {
    unpublished[57 + offset].noteId = static_cast<NoteId>(59 + offset);
    unpublished[57 + offset].note = 49;
  }
  loop.adoptComposedDisplayNotesFromViewport(unpublished);
  loop.visualCacheDirty = true;
  TEST_ASSERT_EQUAL_UINT32(65u, static_cast<uint32_t>(loop.visualCache.notes.size()));

  loop.openOverdubSession(0);
  loop.beginCapture(CapturePhase::Overdub);
  ++loop.playbackRevision;
  TEST_ASSERT_FALSE(loop.publishedSnapshotIsCurrent());

  NoteUtils::DisplayNoteVec drawn;
  gatherDrawSourceDisplayNotes(loop, 0, 2, 0, 0, 127, drawn);
  TEST_ASSERT_EQUAL_UINT32(65u, static_cast<uint32_t>(drawn.size()));
  bool found59 = false;
  bool found66 = false;
  for (const NoteUtils::DisplayNote& note : drawn) {
    if (note.noteId == 59 && note.note == 49) {
      found59 = true;
    }
    if (note.noteId == 66 && note.note == 49) {
      found66 = true;
    }
  }
  TEST_ASSERT_TRUE(found59);
  TEST_ASSERT_TRUE(found66);
}

void adoptFourBarLinearCache(Loop& loop, NoteId pitch39Id) {
  NoteUtils::DisplayNoteVec adopted;
  adopted.push_back(makeAdoptedNote(1, 60, 48, 64));
  adopted.push_back(makeAdoptedNote(2, 60, 768, 800));
  adopted.push_back(makeAdoptedNote(3, 60, 1536, 1568));
  adopted.push_back(makeAdoptedNote(pitch39Id, 39, 2976, 3055));
  loop.adoptComposedDisplayNotesFromViewport(adopted);
  TEST_ASSERT_FALSE(loop.visualCacheDirty);
}

/// [`092344`] OD2 after adopt: clean nonempty cache wins over a stale wrap snapshot.
void test_draw_source_od2_clean_adopt_keeps_linear_tail() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  loop.loopId = 7;
  loop.loopLengthTicks = 3072;
  recordLinearNote(loop, 48, 64, 60);
  TEST_ASSERT_TRUE(loop.publishedSnapshotIsCurrent());

  loop.openOverdubSession(0);
  loop.beginCapture(CapturePhase::Overdub);
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(2976, 1, 39, 90)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(2784, 1, 39, 0)));
  const uint32_t priorRevision = loop.playbackRevision;
  TEST_ASSERT_EQUAL(CommitResult::Committed,
                    loop.commitCapturePass(CommitReason::OverdubWrap, 2784));
  const PassId wrapPassId = loop.lastCommittedPassId();
  PendingNoteChange wrapAdd{};
  wrapAdd.kind = PendingNoteChangeKind::Add;
  wrapAdd.noteId = 144;
  wrapAdd.channel = 1;
  wrapAdd.pitch = 39;
  wrapAdd.velocity = 90;
  wrapAdd.startTick = 2976;
  wrapAdd.endTick = 2784;
  loop.pushPendingNoteChangeForTest(wrapAdd);
  TEST_ASSERT_TRUE(loop.queueOverdubWrapSnapshotJournal(wrapPassId));
  loop.pushOverdubSessionPass(wrapPassId, {});
  loop.recordLastOverdubSeal(wrapPassId, {}, priorRevision);

  size_t steps = 0;
  while (loop.hasPendingWrapJournal() && steps < 10000) {
    loop.stepPendingSnapshotPublish(1, true);
    ++steps;
  }
  TEST_ASSERT_FALSE(loop.hasPendingWrapJournal());
  TEST_ASSERT_TRUE(loop.publishedSnapshotIsCurrent());
  const LoopSnapshotNoteRef snapWrap = loop.publishedSnapshot()->findNote(144, 39);
  TEST_ASSERT_TRUE(static_cast<bool>(snapWrap));
  TEST_ASSERT_EQUAL_UINT32(2976u, snapWrap->startTick);
  TEST_ASSERT_EQUAL_UINT32(2784u, snapWrap->endTick);

  loop.closeOverdubSession();
  adoptFourBarLinearCache(loop, 144);
  ++loop.playbackRevision;
  TEST_ASSERT_FALSE(loop.publishedSnapshotIsCurrent());
  TEST_ASSERT_TRUE(static_cast<bool>(loop.publishedSnapshot()));

  loop.openOverdubSession(0);
  loop.beginCapture(CapturePhase::Overdub);
  TEST_ASSERT_TRUE(loop.captureActive());
  TEST_ASSERT_TRUE(loop.hasOverdubSession());
  TEST_ASSERT_FALSE(loop.visualCacheDirty);
  TEST_ASSERT_FALSE(loop.publishedSnapshotIsCurrent());

  NoteUtils::DisplayNoteVec drawn;
  gatherDrawSourceDisplayNotes(loop, 0, 3, 0, 0, 127, drawn);
  bool foundLinear = false;
  bool foundWrap = false;
  for (const NoteUtils::DisplayNote& note : drawn) {
    if (note.note != 39 || note.startTick != 2976u) {
      continue;
    }
    if (note.endTick == 3055u) {
      foundLinear = true;
    }
    if (note.endTick == 2784u) {
      foundWrap = true;
    }
  }
  TEST_ASSERT_TRUE(foundLinear);
  TEST_ASSERT_FALSE(foundWrap);
}

/// No published snapshot: gather the clean adopted cache, never a snapshot prefix.
void test_draw_source_no_snapshot_uses_clean_visual_cache() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  loop.loopId = 7;
  loop.loopLengthTicks = 3072;
  adoptFourBarLinearCache(loop, 144);
  loop.openOverdubSession(0);
  loop.beginCapture(CapturePhase::Overdub);
  TEST_ASSERT_FALSE(static_cast<bool>(loop.publishedSnapshot()));
  TEST_ASSERT_FALSE(loop.visualCacheDirty);

  NoteUtils::DisplayNoteVec drawn;
  gatherDrawSourceDisplayNotes(loop, 0, 3, 0, 0, 127, drawn);
  bool foundLinear = false;
  for (const NoteUtils::DisplayNote& note : drawn) {
    if (note.note == 39 && note.startTick == 2976u && note.endTick == 3055u) {
      foundLinear = true;
    }
  }
  TEST_ASSERT_TRUE(foundLinear);
}

/// No published snapshot and empty dirty cache: existing empty fallback.
void test_draw_source_no_snapshot_empty_dirty_cache_stays_empty() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  loop.loopId = 7;
  loop.loopLengthTicks = 3072;
  loop.visualCache.notes.clear();
  loop.visualCacheDirty = true;
  loop.openOverdubSession(0);
  loop.beginCapture(CapturePhase::Overdub);
  TEST_ASSERT_FALSE(static_cast<bool>(loop.publishedSnapshot()));

  NoteUtils::DisplayNoteVec drawn;
  gatherDrawSourceDisplayNotes(loop, 0, 3, 0, 0, 127, drawn);
  TEST_ASSERT_TRUE(drawn.empty());
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

  size_t steps = 0;
  while (loop.hasPendingWrapJournal() && steps < 10000) {
    loop.stepPendingSnapshotPublish(1, true);
    ++steps;
  }
  TEST_ASSERT_FALSE(loop.hasPendingWrapJournal());
  TEST_ASSERT_TRUE(static_cast<bool>(loop.publishedSnapshot()));
  loop.closeOverdubSession();
  TEST_ASSERT_FALSE(loop.captureActive());
  TEST_ASSERT_FALSE(loop.hasOverdubSession());
  for (const auto& row : chord) {
    const LoopSnapshotNoteRef snapNote = loop.publishedSnapshot()->findNote(row.noteId, row.pitch);
    TEST_ASSERT_TRUE(static_cast<bool>(snapNote));
    TEST_ASSERT_EQUAL_UINT32(2640u, snapNote->startTick);
    TEST_ASSERT_EQUAL_UINT32(0u, snapNote->endTick);
    TEST_ASSERT_TRUE(snapNote->isWrap(3072));
  }

  NoteUtils::DisplayNoteVec adopted;
  adopted.push_back(makeAdoptedNote(1, 60, 100, 200));
  adopted.push_back(makeAdoptedNote(2, 60, 768, 800));
  adopted.push_back(makeAdoptedNote(3, 60, 1536, 1568));
  for (const auto& row : chord) {
    adopted.push_back(makeAdoptedNote(row.noteId, row.pitch, 2640, row.linearEnd));
  }
  loop.adoptComposedDisplayNotesFromViewport(adopted);
  TEST_ASSERT_FALSE(loop.visualCacheDirty);
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

/// [`011906`] wrap seal: session still open after `captureActive` goes false.
void test_draw_source_keeps_snapshot_when_wrap_clears_capture() {
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
  loop.clearPendingNoteChanges();
  loop.pushOverdubSessionPass(wrapPassId, {});
  loop.recordLastOverdubSeal(wrapPassId, {}, priorRevision);
  TEST_ASSERT_FALSE(loop.captureActive());
  TEST_ASSERT_TRUE(loop.hasOverdubSession());

  size_t steps = 0;
  while (loop.hasPendingWrapJournal() && steps < 10000) {
    loop.stepPendingSnapshotPublish(1, true);
    ++steps;
  }
  TEST_ASSERT_FALSE(loop.hasPendingWrapJournal());
  TEST_ASSERT_TRUE(loop.publishedSnapshotIsCurrent());

  NoteUtils::DisplayNoteVec drawn;
  gatherDrawSourceDisplayNotes(loop, 0, 3, 0, 0, 127, drawn);
  bool foundWrapOne = false;
  for (const NoteUtils::DisplayNote& note : drawn) {
    if (note.note == 72 && note.startTick == 400 && note.endTick == 480) {
      foundWrapOne = true;
    }
  }
  TEST_ASSERT_TRUE(foundWrapOne);
  TEST_ASSERT_EQUAL(visualBefore, loop.visualCache.notes.size());
}

/// [`012714`] wrap 2: snap-current must keep sealed bar-3 notes in the session frame.
void test_draw_source_sealed_survives_current_snapshot() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  loop.loopId = 7;
  loop.loopLengthTicks = 3072;
  recordLinearNote(loop, 100, 200, 60);
  TEST_ASSERT_TRUE(loop.publishedSnapshotIsCurrent());

  loop.openOverdubSession(0);
  loop.beginCapture(CapturePhase::Overdub);
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(2304, 1, 64, 90)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(2784, 1, 64, 0)));
  const uint32_t priorRevision = loop.playbackRevision;
  TEST_ASSERT_EQUAL(CommitResult::Committed,
                    loop.commitCapturePass(CommitReason::OverdubWrap, 2784));
  const PassId wrapPassId = loop.lastCommittedPassId();

  TEST_ASSERT_TRUE(loop.hasSealedCapturePreview());
  TEST_ASSERT_FALSE(loop.sealedCapturePreviewIsCovered());

  PendingNoteChange wrapAdd{};
  wrapAdd.kind = PendingNoteChangeKind::Add;
  wrapAdd.noteId = 21;
  wrapAdd.channel = 1;
  wrapAdd.pitch = 64;
  wrapAdd.velocity = 90;
  wrapAdd.startTick = 2304;
  wrapAdd.endTick = 2784;
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
  TEST_ASSERT_TRUE(loop.publishedSnapshotIsCurrent());
  TEST_ASSERT_TRUE(loop.hasSealedCapturePreview());
  TEST_ASSERT_FALSE(loop.sealedCapturePreviewIsCovered());

  NoteUtils::DisplayNoteVec drawn;
  gatherDrawSourceDisplayNotes(loop, 0, 3, 0, 0, 127, drawn);
  bool foundSealedBarThree = false;
  for (const NoteUtils::DisplayNote& note : drawn) {
    if (note.note == 64 && note.startTick == 2304 && note.endTick == 2784) {
      foundSealedBarThree = true;
    }
  }
  TEST_ASSERT_TRUE(foundSealedBarThree);
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

/// Stage 0/2 fixture from [`session_20260910_175831.log`](../../captures/session_20260910_175831.log):
/// OD2 pitch 18 On@2095 (abs 6703), Off@6 (abs 6918), loopLength 2304.
/// A — pre-wrap open hold: On → currentTick. B — post-wrap open hold with
/// wrap continuation: On → loopEnd-1 plus 0 → currentTick. Policy gate is
/// OVERDUBBING+PLAYING → YES.
void test_overdub_live_wrap_playhead_tails_175831_ab() {
  constexpr uint32_t kLoopLength = 2304;
  constexpr uint32_t kOnTick = 2095;
  constexpr uint32_t kPreWrapClose = 2200;
  constexpr uint32_t kPostWrapClose = 6;
  constexpr uint8_t kPitch = 18;
  const NoteId noteId = 57;

  TEST_ASSERT_TRUE(DisplayWindowUtils::shouldAllowCapturePlayheadWrapContinuation(
      false, true, true));

  CapturePreview preview;
  NoteUtils::DisplayNote openRow{};
  openRow.noteId = noteId;
  openRow.note = kPitch;
  openRow.velocity = 98;
  openRow.startTick = kOnTick;
  openRow.endTick = kOnTick;
  preview.notes.push_back(openRow);
  CapturePreviewNoteState state{};
  state.open = true;
  state.wrapHeld = false;
  preview.noteStates.push_back(state);
  preview.openNoteIndices.push_back(0);

  NoteUtils::DisplayNoteVec drawnA = preview.notes;
  const bool allowWrap =
      DisplayWindowUtils::shouldAllowCapturePlayheadWrapContinuation(false, true, true);
  applyCapturePlayheadTails(preview, kLoopLength, kPreWrapClose, 0, drawnA, allowWrap);
  TEST_ASSERT_EQUAL(1u, drawnA.size());
  TEST_ASSERT_EQUAL_UINT8(kPitch, drawnA[0].note);
  TEST_ASSERT_EQUAL_UINT32(kOnTick, drawnA[0].startTick);
  TEST_ASSERT_EQUAL_UINT32(kPreWrapClose, drawnA[0].endTick);

  preview.noteStates[0].wrapHeld = true;
  NoteUtils::DisplayNoteVec drawnB = preview.notes;
  applyCapturePlayheadTails(preview, kLoopLength, kPostWrapClose, 0, drawnB, allowWrap);
  TEST_ASSERT_EQUAL(2u, drawnB.size());
  bool sawTail = false;
  bool sawHead = false;
  for (const NoteUtils::DisplayNote& note : drawnB) {
    TEST_ASSERT_EQUAL_UINT8(kPitch, note.note);
    if (note.startTick == kOnTick && note.endTick == kLoopLength - 1u) {
      sawTail = true;
    }
    if (note.startTick == 0u && note.endTick == kPostWrapClose) {
      sawHead = true;
    }
  }
  TEST_ASSERT_TRUE(sawTail);
  TEST_ASSERT_TRUE(sawHead);

  NoteUtils::DisplayNoteVec drawnStage1 = preview.notes;
  applyCapturePlayheadTails(preview, kLoopLength, kPostWrapClose, 0, drawnStage1, false);
  TEST_ASSERT_EQUAL(1u, drawnStage1.size());
  TEST_ASSERT_EQUAL_UINT32(kOnTick, drawnStage1[0].startTick);
  TEST_ASSERT_EQUAL_UINT32(kOnTick, drawnStage1[0].endTick);
}

/// Stage 3 fixture from [`session_20260910_180916.log`]: pitch 17 WRAP
/// On@2088 Off@29 on loop 2304. LCR passes alone keeps head only; wrap-held
/// append restores On→loopEnd-1 and 0→Off (STOPPED paint order).
void test_stopped_passes_resolve_append_wrap_held_180916() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  constexpr uint32_t kLoopLength = 2304;
  constexpr uint32_t kOnTick = 2088;
  constexpr uint32_t kOffTick = 29;
  constexpr uint8_t kPitch = 17;
  loop.loopId = 7;
  loop.loopLengthTicks = kLoopLength;
  recordLinearNote(loop, 100, 200, 60);

  loop.openOverdubSession(0);
  loop.beginCapture(CapturePhase::Overdub, 0);
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(kOnTick, 1, kPitch, 100)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(kOffTick, 1, kPitch, 0)));
  TEST_ASSERT_EQUAL(CommitResult::Committed,
                    loop.commitCapturePass(CommitReason::OverdubStop, kOffTick));

  NoteUtils::DisplayNoteVec passesOnly;
  LoopContentResolution::resolveDisplayNotes(loop.passes, kLoopLength, passesOnly);
  bool passesSawTail = false;
  bool passesSawHead = false;
  for (const NoteUtils::DisplayNote& note : passesOnly) {
    if (note.note != kPitch) {
      continue;
    }
    if (note.startTick == kOnTick && note.endTick == kLoopLength - 1u) {
      passesSawTail = true;
    }
    if (note.startTick == 0u && note.endTick == kOffTick) {
      passesSawHead = true;
    }
  }
  TEST_ASSERT_FALSE(passesSawTail);
  TEST_ASSERT_TRUE(passesSawHead);

  NoteUtils::DisplayNoteVec withAppend = passesOnly;
  loop.appendOverdubPassDisplayNotes(withAppend);
  bool appendSawTail = false;
  bool appendSawHead = false;
  for (const NoteUtils::DisplayNote& note : withAppend) {
    if (note.note != kPitch) {
      continue;
    }
    if (note.startTick == kOnTick && note.endTick == kLoopLength - 1u) {
      appendSawTail = true;
    }
    if (note.startTick == 0u && note.endTick == kOffTick) {
      appendSawHead = true;
    }
  }
  TEST_ASSERT_TRUE(appendSawTail);
  TEST_ASSERT_TRUE(appendSawHead);
  TEST_ASSERT_GREATER_THAN(passesOnly.size(), withAppend.size());
}

/// Stage 3B ([`183741`](../../captures/session_20260910_183741.log)): STOPPED
/// draw-source paints current-snapshot spans missing wrap On→loopEnd. Gate +
/// append completes the candidate; snapshot remains base (no VC preference).
void test_stopped_draw_source_snapshot_append_wrap_held_183741() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  constexpr uint32_t kLoopLength = 2304;
  constexpr uint32_t kOnTick = 2088;
  constexpr uint32_t kOffTick = 29;
  constexpr uint8_t kPitch = 17;
  loop.loopId = 7;
  loop.loopLengthTicks = kLoopLength;
  recordLinearNote(loop, 100, 200, 60);

  loop.openOverdubSession(0);
  loop.beginCapture(CapturePhase::Overdub, 0);
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(kOnTick, 1, kPitch, 100)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(kOffTick, 1, kPitch, 0)));
  TEST_ASSERT_EQUAL(CommitResult::Committed,
                    loop.commitCapturePass(CommitReason::OverdubStop, kOffTick));
  const bool published = loop.tryPublishSessionExitSnapshot(CommitReason::OverdubStop, {});
  drainPendingSnapshotPublish(loop);
  TEST_ASSERT_TRUE(published);
  TEST_ASSERT_TRUE(loop.publishedSnapshotIsCurrent());
  TEST_ASSERT_FALSE(loop.captureActive());
  TEST_ASSERT_TRUE(DisplayWindowUtils::shouldCompleteStoppedDrawSourceWrapHeld(
      true, loop.captureActive(), loop.publishedSnapshotIsCurrent()));

  // Draw-source may settle an incomplete committed candidate (passes LCR /
  // incomplete snapshot projection). Same completion helper as Stage 3A.
  NoteUtils::DisplayNoteVec candidate;
  LoopContentResolution::resolveDisplayNotes(loop.passes, kLoopLength, candidate);
  bool candidateSawTail = false;
  bool candidateSawHead = false;
  for (const NoteUtils::DisplayNote& note : candidate) {
    if (note.note != kPitch) {
      continue;
    }
    if (note.startTick == kOnTick && note.endTick == kLoopLength - 1u) {
      candidateSawTail = true;
    }
    if (note.startTick == 0u && note.endTick == kOffTick) {
      candidateSawHead = true;
    }
  }
  TEST_ASSERT_FALSE(candidateSawTail);
  TEST_ASSERT_TRUE(candidateSawHead);

  NoteUtils::DisplayNoteVec completed = candidate;
  loop.appendOverdubPassDisplayNotes(completed);
  bool completedSawTail = false;
  bool completedSawHead = false;
  for (const NoteUtils::DisplayNote& note : completed) {
    if (note.note != kPitch) {
      continue;
    }
    if (note.startTick == kOnTick && note.endTick == kLoopLength - 1u) {
      completedSawTail = true;
    }
    if (note.startTick == 0u && note.endTick == kOffTick) {
      completedSawHead = true;
    }
  }
  TEST_ASSERT_TRUE(completedSawTail);
  TEST_ASSERT_TRUE(completedSawHead);
  TEST_ASSERT_GREATER_THAN(candidate.size(), completed.size());
}

/// Same NoteId wrap head+tail must both survive unique-by-id gather
/// ([`193108`](../../captures/session_20260910_193108.log) STOPPED dropped On@2151).
void test_append_display_note_unique_by_id_keeps_wrap_segments() {
  NoteUtils::DisplayNoteVec out;
  constexpr NoteId kId = 57;
  appendDisplayNoteUniqueById(out, NoteUtils::DisplayNote{kId, 18, 98, 2151, 2303});
  appendDisplayNoteUniqueById(out, NoteUtils::DisplayNote{kId, 18, 98, 0, 81});
  appendDisplayNoteUniqueById(out, NoteUtils::DisplayNote{kId, 18, 98, 0, 81});
  TEST_ASSERT_EQUAL(2u, out.size());
  TEST_ASSERT_EQUAL_UINT32(2151u, out[0].startTick);
  TEST_ASSERT_EQUAL_UINT32(0u, out[1].startTick);
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
    loop.copyEffectiveCommittedEventsInRange(gathered, 0, 3072, false);
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

/// GAP-8: publication-gap handoff may omit bars 2–5; unpublished snapshot supplements them.
/// Stage 7 (`130411` @ `111807617`): closed overdub preview rows must appear in
/// snapshot-backed compose while capture is active and snap is current.
void test_stage7_snapshot_compose_includes_active_overdub_preview_130411() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  loop.loopId = 7;
  constexpr uint32_t kBars = 4;
  constexpr uint32_t kLastBar = kBars - 1;
  seedPublishedRecordGridHost(loop, kBars);
  loop.openOverdubSession(0);
  loop.beginCapture(CapturePhase::Overdub);
  struct PreviewRow {
    NoteId id;
    uint8_t pitch;
    uint32_t start;
    uint32_t end;
  };
  const PreviewRow slice[] = {{334, 76, 384, 432}, {335, 75, 432, 480}, {336, 74, 480, 528},
                              {337, 73, 528, 576}, {338, 72, 576, 624}, {339, 71, 624, 672},
                              {340, 70, 672, 720}};
  for (const PreviewRow& row : slice) {
    pushClosedCapturePreviewRowHost(loop, row.id, row.pitch, row.start, row.end);
  }
  TEST_ASSERT_TRUE(loop.captureActive());
  TEST_ASSERT_TRUE(loop.publishedSnapshotIsCurrent());

  NoteUtils::DisplayNoteVec compose;
  gatherDrawSourceDisplayNotes(loop, 0, kLastBar, 0, 0, 127, compose);
  for (const PreviewRow& row : slice) {
    const NoteUtils::DisplayNote preview = makeAdoptedNote(row.id, row.pitch, row.start, row.end);
    TEST_ASSERT_EQUAL_UINT32(1u, previewComposeMatchHost(compose, preview));
  }
}

/// Stage 7 (`130411`): between-pass overdub idle keeps closed preview rows that
/// must still union into compose when the session is open.
void test_stage7_snapshot_compose_includes_idle_overdub_preview_130411() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  loop.loopId = 7;
  constexpr uint32_t kBars = 4;
  constexpr uint32_t kLastBar = kBars - 1;
  seedPublishedRecordGridHost(loop, kBars);
  loop.openOverdubSession(0);
  loop.beginCapture(CapturePhase::Overdub);
  pushClosedCapturePreviewRowHost(loop, 334, 76, 384, 432);
  loop.capture.store.clear();
  loop.capture.phase = CapturePhase::None;
  TEST_ASSERT_FALSE(loop.captureActive());
  TEST_ASSERT_TRUE(loop.hasOverdubSession());
  TEST_ASSERT_TRUE(loop.publishedSnapshotIsCurrent());

  NoteUtils::DisplayNoteVec compose;
  gatherDrawSourceDisplayNotes(loop, 0, kLastBar, 0, 0, 127, compose);
  const NoteUtils::DisplayNote preview = makeAdoptedNote(334, 76, 384, 432);
  TEST_ASSERT_EQUAL_UINT32(1u, previewComposeMatchHost(compose, preview));
}

void test_gap_handoff_supplements_uncovered_bars_from_unpublished_snapshot() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  loop.loopId = 7;
  constexpr uint32_t kBars = 25;
  const uint32_t loopLength = Config::TICKS_PER_BAR * kBars;
  loop.loopLengthTicks = loopLength;
  loop.beginCapture(CapturePhase::Record);
  for (uint32_t bar = 0; bar < kBars; ++bar) {
    const uint32_t tick = bar * Config::TICKS_PER_BAR;
    TEST_ASSERT_TRUE(loop.appendCaptureEvent(
        MidiEvent::NoteOn(tick + 100, 1, static_cast<uint8_t>(60 + (bar % 20)), 100)));
    TEST_ASSERT_TRUE(loop.appendCaptureEvent(
        MidiEvent::NoteOff(tick + 200, 1, static_cast<uint8_t>(60 + (bar % 20)), 0)));
  }
  applyLiveRecordCompletedTicks(loop, loopLength);
  TEST_ASSERT_EQUAL(CommitResult::Committed,
                    loop.commitCapturePass(CommitReason::RecordStop, loopLength - 48));
  TEST_ASSERT_TRUE(loop.isPlaybackSessionExitPublicationGap());
  TEST_ASSERT_NOT_NULL(loop.unpublishedSnapshot().get());
  for (uint32_t bar = 2; bar <= 5; ++bar) {
    TEST_ASSERT_NOT_NULL(loop.unpublishedSnapshot()->noteMembership(60 + bar, bar).get());
  }

  NoteUtils::DisplayNoteVec handoff;
  for (uint32_t bar = 16; bar <= 20; ++bar) {
    appendSnapshotNotesForBar(*loop.unpublishedSnapshot(), bar, 0, 127, handoff);
  }
  for (uint32_t bar = 2; bar <= 5; ++bar) {
    TEST_ASSERT_FALSE(displayFrameCoversBar(handoff, bar, loopLength));
  }

  size_t committedEnd = handoff.size();
  supplementDrawSourceFromUnpublishedSnapshotForUncoveredBars(loop, 0, 20, 0, 127, handoff,
                                                              committedEnd);
  for (uint32_t bar = 2; bar <= 5; ++bar) {
    TEST_ASSERT_TRUE(displayFrameCoversBar(handoff, bar, loopLength));
  }
  TEST_ASSERT_TRUE(committedEnd >= handoff.size());

  drainPendingSnapshotPublish(loop);
  TEST_ASSERT_FALSE(loop.isPlaybackSessionExitPublicationGap());
  const size_t sizeBeforeNoOp = handoff.size();
  supplementDrawSourceFromUnpublishedSnapshotForUncoveredBars(loop, 2, 5, 0, 127, handoff,
                                                              committedEnd);
  TEST_ASSERT_EQUAL(sizeBeforeNoOp, handoff.size());
}

void test_session_exit_handoff_when_live_record_notes_fit_sealed_length() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  loop.loopId = 7;
  constexpr uint32_t kSealedLength = Config::TICKS_PER_BAR * 12;
  constexpr uint32_t kLiveRecordExtraBars = 2u;
  constexpr uint32_t kProvisionalLength =
      kSealedLength + kLiveRecordExtraBars * Config::TICKS_PER_BAR;
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

void test_session_exit_handoff_when_candidate_shorter_than_sealed_length() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  loop.loopId = 7;
  constexpr uint32_t kSealedLength = Config::TICKS_PER_BAR * 4;
  loop.loopLengthTicks = kSealedLength;
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
  const size_t appliedEventIndex = loop.liveRecordAppliedEventIndex();
  TEST_ASSERT_TRUE(appliedEventIndex > 0);
  TEST_ASSERT_TRUE(loop.hasInFlightSnapshotConstruction());

  TEST_ASSERT_EQUAL(CommitResult::Committed,
                    loop.commitCapturePass(CommitReason::RecordStop, 200));
  TEST_ASSERT_TRUE(loop.pendingSnapshotSessionExitQueued());
  TEST_ASSERT_EQUAL(kSealedLength, loop.loopLengthTicks);

  loop.stepPendingSnapshotPublish(1, true);
  TEST_ASSERT_EQUAL(appliedEventIndex, loop.liveRecordAppliedEventIndex());
  TEST_ASSERT_TRUE(loop.hasPendingSnapshotPublish());
  TEST_ASSERT_EQUAL(kSealedLength, loop.pendingSnapshotCandidateLengthTicks());

  drainPendingSnapshotPublish(loop);
  TEST_ASSERT_TRUE(loop.publishedSnapshotIsCurrent());
  TEST_ASSERT_EQUAL(kSealedLength, loop.publishedSnapshot()->loopLengthTicks());
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

namespace {
GlobalUndoStack* g_testSessionExitUndoAttachStack = nullptr;

void testAttachAfterByOriginatingEntryId(Loop& loop, ImmutableLoopSnapshotRef published,
                                         uint32_t entryId) {
  (void)loop;
  if (g_testSessionExitUndoAttachStack == nullptr || !published || entryId == 0) {
    return;
  }
  for (UndoEntry& entry : g_testSessionExitUndoAttachStack->entries) {
    if (entry.id != entryId) {
      continue;
    }
    if (entry.kind != UndoEntryKind::RecordPassAdded &&
        entry.kind != UndoEntryKind::OverdubPassAdded) {
      return;
    }
    if (!entry.afterPublishedSnapshot) {
      entry.afterPublishedSnapshot = std::move(published);
    }
    return;
  }
}
}  // namespace

void test_session_exit_published_undo_attach_targets_originating_entry_id() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();

  Loop loop;
  loop.loopId = 42;
  loop.loopLengthTicks = 768;
  recordLinearNote(loop, 10, 20, 60);
  const ImmutableLoopSnapshotRef afterRecord = loop.publishedSnapshot();
  TEST_ASSERT_NOT_NULL(afterRecord.get());

  GlobalUndoStack stack;
  UndoEntry entryA{};
  entryA.id = 11;
  entryA.kind = UndoEntryKind::OverdubPassAdded;
  entryA.loopId = loop.loopId;
  entryA.beforePublishedSnapshot = afterRecord;
  UndoEntry entryB{};
  entryB.id = 22;
  entryB.kind = UndoEntryKind::NoteEditPassClosed;
  entryB.loopId = loop.loopId;
  stack.entries.push_back(std::move(entryA));
  stack.entries.push_back(std::move(entryB));
  stack.cursor = 2;

  loop.beginCapture(CapturePhase::Overdub);
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(40, 1, 64, 90)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(50, 1, 64, 0)));
  TEST_ASSERT_EQUAL(CommitResult::Committed,
                    loop.commitCapturePass(CommitReason::OverdubStop, 50));
  TEST_ASSERT_TRUE(loop.queueSessionExitSnapshot(CommitReason::OverdubStop, {}));
  loop.setPendingSessionExitUndoAttachEntryId(11);
  TEST_ASSERT_EQUAL_UINT32(11u, loop.pendingSessionExitUndoAttachEntryId());

  g_testSessionExitUndoAttachStack = &stack;
  Loop::setSessionExitPublishedUndoAttachFn(testAttachAfterByOriginatingEntryId);
  drainPendingSnapshotPublish(loop);
  Loop::setSessionExitPublishedUndoAttachFn(nullptr);
  g_testSessionExitUndoAttachStack = nullptr;

  const ImmutableLoopSnapshotRef afterOverdub = loop.publishedSnapshot();
  TEST_ASSERT_NOT_NULL(afterOverdub.get());
  TEST_ASSERT_TRUE(afterOverdub.get() != afterRecord.get());
  // Pin A: originating entry A received after; tip entry B unchanged.
  TEST_ASSERT_EQUAL_PTR(afterOverdub.get(), stack.entries[0].afterPublishedSnapshot.get());
  TEST_ASSERT_NULL(stack.entries[1].afterPublishedSnapshot.get());

  loop.storePublishedSnapshotForTest(stack.entries[0].beforePublishedSnapshot,
                                     loop.playbackRevision);
  TEST_ASSERT_EQUAL_PTR(afterRecord.get(), loop.publishedSnapshot().get());
  loop.storePublishedSnapshotForTest(stack.entries[0].afterPublishedSnapshot,
                                     loop.playbackRevision);
  TEST_ASSERT_EQUAL_PTR(afterOverdub.get(), loop.publishedSnapshot().get());
}

void test_clear_slot_before_published_snapshot_field_retains_root() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  loop.loopId = 7;
  loop.loopLengthTicks = 768;
  recordLinearNote(loop, 10, 20, 60);
  const ImmutableLoopSnapshotRef beforeClear = loop.publishedSnapshot();
  TEST_ASSERT_NOT_NULL(beforeClear.get());

  UndoEntry clearEntry{};
  clearEntry.kind = UndoEntryKind::ClearSlot;
  clearEntry.loopId = loop.loopId;
  clearEntry.beforePublishedSnapshot = beforeClear;
  loop.resetPassTimeline();
  TEST_ASSERT_NULL(loop.publishedSnapshot().get());
  loop.storePublishedSnapshotForTest(clearEntry.beforePublishedSnapshot, loop.playbackRevision);
  TEST_ASSERT_EQUAL_PTR(beforeClear.get(), loop.publishedSnapshot().get());
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

void assertSameDrawSourceUnion(const NoteUtils::DisplayNoteVec& expected,
                               const NoteUtils::DisplayNoteVec& actual) {
  TEST_ASSERT_EQUAL(expected.size(), actual.size());
  for (const NoteUtils::DisplayNote& exp : expected) {
    bool found = false;
    for (const NoteUtils::DisplayNote& act : actual) {
      if (act.noteId == exp.noteId && act.note == exp.note && act.startTick == exp.startTick &&
          act.endTick == exp.endTick) {
        found = true;
        break;
      }
    }
    TEST_ASSERT_TRUE(found);
  }
}

/// RC-D2: overlay-only invalidation reuses [0, committedEnd) and does not walk
/// snapshot/visual committed notes. Final union matches a full-gather oracle.
void test_draw_source_overlay_only_reuse_skips_committed_walk() {
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

  loop.openOverdubSession(0);
  loop.beginCapture(CapturePhase::Overdub);
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(300, 1, 72, 90)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(420, 1, 72, 0)));

  NoteUtils::DisplayNoteVec first;
  size_t committedEnd = 0;
  DrawSourceGatherWalkCounts firstWalks;
  gatherDrawSourceDisplayNotes(loop, 0, 3, 0, 0, 127, first, &committedEnd, &firstWalks);
  TEST_ASSERT_TRUE(firstWalks.committedWalks > 0);
  TEST_ASSERT_TRUE(firstWalks.previewWalks > 0);
  TEST_ASSERT_TRUE(committedEnd > 0);
  TEST_ASSERT_TRUE(committedEnd <= first.size());

  NoteUtils::DisplayNoteVec committedPrefix(first.begin(),
                                            first.begin() + static_cast<std::ptrdiff_t>(committedEnd));

  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(500, 1, 64, 90)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(560, 1, 64, 0)));

  NoteUtils::DisplayNoteVec oracle;
  DrawSourceGatherWalkCounts oracleWalks;
  gatherDrawSourceDisplayNotes(loop, 0, 3, 0, 0, 127, oracle, nullptr, &oracleWalks, false, true);
  TEST_ASSERT_TRUE(oracleWalks.committedWalks > 0);
  TEST_ASSERT_TRUE(oracleWalks.previewWalks > 0);

  NoteUtils::DisplayNoteVec reused = committedPrefix;
  size_t reusedCommittedEnd = 0;
  DrawSourceGatherWalkCounts reuseWalks;
  gatherDrawSourceDisplayNotes(loop, 0, 3, 0, 0, 127, reused, &reusedCommittedEnd, &reuseWalks,
                               true, true);
  TEST_ASSERT_EQUAL_UINT32(0u, reuseWalks.committedWalks);
  TEST_ASSERT_TRUE(reuseWalks.previewWalks > 0);
  TEST_ASSERT_EQUAL(committedEnd, reusedCommittedEnd);
  assertSameDrawSourceUnion(oracle, reused);

  PendingNoteChange hide{};
  hide.kind = PendingNoteChangeKind::Hide;
  hide.noteId = recordId;
  hide.channel = 1;
  hide.pitch = 60;
  hide.startTick = 100;
  hide.endTick = 200;
  loop.pushPendingNoteChangeForTest(hide);
  TEST_ASSERT_TRUE(loop.hasPendingNoteChanges());

  NoteUtils::DisplayNoteVec pendingOracle;
  DrawSourceGatherWalkCounts pendingOracleWalks;
  gatherDrawSourceDisplayNotes(loop, 0, 3, 0, 0, 127, pendingOracle, nullptr, &pendingOracleWalks,
                               false, true);
  TEST_ASSERT_TRUE(pendingOracleWalks.pendingApplied);

  NoteUtils::DisplayNoteVec pendingReused = committedPrefix;
  DrawSourceGatherWalkCounts pendingReuseWalks;
  gatherDrawSourceDisplayNotes(loop, 0, 3, 0, 0, 127, pendingReused, nullptr, &pendingReuseWalks,
                               true, true);
  TEST_ASSERT_EQUAL_UINT32(0u, pendingReuseWalks.committedWalks);
  TEST_ASSERT_TRUE(pendingReuseWalks.previewWalks > 0);
  TEST_ASSERT_TRUE(pendingReuseWalks.pendingApplied);
  assertSameDrawSourceUnion(pendingOracle, pendingReused);
}

bool displayNoteVecHasId(const NoteUtils::DisplayNoteVec& notes, NoteId noteId) {
  for (const NoteUtils::DisplayNote& note : notes) {
    if (note.noteId == noteId) {
      return true;
    }
  }
  return false;
}

void hideCommittedPrefixNoteById(NoteUtils::DisplayNoteVec& notes, size_t committedEnd,
                                 NoteId noteId) {
  NoteUtils::DisplayNoteVec kept;
  kept.reserve(notes.size());
  for (size_t i = 0; i < notes.size(); ++i) {
    if (i < committedEnd && notes[i].noteId == noteId) {
      continue;
    }
    kept.push_back(notes[i]);
  }
  notes.swap(kept);
}

/// RC-D3: ReuseCommittedDrawSource keeps the untransformed committed prefix.
/// This suite stubs `applyPendingNoteChangesToDisplayNotes`, so Hide is applied
/// on the frame copy the same way sibling draw-source pins do. The stored
/// prefix stays un-hidden. A second reuse from that prefix walks committed
/// notes 0 times and matches the first frame.
void test_draw_source_reuse_reapplies_pending_without_mutating_prefix() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  loop.loopId = 8;
  loop.loopLengthTicks = 3072;
  recordLinearNote(loop, 100, 200, 60);
  TEST_ASSERT_TRUE(loop.publishedSnapshotIsCurrent());
  const auto membership = loop.publishedSnapshot()->noteMembership(60, 0);
  TEST_ASSERT_NOT_NULL(membership.get());
  TEST_ASSERT_TRUE(snapshotMembershipCount(membership.get()) > 0);
  const NoteId recordId = membership->references[0]->noteId;

  loop.openOverdubSession(0);
  loop.beginCapture(CapturePhase::Overdub);
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(300, 1, 72, 90)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(420, 1, 72, 0)));

  NoteUtils::DisplayNoteVec first;
  size_t committedEnd = 0;
  DrawSourceGatherWalkCounts firstWalks;
  gatherDrawSourceDisplayNotes(loop, 0, 3, 0, 0, 127, first, &committedEnd, &firstWalks);
  TEST_ASSERT_TRUE(committedEnd > 0);
  TEST_ASSERT_TRUE(displayNoteVecHasId(first, recordId));

  NoteUtils::DisplayNoteVec storedPrefix(first.begin(),
                                         first.begin() + static_cast<std::ptrdiff_t>(committedEnd));
  TEST_ASSERT_TRUE(displayNoteVecHasId(storedPrefix, recordId));

  PendingNoteChange hide{};
  hide.kind = PendingNoteChangeKind::Hide;
  hide.noteId = recordId;
  hide.channel = 1;
  hide.pitch = 60;
  hide.startTick = 100;
  hide.endTick = 200;
  loop.pushPendingNoteChangeForTest(hide);
  TEST_ASSERT_TRUE(loop.hasPendingNoteChanges());

  NoteUtils::DisplayNoteVec frame1 = storedPrefix;
  size_t frame1End = 0;
  DrawSourceGatherWalkCounts frame1Walks;
  gatherDrawSourceDisplayNotes(loop, 0, 3, 0, 0, 127, frame1, &frame1End, &frame1Walks, true,
                               true);
  TEST_ASSERT_EQUAL_UINT32(0u, frame1Walks.committedWalks);
  TEST_ASSERT_TRUE(frame1Walks.pendingApplied);
  hideCommittedPrefixNoteById(frame1, frame1End, recordId);
  TEST_ASSERT_FALSE(displayNoteVecHasId(frame1, recordId));
  TEST_ASSERT_TRUE(displayNoteVecHasId(storedPrefix, recordId));

  NoteUtils::DisplayNoteVec frame2 = storedPrefix;
  size_t frame2End = 0;
  DrawSourceGatherWalkCounts frame2Walks;
  gatherDrawSourceDisplayNotes(loop, 0, 3, 0, 0, 127, frame2, &frame2End, &frame2Walks, true,
                               true);
  TEST_ASSERT_EQUAL_UINT32(0u, frame2Walks.committedWalks);
  TEST_ASSERT_TRUE(frame2Walks.pendingApplied);
  hideCommittedPrefixNoteById(frame2, frame2End, recordId);
  TEST_ASSERT_FALSE(displayNoteVecHasId(frame2, recordId));
  TEST_ASSERT_TRUE(displayNoteVecHasId(storedPrefix, recordId));
  assertSameDrawSourceUnion(frame1, frame2);
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

/// [`170537`] first overdub: a smaller current snapshot must not drop complete handoff id 112.
void test_draw_source_keeps_complete_handoff_over_smaller_snapshot_170537() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  loop.loopId = 7;
  loop.loopLengthTicks = 3072;
  recordLinearNote(loop, 100, 200, 60);
  TEST_ASSERT_TRUE(loop.publishedSnapshotIsCurrent());

  NoteUtils::DisplayNoteVec handoff;
  handoff.push_back(makeAdoptedNote(1, 60, 100, 200));
  handoff.push_back(makeAdoptedNote(112, 72, 768, 900));
  handoff.push_back(makeAdoptedNote(3, 60, 1536, 1568));
  handoff.push_back(makeAdoptedNote(4, 39, 2976, 3055));
  loop.adoptComposedDisplayNotesFromViewport(handoff);
  loop.visualCache.builtFromRevision = loop.playbackRevision;
  TEST_ASSERT_FALSE(loop.visualCacheDirty);

  loop.openOverdubSession(0);
  loop.beginCapture(CapturePhase::Overdub);
  TEST_ASSERT_TRUE(loop.publishedSnapshotIsCurrent());
  TEST_ASSERT_FALSE(loop.hasSealedCapturePreview());

  NoteUtils::DisplayNoteVec drawn;
  gatherDrawSourceDisplayNotes(loop, 0, 3, 0, 0, 127, drawn);
  bool foundHandoffId = false;
  for (const NoteUtils::DisplayNote& note : drawn) {
    if (note.noteId == 112 && note.note == 72 && note.startTick == 768 && note.endTick == 900) {
      foundHandoffId = true;
    }
  }
  TEST_ASSERT_TRUE(foundHandoffId);
}

/// [`170537`] wrap: sealed replacement is in the committed prefix before Hide can drop the old row.
void test_draw_source_sealed_replacement_before_companion_hide_170537() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  loop.loopId = 7;
  loop.loopLengthTicks = 3072;
  recordLinearNote(loop, 768, 1151, 63);
  const NoteId recordId = 71;

  NoteUtils::DisplayNoteVec staleCache;
  staleCache.push_back(makeAdoptedNote(recordId, 63, 768, 1151));
  for (uint32_t i = 0; i < 65; ++i) {
    staleCache.push_back(makeAdoptedNote(static_cast<NoteId>(200 + i), static_cast<uint8_t>(40 + (i % 20)),
                                         (i % 4) * 768u, (i % 4) * 768u + 48u));
  }
  loop.adoptComposedDisplayNotesFromViewport(staleCache);
  loop.visualCacheDirty = false;
  const size_t staleCount = loop.visualCache.notes.size();
  TEST_ASSERT_TRUE(staleCount > 58u);

  loop.openOverdubSession(0);
  loop.beginCapture(CapturePhase::Overdub);
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(768, 1, 63, 90)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(864, 1, 63, 0)));
  TEST_ASSERT_EQUAL(CommitResult::Committed,
                    loop.commitCapturePass(CommitReason::OverdubWrap, 864));
  loop.beginCapture(CapturePhase::Overdub);
  TEST_ASSERT_TRUE(loop.hasSealedCapturePreview());
  TEST_ASSERT_FALSE(loop.sealedCapturePreviewIsCovered());

  NoteUtils::DisplayNoteVec drawn;
  size_t committedEnd = 0;
  gatherDrawSourceDisplayNotes(loop, 0, 3, 0, 0, 127, drawn, &committedEnd);
  bool replacementInPrefix = false;
  for (size_t i = 0; i < committedEnd; ++i) {
    if (drawn[i].note == 63 && drawn[i].startTick == 768 && drawn[i].endTick == 864) {
      replacementInPrefix = true;
    }
  }
  TEST_ASSERT_TRUE(replacementInPrefix);

  NoteUtils::DisplayNoteVec afterHide;
  for (size_t i = 0; i < drawn.size(); ++i) {
    if (i < committedEnd && recordId != kInvalidNoteId && drawn[i].noteId == recordId &&
        drawn[i].endTick == 1151) {
      continue;
    }
    afterHide.push_back(drawn[i]);
  }
  bool replacementRemains = false;
  for (const NoteUtils::DisplayNote& note : afterHide) {
    if (note.note == 63 && note.startTick == 768 && note.endTick == 864) {
      replacementRemains = true;
    }
  }
  TEST_ASSERT_TRUE(replacementRemains);
  TEST_ASSERT_TRUE(afterHide.size() >= committedEnd - 1u);
}

/// Cache built before the seal is not committed-content authority; last snapshot plus
/// sealed preview still paint the new pass.
void test_draw_source_cache_revision_mismatch_uses_sealed_prepared_170537() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  loop.loopId = 7;
  loop.loopLengthTicks = 3072;
  recordLinearNote(loop, 100, 200, 60);
  const size_t recordVisual = loop.visualCache.notes.size();

  loop.openOverdubSession(0);
  loop.beginCapture(CapturePhase::Overdub);
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(768, 1, 64, 90)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(900, 1, 64, 0)));
  TEST_ASSERT_EQUAL(CommitResult::Committed,
                    loop.commitCapturePass(CommitReason::OverdubWrap, 900));
  loop.beginCapture(CapturePhase::Overdub);

  loop.visualCacheDirty = false;
  loop.visualCache.builtFromRevision = loop.playbackRevision;
  TEST_ASSERT_FALSE(loop.sealedCapturePreviewIsCovered());
  TEST_ASSERT_EQUAL(recordVisual, loop.visualCache.notes.size());

  NoteUtils::DisplayNoteVec drawn;
  gatherDrawSourceDisplayNotes(loop, 0, 3, 0, 0, 127, drawn);
  bool foundSealed = false;
  bool foundRecord = false;
  for (const NoteUtils::DisplayNote& note : drawn) {
    if (note.note == 64 && note.startTick == 768 && note.endTick == 900) {
      foundSealed = true;
    }
    if (note.note == 60 && note.startTick == 100 && note.endTick == 200) {
      foundRecord = true;
    }
  }
  TEST_ASSERT_TRUE(foundSealed);
  TEST_ASSERT_TRUE(foundRecord);
}

/// Matching revision and membership reuses visualCache.
void test_draw_source_reuses_visual_cache_when_revision_and_coverage_match() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  loop.loopId = 7;
  loop.loopLengthTicks = 3072;
  recordLinearNote(loop, 100, 200, 60);
  loop.openOverdubSession(0);
  loop.beginCapture(CapturePhase::Overdub);
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(768, 1, 64, 90)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(900, 1, 64, 0)));
  TEST_ASSERT_EQUAL(CommitResult::Committed,
                    loop.commitCapturePass(CommitReason::OverdubWrap, 900));
  TEST_ASSERT_TRUE(loop.hasSealedCapturePreview());

  for (const NoteUtils::DisplayNote& sealedRow : loop.sealedCapturePreviewNotes()) {
    loop.visualCache.notes.push_back(sealedRow);
  }
  loop.visualCache.builtFromRevision = loop.playbackRevision;
  loop.visualCacheDirty = false;
  TEST_ASSERT_TRUE(loop.sealedCapturePreviewIsCovered());

  loop.beginCapture(CapturePhase::Overdub);
  NoteUtils::DisplayNoteVec drawn;
  gatherDrawSourceDisplayNotes(loop, 0, 3, 0, 0, 127, drawn);
  bool foundSealed = false;
  for (const NoteUtils::DisplayNote& note : drawn) {
    if (note.note == 64 && note.startTick == 768 && note.endTick == 900) {
      foundSealed = true;
    }
  }
  TEST_ASSERT_TRUE(foundSealed);
}

void test_note_edit_committed_base_gather_keeps_snapshot_and_live_capture() {
  // RC2 / 170818: capture-active dirty+current uses Snapshot, not
  // CaptureAwareGather. Live notes stay on capturePreview overlay.
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  loop.loopId = 1;
  loop.loopLengthTicks = 3072;
  loop.beginCapture(CapturePhase::Record);
  for (uint8_t i = 0; i < 3; ++i) {
    const uint32_t onTick = static_cast<uint32_t>(i) * 96u;
    TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(onTick, 1, 60 + i, 100)));
    TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(onTick + 48, 1, 60 + i, 0)));
  }
  TEST_ASSERT_EQUAL(CommitResult::Committed,
                    loop.commitCapturePass(CommitReason::RecordStop, 288));
  drainPendingSnapshotPublish(loop);
  TEST_ASSERT_TRUE(loop.publishedSnapshotIsCurrent());

  NoteUtils::DisplayNoteVec snapshotOnly;
  LoopContentResolution::resolveDisplayNotes(*loop.publishedSnapshot(), snapshotOnly);
  TEST_ASSERT_EQUAL(3u, snapshotOnly.size());

  loop.beginCapture(CapturePhase::Overdub);
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(400, 1, 72, 90)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(448, 1, 72, 0)));
  TEST_ASSERT_TRUE(loop.captureActive());
  loop.visualCacheDirty = true;

  TEST_ASSERT_EQUAL(
      static_cast<int>(DisplayWindowUtils::NoteEditCommittedBaseKind::Snapshot),
      static_cast<int>(DisplayWindowUtils::selectNoteEditCommittedBaseKind(
          loop.captureActive(), loop.visualCacheDirty, loop.publishedSnapshotIsCurrent())));
  TEST_ASSERT_EQUAL(
      static_cast<int>(DisplayWindowUtils::NoteEditCommittedBaseKind::VisualCache),
      static_cast<int>(DisplayWindowUtils::selectNoteEditCommittedBaseKind(
          loop.captureActive(), false, loop.publishedSnapshotIsCurrent())));

  bool snapshotHasLive = false;
  for (const NoteUtils::DisplayNote& note : snapshotOnly) {
    if (note.note == 72) {
      snapshotHasLive = true;
    }
  }
  TEST_ASSERT_FALSE(snapshotHasLive);
  bool previewHasLive = false;
  for (const NoteUtils::DisplayNote& note : loop.capturePreview.notes) {
    if (note.note == 72) {
      previewHasLive = true;
    }
  }
  TEST_ASSERT_TRUE(previewHasLive);

  SessionMidiEventVec gatherBuffer;
  Loop::SnapshotWindowGatherPath path = Loop::SnapshotWindowGatherPath::None;
  bool usedSnapshot = loop.tryGatherPublishedSnapshotEventsInWindowWithCapture(
      gatherBuffer, 0, loop.loopLengthTicks, &path);
  if (path == Loop::SnapshotWindowGatherPath::Overlay) {
    gatherBuffer.clear();
    usedSnapshot = false;
  }
  if (!usedSnapshot) {
    loop.copyEffectiveCommittedEventsInWindowWithCapture(gatherBuffer, 0, loop.loopLengthTicks);
  }
  const NoteUtils::DisplayNoteVec composed =
      NoteUtils::reconstructDisplayNotes(gatherBuffer, loop.loopLengthTicks, false, false);

  bool composedHasLive = false;
  uint32_t snapshotPitchCount = 0;
  for (const NoteUtils::DisplayNote& note : composed) {
    if (note.note == 72) {
      composedHasLive = true;
    }
    if (note.note == 60 || note.note == 61 || note.note == 62) {
      ++snapshotPitchCount;
    }
  }
  TEST_ASSERT_TRUE(composedHasLive);
  TEST_ASSERT_EQUAL(3u, snapshotPitchCount);
}

int main(int /*argc*/, char** /*argv*/) {
  UNITY_BEGIN();
  RUN_TEST(test_prepared_capture_transfer_moves_pass_without_vector_growth);
  RUN_TEST(test_active_overdub_transfer_uses_prewarmed_capacity);
  RUN_TEST(test_imported_takes_survive_invalidateCaches);
  RUN_TEST(test_gather_committed_preserves_takes);
  RUN_TEST(test_restore_empty_pass_snapshot_clears_takes);
  RUN_TEST(test_readonly_flat_access_preserves_takes);
  RUN_TEST(test_post_overdub_stop_path_preserves_takes);
  RUN_TEST(test_seal_overdub_preserves_record_pass);
  RUN_TEST(test_commit_stop_finalize_empty_merged_preserves_takes);
  RUN_TEST(test_multi_take_flatten_matches_live_event_count);
  RUN_TEST(test_pass_snapshot_ignores_derived_flat_mutation);
  RUN_TEST(test_invalidateCaches_gather_reflects_loop_length_change);
  RUN_TEST(test_reclaim_disabled_overdub_releases_committed_chunks);
  RUN_TEST(test_record_stop_publishes_complete_snapshot);
  RUN_TEST(test_record_wrap_note_publishes_one_reference);
  RUN_TEST(test_overdub_wrap_does_not_publish_global_snapshot);
  RUN_TEST(test_overdub_stop_publishes_successor_from_parent);
  RUN_TEST(test_note_edit_close_replaces_published_note);
  RUN_TEST(test_note_edit_close_note_range_reuses_published_pitch);
  RUN_TEST(test_note_edit_close_pitch_then_note_range_keeps_updated_pitch);
  RUN_TEST(test_stale_root_save_admits_edit_pass_to_pending_companions);
  RUN_TEST(test_current_root_save_does_not_admit_companion_and_close_publish_holds);
  RUN_TEST(test_failed_edit_publish_preserves_parent_snapshot);
  RUN_TEST(test_playback_gather_reads_published_snapshot_with_fallback);
  RUN_TEST(test_visual_cache_reads_published_snapshot_with_fallback);
  RUN_TEST(test_rebuild_visual_cache_from_passes_uses_snapshot_resolve_notes);
  RUN_TEST(test_visual_cache_identity_stale_when_published_root_replaced_same_revision);
  RUN_TEST(test_legacy_committed_scratch_snapshot_identity_freshness);
  RUN_TEST(test_committed_scratch_hold_empty_without_snapshot_or_c1_a);
  RUN_TEST(test_committed_scratch_hold_empty_during_session_exit_gap);
  RUN_TEST(test_keep_committed_mover_in_visual_cache_232309);
  RUN_TEST(test_rebuild_visual_cache_from_passes_skips_wrap_held_append_on_snapshot_spans);
  RUN_TEST(test_rematerialize_edit_view_uses_snapshot_span_note_ids);
  RUN_TEST(test_copy_note_edit_commit_base_uses_snapshot_span_note_ids);
  RUN_TEST(test_note_edit_committed_base_gather_keeps_snapshot_and_live_capture);
  RUN_TEST(test_try_copy_note_edit_opened_snapshot_events_uses_span_note_ids);
  RUN_TEST(test_copy_idle_validate_events_uses_snapshot_span_note_ids);
  RUN_TEST(test_try_copy_current_snapshot_display_notes_in_window_uses_span_note_ids);
  RUN_TEST(test_visual_cache_idle_slice_copies_current_snapshot_notes);
  RUN_TEST(test_loaded_visual_cache_falls_back_until_current_snapshot);
  RUN_TEST(test_stale_published_snapshot_gathers_window_without_full_compatibility);
  RUN_TEST(test_stale_snapshot_overlays_unpublished_overdub_pass);
  RUN_TEST(test_stale_snapshot_applies_unpublished_hide_companion);
  RUN_TEST(test_overdub_publish_keeps_pass_events_when_companion_is_missing);
  RUN_TEST(test_prepared_stop_handoff_preserves_restart_before_and_after_publication);
  RUN_TEST(test_stop_handoff_retains_session_companions_and_clear_cancels_receipt);
  RUN_TEST(test_empty_stop_handoff_does_not_duplicate_published_record);
  RUN_TEST(test_stop_handoff_keeps_inflight_wrap_and_final_pass_once);
  RUN_TEST(test_queue_overdub_snapshot_waits_for_idle_steps);
  RUN_TEST(test_live_record_pending_after_length_reaches_one_bar);
  RUN_TEST(test_live_record_does_not_start_in_progress_bar);
  RUN_TEST(test_live_record_continues_next_completed_bar);
  RUN_TEST(test_live_record_extends_candidate_past_seventeen_bars);
  RUN_TEST(test_live_record_scales_applied_frontier_past_seventeen_bars);
  RUN_TEST(test_live_record_second_bar_start_changes_are_bar_two_only);
  RUN_TEST(test_live_record_held_note_pairs_on_next_bar_off);
  RUN_TEST(test_session_exit_keeps_bar_crossing_live_record_open_note);
  RUN_TEST(test_live_record_dense_bar_keeps_cursor_inside_bar);
  RUN_TEST(test_live_record_equal_boundary_event_waits_for_next_window);
  RUN_TEST(test_live_record_same_pitch_second_bar_does_not_duplicate_first);
  RUN_TEST(test_session_exit_pass_change_skips_note_already_on_parent);
  RUN_TEST(test_live_record_starts_snapshot_before_session_exit);
  RUN_TEST(test_overdub_idle_advances_in_flight_construction_only);
  RUN_TEST(test_session_exit_restarts_construction_when_overdub_would_block_start);
  RUN_TEST(test_playback_session_exit_publication_gap_predicate);
  RUN_TEST(test_overdub_wrap_ahead_does_not_publish_until_session_exit);
  RUN_TEST(test_settled_overdub_wrap_publishes_current_snapshot);
  RUN_TEST(test_snapshot_yield_retains_then_retries_to_publish);
  RUN_TEST(test_unstamped_wrap_journal_leaves_snapshot_unselected);
  RUN_TEST(test_wrap_journal_repeated_same_note_id_replace);
  RUN_TEST(test_wrap_journal_replaces_wrap_note_on_off_pair);
  RUN_TEST(test_wrap_journal_hide_removes_published_note);
  RUN_TEST(test_wrap_journal_equal_tick_non_note_event_order);
  RUN_TEST(test_skipped_overdub_stop_retains_in_flight_wrap_journals);
  RUN_TEST(test_queue_session_exit_keeps_wrap_journals_without_new_pass);
  RUN_TEST(test_bounded_session_close_keeps_snapshot_work_and_limits_each_turn);
  RUN_TEST(test_bounded_session_close_rejects_a_changed_session);
  RUN_TEST(test_seal_retains_capture_rows_until_snapshot_covers_revision);
  RUN_TEST(test_retain_at_seal_merges_uncovered_laps);
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
  RUN_TEST(test_draw_source_overlay_only_reuse_skips_committed_walk);
  RUN_TEST(test_draw_source_reuse_reapplies_pending_without_mutating_prefix);
  RUN_TEST(test_draw_source_keeps_stale_snapshot_after_second_wrap);
  RUN_TEST(test_draw_source_retains_unpublished_visual_cache_over_stale_gather_214900);
  RUN_TEST(test_draw_source_od2_clean_adopt_keeps_linear_tail);
  RUN_TEST(test_draw_source_no_snapshot_uses_clean_visual_cache);
  RUN_TEST(test_draw_source_no_snapshot_empty_dirty_cache_stays_empty);
  RUN_TEST(test_draw_source_after_stop_keeps_adopted_linear_ends);
  RUN_TEST(test_draw_source_keeps_snapshot_when_wrap_clears_capture);
  RUN_TEST(test_draw_source_sealed_survives_current_snapshot);
  RUN_TEST(test_draw_source_keeps_complete_handoff_over_smaller_snapshot_170537);
  RUN_TEST(test_draw_source_sealed_replacement_before_companion_hide_170537);
  RUN_TEST(test_draw_source_cache_revision_mismatch_uses_sealed_prepared_170537);
  RUN_TEST(test_draw_source_reuses_visual_cache_when_revision_and_coverage_match);
  RUN_TEST(test_same_pass_draw_source_keeps_closed_bar_four_end);
  RUN_TEST(test_same_pass_playhead_tails_keep_closed_bar_four_end);
  RUN_TEST(test_overdub_live_wrap_playhead_tails_175831_ab);
  RUN_TEST(test_stopped_passes_resolve_append_wrap_held_180916);
  RUN_TEST(test_stopped_draw_source_snapshot_append_wrap_held_183741);
  RUN_TEST(test_append_display_note_unique_by_id_keeps_wrap_segments);
  RUN_TEST(test_closed_same_note_id_keeps_linear_end_after_wrap_two_gather);
  RUN_TEST(test_session_exit_closed_notes_do_not_pair_across_passes);
  RUN_TEST(test_wrap_journal_drain_keeps_first_record_while_session_exit_pending);
  RUN_TEST(test_stage7_snapshot_compose_includes_active_overdub_preview_130411);
  RUN_TEST(test_stage7_snapshot_compose_includes_idle_overdub_preview_130411);
  RUN_TEST(test_gap_handoff_supplements_uncovered_bars_from_unpublished_snapshot);
  RUN_TEST(test_session_exit_handoff_when_live_record_notes_fit_sealed_length);
  RUN_TEST(test_session_exit_handoff_when_candidate_shorter_than_sealed_length);
  RUN_TEST(test_session_exit_discards_when_applied_ticks_exceed_sealed_length);
  RUN_TEST(test_session_exit_pass_starts_as_journal_capped_fragments);
  RUN_TEST(test_session_exit_published_undo_attach_targets_originating_entry_id);
  RUN_TEST(test_clear_slot_before_published_snapshot_field_retains_root);
  RUN_TEST(test_overdub_without_parent_does_not_invent_snapshot);
  return UNITY_END();
}
