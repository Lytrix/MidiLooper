//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// The NOTE_EDIT surface on the engine (fixed-memory-engine E9.8b-2; owner 2026-11-06: core NOTE_EDIT, matching the
// legacy `ControlSurfaceManager` / `EditManager` mappings; motor feedback off, as the legacy default).
//
// * Session: `enter` selects the shown slot's notes in the detailed window as it is now — the whole loop up to 16 bars,
//   else the 16-bar page holding the playhead; the window is frozen while editing (as legacy) — and opens the engine's
//   NOTE_EDIT session (`NoteEditSession`) once gathered. A window with more notes than the selection holds is halved
//   around the playhead until it fits (at least a bar). The bracket starts at the playhead and selects the note at its
//   16th, else the closest. `leave` keeps the edits as one undo step.
// * F1 select (channel 16 pitch bend): the 16th under the fader within the window (`selectTickFromPitchbend`), then
//   the notes starting in that 16th, stacked by start and pitch, chosen by the position inside the step
//   (`selectStackedNoteIndexFromPitchbend`); no note there: the bracket alone (as `SelectNavigation`). A select leaves
//   length mode and arms the geometry faders at once. F1 acts only when it moves `kF1Deadband` or more from the value
//   it last acted on: the DROID repeats a fader at rest and jitters by a few values, and a select at a 16th the moved
//   note has left would drop it (as legacy `shouldApplySelectionOnNoteIdChange`; owner 2026-11-06: F2–F4 did not move
//   the selected note).
// * F2 coarse (channel 14 pitch bend; acts on a movement ≥ 150 or after 1 s at rest): position — the 16th across the
//   loop (`map`), keeping the start's offset in its 16th; length — the end across the loop
//   (`lengthEditCoarsePitchbendToLoopTick`), at least a 16th (`applyLengthEndTargetRules`).
// * F3 fine (channel 15 CC 2): position — the reference 16th's tick plus (value − 64), wrapped; length — the end
//   anchor plus (value − 64) clamped to ±48 and to the loop, at least a 16th.
// * F4 pitch (channel 15 CC 3): the value.
// * The geometry faders act once a selection is 750 ms old (`NOTE_SELECTION_GRACE_PERIOD`) or after an F1 move. One
//   fader gesture on one note is one undo step: a move of the same fader on the same note within 300 ms replaces the
//   gesture's step (as legacy collapses contiguous same-note updates).
// * NOTELEN 35: short = start ↔ end (length) editing — the bracket and the reference 16th move to the note's end or
//   start; double = delete the selected note, or create one at the bracket (C3, velocity 80, a 32nd) when no note starts
//   within 24 ticks of it.
// * Undo / redo (the record button's double / triple while editing) step the session and restore its selection.
// * Motor feedback (owner 2026-11-06; legacy `NoteEditDependentFaderSnapshot`, which was off by default): the DROID's
//   motor faders follow the selection — at the session's opening and after undo, redo, delete or create all four;
//   after an F1 select F2–F4; after an F2 move F3 (the start's offset in its new 16th); after an F3 move F2; after
//   NOTELEN F2 and F3. A fader is written as its value (F1 pitch bend channel 16, F2 pitch bend channel 14, F3 / F4
//   CC 2 / 3 channel 15), then its trigger note (`clear`: channel 16 / 14 note 0, channel 15 note 1 / 2), released
//   after `kMotorGateMs` (legacy 24 ticks at 120 BPM). While a written fader's motor travels (`kMotorSettleMs` after the
//   write, and until it has been quiet `kMotorQuietMs`) its messages are not moves; a fader value that changes nothing
//   takes no undo step.
// No allocation; O(selection) a fader move.

#pragma once

#include <cstdint>

#include "ContentEngine/EditWriter.h"

namespace ContentEngine {

template <typename Engine>
class EditSurface {
 public:
  static constexpr uint32_t kSixteenth = 48, kBar = 768, kWindowBars = 16;
  static constexpr int32_t kPbMin = -8192, kPbMax = 8192;
  static constexpr uint32_t kGraceMs = 750, kCoarseRestMs = 1000, kGestureMs = 300, kSnapTicks = 24;
  static constexpr int32_t kCoarseThreshold = 150;
  static constexpr int32_t kF1Deadband = 96;  // of 16,384: F1's rest test (a 16th of a 16-step window is 1,092)
  enum class Fader : uint8_t { None, Coarse, Fine, Pitch, Delete, Create, Length };
  /// Diagnostics (`!ENGINE trace`): what the last fader message did.
  enum class Why : uint8_t { Acted, NotReady, Grace, AtRest, NoChange, Refused };
  Why lastWhy() const { return why_; }
  uint8_t lastRefusal() const { return refusal_; }  // the `EditResult` of the last refused commit

  void init(Engine& e) {
    e_ = &e;
    state_ = State::Idle;
    sel_ = -1;
    length_ = false;
    gesture_ = Fader::None;
  }
  bool editing() const { return state_ == State::Editing; }
  bool entering() const { return state_ == State::Gathering; }
  bool active() const { return state_ != State::Idle; }

  // ---- Motor feedback ------------------------------------------------------------------------------------------------
  enum : uint8_t { kMotorSelect = 1, kMotorCoarse = 2, kMotorFine = 4, kMotorPitch = 8, kMotorAll = 15 };
  static constexpr uint32_t kMotorGateMs = 125, kMotorSettleMs = 500, kMotorQuietMs = 150;
  struct MotorMsg {
    uint8_t status, d1, d2;
  };
  /// The next motor message due at `ms`: a fader's value, then its trigger note on, then off after the gate. False:
  /// none due.
  bool motor(uint32_t ms, MotorMsg& m) {
    for (uint8_t i = 0; i < 4; ++i) {
      Motor& f = motors_[i];
      if (f.phase == Motor::Gate) {
        if (static_cast<int32_t>(ms - f.offAt) < 0) continue;
        m = trigger(i, false);
        f.phase = Motor::Idle;
        return true;
      }
      if (f.phase == Motor::On) {
        m = trigger(i, true);
        f.offAt = ms + kMotorGateMs;
        f.phase = Motor::Gate;
        return true;
      }
      if (f.want) {
        f.want = false;
        m = value(i, f.value);
        f.phase = Motor::On;
        return true;
      }
    }
    return false;
  }
  uint8_t slot() const { return slot_; }
  uint32_t windowFirst() const { return first_; }
  uint32_t windowTicks() const { return ticks_; }
  uint32_t loop() const { return loop_; }
  uint32_t bracket() const { return bracket_; }
  bool lengthMode() const { return length_; }
  /// The selected note as it is now. False: none selected.
  bool selected(EditNote& n) const {
    if (!editing() || sel_ < 0) return false;
    n = session().current(static_cast<uint16_t>(sel_));
    return !n.deleted;
  }

  /// Edit mode 38: opens NOTE_EDIT on `slot` (its loop `loop`, the playhead at `pos`). False: refused (empty, busy).
  bool enter(uint8_t slot, uint32_t loop, uint32_t pos, uint32_t ms) {
    if (state_ != State::Idle || loop == 0) return false;
    slot_ = slot;
    loop_ = loop;
    pos_ = pos % loop;
    if (loop <= kWindowBars * kBar) {
      first_ = 0;
      ticks_ = loop;
    } else {
      ticks_ = kWindowBars * kBar;
      first_ = (pos_ / ticks_) * ticks_;
      if (first_ + ticks_ > loop) ticks_ = loop - first_;
    }
    enteredMs_ = ms;
    return request();
  }
  /// Leaves NOTE_EDIT keeping the edits (one undo step); a gather in progress is dropped.
  void leave() {
    if (state_ == State::Editing) session().leave();
    state_ = State::Idle;
    sel_ = -1;
    length_ = false;
    gesture_ = Fader::None;
  }

  /// Once per pass: a finished gather opens the session (or narrows the window when it holds too many notes).
  void service(uint32_t ms) {
    if (state_ != State::Gathering) return;
    const auto g = session().status();
    if (g == Engine::NoteEdit::Gather::Running) return;
    if (g == Engine::NoteEdit::Gather::TooMany && ticks_ > kBar) {
      // Halve the window (whole bars) around the playhead's bar and gather again.
      ticks_ = ticks_ / 2 / kBar * kBar;
      if (ticks_ < kBar) ticks_ = kBar;
      const uint32_t half = ticks_ / 2 / kBar * kBar, bar = pos_ / kBar * kBar;
      first_ = bar > half ? bar - half : 0;
      if (first_ + ticks_ > loop_) first_ = loop_ > ticks_ ? loop_ - ticks_ : 0;
      request();
      return;
    }
    if (g != Engine::NoteEdit::Gather::Done || session().enter() != EditResult::Ok) {
      state_ = State::Idle;
      return;
    }
    state_ = State::Editing;
    uint32_t b = pos_;
    if (b < first_ || b >= first_ + ticks_) b = first_;
    bracket_ = b;
    selectAt(bracket_);
    if (sel_ < 0) selectClosest(bracket_);
    if (sel_ >= 0) bracket_ = session().current(static_cast<uint16_t>(sel_)).tick;  // F3's reference: the note's 16th
    refStep_ = bracket_ / kSixteenth;
    armedMs_ = ms;
    armed_ = false;
    gesture_ = Fader::None;
    f1Seen_ = false;  // the first F1 message selects
    drive(kMotorAll, ms);
  }

  // ---- Faders ---------------------------------------------------------------------------------------------------
  void select(int16_t pb, uint32_t ms) {
    if (!editing() || ticks_ < kSixteenth) return;
    const uint32_t steps = ticks_ / kSixteenth;
    int32_t step = static_cast<int32_t>((static_cast<int64_t>(pb - kPbMin) * (steps - 1)) / (kPbMax - kPbMin));
    if (step < 0) step = 0;
    if (step >= static_cast<int32_t>(steps)) step = static_cast<int32_t>(steps - 1);
    if (echo(0, ms) ||
        (f1Seen_ && (pb > f1Pb_ ? pb - f1Pb_ : f1Pb_ - pb) < kF1Deadband)) {  // F1 at rest: the selection stays
      why_ = Why::AtRest;
      return;
    }
    why_ = Why::Acted;
    f1Pb_ = pb;
    f1Seen_ = true;
    const uint32_t target = first_ + static_cast<uint32_t>(step) * kSixteenth;
    // The notes starting in that 16th, by start then pitch.
    int16_t stack[32];
    uint8_t n = 0;
    const uint16_t size = session().size();
    for (uint16_t i = 0; i < size && n < 32; ++i) {
      const EditNote& e = session().current(i);
      if (e.deleted || e.tick < first_ || e.tick >= first_ + ticks_ || (e.tick - first_) / kSixteenth != static_cast<uint32_t>(step))
        continue;
      uint8_t k = n++;
      for (; k > 0 && before(e, session().current(static_cast<uint16_t>(stack[k - 1]))); --k) stack[k] = stack[k - 1];
      stack[k] = static_cast<int16_t>(i);
    }
    if (n == 0) {
      sel_ = -1;
      bracket_ = target;
    } else {
      uint32_t idx = 0;
      if (n > 1 && steps > 1) {
        const int64_t span = kPbMax - kPbMin;
        int64_t rem = (static_cast<int64_t>(pb - kPbMin) * (steps - 1)) % span;
        if (rem < 0) rem += span;
        idx = static_cast<uint32_t>(rem * n / span);
        if (idx >= n) idx = n - 1u;
      }
      sel_ = stack[idx];
      bracket_ = session().current(static_cast<uint16_t>(sel_)).tick;
    }
    length_ = false;  // a new selection leaves length mode
    refStep_ = bracket_ / kSixteenth;
    armed_ = true;  // an F1 move arms the geometry faders at once
    gesture_ = Fader::None;
    drive(kMotorCoarse | kMotorFine | kMotorPitch, ms);
  }
  void coarse(int16_t pb, uint32_t ms) {
    if (!geometryReady(ms)) return;
    const int32_t moved = pb > lastCoarse_ ? pb - lastCoarse_ : lastCoarse_ - pb;
    if (echo(1, ms) || (moved < kCoarseThreshold && hadCoarse_ && ms - lastCoarseMs_ < kCoarseRestMs)) {
      why_ = Why::AtRest;
      return;
    }
    lastCoarse_ = pb;
    lastCoarseMs_ = ms;
    hadCoarse_ = true;
    EditNote v = base(Fader::Coarse, ms);
    if (length_) {
      uint32_t end = lengthCoarseTick(pb);
      lengthRules(v, end);
      setEnd(v, end);
      anchor_ = end;
      refStep_ = end / kSixteenth;
    } else {
      const uint32_t total = loop_ / kSixteenth;
      const uint32_t step = arduinoMap(pb, kPbMin, kPbMax, 0, static_cast<int32_t>(total) - 1);
      uint32_t start = step * kSixteenth + v.tick % kSixteenth;
      if (start >= loop_) start = loop_ - 1;
      v.tick = start;
      refStep_ = step;
    }
    if (commit(Fader::Coarse, v, ms)) drive(kMotorFine, ms);
  }
  void fine(uint8_t cc, uint32_t ms) {
    if (!geometryReady(ms)) return;
    if (echo(2, ms) || (hadFine_ && cc == lastFine_ && ms - lastFineMs_ < 80)) {
      why_ = Why::AtRest;
      return;
    }
    lastFine_ = cc;
    lastFineMs_ = ms;
    hadFine_ = true;
    EditNote v = base(Fader::Fine, ms);
    const int32_t offset = static_cast<int32_t>(cc) - 64;
    if (length_) {
      int32_t o = offset < -static_cast<int32_t>(kSixteenth) ? -static_cast<int32_t>(kSixteenth)
                  : offset > static_cast<int32_t>(kSixteenth) ? static_cast<int32_t>(kSixteenth) : offset;
      const int32_t signedEnd = static_cast<int32_t>(anchor_) + o;
      uint32_t end = signedEnd <= 0 ? 0 : static_cast<uint32_t>(signedEnd) >= loop_ ? loop_ - 1 : static_cast<uint32_t>(signedEnd);
      lengthRules(v, end);
      setEnd(v, end);
    } else {
      const int32_t t = static_cast<int32_t>(refStep_ * kSixteenth) + offset;
      v.tick = t < 0 ? loop_ + static_cast<uint32_t>(t) : static_cast<uint32_t>(t) % loop_;
    }
    if (commit(Fader::Fine, v, ms)) drive(kMotorCoarse, ms);
  }
  void pitch(uint8_t cc, uint32_t ms) {
    if (!geometryReady(ms)) return;
    if (echo(3, ms) || session().current(static_cast<uint16_t>(sel_)).pitch == (cc & 0x7F)) {  // no change
      why_ = Why::NoChange;
      return;
    }
    EditNote v = base(Fader::Pitch, ms);
    v.pitch = cc & 0x7F;
    commit(Fader::Pitch, v, ms);
  }

  // ---- Buttons --------------------------------------------------------------------------------------------------
  /// NOTELEN short: start ↔ end editing.
  void toggleLength(uint32_t ms) {
    if (!editing()) return;
    length_ = !length_;
    gesture_ = Fader::None;
    armed_ = true;  // legacy: entering / leaving length mode enables the geometry faders
    EditNote n;
    if (!selected(n)) return;
    if (length_) {
      anchor_ = (n.tick + n.length) % loop_;
      bracket_ = anchor_;
    } else {
      bracket_ = n.tick;
    }
    refStep_ = bracket_ / kSixteenth;
    drive(kMotorCoarse | kMotorFine, ms);
  }
  /// NOTELEN double: delete the selected note, else create one at the bracket.
  void deleteOrCreate(uint32_t ms) {
    if (!editing()) return;
    gesture_ = Fader::None;
    EditNote n;
    if (selected(n)) {
      n.deleted = 1;
      const EditChange c{static_cast<uint16_t>(sel_), n};
      if (session().apply(&c, 1, focus()) == EditResult::Ok) {
        sel_ = -1;
        drive(kMotorAll, ms);
      }
      return;
    }
    const uint16_t size = session().size();
    uint8_t channel = 0;
    for (uint16_t i = 0; i < size; ++i) {
      const EditNote& e = session().current(i);
      if (i == 0) channel = e.channel;
      if (e.deleted) continue;
      const uint32_t d1 = (e.tick + loop_ - bracket_) % loop_, d2 = (bracket_ + loop_ - e.tick) % loop_;
      if ((d1 < d2 ? d1 : d2) <= kSnapTicks) return;  // a note is there already
    }
    const uint32_t id = e_->noteIds().peek();
    e_->noteIds().advance();
    uint16_t at = 0;
    if (session().create(EditNote{id, bracket_ % loop_, kSixteenth / 2, 60, 80, channel, 0}, focus(), at) ==
        EditResult::Ok) {
      sel_ = static_cast<int16_t>(at);
      armedMs_ = ms;  // a new selection: the geometry faders wait the grace period (legacy)
      armed_ = false;
      drive(kMotorAll, ms);
    }
  }
  /// The record button's double / triple while editing.
  bool undo(uint32_t ms) { return step(true, ms); }
  bool redo(uint32_t ms) { return step(false, ms); }

 private:
  enum class State : uint8_t { Idle, Gathering, Editing };
  auto& session() { return e_->noteEdit(); }
  const auto& session() const { return e_->noteEdit(); }

  bool request() {
    const typename Engine::NoteEdit::Query q{slot_, first_, first_ + ticks_, 0, 127};
    if (session().request(q, 0) != Engine::NoteEdit::Gather::Running) {
      state_ = State::Idle;
      return false;
    }
    state_ = State::Gathering;
    return true;
  }
  static bool before(const EditNote& a, const EditNote& b) {
    return a.tick != b.tick ? a.tick < b.tick : a.pitch < b.pitch;
  }
  void selectAt(uint32_t tick) {
    sel_ = -1;
    const uint16_t size = session().size();
    for (uint16_t i = 0; i < size; ++i) {
      const EditNote& e = session().current(i);
      if (e.deleted || e.tick / kSixteenth != tick / kSixteenth) continue;
      if (sel_ < 0 || before(e, session().current(static_cast<uint16_t>(sel_)))) sel_ = static_cast<int16_t>(i);
    }
  }
  void selectClosest(uint32_t tick) {
    uint32_t best = 0xFFFFFFFFu;
    const uint16_t size = session().size();
    for (uint16_t i = 0; i < size; ++i) {
      const EditNote& e = session().current(i);
      if (e.deleted) continue;
      const uint32_t d1 = (e.tick + loop_ - tick) % loop_, d2 = (tick + loop_ - e.tick) % loop_;
      const uint32_t d = d1 < d2 ? d1 : d2;
      if (d < best) {
        best = d;
        sel_ = static_cast<int16_t>(i);
      }
    }
  }
  bool geometryReady(uint32_t ms) {
    why_ = Why::NotReady;
    if (!editing() || sel_ < 0) return false;
    EditNote n;
    if (!selected(n)) return false;
    if (armed_ || ms - armedMs_ >= kGraceMs) return true;
    why_ = Why::Grace;
    return false;
  }
  /// The note a gesture starts from: its value before the gesture (the step it replaces is undone first).
  EditNote base(Fader f, uint32_t ms) {
    const bool same = f == gesture_ && sel_ == gestureNote_ && ms - gestureMs_ < kGestureMs &&
                      session().depth() == gestureDepth_;
    if (same) {
      uint32_t focusOut;
      session().undo(focusOut);  // the gesture's previous value: replaced by the new one
    }
    return session().current(static_cast<uint16_t>(sel_));
  }
  /// True: the change was taken as a step.
  bool commit(Fader f, const EditNote& v, uint32_t ms) {
    if (v == session().current(static_cast<uint16_t>(sel_))) {
      why_ = Why::NoChange;  // a value that changes nothing (a fader's echo at rest) takes no undo step
      return false;
    }
    const EditChange c{static_cast<uint16_t>(sel_), v};
    const EditResult r = session().apply(&c, 1, focus());
    if (r != EditResult::Ok) {
      gesture_ = Fader::None;
      why_ = Why::Refused;
      refusal_ = static_cast<uint8_t>(r);
      return false;
    }
    why_ = Why::Acted;
    gesture_ = f;
    gestureNote_ = sel_;
    gestureMs_ = ms;
    gestureDepth_ = session().depth();
    if (!length_) bracket_ = v.tick;
    return true;
  }

  // ---- Motor feedback ------------------------------------------------------------------------------------------------
  struct Motor {
    enum Phase : uint8_t { Idle, On, Gate };
    int32_t value = 0;
    uint32_t offAt = 0, settleUntil = 0;
    Phase phase = Idle;
    bool want = false, written = false;
  };
  /// Writes the faders in `mask` to the selection (the bracket alone when nothing is selected: F2 / F3).
  void drive(uint8_t mask, uint32_t ms) {
    EditNote n;
    const bool note = selected(n);
    const uint32_t at = note ? (length_ ? (n.tick + n.length) % loop_ : n.tick) : bracket_;
    if ((mask & kMotorSelect) && note) write(0, f1Value(n), ms);  // its echo is held off by `echo`, not the rest test
    if (mask & kMotorCoarse) {
      write(1, length_ ? lengthPb(at) : positionPb(at), ms);
      lastCoarse_ = static_cast<int16_t>(motors_[1].value);
      lastCoarseMs_ = ms;
      hadCoarse_ = true;
    }
    if (mask & kMotorFine) {
      int32_t cc = length_ ? 64 + static_cast<int32_t>(at) - static_cast<int32_t>(anchor_)
                           : 64 + static_cast<int32_t>(at) - static_cast<int32_t>(refStep_ * kSixteenth);
      write(2, cc < 0 ? 0 : cc > 127 ? 127 : cc, ms);
      lastFine_ = static_cast<uint8_t>(motors_[2].value);
      lastFineMs_ = ms;
      hadFine_ = true;
    }
    if ((mask & kMotorPitch) && note) write(3, n.pitch, ms);
  }
  void write(uint8_t i, int32_t v, uint32_t ms) {
    Motor& f = motors_[i];
    f.value = v;
    f.want = true;
    f.written = true;
    f.settleUntil = ms + kMotorSettleMs;
  }
  /// The fader's own messages while its motor travels to a written value (every position on the way, measured on
  /// the DROID 2026-11-06): not a move. The window lasts `kMotorSettleMs` after the write and `kMotorQuietMs` after the
  /// fader's last message, whichever is later.
  bool echo(uint8_t i, uint32_t ms) {
    Motor& f = motors_[i];
    if (!f.written || static_cast<int32_t>(ms - f.settleUntil) >= 0) return false;
    if (static_cast<int32_t>(ms + kMotorQuietMs - f.settleUntil) > 0) f.settleUntil = ms + kMotorQuietMs;
    return true;
  }
  /// F1's value for the selected note: the centre of its place in its 16th's stack (the inverse of `select`).
  int32_t f1Value(const EditNote& n) {
    const uint32_t steps = ticks_ / kSixteenth;
    if (steps < 2 || n.tick < first_ || n.tick >= first_ + ticks_) return motors_[0].value;
    const uint32_t step = (n.tick - first_) / kSixteenth;
    uint32_t count = 0, below = 0;
    const uint16_t size = session().size();
    for (uint16_t i = 0; i < size; ++i) {
      const EditNote& e = session().current(i);
      if (e.deleted || e.tick < first_ || (e.tick - first_) / kSixteenth != step) continue;
      ++count;
      if (before(e, n)) ++below;
    }
    if (count == 0) count = 1;
    const int64_t span = kPbMax - kPbMin;
    const int64_t pos = static_cast<int64_t>(step) * span + (static_cast<int64_t>(below) * 2 + 1) * span / (count * 2);
    const int64_t pb = kPbMin + pos / static_cast<int64_t>(steps - 1);
    return static_cast<int32_t>(pb > kPbMax - 1 ? kPbMax - 1 : pb);
  }
  /// F2 at `tick` in position mode (legacy `positionModeLoopTickToCoarsePitchbend`: the 16th plus the offset in it),
  /// rounded up so `coarse` maps it back into the same 16th.
  int32_t positionPb(uint32_t tick) const {
    const uint32_t steps = loop_ / kSixteenth;
    if (steps <= 1) return 0;
    const int64_t num = static_cast<int64_t>(tick % loop_) * (kPbMax - kPbMin);
    const int64_t den = static_cast<int64_t>(kSixteenth) * (steps - 1);
    const int64_t pb = kPbMin + (num + den - 1) / den;
    return static_cast<int32_t>(pb > kPbMax - 1 ? kPbMax - 1 : pb);
  }
  /// F2 at the end `tick` in length mode (the inverse of `lengthCoarseTick`).
  int32_t lengthPb(uint32_t tick) const {
    if (loop_ <= 1) return 0;
    const int64_t pb = kPbMin + static_cast<int64_t>(tick % loop_) * (kPbMax - kPbMin) / (loop_ - 1);
    return static_cast<int32_t>(pb > kPbMax - 1 ? kPbMax - 1 : pb);
  }
  static MotorMsg value(uint8_t i, int32_t v) {
    if (i < 2) {  // pitch bend, 14 bits
      const int32_t raw = v + 8192 < 0 ? 0 : v + 8192 > 16383 ? 16383 : v + 8192;
      return MotorMsg{static_cast<uint8_t>(i == 0 ? 0xEF : 0xED), static_cast<uint8_t>(raw & 0x7F),
                      static_cast<uint8_t>(raw >> 7)};
    }
    return MotorMsg{0xBE, static_cast<uint8_t>(i == 2 ? 2 : 3), static_cast<uint8_t>(v & 0x7F)};
  }
  static MotorMsg trigger(uint8_t i, bool on) {
    static const uint8_t kChannel[4] = {15, 13, 14, 14}, kNote[4] = {0, 0, 1, 2};
    return MotorMsg{static_cast<uint8_t>((on ? 0x90 : 0x80) | kChannel[i]), kNote[i], static_cast<uint8_t>(on ? 127 : 0)};
  }
  bool step(bool back, uint32_t ms) {
    if (!editing()) return false;
    uint32_t f = 0;
    const EditResult r = back ? session().undo(f) : session().redo(f);
    if (r == EditResult::Ok && f != 0) {  // 0: the session start, which recorded no selection — keep the current one
      sel_ = static_cast<int16_t>(static_cast<int32_t>(f >> 21) - 1);
      bracket_ = f & 0x1FFFFFu;
      refStep_ = bracket_ / kSixteenth;
    }
    gesture_ = Fader::None;
    if (r == EditResult::Ok) drive(kMotorAll, ms);
    return true;
  }
  uint32_t focus() const { return (static_cast<uint32_t>(sel_ + 1) << 21) | (bracket_ & 0x1FFFFFu); }

  // Legacy helpers (`ControlSurfaceColdHelpers`, Arduino `map`).
  static uint32_t arduinoMap(int32_t x, int32_t inMin, int32_t inMax, int32_t outMin, int32_t outMax) {
    const int64_t v = static_cast<int64_t>(x - inMin) * (outMax - outMin) / (inMax - inMin) + outMin;
    return v < 0 ? 0 : static_cast<uint32_t>(v);
  }
  uint32_t lengthCoarseTick(int16_t pb) const {
    if (loop_ <= 1) return 0;
    const float t = static_cast<float>(pb - kPbMin) / static_cast<float>(kPbMax - kPbMin) * static_cast<float>(loop_ - 1);
    const uint32_t tick = static_cast<uint32_t>(t + 0.5f);
    return tick >= loop_ ? loop_ - 1 : tick;
  }
  /// At least a 16th from the start (`applyLengthEndTargetRules`).
  void lengthRules(const EditNote& n, uint32_t& end) const {
    const uint32_t start = n.tick % loop_, cur = (n.tick + n.length) % loop_;
    end %= loop_;
    if (cur > start) {
      if (end < start + kSixteenth) end = start + kSixteenth;
      return;
    }
    const uint32_t d = (end + loop_ - start) % loop_;
    if (d < kSixteenth) end = (start + kSixteenth) % loop_;
  }
  void setEnd(EditNote& n, uint32_t end) const {
    uint32_t len = (end % loop_ + loop_ - n.tick) % loop_;
    if (len == 0) len = loop_;
    n.length = len;
  }

  Engine* e_ = nullptr;
  State state_ = State::Idle;
  uint8_t slot_ = 0;
  uint32_t loop_ = 0, first_ = 0, ticks_ = 0, pos_ = 0, bracket_ = 0, refStep_ = 0, anchor_ = 0;
  int16_t sel_ = -1;
  Motor motors_[4];
  Why why_ = Why::Acted;
  uint8_t refusal_ = 0;
  int16_t f1Pb_ = 0;     // the F1 value the last select acted on
  bool f1Seen_ = false;  // ... this session
  bool length_ = false, armed_ = false;
  uint32_t armedMs_ = 0, enteredMs_ = 0;
  int16_t lastCoarse_ = 0;
  uint32_t lastCoarseMs_ = 0;
  bool hadCoarse_ = false;
  uint8_t lastFine_ = 64;
  uint32_t lastFineMs_ = 0;
  bool hadFine_ = false;
  Fader gesture_ = Fader::None;
  int16_t gestureNote_ = -1;
  uint32_t gestureMs_ = 0;
  uint8_t gestureDepth_ = 0;
};

}  // namespace ContentEngine
