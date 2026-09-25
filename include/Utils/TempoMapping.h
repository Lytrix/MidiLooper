//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#ifndef TEMPO_MAPPING_H
#define TEMPO_MAPPING_H

#include <cmath>
#include <cstdint>
#include <algorithm>

/**
 * @namespace TempoMapping
 * @brief Pure CC <-> BPM mapping for the loop-edit tempo fader.
 *
 * Dependency-free so the host tests can drive the production conversion and the
 * firmware and its tests cannot drift. The tempo fader reports a 7-bit CC and the
 * full travel spans TEMPO_MIN_BPM..TEMPO_MAX_BPM.
 */
namespace TempoMapping {

constexpr float TEMPO_MIN_BPM = 72.0f;
constexpr float TEMPO_MAX_BPM = 200.0f;
constexpr uint8_t TEMPO_CC_MAX = 127;

/// CC 0..127 -> BPM across the full fader travel.
inline float tempoFromCC(uint8_t ccValue) {
    const uint8_t clamped = ccValue > TEMPO_CC_MAX ? TEMPO_CC_MAX : ccValue;
    return TEMPO_MIN_BPM + (static_cast<float>(clamped) / static_cast<float>(TEMPO_CC_MAX)) *
                               (TEMPO_MAX_BPM - TEMPO_MIN_BPM);
}

/// BPM -> the CC that parks a motor fader at that tempo (rounded).
inline uint8_t ccFromTempo(float tempoBpm) {
    const float clamped = std::max(TEMPO_MIN_BPM, std::min(tempoBpm, TEMPO_MAX_BPM));
    const float span = TEMPO_MAX_BPM - TEMPO_MIN_BPM;
    return static_cast<uint8_t>(
        std::lround((clamped - TEMPO_MIN_BPM) / span * static_cast<float>(TEMPO_CC_MAX)));
}

}  // namespace TempoMapping

#endif  // TEMPO_MAPPING_H
