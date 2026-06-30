//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#ifndef NOTE_EDIT_LENGTH_FADER_MAPPING_H
#define NOTE_EDIT_LENGTH_FADER_MAPPING_H

#include <algorithm>
#include <cstdint>

#include "MidiConfig.h"

namespace NoteEditLengthFaderMapping {

inline int16_t loopTickToCoarsePitchbend(uint32_t tick, uint32_t loopLength) {
    if (loopLength <= 1) {
        return MidiConfig::Pitchbend::CENTER;
    }
    tick %= loopLength;
    const float normalizedPos =
        static_cast<float>(tick) / static_cast<float>(loopLength - 1);
    const int16_t pitchbend = static_cast<int16_t>(
        MidiConfig::Pitchbend::MIN +
        normalizedPos * static_cast<float>(MidiConfig::Pitchbend::MAX - MidiConfig::Pitchbend::MIN));
    return static_cast<int16_t>(std::max<int32_t>(
        MidiConfig::Pitchbend::MIN,
        std::min<int32_t>(MidiConfig::Pitchbend::MAX, pitchbend)));
}

inline uint32_t coarsePitchbendToLoopTick(int16_t pitchValue, uint32_t loopLength) {
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

}  // namespace NoteEditLengthFaderMapping

#endif  // NOTE_EDIT_LENGTH_FADER_MAPPING_H
