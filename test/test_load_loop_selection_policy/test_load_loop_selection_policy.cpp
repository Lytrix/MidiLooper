//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include <unity.h>

#include "../../src/StorageManager/LoadLoopSelectionPolicy.cpp"

#include "LoadLoopSelectionPolicy.h"

using LoadLoopSelectionPolicy::EmptyIdleAction;
using LoadLoopSelectionPolicy::ParkedIdleAction;

void test_parked_focus_resumes() {
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<uint8_t>(ParkedIdleAction::ResumeParkedFocus),
      static_cast<uint8_t>(
          LoadLoopSelectionPolicy::resolveParkedIdleAction(true, false, false)));
}

void test_parked_low_begin_focus_while_playing() {
  // session_20260719_001237: Low parked under PLAYING must not starve focus High begin.
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<uint8_t>(ParkedIdleAction::BeginFocusFromQueue),
      static_cast<uint8_t>(
          LoadLoopSelectionPolicy::resolveParkedIdleAction(false, true, false)));
}

void test_parked_low_skip_while_playing_no_focus_queue() {
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<uint8_t>(ParkedIdleAction::SkipKeepParked),
      static_cast<uint8_t>(
          LoadLoopSelectionPolicy::resolveParkedIdleAction(false, false, false)));
}

void test_parked_low_promote_when_idle() {
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<uint8_t>(ParkedIdleAction::PromoteParkedLow),
      static_cast<uint8_t>(
          LoadLoopSelectionPolicy::resolveParkedIdleAction(false, false, true)));
}

void test_empty_idle_focus_high() {
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<uint8_t>(EmptyIdleAction::BeginFocusFromQueue),
      static_cast<uint8_t>(
          LoadLoopSelectionPolicy::resolveEmptyIdleAction(true, true, false)));
}

void test_empty_idle_background_when_allowed() {
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<uint8_t>(EmptyIdleAction::BeginBackgroundFromQueue),
      static_cast<uint8_t>(
          LoadLoopSelectionPolicy::resolveEmptyIdleAction(false, true, true)));
}

void test_empty_idle_skip_background_while_playing() {
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<uint8_t>(EmptyIdleAction::Skip),
      static_cast<uint8_t>(
          LoadLoopSelectionPolicy::resolveEmptyIdleAction(false, true, false)));
}

void test_non_focus_committing_skips_while_playing() {
  TEST_ASSERT_FALSE(LoadLoopSelectionPolicy::shouldStepLoadLoopJob(false, false));
}

void test_focus_committing_steps_while_playing() {
  TEST_ASSERT_TRUE(LoadLoopSelectionPolicy::shouldStepLoadLoopJob(true, false));
}

void test_non_focus_steps_when_background_allowed() {
  TEST_ASSERT_TRUE(LoadLoopSelectionPolicy::shouldStepLoadLoopJob(false, true));
}

int main(int argc, char** argv) {
  UNITY_BEGIN();
  RUN_TEST(test_parked_focus_resumes);
  RUN_TEST(test_parked_low_begin_focus_while_playing);
  RUN_TEST(test_parked_low_skip_while_playing_no_focus_queue);
  RUN_TEST(test_parked_low_promote_when_idle);
  RUN_TEST(test_empty_idle_focus_high);
  RUN_TEST(test_empty_idle_background_when_allowed);
  RUN_TEST(test_empty_idle_skip_background_while_playing);
  RUN_TEST(test_non_focus_committing_skips_while_playing);
  RUN_TEST(test_focus_committing_steps_while_playing);
  RUN_TEST(test_non_focus_steps_when_background_allowed);
  return UNITY_END();
}
