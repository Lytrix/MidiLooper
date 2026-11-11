//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// What the screen's status band shows: the rules
// behind its fields, kept apart from the drawing so they are tested on the host.
// * Save dots: busy (one dot runs, 200 ms a step) before pending
//   (all dim); a completed save flashes the dots bright for 800 ms, a failed one at mid brightness; else none.
// * Position: bar.beat.16th.tick, the bar 1-based and without leading zeros; a queued launch counts the time left
//   (every part 0-based).
// * Tempo in tenths of a BPM; the output port as one label; note names with C3 = 60 (Yamaha).
// No allocation.

#pragma once

#include <cstdint>

namespace ContentEngine {
namespace status {

enum class SaveDots : uint8_t { Off, Pending, Busy, Done, Failed };

class SaveIndicator {
 public:
  static constexpr uint32_t kFlashMs = 800, kRotateMs = 200;

  /// `writing`: a save or the surface state is being written; `unsaved`: a change waits to be saved; `saves` /
  /// `failed`: the persistence's counters (an increase is a completed / failed save).
  SaveDots update(uint32_t nowMs, bool writing, bool unsaved, uint32_t saves, uint32_t failed) {
    if (saves != saves_) {
      saves_ = saves;
      doneAt_ = nowMs;
      done_ = true;
    }
    if (failed != failed_) {
      failed_ = failed;
      failedAt_ = nowMs;
      failedSeen_ = true;
    }
    if (writing) return SaveDots::Busy;
    if (unsaved) return SaveDots::Pending;
    if (done_ && nowMs - doneAt_ < kFlashMs) return SaveDots::Done;
    if (failedSeen_ && nowMs - failedAt_ < kFlashMs) return SaveDots::Failed;
    return SaveDots::Off;
  }
  /// The counters at boot: no flash for what happened before the screen looked.
  void start(uint32_t saves, uint32_t failed) {
    saves_ = saves;
    failed_ = failed;
  }
  /// The running dot (0-3) while busy.
  static uint8_t step(uint32_t nowMs) { return static_cast<uint8_t>(nowMs / kRotateMs % 4); }

 private:
  uint32_t saves_ = 0, failed_ = 0, doneAt_ = 0, failedAt_ = 0;
  bool done_ = false, failedSeen_ = false;
};

struct Position {
  uint32_t bar;  // 1-based (a countdown: bars left, 0-based)
  uint8_t beat, sixteenth, tick;
};
/// A position in the loop: every part 1-based but the tick.
inline Position position(uint32_t ticks, uint32_t ticksPerBar, uint32_t ticksPerQuarter) {
  const uint32_t six = ticksPerQuarter / 4, inBar = ticks % ticksPerBar;
  return Position{ticks / ticksPerBar + 1, static_cast<uint8_t>(inBar / ticksPerQuarter + 1),
                  static_cast<uint8_t>(inBar % ticksPerQuarter / six + 1), static_cast<uint8_t>(inBar % six)};
}
/// Time left before a queued launch: every part 0-based.
inline Position countdown(uint32_t ticks, uint32_t ticksPerBar, uint32_t ticksPerQuarter) {
  const uint32_t six = ticksPerQuarter / 4, inBar = ticks % ticksPerBar;
  return Position{ticks / ticksPerBar, static_cast<uint8_t>(inBar / ticksPerQuarter),
                  static_cast<uint8_t>(inBar % ticksPerQuarter / six), static_cast<uint8_t>(inBar % six)};
}

/// The tempo in tenths of a BPM, rounded (`usPerTick` at `ticksPerQuarter` PPQN).
inline uint16_t tempoTenths(uint32_t usPerTick, uint32_t ticksPerQuarter) {
  const uint64_t d = static_cast<uint64_t>(usPerTick) * ticksPerQuarter;
  return d == 0 ? 0 : static_cast<uint16_t>((600000000ull + d / 2) / d);
}

/// The output port label for a port mask (bit 0 DIN, 1 USB device, 2 USB host).
inline const char* portLabel(uint8_t mask) {
  switch (mask & 7) {
    case 1: return "DIN1";
    case 2: return "USB1";
    case 4: return "HOST";
    case 7: return "ALL";
    case 0: return "OFF";
    default: return "MIX";
  }
}

/// `C3`, `F#2`, `C-2` (C3 = 60); `out` holds 5 bytes.
inline void noteName(uint8_t pitch, char* out) {
  static const char kNames[] = "C C#D D#E F F#G G#A A#B ";
  const char* n = kNames + (pitch % 12) * 2;
  const int octave = pitch / 12 - 2;
  uint8_t i = 0;
  out[i++] = n[0];
  if (n[1] == '#') out[i++] = '#';
  if (octave < 0) out[i++] = '-';
  out[i++] = static_cast<char>('0' + (octave < 0 ? -octave : octave));
  out[i] = 0;
}

}  // namespace status
}  // namespace ContentEngine
