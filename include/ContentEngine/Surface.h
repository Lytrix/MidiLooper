//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// The control surface on the engine (fixed-memory-engine E9.8b-1; owner 2026-11-06: performance controls first, the
// legacy gesture map kept exactly so the DROID ini stays unchanged).
//
// Input: the DROID control plane, MIDI channels 13–16 (`controlPlane`: never recorded, never thru). Buttons are notes
// on channel 16 (`MidiConfig`): record 36, track switch 37, transport 39, play / stop 40, loop slots 50–57, tracks
// 60–67. Edit mode 38, length 35, the extended edit buttons 41–48, bar / 16th steps and the faders are the NOTE_EDIT
// surface and jam (E9.8b-2, E9.4c): read and ignored here.
// * Presses (`PressDetector`, the legacy `MidiButtonProcessor`): long when held ≥ the long time (on release); else a
//   tap — a second tap within the double window is a double (at once, or after the triple window when the button has a
//   triple action), a third within the triple window a triple; a lone tap is a short once the double window passes.
//   Per-button debounce after a release. A release without its press is dropped.
// * Gestures, on the selected track `t` and its selected slot (engine slot t × 8 + s), as `MidiButtonActions`:
//   - record 36: short = record / overdub toggle (an armed slot punches in; a take ends — Record at the bar, overdub at
//     once; an empty slot arms; a playing track overdubs; a stopped one plays the slot); double = undo; triple = redo;
//     long = clear;
//   - slot 50–57: short = the slot's toggle (selects it; during a take on another slot: the take ends, then the slot
//     records (empty) or plays (filled) at the loop end; an empty slot arms Record — at the playing slot's loop start or
//     the bar — and a second press punches in; the playing slot toggles the track's mute; another filled slot plays
//     from the loop end, or at once on the grid when the track is stopped); double = overdub the slot (when it plays,
//     else the short gesture); triple = redo; long = clear;
//   - track switch 37: short = the next track; double = undo; triple = redo; long = mute toggle;
//   - transport 39: short = start / stop (the internal clock; an external clock runs the transport itself); double =
//     back to the loop start (stop, then start at the next bar line);
//   - play / stop 40: short = the selected track stops, or plays its selected slot;
//   - tracks 60–67: short = select; double = mute toggle; long = solo toggle.
//   A start on a stopped transport starts the internal clock first, and the gesture then acts on the clock's first
//   tick (a Record or a play starts on that bar line, not one bar later).
// * LEDs (channel 15 notes, the legacy `MidiLedManager` / `TrackManagerMidiLedFeedback` velocities), for the selected
//   track's shown slot: 16th content 0–15 (64) and position 16–31 (127) of the current bar, transport 39 (127), bars
//   40–47 (current 127, with notes 64, in the loop 32), loop slots 50–57 (taking or armed 96, queued switch blinking
//   96 / 64, selected 127, playing muted 16, playing 48, filled 32, empty selected 40), tracks 60–67 (selected 127,
//   with content 32). `leds` sends only changes, at most `max` a call.
// No allocation.

#pragma once

#include <cstdint>

#include "ContentEngine/EngineClock.h"

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

 private:
  static constexpr uint8_t kQueue = 8;
  enum class Tap : uint8_t { Idle, PendingShort, PendingDouble };
  struct State {
    bool pressed = false, released = false, tapped = false;
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

template <typename Engine>
class Surface {
 public:
  // Channel 16 button notes (MidiConfig) and their detector indices.
  static constexpr uint8_t kRecordNote = 36, kTrackSwitchNote = 37, kTransportNote = 39, kPlayStopNote = 40;
  static constexpr uint8_t kSlotBase = 50, kTrackBase = 60, kLedChannel = 14;  // channel 15 (0-based 14)
  enum Button : uint8_t { kRecord, kTrackSwitch, kTransport, kPlayStop, kSlot0, kTrack0 = kSlot0 + 8, kButtons = kTrack0 + 8 };
  static constexpr uint8_t kTracks = 8, kSlotsPerTrack = 8;

  void init(Engine& e, EngineClock& clock) {
    e_ = &e;
    clock_ = &clock;
    press_.init();
    using T = typename PressDetector<kButtons>::Timing;
    press_.configure(kRecord, T{300, 400, 600, 0, true});
    press_.configure(kTrackSwitch, T{300, 400, 600, 0, true});
    press_.configure(kTransport, T{300, 400, 3000, 50, false});
    press_.configure(kPlayStop, T{400, 400, 3000, 50, false});
    for (uint8_t s = 0; s < kSlotsPerTrack; ++s) press_.configure(kSlot0 + s, T{300, 400, 600, 0, true});
    for (uint8_t t = 0; t < kTracks; ++t) press_.configure(kTrack0 + t, T{300, 400, 600, 0, false});
    for (uint8_t& s : slot_) s = 0;
    for (uint8_t& v : sent_) v = 0xFF;
    blinkMs_ = 0;
  }
  /// The DROID control plane: channels 13–16 are the surface's, never recorded nor thru.
  static bool controlPlane(uint8_t status) { return (status & 0xF0) < 0xF0 && (status & 0x0F) >= 12; }

  /// A control-plane message at `ms`.
  void input(uint8_t status, uint8_t d1, uint8_t d2, uint32_t ms) {
    const uint8_t type = status & 0xF0, channel = status & 0x0F;
    if (channel != 15 || (type != 0x90 && type != 0x80)) return;  // faders, other planes: E9.8b-2
    const uint8_t b = buttonOf(d1);
    if (b == 0xFF) return;
    if (type == 0x90 && d2 != 0)
      press_.down(b, ms);
    else
      press_.up(b, ms);
  }

  /// Once per main-loop pass: the gestures due, at the last played tick.
  void service(uint32_t ms, uint32_t lastTick, uint32_t nowUs) {
    tick_ = lastTick;
    us_ = nowUs;
    nowMs_ = ms;
    uint8_t b;
    Press p;
    while (press_.next(ms, b, p)) dispatch(b, p);
  }

  /// The engine slot a track has selected (its slot button's), for the screen.
  uint8_t selectedSlot(uint8_t t) const { return static_cast<uint8_t>(t * kSlotsPerTrack + slot_[t]); }
  void selectSlot(uint8_t t, uint8_t s) { slot_[t] = s; }

  /// The LED changes (channel 15 notes), at most `max`: `send(note, velocity)`, velocity 0 = off.
  template <typename Send>
  uint8_t leds(uint32_t lastTick, uint32_t ms, Send send, uint8_t max) {
    uint8_t want[128] = {};
    desired(lastTick, ms, want);
    uint8_t n = 0;
    for (uint8_t note = 0; note < 128 && n < max; ++note) {
      if (sent_[note] == want[note]) continue;
      if (sent_[note] == 0xFF && want[note] == 0) {  // never lit: nothing to clear
        sent_[note] = 0;
        continue;
      }
      send(note, want[note]);
      sent_[note] = want[note];
      ++n;
    }
    return n;
  }

 private:
  using X = typename Engine::Transport;
  static constexpr uint32_t kBar = 768, kSixteenth = kBar / 16;

  static uint8_t buttonOf(uint8_t note) {
    if (note == kRecordNote) return kRecord;
    if (note == kTrackSwitchNote) return kTrackSwitch;
    if (note == kTransportNote) return kTransport;
    if (note == kPlayStopNote) return kPlayStop;
    if (note >= kSlotBase && note < kSlotBase + kSlotsPerTrack) return static_cast<uint8_t>(kSlot0 + note - kSlotBase);
    if (note >= kTrackBase && note < kTrackBase + kTracks) return static_cast<uint8_t>(kTrack0 + note - kTrackBase);
    return 0xFF;
  }

  auto& control() { return e_->control(); }
  auto& transport() { return e_->transport(); }
  uint32_t us() const { return clock_->usPerTick(); }
  /// No content: no state, or a cleared one (Clear is undoable: an empty state).
  bool empty(uint8_t slot) { return e_->transport().contentOf(slot) == kNoManifest; }
  uint8_t selected() { return control().selectedTrack(); }

  void startClock() {
    if (clock_->running() || clock_->external()) return;  // an external clock runs the transport itself
    clock_->start(us_);
    control().transportStart(clock_->nextTick(), us_, us());
    tick_ = clock_->nextTick() - 1;  // the gestures that follow are due on the first tick
  }
  void stopClock() {
    if (clock_->external()) return;
    clock_->stop();
    control().transportStop(tick_, us_, us());
  }
  /// The take on track `t` ends: a Record at the bar, an overdub at once.
  void endTake(uint8_t t) {
    if (transport().mode() == X::Mode::Recording)
      control().queueStop(t, tick_);
    else
      control().stopNow(tick_, us_, us());
  }
  void play(uint8_t t, uint8_t slot, typename Engine::Control::Quantize q) {
    startClock();
    control().queueSwitch(t, slot, q, tick_);
  }

  void dispatch(uint8_t b, Press p) {
    const uint8_t t = selected();
    const uint8_t slot = selectedSlot(t);
    switch (b) {
      case kRecord:
        if (p == Press::Short) toggleRecord(t);
        else if (p == Press::Double) control().undo(slot, tick_, us_, us());
        else if (p == Press::Triple) control().redo(slot, tick_, us_, us());
        else control().clear(slot, tick_, us_, us());
        return;
      case kTrackSwitch:
        if (p == Press::Short) control().selectTrack(static_cast<uint8_t>((t + 1) % kTracks));
        else if (p == Press::Double) control().undo(slot, tick_, us_, us());
        else if (p == Press::Triple) control().redo(slot, tick_, us_, us());
        else control().setMute(t, !control().muted(t), us_);
        return;
      case kTransport:
        if (p == Press::Short) {
          if (clock_->running()) stopClock();
          else startClock();
        } else if (p == Press::Double) {  // back to the loop start: the next bar line
          stopClock();
          startClock();
        }
        return;
      case kPlayStop:
        if (p != Press::Short) return;  // double: the set browser, long: the window (later slices)
        if (transport().playing(t)) control().stopTrack(t, tick_, us_, us());
        else if (!empty(slot)) play(t, slot, Engine::Control::Quantize::NextGrid);
        return;
      default:
        break;
    }
    if (b >= kSlot0 && b < kSlot0 + kSlotsPerTrack) {
      const uint8_t s = static_cast<uint8_t>(b - kSlot0);
      const uint8_t target = static_cast<uint8_t>(t * kSlotsPerTrack + s);
      if (p == Press::Short) {
        slotToggle(t, s);
      } else if (p == Press::Double) {
        if (!empty(target) && transport().playing(t) && transport().playingSlot(t) == target &&
            transport().writingTrack() == 0xFF) {
          slot_[t] = s;
          control().startOverdub(t, tick_);
        } else {
          slotToggle(t, s);
        }
      } else if (p == Press::Triple) {
        slot_[t] = s;
        control().redo(target, tick_, us_, us());
      } else {
        slot_[t] = s;
        control().clear(target, tick_, us_, us());
      }
      return;
    }
    if (b >= kTrack0 && b < kTrack0 + kTracks) {
      const uint8_t tt = static_cast<uint8_t>(b - kTrack0);
      if (p == Press::Short) control().selectTrack(tt);
      else if (p == Press::Double) control().setMute(tt, !control().muted(tt), us_);
      else if (p == Press::Long) control().setSolo(tt, !control().soloed(tt), us_);
    }
  }

  void toggleRecord(uint8_t t) {
    if (control().recordQueued(t)) {  // armed: punch in
      control().punchIn(t, tick_, us_, us());
      return;
    }
    if (transport().writingTrack() == t) {
      endTake(t);
      return;
    }
    const uint8_t slot = selectedSlot(t);
    if (empty(slot)) {
      slotToggle(t, slot_[t]);
      return;
    }
    if (transport().playing(t)) {
      control().startOverdub(t, tick_);
      return;
    }
    play(t, slot, Engine::Control::Quantize::NextGrid);
  }

  void slotToggle(uint8_t t, uint8_t s) {
    const uint8_t slot = static_cast<uint8_t>(t * kSlotsPerTrack + s);
    slot_[t] = s;
    const auto& x = transport();
    if (x.writingTrack() == t && x.writingSlot() != slot) {  // a take on another slot ends first
      endTake(t);
      if (empty(slot)) control().queueRecord(t, slot, tick_);
      else control().queueSwitch(t, slot, Engine::Control::Quantize::LoopEnd, tick_);
      return;
    }
    const uint8_t armed = control().queuedRecordSlot(t);
    if (armed != 0xFF && armed != slot) control().cancelQueued(t);
    if (x.writingTrack() == t) {
      endTake(t);
      return;
    }
    if (empty(slot)) {
      if (control().queuedRecordSlot(t) == slot) {
        control().punchIn(t, tick_, us_, us());
      } else {
        startClock();
        control().queueRecord(t, slot, tick_);
      }
      return;
    }
    if (x.playing(t)) {
      if (x.playingSlot(t) == slot && control().queuedSwitchSlot(t) == 0xFF)
        control().setMute(t, !control().muted(t), us_);
      else
        control().queueSwitch(t, slot, Engine::Control::Quantize::LoopEnd, tick_);
      return;
    }
    play(t, slot, Engine::Control::Quantize::NextGrid);
  }

  // ---- LEDs -----------------------------------------------------------------------------------------------------
  void desired(uint32_t lastTick, uint32_t ms, uint8_t* want) {
    auto& x = transport();
    auto& c = control();
    const uint8_t t = c.selectedTrack();
    for (uint8_t i = 0; i < kTracks; ++i) {
      bool content = false;
      for (uint8_t s = 0; s < kSlotsPerTrack && !content; ++s) content = !empty(static_cast<uint8_t>(i * kSlotsPerTrack + s));
      want[kTrackBase + i] = i == t ? 127 : content ? 32 : 0;
    }
    if (clock_->running()) want[kTransportNote] = 127;
    const bool blinkBright = (ms / 250) % 2 == 0;
    for (uint8_t s = 0; s < kSlotsPerTrack; ++s) {
      const uint8_t slot = static_cast<uint8_t>(t * kSlotsPerTrack + s);
      uint8_t v;
      if (x.writingSlot() == slot || c.queuedRecordSlot(t) == slot) {
        v = 96;
      } else if (c.queuedSwitchSlot(t) == slot) {
        v = blinkBright ? 96 : 64;
      } else if (s == slot_[t]) {
        v = 127;
      } else if (empty(slot)) {
        v = 0;
      } else if (x.playing(t) && x.playingSlot(t) == slot) {
        v = c.muted(t) ? 16 : 48;
      } else {
        v = 32;
      }
      want[kSlotBase + s] = v;
    }
    // The shown slot's bar, steps and position.
    uint8_t slot = selectedSlot(t);
    if (x.writingTrack() == t) slot = x.writingSlot();
    else if (x.playing(t)) slot = x.playingSlot(t);
    const ManifestId m = x.contentOf(slot);
    uint32_t loop = m == kNoManifest ? 0 : e_->manifests().manifest(m).loopLengthTicks;
    uint32_t pos = 0;
    bool moving = false;
    if (x.writingSlot() == slot) {
      pos = x.writePosition(lastTick);
      moving = true;
      if (loop == 0 || loop > 256 * kBar) loop = (pos / kBar + 1) * kBar;
    } else if (x.playing(t) && x.playingSlot(t) == slot && loop != 0) {
      pos = (lastTick - x.origin(t)) % loop;
      moving = true;
    }
    if (loop == 0) return;
    const uint32_t bars = (loop + kBar - 1) / kBar;
    const uint32_t bar = pos / kBar;
    for (uint8_t i = 0; i < 8 && i < bars; ++i) {
      const auto* d = e_->tiles().densityTile(slot, i);
      bool notes = false;
      for (uint8_t k = 0; d != nullptr && k < 64 && !notes; ++k) notes = d->counts[k] != 0;
      want[40 + i] = moving && bar == i ? 127 : notes ? 64 : 32;
    }
    if (moving) want[16 + (pos % kBar) / kSixteenth] = 127;
    if (bar >= 64) return;
    const auto src = e_->tiles().drawSource(slot, static_cast<uint8_t>(bar));
    using Kind = typename Engine::Tiles::Kind;
    if (src.kind == Kind::Span) {
      for (uint16_t i = 0; i < src.header->count; ++i) want[(src.span->segments[i].start % kBar) / kSixteenth] = 64;
    } else if (src.kind == Kind::Dense) {
      for (uint8_t col = 0; col < 32; ++col)
        for (uint8_t k = 0; k < 16; ++k)
          if (src.span->raster[col][k] != 0) want[col / 2] = 64;
    }
  }

  Engine* e_ = nullptr;
  EngineClock* clock_ = nullptr;
  PressDetector<kButtons> press_;
  uint8_t slot_[kTracks] = {};
  uint8_t sent_[128] = {};
  uint32_t tick_ = 0, us_ = 0, nowMs_ = 0, blinkMs_ = 0;
};

}  // namespace ContentEngine
