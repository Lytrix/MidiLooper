//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// Short, double, triple and long presses from note on / off, per button (the control surface's buttons).

#pragma once

#include <cstdint>

namespace ContentEngine {

enum class Press : uint8_t { Short, Double, Triple, Long };

template <uint8_t N>
class PressDetector {
 public:
  struct Timing {
    uint16_t doubleMs = 300, tripleMs = 400, longMs = 600, debounceMs = 0;
    bool triple = false;  // the button has a triple action: a double waits for the triple window
  };

  void init() {
    for (uint8_t b = 0; b < N; ++b) {
      timing_[b] = Timing{};
      state_[b] = State{};
    }
    head_ = count_ = 0;
  }
  void configure(uint8_t b, Timing t) { timing_[b] = t; }

  void down(uint8_t b, uint32_t ms) {
    State& s = state_[b];
    if (s.pressed) {  // a second NoteOn: re-arm
      s.start = ms;
      return;
    }
    if (timing_[b].debounceMs != 0 && s.released && ms - s.lastRelease < timing_[b].debounceMs) return;
    s.pressed = true;
    s.start = ms;
  }
  void up(uint8_t b, uint32_t ms) {
    State& s = state_[b];
    if (!s.pressed) return;  // its press was never seen: no gesture
    s.pressed = false;
    s.released = true;
    s.lastRelease = ms;
    if (s.consumed) {  // used with another control: no gesture, and no tap for a double
      s.consumed = false;
      idle(s);
      return;
    }
    const Timing& t = timing_[b];
    if (ms - s.start >= t.longMs) {
      idle(s);
      push(b, Press::Long);
      return;
    }
    if (s.tap == Tap::PendingDouble && ms - s.secondTap <= t.tripleMs) {
      idle(s);
      push(b, Press::Triple);
    } else if (s.tapped && ms - s.lastTap <= t.doubleMs) {
      if (!t.triple) {
        idle(s);
        push(b, Press::Double);
      } else {
        s.secondTap = ms;
        s.tap = Tap::PendingDouble;
        s.expire = ms + t.tripleMs;
      }
    } else {
      s.tapped = true;
      s.lastTap = ms;
      s.tap = Tap::PendingShort;
      s.expire = ms + t.doubleMs;
    }
  }
  /// The next gesture due at `ms`, if any.
  bool next(uint32_t ms, uint8_t& b, Press& p) {
    for (uint8_t i = 0; i < N && count_ < kQueue; ++i) {
      State& s = state_[i];
      if (s.tap == Tap::Idle || static_cast<int32_t>(ms - s.expire) < 0) continue;
      const Press q = s.tap == Tap::PendingShort ? Press::Short : Press::Double;
      idle(s);
      push(i, q);
    }
    if (count_ == 0) return false;
    b = queue_[head_].b;
    p = queue_[head_].p;
    head_ = static_cast<uint8_t>((head_ + 1) % kQueue);
    --count_;
    return true;
  }
  bool pressed(uint8_t b) const { return state_[b].pressed; }
  /// How long `b` has been held at `ms` (0: not pressed).
  uint32_t heldMs(uint8_t b, uint32_t ms) const { return state_[b].pressed ? ms - state_[b].start : 0; }
  /// The held press of `b` was used with another control: its release is no gesture.
  void consume(uint8_t b) {
    if (state_[b].pressed) state_[b].consumed = true;
  }

 private:
  static constexpr uint8_t kQueue = 8;
  enum class Tap : uint8_t { Idle, PendingShort, PendingDouble };
  struct State {
    bool pressed = false, released = false, tapped = false, consumed = false;
    Tap tap = Tap::Idle;
    uint32_t start = 0, lastRelease = 0, lastTap = 0, secondTap = 0, expire = 0;
  };
  struct Event {
    uint8_t b;
    Press p;
  };
  static void idle(State& s) {
    s.tapped = false;
    s.tap = Tap::Idle;
  }
  void push(uint8_t b, Press p) {
    if (count_ == kQueue) return;  // a burst of more than 8 gestures in one pass: the rest are dropped
    queue_[(head_ + count_) % kQueue] = Event{b, p};
    ++count_;
  }

  Timing timing_[N];
  State state_[N];
  Event queue_[kQueue];
  uint8_t head_ = 0, count_ = 0;
};

}  // namespace ContentEngine
