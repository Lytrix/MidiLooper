//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include <unity.h>

#include "../../src/Logger.cpp"
#include "../../src/Utils/NoteUtils.cpp"
#include "../../src/Utils/LoopTickNormalize.cpp"
#include "../../src/Utils/LoopEventValidation.cpp"
#include "../test_support/LoopCaptureTestDeps.cpp"
#include "../../src/NoteEditFocus.cpp"
#include "../../src/EditApply.cpp"
#include "../../src/LoopPasses.cpp"
#include "../../src/LoopEventStore.cpp"
#include "../test_support/MemoryMonitorNativeDeps.cpp"
#include "../../src/Loop.cpp"

#include "../test_support/CommittedChunkIdTestHelpers.h"
#include "EditApply.h"
#include "EditPass.h"
#include "LoopPasses.h"
#include "Loop.h"
#include "LoopEventBuffer.h"
#include "MidiEvent.h"
#include "../test_support/NoteIdTestFixtures.h"
#include "MidiEvent.h"
#include "NoteEditSessionState.h"
#include "Utils/IntervalProjection.h"
#include "Utils/NoteEditDisplaySnapshot.h"
#include "Utils/NoteMovementWrap.h"
#include "Utils/LoopEventValidation.h"
#include "Utils/LoopTickNormalize.h"

namespace {

using namespace NoteIdTestFixtures;

MidiEventVec makeTwoNoteFlat(uint32_t startA, uint32_t endA, uint32_t startB, uint32_t endB,
                           uint8_t pitch) {
  MidiEventVec flat;
  flat.push_back(noteOnWithNoteId(startA, 1, pitch, 100, 1));
  flat.push_back(MidiEvent::NoteOff(endA, 1, pitch, 0));
  flat.push_back(noteOnWithNoteId(startB, 1, pitch, 100, 2));
  flat.push_back(MidiEvent::NoteOff(endB, 1, pitch, 0));
  return flat;
}

void runNoteEditMacroCommitNormalize(MidiEventVec& session, NoteEditFocus& focus, uint8_t channel,
                                     uint32_t loopLength) {
  pruneOverlapNotesBeforePreCommit(focus, session, channel);
  resolveOverlapNotesForPreCommit(session, focus, channel, loopLength);
  const std::unordered_set<NoteId> closure =
      buildEditClosureNoteIds(focus, session, channel, loopLength);
  if (!closure.empty()) {
    LoopTickNormalize::NormalizeOptions microOptions;
    microOptions.closeOpenTails = false;
    LoopTickNormalize::normalize(session, loopLength,
                                 LoopTickNormalize::NormalizeScope::noteIds(closure),
                                 microOptions);
    const MidiEventVec closureEvents =
        LoopEventValidation::extractEventsForNoteIds(session, closure);
    const auto microResult = LoopEventValidation::validateLoopEvents(
        closureEvents, loopLength, LoopEventValidation::kClosureLinearGeometryMask);
    TEST_ASSERT_TRUE_MESSAGE(microResult.passed, "closure linear geometry after micro normalize");
  }
  LoopTickNormalize::normalizeAll(session, loopLength);
  const auto macroResult = LoopEventValidation::validateLoopEvents(
      session, loopLength, LoopEventValidation::kCanonicalInvariantMask);
  TEST_ASSERT_TRUE_MESSAGE(macroResult.passed, "canonical invariants after macro normalize");
}

}  // namespace

void test_baseline_map_includes_moving_note_at_select() {
  NoteEditFocus focus;
  const MidiEventVec flat = makeTwoNoteFlat(8, 104, 584, 680, 60);
  rebuildNoteEditFocusFromStore(focus, flat, 1, 768, 0);

  TEST_ASSERT_TRUE(focus.active);
  TEST_ASSERT_EQUAL(1, static_cast<int>(focus.baselineMap.size()));
  TEST_ASSERT_TRUE(focus.baselineMap.count(focus.movingNoteId) > 0);
  TEST_ASSERT_EQUAL_UINT32(8, focus.commitBaseline.startTick);
  TEST_ASSERT_EQUAL_UINT32(104, focus.commitBaseline.endTick);
  TEST_ASSERT_EQUAL_UINT8(60, focus.commitBaseline.pitch);
  TEST_ASSERT_EQUAL_UINT32(8, focus.movingNoteRange.start);
  TEST_ASSERT_EQUAL_UINT32(104, focus.movingNoteRange.end);
  TEST_ASSERT_EQUAL(0, static_cast<int>(focus.overlapNotes.size()));
}

void test_populate_baseline_map_for_edit_closure_wrap_sibling() {
  NoteEditFocus focus;
  const MidiEventVec flat = makeTwoNoteFlat(8, 104, 584, 680, 60);
  rebuildNoteEditFocusFromStore(focus, flat, 1, 768, 0);
  TEST_ASSERT_EQUAL(1, static_cast<int>(focus.baselineMap.size()));

  populateBaselineMapForEditClosure(focus, flat, flat, 1, 768);
  TEST_ASSERT_EQUAL(2, static_cast<int>(focus.baselineMap.size()));
  TEST_ASSERT_TRUE(focus.baselineMap.count(1) > 0);
  TEST_ASSERT_TRUE(focus.baselineMap.count(2) > 0);
}

void test_a1_length_updates_moving_note_range_not_commit_baseline() {
  NoteEditFocus focus;
  focus.active = true;
  focus.commitBaseline = {60, 64, 100, 200};
  focus.movingNoteRange = {100, 200};
  focus.last = focus.commitBaseline;

  noteEditFocusApplyLengthEnd(focus, 300);

  TEST_ASSERT_EQUAL_UINT32(200, focus.commitBaseline.endTick);
  TEST_ASSERT_EQUAL_UINT32(300, focus.movingNoteRange.end);
  TEST_ASSERT_EQUAL_UINT32(300, focus.last.endTick);
  TEST_ASSERT_TRUE(noteEditFocusHasPendingLengthChange(focus));
}

void test_a1_no_pending_length_when_moving_note_range_matches_baseline() {
  NoteEditFocus focus;
  focus.active = true;
  focus.commitBaseline = {60, 64, 100, 200};
  focus.movingNoteRange = {100, 200};
  focus.last = focus.commitBaseline;

  TEST_ASSERT_FALSE(noteEditFocusHasPendingLengthChange(focus));
}

void test_note_edit_focus_has_pending_commit_geometry_and_overlap() {
  NoteEditFocus focus;
  focus.active = true;
  focus.commitBaseline = {60, 64, 100, 200};
  focus.last = focus.commitBaseline;
  TEST_ASSERT_FALSE(noteEditFocusHasPendingCommit(focus));

  focus.last.endTick = 300;
  TEST_ASSERT_TRUE(noteEditFocusHasPendingCommit(focus));
  focus.last = focus.commitBaseline;
  TEST_ASSERT_FALSE(noteEditFocusHasPendingCommit(focus));

  OverlapNote hidden{};
  hidden.noteId = 42;
  hidden.state = OverlapNoteStoreState::Hidden;
  hidden.preCommitEmitted = false;
  focus.overlapNotes[42] = hidden;
  TEST_ASSERT_TRUE(noteEditFocusHasPendingCommit(focus));

  hidden.preCommitEmitted = true;
  focus.overlapNotes[42] = hidden;
  TEST_ASSERT_FALSE(noteEditFocusHasPendingCommit(focus));

  OverlapNote shortened{};
  shortened.noteId = 43;
  shortened.state = OverlapNoteStoreState::Shortened;
  shortened.baseline.endTick = 200;
  shortened.shortenedEndTick = 150;
  focus.overlapNotes[43] = shortened;
  TEST_ASSERT_TRUE(noteEditFocusHasPendingCommit(focus));
}

void test_can_apply_simple_pitch_change_without_lane_collision() {
  constexpr uint32_t kLoopLength = 1536;
  NoteEditFocus focus;
  focus.active = true;
  focus.movingNoteId = 1;
  focus.commitBaseline = {60, 64, 100, 200};
  focus.last = focus.commitBaseline;
  focus.movingNoteRange = {100, 200};

  MidiEventVec events;
  events.push_back(noteOnWithNoteId(100, 1, 60, 100, 1));
  events.push_back(MidiEvent::NoteOff(200, 1, 60, 0));
  events.push_back(noteOnWithNoteId(400, 1, 64, 100, 2));
  events.push_back(MidiEvent::NoteOff(500, 1, 64, 0));

  TEST_ASSERT_TRUE(canApplySimplePitchChange(
      events, focus, 1, 60, 67, focus.last.startTick, focus.last.endTick, kLoopLength));
  TEST_ASSERT_TRUE(canApplySimplePitchChange(
      events, focus, 1, 60, 64, focus.last.startTick, focus.last.endTick, kLoopLength));

  events.push_back(noteOnWithNoteId(150, 1, 64, 100, 4));
  events.push_back(MidiEvent::NoteOff(250, 1, 64, 0));
  TEST_ASSERT_FALSE(canApplySimplePitchChange(
      events, focus, 1, 60, 64, focus.last.startTick, focus.last.endTick, kLoopLength));

  events.pop_back();
  events.pop_back();
  events.push_back(noteOnWithNoteId(200, 1, 67, 100, 3));
  events.push_back(MidiEvent::NoteOff(300, 1, 67, 0));
  TEST_ASSERT_FALSE(canApplySimplePitchChange(
      events, focus, 1, 60, 67, focus.last.startTick, focus.last.endTick, kLoopLength));
}

void test_can_apply_simple_pitch_change_blocks_inner_overlap_on_target_lane() {
  constexpr uint32_t kLoopLength = 3072;
  NoteEditFocus focus;
  focus.active = true;
  focus.movingNoteId = 1;
  focus.commitBaseline = {12, 64, 26, 218};
  focus.last = focus.commitBaseline;
  focus.last.pitch = 60;
  focus.movingNoteRange = {26, 218};

  MidiEventVec events;
  events.push_back(noteOnWithNoteId(26, 1, 60, 100, 1));
  events.push_back(MidiEvent::NoteOff(218, 1, 60, 0));
  events.push_back(noteOnWithNoteId(26, 1, 59, 100, 2));
  events.push_back(MidiEvent::NoteOff(144, 1, 59, 0));

  TEST_ASSERT_FALSE(canApplySimplePitchChange(
      events, focus, 1, 60, 59, focus.last.startTick, focus.last.endTick, kLoopLength));
}

void test_inner_overlap_note_in_moving_note_range() {
  NoteEditFocus focus;
  focus.active = true;
  focus.commitBaseline = {60, 64, 496, 1168};
  focus.movingNoteRange = {496, 1168};
  focus.last = focus.commitBaseline;

  TEST_ASSERT_TRUE(isInnerOverlapNoteInMovingNoteRange(focus, 67, 520, 600, 1536));
  TEST_ASSERT_FALSE(isInnerOverlapNoteInMovingNoteRange(focus, 67, 403, 496, 1536));
  TEST_ASSERT_FALSE(isInnerOverlapNoteInMovingNoteRange(focus, 60, 592, 688, 1536));
}

void test_overlap_note_effective_end_shortened_vs_hidden() {
  OverlapNote shortened{};
  shortened.state = OverlapNoteStoreState::Shortened;
  shortened.baseline.endTick = 688;
  shortened.shortenedEndTick = 495;
  TEST_ASSERT_EQUAL_UINT32(495, overlapNoteEffectiveEnd(shortened));

  OverlapNote hidden{};
  hidden.state = OverlapNoteStoreState::Hidden;
  hidden.baseline.endTick = 688;
  TEST_ASSERT_EQUAL_UINT32(688, overlapNoteEffectiveEnd(hidden));
}

void test_shorten_under_49_ticks_classifies_as_hidden_candidate() {
  const uint32_t loopLength = 1536;
  const uint32_t moverStart = 410;
  const uint32_t neighborStart = 400;
  const uint32_t shortenedEnd = moverStart - 1;
  const uint32_t shortenedLength =
      NoteMovementUtils::calculateNoteLength(neighborStart, shortenedEnd, loopLength);
  TEST_ASSERT_TRUE(shortenedLength < 49);
}

void test_pre_commit_edit_change_order_delete_shorten_move_length_pitch() {
  NoteEditFocus focus;
  focus.active = true;
  focus.commitBaseline = {60, 64, 496, 1168};
  focus.last = {60, 64, 520, 1200};
  focus.movingNoteId = 1;

  constexpr NoteId overlapHiddenId = 10;
  constexpr NoteId overlapShortId = 11;
  OverlapNote hiddenEntry{};
  hiddenEntry.noteId = overlapHiddenId;
  hiddenEntry.baseline = {60, 64, 400, 688};
  hiddenEntry.state = OverlapNoteStoreState::Hidden;
  focus.overlapNotes[overlapHiddenId] = hiddenEntry;

  OverlapNote shortenedEntry{};
  shortenedEntry.noteId = overlapShortId;
  shortenedEntry.baseline = {60, 64, 592, 688};
  shortenedEntry.state = OverlapNoteStoreState::Shortened;
  shortenedEntry.shortenedEndTick = 495;
  focus.overlapNotes[overlapShortId] = shortenedEntry;

  const EditPassVec rows = buildPreCommitEditPasses(focus, 1);
  TEST_ASSERT_EQUAL(3, static_cast<int>(rows.size()));
  TEST_ASSERT_EQUAL(static_cast<int>(EditActionType::Delete),
                    static_cast<int>(rows[0].actionType));
  TEST_ASSERT_TRUE(rows[0].targetNoteId == overlapHiddenId);
  TEST_ASSERT_EQUAL(static_cast<int>(EditActionType::Update),
                    static_cast<int>(rows[1].actionType));
  TEST_ASSERT_EQUAL(static_cast<int>(EditPropertyType::Length),
                    static_cast<int>(rows[1].propertyType));
  TEST_ASSERT_TRUE(rows[1].targetNoteId == overlapShortId);
  TEST_ASSERT_EQUAL_UINT32(495, rows[1].endTick);
  TEST_ASSERT_EQUAL(static_cast<int>(EditPropertyType::NoteRange),
                    static_cast<int>(rows[2].propertyType));
  TEST_ASSERT_EQUAL_UINT32(520, rows[2].startTick);
  TEST_ASSERT_EQUAL_UINT32(1200, rows[2].endTick);
}

void test_pre_commit_store_diff_subset_mover_and_overlap_notes() {
  const uint32_t loopLength = 1536;
  NoteEditFocus focus;
  MidiEventVec flat;
  flat.push_back(MidiEvent::NoteOn(100, 1, 67, 100));
  flat.push_back(MidiEvent::NoteOff(200, 1, 67, 0));
  flat.push_back(MidiEvent::NoteOn(400, 1, 60, 100));
  flat.push_back(MidiEvent::NoteOff(688, 1, 60, 0));
  flat.push_back(MidiEvent::NoteOn(496, 1, 60, 100));
  flat.push_back(MidiEvent::NoteOff(1200, 1, 60, 0));

  rebuildNoteEditFocusFromStore(focus, flat, 1, loopLength, 2);
  focus.last.endTick = 1200;
  focus.movingNoteRange.end = 1200;

  constexpr NoteId overlapShortId = 10;
  OverlapNote shortenedEntry{};
  shortenedEntry.noteId = overlapShortId;
  shortenedEntry.baseline = {60, 64, 400, 688};
  shortenedEntry.state = OverlapNoteStoreState::Shortened;
  shortenedEntry.shortenedEndTick = 495;
  focus.overlapNotes[overlapShortId] = shortenedEntry;

  constexpr NoteId overlapHiddenId = 11;
  OverlapNote hiddenEntry{};
  hiddenEntry.noteId = overlapHiddenId;
  hiddenEntry.baseline = {60, 64, 592, 688};
  hiddenEntry.state = OverlapNoteStoreState::Hidden;
  focus.overlapNotes[overlapHiddenId] = hiddenEntry;

  flat.push_back(MidiEvent::NoteOn(592, 1, 60, 100));
  flat.push_back(MidiEvent::NoteOff(688, 1, 60, 0));

  resolveOverlapNotesForPreCommit(flat, focus, 1, loopLength);

  const std::vector<NoteUtils::DisplayNote> resolved =
      NoteUtils::reconstructNotes(flat, loopLength, false);

  for (const NoteUtils::DisplayNote& dn : resolved) {
    NoteId matchedBaselineNoteId = kInvalidNoteId;
    const NoteBaseline* baseline = nullptr;
    for (const auto& [baselineNoteId, bl] : focus.baselineMap) {
      if (dn.note == bl.pitch && dn.startTick == bl.startTick) {
        matchedBaselineNoteId = baselineNoteId;
        baseline = &bl;
        break;
      }
    }
    if (baseline == nullptr) {
      continue;
    }
    if (dn.endTick == baseline->endTick) {
      continue;
    }
    bool allowed = (matchedBaselineNoteId == focus.movingNoteId);
    if (!allowed) {
      for (const auto& [overlapNoteId, entry] : focus.overlapNotes) {
        (void)entry;
        (void)overlapNoteId;
        if (overlapNoteId == matchedBaselineNoteId) {
          allowed = true;
          break;
        }
      }
    }
    TEST_ASSERT_TRUE_MESSAGE(allowed, "store diff outside mover + overlapNotes");
  }

  bool foundShortened = false;
  for (const NoteUtils::DisplayNote& dn : resolved) {
    if (dn.note == 60 && dn.startTick == 400 && dn.endTick == 495) {
      foundShortened = true;
    }
    if (dn.note == 60 && dn.startTick == 592) {
      TEST_FAIL_MESSAGE("hidden overlap note still in store after pre-commit resolve");
    }
  }
  TEST_ASSERT_TRUE(foundShortened);
}

void test_build_pre_commit_changes_replay_lengthen_delete_pitch() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  constexpr uint32_t kLoopLength = 1536;

  NoteEditFocus focus;
  focus.active = true;
  focus.commitBaseline = {60, 64, 8, 680};
  focus.last = {67, 64, 8, 680};
  focus.movingNoteId = 1;
  focus.movingNoteRange = {8, 680};

  constexpr NoteId overlapHiddenId = 12;
  OverlapNote hidden{};
  hidden.noteId = overlapHiddenId;
  hidden.baseline = {60, 64, 584, 680};
  hidden.state = OverlapNoteStoreState::Hidden;
  focus.overlapNotes[overlapHiddenId] = hidden;

  const EditPassVec rows = buildPreCommitEditPasses(focus, 1);
  TEST_ASSERT_EQUAL(2, static_cast<int>(rows.size()));

  LoopEventStore store;
  resetNoteIdCounter();
  storeAppendNoteOn(store, 8, 1, 60, 100, 1);
  store.append(MidiEvent::NoteOff(104, 1, 60, 0));
  storeAppendNoteOn(store, 392, 1, 67, 100, 2);
  store.append(MidiEvent::NoteOff(488, 1, 67, 0));
  storeAppendNoteOn(store, 584, 1, 60, 100, 3);
  store.append(MidiEvent::NoteOff(680, 1, 60, 0));
  CommittedChunkIdList publishedIds;
  TEST_ASSERT_TRUE(transferCaptureStoreToCommittedChunkIds(store, publishedIds));
  RecordPass record{};
  record.id = 1;
  record.state = CapturePassState::Active;
  record.committedChunkIds = std::move(publishedIds);
  LoopPasses passes;
  passes.recordPass = std::move(record);

  EditPass pre{};
  pre.id = 1;
  pre.passType = EditPassType::Note;
  pre.actionType = EditActionType::Update;
  pre.propertyType = EditPropertyType::Length;
  pre.state = EditPassState::Active;
  pre.targetNoteId = 1;
  pre.startTick = 8;
  pre.endTick = 680;
  passes.editPasses.push_back(pre);
  EditPassId nextId = 2;
  for (const EditPass& row : rows) {
    EditPass post = row;
    post.id = nextId++;
    post.passType = EditPassType::Note;
    post.state = EditPassState::Active;
    passes.editPasses.push_back(post);
  }

  MidiEventVec flat;
  passes.materializeToEventVector(flat, kLoopLength);

  const std::vector<NoteUtils::DisplayNote> notes =
      NoteUtils::reconstructNotes(flat, kLoopLength, false);
  bool foundHome = false;
  for (const NoteUtils::DisplayNote& dn : notes) {
    if (dn.note == 67 && dn.startTick == 8) {
      foundHome = true;
      TEST_ASSERT_TRUE(dn.endTick >= 680u);
    }
  }
  TEST_ASSERT_TRUE(foundHome);
}

void test_reselect_keeps_commit_baseline_with_pending_length() {
  constexpr uint32_t kLoopLength = 1536;
  LoopPasses passes;
  LoopEventStore store;
  resetNoteIdCounter();
  TEST_ASSERT_TRUE(storeAppendNoteOn(store, 8, 5, 60, 100, 1));
  TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOff(104, 5, 60, 0)));
  TEST_ASSERT_TRUE(storeAppendNoteOn(store, 585, 5, 60, 100, 2));
  TEST_ASSERT_TRUE(store.append(MidiEvent::NoteOff(680, 5, 60, 0)));
  CommittedChunkIdList publishedIds;
  TEST_ASSERT_TRUE(transferCaptureStoreToCommittedChunkIds(store, publishedIds));
  RecordPass record{};
  record.id = 1;
  record.state = CapturePassState::Active;
  record.committedChunkIds = std::move(publishedIds);
  passes.recordPass = std::move(record);

  MidiEventVec committed;
  passes.materializeToEventVector(committed, kLoopLength);

  NoteEditFocus focus;
  rebuildNoteEditFocusFromStore(focus, committed, 5, kLoopLength, 0);
  focus.last.endTick = 680;
  focus.movingNoteRange.end = 680;

  TEST_ASSERT_EQUAL(104u, focus.commitBaseline.endTick);
  TEST_ASSERT_EQUAL(680u, focus.last.endTick);

  EditPassVec rows = buildPreCommitEditPasses(focus, 5);
  TEST_ASSERT_EQUAL(1, static_cast<int>(rows.size()));
  TEST_ASSERT_EQUAL(EditPropertyType::Length, rows[0].propertyType);
  TEST_ASSERT_EQUAL(680u, rows[0].endTick);
}

void test_filter_excludes_hidden_overlap_note() {
  constexpr uint32_t kLoopLength = 1536;
  NoteEditFocus focus;
  focus.active = true;
  focus.commitBaseline = {67, 64, 8, 680};
  focus.movingNoteRange = {8, 680};

  MidiEventVec flat;
  flat.push_back(MidiEvent::NoteOn(8, 1, 67, 100));
  flat.push_back(MidiEvent::NoteOff(680, 1, 67, 0));
  flat.push_back(MidiEvent::NoteOn(200, 1, 64, 100));
  flat.push_back(MidiEvent::NoteOff(400, 1, 64, 0));
  flat.push_back(MidiEvent::NoteOn(584, 1, 60, 100));
  flat.push_back(MidiEvent::NoteOff(680, 1, 60, 0));

  rebuildNoteEditFocusFromStore(focus, flat, 1, kLoopLength, 0);

  constexpr NoteId hiddenRefId = 12;
  OverlapNote hidden{};
  hidden.noteId = hiddenRefId;
  hidden.baseline = {60, 64, 584, 680};
  hidden.state = OverlapNoteStoreState::Hidden;
  focus.overlapNotes[hiddenRefId] = hidden;

  const NoteUtils::DisplayNoteVec filtered =
      filterSelectableDisplayNotes(flat, focus, 1, kLoopLength);

  TEST_ASSERT_EQUAL(2, static_cast<int>(filtered.size()));
  for (const NoteUtils::DisplayNote& dn : filtered) {
    TEST_ASSERT_FALSE(dn.note == 60 && dn.startTick == 584);
  }
}

void test_filter_includes_shortened_overlap_note() {
  constexpr uint32_t kLoopLength = 1536;
  NoteEditFocus focus;
  focus.active = true;

  MidiEventVec flat;
  flat.push_back(MidiEvent::NoteOn(496, 1, 60, 100));
  flat.push_back(MidiEvent::NoteOff(1200, 1, 60, 0));
  flat.push_back(MidiEvent::NoteOn(400, 1, 60, 100));
  flat.push_back(MidiEvent::NoteOff(495, 1, 60, 0));

  rebuildNoteEditFocusFromStore(focus, flat, 1, kLoopLength, 0);

  constexpr NoteId shortenedRefId = 10;
  OverlapNote shortened{};
  shortened.noteId = shortenedRefId;
  shortened.baseline = {60, 64, 400, 688};
  shortened.state = OverlapNoteStoreState::Shortened;
  shortened.shortenedEndTick = 495;
  focus.overlapNotes[shortenedRefId] = shortened;

  const NoteUtils::DisplayNoteVec filtered =
      filterSelectableDisplayNotes(flat, focus, 1, kLoopLength);

  bool foundShortened = false;
  for (const NoteUtils::DisplayNote& dn : filtered) {
    if (dn.note == 60 && dn.startTick == 400 && dn.endTick == 495) {
      foundShortened = true;
    }
  }
  TEST_ASSERT_TRUE(foundShortened);
}

void test_filter_excludes_inner_under_moving_note() {
  constexpr uint32_t kLoopLength = 1536;
  NoteEditFocus focus;
  focus.active = true;
  focus.commitBaseline = {67, 64, 8, 680};
  focus.movingNoteRange = {8, 680};

  MidiEventVec flat;
  flat.push_back(MidiEvent::NoteOn(8, 1, 67, 100));
  flat.push_back(MidiEvent::NoteOff(680, 1, 67, 0));
  flat.push_back(MidiEvent::NoteOn(392, 1, 60, 100));
  flat.push_back(MidiEvent::NoteOff(488, 1, 60, 0));
  flat.push_back(MidiEvent::NoteOn(584, 1, 60, 100));
  flat.push_back(MidiEvent::NoteOff(680, 1, 60, 0));

  rebuildNoteEditFocusFromStore(focus, flat, 1, kLoopLength, 0);

  constexpr NoteId innerRefId = 12;
  OverlapNote inner{};
  inner.noteId = innerRefId;
  inner.baseline = {60, 64, 584, 680};
  inner.state = OverlapNoteStoreState::Visible;
  inner.innerUnderMovingNote = true;
  focus.overlapNotes[innerRefId] = inner;

  const NoteUtils::DisplayNoteVec filtered =
      filterSelectableDisplayNotes(flat, focus, 1, kLoopLength);

  TEST_ASSERT_EQUAL(2, static_cast<int>(filtered.size()));
  for (const NoteUtils::DisplayNote& dn : filtered) {
    TEST_ASSERT_FALSE(dn.note == 60 && dn.startTick == 584);
  }
}

void test_filter_includes_moving_note_when_hidden_overlap_baseline_matches() {
  // session_20260714_012932.log: mover at 576-624 after absorbing restored neighbor at same span.
  constexpr uint32_t kLoopLength = 2304;
  constexpr uint8_t channel = 1;
  constexpr NoteId kMoverId = 3;
  constexpr NoteId kHiddenId = 4;
  resetNoteIdCounter();

  NoteEditFocus focus;
  MidiEventVec flat;
  flat.push_back(noteOnWithNoteId(576, channel, 60, 100, kMoverId));
  flat.push_back(MidiEvent::NoteOff(624, channel, 60, 0));

  rebuildNoteEditFocusFromStore(focus, flat, channel, kLoopLength, 0);
  TEST_ASSERT_EQUAL(kMoverId, focus.movingNoteId);

  OverlapNote hidden{};
  hidden.noteId = kHiddenId;
  hidden.baseline = {60, 100, 576, 624};
  hidden.state = OverlapNoteStoreState::Hidden;
  focus.overlapNotes[kHiddenId] = hidden;
  focus.last = {60, 100, 576, 624};

  const NoteUtils::DisplayNoteVec filtered =
      filterSelectableDisplayNotes(flat, focus, channel, kLoopLength);

  TEST_ASSERT_EQUAL(1, static_cast<int>(filtered.size()));
  TEST_ASSERT_EQUAL(kMoverId, filtered[0].noteId);
  TEST_ASSERT_EQUAL_UINT32(576u, filtered[0].startTick);
  TEST_ASSERT_EQUAL_UINT32(624u, filtered[0].endTick);
}

void test_filtered_display_note_index_for_note_ref() {
  constexpr uint32_t kLoopLength = 1536;
  NoteEditFocus focus;
  const MidiEventVec flat = makeTwoNoteFlat(8, 104, 584, 680, 60);
  rebuildNoteEditFocusFromStore(focus, flat, 1, kLoopLength, 0);

  const NoteUtils::DisplayNoteVec filtered =
      filterSelectableDisplayNotes(flat, focus, 1, kLoopLength);
  TEST_ASSERT_EQUAL(2, static_cast<int>(filtered.size()));

  const NoteId secondId = noteIdFromFilteredDisplayNote(filtered, 1);
  TEST_ASSERT_EQUAL(2u, secondId);

  const int idx = filteredDisplayNoteIndexForNoteId(filtered, secondId);
  TEST_ASSERT_EQUAL(1, idx);
}

void test_sync_linear_focus_avoids_spurious_display_length_commit() {
  constexpr NoteId kNoteId = 100;
  NoteEditFocus focus;
  focus.active = true;
  focus.movingNoteId = kNoteId;
  focus.commitBaseline = {100, 100, 1454, 1535};
  focus.last = {100, 100, 1454, 43};
  focus.movingNoteRange = {1454, 43};

  MidiEventVec session;
  MidiEvent on = MidiEvent::NoteOn(1454, 1, 100, 100);
  on.noteId = kNoteId;
  session.push_back(on);
  session.push_back(MidiEvent::NoteOff(1681, 1, 100, 0));

  TEST_ASSERT_TRUE(syncNoteEditFocusLinearFromSessionStore(focus, session, 1));
  TEST_ASSERT_EQUAL_UINT32(1454u, focus.last.startTick);
  TEST_ASSERT_EQUAL_UINT32(1681u, focus.last.endTick);

  const EditPassVec rows = buildPreCommitEditPasses(focus, 1);
  for (const EditPass& row : rows) {
    TEST_ASSERT_FALSE(row.propertyType == EditPropertyType::Length && row.endTick == 43u);
  }
  TEST_ASSERT_EQUAL(1, static_cast<int>(rows.size()));
  TEST_ASSERT_EQUAL(EditPropertyType::Length, rows[0].propertyType);
  TEST_ASSERT_EQUAL_UINT32(1681u, rows[0].endTick);
}

void test_prune_overlap_shortened_display_baseline_artifact() {
  constexpr NoteId kOverlapId = 79;
  NoteEditFocus focus;
  focus.active = true;

  MidiEventVec session;
  MidiEvent on = MidiEvent::NoteOn(1490, 1, 79, 100);
  on.noteId = kOverlapId;
  session.push_back(on);
  session.push_back(MidiEvent::NoteOff(1536, 1, 79, 0));

  OverlapNote entry;
  entry.noteId = kOverlapId;
  entry.baseline = {79, 100, 1490, 0};
  entry.state = OverlapNoteStoreState::Shortened;
  entry.shortenedEndTick = 1535;
  focus.overlapNotes[kOverlapId] = entry;

  pruneOverlapNotesBeforePreCommit(focus, session, 1);
  TEST_ASSERT_EQUAL(0, static_cast<int>(focus.overlapNotes.size()));

  const EditPassVec rows = buildPreCommitOverlapEditPasses(focus);
  TEST_ASSERT_EQUAL(0, static_cast<int>(rows.size()));
}

void test_filtered_display_note_index_for_note_id_and_start() {
  constexpr NoteId kWrapId = 90;
  std::vector<NoteUtils::DisplayNote> filtered;
  filtered.push_back({kWrapId, 90, 100, 1472, 1535});
  filtered.push_back({kWrapId, 90, 100, 0, 103});

  TEST_ASSERT_EQUAL(0, filteredDisplayNoteIndexForNoteIdAndStart(filtered, kWrapId, 1472u));
  TEST_ASSERT_EQUAL(1, filteredDisplayNoteIndexForNoteIdAndStart(filtered, kWrapId, 0u));
  TEST_ASSERT_EQUAL(0, filteredDisplayNoteIndexForNoteId(filtered, kWrapId));
}

void test_filtered_display_note_index_for_note_id_and_end() {
  constexpr NoteId kNoteId = 42;
  std::vector<NoteUtils::DisplayNote> filtered;
  filtered.push_back({kNoteId, 60, 100, 100, 200});

  TEST_ASSERT_EQUAL(0, filteredDisplayNoteIndexForNoteIdAndEnd(filtered, kNoteId, 200u));
  TEST_ASSERT_EQUAL(-1, filteredDisplayNoteIndexForNoteIdAndEnd(filtered, kNoteId, 100u));
  TEST_ASSERT_EQUAL(-1, filteredDisplayNoteIndexForNoteIdAndStart(filtered, kNoteId, 200u));
}

void test_filtered_display_note_index_for_moving_note_exact_start_only() {
  constexpr NoteId kWrapId = 90;
  std::vector<NoteUtils::DisplayNote> filtered;
  filtered.push_back({kWrapId, 90, 100, 0, 103});
  filtered.push_back({kWrapId, 90, 100, 1472, 1535});

  TEST_ASSERT_EQUAL(1, filteredDisplayNoteIndexForMovingNote(filtered, kWrapId, 1472u));
  TEST_ASSERT_EQUAL(-1, filteredDisplayNoteIndexForMovingNote(filtered, kWrapId, 1400u));
}

void test_filtered_display_note_index_duplicate_pitch_prefers_linear_start() {
  constexpr NoteId kEarlyId = 10;
  constexpr NoteId kMoverId = 32;
  std::vector<NoteUtils::DisplayNote> filtered;
  filtered.push_back({kEarlyId, 32, 100, 24, 47});
  filtered.push_back({13, 13, 100, 73, 190});
  filtered.push_back({kMoverId, 32, 100, 1484, 1535});
  filtered.push_back({kMoverId, 32, 100, 0, 43});

  TEST_ASSERT_EQUAL(2, filteredDisplayNoteIndexForMovingNote(filtered, kMoverId, 1484u));
}

void test_filtered_display_note_index_duplicate_pitch_rejects_mispaired_low_segment() {
  constexpr NoteId kMoverId = 32;
  std::vector<NoteUtils::DisplayNote> filtered;
  filtered.push_back({kMoverId, 32, 100, 24, 47});
  filtered.push_back({kMoverId, 32, 100, 1484, 1535});

  TEST_ASSERT_EQUAL(1, filteredDisplayNoteIndexForMovingNote(filtered, kMoverId, 1484u));
}

void test_filtered_display_note_index_rejects_only_mispaired_low_segment() {
  constexpr NoteId kMoverId = 32;
  std::vector<NoteUtils::DisplayNote> filtered;
  filtered.push_back({kMoverId, 32, 100, 24, 47});

  TEST_ASSERT_EQUAL(-1, filteredDisplayNoteIndexForMovingNote(filtered, kMoverId, 1484u));
}

void test_selection_index_duplicate_pitch_uses_linear_start() {
  constexpr NoteId kMoverId = 32;
  EditorSelection selection;
  selection.primaryNote = kMoverId;
  selection.selectedTick = 1484;
  selection.selectedNotes.push_back(kMoverId);

  std::vector<NoteUtils::DisplayNote> filtered;
  filtered.push_back({10, 32, 100, 24, 47});
  filtered.push_back({kMoverId, 32, 100, 1484, 1535});

  TEST_ASSERT_EQUAL(1, NoteEditDisplaySnapshot::filteredDisplayNoteIndexForSelection(
                              selection, filtered));
}

void test_selection_index_geometry_move_prefers_focus_display_bracket() {
  // Wrapped mover: noteId-only returns low segment; display bracket picks active segment.
  constexpr NoteId kMoverId = 32;
  constexpr uint32_t kLoopLength = 1536;

  std::vector<NoteUtils::DisplayNote> filtered;
  filtered.push_back({kMoverId, 32, 100, 0, 43});
  filtered.push_back({kMoverId, 32, 100, 1484, 1535});

  TEST_ASSERT_EQUAL(0, filteredDisplayNoteIndexForNoteId(filtered, kMoverId));
  TEST_ASSERT_EQUAL(1, filteredDisplayNoteIndexForNoteIdAndStart(
                              filtered, kMoverId, 1484u, 0u, kLoopLength));
}

void test_is_plausible_storage_span_rejects_lifo_mispair() {
  constexpr uint32_t kLoopLength = 1536;
  TEST_ASSERT_TRUE(isPlausibleStorageSpan(387, 436, kLoopLength));
  TEST_ASSERT_FALSE(isPlausibleStorageSpan(387, 1972, kLoopLength));
  TEST_ASSERT_TRUE(isPlausibleStorageSpan(1419, 1613, kLoopLength));
}

void test_find_linear_note_span_rejects_mispaired_off() {
  constexpr uint32_t kLoopLength = 1536;
  constexpr NoteId kNoteId = 56;
  MidiEventVec session;
  MidiEvent on = MidiEvent::NoteOn(387, 1, 56, 100);
  on.noteId = kNoteId;
  session.push_back(on);
  session.push_back(MidiEvent::NoteOff(436, 1, 56, 0));
  session.push_back(MidiEvent::NoteOff(1972, 1, 56, 0));

  NoteBaseline linear{};
  TEST_ASSERT_TRUE(findLinearNoteSpanForNoteId(session, kNoteId, 1, linear, 387, kLoopLength));
  TEST_ASSERT_EQUAL_UINT32(387u, linear.startTick);
  TEST_ASSERT_EQUAL_UINT32(436u, linear.endTick);
}

void test_is_moving_note_overlap_scratch_entry() {
  NoteEditFocus focus;
  focus.active = true;
  focus.movingNoteId = 32;
  focus.commitBaseline = {94, 100, 1424, 1727};
  focus.last = focus.commitBaseline;

  TEST_ASSERT_TRUE(isMovingNoteOverlapScratchEntry(focus, 32, {94, 100, 1424, 191}));
  TEST_ASSERT_TRUE(isMovingNoteOverlapScratchEntry(focus, kInvalidNoteId, {94, 100, 1424, 191}));
  TEST_ASSERT_FALSE(isMovingNoteOverlapScratchEntry(focus, 79, {79, 100, 1490, 1535}));
}

void test_pitch_linear_focus_no_spurious_length_after_sync() {
  constexpr NoteId kNoteId = 32;
  constexpr uint32_t kLoopLength = 1536;
  NoteEditFocus focus;
  focus.active = true;
  focus.movingNoteId = kNoteId;
  focus.commitBaseline = {94, 100, 1424, 1535};
  focus.last = {94, 100, 1424, 1535};
  focus.movingNoteRange = {1424, 1535};

  MidiEventVec session;
  MidiEvent on = MidiEvent::NoteOn(1424, 1, 94, 100);
  on.noteId = kNoteId;
  session.push_back(on);
  session.push_back(MidiEvent::NoteOff(1727, 1, 94, 0));

  TEST_ASSERT_TRUE(syncNoteEditFocusLinearFromSessionStore(focus, session, 1));
  TEST_ASSERT_EQUAL_UINT32(1727u, focus.last.endTick);
  TEST_ASSERT_EQUAL_UINT32(1727u, focus.movingNoteRange.end);

  const EditPassVec rows = buildPreCommitEditPasses(focus, 1);
  TEST_ASSERT_EQUAL(1, static_cast<int>(rows.size()));
  TEST_ASSERT_EQUAL(EditPropertyType::Length, rows[0].propertyType);
  TEST_ASSERT_EQUAL_UINT32(1727u, rows[0].endTick);
}

void test_linear_baseline_for_overlap_restore_rejects_display_wrap_end() {
  constexpr NoteId kOverlapId = 12;
  NoteEditFocus focus;
  focus.baselineMap[kOverlapId] = {12, 100, 1482, 1577};

  OverlapNote entry;
  entry.noteId = kOverlapId;
  entry.baseline = {12, 100, 1482, 41};
  entry.state = OverlapNoteStoreState::Hidden;

  const NoteBaseline linear = linearBaselineForOverlapRestore(focus, entry, nullptr, 1);
  TEST_ASSERT_EQUAL_UINT32(1482u, linear.startTick);
  TEST_ASSERT_EQUAL_UINT32(1577u, linear.endTick);
}

void test_baseline_map_prefers_linear_span_over_wrap_projection() {
  constexpr NoteId kWrapId = 49;
  constexpr uint32_t kLoopLength = 1536;
  MidiEventVec flat;
  MidiEvent on = MidiEvent::NoteOn(49, 1, 49, 100);
  on.noteId = kWrapId;
  flat.push_back(on);
  flat.push_back(MidiEvent::NoteOff(1535, 1, 49, 0));

  NoteEditFocus focus;
  rebuildNoteEditFocusFromStore(focus, flat, 1, kLoopLength, 0);

  const auto it = focus.baselineMap.find(kWrapId);
  TEST_ASSERT_TRUE(it != focus.baselineMap.end());
  TEST_ASSERT_EQUAL_UINT32(49u, it->second.startTick);
  TEST_ASSERT_EQUAL_UINT32(1535u, it->second.endTick);
}

void test_sync_linear_focus_same_pitch_shortened_overlap_does_not_steal_mover_off() {
  constexpr NoteId kOverlapId = 10;
  constexpr NoteId kMoverId = 32;
  NoteEditFocus focus;
  focus.active = true;
  focus.movingNoteId = kMoverId;
  focus.commitBaseline = {96, 100, 1482, 1577};
  focus.last = {95, 100, 1482, 1577};
  focus.movingNoteRange = {1482, 1577};

  MidiEventVec session;
  MidiEvent overlapOn = MidiEvent::NoteOn(1424, 1, 95, 100);
  overlapOn.noteId = kOverlapId;
  session.push_back(overlapOn);
  MidiEvent moverOn = MidiEvent::NoteOn(1482, 1, 95, 100);
  moverOn.noteId = kMoverId;
  session.push_back(moverOn);
  session.push_back(MidiEvent::NoteOff(1481, 1, 95, 0));
  session.push_back(MidiEvent::NoteOff(1577, 1, 95, 0));

  TEST_ASSERT_TRUE(syncNoteEditFocusLinearFromSessionStore(focus, session, 1));
  TEST_ASSERT_EQUAL_UINT32(1482u, focus.last.startTick);
  TEST_ASSERT_EQUAL_UINT32(1577u, focus.last.endTick);
}

void test_find_linear_note_span_wrapped_mover_ignores_in_loop_orphan_off() {
  constexpr NoteId kMoverId = 84;
  constexpr uint32_t kLoopLength = 1536;
  MidiEventVec session;
  MidiEvent strayOff = MidiEvent::NoteOff(1547, 1, 31, 0);
  session.push_back(strayOff);
  MidiEvent moverOn = MidiEvent::NoteOn(1499, 1, 31, 100);
  moverOn.noteId = kMoverId;
  session.push_back(moverOn);
  MidiEvent moverOff = MidiEvent::NoteOff(1595, 1, 31, 0);
  moverOff.noteId = kMoverId;
  session.push_back(moverOff);

  NoteBaseline linear;
  TEST_ASSERT_TRUE(
      findLinearNoteSpanForNoteId(session, kMoverId, 1, linear, 1499, kLoopLength));
  TEST_ASSERT_EQUAL_UINT32(1499u, linear.startTick);
  TEST_ASSERT_EQUAL_UINT32(1595u, linear.endTick);
}

void test_linear_baseline_for_overlap_restore_shortened_keeps_original_end() {
  constexpr NoteId kOverlapId = 58;
  NoteEditFocus focus;
  focus.baselineMap[kOverlapId] = {58, 100, 483, 1370};

  OverlapNote entry;
  entry.noteId = kOverlapId;
  entry.baseline = {58, 100, 483, 1258};
  entry.state = OverlapNoteStoreState::Shortened;
  entry.shortenedEndTick = 1258;

  MidiEventVec session;
  MidiEvent on = MidiEvent::NoteOn(483, 1, 58, 100);
  on.noteId = kOverlapId;
  session.push_back(on);
  session.push_back(MidiEvent::NoteOff(1258, 1, 58, 0));

  const NoteBaseline linear = linearBaselineForOverlapRestore(focus, entry, &session, 1);
  TEST_ASSERT_EQUAL_UINT32(483u, linear.startTick);
  TEST_ASSERT_EQUAL_UINT32(1370u, linear.endTick);
}

void test_linear_baseline_for_overlap_restore_hidden_uses_hide_snapshot_not_session() {
  constexpr NoteId kOverlapId = 31;
  NoteEditFocus focus;
  focus.baselineMap[kOverlapId] = {31, 100, 1451, 1595};

  OverlapNote entry;
  entry.noteId = kOverlapId;
  entry.baseline = {31, 100, 1451, 1547};
  entry.state = OverlapNoteStoreState::Hidden;

  MidiEventVec session;
  MidiEvent moverOn = MidiEvent::NoteOn(1499, 1, 31, 100);
  moverOn.noteId = 84;
  session.push_back(moverOn);
  session.push_back(MidiEvent::NoteOff(1595, 1, 31, 0));

  const NoteBaseline linear = linearBaselineForOverlapRestore(focus, entry, &session, 1);
  TEST_ASSERT_EQUAL_UINT32(1451u, linear.startTick);
  TEST_ASSERT_EQUAL_UINT32(1547u, linear.endTick);
}

void test_find_linear_off_for_note_id_ignores_same_pitch_neighbor_off() {
  constexpr NoteId kMoverId = 84;
  constexpr uint32_t kLoopLength = 1536;
  MidiEventVec session;
  session.push_back(MidiEvent::NoteOff(1547, 1, 31, 0));
  MidiEvent moverOn = MidiEvent::NoteOn(1499, 1, 31, 100);
  moverOn.noteId = kMoverId;
  session.push_back(moverOn);
  MidiEvent moverOff = MidiEvent::NoteOff(1595, 1, 31, 0);
  moverOff.noteId = kMoverId;
  session.push_back(moverOff);

  MidiEvent* off = findLinearOffForNoteId(session, moverOn, kMoverId, kLoopLength);
  TEST_ASSERT_NOT_NULL(off);
  TEST_ASSERT_EQUAL_UINT32(1595u, off->tick);
}

void test_moving_note_linear_span_ignores_neighbor_lifo_off() {
  // session_20260714_023305.log @190.131: restored neighbor 240-336 must not shorten mover 288-432.
  constexpr uint32_t kLoopLength = 2304;
  constexpr NoteId kNeighborId = 2;
  constexpr NoteId kMoverId = 3;
  MidiEventVec session;
  MidiEvent neighborOn = noteOnWithNoteId(240, 1, 60, 100, kNeighborId);
  session.push_back(neighborOn);
  MidiEvent neighborOff = MidiEvent::NoteOff(336, 1, 60, 0);
  neighborOff.noteId = kNeighborId;
  session.push_back(neighborOff);
  session.push_back(noteOnWithNoteId(288, 1, 60, 100, kMoverId));
  MidiEvent moverOff = MidiEvent::NoteOff(432, 1, 60, 0);
  moverOff.noteId = kMoverId;
  session.push_back(moverOff);

  NoteBaseline linear;
  TEST_ASSERT_TRUE(findLinearNoteSpanForNoteId(session, kMoverId, 1, linear, 288, kLoopLength));
  TEST_ASSERT_EQUAL_UINT32(288u, linear.startTick);
  TEST_ASSERT_EQUAL_UINT32(432u, linear.endTick);
  TEST_ASSERT_EQUAL_UINT32(144u, linear.endTick - linear.startTick);
}

void test_resolve_linear_note_span_for_overlap_prefers_baseline_map() {
  constexpr NoteId kOverlapId = 58;
  constexpr uint32_t kLoopLength = 1536;
  NoteEditFocus focus;
  focus.baselineMap[kOverlapId] = {58, 100, 483, 1370};

  MidiEventVec session;
  MidiEvent on = MidiEvent::NoteOn(483, 1, 58, 100);
  on.noteId = kOverlapId;
  session.push_back(on);
  MidiEvent off = MidiEvent::NoteOff(1403, 1, 58, 0);
  off.noteId = kOverlapId;
  session.push_back(off);

  const NoteUtils::DisplayNote dn{kOverlapId, 58, 100, 483, 1403};
  NoteBaseline linear;
  TEST_ASSERT_TRUE(
      resolveLinearNoteSpanForOverlap(focus, session, 1, dn, linear, kLoopLength));
  TEST_ASSERT_EQUAL_UINT32(483u, linear.startTick);
  TEST_ASSERT_EQUAL_UINT32(1370u, linear.endTick);
}

void test_find_linear_note_span_mover_length_ignores_neighbor_off() {
  constexpr NoteId kMoverId = 31;
  constexpr NoteId kNeighborId = 29;
  constexpr uint32_t kLoopLength = 1536;
  MidiEventVec session;
  MidiEvent moverOn = MidiEvent::NoteOn(1310, 1, 31, 100);
  moverOn.noteId = kMoverId;
  session.push_back(moverOn);
  MidiEvent moverOff = MidiEvent::NoteOff(1505, 1, 31, 0);
  moverOff.noteId = kMoverId;
  session.push_back(moverOff);
  MidiEvent neighborOff = MidiEvent::NoteOff(1534, 1, 31, 0);
  neighborOff.noteId = kNeighborId;
  session.push_back(neighborOff);

  NoteBaseline linear;
  TEST_ASSERT_TRUE(
      findLinearNoteSpanForNoteId(session, kMoverId, 1, linear, 1310, kLoopLength));
  TEST_ASSERT_EQUAL_UINT32(1310u, linear.startTick);
  TEST_ASSERT_EQUAL_UINT32(1505u, linear.endTick);
  TEST_ASSERT_EQUAL_UINT32(195u, linear.endTick - linear.startTick);
}

void test_overlap_hide_shorten_canonical_invariants_at_macro_commit() {
  constexpr uint32_t loopLength = 1536;
  resetNoteIdCounter();
  NoteEditFocus focus;
  MidiEventVec session;
  session.push_back(noteOnWithNoteId(100, 1, 67, 100, 1));
  session.push_back(MidiEvent::NoteOff(200, 1, 67, 0));
  session.push_back(noteOnWithNoteId(400, 1, 60, 100, 2));
  session.push_back(MidiEvent::NoteOff(688, 1, 60, 0));
  session.push_back(noteOnWithNoteId(496, 1, 60, 100, 3));
  session.push_back(MidiEvent::NoteOff(1200, 1, 60, 0));

  rebuildNoteEditFocusFromStore(focus, session, 1, loopLength, 2);
  focus.last.endTick = 1200;
  focus.movingNoteRange.end = 1200;

  const NoteId overlapShortId = noteIdForNoteOn(session, 1, 60, 400);
  OverlapNote shortenedEntry{};
  shortenedEntry.noteId = overlapShortId;
  shortenedEntry.baseline = {60, 64, 400, 688};
  shortenedEntry.state = OverlapNoteStoreState::Shortened;
  shortenedEntry.shortenedEndTick = 495;
  focus.overlapNotes[overlapShortId] = shortenedEntry;

  session.push_back(noteOnWithNoteId(592, 1, 60, 100, 4));
  session.push_back(MidiEvent::NoteOff(688, 1, 60, 0));
  const NoteId overlapHiddenId = noteIdForNoteOn(session, 1, 60, 592);
  OverlapNote hiddenEntry{};
  hiddenEntry.noteId = overlapHiddenId;
  hiddenEntry.baseline = {60, 64, 592, 688};
  hiddenEntry.state = OverlapNoteStoreState::Hidden;
  focus.overlapNotes[overlapHiddenId] = hiddenEntry;

  runNoteEditMacroCommitNormalize(session, focus, 1, loopLength);
}

void test_wrap_move_canonical_invariants_at_macro_commit() {
  constexpr uint32_t loopLength = 1536;
  constexpr uint32_t noteLen = 191;
  constexpr NoteId kMoverId = 42;

  MidiEventVec session;
  session.push_back(MidiEvent::NoteOff(50, 1, 60, 0));
  MidiEvent on = MidiEvent::NoteOn(1344, 1, 60, 100);
  on.noteId = kMoverId;
  session.push_back(on);

  NoteEditFocus focus;
  rebuildNoteEditFocusFromStore(focus, session, 1, loopLength, 0);
  focus.last.startTick = 1345;
  focus.last.endTick = NoteMovementUtils::linearStorageOffTickForSpanEnd(1345, noteLen);
  focus.movingNoteRange.start = 1345;
  focus.movingNoteRange.end = focus.last.endTick;

  session[1].tick = 1345;
  session[0].tick = focus.last.endTick;

  runNoteEditMacroCommitNormalize(session, focus, 1, loopLength);

  bool foundLinearOff = false;
  for (const MidiEvent& evt : session) {
    if (evt.isNoteOff() && evt.data.noteData.note == 60 && evt.tick == 1536) {
      foundLinearOff = true;
    }
    if (evt.isNoteOff() && evt.data.noteData.note == 60 && evt.tick == 0) {
      TEST_FAIL_MESSAGE("wrap move left off@0 in store");
    }
  }
  TEST_ASSERT_TRUE(foundLinearOff);
}

void test_contained_hidden_overlap_removed_at_resolve() {
  constexpr uint32_t loopLength = 1536;
  resetNoteIdCounter();
  NoteEditFocus focus;
  MidiEventVec session;
  session.push_back(noteOnWithNoteId(496, 1, 60, 100, 1));
  session.push_back(MidiEvent::NoteOff(1200, 1, 60, 0));
  session.push_back(noteOnWithNoteId(592, 1, 60, 100, 4));
  session.push_back(MidiEvent::NoteOff(688, 1, 60, 0));

  rebuildNoteEditFocusFromStore(focus, session, 1, loopLength, 0);
  const NoteId hiddenId = noteIdForNoteOn(session, 1, 60, 592);
  OverlapNote hidden{};
  hidden.noteId = hiddenId;
  hidden.baseline = {60, 64, 592, 688};
  hidden.state = OverlapNoteStoreState::Hidden;
  focus.overlapNotes[hiddenId] = hidden;

  resolveOverlapNotesForPreCommit(session, focus, 1, loopLength);

  for (const MidiEvent& evt : session) {
    if (evt.isNoteOn() && evt.data.noteData.note == 60 && evt.tick == 592) {
      TEST_FAIL_MESSAGE("hidden pair should be erased at resolve");
    }
  }
}

void test_editor_selection_resolves_mover_after_macro_normalize() {
  constexpr uint32_t loopLength = 1536;
  resetNoteIdCounter();
  NoteEditFocus focus;
  NoteEditSessionState sessionState{};
  MidiEventVec session;
  session.push_back(noteOnWithNoteId(496, 1, 60, 100, 3));
  session.push_back(MidiEvent::NoteOff(1200, 1, 60, 0));

  rebuildNoteEditFocusFromStore(focus, session, 1, loopLength, 0);
  focus.last.endTick = 1200;
  focus.movingNoteRange.end = 1200;
  sessionState.selection.primaryNote = focus.movingNoteId;
  sessionState.selection.selectedTick = 496;

  runNoteEditMacroCommitNormalize(session, focus, 1, loopLength);

  const auto filtered = filterSelectableDisplayNotes(session, focus, 1, loopLength);
  const int idx = NoteEditDisplaySnapshot::filteredDisplayNoteIndexForSelection(
      sessionState.selection, filtered);
  TEST_ASSERT_TRUE(idx >= 0);
  TEST_ASSERT_EQUAL(focus.movingNoteId, filtered[static_cast<size_t>(idx)].noteId);
  TEST_ASSERT_EQUAL_UINT32(496u, filtered[static_cast<size_t>(idx)].startTick);
}

void test_edit_closure_includes_overlap_participants() {
  constexpr uint32_t loopLength = 1536;
  resetNoteIdCounter();
  NoteEditFocus focus;
  MidiEventVec session;
  session.push_back(noteOnWithNoteId(496, 1, 60, 100, 3));
  session.push_back(MidiEvent::NoteOff(1200, 1, 60, 0));
  session.push_back(noteOnWithNoteId(592, 1, 60, 100, 4));
  session.push_back(MidiEvent::NoteOff(688, 1, 60, 0));

  rebuildNoteEditFocusFromStore(focus, session, 1, loopLength, 0);
  const NoteId hiddenId = noteIdForNoteOn(session, 1, 60, 592);
  OverlapNote hidden{};
  hidden.noteId = hiddenId;
  hidden.baseline = {60, 64, 592, 688};
  hidden.state = OverlapNoteStoreState::Hidden;
  focus.overlapNotes[hiddenId] = hidden;

  const auto closure = buildEditClosureNoteIds(focus, session, 1, loopLength);
  TEST_ASSERT_TRUE(closure.count(focus.movingNoteId) > 0);
  TEST_ASSERT_TRUE(closure.count(hiddenId) > 0);
}

void test_shortened_overlap_materializes_linear_off_at_resolve() {
  constexpr uint32_t loopLength = 1536;
  resetNoteIdCounter();
  NoteEditFocus focus;
  MidiEventVec session;
  session.push_back(noteOnWithNoteId(400, 1, 60, 100, 2));
  session.push_back(MidiEvent::NoteOff(688, 1, 60, 0));

  rebuildNoteEditFocusFromStore(focus, session, 1, loopLength, 0);
  session[0].noteId = kInvalidNoteId;
  const NoteId shortId = 2;
  OverlapNote shortened{};
  shortened.noteId = shortId;
  shortened.baseline = {60, 64, 400, 688};
  shortened.state = OverlapNoteStoreState::Shortened;
  shortened.shortenedEndTick = 495;
  focus.overlapNotes[shortId] = shortened;

  resolveOverlapNotesForPreCommit(session, focus, 1, loopLength);

  bool foundOff495 = false;
  bool foundOff688 = false;
  for (const MidiEvent& evt : session) {
    if (evt.isNoteOff() && evt.data.noteData.note == 60 && evt.tick == 495) {
      foundOff495 = true;
    }
    if (evt.isNoteOff() && evt.data.noteData.note == 60 && evt.tick == 688) {
      foundOff688 = true;
    }
  }
  TEST_ASSERT_TRUE(foundOff495);
  TEST_ASSERT_FALSE(foundOff688);
}

void test_hidden_overlap_uses_baseline_map_when_display_wrap_end() {
  constexpr NoteId kOverlapId = 58;
  NoteEditFocus focus;
  focus.baselineMap[kOverlapId] = {58, 100, 1400, 1536};

  OverlapNote hidden{};
  hidden.noteId = kOverlapId;
  hidden.baseline = {58, 100, 1400, 50};
  hidden.state = OverlapNoteStoreState::Hidden;
  focus.overlapNotes[kOverlapId] = hidden;

  MidiEventVec session;
  const NoteBaseline linear =
      linearBaselineForOverlapRestore(focus, hidden, &session, 1);
  TEST_ASSERT_EQUAL_UINT32(1536u, linear.endTick);
}

void test_move_restore_hidden_neighbor_log_scenario_baseline() {
  // session_20260714_011558.log: pitch 60 neighbor linear 0-144 hidden during move to 0.
  constexpr uint32_t loopLength = 2304;
  constexpr uint8_t channel = 1;
  constexpr NoteId kNeighborId = 1;
  resetNoteIdCounter();

  MidiEventVec session;
  session.push_back(noteOnWithNoteId(0, channel, 60, 100, kNeighborId));
  session.push_back(MidiEvent::NoteOff(144, channel, 60, 0));
  session.push_back(noteOnWithNoteId(384, channel, 60, 100, 3));
  session.push_back(MidiEvent::NoteOff(480, channel, 60, 0));

  NoteEditFocus focus;
  rebuildNoteEditFocusFromStore(focus, session, channel, loopLength, 0);
  focus.baselineMap[kNeighborId] = {60, 100, 0, 144};

  OverlapNote hidden{};
  hidden.noteId = kNeighborId;
  hidden.baseline = {60, 100, 0, 144};
  hidden.state = OverlapNoteStoreState::Hidden;
  focus.overlapNotes[kNeighborId] = hidden;

  session.erase(std::remove_if(session.begin(), session.end(),
                               [](const MidiEvent& evt) {
                                 return evt.data.noteData.note == 60 &&
                                        ((evt.isNoteOn() && evt.tick == 0) ||
                                         (evt.isNoteOff() && evt.tick == 144));
                               }),
                session.end());

  const NoteBaseline restoreSpan =
      linearBaselineForOverlapRestore(focus, hidden, &session, channel);
  TEST_ASSERT_EQUAL_UINT32(0u, restoreSpan.startTick);
  TEST_ASSERT_EQUAL_UINT32(144u, restoreSpan.endTick);

  session.push_back(noteOnWithNoteId(restoreSpan.startTick, channel, restoreSpan.pitch,
                                     restoreSpan.velocity, kNeighborId));
  session.push_back(MidiEvent::NoteOff(restoreSpan.endTick, channel, restoreSpan.pitch, 0));

  bool foundNeighbor = false;
  for (const MidiEvent& evt : session) {
    if (evt.isNoteOn() && evt.data.noteData.note == 60 && evt.tick == 0 &&
        evt.noteId == kNeighborId) {
      foundNeighbor = true;
    }
  }
  TEST_ASSERT_TRUE(foundNeighbor);

  const auto linearStorageSpansOverlap = [](uint32_t start1, uint32_t end1, uint32_t start2,
                                            uint32_t end2) {
    return start1 < end2 && start2 < end1;
  };
  constexpr uint32_t moverStart = 144;
  constexpr uint32_t moverEnd = 240;
  TEST_ASSERT_FALSE(linearStorageSpansOverlap(moverStart, moverEnd, restoreSpan.startTick,
                                              restoreSpan.endTick));
}

void test_edit_projection_context_uses_selection_and_full_loop_window() {
  constexpr uint32_t kLoopLength = 1536;
  EditorSelection selection;
  selection.primaryNote = 32;
  selection.selectedTick = 1484;
  selection.selectedNotes.push_back(32);

  const ProjectionContext context = IntervalProjection::buildEditProjectionContext(
      selection, kLoopLength, IntervalProjection::makeFullLoopEditAnalysisWindow(kLoopLength),
      1484);

  TEST_ASSERT_EQUAL(ProjectionType::Edit, context.type);
  TEST_ASSERT_EQUAL_UINT32(kLoopLength, context.loopLength);
  TEST_ASSERT_EQUAL_INT32(0, context.window.start);
  TEST_ASSERT_EQUAL_INT32(static_cast<int32_t>(kLoopLength), context.window.end);
  TEST_ASSERT_EQUAL_INT32(1484, context.originTick);
  TEST_ASSERT_EQUAL_UINT32(1484u, static_cast<uint32_t>(context.selectedTick));
}

void test_edit_projection_batch_selects_linear_span_for_wrapped_storage() {
  constexpr uint32_t kLoopLength = 1536;
  constexpr NoteId kWrapId = 49;

  EditorSelection selection;
  selection.primaryNote = kWrapId;
  selection.selectedTick = 49;
  selection.selectedNotes.push_back(kWrapId);

  const ProjectionContext context = IntervalProjection::buildEditProjectionContext(
      selection, kLoopLength, IntervalProjection::makeFullLoopEditAnalysisWindow(kLoopLength), 49);

  const CanonicalNoteSpanVec spans = {
      {kWrapId, TickInterval{49, static_cast<int32_t>(kLoopLength - 1)}, 49, 100}};

  const ProjectedIntervalVec projected =
      IntervalProjection::projectEditIntervalsForAnalysis(spans, context);

  TEST_ASSERT_EQUAL(1, static_cast<int>(projected.size()));
  TEST_ASSERT_EQUAL_UINT32(kWrapId, projected[0].noteId);
  TEST_ASSERT_EQUAL_INT32(49, projected[0].interval.start);
  TEST_ASSERT_EQUAL_INT32(static_cast<int32_t>(kLoopLength - 1), projected[0].interval.end);
}

void test_edit_projection_parity_resolve_linear_span_baseline_map() {
  constexpr NoteId kOverlapId = 58;
  constexpr uint32_t kLoopLength = 1536;
  NoteEditFocus focus;
  focus.baselineMap[kOverlapId] = {58, 100, 483, 1370};

  MidiEventVec session;
  MidiEvent on = MidiEvent::NoteOn(483, 1, 58, 100);
  on.noteId = kOverlapId;
  session.push_back(on);
  MidiEvent off = MidiEvent::NoteOff(1403, 1, 58, 0);
  off.noteId = kOverlapId;
  session.push_back(off);

  const NoteUtils::DisplayNote dn{kOverlapId, 58, 100, 483, 1403};
  NoteBaseline linear{};
  TEST_ASSERT_TRUE(
      resolveLinearNoteSpanForOverlap(focus, session, 1, dn, linear, kLoopLength));
  TEST_ASSERT_EQUAL_UINT32(483u, linear.startTick);
  TEST_ASSERT_EQUAL_UINT32(1370u, linear.endTick);

  EditorSelection selection;
  selection.primaryNote = kOverlapId;
  selection.selectedTick = 483;
  const ProjectionContext context = IntervalProjection::buildEditProjectionContext(
      selection, kLoopLength, IntervalProjection::makeFullLoopEditAnalysisWindow(kLoopLength), 483);
  const CanonicalNoteSpanVec spans = {
      {kOverlapId, TickInterval{483, 1370}, 58, 100}};
  const ProjectedIntervalVec projected =
      IntervalProjection::projectEditIntervalsForAnalysis(spans, context);
  TEST_ASSERT_EQUAL(1, static_cast<int>(projected.size()));
  TEST_ASSERT_EQUAL_INT32(483, projected[0].interval.start);
  TEST_ASSERT_EQUAL_INT32(1370, projected[0].interval.end);
}

void test_edit_projection_parity_wrapped_mover_linear_span() {
  constexpr NoteId kMoverId = 84;
  constexpr uint32_t kLoopLength = 1536;

  EditorSelection selection;
  selection.primaryNote = kMoverId;
  selection.selectedTick = 1499;
  const ProjectionContext context = IntervalProjection::buildEditProjectionContext(
      selection, kLoopLength, IntervalProjection::makeFullLoopEditAnalysisWindow(kLoopLength),
      1499);

  const CanonicalNoteSpanVec spans = {{kMoverId, TickInterval{1499, 1595}, 31, 100}};
  const ProjectedIntervalVec projected =
      IntervalProjection::projectEditIntervalsForAnalysis(spans, context);

  TEST_ASSERT_EQUAL(1, static_cast<int>(projected.size()));
  TEST_ASSERT_EQUAL_INT32(1499, projected[0].interval.start);
  TEST_ASSERT_EQUAL_INT32(1595, projected[0].interval.end);
}

void test_find_note_on_for_moving_note_edit_note_id_over_same_pitch_decoy() {
  resetNoteIdCounter();
  constexpr uint32_t kLoopLength = 3840;
  constexpr uint8_t kChannel = 1;
  constexpr uint8_t kPitch = 26;
  constexpr NoteId kMoverId = 1;

  MidiEventVec flat;
  flat.push_back(noteOnWithNoteId(2016, 2, kPitch, 100, 99));
  flat.push_back(MidiEvent::NoteOff(2112, 2, kPitch, 0));
  flat.push_back(noteOnWithNoteId(2050, kChannel, kPitch, 100, kMoverId));
  flat.push_back(MidiEvent::NoteOff(2146, kChannel, kPitch, 0));
  flat.push_back(noteOnWithNoteId(1800, kChannel, kPitch, 100, 2));
  flat.push_back(MidiEvent::NoteOff(1896, kChannel, kPitch, 0));
  flat.push_back(noteOnWithNoteId(2200, kChannel, kPitch, 100, 3));
  flat.push_back(MidiEvent::NoteOff(2296, kChannel, kPitch, 0));

  NoteEditFocus focus;
  focus.active = true;
  focus.movingNoteId = kMoverId;
  focus.last = {kPitch, 100, 2016, 2112};

  bool legacyTickHit = false;
  for (const MidiEvent& evt : flat) {
    if (evt.channel == kChannel && evt.isNoteOn() && evt.data.noteData.velocity > 0 &&
        evt.data.noteData.note == kPitch && evt.tick == focus.last.startTick) {
      legacyTickHit = true;
      break;
    }
  }
  TEST_ASSERT_FALSE(legacyTickHit);

  MidiEvent* moverOn = findNoteOnForMovingNoteEdit(flat, focus, kChannel, kPitch,
                                                   focus.last.startTick, kLoopLength);
  TEST_ASSERT_NOT_NULL(moverOn);
  TEST_ASSERT_EQUAL(kMoverId, moverOn->noteId);
  TEST_ASSERT_EQUAL_UINT32(2050u, moverOn->tick);
}

void test_find_note_on_for_moving_note_edit_note_id_channel_fallback() {
  constexpr uint32_t kLoopLength = 3840;
  constexpr uint8_t kTrackChannel = 5;
  constexpr uint8_t kStoreChannel = 1;
  constexpr uint8_t kPitch = 30;
  constexpr NoteId kMoverId = 42;

  MidiEventVec flat;
  flat.push_back(noteOnWithNoteId(672, kStoreChannel, kPitch, 100, kMoverId));
  flat.push_back(MidiEvent::NoteOff(768, kStoreChannel, kPitch, 0));

  NoteEditFocus focus;
  focus.active = true;
  focus.movingNoteId = kMoverId;
  focus.commitBaseline = {kPitch, 100, 672, 768};
  focus.last = focus.commitBaseline;

  MidiEvent* moverOn = findNoteOnForMovingNoteEdit(flat, focus, kTrackChannel, kPitch,
                                                   focus.last.startTick, kLoopLength);
  TEST_ASSERT_NOT_NULL(moverOn);
  TEST_ASSERT_EQUAL(kMoverId, moverOn->noteId);
  TEST_ASSERT_EQUAL_UINT32(672u, moverOn->tick);
}

void test_find_note_on_for_moving_note_edit_commit_baseline_preferred_start() {
  constexpr uint32_t kLoopLength = 3840;
  constexpr uint8_t kChannel = 5;
  constexpr uint8_t kPitch = 30;
  constexpr NoteId kMoverId = 42;

  MidiEventVec flat;
  flat.push_back(noteOnWithNoteId(2016, kChannel, kPitch, 100, 99));
  flat.push_back(MidiEvent::NoteOff(2112, kChannel, kPitch, 0));
  flat.push_back(noteOnWithNoteId(672, kChannel, kPitch, 100, kMoverId));
  flat.push_back(MidiEvent::NoteOff(768, kChannel, kPitch, 0));

  NoteEditFocus focus;
  focus.active = true;
  focus.movingNoteId = kMoverId;
  focus.commitBaseline = {kPitch, 100, 672, 768};
  focus.last = {kPitch, 100, 2016, 2112};

  MidiEvent* moverOn = findNoteOnForMovingNoteEdit(flat, focus, kChannel, kPitch,
                                                   focus.last.startTick, kLoopLength);
  TEST_ASSERT_NOT_NULL(moverOn);
  TEST_ASSERT_EQUAL(kMoverId, moverOn->noteId);
  TEST_ASSERT_EQUAL_UINT32(672u, moverOn->tick);
}

void test_pitch_pre_commit_requires_active_focus() {
  constexpr uint32_t kLoopLength = 768;
  NoteEditFocus focus;
  focus.active = false;
  focus.commitBaseline = {60, 100, 384, 480};
  focus.last = focus.commitBaseline;
  focus.movingNoteId = 1;

  noteEditFocusApplyPitch(focus, 67, 384, 480, kLoopLength);
  EditPassVec rowsInactive = buildPreCommitEditPasses(focus, 1);
  TEST_ASSERT_EQUAL(0, static_cast<int>(rowsInactive.size()));

  const MidiEventVec flat = makeTwoNoteFlat(384, 480, 584, 680, 60);
  rebuildNoteEditFocusFromStore(focus, flat, 1, kLoopLength, 0);
  TEST_ASSERT_TRUE(focus.active);

  noteEditFocusApplyPitch(focus, 67, focus.last.startTick, focus.last.endTick, kLoopLength);
  const EditPassVec rowsActive = buildPreCommitEditPasses(focus, 1);
  TEST_ASSERT_EQUAL(1, static_cast<int>(rowsActive.size()));
  TEST_ASSERT_EQUAL(EditPropertyType::Pitch, rowsActive[0].propertyType);
  TEST_ASSERT_EQUAL_UINT8(67, rowsActive[0].pitch);
  TEST_ASSERT_EQUAL(focus.movingNoteId, rowsActive[0].targetNoteId);
}

int main(int /*argc*/, char** /*argv*/) {
  UNITY_BEGIN();
  RUN_TEST(test_baseline_map_includes_moving_note_at_select);
  RUN_TEST(test_populate_baseline_map_for_edit_closure_wrap_sibling);
  RUN_TEST(test_a1_length_updates_moving_note_range_not_commit_baseline);
  RUN_TEST(test_a1_no_pending_length_when_moving_note_range_matches_baseline);
  RUN_TEST(test_note_edit_focus_has_pending_commit_geometry_and_overlap);
  RUN_TEST(test_can_apply_simple_pitch_change_without_lane_collision);
  RUN_TEST(test_can_apply_simple_pitch_change_blocks_inner_overlap_on_target_lane);
  RUN_TEST(test_inner_overlap_note_in_moving_note_range);
  RUN_TEST(test_overlap_note_effective_end_shortened_vs_hidden);
  RUN_TEST(test_shorten_under_49_ticks_classifies_as_hidden_candidate);
  RUN_TEST(test_pre_commit_edit_change_order_delete_shorten_move_length_pitch);
  RUN_TEST(test_pre_commit_store_diff_subset_mover_and_overlap_notes);
  RUN_TEST(test_build_pre_commit_changes_replay_lengthen_delete_pitch);
  RUN_TEST(test_reselect_keeps_commit_baseline_with_pending_length);
  RUN_TEST(test_filter_excludes_hidden_overlap_note);
  RUN_TEST(test_filter_includes_moving_note_when_hidden_overlap_baseline_matches);
  RUN_TEST(test_filter_includes_shortened_overlap_note);
  RUN_TEST(test_filter_excludes_inner_under_moving_note);
  RUN_TEST(test_filtered_display_note_index_for_note_ref);
  RUN_TEST(test_sync_linear_focus_avoids_spurious_display_length_commit);
  RUN_TEST(test_prune_overlap_shortened_display_baseline_artifact);
  RUN_TEST(test_filtered_display_note_index_for_note_id_and_start);
  RUN_TEST(test_filtered_display_note_index_for_note_id_and_end);
  RUN_TEST(test_filtered_display_note_index_for_moving_note_exact_start_only);
  RUN_TEST(test_filtered_display_note_index_duplicate_pitch_prefers_linear_start);
  RUN_TEST(test_filtered_display_note_index_duplicate_pitch_rejects_mispaired_low_segment);
  RUN_TEST(test_filtered_display_note_index_rejects_only_mispaired_low_segment);
  RUN_TEST(test_selection_index_duplicate_pitch_uses_linear_start);
  RUN_TEST(test_selection_index_geometry_move_prefers_focus_display_bracket);
  RUN_TEST(test_is_plausible_storage_span_rejects_lifo_mispair);
  RUN_TEST(test_find_linear_note_span_rejects_mispaired_off);
  RUN_TEST(test_is_moving_note_overlap_scratch_entry);
  RUN_TEST(test_pitch_linear_focus_no_spurious_length_after_sync);
  RUN_TEST(test_linear_baseline_for_overlap_restore_rejects_display_wrap_end);
  RUN_TEST(test_baseline_map_prefers_linear_span_over_wrap_projection);
  RUN_TEST(test_sync_linear_focus_same_pitch_shortened_overlap_does_not_steal_mover_off);
  RUN_TEST(test_find_linear_note_span_wrapped_mover_ignores_in_loop_orphan_off);
  RUN_TEST(test_linear_baseline_for_overlap_restore_shortened_keeps_original_end);
  RUN_TEST(test_linear_baseline_for_overlap_restore_hidden_uses_hide_snapshot_not_session);
  RUN_TEST(test_find_linear_off_for_note_id_ignores_same_pitch_neighbor_off);
  RUN_TEST(test_moving_note_linear_span_ignores_neighbor_lifo_off);
  RUN_TEST(test_resolve_linear_note_span_for_overlap_prefers_baseline_map);
  RUN_TEST(test_find_linear_note_span_mover_length_ignores_neighbor_off);
  RUN_TEST(test_overlap_hide_shorten_canonical_invariants_at_macro_commit);
  RUN_TEST(test_wrap_move_canonical_invariants_at_macro_commit);
  RUN_TEST(test_contained_hidden_overlap_removed_at_resolve);
  RUN_TEST(test_editor_selection_resolves_mover_after_macro_normalize);
  RUN_TEST(test_edit_closure_includes_overlap_participants);
  RUN_TEST(test_shortened_overlap_materializes_linear_off_at_resolve);
  RUN_TEST(test_hidden_overlap_uses_baseline_map_when_display_wrap_end);
  RUN_TEST(test_move_restore_hidden_neighbor_log_scenario_baseline);
  RUN_TEST(test_edit_projection_context_uses_selection_and_full_loop_window);
  RUN_TEST(test_edit_projection_batch_selects_linear_span_for_wrapped_storage);
  RUN_TEST(test_edit_projection_parity_resolve_linear_span_baseline_map);
  RUN_TEST(test_edit_projection_parity_wrapped_mover_linear_span);
  RUN_TEST(test_find_note_on_for_moving_note_edit_note_id_over_same_pitch_decoy);
  RUN_TEST(test_find_note_on_for_moving_note_edit_note_id_channel_fallback);
  RUN_TEST(test_find_note_on_for_moving_note_edit_commit_baseline_preferred_start);
  RUN_TEST(test_pitch_pre_commit_requires_active_focus);
  return UNITY_END();
}
