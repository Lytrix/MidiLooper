//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include <unity.h>
#include <cstdint>

#include "NoteEditCurrentState.h"
#include "MidiEvent.h"
#include "NoteEditFocus.h"
#include "EditSessionAction.h"
#include "Utils/NoteUtils.h"

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
  recordChangedOverlapNote(focus, kOverlapA);
  recordChangedOverlapNote(focus, kOverlapB);

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
  recordChangedOverlapNote(focus, kOverlapId);

  const NoteUtils::DisplayNoteVec projected =
      projectNoteEditDisplayNotes(committedBase, store, focus, kChannel, kLoopLength,
                                  &currentState);
  TEST_ASSERT_EQUAL(1, static_cast<int>(projected.size()));
  TEST_ASSERT_EQUAL_UINT32(kMoverId, projected[0].noteId);
}

void test_shorten_overlap_tail_stays_hidden_and_masks_on_deselect() {
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

  TEST_ASSERT_TRUE(currentState.isRowHiddenOrDeleted(kOverlapId));
  TEST_ASSERT_FALSE(currentState.rowProjectsToStore(kOverlapId));

  MidiEventVec store;
  currentState.projectToSessionStore(store, kChannel);

  NoteUtils::DisplayNoteVec committedBase;
  committedBase.push_back({kOverlapId, kPitch, 100, 2640, 3167});
  committedBase.push_back({kMoverId, kPitch, 100, 2736, 3263});

  NoteEditFocus focus;
  focus.active = false;
  focus.baselineMap[kOverlapId] = {kPitch, 100, 2640, 3167};
  recordChangedOverlapNote(focus, kOverlapId);

  const NoteUtils::DisplayNoteVec projected =
      projectNoteEditDisplayNotes(committedBase, store, focus, kChannel, kLoopLength,
                                  &currentState);
  TEST_ASSERT_EQUAL(1, static_cast<int>(projected.size()));
  TEST_ASSERT_EQUAL_UINT32(kMoverId, projected[0].noteId);
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
  recordChangedOverlapNote(focus, kOverlapId);

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
  TEST_ASSERT_EQUAL(2, static_cast<int>(projectedAfterLeave.size()));
  bool foundOverlapAfterLeave = false;
  for (const NoteUtils::DisplayNote& dn : projectedAfterLeave) {
    if (dn.noteId == kOverlapId) {
      TEST_ASSERT_EQUAL_UINT32(2640u, dn.startTick);
      TEST_ASSERT_EQUAL_UINT32(3167u, dn.endTick);
      foundOverlapAfterLeave = true;
    }
  }
  TEST_ASSERT_TRUE(foundOverlapAfterLeave);

  EditSessionAction fullRestore{};
  fullRestore.type = EditSessionActionType::RestoreNote;
  fullRestore.targetNoteId = kOverlapId;
  fullRestore.startTick = 2640;
  fullRestore.endTick = 3167;
  fullRestore.pitch = kPitch;
  currentState.applyEditSessionAction(fullRestore);

  TEST_ASSERT_FALSE(currentState.isRowHiddenOrDeleted(kOverlapId));
  TEST_ASSERT_TRUE(currentState.rowProjectsToStore(kOverlapId));
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
  recordChangedOverlapNote(focus, kOverlapId);

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

void test_display_projection_leave_restore_paints_baseline_when_mover_left_overlap() {
  // RC10g: hidden overlap paints full baselineMap span once mover leaves overlap zone.
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
  recordChangedOverlapNote(focus, kOverlapId);

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
  RUN_TEST(test_shorten_overlap_tail_stays_hidden_and_masks_on_deselect);
  RUN_TEST(test_leave_restore_inventory_masked_tail_stays_hidden);
  RUN_TEST(test_display_projection_leave_restore_paints_baseline_when_mover_left_overlap);
  RUN_TEST(test_display_projection_inactive_focus_projects_session_moved_span);
  RUN_TEST(test_apply_hide_through_current_state_owner);
  RUN_TEST(test_mark_deleted_and_remove_added_row);
  RUN_TEST(test_commit_rows_from_current_state_overlap_shorten);
  return UNITY_END();
}
