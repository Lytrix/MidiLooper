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

int main(int /*argc*/, char** /*argv*/) {
    UNITY_BEGIN();
    RUN_TEST(test_wrap_position_negative_and_boundary);
    RUN_TEST(test_calculate_note_length_plain_and_wrapped);
    return UNITY_END();
}
