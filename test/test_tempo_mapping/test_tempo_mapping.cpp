//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// Host test for the loop-edit tempo fader mapping (runtime-working-set-ownership
// fixture support: the tempo fader lets a HITL pin the device BPM directly).
//
// TempoMapping is dependency-free and drives the production conversion used by
// LoopEditManager, so these cases fix the fader travel end to end.

#include <unity.h>

#include <cmath>

#include "Utils/TempoMapping.h"

namespace {

void assertBpmNear(float expected, float actual) {
	TEST_ASSERT_FLOAT_WITHIN(0.5f, expected, actual);
}

}  // namespace

void test_lower_travel_is_the_minimum_tempo() {
	assertBpmNear(72.0f, TempoMapping::tempoFromCC(0));
}

void test_upper_travel_is_the_maximum_tempo() {
	assertBpmNear(200.0f, TempoMapping::tempoFromCC(127));
}

void test_travel_is_monotonic_and_ordered() {
	float previous = TempoMapping::tempoFromCC(0);
	for (uint8_t cc = 1; cc <= 127; ++cc) {
		const float current = TempoMapping::tempoFromCC(cc);
		TEST_ASSERT_TRUE(current > previous);
		previous = current;
	}
}

void test_out_of_range_cc_clamps_to_the_maximum() {
	assertBpmNear(TempoMapping::TEMPO_MAX_BPM, TempoMapping::tempoFromCC(200));
	assertBpmNear(TempoMapping::TEMPO_MAX_BPM, TempoMapping::tempoFromCC(255));
}

void test_a_known_midpoint_matches_the_linear_law() {
	// 64/127 of the 72..200 span is 136.5 BPM; the fader is linear, not rounded to bars.
	assertBpmNear(72.0f + (64.0f / 127.0f) * (200.0f - 72.0f), TempoMapping::tempoFromCC(64));
}

void test_feedback_round_trips_through_the_cc() {
	for (uint8_t cc = 0; cc <= 127; ++cc) {
		const uint8_t roundTripped = TempoMapping::ccFromTempo(TempoMapping::tempoFromCC(cc));
		TEST_ASSERT_EQUAL_UINT8(cc, roundTripped);
	}
}

void test_feedback_clamps_out_of_range_tempos() {
	TEST_ASSERT_EQUAL_UINT8(0, TempoMapping::ccFromTempo(20.0f));
	TEST_ASSERT_EQUAL_UINT8(127, TempoMapping::ccFromTempo(400.0f));
}

void test_feedback_of_the_default_tempo_parks_the_motor() {
	// 120 BPM is the boot default and must land mid-travel, not at an end stop.
	const uint8_t cc = TempoMapping::ccFromTempo(120.0f);
	TEST_ASSERT_TRUE(cc > 0 && cc < 127);
	assertBpmNear(120.0f, TempoMapping::tempoFromCC(cc));
}

int main(int /*argc*/, char** /*argv*/) {
	UNITY_BEGIN();
	RUN_TEST(test_lower_travel_is_the_minimum_tempo);
	RUN_TEST(test_upper_travel_is_the_maximum_tempo);
	RUN_TEST(test_travel_is_monotonic_and_ordered);
	RUN_TEST(test_out_of_range_cc_clamps_to_the_maximum);
	RUN_TEST(test_a_known_midpoint_matches_the_linear_law);
	RUN_TEST(test_feedback_round_trips_through_the_cc);
	RUN_TEST(test_feedback_clamps_out_of_range_tempos);
	RUN_TEST(test_feedback_of_the_default_tempo_parks_the_motor);
	return UNITY_END();
}
