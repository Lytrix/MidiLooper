//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include <unity.h>
#include <cstdint>

#include "NoteEditCurrentState.h"
#include "MidiEvent.h"
#include "NoteEditFocus.h"
#include "ParticipatingNoteSession.h"
#include "EditSessionAction.h"
#include "NoteEditSessionState.h"
#include "Utils/NoteUtils.h"
#include "Utils/SelectNavigation.h"
#include "Utils/NoteEditDisplaySnapshot.h"

#include "../test_support/NoteEditFocusTestDeps.cpp"
#include "../../src/Logger.cpp"
#include "../../src/Utils/IntervalProjection.cpp"
#include "../../src/Utils/NoteUtils.cpp"

namespace {

constexpr uint8_t kChannel = 1;
constexpr NoteId kNoteA = 17;
constexpr NoteId kNoteB = 25;

MidiEvent noteOn(NoteId noteId, uint8_t pitch, uint32_t start, uint8_t velocity = 100) {
  MidiEvent evt = MidiEvent::NoteOn(start, kChannel, pitch, velocity);
  evt.noteId = noteId;
  return evt;
}

MidiEvent noteOff(NoteId noteId, uint8_t pitch, uint32_t end) {
  MidiEvent evt = MidiEvent::NoteOff(end, kChannel, pitch, 0);
  evt.noteId = noteId;
  return evt;
}

MidiEventVec makeStorePair(NoteId noteId, uint8_t pitch, uint32_t start, uint32_t end) {
  MidiEventVec store;
  store.push_back(noteOn(noteId, pitch, start));
  store.push_back(noteOff(noteId, pitch, end));
  return store;
}

}  // namespace

void test_build_from_session_store_visible_rows() {
  MidiEventVec store = makeStorePair(kNoteA, 88, 3600, 4127);
  store.push_back(noteOn(kNoteB, 88, 1392));
  store.push_back(noteOff(kNoteB, 88, 1919));

  const NoteEditCurrentState state = NoteEditCurrentState::buildFromSessionStore(store, kChannel);
  TEST_ASSERT_EQUAL(2, static_cast<int>(state.size()));

  const NoteEditCurrentNoteState* rowA = state.find(kNoteA);
  TEST_ASSERT_NOT_NULL(rowA);
  TEST_ASSERT_EQUAL(static_cast<int>(NoteEditPresenceType::Visible),
                    static_cast<int>(rowA->presence));
  TEST_ASSERT_EQUAL(3600u, rowA->currentSpan.startTick);
  TEST_ASSERT_EQUAL(4127u, rowA->currentSpan.endTick);
  TEST_ASSERT_EQUAL(3600u, rowA->committedSpan.startTick);

  const NoteEditCurrentNoteState* rowB = state.find(kNoteB);
  TEST_ASSERT_NOT_NULL(rowB);
  TEST_ASSERT_EQUAL(1392u, rowB->currentSpan.startTick);
  TEST_ASSERT_EQUAL(1919u, rowB->currentSpan.endTick);
}

void test_projection_visible_and_added_rows() {
  NoteEditCurrentState state;
  state.upsertRow(kNoteA, {88, 100, 3600, 4127}, {88, 100, 1392, 1919},
                  NoteEditPresenceType::Visible);
  state.upsertRow(kNoteB, {60, 90, 100, 200}, {60, 90, 100, 200},
                  NoteEditPresenceType::Added);

  MidiEventVec store;
  state.projectToSessionStore(store, kChannel);
  TEST_ASSERT_EQUAL(4, static_cast<int>(store.size()));

  const NoteEditCurrentStateVerifyResult projection = state.verifyProjection(store, kChannel);
  TEST_ASSERT_TRUE(projection.passed);
}

void test_hidden_and_deleted_rows_do_not_project() {
  NoteEditCurrentState state;
  state.upsertRow(kNoteA, {88, 100, 3600, 4127}, {88, 100, 3600, 4127},
                  NoteEditPresenceType::Hidden);
  state.upsertRow(kNoteB, {60, 90, 100, 200}, {60, 90, 100, 200},
                  NoteEditPresenceType::Deleted);

  MidiEventVec store;
  state.projectToSessionStore(store, kChannel);
  TEST_ASSERT_EQUAL(0, static_cast<int>(store.size()));

  const NoteEditCurrentStateVerifyResult projection = state.verifyProjection(store, kChannel);
  TEST_ASSERT_TRUE(projection.passed);
  TEST_ASSERT_FALSE(projection.hiddenOrDeletedProjected);
}

void test_build_then_project_round_trip_parity() {
  const MidiEventVec source = makeStorePair(kNoteA, 88, 3600, 4127);
  NoteEditCurrentState state = NoteEditCurrentState::buildFromSessionStore(source, kChannel);

  MidiEventVec projected;
  state.projectToSessionStore(projected, kChannel);

  const NoteEditCurrentStateVerifyResult projection = state.verifyProjection(projected, kChannel);
  TEST_ASSERT_TRUE(projection.passed);
  TEST_ASSERT_FALSE(projection.visibleProjectionMismatch);
}

void test_verify_selected_note_exists() {
  NoteEditCurrentState state;
  state.upsertRow(kNoteA, {88, 100, 3600, 4127}, {88, 100, 3600, 4127},
                  NoteEditPresenceType::Visible);

  const NoteEditCurrentStateVerifyResult ok = state.verifyInvariants(kNoteA);
  TEST_ASSERT_TRUE(ok.passed);

  const NoteEditCurrentStateVerifyResult missing = state.verifyInvariants(kNoteB);
  TEST_ASSERT_FALSE(missing.passed);
  TEST_ASSERT_TRUE(missing.selectedNoteMissing);
}

void test_projection_owner_refresh_restores_parity() {
  NoteEditCurrentState state;
  state.upsertRow(kNoteA, {88, 100, 3600, 4127}, {88, 100, 1392, 1919},
                  NoteEditPresenceType::Visible);

  MidiEventVec projection;
  state.projectToSessionStore(projection, kChannel);

  const NoteEditCurrentStateVerifyResult projectionOk = state.verifyProjection(projection, kChannel);
  TEST_ASSERT_TRUE(projectionOk.passed);
}

void test_compat_direct_store_mutation_breaks_projection_parity() {
  MidiEventVec store = makeStorePair(kNoteA, 88, 3600, 4127);
  const NoteEditCurrentState state = NoteEditCurrentState::buildFromSessionStore(store, kChannel);

  store[0].tick = 9999u;
  const NoteEditCurrentStateVerifyResult broken = state.verifyProjection(store, kChannel);
  TEST_ASSERT_FALSE(broken.passed);
  TEST_ASSERT_TRUE(broken.visibleProjectionMismatch);
}

void test_projection_owner_path_after_current_state_edit() {
  NoteEditCurrentState state =
      NoteEditCurrentState::buildFromSessionStore(makeStorePair(kNoteA, 88, 3600, 4127), kChannel);
  state.upsertRow(kNoteA, {88, 100, 3600, 4127}, {88, 100, 1392, 1919},
                  NoteEditPresenceType::Visible);

  MidiEventVec projection;
  state.projectToSessionStore(projection, kChannel);
  const NoteEditCurrentStateVerifyResult ok = state.verifyProjection(projection, kChannel);
  TEST_ASSERT_TRUE(ok.passed);
  TEST_ASSERT_EQUAL(1392u, projection[0].tick);
}

void test_display_projection_same_pitch_reorder_uses_current_span() {
  // session_20260807_021939: note 17 at current 1392 appears before note 25 at 3504 after reorder.
  constexpr uint32_t kLoopLength = 5376;
  constexpr NoteId kPriorId = 17;
  constexpr NoteId kMoverId = 25;
  constexpr uint8_t kPitch = 88;

  NoteEditCurrentState currentState;
  currentState.upsertRow(kPriorId, {kPitch, 100, 3600, 4127}, {kPitch, 100, 1392, 1919},
                         NoteEditPresenceType::Visible);
  currentState.upsertRow(kMoverId, {kPitch, 100, 3504, 4031}, {kPitch, 100, 3504, 4031},
                         NoteEditPresenceType::Visible);

  MidiEventVec store;
  currentState.projectToSessionStore(store, kChannel);

  NoteUtils::DisplayNoteVec committedBase;
  committedBase.push_back({kMoverId, kPitch, 100, 3504, 4031});
  committedBase.push_back({kPriorId, kPitch, 100, 3600, 4127});

  NoteEditFocus focus;
  focus.active = true;
  focus.movingNoteId = kMoverId;
  focus.last = {kPitch, 100, 3504, 4031};
  focus.commitBaseline = focus.last;
  focus.baselineMap[kPriorId] = {kPitch, 100, 3600, 4127};
  focus.baselineMap[kMoverId] = focus.commitBaseline;

  const NoteUtils::DisplayNoteVec projected =
      projectNoteEditDisplayNotes(committedBase, store, focus, kChannel, kLoopLength,
                                  &currentState);
  TEST_ASSERT_EQUAL(2, static_cast<int>(projected.size()));
  TEST_ASSERT_EQUAL_UINT32(kPriorId, projected[0].noteId);
  TEST_ASSERT_EQUAL_UINT32(1392u, projected[0].startTick);
  TEST_ASSERT_EQUAL_UINT32(kMoverId, projected[1].noteId);
  TEST_ASSERT_EQUAL_UINT32(3504u, projected[1].startTick);
}

void test_display_projection_inactive_focus_masks_hidden_overlaps() {
  // RC10b / session_20260807_140022: empty-step deselect clears focus.active but overlaps stay
  // Hidden in current state — display must not paint committed-pass ghost rows.
  constexpr uint32_t kLoopLength = 5376;
  constexpr NoteId kOverlapA = 9;
  constexpr NoteId kOverlapB = 10;
  constexpr NoteId kMoverId = 17;
  constexpr uint8_t kPitch = 88;

  NoteEditCurrentState currentState;
  currentState.upsertRow(kMoverId, {kPitch, 100, 1776, 2303}, {kPitch, 100, 1728, 2255},
                         NoteEditPresenceType::Visible);
  currentState.upsertRow(kOverlapA, {kPitch, 100, 2256, 2303}, {kPitch, 100, 2256, 2303},
                         NoteEditPresenceType::Hidden);
  currentState.upsertRow(kOverlapB, {kPitch, 100, 2640, 2783}, {kPitch, 100, 2640, 2783},
                         NoteEditPresenceType::Hidden);

  MidiEventVec store;
  currentState.projectToSessionStore(store, kChannel);

  NoteUtils::DisplayNoteVec committedBase;
  committedBase.push_back({kOverlapA, kPitch, 100, 2256, 2303});
  committedBase.push_back({kOverlapB, kPitch, 100, 2640, 2783});
  committedBase.push_back({kMoverId, kPitch, 100, 1776, 2303});

  NoteEditFocus focus;
  focus.active = false;
  focus.baselineMap[kOverlapA] = {kPitch, 100, 2256, 2303};
  focus.baselineMap[kOverlapB] = {kPitch, 100, 2640, 2783};

  const NoteUtils::DisplayNoteVec withoutMask =
      projectNoteEditDisplayNotes(committedBase, store, focus, kChannel, kLoopLength, nullptr);
  TEST_ASSERT_EQUAL(3, static_cast<int>(withoutMask.size()));

  const NoteUtils::DisplayNoteVec masked =
      projectNoteEditDisplayNotes(committedBase, store, focus, kChannel, kLoopLength,
                                  &currentState);
  TEST_ASSERT_EQUAL(1, static_cast<int>(masked.size()));
  TEST_ASSERT_EQUAL_UINT32(kMoverId, masked[0].noteId);
  TEST_ASSERT_EQUAL_UINT32(1728u, masked[0].startTick);
}

void test_sync_focus_last_from_current_state() {
  NoteEditCurrentState currentState;
  currentState.upsertRow(kNoteA, {88, 100, 3600, 4127}, {88, 100, 1392, 1919},
                         NoteEditPresenceType::Visible);

  NoteEditFocus focus;
  focus.active = true;
  focus.movingNoteId = kNoteA;
  focus.last = {88, 100, 3600, 4127};
  focus.movingNoteRange = {3600, 4127};

  syncNoteEditFocusLastFromCurrentState(focus, kNoteA, currentState);
  TEST_ASSERT_EQUAL_UINT32(1392u, focus.last.startTick);
  TEST_ASSERT_EQUAL_UINT32(1919u, focus.last.endTick);
  TEST_ASSERT_EQUAL_UINT32(1392u, focus.movingNoteRange.start);
}

void test_hide_then_shorten_stays_hidden_and_masks_on_deselect() {
  // session_20260807_141218: inner overlap HideNote + ShortenNote must not resurrect tail on deselect.
  constexpr uint32_t kLoopLength = 5376;
  constexpr NoteId kOverlapId = 10;
  constexpr NoteId kMoverId = 17;
  constexpr uint8_t kPitch = 88;

  NoteEditCurrentState currentState;
  currentState.upsertRow(kMoverId, {kPitch, 100, 2736, 3263}, {kPitch, 100, 2736, 3263},
                         NoteEditPresenceType::Visible);
  currentState.upsertRow(kOverlapId, {kPitch, 100, 2640, 2783}, {kPitch, 100, 2640, 2783},
                         NoteEditPresenceType::Visible);

  EditSessionAction hide{};
  hide.type = EditSessionActionType::HideNote;
  hide.targetNoteId = kOverlapId;
  hide.startTick = 2640;
  hide.endTick = 2735;
  hide.pitch = kPitch;
  currentState.applyEditSessionAction(hide);

  EditSessionAction shorten{};
  shorten.type = EditSessionActionType::ShortenNote;
  shorten.targetNoteId = kOverlapId;
  shorten.startTick = 2640;
  shorten.endTick = 2783;
  shorten.pitch = kPitch;
  currentState.applyEditSessionAction(shorten);

  TEST_ASSERT_TRUE(currentState.isRowHiddenOrDeleted(kOverlapId));
  TEST_ASSERT_FALSE(currentState.rowProjectsToStore(kOverlapId));

  MidiEventVec store;
  currentState.projectToSessionStore(store, kChannel);

  NoteUtils::DisplayNoteVec committedBase;
  committedBase.push_back({kOverlapId, kPitch, 100, 2640, 2783});
  committedBase.push_back({kMoverId, kPitch, 100, 2736, 3263});

  NoteEditFocus focus;
  focus.active = false;
  focus.baselineMap[kOverlapId] = {kPitch, 100, 2640, 2783};

  const NoteUtils::DisplayNoteVec projected =
      projectNoteEditDisplayNotes(committedBase, store, focus, kChannel, kLoopLength,
                                  &currentState);
  TEST_ASSERT_EQUAL(1, static_cast<int>(projected.size()));
  TEST_ASSERT_EQUAL_UINT32(kMoverId, projected[0].noteId);
}

void test_shorten_overlap_tail_stays_visible_shortened_and_masks_inventory_on_deselect() {
  // session_20260807_141920: ShortenNote-only overlap tail must not resurrect on deselect.
  constexpr uint32_t kLoopLength = 5376;
  constexpr NoteId kOverlapId = 10;
  constexpr NoteId kMoverId = 17;
  constexpr uint8_t kPitch = 88;

  NoteEditCurrentState currentState;
  currentState.upsertRow(kMoverId, {kPitch, 100, 2736, 3263}, {kPitch, 100, 2736, 3263},
                         NoteEditPresenceType::Visible);
  currentState.upsertRow(kOverlapId, {kPitch, 100, 2640, 3167}, {kPitch, 100, 2640, 3167},
                         NoteEditPresenceType::Visible);

  EditSessionAction shorten{};
  shorten.type = EditSessionActionType::ShortenNote;
  shorten.targetNoteId = kOverlapId;
  shorten.startTick = 2640;
  shorten.endTick = 2783;
  shorten.pitch = kPitch;
  currentState.applyEditSessionAction(shorten);

  TEST_ASSERT_FALSE(currentState.isRowHiddenOrDeleted(kOverlapId));
  TEST_ASSERT_TRUE(currentState.rowProjectsToStore(kOverlapId));
  TEST_ASSERT_FALSE(currentState.rowIncludedInSelectableInventory(kOverlapId));

  MidiEventVec store;
  currentState.projectToSessionStore(store, kChannel);

  NoteUtils::DisplayNoteVec committedBase;
  committedBase.push_back({kOverlapId, kPitch, 100, 2640, 3167});
  committedBase.push_back({kMoverId, kPitch, 100, 2736, 3263});

  NoteEditFocus focus;
  focus.active = true;
  focus.movingNoteId = kMoverId;
  focus.last = {kPitch, 100, 2736, 3263};
  focus.movingNoteRange = {2736, 3263};
  focus.baselineMap[kOverlapId] = {kPitch, 100, 2640, 3167};

  const NoteUtils::DisplayNoteVec projectedWhileOverlap =
      projectNoteEditDisplayNotes(committedBase, store, focus, kChannel, kLoopLength,
                                  &currentState);
  TEST_ASSERT_EQUAL(2, static_cast<int>(projectedWhileOverlap.size()));
  bool foundShortenedStub = false;
  for (const NoteUtils::DisplayNote& dn : projectedWhileOverlap) {
    if (dn.noteId == kOverlapId) {
      TEST_ASSERT_EQUAL_UINT32(2640u, dn.startTick);
      TEST_ASSERT_EQUAL_UINT32(2783u, dn.endTick);
      foundShortenedStub = true;
    }
  }
  TEST_ASSERT_TRUE(foundShortenedStub);

  const NoteUtils::DisplayNoteVec selectableWhileOverlap =
      filterProjectingSelectableDisplayNotes(projectedWhileOverlap, &currentState, focus, 1);
  TEST_ASSERT_EQUAL(1, static_cast<int>(selectableWhileOverlap.size()));
  TEST_ASSERT_EQUAL_UINT32(kMoverId, selectableWhileOverlap[0].noteId);

  focus.active = false;
  const NoteUtils::DisplayNoteVec projected =
      projectNoteEditDisplayNotes(committedBase, store, focus, kChannel, kLoopLength,
                                  &currentState);
  TEST_ASSERT_EQUAL(2, static_cast<int>(projected.size()));
  bool foundStubOnDeselect = false;
  for (const NoteUtils::DisplayNote& dn : projected) {
    if (dn.noteId == kOverlapId) {
      TEST_ASSERT_EQUAL_UINT32(2783u, dn.endTick);
      foundStubOnDeselect = true;
    }
  }
  TEST_ASSERT_TRUE(foundStubOnDeselect);
  const NoteUtils::DisplayNoteVec selectable =
      filterProjectingSelectableDisplayNotes(projected, &currentState, focus, -1);
  TEST_ASSERT_EQUAL(2, static_cast<int>(selectable.size()));
  bool foundOverlapSelectable = false;
  for (const NoteUtils::DisplayNote& dn : selectable) {
    if (dn.noteId == kOverlapId) {
      TEST_ASSERT_EQUAL_UINT32(2783u, dn.endTick);
      foundOverlapSelectable = true;
    }
  }
  TEST_ASSERT_TRUE(foundOverlapSelectable);
}

void test_leave_restore_inventory_masked_tail_stays_hidden() {
  // session_20260807_142819: leave-restore RestoreNote with shortened tail must not resurrect
  // overlap in selectable inventory (DNTE len 143 at overlap tick after select sweep).
  constexpr uint32_t kLoopLength = 5376;
  constexpr NoteId kOverlapId = 10;
  constexpr NoteId kMoverId = 17;
  constexpr uint8_t kPitch = 88;

  NoteEditCurrentState currentState;
  currentState.upsertRow(kMoverId, {kPitch, 100, 2736, 3263}, {kPitch, 100, 2736, 3263},
                         NoteEditPresenceType::Visible);
  currentState.upsertRow(kOverlapId, {kPitch, 100, 2640, 3167}, {kPitch, 100, 2640, 3167},
                         NoteEditPresenceType::Visible);

  EditSessionAction hide{};
  hide.type = EditSessionActionType::HideNote;
  hide.targetNoteId = kOverlapId;
  hide.startTick = 2640;
  hide.endTick = 2735;
  hide.pitch = kPitch;
  currentState.applyEditSessionAction(hide);

  EditSessionAction shorten{};
  shorten.type = EditSessionActionType::ShortenNote;
  shorten.targetNoteId = kOverlapId;
  shorten.startTick = 2640;
  shorten.endTick = 2783;
  shorten.pitch = kPitch;
  currentState.applyEditSessionAction(shorten);

  EditSessionAction leaveRestore{};
  leaveRestore.type = EditSessionActionType::RestoreNote;
  leaveRestore.targetNoteId = kOverlapId;
  leaveRestore.startTick = 2640;
  leaveRestore.endTick = 2783;
  leaveRestore.pitch = kPitch;
  currentState.applyEditSessionAction(leaveRestore);

  TEST_ASSERT_TRUE(currentState.isRowHiddenOrDeleted(kOverlapId));
  TEST_ASSERT_FALSE(currentState.rowProjectsToStore(kOverlapId));

  MidiEventVec store;
  currentState.projectToSessionStore(store, kChannel);

  NoteUtils::DisplayNoteVec committedBase;
  committedBase.push_back({kOverlapId, kPitch, 100, 2640, 3167});
  committedBase.push_back({kMoverId, kPitch, 100, 2736, 3263});

  NoteEditFocus focus;
  focus.active = true;
  focus.movingNoteId = kMoverId;
  focus.commitBaseline = {kPitch, 100, 2736, 3263};
  focus.last = focus.commitBaseline;
  focus.movingNoteRange = {2736, 3263};
  focus.baselineMap[kOverlapId] = {kPitch, 100, 2640, 3167};
  focus.baselineMap[kMoverId] = {kPitch, 100, 2736, 3263};

  const NoteUtils::DisplayNoteVec projectedWhileCovered =
      projectNoteEditDisplayNotes(committedBase, store, focus, kChannel, kLoopLength,
                                  &currentState);
  for (const NoteUtils::DisplayNote& dn : projectedWhileCovered) {
    TEST_ASSERT_FALSE(dn.noteId == kOverlapId);
  }
  TEST_ASSERT_EQUAL(1, static_cast<int>(projectedWhileCovered.size()));
  TEST_ASSERT_EQUAL_UINT32(kMoverId, projectedWhileCovered[0].noteId);

  focus.last = {kPitch, 100, 1920, 2447};
  focus.movingNoteRange = {1920, 2447};
  const NoteUtils::DisplayNoteVec projectedAfterLeave =
      projectNoteEditDisplayNotes(committedBase, store, focus, kChannel, kLoopLength,
                                  &currentState);
  TEST_ASSERT_EQUAL(1, static_cast<int>(projectedAfterLeave.size()));
  TEST_ASSERT_EQUAL_UINT32(kMoverId, projectedAfterLeave[0].noteId);

  EditSessionAction fullRestore{};
  fullRestore.type = EditSessionActionType::RestoreNote;
  fullRestore.targetNoteId = kOverlapId;
  fullRestore.startTick = 2640;
  fullRestore.endTick = 3167;
  fullRestore.pitch = kPitch;
  currentState.applyEditSessionAction(fullRestore);
  currentState.projectToSessionStore(store, kChannel);

  TEST_ASSERT_FALSE(currentState.isRowHiddenOrDeleted(kOverlapId));
  TEST_ASSERT_TRUE(currentState.rowProjectsToStore(kOverlapId));

  const NoteUtils::DisplayNoteVec projectedAfterFullRestore =
      projectNoteEditDisplayNotes(committedBase, store, focus, kChannel, kLoopLength,
                                  &currentState);
  TEST_ASSERT_EQUAL(2, static_cast<int>(projectedAfterFullRestore.size()));
  bool foundOverlapAfterFullRestore = false;
  for (const NoteUtils::DisplayNote& dn : projectedAfterFullRestore) {
    if (dn.noteId == kOverlapId) {
      TEST_ASSERT_EQUAL_UINT32(2640u, dn.startTick);
      TEST_ASSERT_EQUAL_UINT32(3167u, dn.endTick);
      foundOverlapAfterFullRestore = true;
    }
  }
  TEST_ASSERT_TRUE(foundOverlapAfterFullRestore);
}

void test_apply_hide_through_current_state_owner() {
  NoteEditCurrentState state;
  state.upsertRow(kNoteA, {88, 100, 3600, 4127}, {88, 100, 3600, 4127},
                  NoteEditPresenceType::Visible);

  EditSessionAction hide{};
  hide.type = EditSessionActionType::HideNote;
  hide.targetNoteId = kNoteA;
  hide.startTick = 3600;
  hide.endTick = 4127;
  hide.pitch = 88;
  state.applyEditSessionAction(hide);

  TEST_ASSERT_TRUE(state.isRowHiddenOrDeleted(kNoteA));
  MidiEventVec store;
  state.projectToSessionStore(store, kChannel);
  TEST_ASSERT_EQUAL(0, static_cast<int>(store.size()));
}

void test_mark_deleted_and_remove_added_row() {
  NoteEditCurrentState state;
  state.upsertRow(kNoteA, {60, 90, 100, 200}, {60, 90, 100, 200}, NoteEditPresenceType::Visible);
  state.upsertRow(kNoteB, {61, 90, 300, 400}, {61, 90, 300, 400}, NoteEditPresenceType::Added);

  state.markRowDeleted(kNoteA);
  TEST_ASSERT_TRUE(state.isRowHiddenOrDeleted(kNoteA));

  state.removeRow(kNoteB);
  TEST_ASSERT_FALSE(state.hasRow(kNoteB));
}

void test_commit_rows_from_current_state_overlap_shorten() {
  constexpr uint32_t kLoopLength = 5376;
  constexpr NoteId kOverlapId = 17;
  constexpr NoteId kMoverId = 25;

  NoteEditFocus focus;
  focus.active = true;
  focus.movingNoteId = kMoverId;
  focus.commitBaseline = {88, 100, 3600, 4127};
  focus.last = focus.commitBaseline;
  focus.baselineMap[kOverlapId] = {88, 100, 3600, 4127};

  NoteEditCurrentState state;
  state.upsertRow(kMoverId, focus.commitBaseline, focus.last, NoteEditPresenceType::Visible);
  state.upsertRow(kOverlapId, focus.baselineMap[kOverlapId], {88, 100, 1392, 1919},
                  NoteEditPresenceType::Visible);

  const EditPassVec rows =
      buildCommitRowsFromCurrentState(focus, state, kChannel, kLoopLength);
  bool foundOverlapMove = false;
  for (const EditPass& row : rows) {
    if (row.targetNoteId == kOverlapId && row.actionType == EditActionType::Update &&
        row.propertyType == EditPropertyType::NoteRange) {
      TEST_ASSERT_EQUAL_UINT32(1392u, row.startTick);
      TEST_ASSERT_EQUAL_UINT32(1919u, row.endTick);
      foundOverlapMove = true;
    }
  }
  TEST_ASSERT_TRUE(foundOverlapMove);
}

void test_commit_skips_overlap_length_while_visible_tail_active_225025() {
  // session_20260807_225025 @26.3s: macro commit must not seal overlap-elongated same-start tail.
  constexpr uint32_t kLoopLength = 5376;
  constexpr NoteId kOverlapId = 9;
  constexpr NoteId kMoverId = 13;
  constexpr uint8_t kPitch = 88;

  const NoteBaseline kCommitted{kPitch, 100, 2544, 3078};
  const NoteBaseline kBridgeTail{kPitch, 100, 2544, 2831};
  const NoteBaseline kMoverSpan{kPitch, 100, 2832, 2879};

  NoteEditFocus focus;
  focus.active = true;
  focus.movingNoteId = kMoverId;
  focus.commitBaseline = kMoverSpan;
  focus.last = kMoverSpan;
  focus.baselineMap[kOverlapId] = kCommitted;
  focus.baselineMap[kMoverId] = kCommitted;

  NoteEditCurrentState state;
  state.upsertRow(kMoverId, kCommitted, kMoverSpan, NoteEditPresenceType::Visible);
  state.upsertRow(kOverlapId, kCommitted, kBridgeTail, NoteEditPresenceType::Visible);

  const EditPassVec rows = buildCommitRowsFromCurrentState(focus, state, kChannel, kLoopLength);
  for (const EditPass& row : rows) {
    TEST_ASSERT_FALSE(row.targetNoteId == kOverlapId &&
                      row.actionType == EditActionType::Update &&
                      (row.propertyType == EditPropertyType::Length ||
                       row.propertyType == EditPropertyType::NoteRange));
  }
}

void test_deselect_clears_overlap_participation_without_geometry_restore_232118() {
  constexpr NoteId kOverlapId = 9;
  constexpr NoteId kMoverId = 13;
  constexpr uint8_t kPitch = 88;

  const NoteBaseline kCommitted{kPitch, 100, 2544, 3078};
  const NoteBaseline kTail{kPitch, 100, 2544, 2255};
  const NoteBaseline kMoverSpan{kPitch, 100, 3168, 3215};

  NoteEditFocus focus;
  focus.active = true;
  focus.movingNoteId = kMoverId;
  focus.baselineMap[kOverlapId] = kCommitted;

  NoteEditCurrentState state;
  state.upsertRow(kOverlapId, kCommitted, kTail, NoteEditPresenceType::Visible);

  clearChangedOverlapParticipationWhenInteractionCleared(focus, state, kMoverSpan, kMoverId);

  const NoteEditCurrentNoteState* row = state.find(kOverlapId);
  TEST_ASSERT_NOT_NULL(row);
  TEST_ASSERT_EQUAL_UINT32(kTail.endTick, row->currentSpan.endTick);
  TEST_ASSERT_EQUAL(static_cast<int>(NoteEditOverlapParticipationType::Ended),
                    static_cast<int>(row->overlapParticipation));
  TEST_ASSERT_FALSE(currentStateRowIsOverlapParticipant(*row));
}

void test_sync_committed_span_marks_visible_overlap_shorten_sealed() {
  constexpr NoteId kOverlap = 9;
  const NoteBaseline kFull{88, 100, 2016, 3078};
  const NoteBaseline kSealed{88, 100, 2016, 2255};

  NoteEditCurrentState state;
  state.upsertRow(kOverlap, kFull, kFull, NoteEditPresenceType::Visible);
  state.syncCommittedSpan(kOverlap, kSealed);

  const NoteEditCurrentNoteState* row = state.find(kOverlap);
  TEST_ASSERT_NOT_NULL(row);
  TEST_ASSERT_TRUE(row->visibleOverlapShortenSealed);
  TEST_ASSERT_EQUAL_UINT32(kSealed.endTick, row->committedSpan.endTick);
  TEST_ASSERT_EQUAL_UINT32(kSealed.endTick, row->currentSpan.endTick);
}

void test_macro_sealed_sync_committed_aligns_current_span_on_reselect_010657() {
  // session_20260808_010657: after macro seal, scrubbing select must not flash the overlap
  // stub length from stale currentSpan while focus is on another note.
  constexpr uint32_t kLoopLength = 5376;
  constexpr NoteId kOverlapId = 9;
  constexpr NoteId kMoverId = 11;
  constexpr uint8_t kPitch = 88;
  constexpr uint8_t kChannel = 5;
  constexpr NoteBaseline kStorage{kPitch, 100, 1200, 2398};
  constexpr NoteBaseline kSealed{kPitch, 100, 1296, 1438};
  constexpr NoteBaseline kStub{kPitch, 100, 1296, 1343};

  NoteEditCurrentState currentState;
  currentState.upsertRow(kMoverId, {kPitch, 100, 1440, 1487}, {kPitch, 100, 1440, 1487},
                         NoteEditPresenceType::Visible);
  currentState.upsertRow(kOverlapId, kSealed, kStub, NoteEditPresenceType::Visible);
  NoteEditCurrentNoteState* overlapRow = currentState.find(kOverlapId);
  TEST_ASSERT_NOT_NULL(overlapRow);
  overlapRow->visibleOverlapShortenSealed = true;
  currentState.syncCommittedSpan(kOverlapId, kSealed);
  TEST_ASSERT_EQUAL_UINT32(kSealed.endTick, overlapRow->currentSpan.endTick);

  MidiEventVec store;
  currentState.projectToSessionStore(store, kChannel);
  NoteBaseline live{};
  TEST_ASSERT_TRUE(readLiveLinearSpan(store, kOverlapId, kChannel, live));
  TEST_ASSERT_EQUAL_UINT32(kSealed.endTick, live.endTick);

  NoteUtils::DisplayNoteVec committedBase;
  committedBase.push_back({kOverlapId, kPitch, 100, kStorage.startTick, kStorage.endTick});
  committedBase.push_back({kMoverId, kPitch, 100, 1440, 1487});

  NoteEditFocus focus;
  focus.active = true;
  focus.movingNoteId = kMoverId;
  focus.last = {kPitch, 100, 1440, 1487};
  focus.movingNoteRange = {1440, 1487};
  focus.baselineMap[kOverlapId] = kStorage;
  focus.baselineMap[kMoverId] = {kPitch, 100, 1440, 1487};

  const NoteUtils::DisplayNoteVec projected =
      projectNoteEditDisplayNotes(committedBase, store, focus, kChannel, kLoopLength,
                                  &currentState);
  bool foundSealed = false;
  for (const NoteUtils::DisplayNote& dn : projected) {
    if (dn.noteId == kOverlapId) {
      TEST_ASSERT_EQUAL_UINT32(kSealed.endTick, dn.endTick);
      TEST_ASSERT_NOT_EQUAL(kStub.endTick, dn.endTick);
      foundSealed = true;
    }
  }
  TEST_ASSERT_TRUE(foundSealed);
}

void test_move_note_translates_committed_span() {
  // session_20260808_001226: macro-sealed overlap note repositioned must carry committedSpan.
  constexpr NoteId kOverlap = 9;
  const NoteBaseline kSealed{88, 100, 2640, 2975};
  const NoteBaseline kMoved{88, 100, 1680, 2015};

  NoteEditCurrentState state;
  state.upsertRow(kOverlap, kSealed, kSealed, NoteEditPresenceType::Visible);
  state.syncCommittedSpan(kOverlap, kSealed);

  EditSessionAction move{};
  move.type = EditSessionActionType::MoveNote;
  move.targetNoteId = kOverlap;
  move.startTick = kMoved.startTick;
  move.endTick = kMoved.endTick;
  move.pitch = kMoved.pitch;
  move.velocity = kMoved.velocity;
  state.applyEditSessionAction(move);

  const NoteEditCurrentNoteState* row = state.find(kOverlap);
  TEST_ASSERT_NOT_NULL(row);
  TEST_ASSERT_EQUAL_UINT32(kMoved.startTick, row->currentSpan.startTick);
  TEST_ASSERT_EQUAL_UINT32(kMoved.endTick, row->currentSpan.endTick);
  TEST_ASSERT_EQUAL_UINT32(kMoved.startTick, row->committedSpan.startTick);
  TEST_ASSERT_EQUAL_UINT32(kMoved.endTick, row->committedSpan.endTick);
}

void test_sync_committed_span_leave_restore_uses_sealed_position_200656() {
  // session_20260807_200656: leave-restore reverted note 9 to 2208 after macro commit sealed 2544.
  constexpr NoteId kPriorMover = 9;
  const NoteBaseline kOriginalCommitted{88, 100, 2208, 2255};
  const NoteBaseline kSealedSpan{88, 100, 2544, 2591};

  NoteEditCurrentState state;
  state.upsertRow(kPriorMover, kOriginalCommitted, kSealedSpan, NoteEditPresenceType::Hidden);
  state.syncCommittedSpan(kPriorMover, kSealedSpan);

  const ParticipatingNoteState participant =
      buildParticipatingNoteState(*state.find(kPriorMover));
  const NoteBaseline leaveRestore = participatingLeaveRestoreCommittedSpan(participant);
  TEST_ASSERT_EQUAL_UINT32(2544u, leaveRestore.startTick);
  TEST_ASSERT_EQUAL_UINT32(2591u, leaveRestore.endTick);
}

void test_display_projection_leave_restore_paints_baseline_when_mover_left_overlap() {
  // session_20260807_220917: hide+shorten promotes Visible shortened; paint committed length once
  // mover clears overlap — RestoreNote apply still required for inventory/session restore.
  constexpr uint32_t kLoopLength = 5376;
  constexpr NoteId kOverlapId = 10;
  constexpr NoteId kMoverId = 17;
  constexpr uint8_t kPitch = 88;

  NoteEditCurrentState currentState;
  currentState.upsertRow(kMoverId, {kPitch, 100, 3600, 4127}, {kPitch, 100, 1920, 2447},
                         NoteEditPresenceType::Visible);
  currentState.upsertRow(kOverlapId, {kPitch, 100, 2640, 3167}, {kPitch, 100, 2640, 3167},
                         NoteEditPresenceType::Visible);

  EditSessionAction hide{};
  hide.type = EditSessionActionType::HideNote;
  hide.targetNoteId = kOverlapId;
  hide.startTick = 2640;
  hide.endTick = 2735;
  hide.pitch = kPitch;
  currentState.applyEditSessionAction(hide);

  EditSessionAction shorten{};
  shorten.type = EditSessionActionType::ShortenNote;
  shorten.targetNoteId = kOverlapId;
  shorten.startTick = 2640;
  shorten.endTick = 2783;
  shorten.pitch = kPitch;
  currentState.applyEditSessionAction(shorten);

  MidiEventVec store;
  currentState.projectToSessionStore(store, kChannel);

  NoteUtils::DisplayNoteVec committedBase;
  committedBase.push_back({kOverlapId, kPitch, 100, 2640, 3167});
  committedBase.push_back({kMoverId, kPitch, 100, 3600, 4127});

  NoteEditFocus focus;
  focus.active = true;
  focus.movingNoteId = kMoverId;
  focus.last = {kPitch, 100, 1920, 2447};
  focus.movingNoteRange = {1920, 2447};
  focus.baselineMap[kOverlapId] = {kPitch, 100, 2640, 3167};
  focus.baselineMap[kMoverId] = {kPitch, 100, 3600, 4127};

  const NoteUtils::DisplayNoteVec visibleShortenedWhileMoverLeft =
      projectNoteEditDisplayNotes(committedBase, store, focus, kChannel, kLoopLength,
                                  &currentState);
  TEST_ASSERT_EQUAL(2, static_cast<int>(visibleShortenedWhileMoverLeft.size()));
  bool foundOverlapWhileMoverLeft = false;
  for (const NoteUtils::DisplayNote& dn : visibleShortenedWhileMoverLeft) {
    if (dn.noteId == kOverlapId) {
      TEST_ASSERT_EQUAL_UINT32(2640u, dn.startTick);
      TEST_ASSERT_EQUAL_UINT32(2783u, dn.endTick);
      foundOverlapWhileMoverLeft = true;
    }
  }
  TEST_ASSERT_TRUE(foundOverlapWhileMoverLeft);

  EditSessionAction fullRestore{};
  fullRestore.type = EditSessionActionType::RestoreNote;
  fullRestore.targetNoteId = kOverlapId;
  fullRestore.startTick = 2640;
  fullRestore.endTick = 3167;
  fullRestore.pitch = kPitch;
  currentState.applyEditSessionAction(fullRestore);
  currentState.projectToSessionStore(store, kChannel);

  const NoteUtils::DisplayNoteVec projected =
      projectNoteEditDisplayNotes(committedBase, store, focus, kChannel, kLoopLength,
                                  &currentState);
  TEST_ASSERT_EQUAL(2, static_cast<int>(projected.size()));
  bool foundOverlap = false;
  for (const NoteUtils::DisplayNote& dn : projected) {
    if (dn.noteId == kOverlapId) {
      TEST_ASSERT_EQUAL_UINT32(2640u, dn.startTick);
      TEST_ASSERT_EQUAL_UINT32(3167u, dn.endTick);
      foundOverlap = true;
    }
  }
  TEST_ASSERT_TRUE(foundOverlap);
}

void test_display_projection_mover_uses_current_state_not_stale_focus_last() {
  // session_20260807_151441 contract: projection must not paint mover at stale focus.last when
  // NoteEditCurrentState has the session-moved span.
  constexpr uint32_t kLoopLength = 5376;
  constexpr NoteId kMoverId = 17;
  constexpr uint8_t kPitch = 88;

  NoteEditCurrentState currentState;
  currentState.upsertRow(kMoverId, {kPitch, 100, 3600, 4127}, {kPitch, 100, 1296, 1823},
                         NoteEditPresenceType::Visible);

  MidiEventVec store;
  currentState.projectToSessionStore(store, kChannel);

  NoteUtils::DisplayNoteVec committedBase;
  committedBase.push_back({kMoverId, kPitch, 100, 3600, 4127});

  NoteEditFocus focus;
  focus.active = true;
  focus.movingNoteId = kMoverId;
  focus.last = {kPitch, 100, 2640, 2687};
  focus.commitBaseline = {kPitch, 100, 3600, 4127};
  focus.baselineMap[kMoverId] = focus.commitBaseline;

  const NoteUtils::DisplayNoteVec projected =
      projectNoteEditDisplayNotes(committedBase, store, focus, kChannel, kLoopLength,
                                  &currentState);
  TEST_ASSERT_EQUAL(1, static_cast<int>(projected.size()));
  TEST_ASSERT_EQUAL_UINT32(kMoverId, projected[0].noteId);
  TEST_ASSERT_EQUAL_UINT32(1296u, projected[0].startTick);
  TEST_ASSERT_EQUAL_UINT32(1823u, projected[0].endTick);
}

void test_display_projection_inactive_focus_projects_session_moved_span() {
  // RC10g: empty-step deselect still projects when NoteEditCurrentState has session edits.
  constexpr uint32_t kLoopLength = 5376;
  constexpr NoteId kMoverId = 17;
  constexpr uint8_t kPitch = 88;

  NoteEditCurrentState currentState;
  currentState.upsertRow(kMoverId, {kPitch, 100, 3600, 4127}, {kPitch, 100, 1920, 2447},
                         NoteEditPresenceType::Visible);

  MidiEventVec store;
  currentState.projectToSessionStore(store, kChannel);

  NoteUtils::DisplayNoteVec committedBase;
  committedBase.push_back({kMoverId, kPitch, 100, 3600, 4127});

  NoteEditFocus focus;
  focus.active = false;
  focus.baselineMap[kMoverId] = {kPitch, 100, 3600, 4127};

  const NoteUtils::DisplayNoteVec raw =
      projectNoteEditDisplayNotes(committedBase, store, focus, kChannel, kLoopLength, nullptr);
  TEST_ASSERT_EQUAL_UINT32(3600u, raw[0].startTick);

  const NoteUtils::DisplayNoteVec projected =
      projectNoteEditDisplayNotes(committedBase, store, focus, kChannel, kLoopLength,
                                  &currentState);
  TEST_ASSERT_EQUAL(1, static_cast<int>(projected.size()));
  TEST_ASSERT_EQUAL_UINT32(1920u, projected[0].startTick);
  TEST_ASSERT_EQUAL_UINT32(2447u, projected[0].endTick);
}

void test_driver_validation_rejects_hidden_row_matching_focus_last() {
  // session_20260807_153739: span-only driver gate let a Hidden leave-restore stub validate.
  NoteEditCurrentState currentState;
  currentState.upsertRow(kNoteA, {88, 100, 2640, 2687}, {88, 100, 2640, 3167},
                         NoteEditPresenceType::Hidden);

  NoteEditFocus focus;
  focus.active = true;
  focus.movingNoteId = kNoteA;
  focus.last = {88, 100, 2640, 2687};

  EditorSelection selection;
  selection.primaryNote = kNoteA;

  TEST_ASSERT_FALSE(currentState.rowProjectsToStore(kNoteA));
  TEST_ASSERT_FALSE(isLiveEditDriverValidFromCurrentState(selection, focus, currentState));
}

void test_projected_paint_includes_shortened_overlap_inventory_excludes() {
  // session_20260807_202147: OLED paint must show shortened stub; DNTE/select inventory stays masked.
  constexpr uint32_t kLoopLength = 5376;
  constexpr NoteId kOverlapId = 13;
  constexpr NoteId kMoverId = 9;
  constexpr uint8_t kPitch = 88;

  NoteEditCurrentState currentState;
  currentState.upsertRow(kMoverId, {kPitch, 100, 1296, 1391}, {kPitch, 100, 1296, 1391},
                         NoteEditPresenceType::Visible);
  currentState.upsertRow(kOverlapId, {kPitch, 100, 1728, 2364}, {kPitch, 100, 1728, 1775},
                         NoteEditPresenceType::Visible);

  MidiEventVec store;
  currentState.projectToSessionStore(store, kChannel);

  NoteUtils::DisplayNoteVec committedBase;
  committedBase.push_back({kOverlapId, kPitch, 100, 1728, 2364});
  committedBase.push_back({kMoverId, kPitch, 100, 1296, 1391});

  NoteEditFocus focus;
  focus.active = true;
  focus.movingNoteId = kMoverId;
  focus.last = {kPitch, 100, 1776, 1823};
  focus.movingNoteRange = {1776, 1823};
  focus.baselineMap[kOverlapId] = {kPitch, 100, 1728, 2364};
  focus.baselineMap[kMoverId] = {kPitch, 100, 1296, 1391};

  const NoteUtils::DisplayNoteVec paint =
      projectNoteEditDisplayNotes(committedBase, store, focus, kChannel, kLoopLength,
                                  &currentState);
  const NoteUtils::DisplayNoteVec selectable =
      filterProjectingSelectableDisplayNotes(paint, &currentState, focus, 1);

  TEST_ASSERT_EQUAL(2, static_cast<int>(paint.size()));
  TEST_ASSERT_EQUAL(1, static_cast<int>(selectable.size()));
  TEST_ASSERT_EQUAL_UINT32(kMoverId, selectable[0].noteId);

  bool foundShortenedStub = false;
  for (const NoteUtils::DisplayNote& dn : paint) {
    if (dn.noteId == kOverlapId) {
      TEST_ASSERT_EQUAL_UINT32(1728u, dn.startTick);
      TEST_ASSERT_EQUAL_UINT32(1775u, dn.endTick);
      foundShortenedStub = true;
    }
  }
  TEST_ASSERT_TRUE(foundShortenedStub);
}

void test_hide_full_baseline_then_shorten_overlap_tail_promotes_visible() {
  // session_20260807_202538: L→R overlap entry HideNote then ShortenNote must paint shortened stub.
  constexpr NoteId kOverlapId = 13;
  const NoteBaseline kCommitted{88, 100, 1728, 2364};
  const NoteBaseline kStub{88, 100, 1728, 1775};

  NoteEditCurrentState state;
  state.upsertRow(kOverlapId, kCommitted, kCommitted, NoteEditPresenceType::Visible);

  EditSessionAction hide{};
  hide.type = EditSessionActionType::HideNote;
  hide.targetNoteId = kOverlapId;
  hide.startTick = kCommitted.startTick;
  hide.endTick = kCommitted.endTick;
  hide.pitch = kCommitted.pitch;
  state.applyEditSessionAction(hide);

  EditSessionAction shorten{};
  shorten.type = EditSessionActionType::ShortenNote;
  shorten.targetNoteId = kOverlapId;
  shorten.startTick = kStub.startTick;
  shorten.endTick = kStub.endTick;
  shorten.pitch = kStub.pitch;
  state.applyEditSessionAction(shorten);

  TEST_ASSERT_FALSE(state.isRowHiddenOrDeleted(kOverlapId));
  TEST_ASSERT_TRUE(state.rowProjectsToStore(kOverlapId));
  TEST_ASSERT_FALSE(state.rowIncludedInSelectableInventory(kOverlapId));
}

void test_selectable_inventory_excludes_paint_only_hidden_row() {
  // Paint vs inventory split: visible shortened overlap paints current stub; selectable inventory
  // stays masked until macro commit seals the shorten.
  constexpr uint32_t kLoopLength = 5376;
  constexpr NoteId kOverlapId = 10;
  constexpr NoteId kMoverId = 17;
  constexpr uint8_t kPitch = 88;

  NoteEditCurrentState currentState;
  currentState.upsertRow(kMoverId, {kPitch, 100, 3600, 4127}, {kPitch, 100, 1920, 2447},
                         NoteEditPresenceType::Visible);
  currentState.upsertRow(kOverlapId, {kPitch, 100, 2640, 3167}, {kPitch, 100, 2640, 3167},
                         NoteEditPresenceType::Visible);

  EditSessionAction hide{};
  hide.type = EditSessionActionType::HideNote;
  hide.targetNoteId = kOverlapId;
  hide.startTick = 2640;
  hide.endTick = 2735;
  hide.pitch = kPitch;
  currentState.applyEditSessionAction(hide);

  EditSessionAction shorten{};
  shorten.type = EditSessionActionType::ShortenNote;
  shorten.targetNoteId = kOverlapId;
  shorten.startTick = 2640;
  shorten.endTick = 2783;
  shorten.pitch = kPitch;
  currentState.applyEditSessionAction(shorten);

  MidiEventVec store;
  currentState.projectToSessionStore(store, kChannel);

  NoteUtils::DisplayNoteVec committedBase;
  committedBase.push_back({kOverlapId, kPitch, 100, 2640, 3167});
  committedBase.push_back({kMoverId, kPitch, 100, 3600, 4127});

  NoteEditFocus focus;
  focus.active = true;
  focus.movingNoteId = kMoverId;
  focus.last = {kPitch, 100, 1920, 2447};
  focus.movingNoteRange = {1920, 2447};
  focus.baselineMap[kOverlapId] = {kPitch, 100, 2640, 3167};
  focus.baselineMap[kMoverId] = {kPitch, 100, 3600, 4127};

  const NoteUtils::DisplayNoteVec projected =
      projectNoteEditDisplayNotes(committedBase, store, focus, kChannel, kLoopLength,
                                  &currentState);
  TEST_ASSERT_EQUAL(2, static_cast<int>(projected.size()));
  bool foundOverlapPaint = false;
  for (const NoteUtils::DisplayNote& dn : projected) {
    if (dn.noteId == kOverlapId) {
      TEST_ASSERT_EQUAL_UINT32(2640u, dn.startTick);
      TEST_ASSERT_EQUAL_UINT32(2783u, dn.endTick);
      foundOverlapPaint = true;
    }
  }
  TEST_ASSERT_TRUE(foundOverlapPaint);

  const NoteUtils::DisplayNoteVec selectable =
      filterProjectingSelectableDisplayNotes(projected, &currentState, focus, 1);
  TEST_ASSERT_EQUAL(2, static_cast<int>(selectable.size()));
  for (const NoteUtils::DisplayNote& dn : selectable) {
    TEST_ASSERT_TRUE(
        currentState.rowIncludedInSelectableInventory(dn.noteId, focus, 1));
  }
}

void test_geometry_selection_resolves_mover_index_after_overlap_hidden() {
  // session_20260807_162713: overlap hide shrinks selectable; sync must land idx in bounds.
  constexpr uint32_t kLoopLength = 5376;
  constexpr uint32_t kLoopStart = 0;
  constexpr NoteId kOverlapId = 13;
  constexpr NoteId kMoverId = 17;
  constexpr uint8_t kPitch = 88;

  NoteEditCurrentState currentState;
  currentState.upsertRow(kMoverId, {kPitch, 100, 3600, 4127}, {kPitch, 100, 2928, 3359},
                         NoteEditPresenceType::Visible);
  currentState.upsertRow(kOverlapId, {kPitch, 100, 2640, 3167}, {kPitch, 100, 2640, 3167},
                         NoteEditPresenceType::Visible);

  EditSessionAction hide{};
  hide.type = EditSessionActionType::HideNote;
  hide.targetNoteId = kOverlapId;
  hide.startTick = 2928;
  hide.endTick = 2975;
  hide.pitch = kPitch;
  currentState.applyEditSessionAction(hide);

  MidiEventVec store;
  currentState.projectToSessionStore(store, kChannel);

  NoteUtils::DisplayNoteVec committedBase;
  committedBase.push_back({kOverlapId, kPitch, 100, 2640, 3167});
  committedBase.push_back({kMoverId, kPitch, 100, 3600, 4127});

  NoteEditFocus focus;
  focus.active = true;
  focus.movingNoteId = kMoverId;
  focus.last = {kPitch, 100, 2928, 3359};
  focus.baselineMap[kOverlapId] = {kPitch, 100, 2640, 3167};
  focus.baselineMap[kMoverId] = {kPitch, 100, 3600, 4127};

  const NoteUtils::DisplayNoteVec projected =
      projectNoteEditDisplayNotes(committedBase, store, focus, kChannel, kLoopLength,
                                  &currentState);
  const NoteUtils::DisplayNoteVec selectable =
      filterProjectingSelectableDisplayNotes(projected, &currentState, focus, 1);
  TEST_ASSERT_EQUAL(1, static_cast<int>(selectable.size()));

  EditorSelection selection;
  selection.primaryNote = kMoverId;
  selection.selectedNotes.push_back(kMoverId);
  selection.selectedTick = 2928;

  const int matchIdx =
      NoteEditDisplaySnapshot::resolveNoteEditHighlightIndex(selection, selectable, focus,
                                                             kLoopStart, kLoopLength, false);
  TEST_ASSERT_EQUAL(0, matchIdx);
  TEST_ASSERT_EQUAL_UINT32(kMoverId, selectable[static_cast<size_t>(matchIdx)].noteId);

  const int staleIdx = static_cast<int>(selectable.size());
  TEST_ASSERT_EQUAL(1, staleIdx);
  TEST_ASSERT_FALSE(staleIdx < static_cast<int>(selectable.size()));
}

int nearestSelectableDisplayNoteIndex(const NoteUtils::DisplayNoteVec& notes, uint32_t startTick,
                                      uint32_t loopLength, uint32_t loopStartTick) {
  if (notes.empty() || loopLength == 0) {
    return -1;
  }
  const uint32_t modStart = SelectNavigation::displayPhaseTick(startTick, loopLength);
  uint32_t bestDist = loopLength;
  int bestIdx = 0;
  for (int i = 0; i < static_cast<int>(notes.size()); ++i) {
    const uint32_t noteTick =
        displayStartTickFromStorageNote(notes[static_cast<size_t>(i)].startTick, loopStartTick,
                                        loopLength);
    const uint32_t dist =
        std::min((noteTick + loopLength - modStart) % loopLength,
                 (modStart + loopLength - noteTick) % loopLength);
    if (dist < bestDist) {
      bestDist = dist;
      bestIdx = i;
    }
  }
  return bestIdx;
}

void test_select_closest_note_snap_tie_breaks_to_first_display_order() {
  // Mirrors EditManager::selectClosestNote — equal distance keeps lower display-tick row.
  constexpr uint32_t kLoopLength = 1536;
  constexpr uint32_t kLoopStartTick = 0;

  NoteUtils::DisplayNoteVec notes;
  notes.push_back({11, 60, 100, 384, 480});
  notes.push_back({12, 67, 100, 1152, 1248});

  const int fromLeft = nearestSelectableDisplayNoteIndex(notes, 384, kLoopLength, kLoopStartTick);
  TEST_ASSERT_EQUAL(0, fromLeft);
  TEST_ASSERT_EQUAL_UINT32(11u, notes[static_cast<size_t>(fromLeft)].noteId);

  const int fromRight =
      nearestSelectableDisplayNoteIndex(notes, 1152, kLoopLength, kLoopStartTick);
  TEST_ASSERT_EQUAL(1, fromRight);
  TEST_ASSERT_EQUAL_UINT32(12u, notes[static_cast<size_t>(fromRight)].noteId);

  const int fromMidpoint =
      nearestSelectableDisplayNoteIndex(notes, 768, kLoopLength, kLoopStartTick);
  TEST_ASSERT_EQUAL(0, fromMidpoint);
  TEST_ASSERT_EQUAL_UINT32(11u, notes[static_cast<size_t>(fromMidpoint)].noteId);
}

void test_full_overlap_hide_excludes_paint_while_visual_cache_retains_note() {
  // session_20260807_203805 ~34.4s: HideNote full cover on short note 9; visualCache still
  // holds committed span but grid must not paint (visible == false, overlap active).
  constexpr uint32_t kLoopLength = 5376;
  constexpr NoteId kOverlapId = 9;
  constexpr NoteId kMoverId = 17;
  constexpr uint8_t kPitch = 88;
  constexpr NoteBaseline kCommitted{kPitch, 100, 2592, 2639};
  constexpr NoteBaseline kMoverCurrent{kPitch, 100, 2544, 2992};

  NoteEditCurrentState currentState;
  currentState.upsertRow(kMoverId, kMoverCurrent, {kPitch, 100, 3600, 4127},
                         NoteEditPresenceType::Visible);
  currentState.upsertRow(kOverlapId, kCommitted, kCommitted, NoteEditPresenceType::Visible);

  EditSessionAction hide{};
  hide.type = EditSessionActionType::HideNote;
  hide.targetNoteId = kOverlapId;
  hide.startTick = kCommitted.startTick;
  hide.endTick = kCommitted.endTick;
  hide.pitch = kPitch;
  currentState.applyEditSessionAction(hide);

  TEST_ASSERT_TRUE(currentState.isRowHiddenOrDeleted(kOverlapId));
  TEST_ASSERT_FALSE(currentState.rowIncludedInSelectableInventory(kOverlapId));

  MidiEventVec store;
  currentState.projectToSessionStore(store, kChannel);

  NoteUtils::DisplayNoteVec committedBase;
  committedBase.push_back({kOverlapId, kPitch, 100, kCommitted.startTick, kCommitted.endTick});
  committedBase.push_back({kMoverId, kPitch, 100, 3600, 4127});

  NoteEditFocus focus;
  focus.active = true;
  focus.movingNoteId = kMoverId;
  focus.last = kMoverCurrent;
  focus.movingNoteRange = {kMoverCurrent.startTick, kMoverCurrent.endTick};
  focus.baselineMap[kOverlapId] = kCommitted;
  focus.baselineMap[kMoverId] = {kPitch, 100, 3600, 4127};

  const NoteUtils::DisplayNoteVec projected =
      projectNoteEditDisplayNotes(committedBase, store, focus, kChannel, kLoopLength,
                                  &currentState);
  TEST_ASSERT_EQUAL(1, static_cast<int>(projected.size()));
  TEST_ASSERT_EQUAL_UINT32(kMoverId, projected[0].noteId);

  const NoteUtils::DisplayNoteVec selectable =
      filterProjectingSelectableDisplayNotes(projected, &currentState, focus, 0);
  TEST_ASSERT_EQUAL(1, static_cast<int>(selectable.size()));
  TEST_ASSERT_EQUAL_UINT32(kMoverId, selectable[0].noteId);
}

void test_full_overlap_hide_excludes_paint_after_commit_rebuild_clears_focus_latch() {
  // session_20260807_203805 ~30.1s: post-commit focus rebuild — Hidden participants still project-gated
  // while overlap row stays Hidden in current state — paint must still exclude.
  constexpr uint32_t kLoopLength = 5376;
  constexpr NoteId kOverlapId = 9;
  constexpr NoteId kMoverId = 17;
  constexpr uint8_t kPitch = 88;
  constexpr NoteBaseline kCommitted{kPitch, 100, 2592, 2639};
  constexpr NoteBaseline kMoverCurrent{kPitch, 100, 2544, 2992};

  NoteEditCurrentState currentState;
  currentState.upsertRow(kMoverId, kMoverCurrent, {kPitch, 100, 3600, 4127},
                         NoteEditPresenceType::Visible);
  currentState.upsertRow(kOverlapId, kCommitted, kCommitted, NoteEditPresenceType::Hidden);

  MidiEventVec store;
  currentState.projectToSessionStore(store, kChannel);

  NoteUtils::DisplayNoteVec committedBase;
  committedBase.push_back({kOverlapId, kPitch, 100, kCommitted.startTick, kCommitted.endTick});
  committedBase.push_back({kMoverId, kPitch, 100, 3600, 4127});

  NoteEditFocus focus;
  focus.active = true;
  focus.movingNoteId = kMoverId;
  focus.last = kMoverCurrent;
  focus.movingNoteRange = {kMoverCurrent.startTick, kMoverCurrent.endTick};
  focus.baselineMap[kOverlapId] = kCommitted;
  focus.baselineMap[kMoverId] = {kPitch, 100, 3600, 4127};
  // Post-commit rebuild: overlap latch cleared.

  const NoteUtils::DisplayNoteVec projected =
      projectNoteEditDisplayNotes(committedBase, store, focus, kChannel, kLoopLength,
                                  &currentState);
  TEST_ASSERT_EQUAL(1, static_cast<int>(projected.size()));
  TEST_ASSERT_EQUAL_UINT32(kMoverId, projected[0].noteId);
  for (const NoteUtils::DisplayNote& dn : projected) {
    TEST_ASSERT_NOT_EQUAL(kOverlapId, dn.noteId);
  }
}

void test_reselect_span_matched_mover_projects_current_not_visual_cache() {
  // session_20260807_215126 regression: after macro commit seals mover span, current == committed
  // but visualCache still holds the pre-move committed-base row — paint must follow current state.
  constexpr uint32_t kLoopLength = 5376;
  constexpr NoteId kMoverId = 17;
  constexpr NoteId kHiddenOverlapId = 9;
  constexpr uint8_t kPitch = 88;
  constexpr NoteBaseline kMovedSpan{kPitch, 100, 2256, 2303};
  constexpr NoteBaseline kOriginalCommitted{kPitch, 100, 3600, 4127};

  NoteEditCurrentState currentState;
  currentState.upsertRow(kMoverId, kMovedSpan, kMovedSpan, NoteEditPresenceType::Visible);
  currentState.upsertRow(kHiddenOverlapId, kMovedSpan, kMovedSpan, NoteEditPresenceType::Hidden);

  MidiEventVec store;
  currentState.projectToSessionStore(store, kChannel);
  store.push_back(noteOn(kMoverId, kPitch, kMovedSpan.startTick));
  store.push_back(noteOff(kMoverId, kPitch, kMovedSpan.endTick));

  NoteUtils::DisplayNoteVec committedBase;
  committedBase.push_back({kMoverId, kPitch, 100, kOriginalCommitted.startTick,
                           kOriginalCommitted.endTick});
  committedBase.push_back({kHiddenOverlapId, kPitch, 100, kMovedSpan.startTick,
                           kMovedSpan.endTick});

  NoteEditFocus focus;
  focus.active = true;
  focus.movingNoteId = kMoverId;
  focus.last = kMovedSpan;
  focus.movingNoteRange = {kMovedSpan.startTick, kMovedSpan.endTick};
  focus.baselineMap[kMoverId] = kOriginalCommitted;
  focus.baselineMap[kHiddenOverlapId] = kMovedSpan;

  const NoteUtils::DisplayNoteVec projected =
      projectNoteEditDisplayNotes(committedBase, store, focus, kChannel, kLoopLength,
                                  &currentState);
  TEST_ASSERT_EQUAL(1, static_cast<int>(projected.size()));
  TEST_ASSERT_EQUAL_UINT32(kMoverId, projected[0].noteId);
  TEST_ASSERT_EQUAL_UINT32(kMovedSpan.startTick, projected[0].startTick);
  TEST_ASSERT_EQUAL_UINT32(kMovedSpan.endTick, projected[0].endTick);
}

void test_contract_c9_projection_inventory_independence() {
  // C9 / roadmap step 2: paint and selectable are independent consumers — inventory
  // filtering must never drop a visible participant from the paint projection path.
  // Combines session_20260807_203805 shorten (visible stub) with step 1 hidden row.
  constexpr uint32_t kLoopLength = 5376;
  constexpr NoteId kMoverId = 17;
  constexpr NoteId kShortenedId = 13;
  constexpr NoteId kHiddenId = 9;
  constexpr uint8_t kPitch = 88;
  constexpr NoteBaseline kShortenedCommitted{kPitch, 100, 1728, 2364};
  constexpr NoteBaseline kShortenedCurrent{kPitch, 100, 1728, 1775};
  constexpr NoteBaseline kHiddenCommitted{kPitch, 100, 1824, 1871};
  constexpr NoteBaseline kMoverSpan{kPitch, 100, 1776, 2224};

  NoteEditCurrentState currentState;
  currentState.upsertRow(kMoverId, kMoverSpan, {kPitch, 100, 3648, 4127},
                         NoteEditPresenceType::Visible);
  currentState.upsertRow(kShortenedId, kShortenedCommitted, kShortenedCurrent,
                           NoteEditPresenceType::Visible);
  currentState.upsertRow(kHiddenId, kHiddenCommitted, kHiddenCommitted,
                         NoteEditPresenceType::Hidden);

  TEST_ASSERT_FALSE(currentState.rowIncludedInSelectableInventory(kShortenedId));
  TEST_ASSERT_FALSE(currentState.rowIncludedInSelectableInventory(kHiddenId));

  MidiEventVec store;
  currentState.projectToSessionStore(store, kChannel);

  NoteUtils::DisplayNoteVec committedBase;
  committedBase.push_back({kShortenedId, kPitch, 100, kShortenedCommitted.startTick,
                           kShortenedCommitted.endTick});
  committedBase.push_back({kHiddenId, kPitch, 100, kHiddenCommitted.startTick,
                           kHiddenCommitted.endTick});
  committedBase.push_back({kMoverId, kPitch, 100, 3648, 4127});

  NoteEditFocus focus;
  focus.active = true;
  focus.movingNoteId = kMoverId;
  focus.last = {kPitch, 100, 1824, 1871};
  focus.movingNoteRange = {1824, 1871};
  focus.baselineMap[kShortenedId] = kShortenedCommitted;
  focus.baselineMap[kHiddenId] = kHiddenCommitted;
  focus.baselineMap[kMoverId] = {kPitch, 100, 3648, 4127};

  const NoteUtils::DisplayNoteVec paint =
      projectNoteEditDisplayNotes(committedBase, store, focus, kChannel, kLoopLength,
                                  &currentState);
  const NoteUtils::DisplayNoteVec selectable =
      filterProjectingSelectableDisplayNotes(paint, &currentState, focus, 2);

  TEST_ASSERT_TRUE(static_cast<int>(paint.size()) > static_cast<int>(selectable.size()));

  bool paintHasShortenedStub = false;
  for (const NoteUtils::DisplayNote& dn : paint) {
    if (dn.noteId == kShortenedId) {
      TEST_ASSERT_EQUAL_UINT32(kShortenedCurrent.startTick, dn.startTick);
      TEST_ASSERT_EQUAL_UINT32(kShortenedCurrent.endTick, dn.endTick);
      paintHasShortenedStub = true;
    }
    TEST_ASSERT_NOT_EQUAL(kHiddenId, dn.noteId);
  }
  TEST_ASSERT_TRUE(paintHasShortenedStub);

  for (const NoteUtils::DisplayNote& dn : selectable) {
    TEST_ASSERT_NOT_EQUAL(kShortenedId, dn.noteId);
    TEST_ASSERT_NOT_EQUAL(kHiddenId, dn.noteId);
    TEST_ASSERT_TRUE(currentState.rowIncludedInSelectableInventory(dn.noteId));
  }

  for (const NoteUtils::DisplayNote& sel : selectable) {
    bool foundInPaint = false;
    for (const NoteUtils::DisplayNote& p : paint) {
      if (p.noteId == sel.noteId) {
        TEST_ASSERT_EQUAL_UINT32(sel.startTick, p.startTick);
        TEST_ASSERT_EQUAL_UINT32(sel.endTick, p.endTick);
        foundInPaint = true;
        break;
      }
    }
    TEST_ASSERT_TRUE(foundInPaint);
  }
}

void test_contract_c9_203805_shorten_paint_stub_inventory_masked() {
  // session_20260807_203805 ~18.8s: overlap participant note 13 shortened stub painted;
  // selectable inventory masks the tail while mover is inside overlap closure.
  constexpr uint32_t kLoopLength = 5376;
  constexpr NoteId kOverlapId = 13;
  constexpr NoteId kMoverId = 17;
  constexpr uint8_t kPitch = 88;
  constexpr NoteBaseline kCommitted{kPitch, 100, 1728, 2364};
  constexpr NoteBaseline kStub{kPitch, 100, 1728, 2303};

  NoteEditCurrentState currentState;
  currentState.upsertRow(kMoverId, {kPitch, 100, 1824, 2271}, {kPitch, 100, 3648, 4127},
                         NoteEditPresenceType::Visible);
  currentState.upsertRow(kOverlapId, kCommitted, kStub, NoteEditPresenceType::Visible);

  MidiEventVec store;
  currentState.projectToSessionStore(store, kChannel);

  NoteUtils::DisplayNoteVec committedBase;
  committedBase.push_back({kOverlapId, kPitch, 100, kCommitted.startTick, kCommitted.endTick});
  committedBase.push_back({kMoverId, kPitch, 100, 3648, 4127});

  NoteEditFocus focus;
  focus.active = true;
  focus.movingNoteId = kMoverId;
  focus.last = {kPitch, 100, 1824, 1871};
  focus.movingNoteRange = {1824, 1871};
  focus.baselineMap[kOverlapId] = kCommitted;
  focus.baselineMap[kMoverId] = {kPitch, 100, 3648, 4127};

  const NoteUtils::DisplayNoteVec paint =
      projectNoteEditDisplayNotes(committedBase, store, focus, kChannel, kLoopLength,
                                  &currentState);
  const NoteUtils::DisplayNoteVec selectable =
      filterProjectingSelectableDisplayNotes(paint, &currentState, focus, 1);

  TEST_ASSERT_EQUAL(2, static_cast<int>(paint.size()));
  TEST_ASSERT_EQUAL(1, static_cast<int>(selectable.size()));
  TEST_ASSERT_EQUAL_UINT32(kMoverId, selectable[0].noteId);

  bool foundStub = false;
  for (const NoteUtils::DisplayNote& dn : paint) {
    if (dn.noteId == kOverlapId) {
      TEST_ASSERT_EQUAL_UINT32(kStub.startTick, dn.startTick);
      TEST_ASSERT_EQUAL_UINT32(kStub.endTick, dn.endTick);
      foundStub = true;
    }
  }
  TEST_ASSERT_TRUE(foundStub);
}

void test_shortened_visible_selectable_after_deselect_233447() {
  // session_20260807_233447 ~115s: after R→L shorten + deselect, shortened overlap must be
  // selectable at its stub ticks (not inventory-masked when nothing is selected).
  constexpr uint32_t kLoopLength = 5376;
  constexpr NoteId kOverlapId = 9;
  constexpr NoteId kMoverId = 2;
  constexpr uint8_t kPitch = 88;
  constexpr NoteBaseline kCommitted{kPitch, 100, 1248, 1775};
  constexpr NoteBaseline kStub{kPitch, 100, 1248, 1343};

  NoteEditCurrentState currentState;
  currentState.upsertRow(kMoverId, {kPitch, 100, 1680, 1727}, {kPitch, 100, 1680, 1727},
                         NoteEditPresenceType::Visible);
  currentState.upsertRow(kOverlapId, kCommitted, kStub, NoteEditPresenceType::Visible);

  MidiEventVec store;
  currentState.projectToSessionStore(store, kChannel);

  NoteUtils::DisplayNoteVec committedBase;
  committedBase.push_back({kOverlapId, kPitch, 100, kCommitted.startTick, kCommitted.endTick});
  committedBase.push_back({kMoverId, kPitch, 100, 1680, 1727});

  NoteEditFocus focus;
  focus.active = false;
  focus.baselineMap[kOverlapId] = kCommitted;

  const NoteUtils::DisplayNoteVec paint =
      projectNoteEditDisplayNotes(committedBase, store, focus, kChannel, kLoopLength,
                                  &currentState);
  const NoteUtils::DisplayNoteVec selectable =
      filterProjectingSelectableDisplayNotes(paint, &currentState, focus, -1);

  bool foundOverlap = false;
  for (const NoteUtils::DisplayNote& dn : selectable) {
    if (dn.noteId == kOverlapId) {
      TEST_ASSERT_EQUAL_UINT32(kStub.startTick, dn.startTick);
      TEST_ASSERT_EQUAL_UINT32(kStub.endTick, dn.endTick);
      foundOverlap = true;
    }
  }
  TEST_ASSERT_TRUE(foundOverlap);
}

void test_visible_shortened_paints_stub_after_ltr_overlap_cleared_232700() {
  // Visible R→L shorten tail stays at shortened currentSpan once mover clears closure;
  // macro commit (overlap_baseline_diff) seals length — no RestoreNote to full committed.
  constexpr uint32_t kLoopLength = 5376;
  constexpr NoteId kOverlapId = 17;
  constexpr NoteId kMoverId = 13;
  constexpr uint8_t kPitch = 88;
  constexpr NoteBaseline kCommitted{kPitch, 100, 3072, 3520};
  constexpr NoteBaseline kStub{kPitch, 100, 3072, 3119};

  NoteEditCurrentState currentState;
  currentState.upsertRow(kMoverId, {kPitch, 100, 3120, 3167}, {kPitch, 100, 3024, 3071},
                         NoteEditPresenceType::Visible);
  currentState.upsertRow(kOverlapId, kCommitted, kStub, NoteEditPresenceType::Visible);

  MidiEventVec store;
  currentState.projectToSessionStore(store, kChannel);

  NoteUtils::DisplayNoteVec committedBase;
  committedBase.push_back({kOverlapId, kPitch, 100, kCommitted.startTick, kCommitted.endTick});
  committedBase.push_back({kMoverId, kPitch, 100, 3024, 3071});

  NoteEditFocus focus;
  focus.active = true;
  focus.movingNoteId = kMoverId;
  focus.last = {kPitch, 100, 3120, 3167};
  focus.movingNoteRange = {3120, 3167};
  focus.baselineMap[kOverlapId] = kCommitted;
  focus.baselineMap[kMoverId] = {kPitch, 100, 3024, 3071};

  focus.last = {kPitch, 100, 3600, 3647};
  focus.movingNoteRange = {3600, 3647};
  const NoteUtils::DisplayNoteVec afterOverlapCleared =
      projectNoteEditDisplayNotes(committedBase, store, focus, kChannel, kLoopLength,
                                  &currentState);
  bool foundStub = false;
  for (const NoteUtils::DisplayNote& dn : afterOverlapCleared) {
    if (dn.noteId == kOverlapId) {
      TEST_ASSERT_EQUAL_UINT32(kStub.endTick, dn.endTick);
      foundStub = true;
    }
  }
  TEST_ASSERT_TRUE(foundStub);

  focus.active = false;
  const NoteUtils::DisplayNoteVec deselected =
      projectNoteEditDisplayNotes(committedBase, store, focus, kChannel, kLoopLength,
                                  &currentState);
  bool foundStubDeselect = false;
  for (const NoteUtils::DisplayNote& dn : deselected) {
    if (dn.noteId == kOverlapId) {
      TEST_ASSERT_EQUAL_UINT32(kStub.endTick, dn.endTick);
      foundStubDeselect = true;
    }
  }
  TEST_ASSERT_TRUE(foundStubDeselect);
}

void test_sealed_visible_shortened_paints_current_stub_after_second_overlap_cleared_234904() {
  // session_20260807_234904 / session_20260808_013500: after a second overlap stub, projection
  // must keep painting currentSpan (1823). Painting sealed committed (2063) is the deselect
  // flash of the previous length — leave-restore belongs to apply/seal, not projection (C5).
  constexpr uint32_t kLoopLength = 5376;
  constexpr NoteId kOverlapId = 9;
  constexpr NoteId kMoverId = 13;
  constexpr uint8_t kPitch = 88;
  constexpr uint8_t kChannel = 5;
  constexpr NoteBaseline kStorage{kPitch, 100, 1776, 3078};
  constexpr NoteBaseline kSealed{kPitch, 100, 1776, 2063};
  constexpr NoteBaseline kStub{kPitch, 100, 1776, 1823};

  NoteEditCurrentState currentState;
  currentState.upsertRow(kMoverId, {kPitch, 100, 2112, 2159}, {kPitch, 100, 2112, 2159},
                         NoteEditPresenceType::Visible);
  currentState.upsertRow(kOverlapId, kSealed, kStub, NoteEditPresenceType::Visible);
  NoteEditCurrentNoteState* overlapRow = currentState.find(kOverlapId);
  TEST_ASSERT_NOT_NULL(overlapRow);
  overlapRow->visibleOverlapShortenSealed = true;

  MidiEventVec store;
  currentState.projectToSessionStore(store, kChannel);

  NoteUtils::DisplayNoteVec committedBase;
  committedBase.push_back({kOverlapId, kPitch, 100, kStorage.startTick, kStorage.endTick});
  committedBase.push_back({kMoverId, kPitch, 100, 2112, 2159});

  NoteEditFocus focus;
  focus.active = true;
  focus.movingNoteId = kMoverId;
  focus.last = {kPitch, 100, 1823, 1871};
  focus.movingNoteRange = {1823, 1871};
  focus.baselineMap[kOverlapId] = kStorage;
  focus.baselineMap[kMoverId] = {kPitch, 100, 2112, 2159};

  const NoteUtils::DisplayNoteVec duringOverlap =
      projectNoteEditDisplayNotes(committedBase, store, focus, kChannel, kLoopLength,
                                  &currentState);
  bool foundStub = false;
  for (const NoteUtils::DisplayNote& dn : duringOverlap) {
    if (dn.noteId == kOverlapId) {
      TEST_ASSERT_EQUAL_UINT32(kStub.endTick, dn.endTick);
      foundStub = true;
    }
  }
  TEST_ASSERT_TRUE(foundStub);

  focus.last = {kPitch, 100, 1344, 1391};
  focus.movingNoteRange = {1344, 1391};
  const NoteUtils::DisplayNoteVec afterOverlapCleared =
      projectNoteEditDisplayNotes(committedBase, store, focus, kChannel, kLoopLength,
                                  &currentState);
  bool foundCurrentStub = false;
  for (const NoteUtils::DisplayNote& dn : afterOverlapCleared) {
    if (dn.noteId == kOverlapId) {
      TEST_ASSERT_EQUAL_UINT32(kStub.endTick, dn.endTick);
      TEST_ASSERT_NOT_EQUAL(kSealed.endTick, dn.endTick);
      TEST_ASSERT_NOT_EQUAL(kStorage.endTick, dn.endTick);
      foundCurrentStub = true;
    }
  }
  TEST_ASSERT_TRUE(foundCurrentStub);
}

void test_sealed_visible_shortened_paints_current_stub_rtl_after_mover_exits_left_235724() {
  // session_20260807_235724 / session_20260808_013500: R→L exit must not projection-paint the
  // previous sealed committed end — keep currentSpan stub until apply/seal updates it.
  constexpr uint32_t kLoopLength = 5376;
  constexpr NoteId kOverlapId = 9;
  constexpr NoteId kMoverId = 11;
  constexpr uint8_t kPitch = 88;
  constexpr uint8_t kChannel = 5;
  constexpr NoteBaseline kSealed{kPitch, 100, 2016, 2255};
  constexpr NoteBaseline kStub{kPitch, 100, 2016, 2063};

  NoteEditCurrentState currentState;
  currentState.upsertRow(kMoverId, {kPitch, 100, 1968, 2015}, {kPitch, 100, 1968, 2015},
                         NoteEditPresenceType::Visible);
  currentState.upsertRow(kOverlapId, kSealed, kStub, NoteEditPresenceType::Visible);
  NoteEditCurrentNoteState* overlapRow = currentState.find(kOverlapId);
  TEST_ASSERT_NOT_NULL(overlapRow);
  overlapRow->visibleOverlapShortenSealed = true;

  MidiEventVec store;
  currentState.projectToSessionStore(store, kChannel);

  NoteUtils::DisplayNoteVec committedBase;
  committedBase.push_back({kOverlapId, kPitch, 100, kSealed.startTick, kSealed.endTick});
  committedBase.push_back({kMoverId, kPitch, 100, 1968, 2015});

  NoteEditFocus focus;
  focus.active = true;
  focus.movingNoteId = kMoverId;
  focus.last = {kPitch, 100, 1968, 2015};
  focus.movingNoteRange = {1968, 2015};
  focus.baselineMap[kOverlapId] = kSealed;
  focus.baselineMap[kMoverId] = {kPitch, 100, 1968, 2015};

  const NoteUtils::DisplayNoteVec afterRtlExit =
      projectNoteEditDisplayNotes(committedBase, store, focus, kChannel, kLoopLength,
                                  &currentState);
  bool foundCurrentStub = false;
  for (const NoteUtils::DisplayNote& dn : afterRtlExit) {
    if (dn.noteId == kOverlapId) {
      TEST_ASSERT_EQUAL_UINT32(kStub.endTick, dn.endTick);
      TEST_ASSERT_NOT_EQUAL(kSealed.endTick, dn.endTick);
      foundCurrentStub = true;
    }
  }
  TEST_ASSERT_TRUE(foundCurrentStub);
}

void test_sealed_visible_shortened_paints_stub_after_deselect_clears_participation_002309() {
  // session_20260808_002309 / 013500: even with visibleOverlapShortenSealed, projection paints
  // currentSpan stub — never the previous sealed committed length (deselect flash).
  constexpr uint32_t kLoopLength = 5376;
  constexpr NoteId kOverlapId = 9;
  constexpr NoteId kMoverId = 11;
  constexpr uint8_t kPitch = 88;
  constexpr uint8_t kChannel = 5;
  constexpr NoteBaseline kStorage{kPitch, 100, 1824, 2358};
  constexpr NoteBaseline kSealed{kPitch, 100, 1584, 1775};
  constexpr NoteBaseline kStub{kPitch, 100, 1584, 1679};

  NoteEditCurrentState currentState;
  currentState.upsertRow(kMoverId, {kPitch, 100, 1680, 1727}, {kPitch, 100, 1680, 1727},
                         NoteEditPresenceType::Visible);
  currentState.upsertRow(kOverlapId, kSealed, kStub, NoteEditPresenceType::Visible);
  NoteEditCurrentNoteState* overlapRow = currentState.find(kOverlapId);
  TEST_ASSERT_NOT_NULL(overlapRow);
  overlapRow->visibleOverlapShortenSealed = true;

  MidiEventVec store;
  currentState.projectToSessionStore(store, kChannel);

  NoteUtils::DisplayNoteVec committedBase;
  committedBase.push_back({kOverlapId, kPitch, 100, kStorage.startTick, kStorage.endTick});
  committedBase.push_back({kMoverId, kPitch, 100, 1680, 1727});

  NoteEditFocus focus;
  focus.active = true;
  focus.movingNoteId = kMoverId;
  focus.last = {kPitch, 100, 1344, 1391};
  focus.movingNoteRange = {1344, 1391};
  focus.baselineMap[kOverlapId] = kStorage;
  focus.baselineMap[kMoverId] = {kPitch, 100, 1680, 1727};

  const NoteUtils::DisplayNoteVec whileActive =
      projectNoteEditDisplayNotes(committedBase, store, focus, kChannel, kLoopLength,
                                  &currentState);
  bool foundStubWhileActive = false;
  for (const NoteUtils::DisplayNote& dn : whileActive) {
    if (dn.noteId == kOverlapId) {
      TEST_ASSERT_EQUAL_UINT32(kStub.endTick, dn.endTick);
      TEST_ASSERT_NOT_EQUAL(kSealed.endTick, dn.endTick);
      foundStubWhileActive = true;
    }
  }
  TEST_ASSERT_TRUE(foundStubWhileActive);

  clearChangedOverlapParticipationWhenInteractionCleared(focus, currentState, focus.last,
                                                        kMoverId);
  const NoteUtils::DisplayNoteVec afterDeselectParticipationCleared =
      projectNoteEditDisplayNotes(committedBase, store, focus, kChannel, kLoopLength,
                                  &currentState);
  bool foundStubAfterDeselect = false;
  for (const NoteUtils::DisplayNote& dn : afterDeselectParticipationCleared) {
    if (dn.noteId == kOverlapId) {
      TEST_ASSERT_EQUAL_UINT32(kStub.endTick, dn.endTick);
      foundStubAfterDeselect = true;
    }
  }
  TEST_ASSERT_TRUE(foundStubAfterDeselect);
}

void test_visible_shortened_paints_stub_after_inactive_focus_deselect_003330() {
  // session_20260808_003330: live overlap stub must not flash to full storage on deselect
  // when macro has not sealed the shorten yet.
  constexpr uint32_t kLoopLength = 5376;
  constexpr NoteId kOverlapId = 9;
  constexpr NoteId kMoverId = 13;
  constexpr uint8_t kPitch = 88;
  constexpr uint8_t kChannel = 5;
  constexpr NoteBaseline kStorage{kPitch, 100, 1824, 2398};
  constexpr NoteBaseline kCommitted{kPitch, 100, 1824, 2398};
  constexpr NoteBaseline kStub{kPitch, 100, 1824, 2159};

  NoteEditCurrentState currentState;
  currentState.upsertRow(kMoverId, {kPitch, 100, 2688, 2735}, {kPitch, 100, 2688, 2735},
                         NoteEditPresenceType::Visible);
  currentState.upsertRow(kOverlapId, kCommitted, kStub, NoteEditPresenceType::Visible);

  MidiEventVec store;
  currentState.projectToSessionStore(store, kChannel);

  NoteUtils::DisplayNoteVec committedBase;
  committedBase.push_back({kOverlapId, kPitch, 100, kStorage.startTick, kStorage.endTick});
  committedBase.push_back({kMoverId, kPitch, 100, 2688, 2735});

  NoteEditFocus focus;
  focus.active = false;
  focus.baselineMap[kOverlapId] = kStorage;

  const NoteUtils::DisplayNoteVec projected =
      projectNoteEditDisplayNotes(committedBase, store, focus, kChannel, kLoopLength,
                                  &currentState);
  bool foundStub = false;
  for (const NoteUtils::DisplayNote& dn : projected) {
    if (dn.noteId == kOverlapId) {
      TEST_ASSERT_EQUAL_UINT32(kStub.endTick, dn.endTick);
      TEST_ASSERT_NOT_EQUAL(kStorage.endTick, dn.endTick);
      foundStub = true;
    }
  }
  TEST_ASSERT_TRUE(foundStub);
}

void test_macro_sealed_shortened_paints_committed_after_inactive_focus_deselect_004136() {
  // session_20260808_004136 / 013500: after macro seal, syncCommittedSpan aligns currentSpan;
  // inactive deselect then paints the sealed length from currentSpan (not storage, not stub).
  constexpr uint32_t kLoopLength = 5376;
  constexpr NoteId kOverlapId = 9;
  constexpr NoteId kMoverId = 11;
  constexpr uint8_t kPitch = 88;
  constexpr uint8_t kChannel = 5;
  constexpr NoteBaseline kStorage{kPitch, 100, 1440, 2398};
  constexpr NoteBaseline kSealed{kPitch, 100, 1440, 1727};
  constexpr NoteBaseline kStub{kPitch, 100, 1440, 1487};

  NoteEditCurrentState currentState;
  currentState.upsertRow(kMoverId, {kPitch, 100, 1200, 1247}, {kPitch, 100, 1200, 1247},
                         NoteEditPresenceType::Visible);
  // Pre-seal: committed is still storage length; live stub is the active shorten.
  currentState.upsertRow(kOverlapId, kStorage, kStub, NoteEditPresenceType::Visible);
  NoteEditCurrentNoteState* overlapRow = currentState.find(kOverlapId);
  TEST_ASSERT_NOT_NULL(overlapRow);
  currentState.syncCommittedSpan(kOverlapId, kSealed);
  TEST_ASSERT_TRUE(overlapRow->visibleOverlapShortenSealed);
  TEST_ASSERT_EQUAL_UINT32(kSealed.endTick, overlapRow->currentSpan.endTick);

  MidiEventVec store;
  currentState.projectToSessionStore(store, kChannel);

  NoteUtils::DisplayNoteVec committedBase;
  committedBase.push_back({kOverlapId, kPitch, 100, kStorage.startTick, kStorage.endTick});
  committedBase.push_back({kMoverId, kPitch, 100, 1200, 1247});

  NoteEditFocus focus;
  focus.active = false;
  focus.baselineMap[kOverlapId] = kStorage;

  const NoteUtils::DisplayNoteVec projected =
      projectNoteEditDisplayNotes(committedBase, store, focus, kChannel, kLoopLength,
                                  &currentState);
  bool foundSealed = false;
  for (const NoteUtils::DisplayNote& dn : projected) {
    if (dn.noteId == kOverlapId) {
      TEST_ASSERT_EQUAL_UINT32(kSealed.endTick, dn.endTick);
      TEST_ASSERT_NOT_EQUAL(kStub.endTick, dn.endTick);
      TEST_ASSERT_NOT_EQUAL(kStorage.endTick, dn.endTick);
      foundSealed = true;
    }
  }
  TEST_ASSERT_TRUE(foundSealed);
}

void test_second_overlap_shorten_commits_before_deselect_clears_participation_004532() {
  // session_20260808_004532: every F1 navigation must seal overlap geometry while
  // changedOverlapNoteIds is still set — empty-step deselect must not clear participation first.
  constexpr uint32_t kLoopLength = 5376;
  constexpr NoteId kOverlapId = 9;
  constexpr NoteId kMoverId = 11;
  constexpr uint8_t kPitch = 88;
  constexpr uint8_t kChannel = 5;
  constexpr NoteBaseline kStorage{kPitch, 100, 1488, 2398};
  constexpr NoteBaseline kFirstSeal{kPitch, 100, 1488, 1966};
  constexpr NoteBaseline kSecondPass{kPitch, 100, 1488, 1774};

  NoteEditCurrentState currentState;
  currentState.upsertRow(kMoverId, {kPitch, 100, 1776, 1823}, {kPitch, 100, 1776, 1823},
                         NoteEditPresenceType::Visible);
  currentState.upsertRow(kOverlapId, kFirstSeal, kSecondPass, NoteEditPresenceType::Visible);
  NoteEditCurrentNoteState* overlapRow = currentState.find(kOverlapId);
  TEST_ASSERT_NOT_NULL(overlapRow);
  overlapRow->visibleOverlapShortenSealed = true;

  NoteEditFocus focus;
  focus.active = true;
  focus.movingNoteId = kMoverId;
  focus.last = {kPitch, 100, 1200, 1247};
  focus.baselineMap[kOverlapId] = kStorage;
  focus.baselineMap[kMoverId] = {kPitch, 100, 1200, 1247};

  const EditPassVec rowsWhileParticipating =
      buildCommitRowsFromCurrentState(focus, currentState, kChannel, kLoopLength);
  bool foundSecondPassLength = false;
  for (const EditPass& row : rowsWhileParticipating) {
    if (row.targetNoteId == kOverlapId && row.actionType == EditActionType::Update &&
        row.propertyType == EditPropertyType::Length) {
      TEST_ASSERT_EQUAL_UINT32(kSecondPass.startTick, row.startTick);
      TEST_ASSERT_EQUAL_UINT32(kSecondPass.endTick, row.endTick);
      foundSecondPassLength = true;
    }
  }
  TEST_ASSERT_TRUE(foundSecondPassLength);

  clearChangedOverlapParticipationWhenInteractionCleared(focus, currentState, focus.last, kMoverId);
  const EditPassVec rowsAfterParticipationCleared =
      buildCommitRowsFromCurrentState(focus, currentState, kChannel, kLoopLength);
  for (const EditPass& row : rowsAfterParticipationCleared) {
    TEST_ASSERT_FALSE(row.targetNoteId == kOverlapId);
  }
  TEST_ASSERT_EQUAL(static_cast<int>(NoteEditOverlapParticipationType::Ended),
                    static_cast<int>(overlapRow->overlapParticipation));
  TEST_ASSERT_EQUAL_UINT32(kSecondPass.endTick, overlapRow->currentSpan.endTick);

  // §11 step 5.4: stale latch must not re-authorize commit after Ended.
  const EditPassVec rowsWithStaleLatch =
      buildCommitRowsFromCurrentState(focus, currentState, kChannel, kLoopLength);
  for (const EditPass& row : rowsWithStaleLatch) {
    TEST_ASSERT_FALSE(row.targetNoteId == kOverlapId);
  }

  currentState.syncCommittedSpan(kOverlapId, kSecondPass);
  TEST_ASSERT_EQUAL_UINT32(kSecondPass.endTick, overlapRow->committedSpan.endTick);
}

void test_visible_shortened_paints_stub_while_overlap_closure_active_221717() {
  // session_20260807_221717 ~35.7s: short mover 9 over long overlap 17 — leave-restore paint must
  // follow participating overlap-closure (inclusive), not exclusive span overlap on movingNoteRange.
  constexpr uint32_t kLoopLength = 5376;
  constexpr NoteId kOverlapId = 17;
  constexpr NoteId kMoverId = 9;
  constexpr uint8_t kPitch = 88;
  constexpr NoteBaseline kCommitted{kPitch, 100, 2928, 3376};
  constexpr NoteBaseline kStub{kPitch, 100, 2928, 2975};

  NoteEditCurrentState currentState;
  currentState.upsertRow(kMoverId, {kPitch, 100, 2976, 3023}, {kPitch, 100, 2640, 2687},
                         NoteEditPresenceType::Visible);
  currentState.upsertRow(kOverlapId, kCommitted, kStub, NoteEditPresenceType::Visible);

  MidiEventVec store;
  currentState.projectToSessionStore(store, kChannel);

  NoteUtils::DisplayNoteVec committedBase;
  committedBase.push_back({kOverlapId, kPitch, 100, kCommitted.startTick, kCommitted.endTick});
  committedBase.push_back({kMoverId, kPitch, 100, 2640, 2687});

  NoteEditFocus focus;
  focus.active = true;
  focus.movingNoteId = kMoverId;
  focus.last = {kPitch, 100, 2976, 3023};
  focus.movingNoteRange = {2976, 3023};
  focus.baselineMap[kOverlapId] = kCommitted;
  focus.baselineMap[kMoverId] = {kPitch, 100, 2640, 2687};

  const NoteUtils::DisplayNoteVec whileClosureActive =
      projectNoteEditDisplayNotes(committedBase, store, focus, kChannel, kLoopLength,
                                  &currentState);
  bool foundStub = false;
  for (const NoteUtils::DisplayNote& dn : whileClosureActive) {
    if (dn.noteId == kOverlapId) {
      TEST_ASSERT_EQUAL_UINT32(kStub.endTick, dn.endTick);
      foundStub = true;
    }
  }
  TEST_ASSERT_TRUE(foundStub);
}

void test_contract_c7_leave_restore_visible_paints_after_apply_175858() {
  // session_20260807_175858 / 163621 step 3: RestoreNote promotes Hidden → Visible; paint uses
  // currentSpan only after apply — no projection-side leave-restore while still Hidden.
  constexpr uint32_t kLoopLength = 5376;
  constexpr NoteId kOverlapId = 9;
  constexpr NoteId kMoverId = 17;
  constexpr uint8_t kPitch = 88;
  constexpr NoteBaseline kCommitted{kPitch, 100, 2592, 2639};

  NoteEditCurrentState currentState;
  currentState.upsertRow(kMoverId, {kPitch, 100, 2544, 2992}, {kPitch, 100, 3600, 4127},
                         NoteEditPresenceType::Visible);
  currentState.upsertRow(kOverlapId, kCommitted, kCommitted, NoteEditPresenceType::Hidden);

  MidiEventVec store;
  currentState.projectToSessionStore(store, kChannel);

  NoteUtils::DisplayNoteVec committedBase;
  committedBase.push_back({kOverlapId, kPitch, 100, kCommitted.startTick, kCommitted.endTick});
  committedBase.push_back({kMoverId, kPitch, 100, 3600, 4127});

  NoteEditFocus focus;
  focus.active = true;
  focus.movingNoteId = kMoverId;
  focus.last = {kPitch, 100, 1920, 2447};
  focus.movingNoteRange = {1920, 2447};
  focus.baselineMap[kOverlapId] = kCommitted;
  focus.baselineMap[kMoverId] = {kPitch, 100, 3600, 4127};

  const NoteUtils::DisplayNoteVec hiddenPaint =
      projectNoteEditDisplayNotes(committedBase, store, focus, kChannel, kLoopLength,
                                  &currentState);
  TEST_ASSERT_EQUAL(1, static_cast<int>(hiddenPaint.size()));
  TEST_ASSERT_EQUAL_UINT32(kMoverId, hiddenPaint[0].noteId);

  EditSessionAction restore{};
  restore.type = EditSessionActionType::RestoreNote;
  restore.targetNoteId = kOverlapId;
  restore.startTick = kCommitted.startTick;
  restore.endTick = kCommitted.endTick;
  restore.pitch = kPitch;
  currentState.applyEditSessionAction(restore);
  currentState.projectToSessionStore(store, kChannel);

  TEST_ASSERT_FALSE(currentState.isRowHiddenOrDeleted(kOverlapId));
  TEST_ASSERT_TRUE(currentState.rowProjectsToStore(kOverlapId));

  const NoteUtils::DisplayNoteVec restoredPaint =
      projectNoteEditDisplayNotes(committedBase, store, focus, kChannel, kLoopLength,
                                  &currentState);
  TEST_ASSERT_EQUAL(2, static_cast<int>(restoredPaint.size()));

  bool foundRestored = false;
  for (const NoteUtils::DisplayNote& dn : restoredPaint) {
    if (dn.noteId == kOverlapId) {
      TEST_ASSERT_EQUAL_UINT32(kCommitted.startTick, dn.startTick);
      TEST_ASSERT_EQUAL_UINT32(kCommitted.endTick, dn.endTick);
      foundRestored = true;
    }
  }
  TEST_ASSERT_TRUE(foundRestored);

  const NoteUtils::DisplayNoteVec selectable =
      filterProjectingSelectableDisplayNotes(restoredPaint, &currentState, focus, -1);
  TEST_ASSERT_EQUAL(2, static_cast<int>(selectable.size()));
}

int main(int argc, char** argv) {
  UNITY_BEGIN();
  RUN_TEST(test_build_from_session_store_visible_rows);
  RUN_TEST(test_projection_visible_and_added_rows);
  RUN_TEST(test_hidden_and_deleted_rows_do_not_project);
  RUN_TEST(test_build_then_project_round_trip_parity);
  RUN_TEST(test_verify_selected_note_exists);
  RUN_TEST(test_projection_owner_refresh_restores_parity);
  RUN_TEST(test_compat_direct_store_mutation_breaks_projection_parity);
  RUN_TEST(test_projection_owner_path_after_current_state_edit);
  RUN_TEST(test_display_projection_same_pitch_reorder_uses_current_span);
  RUN_TEST(test_display_projection_inactive_focus_masks_hidden_overlaps);
  RUN_TEST(test_sync_focus_last_from_current_state);
  RUN_TEST(test_hide_then_shorten_stays_hidden_and_masks_on_deselect);
  RUN_TEST(test_shorten_overlap_tail_stays_visible_shortened_and_masks_inventory_on_deselect);
  RUN_TEST(test_leave_restore_inventory_masked_tail_stays_hidden);
  RUN_TEST(test_display_projection_leave_restore_paints_baseline_when_mover_left_overlap);
  RUN_TEST(test_display_projection_mover_uses_current_state_not_stale_focus_last);
  RUN_TEST(test_display_projection_inactive_focus_projects_session_moved_span);
  RUN_TEST(test_driver_validation_rejects_hidden_row_matching_focus_last);
  RUN_TEST(test_hide_full_baseline_then_shorten_overlap_tail_promotes_visible);
  RUN_TEST(test_projected_paint_includes_shortened_overlap_inventory_excludes);
  RUN_TEST(test_selectable_inventory_excludes_paint_only_hidden_row);
  RUN_TEST(test_full_overlap_hide_excludes_paint_while_visual_cache_retains_note);
  RUN_TEST(test_full_overlap_hide_excludes_paint_after_commit_rebuild_clears_focus_latch);
  RUN_TEST(test_reselect_span_matched_mover_projects_current_not_visual_cache);
  RUN_TEST(test_contract_c9_projection_inventory_independence);
  RUN_TEST(test_contract_c9_203805_shorten_paint_stub_inventory_masked);
  RUN_TEST(test_shortened_visible_selectable_after_deselect_233447);
  RUN_TEST(test_visible_shortened_paints_stub_after_ltr_overlap_cleared_232700);
  RUN_TEST(test_sealed_visible_shortened_paints_current_stub_after_second_overlap_cleared_234904);
  RUN_TEST(test_sealed_visible_shortened_paints_current_stub_rtl_after_mover_exits_left_235724);
  RUN_TEST(test_sealed_visible_shortened_paints_stub_after_deselect_clears_participation_002309);
  RUN_TEST(test_visible_shortened_paints_stub_after_inactive_focus_deselect_003330);
  RUN_TEST(test_macro_sealed_shortened_paints_committed_after_inactive_focus_deselect_004136);
  RUN_TEST(test_second_overlap_shorten_commits_before_deselect_clears_participation_004532);
  RUN_TEST(test_visible_shortened_paints_stub_while_overlap_closure_active_221717);
  RUN_TEST(test_contract_c7_leave_restore_visible_paints_after_apply_175858);
  RUN_TEST(test_geometry_selection_resolves_mover_index_after_overlap_hidden);
  RUN_TEST(test_select_closest_note_snap_tie_breaks_to_first_display_order);
  RUN_TEST(test_apply_hide_through_current_state_owner);
  RUN_TEST(test_mark_deleted_and_remove_added_row);
  RUN_TEST(test_commit_rows_from_current_state_overlap_shorten);
  RUN_TEST(test_commit_skips_overlap_length_while_visible_tail_active_225025);
  RUN_TEST(test_deselect_clears_overlap_participation_without_geometry_restore_232118);
  RUN_TEST(test_sync_committed_span_marks_visible_overlap_shorten_sealed);
  RUN_TEST(test_macro_sealed_sync_committed_aligns_current_span_on_reselect_010657);
  RUN_TEST(test_move_note_translates_committed_span);
  RUN_TEST(test_sync_committed_span_leave_restore_uses_sealed_position_200656);
  return UNITY_END();
}
