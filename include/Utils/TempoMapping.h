//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#ifndef TEMPO_MAPPING_H
#define TEMPO_MAPPING_H

#include <algorithm>
#include <cmath>
#include <cstdint>

/**
 * @namespace TempoMapping
 * @brief Pure CC <-> BPM mapping for the loop-edit tempo fader.
 *
 * One CC step is exactly one BPM: 128 CC values (0..127) span
 * TEMPO_MIN_BPM..TEMPO_MAX_BPM inclusive, so every reachable tempo is an exact
 * integer and the motor fader has a detent per BPM. Dependency-free so the host
 * tests can drive the production conversion and the firmware and its tests
 * cannot drift.
 */
namespace TempoMapping {

constexpr float TEMPO_MIN_BPM = 60.0f;
constexpr float TEMPO_MAX_BPM = 187.0f;  // 128 steps: 127 CC values above the minimum
constexpr uint8_t TEMPO_CC_MAX = 127;
constexpr uint8_t TEMPO_STEPS = 128;

/// CC 0..127 -> BPM, one BPM per step.
inline float tempoFromCC(uint8_t ccValue) {
    const uint8_t clamped = ccValue > TEMPO_CC_MAX ? TEMPO_CC_MAX : ccValue;
    return TEMPO_MIN_BPM + static_cast<float>(clamped);
}

/// BPM -> the CC that parks a motor fader at that tempo (nearest whole BPM).
inline uint8_t ccFromTempo(float tempoBpm) {
    const float clamped = std::max(TEMPO_MIN_BPM, std::min(tempoBpm, TEMPO_MAX_BPM));
    return static_cast<uint8_t>(std::lround(clamped - TEMPO_MIN_BPM));
}

}  // namespace TempoMapping

#endif  // TEMPO_MAPPING_H
