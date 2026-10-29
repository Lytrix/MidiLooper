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

// --- playbackSetSettled (DEC-196) ----------------------------------------------------------------------------
// The panel becomes interactive only when a first Play sounds every track: the focused slot is painted *and* no
// track is still owed its playback-slot restore.

void test_dec196_a_focus_settled_playback_set_releases() {
  TEST_ASSERT_TRUE(BootGate::playbackSetSettled(true, false));
}

void test_dec196_a_focus_settled_boot_with_tracks_still_owing_their_slot_does_not_release() {
  // The measured defect: Play at 11.67 s with one restored slot, seven tracks still owed
  // (`session_20261029_113133.log`). DEC-184's predicate released here; DEC-196 holds.
  TEST_ASSERT_FALSE(BootGate::playbackSetSettled(true, true));
}

void test_dec196_an_unsettled_focus_slot_never_releases_on_the_playback_set() {
  TEST_ASSERT_FALSE(BootGate::playbackSetSettled(false, false));
  TEST_ASSERT_FALSE(BootGate::playbackSetSettled(false, true));
}

void test_dec196_the_whole_queue_drain_still_releases_a_set_that_cannot_settle() {
  // An empty or unrecoverable playback set never settles; the original contract is the fallback, so no boot wedges.
  TEST_ASSERT_TRUE(BootGate::bootTitleMayRelease(true, BootGate::playbackSetSettled(false, true)));
}

// --- bootSweepMayRun (DEC-210) ---------------------------------------------------------------------------------
// The sweep may not re-dirty cache after cache while the playback set is still preparing its first paint.

void test_dec210_the_sweep_waits_for_the_playback_set_visual_caches() {
  TEST_ASSERT_FALSE_MESSAGE(BootGate::bootSweepMayRun(true, false, false),
                            "the focus cache alone no longer releases the sweep");
  TEST_ASSERT_TRUE_MESSAGE(BootGate::bootSweepMayRun(true, true, false),
                           "a prepared playback set releases the sweep");
}

void test_dec210_the_sweep_bound_releases_an_unpreparable_set() {
  TEST_ASSERT_TRUE(BootGate::bootSweepMayRun(false, false, true));
  TEST_ASSERT_TRUE(BootGate::bootSweepMayRun(true, false, true));
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
  RUN_TEST(test_dec196_a_focus_settled_playback_set_releases);
  RUN_TEST(test_dec196_a_focus_settled_boot_with_tracks_still_owing_their_slot_does_not_release);
  RUN_TEST(test_dec196_an_unsettled_focus_slot_never_releases_on_the_playback_set);
  RUN_TEST(test_dec196_the_whole_queue_drain_still_releases_a_set_that_cannot_settle);
  RUN_TEST(test_dec210_the_sweep_waits_for_the_playback_set_visual_caches);
  RUN_TEST(test_dec210_the_sweep_bound_releases_an_unpreparable_set);
  return UNITY_END();
}
