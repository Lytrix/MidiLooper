#include <unity.h>

#include "../../src/Utils/RuntimeTimingEnvelope.cpp"

void setUp() {
  RuntimeTimingEnvelope::resetForTest();
}

void tearDown() {}

void test_msi_gap_recorded_between_service_calls() {
  RuntimeTimingEnvelope::noteMidiServiceEnter(1000);
  RuntimeTimingEnvelope::noteMidiServiceExit(1200);
  RuntimeTimingEnvelope::noteMidiServiceEnter(5200);  // gap = 4000
  RuntimeTimingEnvelope::noteMidiServiceExit(5300);

  const auto snap = RuntimeTimingEnvelope::peek(5300);
  TEST_ASSERT_EQUAL_UINT32(4000, snap.msiMaxUs);
  TEST_ASSERT_EQUAL_UINT32(0, snap.msiOverCount);
  TEST_ASSERT_EQUAL_UINT32(200, snap.midisvcMaxUs);
}

void test_observational_over_count_uses_soft_ceiling() {
  RuntimeTimingEnvelope::noteMidiServiceEnter(0);
  RuntimeTimingEnvelope::noteMidiServiceExit(100);
  RuntimeTimingEnvelope::noteMidiServiceEnter(100 + 6000);  // gap 6000 > soft ceiling
  RuntimeTimingEnvelope::noteMidiServiceExit(100 + 6000 + 50);

  const auto snap = RuntimeTimingEnvelope::peek();
  TEST_ASSERT_EQUAL_UINT32(6000, snap.msiMaxUs);
  TEST_ASSERT_EQUAL_UINT32(1, snap.msiOverCount);
}

void test_clock_and_tracks_accumulate_independently() {
  RuntimeTimingEnvelope::noteClockDispatch(800);
  RuntimeTimingEnvelope::noteClockDispatch(1200);
  RuntimeTimingEnvelope::noteTracksUpdate(400);
  RuntimeTimingEnvelope::noteTracksUpdate(900);
  RuntimeTimingEnvelope::noteClockPulse();
  RuntimeTimingEnvelope::noteClockPulse();

  const auto snap = RuntimeTimingEnvelope::peek();
  TEST_ASSERT_EQUAL_UINT32(1200, snap.clkMaxUs);
  TEST_ASSERT_EQUAL_UINT32(900, snap.tracksMaxUs);
  TEST_ASSERT_EQUAL_UINT32(2, snap.clockPulses);
}

void test_maybe_emit_rate_limits_and_resets_window() {
  RuntimeTimingEnvelope::noteClockDispatch(9000);
  TEST_ASSERT_FALSE(RuntimeTimingEnvelope::maybeEmit(1000));  // first call arms window

  RuntimeTimingEnvelope::noteClockDispatch(9000);
  TEST_ASSERT_FALSE(
      RuntimeTimingEnvelope::maybeEmit(1000 + RuntimeTimingEnvelope::kEmitIntervalUs - 1));

  TEST_ASSERT_TRUE(RuntimeTimingEnvelope::maybeEmit(1000 + RuntimeTimingEnvelope::kEmitIntervalUs));

  const auto after = RuntimeTimingEnvelope::peek(1000 + RuntimeTimingEnvelope::kEmitIntervalUs);
  TEST_ASSERT_EQUAL_UINT32(0, after.clkMaxUs);
  TEST_ASSERT_EQUAL_UINT32(0, after.clkOverCount);
  TEST_ASSERT_EQUAL_UINT32(0, after.clockPulses);
}

void test_emit_interval_constant() {
  TEST_ASSERT_EQUAL_UINT32(5000000u, RuntimeTimingEnvelope::kEmitIntervalUs);
  TEST_ASSERT_EQUAL_UINT32(5000u, RuntimeTimingEnvelope::kObservationalSoftCeilingUs);
}

int main(int argc, char** argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_msi_gap_recorded_between_service_calls);
  RUN_TEST(test_observational_over_count_uses_soft_ceiling);
  RUN_TEST(test_clock_and_tracks_accumulate_independently);
  RUN_TEST(test_maybe_emit_rate_limits_and_resets_window);
  RUN_TEST(test_emit_interval_constant);
  return UNITY_END();
}
