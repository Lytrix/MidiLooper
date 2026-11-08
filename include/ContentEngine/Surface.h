//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// The control surface on the engine (fixed-memory-engine E9.8b-1; owner 2026-11-06: performance controls first, the
// legacy gesture map kept exactly so the DROID ini stays unchanged).
//
// Input: the DROID control plane, MIDI channels 13–16 (`controlPlane`: never recorded, never thru). Buttons are notes
// on channel 16 (`MidiConfig`): record 36, track switch 37, transport 39, play / stop 40, loop slots 50–57, tracks
// 60–67; NOTE_EDIT (E9.8b-2, `EditSurface`): edit mode 38 (short: NOTE_EDIT on the shown slot ↔ LOOP_EDIT, from
// performance LOOP_EDIT when the slot is empty; long: leave; owner 2026-11-08; LOOP_EDIT's F4 is the tempo, pitch
// bend on channel 15, its motor parked at the tempo on entry; F2 the loop length, 1–128 bars, CC 101 on channel 16 (or
// pitch bend on channel 14), applied at rest as a play window, DEC-257, its motor CC 101; F1 the loop start in 16ths,
// pitch bend on channel 16, applied at rest by rotating the content, counted from LOOP_EDIT's entry), NOTELEN
// 35 (short: start ↔ end, double: delete / create, held: F4 is the velocity), F1 pitch bend channel 16, F2 pitch
// bend channel 14, F3 / F4 CC 2 / CC 3 channel 15; while editing the record button's double / triple are the session's undo / redo. The extended edit
// buttons 41–48, LOOP_EDIT and bar / 16th steps are later slices: read and ignored here.
// * Presses (`PressDetector`, the legacy `MidiButtonProcessor`): long when held ≥ the long time (on release); else a
//   tap — a second tap within the double window is a double (at once, or after the triple window when the button has a
//   triple action), a third within the triple window a triple; a lone tap is a short once the double window passes.
//   Per-button debounce after a release. A release without its press is dropped.
// * Gestures, on the selected track `t` and its selected slot (engine slot t × 8 + s), as `MidiButtonActions`:
//   - record 36: short = record / overdub toggle (armed: on a stopped transport it starts the transport and the take
//     with it, on a running one the arm is cancelled — legacy `handleArmedRecordPress`; a take ends — Record at the
//     bar, overdub at once; an empty slot arms; a playing track overdubs; a stopped one plays the slot); double = undo;
//     triple = redo; long = clear;
//   - slot 50–57: short = the slot's toggle (selects it; during a take on another slot: the take ends, then the slot
//     records (empty) or plays (filled) at the loop end; an empty slot arms Record — at the playing slot's loop start,
//     or the bar on a running transport, or waiting for the transport — and a second press punches in on a playing
//     track, starts a stopped transport, or cancels the arm on a running one; the playing slot toggles the track's
//     mute; another filled slot plays from the loop end, or at once on the grid when the track is stopped); double =
//     clear (owner 2026-11-08; overdub stays on record's short press); triple = redo; long = on an empty slot an empty
//     loop of `kNewLoopBars` bars (owner 2026-11-08: for step entry in NOTE_EDIT; selected, not played);
//   - track switch 37: short = the next track; double = undo; triple = redo; long = mute toggle;
//   - transport 39: short = start / stop (the internal clock; an external clock runs the transport itself); double =
//     back to the loop start (stop, then start at the next bar line);
//   - play / stop 40: short = the selected track stops, or plays its selected slot;
//   - tracks 60–67: short = select; double = mute toggle; long = solo toggle.
//   A play on a stopped transport starts the internal clock first, and the gesture then acts on the clock's first
//   tick (a play starts on that bar line, not one bar later); a Record only arms (owner 2026-11-06: as legacy). Every
//   transport start (`transportStarted`, MIDI Start too) re-queues the armed Records on its first tick and starts
//   every track that is not muted and not playing on its selected slot, when that slot has content (owner
//   2026-11-06: a MIDI Start plays all tracks).
// * LEDs (channel 15 notes, the legacy `MidiLedManager` / `TrackManagerMidiLedFeedback` velocities), for the selected
//   track's shown slot: 16th content 0–15 (64) and position 16–31 (127) of the current bar, transport 39 (127), bars
//   40–47 (current 127, with notes 64, in the loop 32), loop slots 50–57 (taking or armed 96, queued switch blinking
//   96 / 64, selected 127, playing muted 16, playing 48, filled 32, empty selected 40), tracks 60–67 (selected 127,
//   with content 32). `leds` sends only changes, at most `max` a call.
// No allocation.

#pragma once

#include <cstdint>

#include "ContentEngine/EditSurface.h"
#include "ContentEngine/EngineClock.h"
#include "Utils/TempoMapping.h"

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

template <typename Engine>
class Surface {
 public:
  // Channel 16 button notes (MidiConfig) and their detector indices.
  static constexpr uint8_t kRecordNote = 36, kTrackSwitchNote = 37, kTransportNote = 39, kPlayStopNote = 40;
  static constexpr uint8_t kLengthNote = 35, kEditModeNote = 38;
  static constexpr uint32_t kVelocityHoldMs = 600;
  static constexpr uint32_t kNewLoopBars = 1;  // an empty loop's length (owner 2026-11-08: a global setting later)  // NOTELEN held this long: velocity mode (the long press)
  static constexpr uint8_t kSlotBase = 50, kTrackBase = 60, kLedChannel = 14;  // channel 15 (0-based 14)
  /// The DROID's fader bank: Program Change on channel 16 (`_EDIT_NOTE_STATE`; legacy `sendEditSessionMidi`).
  static constexpr uint8_t kProgramChannel = 15;  // channel 16 (0-based 15)
  static constexpr uint8_t kLoopEditBank = 0, kNoteEditBank = 1;
  static constexpr uint8_t kTempoChannel = 14;  // LOOP_EDIT's F4: pitch bend on channel 15 (legacy)
  static constexpr uint8_t kLengthCc = 101;      // LOOP_EDIT's F2 motor: CC 101 on channel 16 (legacy)
  static constexpr uint32_t kLengthSettleMs = 300;
  enum Button : uint8_t {
    kRecord,
    kTrackSwitch,
    kTransport,
    kPlayStop,
    kLength,
    kEditMode,
    kSlot0,
    kTrack0 = kSlot0 + 8,
    kButtons = kTrack0 + 8
  };
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
    press_.configure(kLength, T{300, 400, 600, 0, false});
    press_.configure(kEditMode, T{300, 400, 600, 0, false});
    edit_.init(e);
    for (uint8_t s = 0; s < kSlotsPerTrack; ++s) press_.configure(kSlot0 + s, T{300, 400, 600, 0, true});
    for (uint8_t t = 0; t < kTracks; ++t) press_.configure(kTrack0 + t, T{300, 400, 600, 0, false});
    for (uint8_t& s : slot_) s = 0;
    for (uint8_t& v : sent_) v = 0xFF;
    bankSent_ = 0xFF;
    blinkMs_ = 0;
    loopEdit_ = tempoPending_ = lengthMotorPending_ = false;
    tempoWrittenMs_ = lengthWrittenMs_ = pendingMs_ = 0;
    pendingBars_ = 0;
    startMotor_ = 0;
    startWrittenMs_ = startGateMs_ = startMs_ = pendingStart_ = appliedStart_ = 0;
    startPending_ = false;
  }
  /// The DROID control plane: channels 13–16 are the surface's, never recorded nor thru.
  static bool controlPlane(uint8_t status) { return (status & 0xF0) < 0xF0 && (status & 0x0F) >= 12; }

  /// A control-plane message at `ms`.
  void input(uint8_t status, uint8_t d1, uint8_t d2, uint32_t ms) {
    const uint8_t type = status & 0xF0, channel = status & 0x0F;
    if (type == 0xE0) {  // pitch bend: F1 select (channel 16), F2 coarse (channel 14); LOOP_EDIT: F4 tempo (channel 15)
      const int16_t pb = static_cast<int16_t>(static_cast<int32_t>(d1 | (d2 << 7)) - 8192);
      if (channel == 15 && loopEdit_) startFader(pb, ms);
      else if (channel == 14 && loopEdit_) tempo(pb, ms);
      else if (channel == 13 && loopEdit_) lengthFader(pb, ms);
      else if (channel == 15) edit_.select(pb, ms);
      else if (channel == 13) edit_.coarse(pb, ms);
      return;
    }
    if (type == 0xB0 && channel == 15 && d1 == kLengthCc && loopEdit_) {  // LOOP_EDIT's F2: CC 101, bars − 1
      lengthBars(static_cast<uint16_t>((d2 & 0x7F) + 1), ms);
      return;
    }
    if (type == 0xB0 && channel == 14) {  // F3 fine CC 2, F4 pitch CC 3 (channel 15)
      if (d1 == 2) {
        edit_.fine(d2, ms);
      } else if (d1 == 3 && edit_.editing() && press_.pressed(kLength)) {  // NOTELEN held: F4 is the velocity
        press_.consume(kLength);
        edit_.velocityBegin(ms, false);  // the hand is on F4: no motor
        edit_.velocity(d2, ms);
      } else if (d1 == 3) {
        edit_.pitch(d2, ms);
      }
      return;
    }
    if (channel != 15 || (type != 0x90 && type != 0x80)) return;
    const uint8_t b = buttonOf(d1);
    if (b == 0xFF) return;
    if (type == 0x90 && d2 != 0) {
      press_.down(b, ms);
    } else {
      press_.up(b, ms);
      // A long press — velocity mode, F4 not moved by hand — creates a note (owner 2026-11-08).
      if (b == kLength && edit_.velocityDone(ms)) edit_.create(ms);
    }
  }

  /// Once per main-loop pass: the gestures due, at the last played tick.
  void service(uint32_t ms, uint32_t lastTick, uint32_t nowUs) {
    tick_ = lastTick;
    us_ = nowUs;
    nowMs_ = ms;
    if (edit_.editing() && !edit_.velocityMode() && press_.heldMs(kLength, ms) >= kVelocityHoldMs) {
      press_.consume(kLength);  // a long hold: velocity mode, F4's motor to the velocity
      edit_.velocityBegin(ms, true);
    }
    uint8_t b;
    Press p;
    while (press_.next(ms, b, p)) dispatch(b, p);
    edit_.service(ms);
    settleLength(ms);
    settleStart(ms);
  }
  const EditSurface<Engine>& edit() const { return edit_; }
  /// LOOP_EDIT is open (the edit button's cycle).
  bool loopEdit() const { return loopEdit_; }
  /// LOOP_EDIT's F2 moved, not yet applied: the length it will take, in bars (0: none).
  uint16_t pendingLoopBars() const { return pendingBars_; }
  /// LOOP_EDIT's loop start, in ticks from the content as LOOP_EDIT found it (F1; pending while it settles).
  uint32_t loopStart() const { return startPending_ ? pendingStart_ : appliedStart_; }
  /// The next motor-fader message (NOTE_EDIT feedback), sent like the LEDs.
  /// The next motor message: NOTE_EDIT's, else LOOP_EDIT's tempo fader parked at the tempo on entry (legacy
  /// `sendCurrentTempoPitchBend`: the value alone).
  bool motor(uint32_t ms, typename EditSurface<Engine>::MotorMsg& m) {
    if (edit_.motor(ms, m)) return true;
    if (startMotor_ == 1) {  // legacy `sendCurrentLoopStartPitchbend`: pitch bend, then trigger note 0 (channel 16)
      startMotor_ = 2;
      startWrittenMs_ = ms;
      m = {static_cast<uint8_t>(0xE0 | kProgramChannel), 0, 0};  // the loop start: the fader's bottom
      return true;
    }
    if (startMotor_ == 2) {
      startMotor_ = 3;
      startGateMs_ = ms + EditSurface<Engine>::kMotorGateMs;
      m = {static_cast<uint8_t>(0x90 | kProgramChannel), 0, 127};
      return true;
    }
    if (startMotor_ == 3 && static_cast<int32_t>(ms - startGateMs_) >= 0) {
      startMotor_ = 0;
      m = {static_cast<uint8_t>(0x80 | kProgramChannel), 0, 0};
      return true;
    }
    if (lengthMotorPending_) {  // legacy `sendCurrentLoopLengthCC`: CC 101 on channel 16, bars − 1
      lengthMotorPending_ = false;
      lengthWrittenMs_ = ms;
      const ManifestId c = transport().contentOf(shownSlot(selected()));
      const uint32_t bars = c == kNoManifest ? 1 : e_->manifests().manifest(c).loopLengthTicks / EditSurface<Engine>::kBar;
      m = {static_cast<uint8_t>(0xB0 | kProgramChannel), kLengthCc,
           static_cast<uint8_t>(bars == 0 ? 0 : bars > 128 ? 127 : bars - 1)};
      return true;
    }
    if (!tempoPending_) return false;
    tempoPending_ = false;
    tempoWrittenMs_ = ms;
    const uint16_t wire = TempoMapping::pitchBendWireFromBpm(static_cast<float>(tempoTenths()) / 10.0f);
    m = {static_cast<uint8_t>(0xE0 | kTempoChannel), static_cast<uint8_t>(wire & 0x7F), static_cast<uint8_t>(wire >> 7)};
    return true;
  }

  /// The transport started with `firstTick` as its first tick: armed Records start there (not a bar later).
  void transportStarted(uint32_t firstTick) {
    auto& x = transport();
    for (uint8_t t = 0; t < kTracks; ++t) {
      const uint8_t armed = control().queuedRecordSlot(t);
      if (armed != 0xFF) {
        control().queueRecord(t, armed, firstTick - 1);
        continue;
      }
      // Every track not muted and not playing starts its selected slot on the first tick.
      const uint8_t slot = selectedSlot(t);
      if (x.playing(t) || x.writingTrack() == t || control().muted(t) || empty(slot)) continue;
      control().queueSwitch(t, slot, Engine::Control::Quantize::NextGrid, firstTick - 1);
    }
  }

  /// The engine slot a track has selected (its slot button's), for the screen.
  uint8_t selectedSlot(uint8_t t) const { return static_cast<uint8_t>(t * kSlotsPerTrack + slot_[t]); }
  void selectSlot(uint8_t t, uint8_t s) { slot_[t] = s; }

  /// The DROID's fader bank when it is to change (owner 2026-11-06: F2–F4 sent the loop-edit bank's messages in
  /// NOTE_EDIT — the DROID switches banks on a Program Change on channel 16, which the engine never sent): the
  /// note-edit bank while a NOTE_EDIT session is open (as legacy, at the session's opening), else the loop-edit bank;
  /// the first call (boot) sends the current one, so the DROID follows a restart too. False: no change.
  bool bank(uint8_t& program) {
    const uint8_t want = edit_.editing() ? kNoteEditBank : kLoopEditBank;
    if (want == bankSent_) return false;
    bankSent_ = want;
    program = want;
    return true;
  }

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
    if (note == kLengthNote) return kLength;
    if (note == kEditModeNote) return kEditMode;
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
    transportStarted(clock_->nextTick());
  }
  void stopClock() {
    if (clock_->external()) return;
    clock_->stop();
    control().transportStop(tick_, us_, us());
  }
  /// The take on track `t` ends: a Record at the next bar, or at the bar line just passed when pressed at most half a
  /// bar after it (E9.8i); an overdub at once.
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
        if (p == Press::Double && edit_.editing()) edit_.undo(nowMs_);
        else if (p == Press::Triple && edit_.editing()) edit_.redo(nowMs_);
        else if (p == Press::Short) toggleRecord(t);
        else if (p == Press::Double) control().undo(slot, tick_, us_, us());
        else if (p == Press::Triple) control().redo(slot, tick_, us_, us());
        else control().clear(slot, tick_, us_, us());
        return;
      case kTrackSwitch:
        if (p == Press::Short) switchTrack(static_cast<uint8_t>((t + 1) % kTracks));
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
      case kEditMode:
        if (p == Press::Short) cycleEdit(t);
        else if (p == Press::Long) leaveEdit();
        return;  // double: the set browser (a later slice)
      case kLength:
        if (loopEdit_) {  // LOOP_EDIT: an empty slot gets an empty loop (short or long; a filled one: the popup, later)
          if (p == Press::Short || p == Press::Long)
            control().createEmpty(shownSlot(t), kNewLoopBars * EditSurface<Engine>::kBar);  // refused when filled
          return;
        }
        if (p == Press::Short) edit_.lengthOrCreate(nowMs_);
        else if (p == Press::Double) edit_.deleteOrCreate(nowMs_);
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
      } else if (p == Press::Double) {  // Clear (owner 2026-11-08: the long press creates; overdub is Record's)
        slot_[t] = s;
        control().clear(target, tick_, us_, us());
      } else if (p == Press::Triple) {
        slot_[t] = s;
        control().redo(target, tick_, us_, us());
      } else {  // long: an empty loop on an empty slot (a filled slot: the length popup, a later slice)
        slot_[t] = s;
        if (empty(target)) control().createEmpty(target, kNewLoopBars * EditSurface<Engine>::kBar);
      }
      return;
    }
    if (b >= kTrack0 && b < kTrack0 + kTracks) {
      const uint8_t tt = static_cast<uint8_t>(b - kTrack0);
      if (p == Press::Short) switchTrack(tt);
      else if (p == Press::Double) control().setMute(tt, !control().muted(tt), us_);
      else if (p == Press::Long) control().setSolo(tt, !control().soloed(tt), us_);
    }
  }

  /// A track select. NOTE_EDIT follows the track (owner 2026-11-08: it stayed open on the track left): the session is
  /// left — its edits one undo step on its slot, as leaving — and NOTE_EDIT opens on the new track's shown slot, or
  /// ends when that slot is empty.
  void switchTrack(uint8_t tt) {
    const bool follow = edit_.active() && tt != selected();
    const bool loopFollow = loopEdit_ && tt != selected();
    control().selectTrack(tt);
    if (loopFollow) enterLoopEdit();  // the new track's loop: faders parked, start counted from its content
    if (!follow) return;
    edit_.leave();
    enterEdit(tt);
  }
  /// The edit button's short press (owner 2026-11-08, as legacy): from performance NOTE_EDIT, or LOOP_EDIT when the
  /// shown slot has no notes to edit; then NOTE ↔ LOOP (NOTE only on content; CC later). The long press leaves.
  void cycleEdit(uint8_t t) {
    if (edit_.active()) {
      edit_.leave();
      enterLoopEdit();
      return;
    }
    loopEdit_ = false;
    enterEdit(t);
    if (!edit_.active()) enterLoopEdit();
  }
  void enterLoopEdit() {
    loopEdit_ = true;
    tempoPending_ = !clock_->external();  // F4's motor to the tempo
    lengthMotorPending_ = true;           // F2's to the length
    pendingBars_ = 0;
    startMotor_ = 1;  // F1's to the loop start: rotations count from the content as it is now
    appliedStart_ = 0;
    startPending_ = false;
  }
  /// LOOP_EDIT's F1 (legacy `handleLoopStartFaderInput`: pitch bend on channel 16 over the loop's 16ths): the loop start,
  /// counted from the content as LOOP_EDIT found it (owner 2026-11-08: rotate). Applied at rest as one rotation, one
  /// undo step; not while its motor lands.
  void startFader(int16_t pb, uint32_t ms) {
    if (ms - startWrittenMs_ < EditSurface<Engine>::kMotorSettleMs) return;
    const ManifestId c = transport().contentOf(shownSlot(selected()));
    if (c == kNoManifest) return;
    const uint32_t steps = e_->manifests().manifest(c).loopLengthTicks / EditSurface<Engine>::kSixteenth;
    const uint32_t k = steps > 1 ? static_cast<uint32_t>(static_cast<int32_t>(pb) + 8192) * (steps - 1) / 16383 : 0;
    pendingStart_ = k * EditSurface<Engine>::kSixteenth;
    startPending_ = true;
    startMs_ = ms;
  }
  void settleStart(uint32_t ms) {
    if (!startPending_ || ms - startMs_ < kLengthSettleMs) return;
    startPending_ = false;
    if (!loopEdit_) return;
    const uint8_t slot = shownSlot(selected());
    const ManifestId c = transport().contentOf(slot);
    if (c == kNoManifest) return;
    const uint32_t loop = e_->manifests().manifest(c).loopLengthTicks;
    const uint32_t shift = (pendingStart_ % loop + loop - appliedStart_ % loop) % loop;
    if (shift == 0) return;
    if (control().rotate(slot, shift, tick_, us_, us()) == X::Result::Ok) appliedStart_ = pendingStart_;
  }
  /// LOOP_EDIT's F2 (legacy `handleLoopLengthPitchbend`): the shown loop's length, 1–128 bars over the travel. Applied
  /// once the fader rests `kLengthSettleMs` (one undo step a move; the screen shows the pending length), not while its
  /// motor lands.
  void lengthFader(int16_t pb, uint32_t ms) {
    lengthBars(static_cast<uint16_t>((static_cast<int32_t>(pb) + 8192) * 127 / 16383 + 1), ms);
  }
  /// F2 at `bars` (1–128): the DROID's loop bank sends CC 101 on channel 16 (owner capture 2026-11-08; legacy
  /// `LENGTH_CC_NUMBER`), the pitch bend form is legacy's too.
  void lengthBars(uint16_t bars, uint32_t ms) {
    if (ms - lengthWrittenMs_ < EditSurface<Engine>::kMotorSettleMs) return;
    pendingBars_ = bars;
    pendingMs_ = ms;
  }
  void settleLength(uint32_t ms) {
    if (pendingBars_ == 0 || ms - pendingMs_ < kLengthSettleMs) return;
    const uint32_t loop = pendingBars_ * EditSurface<Engine>::kBar;
    pendingBars_ = 0;
    if (!loopEdit_) return;
    control().resize(shownSlot(selected()), loop, tick_, us_, us());
  }
  /// The internal tempo in tenths of a BPM.
  uint32_t tempoTenths() const {
    const uint64_t d = static_cast<uint64_t>(clock_->usPerTick()) * EngineClock::kTicksPerQuarter;
    return static_cast<uint32_t>((600000000ull + d / 2) / d);
  }
  /// LOOP_EDIT's F4 (legacy `handleTempoInput`): the tempo, 1–300 BPM in tenths over the whole travel
  /// (`TempoMapping`; the engine clock holds at least 20). Not on an external clock, nor while its motor lands.
  void tempo(int16_t pb, uint32_t ms) {
    if (clock_->external() || ms - tempoWrittenMs_ < EditSurface<Engine>::kMotorSettleMs) return;
    const uint32_t tenths = 10u + TempoMapping::bpmStepFromPitchBendLogical(pb);
    if (tenths == tempoTenths()) return;
    clock_->setTempo(tenths * 10u);
  }
  void leaveEdit() {
    edit_.leave();
    loopEdit_ = false;
  }
  /// The track's shown slot: the playing one, else the selected one.
  uint8_t shownSlot(uint8_t t) {
    auto& x = transport();
    return x.playing(t) ? x.playingSlot(t) : selectedSlot(t);
  }
  /// NOTE_EDIT on the shown slot (the playing or selected one), the window at the playhead.
  void enterEdit(uint8_t t) {
    auto& x = transport();
    const uint8_t slot = shownSlot(t);
    const ManifestId m = x.contentOf(slot);
    if (m == kNoManifest || x.writingSlot() == slot) return;
    const uint32_t loop = e_->manifests().manifest(m).loopLengthTicks;
    const uint32_t pos = x.playing(t) && x.playingSlot(t) == slot ? (tick_ - x.origin(t)) % loop : 0;
    edit_.enter(slot, loop, pos, nowMs_);
  }

  void toggleRecord(uint8_t t) {
    if (control().recordQueued(t)) {  // armed (legacy `handleArmedRecordPress`): start the transport, else cancel
      if (!clock_->running()) startClock();
      else control().cancelQueued(t);
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
      if (control().queuedRecordSlot(t) != slot) {
        control().queueRecord(t, slot, tick_);  // armed: at the playing slot's loop start, the bar, or the transport
      } else if (x.playing(t)) {
        control().punchIn(t, tick_, us_, us());  // a second press on a playing track
      } else if (!clock_->running()) {
        startClock();  // a second press on a stopped transport: it starts, and the take with it
      } else {
        control().cancelQueued(t);
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
  EditSurface<Engine> edit_;
  uint8_t slot_[kTracks] = {};
  uint8_t sent_[128] = {};
  uint8_t bankSent_ = 0xFF;  // the DROID fader bank last sent (0xFF: none yet)
  bool loopEdit_ = false;    // LOOP_EDIT (owner 2026-11-08): the edit button's other mode
  bool tempoPending_ = false;  // LOOP_EDIT entered: F4's motor to the tempo
  uint32_t tempoWrittenMs_ = 0;
  bool lengthMotorPending_ = false;  // LOOP_EDIT entered: F2's motor to the length
  uint32_t lengthWrittenMs_ = 0, pendingMs_ = 0;
  uint16_t pendingBars_ = 0;
  uint8_t startMotor_ = 0;  // F1's park: 1 value, 2 trigger on, 3 trigger off due at `startGateMs_`
  uint32_t startWrittenMs_ = 0, startGateMs_ = 0, startMs_ = 0, pendingStart_ = 0, appliedStart_ = 0;
  bool startPending_ = false;
  uint32_t tick_ = 0, us_ = 0, nowMs_ = 0, blinkMs_ = 0;
};

}  // namespace ContentEngine
