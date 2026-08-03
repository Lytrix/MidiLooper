//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "Utils/RecordStopLength.h"

#include <algorithm>

#include "Globals.h"

namespace RecordStopLength {

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

uint32_t computeLoopLengthTicks(uint32_t lastEventTick) {
  constexpr uint32_t ticksPerBar = Config::TICKS_PER_BAR;
  const uint32_t fullBars = lastEventTick / ticksPerBar;
  const uint32_t rem = lastEventTick % ticksPerBar;
  const uint32_t grace = ticksPerBar / 6;

  if (rem <= grace) {
    return (fullBars > 0 ? fullBars : 1) * ticksPerBar;
  }

  if (lastEventTick < ticksPerBar / 2) {
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
  return std::min(transportLength, contentLength);
}

uint32_t computeTruncationRewindTicks(uint32_t rawLength, uint32_t finalLength) {
  if (finalLength == 0 || rawLength <= finalLength) {
    return 0;
  }
  const uint32_t positionInBar = rawLength % Config::TICKS_PER_BAR;
  return rawLength - positionInBar;
}

}  // namespace RecordStopLength
