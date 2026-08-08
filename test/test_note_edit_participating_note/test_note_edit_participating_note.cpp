//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include <unity.h>

#include <algorithm>

#include "../test_support/NoteEditFocusTestDeps.cpp"
#include "../../src/Logger.cpp"
#include "../../src/Utils/IntervalProjection.cpp"
#include "../../src/Utils/NoteUtils.cpp"

#include "ParticipatingNoteSession.h"

void test_participating_phase_maps_from_presence() {
  TEST_ASSERT_EQUAL(static_cast<int>(ParticipatingNotePhase::Hidden),
                    static_cast<int>(
                        participatingPhaseFromPresence(NoteEditPresenceType::Hidden)));
  TEST_ASSERT_EQUAL(static_cast<int>(ParticipatingNotePhase::Visible),
                    static_cast<int>(
                        participatingPhaseFromPresence(NoteEditPresenceType::Visible)));
}

void test_hidden_participant_does_not_project() {
  NoteEditCurrentNoteState row{};
  row.noteId = 17;
  row.committedSpan = {88, 100, 3216, 3743};
  row.currentSpan = {88, 100, 3216, 3263};
  row.presence = NoteEditPresenceType::Hidden;

  const ParticipatingNoteState state = buildParticipatingNoteState(row);
  TEST_ASSERT_EQUAL(static_cast<int>(ParticipatingNotePhase::Hidden),
                    static_cast<int>(state.phase));
  TEST_ASSERT_FALSE(state.projectsToStore);
  TEST_ASSERT_TRUE(state.shortenedVsCommitted);

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
  TEST_ASSERT_TRUE(state.projectsToStore);
  TEST_ASSERT_TRUE(state.shortenedVsCommitted);
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
  TEST_ASSERT_TRUE(session.participatingNotes.at(17).shortenedVsCommitted);
  TEST_ASSERT_TRUE(verifyParticipatingSessionInvariants(session).passed);
}

void test_primary_driver_must_project() {
  ParticipatingNoteSession session{};
  session.primaryNoteId = 17;
  ParticipatingNoteState hidden{};
  hidden.noteId = 17;
  hidden.phase = ParticipatingNotePhase::Hidden;
  hidden.projectsToStore = false;
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
  hidden.phase = ParticipatingNotePhase::Hidden;
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
  visibleShortened.phase = ParticipatingNotePhase::Visible;
  visibleShortened.committedSpan = {88, 100, 2016, 3078};
  visibleShortened.currentSpan = {88, 100, 2016, 2303};
  visibleShortened.shortenedVsCommitted = true;
  TEST_ASSERT_FALSE(participatingNoteQualifiesForLeaveRestoreTarget(visibleShortened, 13));
  TEST_ASSERT_FALSE(participatingNoteNeedsFullCommittedLeaveRestore(visibleShortened));
  const NoteBaseline storage{88, 100, 2016, 3078};
  TEST_ASSERT_FALSE(
      participatingNoteQualifiesForSealedVisibleShortenedLeaveRestore(visibleShortened, storage, 13));
}

void test_participating_sealed_visible_shortened_qualifies_for_committed_leave_restore() {
  ParticipatingNoteState sealedShortened{};
  sealedShortened.noteId = 9;
  sealedShortened.phase = ParticipatingNotePhase::Visible;
  sealedShortened.committedSpan = {88, 100, 1776, 2063};
  sealedShortened.currentSpan = {88, 100, 1776, 1823};
  sealedShortened.shortenedVsCommitted = true;
  const NoteBaseline storage{88, 100, 1776, 3078};
  TEST_ASSERT_TRUE(
      participatingNoteQualifiesForSealedVisibleShortenedLeaveRestore(sealedShortened, storage, 13));
}

void test_participating_sealed_visible_shortened_qualifies_when_macro_sealed_flag_set() {
  ParticipatingNoteState sealedShortened{};
  sealedShortened.noteId = 9;
  sealedShortened.phase = ParticipatingNotePhase::Visible;
  sealedShortened.committedSpan = {88, 100, 2016, 2255};
  sealedShortened.currentSpan = {88, 100, 2016, 2063};
  sealedShortened.shortenedVsCommitted = true;
  sealedShortened.visibleOverlapShortenSealed = true;
  const NoteBaseline storage{88, 100, 2016, 2255};
  TEST_ASSERT_TRUE(
      participatingNoteQualifiesForSealedVisibleShortenedLeaveRestore(sealedShortened, storage, 11));
}

void test_participating_deleted_does_not_qualify_for_leave_restore_022849() {
  // Stage 7.5.E2: sealed Deleted after deselect must not leave-restore.
  ParticipatingNoteState deleted{};
  deleted.noteId = 11;
  deleted.phase = ParticipatingNotePhase::Deleted;
  deleted.committedSpan = {88, 100, 1584, 1631};
  deleted.currentSpan = {88, 100, 1584, 1631};
  TEST_ASSERT_FALSE(participatingNoteQualifiesForLeaveRestoreTarget(deleted, 7));
  TEST_ASSERT_FALSE(participatingNoteNeedsFullCommittedLeaveRestore(deleted));
}

void test_overlap_closure_active_and_cleared() {
  ParticipatingNoteState hidden{};
  hidden.noteId = 17;
  hidden.phase = ParticipatingNotePhase::Hidden;
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
  RUN_TEST(test_participating_phase_maps_from_presence);
  RUN_TEST(test_hidden_participant_does_not_project);
  RUN_TEST(test_visible_shortened_tail_projects_when_visible);
  RUN_TEST(test_session_builds_from_current_state_and_selection);
  RUN_TEST(test_primary_driver_must_project);
  RUN_TEST(test_collect_overlap_participant_ids_from_current_state);
  RUN_TEST(test_participating_leave_restore_hidden_qualifies);
  RUN_TEST(test_participating_deleted_does_not_qualify_for_leave_restore_022849);
  RUN_TEST(test_participating_visible_shortened_does_not_qualify_for_leave_restore);
  RUN_TEST(test_participating_sealed_visible_shortened_qualifies_for_committed_leave_restore);
  RUN_TEST(test_participating_sealed_visible_shortened_qualifies_when_macro_sealed_flag_set);
  RUN_TEST(test_overlap_closure_active_and_cleared);
  return UNITY_END();
}
