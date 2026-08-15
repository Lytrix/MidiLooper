//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// Phase 2 G2 slice 1 — pending session delta + geometry bridge.

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
#include "../../src/EditManager/EditSessionLiveStoreSpan.cpp"
#include "../../src/EditManager/NoteEditCurrentState.cpp"
#include "../../src/EditManager/EditSessionInteraction.cpp"
#include "../../src/EditManager/ResolveConstrainedGeometry.cpp"
#include "../../src/EditManager/ParticipatingNoteSession.cpp"
#include "../../src/Loop/LoopPendingNoteChange.cpp"
#include "../../src/Utils/RuntimeTimingTelemetry.cpp"

#include "Loop.h"
#include "../test_support/CommittedChunkIdTestHelpers.h"
#include "../test_support/NoteIdTestFixtures.h"
#include "EditPass.h"
#include "OverlapNoteIdSet.h"
#include "Utils/IntervalProjection.h"
#include "Utils/NoteUtils.h"

#include <initializer_list>

namespace {

using namespace NoteIdTestFixtures;

constexpr uint32_t kLoopLen = Config::TICKS_PER_BAR * 8;

int countKind(const PendingNoteChangeVec& pending, PendingNoteChangeKind kind) {
  int count = 0;
  for (const PendingNoteChange& change : pending) {
    if (change.kind == kind) {
      ++count;
    }
  }
  return count;
}

const PendingNoteChange* findTransform(const PendingNoteChangeVec& pending, NoteId noteId) {
  for (const PendingNoteChange& change : pending) {
    if ((change.kind == PendingNoteChangeKind::Shorten ||
         change.kind == PendingNoteChangeKind::Hide) &&
        change.noteId == noteId) {
      return &change;
    }
  }
  return nullptr;
}

void seedLongSourceNote(Loop& loop, NoteId id, uint32_t onTick, uint32_t offTick, uint8_t pitch,
                        uint32_t loopLength = kLoopLen) {
  loop.loopLengthTicks = loopLength;
  LoopEventStore store;
  TEST_ASSERT_TRUE(storeAppendNoteOn(store, onTick, 1, pitch, 100, id));
  TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOff(offTick, 1, pitch, 0)));
  loop.seedRecordPassFromStore(store);
  loop.nextNoteId_ = id + 1;
}

OverlapNoteIdSet overlapIds(std::initializer_list<NoteId> ids) {
  OverlapNoteIdSet out;
  for (NoteId id : ids) {
    (void)out.insert(id);
  }
  return out;
}

}  // namespace

void test_pending_requires_source_view() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  loop.loopLengthTicks = kLoopLen;
  Loop::resetCommittedPitchQueryWork();
  TEST_ASSERT_FALSE(loop.accumulatePendingNoteChangesForIncomingNote(1, 60, 100, 20, 40, 99,
                                                                    overlapIds({})));
  TEST_ASSERT_FALSE(loop.hasPendingNoteChanges());
  TEST_ASSERT_EQUAL_UINT32(0, Loop::committedEventsFullMaterializeCount());
}

void test_pending_add_only_when_no_overlap() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  seedLongSourceNote(loop, 1, 10, 58, 60);
  loop.beginCapture(CapturePhase::Overdub);
  Loop::resetCommittedPitchQueryWork();

  TEST_ASSERT_TRUE(loop.accumulatePendingNoteChangesForIncomingNote(1, 72, 90, 200, 240, 10,
                                                                   overlapIds({})));
  TEST_ASSERT_EQUAL(1, countKind(loop.pendingNoteChanges(), PendingNoteChangeKind::Add));
  TEST_ASSERT_EQUAL(0, countKind(loop.pendingNoteChanges(), PendingNoteChangeKind::Shorten));
  TEST_ASSERT_EQUAL(0, countKind(loop.pendingNoteChanges(), PendingNoteChangeKind::Hide));
  TEST_ASSERT_TRUE(loop.hasOverdubSourceView());
  TEST_ASSERT_FALSE(loop.overdubSourceViewNotes().empty());
  TEST_ASSERT_EQUAL_UINT32(0, Loop::committedEventsFullMaterializeCount());
}

void test_no_overlap_with_companion_edit_does_not_full_materialize() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  seedLongSourceNote(loop, 1, 10, 58, 60);
  EditPass hideRow{};
  hideRow.passType = EditPassType::Note;
  hideRow.actionType = EditActionType::Delete;
  hideRow.propertyType = EditPropertyType::None;
  hideRow.targetNoteId = 2;
  TEST_ASSERT_NOT_EQUAL(kInvalidEditPassId, loop.saveNoteEditPass(0, std::move(hideRow)));
  loop.beginCapture(CapturePhase::Overdub);
  Loop::resetCommittedPitchQueryWork();

  TEST_ASSERT_TRUE(loop.accumulatePendingNoteChangesForIncomingNote(1, 72, 90, 200, 240, 10,
                                                                   overlapIds({})));
  TEST_ASSERT_EQUAL(1, countKind(loop.pendingNoteChanges(), PendingNoteChangeKind::Add));
  TEST_ASSERT_EQUAL(0, countKind(loop.pendingNoteChanges(), PendingNoteChangeKind::Shorten));
  TEST_ASSERT_EQUAL_UINT32(0, Loop::committedEventsFullMaterializeCount());
}

void test_empty_overlap_ids_add_only_when_source_overlaps() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  seedLongSourceNote(loop, 1, 50, 200, 60);
  loop.beginCapture(CapturePhase::Overdub);
  Loop::resetCommittedPitchQueryWork();

  TEST_ASSERT_TRUE(loop.accumulatePendingNoteChangesForIncomingNote(1, 60, 90, 120, 160, 10,
                                                                   overlapIds({})));
  TEST_ASSERT_EQUAL(1, countKind(loop.pendingNoteChanges(), PendingNoteChangeKind::Add));
  TEST_ASSERT_EQUAL(0, countKind(loop.pendingNoteChanges(), PendingNoteChangeKind::Shorten));
  TEST_ASSERT_EQUAL(0, countKind(loop.pendingNoteChanges(), PendingNoteChangeKind::Hide));
  TEST_ASSERT_EQUAL_UINT32(0, Loop::committedEventsFullMaterializeCount());
  TEST_ASSERT_EQUAL_UINT32(1, loop.overlapHoldTotals().noteOffs);
  TEST_ASSERT_EQUAL_UINT32(1, loop.overlapHoldTotals().emptySets);
  TEST_ASSERT_EQUAL_UINT32(0, loop.overlapHoldTotals().lookedUp);
  TEST_ASSERT_EQUAL_UINT32(0, loop.overlapHoldTotals().maxExamined);
  TEST_ASSERT_EQUAL_UINT32(1, loop.overlapHoldTotals().add);
  TEST_ASSERT_EQUAL_UINT32(0, loop.overlapHoldTotals().shorten);
}

void test_pending_shorten_long_source_on_overlap() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  seedLongSourceNote(loop, 1, 50, 200, 60);
  loop.beginCapture(CapturePhase::Overdub);

  TEST_ASSERT_TRUE(loop.accumulatePendingNoteChangesForIncomingNote(1, 60, 90, 120, 160, 10,
                                                                   overlapIds({1})));
  TEST_ASSERT_EQUAL(1, countKind(loop.pendingNoteChanges(), PendingNoteChangeKind::Add));
  TEST_ASSERT_EQUAL(1, countKind(loop.pendingNoteChanges(), PendingNoteChangeKind::Shorten));
  TEST_ASSERT_EQUAL(0, countKind(loop.pendingNoteChanges(), PendingNoteChangeKind::Hide));

  const PendingNoteChange* shorten = findTransform(loop.pendingNoteChanges(), 1);
  TEST_ASSERT_NOT_NULL(shorten);
  TEST_ASSERT_EQUAL(static_cast<int>(PendingNoteChangeKind::Shorten),
                    static_cast<int>(shorten->kind));
  TEST_ASSERT_EQUAL_UINT32(50u, shorten->startTick);
  TEST_ASSERT_EQUAL_UINT32(119u, shorten->endTick);

  TEST_ASSERT_TRUE(loop.hasOverdubSourceView());
  TEST_ASSERT_FALSE(loop.overdubSourceViewNotes().empty());
  TEST_ASSERT_EQUAL_UINT32(1, loop.overlapHoldTotals().noteOffs);
  TEST_ASSERT_EQUAL_UINT32(0, loop.overlapHoldTotals().emptySets);
  TEST_ASSERT_EQUAL_UINT32(1, loop.overlapHoldTotals().lookedUp);
  TEST_ASSERT_EQUAL_UINT32(1, loop.overlapHoldTotals().maxIds);
  TEST_ASSERT_TRUE(loop.overlapHoldTotals().maxExamined >= 1);
  TEST_ASSERT_EQUAL_UINT32(loop.overlapHoldTotals().maxExamined,
                           loop.overlapHoldTotals().sumExamined);
  TEST_ASSERT_EQUAL_UINT32(1, loop.overlapHoldTotals().add);
  TEST_ASSERT_EQUAL_UINT32(1, loop.overlapHoldTotals().shorten);
}

void test_pending_shorten_ignores_recorded_channel() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  seedLongSourceNote(loop, 1, 50, 200, 60);
  loop.beginCapture(CapturePhase::Overdub);

  TEST_ASSERT_TRUE(loop.accumulatePendingNoteChangesForIncomingNote(2, 60, 90, 120, 160, 10,
                                                                   overlapIds({1})));
  TEST_ASSERT_EQUAL(1, countKind(loop.pendingNoteChanges(), PendingNoteChangeKind::Add));
  TEST_ASSERT_EQUAL(1, countKind(loop.pendingNoteChanges(), PendingNoteChangeKind::Shorten));
  TEST_ASSERT_EQUAL(0, countKind(loop.pendingNoteChanges(), PendingNoteChangeKind::Hide));

  const PendingNoteChange* shorten = findTransform(loop.pendingNoteChanges(), 1);
  TEST_ASSERT_NOT_NULL(shorten);
  TEST_ASSERT_EQUAL_UINT32(50u, shorten->startTick);
  TEST_ASSERT_EQUAL_UINT32(119u, shorten->endTick);
}

void test_pending_hide_when_covered() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;

  LoopEventStore store;
  TEST_ASSERT_TRUE(storeAppendNoteOn(store, 10, 1, 60, 100, 1));
  TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOff(40, 1, 60, 0)));
  TEST_ASSERT_TRUE(storeAppendNoteOn(store, 50, 1, 60, 100, 2));
  TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOff(80, 1, 60, 0)));
  TEST_ASSERT_TRUE(storeAppendNoteOn(store, 90, 1, 60, 100, 3));
  TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOff(120, 1, 60, 0)));
  loop.loopLengthTicks = kLoopLen;
  loop.seedRecordPassFromStore(store);
  loop.nextNoteId_ = 4;
  loop.beginCapture(CapturePhase::Overdub);

  TEST_ASSERT_TRUE(loop.accumulatePendingNoteChangesForIncomingNote(1, 60, 100, 5, 130, 20,
                                                                   overlapIds({1, 2, 3})));
  TEST_ASSERT_EQUAL(1, countKind(loop.pendingNoteChanges(), PendingNoteChangeKind::Add));
  TEST_ASSERT_EQUAL(3, countKind(loop.pendingNoteChanges(), PendingNoteChangeKind::Hide));
  TEST_ASSERT_NOT_NULL(findTransform(loop.pendingNoteChanges(), 1));
  TEST_ASSERT_NOT_NULL(findTransform(loop.pendingNoteChanges(), 2));
  TEST_ASSERT_NOT_NULL(findTransform(loop.pendingNoteChanges(), 3));
  TEST_ASSERT_EQUAL(static_cast<int>(PendingNoteChangeKind::Hide),
                    static_cast<int>(findTransform(loop.pendingNoteChanges(), 1)->kind));
  TEST_ASSERT_EQUAL_UINT32(1, loop.overlapHoldTotals().lookedUp);
  TEST_ASSERT_EQUAL_UINT32(3, loop.overlapHoldTotals().maxIds);
  TEST_ASSERT_TRUE(loop.overlapHoldTotals().maxExamined >= 3);
  TEST_ASSERT_EQUAL_UINT32(1, loop.overlapHoldTotals().add);
  TEST_ASSERT_EQUAL_UINT32(3, loop.overlapHoldTotals().hide);
}

void test_pending_survives_wraps_and_accumulates() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  seedLongSourceNote(loop, 1, 50, 200, 60);
  loop.beginCapture(CapturePhase::Overdub);

  TEST_ASSERT_TRUE(loop.accumulatePendingNoteChangesForIncomingNote(1, 60, 90, 120, 160, 10,
                                                                   overlapIds({1})));
  // Simulate wrap: second insert at low phase against same source view.
  TEST_ASSERT_TRUE(loop.accumulatePendingNoteChangesForIncomingNote(1, 72, 90, 8, 30, 11,
                                                                   overlapIds({})));
  TEST_ASSERT_EQUAL(2, countKind(loop.pendingNoteChanges(), PendingNoteChangeKind::Add));
  TEST_ASSERT_EQUAL(1, countKind(loop.pendingNoteChanges(), PendingNoteChangeKind::Shorten));
  TEST_ASSERT_TRUE(loop.hasOverdubSourceView());
  TEST_ASSERT_FALSE(loop.overdubSourceViewNotes().empty());
  TEST_ASSERT_EQUAL_UINT32(2, loop.overlapHoldTotals().noteOffs);
  TEST_ASSERT_EQUAL_UINT32(1, loop.overlapHoldTotals().emptySets);
  TEST_ASSERT_EQUAL_UINT32(1, loop.overlapHoldTotals().lookedUp);
  TEST_ASSERT_EQUAL_UINT32(2, loop.overlapHoldTotals().add);
  TEST_ASSERT_EQUAL_UINT32(1, loop.overlapHoldTotals().shorten);
}

void test_establish_resets_overlap_hold_totals() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  seedLongSourceNote(loop, 1, 50, 200, 60);
  loop.beginCapture(CapturePhase::Overdub);
  TEST_ASSERT_TRUE(loop.accumulatePendingNoteChangesForIncomingNote(1, 60, 90, 120, 160, 10,
                                                                   overlapIds({1})));
  TEST_ASSERT_EQUAL_UINT32(1, loop.overlapHoldTotals().lookedUp);
  TEST_ASSERT_EQUAL_UINT32(1, loop.overlapHoldTotals().shorten);
  loop.beginCapture(CapturePhase::Overdub);
  TEST_ASSERT_EQUAL_UINT32(0, loop.overlapHoldTotals().noteOffs);
  TEST_ASSERT_EQUAL_UINT32(0, loop.overlapHoldTotals().lookedUp);
  TEST_ASSERT_EQUAL_UINT32(0, loop.overlapHoldTotals().add);
  TEST_ASSERT_EQUAL_UINT32(0, loop.overlapHoldTotals().shorten);
}

void test_discard_clears_pending_with_source_view() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  seedLongSourceNote(loop, 1, 50, 200, 60);
  loop.beginCapture(CapturePhase::Overdub);
  TEST_ASSERT_TRUE(loop.accumulatePendingNoteChangesForIncomingNote(1, 60, 90, 120, 160, 10,
                                                                   overlapIds({1})));
  TEST_ASSERT_TRUE(loop.hasPendingNoteChanges());
  loop.discardCapture();
  TEST_ASSERT_FALSE(loop.hasPendingNoteChanges());
  TEST_ASSERT_FALSE(loop.hasOverdubSourceView());
}

void test_pending_shorten_wrap_crossing_incoming_tail() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  seedLongSourceNote(loop, 1, kLoopLen - 80, kLoopLen - 10, 60);
  loop.beginCapture(CapturePhase::Overdub);

  TEST_ASSERT_TRUE(loop.accumulatePendingNoteChangesForIncomingNote(
      1, 60, 90, kLoopLen - 40, 20, 10, overlapIds({1})));
  TEST_ASSERT_EQUAL(1, countKind(loop.pendingNoteChanges(), PendingNoteChangeKind::Add));
  TEST_ASSERT_EQUAL(1, countKind(loop.pendingNoteChanges(), PendingNoteChangeKind::Shorten));
  TEST_ASSERT_EQUAL(0, countKind(loop.pendingNoteChanges(), PendingNoteChangeKind::Hide));

  const PendingNoteChange* shorten = findTransform(loop.pendingNoteChanges(), 1);
  TEST_ASSERT_NOT_NULL(shorten);
  TEST_ASSERT_EQUAL_UINT32(kLoopLen - 80u, shorten->startTick);
  TEST_ASSERT_EQUAL_UINT32(kLoopLen - 41u, shorten->endTick);
  TEST_ASSERT_EQUAL_UINT32(1, loop.overlapHoldTotals().noteOffs);
  TEST_ASSERT_EQUAL_UINT32(1, loop.overlapHoldTotals().lookedUp);
  TEST_ASSERT_EQUAL_UINT32(1, loop.overlapHoldTotals().add);
  TEST_ASSERT_EQUAL_UINT32(1, loop.overlapHoldTotals().shorten);
}

void test_pending_shorten_long_source_4000_4200() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  seedLongSourceNote(loop, 1, 0, 5000, 60);
  loop.beginCapture(CapturePhase::Overdub);

  TEST_ASSERT_TRUE(loop.accumulatePendingNoteChangesForIncomingNote(1, 60, 90, 4000, 4200, 10,
                                                                   overlapIds({1})));
  TEST_ASSERT_EQUAL(1, countKind(loop.pendingNoteChanges(), PendingNoteChangeKind::Add));
  TEST_ASSERT_EQUAL(1, countKind(loop.pendingNoteChanges(), PendingNoteChangeKind::Shorten));
  TEST_ASSERT_EQUAL(0, countKind(loop.pendingNoteChanges(), PendingNoteChangeKind::Hide));

  const PendingNoteChange* shorten = findTransform(loop.pendingNoteChanges(), 1);
  TEST_ASSERT_NOT_NULL(shorten);
  TEST_ASSERT_EQUAL_UINT32(0u, shorten->startTick);
  TEST_ASSERT_EQUAL_UINT32(3999u, shorten->endTick);
}

void test_pending_hide_long_source_wrap_loop_4000() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  seedLongSourceNote(loop, 1, 0, 3999, 60, 4000);
  loop.beginCapture(CapturePhase::Overdub);

  const uint32_t incomingStart = IntervalProjection::tickPhaseInLoop(4000, 0, 4000);
  const uint32_t incomingEnd = IntervalProjection::tickPhaseInLoop(200, 0, 4000);
  TEST_ASSERT_TRUE(loop.accumulatePendingNoteChangesForIncomingNote(
      1, 60, 90, incomingStart, incomingEnd, 10, overlapIds({1})));
  TEST_ASSERT_EQUAL(1, countKind(loop.pendingNoteChanges(), PendingNoteChangeKind::Add));
  TEST_ASSERT_EQUAL(0, countKind(loop.pendingNoteChanges(), PendingNoteChangeKind::Shorten));
  TEST_ASSERT_EQUAL(1, countKind(loop.pendingNoteChanges(), PendingNoteChangeKind::Hide));
  TEST_ASSERT_NOT_NULL(findTransform(loop.pendingNoteChanges(), 1));
  TEST_ASSERT_EQUAL(static_cast<int>(PendingNoteChangeKind::Hide),
                    static_cast<int>(findTransform(loop.pendingNoteChanges(), 1)->kind));
}

void test_pending_shorten_long_source_wrap_loop_4100() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  seedLongSourceNote(loop, 1, 0, 4099, 60, 4100);
  loop.beginCapture(CapturePhase::Overdub);

  const uint32_t incomingStart = IntervalProjection::tickPhaseInLoop(4000, 0, 4100);
  const uint32_t incomingEnd = IntervalProjection::tickPhaseInLoop(200, 0, 4100);
  TEST_ASSERT_TRUE(loop.accumulatePendingNoteChangesForIncomingNote(
      1, 60, 90, incomingStart, incomingEnd, 10, overlapIds({1})));
  TEST_ASSERT_EQUAL(1, countKind(loop.pendingNoteChanges(), PendingNoteChangeKind::Add));
  TEST_ASSERT_EQUAL(1, countKind(loop.pendingNoteChanges(), PendingNoteChangeKind::Shorten));
  TEST_ASSERT_EQUAL(0, countKind(loop.pendingNoteChanges(), PendingNoteChangeKind::Hide));

  const PendingNoteChange* shorten = findTransform(loop.pendingNoteChanges(), 1);
  TEST_ASSERT_NOT_NULL(shorten);
  TEST_ASSERT_EQUAL_UINT32(0u, shorten->startTick);
  TEST_ASSERT_EQUAL_UINT32(3999u, shorten->endTick);
}

void test_pending_wrap_crossing_incoming_skips_head_hide() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  seedLongSourceNote(loop, 1, 8, 40, 60);
  loop.beginCapture(CapturePhase::Overdub);

  TEST_ASSERT_TRUE(loop.accumulatePendingNoteChangesForIncomingNote(
      1, 60, 90, kLoopLen - 40, 50, 10, overlapIds({1})));
  TEST_ASSERT_EQUAL(1, countKind(loop.pendingNoteChanges(), PendingNoteChangeKind::Add));
  TEST_ASSERT_EQUAL(0, countKind(loop.pendingNoteChanges(), PendingNoteChangeKind::Shorten));
  TEST_ASSERT_EQUAL(0, countKind(loop.pendingNoteChanges(), PendingNoteChangeKind::Hide));
  TEST_ASSERT_NULL(findTransform(loop.pendingNoteChanges(), 1));
  TEST_ASSERT_EQUAL_UINT32(1, loop.overlapHoldTotals().noteOffs);
  TEST_ASSERT_EQUAL_UINT32(1, loop.overlapHoldTotals().add);
  TEST_ASSERT_EQUAL_UINT32(0, loop.overlapHoldTotals().hide);
}

void test_seal_pending_shorten_to_edit_pass_after_overdub_publish() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  seedLongSourceNote(loop, 1, 50, 200, 60);
  loop.beginCapture(CapturePhase::Overdub);
  TEST_ASSERT_TRUE(loop.accumulatePendingNoteChangesForIncomingNote(1, 60, 90, 120, 160, 10,
                                                                   overlapIds({1})));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(120, 1, 60, 90)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(160, 1, 60, 0)));
  TEST_ASSERT_EQUAL(SealOutcome::Ok, loop.sealCapture(0));
  TEST_ASSERT_TRUE(loop.commitPendingCapturePass());
  TEST_ASSERT_EQUAL(1u, loop.passes.overdubPasses.size());

  const EditPassIdList companionIds = loop.sealPendingNoteChangesToEditPasses();
  TEST_ASSERT_EQUAL(1u, companionIds.size());
  TEST_ASSERT_FALSE(loop.hasPendingNoteChanges());
  TEST_ASSERT_EQUAL(1u, loop.passes.editPasses.size());
  TEST_ASSERT_EQUAL(static_cast<int>(EditActionType::Update),
                    static_cast<int>(loop.passes.editPasses[0].actionType));
  TEST_ASSERT_EQUAL(static_cast<int>(EditPropertyType::Length),
                    static_cast<int>(loop.passes.editPasses[0].propertyType));
  TEST_ASSERT_EQUAL_UINT32(1u, loop.passes.editPasses[0].targetNoteId);
  TEST_ASSERT_EQUAL_UINT32(119u, loop.passes.editPasses[0].endTick);
  TEST_ASSERT_EQUAL_UINT8(kOverdubCompanionEditPassIndex, loop.passes.editPasses[0].editPassIndex);

  SessionMidiEventVec flat;
  loop.gatherCommittedEvents(flat);
  bool foundShortenedOff = false;
  for (const MidiEvent& evt : flat) {
    if (evt.isNoteOff() && evt.data.noteData.note == 60 && evt.tick == 119) {
      foundShortenedOff = true;
    }
  }
  TEST_ASSERT_TRUE(foundShortenedOff);
}

int main(int /*argc*/, char** /*argv*/) {
  UNITY_BEGIN();
  RUN_TEST(test_pending_requires_source_view);
  RUN_TEST(test_pending_add_only_when_no_overlap);
  RUN_TEST(test_no_overlap_with_companion_edit_does_not_full_materialize);
  RUN_TEST(test_empty_overlap_ids_add_only_when_source_overlaps);
  RUN_TEST(test_pending_shorten_long_source_on_overlap);
  RUN_TEST(test_pending_shorten_ignores_recorded_channel);
  RUN_TEST(test_pending_hide_when_covered);
  RUN_TEST(test_pending_survives_wraps_and_accumulates);
  RUN_TEST(test_establish_resets_overlap_hold_totals);
  RUN_TEST(test_discard_clears_pending_with_source_view);
  RUN_TEST(test_pending_shorten_wrap_crossing_incoming_tail);
  RUN_TEST(test_pending_shorten_long_source_4000_4200);
  RUN_TEST(test_pending_hide_long_source_wrap_loop_4000);
  RUN_TEST(test_pending_shorten_long_source_wrap_loop_4100);
  RUN_TEST(test_pending_wrap_crossing_incoming_skips_head_hide);
  RUN_TEST(test_seal_pending_shorten_to_edit_pass_after_overdub_publish);
  return UNITY_END();
}
