//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include <unity.h>
#include <cstdint>

#include "TickPhase.h"

#include "../../src/Utils/IntervalProjection.cpp"

void test_tick_phase_loop_length_zero() {
    TEST_ASSERT_EQUAL_UINT32(0, tickPhaseInLoop(5, 0, 0));
    TEST_ASSERT_EQUAL_UINT32(0, tickPhaseInLoop(0xFFFFFFFFu, 10, 0));
}

void test_tick_phase_before_start_no_wrap() {
    TEST_ASSERT_EQUAL_UINT32(5, tickPhaseInLoop(5, 10'000, 100));
    TEST_ASSERT_EQUAL_UINT32(0, tickPhaseInLoop(10'000, 10'000, 100));
}

void test_tick_phase_modulo_and_signed_delta() {
    TEST_ASSERT_EQUAL_UINT32(7, tickPhaseInLoop(10'007, 10'000, 100));
    TEST_ASSERT_EQUAL_UINT32(99, tickPhaseInLoop(9'999, 10'000, 100));
}

void test_tick_phase_uint32_wrap_start() {
    const uint32_t start = 0xFFFFFFF0u;
    const uint32_t loop = 16;
    // current == start  => phase 0
    TEST_ASSERT_EQUAL_UINT32(0, tickPhaseInLoop(start, start, loop));
    // current = start + 3
    TEST_ASSERT_EQUAL_UINT32(3, tickPhaseInLoop(start + 3, start, loop));
}

void test_tick_phase_negative_delta_stable() {
    const uint32_t loop = 3840;
    const uint32_t start = 100;
    TEST_ASSERT_EQUAL_UINT32(3839u, tickPhaseInLoop(start - 1u, start, loop));
}

void test_overdub_freeze_close_tick_uses_loop_phase() {
    const uint32_t loopLength = 25344;
    const uint32_t startLoopTick = 40000;
    const uint32_t freezeAbsoluteTick = 45320;
    const uint32_t closeTick = tickPhaseInLoop(freezeAbsoluteTick, startLoopTick, loopLength);
    TEST_ASSERT_EQUAL_UINT32(5320u, closeTick);
}

int main(int /*argc*/, char** /*argv*/) {
    UNITY_BEGIN();
    RUN_TEST(test_tick_phase_loop_length_zero);
    RUN_TEST(test_tick_phase_before_start_no_wrap);
    RUN_TEST(test_tick_phase_modulo_and_signed_delta);
    RUN_TEST(test_tick_phase_uint32_wrap_start);
    RUN_TEST(test_tick_phase_negative_delta_stable);
    RUN_TEST(test_overdub_freeze_close_tick_uses_loop_phase);
    return UNITY_END();
}
