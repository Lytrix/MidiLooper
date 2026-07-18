//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include <unity.h>

#include "../../src/Logger.cpp"
#include "../../src/Utils/IntervalProjection.cpp"
#include "../../src/Utils/NoteUtils.cpp"
#include "Utils/BootLoopSlotRestore.h"
#include "Utils/LoopEditDepartGeometry.h"
#include "Utils/SlotFocusDisplay.h"
#include "Utils/SlotLaunchCommit.h"
#include "Utils/SlotLoopContent.h"
#include "ActiveNoteLedger.h"

void test_boot_restore_priority_selected_track_selected_slot() {
  // Focus selected (split from active) is audible — priority 0.
  const uint8_t active[] = {1, 0};
  const uint8_t selected[] = {2, 0};
  TEST_ASSERT_EQUAL_UINT8(0, computeBootRestorePriority(0, 2, 0, active, 2, selected, 2));
  TEST_ASSERT_TRUE(isAudibleBootSlot(0, 2, 0, active, 2, selected, 2));
}

void test_boot_restore_priority_selected_track_active_slot_when_split() {
  // Active slot is always audible (Play), even when selected differs.
  const uint8_t active[] = {1, 0};
  const uint8_t selected[] = {2, 0};
  TEST_ASSERT_EQUAL_UINT8(0, computeBootRestorePriority(0, 1, 0, active, 2, selected, 2));
  TEST_ASSERT_TRUE(isAudibleBootSlot(0, 1, 0, active, 2, selected, 2));
}

void test_boot_restore_priority_selected_track_other_slot() {
  const uint8_t active[] = {1, 0};
  const uint8_t selected[] = {2, 0};
  TEST_ASSERT_EQUAL_UINT8(2, computeBootRestorePriority(0, 3, 0, active, 2, selected, 2));
  TEST_ASSERT_FALSE(isAudibleBootSlot(0, 3, 0, active, 2, selected, 2));
}

void test_boot_restore_priority_other_track_active_slot() {
  // Other track active slot is audible (Play all tracks).
  const uint8_t active[] = {1, 4};
  const uint8_t selected[] = {2, 0};
  TEST_ASSERT_EQUAL_UINT8(0, computeBootRestorePriority(1, 4, 0, active, 2, selected, 2));
  TEST_ASSERT_TRUE(isAudibleBootSlot(1, 4, 0, active, 2, selected, 2));
}

void test_boot_restore_priority_other_track_selected_slot() {
  // Every track's selected slot is in the boot playback set (stopped remaps active:=selected).
  const uint8_t active[] = {1, 4};
  const uint8_t selected[] = {2, 0};
  TEST_ASSERT_EQUAL_UINT8(0, computeBootRestorePriority(1, 0, 0, active, 2, selected, 2));
  TEST_ASSERT_TRUE(isAudibleBootSlot(1, 0, 0, active, 2, selected, 2));
}

void test_boot_restore_priority_other_track_non_selected_slot() {
  const uint8_t active[] = {1, 0};
  const uint8_t selected[] = {2, 0};
  TEST_ASSERT_EQUAL_UINT8(2, computeBootRestorePriority(1, 4, 0, active, 2, selected, 2));
}

void test_slot_has_loop_content_ram_only() {
  TEST_ASSERT_TRUE(slotHasLoopContentInRamOrSd(true, false));
}

void test_slot_has_loop_content_sd_only() {
  TEST_ASSERT_TRUE(slotHasLoopContentInRamOrSd(false, true));
}

void test_slot_has_loop_content_neither() {
  TEST_ASSERT_FALSE(slotHasLoopContentInRamOrSd(false, false));
}

void test_preview_playhead_pending_while_playing() {
  TEST_ASSERT_TRUE(isPreviewPlayheadPending(2, 1, true));
}

void test_preview_playhead_not_pending_when_aligned() {
  TEST_ASSERT_FALSE(isPreviewPlayheadPending(1, 1, true));
}

void test_preview_playhead_not_pending_when_stopped() {
  TEST_ASSERT_FALSE(isPreviewPlayheadPending(2, 1, false));
}

void test_preview_playhead_flash_toggles() {
  TEST_ASSERT_NOT_EQUAL(previewPlayheadFlashVisible(0), previewPlayheadFlashVisible(250));
}

void test_loop_end_commit_uses_projection_wrap() {
  constexpr uint32_t loopLength = 1536;
  constexpr int32_t projectionCycleStartTick = 55210;
  constexpr uint32_t lastTickInLoop = 1535;
  const uint32_t wrapTick = static_cast<uint32_t>(projectionCycleStartTick) + loopLength;
  TEST_ASSERT_TRUE(shouldCommitLoopEndSwitch(wrapTick, projectionCycleStartTick, loopLength, 0, 0,
                                             lastTickInLoop));
}

void test_loop_end_commit_fallback_when_playback_index_uninitialized() {
  constexpr uint32_t loopLength = 3072;
  TEST_ASSERT_TRUE(shouldCommitLoopEndSwitch(loopLength, 0, loopLength, 0, 0, UINT32_MAX));
  TEST_ASSERT_FALSE(shouldCommitLoopEndSwitch(100, 0, loopLength, 0, 0, UINT32_MAX));
}

void test_loop_end_commit_uses_display_wrap_with_loop_start_offset() {
  constexpr uint32_t loopLength = 3072;
  constexpr uint32_t loopStartTick = 50;
  constexpr int32_t projectionCycleStartTick = 0;
  // Storage phase 3072 -> 0 is not a display wrap when loopStartTick = 50.
  TEST_ASSERT_FALSE(shouldCommitLoopEndSwitch(3072, projectionCycleStartTick, loopLength,
                                              loopStartTick, 0, 3071U));
  // Display wrap at storage phase 50 (display 3071 -> 0).
  TEST_ASSERT_TRUE(shouldCommitLoopEndSwitch(50, projectionCycleStartTick, loopLength,
                                             loopStartTick, 0, 49U));
}

void test_loop_end_commit_not_at_legacy_storage_phase_zero() {
  constexpr uint32_t loopLength = 1536;
  constexpr int32_t projectionCycleStartTick = 55210;
  constexpr uint32_t startLoopTick = 1;
  const uint32_t currentTick = static_cast<uint32_t>(projectionCycleStartTick) + 100U;
  const uint32_t legacyPhase =
      (currentTick >= startLoopTick) ? ((currentTick - startLoopTick) % loopLength)
                                     : ((currentTick + loopLength - startLoopTick) % loopLength);
  TEST_ASSERT_NOT_EQUAL(0U, legacyPhase);
  TEST_ASSERT_FALSE(shouldCommitLoopEndSwitch(currentTick, projectionCycleStartTick, loopLength, 0,
                                              startLoopTick, 99U));
}

void test_playhead_storage_phase_uses_projection_on_active_slot() {
  // session_20260713_221554: slot-1 launch at tick 26880 after commitQueuedPlaybackStart.
  constexpr uint32_t loopLength = 4608;
  constexpr uint32_t commitTick = 26880;
  constexpr int32_t projectionCycleStartTick = static_cast<int32_t>(commitTick);
  constexpr uint8_t slotIndex = 0;
  const uint32_t phase = resolvePlayheadStoragePhase(
      commitTick, projectionCycleStartTick, loopLength, slotIndex, slotIndex, true, commitTick, 0);
  TEST_ASSERT_EQUAL_UINT32(0, phase);
  const uint32_t legacyPhase = resolvePlayheadStoragePhase(
      commitTick, projectionCycleStartTick, loopLength, slotIndex, slotIndex, false, commitTick, 0);
  TEST_ASSERT_EQUAL_UINT32(3840, legacyPhase);
}

void test_active_note_ledger_orphan_off_is_inactive() {
  ActiveNoteLedger ledger;
  TEST_ASSERT_FALSE(ledger.isActive(3, 93));
  ledger.noteOn(3, 92, 100, 100);
  TEST_ASSERT_TRUE(ledger.isActive(3, 92));
  TEST_ASSERT_FALSE(ledger.isActive(3, 93));
  ledger.noteOff(3, 92);
  TEST_ASSERT_FALSE(ledger.isActive(3, 92));
}

void test_playhead_storage_phase_inactive_slot_uses_start_loop_tick() {
  constexpr uint32_t loopLength = 4608;
  constexpr uint32_t currentTick = 26880;
  constexpr int32_t projectionCycleStartTick = static_cast<int32_t>(currentTick);
  const uint32_t phase = resolvePlayheadStoragePhase(currentTick, projectionCycleStartTick,
                                                    loopLength, 0, 1, true, currentTick, 0);
  TEST_ASSERT_EQUAL_UINT32(3840, phase);
}

void test_may_not_write_loop_geometry_on_edit_depart_while_transport_active() {
  TEST_ASSERT_FALSE(mayWriteLoopGeometryOnEditDepart(true));
  TEST_ASSERT_TRUE(mayWriteLoopGeometryOnEditDepart(false));
}

void test_sync_loop_edit_baseline_from_transport_reanchor_zero_start() {
  uint32_t baselineStart = 3216;
  uint32_t baselineLength = 6144;
  syncLoopEditBaselineFromLiveGeometry(0, 6144, baselineStart, baselineLength);
  TEST_ASSERT_EQUAL_UINT32(0, baselineStart);
  TEST_ASSERT_EQUAL_UINT32(6144, baselineLength);
}

void test_loop_end_commit_after_transport_reanchor_uses_storage_wrap() {
  // After reanchorPlaybackProjection zeros loopStartTick, LoopEnd commits at length wrap.
  constexpr uint32_t loopLength = 6144;
  constexpr int32_t projectionCycleStartTick = 0;
  TEST_ASSERT_TRUE(shouldCommitLoopEndSwitch(loopLength, projectionCycleStartTick, loopLength, 0, 0,
                                             loopLength - 1U));
  TEST_ASSERT_FALSE(shouldCommitLoopEndSwitch(3216, projectionCycleStartTick, loopLength, 0, 0,
                                              3215U));
}

void test_mid_play_loop_start_restore_shifts_loop_end_commit_point() {
  // session_20260717_234742: reverting SD start=3216 mid-play moves LoopEnd wrap early.
  constexpr uint32_t loopLength = 6144;
  constexpr int32_t projectionCycleStartTick = 0;
  constexpr uint32_t restoredStart = 3216;
  TEST_ASSERT_TRUE(shouldCommitLoopEndSwitch(restoredStart, projectionCycleStartTick, loopLength,
                                             restoredStart, 0, restoredStart - 1U));
  TEST_ASSERT_FALSE(shouldCommitLoopEndSwitch(loopLength, projectionCycleStartTick, loopLength,
                                              restoredStart, 0, loopLength - 1U));
}

void test_ticks_remaining_until_loop_end_launch_counts_down() {
  constexpr uint32_t loopLength = 6144;  // 8 bars
  constexpr int32_t projectionCycleStartTick = 0;
  // Four bars remaining: phase = 6144 - 3072 = 3072.
  TEST_ASSERT_EQUAL_UINT32(
      3072U, ticksRemainingUntilLoopEndLaunch(3072, projectionCycleStartTick, loopLength, 0));
  // One bar remaining.
  TEST_ASSERT_EQUAL_UINT32(
      768U, ticksRemainingUntilLoopEndLaunch(5376, projectionCycleStartTick, loopLength, 0));
  TEST_ASSERT_EQUAL_UINT32(
      1U, ticksRemainingUntilLoopEndLaunch(loopLength - 1U, projectionCycleStartTick, loopLength,
                                           0));
}

void test_ticks_remaining_respects_loop_start_display_wrap() {
  constexpr uint32_t loopLength = 3072;
  constexpr uint32_t loopStartTick = 50;
  constexpr int32_t projectionCycleStartTick = 0;
  // One 16th (48 ticks) before display wrap at storage phase 50.
  TEST_ASSERT_EQUAL_UINT32(
      48U, ticksRemainingUntilLoopEndLaunch(50 - 48, projectionCycleStartTick, loopLength,
                                            loopStartTick));
}

int main(int /*argc*/, char** /*argv*/) {
  UNITY_BEGIN();
  RUN_TEST(test_boot_restore_priority_selected_track_selected_slot);
  RUN_TEST(test_boot_restore_priority_selected_track_active_slot_when_split);
  RUN_TEST(test_boot_restore_priority_selected_track_other_slot);
  RUN_TEST(test_boot_restore_priority_other_track_active_slot);
  RUN_TEST(test_boot_restore_priority_other_track_selected_slot);
  RUN_TEST(test_boot_restore_priority_other_track_non_selected_slot);
  RUN_TEST(test_slot_has_loop_content_ram_only);
  RUN_TEST(test_slot_has_loop_content_sd_only);
  RUN_TEST(test_slot_has_loop_content_neither);
  RUN_TEST(test_preview_playhead_pending_while_playing);
  RUN_TEST(test_preview_playhead_not_pending_when_aligned);
  RUN_TEST(test_preview_playhead_not_pending_when_stopped);
  RUN_TEST(test_preview_playhead_flash_toggles);
  RUN_TEST(test_loop_end_commit_uses_projection_wrap);
  RUN_TEST(test_loop_end_commit_fallback_when_playback_index_uninitialized);
  RUN_TEST(test_loop_end_commit_uses_display_wrap_with_loop_start_offset);
  RUN_TEST(test_loop_end_commit_not_at_legacy_storage_phase_zero);
  RUN_TEST(test_playhead_storage_phase_uses_projection_on_active_slot);
  RUN_TEST(test_active_note_ledger_orphan_off_is_inactive);
  RUN_TEST(test_playhead_storage_phase_inactive_slot_uses_start_loop_tick);
  RUN_TEST(test_may_not_write_loop_geometry_on_edit_depart_while_transport_active);
  RUN_TEST(test_sync_loop_edit_baseline_from_transport_reanchor_zero_start);
  RUN_TEST(test_loop_end_commit_after_transport_reanchor_uses_storage_wrap);
  RUN_TEST(test_mid_play_loop_start_restore_shifts_loop_end_commit_point);
  RUN_TEST(test_ticks_remaining_until_loop_end_launch_counts_down);
  RUN_TEST(test_ticks_remaining_respects_loop_start_display_wrap);
  return UNITY_END();
}
