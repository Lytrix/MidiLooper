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

int main(int /*argc*/, char** /*argv*/) {
    UNITY_BEGIN();
    RUN_TEST(test_wrap_position_negative_and_boundary);
    RUN_TEST(test_calculate_note_length_plain_and_wrapped);
    RUN_TEST(test_moving_note_range_wrap_does_not_contain_unrelated_loop_start_note);
    RUN_TEST(test_display_wrapped_tail_on_tick_inside_linear_mover_span);
    return UNITY_END();
}
