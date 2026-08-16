//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include <unity.h>
#include <cstdint>

#include "NoteEditGeometryApplyWrap.h"

void test_wrap_position_negative_and_boundary() {
    const uint32_t loop = 3840;
    TEST_ASSERT_EQUAL_UINT32(loop - 1u, NoteEditGeometryApply::wrapPosition(-1, loop));
    TEST_ASSERT_EQUAL_UINT32(0u, NoteEditGeometryApply::wrapPosition(static_cast<int32_t>(loop), loop));
    TEST_ASSERT_EQUAL_UINT32(1u, NoteEditGeometryApply::wrapPosition(static_cast<int32_t>(loop) + 1, loop));
}

void test_calculate_note_length_plain_and_wrapped() {
    const uint32_t loop = 3840;
    TEST_ASSERT_EQUAL_UINT32(100u, NoteEditGeometryApply::calculateNoteLength(100, 200, loop));
    TEST_ASSERT_EQUAL_UINT32(7u, NoteEditGeometryApply::calculateNoteLength(3838, 5, loop));
    // 192259: wrap 2592–96 on a 3072 loop is length 576, not head 96.
    TEST_ASSERT_EQUAL_UINT32(576u, NoteEditGeometryApply::calculateNoteLength(2592, 96, 3072));
    // Edit storage is linear (DEC-013 / 195941). Display phase 144 is reconstruct only.
    TEST_ASSERT_EQUAL_UINT32(3216u, NoteEditGeometryApply::linearStorageOffTickForSpanEnd(2640, 576));
    TEST_ASSERT_EQUAL_UINT32(144u, (2640u + 576u) % 3072u);
    TEST_ASSERT_EQUAL_UINT32(576u, NoteEditGeometryApply::calculateNoteLength(2640, 144, 3072));
}

void test_moving_note_range_wrap_does_not_contain_unrelated_loop_start_note() {
    constexpr uint32_t loop = 1536;
    TEST_ASSERT_FALSE(
        NoteEditGeometryApply::isNoteWithinMovingNoteRange(7, 55, 1472, 1583, loop));
    TEST_ASSERT_FALSE(
        NoteEditGeometryApply::isNoteWithinMovingNoteRange(1490, 0, 1472, 1583, loop));
    TEST_ASSERT_TRUE(
        NoteEditGeometryApply::isNoteWithinMovingNoteRange(1500, 1535, 1472, 1583, loop));
}

void test_display_wrapped_tail_on_tick_inside_linear_mover_span() {
    constexpr uint32_t newStart = 1482;
    constexpr uint32_t newEnd = 1577;
    const uint32_t noteStart = 1490;
    const uint32_t noteEnd = 0;
    TEST_ASSERT_TRUE(noteEnd < noteStart);
    TEST_ASSERT_TRUE(noteStart >= newStart && noteStart < newEnd);
}

void test_linear_storage_spans_overlap_tail_mover_vs_head_note() {
    constexpr uint32_t loop = 1536;
    const auto linearStorageSpansOverlap = [](uint32_t start1, uint32_t end1, uint32_t start2,
                                              uint32_t end2) {
        return start1 < end2 && start2 < end1;
    };
    TEST_ASSERT_FALSE(linearStorageSpansOverlap(1499, 1595, 11, 61));
    const auto notesOverlapWithWrapPhantom = [](uint32_t start1, uint32_t end1, uint32_t start2,
                                                uint32_t end2, uint32_t loopLength) {
        const bool note2WrapsAndOverlaps =
            (start2 + loopLength < end1) && (start1 < end2 + loopLength);
        return note2WrapsAndOverlaps;
    };
    TEST_ASSERT_TRUE(notesOverlapWithWrapPhantom(1499, 1595, 11, 61, loop));
}

void test_move_restore_adjacent_prefix_touch_is_not_overlap() {
    // session_20260714_011558.log: mover [144,240), restored neighbor [0,144)
    const auto linearStorageSpansOverlap = [](uint32_t start1, uint32_t end1, uint32_t start2,
                                              uint32_t end2) {
        return start1 < end2 && start2 < end1;
    };
    constexpr uint32_t newStart = 144;
    constexpr uint32_t newEnd = 240;
    constexpr uint32_t neighborStart = 0;
    constexpr uint32_t neighborEnd = 144;
    TEST_ASSERT_FALSE(
        linearStorageSpansOverlap(newStart, newEnd, neighborStart, neighborEnd));
    const bool oldPrefixDeleteRule =
        (neighborEnd == newStart && neighborStart < newStart);
    TEST_ASSERT_TRUE(oldPrefixDeleteRule);
    const bool partialPrefixOverlap =
        (neighborStart < newStart && neighborEnd > newStart &&
         linearStorageSpansOverlap(newStart, newEnd, neighborStart, neighborEnd));
    TEST_ASSERT_FALSE(partialPrefixOverlap);
}

void test_partial_prefix_under_mover_start_is_overlap() {
    const auto linearStorageSpansOverlap = [](uint32_t start1, uint32_t end1, uint32_t start2,
                                              uint32_t end2) {
        return start1 < end2 && start2 < end1;
    };
    constexpr uint32_t newStart = 144;
    constexpr uint32_t newEnd = 240;
    constexpr uint32_t neighborStart = 48;
    constexpr uint32_t neighborEnd = 200;
    TEST_ASSERT_TRUE(
        linearStorageSpansOverlap(newStart, newEnd, neighborStart, neighborEnd));
    const bool partialPrefixOverlap =
        (neighborStart < newStart && neighborEnd > newStart &&
         linearStorageSpansOverlap(newStart, newEnd, neighborStart, neighborEnd));
    TEST_ASSERT_TRUE(partialPrefixOverlap);
}

void test_notes_overlap_linear_span_avoids_display_wrap_false_positive() {
    constexpr uint32_t loop = 1536;
    const uint32_t moverStart = 1355;
    const uint32_t moverEnd = 1451;
    const uint32_t existingStart = 436;
    const uint32_t linearEnd = 483;
    const uint32_t displayWrapEnd = 1535;
    const auto intervalsOverlap = [](uint32_t start1, uint32_t end1, uint32_t start2,
                                     uint32_t end2) {
        return (start1 < end2) && (start2 < end1);
    };
    TEST_ASSERT_FALSE(intervalsOverlap(moverStart, moverEnd, existingStart, linearEnd));
    TEST_ASSERT_TRUE(intervalsOverlap(moverStart, moverEnd, existingStart, displayWrapEnd));
    (void)loop;
}

int main(int /*argc*/, char** /*argv*/) {
    UNITY_BEGIN();
    RUN_TEST(test_wrap_position_negative_and_boundary);
    RUN_TEST(test_calculate_note_length_plain_and_wrapped);
    RUN_TEST(test_moving_note_range_wrap_does_not_contain_unrelated_loop_start_note);
    RUN_TEST(test_display_wrapped_tail_on_tick_inside_linear_mover_span);
    RUN_TEST(test_linear_storage_spans_overlap_tail_mover_vs_head_note);
    RUN_TEST(test_move_restore_adjacent_prefix_touch_is_not_overlap);
    RUN_TEST(test_partial_prefix_under_mover_start_is_overlap);
    RUN_TEST(test_notes_overlap_linear_span_avoids_display_wrap_false_positive);
    return UNITY_END();
}
