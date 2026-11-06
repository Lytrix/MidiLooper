//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// fixed-memory-engine E9.8a: the engine's Clock. Internal tempo: one tick per period, from tick 0 at the first start
// and from the next bar line at a later one (tick numbers only rise), a long block caught up `kCatchUp` ticks a pass
// without a tick number skipped; stopped, nothing due. External MIDI clock: Start holds the next bar line until the
// first pulse, 8 ticks per pulse, ticks spread over the measured interval but never past
// the next pulse's tick before it arrives, a late pulse caught up in order, Stop / Continue, the tempo from the
// intervals; pulses without Start followed from the next tick; the source lost after 500 ms (internal, stopped).

#include <unity.h>

#include <vector>

#include "ContentEngine/EngineClock.h"

using namespace ContentEngine;

static EngineClock gC;

// Every tick due at `now` (passes of the main loop until none is due).
static std::vector<uint32_t> drain(uint32_t now) {
  std::vector<uint32_t> v;
  for (;;) {
    uint8_t count = 0;
    uint32_t t;
    bool any = false;
    while (gC.due(now, t, count)) {
      v.push_back(t);
      any = true;
    }
    if (!any) return v;
  }
}

void setUp() { gC.init(12000); }  // 120 BPM: 2,604 µs a tick
void tearDown() {}

void test_internal_ticks_at_the_tempo_from_zero() {
  TEST_ASSERT_EQUAL_UINT32(2604, gC.usPerTick());
  TEST_ASSERT_TRUE(drain(1000000).empty());  // stopped
  gC.start(1000000);
  TEST_ASSERT_TRUE((std::vector<uint32_t>{0}) == drain(1000000));
  TEST_ASSERT_TRUE(drain(1000000 + 2603).empty());
  TEST_ASSERT_TRUE((std::vector<uint32_t>{1}) == drain(1000000 + 2604));
  TEST_ASSERT_TRUE((std::vector<uint32_t>{2, 3}) == drain(1000000 + 3 * 2604));
  gC.stop();
  TEST_ASSERT_TRUE(drain(1000000 + 10 * 2604).empty());
  gC.start(2000000);  // a later start: the next bar line, not back to 0
  TEST_ASSERT_TRUE((std::vector<uint32_t>{768}) == drain(2000000));
}

void test_a_long_block_is_caught_up_in_order_a_pass_at_a_time() {
  gC.start(0);
  drain(0);
  uint8_t count = 0;
  uint32_t t, expect = 1;
  while (gC.due(100 * 2604, t, count)) TEST_ASSERT_EQUAL_UINT32(expect++, t);
  TEST_ASSERT_EQUAL_UINT8(EngineClock::kCatchUp, count);  // one pass: at most kCatchUp
  const std::vector<uint32_t> rest = drain(100 * 2604);
  for (uint32_t k : rest) TEST_ASSERT_EQUAL_UINT32(expect++, k);  // no tick number skipped
}

void test_external_start_pulses_and_interpolation() {
  const uint32_t pulse = 20833;  // 120 BPM: 24 pulses a quarter
  gC.midiStart(0);
  TEST_ASSERT_TRUE(drain(5000).empty());  // held at 0 until the first pulse
  gC.midiPulse(10000);
  TEST_ASSERT_TRUE((std::vector<uint32_t>{0}) == drain(10000));
  // No interval measured yet: the internal tempo spreads ticks, but never past tick 7.
  TEST_ASSERT_TRUE((std::vector<uint32_t>{1, 2, 3, 4, 5, 6, 7}) == drain(10000 + 500000 - 1));
  gC.midiPulse(10000 + pulse);  // pulse 1: tick 8 due at once
  TEST_ASSERT_TRUE((std::vector<uint32_t>{8}) == drain(10000 + pulse));
  TEST_ASSERT_EQUAL_UINT32(pulse / 8, gC.usPerTick());
  TEST_ASSERT_TRUE((std::vector<uint32_t>{9}) == drain(10000 + pulse + pulse / 8));
  TEST_ASSERT_TRUE(drain(10000 + pulse + pulse / 8 + 10).empty());
}

void test_a_late_pulse_catches_up_without_skipping() {
  gC.midiStart(0);
  gC.midiPulse(0);
  drain(0);
  gC.midiPulse(20000);
  drain(20000);
  // The next pulses arrive together (a USB burst): every tick up to 24 comes, in order.
  gC.midiPulse(21000);
  gC.midiPulse(21100);
  const std::vector<uint32_t> v = drain(21100);
  TEST_ASSERT_FALSE(v.empty());
  uint32_t expect = 9;
  for (uint32_t k : v) TEST_ASSERT_EQUAL_UINT32(expect++, k);
  TEST_ASSERT_TRUE(expect > 24);
}

void test_a_later_midi_start_moves_to_the_next_bar() {
  gC.midiStart(0);
  gC.midiPulse(0);
  drain(0);
  gC.midiPulse(20000);
  drain(20000);  // ticks 0..8
  gC.midiStop(30000);
  gC.midiStart(40000);
  TEST_ASSERT_TRUE(drain(45000).empty());  // held until the first pulse
  gC.midiPulse(50000);
  TEST_ASSERT_TRUE((std::vector<uint32_t>{768}) == drain(50000));
  gC.midiPulse(70000);
  const std::vector<uint32_t> v = drain(70000);
  TEST_ASSERT_EQUAL_UINT32(776, v.back());
}

void test_external_stop_continue_and_timeout() {
  gC.midiStart(0);
  gC.midiPulse(0);
  drain(0);
  gC.midiStop(1000);
  gC.midiPulse(20000);
  TEST_ASSERT_TRUE(drain(30000).empty());
  gC.midiContinue(40000);
  gC.midiPulse(40000);
  const std::vector<uint32_t> v = drain(40000);
  TEST_ASSERT_FALSE(v.empty());
  TEST_ASSERT_EQUAL_UINT32(1, v.front());  // continues where it stopped
  // Stopped, no pulse for 500 ms: back to the internal clock, stopped.
  gC.midiStop(50000);
  TEST_ASSERT_TRUE(drain(50000 + EngineClock::kExternalTimeoutUs + 1).empty());
  TEST_ASSERT_FALSE(gC.external());
  TEST_ASSERT_FALSE(gC.running());
}

// Owner 2026-11-06: the source's clock paused ~0.5 s mid-take and the clock stopped for good. As legacy: a running
// clock keeps going on the internal clock at the last external tempo, then follows the pulses again, no tick skipped;
// the gap does not move the tempo.
void test_a_gap_in_the_clock_keeps_running_and_resumes() {
  const uint32_t pulse = 25000;  // 100 BPM: not the internal default, so the gap runs at the measured tempo
  gC.midiStart(0);
  uint32_t t = 0, expect = 0;
  for (int i = 0; i < 48; ++i, t += pulse) {
    gC.midiPulse(t);
    for (uint32_t k : drain(t)) TEST_ASSERT_EQUAL_UINT32(expect++, k);
  }
  TEST_ASSERT_UINT32_WITHIN(2, pulse / 8, gC.usPerTick());
  // No pulse for 600 ms: still running, internally at the measured tempo, ticks in order.
  const uint32_t gapEnd = t + 600000;
  for (uint32_t u = t; u < gapEnd; u += 1000)
    for (uint32_t k : drain(u)) TEST_ASSERT_EQUAL_UINT32(expect++, k);
  TEST_ASSERT_TRUE(gC.running());
  TEST_ASSERT_FALSE(gC.external());
  TEST_ASSERT_UINT32_WITHIN(2, pulse / 8, gC.usPerTick());  // the internal clock at the external tempo
  // It waits for the source up to the timeout (never ahead of it: at most the next pulse's tick), then runs on:
  // ~30 ticks in the last 100 ms of the gap.
  TEST_ASSERT_TRUE(expect >= 48 * 8 + 25);
  // Pulses again: external, no tick skipped or repeated, the tempo unmoved.
  for (int i = 0; i < 24; ++i, t += pulse) {
    gC.midiPulse(gapEnd + i * pulse);
    for (uint32_t k : drain(gapEnd + i * pulse)) TEST_ASSERT_EQUAL_UINT32(expect++, k);
  }
  TEST_ASSERT_TRUE(gC.external());
  TEST_ASSERT_UINT32_WITHIN(2, pulse / 8, gC.usPerTick());
}

// Owner 2026-11-06 (Ableton over USB): pulses arrive bunched — several in one USB packet, read in one pass with one
// time (intervals of 0). The tempo (over the span of 24 intervals) holds; no tick is skipped; no timeout.
void test_bunched_pulses_keep_the_tempo() {
  const uint32_t pulse = 20833;
  gC.midiStart(0);
  uint32_t t = 0, expect = 0, sent = 0;
  for (int burst = 0; burst < 40; ++burst) {  // 4 pulses a packet, every 4 pulse periods
    t += 4 * pulse;
    for (int k = 0; k < 4; ++k, ++sent) gC.midiPulse(t);
    for (uint32_t u = t; u < t + 4 * pulse; u += 1000)
      for (uint32_t k : drain(u)) TEST_ASSERT_EQUAL_UINT32(expect++, k);
  }
  TEST_ASSERT_UINT32_WITHIN(30, pulse / 8, gC.usPerTick());
  TEST_ASSERT_TRUE(gC.external());
  TEST_ASSERT_TRUE(gC.running());
  TEST_ASSERT_UINT32_WITHIN(8, sent * 8, expect);  // every pulse's ticks played, in order
}

// A flood (a MIDI feedback loop in the host, owner 2026-11-06: measured 106 µs a tick, ~2,900 BPM): a beat's pulses
// within 20 ms. A span outside 1–300 BPM is not taken (legacy's range): the tempo stays a tempo.
void test_a_flood_of_pulses_does_not_set_an_absurd_tempo() {
  const uint32_t pulse = 20833;
  gC.midiStart(0);
  uint32_t t = 0;
  for (int i = 0; i < 30; ++i, t += pulse) gC.midiPulse(t), drain(t);
  for (int i = 0; i < 60; ++i, t += 800) {  // 24 pulses in 19 ms, sustained
    gC.midiPulse(t), drain(t);
    TEST_ASSERT_TRUE(gC.usPerTick() >= EngineClock::kMinUsPerTick);
  }
}

void test_a_lone_late_pulse_does_not_move_the_tempo_but_a_new_tempo_does() {
  const uint32_t pulse = 20833;
  gC.midiStart(0);
  uint32_t t = 0;
  for (int i = 0; i < 30; ++i, t += pulse) gC.midiPulse(t), drain(t);
  t += 200000;  // one late pulse (a 220 ms interval)
  gC.midiPulse(t), drain(t);
  TEST_ASSERT_UINT32_WITHIN(2, pulse / 8, gC.usPerTick());
  for (int i = 0; i < 30; ++i) t += 2 * pulse + 2000, gC.midiPulse(t), drain(t);  // half the tempo, sustained
  TEST_ASSERT_UINT32_WITHIN(5, (2 * pulse + 2000) / 8, gC.usPerTick());
}

void test_pulses_without_start_follow_from_the_next_tick() {
  gC.start(0);
  drain(3 * 2604);  // ticks 0..3 internal
  gC.midiPulse(3 * 2604 + 10);
  TEST_ASSERT_TRUE((std::vector<uint32_t>{4}) == drain(3 * 2604 + 10));  // this pulse is tick 4: not a pulse ahead
  gC.midiPulse(3 * 2604 + 10 + 20000);
  const std::vector<uint32_t> v = drain(3 * 2604 + 10 + 20000);
  TEST_ASSERT_FALSE(v.empty());
  uint32_t expect = 5;
  for (uint32_t k : v) TEST_ASSERT_EQUAL_UINT32(expect++, k);
  TEST_ASSERT_TRUE(gC.external());
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_internal_ticks_at_the_tempo_from_zero);
  RUN_TEST(test_a_long_block_is_caught_up_in_order_a_pass_at_a_time);
  RUN_TEST(test_external_start_pulses_and_interpolation);
  RUN_TEST(test_a_late_pulse_catches_up_without_skipping);
  RUN_TEST(test_a_later_midi_start_moves_to_the_next_bar);
  RUN_TEST(test_external_stop_continue_and_timeout);
  RUN_TEST(test_pulses_without_start_follow_from_the_next_tick);
  RUN_TEST(test_a_gap_in_the_clock_keeps_running_and_resumes);
  RUN_TEST(test_a_lone_late_pulse_does_not_move_the_tempo_but_a_new_tempo_does);
  RUN_TEST(test_bunched_pulses_keep_the_tempo);
  RUN_TEST(test_a_flood_of_pulses_does_not_set_an_absurd_tempo);
  return UNITY_END();
}
