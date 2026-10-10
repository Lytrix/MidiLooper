//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// Host test for the loop-edit tempo fader mapping (DEC-086; tempo fader moved to the 14-bit pitchbend 2026-10-11,
// DEC-108 item 7 revised to the enforced 1–300 BPM range).
//
// TempoMapping is dependency-free and drives the production conversions used by LoopEditManager. The legacy CC
// mapping is one BPM per step over 60..187 (tests below keep it honest until the pitchbend fader replaces it); the
// pitchbend mapping spans the whole enforced clock range 1..300 BPM at a 0.1 BPM quantum, because an external MIDI
// clock can also only be estimated to 0.1.

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

// --- Pitchbend tempo fader: 1..300 BPM at a 0.1 quantum over the 14-bit range ---------------------------------

void test_pitchbend_travel_spans_the_whole_clock_range_at_a_tenth_of_a_bpm() {
	// Full lower travel is 1 BPM, full upper travel is 300 BPM: the whole enforced clock range, which the 7-bit CC
	// fader cannot span at 0.1 resolution.
	assertBpmExact(1.0f, TempoMapping::bpmFromPitchBendLogical(-8192));
	assertBpmExact(300.0f, TempoMapping::bpmFromPitchBendLogical(8191));
	assertBpmExact(1.0f, TempoMapping::bpmFromPitchBendWire(0));
	assertBpmExact(300.0f, TempoMapping::bpmFromPitchBendWire(TempoMapping::PITCHBEND_WIRE_MAX));
	TEST_ASSERT_EQUAL_FLOAT(0.1f, TempoMapping::BPM_STEP);
	TEST_ASSERT_EQUAL_UINT16(2991, TempoMapping::BPM_STEP_COUNT);
	// 1 + 2990 x 0.1 = 300, i.e. the quantum and the position count describe the same range.
	TEST_ASSERT_FLOAT_WITHIN(0.01f, 2991.0f,
	                         (TempoMapping::BPM_MAX - TempoMapping::BPM_MIN) / TempoMapping::BPM_STEP + 1.0f);
}

void test_every_pitchbend_value_resolves_on_a_tenth_and_never_goes_backwards() {
	// 16,384 wire values onto 2,991 positions: monotonic, each value exactly on a 0.1 step, and every position
	// reachable (5.5 wire values per position means no gaps).
	float previous = -1.0f;
	int positions = 0;
	for (uint32_t wire = 0; wire <= TempoMapping::PITCHBEND_WIRE_MAX; ++wire) {
		const float bpm = TempoMapping::bpmFromPitchBendWire(static_cast<uint16_t>(wire));
		TEST_ASSERT_TRUE(bpm >= previous);
		if (bpm > previous) {
			++positions;
		}
		const float stepped = (bpm - TempoMapping::BPM_MIN) / TempoMapping::BPM_STEP;
		TEST_ASSERT_FLOAT_WITHIN(0.001f, std::lround(stepped), stepped);
		previous = bpm;
	}
	TEST_ASSERT_EQUAL_INT(2991, positions);
}

void test_the_pitchbend_midpoint_is_the_mid_tempo() {
	assertBpmExact(150.5f, TempoMapping::bpmFromPitchBendWire(8192));
}

void test_motor_park_round_trips_within_half_a_step() {
	const float tempos[] = {1.0f, 60.0f, 119.9f, 120.0f, 120.5f, 187.0f, 299.9f, 300.0f};
	for (const float tempo : tempos) {
		const uint16_t wire = TempoMapping::pitchBendWireFromBpm(tempo);
		TEST_ASSERT_FLOAT_WITHIN(0.0501f, tempo, TempoMapping::bpmFromPitchBendWire(wire));
	}
	// A tempo already on the declared grid is recovered exactly.
	assertBpmExact(120.5f, TempoMapping::bpmFromPitchBendWire(TempoMapping::pitchBendWireFromBpm(120.5f)));
}

void test_out_of_range_tempos_clamp_to_the_range() {
	TEST_ASSERT_EQUAL_UINT16(0, TempoMapping::bpmStepFromBpm(-10.0f));
	TEST_ASSERT_EQUAL_UINT16(0, TempoMapping::bpmStepFromBpm(0.9f));
	TEST_ASSERT_EQUAL_UINT16(TempoMapping::BPM_STEP_COUNT - 1, TempoMapping::bpmStepFromBpm(500.0f));
	TEST_ASSERT_EQUAL_UINT16(0, TempoMapping::pitchBendWireFromBpm(0.5f));
	TEST_ASSERT_EQUAL_UINT16(TempoMapping::PITCHBEND_WIRE_MAX, TempoMapping::pitchBendWireFromBpm(500.0f));
	TEST_ASSERT_EQUAL_INT16(-8192, TempoMapping::pitchBendLogicalFromBpm(1.0f));
	TEST_ASSERT_EQUAL_INT16(8191, TempoMapping::pitchBendLogicalFromBpm(300.0f));
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
	RUN_TEST(test_pitchbend_travel_spans_the_whole_clock_range_at_a_tenth_of_a_bpm);
	RUN_TEST(test_every_pitchbend_value_resolves_on_a_tenth_and_never_goes_backwards);
	RUN_TEST(test_the_pitchbend_midpoint_is_the_mid_tempo);
	RUN_TEST(test_motor_park_round_trips_within_half_a_step);
	RUN_TEST(test_out_of_range_tempos_clamp_to_the_range);
	return UNITY_END();
}
