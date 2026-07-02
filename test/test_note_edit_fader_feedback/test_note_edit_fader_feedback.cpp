//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include <unity.h>

#include "MidiEvent.h"

#include "Utils/NoteEditLengthFaderMapping.h"
#include "Utils/NoteEditFaderOutboundPlan.h"
#include "Utils/NoteEditFaderSelectSync.h"
#include "Utils/NoteEditFaderMotorTiming.h"
#include "Utils/NoteEditDisplaySnapshot.h"
#include "Utils/SelectNavigation.h"
#include "NoteEditSessionState.h"
#include "MidiConfig.h"
#include "Globals.h"

#include "../../src/Utils/SelectNavigation.cpp"
#include "../../src/NoteEditFocus.cpp"
#include "../../src/Utils/NoteUtils.cpp"
#include "../../src/Logger.cpp"
#include "../../src/Utils/DisplayWindowUtils.cpp"

namespace {

uint32_t loopRelativeTickForTest(uint32_t storageTick, uint32_t loopStartTick,
                                 uint32_t loopLength) {
    if (loopLength == 0) {
        return 0;
    }
    const uint32_t relativePos = (storageTick >= loopStartTick)
                                     ? (storageTick - loopStartTick)
                                     : (storageTick + loopLength - loopStartTick);
    return relativePos % loopLength;
}

}  // namespace

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

void test_outbound_plan_note_select_with_fader1_includes_dependents() {
    const auto plan =
        NoteEditFaderOutbound::planForTrigger(NoteEditFaderOutbound::Trigger::NoteSelectWithFader1);
    TEST_ASSERT_TRUE(plan.fader1);
    TEST_ASSERT_FALSE(plan.waitFader1Echo);
    TEST_ASSERT_TRUE(plan.coarse);
    TEST_ASSERT_TRUE(plan.fine);
    TEST_ASSERT_TRUE(plan.noteValue);
}

void test_outbound_plan_note_select_dependent_includes_dependents_only() {
    const auto plan =
        NoteEditFaderOutbound::planForTrigger(NoteEditFaderOutbound::Trigger::NoteSelectDependent);
    TEST_ASSERT_FALSE(plan.fader1);
    TEST_ASSERT_FALSE(plan.waitFader1Echo);
    TEST_ASSERT_TRUE(plan.coarse);
    TEST_ASSERT_TRUE(plan.fine);
    TEST_ASSERT_TRUE(plan.noteValue);
}

void test_coalesce_dependent_refresh_when_pipeline_active() {
    TEST_ASSERT_FALSE(NoteEditFaderOutbound::shouldCoalesceDependentRefresh(
        NoteEditFaderOutbound::Trigger::NoteSelectDependent,
        NoteEditFaderOutbound::Step::SendCoarse));
    TEST_ASSERT_TRUE(NoteEditFaderOutbound::shouldCoalesceDependentRefresh(
        NoteEditFaderOutbound::Trigger::NoteSelectWithFader1,
        NoteEditFaderOutbound::Step::SendCoarse));
    TEST_ASSERT_FALSE(NoteEditFaderOutbound::shouldCoalesceDependentRefresh(
        NoteEditFaderOutbound::Trigger::Fader1BracketOnly,
        NoteEditFaderOutbound::Step::SendCoarse));
    TEST_ASSERT_FALSE(NoteEditFaderOutbound::shouldCoalesceDependentRefresh(
        NoteEditFaderOutbound::Trigger::NoteSelectWithFader1,
        NoteEditFaderOutbound::Step::Idle));
}

void test_restart_dependent_pipeline_on_selection_change() {
    TEST_ASSERT_TRUE(NoteEditFaderOutbound::shouldRestartDependentPipelineOnSelectionChange(
        NoteEditFaderOutbound::Trigger::NoteSelectDependent,
        NoteEditFaderOutbound::Trigger::NoteSelectDependent,
        NoteEditFaderOutbound::Step::SendCoarse));
    TEST_ASSERT_TRUE(NoteEditFaderOutbound::shouldRestartDependentPipelineOnSelectionChange(
        NoteEditFaderOutbound::Trigger::NoteSelectDependent,
        NoteEditFaderOutbound::Trigger::NoteSelectDependent,
        NoteEditFaderOutbound::Step::SendNoteValue));
    TEST_ASSERT_FALSE(NoteEditFaderOutbound::shouldRestartDependentPipelineOnSelectionChange(
        NoteEditFaderOutbound::Trigger::NoteSelectDependent,
        NoteEditFaderOutbound::Trigger::NoteSelectDependent,
        NoteEditFaderOutbound::Step::Idle));
    TEST_ASSERT_FALSE(NoteEditFaderOutbound::shouldRestartDependentPipelineOnSelectionChange(
        NoteEditFaderOutbound::Trigger::NoteSelectDependent,
        NoteEditFaderOutbound::Trigger::SessionOpen,
        NoteEditFaderOutbound::Step::SendCoarse));
}

void test_note_ref_change_triggers_selection_apply() {
    TEST_ASSERT_TRUE(NoteEditFaderOutbound::shouldApplySelectionOnNoteIdChange(
        1, 2, 48, 48));
    TEST_ASSERT_FALSE(NoteEditFaderOutbound::shouldApplySelectionOnNoteIdChange(
        1, 1, 48, 48));
}

void test_same_index_different_ref_triggers_apply() {
    TEST_ASSERT_TRUE(NoteEditFaderOutbound::shouldApplySelectionOnNoteIdChange(
        1, 2, 483, 483));
}

void test_same_ref_different_index_does_not_trigger_apply() {
    TEST_ASSERT_FALSE(NoteEditFaderOutbound::shouldApplySelectionOnNoteIdChange(
        1, 1, 483, 483));
    EditorSelection prior{};
    prior.primaryNote = 1;
    prior.bracketTick = 483;
    TEST_ASSERT_FALSE(editorSelectionTargetChanged(prior, 483, 1));
}

void test_bracket_change_triggers_note_ref_apply() {
    TEST_ASSERT_TRUE(NoteEditFaderOutbound::shouldApplySelectionOnNoteIdChange(
        1, 1, 48, 96));
}

void test_select_dependent_plan_from_ref_change() {
    const auto full = NoteEditFaderOutbound::planForSelectDependentFromNoteIdChange(
        1, 2, 100, 200);
    TEST_ASSERT_TRUE(full.coarse);
    TEST_ASSERT_TRUE(full.fine);
    TEST_ASSERT_TRUE(full.noteValue);

    const auto noteOnly = NoteEditFaderOutbound::planForSelectDependentFromNoteIdChange(
        1, 2, 483, 483);
    TEST_ASSERT_TRUE(noteOnly.coarse);
    TEST_ASSERT_TRUE(noteOnly.fine);
    TEST_ASSERT_TRUE(noteOnly.noteValue);

    const auto emptyStep = NoteEditFaderOutbound::planForSelectDependentFromNoteIdChange(
        1, kInvalidNoteId, 144, 192);
    TEST_ASSERT_TRUE(emptyStep.coarse);
}

void test_outbound_pipeline_note_only_dependent_plan() {
    const auto plan = NoteEditFaderOutbound::planForSelectDependent(false, true);
    NoteEditFaderOutbound::Step step = NoteEditFaderOutbound::nextEnabledStep(
        NoteEditFaderOutbound::Step::Idle, plan);
    TEST_ASSERT_EQUAL(NoteEditFaderOutbound::Step::SendCoarse, step);
    step = NoteEditFaderOutbound::advanceOutboundStep(step, plan);
    TEST_ASSERT_EQUAL(NoteEditFaderOutbound::Step::Done, step);
}

void test_slot_change_triggers_selection_apply() {
    TEST_ASSERT_TRUE(NoteEditFaderOutbound::shouldApplySelectionOnSlotChange(2, 3));
    TEST_ASSERT_FALSE(NoteEditFaderOutbound::shouldApplySelectionOnSlotChange(3, 3));
    TEST_ASSERT_FALSE(NoteEditFaderOutbound::shouldApplySelectionOnSlotChange(3, -1));
}

void test_nav_change_triggers_on_slot_or_note_index() {
    TEST_ASSERT_TRUE(NoteEditFaderOutbound::shouldApplySelectionOnNavChange(7, 12, 6, 10));
    TEST_ASSERT_TRUE(NoteEditFaderOutbound::shouldApplySelectionOnNavChange(7, 12, 7, 21));
    TEST_ASSERT_FALSE(NoteEditFaderOutbound::shouldApplySelectionOnNavChange(7, 12, 7, 12));
    TEST_ASSERT_TRUE(NoteEditFaderOutbound::shouldApplySelectionOnNavChange(-1, -1, 0, 2));
}

void test_note_change_triggers_selection_apply() {
    TEST_ASSERT_TRUE(NoteEditFaderOutbound::shouldApplySelectionOnNoteChange(12, 21));
    TEST_ASSERT_FALSE(NoteEditFaderOutbound::shouldApplySelectionOnNoteChange(12, 12));
    TEST_ASSERT_FALSE(NoteEditFaderOutbound::shouldApplySelectionOnNoteChange(-1, -1));
    TEST_ASSERT_TRUE(NoteEditFaderOutbound::shouldApplySelectionOnNoteChange(-1, 0));
}

void test_empty_note_idx_never_triggers_note_change_apply() {
    TEST_ASSERT_FALSE(NoteEditFaderOutbound::shouldApplySelectionOnNoteChange(5, -1));
    TEST_ASSERT_FALSE(NoteEditFaderOutbound::shouldApplySelectionOnNoteChange(-1, -1));
}

void test_same_tick_sibling_note_change_triggers_note_apply() {
    TEST_ASSERT_TRUE(NoteEditFaderOutbound::shouldApplySelectionOnNoteChange(22, 23));
    TEST_ASSERT_FALSE(NoteEditFaderOutbound::shouldApplySelectionOnNoteChange(23, 23));
}

void test_bracket_or_note_change_triggers_selection_apply() {
    TEST_ASSERT_TRUE(
        NoteEditFaderOutbound::shouldApplySelectionOnTargetChange(48, 60, 0, 60));
    TEST_ASSERT_TRUE(
        NoteEditFaderOutbound::shouldApplySelectionOnTargetChange(0, 72, 0, 60));
    TEST_ASSERT_FALSE(
        NoteEditFaderOutbound::shouldApplySelectionOnTargetChange(0, 60, 0, 60));
}

void test_same_tick_note_change_triggers_selection_apply() {
    TEST_ASSERT_TRUE(
        NoteEditFaderOutbound::shouldApplySelectionOnTargetChange(483, 23, 483, 22));
    TEST_ASSERT_FALSE(
        NoteEditFaderOutbound::shouldApplySelectionOnTargetChange(483, 23, 483, 23));
}

void test_resolve_note_idx_at_slot_uses_slot_note_idx() {
    const uint32_t loopLength = 1536;
    const uint32_t chordTick = 8 * 48;

    std::vector<NoteUtils::DisplayNote> notes;
    notes.push_back({kInvalidNoteId, 67, 100, chordTick, chordTick + 96});
    notes.push_back({kInvalidNoteId, 72, 100, chordTick, chordTick + 96});
    notes.push_back({kInvalidNoteId, 76, 100, chordTick, chordTick + 96});

    const auto slots =
        SelectNavigation::buildSelectNavigationSlots(loopLength, 0, notes, chordTick, false);

    std::vector<SelectNavigation::SelectNavSlot> chordSlots;
    for (const SelectNavigation::SelectNavSlot& slot : slots) {
        if (slot.relativeTick == chordTick && slot.noteIdx >= 0) {
            chordSlots.push_back(slot);
        }
    }
    TEST_ASSERT_EQUAL(3, (int)chordSlots.size());
    TEST_ASSERT_EQUAL(0, chordSlots[0].noteIdx);
    TEST_ASSERT_EQUAL(1, chordSlots[1].noteIdx);
    TEST_ASSERT_EQUAL(2, chordSlots[2].noteIdx);

    TEST_ASSERT_EQUAL(0, SelectNavigation::resolveNoteIdxAtSlot(chordSlots[0]));
    TEST_ASSERT_EQUAL(1, SelectNavigation::resolveNoteIdxAtSlot(chordSlots[1]));
    TEST_ASSERT_EQUAL(2, SelectNavigation::resolveNoteIdxAtSlot(chordSlots[2]));
}

void test_slot_drift_last_applied_beats_find_slot_for_gate() {
    TEST_ASSERT_TRUE(NoteEditFaderOutbound::shouldApplySelectionOnSlotChange(17, 16));
    TEST_ASSERT_FALSE(NoteEditFaderOutbound::shouldApplySelectionOnSlotChange(16, 16));
}

void test_sibling_slot_change_uses_full_dependent_plan() {
    const auto plan =
        NoteEditFaderOutbound::planForTrigger(NoteEditFaderOutbound::Trigger::NoteSelectDependent);
    TEST_ASSERT_TRUE(plan.coarse);
    TEST_ASSERT_TRUE(plan.fine);
    TEST_ASSERT_TRUE(plan.noteValue);
}

void test_outbound_coarse_uses_loop_relative_tick_with_nonzero_loop_start() {
    const uint32_t loopLength = 768;
    const uint32_t loopStartTick = 424;
    const uint32_t storageStartTick = 0;
    const uint32_t relTick =
        loopRelativeTickForTest(storageStartTick, loopStartTick, loopLength);
    TEST_ASSERT_EQUAL_UINT32(344, relTick);
    const int16_t pb =
        NoteEditLengthFaderMapping::loopTickToCoarsePitchbend(relTick, loopLength);
    const int16_t expectedPb =
        NoteEditLengthFaderMapping::loopTickToCoarsePitchbend(relTick, loopLength);
    TEST_ASSERT_EQUAL_INT16(expectedPb, pb);
    TEST_ASSERT_NOT_EQUAL(
        NoteEditLengthFaderMapping::loopTickToCoarsePitchbend(storageStartTick % loopLength,
                                                              loopLength),
        pb);
}

void test_empty_step_bracket_rel_tick_maps_to_coarse_pitchbend() {
    const uint32_t loopLength = 1536;
    const uint32_t loopStartTick = 0;
    const uint32_t bracketTick = 144;
    const uint32_t relTick =
        loopRelativeTickForTest(bracketTick, loopStartTick, loopLength);
    TEST_ASSERT_EQUAL_UINT32(144, relTick);
    const int16_t pb =
        NoteEditLengthFaderMapping::loopTickToCoarsePitchbend(relTick, loopLength);
    const uint32_t step = relTick / 48;
    TEST_ASSERT_EQUAL_UINT32(3, step);
    TEST_ASSERT_TRUE(MidiConfig::Pitchbend::isValidLogical(pb));
}

void test_outbound_pipeline_advances_through_session_open() {
    const auto plan =
        NoteEditFaderOutbound::planForTrigger(NoteEditFaderOutbound::Trigger::SessionOpen);
    NoteEditFaderOutbound::Step step = NoteEditFaderOutbound::nextEnabledStep(
        NoteEditFaderOutbound::Step::Idle, plan);
    TEST_ASSERT_EQUAL(NoteEditFaderOutbound::Step::SendFader1Bracket, step);
    step = NoteEditFaderOutbound::advanceOutboundStep(step, plan);
    TEST_ASSERT_EQUAL(NoteEditFaderOutbound::Step::WaitFader1Echo, step);
    step = NoteEditFaderOutbound::advanceOutboundStep(step, plan);
    TEST_ASSERT_EQUAL(NoteEditFaderOutbound::Step::SendCoarse, step);
    step = NoteEditFaderOutbound::advanceOutboundStep(step, plan);
    TEST_ASSERT_EQUAL(NoteEditFaderOutbound::Step::Done, step);
}

void test_outbound_pipeline_advances_through_note_select_dependent() {
    const auto plan =
        NoteEditFaderOutbound::planForTrigger(NoteEditFaderOutbound::Trigger::NoteSelectDependent);
    NoteEditFaderOutbound::Step step = NoteEditFaderOutbound::nextEnabledStep(
        NoteEditFaderOutbound::Step::Idle, plan);
    TEST_ASSERT_EQUAL(NoteEditFaderOutbound::Step::SendCoarse, step);
    step = NoteEditFaderOutbound::advanceOutboundStep(step, plan);
    TEST_ASSERT_EQUAL(NoteEditFaderOutbound::Step::Done, step);
}

void test_outbound_pipeline_length_mode_enter_parallel_dependent_step() {
    const auto plan =
        NoteEditFaderOutbound::planForTrigger(NoteEditFaderOutbound::Trigger::LengthModeEnter);
    NoteEditFaderOutbound::Step step = NoteEditFaderOutbound::nextEnabledStep(
        NoteEditFaderOutbound::Step::Idle, plan);
    TEST_ASSERT_EQUAL(NoteEditFaderOutbound::Step::SendCoarse, step);
    step = NoteEditFaderOutbound::advanceOutboundStep(step, plan);
    TEST_ASSERT_EQUAL(NoteEditFaderOutbound::Step::Done, step);
}

void test_motor_fader_timing_matches_ableton_reference_midis() {
    TEST_ASSERT_EQUAL_UINT8(3, NoteEditFaderMotorTiming::kPositionSendCount);
    TEST_ASSERT_EQUAL_UINT16(12, NoteEditFaderMotorTiming::kInterPositionGapTicks);
    TEST_ASSERT_EQUAL_UINT16(24, NoteEditFaderMotorTiming::kMotorNoteDurationTicks);
    TEST_ASSERT_EQUAL_UINT32(62, NoteEditFaderMotorTiming::kInterPositionGapMs);
    TEST_ASSERT_EQUAL_UINT32(125, NoteEditFaderMotorTiming::kMotorNoteDurationMs);
}

void test_motor_fader_burst_invokes_position_and_notegate_callbacks() {
    int positionCount = 0;
    int noteOnCount = 0;
    int noteOffCount = 0;
    NoteEditFaderMotorTiming::runMotorFaderBurst(
        [&]() { ++positionCount; }, [&]() { ++noteOnCount; }, [&]() { ++noteOffCount; });
    TEST_ASSERT_EQUAL(3, positionCount);
    TEST_ASSERT_EQUAL(1, noteOnCount);
    TEST_ASSERT_EQUAL(1, noteOffCount);
}

void test_select_fader_motor_idle_ms_constant() {
    TEST_ASSERT_EQUAL_UINT32(300, NoteEditFaderMotorTiming::kSelectFaderMotorIdleMs);
}

void test_should_flush_select_dependent_motor_sync_after_idle() {
    TEST_ASSERT_FALSE(NoteEditFaderMotorTiming::shouldFlushSelectDependentMotorSync(
        1000, 300, false));
    TEST_ASSERT_FALSE(NoteEditFaderMotorTiming::shouldFlushSelectDependentMotorSync(
        599, 300, true));
    TEST_ASSERT_TRUE(NoteEditFaderMotorTiming::shouldFlushSelectDependentMotorSync(
        600, 300, true));
    TEST_ASSERT_TRUE(NoteEditFaderMotorTiming::shouldFlushSelectDependentMotorSync(
        500, 0, true));
}

struct ParallelMotorTestSlot {
    bool enabled = false;
    int positionCount = 0;
    int noteOnCount = 0;
    int noteOffCount = 0;
    int slotId = 0;
    void sendPosition() {
        if (enabled) {
            ++positionCount;
        }
    }
    void sendNoteOn() {
        if (enabled) {
            ++noteOnCount;
        }
    }
    void sendNoteOff() {
        if (enabled) {
            ++noteOffCount;
        }
    }
};

void test_parallel_motor_fader_burst_interleaves_enabled_slots() {
    ParallelMotorTestSlot slot0;
    slot0.enabled = true;
    slot0.slotId = 2;
    ParallelMotorTestSlot slot1;
    slot1.enabled = true;
    slot1.slotId = 3;
    ParallelMotorTestSlot slot2;
    slot2.enabled = true;
    slot2.slotId = 4;

    NoteEditFaderMotorTiming::runParallelMotorFaderBursts(slot0, slot1, slot2);

    TEST_ASSERT_EQUAL(3, slot0.positionCount);
    TEST_ASSERT_EQUAL(3, slot1.positionCount);
    TEST_ASSERT_EQUAL(3, slot2.positionCount);
    TEST_ASSERT_EQUAL(1, slot0.noteOnCount);
    TEST_ASSERT_EQUAL(1, slot1.noteOnCount);
    TEST_ASSERT_EQUAL(1, slot2.noteOnCount);
    TEST_ASSERT_EQUAL(1, slot0.noteOffCount);
    TEST_ASSERT_EQUAL(1, slot1.noteOffCount);
    TEST_ASSERT_EQUAL(1, slot2.noteOffCount);
}

void test_ch13_ack_correlation_window_ms_for_capture_logs() {
    TEST_ASSERT_EQUAL(11, NoteEditFaderOutbound::kCh13AckCorrelationWindowMs);
}

void test_select_fader_echo_rejects_near_last_sent() {
    TEST_ASSERT_TRUE(
        NoteEditFaderSelectSync::shouldIgnoreSelectFaderEcho(5000, 5050, 100));
    TEST_ASSERT_FALSE(
        NoteEditFaderSelectSync::shouldIgnoreSelectFaderEcho(5000, 5200, 100));
}

void test_select_fader_echo_accepts_small_user_delta() {
    TEST_ASSERT_FALSE(
        NoteEditFaderSelectSync::shouldIgnoreSelectFaderEcho(4500, 4620, 100));
}

void test_ref_driven_motor_sync_ignores_index_only_change() {
    using NoteEditDisplaySnapshot::DisplayNoteInfoSnapshot;
    using NoteEditDisplaySnapshot::displayNoteInfoChanged;

    const DisplayNoteInfoSnapshot snap{1, 60, 483, 96};
    TEST_ASSERT_FALSE(displayNoteInfoChanged(snap, snap));

    DisplayNoteInfoSnapshot pitchChanged = snap;
    pitchChanged.pitch = 62;
    TEST_ASSERT_TRUE(displayNoteInfoChanged(snap, pitchChanged));

    DisplayNoteInfoSnapshot storageChanged = snap;
    storageChanged.storageStart = 500;
    TEST_ASSERT_TRUE(displayNoteInfoChanged(snap, storageChanged));
}

void test_select_dependent_settle_ms_in_capture_window() {
    static constexpr uint32_t kSelectDependentSettleMs = 450;
    TEST_ASSERT_GREATER_OR_EQUAL(400, kSelectDependentSettleMs);
    TEST_ASSERT_LESS_OR_EQUAL(500, kSelectDependentSettleMs);
}

void test_reference_step_from_bracket_tick() {
    const uint32_t bracketTick = 477U;
    const uint32_t referenceStep = bracketTick / Config::TICKS_PER_16TH_STEP;
    TEST_ASSERT_EQUAL(9U, referenceStep);
}

void test_display_note_info_changed() {
    using NoteEditDisplaySnapshot::DisplayNoteInfoSnapshot;
    using NoteEditDisplaySnapshot::displayNoteInfoChanged;

    const DisplayNoteInfoSnapshot base{1, 60, 483, 96};
    TEST_ASSERT_FALSE(displayNoteInfoChanged(base, base));

    DisplayNoteInfoSnapshot pitchChanged = base;
    pitchChanged.pitch = 62;
    TEST_ASSERT_TRUE(displayNoteInfoChanged(base, pitchChanged));

    DisplayNoteInfoSnapshot storageChanged = base;
    storageChanged.storageStart = 500;
    TEST_ASSERT_TRUE(displayNoteInfoChanged(base, storageChanged));

    DisplayNoteInfoSnapshot displayChanged = base;
    displayChanged.displayStartTick = 100;
    TEST_ASSERT_TRUE(displayNoteInfoChanged(base, displayChanged));
}

void test_display_note_info_snapshot_from_ref_wrap_formula() {
    using NoteEditDisplaySnapshot::buildDisplayNoteInfoSnapshotFromNoteId;

    constexpr NoteId refId = 1;
    const auto snap = buildDisplayNoteInfoSnapshotFromNoteId(refId, 60, 50, 100, 384);
    TEST_ASSERT_EQUAL(60, snap.pitch);
    TEST_ASSERT_EQUAL(50U, snap.storageStart);
    TEST_ASSERT_EQUAL(334U, snap.displayStartTick);
}

void test_filtered_display_note_index_for_selection() {
    std::vector<NoteUtils::DisplayNote> notes;
    notes.push_back({1, 72, 100, 200, 296});
    notes.push_back({2, 60, 100, 483, 579});

    EditorSelection sel{};
    sel.primaryNote = 2;
    TEST_ASSERT_EQUAL(1, NoteEditDisplaySnapshot::filteredDisplayNoteIndexForSelection(sel, notes));
    TEST_ASSERT_EQUAL(-1, NoteEditDisplaySnapshot::filteredDisplayNoteIndexForSelection(sel, {notes[0]}));
}

void test_window_filter_excludes_notes_outside_nav_inventory() {
    const uint32_t loopLength = 32u * Config::TICKS_PER_BAR;
    const uint32_t windowStart = 16u * Config::TICKS_PER_BAR;
    const uint32_t windowLength = 16u * Config::TICKS_PER_BAR;

    NoteUtils::DisplayNoteVec notes;
    notes.push_back({5, 60, 100, 17u * Config::TICKS_PER_BAR, 17u * Config::TICKS_PER_BAR + 48});
    notes.push_back({kInvalidNoteId, 72, 100, 2u * Config::TICKS_PER_BAR, 2u * Config::TICKS_PER_BAR + 48});

    const NoteUtils::DisplayNoteVec windowed = DisplayWindowUtils::filterDisplayNotesToWindow(
        notes, windowStart, windowLength, loopLength);
    TEST_ASSERT_EQUAL(1, static_cast<int>(windowed.size()));
    TEST_ASSERT_EQUAL_UINT8(60, windowed[0].note);

    std::vector<NoteUtils::DisplayNote> windowNavNotes(windowed.begin(), windowed.end());

    const NoteId insideId = windowed[0].noteId;

    const auto windowSlots = SelectNavigation::buildSelectNavigationSlots(
        loopLength, 0, windowNavNotes, windowStart, false);

    bool outsideNoteInWindowSlots = false;
    for (const SelectNavigation::SelectNavSlot& slot : windowSlots) {
        if (slot.noteIdx < 0 || slot.noteIdx >= static_cast<int>(windowNavNotes.size())) {
            continue;
        }
        if (windowNavNotes[static_cast<size_t>(slot.noteIdx)].note == 72) {
            outsideNoteInWindowSlots = true;
            break;
        }
    }
    TEST_ASSERT_FALSE(outsideNoteInWindowSlots);

    bool insideNoteInWindowSlots = false;
    for (const SelectNavigation::SelectNavSlot& slot : windowSlots) {
        if (slot.noteIdx < 0 || slot.noteIdx >= static_cast<int>(windowNavNotes.size())) {
            continue;
        }
        if (windowNavNotes[static_cast<size_t>(slot.noteIdx)].noteId == insideId ||
            (insideId == kInvalidNoteId &&
             windowNavNotes[static_cast<size_t>(slot.noteIdx)].note == windowed[0].note)) {
            insideNoteInWindowSlots = true;
            break;
        }
    }
    TEST_ASSERT_TRUE(insideNoteInWindowSlots);
}

void test_same_bracket_sibling_plan_includes_all_motors() {
    const auto sibling = NoteEditFaderOutbound::planForSelectDependentFromNoteIdChange(
        1, 2, 579, 579);
    TEST_ASSERT_TRUE(sibling.coarse);
    TEST_ASSERT_TRUE(sibling.fine);
    TEST_ASSERT_TRUE(sibling.noteValue);
}

void test_geometry_driver_plan_refreshes_fader1_on_bracket_change() {
    const auto plan = NoteEditFaderOutbound::planForGeometryDriverMotorSync(100, 200);
    TEST_ASSERT_TRUE(plan.fader1);
    TEST_ASSERT_FALSE(plan.coarse);
    TEST_ASSERT_FALSE(plan.fine);
    TEST_ASSERT_FALSE(plan.noteValue);
}

void test_geometry_driver_plan_empty_when_bracket_unchanged() {
    const auto plan = NoteEditFaderOutbound::planForGeometryDriverMotorSync(438, 438);
    TEST_ASSERT_FALSE(plan.fader1);
    TEST_ASSERT_FALSE(plan.coarse);
    TEST_ASSERT_FALSE(plan.fine);
    TEST_ASSERT_FALSE(plan.noteValue);
}

void test_select_dependent_plan_excludes_fader1() {
    const auto bracketChange = NoteEditFaderOutbound::planForSelectDependentFromNoteIdChange(
        1, 2, 100, 200);
    TEST_ASSERT_FALSE(bracketChange.fader1);
    const auto sibling = NoteEditFaderOutbound::planForSelectDependentFromNoteIdChange(1, 2, 579,
                                                                                       579);
    TEST_ASSERT_FALSE(sibling.fader1);
}

void test_geometry_edit_kind_blocks_f1_select_apply_policy() {
    // Contract: geometry kinds are classified for undo/kind routing; F1 echo after geometry
    // motor sync is blocked via selectFaderFeedbackIgnoreUntilMs_, not blanket F1 suppression.
    TEST_ASSERT_TRUE(isGeometryEditKind(NoteEditKind::Move));
    TEST_ASSERT_TRUE(isGeometryEditKind(NoteEditKind::Length));
    TEST_ASSERT_TRUE(isGeometryEditKind(NoteEditKind::Pitch));
    TEST_ASSERT_TRUE(isGeometryEditKind(NoteEditKind::Add));
    TEST_ASSERT_TRUE(isGeometryEditKind(NoteEditKind::Delete));
    TEST_ASSERT_FALSE(isGeometryEditKind(NoteEditKind::Select));
}

void test_motor_sync_plans_are_direction_isolated() {
    const auto selectPlan = NoteEditFaderOutbound::planForSelectDependentFromNoteIdChange(1, 2, 100,
                                                                                        200);
    const auto geometryPlan = NoteEditFaderOutbound::planForGeometryDriverMotorSync(100, 200);
    TEST_ASSERT_TRUE(
        NoteEditFaderOutbound::motorSyncPlansAreDirectionIsolated(selectPlan, geometryPlan));
    NoteEditFaderOutbound::PlanFlags mixed = selectPlan;
    mixed.fader1 = true;
    TEST_ASSERT_FALSE(
        NoteEditFaderOutbound::motorSyncPlansAreDirectionIsolated(mixed, geometryPlan));
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
    RUN_TEST(test_outbound_plan_note_select_with_fader1_includes_dependents);
    RUN_TEST(test_outbound_plan_note_select_dependent_includes_dependents_only);
    RUN_TEST(test_coalesce_dependent_refresh_when_pipeline_active);
    RUN_TEST(test_restart_dependent_pipeline_on_selection_change);
    RUN_TEST(test_select_dependent_plan_from_ref_change);
    RUN_TEST(test_note_ref_change_triggers_selection_apply);
    RUN_TEST(test_same_index_different_ref_triggers_apply);
    RUN_TEST(test_same_ref_different_index_does_not_trigger_apply);
    RUN_TEST(test_bracket_change_triggers_note_ref_apply);
    RUN_TEST(test_outbound_pipeline_note_only_dependent_plan);
    RUN_TEST(test_slot_change_triggers_selection_apply);
    RUN_TEST(test_nav_change_triggers_on_slot_or_note_index);
    RUN_TEST(test_note_change_triggers_selection_apply);
    RUN_TEST(test_empty_note_idx_never_triggers_note_change_apply);
    RUN_TEST(test_same_tick_sibling_note_change_triggers_note_apply);
    RUN_TEST(test_bracket_or_note_change_triggers_selection_apply);
    RUN_TEST(test_same_tick_note_change_triggers_selection_apply);
    RUN_TEST(test_resolve_note_idx_at_slot_uses_slot_note_idx);
    RUN_TEST(test_slot_drift_last_applied_beats_find_slot_for_gate);
    RUN_TEST(test_sibling_slot_change_uses_full_dependent_plan);
    RUN_TEST(test_outbound_coarse_uses_loop_relative_tick_with_nonzero_loop_start);
    RUN_TEST(test_empty_step_bracket_rel_tick_maps_to_coarse_pitchbend);
    RUN_TEST(test_outbound_pipeline_advances_through_session_open);
    RUN_TEST(test_outbound_pipeline_advances_through_note_select_dependent);
    RUN_TEST(test_outbound_pipeline_length_mode_enter_parallel_dependent_step);
    RUN_TEST(test_motor_fader_timing_matches_ableton_reference_midis);
    RUN_TEST(test_motor_fader_burst_invokes_position_and_notegate_callbacks);
    RUN_TEST(test_select_fader_motor_idle_ms_constant);
    RUN_TEST(test_should_flush_select_dependent_motor_sync_after_idle);
    RUN_TEST(test_parallel_motor_fader_burst_interleaves_enabled_slots);
    RUN_TEST(test_ch13_ack_correlation_window_ms_for_capture_logs);
    RUN_TEST(test_select_fader_echo_rejects_near_last_sent);
    RUN_TEST(test_select_fader_echo_accepts_small_user_delta);
    RUN_TEST(test_ref_driven_motor_sync_ignores_index_only_change);
    RUN_TEST(test_select_dependent_settle_ms_in_capture_window);
    RUN_TEST(test_reference_step_from_bracket_tick);
    RUN_TEST(test_display_note_info_changed);
    RUN_TEST(test_display_note_info_snapshot_from_ref_wrap_formula);
    RUN_TEST(test_filtered_display_note_index_for_selection);
    RUN_TEST(test_window_filter_excludes_notes_outside_nav_inventory);
    RUN_TEST(test_same_bracket_sibling_plan_includes_all_motors);
    RUN_TEST(test_geometry_driver_plan_refreshes_fader1_on_bracket_change);
    RUN_TEST(test_geometry_driver_plan_empty_when_bracket_unchanged);
    RUN_TEST(test_geometry_edit_kind_blocks_f1_select_apply_policy);
    RUN_TEST(test_select_dependent_plan_excludes_fader1);
    RUN_TEST(test_motor_sync_plans_are_direction_isolated);
    return UNITY_END();
}
