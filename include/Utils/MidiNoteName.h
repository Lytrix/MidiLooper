//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

/**
 * @file MidiNoteName.h
 * @brief The one place the MIDI trace lines' note arithmetic lives: pitch-class name plus the
 *        **middle-C = C4** octave convention (`octave = note / 12 - 1`).
 *
 * Why a header and not a local table: two hot-ish paths printed the same pair
 * (`Track::sendMidiEvent` on the playback hot path, `MidiHandler::handleMidiMessage` on the input
 * path) and each built its own 12-entry pointer table on the stack for every note. The table build is
 * the cost the timing-path review named (item 4): it ran for **every played note** even when the
 * `LOG_TRACE` / `LOG_DEBUG` line that consumes it was filtered out, so the gate now asks the logger
 * first (`Logger::isEnabled`) and this header is what the surviving call sites share.
 *
 * Dependency-free on purpose, so a native suite can pin the arithmetic without the firmware.
 *
 * The chain is `if`/`else`, not a table and not a `switch`: a function-local `const char* [12]` costs
 * 48 B of `.rodata` per translation unit and a `switch` over 12 cases emits a jump table into
 * `.data` -- and this tree's RAM1 rule records both as ways to grow a region that is checked
 * byte-for-byte (`scripts/ram1_budget.json`; the S3.1 and S3.5 instances).
 */

#pragma once

#include <cstdint>

namespace MidiNoteName {

/// Pitch-class name for `note % 12`, in the order the trace lines have always printed it.
inline const char* pitchClassName(uint8_t note) {
  const uint8_t pitchClass = static_cast<uint8_t>(note % 12u);
  if (pitchClass == 0u) return "C";
  if (pitchClass == 1u) return "C#";
  if (pitchClass == 2u) return "D";
  if (pitchClass == 3u) return "D#";
  if (pitchClass == 4u) return "E";
  if (pitchClass == 5u) return "F";
  if (pitchClass == 6u) return "F#";
  if (pitchClass == 7u) return "G";
  if (pitchClass == 8u) return "G#";
  if (pitchClass == 9u) return "A";
  if (pitchClass == 10u) return "A#";
  return "B";
}

/// Scientific-pitch octave for a MIDI note: note 60 (`C4`) is `3 * 12 + 0`, so the octave is
/// `note / 12 - 1` and the lowest note (0) is `C-1`.
inline int octave(uint8_t note) { return static_cast<int>(note / 12u) - 1; }

}  // namespace MidiNoteName
