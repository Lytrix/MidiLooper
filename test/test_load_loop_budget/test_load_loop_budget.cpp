//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include <unity.h>

#include "../../src/LoadLoopBudget.cpp"

#include "LoadLoopBudget.h"

void test_resolve_boot_title_budget() {
  TEST_ASSERT_EQUAL_UINT32(LoadLoopBudget::BootTitleRestoreUs,
                           LoadLoopBudget::resolveLoadLoopSliceBudgetUs(true, false, false));
  TEST_ASSERT_EQUAL_UINT32(LoadLoopBudget::BootTitleRestoreUs,
                           LoadLoopBudget::resolveLoadLoopSliceBudgetUs(true, true, true));
}

void test_resolve_focus_vs_background() {
  TEST_ASSERT_EQUAL_UINT32(LoadLoopBudget::FocusRestoreUs,
                           LoadLoopBudget::resolveLoadLoopSliceBudgetUs(false, true, false));
  TEST_ASSERT_EQUAL_UINT32(LoadLoopBudget::BackgroundRestoreUs,
                           LoadLoopBudget::resolveLoadLoopSliceBudgetUs(false, false, false));
}

void test_resolve_capture_suspends_background() {
  TEST_ASSERT_EQUAL_UINT32(0u, LoadLoopBudget::resolveLoadLoopSliceBudgetUs(false, false, true));
  TEST_ASSERT_EQUAL_UINT32(LoadLoopBudget::FocusRestoreUs,
                           LoadLoopBudget::resolveLoadLoopSliceBudgetUs(false, true, true));
}

void test_slice_budget_exhausted() {
  TEST_ASSERT_FALSE(LoadLoopBudget::loadLoopSliceBudgetExhausted(1200, 0));
  TEST_ASSERT_FALSE(LoadLoopBudget::loadLoopSliceBudgetExhausted(1200, 1199));
  TEST_ASSERT_TRUE(LoadLoopBudget::loadLoopSliceBudgetExhausted(1200, 1200));
  TEST_ASSERT_TRUE(LoadLoopBudget::loadLoopSliceBudgetExhausted(1200, 5000));
}

void test_preempt_finish_when_apply_pending_or_one_chunk() {
  TEST_ASSERT_TRUE(LoadLoopBudget::shouldFinishActiveBeforePreempt(100, 100));
  TEST_ASSERT_TRUE(LoadLoopBudget::shouldFinishActiveBeforePreempt(100, 100 + 500));
  TEST_ASSERT_TRUE(
      LoadLoopBudget::shouldFinishActiveBeforePreempt(0, LoadLoopBudget::ReadChunkBytes));
  TEST_ASSERT_FALSE(LoadLoopBudget::shouldFinishActiveBeforePreempt(
      0, LoadLoopBudget::ReadChunkBytes + 1));
}

int main(int argc, char** argv) {
  UNITY_BEGIN();
  RUN_TEST(test_resolve_boot_title_budget);
  RUN_TEST(test_resolve_focus_vs_background);
  RUN_TEST(test_resolve_capture_suspends_background);
  RUN_TEST(test_slice_budget_exhausted);
  RUN_TEST(test_preempt_finish_when_apply_pending_or_one_chunk);
  return UNITY_END();
}
