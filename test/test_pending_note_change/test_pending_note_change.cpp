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
#define LOOP_PENDING_NOTE_CHANGE_PROVIDES_DISPLAY_APPLY
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
#include "Utils/DisplayWindowUtils.h"
#include "Utils/NoteUtils.h"
#include "Utils/PlaybackCursorAdvance.h"
#include "Utils/SnapshotNoteConversion.h"
#include "VisualCache.h"
#include "PlaybackMergedMidiEvents.h"
#include "GlobalUndoStack.h"

#include "../../src/Utils/PlaybackCursorAdvance.cpp"
#include "../../src/Utils/CommittedPlaybackLedgerCatchUp.cpp"

#include <algorithm>
#include <initializer_list>
#include <map>
#include <utility>

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

NoteUtils::DisplayNote testSourceSpan(NoteId id, uint8_t pitch, uint32_t startTick, uint32_t endTick) {
  NoteUtils::DisplayNote note{};
  note.noteId = id;
  note.note = pitch;
  note.velocity = 100;
  note.startTick = startTick;
  note.endTick = endTick;
  return note;
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

const NoteUtils::DisplayNote* findDisplayNoteById(const NoteUtils::DisplayNoteVec& notes,
                                                  NoteId noteId) {
  for (const NoteUtils::DisplayNote& note : notes) {
    if (note.noteId == noteId) {
      return &note;
    }
  }
  return nullptr;
}

const EditPass* findEditPassByTarget(const Loop& loop, NoteId targetNoteId) {
  for (const EditPass& row : loop.passes.editPasses) {
    if (row.targetNoteId == targetNoteId) {
      return &row;
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

uint32_t countNoteOnWithId(const SessionMidiEventVec& events, NoteId id) {
  uint32_t count = 0;
  for (const MidiEvent& evt : events) {
    if (evt.isNoteOn() && evt.noteId == id) {
      ++count;
    }
  }
  return count;
}

uint32_t noteOffTickForId(const SessionMidiEventVec& events, NoteId id, uint8_t pitch) {
  uint32_t onTick = UINT32_MAX;
  for (const MidiEvent& evt : events) {
    if (evt.isNoteOn() && evt.noteId == id) {
      onTick = evt.tick;
      continue;
    }
    if (onTick != UINT32_MAX && evt.isNoteOff() && evt.data.noteData.note == pitch &&
        evt.tick >= onTick) {
      return evt.tick;
    }
  }
  return UINT32_MAX;
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
  TEST_ASSERT_EQUAL_UINT32(0, loop.overlapHoldTotals().emptySets);
  TEST_ASSERT_EQUAL_UINT32(1, loop.overlapHoldTotals().lookedUp);
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
  TEST_ASSERT_EQUAL_UINT32(0, loop.overlapHoldTotals().emptySets);
  TEST_ASSERT_EQUAL_UINT32(1, loop.overlapHoldTotals().lookedUp);
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

// Consume attribution: occupy id present, but no source-view note exists at lookup
// time. Only the long-loop JIT branch can reach that candidate.
void test_consume_attribution_counts_late_note_for_jit_ahead_candidate() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  constexpr uint32_t kLongLoop = Config::TICKS_PER_BAR * 64;
  seedLongSourceNote(loop, 1, 224, 288, 60, kLongLoop);
  loop.beginCapture(CapturePhase::Overdub, kLongLoop - 80);
  TEST_ASSERT_FALSE(hasDisplayNote(loop.overdubSourceViewNotes(), 60, 224));
  TEST_ASSERT_TRUE(loop.loopLengthTicks >
                   Loop::kOverdubSourceWindowBars * Config::TICKS_PER_BAR);

  TEST_ASSERT_TRUE(loop.accumulatePendingNoteChangesForIncomingNote(1, 60, 90, 64, 240, 10,
                                                                   overlapIds({1})));

  TEST_ASSERT_EQUAL_UINT32(1, loop.overlapHoldTotals().lookedUp);
  TEST_ASSERT_EQUAL_UINT32(0, loop.overlapHoldTotals().emptySets);
  TEST_ASSERT_EQUAL_UINT32(0, loop.overlapHoldTotals().lateNoteCandidates);
  TEST_ASSERT_EQUAL_UINT32(0, loop.overlapHoldTotals().scanOnlyCandidates);
  TEST_ASSERT_EQUAL_UINT32(0, loop.overlapHoldTotals().idsWithoutNotes);
  TEST_ASSERT_NOT_NULL(findTransform(loop.pendingNoteChanges(), 1));
}

// Consume attribution: incoming hold covers a second note that starts after S and
// is absent from the occupy set. Geometric id completion selects it without window scan.
void test_consume_attribution_counts_scan_only_for_id_absent_candidate() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  loop.loopLengthTicks = kLoopLen;
  LoopEventStore store;
  TEST_ASSERT_TRUE(storeAppendNoteOn(store, 50, 1, 60, 100, 1));
  TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOff(200, 1, 60, 0)));
  TEST_ASSERT_TRUE(storeAppendNoteOn(store, 300, 1, 60, 100, 2));
  TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOff(400, 1, 60, 0)));
  loop.seedRecordPassFromStore(store);
  loop.nextNoteId_ = 3;
  loop.beginCapture(CapturePhase::Overdub);
  TEST_ASSERT_TRUE(loop.loopLengthTicks <=
                   Loop::kOverdubSourceWindowBars * Config::TICKS_PER_BAR);
  TEST_ASSERT_TRUE(hasDisplayNote(loop.overdubSourceViewNotes(), 60, 50));
  TEST_ASSERT_TRUE(hasDisplayNote(loop.overdubSourceViewNotes(), 60, 300));

  TEST_ASSERT_TRUE(loop.accumulatePendingNoteChangesForIncomingNote(1, 60, 90, 100, 350, 10,
                                                                   overlapIds({1})));

  TEST_ASSERT_EQUAL_UINT32(0, loop.overlapHoldTotals().scanOnlyCandidates);
  TEST_ASSERT_EQUAL_UINT32(0, loop.overlapHoldTotals().lateNoteCandidates);
  TEST_ASSERT_EQUAL_UINT32(0, loop.overlapHoldTotals().idsWithoutNotes);
  TEST_ASSERT_NOT_NULL(findTransform(loop.pendingNoteChanges(), 1));
  TEST_ASSERT_NOT_NULL(findTransform(loop.pendingNoteChanges(), 2));
}

// Ledger id with no source-view row at lookup is repaired by hold fill before id lookup.
void test_consume_id_resolution_norow_repaired_by_hold_fill() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  constexpr uint32_t kLongLoop = Config::TICKS_PER_BAR * 64;
  seedLongSourceNote(loop, 1, 224, 288, 60, kLongLoop);
  loop.beginCapture(CapturePhase::Overdub, kLongLoop - 80);
  TEST_ASSERT_FALSE(hasDisplayNote(loop.overdubSourceViewNotes(), 60, 224));

  TEST_ASSERT_TRUE(loop.accumulatePendingNoteChangesForIncomingNote(1, 60, 90, 64, 240, 10,
                                                                   overlapIds({1})));
  TEST_ASSERT_TRUE(hasDisplayNote(loop.overdubSourceViewNotes(), 60, 224));
  TEST_ASSERT_EQUAL_UINT32(0, loop.overlapHoldTotals().idsWithoutNotes);
  TEST_ASSERT_EQUAL_UINT32(0, loop.overlapHoldTotals().scanOnlyCandidates);
  TEST_ASSERT_NOT_NULL(findTransform(loop.pendingNoteChanges(), 1));
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

void test_pending_hide_same_start_shorter_hides_longer() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  seedLongSourceNote(loop, 1, 64, 288, 60);
  loop.beginCapture(CapturePhase::Overdub);

  TEST_ASSERT_TRUE(loop.accumulatePendingNoteChangesForIncomingNote(1, 60, 90, 64, 160, 10,
                                                                   overlapIds({1})));
  TEST_ASSERT_EQUAL(1, countKind(loop.pendingNoteChanges(), PendingNoteChangeKind::Add));
  TEST_ASSERT_EQUAL(1, countKind(loop.pendingNoteChanges(), PendingNoteChangeKind::Shorten));
  TEST_ASSERT_EQUAL(0, countKind(loop.pendingNoteChanges(), PendingNoteChangeKind::Hide));
  const PendingNoteChange* shorten = findTransform(loop.pendingNoteChanges(), 1);
  TEST_ASSERT_NOT_NULL(shorten);
  TEST_ASSERT_EQUAL(static_cast<int>(PendingNoteChangeKind::Shorten),
                    static_cast<int>(shorten->kind));
  TEST_ASSERT_EQUAL_UINT32(64u, shorten->startTick);
  TEST_ASSERT_EQUAL_UINT32(159u, shorten->endTick);
}

void test_occupy_shorter_note_off_seals_length_not_delete() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  seedLongSourceNote(loop, 1, 64, 288, 60);
  loop.openOverdubSession(0);
  loop.beginCapture(CapturePhase::Overdub);

  TEST_ASSERT_TRUE(loop.accumulatePendingNoteChangesForIncomingNote(1, 60, 90, 64, 160, 10,
                                                                   overlapIds({1})));
  TEST_ASSERT_EQUAL(1, countKind(loop.pendingNoteChanges(), PendingNoteChangeKind::Shorten));
  TEST_ASSERT_EQUAL(0, countKind(loop.pendingNoteChanges(), PendingNoteChangeKind::Hide));

  const EditPassIdList companions = loop.sealPendingNoteChangesToEditPasses();
  TEST_ASSERT_EQUAL(1u, companions.size());
  const EditPass* companion = findEditPassByTarget(loop, 1);
  TEST_ASSERT_NOT_NULL(companion);
  TEST_ASSERT_EQUAL(static_cast<int>(EditActionType::Update),
                    static_cast<int>(companion->actionType));
  TEST_ASSERT_EQUAL(static_cast<int>(EditPropertyType::Length),
                    static_cast<int>(companion->propertyType));
  TEST_ASSERT_EQUAL_UINT32(64u, companion->startTick);
  TEST_ASSERT_EQUAL_UINT32(159u, companion->endTick);

  const NoteUtils::DisplayNote* remnant = findDisplayNoteById(loop.overdubSourceViewNotes(), 1);
  TEST_ASSERT_NOT_NULL(remnant);
  TEST_ASSERT_EQUAL_UINT32(64u, remnant->startTick);
  TEST_ASSERT_EQUAL_UINT32(159u, remnant->endTick);
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

/// RC-D3: Hide/Shorten apply to a frame-local copy. The reusable committed
/// prefix stays un-hidden so a second apply from that prefix is idempotent.
void test_pending_hide_does_not_mutate_copied_committed_prefix() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  seedLongSourceNote(loop, 1, 64, 240, 60);
  loop.beginCapture(CapturePhase::Overdub);

  TEST_ASSERT_TRUE(loop.accumulatePendingNoteChangesForIncomingNote(1, 60, 90, 64, 288, 10,
                                                                   overlapIds({1})));
  NoteUtils::DisplayNoteVec storedPrefix = loop.overdubSourceViewNotes();
  TEST_ASSERT_TRUE(hasDisplayNote(storedPrefix, 60, 64));

  NoteUtils::DisplayNoteVec frame1 = storedPrefix;
  loop.applyPendingNoteChangesToDisplayNotes(frame1);
  TEST_ASSERT_FALSE(hasDisplayNote(frame1, 60, 64));
  TEST_ASSERT_TRUE(hasDisplayNote(storedPrefix, 60, 64));

  NoteUtils::DisplayNoteVec frame2 = storedPrefix;
  loop.applyPendingNoteChangesToDisplayNotes(frame2);
  TEST_ASSERT_FALSE(hasDisplayNote(frame2, 60, 64));
  TEST_ASSERT_TRUE(hasDisplayNote(storedPrefix, 60, 64));
}

void test_resolve_occupancy_at_note_on_same_start_hides() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  seedLongSourceNote(loop, 1, 0, 576, 60);
  loop.beginCapture(CapturePhase::Overdub);

  OverlapNoteIdSet resolved;
  loop.resolveOccupancyAtNoteOn(1, 60, 90, overlapIds({1}), 0, &resolved);
  TEST_ASSERT_EQUAL(0, countKind(loop.pendingNoteChanges(), PendingNoteChangeKind::Add));
  TEST_ASSERT_EQUAL(0, countKind(loop.pendingNoteChanges(), PendingNoteChangeKind::Shorten));
  TEST_ASSERT_EQUAL(1, countKind(loop.pendingNoteChanges(), PendingNoteChangeKind::Hide));
  TEST_ASSERT_TRUE(resolved.contains(1));
  const PendingNoteChange* hide = findTransform(loop.pendingNoteChanges(), 1);
  TEST_ASSERT_NOT_NULL(hide);
  TEST_ASSERT_EQUAL_UINT32(0u, hide->startTick);
  TEST_ASSERT_EQUAL_UINT32(576u, hide->endTick);
}

void test_pending_hide_applies_when_display_note_id_differs() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  seedLongSourceNote(loop, 1, 0, 576, 60);
  loop.beginCapture(CapturePhase::Overdub);
  loop.resolveOccupancyAtNoteOn(1, 60, 90, overlapIds({1}), 0);

  NoteUtils::DisplayNoteVec paint;
  NoteUtils::DisplayNote row{};
  row.noteId = 73;
  row.note = 60;
  row.velocity = 100;
  row.startTick = 0;
  row.endTick = 576;
  paint.push_back(row);
  loop.applyPendingNoteChangesToDisplayNotes(paint);
  TEST_ASSERT_TRUE(paint.empty());
}

void test_pending_hide_keeps_live_capture_tail() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  seedLongSourceNote(loop, 1, 0, 576, 60);
  loop.beginCapture(CapturePhase::Overdub);
  loop.resolveOccupancyAtNoteOn(1, 60, 90, overlapIds({1}), 0);

  NoteUtils::DisplayNoteVec paint;
  NoteUtils::DisplayNote committed{};
  committed.noteId = 73;
  committed.note = 60;
  committed.velocity = 100;
  committed.startTick = 0;
  committed.endTick = 576;
  paint.push_back(committed);
  NoteUtils::DisplayNote tail{};
  tail.noteId = kInvalidNoteId;
  tail.note = 60;
  tail.velocity = 90;
  tail.startTick = 0;
  tail.endTick = 48;
  paint.push_back(tail);
  loop.applyPendingNoteChangesToDisplayNotes(paint, 1);
  TEST_ASSERT_EQUAL(1u, paint.size());
  TEST_ASSERT_EQUAL_UINT8(60, paint[0].note);
  TEST_ASSERT_EQUAL_UINT32(0u, paint[0].startTick);
  TEST_ASSERT_EQUAL_UINT32(48u, paint[0].endTick);
}

void test_upsert_occupied_hides_empty_ledger_uses_hold_participants() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  seedLongSourceNote(loop, 73, 912, 1296, 60);
  loop.beginCapture(CapturePhase::Overdub);

  loop.resolveOccupancyAtNoteOn(1, 60, 90, overlapIds({}), 912);
  TEST_ASSERT_EQUAL(1, countKind(loop.pendingNoteChanges(), PendingNoteChangeKind::Hide));
  const PendingNoteChange* hide = findTransform(loop.pendingNoteChanges(), 73);
  TEST_ASSERT_NOT_NULL(hide);
  TEST_ASSERT_EQUAL_UINT32(912u, hide->startTick);
  TEST_ASSERT_EQUAL_UINT32(1296u, hide->endTick);
}

void test_upsert_occupied_hides_norowid_falls_back_to_hold_participants() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  seedLongSourceNote(loop, 73, 0, 576, 60);
  loop.beginCapture(CapturePhase::Overdub);

  loop.resolveOccupancyAtNoteOn(1, 60, 90, overlapIds({77}), 0);
  TEST_ASSERT_EQUAL(1, countKind(loop.pendingNoteChanges(), PendingNoteChangeKind::Hide));
  const PendingNoteChange* hide = findTransform(loop.pendingNoteChanges(), 73);
  TEST_ASSERT_NOT_NULL(hide);
  TEST_ASSERT_EQUAL_UINT32(0u, hide->startTick);
  TEST_ASSERT_EQUAL_UINT32(576u, hide->endTick);
}

void test_occupancy_ledger_id_not_present_at_hold_does_not_transform_134436() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  constexpr uint32_t kFourBar = Config::TICKS_PER_BAR * 4;
  seedLongSourceNote(loop, 1, 2948, 107, 52, kFourBar);
  loop.beginCapture(CapturePhase::Overdub);

  OverlapNoteIdSet resolved;
  loop.resolveOccupancyAtNoteOn(1, 52, 90, overlapIds({1}), 263, &resolved);
  TEST_ASSERT_EQUAL(0, countKind(loop.pendingNoteChanges(), PendingNoteChangeKind::Shorten));
  TEST_ASSERT_EQUAL(0, countKind(loop.pendingNoteChanges(), PendingNoteChangeKind::Hide));
  TEST_ASSERT_FALSE(resolved.contains(1));
}

void test_recount_overlap_hold_totals_follows_pending_after_seal() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  seedLongSourceNote(loop, 1, 0, 576, 60);
  loop.beginCapture(CapturePhase::Overdub);
  loop.resolveOccupancyAtNoteOn(1, 60, 90, overlapIds({1}), 192);
  TEST_ASSERT_EQUAL_UINT32(1, loop.overlapHoldTotals().shorten);
  TEST_ASSERT_EQUAL(1, countKind(loop.pendingNoteChanges(), PendingNoteChangeKind::Shorten));
  (void)loop.sealPendingNoteChangesToEditPasses();
  TEST_ASSERT_EQUAL_UINT32(1, loop.overlapHoldTotals().shorten);
  loop.recountOverlapHoldTotalsFromPending();
  TEST_ASSERT_EQUAL_UINT32(0, loop.overlapHoldTotals().shorten);
  TEST_ASSERT_EQUAL_UINT32(0, loop.overlapHoldTotals().hide);
  TEST_ASSERT_EQUAL_UINT32(0, loop.overlapHoldTotals().add);
}

void test_occupancy_hide_seals_as_delete() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  seedLongSourceNote(loop, 1, 0, 576, 60);
  loop.beginCapture(CapturePhase::Overdub);
  loop.resolveOccupancyAtNoteOn(1, 60, 90, overlapIds({1}), 0);

  const EditPassIdList sealed = loop.sealPendingNoteChangesToEditPasses();
  TEST_ASSERT_EQUAL(1u, sealed.size());
  TEST_ASSERT_EQUAL(1u, loop.passes.editPasses.size());
  TEST_ASSERT_EQUAL(0, countKind(loop.pendingNoteChanges(), PendingNoteChangeKind::Hide));
  TEST_ASSERT_FALSE(loop.hasPendingNoteChanges());
  const EditPass* row = findEditPassByTarget(loop, 1);
  TEST_ASSERT_NOT_NULL(row);
  TEST_ASSERT_EQUAL(static_cast<int>(EditActionType::Delete), static_cast<int>(row->actionType));
}

uint32_t countDisplayNotesAtPitchStart(const NoteUtils::DisplayNoteVec& notes, uint8_t pitch,
                                       uint32_t startTick) {
  uint32_t count = 0;
  for (const NoteUtils::DisplayNote& note : notes) {
    if (note.note == pitch && note.startTick == startTick) {
      ++count;
    }
  }
  return count;
}

void seedFiveSiblingPitch64At768(Loop& loop) {
  constexpr uint32_t kFourBars = Config::TICKS_PER_BAR * 4;
  loop.loopLengthTicks = kFourBars;
  LoopEventStore store;
  const uint32_t ends[5] = {1344, 1152, 1056, 960, 864};
  for (NoteId id = 1; id <= 5; ++id) {
    TEST_ASSERT_TRUE(storeAppendNoteOn(store, 768, 1, 64, 100, id));
    TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOff(ends[id - 1], 1, 64, 0)));
  }
  loop.seedRecordPassFromStore(store);
  loop.nextNoteId_ = 6;
}

void test_occupancy_five_sibling_convergence() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  seedFiveSiblingPitch64At768(loop);
  loop.openOverdubSession(0);
  loop.beginCapture(CapturePhase::Overdub);

  OverlapNoteIdSet resolved;
  loop.resolveOccupancyAtNoteOn(1, 64, 90, overlapIds({1, 2, 3, 4, 5}), 768, &resolved);
  TEST_ASSERT_EQUAL_UINT32(5u, static_cast<uint32_t>(resolved.size()));
  TEST_ASSERT_EQUAL(5, countKind(loop.pendingNoteChanges(), PendingNoteChangeKind::Hide));
  TEST_ASSERT_EQUAL(0, countKind(loop.pendingNoteChanges(), PendingNoteChangeKind::Shorten));

  TEST_ASSERT_TRUE(loop.accumulatePendingNoteChangesForIncomingNote(1, 64, 90, 768, 912, 10,
                                                                   overlapIds({1, 2, 3, 4, 5}),
                                                                   &resolved));
  TEST_ASSERT_EQUAL(1, countKind(loop.pendingNoteChanges(), PendingNoteChangeKind::Add));
  TEST_ASSERT_EQUAL(5, countKind(loop.pendingNoteChanges(), PendingNoteChangeKind::Hide));
  TEST_ASSERT_EQUAL(0, countKind(loop.pendingNoteChanges(), PendingNoteChangeKind::Shorten));

  SessionMidiEventVec firstFlat;
  loop.gatherCommittedEvents(firstFlat);
  NoteUtils::DisplayNoteVec firstNotes =
      NoteUtils::reconstructDisplayNotes(firstFlat, loop.loopLengthTicks, false);
  loop.applyPendingNoteChangesToDisplayNotes(firstNotes);
  TEST_ASSERT_EQUAL_UINT32(0u, countDisplayNotesAtPitchStart(firstNotes, 64, 768));

  SessionMidiEventVec repeatFlat;
  loop.gatherCommittedEvents(repeatFlat);
  NoteUtils::DisplayNoteVec repeatNotes =
      NoteUtils::reconstructDisplayNotes(repeatFlat, loop.loopLengthTicks, false);
  loop.applyPendingNoteChangesToDisplayNotes(repeatNotes);
  TEST_ASSERT_EQUAL_UINT32(0u, countDisplayNotesAtPitchStart(repeatNotes, 64, 768));

  loop.applyPendingNoteChangesToOverdubSourceView();
  TEST_ASSERT_EQUAL_UINT32(1u, countDisplayNotesAtPitchStart(loop.overdubSourceViewNotes(), 64, 768));
  const NoteUtils::DisplayNote* added = findDisplayNoteById(loop.overdubSourceViewNotes(), 10);
  TEST_ASSERT_NOT_NULL(added);
  TEST_ASSERT_EQUAL_UINT32(768u, added->startTick);
  TEST_ASSERT_EQUAL_UINT32(912u, added->endTick);

  loop.rebuildOverdubSourceView(768, "test");
  loop.applyPendingNoteChangesToOverdubSourceView();
  TEST_ASSERT_EQUAL_UINT32(1u, countDisplayNotesAtPitchStart(loop.overdubSourceViewNotes(), 64, 768));

  TEST_ASSERT_TRUE(loop.appendCaptureEvent(noteOnWithNoteId(768, 1, 64, 90, 10)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(912, 1, 64, 0)));
  TEST_ASSERT_EQUAL(CommitResult::Committed, loop.commitCapturePass(CommitReason::OverdubStop, 0));
  const PassId passId = loop.lastCommittedPassId();
  const EditPassIdList sealed = loop.sealPendingNoteChangesToEditPasses();
  TEST_ASSERT_EQUAL(5u, sealed.size());
  loop.pushOverdubSessionPass(passId, sealed);

  SessionMidiEventVec sealedFlat;
  loop.gatherCommittedEvents(sealedFlat);
  const NoteUtils::DisplayNoteVec sealedNotes =
      NoteUtils::reconstructDisplayNotes(sealedFlat, loop.loopLengthTicks, false);
  TEST_ASSERT_EQUAL_UINT32(1u, countDisplayNotesAtPitchStart(sealedNotes, 64, 768));

  loop.rebuildOverdubSourceView(768, "test-sealed");
  TEST_ASSERT_EQUAL_UINT32(1u, countDisplayNotesAtPitchStart(loop.overdubSourceViewNotes(), 64, 768));
}

void test_occupancy_ordering_equivalence() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop first;
  seedLongSourceNote(first, 1, 0, 576, 60);
  first.beginCapture(CapturePhase::Overdub);
  OverlapNoteIdSet resolvedFirst;
  first.resolveOccupancyAtNoteOn(1, 60, 90, overlapIds({1}), 192, &resolvedFirst);
  TEST_ASSERT_TRUE(first.accumulatePendingNoteChangesForIncomingNote(1, 60, 90, 192, 288, 10,
                                                                    overlapIds({1}), &resolvedFirst));

  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop second;
  seedLongSourceNote(second, 1, 0, 576, 60);
  second.beginCapture(CapturePhase::Overdub);
  OverlapNoteIdSet resolvedSecond;
  second.resolveOccupancyAtNoteOn(1, 60, 90, overlapIds({1}), 192, &resolvedSecond);
  TEST_ASSERT_TRUE(second.accumulatePendingNoteChangesForIncomingNote(
      1, 60, 90, 192, 288, 10, overlapIds({1}), &resolvedSecond));

  TEST_ASSERT_EQUAL(countKind(first.pendingNoteChanges(), PendingNoteChangeKind::Add),
                    countKind(second.pendingNoteChanges(), PendingNoteChangeKind::Add));
  TEST_ASSERT_EQUAL(countKind(first.pendingNoteChanges(), PendingNoteChangeKind::Shorten),
                    countKind(second.pendingNoteChanges(), PendingNoteChangeKind::Shorten));
  TEST_ASSERT_EQUAL(countKind(first.pendingNoteChanges(), PendingNoteChangeKind::Hide),
                    countKind(second.pendingNoteChanges(), PendingNoteChangeKind::Hide));
  const PendingNoteChange* firstA = findTransform(first.pendingNoteChanges(), 1);
  const PendingNoteChange* secondA = findTransform(second.pendingNoteChanges(), 1);
  TEST_ASSERT_NOT_NULL(firstA);
  TEST_ASSERT_NOT_NULL(secondA);
  TEST_ASSERT_EQUAL(static_cast<int>(firstA->kind), static_cast<int>(secondA->kind));
  TEST_ASSERT_EQUAL_UINT32(firstA->endTick, secondA->endTick);
  TEST_ASSERT_EQUAL(static_cast<int>(PendingNoteChangeKind::Shorten),
                    static_cast<int>(firstA->kind));
  TEST_ASSERT_EQUAL_UINT32(191u, firstA->endTick);
}

void test_occupancy_three_note_nesting() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  seedLongSourceNote(loop, 1, 0, 2000, 60);
  loop.beginCapture(CapturePhase::Overdub);

  OverlapNoteIdSet resolvedB;
  loop.resolveOccupancyAtNoteOn(1, 60, 90, overlapIds({1}), 800, &resolvedB);
  TEST_ASSERT_TRUE(loop.accumulatePendingNoteChangesForIncomingNote(1, 60, 90, 800, 1200, 10,
                                                                   overlapIds({1}), &resolvedB));
  loop.applyPendingNoteChangesToOverdubSourceView();

  OverlapNoteIdSet resolvedC;
  loop.resolveOccupancyAtNoteOn(1, 60, 90, overlapIds({1, 10}), 900, &resolvedC);
  const PendingNoteChange* a = findTransform(loop.pendingNoteChanges(), 1);
  TEST_ASSERT_NOT_NULL(a);
  TEST_ASSERT_EQUAL(static_cast<int>(PendingNoteChangeKind::Shorten), static_cast<int>(a->kind));
  TEST_ASSERT_EQUAL_UINT32(799u, a->endTick);
  const PendingNoteChange* b = findTransform(loop.pendingNoteChanges(), 10);
  TEST_ASSERT_NOT_NULL(b);
  TEST_ASSERT_EQUAL(static_cast<int>(PendingNoteChangeKind::Shorten), static_cast<int>(b->kind));
  TEST_ASSERT_EQUAL_UINT32(899u, b->endTick);
}

void test_occupancy_composition_hide_absorbs_shorten() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  seedLongSourceNote(loop, 1, 768, 1920, 64);
  loop.beginCapture(CapturePhase::Overdub);
  loop.resolveOccupancyAtNoteOn(1, 64, 90, overlapIds({1}), 768);
  TEST_ASSERT_EQUAL(1, countKind(loop.pendingNoteChanges(), PendingNoteChangeKind::Hide));
  loop.resolveOccupancyAtNoteOn(1, 64, 90, overlapIds({1}), 900);
  TEST_ASSERT_EQUAL(1, countKind(loop.pendingNoteChanges(), PendingNoteChangeKind::Hide));
  TEST_ASSERT_EQUAL(0, countKind(loop.pendingNoteChanges(), PendingNoteChangeKind::Shorten));
}

void test_occupancy_composition_shorten_keeps_earlier_end() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  seedLongSourceNote(loop, 1, 760, 1920, 64);
  loop.beginCapture(CapturePhase::Overdub);
  loop.resolveOccupancyAtNoteOn(1, 64, 90, overlapIds({1}), 800);
  const PendingNoteChange* first = findTransform(loop.pendingNoteChanges(), 1);
  TEST_ASSERT_NOT_NULL(first);
  TEST_ASSERT_EQUAL(static_cast<int>(PendingNoteChangeKind::Shorten), static_cast<int>(first->kind));
  TEST_ASSERT_EQUAL_UINT32(799u, first->endTick);
  loop.resolveOccupancyAtNoteOn(1, 64, 90, overlapIds({1}), 900);
  const PendingNoteChange* second = findTransform(loop.pendingNoteChanges(), 1);
  TEST_ASSERT_NOT_NULL(second);
  TEST_ASSERT_EQUAL(static_cast<int>(PendingNoteChangeKind::Shorten), static_cast<int>(second->kind));
  TEST_ASSERT_EQUAL_UINT32(799u, second->endTick);
  TEST_ASSERT_EQUAL(1, countKind(loop.pendingNoteChanges(), PendingNoteChangeKind::Shorten));
}

void test_occupancy_composition_shorten_below_min_length_hides() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  seedLongSourceNote(loop, 1, 760, 1920, 64);
  loop.beginCapture(CapturePhase::Overdub);
  TEST_ASSERT_TRUE(noteMinLengthRemoveEnabled);
  TEST_ASSERT_EQUAL_UINT32(8u, noteMinLengthTicks);
  loop.resolveOccupancyAtNoteOn(1, 64, 90, overlapIds({1}), 764);
  TEST_ASSERT_EQUAL(1, countKind(loop.pendingNoteChanges(), PendingNoteChangeKind::Hide));
  TEST_ASSERT_EQUAL(0, countKind(loop.pendingNoteChanges(), PendingNoteChangeKind::Shorten));
}

void test_occupancy_wrap_split_keeps_most_restrictive() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  seedLongSourceNote(loop, 1, 0, 4099, 60, 4100);
  loop.beginCapture(CapturePhase::Overdub);
  TEST_ASSERT_TRUE(loop.accumulatePendingNoteChangesForIncomingNote(1, 60, 90, 4000, 200, 10,
                                                                   overlapIds({1})));
  TEST_ASSERT_EQUAL(1, countKind(loop.pendingNoteChanges(), PendingNoteChangeKind::Shorten));
  TEST_ASSERT_EQUAL(0, countKind(loop.pendingNoteChanges(), PendingNoteChangeKind::Hide));
  const PendingNoteChange* transform = findTransform(loop.pendingNoteChanges(), 1);
  TEST_ASSERT_NOT_NULL(transform);
  TEST_ASSERT_EQUAL_UINT32(0u, transform->startTick);
  TEST_ASSERT_EQUAL_UINT32(199u, transform->endTick);
}

void test_occupancy_add_then_shorten_composes_in_source_view() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  loop.loopLengthTicks = kLoopLen;
  loop.beginCapture(CapturePhase::Overdub);
  TEST_ASSERT_TRUE(loop.accumulatePendingNoteChangesForIncomingNote(1, 60, 90, 64, 240, 10,
                                                                   overlapIds({})));
  loop.applyPendingNoteChangesToOverdubSourceView();
  OverlapNoteIdSet resolvedC;
  loop.resolveOccupancyAtNoteOn(1, 60, 90, overlapIds({10}), 160, &resolvedC);
  loop.applyPendingNoteChangesToOverdubSourceView();
  const NoteUtils::DisplayNote* composed = findDisplayNoteById(loop.overdubSourceViewNotes(), 10);
  TEST_ASSERT_NOT_NULL(composed);
  TEST_ASSERT_EQUAL_UINT32(64u, composed->startTick);
  TEST_ASSERT_EQUAL_UINT32(159u, composed->endTick);
}

void test_retrigger_pending_note_slot_replaces_first() {
  // Pins Track::noteOn single-slot pendingNotes[{note, channel}] replacement.
  // Stage A must not add a second slot or merge occupancyResolvedNoteIds across retriggers.
  struct Slot {
    uint32_t startNoteTick = 0;
    OverlapNoteIdSet occupancyResolvedNoteIds;
  };
  std::map<std::pair<uint8_t, uint8_t>, Slot> pendingNotes;
  Slot first;
  first.startNoteTick = 100;
  first.occupancyResolvedNoteIds = overlapIds({1});
  pendingNotes[{60, 1}] = first;
  Slot second;
  second.startNoteTick = 200;
  pendingNotes[{60, 1}] = second;
  TEST_ASSERT_EQUAL(1u, pendingNotes.size());
  const Slot& replaced = pendingNotes[{60, 1}];
  TEST_ASSERT_EQUAL_UINT32(200u, replaced.startNoteTick);
  TEST_ASSERT_EQUAL_UINT32(0u, static_cast<uint32_t>(replaced.occupancyResolvedNoteIds.size()));
}

void test_occupancy_undo_restores_original_geometry() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  seedLongSourceNote(loop, 1, 64, 1920, 64);
  loop.openOverdubSession(0);
  loop.beginCapture(CapturePhase::Overdub);
  loop.resolveOccupancyAtNoteOn(1, 64, 90, overlapIds({1}), 768);
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(noteOnWithNoteId(768, 1, 64, 90, 10)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(912, 1, 64, 0)));
  TEST_ASSERT_EQUAL(CommitResult::Committed, loop.commitCapturePass(CommitReason::OverdubStop, 0));
  const PassId passId = loop.lastCommittedPassId();
  const EditPassIdList sealed = loop.sealPendingNoteChangesToEditPasses();
  TEST_ASSERT_EQUAL(1u, sealed.size());
  loop.pushOverdubSessionPass(passId, sealed);

  SessionMidiEventVec occupied;
  loop.gatherCommittedEvents(occupied);
  const NoteUtils::DisplayNoteVec occupiedNotes =
      NoteUtils::reconstructDisplayNotes(occupied, loop.loopLengthTicks, false);
  const NoteUtils::DisplayNote* shortened = findDisplayNoteById(occupiedNotes, 1);
  TEST_ASSERT_TRUE(shortened == nullptr || shortened->endTick == 767);

  TEST_ASSERT_TRUE(loop.undoOverdubSession());
  SessionMidiEventVec restored;
  loop.gatherCommittedEvents(restored);
  const NoteUtils::DisplayNoteVec restoredNotes =
      NoteUtils::reconstructDisplayNotes(restored, loop.loopLengthTicks, false);
  const NoteUtils::DisplayNote* original = findDisplayNoteById(restoredNotes, 1);
  TEST_ASSERT_NOT_NULL(original);
  TEST_ASSERT_EQUAL_UINT32(64u, original->startTick);
  TEST_ASSERT_EQUAL_UINT32(1920u, original->endTick);
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
  TEST_ASSERT_EQUAL(0, countKind(loop.pendingNoteChanges(), PendingNoteChangeKind::Hide));
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
  TEST_ASSERT_EQUAL(0, countKind(loop.pendingNoteChanges(), PendingNoteChangeKind::Hide));
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
  TEST_ASSERT_EQUAL(1, countKind(loop.pendingNoteChanges(), PendingNoteChangeKind::Shorten));
  TEST_ASSERT_EQUAL(0, countKind(loop.pendingNoteChanges(), PendingNoteChangeKind::Hide));
  const PendingNoteChange* shorten = findTransform(loop.pendingNoteChanges(), 1);
  TEST_ASSERT_NOT_NULL(shorten);
  TEST_ASSERT_EQUAL(static_cast<int>(PendingNoteChangeKind::Shorten),
                    static_cast<int>(shorten->kind));
  TEST_ASSERT_EQUAL_UINT32(0u, shorten->startTick);
  TEST_ASSERT_EQUAL_UINT32(199u, shorten->endTick);
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
  TEST_ASSERT_EQUAL(1, countKind(loop.pendingNoteChanges(), PendingNoteChangeKind::Shorten));
  TEST_ASSERT_EQUAL(0, countKind(loop.pendingNoteChanges(), PendingNoteChangeKind::Hide));
  const PendingNoteChange* shorten = findTransform(loop.pendingNoteChanges(), 1);
  TEST_ASSERT_NOT_NULL(shorten);
  TEST_ASSERT_EQUAL(static_cast<int>(PendingNoteChangeKind::Shorten),
                    static_cast<int>(shorten->kind));
  TEST_ASSERT_EQUAL_UINT32(0u, shorten->startTick);
  TEST_ASSERT_EQUAL_UINT32(199u, shorten->endTick);
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

// Pin 121141 three-point proof: resolver Shorten(6079,167) → seal → rebuild source view.
void test_121141_three_point_resolver_seal_rebuild_handoff() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  constexpr uint32_t kOneBar = Config::TICKS_PER_BAR;
  constexpr uint8_t kPitch = 24;
  constexpr NoteId kIdA = 6079;
  constexpr NoteId kIdB = 6073;

  Loop loop;
  loop.loopLengthTicks = kOneBar;
  loop.nextNoteId_ = kIdB;
  LoopEventStore store;
  TEST_ASSERT_TRUE(storeAppendNoteOn(store, 144, 1, kPitch, 100, kIdA));
  TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOff(360, 1, kPitch, 0)));
  loop.seedRecordPassFromStore(store);

  loop.beginCapture(CapturePhase::Overdub, 0);
  Loop::resetCommittedPitchQueryWork();
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(noteOnWithNoteId(168, 1, kPitch, 100, kIdB)));
  TEST_ASSERT_TRUE(loop.accumulatePendingNoteChangesForIncomingNote(1, kPitch, 100, 168, 264, kIdB,
                                                                   overlapIds({kIdA})));

  const PendingNoteChange* shortenA = findTransform(loop.pendingNoteChanges(), kIdA);
  TEST_ASSERT_NOT_NULL(shortenA);
  TEST_ASSERT_EQUAL(static_cast<int>(PendingNoteChangeKind::Shorten),
                    static_cast<int>(shortenA->kind));
  TEST_ASSERT_EQUAL_UINT32(kIdA, shortenA->noteId);
  TEST_ASSERT_EQUAL_UINT32(167u, shortenA->endTick);

  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(264, 1, kPitch, 0)));
  TEST_ASSERT_EQUAL(CommitResult::Committed,
                    loop.commitCapturePass(CommitReason::OverdubWrap, 0));

  const EditPassIdList companionIds = loop.sealPendingNoteChangesToEditPasses();
  TEST_ASSERT_EQUAL(1u, companionIds.size());
  TEST_ASSERT_FALSE(loop.hasPendingNoteChanges());
  const EditPass* sealedA = findEditPassByTarget(loop, kIdA);
  TEST_ASSERT_NOT_NULL(sealedA);
  TEST_ASSERT_EQUAL(static_cast<int>(EditActionType::Update),
                    static_cast<int>(sealedA->actionType));
  TEST_ASSERT_EQUAL(static_cast<int>(EditPropertyType::Length),
                    static_cast<int>(sealedA->propertyType));
  TEST_ASSERT_EQUAL_UINT32(167u, sealedA->endTick);

  loop.rebuildOverdubSourceView(0);
  TEST_ASSERT_TRUE(loop.hasOverdubSourceView());
  const NoteUtils::DisplayNote* viewA = findDisplayNoteById(loop.overdubSourceViewNotes(), kIdA);
  const NoteUtils::DisplayNote* viewB = findDisplayNoteById(loop.overdubSourceViewNotes(), kIdB);
  TEST_ASSERT_NOT_NULL(viewA);
  TEST_ASSERT_NOT_NULL(viewB);
  TEST_ASSERT_EQUAL_UINT8(kPitch, viewA->note);
  TEST_ASSERT_EQUAL_UINT32(144u, viewA->startTick);
  TEST_ASSERT_EQUAL_UINT32(167u, viewA->endTick);
  TEST_ASSERT_EQUAL_UINT8(kPitch, viewB->note);
  TEST_ASSERT_EQUAL_UINT32(168u, viewB->startTick);
  TEST_ASSERT_EQUAL_UINT32(264u, viewB->endTick);
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
  TEST_ASSERT_EQUAL_UINT32(50u, loop.passes.editPasses[0].startTick);
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

void test_wrap_tick_occupy_without_full_rebuild_and_no_duplicate_after_later_rebuild() {
  // Wrap tick: incremental source-view append + lastCommittedPassId ledger apply.
  // Does not call ensurePlaybackMergedMidiEventsBuilt or rebuildOverdubSourceView.
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  constexpr uint32_t kLoopLenTicks = Config::TICKS_PER_BAR;
  constexpr uint32_t kS = 416;
  constexpr uint32_t kPrev = 408;
  constexpr uint32_t kOff = 480;
  constexpr uint8_t kPitch = 60;
  constexpr NoteId kSourceId = 1;
  constexpr NoteId kWrapNoteId = 9;
  seedLongSourceNote(loop, kSourceId, 48, 72, 72, kLoopLenTicks);
  loop.openOverdubSession(kS);
  loop.establishOverdubSourceView(kS);
  TEST_ASSERT_TRUE(loop.hasOverdubSourceView());
  TEST_ASSERT_NOT_NULL(findDisplayNoteById(loop.overdubSourceViewNotes(), kSourceId));
  TEST_ASSERT_NULL(findDisplayNoteById(loop.overdubSourceViewNotes(), kWrapNoteId));

  loop.beginCapture(CapturePhase::Overdub, kS);
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(noteOnWithNoteId(kS, 1, kPitch, 90, kWrapNoteId)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(kOff, 1, kPitch, 0)));
  TEST_ASSERT_EQUAL(CommitResult::Committed, loop.commitCapturePass(CommitReason::OverdubWrap, kS));
  loop.appendLastCommittedOverdubPassToSourceView();

  uint32_t wrapNoteCount = 0;
  for (const NoteUtils::DisplayNote& note : loop.overdubSourceViewNotes()) {
    if (note.noteId == kWrapNoteId) {
      ++wrapNoteCount;
    }
  }
  TEST_ASSERT_EQUAL_UINT32(1u, wrapNoteCount);
  const NoteUtils::DisplayNote* wrapNote =
      findDisplayNoteById(loop.overdubSourceViewNotes(), kWrapNoteId);
  TEST_ASSERT_NOT_NULL(wrapNote);
  TEST_ASSERT_EQUAL_UINT8(kPitch, wrapNote->note);
  TEST_ASSERT_EQUAL_UINT32(kS, wrapNote->startTick);

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

  OverlapNoteIdSet holdIds;
  loop.collectOverdubSourceHoldParticipantIds(kS, kPitch, holdIds);
  TEST_ASSERT_EQUAL_UINT32(1u, static_cast<uint32_t>(holdIds.size()));
  TEST_ASSERT_TRUE(holdIds.contains(kWrapNoteId));

  loop.rebuildOverdubSourceView(kS);
  wrapNoteCount = 0;
  for (const NoteUtils::DisplayNote& note : loop.overdubSourceViewNotes()) {
    if (note.noteId == kWrapNoteId) {
      ++wrapNoteCount;
    }
  }
  TEST_ASSERT_EQUAL_UINT32(1u, wrapNoteCount);
}

void test_incremental_overdub_seal_admits_wrap_pass_after_s_without_full_gather() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  constexpr uint32_t kLoopLenTicks = Config::TICKS_PER_BAR;
  constexpr uint32_t kS = 416;
  constexpr uint32_t kAfterS = 480;
  constexpr uint8_t kPitch = 60;
  constexpr NoteId kSourceId = 1;
  constexpr NoteId kWrapNoteId = 9;
  seedLongSourceNote(loop, kSourceId, 48, 72, 72, kLoopLenTicks);
  PlaybackMergedMidiEvents merged;
  loop.gatherCommittedEventsForDerivedView(merged.mergedEvents);
  merged.builtFromRevision = loop.playbackRevision;
  merged.windowStartTick = 0;
  merged.windowLengthTicks = loop.loopLengthTicks;
  TEST_ASSERT_EQUAL_UINT32(1u, countNoteOnWithId(merged.mergedEvents, kSourceId));
  TEST_ASSERT_EQUAL_UINT32(0u, countNoteOnWithId(merged.mergedEvents, kWrapNoteId));
  const uint32_t priorRevision = loop.playbackRevision;

  loop.openOverdubSession(kS);
  loop.establishOverdubSourceView(kS);
  loop.beginCapture(CapturePhase::Overdub, kS);
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(noteOnWithNoteId(kS, 1, kPitch, 90, kWrapNoteId)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(kAfterS, 1, kPitch, 0)));
  TEST_ASSERT_EQUAL(CommitResult::Committed, loop.commitCapturePass(CommitReason::OverdubWrap, kS));
  const EditPassIdList companionIds = loop.sealPendingNoteChangesToEditPasses();
  loop.recordLastOverdubSeal(loop.lastCommittedPassId(), companionIds, priorRevision);
  TEST_ASSERT_TRUE(loop.hasLastOverdubSealForTest());
  TEST_ASSERT_EQUAL_UINT32(0u, countNoteOnWithId(merged.mergedEvents, kWrapNoteId));

  TEST_ASSERT_TRUE(loop.tryAdmitLastOverdubSealIntoMergedEvents(merged));
  TEST_ASSERT_FALSE(loop.hasLastOverdubSealForTest());
  TEST_ASSERT_EQUAL_UINT32(loop.playbackRevision, merged.builtFromRevision);
  TEST_ASSERT_EQUAL_UINT32(1u, countNoteOnWithId(merged.mergedEvents, kWrapNoteId));
  bool foundAfterS = false;
  for (const MidiEvent& evt : merged.mergedEvents) {
    if (evt.isNoteOff() && evt.data.noteData.note == kPitch && evt.tick == kAfterS) {
      foundAfterS = true;
      break;
    }
  }
  TEST_ASSERT_TRUE(foundAfterS);

  SessionMidiEventVec rebuilt;
  loop.gatherCommittedEventsForDerivedView(rebuilt);
  TEST_ASSERT_EQUAL_UINT32(1u, countNoteOnWithId(rebuilt, kWrapNoteId));
  TEST_ASSERT_EQUAL_UINT32(1u, countNoteOnWithId(merged.mergedEvents, kWrapNoteId));
}

void test_incremental_overdub_seal_applies_companion_shorten() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  constexpr uint32_t kLoopLenTicks = Config::TICKS_PER_BAR;
  constexpr NoteId kSourceId = 1;
  constexpr NoteId kOverdubId = 10;
  seedLongSourceNote(loop, kSourceId, 50, 200, 60, kLoopLenTicks);
  PlaybackMergedMidiEvents merged;
  loop.gatherCommittedEventsForDerivedView(merged.mergedEvents);
  merged.builtFromRevision = loop.playbackRevision;
  merged.windowStartTick = 0;
  merged.windowLengthTicks = loop.loopLengthTicks;
  TEST_ASSERT_EQUAL_UINT32(200u, noteOffTickForId(merged.mergedEvents, kSourceId, 60));
  const uint32_t priorRevision = loop.playbackRevision;

  loop.openOverdubSession(0);
  loop.establishOverdubSourceView(0);
  loop.beginCapture(CapturePhase::Overdub, 0);
  TEST_ASSERT_TRUE(loop.accumulatePendingNoteChangesForIncomingNote(1, 60, 90, 120, 160, kOverdubId,
                                                                   overlapIds({kSourceId})));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(noteOnWithNoteId(120, 1, 60, 90, kOverdubId)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(160, 1, 60, 0)));
  TEST_ASSERT_EQUAL(CommitResult::Committed, loop.commitCapturePass(CommitReason::OverdubStop, 0));
  const EditPassIdList companionIds = loop.sealPendingNoteChangesToEditPasses();
  TEST_ASSERT_EQUAL(1u, companionIds.size());
  loop.recordLastOverdubSeal(loop.lastCommittedPassId(), companionIds, priorRevision);

  TEST_ASSERT_TRUE(loop.tryAdmitLastOverdubSealIntoMergedEvents(merged));
  TEST_ASSERT_EQUAL_UINT32(1u, countNoteOnWithId(merged.mergedEvents, kOverdubId));
  TEST_ASSERT_EQUAL_UINT32(1u, countNoteOnWithId(merged.mergedEvents, kSourceId));
  TEST_ASSERT_EQUAL_UINT32(119u, noteOffTickForId(merged.mergedEvents, kSourceId, 60));
}

void test_overdub_stop_reanchor_preserves_merged_then_admits_seal() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  constexpr uint32_t kLoopLenTicks = Config::TICKS_PER_BAR;
  constexpr NoteId kSourceId = 1;
  constexpr NoteId kStopNoteId = 11;
  seedLongSourceNote(loop, kSourceId, 48, 200, 60, kLoopLenTicks);
  PlaybackMergedMidiEvents merged;
  loop.gatherCommittedEventsForDerivedView(merged.mergedEvents);
  merged.builtFromRevision = loop.playbackRevision;
  merged.windowStartTick = 0;
  merged.windowLengthTicks = loop.loopLengthTicks;
  const size_t representationSize = merged.mergedEvents.size();
  TEST_ASSERT_EQUAL_UINT32(1u, countNoteOnWithId(merged.mergedEvents, kSourceId));
  const uint32_t priorRevision = loop.playbackRevision;

  loop.openOverdubSession(0);
  loop.establishOverdubSourceView(0);
  loop.beginCapture(CapturePhase::Overdub, 0);
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(noteOnWithNoteId(96, 1, 64, 90, kStopNoteId)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(128, 1, 64, 0)));
  TEST_ASSERT_EQUAL(CommitResult::Committed, loop.commitCapturePass(CommitReason::OverdubStop, 96));
  const EditPassIdList companionIds = loop.sealPendingNoteChangesToEditPasses();
  loop.recordLastOverdubSeal(loop.lastCommittedPassId(), companionIds, priorRevision);

  // KEEP-only reanchor: traversal state moves; the merged representation stays R.
  loop.nextEventIndex = 0;
  loop.lastTickInLoop = 96;
  TEST_ASSERT_EQUAL_UINT32(representationSize, merged.mergedEvents.size());
  TEST_ASSERT_EQUAL_UINT32(1u, countNoteOnWithId(merged.mergedEvents, kSourceId));
  TEST_ASSERT_EQUAL_UINT32(0u, countNoteOnWithId(merged.mergedEvents, kStopNoteId));
  TEST_ASSERT_EQUAL_UINT32(priorRevision, merged.builtFromRevision);

  TEST_ASSERT_TRUE(loop.tryAdmitLastOverdubSealIntoMergedEvents(merged));
  TEST_ASSERT_EQUAL_UINT32(1u, countNoteOnWithId(merged.mergedEvents, kSourceId));
  TEST_ASSERT_EQUAL_UINT32(1u, countNoteOnWithId(merged.mergedEvents, kStopNoteId));
  TEST_ASSERT_EQUAL_UINT32(loop.playbackRevision, merged.builtFromRevision);
}

void test_overdub_stop_shaped_receipt_admits_without_duplication() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  constexpr uint32_t kLoopLenTicks = Config::TICKS_PER_BAR;
  constexpr NoteId kSourceId = 1;
  constexpr NoteId kStopNoteId = 12;
  seedLongSourceNote(loop, kSourceId, 50, 200, 60, kLoopLenTicks);
  PlaybackMergedMidiEvents merged;
  loop.gatherCommittedEventsForDerivedView(merged.mergedEvents);
  merged.builtFromRevision = loop.playbackRevision;
  merged.windowStartTick = 0;
  merged.windowLengthTicks = loop.loopLengthTicks;
  const uint32_t priorRevision = loop.playbackRevision;

  loop.openOverdubSession(0);
  loop.establishOverdubSourceView(0);
  loop.beginCapture(CapturePhase::Overdub, 0);
  TEST_ASSERT_TRUE(loop.accumulatePendingNoteChangesForIncomingNote(1, 60, 90, 120, 160, kStopNoteId,
                                                                   overlapIds({kSourceId})));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(noteOnWithNoteId(120, 1, 60, 90, kStopNoteId)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(160, 1, 60, 0)));
  TEST_ASSERT_EQUAL(CommitResult::Committed, loop.commitCapturePass(CommitReason::OverdubStop, 0));
  const EditPassIdList companionIds = loop.sealPendingNoteChangesToEditPasses();
  TEST_ASSERT_EQUAL(1u, companionIds.size());
  loop.recordLastOverdubSeal(loop.lastCommittedPassId(), companionIds, priorRevision);

  TEST_ASSERT_TRUE(loop.tryAdmitLastOverdubSealIntoMergedEvents(merged));
  TEST_ASSERT_EQUAL_UINT32(1u, countNoteOnWithId(merged.mergedEvents, kSourceId));
  TEST_ASSERT_EQUAL_UINT32(1u, countNoteOnWithId(merged.mergedEvents, kStopNoteId));
  TEST_ASSERT_EQUAL_UINT32(119u, noteOffTickForId(merged.mergedEvents, kSourceId, 60));

  SessionMidiEventVec rebuilt;
  loop.gatherCommittedEventsForDerivedView(rebuilt);
  TEST_ASSERT_EQUAL_UINT32(countNoteOnWithId(rebuilt, kSourceId),
                           countNoteOnWithId(merged.mergedEvents, kSourceId));
  TEST_ASSERT_EQUAL_UINT32(countNoteOnWithId(rebuilt, kStopNoteId),
                           countNoteOnWithId(merged.mergedEvents, kStopNoteId));
}

void test_incremental_overdub_seal_provenance_miss_falls_back() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  constexpr uint32_t kLoopLenTicks = Config::TICKS_PER_BAR;
  constexpr NoteId kSourceId = 1;
  constexpr NoteId kWrapNoteId = 9;
  seedLongSourceNote(loop, kSourceId, 48, 72, 72, kLoopLenTicks);
  PlaybackMergedMidiEvents merged;
  loop.gatherCommittedEventsForDerivedView(merged.mergedEvents);
  merged.builtFromRevision = loop.playbackRevision;
  merged.windowStartTick = 0;
  merged.windowLengthTicks = loop.loopLengthTicks;
  const uint32_t priorRevision = loop.playbackRevision;
  const uint32_t builtRevision = merged.builtFromRevision;

  loop.openOverdubSession(96);
  loop.beginCapture(CapturePhase::Overdub, 96);
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(noteOnWithNoteId(96, 1, 60, 90, kWrapNoteId)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(128, 1, 60, 0)));
  TEST_ASSERT_EQUAL(CommitResult::Committed, loop.commitCapturePass(CommitReason::OverdubWrap, 96));
  const EditPassIdList companionIds = loop.sealPendingNoteChangesToEditPasses();

  TEST_ASSERT_FALSE(loop.tryAdmitLastOverdubSealIntoMergedEvents(merged));
  TEST_ASSERT_EQUAL_UINT32(builtRevision, merged.builtFromRevision);
  TEST_ASSERT_EQUAL_UINT32(0u, countNoteOnWithId(merged.mergedEvents, kWrapNoteId));

  loop.recordLastOverdubSeal(loop.lastCommittedPassId(), companionIds, priorRevision + 1u);
  TEST_ASSERT_TRUE(loop.hasLastOverdubSealForTest());
  TEST_ASSERT_FALSE(loop.tryAdmitLastOverdubSealIntoMergedEvents(merged));
  TEST_ASSERT_FALSE(loop.hasLastOverdubSealForTest());
  TEST_ASSERT_EQUAL_UINT32(builtRevision, merged.builtFromRevision);

  loop.recordLastOverdubSeal(loop.lastCommittedPassId() + 99u, companionIds, priorRevision);
  TEST_ASSERT_TRUE(loop.hasLastOverdubSealForTest());
  TEST_ASSERT_FALSE(loop.tryAdmitLastOverdubSealIntoMergedEvents(merged));
  TEST_ASSERT_FALSE(loop.hasLastOverdubSealForTest());
  TEST_ASSERT_EQUAL_UINT32(builtRevision, merged.builtFromRevision);
  TEST_ASSERT_EQUAL_UINT32(0u, countNoteOnWithId(merged.mergedEvents, kWrapNoteId));

  EditPassIdList missingCompanion;
  missingCompanion.push_back(static_cast<EditPassId>(9999));
  loop.recordLastOverdubSeal(loop.lastCommittedPassId(), missingCompanion, priorRevision);
  TEST_ASSERT_FALSE(loop.tryAdmitLastOverdubSealIntoMergedEvents(merged));
  TEST_ASSERT_FALSE(loop.hasLastOverdubSealForTest());
  TEST_ASSERT_EQUAL_UINT32(0u, countNoteOnWithId(merged.mergedEvents, kWrapNoteId));
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
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(7, 1, kPitch, 0)));
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

void test_occupy_clock_duplicate_off_closes_both_pre_abut_103234() {
  // session_20260819_103234 L6674 pitch 24 hs=120 n=3 a=2: two Off@71, second skipped.
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  constexpr uint32_t kLoopLenTicks = 768;
  constexpr uint8_t kPitch = 24;
  constexpr NoteId kLeftOpen = 5568;
  constexpr NoteId kNewerPreOff = 5570;
  constexpr NoteId kAbut = 5575;
  constexpr NoteId kNested = 5583;
  loop.loopLengthTicks = kLoopLenTicks;
  loop.lastTickInLoop = 0;
  loop.nextEventIndex = 0;

  MidiEvent on24 = MidiEvent::NoteOn(24, 1, kPitch, 100);
  on24.noteId = kLeftOpen;
  MidiEvent on48 = MidiEvent::NoteOn(48, 1, kPitch, 100);
  on48.noteId = kNewerPreOff;
  MidiEvent off71a = MidiEvent::NoteOff(71, 1, kPitch, 0);
  MidiEvent off71b = MidiEvent::NoteOff(71, 1, kPitch, 0);
  MidiEvent on72 = MidiEvent::NoteOn(72, 1, kPitch, 100);
  on72.noteId = kAbut;
  MidiEvent on120 = MidiEvent::NoteOn(120, 1, kPitch, 100);
  on120.noteId = kNested;

  struct DirectPlaybackStreamCtx {
    MidiEventVec events;
  };
  DirectPlaybackStreamCtx streamCtx;
  streamCtx.events.push_back(on24);
  streamCtx.events.push_back(on48);
  streamCtx.events.push_back(off71a);
  streamCtx.events.push_back(off71b);
  streamCtx.events.push_back(on72);
  streamCtx.events.push_back(on120);

  auto streamSize = [](const void* ctx) -> size_t {
    return static_cast<const DirectPlaybackStreamCtx*>(ctx)->events.size();
  };
  auto streamEventAt = [](const void* ctx, uint16_t cursor) -> const MidiEvent& {
    return static_cast<const DirectPlaybackStreamCtx*>(ctx)->events[cursor];
  };
  auto streamPhase = [](const MidiEvent& evt, const ProjectionContext&) -> uint32_t {
    return evt.tick;
  };

  struct LedgerSendLog {
    ActiveNoteLedger ledger;
    unsigned midiOffs = 0;
  };
  LedgerSendLog log;
  auto send = [](void* ctx, const MidiEvent& evt, uint8_t) {
    auto* state = static_cast<LedgerSendLog*>(ctx);
    (void)state->ledger.applyPlaybackEvent(1, evt);
    if (evt.isNoteOff()) {
      ++state->midiOffs;
    }
  };
  auto applyLedgerOnly = [](void* ctx, const MidiEvent& evt, uint8_t) {
    auto* state = static_cast<LedgerSendLog*>(ctx);
    (void)state->ledger.applyPlaybackEvent(1, evt);
  };

  const PlaybackEventStream stream{&streamCtx, streamSize, nullptr, streamEventAt, streamPhase};
  ProjectionContext playbackContext{};
  playbackContext.loopLength = kLoopLenTicks;
  uint16_t cursor = 0;
  PlaybackTickFrame frame{&playbackContext, 120U, 0U, false};
  PlaybackCursorAdvanceState advance{&cursor, nullptr};
  TEST_ASSERT_EQUAL(PlaybackAdvanceResult::Completed,
                    advancePlaybackCursor(advance, frame, PlaybackEmitPolicy::ActiveCommitted, stream,
                                          send, &log, 0, nullptr, nullptr, 1, applyLedgerOnly));
  TEST_ASSERT_EQUAL_UINT32(1, log.midiOffs);

  OverlapNoteIdSet occupyIds;
  loop.collectOverdubNoteOnParticipantIds(kPitch, 1, log.ledger, occupyIds);
  TEST_ASSERT_EQUAL_UINT32(2u, static_cast<uint32_t>(occupyIds.size()));
  TEST_ASSERT_TRUE(occupyIds.contains(kAbut));
  TEST_ASSERT_TRUE(occupyIds.contains(kNested));
  TEST_ASSERT_FALSE(occupyIds.contains(kLeftOpen));
  TEST_ASSERT_FALSE(occupyIds.contains(kNewerPreOff));
}

void test_occupy_leftover_off_at_exclusive_end_closes_wrap_head_not_ghost_104654() {
  // session_20260819_104654 L2668: Off@96 LIFO-closes wrap-head 5772; 5701 stays.
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  constexpr uint32_t kLoopLenTicks = 768;
  constexpr uint8_t kPitch = 12;
  constexpr NoteId kGhost = 5701;
  constexpr NoteId kWrapHead = 5772;
  loop.loopLengthTicks = kLoopLenTicks;

  ActiveNoteLedger ledger;
  ledger.noteOn(1, kPitch, kGhost, 96, 100);
  ledger.noteOn(1, kPitch, kWrapHead, 0, 100);
  MidiEvent off96 = MidiEvent::NoteOff(96, 1, kPitch, 0);
  TEST_ASSERT_TRUE(ledger.applyPlaybackEvent(1, off96));

  OverlapNoteIdSet occupyIds;
  loop.collectOverdubNoteOnParticipantIds(kPitch, 1, ledger, occupyIds);
  TEST_ASSERT_EQUAL_UINT32(1u, static_cast<uint32_t>(occupyIds.size()));
  TEST_ASSERT_TRUE(occupyIds.contains(kGhost));
  TEST_ASSERT_FALSE(occupyIds.contains(kWrapHead));
}

void test_erase_open_notes_missing_from_committed_note_ons_drops_5701_keeps_5772() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  constexpr uint32_t kLoopLenTicks = 768;
  constexpr uint8_t kPitch = 12;
  constexpr NoteId kGhost = 5701;
  constexpr NoteId kWrapHead = 5772;
  loop.loopLengthTicks = kLoopLenTicks;

  ActiveNoteLedger ledger;
  ledger.noteOn(1, kPitch, kGhost, 96, 100);
  ledger.noteOn(1, kPitch, kWrapHead, 0, 100);

  MidiEvent onWrap = MidiEvent::NoteOn(0, 1, kPitch, 100);
  onWrap.noteId = kWrapHead;
  MidiEvent off96 = MidiEvent::NoteOff(96, 1, kPitch, 0);
  MidiEvent committed[2] = {onWrap, off96};
  ledger.eraseOpenNotesMissingFromCommittedNoteOns(committed, 2);

  OverlapNoteIdSet afterErase;
  loop.collectOverdubNoteOnParticipantIds(kPitch, 1, ledger, afterErase);
  TEST_ASSERT_EQUAL_UINT32(1u, static_cast<uint32_t>(afterErase.size()));
  TEST_ASSERT_TRUE(afterErase.contains(kWrapHead));
  TEST_ASSERT_FALSE(afterErase.contains(kGhost));

  TEST_ASSERT_TRUE(ledger.applyPlaybackEvent(1, off96));
  OverlapNoteIdSet afterOff;
  loop.collectOverdubNoteOnParticipantIds(kPitch, 1, ledger, afterOff);
  TEST_ASSERT_EQUAL_UINT32(0u, static_cast<uint32_t>(afterOff.size()));
}

void test_occupy_source_hold_drops_span_without_merged_note_on() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  constexpr uint32_t kLoopLenTicks = 768;
  constexpr uint8_t kPitch = 60;
  constexpr NoteId kIdA = 1;
  constexpr NoteId kIdB = 2;
  loop.loopLengthTicks = kLoopLenTicks;

  NoteUtils::DisplayNoteVec spans;
  spans.push_back(testSourceSpan(kIdA, kPitch, 0, 743));
  spans.push_back(testSourceSpan(kIdB, kPitch, 0, 168));
  loop.replaceOverdubSourceViewNotesForTest(std::move(spans));

  PlaybackMergedMidiEvents merged;
  merged.windowStartTick = 0;
  merged.windowLengthTicks = kLoopLenTicks;
  MidiEvent onB = MidiEvent::NoteOn(744, 1, kPitch, 100);
  onB.noteId = kIdB;
  MidiEvent offB = MidiEvent::NoteOff(168, 1, kPitch, 0);
  merged.mergedEvents.push_back(onB);
  merged.mergedEvents.push_back(offB);
  loop.replaceCommittedPlaybackNoteOnIdentities(merged);

  OverlapNoteIdSet sourceIds;
  loop.collectOverdubSourceHoldParticipantIds(72, kPitch, sourceIds);
  TEST_ASSERT_EQUAL_UINT32(1u, static_cast<uint32_t>(sourceIds.size()));
  TEST_ASSERT_TRUE(sourceIds.contains(kIdB));
  TEST_ASSERT_FALSE(sourceIds.contains(kIdA));
}

void test_occupy_source_hold_retains_span_with_merged_note_on() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  constexpr uint32_t kLoopLenTicks = 768;
  constexpr uint8_t kPitch = 60;
  constexpr NoteId kIdA = 1;
  constexpr NoteId kIdB = 2;
  loop.loopLengthTicks = kLoopLenTicks;

  NoteUtils::DisplayNoteVec spans;
  spans.push_back(testSourceSpan(kIdA, kPitch, 0, 743));
  spans.push_back(testSourceSpan(kIdB, kPitch, 0, 168));
  loop.replaceOverdubSourceViewNotesForTest(std::move(spans));

  PlaybackMergedMidiEvents merged;
  merged.windowStartTick = 0;
  merged.windowLengthTicks = kLoopLenTicks;
  MidiEvent onA = MidiEvent::NoteOn(0, 1, kPitch, 100);
  onA.noteId = kIdA;
  MidiEvent offA = MidiEvent::NoteOff(743, 1, kPitch, 0);
  MidiEvent onB = MidiEvent::NoteOn(744, 1, kPitch, 100);
  onB.noteId = kIdB;
  MidiEvent offB = MidiEvent::NoteOff(168, 1, kPitch, 0);
  merged.mergedEvents.push_back(onA);
  merged.mergedEvents.push_back(offA);
  merged.mergedEvents.push_back(onB);
  merged.mergedEvents.push_back(offB);
  loop.replaceCommittedPlaybackNoteOnIdentities(merged);

  OverlapNoteIdSet sourceIds;
  loop.collectOverdubSourceHoldParticipantIds(72, kPitch, sourceIds);
  TEST_ASSERT_EQUAL_UINT32(2u, static_cast<uint32_t>(sourceIds.size()));
  TEST_ASSERT_TRUE(sourceIds.contains(kIdA));
  TEST_ASSERT_TRUE(sourceIds.contains(kIdB));
}

void test_occupy_source_hold_partial_window_does_not_drop_span() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  constexpr uint32_t kLoopLenTicks = 768;
  constexpr uint8_t kPitch = 60;
  constexpr NoteId kIdA = 1;
  constexpr NoteId kIdB = 2;
  loop.loopLengthTicks = kLoopLenTicks;

  NoteUtils::DisplayNoteVec spans;
  spans.push_back(testSourceSpan(kIdA, kPitch, 0, 743));
  spans.push_back(testSourceSpan(kIdB, kPitch, 0, 168));
  loop.replaceOverdubSourceViewNotesForTest(std::move(spans));

  PlaybackMergedMidiEvents merged;
  merged.windowStartTick = 6912;
  merged.windowLengthTicks = 1536;
  MidiEvent onB = MidiEvent::NoteOn(744, 1, kPitch, 100);
  onB.noteId = kIdB;
  merged.mergedEvents.push_back(onB);
  loop.replaceCommittedPlaybackNoteOnIdentities(merged);

  OverlapNoteIdSet sourceIds;
  loop.collectOverdubSourceHoldParticipantIds(72, kPitch, sourceIds);
  TEST_ASSERT_EQUAL_UINT32(2u, static_cast<uint32_t>(sourceIds.size()));
  TEST_ASSERT_TRUE(sourceIds.contains(kIdA));
  TEST_ASSERT_TRUE(sourceIds.contains(kIdB));
}

void test_occupy_source_hold_stale_revision_does_not_drop_span() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  constexpr uint32_t kLoopLenTicks = 768;
  constexpr uint8_t kPitch = 60;
  constexpr NoteId kIdA = 1;
  constexpr NoteId kIdB = 2;
  loop.loopLengthTicks = kLoopLenTicks;
  loop.playbackRevision = 9;

  NoteUtils::DisplayNoteVec spans;
  spans.push_back(testSourceSpan(kIdA, kPitch, 0, 743));
  spans.push_back(testSourceSpan(kIdB, kPitch, 0, 168));
  loop.replaceOverdubSourceViewNotesForTest(std::move(spans));

  PlaybackMergedMidiEvents merged;
  merged.builtFromRevision = 8;  // stale w.r.t. loop.playbackRevision
  merged.windowStartTick = 0;
  merged.windowLengthTicks = kLoopLenTicks;
  MidiEvent onB = MidiEvent::NoteOn(744, 1, kPitch, 100);
  onB.noteId = kIdB;
  merged.mergedEvents.push_back(onB);
  loop.replaceCommittedPlaybackNoteOnIdentities(merged);

  OverlapNoteIdSet sourceIds;
  loop.collectOverdubSourceHoldParticipantIds(72, kPitch, sourceIds);
  TEST_ASSERT_EQUAL_UINT32(2u, static_cast<uint32_t>(sourceIds.size()));
  TEST_ASSERT_TRUE(sourceIds.contains(kIdA));
  TEST_ASSERT_TRUE(sourceIds.contains(kIdB));
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

void applyIdentityHideAndShorten(NoteUtils::DisplayNoteVec& notes,
                                 const PendingNoteChangeVec& overlay) {
  NoteUtils::DisplayNoteVec kept;
  kept.reserve(notes.size());
  for (const NoteUtils::DisplayNote& note : notes) {
    const PendingNoteChange* transform = nullptr;
    for (const PendingNoteChange& change : overlay) {
      if ((change.kind == PendingNoteChangeKind::Hide ||
           change.kind == PendingNoteChangeKind::Shorten) &&
          change.noteId == note.noteId) {
        transform = &change;
        break;
      }
    }
    if (transform == nullptr) {
      kept.push_back(note);
      continue;
    }
    if (transform->kind == PendingNoteChangeKind::Hide) {
      continue;
    }
    NoteUtils::DisplayNote revised = note;
    revised.startTick = transform->startTick;
    revised.endTick = transform->endTick;
    kept.push_back(revised);
  }
  notes.swap(kept);
}

PendingNoteChange hideChange(NoteId noteId, uint8_t pitch, uint32_t startTick, uint32_t endTick) {
  PendingNoteChange change{};
  change.kind = PendingNoteChangeKind::Hide;
  change.noteId = noteId;
  change.channel = 1;
  change.pitch = pitch;
  change.startTick = startTick;
  change.endTick = endTick;
  return change;
}

PendingNoteChange shortenChange(NoteId noteId, uint8_t pitch, uint32_t startTick, uint32_t endTick) {
  PendingNoteChange change{};
  change.kind = PendingNoteChangeKind::Shorten;
  change.noteId = noteId;
  change.channel = 1;
  change.pitch = pitch;
  change.startTick = startTick;
  change.endTick = endTick;
  return change;
}

NoteUtils::DisplayNote displayNote(NoteId noteId, uint8_t pitch, uint32_t startTick,
                                   uint32_t endTick) {
  NoteUtils::DisplayNote note{};
  note.noteId = noteId;
  note.note = pitch;
  note.velocity = 90;
  note.startTick = startTick;
  note.endTick = endTick;
  return note;
}

const EditPass* findEditPassById(const Loop& loop, EditPassId id) {
  for (const EditPass& row : loop.passes.editPasses) {
    if (row.id == id) {
      return &row;
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

bool completeSnapshotConstruction(LoopSnapshotConstruction& construction) {
  size_t steps = 0;
  while (!construction.complete() && !construction.failed() && steps < 10000) {
    construction.step(1);
    ++steps;
  }
  return construction.complete() && !construction.failed();
}

ImmutableLoopSnapshotRef snapshotWithAddedNote(const ImmutableLoopSnapshotRef& parent,
                                               NoteId noteId, uint8_t pitch, uint32_t startTick,
                                               uint32_t endTick) {
  LoopSnapshotChangeSet addA;
  SnapshotNoteChange add{};
  add.kind = SnapshotChangeKind::Add;
  add.next.loopId = parent->loopId();
  add.next.noteId = noteId;
  add.next.pitch = pitch;
  add.next.velocity = 90;
  add.next.startTick = startTick;
  add.next.endTick = endTick;
  addA.notes.push_back(add);
  LoopSnapshotConstruction construction(parent, addA);
  TEST_ASSERT_TRUE(completeSnapshotConstruction(construction));
  const ImmutableLoopSnapshotRef withA = construction.freeze();
  TEST_ASSERT_TRUE(static_cast<bool>(withA));
  TEST_ASSERT_TRUE(static_cast<bool>(withA->findNote(noteId, pitch)));
  return withA;
}

void sealRecordedHideCompanion(Loop& loop, NoteId noteId, uint8_t pitch, uint32_t startTick,
                               uint32_t endTick, uint32_t startRevision) {
  loop.beginCapture(CapturePhase::Overdub);
  loop.pushPendingNoteChangeForTest(hideChange(noteId, pitch, startTick, endTick));
  loop.playbackRevision = startRevision;
  const EditPassIdList ids = loop.sealPendingNoteChangesToEditPasses(false);
  TEST_ASSERT_EQUAL(1u, ids.size());
  loop.recordLastOverdubSeal(loop.lastCommittedPassId(), ids, startRevision);
  TEST_ASSERT_TRUE(loop.pendingNoteChanges().empty());
}

void sealRecordedShortenCompanion(Loop& loop, NoteId noteId, uint8_t pitch, uint32_t startTick,
                                  uint32_t endTick, uint32_t startRevision) {
  loop.beginCapture(CapturePhase::Overdub);
  loop.pushPendingNoteChangeForTest(shortenChange(noteId, pitch, startTick, endTick));
  loop.playbackRevision = startRevision;
  const EditPassIdList ids = loop.sealPendingNoteChangesToEditPasses(false);
  TEST_ASSERT_EQUAL(1u, ids.size());
  loop.recordLastOverdubSeal(loop.lastCommittedPassId(), ids, startRevision);
  TEST_ASSERT_TRUE(loop.pendingNoteChanges().empty());
}

void publishSnapshotAtRevision(Loop& loop, uint32_t revision) {
  loop.playbackRevision = revision;
  loop.storePublishedSnapshotForTest(
      LoopSnapshot::createEmpty(loop.loopId, loop.loopLengthTicks, loop.loopStartTick, 1, 1),
      revision);
  TEST_ASSERT_TRUE(loop.publishedSnapshotIsCurrent());
}

/// RC3: sealed companion IDs are EditPass IDs that project Hide by targetNoteId.
void test_seal_companion_ids_project_hide_by_note_id() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  seedLongSourceNote(loop, 123, 2304, 2784, 64, 3072);
  sealRecordedHideCompanion(loop, 123, 64, 2304, 2784, 9);

  TEST_ASSERT_EQUAL(1u, loop.lastOverdubSealCompanionIdsForTest().size());
  const EditPass* row = findEditPassById(loop, loop.lastOverdubSealCompanionIdsForTest()[0]);
  TEST_ASSERT_NOT_NULL(row);
  TEST_ASSERT_EQUAL(static_cast<int>(EditActionType::Delete), static_cast<int>(row->actionType));
  TEST_ASSERT_EQUAL(123, row->targetNoteId);

  TEST_ASSERT_TRUE(loop.sealedCompanionOverlayRequired());
  NoteUtils::DisplayNoteVec paint;
  paint.push_back(displayNote(123, 64, 2304, 2784));
  loop.applyPendingNoteChangesToDisplayNotes(paint);
  TEST_ASSERT_TRUE(paint.empty());
}

/// RC3: overlay matches NoteId, not pitch/start.
void test_companion_overlay_ignores_mismatched_note_id() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  seedLongSourceNote(loop, 123, 2304, 2784, 64, 3072);
  sealRecordedHideCompanion(loop, 456, 64, 2304, 2784, 9);
  TEST_ASSERT_TRUE(loop.sealedCompanionOverlayRequired());

  NoteUtils::DisplayNoteVec paint;
  paint.push_back(displayNote(123, 64, 2304, 2784));
  loop.applyPendingNoteChangesToDisplayNotes(paint);
  TEST_ASSERT_EQUAL(1u, paint.size());
  TEST_ASSERT_EQUAL(123, paint[0].noteId);
}

/// RC3: overlay is idempotent and does not resurrect.
void test_companion_overlay_hide_is_idempotent() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  seedLongSourceNote(loop, 123, 2304, 2784, 64, 3072);
  sealRecordedHideCompanion(loop, 123, 64, 2304, 2784, 9);

  NoteUtils::DisplayNoteVec paint;
  paint.push_back(displayNote(123, 64, 2304, 2784));
  loop.applyPendingNoteChangesToDisplayNotes(paint);
  TEST_ASSERT_TRUE(paint.empty());
  loop.applyPendingNoteChangesToDisplayNotes(paint);
  TEST_ASSERT_TRUE(paint.empty());
}

/// RC3: revision >= Rseal is not enough to re-admit a still-present Hide target.
void test_seal_revision_implies_cumulative_companion_coverage() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  seedLongSourceNote(loop, 123, 2304, 2784, 64, 3072);
  sealRecordedHideCompanion(loop, 123, 64, 2304, 2784, 9);

  const uint32_t rSeal = loop.lastOverdubSealPlaybackRevisionForTest();
  TEST_ASSERT_EQUAL_UINT32(10u, rSeal);
  TEST_ASSERT_TRUE(loop.sealedCompanionOverlayRequired());
  TEST_ASSERT_TRUE(loop.hasSealedCompanionOverlay());

  NoteUtils::DisplayNoteVec stale;
  stale.push_back(displayNote(123, 64, 2304, 2784));
  loop.applyPendingNoteChangesToDisplayNotes(stale);
  TEST_ASSERT_TRUE(stale.empty());

  publishSnapshotAtRevision(loop, 10);
  TEST_ASSERT_FALSE(loop.sealedCompanionOverlayRequired());
  TEST_ASSERT_TRUE(loop.hasSealedCompanionOverlay());
  NoteUtils::DisplayNoteVec atSeal;
  atSeal.push_back(displayNote(123, 64, 2304, 2784));
  loop.applyPendingNoteChangesToDisplayNotes(atSeal);
  TEST_ASSERT_TRUE(atSeal.empty());

  publishSnapshotAtRevision(loop, 11);
  TEST_ASSERT_FALSE(loop.sealedCompanionOverlayRequired());
  TEST_ASSERT_TRUE(loop.hasSealedCompanionOverlay());
  NoteUtils::DisplayNoteVec afterSeal;
  afterSeal.push_back(displayNote(123, 64, 2304, 2784));
  loop.applyPendingNoteChangesToDisplayNotes(afterSeal);
  TEST_ASSERT_TRUE(afterSeal.empty());
}

/// 201956: after snapshot-current, Hide keeps 768→864 and drops record 768→1151.
void test_sealed_hide_keeps_short_overdub_after_snapshot_current() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  seedLongSourceNote(loop, 71, 768, 1152, 63, 3072);
  sealRecordedHideCompanion(loop, 71, 63, 768, 1151, 9);
  publishSnapshotAtRevision(loop, 10);
  TEST_ASSERT_FALSE(loop.sealedCompanionOverlayRequired());
  TEST_ASSERT_TRUE(loop.hasSealedCompanionOverlay());

  NoteUtils::DisplayNoteVec paint;
  paint.push_back(displayNote(71, 63, 768, 1151));
  paint.push_back(displayNote(95, 63, 768, 864));
  loop.applyPendingNoteChangesToDisplayNotes(paint);
  TEST_ASSERT_EQUAL(1u, paint.size());
  TEST_ASSERT_EQUAL(95, paint[0].noteId);
  TEST_ASSERT_EQUAL_UINT8(63, paint[0].note);
  TEST_ASSERT_EQUAL_UINT32(768u, paint[0].startTick);
  TEST_ASSERT_EQUAL_UINT32(864u, paint[0].endTick);
}

/// RC1: wrap 2 must not replace wrap-1 overlay Hide. Admission list stays last wrap.
void test_sealed_companion_overlay_retains_prior_wrap_hide() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  seedLongSourceNote(loop, 10, 1680, 2112, 63, 5376);
  seedLongSourceNote(loop, 20, 2304, 2448, 63, 5376);
  sealRecordedHideCompanion(loop, 10, 63, 1680, 2112, 9);
  TEST_ASSERT_EQUAL(1u, loop.lastOverdubSealCompanionIdsForTest().size());
  TEST_ASSERT_EQUAL(1u, loop.sealedCompanionOverlayIdsForTest().size());
  const EditPassId wrap1Id = loop.lastOverdubSealCompanionIdsForTest()[0];

  sealRecordedHideCompanion(loop, 20, 63, 2304, 2448, 11);
  TEST_ASSERT_EQUAL(1u, loop.lastOverdubSealCompanionIdsForTest().size());
  TEST_ASSERT_TRUE(loop.lastOverdubSealCompanionIdsForTest()[0] != wrap1Id);
  TEST_ASSERT_EQUAL(2u, loop.sealedCompanionOverlayIdsForTest().size());
  bool hasWrap1 = false;
  bool hasWrap2 = false;
  for (const EditPassId id : loop.sealedCompanionOverlayIdsForTest()) {
    if (id == wrap1Id) {
      hasWrap1 = true;
    }
    if (id == loop.lastOverdubSealCompanionIdsForTest()[0]) {
      hasWrap2 = true;
    }
  }
  TEST_ASSERT_TRUE(hasWrap1);
  TEST_ASSERT_TRUE(hasWrap2);

  NoteUtils::DisplayNoteVec paint;
  paint.push_back(displayNote(10, 63, 1680, 1968));
  paint.push_back(displayNote(20, 63, 2304, 2448));
  loop.applyPendingNoteChangesToDisplayNotes(paint);
  TEST_ASSERT_TRUE(paint.empty());
}

/// 201956: Hide named by pitch+start still drops a longer record sibling with a new noteId.
void test_sealed_hide_drops_same_start_longer_when_note_id_differs() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  seedLongSourceNote(loop, 71, 768, 1152, 63, 3072);
  sealRecordedHideCompanion(loop, 71, 63, 768, 1151, 9);
  publishSnapshotAtRevision(loop, 10);

  NoteUtils::DisplayNoteVec paint;
  paint.push_back(displayNote(99, 63, 768, 1151));
  paint.push_back(displayNote(95, 63, 768, 864));
  loop.applyPendingNoteChangesToDisplayNotes(paint);
  TEST_ASSERT_EQUAL(1u, paint.size());
  TEST_ASSERT_EQUAL(95, paint[0].noteId);
  TEST_ASSERT_EQUAL_UINT32(768u, paint[0].startTick);
  TEST_ASSERT_EQUAL_UINT32(864u, paint[0].endTick);
}

/// `123236`: sealed Shorten keeps 768→863 and drops the longer same-start remnant.
void test_sealed_shorten_drops_same_start_longer_sibling() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  seedLongSourceNote(loop, 71, 768, 1392, 63, 7680);
  sealRecordedShortenCompanion(loop, 71, 63, 768, 863, 9);
  publishSnapshotAtRevision(loop, 10);
  TEST_ASSERT_TRUE(loop.hasSealedCompanionOverlay());

  NoteUtils::DisplayNoteVec paint;
  paint.push_back(displayNote(71, 63, 768, 1392));
  paint.push_back(displayNote(99, 63, 768, 1344));
  loop.applyPendingNoteChangesToDisplayNotes(paint);
  TEST_ASSERT_EQUAL(1u, paint.size());
  TEST_ASSERT_EQUAL(71, paint[0].noteId);
  TEST_ASSERT_EQUAL_UINT8(63, paint[0].note);
  TEST_ASSERT_EQUAL_UINT32(768u, paint[0].startTick);
  TEST_ASSERT_EQUAL_UINT32(863u, paint[0].endTick);
}

/// `123236`: pending Shorten (not yet sealed) drops the longer same-start remnant.
void test_pending_shorten_drops_same_start_longer_sibling() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  loop.loopLengthTicks = 7680;
  loop.pushPendingNoteChangeForTest(shortenChange(71, 63, 768, 863));

  NoteUtils::DisplayNoteVec paint;
  paint.push_back(displayNote(71, 63, 768, 1392));
  paint.push_back(displayNote(99, 63, 768, 1344));
  loop.applyPendingNoteChangesToDisplayNotes(paint);
  TEST_ASSERT_EQUAL(1u, paint.size());
  TEST_ASSERT_EQUAL(71, paint[0].noteId);
  TEST_ASSERT_EQUAL_UINT32(768u, paint[0].startTick);
  TEST_ASSERT_EQUAL_UINT32(863u, paint[0].endTick);
}

/// `172734` RC1: transform eligibility is open-hold, not suffix position.
void test_pending_shorten_172734_open_hold_eligibility_matrix() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  seedLongSourceNote(loop, 80, 680, 1352, 63, 5376);
  loop.beginCapture(CapturePhase::Overdub);
  loop.pushPendingNoteChangeForTest(shortenChange(80, 63, 680, 1207));

  NoteUtils::DisplayNoteVec prefixPaint;
  prefixPaint.push_back(displayNote(80, 63, 680, 1352));
  loop.applyPendingNoteChangesToDisplayNotes(prefixPaint, 1);
  TEST_ASSERT_EQUAL(1u, prefixPaint.size());
  TEST_ASSERT_EQUAL(80, prefixPaint[0].noteId);
  TEST_ASSERT_EQUAL_UINT32(680u, prefixPaint[0].startTick);
  TEST_ASSERT_EQUAL_UINT32(1207u, prefixPaint[0].endTick);

  NoteUtils::DisplayNoteVec suffixPaint;
  suffixPaint.push_back(displayNote(1, 71, 536, 584));
  suffixPaint.push_back(displayNote(80, 63, 680, 1352));
  loop.applyPendingNoteChangesToDisplayNotes(suffixPaint, 1);
  TEST_ASSERT_EQUAL(2u, suffixPaint.size());
  TEST_ASSERT_EQUAL(1, suffixPaint[0].noteId);
  TEST_ASSERT_EQUAL_UINT32(584u, suffixPaint[0].endTick);
  TEST_ASSERT_EQUAL(80, suffixPaint[1].noteId);
  TEST_ASSERT_EQUAL_UINT32(680u, suffixPaint[1].startTick);
  TEST_ASSERT_EQUAL_UINT32(1207u, suffixPaint[1].endTick);

  NoteUtils::DisplayNoteVec openPaint;
  openPaint.push_back(displayNote(1, 71, 536, 584));
  openPaint.push_back(displayNote(80, 63, 1208, 1208));
  loop.applyPendingNoteChangesToDisplayNotes(openPaint, 1);
  TEST_ASSERT_EQUAL(2u, openPaint.size());
  TEST_ASSERT_EQUAL(80, openPaint[1].noteId);
  TEST_ASSERT_EQUAL_UINT32(1208u, openPaint[1].startTick);
  TEST_ASSERT_EQUAL_UINT32(1208u, openPaint[1].endTick);

  NoteUtils::DisplayNoteVec unrelatedPaint;
  unrelatedPaint.push_back(displayNote(1, 71, 536, 584));
  unrelatedPaint.push_back(displayNote(81, 69, 728, 776));
  loop.applyPendingNoteChangesToDisplayNotes(unrelatedPaint, 1);
  TEST_ASSERT_EQUAL(2u, unrelatedPaint.size());
  TEST_ASSERT_EQUAL(81, unrelatedPaint[1].noteId);
  TEST_ASSERT_EQUAL_UINT32(728u, unrelatedPaint[1].startTick);
  TEST_ASSERT_EQUAL_UINT32(776u, unrelatedPaint[1].endTick);

  TEST_ASSERT_NOT_NULL(findDisplayNoteById(loop.overdubSourceViewNotes(), 80));
  const NoteUtils::DisplayNote* source = findDisplayNoteById(loop.overdubSourceViewNotes(), 80);
  TEST_ASSERT_EQUAL_UINT32(680u, source->startTick);
  TEST_ASSERT_EQUAL_UINT32(1352u, source->endTick);
}

/// `193815` Bug 2 diagnosis: occupy `63` as=1536 ae=2208 hs=1920 → Shorten 1919.
/// Records what `applyPendingNoteChangesToDisplayNotes` does for each device-shaped
/// paint. Does not assume the root mechanism.
void test_pending_shorten_193815_63_1536_hold_1919_matrix() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  loop.loopLengthTicks = 3072;
  loop.beginCapture(CapturePhase::Overdub);
  loop.pushPendingNoteChangeForTest(shortenChange(40, 63, 1536, 1919));

  NoteUtils::DisplayNoteVec sameIdClosed;
  sameIdClosed.push_back(displayNote(40, 63, 1536, 2208));
  loop.applyPendingNoteChangesToDisplayNotes(sameIdClosed, 1);
  TEST_ASSERT_EQUAL(1u, sameIdClosed.size());
  TEST_ASSERT_EQUAL(40, sameIdClosed[0].noteId);
  TEST_ASSERT_EQUAL_UINT32(1536u, sameIdClosed[0].startTick);
  TEST_ASSERT_EQUAL_UINT32(1919u, sameIdClosed[0].endTick);

  NoteUtils::DisplayNoteVec invalidIdClosed;
  invalidIdClosed.push_back(displayNote(kInvalidNoteId, 63, 1536, 2208));
  loop.applyPendingNoteChangesToDisplayNotes(invalidIdClosed, 1);
  TEST_ASSERT_EQUAL(1u, invalidIdClosed.size());
  TEST_ASSERT_EQUAL(kInvalidNoteId, invalidIdClosed[0].noteId);
  TEST_ASSERT_EQUAL_UINT32(1536u, invalidIdClosed[0].startTick);
  TEST_ASSERT_EQUAL_UINT32(2208u, invalidIdClosed[0].endTick);

  NoteUtils::DisplayNoteVec openHold;
  openHold.push_back(displayNote(40, 63, 1920, 1920));
  loop.applyPendingNoteChangesToDisplayNotes(openHold, 1);
  TEST_ASSERT_EQUAL(1u, openHold.size());
  TEST_ASSERT_EQUAL(40, openHold[0].noteId);
  TEST_ASSERT_EQUAL_UINT32(1920u, openHold[0].startTick);
  TEST_ASSERT_EQUAL_UINT32(1920u, openHold[0].endTick);

  NoteUtils::DisplayNoteVec otherIdClosed;
  otherIdClosed.push_back(displayNote(99, 63, 1536, 2208));
  loop.applyPendingNoteChangesToDisplayNotes(otherIdClosed, 1);
  TEST_ASSERT_EQUAL(1u, otherIdClosed.size());
  TEST_ASSERT_EQUAL(99, otherIdClosed[0].noteId);
  TEST_ASSERT_EQUAL_UINT32(1536u, otherIdClosed[0].startTick);
  TEST_ASSERT_EQUAL_UINT32(2208u, otherIdClosed[0].endTick);
}

bool previewNoteIntersectsBarForTest(const NoteUtils::DisplayNote& note, uint32_t bar,
                                     uint32_t loopLength) {
  const uint32_t barStart = bar * Config::TICKS_PER_BAR;
  return DisplayWindowUtils::noteIntersectsWindow(note.startTick, note.endTick, barStart,
                                                  Config::TICKS_PER_BAR, loopLength);
}

/// Same sequence as `DisplayManagerInternal::applyDrawSourceOverlays`.
void applyDrawSourceOverlaysForTest(const Loop& loop, uint32_t firstBar, uint32_t lastBar,
                                    NoteUtils::DisplayNoteVec& out, size_t committedEnd) {
  const uint32_t loopLength = loop.loopLengthTicks;
  if (loopLength == 0 || firstBar > lastBar) {
    return;
  }
  const bool captureActive = loop.captureActive();
  if (DisplayWindowUtils::shouldUnionSealedCapturePreviewForDrawSource(
          loop.hasSealedCapturePreview(), loop.sealedCapturePreviewIsCovered(), captureActive,
          loop.hasOverdubSession())) {
    for (const NoteUtils::DisplayNote& sealedRow : loop.sealedCapturePreviewNotes()) {
      for (uint32_t bar = firstBar; bar <= lastBar; ++bar) {
        if (!previewNoteIntersectsBarForTest(sealedRow, bar, loopLength)) {
          continue;
        }
        DisplayWindowUtils::upsertDisplayNoteAtPitchStartFrom(out, committedEnd, sealedRow,
                                                             captureActive);
        break;
      }
    }
  }
  if (!captureActive) {
    return;
  }
  for (const NoteUtils::DisplayNote& previewNote : loop.capturePreview.notes) {
    bool intersectsWindow = false;
    for (uint32_t bar = firstBar; bar <= lastBar; ++bar) {
      if (previewNoteIntersectsBarForTest(previewNote, bar, loopLength)) {
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
      DisplayWindowUtils::upsertDisplayNoteAtPitchStartFrom(out, committedEnd, previewNote, true);
    }
  }
}

/// Same body as overdub `applyCapturePlayheadTails` (`allowWrapContinuation=false`).
void applyOverdubCapturePlayheadTailsForTest(const CapturePreview& preview, uint32_t loopLength,
                                             uint32_t closeTick, size_t captureRegionStart,
                                             NoteUtils::DisplayNoteVec& notes) {
  const uint32_t clampedCloseTick =
      (loopLength == 0) ? 0u : (closeTick >= loopLength ? (loopLength - 1u) : closeTick);
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
    uint32_t noteStartTick = previewNote.startTick;
    uint32_t playheadEndTick =
        noteStartTick > clampedCloseTick ? noteStartTick : clampedCloseTick;
    DisplayWindowUtils::clampNonWrapDisplayNoteBarTicks(noteStartTick, playheadEndTick, loopLength);
    bool updated = false;
    for (size_t i = captureRegionStart; i < notes.size(); ++i) {
      if (notes[i].note == previewNote.note && notes[i].startTick == previewNote.startTick) {
        notes[i].startTick = noteStartTick;
        notes[i].endTick = playheadEndTick;
        updated = true;
        break;
      }
    }
    if (!updated) {
      NoteUtils::DisplayNote liveNote{};
      liveNote.note = previewNote.note;
      liveNote.velocity = previewNote.velocity;
      liveNote.startTick = noteStartTick;
      liveNote.endTick = playheadEndTick;
      notes.push_back(liveNote);
    }
  }
}

const NoteUtils::DisplayNote* findPaintAtPitchStart(const NoteUtils::DisplayNoteVec& notes,
                                                    uint8_t pitch, uint32_t startTick) {
  for (const NoteUtils::DisplayNote& note : notes) {
    if (note.note == pitch && note.startTick == startTick) {
      return &note;
    }
  }
  return nullptr;
}

void seedOccupyOpenHoldPreview(Loop& loop, NoteId holdId) {
  NoteUtils::DisplayNote hold = displayNote(holdId, 63, 1920, 1920);
  loop.capturePreview.notes.push_back(hold);
  CapturePreviewNoteState state{};
  state.open = true;
  loop.capturePreview.noteStates.push_back(state);
  loop.capturePreview.openNoteIndices.push_back(0);
}

/// `193815` Bug 2 compose pin. Live painted NoteId is not in the capture.
/// Host stand-in ids label layer shapes only — not the device id.
/// Sequence matches `resolveDisplayNotesByDrawSource`: prefix + overlays stay
/// on compose; pending paints a copy after overdub tails.
void test_draw_source_compose_193815_63_1536_forks() {
  constexpr uint32_t kLoopLength = 3072;
  constexpr uint32_t kLastBar = (kLoopLength / Config::TICKS_PER_BAR) - 1u;
  constexpr NoteId kHostOccupyId = 1;
  constexpr NoteId kHostHoldId = 2;
  constexpr NoteId kHostOtherId = 3;

  auto compose = [&](Loop& loop, NoteUtils::DisplayNoteVec prefix) -> NoteUtils::DisplayNoteVec {
    const size_t committedEnd = prefix.size();
    applyDrawSourceOverlaysForTest(loop, 0, kLastBar, prefix, committedEnd);
    return prefix;
  };

  auto paintPending = [&](Loop& loop, const NoteUtils::DisplayNoteVec& composeBase,
                          size_t committedEnd, uint32_t closeTick) -> NoteUtils::DisplayNoteVec {
    NoteUtils::DisplayNoteVec painted = composeBase;
    applyOverdubCapturePlayheadTailsForTest(loop.capturePreview, kLoopLength, closeTick,
                                            committedEnd, painted);
    loop.applyPendingNoteChangesToDisplayNotes(painted, committedEnd);
    return painted;
  };

  {
    LoopEventStore::resetPoolForTests();
    LoopEventStore::initPool();
    Loop loop;
    loop.loopLengthTicks = kLoopLength;
    loop.beginCapture(CapturePhase::Overdub);
    loop.pushPendingNoteChangeForTest(shortenChange(kHostOccupyId, 63, 1536, 1919));
    seedOccupyOpenHoldPreview(loop, kHostHoldId);

    NoteUtils::DisplayNoteVec composeBase = compose(loop, {});
    TEST_ASSERT_NULL(findPaintAtPitchStart(composeBase, 63, 1536));

    NoteUtils::DisplayNoteVec painted = paintPending(loop, composeBase, 0, 1920);
    TEST_ASSERT_NULL(findPaintAtPitchStart(painted, 63, 1536));
    const NoteUtils::DisplayNote* hold = findPaintAtPitchStart(painted, 63, 1920);
    TEST_ASSERT_NOT_NULL(hold);
    TEST_ASSERT_EQUAL_UINT32(1920u, hold->endTick);
  }

  {
    LoopEventStore::resetPoolForTests();
    LoopEventStore::initPool();
    Loop loop;
    loop.loopLengthTicks = kLoopLength;
    loop.beginCapture(CapturePhase::Overdub);
    loop.pushPendingNoteChangeForTest(shortenChange(kHostOccupyId, 63, 1536, 1919));
    seedOccupyOpenHoldPreview(loop, kHostHoldId);

    NoteUtils::DisplayNoteVec prefix;
    prefix.push_back(displayNote(kInvalidNoteId, 63, 1536, 2208));
    NoteUtils::DisplayNoteVec composeBase = compose(loop, prefix);
    const NoteUtils::DisplayNote* painted = findPaintAtPitchStart(composeBase, 63, 1536);
    TEST_ASSERT_NOT_NULL(painted);
    TEST_ASSERT_EQUAL(kInvalidNoteId, painted->noteId);
    TEST_ASSERT_EQUAL_UINT32(2208u, painted->endTick);
    NoteUtils::DisplayNoteVec paintedNotes = paintPending(loop, composeBase, 1, 1920);
    painted = findPaintAtPitchStart(paintedNotes, 63, 1536);
    TEST_ASSERT_NOT_NULL(painted);
    TEST_ASSERT_EQUAL_UINT32(2208u, painted->endTick);

    prefix.clear();
    prefix.push_back(displayNote(kHostOtherId, 63, 1536, 2208));
    composeBase = compose(loop, prefix);
    painted = findPaintAtPitchStart(composeBase, 63, 1536);
    TEST_ASSERT_NOT_NULL(painted);
    TEST_ASSERT_EQUAL(kHostOtherId, painted->noteId);
    TEST_ASSERT_EQUAL_UINT32(2208u, painted->endTick);
    paintedNotes = paintPending(loop, composeBase, 1, 1920);
    painted = findPaintAtPitchStart(paintedNotes, 63, 1536);
    TEST_ASSERT_NOT_NULL(painted);
    TEST_ASSERT_EQUAL_UINT32(2208u, painted->endTick);
  }

  {
    LoopEventStore::resetPoolForTests();
    LoopEventStore::initPool();
    Loop loop;
    loop.loopLengthTicks = kLoopLength;
    loop.beginCapture(CapturePhase::Overdub);
    loop.pushPendingNoteChangeForTest(shortenChange(kHostOccupyId, 63, 1536, 1919));
    seedOccupyOpenHoldPreview(loop, kHostHoldId);

    NoteUtils::DisplayNoteVec prefix;
    prefix.push_back(displayNote(kHostOccupyId, 63, 1536, 2208));
    NoteUtils::DisplayNoteVec composeBase = compose(loop, prefix);
    const NoteUtils::DisplayNote* composeNote = findPaintAtPitchStart(composeBase, 63, 1536);
    TEST_ASSERT_NOT_NULL(composeNote);
    TEST_ASSERT_EQUAL(kHostOccupyId, composeNote->noteId);
    TEST_ASSERT_EQUAL_UINT32(2208u, composeNote->endTick);

    NoteUtils::DisplayNoteVec painted = paintPending(loop, composeBase, 1, 2365);
    const NoteUtils::DisplayNote* paintedNote = findPaintAtPitchStart(painted, 63, 1536);
    TEST_ASSERT_NOT_NULL(paintedNote);
    TEST_ASSERT_EQUAL_UINT32(1919u, paintedNote->endTick);
    composeNote = findPaintAtPitchStart(composeBase, 63, 1536);
    TEST_ASSERT_NOT_NULL(composeNote);
    TEST_ASSERT_EQUAL_UINT32(2208u, composeNote->endTick);
    const NoteUtils::DisplayNote* hold = findPaintAtPitchStart(painted, 63, 1920);
    TEST_ASSERT_NOT_NULL(hold);
    TEST_ASSERT_EQUAL_UINT32(2365u, hold->endTick);
  }
}

/// `215658`: NoteOn resolve writes pending Shorten (`as=768,ae=1440,hs=1104`).
/// Compose keeps the original end; the first paint copy has holdStart-1.
void test_noteon_shorten_paint_keeps_compose_original_end_215658() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  seedLongSourceNote(loop, 40, 768, 1440, 63, 3840);
  loop.openOverdubSession(0);
  loop.beginCapture(CapturePhase::Overdub);

  OverlapNoteIdSet resolved;
  loop.resolveOccupancyAtNoteOn(1, 63, 90, overlapIds({40}), 1104, &resolved);
  TEST_ASSERT_TRUE(resolved.contains(40));
  TEST_ASSERT_EQUAL(1, countKind(loop.pendingNoteChanges(), PendingNoteChangeKind::Shorten));
  const PendingNoteChange* shorten = findTransform(loop.pendingNoteChanges(), 40);
  TEST_ASSERT_NOT_NULL(shorten);
  TEST_ASSERT_EQUAL_UINT32(768u, shorten->startTick);
  TEST_ASSERT_EQUAL_UINT32(1103u, shorten->endTick);

  NoteUtils::DisplayNoteVec compose;
  uint32_t originalEnd = 0;
  for (const NoteUtils::DisplayNote& note : loop.overdubSourceViewNotes()) {
    if (note.noteId == 40) {
      compose.push_back(note);
      originalEnd = note.endTick;
      break;
    }
  }
  TEST_ASSERT_EQUAL(1u, compose.size());
  TEST_ASSERT_TRUE(originalEnd > 1103u);

  NoteUtils::DisplayNoteVec painted = compose;
  loop.applyPendingNoteChangesToDisplayNotes(painted, painted.size());
  const DisplayWindowUtils::PaintedPendingShorten described =
      DisplayWindowUtils::describePaintedPendingShorten(compose, painted, 40, 1103);
  TEST_ASSERT_TRUE(described.composeHasTarget);
  TEST_ASSERT_EQUAL_UINT32(originalEnd, described.composeEndTick);
  TEST_ASSERT_EQUAL_UINT32(originalEnd, compose[0].endTick);
  TEST_ASSERT_TRUE(described.paintHasTarget);
  TEST_ASSERT_TRUE(described.paintHasShortenedEnd);
  TEST_ASSERT_EQUAL_UINT32(768u, described.paintStartTick);
  TEST_ASSERT_EQUAL_UINT32(1103u, described.paintEndTick);
  TEST_ASSERT_EQUAL(0, loop.passes.editPasses.size());
}

/// `202821` Stage 1: NoteOn pending Shorten paints holdStart-1 on the next
/// live frame. Compose / capture storage stay untransformed until seal.
void test_noteon_pending_paint_copy_shortens_before_stop_202821() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  loop.loopLengthTicks = 3072;
  loop.beginCapture(CapturePhase::Overdub);
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(noteOnWithNoteId(1536, 0, 63, 100, 73)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(2208, 0, 63, 0)));
  const uint16_t captureRevision = loop.captureDisplayRevision;
  const size_t captureEvents = loop.capture.store.size();
  loop.pushPendingNoteChangeForTest(shortenChange(73, 63, 1536, 2015));

  NoteUtils::DisplayNoteVec composeBase;
  composeBase.push_back(displayNote(73, 63, 1536, 2208));
  TEST_ASSERT_TRUE(DisplayWindowUtils::shouldPaintPendingDrawSourceRepresentation(
      true, loop.hasPendingNoteChanges(), loop.hasSealedCompanionOverlay()));

  NoteUtils::DisplayNoteVec painted = composeBase;
  loop.applyPendingNoteChangesToDisplayNotes(painted, painted.size());
  TEST_ASSERT_EQUAL_UINT32(2208u, composeBase[0].endTick);
  TEST_ASSERT_EQUAL_UINT32(2015u, painted[0].endTick);
  TEST_ASSERT_EQUAL(captureEvents, loop.capture.store.size());
  TEST_ASSERT_EQUAL_UINT16(captureRevision, loop.captureDisplayRevision);
  TEST_ASSERT_EQUAL(0, loop.passes.editPasses.size());
}

/// `212700`: snapshot compose drops NoteId 84 while VisualCache still has it.
/// Recover committed geometry, then pending Shorten paints the shortened end.
void test_pending_shorten_recovers_authoritative_source_212700() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  loop.loopLengthTicks = 6144;
  loop.pushPendingNoteChangeForTest(shortenChange(84, 84, 4560, 4575));

  NoteUtils::DisplayNoteVec compose;
  NoteUtils::DisplayNoteVec visual;
  visual.push_back(displayNote(84, 84, 4560, 4608));
  NoteUtils::DisplayNoteVec authoritative;
  authoritative.push_back(displayNote(84, 84, 4560, 4608));
  const NoteId shortenIds[] = {84};
  const DisplayWindowUtils::PendingShortenRecoveryCounts counts =
      DisplayWindowUtils::recoverMissingPendingShortenRows(compose, visual, shortenIds, 1,
                                                           authoritative);
  TEST_ASSERT_EQUAL_UINT32(1u, counts.recovered);
  TEST_ASSERT_EQUAL(1u, compose.size());
  TEST_ASSERT_EQUAL_UINT32(4608u, compose[0].endTick);

  NoteUtils::DisplayNoteVec painted = compose;
  loop.applyPendingNoteChangesToDisplayNotes(painted, painted.size());
  TEST_ASSERT_EQUAL(1u, painted.size());
  TEST_ASSERT_EQUAL(84, painted[0].noteId);
  TEST_ASSERT_EQUAL_UINT32(4560u, painted[0].startTick);
  TEST_ASSERT_EQUAL_UINT32(4575u, painted[0].endTick);
}

/// `223435`: after wrap, compose and VisualCache both miss id 70 `1536–2208`
/// while source-view still has the row. Recover authoritative geometry, then
/// the first NoteOn paint shows holdStart-1. Compose keeps the original end.
void test_wrap_noteon_shorten_recovers_without_visual_identity_223435() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  seedLongSourceNote(loop, 70, 1536, 2208, 63, 3072);
  loop.openOverdubSession(0);
  loop.beginCapture(CapturePhase::Overdub);

  OverlapNoteIdSet resolved;
  loop.resolveOccupancyAtNoteOn(1, 63, 100, overlapIds({70}), 1824, &resolved);
  TEST_ASSERT_TRUE(resolved.contains(70));
  const PendingNoteChange* shorten = findTransform(loop.pendingNoteChanges(), 70);
  TEST_ASSERT_NOT_NULL(shorten);
  TEST_ASSERT_EQUAL(static_cast<int>(PendingNoteChangeKind::Shorten),
                    static_cast<int>(shorten->kind));
  TEST_ASSERT_EQUAL_UINT32(1536u, shorten->startTick);
  TEST_ASSERT_EQUAL_UINT32(1823u, shorten->endTick);

  NoteUtils::DisplayNoteVec compose;
  NoteUtils::DisplayNoteVec visual;
  NoteUtils::DisplayNoteVec authoritative;
  for (const NoteUtils::DisplayNote& note : loop.overdubSourceViewNotes()) {
    if (note.noteId == 70) {
      authoritative.push_back(note);
      break;
    }
  }
  TEST_ASSERT_EQUAL(1u, authoritative.size());
  TEST_ASSERT_EQUAL_UINT32(2208u, authoritative[0].endTick);

  const NoteId shortenIds[] = {70};
  const DisplayWindowUtils::PendingShortenRecoveryCounts counts =
      DisplayWindowUtils::recoverMissingPendingShortenRows(compose, visual, shortenIds, 1,
                                                           authoritative);
  TEST_ASSERT_EQUAL_UINT32(1u, counts.recovered);
  TEST_ASSERT_EQUAL_UINT32(0u, counts.authoritativeAbsence);
  TEST_ASSERT_EQUAL(1u, compose.size());
  TEST_ASSERT_EQUAL(70, compose[0].noteId);
  TEST_ASSERT_EQUAL_UINT32(1536u, compose[0].startTick);
  TEST_ASSERT_EQUAL_UINT32(2208u, compose[0].endTick);

  NoteUtils::DisplayNoteVec painted = compose;
  loop.applyPendingNoteChangesToDisplayNotes(painted, painted.size());
  const DisplayWindowUtils::PaintedPendingShorten described =
      DisplayWindowUtils::describePaintedPendingShorten(compose, painted, 70, 1823);
  TEST_ASSERT_TRUE(described.composeHasTarget);
  TEST_ASSERT_EQUAL_UINT32(2208u, described.composeEndTick);
  TEST_ASSERT_TRUE(described.paintHasShortenedEnd);
  TEST_ASSERT_EQUAL_UINT32(1536u, described.paintStartTick);
  TEST_ASSERT_EQUAL_UINT32(1823u, described.paintEndTick);
  TEST_ASSERT_EQUAL(0, loop.passes.editPasses.size());
}

/// VisualCache still showing 84 is not enough to fabricate a Shorten source.
void test_pending_shorten_authoritative_absence_does_not_resurrect_212700() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  loop.loopLengthTicks = 6144;
  loop.pushPendingNoteChangeForTest(shortenChange(84, 84, 4560, 4575));

  NoteUtils::DisplayNoteVec compose;
  NoteUtils::DisplayNoteVec visual;
  visual.push_back(displayNote(84, 84, 4560, 4608));
  NoteUtils::DisplayNoteVec authoritative;
  const NoteId shortenIds[] = {84};
  const DisplayWindowUtils::PendingShortenRecoveryCounts counts =
      DisplayWindowUtils::recoverMissingPendingShortenRows(compose, visual, shortenIds, 1,
                                                           authoritative);
  TEST_ASSERT_EQUAL_UINT32(0u, counts.recovered);
  TEST_ASSERT_EQUAL_UINT32(1u, counts.authoritativeAbsence);
  TEST_ASSERT_TRUE(compose.empty());

  loop.applyPendingNoteChangesToDisplayNotes(compose, compose.size());
  TEST_ASSERT_TRUE(compose.empty());
}

/// Hide keeps the existing absence path. Recovery is Shorten-only.
void test_pending_hide_does_not_recover_dropped_source_row() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  loop.loopLengthTicks = 6144;
  loop.pushPendingNoteChangeForTest(hideChange(84, 84, 4560, 4608));

  NoteUtils::DisplayNoteVec compose;
  NoteUtils::DisplayNoteVec visual;
  visual.push_back(displayNote(84, 84, 4560, 4608));
  NoteUtils::DisplayNoteVec authoritative;
  authoritative.push_back(displayNote(84, 84, 4560, 4608));
  const DisplayWindowUtils::PendingShortenRecoveryCounts counts =
      DisplayWindowUtils::recoverMissingPendingShortenRows(compose, visual, nullptr, 0,
                                                           authoritative);
  TEST_ASSERT_EQUAL_UINT32(0u, counts.recovered);
  TEST_ASSERT_TRUE(compose.empty());

  loop.applyPendingNoteChangesToDisplayNotes(compose, compose.size());
  TEST_ASSERT_TRUE(compose.empty());
}

/// `000402`: paint shortens later NoteIds, but a longer suffix remnant at the
/// same pitch+start stayed visible. Drop that remnant. Keep an open hold.
void test_pending_shorten_drops_suffix_longer_remnant_000402() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  seedLongSourceNote(loop, 71, 768, 1392, 63, 7680);
  sealRecordedShortenCompanion(loop, 71, 63, 768, 863, 9);

  NoteUtils::DisplayNoteVec paint;
  paint.push_back(displayNote(71, 63, 768, 1392));
  paint.push_back(displayNote(99, 63, 768, 1344));
  paint.push_back(displayNote(200, 63, 768, 2000));
  paint.push_back(displayNote(201, 63, 864, 864));
  loop.applyPendingNoteChangesToDisplayNotes(paint, 2);
  TEST_ASSERT_EQUAL(2u, paint.size());
  TEST_ASSERT_EQUAL(71, paint[0].noteId);
  TEST_ASSERT_EQUAL_UINT32(863u, paint[0].endTick);
  TEST_ASSERT_EQUAL(201, paint[1].noteId);
  TEST_ASSERT_EQUAL_UINT32(864u, paint[1].endTick);
}

/// `000402`: first occupy Shorten paints; later occupy also paints by NoteId but
/// a sealed-preview original at that pitch+start must not remain beside it.
void test_later_occupy_shorten_drops_suffix_remnant_000402() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  loop.loopLengthTicks = 3072;
  loop.pushPendingNoteChangeForTest(shortenChange(71, 63, 1536, 1967));
  loop.pushPendingNoteChangeForTest(shortenChange(73, 63, 2296, 2639));

  NoteUtils::DisplayNoteVec paint;
  paint.push_back(displayNote(71, 63, 1536, 2208));
  paint.push_back(displayNote(73, 63, 2296, 2976));
  paint.push_back(displayNote(171, 63, 1536, 2208));
  paint.push_back(displayNote(173, 63, 2296, 2976));
  loop.applyPendingNoteChangesToDisplayNotes(paint, 2);
  TEST_ASSERT_EQUAL(2u, paint.size());
  TEST_ASSERT_NOT_NULL(findDisplayNoteById(paint, 71));
  TEST_ASSERT_NOT_NULL(findDisplayNoteById(paint, 73));
  TEST_ASSERT_EQUAL_UINT32(1967u, findDisplayNoteById(paint, 71)->endTick);
  TEST_ASSERT_EQUAL_UINT32(2639u, findDisplayNoteById(paint, 73)->endTick);
  TEST_ASSERT_NULL(findDisplayNoteById(paint, 171));
  TEST_ASSERT_NULL(findDisplayNoteById(paint, 173));
}

/// `001145`: same-start Hide after earlier Shortens must drop the closed remnant
/// at that pitch+start. The new occupy is an open hold and cannot dominate it.
void test_same_start_hide_drops_closed_remnant_keeps_open_hold_001145() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  loop.loopLengthTicks = 3072;
  loop.pushPendingNoteChangeForTest(hideChange(80, 63, 1152, 1440));

  NoteUtils::DisplayNoteVec paint;
  paint.push_back(displayNote(80, 63, 1152, 1440));
  paint.push_back(displayNote(180, 63, 1152, 1440));
  paint.push_back(displayNote(90, 63, 1152, 1152));
  loop.applyPendingNoteChangesToDisplayNotes(paint, 2);
  TEST_ASSERT_EQUAL(1u, paint.size());
  TEST_ASSERT_EQUAL(90, paint[0].noteId);
  TEST_ASSERT_EQUAL_UINT32(1152u, paint[0].startTick);
  TEST_ASSERT_EQUAL_UINT32(1152u, paint[0].endTick);
}

/// `001145`: Hide target already gone; remnant still drops when the new occupy
/// hold is present. A Hide whose target was never in the frame stays RC3.
void test_same_start_hide_drops_remnant_when_hold_present_001145() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  loop.loopLengthTicks = 3072;
  loop.pushPendingNoteChangeForTest(hideChange(80, 63, 1152, 1440));

  NoteUtils::DisplayNoteVec paint;
  paint.push_back(displayNote(180, 63, 1152, 1440));
  paint.push_back(displayNote(90, 63, 1152, 1152));
  loop.applyPendingNoteChangesToDisplayNotes(paint, 1);
  TEST_ASSERT_EQUAL(1u, paint.size());
  TEST_ASSERT_EQUAL(90, paint[0].noteId);
  TEST_ASSERT_EQUAL_UINT32(1152u, paint[0].endTick);
}

/// RC3: overlay does not manufacture a Hide for a note occupancy never resolved.
void test_sealed_overlay_leaves_unresolved_bar_two_note() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  seedLongSourceNote(loop, 123, 2304, 2784, 64, 3072);
  sealRecordedHideCompanion(loop, 123, 64, 2304, 2784, 9);

  NoteUtils::DisplayNoteVec paint;
  paint.push_back(displayNote(123, 64, 2304, 2784));
  paint.push_back(displayNote(200, 64, 768, 816));
  loop.applyPendingNoteChangesToDisplayNotes(paint);
  TEST_ASSERT_EQUAL(1u, paint.size());
  TEST_ASSERT_EQUAL(200, paint[0].noteId);
  TEST_ASSERT_EQUAL_UINT32(768u, paint[0].startTick);
}

/// RC3: playback admit-receipt clear must not drop the display overlay.
void test_sealed_overlay_survives_admit_receipt_clear() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  seedLongSourceNote(loop, 123, 2304, 2784, 64, 3072);
  sealRecordedHideCompanion(loop, 123, 64, 2304, 2784, 9);
  TEST_ASSERT_TRUE(loop.sealedCompanionOverlayRequired());

  loop.clearLastOverdubSealAdmitReceipt();
  TEST_ASSERT_FALSE(loop.hasLastOverdubSealForTest());
  TEST_ASSERT_TRUE(loop.sealedCompanionOverlayRequired());

  NoteUtils::DisplayNoteVec paint;
  paint.push_back(displayNote(123, 64, 2304, 2784));
  loop.applyPendingNoteChangesToDisplayNotes(paint);
  TEST_ASSERT_TRUE(paint.empty());
}

/// RC3 journal: missing parent target is not grounds to drop Hide.
void test_wrap_journal_keeps_hide_when_parent_lacks_note() {
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
  drainPendingSnapshotPublish(loop);
  TEST_ASSERT_TRUE(loop.publishedSnapshotIsCurrent());
  const NoteId recordNoteId =
      loop.publishedSnapshot()->noteMembership(60, 0)->references[0]->noteId;
  TEST_ASSERT_NOT_EQUAL(kInvalidNoteId, recordNoteId);

  loop.openOverdubSession(0);
  loop.beginCapture(CapturePhase::Overdub);
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(400, 1, 72, 90)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(480, 1, 72, 0)));
  TEST_ASSERT_EQUAL(CommitResult::Committed,
                    loop.commitCapturePass(CommitReason::OverdubWrap, 480));
  loop.pushPendingNoteChangeForTest(hideChange(999, 64, 2304, 2784));
  TEST_ASSERT_TRUE(loop.queueOverdubWrapSnapshotJournal(loop.lastCommittedPassId()));
  const LoopSnapshotChangeSet* queued = loop.pendingWrapJournalChangesForTest();
  TEST_ASSERT_NOT_NULL(queued);
  LoopSnapshotChangeSet journal = *queued;
  bool keptIdentityDelete = false;
  for (const SnapshotNoteChange& change : journal.notes) {
    if (change.kind == SnapshotChangeKind::Remove && change.next.noteId == 999 &&
        !change.previous) {
      keptIdentityDelete = true;
    }
  }
  TEST_ASSERT_TRUE(keptIdentityDelete);

  const ImmutableLoopSnapshotRef parent = loop.publishedSnapshot();
  TEST_ASSERT_TRUE(static_cast<bool>(parent));
  TEST_ASSERT_FALSE(static_cast<bool>(parent->findNote(999, 64)));

  LoopSnapshotConstruction missingTarget(parent, journal);
  TEST_ASSERT_TRUE(completeSnapshotConstruction(missingTarget));
  const ImmutableLoopSnapshotRef stillMissing = missingTarget.freeze();
  TEST_ASSERT_TRUE(static_cast<bool>(stillMissing));
  TEST_ASSERT_FALSE(static_cast<bool>(stillMissing->findNote(999, 64)));
  TEST_ASSERT_TRUE(static_cast<bool>(stillMissing->findNote(recordNoteId, 60)));

  const ImmutableLoopSnapshotRef withA = snapshotWithAddedNote(parent, 999, 64, 2304, 2784);
  LoopSnapshotConstruction applyHide(withA, journal);
  TEST_ASSERT_TRUE(completeSnapshotConstruction(applyHide));
  const ImmutableLoopSnapshotRef afterHide = applyHide.freeze();
  TEST_ASSERT_TRUE(static_cast<bool>(afterHide));
  TEST_ASSERT_FALSE(static_cast<bool>(afterHide->findNote(999, 64)));
}

/// RC4 (172532 nopart): linear 2304→2496 is not present at hs=0 / hs=48.
void test_linear_bar_four_source_row_is_absent_at_hold_zero_and_forty_eight() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  seedLongSourceNote(loop, 1, 100, 200, 60, 3072);
  loop.openOverdubSession(0);
  loop.beginCapture(CapturePhase::Overdub, 0);
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(noteOnWithNoteId(2304, 1, 64, 90, 85)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(2496, 1, 64, 0)));
  TEST_ASSERT_EQUAL(CommitResult::Committed,
                    loop.commitCapturePass(CommitReason::OverdubWrap, 2496));
  loop.appendLastCommittedOverdubPassToSourceView();

  const NoteUtils::DisplayNote* row = findDisplayNoteById(loop.overdubSourceViewNotes(), 85);
  TEST_ASSERT_NOT_NULL(row);
  TEST_ASSERT_EQUAL_UINT32(2304u, row->startTick);
  TEST_ASSERT_EQUAL_UINT32(2496u, row->endTick);
  TEST_ASSERT_FALSE(row->endTick < row->startTick);

  OverlapNoteIdSet atZero;
  loop.collectOverdubSourceHoldParticipantIds(0, 64, atZero);
  TEST_ASSERT_EQUAL(0u, atZero.size());
  OverlapNoteIdSet atFortyEight;
  loop.collectOverdubSourceHoldParticipantIds(48, 64, atFortyEight);
  TEST_ASSERT_EQUAL(0u, atFortyEight.size());
}

/// RC4: a wrap-held row that is actually in the source view must occupy hs=0.
void test_wrap_held_source_row_occupies_hold_zero() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  seedLongSourceNote(loop, 1, 100, 200, 60, 3072);
  loop.openOverdubSession(0);
  loop.beginCapture(CapturePhase::Overdub, 0);
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(noteOnWithNoteId(2304, 1, 64, 90, 85)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(432, 1, 64, 0)));
  TEST_ASSERT_EQUAL(CommitResult::Committed,
                    loop.commitCapturePass(CommitReason::OverdubWrap, 432));
  loop.appendLastCommittedOverdubPassToSourceView();

  bool idInView = false;
  bool wrapHeldInView = false;
  for (const NoteUtils::DisplayNote& note : loop.overdubSourceViewNotes()) {
    if (note.noteId != 85) {
      continue;
    }
    idInView = true;
    if (note.endTick < note.startTick) {
      wrapHeldInView = true;
    }
  }
  TEST_ASSERT_TRUE(idInView);

  OverlapNoteIdSet atZero;
  loop.collectOverdubSourceHoldParticipantIds(0, 64, atZero);
  TEST_ASSERT_TRUE_MESSAGE(atZero.contains(85),
                           wrapHeldInView
                               ? "wrap-held source row missed hs=0"
                               : "On@2304 Off@432 is in the source view but not present at hs=0");
}

/// `225200`: stored wrap pair is `WRAP,4560,760` id 96, not Off@48. That
/// `233150`: pending Shorten painted before wrap must survive the first
/// post-seal draw-source paint. Compose may still hold VisualCache originals.
void test_wrap_seal_keeps_painted_shorten_233150() {
  constexpr uint32_t kLoopLength = 3072;
  constexpr NoteId kId = 71;

  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  seedLongSourceNote(loop, kId, 2304, 2880, 63, kLoopLength);
  loop.openOverdubSession(0);
  loop.beginCapture(CapturePhase::Overdub);

  OverlapNoteIdSet resolved;
  loop.resolveOccupancyAtNoteOn(1, 63, 100, overlapIds({kId}), 2736, &resolved);
  TEST_ASSERT_TRUE(resolved.contains(kId));
  const PendingNoteChange* pending = findTransform(loop.pendingNoteChanges(), kId);
  TEST_ASSERT_NOT_NULL(pending);
  TEST_ASSERT_EQUAL(static_cast<int>(PendingNoteChangeKind::Shorten),
                    static_cast<int>(pending->kind));
  TEST_ASSERT_EQUAL_UINT32(2304u, pending->startTick);
  TEST_ASSERT_EQUAL_UINT32(2735u, pending->endTick);

  NoteUtils::DisplayNoteVec compose;
  compose.push_back(displayNote(kId, 63, 2304, 2880));
  NoteUtils::DisplayNoteVec painted = compose;
  loop.applyPendingNoteChangesToDisplayNotes(painted, compose.size());
  const NoteUtils::DisplayNote* pendingPaint = findDisplayNoteById(painted, kId);
  TEST_ASSERT_NOT_NULL(pendingPaint);
  TEST_ASSERT_EQUAL_UINT32(2735u, pendingPaint->endTick);

  const uint32_t priorRevision = loop.playbackRevision;
  const EditPassIdList companionIds = loop.sealPendingNoteChangesToEditPasses(false);
  TEST_ASSERT_EQUAL(1u, companionIds.size());
  TEST_ASSERT_FALSE(companionIds.empty());
  loop.recordLastOverdubSeal(loop.lastCommittedPassId(), companionIds, priorRevision);
  TEST_ASSERT_TRUE(loop.pendingNoteChanges().empty());
  TEST_ASSERT_TRUE(loop.hasSealedCompanionOverlay());

  const EditPass* sealedRow = findEditPassById(loop, companionIds[0]);
  TEST_ASSERT_NOT_NULL(sealedRow);
  TEST_ASSERT_EQUAL(static_cast<int>(EditActionType::Update),
                    static_cast<int>(sealedRow->actionType));
  TEST_ASSERT_EQUAL(static_cast<int>(EditPropertyType::Length),
                    static_cast<int>(sealedRow->propertyType));
  TEST_ASSERT_EQUAL(kId, sealedRow->targetNoteId);
  TEST_ASSERT_EQUAL_UINT32(2735u, sealedRow->endTick);

  NoteUtils::DisplayNoteVec postCompose;
  postCompose.push_back(displayNote(80, 63, 2736, 2832));
  NoteUtils::DisplayNoteVec postPaint = postCompose;
  loop.applyPendingNoteChangesToDisplayNotes(postPaint, 0u);
  TEST_ASSERT_NULL(findDisplayNoteById(postPaint, kId));

  postCompose.clear();
  postCompose.push_back(displayNote(kId, 63, 2304, 2880));
  postCompose.push_back(displayNote(80, 63, 2736, 2832));
  const size_t committedEnd = 1u;
  postPaint = postCompose;
  loop.applyPendingNoteChangesToDisplayNotes(postPaint, committedEnd);
  const NoteUtils::DisplayNote* sealedPaint = findDisplayNoteById(postPaint, kId);
  TEST_ASSERT_NOT_NULL(sealedPaint);
  TEST_ASSERT_EQUAL_UINT32(2304u, sealedPaint->startTick);
  TEST_ASSERT_EQUAL_UINT32(2735u, sealedPaint->endTick);

  postCompose.clear();
  postCompose.push_back(displayNote(82, 82, 96, 104));
  postCompose.push_back(displayNote(kId, 63, 2304, 2880));
  postPaint = postCompose;
  loop.applyPendingNoteChangesToDisplayNotes(postPaint, 1u);
  sealedPaint = findDisplayNoteById(postPaint, kId);
  TEST_ASSERT_NOT_NULL(sealedPaint);
  TEST_ASSERT_EQUAL_UINT32(2735u, sealedPaint->endTick);

  NoteUtils::DisplayNoteVec missingCompose;
  NoteUtils::DisplayNoteVec authoritative;
  for (const NoteUtils::DisplayNote& note : loop.overdubSourceViewNotes()) {
    if (note.noteId == kId) {
      authoritative.push_back(note);
      break;
    }
  }
  TEST_ASSERT_EQUAL(1u, authoritative.size());
  const NoteId recoverIds[] = {kId};
  const DisplayWindowUtils::PendingShortenRecoveryCounts counts =
      DisplayWindowUtils::recoverMissingPendingShortenRows(missingCompose, {}, recoverIds, 1,
                                                         authoritative);
  TEST_ASSERT_EQUAL_UINT32(1u, counts.recovered);
  loop.applyPendingNoteChangesToDisplayNotes(missingCompose, missingCompose.size());
  sealedPaint = findDisplayNoteById(missingCompose, kId);
  TEST_ASSERT_NOT_NULL(sealedPaint);
  TEST_ASSERT_EQUAL_UINT32(2735u, sealedPaint->endTick);
}

/// `235546`: first sealed Shorten (id 74) stayed after wrap; later id 76 did not.
/// Snapshot/source-view can supply only the first wrap-1 NoteId. Sealed Length
/// geometry must recover every missing companion, not just the first lookup hit.
void test_wrap_seal_keeps_later_painted_shorten_235546() {
  constexpr uint32_t kLoopLength = 3072;
  constexpr NoteId kFirstId = 74;
  constexpr NoteId kLaterId = 76;

  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  loop.loopLengthTicks = kLoopLength;
  LoopEventStore store;
  TEST_ASSERT_TRUE(storeAppendNoteOn(store, 1536, 1, 63, 100, kFirstId));
  TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOff(2256, 1, 63, 0)));
  TEST_ASSERT_TRUE(storeAppendNoteOn(store, 2304, 1, 63, 100, kLaterId));
  TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOff(3024, 1, 63, 0)));
  loop.seedRecordPassFromStore(store);
  loop.nextNoteId_ = kLaterId + 1;
  loop.openOverdubSession(0);
  loop.beginCapture(CapturePhase::Overdub);

  OverlapNoteIdSet firstResolved;
  loop.resolveOccupancyAtNoteOn(1, 63, 100, overlapIds({kFirstId}), 1968, &firstResolved);
  TEST_ASSERT_TRUE(firstResolved.contains(kFirstId));
  OverlapNoteIdSet laterResolved;
  loop.resolveOccupancyAtNoteOn(1, 63, 100, overlapIds({kLaterId}), 2736, &laterResolved);
  TEST_ASSERT_TRUE(laterResolved.contains(kLaterId));
  TEST_ASSERT_NOT_NULL(findTransform(loop.pendingNoteChanges(), kFirstId));
  TEST_ASSERT_NOT_NULL(findTransform(loop.pendingNoteChanges(), kLaterId));
  TEST_ASSERT_EQUAL_UINT32(1967u, findTransform(loop.pendingNoteChanges(), kFirstId)->endTick);
  TEST_ASSERT_EQUAL_UINT32(2735u, findTransform(loop.pendingNoteChanges(), kLaterId)->endTick);

  const uint32_t priorRevision = loop.playbackRevision;
  const EditPassIdList companionIds = loop.sealPendingNoteChangesToEditPasses(false);
  TEST_ASSERT_EQUAL(2u, companionIds.size());
  loop.recordLastOverdubSeal(loop.lastCommittedPassId(), companionIds, priorRevision);
  TEST_ASSERT_TRUE(loop.pendingNoteChanges().empty());
  TEST_ASSERT_TRUE(loop.hasSealedCompanionOverlay());

  uint32_t lengthRows = 0;
  const EditPass* laterLength = nullptr;
  for (const EditPassId id : companionIds) {
    const EditPass* row = findEditPassById(loop, id);
    TEST_ASSERT_NOT_NULL(row);
    if (row->actionType == EditActionType::Update &&
        row->propertyType == EditPropertyType::Length) {
      ++lengthRows;
      if (row->targetNoteId == kLaterId) {
        laterLength = row;
      }
    }
  }
  TEST_ASSERT_EQUAL_UINT32(2u, lengthRows);
  TEST_ASSERT_NOT_NULL(laterLength);
  TEST_ASSERT_EQUAL_UINT32(2304u, laterLength->startTick);
  TEST_ASSERT_EQUAL_UINT32(2735u, laterLength->endTick);

  NoteUtils::DisplayNoteVec bothOriginals;
  bothOriginals.push_back(displayNote(kFirstId, 63, 1536, 2256));
  bothOriginals.push_back(displayNote(kLaterId, 63, 2304, 3024));
  NoteUtils::DisplayNoteVec bothPaint = bothOriginals;
  loop.applyPendingNoteChangesToDisplayNotes(bothPaint, bothOriginals.size());
  TEST_ASSERT_NOT_NULL(findDisplayNoteById(bothPaint, kFirstId));
  TEST_ASSERT_NOT_NULL(findDisplayNoteById(bothPaint, kLaterId));
  TEST_ASSERT_EQUAL_UINT32(1967u, findDisplayNoteById(bothPaint, kFirstId)->endTick);
  TEST_ASSERT_EQUAL_UINT32(2735u, findDisplayNoteById(bothPaint, kLaterId)->endTick);

  NoteUtils::DisplayNoteVec firstOnly;
  firstOnly.push_back(displayNote(kFirstId, 63, 1536, 2256));
  NoteUtils::DisplayNoteVec laterAuthoritative;
  laterAuthoritative.push_back(displayNote(kLaterId, laterLength->pitch, laterLength->startTick,
                                           laterLength->endTick));
  const NoteId recoverIds[] = {kFirstId, kLaterId};
  const DisplayWindowUtils::PendingShortenRecoveryCounts counts =
      DisplayWindowUtils::recoverMissingPendingShortenRows(firstOnly, {}, recoverIds, 2,
                                                           laterAuthoritative);
  TEST_ASSERT_EQUAL_UINT32(1u, counts.recovered);
  TEST_ASSERT_NOT_NULL(findDisplayNoteById(firstOnly, kLaterId));
  loop.applyPendingNoteChangesToDisplayNotes(firstOnly, firstOnly.size());
  TEST_ASSERT_NOT_NULL(findDisplayNoteById(firstOnly, kFirstId));
  TEST_ASSERT_NOT_NULL(findDisplayNoteById(firstOnly, kLaterId));
  TEST_ASSERT_EQUAL_UINT32(1967u, findDisplayNoteById(firstOnly, kFirstId)->endTick);
  TEST_ASSERT_EQUAL_UINT32(2735u, findDisplayNoteById(firstOnly, kLaterId)->endTick);
}

/// wrap-held row must stay in source view after overdub open + wrap rebuild so
/// occupy at hs=48 can Shorten. Failure A is a missing row (do not touch
/// occupancy). Failure B is occupancy after the row exists. Intermediate
/// reconstruct/collapse asserts name the owner. Untagged Off@4607 is in the
/// capture and must not steal the wrap pair.
void test_wrap_held_record_row_occupies_head_225200() {
  constexpr uint32_t kLen = 4608;
  constexpr NoteId kId = 96;
  constexpr uint8_t kPitch = 69;

  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  loop.loopLengthTicks = kLen;
  LoopEventStore store;
  TEST_ASSERT_TRUE(storeAppendNoteOn(store, 4560, 1, kPitch, 100, kId));
  TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOff(760, 1, kPitch, 0)));
  TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOff(4607, 1, kPitch, 0)));
  loop.seedRecordPassFromStore(store);
  loop.nextNoteId_ = kId + 1;

  SessionMidiEventVec recordEvents;
  loop.passes.materializeToEventVector(recordEvents, kLen);
  const NoteUtils::DisplayNoteVec reconstructed =
      NoteUtils::reconstructDisplayNotes(recordEvents, kLen, false, false);
  bool reconstructedWrapHeld = false;
  bool reconstructedTail = false;
  bool reconstructedHead = false;
  for (const NoteUtils::DisplayNote& note : reconstructed) {
    if (note.noteId != kId) {
      continue;
    }
    TEST_ASSERT_EQUAL(kPitch, note.note);
    if (note.endTick < note.startTick && note.startTick == 4560u && note.endTick == 760u) {
      reconstructedWrapHeld = true;
    }
    if (note.startTick == 4560u && note.endTick == kLen - 1u) {
      reconstructedTail = true;
    }
    if (note.startTick == 0u && note.endTick == 760u) {
      reconstructedHead = true;
    }
  }
  TEST_ASSERT_TRUE_MESSAGE(
      reconstructedWrapHeld || (reconstructedTail && reconstructedHead),
      "reconstructDisplayNotes dropped WRAP 4560→760 id 96 (no wrap-held and no split head+tail)");

  loop.openOverdubSession(0);
  loop.beginCapture(CapturePhase::Overdub, 0);
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(noteOnWithNoteId(240, 1, 72, 90, 200)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(288, 1, 72, 0)));
  TEST_ASSERT_EQUAL(CommitResult::Committed,
                    loop.commitCapturePass(CommitReason::OverdubWrap, 0));
  loop.appendLastCommittedOverdubPassToSourceView();
  loop.rebuildOverdubSourceView(48, "test-225200");

  const NoteUtils::DisplayNote* sourceRow =
      findDisplayNoteById(loop.overdubSourceViewNotes(), kId);
  TEST_ASSERT_NOT_NULL_MESSAGE(
      sourceRow, "rebuildOverdubSourceView dropped id 96 after wrap (span-cache copy)");
  TEST_ASSERT_TRUE_MESSAGE(sourceRow->endTick < sourceRow->startTick,
                           "collapseSplitHeadTailToWrapHeld did not merge id 96 to wrap-held");
  TEST_ASSERT_EQUAL_UINT32(4560u, sourceRow->startTick);
  TEST_ASSERT_EQUAL_UINT32(760u, sourceRow->endTick);

  OverlapNoteIdSet atHead;
  loop.collectOverdubSourceHoldParticipantIds(48, kPitch, atHead);
  TEST_ASSERT_TRUE_MESSAGE(atHead.contains(kId),
                           "Failure A: wrap-held 69@4560→760 missing at hs=48");

  OverlapNoteIdSet resolved;
  loop.resolveOccupancyAtNoteOn(1, kPitch, 100, overlapIds({kId}), 48, &resolved);
  TEST_ASSERT_TRUE(resolved.contains(kId));
  const PendingNoteChange* shorten = findTransform(loop.pendingNoteChanges(), kId);
  TEST_ASSERT_NOT_NULL_MESSAGE(shorten, "Failure B: row present but occupancy wrote no Shorten");
  TEST_ASSERT_EQUAL(static_cast<int>(PendingNoteChangeKind::Shorten),
                    static_cast<int>(shorten->kind));
  TEST_ASSERT_EQUAL_UINT32(4560u, shorten->startTick);
  TEST_ASSERT_EQUAL_UINT32(47u, shorten->endTick);
}

/// RC5: record pass stays out of session wrap ids; slot stack is two kinds.
void test_record_then_session_wraps_are_two_slot_undo_entries() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  Loop loop;
  seedLongSourceNote(loop, 1, 100, 200, 60, 3072);
  const PassId recordPassId = loop.passes.recordPass.id;
  TEST_ASSERT_NOT_EQUAL(kInvalidPassId, recordPassId);

  loop.openOverdubSession(0);
  loop.beginCapture(CapturePhase::Overdub, 0);
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(noteOnWithNoteId(2304, 1, 64, 90, 85)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(2496, 1, 64, 0)));
  TEST_ASSERT_EQUAL(CommitResult::Committed,
                    loop.commitCapturePass(CommitReason::OverdubWrap, 2496));
  const PassId wrap1 = loop.lastCommittedPassId();
  loop.pushOverdubSessionPass(wrap1, {});
  loop.beginCapture(CapturePhase::Overdub, 0);
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(noteOnWithNoteId(240, 1, 64, 90, 88)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(432, 1, 64, 0)));
  TEST_ASSERT_EQUAL(CommitResult::Committed,
                    loop.commitCapturePass(CommitReason::OverdubWrap, 432));
  const PassId wrap2 = loop.lastCommittedPassId();
  loop.pushOverdubSessionPass(wrap2, {});

  PassIdList sessionIds;
  EditPassIdList companions;
  loop.collectOverdubSessionUndoPasses(sessionIds, companions);
  TEST_ASSERT_EQUAL(2u, sessionIds.size());
  TEST_ASSERT_EQUAL(wrap1, sessionIds[0]);
  TEST_ASSERT_EQUAL(wrap2, sessionIds[1]);
  TEST_ASSERT_NOT_EQUAL(recordPassId, sessionIds[0]);
  TEST_ASSERT_NOT_EQUAL(recordPassId, sessionIds[1]);

  GlobalUndoStack stack;
  UndoEntry record{};
  record.kind = UndoEntryKind::RecordPassAdded;
  record.slotIndex = 0;
  record.passId = recordPassId;
  stack.entries.push_back(record);
  UndoEntry overdub{};
  overdub.kind = UndoEntryKind::OverdubPassAdded;
  overdub.slotIndex = 0;
  overdub.passId = sessionIds.back();
  overdub.passIds = sessionIds;
  stack.entries.push_back(overdub);

  TEST_ASSERT_EQUAL(2u, stack.entries.size());
  TEST_ASSERT_EQUAL(static_cast<int>(UndoEntryKind::RecordPassAdded),
                    static_cast<int>(stack.entries[0].kind));
  TEST_ASSERT_EQUAL(static_cast<int>(UndoEntryKind::OverdubPassAdded),
                    static_cast<int>(stack.entries[1].kind));
  TEST_ASSERT_EQUAL(2u, stack.entries[1].passIds.size());
  TEST_ASSERT_NOT_EQUAL(recordPassId, stack.entries[1].passId);
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
  RUN_TEST(test_consume_attribution_counts_late_note_for_jit_ahead_candidate);
  RUN_TEST(test_consume_attribution_counts_scan_only_for_id_absent_candidate);
  RUN_TEST(test_consume_id_resolution_norow_repaired_by_hold_fill);
  RUN_TEST(test_pending_shorten_long_source_on_overlap);
  RUN_TEST(test_pending_shorten_ignores_recorded_channel);
  RUN_TEST(test_pending_hide_when_covered);
  RUN_TEST(test_pending_hide_same_start_shorter_hides_longer);
  RUN_TEST(test_occupy_shorter_note_off_seals_length_not_delete);
  RUN_TEST(test_pending_hide_same_start_longer);
  RUN_TEST(test_pending_hide_applies_to_display_notes_not_source_view);
  RUN_TEST(test_pending_hide_does_not_mutate_copied_committed_prefix);
  RUN_TEST(test_resolve_occupancy_at_note_on_same_start_hides);
  RUN_TEST(test_pending_hide_applies_when_display_note_id_differs);
  RUN_TEST(test_pending_hide_keeps_live_capture_tail);
  RUN_TEST(test_upsert_occupied_hides_empty_ledger_uses_hold_participants);
  RUN_TEST(test_upsert_occupied_hides_norowid_falls_back_to_hold_participants);
  RUN_TEST(test_occupancy_ledger_id_not_present_at_hold_does_not_transform_134436);
  RUN_TEST(test_recount_overlap_hold_totals_follows_pending_after_seal);
  RUN_TEST(test_occupancy_hide_seals_as_delete);
  RUN_TEST(test_occupancy_five_sibling_convergence);
  RUN_TEST(test_occupancy_ordering_equivalence);
  RUN_TEST(test_occupancy_three_note_nesting);
  RUN_TEST(test_occupancy_composition_hide_absorbs_shorten);
  RUN_TEST(test_occupancy_composition_shorten_keeps_earlier_end);
  RUN_TEST(test_occupancy_composition_shorten_below_min_length_hides);
  RUN_TEST(test_occupancy_wrap_split_keeps_most_restrictive);
  RUN_TEST(test_occupancy_add_then_shorten_composes_in_source_view);
  RUN_TEST(test_retrigger_pending_note_slot_replaces_first);
  RUN_TEST(test_occupancy_undo_restores_original_geometry);
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
  RUN_TEST(test_121141_three_point_resolver_seal_rebuild_handoff);
  RUN_TEST(test_seal_pending_shorten_to_edit_pass_after_overdub_publish);
  RUN_TEST(test_prepared_present_note_ids_match_source_hold_participants);
  RUN_TEST(test_prepared_present_note_ids_miss_does_not_fill_source_window);
  RUN_TEST(test_prepared_present_note_ids_filters_pitch_and_exclusive_end);
  RUN_TEST(test_note_on_occupy_reads_ledger_note_id);
  RUN_TEST(test_note_on_occupy_empty_when_ledger_inactive);
  RUN_TEST(test_wrap_committed_note_at_s_occupies_ledger_same_tick);
  RUN_TEST(test_wrap_pass_events_at_s_occupy_without_merged_rebuild);
  RUN_TEST(test_wrap_tick_occupy_without_full_rebuild_and_no_duplicate_after_later_rebuild);
  RUN_TEST(test_incremental_overdub_seal_admits_wrap_pass_after_s_without_full_gather);
  RUN_TEST(test_incremental_overdub_seal_applies_companion_shorten);
  RUN_TEST(test_overdub_stop_reanchor_preserves_merged_then_admits_seal);
  RUN_TEST(test_overdub_stop_shaped_receipt_admits_without_duplication);
  RUN_TEST(test_incremental_overdub_seal_provenance_miss_falls_back);
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
  RUN_TEST(test_occupy_clock_duplicate_off_closes_both_pre_abut_103234);
  RUN_TEST(test_occupy_leftover_off_at_exclusive_end_closes_wrap_head_not_ghost_104654);
  RUN_TEST(test_erase_open_notes_missing_from_committed_note_ons_drops_5701_keeps_5772);
  RUN_TEST(test_occupy_source_hold_drops_span_without_merged_note_on);
  RUN_TEST(test_occupy_source_hold_retains_span_with_merged_note_on);
  RUN_TEST(test_occupy_source_hold_partial_window_does_not_drop_span);
  RUN_TEST(test_occupy_source_hold_stale_revision_does_not_drop_span);
  RUN_TEST(test_occupy_ledger_catchup_same_tick_skip_when_occupy_equals_last_tick);
  RUN_TEST(test_occupy_ledger_catchup_skips_wrap_crossing);
  RUN_TEST(test_seal_companion_ids_project_hide_by_note_id);
  RUN_TEST(test_companion_overlay_ignores_mismatched_note_id);
  RUN_TEST(test_companion_overlay_hide_is_idempotent);
  RUN_TEST(test_seal_revision_implies_cumulative_companion_coverage);
  RUN_TEST(test_sealed_hide_keeps_short_overdub_after_snapshot_current);
  RUN_TEST(test_sealed_companion_overlay_retains_prior_wrap_hide);
  RUN_TEST(test_sealed_hide_drops_same_start_longer_when_note_id_differs);
  RUN_TEST(test_sealed_shorten_drops_same_start_longer_sibling);
  RUN_TEST(test_pending_shorten_drops_same_start_longer_sibling);
  RUN_TEST(test_pending_shorten_172734_open_hold_eligibility_matrix);
  RUN_TEST(test_pending_shorten_193815_63_1536_hold_1919_matrix);
  RUN_TEST(test_draw_source_compose_193815_63_1536_forks);
  RUN_TEST(test_noteon_shorten_paint_keeps_compose_original_end_215658);
  RUN_TEST(test_noteon_pending_paint_copy_shortens_before_stop_202821);
  RUN_TEST(test_pending_shorten_recovers_authoritative_source_212700);
  RUN_TEST(test_wrap_noteon_shorten_recovers_without_visual_identity_223435);
  RUN_TEST(test_pending_shorten_authoritative_absence_does_not_resurrect_212700);
  RUN_TEST(test_pending_hide_does_not_recover_dropped_source_row);
  RUN_TEST(test_pending_shorten_drops_suffix_longer_remnant_000402);
  RUN_TEST(test_later_occupy_shorten_drops_suffix_remnant_000402);
  RUN_TEST(test_same_start_hide_drops_closed_remnant_keeps_open_hold_001145);
  RUN_TEST(test_same_start_hide_drops_remnant_when_hold_present_001145);
  RUN_TEST(test_sealed_overlay_leaves_unresolved_bar_two_note);
  RUN_TEST(test_sealed_overlay_survives_admit_receipt_clear);
  RUN_TEST(test_wrap_journal_keeps_hide_when_parent_lacks_note);
  RUN_TEST(test_linear_bar_four_source_row_is_absent_at_hold_zero_and_forty_eight);
  RUN_TEST(test_wrap_held_source_row_occupies_hold_zero);
  RUN_TEST(test_wrap_seal_keeps_painted_shorten_233150);
  RUN_TEST(test_wrap_seal_keeps_later_painted_shorten_235546);
  RUN_TEST(test_wrap_held_record_row_occupies_head_225200);
  RUN_TEST(test_record_then_session_wraps_are_two_slot_undo_entries);
  return UNITY_END();
}
