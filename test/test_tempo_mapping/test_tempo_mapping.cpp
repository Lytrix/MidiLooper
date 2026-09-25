//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// Host test for the loop-edit tempo fader mapping (DEC-086).
//
// TempoMapping is dependency-free and drives the production conversion used by
// LoopEditManager. One CC step is exactly one BPM, so the fader has a detent per
// BPM and the reachable tempos are exact integers.

#include <unity.h>

#include <cmath>

#include "Utils/TempoMapping.h"

namespace {

void assertBpmExact(float expected, float actual) {
	TEST_ASSERT_FLOAT_WITHIN(0.001f, expected, actual);
}

}  // namespace

void test_lower_travel_is_the_minimum_tempo() {
	assertBpmExact(60.0f, TempoMapping::tempoFromCC(0));
}

void test_upper_travel_is_the_maximum_tempo() {
	assertBpmExact(187.0f, TempoMapping::tempoFromCC(127));
}

void test_span_is_127_bpm_over_128_steps() {
	// 128 reachable values (CC 0..127) with exactly one BPM between adjacent steps.
	TEST_ASSERT_EQUAL_FLOAT(127.0f, TempoMapping::TEMPO_MAX_BPM - TempoMapping::TEMPO_MIN_BPM);
	TEST_ASSERT_EQUAL_UINT8(128, TempoMapping::TEMPO_STEPS);
	TEST_ASSERT_EQUAL_FLOAT(TempoMapping::TEMPO_MAX_BPM,
	                        TempoMapping::TEMPO_MIN_BPM + (TempoMapping::TEMPO_STEPS - 1));
}

void test_every_step_is_exactly_one_bpm() {
	for (uint8_t cc = 0; cc < 127; ++cc) {
		const float current = TempoMapping::tempoFromCC(cc);
		const float next = TempoMapping::tempoFromCC(static_cast<uint8_t>(cc + 1));
		assertBpmExact(1.0f, next - current);
	}
}

void test_round_tempos_are_exactly_reachable() {
	// 120 BPM and 119.9's neighbours are the practical cases for a HITL.
	assertBpmExact(120.0f, TempoMapping::tempoFromCC(60));
	assertBpmExact(119.0f, TempoMapping::tempoFromCC(59));
	assertBpmExact(121.0f, TempoMapping::tempoFromCC(61));
}

void test_out_of_range_cc_clamps_to_the_maximum() {
	assertBpmExact(TempoMapping::TEMPO_MAX_BPM, TempoMapping::tempoFromCC(200));
	assertBpmExact(TempoMapping::TEMPO_MAX_BPM, TempoMapping::tempoFromCC(255));
}

void test_feedback_round_trips_through_the_cc() {
	for (uint8_t cc = 0; cc <= 127; ++cc) {
		const uint8_t roundTripped = TempoMapping::ccFromTempo(TempoMapping::tempoFromCC(cc));
		TEST_ASSERT_EQUAL_UINT8(cc, roundTripped);
	}
}

void test_feedback_rounds_a_fractional_tempo_to_the_nearest_step() {
	// The saved-set tempo is 119.9 BPM; the motor parks at 120, one step away.
	TEST_ASSERT_EQUAL_UINT8(60, TempoMapping::ccFromTempo(119.9f));
	TEST_ASSERT_EQUAL_UINT8(60, TempoMapping::ccFromTempo(120.4f));
	TEST_ASSERT_EQUAL_UINT8(59, TempoMapping::ccFromTempo(119.4f));
}

void test_feedback_clamps_out_of_range_tempos() {
	TEST_ASSERT_EQUAL_UINT8(0, TempoMapping::ccFromTempo(20.0f));
	TEST_ASSERT_EQUAL_UINT8(127, TempoMapping::ccFromTempo(400.0f));
}

void test_feedback_of_the_default_tempo_parks_the_motor_mid_travel() {
	// 120 BPM is the boot default and must land mid-travel, not at an end stop.
	const uint8_t cc = TempoMapping::ccFromTempo(120.0f);
	TEST_ASSERT_TRUE(cc > 0 && cc < 127);
	assertBpmExact(120.0f, TempoMapping::tempoFromCC(cc));
}

int main(int /*argc*/, char** /*argv*/) {
	UNITY_BEGIN();
	RUN_TEST(test_lower_travel_is_the_minimum_tempo);
	RUN_TEST(test_upper_travel_is_the_maximum_tempo);
	RUN_TEST(test_span_is_127_bpm_over_128_steps);
	RUN_TEST(test_every_step_is_exactly_one_bpm);
	RUN_TEST(test_round_tempos_are_exactly_reachable);
	RUN_TEST(test_out_of_range_cc_clamps_to_the_maximum);
	RUN_TEST(test_feedback_round_trips_through_the_cc);
	RUN_TEST(test_feedback_rounds_a_fractional_tempo_to_the_nearest_step);
	RUN_TEST(test_feedback_clamps_out_of_range_tempos);
	RUN_TEST(test_feedback_of_the_default_tempo_parks_the_motor_mid_travel);
	return UNITY_END();
}
