//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// The engine's Clock (fixed-memory-engine E9.8a): which tick is due, from the internal tempo or an external MIDI clock.
//
// Ticks are the engine's (192 per quarter, `kTicksPerPulse` = 8 per MIDI clock pulse). The main loop asks `due` each
// pass and plays every tick it returns, in order, at most `kCatchUp` a pass (a long block is caught up over the
// next passes, never skipped).
// * Internal: one tick every `usPerTick` (from the tempo, BPM × 100) while running; `start` / `stop` from the surface.
// * External: a MIDI clock pulse at tick 8k makes ticks up to 8k due at once; between pulses ticks are spread over the
//   last measured pulse interval, but never past 8k + 7 before the next pulse arrives (the clock never runs ahead of
//   its source). Tempo: the mean of the last 24 intervals. MIDI Start: the next bar line, held there until the first
//   pulse (as the legacy `ClockManager` holds tick 0); Stop: no tick due; Continue: running from where it stopped.
// * Tick numbers only rise and a running clock never skips one (a late clock catches up `kCatchUp` ticks a pass): the
//   engine's tracks, jobs and queued gestures key on them. A start — `start` or MIDI Start — moves on to the next bar
//   line (`barTicks`) instead of going back to 0, so the bar grid is where the song starts; nothing plays meanwhile.
// * The source is the external clock while pulses arrive (a pulse within `kExternalTimeoutUs`); without them, internal.
// No allocation; O(1) a call.

#pragma once

#include <cstdint>

namespace ContentEngine {

class EngineClock {
 public:
  static constexpr uint32_t kTicksPerQuarter = 192;
  static constexpr uint32_t kTicksPerPulse = kTicksPerQuarter / 24;
  static constexpr uint32_t kExternalTimeoutUs = 500000;
  static constexpr uint8_t kCatchUp = 16;
  static constexpr uint8_t kWindow = 24;  // pulse intervals averaged for the tempo

  void init(uint32_t bpmX100, uint32_t barTicks = 768) {
    barTicks_ = barTicks == 0 ? 1 : barTicks;
    setTempo(bpmX100);
    running_ = false;
    external_ = false;
    next_ = 0;
    lastTickUs_ = 0;
    pulses_ = 0;
    lastPulseUs_ = 0;
    pulseLimit_ = 0;
    intervals_ = 0;
    intervalSum_ = 0;
    head_ = 0;
    holdForPulse_ = aligned_ = false;
    for (uint32_t& i : interval_) i = 0;
  }
  /// The internal tempo (BPM × 100, e.g. 12000).
  void setTempo(uint32_t bpmX100) {
    if (bpmX100 < 2000) bpmX100 = 2000;
    internalUsPerTick_ = static_cast<uint32_t>(6000000000ull / (static_cast<uint64_t>(bpmX100) * kTicksPerQuarter));
  }

  // ---- Surface (internal) -----------------------------------------------------------------------------------------
  void start(uint32_t nowUs) {
    if (external_) return;
    running_ = true;
    next_ = nextBar();
    lastTickUs_ = nowUs - internalUsPerTick_;  // the bar's first tick is due now
  }
  void stop() {
    if (!external_) running_ = false;
  }

  // ---- MIDI realtime (external) -------------------------------------------------------------------------------------
  void midiStart(uint32_t nowUs) {
    external_ = true;
    lastPulseUs_ = nowUs;
    running_ = true;
    next_ = nextBar();
    pulses_ = 0;
    pulseLimit_ = next_;
    holdForPulse_ = true;  // the bar's first tick at the first pulse
  }
  void midiStop(uint32_t nowUs) {
    external_ = true;
    lastPulseUs_ = nowUs;
    running_ = false;
  }
  void midiContinue(uint32_t nowUs) {
    external_ = true;
    lastPulseUs_ = nowUs;
    running_ = true;
  }
  void midiPulse(uint32_t nowUs) {
    if (external_ && pulses_ != 0) {
      const uint32_t dt = nowUs - lastPulseUs_;
      if (intervals_ == kWindow) intervalSum_ -= interval_[head_];
      else ++intervals_;
      interval_[head_] = dt;
      intervalSum_ += dt;
      head_ = static_cast<uint8_t>((head_ + 1) % kWindow);
    }
    if (!external_) {  // pulses without a Start: follow them from the tick due next (no tick is skipped)
      external_ = true;
      aligned_ = true;
      pulseLimit_ = next_;
      pulses_ = 0;
    }
    lastPulseUs_ = nowUs;
    ++pulses_;
    if (!running_) return;
    if (holdForPulse_) {
      holdForPulse_ = false;
    } else if (aligned_) {
      aligned_ = false;
    } else {
      pulseLimit_ += kTicksPerPulse;
    }
  }

  /// The next tick to play at `nowUs`, if one is due; `count` limits the catch-up within one pass.
  bool due(uint32_t nowUs, uint32_t& tick, uint8_t& count) {
    if (external_ && nowUs - lastPulseUs_ > kExternalTimeoutUs) {  // the source went away: internal, stopped
      external_ = false;
      running_ = false;
    }
    if (!running_ || count >= kCatchUp) return false;
    if (external_) {
      if (holdForPulse_) return false;
      // Due: up to the last pulse at once, then spread over the measured interval, never past the next pulse's tick.
      bool ok = next_ <= pulseLimit_;
      if (!ok && next_ < pulseLimit_ + kTicksPerPulse) {
        const uint32_t step = usPerTick();
        ok = step != 0 && nowUs - lastPulseUs_ >= (next_ - pulseLimit_) * step;
      }
      if (!ok) return false;
    } else {
      if (nowUs - lastTickUs_ < internalUsPerTick_) return false;
      lastTickUs_ += internalUsPerTick_;
      if (nowUs - lastTickUs_ > kCatchUp * internalUsPerTick_) lastTickUs_ = nowUs - kCatchUp * internalUsPerTick_;
    }
    tick = next_++;
    ++count;
    return true;
  }

  bool running() const { return running_; }
  bool external() const { return external_; }
  /// Microseconds per tick: the external clock's measured value, else the internal tempo's.
  uint32_t usPerTick() const {
    if (external_ && intervals_ != 0) return intervalSum_ / intervals_ / kTicksPerPulse;
    return internalUsPerTick_;
  }
  /// The tick the next `due` returns.
  uint32_t nextTick() const { return next_; }
  void setBar(uint32_t barTicks) { barTicks_ = barTicks == 0 ? 1 : barTicks; }

 private:
  /// The first tick of the next bar (this one, when nothing of it has played).
  uint32_t nextBar() const { return (next_ + barTicks_ - 1) / barTicks_ * barTicks_; }

  uint32_t internalUsPerTick_ = 0, barTicks_ = 768;
  bool running_ = false, external_ = false, holdForPulse_ = false, aligned_ = false;
  uint32_t next_ = 0, lastTickUs_ = 0;
  uint32_t pulses_ = 0, lastPulseUs_ = 0, pulseLimit_ = 0;
  uint32_t interval_[kWindow] = {};
  uint32_t intervalSum_ = 0;
  uint8_t intervals_ = 0, head_ = 0;
};

}  // namespace ContentEngine
