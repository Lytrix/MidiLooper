//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "ControlSurfaceManagerInternal.h"

#include <Arduino.h>

#include "Globals.h"
#include "MidiConfig.h"
#include "Utils/NoteEditMem.h"
#include "Utils/NoteMovementUtils.h"

NOTE_EDIT_MEM void applyLengthEndTargetRules(uint32_t noteStart, uint32_t currentEnd, uint32_t loopLength,
                                             uint32_t minNoteDuration, uint32_t& targetEndTick) {
    if (loopLength == 0) {
        return;
    }
    noteStart %= loopLength;
    currentEnd %= loopLength;
    targetEndTick %= loopLength;

    const bool nonWrap = currentEnd > noteStart;
    if (nonWrap) {
        const uint32_t minEndTick = noteStart + minNoteDuration;
        if (targetEndTick < minEndTick) {
            targetEndTick = minEndTick;
        }
        return;
    }

    const uint32_t newNoteDuration =
        NoteMovementUtils::calculateNoteLength(noteStart, targetEndTick, loopLength);
    if (newNoteDuration < minNoteDuration) {
        targetEndTick = (noteStart + minNoteDuration) % loopLength;
    }
}

NOTE_EDIT_MEM int16_t lengthEditLoopTickToCoarsePitchbend(uint32_t tick, uint32_t loopLength) {
    if (loopLength <= 1) {
        return MidiConfig::Pitchbend::CENTER;
    }
    tick %= loopLength;
    const float normalizedPos =
        static_cast<float>(tick) / static_cast<float>(loopLength - 1);
    const int16_t pitchbend = static_cast<int16_t>(
        MidiConfig::Pitchbend::MIN +
        normalizedPos * static_cast<float>(MidiConfig::Pitchbend::MAX - MidiConfig::Pitchbend::MIN));
    return constrain(pitchbend, MidiConfig::Pitchbend::MIN, MidiConfig::Pitchbend::MAX);
}

NOTE_EDIT_MEM uint32_t lengthEditCoarsePitchbendToLoopTick(int16_t pitchValue, uint32_t loopLength) {
    if (loopLength <= 1) {
        return 0;
    }
    const float normalizedPos =
        static_cast<float>(pitchValue - MidiConfig::Pitchbend::MIN) /
        static_cast<float>(MidiConfig::Pitchbend::MAX - MidiConfig::Pitchbend::MIN);
    const float tickFloat = normalizedPos * static_cast<float>(loopLength - 1);
    const uint32_t tick = static_cast<uint32_t>(tickFloat + 0.5f);
    return tick >= loopLength ? loopLength - 1 : tick;
}

NOTE_EDIT_MEM void clampLengthEditFineTargetTick(int32_t signedTick, uint32_t loopLength,
                                                 uint32_t& outRelativeTick) {
    if (loopLength == 0) {
        outRelativeTick = 0;
        return;
    }
    if (signedTick <= 0) {
        outRelativeTick = 0;
        return;
    }
    if (static_cast<uint32_t>(signedTick) >= loopLength) {
        outRelativeTick = loopLength - 1;
        return;
    }
    outRelativeTick = static_cast<uint32_t>(signedTick);
}

NOTE_EDIT_MEM int32_t lengthEditFineOffsetFromCc(uint8_t ccValue) {
    const int32_t halfRange = static_cast<int32_t>(Config::TICKS_PER_16TH_STEP);
    const int32_t rawOffset = static_cast<int32_t>(ccValue) - 64;
    return constrain(rawOffset, -halfRange, halfRange);
}

NOTE_EDIT_MEM uint8_t lengthEditFineCcFromOffset(int32_t offsetFromAnchor) {
    const int32_t halfRange = static_cast<int32_t>(Config::TICKS_PER_16TH_STEP);
    const int32_t clampedOffset = constrain(offsetFromAnchor, -halfRange, halfRange);
    return static_cast<uint8_t>(constrain(64 + clampedOffset, 0, 127));
}
