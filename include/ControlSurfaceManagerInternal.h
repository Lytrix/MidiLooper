//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#pragma once

#include <cstdint>

#include "Utils/NoteEditMem.h"

NOTE_EDIT_MEM void applyLengthEndTargetRules(uint32_t noteStart, uint32_t currentEnd, uint32_t loopLength,
                                             uint32_t minNoteDuration, uint32_t& targetEndTick);

NOTE_EDIT_MEM uint32_t lengthEditCoarsePitchbendToLoopTick(int16_t pitchValue, uint32_t loopLength);

NOTE_EDIT_MEM void clampLengthEditFineTargetTick(int32_t signedTick, uint32_t loopLength,
                                                 uint32_t& outRelativeTick);

NOTE_EDIT_MEM int32_t lengthEditFineOffsetFromCc(uint8_t ccValue);
