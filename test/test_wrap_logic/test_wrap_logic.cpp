//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include <unity.h>
#include <cstdint>

#include "Utils/NoteMovementWrap.h"

void test_wrap_position_negative_and_boundary() {
    const uint32_t loop = 3840;
    TEST_ASSERT_EQUAL_UINT32(loop - 1u, NoteMovementUtils::wrapPosition(-1, loop));
    TEST_ASSERT_EQUAL_UINT32(0u, NoteMovementUtils::wrapPosition(static_cast<int32_t>(loop), loop));
    TEST_ASSERT_EQUAL_UINT32(1u, NoteMovementUtils::wrapPosition(static_cast<int32_t>(loop) + 1, loop));
}

void test_calculate_note_length_plain_and_wrapped() {
    const uint32_t loop = 3840;
    TEST_ASSERT_EQUAL_UINT32(100u, NoteMovementUtils::calculateNoteLength(100, 200, loop));
    TEST_ASSERT_EQUAL_UINT32(7u, NoteMovementUtils::calculateNoteLength(3838, 5, loop));
}

void test_moving_note_range_wrap_does_not_contain_unrelated_loop_start_note() {
    constexpr uint32_t loop = 1536;
    TEST_ASSERT_FALSE(
        NoteMovementUtils::isNoteWithinMovingNoteRange(7, 55, 1472, 1583, loop));
    TEST_ASSERT_FALSE(
        NoteMovementUtils::isNoteWithinMovingNoteRange(1490, 0, 1472, 1583, loop));
    TEST_ASSERT_TRUE(
        NoteMovementUtils::isNoteWithinMovingNoteRange(1500, 1535, 1472, 1583, loop));
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
    RUN_TEST(test_notes_overlap_linear_span_avoids_display_wrap_false_positive);
    return UNITY_END();
}
