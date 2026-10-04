// Copyright (c) 2026 Lytrix (Eelke Jager)
// Licensed under the PolyForm Noncommercial 1.0.0
#pragma once
#include <cstdint>

/// ClockManager's tick identity, independent of loop phase. Zero epoch is unqualified.
struct PlaybackDeadlineClock {
  uint64_t tick = 0;
  uint32_t epoch = 0;
};

/// Arithmetic state owned solely by ClockManager. Callers serialize with tick writes.
/// Explicit rebases invalidate promises, including same-value and forward assignments.
class PlaybackDeadlineClockState {
 public:
  PlaybackDeadlineClock stamp() const { return {tick_, epoch_}; }
  void advance(uint32_t amount) {
    if (tick_ > UINT64_MAX - amount) {
      epoch_ = 0;  // Never recycle an identity after exhaustion.
      return;
    }
    tick_ += amount;
  }
  void invalidate() {
    if (epoch_ != 0) epoch_ = epoch_ == UINT32_MAX ? 0 : epoch_ + 1;
  }
  void rebase(uint32_t tick) { invalidate(); tick_ = tick; }
 private:
  uint64_t tick_ = 0;
  uint32_t epoch_ = 1;
};
