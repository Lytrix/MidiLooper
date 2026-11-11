//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#ifndef TEMPO_MAPPING_H
#define TEMPO_MAPPING_H

#include <algorithm>
#include <cmath>
#include <cstdint>

/**
 * @namespace TempoMapping
 * @brief Pure fader <-> BPM mappings for the loop-edit tempo fader.
 *
 * Two mappings, for a 7-bit CC fader and a 14-bit pitchbend fader (DEC-108, tempo range 1–300 BPM):
 *
 * * **CC:** one CC step is exactly one BPM over `TEMPO_MIN_BPM..TEMPO_MAX_BPM` = 60..187, so the motor fader has a
 *   detent per BPM; 128 steps cannot span 1–300 at 0.1.
 * * **Pitchbend (the tempo fader):** 16,384 wire values span **1..300 BPM at a 0.1 quantum** — 2,991 positions,
 *   5.5 wire values per position, monotonic with no gaps. 0.1 is the declared quantum because an external MIDI
 *   clock can also only be estimated to that resolution.
 *
 * Dependency-free so the host tests can drive the production conversions and the firmware and its tests cannot
 * drift.
 */
namespace TempoMapping {

// --- CC fader mapping (one BPM a step) -----------------------------------------------------------------------
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

// --- Pitchbend tempo fader: the whole enforced clock range at a 0.1 BPM quantum --------------------------------
constexpr float BPM_MIN = 1.0f;
constexpr float BPM_MAX = 300.0f;
constexpr float BPM_STEP = 0.1f;
/// Highest 14-bit pitchbend wire value.
constexpr uint16_t PITCHBEND_WIRE_MAX = 16383;
/// Positions in 1..300 inclusive at 0.1 BPM.
constexpr uint16_t BPM_STEP_COUNT = 2991;

/// 14-bit wire value (0..16383) -> the 0.1 BPM step index (0..2990).
inline uint16_t bpmStepFromPitchBendWire(uint16_t wire) {
    const uint32_t clamped = wire > PITCHBEND_WIRE_MAX ? PITCHBEND_WIRE_MAX : wire;
    return static_cast<uint16_t>((clamped * (BPM_STEP_COUNT - 1) + PITCHBEND_WIRE_MAX / 2) / PITCHBEND_WIRE_MAX);
}

/// 14-bit wire value -> BPM, in 0.1 steps.
inline float bpmFromPitchBendWire(uint16_t wire) {
    return BPM_MIN + static_cast<float>(bpmStepFromPitchBendWire(wire)) * BPM_STEP;
}

/// Logical (signed, centred) pitchbend value -> the 0.1 BPM step index.
inline uint16_t bpmStepFromPitchBendLogical(int16_t logical) {
    const int32_t wire = static_cast<int32_t>(logical) + 8192;
    const int32_t clamped = std::max<int32_t>(0, std::min<int32_t>(wire, PITCHBEND_WIRE_MAX));
    return bpmStepFromPitchBendWire(static_cast<uint16_t>(clamped));
}

/// Logical (signed) pitchbend value -> BPM, in 0.1 steps.
inline float bpmFromPitchBendLogical(int16_t logical) {
    return BPM_MIN + static_cast<float>(bpmStepFromPitchBendLogical(logical)) * BPM_STEP;
}

/// BPM -> the 0.1 BPM step index, clamped into the range.
inline uint16_t bpmStepFromBpm(float tempoBpm) {
    const float clamped = std::max(BPM_MIN, std::min(tempoBpm, BPM_MAX));
    long index = std::lround((clamped - BPM_MIN) / BPM_STEP);
    if (index < 0) {
        index = 0;
    }
    if (index > static_cast<long>(BPM_STEP_COUNT - 1)) {
        index = BPM_STEP_COUNT - 1;
    }
    return static_cast<uint16_t>(index);
}

/// BPM -> the pitchbend wire value that parks a motor fader at that tempo.
inline uint16_t pitchBendWireFromBpm(float tempoBpm) {
    const uint32_t index = bpmStepFromBpm(tempoBpm);
    return static_cast<uint16_t>((index * PITCHBEND_WIRE_MAX + (BPM_STEP_COUNT - 1) / 2) / (BPM_STEP_COUNT - 1));
}

/// BPM -> the logical (signed, centred) pitchbend value for the motor park.
inline int16_t pitchBendLogicalFromBpm(float tempoBpm) {
    return static_cast<int16_t>(static_cast<int32_t>(pitchBendWireFromBpm(tempoBpm)) - 8192);
}

}  // namespace TempoMapping

#endif  // TEMPO_MAPPING_H
