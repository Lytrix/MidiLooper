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
#include "LoopContentResolution.h"
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

bool hasDisplayNote(const NoteUtils::DisplayNoteVec& notes, uint8_t pitch, uint32_t startTick) {
  for (const NoteUtils::DisplayNote& note : notes) {
    if (note.note == pitch && note.startTick == startTick) {
      return true;
    }
  }
  return false;
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

void prepareLoopContent(Loop& loop) {
  LoopContentResolution::deviceGateReset();
  LoopContentResolution::DeviceGateSample sample;
  LoopContentResolution::measureDeviceGate(loop.passes, loop.loopLengthTicks, sample);
  LoopContentResolution::deviceGateComplete(loop.playbackRevision);
  TEST_ASSERT_TRUE(LoopContentResolution::preparedWindowReady(loop.playbackRevision));
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

void test_overdub_consumes_existing_source_view_overlap() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  seedLongSourceNote(loop, 1, 50, 200, 60);
  loop.beginCapture(CapturePhase::Overdub);
  Loop::resetCommittedPitchQueryWork();

  TEST_ASSERT_TRUE(loop.accumulatePendingNoteChangesForIncomingNote(1, 60, 90, 120, 160, 10,
                                                                   overlapIds({})));
  TEST_ASSERT_EQUAL(1, countKind(loop.pendingNoteChanges(), PendingNoteChangeKind::Add));
  TEST_ASSERT_TRUE(countKind(loop.pendingNoteChanges(), PendingNoteChangeKind::Shorten) +
                       countKind(loop.pendingNoteChanges(), PendingNoteChangeKind::Hide) >=
                   1);
  TEST_ASSERT_NOT_NULL(findTransform(loop.pendingNoteChanges(), 1));
  TEST_ASSERT_EQUAL_UINT32(0, Loop::committedEventsFullMaterializeCount());
  TEST_ASSERT_EQUAL_UINT32(1, loop.overlapHoldTotals().noteOffs);
  TEST_ASSERT_EQUAL_UINT32(1, loop.overlapHoldTotals().emptySets);
  TEST_ASSERT_EQUAL_UINT32(0, loop.overlapHoldTotals().lookedUp);
}

void test_overdub_consumes_source_view_when_hold_ids_incomplete() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  loop.loopLengthTicks = kLoopLen;
  LoopEventStore store;
  TEST_ASSERT_TRUE(storeAppendNoteOn(store, 50, 1, 60, 100, 1));
  TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOff(200, 1, 60, 0)));
  TEST_ASSERT_TRUE(storeAppendNoteOn(store, 100, 1, 60, 100, 2));
  TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOff(250, 1, 60, 0)));
  loop.seedRecordPassFromStore(store);
  loop.nextNoteId_ = 3;
  loop.beginCapture(CapturePhase::Overdub);
  Loop::resetCommittedPitchQueryWork();

  TEST_ASSERT_TRUE(loop.accumulatePendingNoteChangesForIncomingNote(1, 60, 90, 120, 160, 10,
                                                                   overlapIds({1})));
  TEST_ASSERT_EQUAL(1, countKind(loop.pendingNoteChanges(), PendingNoteChangeKind::Add));
  TEST_ASSERT_NOT_NULL(findTransform(loop.pendingNoteChanges(), 1));
  TEST_ASSERT_NOT_NULL(findTransform(loop.pendingNoteChanges(), 2));
  TEST_ASSERT_TRUE(countKind(loop.pendingNoteChanges(), PendingNoteChangeKind::Shorten) +
                       countKind(loop.pendingNoteChanges(), PendingNoteChangeKind::Hide) >=
                   1);
  TEST_ASSERT_EQUAL_UINT32(0, Loop::committedEventsFullMaterializeCount());
  TEST_ASSERT_EQUAL_UINT32(1, loop.overlapHoldTotals().lookedUp);
}

void test_empty_ids_resolve_jit_ahead_note_on_64_bar_loop() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  constexpr uint32_t kBar = Config::TICKS_PER_BAR;
  constexpr uint32_t kLongLoop = kBar * 64;
  loop.loopLengthTicks = kLongLoop;
  LoopEventStore store;
  TEST_ASSERT_TRUE(storeAppendNoteOn(store, 224, 1, 60, 100, 1));
  TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOff(288, 1, 60, 0)));
  for (uint32_t bar = 0; bar < 64; ++bar) {
    const uint32_t onTick = bar * kBar + 10;
    TEST_ASSERT_TRUE(storeAppendNoteOn(store, onTick, 1, 72, 100, static_cast<NoteId>(bar + 2)));
    TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOff(onTick + 40, 1, 72, 0)));
  }
  loop.seedRecordPassFromStore(store);
  loop.nextNoteId_ = 66;
  loop.beginCapture(CapturePhase::Overdub, kLongLoop - 80);

  TEST_ASSERT_FALSE(hasDisplayNote(loop.overdubSourceViewNotes(), 60, 224));
  const size_t notesAtEnter = loop.overdubSourceViewNotes().size();
  TEST_ASSERT_TRUE(notesAtEnter > 0);
  TEST_ASSERT_LESS_THAN(64u, notesAtEnter);

  loop.ensureOverdubSourceNotesForHold(64, 60, nullptr, true);
  TEST_ASSERT_FALSE(hasDisplayNote(loop.overdubSourceViewNotes(), 60, 224));
  TEST_ASSERT_EQUAL(notesAtEnter, loop.overdubSourceViewNotes().size());

  NoteUtils::DisplayNoteVec merged;
  loop.ensureOverdubSourceNotesForHold(64, 60, &merged);
  TEST_ASSERT_EQUAL(1, merged.size());
  TEST_ASSERT_EQUAL(60, merged[0].note);
  TEST_ASSERT_EQUAL_UINT32(224u, merged[0].startTick);
  TEST_ASSERT_TRUE(hasDisplayNote(loop.overdubSourceViewNotes(), 60, 224));
  TEST_ASSERT_EQUAL(notesAtEnter + 1, loop.overdubSourceViewNotes().size());
}

void test_note_off_skips_hold_fill_when_source_view_covers_loop() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  seedLongSourceNote(loop, 1, 50, 200, 60);
  loop.beginCapture(CapturePhase::Overdub, 0);
  TEST_ASSERT_TRUE(loop.hasOverdubSourceView());
  TEST_ASSERT_TRUE(hasDisplayNote(loop.overdubSourceViewNotes(), 60, 50));
  const size_t notesAtEnter = loop.overdubSourceViewNotes().size();
  TEST_ASSERT_TRUE(loop.loopLengthTicks <=
                   Loop::kOverdubSourceWindowBars * Config::TICKS_PER_BAR);

  TEST_ASSERT_TRUE(loop.accumulatePendingNoteChangesForIncomingNote(1, 60, 90, 120, 160, 10,
                                                                   overlapIds({})));
  TEST_ASSERT_EQUAL(notesAtEnter, loop.overdubSourceViewNotes().size());
  TEST_ASSERT_EQUAL(1, countKind(loop.pendingNoteChanges(), PendingNoteChangeKind::Add));
  TEST_ASSERT_TRUE(countKind(loop.pendingNoteChanges(), PendingNoteChangeKind::Shorten) +
                       countKind(loop.pendingNoteChanges(), PendingNoteChangeKind::Hide) >=
                   1);
  TEST_ASSERT_NOT_NULL(findTransform(loop.pendingNoteChanges(), 1));
  TEST_ASSERT_EQUAL_UINT32(1, loop.overlapHoldTotals().emptySets);
  TEST_ASSERT_EQUAL_UINT32(0, loop.overlapHoldTotals().lookedUp);
}

void test_empty_ids_shorten_jit_ahead_after_sounding_snapshot() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  constexpr uint32_t kLongLoop = Config::TICKS_PER_BAR * 64;
  seedLongSourceNote(loop, 1, 224, 288, 60, kLongLoop);
  loop.beginCapture(CapturePhase::Overdub, kLongLoop - 80);
  TEST_ASSERT_FALSE(hasDisplayNote(loop.overdubSourceViewNotes(), 60, 224));

  loop.ensureOverdubSourceNotesForHold(64, 60, nullptr, true);
  TEST_ASSERT_FALSE(hasDisplayNote(loop.overdubSourceViewNotes(), 60, 224));

  TEST_ASSERT_TRUE(loop.accumulatePendingNoteChangesForIncomingNote(1, 60, 90, 64, 240, 10,
                                                                   overlapIds({})));
  TEST_ASSERT_EQUAL(1, countKind(loop.pendingNoteChanges(), PendingNoteChangeKind::Add));
  TEST_ASSERT_TRUE(countKind(loop.pendingNoteChanges(), PendingNoteChangeKind::Shorten) +
                       countKind(loop.pendingNoteChanges(), PendingNoteChangeKind::Hide) >=
                   1);
  TEST_ASSERT_EQUAL_UINT32(0, Loop::committedEventsFullMaterializeCount());
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

void test_pending_hide_same_start_longer() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  seedLongSourceNote(loop, 1, 64, 240, 60);
  loop.beginCapture(CapturePhase::Overdub);

  TEST_ASSERT_TRUE(loop.accumulatePendingNoteChangesForIncomingNote(1, 60, 90, 64, 288, 10,
                                                                   overlapIds({1})));
  TEST_ASSERT_EQUAL(1, countKind(loop.pendingNoteChanges(), PendingNoteChangeKind::Add));
  TEST_ASSERT_EQUAL(0, countKind(loop.pendingNoteChanges(), PendingNoteChangeKind::Shorten));
  TEST_ASSERT_EQUAL(1, countKind(loop.pendingNoteChanges(), PendingNoteChangeKind::Hide));
  TEST_ASSERT_NOT_NULL(findTransform(loop.pendingNoteChanges(), 1));
  TEST_ASSERT_EQUAL(static_cast<int>(PendingNoteChangeKind::Hide),
                    static_cast<int>(findTransform(loop.pendingNoteChanges(), 1)->kind));
}

void test_pending_hide_applies_to_display_notes_not_source_view() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  seedLongSourceNote(loop, 1, 64, 240, 60);
  loop.beginCapture(CapturePhase::Overdub);

  TEST_ASSERT_TRUE(loop.accumulatePendingNoteChangesForIncomingNote(1, 60, 90, 64, 288, 10,
                                                                   overlapIds({1})));
  NoteUtils::DisplayNoteVec paint = loop.overdubSourceViewNotes();
  TEST_ASSERT_TRUE(hasDisplayNote(paint, 60, 64));
  loop.applyPendingNoteChangesToDisplayNotes(paint);
  TEST_ASSERT_FALSE(hasDisplayNote(paint, 60, 64));
  TEST_ASSERT_TRUE(hasDisplayNote(loop.overdubSourceViewNotes(), 60, 64));
}

void test_pending_shorten_applies_to_display_notes() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  seedLongSourceNote(loop, 1, 50, 200, 60);
  loop.beginCapture(CapturePhase::Overdub);

  TEST_ASSERT_TRUE(loop.accumulatePendingNoteChangesForIncomingNote(1, 60, 90, 120, 160, 10,
                                                                   overlapIds({1})));
  NoteUtils::DisplayNoteVec paint = loop.overdubSourceViewNotes();
  loop.applyPendingNoteChangesToDisplayNotes(paint);
  bool shortened = false;
  for (const NoteUtils::DisplayNote& note : paint) {
    if (note.noteId == 1 && note.startTick == 50 && note.endTick == 119) {
      shortened = true;
    }
  }
  TEST_ASSERT_TRUE(shortened);
  bool sourceUnchanged = false;
  for (const NoteUtils::DisplayNote& note : loop.overdubSourceViewNotes()) {
    if (note.noteId == 1 && note.startTick == 50 && note.endTick == 200) {
      sourceUnchanged = true;
    }
  }
  TEST_ASSERT_TRUE(sourceUnchanged);
}

void test_wrap_keeps_source_view_inner_shortens_prior_add() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  loop.loopLengthTicks = kLoopLen;
  loop.beginCapture(CapturePhase::Overdub);

  TEST_ASSERT_TRUE(loop.accumulatePendingNoteChangesForIncomingNote(1, 60, 90, 64, 240, 10,
                                                                   overlapIds({})));
  loop.applyPendingNoteChangesToOverdubSourceView();
  (void)loop.sealPendingNoteChangesToEditPasses();
  loop.beginCapture(CapturePhase::Overdub);

  bool foundPriorAdd = false;
  for (const NoteUtils::DisplayNote& note : loop.overdubSourceViewNotes()) {
    if (note.noteId == 10 && note.startTick == 64 && note.endTick == 240) {
      foundPriorAdd = true;
    }
  }
  TEST_ASSERT_TRUE(foundPriorAdd);

  TEST_ASSERT_TRUE(loop.accumulatePendingNoteChangesForIncomingNote(1, 60, 90, 224, 240, 11,
                                                                   overlapIds({10})));
  TEST_ASSERT_EQUAL(1, countKind(loop.pendingNoteChanges(), PendingNoteChangeKind::Add));
  TEST_ASSERT_EQUAL(1, countKind(loop.pendingNoteChanges(), PendingNoteChangeKind::Shorten));
  const PendingNoteChange* shorten = findTransform(loop.pendingNoteChanges(), 10);
  TEST_ASSERT_NOT_NULL(shorten);
  TEST_ASSERT_EQUAL_UINT32(64u, shorten->startTick);
  TEST_ASSERT_EQUAL_UINT32(223u, shorten->endTick);
}

void test_wrap_commit_keeps_source_view_same_start_longer_hides_prior_add() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  seedLongSourceNote(loop, 1, 64, 176, 60);
  loop.openOverdubSession(0);
  loop.beginCapture(CapturePhase::Overdub);

  TEST_ASSERT_TRUE(loop.accumulatePendingNoteChangesForIncomingNote(1, 60, 90, 64, 240, 10,
                                                                   overlapIds({1})));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(noteOnWithNoteId(64, 1, 60, 90, 10)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(240, 1, 60, 0)));
  TEST_ASSERT_EQUAL(SealOutcome::Ok, loop.sealCapture(0));
  TEST_ASSERT_TRUE(loop.commitPendingCapturePass());
  TEST_ASSERT_TRUE(loop.hasOverdubSourceView());
  (void)loop.sealPendingNoteChangesToEditPasses();
  loop.rebuildOverdubSourceView(0);
  loop.beginCapture(CapturePhase::Overdub);
  TEST_ASSERT_TRUE(loop.hasOverdubSourceView());

  bool foundPriorAdd = false;
  bool foundHiddenOccupant = false;
  for (const NoteUtils::DisplayNote& note : loop.overdubSourceViewNotes()) {
    if (note.noteId == 10 && note.startTick == 64 && note.endTick == 240) {
      foundPriorAdd = true;
    }
    if (note.noteId == 1 && note.startTick == 64 && note.endTick == 176) {
      foundHiddenOccupant = true;
    }
  }
  TEST_ASSERT_TRUE(foundPriorAdd);
  TEST_ASSERT_FALSE(foundHiddenOccupant);

  TEST_ASSERT_TRUE(loop.accumulatePendingNoteChangesForIncomingNote(1, 60, 90, 64, 288, 11,
                                                                   overlapIds({10})));
  TEST_ASSERT_EQUAL(1, countKind(loop.pendingNoteChanges(), PendingNoteChangeKind::Hide));
  TEST_ASSERT_NOT_NULL(findTransform(loop.pendingNoteChanges(), 10));
}

void test_wrap_commit_hide_drops_shorter_same_start_from_display() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  loop.loopLengthTicks = kLoopLen;
  loop.openOverdubSession(0);
  loop.beginCapture(CapturePhase::Overdub);

  TEST_ASSERT_TRUE(loop.accumulatePendingNoteChangesForIncomingNote(1, 60, 90, 64, 240, 10,
                                                                   overlapIds({})));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(noteOnWithNoteId(64, 1, 60, 90, 10)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(240, 1, 60, 0)));
  TEST_ASSERT_EQUAL(SealOutcome::Ok, loop.sealCapture(0));
  TEST_ASSERT_TRUE(loop.commitPendingCapturePass());
  loop.applyPendingNoteChangesToOverdubSourceView();
  (void)loop.sealPendingNoteChangesToEditPasses();
  loop.invalidateCaches();
  loop.beginCapture(CapturePhase::Overdub);

  TEST_ASSERT_TRUE(loop.accumulatePendingNoteChangesForIncomingNote(1, 60, 90, 64, 288, 11,
                                                                   overlapIds({10})));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(noteOnWithNoteId(64, 1, 60, 90, 11)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(288, 1, 60, 0)));
  TEST_ASSERT_EQUAL(SealOutcome::Ok, loop.sealCapture(0));
  TEST_ASSERT_TRUE(loop.commitPendingCapturePass());
  loop.applyPendingNoteChangesToOverdubSourceView();
  const EditPassIdList companions = loop.sealPendingNoteChangesToEditPasses();
  TEST_ASSERT_EQUAL(1u, companions.size());
  loop.invalidateCaches();

  SessionMidiEventVec flat;
  loop.gatherCommittedEvents(flat);
  NoteUtils::DisplayNoteVec notes =
      NoteUtils::reconstructDisplayNotes(flat, kLoopLen, false, false);
  loop.appendOverdubPassDisplayNotes(notes);

  int sixtyAt64 = 0;
  bool has176 = false;
  bool has224 = false;
  for (const NoteUtils::DisplayNote& note : notes) {
    if (note.note != 60 || note.startTick != 64) {
      continue;
    }
    ++sixtyAt64;
    if (note.endTick == 240) {
      has176 = true;
    }
    if (note.endTick == 288) {
      has224 = true;
    }
  }
  TEST_ASSERT_EQUAL(1, sixtyAt64);
  TEST_ASSERT_FALSE(has176);
  TEST_ASSERT_TRUE(has224);
}

void test_wrap_keeps_source_view_same_start_longer_hides_prior_add() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  loop.loopLengthTicks = kLoopLen;
  loop.beginCapture(CapturePhase::Overdub);

  TEST_ASSERT_TRUE(loop.accumulatePendingNoteChangesForIncomingNote(1, 60, 90, 64, 240, 10,
                                                                   overlapIds({})));
  loop.applyPendingNoteChangesToOverdubSourceView();
  (void)loop.sealPendingNoteChangesToEditPasses();
  loop.beginCapture(CapturePhase::Overdub);

  TEST_ASSERT_TRUE(loop.accumulatePendingNoteChangesForIncomingNote(1, 60, 90, 64, 288, 11,
                                                                   overlapIds({10})));
  TEST_ASSERT_EQUAL(1, countKind(loop.pendingNoteChanges(), PendingNoteChangeKind::Hide));
  TEST_ASSERT_NOT_NULL(findTransform(loop.pendingNoteChanges(), 10));
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
  loop.establishOverdubSourceView(0);
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

void test_pending_hide_long_source_wrap_loop_4100() {
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
  TEST_ASSERT_EQUAL(0, countKind(loop.pendingNoteChanges(), PendingNoteChangeKind::Shorten));
  TEST_ASSERT_EQUAL(1, countKind(loop.pendingNoteChanges(), PendingNoteChangeKind::Hide));
  TEST_ASSERT_NOT_NULL(findTransform(loop.pendingNoteChanges(), 1));
  TEST_ASSERT_EQUAL(static_cast<int>(PendingNoteChangeKind::Hide),
                    static_cast<int>(findTransform(loop.pendingNoteChanges(), 1)->kind));
  TEST_ASSERT_EQUAL_UINT32(1, loop.overlapHoldTotals().noteOffs);
  TEST_ASSERT_EQUAL_UINT32(1, loop.overlapHoldTotals().add);
}

void test_pending_wrap_crossing_incoming_consumes_head_occupied_lane() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  constexpr uint32_t kOneBar = Config::TICKS_PER_BAR;
  seedLongSourceNote(loop, 1, 64, 288, 60, kOneBar);
  loop.beginCapture(CapturePhase::Overdub);

  TEST_ASSERT_TRUE(loop.accumulatePendingNoteChangesForIncomingNote(
      1, 60, 90, kOneBar - 40, 240, 10, overlapIds({1})));
  TEST_ASSERT_EQUAL(1, countKind(loop.pendingNoteChanges(), PendingNoteChangeKind::Add));
  TEST_ASSERT_EQUAL_UINT32(1, loop.overlapHoldTotals().noteOffs);
  TEST_ASSERT_EQUAL_UINT32(1, loop.overlapHoldTotals().add);
  const PendingNoteChange* transform = findTransform(loop.pendingNoteChanges(), 1);
  TEST_ASSERT_NOT_NULL(transform);
  TEST_ASSERT_TRUE(transform->kind == PendingNoteChangeKind::Hide ||
                   transform->kind == PendingNoteChangeKind::Shorten);
  TEST_ASSERT_EQUAL(1, countKind(loop.pendingNoteChanges(), PendingNoteChangeKind::Hide) +
                           countKind(loop.pendingNoteChanges(), PendingNoteChangeKind::Shorten));
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

void test_prepared_present_note_ids_match_source_hold_participants() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  seedLongSourceNote(loop, 1, 50, 200, 60);
  prepareLoopContent(loop);
  loop.beginCapture(CapturePhase::Overdub, 0);
  TEST_ASSERT_TRUE(loop.hasOverdubSourceView());

  OverlapNoteIdSet sourceIds;
  loop.collectOverdubSourceHoldParticipantIds(100, 60, sourceIds);
  OverlapNoteIdSet preparedIds;
  TEST_ASSERT_TRUE(loop.tryCollectPreparedPresentNoteIdsAtTick(100, 60, preparedIds));
  TEST_ASSERT_EQUAL_UINT32(1u, static_cast<uint32_t>(sourceIds.size()));
  TEST_ASSERT_TRUE(sourceIds.contains(1));
  TEST_ASSERT_TRUE(sourceIds == preparedIds);
  LoopContentResolution::deviceGateReset();
}

void test_prepared_present_note_ids_miss_does_not_fill_source_window() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  LoopContentResolution::deviceGateReset();
  Loop loop;
  seedLongSourceNote(loop, 1, 50, 200, 60);
  loop.beginCapture(CapturePhase::Overdub, 0);
  TEST_ASSERT_TRUE(loop.hasOverdubSourceView());
  const size_t notesBefore = loop.overdubSourceViewNotes().size();
  TEST_ASSERT_FALSE(LoopContentResolution::preparedWindowReady(loop.playbackRevision));

  OverlapNoteIdSet preparedIds;
  preparedIds.insert(99);
  TEST_ASSERT_FALSE(loop.tryCollectPreparedPresentNoteIdsAtTick(100, 60, preparedIds));
  TEST_ASSERT_EQUAL_UINT32(0u, static_cast<uint32_t>(preparedIds.size()));
  TEST_ASSERT_EQUAL(notesBefore, loop.overdubSourceViewNotes().size());
}

void test_prepared_present_note_ids_filters_pitch_and_exclusive_end() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  seedLongSourceNote(loop, 1, 50, 200, 60);
  prepareLoopContent(loop);
  loop.beginCapture(CapturePhase::Overdub, 0);

  OverlapNoteIdSet atStart;
  TEST_ASSERT_TRUE(loop.tryCollectPreparedPresentNoteIdsAtTick(50, 60, atStart));
  TEST_ASSERT_TRUE(atStart.contains(1));

  OverlapNoteIdSet atEnd;
  TEST_ASSERT_TRUE(loop.tryCollectPreparedPresentNoteIdsAtTick(200, 60, atEnd));
  TEST_ASSERT_FALSE(atEnd.contains(1));
  TEST_ASSERT_EQUAL_UINT32(0u, static_cast<uint32_t>(atEnd.size()));

  OverlapNoteIdSet otherPitch;
  TEST_ASSERT_TRUE(loop.tryCollectPreparedPresentNoteIdsAtTick(100, 72, otherPitch));
  TEST_ASSERT_EQUAL_UINT32(0u, static_cast<uint32_t>(otherPitch.size()));
  LoopContentResolution::deviceGateReset();
}

void test_note_on_occupy_uses_prepared_present_without_source_window_fill() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  constexpr uint32_t kBar = Config::TICKS_PER_BAR;
  constexpr uint32_t kLongLoop = kBar * 64;
  constexpr uint32_t kHoldTick = 20000;
  seedLongSourceNote(loop, 1, 10, 25000, 60, kLongLoop);
  prepareLoopContent(loop);
  loop.beginCapture(CapturePhase::Overdub, kHoldTick);
  TEST_ASSERT_TRUE(loop.hasOverdubSourceView());
  TEST_ASSERT_FALSE(hasDisplayNote(loop.overdubSourceViewNotes(), 60, 10));
  const size_t notesAtEnter = loop.overdubSourceViewNotes().size();

  OverlapNoteIdSet sourceIds;
  loop.collectOverdubSourceHoldParticipantIds(kHoldTick, 60, sourceIds);
  TEST_ASSERT_EQUAL_UINT32(0u, static_cast<uint32_t>(sourceIds.size()));

  OverlapNoteIdSet occupyIds;
  loop.collectOverdubNoteOnParticipantIds(kHoldTick, 60, occupyIds);
  TEST_ASSERT_EQUAL_UINT32(1u, static_cast<uint32_t>(occupyIds.size()));
  TEST_ASSERT_TRUE(occupyIds.contains(1));
  TEST_ASSERT_EQUAL(notesAtEnter, loop.overdubSourceViewNotes().size());
  TEST_ASSERT_FALSE(hasDisplayNote(loop.overdubSourceViewNotes(), 60, 10));
  LoopContentResolution::deviceGateReset();
}

void test_note_on_occupy_miss_uses_source_view_without_window_fill() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  LoopContentResolution::deviceGateReset();
  Loop loop;
  constexpr uint32_t kBar = Config::TICKS_PER_BAR;
  constexpr uint32_t kLongLoop = kBar * 64;
  constexpr uint32_t kHoldTick = 20000;
  seedLongSourceNote(loop, 1, 10, 25000, 60, kLongLoop);
  loop.beginCapture(CapturePhase::Overdub, kHoldTick);
  TEST_ASSERT_TRUE(loop.hasOverdubSourceView());
  TEST_ASSERT_FALSE(LoopContentResolution::preparedWindowReady(loop.playbackRevision));
  TEST_ASSERT_FALSE(hasDisplayNote(loop.overdubSourceViewNotes(), 60, 10));
  const size_t notesAtEnter = loop.overdubSourceViewNotes().size();

  OverlapNoteIdSet occupyIds;
  occupyIds.insert(99);
  loop.collectOverdubNoteOnParticipantIds(kHoldTick, 60, occupyIds);
  TEST_ASSERT_EQUAL_UINT32(0u, static_cast<uint32_t>(occupyIds.size()));
  TEST_ASSERT_EQUAL(notesAtEnter, loop.overdubSourceViewNotes().size());
  TEST_ASSERT_FALSE(hasDisplayNote(loop.overdubSourceViewNotes(), 60, 10));
}

void test_note_on_occupy_matches_source_view_when_note_is_in_window() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  seedLongSourceNote(loop, 1, 50, 200, 60);
  prepareLoopContent(loop);
  loop.beginCapture(CapturePhase::Overdub, 0);
  TEST_ASSERT_TRUE(loop.hasOverdubSourceView());

  OverlapNoteIdSet occupyIds;
  loop.collectOverdubNoteOnParticipantIds(100, 60, occupyIds);
  OverlapNoteIdSet sourceIds;
  loop.collectOverdubSourceHoldParticipantIds(100, 60, sourceIds);
  TEST_ASSERT_EQUAL_UINT32(1u, static_cast<uint32_t>(occupyIds.size()));
  TEST_ASSERT_TRUE(occupyIds.contains(1));
  TEST_ASSERT_TRUE(occupyIds == sourceIds);
  LoopContentResolution::deviceGateReset();
}

int main(int /*argc*/, char** /*argv*/) {
  UNITY_BEGIN();
  RUN_TEST(test_pending_requires_source_view);
  RUN_TEST(test_pending_add_only_when_no_overlap);
  RUN_TEST(test_no_overlap_with_companion_edit_does_not_full_materialize);
  RUN_TEST(test_overdub_consumes_existing_source_view_overlap);
  RUN_TEST(test_overdub_consumes_source_view_when_hold_ids_incomplete);
  RUN_TEST(test_empty_ids_resolve_jit_ahead_note_on_64_bar_loop);
  RUN_TEST(test_note_off_skips_hold_fill_when_source_view_covers_loop);
  RUN_TEST(test_empty_ids_shorten_jit_ahead_after_sounding_snapshot);
  RUN_TEST(test_pending_shorten_long_source_on_overlap);
  RUN_TEST(test_pending_shorten_ignores_recorded_channel);
  RUN_TEST(test_pending_hide_when_covered);
  RUN_TEST(test_pending_hide_same_start_longer);
  RUN_TEST(test_pending_hide_applies_to_display_notes_not_source_view);
  RUN_TEST(test_pending_shorten_applies_to_display_notes);
  RUN_TEST(test_wrap_keeps_source_view_inner_shortens_prior_add);
  RUN_TEST(test_wrap_keeps_source_view_same_start_longer_hides_prior_add);
  RUN_TEST(test_wrap_commit_keeps_source_view_same_start_longer_hides_prior_add);
  RUN_TEST(test_wrap_commit_hide_drops_shorter_same_start_from_display);
  RUN_TEST(test_pending_survives_wraps_and_accumulates);
  RUN_TEST(test_establish_resets_overlap_hold_totals);
  RUN_TEST(test_discard_clears_pending_with_source_view);
  RUN_TEST(test_pending_shorten_wrap_crossing_incoming_tail);
  RUN_TEST(test_pending_shorten_long_source_4000_4200);
  RUN_TEST(test_pending_hide_long_source_wrap_loop_4000);
  RUN_TEST(test_pending_hide_long_source_wrap_loop_4100);
  RUN_TEST(test_pending_wrap_crossing_incoming_consumes_head_occupied_lane);
  RUN_TEST(test_seal_pending_shorten_to_edit_pass_after_overdub_publish);
  RUN_TEST(test_prepared_present_note_ids_match_source_hold_participants);
  RUN_TEST(test_prepared_present_note_ids_miss_does_not_fill_source_window);
  RUN_TEST(test_prepared_present_note_ids_filters_pitch_and_exclusive_end);
  RUN_TEST(test_note_on_occupy_uses_prepared_present_without_source_window_fill);
  RUN_TEST(test_note_on_occupy_miss_uses_source_view_without_window_fill);
  RUN_TEST(test_note_on_occupy_matches_source_view_when_note_is_in_window);
  return UNITY_END();
}
