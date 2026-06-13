//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include <unity.h>
#include <cstdint>

#include "Utils/ClockTransportUtils.h"

namespace {
constexpr int kInternal = 0;
constexpr int kExternal = 1;
constexpr uint32_t kTimeout = 500000;
}

void test_internal_clock_is_always_master() {
  TEST_ASSERT_TRUE(ClockTransportUtils::shouldEmitMidiTransportAsMaster(
      kInternal, 0, 1'000'000, kTimeout));
}

void test_external_clock_active_is_not_master() {
  const uint32_t now = 2'000'000;
  TEST_ASSERT_FALSE(ClockTransportUtils::shouldEmitMidiTransportAsMaster(
      kExternal, now - 1000, now, kTimeout));
}

void test_external_clock_timed_out_is_master() {
  const uint32_t now = 2'000'000;
  TEST_ASSERT_TRUE(ClockTransportUtils::shouldEmitMidiTransportAsMaster(
      kExternal, now - kTimeout - 1, now, kTimeout));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_internal_clock_is_always_master);
  RUN_TEST(test_external_clock_active_is_not_master);
  RUN_TEST(test_external_clock_timed_out_is_master);
  return UNITY_END();
}
