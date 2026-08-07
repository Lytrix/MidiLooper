//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include <unity.h>

#include "../../src/Logger.cpp"
#include "../../src/Utils/NoteUtils.cpp"
#include "../../src/Utils/LoopTickNormalize.cpp"
#include "../../src/Utils/LoopEventValidation.cpp"
#include "../test_support/LoopCaptureTestDeps.cpp"
#include "../test_support/NoteEditFocusTestDeps.cpp"
#include "../../src/EditManager/ApplyEditSessionActions.cpp"
#include "../../src/EditManager/ApplyOwnedEditPassRows.cpp"
#include "../../src/EditManager/EditSessionStoreInvariant.cpp"
#include "../../src/EditManager/EditApply.cpp"
#include "../../src/Loop/LoopPasses.cpp"
#include "../../src/LoopEventStore.cpp"
#include "../test_support/MemoryMonitorNativeDeps.cpp"
#include "../../src/Loop.cpp"

#include "../test_support/CommittedChunkIdTestHelpers.h"
#include "EditApply.h"
#include "ApplyEditSessionActions.h"
#include "EditSessionAction.h"
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

void test_populate_baseline_full_loop_includes_cross_pitch() {
  constexpr uint32_t loopLength = 384;
  constexpr uint8_t channel = 1;
  constexpr NoteId kHead12 = 77;
  constexpr NoteId kMoverId = 79;
  constexpr NoteId kCross93 = 3;
  constexpr NoteId kCross96 = 5;

  MidiEventVec committed;
  committed.push_back(noteOnWithNoteId(0, channel, 12, 100, kHead12));
  committed.push_back(MidiEvent::NoteOff(192, channel, 12, 0));
  committed.push_back(noteOnWithNoteId(96, channel, 12, 100, kMoverId));
  committed.push_back(MidiEvent::NoteOff(192, channel, 12, 0));
  committed.push_back(noteOnWithNoteId(144, channel, 93, 100, kCross93));
  committed.push_back(MidiEvent::NoteOff(192, channel, 93, 0));
  committed.push_back(noteOnWithNoteId(144, channel, 96, 100, kCross96));
  committed.push_back(MidiEvent::NoteOff(192, channel, 96, 0));

  MidiEventVec store = committed;

  NoteEditFocus focus;
  focus.active = true;
  focus.movingNoteId = kMoverId;
  focus.commitBaseline = {12, 100, 96, 192};
  focus.last = focus.commitBaseline;
  focus.baselineMap[kMoverId] = focus.commitBaseline;

  populateBaselineMapForEditClosure(focus, committed, store, channel, loopLength);
  TEST_ASSERT_TRUE(focus.baselineMap.count(kHead12) > 0);
  TEST_ASSERT_TRUE(focus.baselineMap.count(kCross93) > 0);
  TEST_ASSERT_TRUE(focus.baselineMap.count(kCross96) > 0);
  TEST_ASSERT_EQUAL_UINT32(144u, focus.baselineMap[kCross93].startTick);
  TEST_ASSERT_EQUAL_UINT32(192u, focus.baselineMap[kCross93].endTick);
}

void test_populate_baseline_includes_store_channel_notes() {
  // session_20260805_033545: storeNoteOns=73 but baselineMap=1 because closure still filtered
  // live noteIds by the track output channel. The edit session store's NoteId is the identity key.
  constexpr uint32_t loopLength = 2304;
  constexpr uint8_t trackChannel = 2;
  constexpr uint8_t storeChannel = 5;
  constexpr NoteId kMoverId = 78;
  constexpr NoteId kOverlapId = 91;

  MidiEventVec committed;
  committed.push_back(noteOnWithNoteId(912, storeChannel, 25, 100, kOverlapId));
  committed.push_back(MidiEvent::NoteOff(1008, storeChannel, 25, 0));
  committed.push_back(noteOnWithNoteId(960, storeChannel, 25, 100, kMoverId));
  committed.push_back(MidiEvent::NoteOff(1344, storeChannel, 25, 0));

  MidiEventVec store = committed;

  NoteEditFocus focus;
  focus.active = true;
  focus.movingNoteId = kMoverId;
  focus.commitBaseline = {25, 100, 960, 1344};
  focus.last = focus.commitBaseline;
  focus.baselineMap[kMoverId] = focus.commitBaseline;

  populateBaselineMapForEditClosure(focus, committed, store, trackChannel, loopLength);

  TEST_ASSERT_TRUE(focus.baselineMap.count(kOverlapId) > 0);
  TEST_ASSERT_EQUAL_UINT32(912u, focus.baselineMap[kOverlapId].startTick);
  TEST_ASSERT_EQUAL_UINT32(1008u, focus.baselineMap[kOverlapId].endTick);
}

void test_populate_baseline_maps_committed_pitch_start_via_live_note_id() {
  // Record-pass materialize may lack noteId on note-ons; session store has stable ids.
  constexpr uint32_t loopLength = 2304;
  constexpr uint8_t channel = 1;
  constexpr NoteId kMoverId = 78;
  constexpr NoteId kCross93 = 4;

  MidiEvent committedOn = MidiEvent::NoteOn(144, channel, 93, 100);
  committedOn.noteId = kInvalidNoteId;
  MidiEvent committedOff = MidiEvent::NoteOff(192, channel, 93, 0);

  MidiEventVec committed;
  committed.push_back(committedOn);
  committed.push_back(committedOff);
  committed.push_back(noteOnWithNoteId(360, channel, 26, 100, kMoverId));
  committed.push_back(MidiEvent::NoteOff(576, channel, 26, 0));

  MidiEventVec store;
  store.push_back(noteOnWithNoteId(144, channel, 93, 100, kCross93));
  MidiEvent liveOff = MidiEvent::NoteOff(192, channel, 93, 0);
  liveOff.noteId = kCross93;
  store.push_back(liveOff);
  store.push_back(noteOnWithNoteId(360, channel, 26, 100, kMoverId));
  MidiEvent moverOff = MidiEvent::NoteOff(576, channel, 26, 0);
  moverOff.noteId = kMoverId;
  store.push_back(moverOff);

  NoteEditFocus focus;
  focus.active = true;
  focus.movingNoteId = kMoverId;
  focus.baselineMap[kMoverId] = {26, 100, 360, 576};

  populateBaselineMapForEditClosure(focus, committed, store, channel, loopLength);
  TEST_ASSERT_TRUE(focus.baselineMap.count(kCross93) > 0);
  TEST_ASSERT_EQUAL_UINT32(144u, focus.baselineMap[kCross93].startTick);
  TEST_ASSERT_EQUAL_UINT32(192u, focus.baselineMap[kCross93].endTick);
}

void test_populate_baseline_keys_by_live_note_id_not_pass_id() {
  // Driver-boundary snapshot keys from live store; pass-only ids are never inserted.
  constexpr uint32_t loopLength = 2304;
  constexpr uint8_t channel = 1;
  constexpr NoteId kMoverId = 78;
  constexpr NoteId kPassCrossId = 3;
  constexpr NoteId kLiveCrossId = 99;

  MidiEventVec committed;
  committed.push_back(noteOnWithNoteId(144, channel, 93, 100, kPassCrossId));
  committed.push_back(MidiEvent::NoteOff(192, channel, 93, 0));
  committed.push_back(noteOnWithNoteId(360, channel, 13, 100, kMoverId));
  committed.push_back(MidiEvent::NoteOff(576, channel, 13, 0));

  MidiEventVec store;
  store.push_back(noteOnWithNoteId(144, channel, 93, 100, kLiveCrossId));
  MidiEvent liveOff = MidiEvent::NoteOff(192, channel, 93, 0);
  liveOff.noteId = kLiveCrossId;
  store.push_back(liveOff);
  store.push_back(noteOnWithNoteId(360, channel, 13, 100, kMoverId));
  MidiEvent moverOff = MidiEvent::NoteOff(576, channel, 13, 0);
  moverOff.noteId = kMoverId;
  store.push_back(moverOff);

  NoteEditFocus focus;
  focus.active = true;
  focus.movingNoteId = kMoverId;
  focus.baselineMap[kMoverId] = {13, 100, 360, 576};
  // Fresh driver boundary clears stale pass ids — simulate clear + mover seed only.
  focus.baselineMap.clear();
  focus.baselineMap[kMoverId] = {13, 100, 360, 576};

  populateBaselineMapForEditClosure(focus, committed, store, channel, loopLength);
  TEST_ASSERT_EQUAL(0, static_cast<int>(focus.baselineMap.count(kPassCrossId)));
  TEST_ASSERT_TRUE(focus.baselineMap.count(kLiveCrossId) > 0);
  TEST_ASSERT_EQUAL_UINT32(144u, focus.baselineMap[kLiveCrossId].startTick);
  TEST_ASSERT_EQUAL_UINT32(192u, focus.baselineMap[kLiveCrossId].endTick);
}

void test_hidden_note_baseline_survives_for_pre_commit_delete() {
  // Hide removes the live pair; immutable baseline must still yield a Delete row.
  constexpr uint32_t loopLength = 2304;
  constexpr uint8_t channel = 1;
  constexpr NoteId kInnerId = 3;
  constexpr NoteId kMoverId = 9;

  NoteEditFocus focus;
  focus.active = true;
  focus.movingNoteId = kMoverId;
  focus.commitBaseline = {23, 100, 144, 336};
  focus.last = focus.commitBaseline;
  focus.baselineMap[kInnerId] = {23, 100, 192, 240};
  focus.baselineMap[kMoverId] = focus.commitBaseline;
  // The pipeline hid this note, which is what authorises the Delete row.
  recordChangedOverlapNote(focus, kInnerId);

  MidiEventVec store;
  store.push_back(noteOnWithNoteId(144, channel, 23, 100, kMoverId));
  store.push_back(MidiEvent::NoteOff(336, channel, 23, 0));
  // Inner hidden — absent from live store; baseline entry must remain.
  TEST_ASSERT_TRUE(focus.baselineMap.count(kInnerId) > 0);
  TEST_ASSERT_TRUE(
      noteEditFocusHasPendingBaselineMapDiff(focus, store, channel, loopLength));

  const EditPassVec rows = buildPreCommitEditPasses(focus, channel, &store, loopLength);
  TEST_ASSERT_EQUAL(1, static_cast<int>(rows.size()));
  TEST_ASSERT_EQUAL(static_cast<int>(EditActionType::Delete),
                    static_cast<int>(rows[0].actionType));
  TEST_ASSERT_EQUAL(kInnerId, rows[0].targetNoteId);

  // Re-populate must not erase the hidden note's baseline (insert-if-missing only).
  populateBaselineMapForEditClosure(focus, store, store, channel, loopLength);
  TEST_ASSERT_TRUE(focus.baselineMap.count(kInnerId) > 0);
  TEST_ASSERT_EQUAL_UINT32(192u, focus.baselineMap[kInnerId].startTick);
  TEST_ASSERT_EQUAL_UINT32(240u, focus.baselineMap[kInnerId].endTick);
}

void test_hidden_overlap_same_start_as_mover_emits_delete_not_length() {
  constexpr uint32_t loopLength = 2304;
  constexpr uint8_t channel = 1;
  constexpr NoteId kHiddenId = 84;
  constexpr NoteId kMoverId = 87;

  NoteEditFocus focus;
  focus.active = true;
  focus.movingNoteId = kMoverId;
  focus.commitBaseline = {23, 100, 672, 837};
  focus.last = {23, 100, 288, 453};
  focus.baselineMap[kHiddenId] = {23, 100, 288, 383};
  focus.baselineMap[kMoverId] = focus.commitBaseline;
  recordChangedOverlapNote(focus, kHiddenId);

  MidiEventVec store;
  store.push_back(noteOnWithNoteId(288, channel, 23, 100, kMoverId));
  store.push_back(MidiEvent::NoteOff(453, channel, 23, 0));

  TEST_ASSERT_TRUE(noteEditFocusHasPendingBaselineMapDiff(focus, store, channel, loopLength));

  const EditPassVec rows = buildPreCommitEditPasses(focus, channel, &store, loopLength);
  TEST_ASSERT_EQUAL(2, static_cast<int>(rows.size()));
  TEST_ASSERT_EQUAL(static_cast<int>(EditActionType::Delete),
                    static_cast<int>(rows[0].actionType));
  TEST_ASSERT_EQUAL(kHiddenId, rows[0].targetNoteId);
  TEST_ASSERT_EQUAL(static_cast<int>(EditActionType::Update),
                    static_cast<int>(rows[1].actionType));
  TEST_ASSERT_EQUAL(static_cast<int>(EditPropertyType::NoteRange),
                    static_cast<int>(rows[1].propertyType));
  TEST_ASSERT_EQUAL(kMoverId, rows[1].targetNoteId);
}

void test_unchanged_overlap_live_mismatch_emits_no_length_row() {
  // session_20260805_123427.log: after move/deselect/reselect, stale live overlap length
  // at start=288 emitted a pre-commit Length row even though geometry no longer marked the
  // overlap changed. Only changedOverlapNoteIds authorizes overlap update rows.
  constexpr uint32_t loopLength = 2304;
  constexpr uint8_t channel = 1;
  constexpr NoteId kOverlapId = 82;
  constexpr NoteId kMoverId = 68;

  NoteEditFocus focus;
  focus.active = true;
  focus.movingNoteId = kMoverId;
  focus.commitBaseline = {82, 100, 672, 1151};
  focus.last = focus.commitBaseline;
  focus.baselineMap[kOverlapId] = {23, 100, 288, 719};
  focus.baselineMap[kMoverId] = focus.commitBaseline;
  TEST_ASSERT_TRUE(focus.changedOverlapNoteIds.empty());

  MidiEventVec store;
  store.push_back(noteOnWithNoteId(288, channel, 23, 100, kOverlapId));
  MidiEvent staleOverlapOff = MidiEvent::NoteOff(431, channel, 23, 0);
  staleOverlapOff.noteId = kOverlapId;
  store.push_back(staleOverlapOff);
  store.push_back(noteOnWithNoteId(672, channel, 82, 100, kMoverId));
  MidiEvent moverOff = MidiEvent::NoteOff(1151, channel, 82, 0);
  moverOff.noteId = kMoverId;
  store.push_back(moverOff);

  TEST_ASSERT_FALSE(noteEditFocusHasPendingBaselineMapDiff(focus, store, channel, loopLength));
  const EditPassVec rows = buildPreCommitEditPasses(focus, channel, &store, loopLength);
  TEST_ASSERT_EQUAL(0, static_cast<int>(rows.size()));
}

void test_committed_overlap_delete_clears_focus_restore_authority() {
  // session_20260805_124222.log: committed hidden overlap noteId=68 reappeared after
  // moving noteId=70 away because the deleted note stayed in focus.baselineMap.
  constexpr NoteId kHiddenId = 68;
  constexpr NoteId kMoverId = 70;

  NoteEditFocus focus;
  focus.active = true;
  focus.movingNoteId = kMoverId;
  focus.commitBaseline = {23, 100, 288, 431};
  focus.last = {23, 100, 432, 575};
  focus.baselineMap[kHiddenId] = {23, 100, 288, 431};
  focus.baselineMap[kMoverId] = focus.commitBaseline;

  OverlapNote hidden;
  hidden.noteId = kHiddenId;
  hidden.baseline = focus.baselineMap[kHiddenId];
  hidden.state = OverlapNoteStoreState::Hidden;
  hidden.preCommitEmitted = true;
  focus.overlapNotes[kHiddenId] = hidden;
  recordChangedOverlapNote(focus, kHiddenId);

  NoteIdList committedDeletes;
  committedDeletes.push_back(kHiddenId);
  committedDeletes.push_back(kMoverId);
  committedDeletes.push_back(kInvalidNoteId);
  clearCommittedOverlapDeleteIdsFromFocus(focus, committedDeletes);

  TEST_ASSERT_EQUAL(0, static_cast<int>(focus.baselineMap.count(kHiddenId)));
  TEST_ASSERT_EQUAL(0, static_cast<int>(focus.overlapNotes.count(kHiddenId)));
  TEST_ASSERT_FALSE(hasChangedOverlapNote(focus, kHiddenId));
  TEST_ASSERT_TRUE(focus.baselineMap.count(kMoverId) > 0);
}

void test_committed_overlap_update_promotes_shortened_focus_baseline() {
  // session_20260805_131133.log: a shortened overlap must become the new committed baseline
  // after macro commit; otherwise later ticks keep stale changed-overlap authority.
  constexpr NoteId kOverlapId = 86;
  constexpr NoteId kMoverId = 71;

  NoteEditFocus focus;
  focus.active = true;
  focus.movingNoteId = kMoverId;
  focus.commitBaseline = {23, 100, 1296, 1487};
  focus.last = focus.commitBaseline;
  focus.baselineMap[kOverlapId] = {23, 100, 1230, 1325};
  focus.baselineMap[kMoverId] = focus.commitBaseline;

  OverlapNote shortened;
  shortened.noteId = kOverlapId;
  shortened.baseline = focus.baselineMap[kOverlapId];
  shortened.state = OverlapNoteStoreState::Shortened;
  shortened.shortenedEndTick = 1247;
  focus.overlapNotes[kOverlapId] = shortened;
  recordChangedOverlapNote(focus, kOverlapId);

  const NoteBaseline committedShortened{23, 100, 1230, 1247};
  applyCommittedOverlapUpdateToFocus(focus, kOverlapId, committedShortened);

  TEST_ASSERT_TRUE(focus.baselineMap.count(kOverlapId) > 0);
  TEST_ASSERT_EQUAL_UINT32(1230u, focus.baselineMap[kOverlapId].startTick);
  TEST_ASSERT_EQUAL_UINT32(1247u, focus.baselineMap[kOverlapId].endTick);
  TEST_ASSERT_EQUAL(0, static_cast<int>(focus.overlapNotes.count(kOverlapId)));
  TEST_ASSERT_FALSE(hasChangedOverlapNote(focus, kOverlapId));
  TEST_ASSERT_TRUE(focus.baselineMap.count(kMoverId) > 0);
}

void test_session_134610_shortened_overlap_commit_rows() {
  // session_20260805_134610.log: ShortenNote noteId=87 (192-239) + MoveNote noteId=71
  // (240-431) on pitch 22; first commit must persist overlap Length with targetNoteId=87,
  // then a second hide on the promoted shortened overlap must yield a Delete row.
  constexpr uint32_t loopLength = 1536;
  constexpr uint8_t channel = 1;
  constexpr NoteId kOverlapId = 87;
  constexpr NoteId kMoverId = 71;

  NoteEditFocus focus;
  focus.active = true;
  focus.movingNoteId = kMoverId;
  focus.commitBaseline = {22, 100, 288, 479};
  focus.last = {22, 100, 240, 431};
  focus.baselineMap[kOverlapId] = {22, 100, 192, 287};
  focus.baselineMap[kMoverId] = focus.commitBaseline;
  recordChangedOverlapNote(focus, kOverlapId);

  MidiEventVec store;
  store.push_back(noteOnWithNoteId(192, channel, 22, 100, kOverlapId));
  MidiEvent overlapOff = MidiEvent::NoteOff(239, channel, 22, 0);
  overlapOff.noteId = kOverlapId;
  store.push_back(overlapOff);
  store.push_back(noteOnWithNoteId(240, channel, 22, 100, kMoverId));
  MidiEvent moverOff = MidiEvent::NoteOff(431, channel, 22, 0);
  moverOff.noteId = kMoverId;
  store.push_back(moverOff);

  TEST_ASSERT_TRUE(noteEditFocusHasPendingBaselineMapDiff(focus, store, channel, loopLength));
  const EditPassVec rows = buildPreCommitEditPasses(focus, channel, &store, loopLength);
  TEST_ASSERT_EQUAL(2, static_cast<int>(rows.size()));

  TEST_ASSERT_EQUAL(static_cast<int>(EditActionType::Update),
                    static_cast<int>(rows[0].actionType));
  TEST_ASSERT_EQUAL(static_cast<int>(EditPropertyType::Length),
                    static_cast<int>(rows[0].propertyType));
  TEST_ASSERT_EQUAL(kOverlapId, rows[0].targetNoteId);
  TEST_ASSERT_EQUAL_UINT32(192u, rows[0].startTick);
  TEST_ASSERT_EQUAL_UINT32(239u, rows[0].endTick);

  TEST_ASSERT_EQUAL(static_cast<int>(EditActionType::Update),
                    static_cast<int>(rows[1].actionType));
  TEST_ASSERT_EQUAL(static_cast<int>(EditPropertyType::NoteRange),
                    static_cast<int>(rows[1].propertyType));
  TEST_ASSERT_EQUAL(kMoverId, rows[1].targetNoteId);
  TEST_ASSERT_EQUAL_UINT32(240u, rows[1].startTick);
  TEST_ASSERT_EQUAL_UINT32(431u, rows[1].endTick);

  const NoteBaseline committedShortened{22, 100, 192, 239};
  applyCommittedOverlapUpdateToFocus(focus, kOverlapId, committedShortened);
  focus.commitBaseline = focus.last;
  TEST_ASSERT_FALSE(hasChangedOverlapNote(focus, kOverlapId));

  recordChangedOverlapNote(focus, kOverlapId);
  store.clear();
  store.push_back(noteOnWithNoteId(240, channel, 22, 100, kMoverId));
  moverOff = MidiEvent::NoteOff(431, channel, 22, 0);
  moverOff.noteId = kMoverId;
  store.push_back(moverOff);

  const EditPassVec hideRows = buildPreCommitEditPasses(focus, channel, &store, loopLength);
  TEST_ASSERT_EQUAL(1, static_cast<int>(hideRows.size()));
  TEST_ASSERT_EQUAL(static_cast<int>(EditActionType::Delete),
                    static_cast<int>(hideRows[0].actionType));
  TEST_ASSERT_EQUAL(kOverlapId, hideRows[0].targetNoteId);
}

/// Delete authority — an unresolved baseline entry is preserved, never deleted.
/// Regression for session_20260805_020716: the full-loop baseline turned every noteId lookup
/// miss into a Delete row, so edit passes stripped 12 notes off the take
/// (take_only flatEvents=172 vs replay_flat flatEvents=148).
void test_unresolved_baseline_entry_emits_no_delete_row() {
  constexpr uint32_t loopLength = 2304;
  constexpr uint8_t channel = 1;
  constexpr NoteId kMoverId = 9;
  constexpr NoteId kPresentA = 3;
  constexpr NoteId kUnresolvedB = 4;
  constexpr NoteId kPresentC = 5;

  NoteEditFocus focus;
  focus.active = true;
  focus.movingNoteId = kMoverId;
  focus.commitBaseline = {23, 100, 144, 336};
  focus.last = focus.commitBaseline;
  focus.baselineMap[kMoverId] = focus.commitBaseline;
  focus.baselineMap[kPresentA] = {30, 100, 480, 528};
  focus.baselineMap[kUnresolvedB] = {31, 100, 720, 768};
  focus.baselineMap[kPresentC] = {32, 100, 960, 1008};

  // Live store holds A and C but not B. Nothing was hidden by the pipeline.
  MidiEventVec store;
  store.push_back(noteOnWithNoteId(144, channel, 23, 100, kMoverId));
  store.push_back(MidiEvent::NoteOff(336, channel, 23, 0));
  store.push_back(noteOnWithNoteId(480, channel, 30, 100, kPresentA));
  store.push_back(MidiEvent::NoteOff(528, channel, 30, 0));
  store.push_back(noteOnWithNoteId(960, channel, 32, 100, kPresentC));
  store.push_back(MidiEvent::NoteOff(1008, channel, 32, 0));

  TEST_ASSERT_TRUE(focus.changedOverlapNoteIds.empty());
  TEST_ASSERT_FALSE(noteEditFocusHasPendingBaselineMapDiff(focus, store, channel, loopLength));

  const EditPassVec rows = buildPreCommitEditPasses(focus, channel, &store, loopLength);
  for (const EditPass& row : rows) {
    TEST_ASSERT_NOT_EQUAL(static_cast<int>(EditActionType::Delete),
                          static_cast<int>(row.actionType));
  }
  // B keeps its baseline entry — diagnostics must not remove data.
  TEST_ASSERT_TRUE(focus.baselineMap.count(kUnresolvedB) > 0);
}

/// Restore erases the id, so a note that was hidden then restored leaves nothing pending.
void test_restored_overlap_note_leaves_no_pending_delete() {
  constexpr uint32_t loopLength = 2304;
  constexpr uint8_t channel = 1;
  constexpr NoteId kMoverId = 9;
  constexpr NoteId kInnerId = 3;

  NoteEditFocus focus;
  focus.active = true;
  focus.movingNoteId = kMoverId;
  focus.commitBaseline = {23, 100, 144, 336};
  focus.last = focus.commitBaseline;
  focus.baselineMap[kMoverId] = focus.commitBaseline;
  focus.baselineMap[kInnerId] = {23, 100, 192, 240};
  recordChangedOverlapNote(focus, kInnerId);

  MidiEventVec store;
  store.push_back(noteOnWithNoteId(144, channel, 23, 100, kMoverId));
  store.push_back(MidiEvent::NoteOff(336, channel, 23, 0));
  TEST_ASSERT_TRUE(noteEditFocusHasPendingBaselineMapDiff(focus, store, channel, loopLength));

  // Restore puts the pair back and clears the id.
  forgetChangedOverlapNote(focus, kInnerId);
  store.push_back(noteOnWithNoteId(192, channel, 23, 100, kInnerId));
  store.push_back(MidiEvent::NoteOff(240, channel, 23, 0));

  TEST_ASSERT_FALSE(noteEditFocusHasPendingBaselineMapDiff(focus, store, channel, loopLength));
  const EditPassVec rows = buildPreCommitEditPasses(focus, channel, &store, loopLength);
  for (const EditPass& row : rows) {
    TEST_ASSERT_NOT_EQUAL(static_cast<int>(EditActionType::Delete),
                          static_cast<int>(row.actionType));
  }
}

void test_empty_step_select_preserves_changed_overlap_note_ids() {
  constexpr NoteId kOverlapId = 5;
  constexpr NoteId kMoverId = 3;
  constexpr uint32_t loopLength = 2304;

  NoteEditFocus focus;
  focus.active = true;
  focus.movingNoteId = kMoverId;
  focus.baselineMap[kOverlapId] = {94, 100, 800, 1000};
  focus.baselineMap[kMoverId] = {94, 100, 1000, 1200};
  recordChangedOverlapNote(focus, kOverlapId);
  OverlapNote scratch{};
  scratch.noteId = kOverlapId;
  scratch.state = OverlapNoteStoreState::Shortened;
  scratch.baseline = focus.baselineMap[kOverlapId];
  scratch.shortenedEndTick = 900;
  focus.overlapNotes[kOverlapId] = scratch;

  NoteIdList preservedChangedOverlapNoteIds = focus.changedOverlapNoteIds;
  BaselineMap preservedOverlapBaselines;
  preservedOverlapBaselines[kOverlapId] = focus.baselineMap[kOverlapId];
  OverlapNoteMap preservedOverlapNotes;
  preservedOverlapNotes[kOverlapId] = focus.overlapNotes[kOverlapId];

  MidiEventVec flat;
  rebuildNoteEditFocusFromStore(focus, flat, 1, loopLength, -1);
  TEST_ASSERT_TRUE(focus.changedOverlapNoteIds.empty());

  focus.changedOverlapNoteIds = std::move(preservedChangedOverlapNoteIds);
  focus.baselineMap[kOverlapId] = preservedOverlapBaselines[kOverlapId];
  focus.overlapNotes[kOverlapId] = preservedOverlapNotes[kOverlapId];

  TEST_ASSERT_TRUE(hasChangedOverlapNote(focus, kOverlapId));
  TEST_ASSERT_TRUE(focus.overlapNotes.count(kOverlapId) > 0);
}

void test_empty_deselect_keeps_session_projection_after_pending_move_015614() {
  // session_20260807_015614: note 17 moved 3600→1248; empty deselect at 3600 reverted display.
  constexpr uint32_t kLoopLength = 5376;
  constexpr uint8_t channel = 5;
  constexpr NoteId kMoverId = 17;
  constexpr uint32_t kCommittedStart = 3600;
  constexpr uint32_t kCommittedEnd = 4127;
  constexpr uint32_t kMovedStart = 1248;
  constexpr uint32_t kMovedEnd = 1775;

  MidiEventVec committedEvents;
  committedEvents.push_back(noteOnWithNoteId(kCommittedStart, channel, 88, 100, kMoverId));
  committedEvents.push_back(MidiEvent::NoteOff(kCommittedEnd, channel, 88, 0));
  const NoteUtils::DisplayNoteVec committedBase =
      NoteUtils::reconstructDisplayNotes(committedEvents, kLoopLength, false);

  MidiEventVec session;
  session.push_back(noteOnWithNoteId(kMovedStart, channel, 88, 100, kMoverId));
  session.push_back(MidiEvent::NoteOff(kMovedEnd, channel, 88, 0));

  NoteEditFocus focus;
  focus.active = true;
  focus.movingNoteId = kMoverId;
  focus.commitBaseline = {88, 100, kCommittedStart, kCommittedEnd};
  focus.last = {88, 100, kMovedStart, kMovedEnd};
  focus.movingNoteRange.start = kMovedStart;
  focus.movingNoteRange.end = kMovedEnd;
  focus.baselineMap[kMoverId] = focus.commitBaseline;
  TEST_ASSERT_TRUE(noteEditFocusHasPendingCommit(focus));

  NoteEditFocus clearedFocus;
  rebuildNoteEditFocusFromStore(clearedFocus, committedEvents, channel, kLoopLength, -1);
  const NoteUtils::DisplayNoteVec reverted =
      projectNoteEditDisplayNotes(committedBase, session, clearedFocus, channel, kLoopLength);
  for (const NoteUtils::DisplayNote& dn : reverted) {
    if (dn.noteId == kMoverId) {
      TEST_ASSERT_EQUAL_UINT32(kCommittedStart, dn.startTick);
    }
  }

  const NoteUtils::DisplayNoteVec projected =
      projectNoteEditDisplayNotes(committedBase, session, focus, channel, kLoopLength);
  bool foundMovedMover = false;
  for (const NoteUtils::DisplayNote& dn : projected) {
    if (dn.noteId == kMoverId) {
      TEST_ASSERT_EQUAL_UINT32(kMovedStart, dn.startTick);
      TEST_ASSERT_EQUAL_UINT32(kMovedEnd, dn.endTick);
      foundMovedMover = true;
    }
  }
  TEST_ASSERT_TRUE(foundMovedMover);
}

void test_reconcile_changed_overlap_ids_marks_live_baseline_diff() {
  constexpr NoteId kOverlapId = 5;
  constexpr NoteId kMoverId = 3;
  constexpr uint32_t loopLength = 2304;
  constexpr uint8_t channel = 1;

  NoteEditFocus focus;
  focus.active = true;
  focus.movingNoteId = kMoverId;
  focus.baselineMap[kOverlapId] = {94, 100, 800, 1000};
  focus.baselineMap[kMoverId] = {94, 100, 1000, 1200};
  TEST_ASSERT_TRUE(focus.changedOverlapNoteIds.empty());

  MidiEventVec store;
  store.push_back(noteOnWithNoteId(800, channel, 94, 100, kOverlapId));
  MidiEvent overlapOff = MidiEvent::NoteOff(900, channel, 94, 0);
  overlapOff.noteId = kOverlapId;
  store.push_back(overlapOff);

  reconcileChangedOverlapNoteIdsFromLiveStore(focus, store, channel, loopLength);
  TEST_ASSERT_TRUE(hasChangedOverlapNote(focus, kOverlapId));
}

void test_reconcile_changed_overlap_ids_ignores_missing_live_on_empty_session() {
  constexpr NoteId kOverlapId = 5;
  constexpr NoteId kNeighborId = 7;
  constexpr NoteId kMoverId = 3;
  constexpr uint32_t loopLength = 2304;
  constexpr uint8_t channel = 1;

  NoteEditFocus focus;
  focus.active = true;
  focus.movingNoteId = kMoverId;
  focus.baselineMap[kOverlapId] = {94, 100, 800, 1000};
  focus.baselineMap[kNeighborId] = {65, 100, 400, 600};
  focus.baselineMap[kMoverId] = {94, 100, 1000, 1200};

  MidiEventVec store;
  reconcileChangedOverlapNoteIdsFromLiveStore(focus, store, channel, loopLength);
  TEST_ASSERT_FALSE(hasChangedOverlapNote(focus, kOverlapId));
  TEST_ASSERT_FALSE(hasChangedOverlapNote(focus, kNeighborId));
  TEST_ASSERT_FALSE(noteEditFocusHasPendingBaselineMapDiff(focus, store, channel, loopLength));
}

void test_reconcile_changed_overlap_ids_keeps_hide_scratch_without_live() {
  constexpr NoteId kOverlapId = 5;
  constexpr NoteId kMoverId = 3;
  constexpr uint32_t loopLength = 2304;
  constexpr uint8_t channel = 1;

  NoteEditFocus focus;
  focus.active = true;
  focus.movingNoteId = kMoverId;
  focus.baselineMap[kOverlapId] = {94, 100, 800, 1000};
  focus.baselineMap[kMoverId] = {94, 100, 1000, 1200};
  OverlapNote scratch{};
  scratch.noteId = kOverlapId;
  scratch.baseline = focus.baselineMap[kOverlapId];
  scratch.state = OverlapNoteStoreState::Hidden;
  focus.overlapNotes[kOverlapId] = scratch;
  recordChangedOverlapNote(focus, kOverlapId);

  MidiEventVec store;
  reconcileChangedOverlapNoteIdsFromLiveStore(focus, store, channel, loopLength);
  TEST_ASSERT_TRUE(hasChangedOverlapNote(focus, kOverlapId));
  TEST_ASSERT_TRUE(noteEditFocusHasPendingBaselineMapDiff(focus, store, channel, loopLength));
}

void test_reconcile_changed_overlap_ids_forgets_when_live_matches_baseline() {
  constexpr NoteId kOverlapId = 5;
  constexpr NoteId kMoverId = 3;
  constexpr uint32_t loopLength = 2304;
  constexpr uint8_t channel = 1;

  NoteEditFocus focus;
  focus.active = true;
  focus.movingNoteId = kMoverId;
  focus.last = {94, 100, 1000, 1200};
  focus.baselineMap[kOverlapId] = {94, 100, 800, 1000};
  focus.baselineMap[kMoverId] = {94, 100, 1000, 1200};
  recordChangedOverlapNote(focus, kOverlapId);

  MidiEventVec store;
  store.push_back(noteOnWithNoteId(800, channel, 94, 100, kOverlapId));
  MidiEvent overlapOff = MidiEvent::NoteOff(1000, channel, 94, 0);
  overlapOff.noteId = kOverlapId;
  store.push_back(overlapOff);

  reconcileChangedOverlapNoteIdsFromLiveStore(focus, store, channel, loopLength);
  TEST_ASSERT_FALSE(hasChangedOverlapNote(focus, kOverlapId));
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

void test_populate_baseline_map_includes_linear_same_pitch_neighbor() {
  // session_20260804_210819: mover at 907 must see neighbor at 666 on pitch 64 as overlap candidate.
  NoteEditFocus focus;
  MidiEventVec flat;
  flat.push_back(noteOnWithNoteId(666, 1, 64, 100, 3));
  MidiEvent neighborOff = MidiEvent::NoteOff(815, 1, 64, 0);
  flat.push_back(neighborOff);
  flat.push_back(noteOnWithNoteId(907, 1, 64, 100, 4));
  MidiEvent moverOff = MidiEvent::NoteOff(1098, 1, 64, 0);
  flat.push_back(moverOff);

  rebuildNoteEditFocusFromStore(focus, flat, 1, 2400, 1);
  TEST_ASSERT_EQUAL_UINT32(4u, focus.movingNoteId);
  populateBaselineMapForEditClosure(focus, flat, flat, 1, 2400);
  TEST_ASSERT_TRUE(focus.baselineMap.count(3) > 0);
  TEST_ASSERT_TRUE(focus.baselineMap.count(4) > 0);
  TEST_ASSERT_EQUAL_UINT32(666u, focus.baselineMap[3].startTick);
  TEST_ASSERT_EQUAL_UINT32(815u, focus.baselineMap[3].endTick);
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

  focus.last.pitch = 67;
  TEST_ASSERT_TRUE(noteEditFocusHasPendingCommit(focus));
  focus.last = focus.commitBaseline;
  TEST_ASSERT_FALSE(noteEditFocusHasPendingCommit(focus));

  // Overlap pending is baselineMap vs live (not overlapNotes scratch).
  focus.movingNoteId = 9;
  focus.baselineMap[42] = {60, 64, 50, 80};
  focus.baselineMap[9] = focus.commitBaseline;
  recordChangedOverlapNote(focus, 42);
  MidiEventVec store;
  store.push_back(noteOnWithNoteId(100, 1, 60, 64, 9));
  store.push_back(MidiEvent::NoteOff(200, 1, 60, 0));
  TEST_ASSERT_TRUE(noteEditFocusHasPendingBaselineMapDiff(focus, store, 1, 768));
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

void test_can_apply_simple_pitch_change_blocks_store_channel_target_lane() {
  constexpr uint32_t kLoopLength = 768;
  constexpr uint8_t kTrackChannel = 1;
  constexpr uint8_t kStoreChannel = 5;
  NoteEditFocus focus;
  focus.active = true;
  focus.movingNoteId = 1;
  focus.commitBaseline = {60, 100, 100, 200};
  focus.last = focus.commitBaseline;
  focus.movingNoteRange = {100, 200};
  focus.baselineMap[1] = focus.commitBaseline;
  focus.baselineMap[2] = {64, 100, 150, 250};

  MidiEventVec events;
  events.push_back(noteOnWithNoteId(100, kStoreChannel, 60, 100, 1));
  events.push_back(MidiEvent::NoteOff(200, kStoreChannel, 60, 0));
  events.push_back(noteOnWithNoteId(150, kStoreChannel, 64, 100, 2));
  events.push_back(MidiEvent::NoteOff(250, kStoreChannel, 64, 0));

  TEST_ASSERT_FALSE(canApplySimplePitchChange(
      events, focus, kTrackChannel, 60, 64, focus.last.startTick, focus.last.endTick,
      kLoopLength));
}

void test_can_apply_simple_pitch_change_blocks_when_baseline_map_lane_needs_restore() {
  // session_20260804_213759: pipeline Hide on pitch 65 leaves overlapNotes empty; leaving
  // 65→64 took the simple path and never RestoreNote'd the hidden overlap note.
  constexpr uint32_t kLoopLength = 1536;
  constexpr NoteId kMoverId = 1;
  constexpr NoteId kOverlapId = 2;

  NoteEditFocus focus;
  focus.active = true;
  focus.movingNoteId = kMoverId;
  focus.commitBaseline = {64, 100, 906, 1055};
  focus.last = {65, 100, 906, 1055};
  focus.movingNoteRange = {906, 1055};
  focus.baselineMap[kMoverId] = focus.commitBaseline;
  focus.baselineMap[kOverlapId] = {65, 100, 906, 1001};

  MidiEventVec events;
  // Mover still on pitch 65; overlap note hidden (removed from live store).
  events.push_back(noteOnWithNoteId(906, 1, 65, 100, kMoverId));
  events.push_back(MidiEvent::NoteOff(1055, 1, 65, 0));
  events.back().noteId = kMoverId;

  TEST_ASSERT_FALSE(canApplySimplePitchChange(
      events, focus, 1, 65, 64, focus.last.startTick, focus.last.endTick, kLoopLength));

  // After RestoreNote, live matches baselineMap → simple path allowed again.
  events.push_back(noteOnWithNoteId(906, 1, 65, 100, kOverlapId));
  events.push_back(MidiEvent::NoteOff(1001, 1, 65, 0));
  events.back().noteId = kOverlapId;
  TEST_ASSERT_TRUE(canApplySimplePitchChange(
      events, focus, 1, 65, 64, focus.last.startTick, focus.last.endTick, kLoopLength));
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
  focus.baselineMap[1] = focus.commitBaseline;

  constexpr NoteId overlapHiddenId = 10;
  constexpr NoteId overlapShortId = 11;
  focus.baselineMap[overlapHiddenId] = {60, 64, 300, 350};
  focus.baselineMap[overlapShortId] = {60, 64, 400, 688};
  recordChangedOverlapNote(focus, overlapHiddenId);
  recordChangedOverlapNote(focus, overlapShortId);

  // Live store: hidden absent; shortened end 495; mover still at commit baseline ticks.
  MidiEventVec store;
  store.push_back(noteOnWithNoteId(400, 1, 60, 64, overlapShortId));
  store.push_back(MidiEvent::NoteOff(495, 1, 60, 0));
  store.back().noteId = overlapShortId;
  store.push_back(noteOnWithNoteId(496, 1, 60, 64, 1));
  store.push_back(MidiEvent::NoteOff(1168, 1, 60, 0));
  store.back().noteId = 1;

  const EditPassVec rows = buildPreCommitEditPasses(focus, 1, &store, 1536);
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
  focus.baselineMap[1] = focus.commitBaseline;

  constexpr NoteId overlapHiddenId = 12;
  focus.baselineMap[overlapHiddenId] = {60, 64, 584, 680};
  recordChangedOverlapNote(focus, overlapHiddenId);

  MidiEventVec sessionStore;
  sessionStore.push_back(noteOnWithNoteId(8, 1, 60, 64, 1));
  sessionStore.push_back(MidiEvent::NoteOff(680, 1, 60, 0));
  sessionStore.back().noteId = 1;

  const EditPassVec rows = buildPreCommitEditPasses(focus, 1, &sessionStore, kLoopLength);
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
  // Hidden notes are erased from the live store; baselineMap retains them for restore/commit.
  constexpr uint32_t kLoopLength = 1536;
  NoteEditFocus focus;
  focus.active = true;
  focus.commitBaseline = {67, 64, 8, 680};
  focus.movingNoteRange = {8, 680};

  MidiEventVec flat;
  flat.push_back(noteOnWithNoteId(8, 1, 67, 100, 1));
  flat.push_back(MidiEvent::NoteOff(680, 1, 67, 0));
  flat.push_back(noteOnWithNoteId(200, 1, 64, 100, 2));
  flat.push_back(MidiEvent::NoteOff(400, 1, 64, 0));
  // Pitch-60 note already hidden (absent from store).

  rebuildNoteEditFocusFromStore(focus, flat, 1, kLoopLength, 0);
  focus.baselineMap[12] = {60, 64, 584, 680};

  const NoteUtils::DisplayNoteVec filtered =
      projectNoteEditDisplayNotes(flat, focus, 1, kLoopLength);

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
  populateBaselineMapForEditClosure(focus, flat, flat, 1, kLoopLength);

  const NoteUtils::DisplayNoteVec filtered =
      projectNoteEditDisplayNotes(flat, focus, 1, kLoopLength);

  bool foundShortened = false;
  for (const NoteUtils::DisplayNote& dn : filtered) {
    if (dn.note == 60 && dn.startTick == 400 && dn.endTick == 495) {
      foundShortened = true;
    }
  }
  TEST_ASSERT_TRUE(foundShortened);
}

void test_filter_shortened_overlap_uses_live_span_when_reconstruct_mispairs() {
  // session_20260805_140106: overlap noteId=87 shortened to 192-239; reconstruct may LIFO-pair
  // to a stale untagged off and report length 95 on the display list.
  constexpr uint32_t kLoopLength = 1536;
  constexpr uint8_t channel = 1;
  constexpr NoteId kOverlapId = 87;
  constexpr NoteId kMoverId = 72;

  NoteEditFocus focus;
  focus.active = true;
  focus.movingNoteId = kMoverId;
  focus.last = {22, 100, 240, 335};
  focus.commitBaseline = {22, 100, 288, 479};
  focus.baselineMap[kOverlapId] = {22, 100, 192, 287};
  focus.baselineMap[kMoverId] = focus.commitBaseline;
  recordChangedOverlapNote(focus, kOverlapId);

  MidiEventVec store;
  store.push_back(noteOnWithNoteId(192, channel, 22, 100, kOverlapId));
  MidiEvent overlapOff = MidiEvent::NoteOff(239, channel, 22, 0);
  overlapOff.noteId = kOverlapId;
  store.push_back(overlapOff);
  store.push_back(MidiEvent::NoteOff(287, channel, 22, 0));
  store.push_back(noteOnWithNoteId(240, channel, 22, 100, kMoverId));
  MidiEvent moverOff = MidiEvent::NoteOff(335, channel, 22, 0);
  moverOff.noteId = kMoverId;
  store.push_back(moverOff);

  const NoteUtils::DisplayNoteVec filtered =
      projectNoteEditDisplayNotes(store, focus, channel, kLoopLength);

  bool foundShortenedOverlap = false;
  for (const NoteUtils::DisplayNote& dn : filtered) {
    if (dn.noteId == kOverlapId) {
      TEST_ASSERT_EQUAL_UINT32(192u, dn.startTick);
      TEST_ASSERT_EQUAL_UINT32(239u, dn.endTick);
      foundShortenedOverlap = true;
    }
    if (dn.noteId == kMoverId) {
      TEST_ASSERT_EQUAL_UINT32(240u, dn.startTick);
      TEST_ASSERT_EQUAL_UINT32(335u, dn.endTick);
    }
  }
  TEST_ASSERT_TRUE(foundShortenedOverlap);
}

void test_filter_excludes_inner_under_moving_note() {
  // Q14: cross-pitch notes under the mover stay selectable (no Hide/Shorten).
  constexpr uint32_t kLoopLength = 1536;
  NoteEditFocus focus;
  focus.active = true;
  focus.commitBaseline = {67, 64, 8, 680};
  focus.movingNoteRange = {8, 680};

  MidiEventVec flat;
  flat.push_back(noteOnWithNoteId(8, 1, 67, 100, 1));
  flat.push_back(MidiEvent::NoteOff(680, 1, 67, 0));
  flat.push_back(noteOnWithNoteId(392, 1, 60, 100, 2));
  flat.push_back(MidiEvent::NoteOff(488, 1, 60, 0));
  flat.push_back(noteOnWithNoteId(584, 1, 60, 100, 3));
  flat.push_back(MidiEvent::NoteOff(680, 1, 60, 0));

  rebuildNoteEditFocusFromStore(focus, flat, 1, kLoopLength, 0);
  populateBaselineMapForEditClosure(focus, flat, flat, 1, kLoopLength);

  const NoteUtils::DisplayNoteVec filtered =
      projectNoteEditDisplayNotes(flat, focus, 1, kLoopLength);

  TEST_ASSERT_EQUAL(3, static_cast<int>(filtered.size()));
  bool foundInner = false;
  for (const NoteUtils::DisplayNote& dn : filtered) {
    if (dn.note == 60 && dn.startTick == 584) {
      foundInner = true;
    }
  }
  TEST_ASSERT_TRUE(foundInner);
}

void test_filter_includes_moving_note_when_hidden_overlap_baseline_matches() {
  // Mover remains selectable when a same-span neighbor is hidden (absent) in baselineMap.
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

  focus.baselineMap[kHiddenId] = {60, 100, 576, 624};
  focus.last = {60, 100, 576, 624};

  const NoteUtils::DisplayNoteVec filtered =
      projectNoteEditDisplayNotes(flat, focus, channel, kLoopLength);

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
      projectNoteEditDisplayNotes(flat, focus, 1, kLoopLength);
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

  const EditPassVec rows = buildPreCommitEditPasses(focus, 1);
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

void test_selection_index_geometry_hold_when_primary_note_at_current_idx() {
  // RC10 / session_20260806_223833: overlap hide reorders list; highlight resolver may
  // return a new index while selectedNoteIdx still points at primaryNote — hold policy
  // keeps the cursor index when notes[selectedNoteIdx].noteId == primaryNote.
  constexpr NoteId kMoverId = 7;
  constexpr NoteId kOverlapId = 3;
  constexpr uint32_t kLoopLength = 1536;

  EditorSelection selection{};
  selection.primaryNote = kMoverId;
  selection.selectedTick = 660u;

  NoteEditFocus focus{};
  focus.active = true;
  focus.movingNoteId = kMoverId;
  focus.last = {65, 100, 660, 761};

  std::vector<NoteUtils::DisplayNote> before;
  before.push_back({kOverlapId, 65, 100, 609, 959});
  before.push_back({kMoverId, 65, 100, 660, 761});

  std::vector<NoteUtils::DisplayNote> after;
  after.push_back({kMoverId, 65, 100, 660, 761});

  const int idxBefore = NoteEditDisplaySnapshot::resolveNoteEditHighlightIndex(
      selection, before, focus, 0u, kLoopLength, false);
  const int idxAfter = NoteEditDisplaySnapshot::resolveNoteEditHighlightIndex(
      selection, after, focus, 0u, kLoopLength, false);

  TEST_ASSERT_EQUAL(1, idxBefore);
  TEST_ASSERT_EQUAL(0, idxAfter);
  TEST_ASSERT_TRUE(before[static_cast<size_t>(idxBefore)].noteId == kMoverId);
  TEST_ASSERT_TRUE(after[static_cast<size_t>(idxAfter)].noteId == kMoverId);
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

/// Materialized passes carry the channel played at record time. A span lookup that misses because
/// the track now reports a different output channel makes the note invisible to the whole geometry
/// pipeline (session_20260805_030517: baselineMap=1). NoteId resolves it on its own.
void test_find_linear_note_span_resolves_across_store_channel() {
  constexpr uint32_t kLoopLength = 2304;
  constexpr NoteId kNoteId = 62;
  MidiEventVec session;
  MidiEvent on = MidiEvent::NoteOn(2016, 5, 26, 100);
  on.noteId = kNoteId;
  session.push_back(on);
  MidiEvent off = MidiEvent::NoteOff(2112, 5, 26, 0);
  off.noteId = kNoteId;
  session.push_back(off);

  NoteBaseline linear{};
  TEST_ASSERT_TRUE(
      findLinearNoteSpanForNoteId(session, kNoteId, 2, linear, UINT32_MAX, kLoopLength));
  TEST_ASSERT_EQUAL_UINT32(2016u, linear.startTick);
  TEST_ASSERT_EQUAL_UINT32(2112u, linear.endTick);
  TEST_ASSERT_EQUAL_UINT8(26u, linear.pitch);
}

/// Note-offs must receive their note-on's id regardless of the track's output channel, otherwise
/// nothing downstream can pair the note by NoteId.
void test_stamp_note_ids_pairs_within_store_channel() {
  MidiEventVec session;
  MidiEvent onA = MidiEvent::NoteOn(0, 5, 26, 100);
  onA.noteId = 11;
  session.push_back(onA);
  MidiEvent onB = MidiEvent::NoteOn(48, 9, 26, 100);
  onB.noteId = 12;
  session.push_back(onB);
  session.push_back(MidiEvent::NoteOff(96, 9, 26, 0));
  session.push_back(MidiEvent::NoteOff(144, 5, 26, 0));

  stampNoteIdsOntoPairedNoteOffs(session);

  // Each off takes the id of the note-on on its own channel, never across channels.
  TEST_ASSERT_EQUAL_UINT32(12u, session[2].noteId);
  TEST_ASSERT_EQUAL_UINT32(11u, session[3].noteId);
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

void test_evict_overlap_scratch_when_overlap_target_selected() {
  constexpr NoteId kOverlapId = 10;
  constexpr NoteId kOtherOverlapId = 11;
  NoteEditFocus focus;
  focus.active = true;

  OverlapNote shortened{};
  shortened.noteId = kOverlapId;
  shortened.baseline = {67, 100, 426, 619};
  shortened.state = OverlapNoteStoreState::Shortened;
  shortened.shortenedEndTick = 569;
  focus.overlapNotes[kOverlapId] = shortened;

  OverlapNote hidden{};
  hidden.noteId = kOtherOverlapId;
  hidden.baseline = {60, 100, 619, 907};
  hidden.state = OverlapNoteStoreState::Hidden;
  focus.overlapNotes[kOtherOverlapId] = hidden;

  TEST_ASSERT_TRUE(evictOverlapScratchForSelectedNote(focus, kOverlapId));
  TEST_ASSERT_EQUAL(1, static_cast<int>(focus.overlapNotes.size()));
  TEST_ASSERT_NULL(findOverlapNoteEntry(focus, kOverlapId));
  TEST_ASSERT_NOT_NULL(findOverlapNoteEntry(focus, kOtherOverlapId));

  TEST_ASSERT_FALSE(evictOverlapScratchForSelectedNote(focus, kInvalidNoteId));
  TEST_ASSERT_FALSE(evictOverlapScratchForSelectedNote(focus, kOverlapId));
}

void test_select_overlap_target_evicts_shortened_scratch_session_log() {
  // session_20260804_180228 @519.5s: pitch=67 start=426 shortened to 569; select then move must not
  // leave overlapNotes self-collision on the mover.
  constexpr NoteId kOverlapId = 67;
  constexpr uint32_t kLoopLength = 1536;
  NoteEditFocus focus;
  focus.active = true;
  focus.movingNoteId = kOverlapId;

  OverlapNote shortened{};
  shortened.noteId = kOverlapId;
  shortened.baseline = {67, 100, 426, 619};
  shortened.state = OverlapNoteStoreState::Shortened;
  shortened.shortenedEndTick = 569;
  focus.overlapNotes[kOverlapId] = shortened;

  focus.commitBaseline = {67, 100, 426, 898};
  focus.last = focus.commitBaseline;
  focus.movingNoteRange = {426, 898};
  focus.baselineMap[kOverlapId] = {67, 100, 426, 619};

  TEST_ASSERT_TRUE(evictOverlapScratchForSelectedNote(focus, kOverlapId));
  TEST_ASSERT_EQUAL(0, static_cast<int>(focus.overlapNotes.size()));
  TEST_ASSERT_FALSE(noteEditFocusHasPendingCommit(focus));

  MidiEventVec session;
  MidiEvent on = MidiEvent::NoteOn(426, 1, 67, 100);
  on.noteId = kOverlapId;
  session.push_back(on);
  session.push_back(MidiEvent::NoteOff(898, 1, 67, 0));

  NoteBaseline linear{};
  TEST_ASSERT_TRUE(
      findLinearNoteSpanForNoteId(session, kOverlapId, 1, linear, 426, kLoopLength));
  TEST_ASSERT_EQUAL_UINT32(426u, linear.startTick);
  TEST_ASSERT_EQUAL_UINT32(898u, linear.endTick);
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

void test_find_linear_off_for_note_id_prefers_nearest_same_pitch_duplicate_off() {
  // session_20260805_122352.log: after overlap restore+move, a stale tagged off at the prior
  // mover end must not inflate focus.last or move length when a nearer off exists.
  constexpr NoteId kMoverId = 68;
  constexpr uint32_t kLoopLength = 2304;
  MidiEventVec session;
  MidiEvent moverOn = MidiEvent::NoteOn(288, 1, 23, 100);
  moverOn.noteId = kMoverId;
  session.push_back(moverOn);
  MidiEvent liveOff = MidiEvent::NoteOff(383, 1, 23, 0);
  liveOff.noteId = kMoverId;
  session.push_back(liveOff);
  MidiEvent staleOff = MidiEvent::NoteOff(431, 1, 23, 0);
  staleOff.noteId = kMoverId;
  session.push_back(staleOff);

  MidiEvent* off = findLinearOffForNoteId(session, moverOn, kMoverId, kLoopLength);
  TEST_ASSERT_NOT_NULL(off);
  TEST_ASSERT_EQUAL_UINT32(383u, off->tick);

  NoteEditFocus focus;
  focus.active = true;
  focus.movingNoteId = kMoverId;
  focus.commitBaseline = {23, 100, 1344, 1439};
  focus.last = {23, 100, 288, 431};
  focus.movingNoteRange = {288, 431};
  TEST_ASSERT_TRUE(syncNoteEditFocusLinearFromSessionStore(focus, session, 1, kLoopLength));
  TEST_ASSERT_EQUAL_UINT32(288u, focus.last.startTick);
  TEST_ASSERT_EQUAL_UINT32(383u, focus.last.endTick);
}

void test_find_linear_off_for_note_id_ignores_cross_pitch_same_note_id_off() {
  // session_20260804_205144.log: mover pitch 65 @1050 must not pair to pitch 64 off @1289.
  constexpr NoteId kSharedId = 42;
  constexpr uint32_t kLoopLength = 2400;
  MidiEventVec session;
  MidiEvent moverOn = MidiEvent::NoteOn(1050, 1, 65, 100);
  moverOn.noteId = kSharedId;
  session.push_back(moverOn);
  MidiEvent moverOff = MidiEvent::NoteOff(1145, 1, 65, 0);
  moverOff.noteId = kSharedId;
  session.push_back(moverOff);
  MidiEvent strayOff = MidiEvent::NoteOff(1289, 1, 64, 0);
  strayOff.noteId = kSharedId;
  session.push_back(strayOff);

  MidiEvent* off = findLinearOffForNoteId(session, moverOn, kSharedId, kLoopLength);
  TEST_ASSERT_NOT_NULL(off);
  TEST_ASSERT_EQUAL_UINT32(1145u, off->tick);
  TEST_ASSERT_EQUAL_UINT8(65, off->data.noteData.note);
}

void test_sync_linear_focus_ignores_cross_pitch_same_note_id_off() {
  constexpr NoteId kSharedId = 42;
  constexpr uint32_t kLoopLength = 2400;
  NoteEditFocus focus;
  focus.active = true;
  focus.movingNoteId = kSharedId;
  focus.commitBaseline = {65, 100, 1050, 1145};
  focus.last = focus.commitBaseline;
  focus.movingNoteRange = {1050, 1145};

  MidiEventVec session;
  MidiEvent moverOn = MidiEvent::NoteOn(1050, 1, 65, 100);
  moverOn.noteId = kSharedId;
  session.push_back(moverOn);
  MidiEvent moverOff = MidiEvent::NoteOff(1145, 1, 65, 0);
  moverOff.noteId = kSharedId;
  session.push_back(moverOff);
  MidiEvent strayOff = MidiEvent::NoteOff(1289, 1, 64, 0);
  strayOff.noteId = kSharedId;
  session.push_back(strayOff);

  TEST_ASSERT_TRUE(syncNoteEditFocusLinearFromSessionStore(focus, session, 1, kLoopLength));
  TEST_ASSERT_EQUAL_UINT32(1050u, focus.last.startTick);
  TEST_ASSERT_EQUAL_UINT32(1145u, focus.last.endTick);
  TEST_ASSERT_EQUAL_UINT8(65, focus.last.pitch);
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

  const auto filtered = projectNoteEditDisplayNotes(session, focus, 1, loopLength);
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

void test_baseline_map_diff_pending_commit_when_mover_unchanged() {
  constexpr uint32_t loopLength = 2304;
  constexpr uint8_t channel = 1;
  constexpr NoteId kInnerId = 3;
  constexpr NoteId kMoverId = 9;

  NoteEditFocus focus;
  focus.active = true;
  focus.movingNoteId = kMoverId;
  focus.commitBaseline = {23, 100, 192, 384};
  focus.last = focus.commitBaseline;
  focus.movingNoteRange = {192, 384};
  focus.baselineMap[kInnerId] = {93, 100, 144, 192};
  focus.baselineMap[kMoverId] = focus.commitBaseline;
  recordChangedOverlapNote(focus, kInnerId);

  MidiEventVec store;
  store.push_back(noteOnWithNoteId(192, channel, 23, 100, kMoverId));
  store.push_back(MidiEvent::NoteOff(384, channel, 23, 0));

  TEST_ASSERT_FALSE(noteEditFocusHasPendingCommit(focus));
  TEST_ASSERT_TRUE(
      noteEditFocusHasPendingBaselineMapDiff(focus, store, channel, loopLength));

  const EditPassVec rows = buildPreCommitEditPasses(focus, channel, &store, loopLength);
  TEST_ASSERT_EQUAL(1, static_cast<int>(rows.size()));
  TEST_ASSERT_EQUAL(static_cast<int>(EditActionType::Delete),
                    static_cast<int>(rows[0].actionType));
  TEST_ASSERT_EQUAL(kInnerId, rows[0].targetNoteId);
}

void test_baseline_map_diff_reads_store_channel_live_span() {
  constexpr uint32_t loopLength = 2304;
  constexpr uint8_t trackChannel = 2;
  constexpr uint8_t storeChannel = 5;
  constexpr NoteId kOverlapId = 91;
  constexpr NoteId kMoverId = 78;

  NoteEditFocus focus;
  focus.active = true;
  focus.movingNoteId = kMoverId;
  focus.commitBaseline = {25, 100, 960, 1344};
  focus.last = focus.commitBaseline;
  focus.baselineMap[kOverlapId] = {25, 100, 912, 1008};
  focus.baselineMap[kMoverId] = focus.commitBaseline;
  recordChangedOverlapNote(focus, kOverlapId);

  MidiEventVec store;
  store.push_back(noteOnWithNoteId(912, storeChannel, 25, 100, kOverlapId));
  MidiEvent shortenedOff = MidiEvent::NoteOff(959, storeChannel, 25, 0);
  shortenedOff.noteId = kOverlapId;
  store.push_back(shortenedOff);
  store.push_back(noteOnWithNoteId(960, storeChannel, 25, 100, kMoverId));
  MidiEvent moverOff = MidiEvent::NoteOff(1344, storeChannel, 25, 0);
  moverOff.noteId = kMoverId;
  store.push_back(moverOff);

  TEST_ASSERT_TRUE(
      noteEditFocusHasPendingBaselineMapDiff(focus, store, trackChannel, loopLength));

  const EditPassVec rows = buildPreCommitEditPasses(focus, trackChannel, &store, loopLength);
  TEST_ASSERT_EQUAL(1, static_cast<int>(rows.size()));
  TEST_ASSERT_EQUAL(static_cast<int>(EditActionType::Update),
                    static_cast<int>(rows[0].actionType));
  TEST_ASSERT_EQUAL(EditPropertyType::Length, rows[0].propertyType);
  TEST_ASSERT_EQUAL(kOverlapId, rows[0].targetNoteId);
  TEST_ASSERT_EQUAL_UINT32(959u, rows[0].endTick);
}

void test_session_171134_canonical_commit_rows_from_final_session_store() {
  // session_20260805_171134: commit must serialize what the edit became, not the
  // apply-owned action history. Mover noteId=36 moved left; overlap noteId=31 is hidden.
  constexpr uint32_t loopLength = 3072;
  constexpr uint8_t channel = 1;
  constexpr NoteId kOverlapId = 31;
  constexpr NoteId kMoverId = 36;

  NoteEditFocus focus;
  focus.active = true;
  focus.movingNoteId = kMoverId;
  focus.commitBaseline = {12, 100, 1488, 1679};
  focus.last = {12, 100, 1248, 1439};
  focus.baselineMap[kOverlapId] = {12, 100, 1296, 1391};
  focus.baselineMap[kMoverId] = focus.commitBaseline;
  recordChangedOverlapNote(focus, kOverlapId);

  MidiEventVec store;
  store.push_back(noteOnWithNoteId(1248, channel, 12, 100, kMoverId));
  MidiEvent moverOff = MidiEvent::NoteOff(1439, channel, 12, 0);
  moverOff.noteId = kMoverId;
  store.push_back(moverOff);

  TEST_ASSERT_TRUE(noteEditFocusHasPendingBaselineMapDiff(focus, store, channel, loopLength));

  const EditPassVec rows = buildPreCommitEditPasses(focus, channel, &store, loopLength);
  TEST_ASSERT_EQUAL(2, static_cast<int>(rows.size()));
  TEST_ASSERT_EQUAL(static_cast<int>(EditActionType::Delete),
                    static_cast<int>(rows[0].actionType));
  TEST_ASSERT_EQUAL(kOverlapId, rows[0].targetNoteId);
  TEST_ASSERT_EQUAL(static_cast<int>(EditActionType::Update),
                    static_cast<int>(rows[1].actionType));
  TEST_ASSERT_EQUAL(EditPropertyType::NoteRange, rows[1].propertyType);
  TEST_ASSERT_EQUAL(kMoverId, rows[1].targetNoteId);
  TEST_ASSERT_EQUAL_UINT32(1248u, rows[1].startTick);
  TEST_ASSERT_EQUAL_UINT32(1439u, rows[1].endTick);
}

void test_focus_rebuild_pending_after_pitch_move_stale_display_hint_session_193016() {
  // session_20260805_193016: pitch change + coarse move left session store at 2303/pitch 12 while
  // display cache still pointed at the pre-move tick; stale preferredStartTick missed the live span
  // and rebuild aligned commitBaseline to stale display, dropping pending commit on deselect.
  constexpr NoteId kMoverId = 36;
  constexpr uint32_t loopLength = 3072;

  MidiEventVec committed;
  MidiEvent committedOn = MidiEvent::NoteOn(1583, 1, 26, 100);
  committedOn.noteId = kMoverId;
  committed.push_back(committedOn);
  committed.push_back(MidiEvent::NoteOff(1774, 1, 26, 0));

  MidiEventVec session;
  MidiEvent liveOn = MidiEvent::NoteOn(2303, 1, 12, 100);
  liveOn.noteId = kMoverId;
  session.push_back(liveOn);
  session.push_back(MidiEvent::NoteOff(2494, 1, 12, 0));

  NoteBaseline committedSpan;
  TEST_ASSERT_TRUE(
      findLinearNoteSpanForNoteId(committed, kMoverId, 1, committedSpan, UINT32_MAX, loopLength));
  NoteBaseline liveSpan;
  TEST_ASSERT_TRUE(
      findLinearNoteSpanForNoteId(session, kMoverId, 1, liveSpan, UINT32_MAX, loopLength));
  TEST_ASSERT_FALSE(
      findLinearNoteSpanForNoteId(session, kMoverId, 1, liveSpan, 1583, loopLength));

  NoteEditFocus focus;
  focus.active = true;
  focus.movingNoteId = kMoverId;
  focus.commitBaseline = committedSpan;
  focus.last = liveSpan;
  focus.baselineMap[kMoverId] = committedSpan;
  focus.movingNoteRange.start = liveSpan.startTick;
  focus.movingNoteRange.end = liveSpan.endTick;

  TEST_ASSERT_TRUE(noteEditFocusHasPendingCommit(focus));

  const EditPassVec rows = buildPreCommitEditPasses(focus, 1, &session, loopLength);
  TEST_ASSERT_EQUAL(2, static_cast<int>(rows.size()));
  const auto noteRangeIt = std::find_if(rows.begin(), rows.end(), [](const EditPass& row) {
    return row.targetNoteId == kMoverId && row.propertyType == EditPropertyType::NoteRange;
  });
  const auto pitchIt = std::find_if(rows.begin(), rows.end(), [](const EditPass& row) {
    return row.targetNoteId == kMoverId && row.propertyType == EditPropertyType::Pitch;
  });
  TEST_ASSERT_TRUE(noteRangeIt != rows.end());
  TEST_ASSERT_TRUE(pitchIt != rows.end());
  TEST_ASSERT_EQUAL_UINT32(2303u, noteRangeIt->startTick);
  TEST_ASSERT_EQUAL_UINT8(12, pitchIt->pitch);
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

void test_project_pitch23_shortened_overlap_after_apply_actions() {
  // session_20260805_141706: ShortenNote noteId=83 (192-239) + MoveNote noteId=89 (240-431).
  constexpr uint32_t kLoopLength = 1536;
  constexpr uint8_t channel = 1;
  constexpr NoteId kOverlapId = 83;
  constexpr NoteId kMoverId = 89;

  NoteEditFocus focus;
  focus.active = true;
  focus.movingNoteId = kMoverId;
  focus.commitBaseline = {23, 100, 336, 527};
  focus.last = {23, 100, 288, 479};
  focus.baselineMap[kOverlapId] = {23, 100, 192, 287};
  focus.baselineMap[kMoverId] = focus.commitBaseline;

  MidiEventVec store;
  store.push_back(noteOnWithNoteId(192, channel, 23, 100, kOverlapId));
  store.push_back(MidiEvent::NoteOff(287, channel, 23, 0));
  store.push_back(noteOnWithNoteId(288, channel, 23, 100, kMoverId));
  store.push_back(MidiEvent::NoteOff(479, channel, 23, 0));
  const NoteUtils::DisplayNoteVec committedBase =
      NoteUtils::reconstructDisplayNotes(store, kLoopLength, false);

  EditSessionActions actions;
  EditSessionAction shorten{};
  shorten.type = EditSessionActionType::ShortenNote;
  shorten.targetNoteId = kOverlapId;
  shorten.startTick = 192;
  shorten.endTick = 239;
  shorten.pitch = 23;
  actions.push_back(shorten);
  EditSessionAction move{};
  move.type = EditSessionActionType::MoveNote;
  move.targetNoteId = kMoverId;
  move.startTick = 240;
  move.endTick = 431;
  move.pitch = 23;
  actions.push_back(move);
  applyEditSessionActions(actions, store, focus, channel, kLoopLength);

  const NoteUtils::DisplayNoteVec projected =
      projectNoteEditDisplayNotes(committedBase, store, focus, channel, kLoopLength);

  bool foundShortenedOverlap = false;
  for (const NoteUtils::DisplayNote& dn : projected) {
    if (dn.noteId == kOverlapId) {
      TEST_ASSERT_EQUAL_UINT32(192u, dn.startTick);
      TEST_ASSERT_EQUAL_UINT32(239u, dn.endTick);
      foundShortenedOverlap = true;
    }
    if (dn.noteId == kMoverId) {
      TEST_ASSERT_EQUAL_UINT32(240u, dn.startTick);
      TEST_ASSERT_EQUAL_UINT32(431u, dn.endTick);
    }
  }
  TEST_ASSERT_TRUE(foundShortenedOverlap);
}

void test_project_pitch22_hidden_overlap_after_apply_actions() {
  // session_20260805_141706: HideNote noteId=87 + MoveNote noteId=72 on pitch 22.
  constexpr uint32_t kLoopLength = 1536;
  constexpr uint8_t channel = 1;
  constexpr NoteId kOverlapId = 87;
  constexpr NoteId kMoverId = 72;

  NoteEditFocus focus;
  focus.active = true;
  focus.movingNoteId = kMoverId;
  focus.commitBaseline = {22, 100, 240, 479};
  focus.last = {22, 100, 192, 431};
  focus.baselineMap[kOverlapId] = {22, 100, 192, 287};
  focus.baselineMap[kMoverId] = focus.commitBaseline;

  MidiEventVec store;
  store.push_back(noteOnWithNoteId(192, channel, 22, 100, kOverlapId));
  store.push_back(MidiEvent::NoteOff(287, channel, 22, 0));
  store.push_back(noteOnWithNoteId(240, channel, 22, 100, kMoverId));
  store.push_back(MidiEvent::NoteOff(479, channel, 22, 0));
  const NoteUtils::DisplayNoteVec committedBase =
      NoteUtils::reconstructDisplayNotes(store, kLoopLength, false);

  EditSessionActions actions;
  EditSessionAction hide{};
  hide.type = EditSessionActionType::HideNote;
  hide.targetNoteId = kOverlapId;
  hide.startTick = 192;
  hide.endTick = 239;
  hide.pitch = 22;
  actions.push_back(hide);
  EditSessionAction move{};
  move.type = EditSessionActionType::MoveNote;
  move.targetNoteId = kMoverId;
  move.startTick = 192;
  move.endTick = 431;
  move.pitch = 22;
  actions.push_back(move);
  applyEditSessionActions(actions, store, focus, channel, kLoopLength);

  const NoteUtils::DisplayNoteVec projected =
      projectNoteEditDisplayNotes(committedBase, store, focus, channel, kLoopLength);

  for (const NoteUtils::DisplayNote& dn : projected) {
    TEST_ASSERT_FALSE(dn.noteId == kOverlapId);
    if (dn.noteId == kMoverId) {
      TEST_ASSERT_EQUAL_UINT32(192u, dn.startTick);
      TEST_ASSERT_EQUAL_UINT32(431u, dn.endTick);
    }
  }
}

void test_project_untouched_same_pitch_neighbor_keeps_reconstruction_not_live_span() {
  // session_20260805_143545: after overlap shorten on pitch 12, untouched same-pitch grid notes
  // must not inherit the mover's distant off via findLinearNoteSpan live overlay.
  constexpr uint32_t kLoopLength = 3072;
  constexpr uint8_t channel = 1;
  constexpr NoteId kOverlapId = 74;
  constexpr NoteId kMoverId = 76;
  constexpr NoteId kGridId = 99;

  NoteEditFocus focus;
  focus.active = true;
  focus.movingNoteId = kMoverId;
  focus.last = {12, 100, 2160, 2351};
  focus.commitBaseline = {12, 100, 2160, 2351};
  focus.baselineMap[kOverlapId] = {12, 100, 2112, 2495};
  focus.baselineMap[kMoverId] = focus.commitBaseline;
  focus.baselineMap[kGridId] = {12, 100, 2208, 2255};
  recordChangedOverlapNote(focus, kOverlapId);

  MidiEventVec store;
  store.push_back(noteOnWithNoteId(2112, channel, 12, 100, kOverlapId));
  MidiEvent overlapOff = MidiEvent::NoteOff(2159, channel, 12, 0);
  overlapOff.noteId = kOverlapId;
  store.push_back(overlapOff);
  store.push_back(noteOnWithNoteId(2160, channel, 12, 100, kMoverId));
  MidiEvent moverOff = MidiEvent::NoteOff(2351, channel, 12, 0);
  moverOff.noteId = kMoverId;
  store.push_back(moverOff);
  store.push_back(noteOnWithNoteId(2208, channel, 12, 100, kGridId));
  MidiEvent gridOff = MidiEvent::NoteOff(2255, channel, 12, 0);
  gridOff.noteId = kGridId;
  store.push_back(gridOff);

  const NoteUtils::DisplayNoteVec committedBase =
      NoteUtils::reconstructDisplayNotes(store, kLoopLength, false);

  const NoteUtils::DisplayNoteVec projected =
      projectNoteEditDisplayNotes(committedBase, store, focus, channel, kLoopLength);

  bool foundGrid = false;
  for (const NoteUtils::DisplayNote& dn : projected) {
    if (dn.noteId == kGridId) {
      TEST_ASSERT_EQUAL_UINT32(2208u, dn.startTick);
      TEST_ASSERT_EQUAL_UINT32(2255u, dn.endTick);
      foundGrid = true;
    }
  }
  TEST_ASSERT_TRUE(foundGrid);
}

void test_project_pitch_restore_does_not_append_committed_overlap_161117() {
  // session_20260805_161117: restored committed noteId=40 must not append when committed base
  // lacks stable NoteId match (visual-cache note without id).
  constexpr uint32_t kLoopLength = 3072;
  constexpr uint8_t channel = 1;
  constexpr NoteId kMoverId = 23;
  constexpr NoteId kRestoredId = 40;

  NoteUtils::DisplayNoteVec committedBase;
  committedBase.push_back({kInvalidNoteId, 30, 100, 0, 3071});
  committedBase.push_back({kMoverId, 29, 100, 1296, 1487});

  NoteEditFocus focus;
  focus.active = true;
  focus.movingNoteId = kMoverId;
  focus.commitBaseline = {29, 100, 1296, 1487};
  focus.last = {29, 100, 1296, 1487};
  focus.baselineMap[kRestoredId] = {30, 100, 0, 3071};
  focus.baselineMap[kMoverId] = focus.commitBaseline;
  recordChangedOverlapNote(focus, kRestoredId);

  MidiEventVec store;
  store.push_back(noteOnWithNoteId(0, channel, 30, 100, kRestoredId));
  MidiEvent off40 = MidiEvent::NoteOff(3071, channel, 30, 0);
  off40.noteId = kRestoredId;
  store.push_back(off40);
  store.push_back(noteOnWithNoteId(1296, channel, 29, 100, kMoverId));
  MidiEvent off23 = MidiEvent::NoteOff(1487, channel, 29, 0);
  off23.noteId = kMoverId;
  store.push_back(off23);

  const NoteUtils::DisplayNoteVec projected =
      projectNoteEditDisplayNotes(committedBase, store, focus, channel, kLoopLength);
  TEST_ASSERT_EQUAL(committedBase.size(), projected.size());
  bool foundRestored = false;
  for (const NoteUtils::DisplayNote& dn : projected) {
    if (dn.noteId == kRestoredId) {
      foundRestored = true;
      TEST_ASSERT_EQUAL_UINT8(30, dn.note);
      TEST_ASSERT_EQUAL_UINT32(0u, dn.startTick);
      TEST_ASSERT_EQUAL_UINT32(3071u, dn.endTick);
    }
  }
  TEST_ASSERT_TRUE(foundRestored);
}

void test_project_non_participant_baseline_note_is_copied_when_live_lookup_misses_161117() {
  // session_20260805_161117: non-participant baseline notes must copy from committed base when
  // live session-store lookup misses (display shrink regression while browsing).
  constexpr uint32_t kLoopLength = 3072;
  constexpr uint8_t channel = 1;
  constexpr NoteId kMoverId = 23;
  constexpr NoteId kStableId = 55;

  NoteUtils::DisplayNoteVec committedBase;
  committedBase.push_back({kStableId, 12, 100, 1152, 1343});
  committedBase.push_back({kMoverId, 29, 100, 1296, 1487});

  NoteEditFocus focus;
  focus.active = true;
  focus.movingNoteId = kMoverId;
  focus.commitBaseline = {29, 100, 1296, 1487};
  focus.last = {29, 100, 1296, 1487};
  focus.baselineMap[kStableId] = {12, 100, 1152, 1343};
  focus.baselineMap[kMoverId] = focus.commitBaseline;

  MidiEventVec store;
  store.push_back(noteOnWithNoteId(1296, channel, 29, 100, kMoverId));
  MidiEvent off23 = MidiEvent::NoteOff(1487, channel, 29, 0);
  off23.noteId = kMoverId;
  store.push_back(off23);

  const NoteUtils::DisplayNoteVec projected =
      projectNoteEditDisplayNotes(committedBase, store, focus, channel, kLoopLength);
  TEST_ASSERT_EQUAL(committedBase.size(), projected.size());
  bool foundStable = false;
  for (const NoteUtils::DisplayNote& dn : projected) {
    if (dn.noteId == kStableId) {
      foundStable = true;
      TEST_ASSERT_EQUAL_UINT8(12, dn.note);
      TEST_ASSERT_EQUAL_UINT32(1152u, dn.startTick);
      TEST_ASSERT_EQUAL_UINT32(1343u, dn.endTick);
    }
  }
  TEST_ASSERT_TRUE(foundStable);
}

void test_project_hidden_changed_overlap_not_selectable_when_live_lookup_misses_162324() {
  // session_20260805_162324: overlap hidden via HideNote must not remain selectable from
  // committed base when live session-store lookup misses (reselect commit would Delete it).
  constexpr uint32_t kLoopLength = 3072;
  constexpr uint8_t channel = 1;
  constexpr NoteId kMoverId = 31;
  constexpr NoteId kHiddenId = 26;

  NoteUtils::DisplayNoteVec committedBase;
  committedBase.push_back({kHiddenId, 26, 100, 1248, 1343});
  committedBase.push_back({kMoverId, 26, 100, 1296, 1391});

  NoteEditFocus focus;
  focus.active = true;
  focus.movingNoteId = kMoverId;
  focus.commitBaseline = {26, 100, 1296, 1391};
  focus.last = {26, 100, 1200, 1295};
  focus.baselineMap[kHiddenId] = {26, 100, 1248, 1343};
  focus.baselineMap[kMoverId] = focus.commitBaseline;
  recordChangedOverlapNote(focus, kHiddenId);

  MidiEventVec store;
  store.push_back(noteOnWithNoteId(1200, channel, 26, 100, kMoverId));
  MidiEvent off31 = MidiEvent::NoteOff(1295, channel, 26, 0);
  off31.noteId = kMoverId;
  store.push_back(off31);

  const NoteUtils::DisplayNoteVec projected =
      projectNoteEditDisplayNotes(committedBase, store, focus, channel, kLoopLength);
  for (const NoteUtils::DisplayNote& dn : projected) {
    TEST_ASSERT_FALSE(dn.noteId == kHiddenId);
  }
  bool foundMover = false;
  for (const NoteUtils::DisplayNote& dn : projected) {
    if (dn.noteId == kMoverId) {
      foundMover = true;
      TEST_ASSERT_EQUAL_UINT32(1200u, dn.startTick);
      TEST_ASSERT_EQUAL_UINT32(1295u, dn.endTick);
    }
  }
  TEST_ASSERT_TRUE(foundMover);
}

void test_session_163142_highlight_resolver_prefers_mover_over_shortened_overlap() {
  // session_20260805_163142: after commit, stale selectedTick (overlap display start) must not
  // win over focus.last mover span on the same pitch lane.
  constexpr uint32_t kLoopLength = 3072;
  constexpr uint32_t kLoopStart = 960;
  constexpr NoteId kOverlapId = 34;
  constexpr NoteId kMoverId = 37;

  NoteEditFocus focus;
  focus.active = true;
  focus.movingNoteId = kMoverId;
  focus.last = {30, 100, 2256, 2351};
  focus.commitBaseline = focus.last;

  EditorSelection selection;
  selection.primaryNote = kMoverId;
  selection.selectedTick = 1248;
  selection.selectedNotes.push_back(kMoverId);

  std::vector<NoteUtils::DisplayNote> filtered;
  filtered.push_back({10, 12, 100, 1920, 2015});
  filtered.push_back({kOverlapId, 30, 100, 2208, 2255});
  filtered.push_back({kMoverId, 30, 100, 2256, 2351});

  const int byStaleTick = NoteEditDisplaySnapshot::filteredDisplayNoteIndexForSelection(
      selection, filtered, kLoopStart, kLoopLength, false);
  TEST_ASSERT_EQUAL(-1, byStaleTick);

  const int byFocus = NoteEditDisplaySnapshot::resolveNoteEditHighlightIndex(
      selection, filtered, focus, kLoopStart, kLoopLength, false);
  TEST_ASSERT_EQUAL(2, byFocus);
  TEST_ASSERT_EQUAL(kMoverId, filtered[static_cast<size_t>(byFocus)].noteId);
  TEST_ASSERT_EQUAL_UINT32(2256u, filtered[static_cast<size_t>(byFocus)].startTick);
  TEST_ASSERT_EQUAL_UINT32(95u,
                           filtered[static_cast<size_t>(byFocus)].endTick -
                               filtered[static_cast<size_t>(byFocus)].startTick);
}

void test_session_163142_moving_note_index_uses_loop_origin_display_bracket() {
  constexpr uint32_t kLoopLength = 3072;
  constexpr uint32_t kLoopStart = 960;
  constexpr NoteId kMoverId = 37;

  std::vector<NoteUtils::DisplayNote> filtered;
  filtered.push_back({34, 30, 100, 2208, 2255});
  filtered.push_back({kMoverId, 30, 100, 2256, 2351});

  TEST_ASSERT_EQUAL(1, filteredDisplayNoteIndexForMovingNote(filtered, kMoverId, 2256u,
                                                              kLoopStart, kLoopLength));
}

void test_project_post_commit_no_phantom_note_153954() {
  // session_20260805_153954: commit rows target noteId=35 length and noteId=31 move only, but full
  // session-store reconstruction grows the display list by one phantom note on pitch 12.
  constexpr uint32_t kLoopLength = 3072;
  constexpr uint8_t channel = 1;
  constexpr NoteId kMoverId = 31;
  constexpr NoteId kLengthId = 35;
  constexpr NoteId kOverlapId = 23;
  constexpr NoteId kGridId = 99;
  constexpr NoteId kPitch71Id = 41;

  MidiEventVec committedEvents;
  committedEvents.push_back(noteOnWithNoteId(1776, channel, 12, 100, kOverlapId));
  MidiEvent off23 = MidiEvent::NoteOff(1967, channel, 12, 0);
  off23.noteId = kOverlapId;
  committedEvents.push_back(off23);
  committedEvents.push_back(noteOnWithNoteId(1920, channel, 12, 100, kMoverId));
  MidiEvent off31 = MidiEvent::NoteOff(2016, channel, 12, 0);
  off31.noteId = kMoverId;
  committedEvents.push_back(off31);
  committedEvents.push_back(noteOnWithNoteId(2208, channel, 12, 100, kGridId));
  MidiEvent off99 = MidiEvent::NoteOff(2255, channel, 12, 0);
  off99.noteId = kGridId;
  committedEvents.push_back(off99);
  committedEvents.push_back(noteOnWithNoteId(2304, channel, 12, 100, kLengthId));
  MidiEvent off35 = MidiEvent::NoteOff(2496, channel, 12, 0);
  off35.noteId = kLengthId;
  committedEvents.push_back(off35);
  committedEvents.push_back(noteOnWithNoteId(720, channel, 71, 100, kPitch71Id));
  MidiEvent off71 = MidiEvent::NoteOff(767, channel, 71, 0);
  off71.noteId = kPitch71Id;
  committedEvents.push_back(off71);

  const NoteUtils::DisplayNoteVec committedBase =
      NoteUtils::reconstructDisplayNotes(committedEvents, kLoopLength, false);

  NoteEditFocus focus;
  focus.active = true;
  focus.movingNoteId = kMoverId;
  focus.commitBaseline = {12, 100, 1920, 2016};
  focus.last = {12, 100, 2352, 2447};
  focus.baselineMap[kOverlapId] = {12, 100, 1776, 1967};
  focus.baselineMap[kMoverId] = focus.commitBaseline;
  focus.baselineMap[kLengthId] = {12, 100, 2304, 2496};
  focus.baselineMap[kGridId] = {12, 100, 2208, 2255};
  focus.baselineMap[kPitch71Id] = {71, 100, 720, 767};
  recordChangedOverlapNote(focus, kLengthId);

  MidiEventVec store = committedEvents;

  EditSessionActions actions;
  EditSessionAction changeLength{};
  changeLength.type = EditSessionActionType::ChangeLength;
  changeLength.targetNoteId = kLengthId;
  changeLength.startTick = 2304;
  changeLength.endTick = 2351;
  changeLength.pitch = 12;
  changeLength.velocity = 100;
  actions.push_back(changeLength);
  EditSessionAction move{};
  move.type = EditSessionActionType::MoveNote;
  move.targetNoteId = kMoverId;
  move.startTick = 2352;
  move.endTick = 2447;
  move.pitch = 12;
  move.velocity = 100;
  actions.push_back(move);
  applyEditSessionActions(actions, store, focus, channel, kLoopLength);
  // Untagged same-pitch pair in the mutable store inflates full reconstruction without a committed
  // visual-cache counterpart (session_153954 class phantom).
  store.push_back(MidiEvent::NoteOn(2064, channel, 12, 100));
  store.push_back(MidiEvent::NoteOff(2111, channel, 12, 0));

  const NoteUtils::DisplayNoteVec fullRecon =
      NoteUtils::reconstructDisplayNotes(store, kLoopLength, false);
  TEST_ASSERT_GREATER_THAN(committedBase.size(), fullRecon.size());

  const NoteUtils::DisplayNoteVec legacyProjected =
      projectNoteEditDisplayNotes(store, focus, channel, kLoopLength);
  TEST_ASSERT_GREATER_THAN(committedBase.size(), legacyProjected.size());

  const NoteUtils::DisplayNoteVec projected =
      projectNoteEditDisplayNotes(committedBase, store, focus, channel, kLoopLength);
  TEST_ASSERT_EQUAL(committedBase.size(), projected.size());

  for (const NoteUtils::DisplayNote& committed : committedBase) {
    if (committed.noteId == kMoverId || committed.noteId == kLengthId) {
      continue;
    }
    bool found = false;
    for (const NoteUtils::DisplayNote& dn : projected) {
      if (dn.noteId != committed.noteId) {
        continue;
      }
      found = true;
      TEST_ASSERT_EQUAL_UINT8(committed.note, dn.note);
      TEST_ASSERT_EQUAL_UINT32(committed.startTick, dn.startTick);
      TEST_ASSERT_EQUAL_UINT32(committed.endTick, dn.endTick);
      TEST_ASSERT_TRUE(dn.endTick < kLoopLength);
    }
    TEST_ASSERT_TRUE(found);
  }

  bool foundLength = false;
  bool foundMover = false;
  for (const NoteUtils::DisplayNote& dn : projected) {
    if (dn.noteId == kLengthId) {
      foundLength = true;
      TEST_ASSERT_EQUAL_UINT32(2304u, dn.startTick);
      TEST_ASSERT_EQUAL_UINT32(2351u, dn.endTick);
    }
    if (dn.noteId == kMoverId) {
      foundMover = true;
      TEST_ASSERT_EQUAL_UINT32(2352u, dn.startTick);
      TEST_ASSERT_EQUAL_UINT32(2447u, dn.endTick);
    }
  }
  TEST_ASSERT_TRUE(foundLength);
  TEST_ASSERT_TRUE(foundMover);
}

void test_project_pitch65_outer_shorten_inner_move_214302() {
  // session_20260806_214302: inner noteId=7 moves left through outer noteId=3 on pitch 65.
  // Geometry emits ShortenNote on outer + MoveNote on inner; projection must show live store
  // spans (one bar per NoteId) without keeping the pre-shorten committed outer length.
  constexpr uint32_t kLoopLength = 1536;
  constexpr uint8_t channel = 1;
  constexpr NoteId kEarlyId = 1;
  constexpr NoteId kMidId = 2;
  constexpr NoteId kOuterId = 3;
  constexpr NoteId kNestedId = 6;
  constexpr NoteId kMoverId = 7;

  MidiEventVec store;
  store.push_back(noteOnWithNoteId(378, channel, 65, 100, kEarlyId));
  store.push_back(MidiEvent::NoteOff(426, channel, 65, 0));
  store.push_back(noteOnWithNoteId(714, channel, 65, 100, kMidId));
  store.push_back(MidiEvent::NoteOff(762, channel, 65, 0));
  store.push_back(noteOnWithNoteId(609, channel, 65, 100, kOuterId));
  store.push_back(MidiEvent::NoteOff(959, channel, 65, 0));
  store.push_back(noteOnWithNoteId(1050, channel, 65, 100, kNestedId));
  store.push_back(MidiEvent::NoteOff(1139, channel, 65, 0));
  store.push_back(noteOnWithNoteId(1044, channel, 65, 100, kMoverId));
  store.push_back(MidiEvent::NoteOff(1145, channel, 65, 0));

  const NoteUtils::DisplayNoteVec committedBase =
      NoteUtils::reconstructDisplayNotes(store, kLoopLength, false);
  TEST_ASSERT_EQUAL(5, static_cast<int>(committedBase.size()));

  NoteEditFocus focus;
  focus.active = true;
  focus.movingNoteId = kMoverId;
  focus.commitBaseline = {65, 100, 1044, 1145};
  focus.last = {65, 100, 948, 1049};
  focus.baselineMap[kEarlyId] = {65, 100, 378, 426};
  focus.baselineMap[kMidId] = {65, 100, 714, 762};
  focus.baselineMap[kOuterId] = {65, 100, 609, 959};
  focus.baselineMap[kNestedId] = {65, 100, 1050, 1139};
  focus.baselineMap[kMoverId] = focus.commitBaseline;

  EditSessionActions actions;
  EditSessionAction restore{};
  restore.type = EditSessionActionType::RestoreNote;
  restore.targetNoteId = kNestedId;
  restore.startTick = 1050;
  restore.endTick = 1139;
  restore.pitch = 65;
  actions.push_back(restore);
  EditSessionAction shorten{};
  shorten.type = EditSessionActionType::ShortenNote;
  shorten.targetNoteId = kOuterId;
  shorten.startTick = 609;
  shorten.endTick = 947;
  shorten.pitch = 65;
  actions.push_back(shorten);
  EditSessionAction move{};
  move.type = EditSessionActionType::MoveNote;
  move.targetNoteId = kMoverId;
  move.startTick = 948;
  move.endTick = 1049;
  move.pitch = 65;
  actions.push_back(move);
  applyEditSessionActions(actions, store, focus, channel, kLoopLength);

  const NoteUtils::DisplayNoteVec projected =
      projectNoteEditDisplayNotes(committedBase, store, focus, channel, kLoopLength);

  TEST_ASSERT_EQUAL(committedBase.size(), projected.size());

  std::unordered_map<NoteId, int> idCounts;
  for (const NoteUtils::DisplayNote& dn : projected) {
    if (dn.noteId == kInvalidNoteId) {
      continue;
    }
    ++idCounts[dn.noteId];
    TEST_ASSERT_EQUAL(1, idCounts[dn.noteId]);
  }

  bool foundOuter = false;
  bool foundMover = false;
  bool foundNested = false;
  for (const NoteUtils::DisplayNote& dn : projected) {
    if (dn.noteId == kOuterId) {
      TEST_ASSERT_EQUAL_UINT32(609u, dn.startTick);
      TEST_ASSERT_EQUAL_UINT32(947u, dn.endTick);
      foundOuter = true;
    }
    if (dn.noteId == kMoverId) {
      TEST_ASSERT_EQUAL_UINT32(948u, dn.startTick);
      TEST_ASSERT_EQUAL_UINT32(1049u, dn.endTick);
      foundMover = true;
    }
    if (dn.noteId == kNestedId) {
      TEST_ASSERT_EQUAL_UINT32(1050u, dn.startTick);
      TEST_ASSERT_EQUAL_UINT32(1139u, dn.endTick);
      foundNested = true;
    }
  }
  TEST_ASSERT_TRUE(foundOuter);
  TEST_ASSERT_TRUE(foundMover);
  TEST_ASSERT_TRUE(foundNested);
}

void test_project_pitch65_hidden_nested_not_in_display_214302() {
  // session_20260806_214302: after HideNote on nested noteId=6, projection must not paint it
  // from the committed base while storeNoteOns drops (DISP 5→4 class).
  constexpr uint32_t kLoopLength = 1536;
  constexpr uint8_t channel = 1;
  constexpr NoteId kOuterId = 3;
  constexpr NoteId kNestedId = 6;
  constexpr NoteId kMoverId = 7;

  MidiEventVec store;
  store.push_back(noteOnWithNoteId(609, channel, 65, 100, kOuterId));
  store.push_back(MidiEvent::NoteOff(959, channel, 65, 0));
  store.push_back(noteOnWithNoteId(1050, channel, 65, 100, kNestedId));
  store.push_back(MidiEvent::NoteOff(1139, channel, 65, 0));
  store.push_back(noteOnWithNoteId(1044, channel, 65, 100, kMoverId));
  store.push_back(MidiEvent::NoteOff(1145, channel, 65, 0));

  const NoteUtils::DisplayNoteVec committedBase =
      NoteUtils::reconstructDisplayNotes(store, kLoopLength, false);
  TEST_ASSERT_EQUAL(3, static_cast<int>(committedBase.size()));

  NoteEditFocus focus;
  focus.active = true;
  focus.movingNoteId = kMoverId;
  focus.commitBaseline = {65, 100, 1044, 1145};
  focus.last = {65, 100, 996, 1097};
  focus.baselineMap[kOuterId] = {65, 100, 609, 959};
  focus.baselineMap[kNestedId] = {65, 100, 1050, 1139};
  focus.baselineMap[kMoverId] = focus.commitBaseline;

  EditSessionActions actions;
  EditSessionAction hide{};
  hide.type = EditSessionActionType::HideNote;
  hide.targetNoteId = kNestedId;
  hide.startTick = 1050;
  hide.endTick = 1139;
  hide.pitch = 65;
  actions.push_back(hide);
  EditSessionAction move{};
  move.type = EditSessionActionType::MoveNote;
  move.targetNoteId = kMoverId;
  move.startTick = 996;
  move.endTick = 1097;
  move.pitch = 65;
  actions.push_back(move);
  applyEditSessionActions(actions, store, focus, channel, kLoopLength);

  const NoteUtils::DisplayNoteVec projected =
      projectNoteEditDisplayNotes(committedBase, store, focus, channel, kLoopLength);

  TEST_ASSERT_EQUAL(2, static_cast<int>(projected.size()));
  for (const NoteUtils::DisplayNote& dn : projected) {
    TEST_ASSERT_FALSE(dn.noteId == kNestedId);
  }
}

void test_project_pitch65_visual_cache_lane_bar_not_left_alongside_participants_214302() {
  // Visual cache can retain a kInvalidNoteId lane-wide bar alongside per-NoteId rows on the same
  // pitch. After overlap shorten, that ghost row must not survive next to live participant spans.
  constexpr uint32_t kLoopLength = 1536;
  constexpr uint8_t channel = 1;
  constexpr NoteId kOuterId = 3;
  constexpr NoteId kNestedId = 6;
  constexpr NoteId kMoverId = 7;

  NoteUtils::DisplayNoteVec committedBase;
  committedBase.push_back({kInvalidNoteId, 65, 100, 378, 1145});
  committedBase.push_back({kOuterId, 65, 100, 609, 959});
  committedBase.push_back({kNestedId, 65, 100, 1050, 1139});
  committedBase.push_back({kMoverId, 65, 100, 1044, 1145});

  MidiEventVec store;
  store.push_back(noteOnWithNoteId(609, channel, 65, 100, kOuterId));
  store.push_back(MidiEvent::NoteOff(947, channel, 65, 0));
  store.push_back(noteOnWithNoteId(1050, channel, 65, 100, kNestedId));
  store.push_back(MidiEvent::NoteOff(1139, channel, 65, 0));
  store.push_back(noteOnWithNoteId(948, channel, 65, 100, kMoverId));
  store.push_back(MidiEvent::NoteOff(1049, channel, 65, 0));

  NoteEditFocus focus;
  focus.active = true;
  focus.movingNoteId = kMoverId;
  focus.commitBaseline = {65, 100, 1044, 1145};
  focus.last = {65, 100, 948, 1049};
  focus.baselineMap[kOuterId] = {65, 100, 609, 959};
  focus.baselineMap[kNestedId] = {65, 100, 1050, 1139};
  focus.baselineMap[kMoverId] = focus.commitBaseline;
  recordChangedOverlapNote(focus, kOuterId);
  recordChangedOverlapNote(focus, kNestedId);

  const NoteUtils::DisplayNoteVec projected =
      projectNoteEditDisplayNotes(committedBase, store, focus, channel, kLoopLength);

  for (const NoteUtils::DisplayNote& dn : projected) {
    TEST_ASSERT_FALSE(dn.noteId == kInvalidNoteId && dn.note == 65);
    if (dn.noteId == kOuterId) {
      TEST_ASSERT_EQUAL_UINT32(609u, dn.startTick);
      TEST_ASSERT_EQUAL_UINT32(947u, dn.endTick);
    }
  }
  TEST_ASSERT_EQUAL(3, static_cast<int>(projected.size()));
}

void test_display_fingerprint_changes_when_overlap_geometry_changes() {
  NoteEditFocus focus;
  focus.active = true;
  focus.movingNoteId = 72;
  focus.last = {22, 100, 240, 335};
  const uint32_t fpBefore = noteEditDisplayCacheFingerprint(focus);
  recordChangedOverlapNote(focus, 87);
  const uint32_t fpAfter = noteEditDisplayCacheFingerprint(focus, nullptr);
  TEST_ASSERT_NOT_EQUAL(fpBefore, fpAfter);

  NoteEditCurrentState currentState;
  currentState.upsertRow(87, {22, 100, 100, 200}, {22, 100, 100, 200},
                         NoteEditPresenceType::Visible);
  const uint32_t fpWithState = noteEditDisplayCacheFingerprint(focus, &currentState);
  TEST_ASSERT_NOT_EQUAL(fpAfter, fpWithState);
}

void test_is_live_edit_driver_valid_rejects_id_match_span_mismatch() {
  constexpr uint32_t loopLength = 1536;
  constexpr NoteId kInnerId = 7;
  constexpr NoteId kOuterId = 8;
  EditorSelection sel{};
  NoteEditFocus focus;
  MidiEventVec session;
  session.push_back(noteOnWithNoteId(804, 1, 65, 100, kInnerId));
  session.push_back(MidiEvent::NoteOff(899, 1, 65, 0));
  session.push_back(noteOnWithNoteId(1185, 1, 65, 100, kOuterId));
  session.push_back(MidiEvent::NoteOff(1535, 1, 65, 0));

  focus.active = true;
  focus.movingNoteId = kInnerId;
  focus.last = {65, 100, 1185, 1535};
  sel.primaryNote = kInnerId;
  sel.selectedNotes.push_back(kInnerId);

  TEST_ASSERT_TRUE(editorSelectionMatchesDriverNote(sel, focus.movingNoteId));
  TEST_ASSERT_FALSE(isLiveEditDriverValid(sel, focus, session, 1, loopLength));

  focus.last = {65, 100, 804, 899};
  TEST_ASSERT_TRUE(isLiveEditDriverValid(sel, focus, session, 1, loopLength));

  sel.primaryNote = kOuterId;
  sel.selectedNotes[0] = kOuterId;
  focus.movingNoteId = kOuterId;
  focus.last = {65, 100, 1185, 1535};
  TEST_ASSERT_TRUE(isLiveEditDriverValid(sel, focus, session, 1, loopLength));
}

void test_pre_commit_rejects_mover_note_range_zero_start_after_nonzero_baseline() {
  constexpr uint32_t loopLength = 1536;
  constexpr NoteId kMoverId = 3;
  NoteEditFocus focus;
  focus.active = true;
  focus.movingNoteId = kMoverId;
  focus.commitBaseline = {65, 100, 1233, 1583};
  focus.last = {65, 100, 0, 1145};

  MidiEventVec session;
  session.push_back(noteOnWithNoteId(1233, 1, 65, 100, kMoverId));
  session.push_back(MidiEvent::NoteOff(1583, 1, 65, 0));

  const EditPassVec rows = buildPreCommitEditPasses(focus, 1, &session, loopLength);
  TEST_ASSERT_EQUAL(0, static_cast<int>(rows.size()));
}

void test_pre_commit_emits_valid_mover_note_range() {
  constexpr uint32_t loopLength = 1536;
  constexpr NoteId kMoverId = 3;
  NoteEditFocus focus;
  focus.active = true;
  focus.movingNoteId = kMoverId;
  focus.commitBaseline = {65, 100, 1185, 1535};
  focus.last = {65, 100, 1233, 1583};

  MidiEventVec session;
  session.push_back(noteOnWithNoteId(1233, 1, 65, 100, kMoverId));
  session.push_back(MidiEvent::NoteOff(1583, 1, 65, 0));

  const EditPassVec rows = buildPreCommitEditPasses(focus, 1, &session, loopLength);
  TEST_ASSERT_EQUAL(1, static_cast<int>(rows.size()));
  TEST_ASSERT_EQUAL(static_cast<int>(EditPropertyType::NoteRange),
                    static_cast<int>(rows[0].propertyType));
  TEST_ASSERT_EQUAL_UINT32(1233, rows[0].startTick);
  TEST_ASSERT_EQUAL_UINT32(1583, rows[0].endTick);
}

void test_macro_commit_aligned_with_select_target_rejects_bracket_mismatch_220331() {
  // session_20260806_220331 (~55.7s): re-select same NoteId at tick 609 while focus.last starts
  // at 993 must not macro-commit pending mover geometry.
  constexpr uint32_t kLoopLength = 1536;
  constexpr uint32_t kLoopStart = 0;
  constexpr NoteId kMoverId = 3;

  NoteEditFocus focus;
  focus.active = true;
  focus.movingNoteId = kMoverId;
  focus.last = {65, 100, 993, 1187};
  focus.commitBaseline = focus.last;

  TEST_ASSERT_FALSE(isMacroCommitAlignedWithSelectTarget(kMoverId, 609u, focus, kLoopStart,
                                                         kLoopLength, false));
  TEST_ASSERT_TRUE(isMacroCommitAlignedWithSelectTarget(kMoverId, 993u, focus, kLoopStart,
                                                        kLoopLength, false));
  TEST_ASSERT_TRUE(isMacroCommitAlignedWithSelectTarget(kMoverId, 1187u, focus, kLoopStart,
                                                        kLoopLength, true));
  TEST_ASSERT_FALSE(isMacroCommitAlignedWithSelectTarget(kMoverId, 609u, focus, kLoopStart,
                                                         kLoopLength, true));
  TEST_ASSERT_TRUE(
      isMacroCommitAlignedWithSelectTarget(7u, 609u, focus, kLoopStart, kLoopLength, false));
}

void test_macro_commit_blocks_different_note_while_mover_pending_014541() {
  constexpr uint32_t kLoopLength = 5376;
  constexpr uint32_t kLoopStart = 0;
  constexpr NoteId kMoverId = 17;
  constexpr NoteId kOtherId = 16;

  NoteEditFocus focus;
  focus.active = true;
  focus.movingNoteId = kMoverId;
  focus.commitBaseline = {88, 100, 3600, 4127};
  focus.last = {88, 100, 3504, 4031};

  TEST_ASSERT_FALSE(isMacroCommitAlignedWithSelectTarget(kOtherId, 3600u, focus, kLoopStart,
                                                         kLoopLength, false));
  TEST_ASSERT_FALSE(isMacroCommitAlignedWithSelectTarget(kInvalidNoteId, 3600u, focus, kLoopStart,
                                                         kLoopLength, false));
  TEST_ASSERT_TRUE(isMacroCommitAlignedWithSelectTarget(kMoverId, 3504u, focus, kLoopStart,
                                                        kLoopLength, false));
}

void test_overlap_pre_commit_skips_implausible_live_span_015045() {
  constexpr uint32_t kLoopLength = 5376;
  constexpr uint8_t kChannel = 5;
  constexpr NoteId kOverlapId = 16;
  constexpr NoteId kMoverId = 17;

  NoteEditFocus focus;
  focus.active = true;
  focus.movingNoteId = kMoverId;
  focus.commitBaseline = {88, 100, 1872, 2399};
  focus.last = focus.commitBaseline;
  focus.baselineMap[kOverlapId] = {84, 100, 3504, 4031};
  recordChangedOverlapNote(focus, kOverlapId);

  MidiEventVec store;
  store.push_back(noteOnWithNoteId(4176, kChannel, 84, 100, kOverlapId));
  MidiEvent overlapOff = MidiEvent::NoteOff(4224, kChannel, 84, 0);
  overlapOff.noteId = kOverlapId;
  store.push_back(overlapOff);
  store.push_back(noteOnWithNoteId(1872, kChannel, 88, 100, kMoverId));
  MidiEvent moverOff = MidiEvent::NoteOff(2399, kChannel, 88, 0);
  moverOff.noteId = kMoverId;
  store.push_back(moverOff);

  const EditPassVec rows = buildPreCommitEditPasses(focus, kChannel, &store, kLoopLength);
  TEST_ASSERT_EQUAL(1, static_cast<int>(rows.size()));
  TEST_ASSERT_EQUAL(kOverlapId, rows[0].targetNoteId);
  TEST_ASSERT_EQUAL(static_cast<int>(EditPropertyType::NoteRange),
                    static_cast<int>(rows[0].propertyType));
  TEST_ASSERT_EQUAL_UINT32(4176u, rows[0].startTick);
  TEST_ASSERT_EQUAL_UINT32(4224u, rows[0].endTick);
}

void test_mover_pre_commit_rejects_stale_wrapped_commit_baseline_length() {
  constexpr uint32_t kLoopLength = 5376;
  NoteEditFocus focus;
  focus.active = true;
  focus.movingNoteId = 16;
  focus.commitBaseline = {84, 100, 3504, 5376};
  focus.last = {84, 100, 3504, 3552};

  const EditPassVec rows = buildPreCommitEditPasses(focus, 5, nullptr, kLoopLength);
  TEST_ASSERT_EQUAL(0, static_cast<int>(rows.size()));
}

int main(int /*argc*/, char** /*argv*/) {
  UNITY_BEGIN();
  RUN_TEST(test_baseline_map_includes_moving_note_at_select);
  RUN_TEST(test_populate_baseline_full_loop_includes_cross_pitch);
  RUN_TEST(test_populate_baseline_includes_store_channel_notes);
  RUN_TEST(test_populate_baseline_maps_committed_pitch_start_via_live_note_id);
  RUN_TEST(test_populate_baseline_keys_by_live_note_id_not_pass_id);
  RUN_TEST(test_hidden_note_baseline_survives_for_pre_commit_delete);
  RUN_TEST(test_hidden_overlap_same_start_as_mover_emits_delete_not_length);
  RUN_TEST(test_unchanged_overlap_live_mismatch_emits_no_length_row);
  RUN_TEST(test_committed_overlap_delete_clears_focus_restore_authority);
  RUN_TEST(test_committed_overlap_update_promotes_shortened_focus_baseline);
  RUN_TEST(test_session_134610_shortened_overlap_commit_rows);
  RUN_TEST(test_unresolved_baseline_entry_emits_no_delete_row);
  RUN_TEST(test_restored_overlap_note_leaves_no_pending_delete);
  RUN_TEST(test_empty_step_select_preserves_changed_overlap_note_ids);
  RUN_TEST(test_empty_deselect_keeps_session_projection_after_pending_move_015614);
  RUN_TEST(test_reconcile_changed_overlap_ids_marks_live_baseline_diff);
  RUN_TEST(test_reconcile_changed_overlap_ids_ignores_missing_live_on_empty_session);
  RUN_TEST(test_reconcile_changed_overlap_ids_keeps_hide_scratch_without_live);
  RUN_TEST(test_reconcile_changed_overlap_ids_forgets_when_live_matches_baseline);
  RUN_TEST(test_populate_baseline_map_for_edit_closure_wrap_sibling);
  RUN_TEST(test_populate_baseline_map_includes_linear_same_pitch_neighbor);
  RUN_TEST(test_a1_length_updates_moving_note_range_not_commit_baseline);
  RUN_TEST(test_a1_no_pending_length_when_moving_note_range_matches_baseline);
  RUN_TEST(test_note_edit_focus_has_pending_commit_geometry_and_overlap);
  RUN_TEST(test_can_apply_simple_pitch_change_without_lane_collision);
  RUN_TEST(test_can_apply_simple_pitch_change_blocks_inner_overlap_on_target_lane);
  RUN_TEST(test_can_apply_simple_pitch_change_blocks_store_channel_target_lane);
  RUN_TEST(test_can_apply_simple_pitch_change_blocks_when_baseline_map_lane_needs_restore);
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
  RUN_TEST(test_filter_shortened_overlap_uses_live_span_when_reconstruct_mispairs);
  RUN_TEST(test_project_pitch23_shortened_overlap_after_apply_actions);
  RUN_TEST(test_project_pitch22_hidden_overlap_after_apply_actions);
  RUN_TEST(test_project_untouched_same_pitch_neighbor_keeps_reconstruction_not_live_span);
  RUN_TEST(test_project_pitch_restore_does_not_append_committed_overlap_161117);
  RUN_TEST(test_project_non_participant_baseline_note_is_copied_when_live_lookup_misses_161117);
  RUN_TEST(test_project_hidden_changed_overlap_not_selectable_when_live_lookup_misses_162324);
  RUN_TEST(test_session_163142_highlight_resolver_prefers_mover_over_shortened_overlap);
  RUN_TEST(test_session_163142_moving_note_index_uses_loop_origin_display_bracket);
  RUN_TEST(test_project_post_commit_no_phantom_note_153954);
  RUN_TEST(test_project_pitch65_outer_shorten_inner_move_214302);
  RUN_TEST(test_project_pitch65_hidden_nested_not_in_display_214302);
  RUN_TEST(test_project_pitch65_visual_cache_lane_bar_not_left_alongside_participants_214302);
  RUN_TEST(test_display_fingerprint_changes_when_overlap_geometry_changes);
  RUN_TEST(test_is_live_edit_driver_valid_rejects_id_match_span_mismatch);
  RUN_TEST(test_pre_commit_rejects_mover_note_range_zero_start_after_nonzero_baseline);
  RUN_TEST(test_pre_commit_emits_valid_mover_note_range);
  RUN_TEST(test_macro_commit_aligned_with_select_target_rejects_bracket_mismatch_220331);
  RUN_TEST(test_macro_commit_blocks_different_note_while_mover_pending_014541);
  RUN_TEST(test_overlap_pre_commit_skips_implausible_live_span_015045);
  RUN_TEST(test_mover_pre_commit_rejects_stale_wrapped_commit_baseline_length);
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
  RUN_TEST(test_selection_index_geometry_hold_when_primary_note_at_current_idx);
  RUN_TEST(test_is_plausible_storage_span_rejects_lifo_mispair);
  RUN_TEST(test_find_linear_note_span_rejects_mispaired_off);
  RUN_TEST(test_find_linear_note_span_resolves_across_store_channel);
  RUN_TEST(test_stamp_note_ids_pairs_within_store_channel);
  RUN_TEST(test_is_moving_note_overlap_scratch_entry);
  RUN_TEST(test_evict_overlap_scratch_when_overlap_target_selected);
  RUN_TEST(test_select_overlap_target_evicts_shortened_scratch_session_log);
  RUN_TEST(test_pitch_linear_focus_no_spurious_length_after_sync);
  RUN_TEST(test_linear_baseline_for_overlap_restore_rejects_display_wrap_end);
  RUN_TEST(test_baseline_map_prefers_linear_span_over_wrap_projection);
  RUN_TEST(test_sync_linear_focus_same_pitch_shortened_overlap_does_not_steal_mover_off);
  RUN_TEST(test_find_linear_note_span_wrapped_mover_ignores_in_loop_orphan_off);
  RUN_TEST(test_linear_baseline_for_overlap_restore_shortened_keeps_original_end);
  RUN_TEST(test_linear_baseline_for_overlap_restore_hidden_uses_hide_snapshot_not_session);
  RUN_TEST(test_find_linear_off_for_note_id_ignores_same_pitch_neighbor_off);
  RUN_TEST(test_find_linear_off_for_note_id_prefers_nearest_same_pitch_duplicate_off);
  RUN_TEST(test_find_linear_off_for_note_id_ignores_cross_pitch_same_note_id_off);
  RUN_TEST(test_sync_linear_focus_ignores_cross_pitch_same_note_id_off);
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
  RUN_TEST(test_baseline_map_diff_pending_commit_when_mover_unchanged);
  RUN_TEST(test_baseline_map_diff_reads_store_channel_live_span);
  RUN_TEST(test_session_171134_canonical_commit_rows_from_final_session_store);
  RUN_TEST(test_focus_rebuild_pending_after_pitch_move_stale_display_hint_session_193016);
  RUN_TEST(test_pitch_pre_commit_requires_active_focus);
  return UNITY_END();
}
