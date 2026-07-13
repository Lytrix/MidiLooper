//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include <unity.h>
#include <cstdint>
#include <vector>

#include "MidiEvent.h"
#include "NoteEditSessionState.h"
#include "Utils/IntervalProjection.h"

#include "../../src/Utils/IntervalProjection.cpp"

namespace {

constexpr NoteId kTestNoteId = 42;

ProjectionContext makeContext(ProjectionType type, const TickInterval& window, uint32_t loopLength,
                              int32_t originTick = 0) {
    ProjectionContext context{};
    context.loopLength = loopLength;
    context.window = window;
    context.type = type;
    context.originTick = originTick;
    return context;
}

CanonicalNoteSpan makeSpan(int32_t start, int32_t end) {
    return CanonicalNoteSpan{kTestNoteId, TickInterval{start, end}, 60, 100};
}

void assertInterval(const ProjectedNoteInterval& projected, int32_t start, int32_t end) {
    TEST_ASSERT_EQUAL_INT32(start, projected.interval.start);
    TEST_ASSERT_EQUAL_INT32(end, projected.interval.end);
    TEST_ASSERT_EQUAL_UINT32(kTestNoteId, projected.noteId);
    TEST_ASSERT_EQUAL_UINT8(60, projected.pitch);
}

}  // namespace

void test_tick_interval_intersects_spec_example() {
    const TickInterval candidate{900, 1080};
    const TickInterval window{850, 950};
    TEST_ASSERT_TRUE(candidate.intersects(window));
}

void test_equivalent_intervals_900_1080_L960() {
    constexpr uint32_t loopLength = 960;
    const TickInterval window{-100, 2100};
    const ProjectionContext context = makeContext(ProjectionType::Playback, window, loopLength);
    const CanonicalNoteSpan span = makeSpan(900, 1080);

    const ProjectedIntervalVec candidates =
        IntervalProjection::generateEquivalentIntervals(span, loopLength, context);

    TEST_ASSERT_EQUAL(3, candidates.size());
    assertInterval(candidates[0], -60, 120);
    assertInterval(candidates[1], 900, 1080);
    assertInterval(candidates[2], 1860, 2040);
}

void test_k_bounds_no_fixed_cap() {
    constexpr uint32_t loopLength = 960;
    const TickInterval window{0, static_cast<int32_t>(5 * loopLength)};
    const ProjectionContext context = makeContext(ProjectionType::Playback, window, loopLength);
    const CanonicalNoteSpan span = makeSpan(100, 200);

    const ProjectedIntervalVec candidates =
        IntervalProjection::generateEquivalentIntervals(span, loopLength, context);

    TEST_ASSERT_EQUAL(5, candidates.size());
    assertInterval(candidates[0], 100, 200);
    assertInterval(candidates[1], 1060, 1160);
    assertInterval(candidates[4], 3940, 4040);
}

void test_generate_same_for_all_projection_types() {
    constexpr uint32_t loopLength = 960;
    const TickInterval window{-100, 2100};
    const CanonicalNoteSpan span = makeSpan(900, 1080);

    const auto playback =
        IntervalProjection::generateEquivalentIntervals(
            span, loopLength, makeContext(ProjectionType::Playback, window, loopLength));
    const auto display = IntervalProjection::generateEquivalentIntervals(
        span, loopLength, makeContext(ProjectionType::Display, window, loopLength));
    const auto edit = IntervalProjection::generateEquivalentIntervals(
        span, loopLength, makeContext(ProjectionType::Edit, window, loopLength));

    TEST_ASSERT_EQUAL(playback.size(), display.size());
    TEST_ASSERT_EQUAL(playback.size(), edit.size());
    for (size_t i = 0; i < playback.size(); ++i) {
        TEST_ASSERT_EQUAL_INT32(playback[i].interval.start, display[i].interval.start);
        TEST_ASSERT_EQUAL_INT32(playback[i].interval.end, edit[i].interval.end);
    }
}

void test_window_is_not_shifted() {
    constexpr uint32_t loopLength = 960;
    ProjectionContext context = makeContext(ProjectionType::Playback, {-100, 2100}, loopLength);
    const TickInterval windowBefore = context.window;
    const CanonicalNoteSpan span = makeSpan(900, 1080);

    (void)IntervalProjection::generateEquivalentIntervals(span, loopLength, context);

    TEST_ASSERT_EQUAL_INT32(windowBefore.start, context.window.start);
    TEST_ASSERT_EQUAL_INT32(windowBefore.end, context.window.end);
}

void test_selection_playback_prefers_origin() {
    constexpr uint32_t loopLength = 960;
    const TickInterval window{-100, 2100};
    ProjectionContext context = makeContext(ProjectionType::Playback, window, loopLength);
    context.originTick = 950;
    const CanonicalNoteSpan span = makeSpan(900, 1080);

    const ProjectedIntervalVec candidates =
        IntervalProjection::generateEquivalentIntervals(span, loopLength, context);
    const ProjectedNoteInterval selected =
        IntervalProjection::selectProjectedInterval(candidates, context);

    assertInterval(selected, 900, 1080);
}

void test_selection_playback_closest_when_origin_outside() {
    constexpr uint32_t loopLength = 960;
    const TickInterval window{-100, 2100};
    ProjectionContext context = makeContext(ProjectionType::Playback, window, loopLength);
    context.originTick = 130;
    const CanonicalNoteSpan span = makeSpan(900, 1080);

    const ProjectedIntervalVec candidates =
        IntervalProjection::generateEquivalentIntervals(span, loopLength, context);
    const ProjectedNoteInterval selected =
        IntervalProjection::selectProjectedInterval(candidates, context);

    assertInterval(selected, -60, 120);
}

void test_selection_display_returns_all() {
    constexpr uint32_t loopLength = 960;
    const TickInterval window{-100, 2100};
    ProjectionContext context = makeContext(ProjectionType::Display, window, loopLength);
    const CanonicalNoteSpan span = makeSpan(900, 1080);

    const ProjectedIntervalVec candidates =
        IntervalProjection::generateEquivalentIntervals(span, loopLength, context);
    const ProjectedIntervalVec selected =
        IntervalProjection::selectProjectedIntervalsForDisplay(candidates, context);

    TEST_ASSERT_EQUAL(3, selected.size());
    assertInterval(selected[0], -60, 120);
    assertInterval(selected[1], 900, 1080);
    assertInterval(selected[2], 1860, 2040);
}

void test_selection_edit_closest_to_origin() {
    constexpr uint32_t loopLength = 960;
    const TickInterval window{-100, 2100};
    ProjectionContext context = makeContext(ProjectionType::Edit, window, loopLength);
    context.originTick = 1900;
    const CanonicalNoteSpan span = makeSpan(900, 1080);

    const ProjectedIntervalVec candidates =
        IntervalProjection::generateEquivalentIntervals(span, loopLength, context);
    const ProjectedNoteInterval selected =
        IntervalProjection::selectProjectedInterval(candidates, context);

    assertInterval(selected, 1860, 2040);
}

void test_wrap_advance_projection_cycle() {
    TEST_ASSERT_EQUAL_INT32(10960,
                            IntervalProjection::advanceProjectionCycleStartTickOnWrap(10000, 960));
}

void test_mid_cycle_length_change_phase() {
    constexpr int32_t cycleStart = 10000;
    constexpr uint32_t currentTick = 10500;
    constexpr uint32_t oldLength = 960;
    constexpr uint32_t newLength = 1920;

    TEST_ASSERT_EQUAL_UINT32(500,
                             IntervalProjection::tickPhaseInProjectionCycle(currentTick, cycleStart,
                                                                            oldLength));
    TEST_ASSERT_EQUAL_UINT32(500,
                             IntervalProjection::tickPhaseInProjectionCycle(currentTick, cycleStart,
                                                                            newLength));
    TEST_ASSERT_EQUAL_INT32(cycleStart,
                            IntervalProjection::advanceProjectionCycleStartTickOnWrap(cycleStart,
                                                                                      oldLength) -
                                static_cast<int32_t>(oldLength));
}

void test_note_id_invariant_all_k() {
    constexpr uint32_t loopLength = 960;
    const ProjectionContext context =
        makeContext(ProjectionType::Playback, {-100, 2100}, loopLength);
    const CanonicalNoteSpan span = makeSpan(900, 1080);

    const ProjectedIntervalVec candidates =
        IntervalProjection::generateEquivalentIntervals(span, loopLength, context);

    for (const ProjectedNoteInterval& candidate : candidates) {
        TEST_ASSERT_EQUAL_UINT32(kTestNoteId, candidate.noteId);
    }
}

void test_project_note_intervals_batch() {
    constexpr uint32_t loopLength = 960;
    const TickInterval window{-100, 2100};
    const CanonicalNoteSpanVec spans = {makeSpan(900, 1080)};

    ProjectionContext playbackContext = makeContext(ProjectionType::Playback, window, loopLength);
    playbackContext.originTick = 950;
    const ProjectedIntervalVec playbackProjected =
        IntervalProjection::projectNoteIntervals(spans, playbackContext);
    TEST_ASSERT_EQUAL(1, playbackProjected.size());
    assertInterval(playbackProjected[0], 900, 1080);

    const ProjectionContext displayContext = makeContext(ProjectionType::Display, window, loopLength);
    const ProjectedIntervalVec displayProjected =
        IntervalProjection::projectNoteIntervals(spans, displayContext);
    TEST_ASSERT_EQUAL(3, displayProjected.size());
}

void test_select_empty_candidates_returns_invalid_note() {
    const ProjectionContext context = makeContext(ProjectionType::Playback, {0, 960}, 960);
    const ProjectedNoteInterval selected =
        IntervalProjection::selectProjectedInterval({}, context);
    TEST_ASSERT_EQUAL_UINT32(kInvalidNoteId, selected.noteId);
}

void test_build_edit_projection_context_fields() {
    constexpr uint32_t loopLength = 1536;
    EditorSelection selection;
    selection.primaryNote = 42;
    selection.selectedTick = 900;
    selection.selectedNotes.push_back(42);

    const TickInterval window = IntervalProjection::makeFullLoopEditAnalysisWindow(loopLength);
    const ProjectionContext context =
        IntervalProjection::buildEditProjectionContext(selection, loopLength, window, 1484);

    TEST_ASSERT_EQUAL(ProjectionType::Edit, context.type);
    TEST_ASSERT_EQUAL_UINT32(loopLength, context.loopLength);
    TEST_ASSERT_EQUAL_INT32(0, context.window.start);
    TEST_ASSERT_EQUAL_INT32(static_cast<int32_t>(loopLength), context.window.end);
    TEST_ASSERT_EQUAL_INT32(1484, context.originTick);
    TEST_ASSERT_EQUAL_INT32(900, context.selectedTick);
}

void test_project_edit_intervals_for_analysis_batch() {
    constexpr uint32_t loopLength = 960;
    const TickInterval window = IntervalProjection::makeFullLoopEditAnalysisWindow(loopLength);
    EditorSelection selection;
    selection.primaryNote = kTestNoteId;
    selection.selectedTick = 950;
    ProjectionContext context =
        IntervalProjection::buildEditProjectionContext(selection, loopLength, window, 950);

    const CanonicalNoteSpanVec spans = {makeSpan(900, 1080)};
    const ProjectedIntervalVec projected =
        IntervalProjection::projectEditIntervalsForAnalysis(spans, context);

    TEST_ASSERT_EQUAL(1, projected.size());
    assertInterval(projected[0], 900, 1080);
}

void test_project_edit_intervals_for_analysis_forces_edit_type() {
    constexpr uint32_t loopLength = 960;
    ProjectionContext context = makeContext(ProjectionType::Playback, {0, 960}, loopLength);
    context.originTick = 130;
    const CanonicalNoteSpanVec spans = {makeSpan(900, 1080)};

    const ProjectedIntervalVec projected =
        IntervalProjection::projectEditIntervalsForAnalysis(spans, context);

    TEST_ASSERT_EQUAL(1, projected.size());
    assertInterval(projected[0], -60, 120);
}

void test_playback_linear_off_at_loop_head() {
    constexpr uint32_t loopLength = 1536;
    const TickInterval window = IntervalProjection::makeFullLoopPlaybackWindow(loopLength);
    ProjectionContext context =
        IntervalProjection::buildPlaybackProjectionContext(loopLength, window, 0, 0, 0, false, 0);

    TEST_ASSERT_EQUAL_UINT32(0, IntervalProjection::projectPlaybackEventPhase(1536, context));
}

void test_playback_order_linear_off_before_in_loop() {
    constexpr uint32_t loopLength = 1536;
    const TickInterval window = IntervalProjection::makeFullLoopPlaybackWindow(loopLength);
    ProjectionContext context =
        IntervalProjection::buildPlaybackProjectionContext(loopLength, window, 0, 0, 0, false, 0);

    const uint32_t noteOnPhase = IntervalProjection::projectPlaybackEventPhase(1400, context);
    const uint32_t noteOffPhase = IntervalProjection::projectPlaybackEventPhase(1536, context);
    TEST_ASSERT_TRUE(noteOffPhase < noteOnPhase);
}

void test_build_playback_projection_context_fields() {
    constexpr uint32_t loopLength = 960;
    const TickInterval window = IntervalProjection::makeFullLoopPlaybackWindow(loopLength);
    const ProjectionContext context = IntervalProjection::buildPlaybackProjectionContext(
        loopLength, window, 5000, 4800, 120, true, 240);

    TEST_ASSERT_EQUAL(ProjectionType::Playback, context.type);
    TEST_ASSERT_EQUAL_UINT32(loopLength, context.loopLength);
    TEST_ASSERT_EQUAL_INT32(5000, context.originTick);
    TEST_ASSERT_EQUAL_INT32(4800, context.projectionCycleStartTick);
    TEST_ASSERT_EQUAL_INT32(120, context.loopStartTick);
    TEST_ASSERT_TRUE(context.useQueuedStart);
    TEST_ASSERT_EQUAL_INT32(240, context.queuedStartTick);
}

void test_display_playhead_aligns_with_projection_cycle_after_slot_commit() {
    constexpr uint32_t loopLength = 1536;
    constexpr uint32_t startLoopTick = 1;
    constexpr int32_t projectionCycleStartTick = 55210;
    constexpr uint32_t loopStartTick = 0;
    constexpr uint32_t currentTick = static_cast<uint32_t>(projectionCycleStartTick) + 100U;

    const uint32_t playbackPhase = IntervalProjection::tickPhaseInProjectionCycle(
        currentTick, projectionCycleStartTick, loopLength);

    const uint32_t legacyStoragePhase =
        IntervalProjection::tickPhaseInLoop(currentTick, startLoopTick, loopLength);
    const uint32_t legacyDisplay =
        IntervalProjection::noteRelativeTick(legacyStoragePhase, loopStartTick, loopLength);

    const uint32_t alignedStoragePhase = IntervalProjection::tickPhaseInProjectionCycle(
        currentTick, projectionCycleStartTick, loopLength);
    const uint32_t alignedDisplay =
        IntervalProjection::noteRelativeTick(alignedStoragePhase, loopStartTick, loopLength);

    TEST_ASSERT_EQUAL_UINT32(100U, playbackPhase);
    TEST_ASSERT_NOT_EQUAL(playbackPhase, legacyDisplay);
    TEST_ASSERT_EQUAL_UINT32(playbackPhase, alignedDisplay);
}

void test_tick_phase_in_projection_cycle_negative_origin() {
    constexpr uint32_t loopLength = 3072;
    constexpr int32_t projectionCycleStartTick = -24;
    TEST_ASSERT_EQUAL_UINT32(24U,
                             IntervalProjection::tickPhaseInProjectionCycle(0, projectionCycleStartTick,
                                                                            loopLength));
    TEST_ASSERT_EQUAL_UINT32(48U,
                             IntervalProjection::tickPhaseInProjectionCycle(24, projectionCycleStartTick,
                                                                            loopLength));
}

void test_transport_downbeat_display_with_loop_start_offset() {
    // Record-stop / preserve path: projection aligned to loopStartTick, display at bar 1.
    constexpr uint32_t loopLength = 3072;
    constexpr uint32_t loopStartTick = 24;
    constexpr int32_t projectionCycleStartTick = -static_cast<int32_t>(loopStartTick);
    constexpr uint32_t currentTick = 0;

    const uint32_t storagePhase = IntervalProjection::tickPhaseInProjectionCycle(
        currentTick, projectionCycleStartTick, loopLength);
    const uint32_t displayPhase =
        IntervalProjection::noteRelativeTick(storagePhase, loopStartTick, loopLength);

    TEST_ASSERT_EQUAL_UINT32(loopStartTick, storagePhase);
    TEST_ASSERT_EQUAL_UINT32(0U, displayPhase);
}

void test_fresh_transport_linear_display_phase() {
    // Fresh transport clears loopStartTick; playhead is linear storage phase.
    constexpr uint32_t loopLength = 3072;
    constexpr int32_t projectionCycleStartTick = 0;
    constexpr uint32_t loopStartTick = 0;

    for (uint32_t tick : {0U, 768U, 1536U, 2304U, 3071U}) {
        const uint32_t storagePhase = IntervalProjection::tickPhaseInProjectionCycle(
            tick, projectionCycleStartTick, loopLength);
        const uint32_t displayPhase =
            IntervalProjection::noteRelativeTick(storagePhase, loopStartTick, loopLength);
        TEST_ASSERT_EQUAL_UINT32(tick, storagePhase);
        TEST_ASSERT_EQUAL_UINT32(tick, displayPhase);
        TEST_ASSERT_EQUAL_UINT32(tick / 768U, displayPhase / 768U);
    }
}

void test_display_wrap_backward_only_on_musical_loop_head() {
    constexpr uint32_t loopLength = 3072;
    constexpr uint32_t loopStartTick = 24;

    TEST_ASSERT_FALSE(IntervalProjection::didDisplayPlayheadWrapBackward(0U, 3071U, loopStartTick,
                                                                        loopLength));
    TEST_ASSERT_TRUE(IntervalProjection::didDisplayPlayheadWrapBackward(24U, 23U, loopStartTick,
                                                                        loopLength));
}

int main(int /*argc*/, char** /*argv*/) {
    UNITY_BEGIN();
    RUN_TEST(test_tick_interval_intersects_spec_example);
    RUN_TEST(test_equivalent_intervals_900_1080_L960);
    RUN_TEST(test_k_bounds_no_fixed_cap);
    RUN_TEST(test_generate_same_for_all_projection_types);
    RUN_TEST(test_window_is_not_shifted);
    RUN_TEST(test_selection_playback_prefers_origin);
    RUN_TEST(test_selection_playback_closest_when_origin_outside);
    RUN_TEST(test_selection_display_returns_all);
    RUN_TEST(test_selection_edit_closest_to_origin);
    RUN_TEST(test_wrap_advance_projection_cycle);
    RUN_TEST(test_mid_cycle_length_change_phase);
    RUN_TEST(test_note_id_invariant_all_k);
    RUN_TEST(test_project_note_intervals_batch);
    RUN_TEST(test_select_empty_candidates_returns_invalid_note);
    RUN_TEST(test_build_edit_projection_context_fields);
    RUN_TEST(test_project_edit_intervals_for_analysis_batch);
    RUN_TEST(test_project_edit_intervals_for_analysis_forces_edit_type);
    RUN_TEST(test_playback_linear_off_at_loop_head);
    RUN_TEST(test_playback_order_linear_off_before_in_loop);
    RUN_TEST(test_build_playback_projection_context_fields);
    RUN_TEST(test_display_playhead_aligns_with_projection_cycle_after_slot_commit);
    RUN_TEST(test_tick_phase_in_projection_cycle_negative_origin);
    RUN_TEST(test_transport_downbeat_display_with_loop_start_offset);
    RUN_TEST(test_fresh_transport_linear_display_phase);
    RUN_TEST(test_display_wrap_backward_only_on_musical_loop_head);
    return UNITY_END();
}
