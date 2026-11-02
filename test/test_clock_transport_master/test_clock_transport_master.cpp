//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include <unity.h>
#include <cstdint>

#include "Utils/ClockTransportUtils.h"
#include "Utils/PlaybackDeadlineClock.h"
#include "Utils/PlaybackLaneMeasurement.h"

namespace {
constexpr int kInternal = 0;
constexpr int kExternal = 1;
constexpr uint32_t kTimeout = 500000;
}

void test_internal_clock_is_always_master() {
  TEST_ASSERT_TRUE(ClockTransportUtils::shouldEmitMidiTransportAsMaster(
      kInternal, 0, 1'000'000, kTimeout));
}

void test_external_clock_active_is_not_master() {
  const uint32_t now = 2'000'000;
  TEST_ASSERT_FALSE(ClockTransportUtils::shouldEmitMidiTransportAsMaster(
      kExternal, now - 1000, now, kTimeout));
}

void test_external_clock_timed_out_is_master() {
  const uint32_t now = 2'000'000;
  TEST_ASSERT_TRUE(ClockTransportUtils::shouldEmitMidiTransportAsMaster(
      kExternal, now - kTimeout - 1, now, kTimeout));
}

void test_deadline_clock_extends_wrap_without_recycling_epoch() {
  PlaybackDeadlineClockState clock;
  const uint32_t epoch = clock.stamp().epoch;
  TEST_ASSERT_NOT_EQUAL(0, epoch);
  clock.advance(UINT32_MAX);
  clock.advance(8);
  TEST_ASSERT_EQUAL_UINT64(uint64_t{UINT32_MAX} + 8, clock.stamp().tick);
  TEST_ASSERT_EQUAL_UINT32(epoch, clock.stamp().epoch);
  clock.advance(UINT32_MAX);
  TEST_ASSERT_EQUAL_UINT64(uint64_t{UINT32_MAX} * 2 + 8, clock.stamp().tick);
}

void test_deadline_clock_invalidates_same_forward_and_unobserved_resets() {
  PlaybackDeadlineClockState clock;
  clock.advance(100);
  const auto before = clock.stamp();
  clock.rebase(100);
  TEST_ASSERT_NOT_EQUAL(before.epoch, clock.stamp().epoch);
  auto epoch = clock.stamp().epoch;
  clock.rebase(9000);
  TEST_ASSERT_EQUAL_UINT64(9000, clock.stamp().tick);
  TEST_ASSERT_NOT_EQUAL(epoch, clock.stamp().epoch);
  epoch = clock.stamp().epoch;
  clock.rebase(0);
  clock.advance(10000);
  TEST_ASSERT_NOT_EQUAL(epoch, clock.stamp().epoch);
  epoch = clock.stamp().epoch;
  clock.invalidate();
  TEST_ASSERT_EQUAL_UINT64(10000, clock.stamp().tick);
  TEST_ASSERT_NOT_EQUAL(epoch, clock.stamp().epoch);
}

void test_lane_measurement_counts_idle_dispatches_without_inventing_pending_work() {
  PlaybackLaneMeasurement sample;
  sample.observe({0,1}, true, false, false, false, 0);
  sample.observe({8000,1}, true, false, false, false, 0);
  sample.observe({8008,1}, true, true, true, false, 25);
  TEST_ASSERT_EQUAL_UINT32(3, sample.opportunities);
  TEST_ASSERT_EQUAL_UINT32(8000, sample.maxDeltaTicks);
  TEST_ASSERT_EQUAL_UINT32(0, sample.eligiblePredecessorMax);
  sample.observe({8016,1}, true, true, false, true, 37);
  TEST_ASSERT_EQUAL_UINT32(8, sample.eligiblePredecessorMax);
  sample.observe({9016,1}, true, false, false, false, 0);
  TEST_ASSERT_EQUAL_UINT32(1000, sample.eligiblePredecessorMax);
  TEST_ASSERT_EQUAL_UINT32(2, sample.eligibleTurns);
  TEST_ASSERT_EQUAL_UINT32(1, sample.progressTurns);
  TEST_ASSERT_EQUAL_UINT32(1, sample.deniedTurns);
  TEST_ASSERT_EQUAL_UINT32(37, sample.maxWorkerUs);
  sample.clearBlock();
  sample.observe({9024,1}, true, false, false, false, 0);
  TEST_ASSERT_EQUAL_UINT32(6, sample.opportunities);
  TEST_ASSERT_EQUAL_UINT32(8, sample.maxDeltaTicks);
  TEST_ASSERT_EQUAL_UINT32(0, sample.eligiblePredecessorMax);
  TEST_ASSERT_EQUAL_UINT32(0, sample.maxWorkerUs);
}

// C5.3b (DEC-246 amendment 24): the spans of 16 / 128 consecutive running dispatches -- the ticks per owner turn the
// successor-lead measurement needs. A stall inside the run is charged to every span that covers it; a stop or a new
// epoch restarts the run, so no span crosses one; the block clear keeps the run.
void test_lane_measurement_spans_consecutive_running_dispatches() {
  PlaybackLaneMeasurement sample;
  uint64_t tick = 100;
  uint32_t us = 1000;
  for (int i = 0; i < 200; ++i) {
    tick += (i == 150) ? 90 : 1;  // one 90-tick stall at dispatch 150
    us += 50;
    sample.observe({tick, 1}, true, false, false, false, 0, 64, us);
  }
  TEST_ASSERT_EQUAL_UINT32(16 - 1 + 90, sample.span16MaxTicks);    // 15 one-tick turns plus the stall
  TEST_ASSERT_EQUAL_UINT32(128 - 1 + 90, sample.span128MaxTicks);
  TEST_ASSERT_EQUAL_UINT32(128 * 50, sample.span128MaxUs);
  // A stopped dispatch and a new epoch each restart the run: 128 more dispatches (127 intervals) never complete a 128
  // span (a span of N turns is N intervals, N + 1 dispatches).
  sample.clearBlock();
  sample.observe({tick + 1, 1}, false, false, false, false, 0, 64, us);
  for (int i = 0; i < 128; ++i) sample.observe({tick + 2000 + i, 2}, true, false, false, false, 0, 64, us + i);
  TEST_ASSERT_EQUAL_UINT32(0, sample.span128MaxTicks);
  TEST_ASSERT_EQUAL_UINT32(16, sample.span16MaxTicks);
  // One more makes the first span of the new run: no tick from before the stop is in it.
  sample.observe({tick + 2000 + 128, 2}, true, false, false, false, 0, 64, us + 128);
  TEST_ASSERT_EQUAL_UINT32(128, sample.span128MaxTicks);
}

void test_lane_measurement_discriminates_wrap_reset_stopped_and_saturation() {
  PlaybackLaneMeasurement sample;
  sample.observe({UINT32_MAX,1}, true, true, true, false, 1);
  sample.observe({uint64_t{UINT32_MAX}+8,1}, true, true, true, false, 1);
  TEST_ASSERT_EQUAL_UINT32(8, sample.maxDeltaTicks);
  sample.clearBlock();
  sample.observe({uint64_t{UINT32_MAX}+10000,2}, true, true, true, false, 1);
  TEST_ASSERT_EQUAL_UINT32(1, sample.discontinuities);
  TEST_ASSERT_EQUAL_UINT32(0, sample.maxDeltaTicks);
  sample.observe({0,2}, true, true, true, false, 1);
  TEST_ASSERT_EQUAL_UINT32(2, sample.discontinuities);
  sample.observe({10000,2}, false, false, false, false, 0);
  sample.observe({20000,2}, true, false, false, false, 0);
  TEST_ASSERT_EQUAL_UINT32(0, sample.maxDeltaTicks);
  sample.observe({uint64_t{UINT32_MAX}+20001,2}, true, false, false, false, 0);
  TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, sample.maxDeltaTicks);
  TEST_ASSERT_EQUAL_UINT32(1, sample.saturatedGaps);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_internal_clock_is_always_master);
  RUN_TEST(test_external_clock_active_is_not_master);
  RUN_TEST(test_external_clock_timed_out_is_master);
  RUN_TEST(test_deadline_clock_extends_wrap_without_recycling_epoch);
  RUN_TEST(test_deadline_clock_invalidates_same_forward_and_unobserved_resets);
  RUN_TEST(test_lane_measurement_counts_idle_dispatches_without_inventing_pending_work);
  RUN_TEST(test_lane_measurement_discriminates_wrap_reset_stopped_and_saturation);
  RUN_TEST(test_lane_measurement_spans_consecutive_running_dispatches);
  return UNITY_END();
}
