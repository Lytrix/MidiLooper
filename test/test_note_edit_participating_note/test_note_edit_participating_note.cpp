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

int main(int /*argc*/, char** /*argv*/) {
  UNITY_BEGIN();
  RUN_TEST(test_participating_phase_maps_from_presence);
  RUN_TEST(test_hidden_participant_does_not_project);
  RUN_TEST(test_visible_shortened_tail_projects_when_visible);
  RUN_TEST(test_session_builds_from_current_state_and_selection);
  RUN_TEST(test_primary_driver_must_project);
  RUN_TEST(test_collect_overlap_participant_ids_from_current_state);
  return UNITY_END();
}
