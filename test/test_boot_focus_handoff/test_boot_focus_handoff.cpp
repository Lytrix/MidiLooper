//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// DEC-184 — **boot title release at focus commit**.
//
// Device evidence (`captures/session_20261028_214829_paintgate5.log`, one boot):
//
//   * 5.59 s  the selected slot's restore starts (`RRQ,7,3,...running`)
//   * 6.85 s  it commits (`RRQ,7,3,...terminal,none,committed`)
//   * 5.57-10.88 s  `DIAG,paint_gate` reports `drain=1, restore=1` with the restore queue draining 24 -> 0
//   * 10.88 s `boot_commit` (366 ms) — the title hold ends only now
//   * 10.89 s first `DISP`, 11.01 s first content
//
// The hold's condition was `StorageManager::bootInteractiveReady()` — the whole queue empty. The selected slot
// was ready 4 s earlier, so the panel waited for 23 restores it does not display. DEC-184 releases the hold when
// the **selected** slot is settled; the rest of the queue keeps restoring behind DEC-182's post-boot sweep gate.
//
// These fixtures pin the predicate, including the case the gate must not regress: a slot with no content never
// "settles", so a boot whose selected slot is empty keeps the original whole-queue contract.

#include <unity.h>

#include "BootGate.h"

// --- focusSlotSettled ---------------------------------------------------------------------------------------
// Settled = content present, nothing owed for that slot, and no load work mid-flight.

void test_dec184_a_committed_focus_slot_with_no_outstanding_work_is_settled() {
  TEST_ASSERT_TRUE(BootGate::focusSlotSettled(true, false, false, false));
}

void test_dec184_focus_restore_work_still_owed_is_not_settled() {
  TEST_ASSERT_FALSE(BootGate::focusSlotSettled(true, true, false, false));
}

void test_dec184_a_live_slot_load_session_is_not_settled() {
  TEST_ASSERT_FALSE(BootGate::focusSlotSettled(true, false, true, false));
}

void test_dec184_an_active_load_loop_job_is_not_settled() {
  TEST_ASSERT_FALSE(BootGate::focusSlotSettled(true, false, false, true));
}

void test_dec184_an_empty_focus_slot_never_settles() {
  // The whole-queue contract is the only release for a selected slot that has nothing to show.
  TEST_ASSERT_FALSE(BootGate::focusSlotSettled(false, false, false, false));
}

// --- bootTitleMayRelease ------------------------------------------------------------------------------------

void test_dec184_a_drained_queue_releases_the_title_as_before() {
  TEST_ASSERT_TRUE(BootGate::bootTitleMayRelease(true, false));
  TEST_ASSERT_TRUE(BootGate::bootTitleMayRelease(true, true));
}

void test_dec184_a_settled_focus_slot_releases_the_title_with_restores_still_queued() {
  // This is the DEC-184 change: the queue is *not* empty (wholeQueueReady == false) and the title is released.
  TEST_ASSERT_TRUE(BootGate::bootTitleMayRelease(false, true));
}

void test_dec184_a_settled_but_unready_boot_still_holds_the_title() {
  TEST_ASSERT_FALSE(BootGate::bootTitleMayRelease(false, false));
}

int main(int /*argc*/, char** /*argv*/) {
  UNITY_BEGIN();
  RUN_TEST(test_dec184_a_committed_focus_slot_with_no_outstanding_work_is_settled);
  RUN_TEST(test_dec184_focus_restore_work_still_owed_is_not_settled);
  RUN_TEST(test_dec184_a_live_slot_load_session_is_not_settled);
  RUN_TEST(test_dec184_an_active_load_loop_job_is_not_settled);
  RUN_TEST(test_dec184_an_empty_focus_slot_never_settles);
  RUN_TEST(test_dec184_a_drained_queue_releases_the_title_as_before);
  RUN_TEST(test_dec184_a_settled_focus_slot_releases_the_title_with_restores_still_queued);
  RUN_TEST(test_dec184_a_settled_but_unready_boot_still_holds_the_title);
  return UNITY_END();
}
