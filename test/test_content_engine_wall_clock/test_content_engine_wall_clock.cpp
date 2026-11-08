//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// Sets browser slice 1: wall-clock time — only a valid RTC time is shown (never a substitute), local time is UTC plus
// the owner's offset, and the browser's formats.

#include <unity.h>

#include <cstring>

#include "ContentEngine/WallClock.h"

using namespace ContentEngine::wallclock;

void setUp() {}
void tearDown() {}

void test_only_a_plausible_time_is_valid() {
  TEST_ASSERT_FALSE(valid(0));              // a clock without its battery
  TEST_ASSERT_FALSE(valid(86400 * 365));    // 1971
  TEST_ASSERT_FALSE(valid(kFirstValid - 1));
  TEST_ASSERT_TRUE(valid(kFirstValid));
  TEST_ASSERT_TRUE(valid(1794146400u));     // 8 Nov 2026
  TEST_ASSERT_FALSE(valid(kLastValid + 1));
}

void test_local_fields_follow_the_calendar() {
  const Local a = local(1794146400u, 0);  // 2026-11-08 14:00:00 UTC
  TEST_ASSERT_EQUAL_UINT16(2026, a.year);
  TEST_ASSERT_EQUAL_UINT8(11, a.month);
  TEST_ASSERT_EQUAL_UINT8(8, a.day);
  TEST_ASSERT_EQUAL_UINT8(14, a.hour);
  TEST_ASSERT_EQUAL_UINT8(0, a.minute);
  const Local leap = local(1709208000u, 0);  // 2024-02-29 12:00 UTC
  TEST_ASSERT_EQUAL_UINT8(2, leap.month);
  TEST_ASSERT_EQUAL_UINT8(29, leap.day);
  const Local ny = local(1798761599u, 0);  // 2026-12-31 23:59:59 UTC
  TEST_ASSERT_EQUAL_UINT16(2026, ny.year);
  TEST_ASSERT_EQUAL_UINT8(12, ny.month);
  TEST_ASSERT_EQUAL_UINT8(31, ny.day);
  TEST_ASSERT_EQUAL_UINT8(59, ny.second);
}

void test_the_offset_moves_the_local_time_across_days() {
  const Local cet = local(1798761599u, 60);  // +1 h: into 2027
  TEST_ASSERT_EQUAL_UINT16(2027, cet.year);
  TEST_ASSERT_EQUAL_UINT8(1, cet.month);
  TEST_ASSERT_EQUAL_UINT8(1, cet.day);
  TEST_ASSERT_EQUAL_UINT8(0, cet.hour);
  const Local west = local(1794146400u, -15 * 60);  // −15 h: the day before
  TEST_ASSERT_EQUAL_UINT8(7, west.day);
  TEST_ASSERT_EQUAL_UINT8(23, west.hour);
  TEST_ASSERT_EQUAL_INT16(kMaxOffset, clampOffset(20 * 60));
  TEST_ASSERT_EQUAL_INT16(kMinOffset, clampOffset(-20 * 60));
}

void test_formats_name_an_unset_clock() {
  char b[24];
  formatStamp(1794146400u + 20 * 60, 60, b, sizeof b);
  TEST_ASSERT_EQUAL_STRING("8 NOV 2026 15:20", b);
  formatDay(1794146400u, 60, b, sizeof b);
  TEST_ASSERT_EQUAL_STRING("8 NOV", b);
  formatClock(1794146400u + 32 * 60, 60, b, sizeof b);
  TEST_ASSERT_EQUAL_STRING("15:32", b);
  formatStamp(1000, 60, b, sizeof b);
  TEST_ASSERT_EQUAL_STRING("TIME NOT SET", b);
  formatDay(1000, 60, b, sizeof b);
  TEST_ASSERT_EQUAL_STRING("--", b);
  formatClock(1000, 60, b, sizeof b);
  TEST_ASSERT_EQUAL_STRING("--:--", b);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_only_a_plausible_time_is_valid);
  RUN_TEST(test_local_fields_follow_the_calendar);
  RUN_TEST(test_the_offset_moves_the_local_time_across_days);
  RUN_TEST(test_formats_name_an_unset_clock);
  return UNITY_END();
}
