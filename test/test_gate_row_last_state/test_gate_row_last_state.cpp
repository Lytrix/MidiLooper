// Copyright (c) 2026 Lytrix (Eelke Jager)
// Licensed under the PolyForm Noncommercial 1.0.0
//
// The gate-row dedupe tables live in `DMAMEM` (`.bss.dma`, NOLOAD): the linker emits no initial image for them and
// Teensy's startup clears only ordinary `.bss`, so a member initialiser is not boot-time behaviour. A stale non-null
// `exit` reached `strcmp` on the first row for a slot after boot and rebooted the device four times for four queued
// switches (`openspec/changes/windowed-playback-generation/evidence/2026-10-20-s37d-device-switch-crash`). These cases
// feed the helper the shape stale RAM has (non-zero, non-null) and pin that first use clears it, while a written entry
// keeps the values the dedupe compares.

#include <cstdint>
#include <cstring>

#include <unity.h>

#include "Utils/GateRowLastState.h"

namespace {

/// Stale `DMAMEM` as measured: arbitrary non-zero bytes, so both the magic and the null checks see "written".
template <typename State>
void fillWithStaleBytes(State& state, uint8_t byte) {
  std::memset(&state, byte, sizeof(state));
}

}  // namespace

void test_stale_launch_entry_is_reset_before_the_first_compare() {
  GateRowLastState::LaunchState stale;
  fillWithStaleBytes(stale, 0xA5);
  // The fault condition the device hit: a null check that the boot never made, seeing a non-null "pointer".
  TEST_ASSERT_NOT_NULL(stale.exit);

  TEST_ASSERT_TRUE(GateRowLastState::ensureInitialized(stale, GateRowLastState::kLaunchMagic));

  TEST_ASSERT_NULL(stale.exit);
  TEST_ASSERT_EQUAL_UINT32(GateRowLastState::kLaunchMagic, stale.magic);
  TEST_ASSERT_EQUAL_UINT32(0, stale.loopLengthTicks);
  TEST_ASSERT_EQUAL_UINT16(0, stale.flags);
  TEST_ASSERT_EQUAL_UINT8(0, stale.launchPreparation);
}

void test_written_launch_entry_survives_and_reports_no_reset() {
  GateRowLastState::LaunchState written;
  fillWithStaleBytes(written, 0x3C);
  TEST_ASSERT_TRUE(GateRowLastState::ensureInitialized(written, GateRowLastState::kLaunchMagic));
  written.exit = "launch_ready";
  written.loopLengthTicks = 24576;
  written.flags = 0x0A5Au;
  written.launchPreparation = 3;

  TEST_ASSERT_FALSE(GateRowLastState::ensureInitialized(written, GateRowLastState::kLaunchMagic));

  TEST_ASSERT_EQUAL_STRING("launch_ready", written.exit);
  TEST_ASSERT_EQUAL_UINT32(24576, written.loopLengthTicks);
  TEST_ASSERT_EQUAL_UINT16(0x0A5Au, written.flags);
  TEST_ASSERT_EQUAL_UINT8(3, written.launchPreparation);
}

void test_stale_rolling_entry_clears_seen_and_exit_together() {
  GateRowLastState::RollingState stale;
  fillWithStaleBytes(stale, 0x5A);
  // A stale non-zero `seen` with a coincidentally matching key is the masked half of the same defect. `seen` is
  // written as a `bool` (a raw 0x5A byte is an indeterminate value for the type, not a portable "true").
  stale.seen = true;
  TEST_ASSERT_TRUE(stale.seen);
  TEST_ASSERT_NOT_NULL(stale.exit);

  TEST_ASSERT_TRUE(GateRowLastState::ensureInitialized(stale, GateRowLastState::kRollingMagic));

  TEST_ASSERT_FALSE(stale.seen);
  TEST_ASSERT_NULL(stale.exit);
  TEST_ASSERT_EQUAL_UINT32(GateRowLastState::kRollingMagic, stale.magic);
  TEST_ASSERT_EQUAL_UINT16(0, stale.key);
  TEST_ASSERT_EQUAL_UINT8(0, stale.slot);
  TEST_ASSERT_EQUAL_UINT32(0, stale.loopLengthTicks);
  TEST_ASSERT_EQUAL_UINT8(0, stale.requestResult);
}

void test_written_rolling_entry_keeps_its_dedupe_key() {
  GateRowLastState::RollingState written;
  fillWithStaleBytes(written, 0x11);
  TEST_ASSERT_TRUE(GateRowLastState::ensureInitialized(written, GateRowLastState::kRollingMagic));
  written.key = 0x93FFu;
  written.slot = 3;
  written.loopLengthTicks = 24576;
  written.requestResult = 2;
  written.exit = "win_blocked_by_request";
  written.seen = true;

  TEST_ASSERT_FALSE(GateRowLastState::ensureInitialized(written, GateRowLastState::kRollingMagic));

  TEST_ASSERT_EQUAL_UINT16(0x93FFu, written.key);
  TEST_ASSERT_EQUAL_UINT8(3, written.slot);
  TEST_ASSERT_EQUAL_UINT32(24576, written.loopLengthTicks);
  TEST_ASSERT_EQUAL_UINT8(2, written.requestResult);
  TEST_ASSERT_EQUAL_STRING("win_blocked_by_request", written.exit);
  TEST_ASSERT_TRUE(written.seen);
}

void test_the_two_tables_never_accept_each_others_magic() {
  // One magic per table: an entry stamped by the other table's helper is still "never written" and is reset, so a
  // copy/paste of the wrong constant fails loudly here rather than by silently suppressing or duplicating rows.
  TEST_ASSERT_NOT_EQUAL(GateRowLastState::kLaunchMagic, GateRowLastState::kRollingMagic);

  GateRowLastState::LaunchState launch;
  fillWithStaleBytes(launch, 0x00);
  TEST_ASSERT_TRUE(GateRowLastState::ensureInitialized(launch, GateRowLastState::kRollingMagic));
  launch.exit = "launch_ready";
  TEST_ASSERT_TRUE(GateRowLastState::ensureInitialized(launch, GateRowLastState::kLaunchMagic));
  TEST_ASSERT_NULL(launch.exit);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_stale_launch_entry_is_reset_before_the_first_compare);
  RUN_TEST(test_written_launch_entry_survives_and_reports_no_reset);
  RUN_TEST(test_stale_rolling_entry_clears_seen_and_exit_together);
  RUN_TEST(test_written_rolling_entry_keeps_its_dedupe_key);
  RUN_TEST(test_the_two_tables_never_accept_each_others_magic);
  return UNITY_END();
}
