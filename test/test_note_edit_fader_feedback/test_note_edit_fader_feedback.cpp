//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include <unity.h>

#include "Utils/NoteEditLengthFaderMapping.h"
#include "Utils/NoteEditFaderOutboundPlan.h"
#include "MidiConfig.h"

void test_length_coarse_pitchbend_round_trip_at_loop_start() {
    const uint32_t loopLength = 1536;
    const uint32_t tick = 0;
    const int16_t pb =
        NoteEditLengthFaderMapping::loopTickToCoarsePitchbend(tick, loopLength);
    const uint32_t back =
        NoteEditLengthFaderMapping::coarsePitchbendToLoopTick(pb, loopLength);
    TEST_ASSERT_EQUAL_UINT32(tick, back);
}

void test_length_coarse_pitchbend_round_trip_at_loop_end() {
    const uint32_t loopLength = 1536;
    const uint32_t tick = loopLength - 1;
    const int16_t pb =
        NoteEditLengthFaderMapping::loopTickToCoarsePitchbend(tick, loopLength);
    const uint32_t back =
        NoteEditLengthFaderMapping::coarsePitchbendToLoopTick(pb, loopLength);
    TEST_ASSERT_EQUAL_UINT32(tick, back);
}

void test_length_coarse_pitchbend_round_trip_mid_loop() {
    const uint32_t loopLength = 1536;
    const uint32_t tick = 768;
    const int16_t pb =
        NoteEditLengthFaderMapping::loopTickToCoarsePitchbend(tick, loopLength);
    const uint32_t back =
        NoteEditLengthFaderMapping::coarsePitchbendToLoopTick(pb, loopLength);
    TEST_ASSERT_EQUAL_UINT32(tick, back);
}

void test_length_coarse_pitchbend_min_loop_length() {
    const int16_t pb = NoteEditLengthFaderMapping::loopTickToCoarsePitchbend(0, 1);
    TEST_ASSERT_EQUAL_INT16(MidiConfig::Pitchbend::CENTER, pb);
    TEST_ASSERT_EQUAL_UINT32(0, NoteEditLengthFaderMapping::coarsePitchbendToLoopTick(pb, 1));
}

void test_length_coarse_pitchbend_full_deflection_uses_logical_max() {
    const uint32_t loopLength = 1536;
    const int16_t pb =
        NoteEditLengthFaderMapping::loopTickToCoarsePitchbend(loopLength - 1, loopLength);
    TEST_ASSERT_EQUAL_INT16(MidiConfig::Pitchbend::MAX, pb);
    TEST_ASSERT_TRUE(MidiConfig::Pitchbend::isValidLogical(pb));
}

void test_fader_coarse_motor_trigger_note_matches_fader1() {
    TEST_ASSERT_EQUAL_UINT8(0, MidiConfig::Fader::MOTOR_TRIGGER_NOTE);
}

void test_fader_motor_trigger_notes_match_droid_notegate() {
    TEST_ASSERT_EQUAL_UINT8(0, MidiConfig::Fader::MOTOR_TRIGGER_NOTE);
    TEST_ASSERT_EQUAL_UINT8(1, MidiConfig::Fader::FINE_MOTOR_TRIGGER_NOTE);
    TEST_ASSERT_EQUAL_UINT8(2, MidiConfig::Fader::NOTE_VALUE_MOTOR_TRIGGER_NOTE);
}

void test_outbound_plan_session_open_includes_fader1_and_dependents() {
    const auto plan = NoteEditFaderOutbound::planForTrigger(NoteEditFaderOutbound::Trigger::SessionOpen);
    TEST_ASSERT_TRUE(plan.fader1);
    TEST_ASSERT_TRUE(plan.waitFader1Echo);
    TEST_ASSERT_TRUE(plan.coarse);
    TEST_ASSERT_TRUE(plan.fine);
    TEST_ASSERT_TRUE(plan.noteValue);
}

void test_outbound_plan_note_select_dependent_skips_fader1() {
    const auto plan =
        NoteEditFaderOutbound::planForTrigger(NoteEditFaderOutbound::Trigger::NoteSelectDependent);
    TEST_ASSERT_FALSE(plan.fader1);
    TEST_ASSERT_TRUE(plan.coarse);
    TEST_ASSERT_TRUE(plan.fine);
    TEST_ASSERT_TRUE(plan.noteValue);
}

void test_slot_change_triggers_selection_apply() {
    TEST_ASSERT_TRUE(NoteEditFaderOutbound::shouldApplySelectionOnSlotChange(2, 3));
    TEST_ASSERT_FALSE(NoteEditFaderOutbound::shouldApplySelectionOnSlotChange(3, 3));
    TEST_ASSERT_FALSE(NoteEditFaderOutbound::shouldApplySelectionOnSlotChange(3, -1));
}

void test_coalesce_dependent_while_channel15_active() {
    TEST_ASSERT_TRUE(NoteEditFaderOutbound::shouldCoalesceDependentRefresh(
        NoteEditFaderOutbound::Trigger::NoteSelectDependent,
        NoteEditFaderOutbound::Step::SendCoarse));
    TEST_ASSERT_FALSE(NoteEditFaderOutbound::shouldCoalesceDependentRefresh(
        NoteEditFaderOutbound::Trigger::LengthModeEnter,
        NoteEditFaderOutbound::Step::SendCoarse));
}

void test_user_quiet_after_400ms() {
    TEST_ASSERT_FALSE(NoteEditFaderOutbound::isUserQuiet(500, 0));
    TEST_ASSERT_FALSE(NoteEditFaderOutbound::isUserQuiet(500, 400));
    TEST_ASSERT_TRUE(NoteEditFaderOutbound::isUserQuiet(900, 400));
}

void test_outbound_pipeline_advances_through_triggers() {
    const auto plan =
        NoteEditFaderOutbound::planForTrigger(NoteEditFaderOutbound::Trigger::NoteSelectDependent);
    NoteEditFaderOutbound::Step step = NoteEditFaderOutbound::nextEnabledStep(
        NoteEditFaderOutbound::Step::Idle, plan);
    TEST_ASSERT_EQUAL(NoteEditFaderOutbound::Step::ArmMotorBank, step);
    step = NoteEditFaderOutbound::advanceOutboundStep(step, plan);
    TEST_ASSERT_EQUAL(NoteEditFaderOutbound::Step::SendCoarse, step);
    step = NoteEditFaderOutbound::advanceOutboundStep(step, plan);
    TEST_ASSERT_EQUAL(NoteEditFaderOutbound::Step::TriggerCoarse, step);
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    UNITY_BEGIN();
    RUN_TEST(test_length_coarse_pitchbend_round_trip_at_loop_start);
    RUN_TEST(test_length_coarse_pitchbend_round_trip_at_loop_end);
    RUN_TEST(test_length_coarse_pitchbend_round_trip_mid_loop);
    RUN_TEST(test_length_coarse_pitchbend_min_loop_length);
    RUN_TEST(test_length_coarse_pitchbend_full_deflection_uses_logical_max);
    RUN_TEST(test_fader_coarse_motor_trigger_note_matches_fader1);
    RUN_TEST(test_fader_motor_trigger_notes_match_droid_notegate);
    RUN_TEST(test_outbound_plan_session_open_includes_fader1_and_dependents);
    RUN_TEST(test_outbound_plan_note_select_dependent_skips_fader1);
    RUN_TEST(test_slot_change_triggers_selection_apply);
    RUN_TEST(test_coalesce_dependent_while_channel15_active);
    RUN_TEST(test_user_quiet_after_400ms);
    RUN_TEST(test_outbound_pipeline_advances_through_triggers);
    return UNITY_END();
}
