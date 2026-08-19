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
#include "ActiveNoteLedger.h"
#include "Utils/CommittedPlaybackLedgerCatchUp.h"
#include "Utils/IntervalProjection.h"
#include "Utils/NoteUtils.h"
#include "Utils/PlaybackCursorAdvance.h"

#include "../../src/Utils/PlaybackCursorAdvance.cpp"
#include "../../src/Utils/CommittedPlaybackLedgerCatchUp.cpp"

#include <algorithm>
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

void applyGatheredThroughTick(ActiveNoteLedger& ledger, const MidiEventVec& events,
                              uint32_t throughTick) {
  for (const MidiEvent& evt : events) {
    if (evt.tick > throughTick) {
      continue;
    }
    (void)ledger.applyPlaybackEvent(1, evt);
  }
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

void test_note_on_occupy_reads_ledger_note_id() {
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

  ActiveNoteLedger ledger;
  ledger.noteOn(1, 60, 1, 10, 100);
  OverlapNoteIdSet occupyIds;
  loop.collectOverdubNoteOnParticipantIds(60, 1, ledger, occupyIds);
  TEST_ASSERT_EQUAL_UINT32(1u, static_cast<uint32_t>(occupyIds.size()));
  TEST_ASSERT_TRUE(occupyIds.contains(1));
  TEST_ASSERT_EQUAL(notesAtEnter, loop.overdubSourceViewNotes().size());
  TEST_ASSERT_FALSE(hasDisplayNote(loop.overdubSourceViewNotes(), 60, 10));
  LoopContentResolution::deviceGateReset();
}

void test_note_on_occupy_empty_when_ledger_inactive() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  LoopContentResolution::deviceGateReset();
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

  OverlapNoteIdSet preparedIds;
  TEST_ASSERT_TRUE(loop.tryCollectPreparedPresentNoteIdsAtTick(kHoldTick, 60, preparedIds));
  TEST_ASSERT_EQUAL_UINT32(1u, static_cast<uint32_t>(preparedIds.size()));

  ActiveNoteLedger ledger;
  OverlapNoteIdSet occupyIds;
  occupyIds.insert(99);
  loop.collectOverdubNoteOnParticipantIds(60, 1, ledger, occupyIds);
  TEST_ASSERT_EQUAL_UINT32(0u, static_cast<uint32_t>(occupyIds.size()));
  TEST_ASSERT_EQUAL(notesAtEnter, loop.overdubSourceViewNotes().size());
  TEST_ASSERT_FALSE(hasDisplayNote(loop.overdubSourceViewNotes(), 60, 10));
  LoopContentResolution::deviceGateReset();
}

void test_wrap_committed_note_at_s_occupies_ledger_same_tick() {
  // session_20260818_152745: wrap-committed 71 @ 656–720, 1-bar 768, occupy at S.
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  constexpr uint32_t kLoopLenTicks = Config::TICKS_PER_BAR;
  constexpr uint32_t kS = 656;
  constexpr uint32_t kPrev = 655;
  constexpr uint32_t kOff = 720;
  constexpr uint8_t kPitch = 71;
  constexpr NoteId kWrapNoteId = 5;
  seedLongSourceNote(loop, 1, 0, 48, 60, kLoopLenTicks);
  loop.openOverdubSession(kS);
  loop.beginCapture(CapturePhase::Overdub, kS);
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(noteOnWithNoteId(kS, 1, kPitch, 90, kWrapNoteId)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(kOff, 1, kPitch, 0)));
  TEST_ASSERT_EQUAL(CommitResult::Committed, loop.commitCapturePass(CommitReason::OverdubWrap, kS));
  loop.rebuildOverdubSourceView(kS);

  SessionMidiEventVec merged;
  loop.gatherCommittedEvents(merged);
  std::sort(merged.begin(), merged.end(),
            [](const MidiEvent& a, const MidiEvent& b) { return a.tick < b.tick; });
  NoteId wrapId = kInvalidNoteId;
  for (const MidiEvent& evt : merged) {
    if (evt.isNoteOn() && evt.data.noteData.note == kPitch && evt.tick == kS) {
      wrapId = evt.noteId;
      break;
    }
  }
  TEST_ASSERT_EQUAL_UINT32(kWrapNoteId, wrapId);

  struct DirectPlaybackStreamCtx {
    SessionMidiEventVec events;
  };
  DirectPlaybackStreamCtx streamCtx;
  streamCtx.events = merged;

  auto streamSize = [](const void* ctx) -> size_t {
    return static_cast<const DirectPlaybackStreamCtx*>(ctx)->events.size();
  };
  auto streamEventAt = [](const void* ctx, uint16_t cursor) -> const MidiEvent& {
    return static_cast<const DirectPlaybackStreamCtx*>(ctx)->events[cursor];
  };
  auto streamPhase = [](const MidiEvent& evt, const ProjectionContext&) -> uint32_t {
    return evt.tick;
  };
  auto applyLedger = [](void* ctx, const MidiEvent& evt, uint8_t) {
    static_cast<ActiveNoteLedger*>(ctx)->applyPlaybackEvent(1, evt);
  };

  const PlaybackEventStream stream{&streamCtx, streamSize, nullptr, streamEventAt, streamPhase};
  ProjectionContext playbackContext{};
  playbackContext.loopLength = kLoopLenTicks;

  // Miss path from 152745: lastTick already S, then (S, S+1] never crosses the NoteOn at S.
  uint16_t cursorAtS = 0;
  while (static_cast<size_t>(cursorAtS) < streamCtx.events.size() &&
         streamCtx.events[cursorAtS].tick <= kS) {
    ++cursorAtS;
  }
  ActiveNoteLedger missLedger;
  PlaybackTickFrame missFrame{&playbackContext, kS + 1U, kS, false};
  PlaybackCursorAdvanceState missAdvance{&cursorAtS, nullptr};
  TEST_ASSERT_EQUAL(PlaybackAdvanceResult::Completed,
                    advancePlaybackCursor(missAdvance, missFrame, PlaybackEmitPolicy::LayeredSlot,
                                          stream, applyLedger, &missLedger, 0, nullptr, nullptr, 1));
  OverlapNoteIdSet missOccupy;
  loop.collectOverdubNoteOnParticipantIds(kPitch, 1, missLedger, missOccupy);
  TEST_ASSERT_EQUAL_UINT32(0u, static_cast<uint32_t>(missOccupy.size()));

  // Wrap tick: reanchor while lastTick is still prev, then apply (prev, S] on the new stream.
  uint16_t cursorAtPrev = 0;
  while (static_cast<size_t>(cursorAtPrev) < streamCtx.events.size() &&
         streamCtx.events[cursorAtPrev].tick <= kPrev) {
    ++cursorAtPrev;
  }
  ActiveNoteLedger wrapLedger;
  PlaybackTickFrame wrapFrame{&playbackContext, kS, kPrev, false};
  PlaybackCursorAdvanceState wrapAdvance{&cursorAtPrev, nullptr};
  TEST_ASSERT_EQUAL(PlaybackAdvanceResult::Completed,
                    advancePlaybackCursor(wrapAdvance, wrapFrame, PlaybackEmitPolicy::LayeredSlot,
                                          stream, applyLedger, &wrapLedger, 0, nullptr, nullptr, 1));
  TEST_ASSERT_EQUAL_UINT32(kWrapNoteId, wrapLedger.noteId(1, kPitch));
  OverlapNoteIdSet occupyIds;
  loop.collectOverdubNoteOnParticipantIds(kPitch, 1, wrapLedger, occupyIds);
  TEST_ASSERT_EQUAL_UINT32(1u, static_cast<uint32_t>(occupyIds.size()));
  TEST_ASSERT_TRUE(occupyIds.contains(kWrapNoteId));
}

void test_wrap_pass_events_at_s_occupy_without_merged_rebuild() {
  // session_20260818_180844 wrap 15: wrap-committed 60 @ 416, 1-bar 768.
  // Ledger catch-up uses lastCommittedPassId() chunks only — no full-loop gather.
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  constexpr uint32_t kLoopLenTicks = Config::TICKS_PER_BAR;
  constexpr uint32_t kS = 416;
  constexpr uint32_t kPrev = 408;
  constexpr uint32_t kOff = 480;
  constexpr uint8_t kPitch = 60;
  constexpr NoteId kWrapNoteId = 9;
  seedLongSourceNote(loop, 1, 0, 48, 72, kLoopLenTicks);
  loop.openOverdubSession(kS);
  loop.beginCapture(CapturePhase::Overdub, kS);
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(noteOnWithNoteId(kS, 1, kPitch, 90, kWrapNoteId)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(kOff, 1, kPitch, 0)));
  TEST_ASSERT_EQUAL(CommitResult::Committed, loop.commitCapturePass(CommitReason::OverdubWrap, kS));

  const PassId wrapId = loop.lastCommittedPassId();
  const OverdubPass* wrapPass = nullptr;
  for (const OverdubPass& pass : loop.passes.overdubPasses) {
    if (pass.id == wrapId) {
      wrapPass = &pass;
      break;
    }
  }
  TEST_ASSERT_NOT_NULL(wrapPass);

  SessionMidiEventVec passEvents;
  LoopEventStore::appendChunkRefEvents(wrapPass->committedChunkIds, passEvents);
  TEST_ASSERT_FALSE(passEvents.empty());
  NoteId wrapIdFound = kInvalidNoteId;
  for (const MidiEvent& evt : passEvents) {
    if (evt.isNoteOn() && evt.data.noteData.note == kPitch && evt.tick == kS) {
      wrapIdFound = evt.noteId;
      break;
    }
  }
  TEST_ASSERT_EQUAL_UINT32(kWrapNoteId, wrapIdFound);

  struct DirectPlaybackStreamCtx {
    SessionMidiEventVec events;
  };
  DirectPlaybackStreamCtx streamCtx;
  streamCtx.events = passEvents;

  auto streamSize = [](const void* ctx) -> size_t {
    return static_cast<const DirectPlaybackStreamCtx*>(ctx)->events.size();
  };
  auto streamEventAt = [](const void* ctx, uint16_t cursor) -> const MidiEvent& {
    return static_cast<const DirectPlaybackStreamCtx*>(ctx)->events[cursor];
  };
  auto streamPhase = [](const MidiEvent& evt, const ProjectionContext&) -> uint32_t {
    return evt.tick;
  };
  auto applyLedger = [](void* ctx, const MidiEvent& evt, uint8_t) {
    static_cast<ActiveNoteLedger*>(ctx)->applyPlaybackEvent(1, evt);
  };

  const PlaybackEventStream stream{&streamCtx, streamSize, nullptr, streamEventAt, streamPhase};
  ProjectionContext playbackContext{};
  playbackContext.loopLength = kLoopLenTicks;
  uint16_t cursor = 0;
  ActiveNoteLedger wrapLedger;
  PlaybackTickFrame wrapFrame{&playbackContext, kS, kPrev, false};
  PlaybackCursorAdvanceState wrapAdvance{&cursor, nullptr};
  TEST_ASSERT_EQUAL(PlaybackAdvanceResult::Completed,
                    advancePlaybackCursor(wrapAdvance, wrapFrame, PlaybackEmitPolicy::LayeredSlot,
                                          stream, applyLedger, &wrapLedger, 0, nullptr, nullptr, 1));
  TEST_ASSERT_EQUAL_UINT32(kWrapNoteId, wrapLedger.noteId(1, kPitch));
  OverlapNoteIdSet occupyIds;
  loop.collectOverdubNoteOnParticipantIds(kPitch, 1, wrapLedger, occupyIds);
  TEST_ASSERT_EQUAL_UINT32(1u, static_cast<uint32_t>(occupyIds.size()));
  TEST_ASSERT_TRUE(occupyIds.contains(kWrapNoteId));
}

void test_wrap_pass_events_at_loop_head_occupy_without_merged_rebuild() {
  // session_20260818_185831 wrap 1: wrap-committed 60 On@0 Off@8, reanchor at 696,
  // then atLoopStart (760, 0]. OverdubWrap keeps the 8-tick pair (Q16 is stop-only).
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  constexpr uint32_t kLoopLenTicks = Config::TICKS_PER_BAR;
  constexpr uint32_t kS = 696;
  constexpr uint32_t kPrevAtHead = 760;
  constexpr uint8_t kPitch = 60;
  constexpr NoteId kOnAtZeroId = 11;
  constexpr NoteId kOnAt64Id = 12;
  seedLongSourceNote(loop, 1, 48, 72, 72, kLoopLenTicks);
  loop.openOverdubSession(kS);
  loop.beginCapture(CapturePhase::Overdub, kS);
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(noteOnWithNoteId(0, 1, kPitch, 90, kOnAtZeroId)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(8, 1, kPitch, 0)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(noteOnWithNoteId(64, 1, kPitch, 90, kOnAt64Id)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(416, 1, kPitch, 0)));
  TEST_ASSERT_EQUAL(CommitResult::Committed, loop.commitCapturePass(CommitReason::OverdubWrap, kS));

  const PassId wrapId = loop.lastCommittedPassId();
  const OverdubPass* wrapPass = nullptr;
  for (const OverdubPass& pass : loop.passes.overdubPasses) {
    if (pass.id == wrapId) {
      wrapPass = &pass;
      break;
    }
  }
  TEST_ASSERT_NOT_NULL(wrapPass);

  SessionMidiEventVec passEvents;
  LoopEventStore::appendChunkRefEvents(wrapPass->committedChunkIds, passEvents);
  TEST_ASSERT_FALSE(passEvents.empty());
  NoteId onAtZeroFound = kInvalidNoteId;
  for (const MidiEvent& evt : passEvents) {
    if (evt.isNoteOn() && evt.data.noteData.note == kPitch && evt.tick == 0) {
      onAtZeroFound = evt.noteId;
      break;
    }
  }
  TEST_ASSERT_EQUAL_UINT32(kOnAtZeroId, onAtZeroFound);
  TEST_ASSERT_EQUAL_UINT32(0, IntervalProjection::playbackEventPhase(0, kLoopLenTicks));
  uint32_t offAtZeroCount = 0;
  for (const MidiEvent& evt : passEvents) {
    if (evt.isNoteOff() && evt.data.noteData.note == kPitch && evt.tick == 0) {
      ++offAtZeroCount;
    }
  }
  TEST_ASSERT_EQUAL_UINT32(0, offAtZeroCount);

  std::sort(passEvents.begin(), passEvents.end(),
            [&](const MidiEvent& a, const MidiEvent& b) {
              const uint32_t phaseA = IntervalProjection::playbackEventPhase(a.tick, kLoopLenTicks);
              const uint32_t phaseB = IntervalProjection::playbackEventPhase(b.tick, kLoopLenTicks);
              if (phaseA != phaseB) {
                return phaseA < phaseB;
              }
              return a.tick < b.tick;
            });

  struct DirectPlaybackStreamCtx {
    SessionMidiEventVec events;
  };
  DirectPlaybackStreamCtx streamCtx;
  streamCtx.events = passEvents;
  TEST_ASSERT_FALSE(streamCtx.events.empty());
  TEST_ASSERT_TRUE(streamCtx.events.front().isNoteOn());
  TEST_ASSERT_EQUAL_UINT32(0, streamCtx.events.front().tick);
  TEST_ASSERT_EQUAL_UINT32(
      0, IntervalProjection::playbackEventPhase(streamCtx.events.front().tick, kLoopLenTicks));

  struct AppliedLog {
    ActiveNoteLedger* ledger = nullptr;
    uint32_t appliedOn = 0;
    uint32_t appliedOff = 0;
    uint32_t lastAppliedTick = UINT32_MAX;
  };

  auto streamSize = [](const void* ctx) -> size_t {
    return static_cast<const DirectPlaybackStreamCtx*>(ctx)->events.size();
  };
  auto streamEventAt = [](const void* ctx, uint16_t cursor) -> const MidiEvent& {
    return static_cast<const DirectPlaybackStreamCtx*>(ctx)->events[cursor];
  };
  auto streamPhase = [](const MidiEvent& evt, const ProjectionContext& playbackContext) -> uint32_t {
    return IntervalProjection::playbackEventPhase(evt.tick, playbackContext.loopLength);
  };
  auto applyLedger = [](void* ctx, const MidiEvent& evt, uint8_t) {
    AppliedLog* log = static_cast<AppliedLog*>(ctx);
    log->ledger->applyPlaybackEvent(1, evt);
    if (evt.isNoteOn()) {
      ++log->appliedOn;
    } else if (evt.isNoteOff()) {
      ++log->appliedOff;
    }
    log->lastAppliedTick = evt.tick;
  };

  const PlaybackEventStream stream{&streamCtx, streamSize, nullptr, streamEventAt, streamPhase};
  ProjectionContext playbackContext{};
  playbackContext.loopLength = kLoopLenTicks;

  uint16_t cursor = 0;
  while (static_cast<size_t>(cursor) < streamCtx.events.size()) {
    const uint32_t evPhase =
        IntervalProjection::playbackEventPhase(streamCtx.events[cursor].tick, kLoopLenTicks);
    if (evPhase > kS) {
      break;
    }
    ++cursor;
  }
  TEST_ASSERT_TRUE(cursor > 0);

  cursor = 0;
  ActiveNoteLedger headLedger;
  AppliedLog applied{&headLedger, 0, 0, UINT32_MAX};
  const bool atLoopStart =
      IntervalProjection::isPlaybackCatchUpWindow(kPrevAtHead, 0);
  TEST_ASSERT_TRUE(atLoopStart);
  PlaybackTickFrame headFrame{&playbackContext, 0, kPrevAtHead, atLoopStart};
  PlaybackCursorAdvanceState headAdvance{&cursor, nullptr};
  TEST_ASSERT_EQUAL(PlaybackAdvanceResult::Completed,
                    advancePlaybackCursor(headAdvance, headFrame, PlaybackEmitPolicy::LayeredSlot,
                                          stream, applyLedger, &applied, 0, nullptr, nullptr, 1));
  TEST_ASSERT_EQUAL_UINT32(1, applied.appliedOn);
  TEST_ASSERT_EQUAL_UINT32(0, applied.appliedOff);
  TEST_ASSERT_EQUAL_UINT32(0, applied.lastAppliedTick);
  TEST_ASSERT_EQUAL_UINT32(kOnAtZeroId, headLedger.noteId(1, kPitch));
  OverlapNoteIdSet occupyIds;
  loop.collectOverdubNoteOnParticipantIds(kPitch, 1, headLedger, occupyIds);
  TEST_ASSERT_EQUAL_UINT32(1u, static_cast<uint32_t>(occupyIds.size()));
  TEST_ASSERT_TRUE(occupyIds.contains(kOnAtZeroId));
}

void test_wrap_pass_on_at_96_occupies_after_s_interval() {
  // session_20260818_203948 wrap 2: S=64, occupy 12 @ 96. Wrap-S (prev, S] does not
  // apply On@96. Named write: advancePlaybackCursor (64, 96] after reanchor at S.
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  constexpr uint32_t kLoopLenTicks = Config::TICKS_PER_BAR;
  constexpr uint32_t kS = 64;
  constexpr uint32_t kPrev = 56;
  constexpr uint32_t kOccupy = 96;
  constexpr uint32_t kOff = 200;
  constexpr uint8_t kPitch = 12;
  constexpr NoteId kOnAt96Id = 21;
  seedLongSourceNote(loop, 1, 48, 72, 72, kLoopLenTicks);
  loop.openOverdubSession(kS);
  loop.beginCapture(CapturePhase::Overdub, kS);
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(noteOnWithNoteId(kOccupy, 1, kPitch, 90, kOnAt96Id)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(kOff, 1, kPitch, 0)));
  TEST_ASSERT_EQUAL(CommitResult::Committed, loop.commitCapturePass(CommitReason::OverdubWrap, kS));
  loop.rebuildOverdubSourceView(kOccupy);

  OverlapNoteIdSet sourceViewIds;
  loop.collectOverdubSourceHoldParticipantIds(kOccupy, kPitch, sourceViewIds);
  TEST_ASSERT_EQUAL_UINT32(1u, static_cast<uint32_t>(sourceViewIds.size()));
  TEST_ASSERT_TRUE(sourceViewIds.contains(kOnAt96Id));

  const PassId wrapId = loop.lastCommittedPassId();
  const OverdubPass* wrapPass = nullptr;
  for (const OverdubPass& pass : loop.passes.overdubPasses) {
    if (pass.id == wrapId) {
      wrapPass = &pass;
      break;
    }
  }
  TEST_ASSERT_NOT_NULL(wrapPass);
  SessionMidiEventVec passEvents;
  LoopEventStore::appendChunkRefEvents(wrapPass->committedChunkIds, passEvents);
  TEST_ASSERT_FALSE(passEvents.empty());

  struct DirectPlaybackStreamCtx {
    SessionMidiEventVec events;
  };
  DirectPlaybackStreamCtx streamCtx;
  streamCtx.events = passEvents;

  auto streamSize = [](const void* ctx) -> size_t {
    return static_cast<const DirectPlaybackStreamCtx*>(ctx)->events.size();
  };
  auto streamEventAt = [](const void* ctx, uint16_t cursor) -> const MidiEvent& {
    return static_cast<const DirectPlaybackStreamCtx*>(ctx)->events[cursor];
  };
  auto streamPhase = [](const MidiEvent& evt, const ProjectionContext&) -> uint32_t {
    return evt.tick;
  };
  auto applyLedger = [](void* ctx, const MidiEvent& evt, uint8_t) {
    static_cast<ActiveNoteLedger*>(ctx)->applyPlaybackEvent(1, evt);
  };
  const PlaybackEventStream stream{&streamCtx, streamSize, nullptr, streamEventAt, streamPhase};
  ProjectionContext playbackContext{};
  playbackContext.loopLength = kLoopLenTicks;

  uint16_t cursor = 0;
  while (static_cast<size_t>(cursor) < streamCtx.events.size() &&
         streamCtx.events[cursor].tick <= kS) {
    ++cursor;
  }
  ActiveNoteLedger missLedger;
  PlaybackTickFrame wrapFrame{&playbackContext, kS, kPrev, false};
  PlaybackCursorAdvanceState wrapAdvance{&cursor, nullptr};
  TEST_ASSERT_EQUAL(PlaybackAdvanceResult::Completed,
                    advancePlaybackCursor(wrapAdvance, wrapFrame, PlaybackEmitPolicy::LayeredSlot,
                                          stream, applyLedger, &missLedger, 0, nullptr, nullptr, 1));
  OverlapNoteIdSet missOccupy;
  loop.collectOverdubNoteOnParticipantIds(kPitch, 1, missLedger, missOccupy);
  TEST_ASSERT_EQUAL_UINT32(0u, static_cast<uint32_t>(missOccupy.size()));

  cursor = 0;
  while (static_cast<size_t>(cursor) < streamCtx.events.size() &&
         streamCtx.events[cursor].tick <= kS) {
    ++cursor;
  }
  ActiveNoteLedger hitLedger;
  PlaybackTickFrame occupyFrame{&playbackContext, kOccupy, kS, false};
  PlaybackCursorAdvanceState occupyAdvance{&cursor, nullptr};
  TEST_ASSERT_EQUAL(PlaybackAdvanceResult::Completed,
                    advancePlaybackCursor(occupyAdvance, occupyFrame, PlaybackEmitPolicy::LayeredSlot,
                                          stream, applyLedger, &hitLedger, 0, nullptr, nullptr, 1));
  TEST_ASSERT_EQUAL_UINT32(kOnAt96Id, hitLedger.noteId(1, kPitch));
  OverlapNoteIdSet occupyIds;
  loop.collectOverdubNoteOnParticipantIds(kPitch, 1, hitLedger, occupyIds);
  TEST_ASSERT_EQUAL_UINT32(1u, static_cast<uint32_t>(occupyIds.size()));
  TEST_ASSERT_TRUE(occupyIds.contains(kOnAt96Id));
}

void test_wrap_pass_spanning_on_at_0_occupies_at_96() {
  // On@0 Off@200 still present at 96 after wrap at 64. Loop-head (760, 0] writes
  // the Entry; wrap (56, 64] does not NoteOff it.
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  constexpr uint32_t kLoopLenTicks = Config::TICKS_PER_BAR;
  constexpr uint32_t kS = 64;
  constexpr uint32_t kPrevWrap = 56;
  constexpr uint32_t kPrevHead = 760;
  constexpr uint32_t kOccupy = 96;
  constexpr uint32_t kOff = 200;
  constexpr uint8_t kPitch = 12;
  constexpr NoteId kOnAtZeroId = 22;
  seedLongSourceNote(loop, 1, 48, 72, 72, kLoopLenTicks);
  loop.openOverdubSession(kS);
  loop.beginCapture(CapturePhase::Overdub, kS);
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(noteOnWithNoteId(0, 1, kPitch, 90, kOnAtZeroId)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(kOff, 1, kPitch, 0)));
  TEST_ASSERT_EQUAL(CommitResult::Committed, loop.commitCapturePass(CommitReason::OverdubWrap, kS));
  loop.rebuildOverdubSourceView(kOccupy);

  OverlapNoteIdSet sourceViewIds;
  loop.collectOverdubSourceHoldParticipantIds(kOccupy, kPitch, sourceViewIds);
  TEST_ASSERT_EQUAL_UINT32(1u, static_cast<uint32_t>(sourceViewIds.size()));
  TEST_ASSERT_TRUE(sourceViewIds.contains(kOnAtZeroId));

  const PassId wrapId = loop.lastCommittedPassId();
  const OverdubPass* wrapPass = nullptr;
  for (const OverdubPass& pass : loop.passes.overdubPasses) {
    if (pass.id == wrapId) {
      wrapPass = &pass;
      break;
    }
  }
  TEST_ASSERT_NOT_NULL(wrapPass);
  SessionMidiEventVec passEvents;
  LoopEventStore::appendChunkRefEvents(wrapPass->committedChunkIds, passEvents);

  struct DirectPlaybackStreamCtx {
    SessionMidiEventVec events;
  };
  DirectPlaybackStreamCtx streamCtx;
  streamCtx.events = passEvents;
  std::sort(streamCtx.events.begin(), streamCtx.events.end(),
            [](const MidiEvent& a, const MidiEvent& b) { return a.tick < b.tick; });

  auto streamSize = [](const void* ctx) -> size_t {
    return static_cast<const DirectPlaybackStreamCtx*>(ctx)->events.size();
  };
  auto streamEventAt = [](const void* ctx, uint16_t cursor) -> const MidiEvent& {
    return static_cast<const DirectPlaybackStreamCtx*>(ctx)->events[cursor];
  };
  auto streamPhase = [](const MidiEvent& evt, const ProjectionContext& playbackContext) -> uint32_t {
    return IntervalProjection::playbackEventPhase(evt.tick, playbackContext.loopLength);
  };
  auto applyLedger = [](void* ctx, const MidiEvent& evt, uint8_t) {
    static_cast<ActiveNoteLedger*>(ctx)->applyPlaybackEvent(1, evt);
  };
  const PlaybackEventStream stream{&streamCtx, streamSize, nullptr, streamEventAt, streamPhase};
  ProjectionContext playbackContext{};
  playbackContext.loopLength = kLoopLenTicks;

  uint16_t cursor = 0;
  ActiveNoteLedger ledger;
  const bool atLoopStart = IntervalProjection::isPlaybackCatchUpWindow(kPrevHead, 0);
  TEST_ASSERT_TRUE(atLoopStart);
  PlaybackTickFrame headFrame{&playbackContext, 0, kPrevHead, atLoopStart};
  PlaybackCursorAdvanceState headAdvance{&cursor, nullptr};
  TEST_ASSERT_EQUAL(PlaybackAdvanceResult::Completed,
                    advancePlaybackCursor(headAdvance, headFrame, PlaybackEmitPolicy::LayeredSlot,
                                          stream, applyLedger, &ledger, 0, nullptr, nullptr, 1));
  TEST_ASSERT_EQUAL_UINT32(kOnAtZeroId, ledger.noteId(1, kPitch));

  PlaybackTickFrame wrapFrame{&playbackContext, kS, kPrevWrap, false};
  PlaybackCursorAdvanceState wrapAdvance{&cursor, nullptr};
  TEST_ASSERT_EQUAL(PlaybackAdvanceResult::Completed,
                    advancePlaybackCursor(wrapAdvance, wrapFrame, PlaybackEmitPolicy::LayeredSlot,
                                          stream, applyLedger, &ledger, 0, nullptr, nullptr, 1));
  OverlapNoteIdSet occupyIds;
  loop.collectOverdubNoteOnParticipantIds(kPitch, 1, ledger, occupyIds);
  TEST_ASSERT_EQUAL_UINT32(1u, static_cast<uint32_t>(occupyIds.size()));
  TEST_ASSERT_TRUE(occupyIds.contains(kOnAtZeroId));
}

void test_overdub_stop_still_removes_pairs_shorter_than_min_length() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  TEST_ASSERT_TRUE(noteMinLengthRemoveEnabled);
  Loop loop;
  constexpr uint32_t kLoopLenTicks = Config::TICKS_PER_BAR;
  constexpr uint8_t kPitch = 60;
  constexpr NoteId kShortId = 11;
  constexpr NoteId kKeptId = 12;
  seedLongSourceNote(loop, 1, 48, 72, 72, kLoopLenTicks);
  loop.beginCapture(CapturePhase::Overdub, 0);
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(noteOnWithNoteId(0, 1, kPitch, 90, kShortId)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(8, 1, kPitch, 0)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(noteOnWithNoteId(64, 1, kPitch, 90, kKeptId)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(416, 1, kPitch, 0)));
  TEST_ASSERT_EQUAL(CommitResult::Committed, loop.commitCapturePass(CommitReason::OverdubStop, 0));

  const PassId stopId = loop.lastCommittedPassId();
  const OverdubPass* stopPass = nullptr;
  for (const OverdubPass& pass : loop.passes.overdubPasses) {
    if (pass.id == stopId) {
      stopPass = &pass;
      break;
    }
  }
  TEST_ASSERT_NOT_NULL(stopPass);
  SessionMidiEventVec passEvents;
  LoopEventStore::appendChunkRefEvents(stopPass->committedChunkIds, passEvents);
  bool hasShortOn = false;
  bool hasKeptOn = false;
  for (const MidiEvent& evt : passEvents) {
    if (!evt.isNoteOn() || evt.data.noteData.note != kPitch) {
      continue;
    }
    if (evt.tick == 0 && evt.noteId == kShortId) {
      hasShortOn = true;
    }
    if (evt.tick == 64 && evt.noteId == kKeptId) {
      hasKeptOn = true;
    }
  }
  TEST_ASSERT_FALSE(hasShortOn);
  TEST_ASSERT_TRUE(hasKeptOn);
}

void test_note_on_occupy_collects_every_open_identity() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  seedLongSourceNote(loop, 1, 50, 200, 60);
  loop.beginCapture(CapturePhase::Overdub, 0);
  TEST_ASSERT_TRUE(loop.hasOverdubSourceView());

  ActiveNoteLedger ledger;
  ledger.noteOn(1, 60, 1, 50, 100);
  ledger.noteOn(1, 60, 7, 80, 90);
  OverlapNoteIdSet occupyIds;
  loop.collectOverdubNoteOnParticipantIds(60, 1, ledger, occupyIds);
  TEST_ASSERT_EQUAL_UINT32(2u, static_cast<uint32_t>(occupyIds.size()));
  TEST_ASSERT_TRUE(occupyIds.contains(7));
  TEST_ASSERT_TRUE(occupyIds.contains(1));

  ledger.noteOff(1, 60);
  loop.collectOverdubNoteOnParticipantIds(60, 1, ledger, occupyIds);
  TEST_ASSERT_EQUAL_UINT32(1u, static_cast<uint32_t>(occupyIds.size()));
  TEST_ASSERT_TRUE(occupyIds.contains(1));
  TEST_ASSERT_FALSE(occupyIds.contains(7));

  loop.collectOverdubNoteOnParticipantIds(72, 1, ledger, occupyIds);
  TEST_ASSERT_EQUAL_UINT32(0u, static_cast<uint32_t>(occupyIds.size()));
}

void test_capture_off_does_not_clear_committed_occupy() {
  // session_20260818_221334 / 224719 n=0 a=1: capture Off in WithCapture gather
  // last-writes occupy’s ledger. Playback mergedMidiEvents must use committed gather.
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  constexpr uint32_t kLoopLenTicks = Config::TICKS_PER_BAR;
  constexpr uint32_t kOn = 384;
  constexpr uint32_t kOff = 480;
  constexpr uint32_t kCaptureOff = 400;
  constexpr uint32_t kOccupy = 420;
  constexpr uint8_t kPitch = 23;
  constexpr NoteId kCommittedId = 23;
  seedLongSourceNote(loop, kCommittedId, kOn, kOff, kPitch, kLoopLenTicks);
  loop.beginCapture(CapturePhase::Overdub, 0);
  TEST_ASSERT_TRUE(loop.hasOverdubSourceView());
  loop.rebuildOverdubSourceView(kOccupy);
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(kCaptureOff, 1, kPitch, 0)));

  OverlapNoteIdSet sourceViewIds;
  loop.collectOverdubSourceHoldParticipantIds(kOccupy, kPitch, sourceViewIds);
  TEST_ASSERT_EQUAL_UINT32(1u, static_cast<uint32_t>(sourceViewIds.size()));
  TEST_ASSERT_TRUE(sourceViewIds.contains(kCommittedId));

  MidiEventVec withCapture;
  loop.gatherCommittedEventsWithCapture(withCapture);
  ActiveNoteLedger foldedCapture;
  applyGatheredThroughTick(foldedCapture, withCapture, kOccupy);
  OverlapNoteIdSet clearedOccupy;
  loop.collectOverdubNoteOnParticipantIds(kPitch, 1, foldedCapture, clearedOccupy);
  TEST_ASSERT_EQUAL_UINT32(0u, static_cast<uint32_t>(clearedOccupy.size()));

  MidiEventVec committedOnly;
  loop.gatherCommittedEventsForDerivedView(committedOnly);
  ActiveNoteLedger ledger;
  applyGatheredThroughTick(ledger, committedOnly, kOccupy);
  OverlapNoteIdSet occupyIds;
  loop.collectOverdubNoteOnParticipantIds(kPitch, 1, ledger, occupyIds);
  TEST_ASSERT_EQUAL_UINT32(1u, static_cast<uint32_t>(occupyIds.size()));
  TEST_ASSERT_TRUE(occupyIds.contains(kCommittedId));
}

void test_capture_on_does_not_occupy_empty_source_view() {
  // session_20260818_221334 / 224719 n=1 a=0: capture On in WithCapture gather
  // occupies when source-view has no pitch. Committed gather must not.
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  constexpr uint32_t kLoopLenTicks = Config::TICKS_PER_BAR;
  constexpr uint32_t kOn = 624;
  constexpr uint32_t kOff = 720;
  constexpr uint32_t kOccupy = 45;
  constexpr uint8_t kPitch = 12;
  constexpr NoteId kCommittedId = 12;
  constexpr NoteId kCaptureId = 99;
  seedLongSourceNote(loop, kCommittedId, kOn, kOff, kPitch, kLoopLenTicks);
  loop.beginCapture(CapturePhase::Overdub, 0);
  TEST_ASSERT_TRUE(loop.hasOverdubSourceView());
  loop.rebuildOverdubSourceView(kOccupy);
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(noteOnWithNoteId(kOccupy, 1, kPitch, 90, kCaptureId)));

  OverlapNoteIdSet sourceViewIds;
  loop.collectOverdubSourceHoldParticipantIds(kOccupy, kPitch, sourceViewIds);
  TEST_ASSERT_EQUAL_UINT32(0u, static_cast<uint32_t>(sourceViewIds.size()));

  MidiEventVec withCapture;
  loop.gatherCommittedEventsWithCapture(withCapture);
  ActiveNoteLedger leftoverLedger;
  applyGatheredThroughTick(leftoverLedger, withCapture, kOccupy);
  OverlapNoteIdSet leftoverOccupy;
  loop.collectOverdubNoteOnParticipantIds(kPitch, 1, leftoverLedger, leftoverOccupy);
  TEST_ASSERT_EQUAL_UINT32(1u, static_cast<uint32_t>(leftoverOccupy.size()));
  TEST_ASSERT_TRUE(leftoverOccupy.contains(kCaptureId));

  MidiEventVec committedOnly;
  loop.gatherCommittedEventsForDerivedView(committedOnly);
  ActiveNoteLedger ledger;
  applyGatheredThroughTick(ledger, committedOnly, kOccupy);
  OverlapNoteIdSet occupyIds;
  loop.collectOverdubNoteOnParticipantIds(kPitch, 1, ledger, occupyIds);
  TEST_ASSERT_EQUAL_UINT32(0u, static_cast<uint32_t>(occupyIds.size()));
}

void test_occupy_ledger_catchup_on_at_192_after_last_tick_184() {
  // session_20260818_231038 L5241: lastTick 184, On@192, occupy 192. USB reads
  // ledger before clock (prev, current] applies the On.
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  constexpr uint32_t kLoopLenTicks = Config::TICKS_PER_BAR;
  constexpr uint32_t kLastTick = 184;
  constexpr uint32_t kOn = 192;
  constexpr uint32_t kOff = 280;
  constexpr uint32_t kOccupy = 192;
  constexpr uint8_t kPitch = 12;
  constexpr NoteId kCommittedId = 12;
  seedLongSourceNote(loop, kCommittedId, kOn, kOff, kPitch, kLoopLenTicks);
  loop.beginCapture(CapturePhase::Overdub, 0);
  TEST_ASSERT_TRUE(loop.hasOverdubSourceView());
  loop.rebuildOverdubSourceView(kOccupy);
  loop.lastTickInLoop = kLastTick;
  loop.nextEventIndex = 7;

  OverlapNoteIdSet sourceViewIds;
  loop.collectOverdubSourceHoldParticipantIds(kOccupy, kPitch, sourceViewIds);
  TEST_ASSERT_EQUAL_UINT32(1u, static_cast<uint32_t>(sourceViewIds.size()));
  TEST_ASSERT_TRUE(sourceViewIds.contains(kCommittedId));

  MidiEventVec committedOnly;
  loop.gatherCommittedEventsForDerivedView(committedOnly);
  ActiveNoteLedger behindClock;
  applyGatheredThroughTick(behindClock, committedOnly, kLastTick);
  OverlapNoteIdSet behindIds;
  loop.collectOverdubNoteOnParticipantIds(kPitch, 1, behindClock, behindIds);
  TEST_ASSERT_EQUAL_UINT32(0u, static_cast<uint32_t>(behindIds.size()));

  TEST_ASSERT_TRUE(CommittedPlaybackLedgerCatchUp::shouldApply(loop, kOccupy));
  CommittedPlaybackLedgerCatchUp::applyOpenClosedInterval(behindClock, 1, committedOnly, kLastTick,
                                                          kOccupy, kLoopLenTicks);
  OverlapNoteIdSet occupyIds;
  loop.collectOverdubNoteOnParticipantIds(kPitch, 1, behindClock, occupyIds);
  TEST_ASSERT_EQUAL_UINT32(1u, static_cast<uint32_t>(occupyIds.size()));
  TEST_ASSERT_TRUE(occupyIds.contains(kCommittedId));
  TEST_ASSERT_EQUAL_UINT32(kLastTick, loop.lastTickInLoop);
  TEST_ASSERT_EQUAL_UINT16(7, loop.nextEventIndex);
}

void test_occupy_ledger_catchup_exclusive_when_occupy_equals_last_tick() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  constexpr uint32_t kLoopLenTicks = Config::TICKS_PER_BAR;
  constexpr uint32_t kOn = 192;
  constexpr uint32_t kOff = 280;
  constexpr uint32_t kOccupy = 192;
  constexpr uint8_t kPitch = 12;
  constexpr NoteId kCommittedId = 12;
  seedLongSourceNote(loop, kCommittedId, kOn, kOff, kPitch, kLoopLenTicks);
  loop.beginCapture(CapturePhase::Overdub, 0);
  loop.lastTickInLoop = kOccupy;
  loop.nextEventIndex = 7;

  MidiEventVec committedOnly;
  loop.gatherCommittedEventsForDerivedView(committedOnly);
  TEST_ASSERT_FALSE(CommittedPlaybackLedgerCatchUp::shouldApply(loop, kOccupy));
  ActiveNoteLedger ledger;
  CommittedPlaybackLedgerCatchUp::applyOpenClosedInterval(ledger, 1, committedOnly, kOccupy, kOccupy,
                                                          kLoopLenTicks);
  OverlapNoteIdSet occupyIds;
  loop.collectOverdubNoteOnParticipantIds(kPitch, 1, ledger, occupyIds);
  TEST_ASSERT_EQUAL_UINT32(0u, static_cast<uint32_t>(occupyIds.size()));
  TEST_ASSERT_EQUAL_UINT32(kOccupy, loop.lastTickInLoop);
  TEST_ASSERT_EQUAL_UINT16(7, loop.nextEventIndex);
}

void test_occupy_ledger_catchup_same_tick_off_before_on_replaces() {
  // session_20260818_233247 L2387: occupy 240 includes Off@240 of 192–240 and
  // On@240 of 240–288. Merged vector can list On then Off at the same tick.
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  constexpr uint32_t kLoopLenTicks = Config::TICKS_PER_BAR;
  constexpr uint32_t kLastTick = 232;
  constexpr uint32_t kOccupy = 240;
  constexpr uint8_t kPitch = 12;
  constexpr NoteId kOldId = 100;
  constexpr NoteId kNewId = 200;
  loop.loopLengthTicks = kLoopLenTicks;
  loop.lastTickInLoop = kLastTick;
  loop.nextEventIndex = 7;

  MidiEvent oldOn = MidiEvent::NoteOn(192, 1, kPitch, 100);
  oldOn.noteId = kOldId;
  MidiEvent oldOff = MidiEvent::NoteOff(240, 1, kPitch, 0);
  MidiEvent newOn = MidiEvent::NoteOn(240, 1, kPitch, 100);
  newOn.noteId = kNewId;
  MidiEvent newOff = MidiEvent::NoteOff(288, 1, kPitch, 0);
  MidiEventVec events;
  events.push_back(oldOn);
  events.push_back(newOn);
  events.push_back(oldOff);
  events.push_back(newOff);

  ActiveNoteLedger ledger;
  applyGatheredThroughTick(ledger, events, kLastTick);
  TEST_ASSERT_EQUAL_UINT32(kOldId, ledger.noteId(1, kPitch));

  TEST_ASSERT_TRUE(CommittedPlaybackLedgerCatchUp::shouldApply(loop, kOccupy));
  CommittedPlaybackLedgerCatchUp::applyOpenClosedInterval(ledger, 1, events, kLastTick, kOccupy,
                                                          kLoopLenTicks);
  OverlapNoteIdSet occupyIds;
  loop.collectOverdubNoteOnParticipantIds(kPitch, 1, ledger, occupyIds);
  TEST_ASSERT_EQUAL_UINT32(1u, static_cast<uint32_t>(occupyIds.size()));
  TEST_ASSERT_TRUE(occupyIds.contains(kNewId));
  TEST_ASSERT_FALSE(occupyIds.contains(kOldId));
  TEST_ASSERT_EQUAL_UINT32(kNewId, ledger.noteId(1, kPitch));
  TEST_ASSERT_EQUAL_UINT32(kLastTick, loop.lastTickInLoop);
  TEST_ASSERT_EQUAL_UINT16(7, loop.nextEventIndex);
}

void test_occupy_ledger_catchup_closes_ons_started_in_same_interval_095902() {
  // session_20260819_095902 pitch 12 hs=336 n=3 a=1: covering 328-432 (5242);
  // ended 240-287 (5218) and 288-327 (5224). mmevt lists On@328 before Off@327.
  // Global two-pass applies Off@287/@327 first (orphan) then leaves all three Ons.
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  constexpr uint32_t kLoopLenTicks = Config::TICKS_PER_BAR;
  constexpr uint32_t kLastTick = 232;
  constexpr uint32_t kOccupy = 336;
  constexpr uint8_t kPitch = 12;
  constexpr NoteId kEndedFirst = 5218;
  constexpr NoteId kEndedSecond = 5224;
  constexpr NoteId kCovering = 5242;
  loop.loopLengthTicks = kLoopLenTicks;
  loop.lastTickInLoop = kLastTick;
  loop.nextEventIndex = 7;

  MidiEvent on240 = MidiEvent::NoteOn(240, 1, kPitch, 100);
  on240.noteId = kEndedFirst;
  MidiEvent off287 = MidiEvent::NoteOff(287, 1, kPitch, 0);
  MidiEvent on288 = MidiEvent::NoteOn(288, 1, kPitch, 100);
  on288.noteId = kEndedSecond;
  MidiEvent on328 = MidiEvent::NoteOn(328, 1, kPitch, 100);
  on328.noteId = kCovering;
  MidiEvent off327 = MidiEvent::NoteOff(327, 1, kPitch, 0);
  MidiEvent off432 = MidiEvent::NoteOff(432, 1, kPitch, 0);
  MidiEvent on480 = MidiEvent::NoteOn(480, 1, kPitch, 100);
  on480.noteId = 5226;
  MidiEventVec events;
  events.push_back(on240);
  events.push_back(off287);
  events.push_back(on288);
  events.push_back(on328);
  events.push_back(off327);
  events.push_back(off432);
  events.push_back(on480);

  ActiveNoteLedger ledger;
  TEST_ASSERT_TRUE(CommittedPlaybackLedgerCatchUp::shouldApply(loop, kOccupy));
  CommittedPlaybackLedgerCatchUp::applyOpenClosedInterval(ledger, 1, events, kLastTick, kOccupy,
                                                          kLoopLenTicks);
  OverlapNoteIdSet occupyIds;
  loop.collectOverdubNoteOnParticipantIds(kPitch, 1, ledger, occupyIds);
  TEST_ASSERT_EQUAL_UINT32(1u, static_cast<uint32_t>(occupyIds.size()));
  TEST_ASSERT_TRUE(occupyIds.contains(kCovering));
  TEST_ASSERT_FALSE(occupyIds.contains(kEndedFirst));
  TEST_ASSERT_FALSE(occupyIds.contains(kEndedSecond));
  TEST_ASSERT_EQUAL_UINT32(kCovering, ledger.noteId(1, kPitch));
  TEST_ASSERT_EQUAL_UINT32(kLastTick, loop.lastTickInLoop);
  TEST_ASSERT_EQUAL_UINT16(7, loop.nextEventIndex);
}

void test_occupy_ledger_catchup_then_clock_replay_keeps_one_covering_095902() {
  // Catch-up does not advance lastTick; clock walks the same (lastTick, occupy].
  // Duplicate On of 5242 must not stack a second Entry (101319 led > n).
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  constexpr uint32_t kLoopLenTicks = Config::TICKS_PER_BAR;
  constexpr uint32_t kLastTick = 232;
  constexpr uint32_t kOccupy = 336;
  constexpr uint8_t kPitch = 12;
  constexpr NoteId kEndedFirst = 5218;
  constexpr NoteId kEndedSecond = 5224;
  constexpr NoteId kCovering = 5242;
  loop.loopLengthTicks = kLoopLenTicks;
  loop.lastTickInLoop = kLastTick;
  loop.nextEventIndex = 7;

  MidiEvent on240 = MidiEvent::NoteOn(240, 1, kPitch, 100);
  on240.noteId = kEndedFirst;
  MidiEvent off287 = MidiEvent::NoteOff(287, 1, kPitch, 0);
  MidiEvent on288 = MidiEvent::NoteOn(288, 1, kPitch, 100);
  on288.noteId = kEndedSecond;
  MidiEvent off327 = MidiEvent::NoteOff(327, 1, kPitch, 0);
  MidiEvent on328 = MidiEvent::NoteOn(328, 1, kPitch, 100);
  on328.noteId = kCovering;
  MidiEventVec events;
  events.push_back(on240);
  events.push_back(on288);
  events.push_back(on328);
  events.push_back(off287);
  events.push_back(off327);

  ActiveNoteLedger ledger;
  CommittedPlaybackLedgerCatchUp::applyOpenClosedInterval(ledger, 1, events, kLastTick, kOccupy,
                                                          kLoopLenTicks);
  const MidiEvent clockOrder[] = {on240, off287, on288, off327, on328};
  for (const MidiEvent& evt : clockOrder) {
    TEST_ASSERT_TRUE(ledger.applyPlaybackEvent(1, evt));
  }
  OverlapNoteIdSet occupyIds;
  loop.collectOverdubNoteOnParticipantIds(kPitch, 1, ledger, occupyIds);
  TEST_ASSERT_EQUAL_UINT32(1u, static_cast<uint32_t>(occupyIds.size()));
  TEST_ASSERT_TRUE(occupyIds.contains(kCovering));
  TEST_ASSERT_FALSE(occupyIds.contains(kEndedFirst));
  TEST_ASSERT_FALSE(occupyIds.contains(kEndedSecond));
  uint8_t seen = 0;
  ledger.forEachActive([&](uint8_t channel, uint8_t note, const ActiveNoteLedger::Entry&) {
    if (channel == 1 && note == kPitch) {
      ++seen;
    }
  });
  TEST_ASSERT_EQUAL_UINT8(1, seen);
  TEST_ASSERT_EQUAL_UINT32(kLastTick, loop.lastTickInLoop);
  TEST_ASSERT_EQUAL_UINT16(7, loop.nextEventIndex);
}

void test_occupy_ledger_catchup_same_tick_skip_when_occupy_equals_last_tick() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  constexpr uint32_t kLoopLenTicks = Config::TICKS_PER_BAR;
  constexpr uint32_t kLastTick = 240;
  constexpr uint32_t kOccupy = 240;
  constexpr uint8_t kPitch = 12;
  constexpr NoteId kOldId = 100;
  constexpr NoteId kNewId = 200;
  loop.loopLengthTicks = kLoopLenTicks;
  loop.lastTickInLoop = kLastTick;
  loop.nextEventIndex = 7;

  MidiEvent oldOn = MidiEvent::NoteOn(192, 1, kPitch, 100);
  oldOn.noteId = kOldId;
  MidiEvent oldOff = MidiEvent::NoteOff(240, 1, kPitch, 0);
  MidiEvent newOn = MidiEvent::NoteOn(240, 1, kPitch, 100);
  newOn.noteId = kNewId;
  MidiEvent newOff = MidiEvent::NoteOff(288, 1, kPitch, 0);
  MidiEventVec events;
  events.push_back(oldOn);
  events.push_back(newOn);
  events.push_back(oldOff);
  events.push_back(newOff);

  TEST_ASSERT_FALSE(CommittedPlaybackLedgerCatchUp::shouldApply(loop, kOccupy));
  ActiveNoteLedger ledger;
  CommittedPlaybackLedgerCatchUp::applyOpenClosedInterval(ledger, 1, events, kLastTick, kOccupy,
                                                          kLoopLenTicks);
  OverlapNoteIdSet occupyIds;
  loop.collectOverdubNoteOnParticipantIds(kPitch, 1, ledger, occupyIds);
  TEST_ASSERT_EQUAL_UINT32(0u, static_cast<uint32_t>(occupyIds.size()));
  TEST_ASSERT_EQUAL_UINT32(kInvalidNoteId, ledger.noteId(1, kPitch));
  TEST_ASSERT_EQUAL_UINT32(kLastTick, loop.lastTickInLoop);
  TEST_ASSERT_EQUAL_UINT16(7, loop.nextEventIndex);
}

void test_occupy_ledger_catchup_skips_wrap_crossing() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  constexpr uint32_t kLoopLenTicks = Config::TICKS_PER_BAR;
  constexpr uint32_t kLastTick = 760;
  constexpr uint32_t kOccupy = 10;
  constexpr uint8_t kPitch = 12;
  constexpr NoteId kOnAtZeroId = 12;
  seedLongSourceNote(loop, kOnAtZeroId, 0, 200, kPitch, kLoopLenTicks);
  loop.openOverdubSession(0);
  loop.armOverdubWrapAfterLeavingStart(1);
  loop.beginCapture(CapturePhase::Overdub, 0);
  loop.lastTickInLoop = kLastTick;
  loop.nextEventIndex = 7;
  TEST_ASSERT_TRUE(loop.shouldCommitOverdubWrap(kLastTick, kOccupy));
  TEST_ASSERT_FALSE(CommittedPlaybackLedgerCatchUp::shouldApply(loop, kOccupy));

  MidiEventVec committedOnly;
  loop.gatherCommittedEventsForDerivedView(committedOnly);
  ActiveNoteLedger ledger;
  OverlapNoteIdSet occupyIds;
  loop.collectOverdubNoteOnParticipantIds(kPitch, 1, ledger, occupyIds);
  TEST_ASSERT_EQUAL_UINT32(0u, static_cast<uint32_t>(occupyIds.size()));
  TEST_ASSERT_TRUE(loop.captureActive());
  TEST_ASSERT_EQUAL_UINT32(kLastTick, loop.lastTickInLoop);
  TEST_ASSERT_EQUAL_UINT16(7, loop.nextEventIndex);
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
  RUN_TEST(test_note_on_occupy_reads_ledger_note_id);
  RUN_TEST(test_note_on_occupy_empty_when_ledger_inactive);
  RUN_TEST(test_wrap_committed_note_at_s_occupies_ledger_same_tick);
  RUN_TEST(test_wrap_pass_events_at_s_occupy_without_merged_rebuild);
  RUN_TEST(test_wrap_pass_events_at_loop_head_occupy_without_merged_rebuild);
  RUN_TEST(test_wrap_pass_on_at_96_occupies_after_s_interval);
  RUN_TEST(test_wrap_pass_spanning_on_at_0_occupies_at_96);
  RUN_TEST(test_overdub_stop_still_removes_pairs_shorter_than_min_length);
  RUN_TEST(test_note_on_occupy_collects_every_open_identity);
  RUN_TEST(test_capture_off_does_not_clear_committed_occupy);
  RUN_TEST(test_capture_on_does_not_occupy_empty_source_view);
  RUN_TEST(test_occupy_ledger_catchup_on_at_192_after_last_tick_184);
  RUN_TEST(test_occupy_ledger_catchup_exclusive_when_occupy_equals_last_tick);
  RUN_TEST(test_occupy_ledger_catchup_same_tick_off_before_on_replaces);
  RUN_TEST(test_occupy_ledger_catchup_closes_ons_started_in_same_interval_095902);
  RUN_TEST(test_occupy_ledger_catchup_then_clock_replay_keeps_one_covering_095902);
  RUN_TEST(test_occupy_ledger_catchup_same_tick_skip_when_occupy_equals_last_tick);
  RUN_TEST(test_occupy_ledger_catchup_skips_wrap_crossing);
  return UNITY_END();
}
