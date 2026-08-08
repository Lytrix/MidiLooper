//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include <unity.h>

#include <algorithm>

#include "../test_support/NoteEditFocusTestDeps.cpp"
#include "../../src/Logger.cpp"
#include "../../src/Utils/IntervalProjection.cpp"
#include "../../src/Utils/NoteUtils.cpp"

#include "EditSessionAction.h"
#include "ParticipatingNoteSession.h"

void test_current_state_row_visibility_maps_from_presence() {
  NoteEditCurrentNoteState visible{};
  visible.presence = NoteEditPresenceType::Visible;
  NoteEditCurrentNoteState added{};
  added.presence = NoteEditPresenceType::Added;
  NoteEditCurrentNoteState hidden{};
  hidden.presence = NoteEditPresenceType::Hidden;
  NoteEditCurrentNoteState deleted{};
  deleted.presence = NoteEditPresenceType::Deleted;

  TEST_ASSERT_TRUE(currentStateRowIsVisible(visible));
  TEST_ASSERT_TRUE(currentStateRowIsVisible(added));
  TEST_ASSERT_FALSE(currentStateRowIsVisible(hidden));
  TEST_ASSERT_FALSE(currentStateRowIsVisible(deleted));
}

void test_participating_note_is_visible_matches_current_state() {
  NoteEditCurrentNoteState row{};
  row.noteId = 17;
  row.committedSpan = {88, 100, 3216, 3743};
  row.currentSpan = {88, 100, 3216, 3263};
  row.presence = NoteEditPresenceType::Hidden;

  const ParticipatingNoteState state = buildParticipatingNoteState(row);
  TEST_ASSERT_FALSE(state.visible);
  TEST_ASSERT_FALSE(participatingNoteIsVisible(state));

  row.presence = NoteEditPresenceType::Visible;
  const ParticipatingNoteState visibleState = buildParticipatingNoteState(row);
  TEST_ASSERT_TRUE(visibleState.visible);
  TEST_ASSERT_TRUE(participatingNoteIsVisible(visibleState));
}

void test_row_is_visible_matches_row_projects_to_store() {
  NoteEditCurrentState currentState;
  currentState.upsertRow(3, {88, 100, 3072, 3215}, {88, 100, 3168, 3311},
                         NoteEditPresenceType::Visible);
  currentState.upsertRow(17, {88, 100, 3216, 3743}, {88, 100, 3216, 3263},
                         NoteEditPresenceType::Hidden);
  currentState.upsertRow(42, {60, 100, 100, 200}, {60, 100, 100, 200},
                         NoteEditPresenceType::Added);

  TEST_ASSERT_TRUE(currentState.rowIsVisible(3));
  TEST_ASSERT_FALSE(currentState.rowIsVisible(17));
  TEST_ASSERT_TRUE(currentState.rowIsVisible(42));
  TEST_ASSERT_EQUAL(currentState.rowIsVisible(3), currentState.rowProjectsToStore(3));
  TEST_ASSERT_EQUAL(currentState.rowIsVisible(17), currentState.rowProjectsToStore(17));
  TEST_ASSERT_EQUAL(currentState.rowIsVisible(42), currentState.rowProjectsToStore(42));
}

void test_current_state_row_lifecycle_maps_from_presence() {
  NoteEditCurrentNoteState visible{};
  visible.presence = NoteEditPresenceType::Visible;
  NoteEditCurrentNoteState hidden{};
  hidden.presence = NoteEditPresenceType::Hidden;
  NoteEditCurrentNoteState added{};
  added.presence = NoteEditPresenceType::Added;
  NoteEditCurrentNoteState deleted{};
  deleted.presence = NoteEditPresenceType::Deleted;

  TEST_ASSERT_EQUAL(static_cast<int>(NoteEditLifecycleType::Existing),
                    static_cast<int>(currentStateRowLifecycle(visible)));
  TEST_ASSERT_EQUAL(static_cast<int>(NoteEditLifecycleType::Existing),
                    static_cast<int>(currentStateRowLifecycle(hidden)));
  TEST_ASSERT_EQUAL(static_cast<int>(NoteEditLifecycleType::Added),
                    static_cast<int>(currentStateRowLifecycle(added)));
  TEST_ASSERT_EQUAL(static_cast<int>(NoteEditLifecycleType::Deleted),
                    static_cast<int>(currentStateRowLifecycle(deleted)));
}

void test_lifecycle_independent_from_visibility() {
  NoteEditCurrentNoteState hidden{};
  hidden.noteId = 17;
  hidden.presence = NoteEditPresenceType::Hidden;
  hidden.committedSpan = {88, 100, 3216, 3743};
  hidden.currentSpan = {88, 100, 3216, 3263};

  NoteEditCurrentNoteState deleted{};
  deleted.noteId = 18;
  deleted.presence = NoteEditPresenceType::Deleted;
  deleted.committedSpan = {88, 100, 4000, 4500};
  deleted.currentSpan = {88, 100, 4000, 4500};

  NoteEditCurrentNoteState added{};
  added.noteId = 42;
  added.presence = NoteEditPresenceType::Added;
  added.committedSpan = {60, 100, 100, 200};
  added.currentSpan = {60, 100, 100, 200};

  const ParticipatingNoteState hiddenState = buildParticipatingNoteState(hidden);
  TEST_ASSERT_FALSE(hiddenState.visible);
  TEST_ASSERT_EQUAL(static_cast<int>(NoteEditLifecycleType::Existing),
                    static_cast<int>(hiddenState.lifecycle));
  TEST_ASSERT_FALSE(currentStateRowLifecycleIsDeleted(hidden));
  TEST_ASSERT_FALSE(currentStateRowLifecycleIsAdded(hidden));

  const ParticipatingNoteState deletedState = buildParticipatingNoteState(deleted);
  TEST_ASSERT_FALSE(deletedState.visible);
  TEST_ASSERT_EQUAL(static_cast<int>(NoteEditLifecycleType::Deleted),
                    static_cast<int>(deletedState.lifecycle));
  TEST_ASSERT_TRUE(currentStateRowLifecycleIsDeleted(deleted));

  const ParticipatingNoteState addedState = buildParticipatingNoteState(added);
  TEST_ASSERT_TRUE(addedState.visible);
  TEST_ASSERT_EQUAL(static_cast<int>(NoteEditLifecycleType::Added),
                    static_cast<int>(addedState.lifecycle));
  TEST_ASSERT_TRUE(currentStateRowLifecycleIsAdded(added));
  TEST_ASSERT_TRUE(participatingNoteLifecycle(addedState) == NoteEditLifecycleType::Added);
}

void test_row_lifecycle_matches_current_state() {
  NoteEditCurrentState currentState;
  currentState.upsertRow(3, {88, 100, 3072, 3215}, {88, 100, 3168, 3311},
                         NoteEditPresenceType::Visible);
  currentState.upsertRow(17, {88, 100, 3216, 3743}, {88, 100, 3216, 3263},
                         NoteEditPresenceType::Hidden);
  currentState.upsertRow(99, {60, 100, 100, 200}, {60, 100, 100, 200},
                         NoteEditPresenceType::Deleted);

  TEST_ASSERT_EQUAL(static_cast<int>(NoteEditLifecycleType::Existing),
                    static_cast<int>(currentState.rowLifecycle(3)));
  TEST_ASSERT_EQUAL(static_cast<int>(NoteEditLifecycleType::Existing),
                    static_cast<int>(currentState.rowLifecycle(17)));
  TEST_ASSERT_EQUAL(static_cast<int>(NoteEditLifecycleType::Deleted),
                    static_cast<int>(currentState.rowLifecycle(99)));
}

NoteBaseline makeSpan(uint8_t pitch, uint32_t start, uint32_t end) {
  return {pitch, 100, start, end};
}

void test_span_geometry_predicates_plan_table() {
  const NoteBaseline committed = makeSpan(60, 100, 200);

  const NoteBaseline movedOriginal = makeSpan(60, 120, 220);
  TEST_ASSERT_TRUE(participatingSpanIsMoved(movedOriginal, committed));
  TEST_ASSERT_TRUE(participatingSpanHasOriginalLength(movedOriginal, committed));
  TEST_ASSERT_FALSE(participatingSpanIsRightTailShortened(movedOriginal, committed));
  TEST_ASSERT_FALSE(participatingSpanHasShorterDuration(movedOriginal, committed));

  const NoteBaseline rightTailShortened = makeSpan(60, 100, 150);
  TEST_ASSERT_TRUE(participatingSpanIsRightTailShortened(rightTailShortened, committed));
  TEST_ASSERT_TRUE(participatingSpanHasShorterDuration(rightTailShortened, committed));
  TEST_ASSERT_FALSE(participatingSpanIsMoved(rightTailShortened, committed));
  TEST_ASSERT_FALSE(participatingSpanHasOriginalLength(rightTailShortened, committed));

  const NoteBaseline movedShorter = makeSpan(60, 120, 170);
  TEST_ASSERT_TRUE(participatingSpanIsMoved(movedShorter, committed));
  TEST_ASSERT_TRUE(participatingSpanHasShorterDuration(movedShorter, committed));
  TEST_ASSERT_FALSE(participatingSpanIsRightTailShortened(movedShorter, committed));
  TEST_ASSERT_FALSE(participatingSpanHasOriginalLength(movedShorter, committed));
}

void test_current_state_row_geometry_predicates() {
  NoteEditCurrentNoteState row{};
  row.committedSpan = makeSpan(60, 100, 200);
  row.currentSpan = makeSpan(60, 100, 150);

  TEST_ASSERT_TRUE(currentStateRowIsRightTailShortened(row));
  TEST_ASSERT_TRUE(currentStateRowHasShorterDuration(row));
  TEST_ASSERT_FALSE(currentStateRowIsMoved(row));
  TEST_ASSERT_FALSE(currentStateRowHasOriginalLength(row));
}

void test_participating_note_geometry_predicates_match_spans() {
  ParticipatingNoteState state{};
  state.committedSpan = makeSpan(60, 100, 200);
  state.currentSpan = makeSpan(60, 120, 170);

  TEST_ASSERT_TRUE(participatingNoteIsMoved(state));
  TEST_ASSERT_TRUE(participatingNoteHasShorterDuration(state));
  TEST_ASSERT_FALSE(participatingNoteIsRightTailShortened(state));
  TEST_ASSERT_TRUE(participatingNoteShortenedVsCommitted(state.currentSpan, state.committedSpan) ==
                   participatingNoteIsRightTailShortened(state));
}

void test_hidden_participant_does_not_project() {
  NoteEditCurrentNoteState row{};
  row.noteId = 17;
  row.committedSpan = {88, 100, 3216, 3743};
  row.currentSpan = {88, 100, 3216, 3263};
  row.presence = NoteEditPresenceType::Hidden;

  const ParticipatingNoteState state = buildParticipatingNoteState(row);
  TEST_ASSERT_FALSE(state.visible);
  TEST_ASSERT_FALSE(participatingNoteIsVisible(state));
  TEST_ASSERT_TRUE(participatingNoteIsRightTailShortened(state));

  const ParticipatingNoteInvariantResult inv = verifyParticipatingNoteInvariants(state);
  TEST_ASSERT_TRUE(inv.passed);
}

void test_visible_shortened_tail_projects_when_visible() {
  NoteEditCurrentNoteState row{};
  row.noteId = 10;
  row.committedSpan = {88, 100, 2640, 3167};
  row.currentSpan = {88, 100, 2640, 2783};
  row.presence = NoteEditPresenceType::Visible;

  const ParticipatingNoteState state = buildParticipatingNoteState(row);
  TEST_ASSERT_TRUE(state.visible);
  TEST_ASSERT_TRUE(participatingNoteIsVisible(state));
  TEST_ASSERT_TRUE(participatingNoteIsRightTailShortened(state));
  TEST_ASSERT_TRUE(verifyParticipatingNoteInvariants(state).passed);
}

void test_session_builds_from_current_state_and_selection() {
  EditorSelection selection{};
  selection.primaryNote = 3;
  selection.selectedNotes.push_back(3);
  selection.selectedNotes.push_back(17);

  NoteEditCurrentState currentState;
  currentState.upsertRow(3, {88, 100, 3072, 3215}, {88, 100, 3168, 3311},
                         NoteEditPresenceType::Visible);
  currentState.upsertRow(17, {88, 100, 3216, 3743}, {88, 100, 3216, 3263},
                         NoteEditPresenceType::Hidden);

  const ParticipatingNoteSession session =
      buildParticipatingNoteSession(selection, currentState);
  TEST_ASSERT_EQUAL_UINT32(3u, session.primaryNoteId);
  TEST_ASSERT_EQUAL(2, static_cast<int>(session.participatingNotes.size()));
  TEST_ASSERT_TRUE(participatingNoteIsRightTailShortened(session.participatingNotes.at(17)));
  TEST_ASSERT_TRUE(verifyParticipatingSessionInvariants(session).passed);
}

void test_primary_driver_must_project() {
  ParticipatingNoteSession session{};
  session.primaryNoteId = 17;
  ParticipatingNoteState hidden{};
  hidden.noteId = 17;
  hidden.visible = false;
  hidden.lifecycle = NoteEditLifecycleType::Existing;
  hidden.committedSpan = {88, 100, 3216, 3743};
  hidden.currentSpan = {88, 100, 3216, 3263};
  session.participatingNotes.emplace(17, hidden);

  const ParticipatingSessionInvariantResult inv = verifyParticipatingSessionInvariants(session);
  TEST_ASSERT_FALSE(inv.passed);
  TEST_ASSERT_TRUE(inv.actionTargetWouldDependOnProjection);
}

void test_collect_overlap_participant_ids_from_current_state() {
  NoteEditCurrentState currentState;
  currentState.upsertRow(3, {88, 100, 3072, 3215}, {88, 100, 3168, 3311},
                         NoteEditPresenceType::Visible);
  currentState.upsertRow(17, {88, 100, 3216, 3743}, {88, 100, 3216, 3263},
                         NoteEditPresenceType::Hidden);
  currentState.upsertRow(10, {88, 100, 2640, 3167}, {88, 100, 2640, 2783},
                         NoteEditPresenceType::Visible);

  const NoteIdList participants =
      collectOverlapParticipantNoteIdsFromCurrentState(currentState, 3);
  TEST_ASSERT_EQUAL(2, static_cast<int>(participants.size()));
  TEST_ASSERT_TRUE(std::find(participants.begin(), participants.end(), 17) !=
                   participants.end());
  TEST_ASSERT_TRUE(std::find(participants.begin(), participants.end(), 10) !=
                   participants.end());
}

void test_participating_leave_restore_hidden_qualifies() {
  ParticipatingNoteState hidden{};
  hidden.noteId = 17;
  hidden.visible = false;
  hidden.lifecycle = NoteEditLifecycleType::Existing;
  hidden.committedSpan = {88, 100, 3216, 3743};
  hidden.currentSpan = {88, 100, 3216, 3263};
  TEST_ASSERT_TRUE(participatingNoteQualifiesForLeaveRestoreTarget(hidden, 3));
  TEST_ASSERT_TRUE(participatingNoteNeedsFullCommittedLeaveRestore(hidden));
  const NoteBaseline span = participatingLeaveRestoreCommittedSpan(hidden);
  TEST_ASSERT_EQUAL_UINT32(3216u, span.startTick);
  TEST_ASSERT_EQUAL_UINT32(3743u, span.endTick);
}

void test_participating_visible_shortened_does_not_qualify_for_leave_restore() {
  ParticipatingNoteState visibleShortened{};
  visibleShortened.noteId = 9;
  visibleShortened.visible = true;
  visibleShortened.lifecycle = NoteEditLifecycleType::Existing;
  visibleShortened.committedSpan = {88, 100, 2016, 3078};
  visibleShortened.currentSpan = {88, 100, 2016, 2303};
  TEST_ASSERT_FALSE(participatingNoteQualifiesForLeaveRestoreTarget(visibleShortened, 13));
  TEST_ASSERT_FALSE(participatingNoteNeedsFullCommittedLeaveRestore(visibleShortened));
  const NoteBaseline storage{88, 100, 2016, 3078};
  TEST_ASSERT_FALSE(
      participatingNoteQualifiesForSealedVisibleShortenedLeaveRestore(visibleShortened, storage, 13));
}

void test_participating_sealed_visible_shortened_qualifies_for_committed_leave_restore() {
  ParticipatingNoteState sealedShortened{};
  sealedShortened.noteId = 9;
  sealedShortened.visible = true;
  sealedShortened.lifecycle = NoteEditLifecycleType::Existing;
  sealedShortened.committedSpan = {88, 100, 1776, 2063};
  sealedShortened.currentSpan = {88, 100, 1776, 1823};
  const NoteBaseline storage{88, 100, 1776, 3078};
  TEST_ASSERT_TRUE(
      participatingNoteQualifiesForSealedVisibleShortenedLeaveRestore(sealedShortened, storage, 13));
}

void test_participating_sealed_visible_shortened_qualifies_when_macro_sealed_flag_set() {
  ParticipatingNoteState sealedShortened{};
  sealedShortened.noteId = 9;
  sealedShortened.visible = true;
  sealedShortened.lifecycle = NoteEditLifecycleType::Existing;
  sealedShortened.committedSpan = {88, 100, 2016, 2255};
  sealedShortened.currentSpan = {88, 100, 2016, 2063};
  sealedShortened.visibleOverlapShortenSealed = true;
  const NoteBaseline storage{88, 100, 2016, 2255};
  TEST_ASSERT_TRUE(
      participatingNoteQualifiesForSealedVisibleShortenedLeaveRestore(sealedShortened, storage, 11));
}

void test_participating_deleted_does_not_qualify_for_leave_restore_022849() {
  // Stage 7.5.E2: sealed Deleted after deselect must not leave-restore.
  ParticipatingNoteState deleted{};
  deleted.noteId = 11;
  deleted.visible = false;
  deleted.lifecycle = NoteEditLifecycleType::Deleted;
  deleted.committedSpan = {88, 100, 1584, 1631};
  deleted.currentSpan = {88, 100, 1584, 1631};
  TEST_ASSERT_FALSE(participatingNoteQualifiesForLeaveRestoreTarget(deleted, 7));
  TEST_ASSERT_FALSE(participatingNoteNeedsFullCommittedLeaveRestore(deleted));
}

void test_overlap_participation_ended_after_sticky_clear() {
  // §11 step 5.3: sticky clear marks Ended — participation false, geometry unchanged, latch dual-write forgotten.
  constexpr NoteId kOverlapId = 9;
  constexpr NoteId kMoverId = 11;
  constexpr uint8_t kPitch = 88;
  const NoteBaseline kCommitted{kPitch, 100, 1488, 1966};
  const NoteBaseline kStub{kPitch, 100, 1488, 1774};
  const NoteBaseline kMoverPast{kPitch, 100, 1200, 1247};

  NoteEditCurrentState currentState;
  currentState.upsertRow(kOverlapId, kCommitted, kStub, NoteEditPresenceType::Visible);
  currentState.upsertRow(kMoverId, kMoverPast, kMoverPast, NoteEditPresenceType::Visible);

  NoteEditFocus focus;
  focus.active = true;
  focus.movingNoteId = kMoverId;
  focus.last = kMoverPast;

  TEST_ASSERT_TRUE(currentStateRowIsOverlapParticipant(*currentState.find(kOverlapId)));
  TEST_ASSERT_FALSE(overlapParticipationEndedWhileGeometryDiffers(currentState, kOverlapId));

  clearChangedOverlapParticipationWhenInteractionCleared(focus, currentState, kMoverPast,
                                                         kMoverId);
  const NoteEditCurrentNoteState* row = currentState.find(kOverlapId);
  TEST_ASSERT_NOT_NULL(row);
  TEST_ASSERT_EQUAL(static_cast<int>(NoteEditOverlapParticipationType::Ended),
                    static_cast<int>(row->overlapParticipation));
  TEST_ASSERT_FALSE(currentStateRowIsOverlapParticipant(*row));
  TEST_ASSERT_TRUE(currentStateRowGeometryDiffersFromCommitted(*row));
  TEST_ASSERT_TRUE(overlapParticipationEndedWhileGeometryDiffers(currentState, kOverlapId));
  TEST_ASSERT_EQUAL_UINT32(kStub.endTick, row->currentSpan.endTick);
}

void test_note_is_overlap_participant_prefers_current_state_over_latch() {
  constexpr NoteId kEndedId = 9;
  constexpr NoteId kActiveId = 10;
  const NoteBaseline kCommitted{88, 100, 1488, 1966};
  const NoteBaseline kStub{88, 100, 1488, 1774};

  NoteEditCurrentState currentState;
  currentState.upsertRow(kEndedId, kCommitted, kStub, NoteEditPresenceType::Visible);
  currentState.upsertRow(kActiveId, kCommitted, kStub, NoteEditPresenceType::Visible);
  currentState.markOverlapParticipationEnded(kEndedId);

  NoteEditFocus focus;
  focus.movingNoteId = 11;

  TEST_ASSERT_FALSE(noteIsOverlapParticipant(kEndedId, focus, &currentState));
  TEST_ASSERT_TRUE(noteIsOverlapParticipant(kActiveId, focus, &currentState));
  TEST_ASSERT_TRUE(hasOverlapParticipants(focus, &currentState));

  currentState.markOverlapParticipationEnded(kActiveId);
  TEST_ASSERT_FALSE(hasOverlapParticipants(focus, &currentState));
  NoteEditCurrentState emptyState;
  TEST_ASSERT_FALSE(noteIsOverlapParticipant(kEndedId, focus, &emptyState));
  TEST_ASSERT_FALSE(hasOverlapParticipants(focus, &emptyState));
}

void test_shorten_reactivates_overlap_participation_after_ended() {
  constexpr NoteId kOverlapId = 9;
  const NoteBaseline kCommitted{88, 100, 1488, 1966};
  const NoteBaseline kStub{88, 100, 1488, 1774};
  const NoteBaseline kShorter{88, 100, 1488, 1600};

  NoteEditCurrentState currentState;
  currentState.upsertRow(kOverlapId, kCommitted, kStub, NoteEditPresenceType::Visible);
  currentState.markOverlapParticipationEnded(kOverlapId);
  TEST_ASSERT_FALSE(currentStateRowIsOverlapParticipant(*currentState.find(kOverlapId)));

  EditSessionAction shorten{};
  shorten.type = EditSessionActionType::ShortenNote;
  shorten.targetNoteId = kOverlapId;
  shorten.pitch = kShorter.pitch;
  shorten.velocity = kShorter.velocity;
  shorten.startTick = kShorter.startTick;
  shorten.endTick = kShorter.endTick;
  currentState.applyEditSessionAction(shorten);

  const NoteEditCurrentNoteState* row = currentState.find(kOverlapId);
  TEST_ASSERT_NOT_NULL(row);
  TEST_ASSERT_EQUAL(static_cast<int>(NoteEditOverlapParticipationType::Active),
                    static_cast<int>(row->overlapParticipation));
  TEST_ASSERT_TRUE(currentStateRowIsOverlapParticipant(*row));
}

void test_overlap_closure_active_and_cleared() {
  ParticipatingNoteState hidden{};
  hidden.noteId = 17;
  hidden.visible = false;
  hidden.lifecycle = NoteEditLifecycleType::Existing;
  hidden.committedSpan = {88, 100, 1488, 1936};
  hidden.currentSpan = {88, 100, 1488, 1631};

  const NoteBaseline moverInside{88, 100, 1536, 1583};
  const NoteBaseline moverOutside{88, 100, 1940, 2000};

  TEST_ASSERT_TRUE(participatingNoteOverlapClosureActive(hidden, moverInside));
  TEST_ASSERT_FALSE(participatingNoteOverlapInteractionCleared(hidden, moverInside));
  TEST_ASSERT_FALSE(participatingNoteOverlapClosureActive(hidden, moverOutside));
  TEST_ASSERT_TRUE(participatingNoteOverlapInteractionCleared(hidden, moverOutside));
}

int main(int /*argc*/, char** /*argv*/) {
  UNITY_BEGIN();
  RUN_TEST(test_current_state_row_visibility_maps_from_presence);
  RUN_TEST(test_participating_note_is_visible_matches_current_state);
  RUN_TEST(test_row_is_visible_matches_row_projects_to_store);
  RUN_TEST(test_current_state_row_lifecycle_maps_from_presence);
  RUN_TEST(test_lifecycle_independent_from_visibility);
  RUN_TEST(test_row_lifecycle_matches_current_state);
  RUN_TEST(test_span_geometry_predicates_plan_table);
  RUN_TEST(test_current_state_row_geometry_predicates);
  RUN_TEST(test_participating_note_geometry_predicates_match_spans);
  RUN_TEST(test_hidden_participant_does_not_project);
  RUN_TEST(test_visible_shortened_tail_projects_when_visible);
  RUN_TEST(test_session_builds_from_current_state_and_selection);
  RUN_TEST(test_primary_driver_must_project);
  RUN_TEST(test_collect_overlap_participant_ids_from_current_state);
  RUN_TEST(test_participating_leave_restore_hidden_qualifies);
  RUN_TEST(test_participating_deleted_does_not_qualify_for_leave_restore_022849);
  RUN_TEST(test_overlap_participation_ended_after_sticky_clear);
  RUN_TEST(test_note_is_overlap_participant_prefers_current_state_over_latch);
  RUN_TEST(test_shorten_reactivates_overlap_participation_after_ended);
  RUN_TEST(test_participating_visible_shortened_does_not_qualify_for_leave_restore);
  RUN_TEST(test_participating_sealed_visible_shortened_qualifies_for_committed_leave_restore);
  RUN_TEST(test_participating_sealed_visible_shortened_qualifies_when_macro_sealed_flag_set);
  RUN_TEST(test_overlap_closure_active_and_cleared);
  return UNITY_END();
}
