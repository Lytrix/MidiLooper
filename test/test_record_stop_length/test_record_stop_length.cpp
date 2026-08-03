//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include <unity.h>

#include "Globals.h"
#include "Utils/RecordStopLength.h"

#include "../../src/Utils/RecordStopLength.cpp"

void test_record_stop_uses_content_length_when_shorter_than_transport() {
  constexpr uint32_t ticksPerBar = Config::TICKS_PER_BAR;
  const uint32_t lastEvent = 4 * ticksPerBar - 1;
  const uint32_t rawLength = 5096;
  const uint32_t finalLength =
      RecordStopLength::computeRecordStopLengthTicks(rawLength, lastEvent);
  TEST_ASSERT_EQUAL_UINT32(4 * ticksPerBar, finalLength);
}

void test_record_stop_uses_transport_when_no_content() {
  constexpr uint32_t ticksPerBar = Config::TICKS_PER_BAR;
  const uint32_t rawLength = 5096;
  const uint32_t finalLength = RecordStopLength::computeRecordStopLengthTicks(rawLength, 0);
  TEST_ASSERT_EQUAL_UINT32(7 * ticksPerBar, finalLength);
}

void test_truncation_rewind_when_raw_exceeds_final() {
  constexpr uint32_t ticksPerBar = Config::TICKS_PER_BAR;
  const uint32_t rawLength = 5096;
  const uint32_t finalLength = 4 * ticksPerBar;
  const uint32_t expected = rawLength - (rawLength % ticksPerBar);
  TEST_ASSERT_EQUAL_UINT32(
      expected, RecordStopLength::computeTruncationRewindTicks(rawLength, finalLength));
}

void test_truncation_rewind_zero_when_equal() {
  constexpr uint32_t ticksPerBar = Config::TICKS_PER_BAR;
  const uint32_t length = 5 * ticksPerBar;
  TEST_ASSERT_EQUAL_UINT32(0, RecordStopLength::computeTruncationRewindTicks(length, length));
}

void test_truncation_rewind_zero_when_final_zero() {
  TEST_ASSERT_EQUAL_UINT32(0, RecordStopLength::computeTruncationRewindTicks(100, 0));
}

int main(int /*argc*/, char** /*argv*/) {
  UNITY_BEGIN();
  RUN_TEST(test_record_stop_uses_content_length_when_shorter_than_transport);
  RUN_TEST(test_record_stop_uses_transport_when_no_content);
  RUN_TEST(test_truncation_rewind_when_raw_exceeds_final);
  RUN_TEST(test_truncation_rewind_zero_when_equal);
  RUN_TEST(test_truncation_rewind_zero_when_final_zero);
  return UNITY_END();
}
