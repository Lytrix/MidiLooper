//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include <unity.h>

#include "Globals.h"

namespace {

uint32_t quantizeTransportRecordLength(uint32_t rawLength) {
  constexpr uint32_t ticksPerBar = Config::TICKS_PER_BAR;
  if (rawLength == 0) {
    return ticksPerBar;
  }
  const uint32_t rem = rawLength % ticksPerBar;
  const uint32_t grace = ticksPerBar / 2;
  if (rem <= grace) {
    const uint32_t quantized = (rawLength / ticksPerBar) * ticksPerBar;
    return quantized == 0 ? ticksPerBar : quantized;
  }
  return ((rawLength / ticksPerBar) + 1) * ticksPerBar;
}

uint32_t computeLoopLengthTicks(uint32_t lastTick) {
  constexpr uint32_t ticksPerBar = Config::TICKS_PER_BAR;
  const uint32_t fullBars = lastTick / ticksPerBar;
  const uint32_t rem = lastTick % ticksPerBar;
  const uint32_t grace = ticksPerBar / 6;
  if (rem <= grace) {
    return (fullBars > 0 ? fullBars : 1) * ticksPerBar;
  }
  if (lastTick < ticksPerBar / 2) {
    return ticksPerBar;
  }
  return (fullBars + 1) * ticksPerBar;
}

uint32_t computeRecordStopLengthTicks(uint32_t rawLength, uint32_t lastEventTick) {
  const uint32_t transportLength = quantizeTransportRecordLength(rawLength);
  if (lastEventTick == 0) {
    return transportLength;
  }
  const uint32_t contentLength = computeLoopLengthTicks(lastEventTick);
  return transportLength < contentLength ? transportLength : contentLength;
}

}  // namespace

void test_record_stop_uses_content_length_when_shorter_than_transport() {
  constexpr uint32_t ticksPerBar = Config::TICKS_PER_BAR;
  const uint32_t lastEvent = 4 * ticksPerBar - 1;
  const uint32_t rawLength = 5096;
  const uint32_t finalLength = computeRecordStopLengthTicks(rawLength, lastEvent);
  TEST_ASSERT_EQUAL_UINT32(4 * ticksPerBar, finalLength);
}

void test_record_stop_uses_transport_when_no_content() {
  constexpr uint32_t ticksPerBar = Config::TICKS_PER_BAR;
  const uint32_t rawLength = 5096;
  const uint32_t finalLength = computeRecordStopLengthTicks(rawLength, 0);
  TEST_ASSERT_EQUAL_UINT32(7 * ticksPerBar, finalLength);
}

int main(int /*argc*/, char** /*argv*/) {
  UNITY_BEGIN();
  RUN_TEST(test_record_stop_uses_content_length_when_shorter_than_transport);
  RUN_TEST(test_record_stop_uses_transport_when_no_content);
  return UNITY_END();
}
