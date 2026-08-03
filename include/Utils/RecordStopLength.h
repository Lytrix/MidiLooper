//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#pragma once

#include <cstdint>

/// Pure record-stop length math shared by Track and native tests.
/// Uses Config::TICKS_PER_BAR — no Track instance required.
namespace RecordStopLength {

uint32_t quantizeTransportRecordLength(uint32_t rawLength);
uint32_t computeLoopLengthTicks(uint32_t lastEventTick);
uint32_t computeRecordStopLengthTicks(uint32_t rawLength, uint32_t lastEventTick);
uint32_t computeTruncationRewindTicks(uint32_t rawLength, uint32_t finalLength);

}  // namespace RecordStopLength
