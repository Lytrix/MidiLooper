//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// Which channel messages may be limited (owner decision 2026-11-05). One rule for recording, playback and the ports:
//   * Essential: Bank Select (CC0 / CC32), Program Change, the switch controllers CC64-69 (sustain, portamento,
//     sostenuto, soft, legato, hold 2) and channel mode CC120-127 — state a note depends on: never limited.
//   * Parameter: RPN / NRPN selection and data entry (CC6, CC38, CC96-101) — an ordered sequence: never limited.
//   * Continuous: every other CC, pitch bend, channel pressure, poly pressure — a value stream where a later value
//     supersedes an earlier one: at most `Config::kMaxContinuousPerTick` per track per tick, recorded or played.

#pragma once

#include <cstdint>

namespace ContentEngine {

enum class ControlClass : uint8_t { Essential, Parameter, Continuous };

constexpr bool isEssentialControl(uint8_t status, uint8_t d1) {
  return (status & 0xF0) == 0xC0 ||
         ((status & 0xF0) == 0xB0 && (d1 == 0 || d1 == 32 || (d1 >= 64 && d1 <= 69) || d1 >= 120));
}
constexpr bool isParameterControl(uint8_t status, uint8_t d1) {
  return (status & 0xF0) == 0xB0 && (d1 == 6 || d1 == 38 || (d1 >= 96 && d1 <= 101));
}
/// For a channel message that is not a note.
constexpr ControlClass controlClass(uint8_t status, uint8_t d1) {
  return isEssentialControl(status, d1)   ? ControlClass::Essential
         : isParameterControl(status, d1) ? ControlClass::Parameter
                                          : ControlClass::Continuous;
}

}  // namespace ContentEngine
