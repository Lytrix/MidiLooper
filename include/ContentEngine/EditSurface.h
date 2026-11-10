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
//   loop (`map`), keeping the start's offset from the reference 16th (F3's, which may lie past the 16th); length — the end across the loop
//   (`lengthEditCoarsePitchbendToLoopTick`), at least a 16th (`applyLengthEndTargetRules`).
// * F3 fine (channel 15 CC 2): position — the reference 16th's tick plus (value − 64), wrapped; length — the end
//   anchor plus (value − 64) clamped to ±48 and to the loop, at least a 16th.
// * F4 pitch (channel 15 CC 3): the value; while NOTELEN is held, the velocity (1–127; owner 2026-11-08, not in
//   legacy), 0 muting the note (`kVelocityMuted`: drawn, editable, not sent) — that press is then no short / double. Velocity mode begins with the first F4 move while held, or after
//   `Surface`'s long hold, which writes F4's motor to the velocity; on NOTELEN's release F4's motor goes back to the
//   pitch. A long hold released without F4 moved by hand is a long press: a note is created at the bracket (owner
//   2026-11-08).
// * The geometry faders act once a selection is 750 ms old (`NOTE_SELECTION_GRACE_PERIOD`) or after an F1 move. Every
//   change to the selected note is one undo step, kept when the note is deselected or NOTE_EDIT is left (as legacy;
//   owner 2026-11-08: a fader is never exact, so a step a move was too many): a later change replaces the note's
//   step, and one back at the note's value before it takes the step away.
// * NOTELEN 35: short = start ↔ end (length) editing — the bracket and the reference 16th move to the note's end or
//   start; with no note selected (an empty 16th) it creates one there instead (owner 2026-11-08: nothing to lengthen);
//   double = delete the selected note, or create one at the bracket (C3, velocity 80, a 32nd) when no note starts
//   within 24 ticks of it.
// * Step buttons 0–15 (surface slice D; legacy `BarStepButtonHandler::executeNoteEditAction`, owner 2026-11-10): the
//   16ths of the bar holding the bracket. A press selects the step's first note in F1's stack order (start, then
//   pitch) — a press on the step holding the selection the next one, wrapping (the cycle) — and arms the geometry
//   faders; an empty step gets a 16th C4 (MIDI 60). A double deletes the step's selected note (its first, when the
//   selection is elsewhere). A hold is a parameter lock (`lockBegin` / `lockEnd`): the step's note is selected while it
//   is held, the faders act on it at once, and the release closes its changes as one undo step and brings the
//   previous selection back. The bar buttons do nothing here (as legacy).
// * The encoder (slice E, `nudge`): the selected note's pitch (semitones), start (ticks, wrapped), length (ticks, at
//   least 1, at most the loop) or velocity (1–127, a mute kept) by the turn — accelerated by `Surface` — one undo step
//   a note as the faders; the faders showing it follow. While a step is held its push adds a 16th C4 there even
//   beside other notes (`stepAdd`), its double push deletes the selected note (`deleteSelected`).
// * Copy / paste (owner 2026-11-10: hold the source, press the target; one undo step each): a step held, another step
//   pressed — the selected note (the held step's, locked) copied to that 16th at the same offset in it, beside what is
//   there (`copyNote`); a bar button held, another pressed — that bar of the 8-bar page holding the bracket copied over
//   the other (its notes replaced; `copyBar`, at most `kMaxPaste` notes deleted and added).
// * Undo / redo (the record button's double / triple while editing) step the session and restore its selection.
// * Overlap (owner 2026-11-07): the selected note is the causing note (`NoteEditSession::cause`); a new selection —
//   F1, delete, create — settles the previous one's results first (they become the notes' own state, in its last E:
//   step); undo / redo restore the step's selection as the causing note; leaving keeps the results as published.
// * Motor feedback (owner 2026-11-06; legacy `NoteEditDependentFaderSnapshot`, which was off by default): the DROID's
//   motor faders follow the selection — at the session's opening and after undo, redo, delete or create all four;
//   after an F1 select F2–F4; after an F2 move F3 (the start's offset in its new 16th); after NOTELEN F2 and F3. An
//   F3 move writes nothing: F2 shows the reference 16th, which F3 offsets from (owner 2026-11-06: F2 followed F3, its
//   landing read as a move took the note a 16th on, and F3 was pushed). A fader is written as its value (F1 pitch bend channel 16, F2 pitch bend channel 14, F3 / F4
//   CC 2 / 3 channel 15), then its trigger note (`clear`: channel 16 / 14 note 0, channel 15 note 1 / 2), released
//   after `kMotorGateMs` (legacy 24 ticks at 120 BPM). A fader the musician moves has its position sent back as a
//   value alone (legacy ValueOnly): the DROID's stored start value — where its `clear` sends the motor — stays with
//   the fader. The patch also reads channel 15 notes 1 / 2 as F3 / F4's `clear`, and those are the 16th-step LEDs 2 /
//   3: a stale stored value sent F4 back to an old pitch whenever those LEDs changed (owner 2026-11-06). While a
//   written fader's motor travels (`kMotorSettleMs` after the write, and until it has been quiet `kMotorQuietMs`) its
//   messages are not moves, nor where it comes to rest (within `kMotorLandingPb` / `kMotorLandingCc` of the value: the
//   DROID's motors land a few percent off); a fader value that changes nothing takes no undo step.
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
  static constexpr uint32_t kGraceMs = 750, kCoarseRestMs = 1000, kSnapTicks = 24;
  static constexpr int32_t kCoarseThreshold = 150;
  static constexpr int32_t kF1Deadband = 96;  // of 16,384: F1's rest test (a 16th of a 16-step window is 1,092)
  /// Diagnostics (`!ENGINE trace`): what the last fader message did.
  enum class Why : uint8_t { Acted, NotReady, Grace, AtRest, NoChange, Refused };
  Why lastWhy() const { return why_; }
  uint8_t lastRefusal() const { return refusal_; }  // the `EditResult` of the last refused commit

  void init(Engine& e) {
    e_ = &e;
    state_ = State::Idle;
    sel_ = -1;
    length_ = false;
    velocity_ = false;
    open_ = false;
    lock_ = false;
    for (Motor& m : motors_) m = Motor{};
    f1Seen_ = false;
    why_ = Why::Acted;
    refusal_ = 0;
  }
  bool editing() const { return state_ == State::Editing; }
  bool entering() const { return state_ == State::Gathering; }
  bool active() const { return state_ != State::Idle; }

  // ---- Motor feedback ------------------------------------------------------------------------------------------------
  enum : uint8_t { kMotorSelect = 1, kMotorCoarse = 2, kMotorFine = 4, kMotorPitch = 8, kMotorAll = 15 };
  static constexpr uint32_t kMotorGateMs = 125, kMotorSettleMs = 500, kMotorQuietMs = 150;
  static constexpr int32_t kMotorLandingPb = 600, kMotorLandingCc = 4;
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
        f.sync = false;
        m = value(i, f.value);
        f.phase = Motor::On;
        return true;
      }
      if (f.sync && f.phase == Motor::Idle) {  // value only: no trigger, the motor stays where the hand put it
        f.sync = false;
        m = value(i, f.at);
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
  /// The screen's EDIT count: the session's E: level (History holds the session as one state, replaced in place) less
  /// the selected note's step, which counts once the note is deselected (as legacy; owner 2026-11-08).
  uint8_t steps() const {
    if (!editing()) return 0;
    const uint8_t d = session().depth();
    return open_ && d == openDepth_ && d > 0 ? static_cast<uint8_t>(d - 1) : d;
  }
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
    velocity_ = false;
    open_ = false;
    lock_ = false;
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
    session().cause(sel_);
    refStep_ = bracket_ / kSixteenth;
    armedMs_ = ms;
    armed_ = false;
    open_ = false;
    f1Seen_ = false;  // the first F1 message selects
    drive(kMotorAll, ms);
  }

  // ---- Faders ---------------------------------------------------------------------------------------------------
  void select(int16_t pb, uint32_t ms) {
    const bool travel = echo(0, pb, ms);
    if (!editing() || ticks_ < kSixteenth) return;
    const uint32_t steps = ticks_ / kSixteenth;
    int32_t step = static_cast<int32_t>((static_cast<int64_t>(pb - kPbMin) * (steps - 1)) / (kPbMax - kPbMin));
    if (step < 0) step = 0;
    if (step >= static_cast<int32_t>(steps)) step = static_cast<int32_t>(steps - 1);
    if (travel ||
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
      pick(-1);
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
      pick(stack[idx]);
      bracket_ = session().current(static_cast<uint16_t>(sel_)).tick;
    }
    length_ = false;  // a new selection leaves length mode
    refStep_ = bracket_ / kSixteenth;
    armed_ = true;  // an F1 move arms the geometry faders at once
    open_ = false;
    drive(kMotorCoarse | kMotorFine | kMotorPitch, ms);
  }
  void coarse(int16_t pb, uint32_t ms) {
    const bool travel = echo(1, pb, ms);
    if (!geometryReady(ms)) return;
    const int32_t moved = pb > lastCoarse_ ? pb - lastCoarse_ : lastCoarse_ - pb;
    if (travel || (moved < kCoarseThreshold && hadCoarse_ && ms - lastCoarseMs_ < kCoarseRestMs)) {
      why_ = Why::AtRest;
      return;
    }
    lastCoarse_ = pb;
    lastCoarseMs_ = ms;
    hadCoarse_ = true;
    // The start's offset from the reference 16th, which F3 sets (−64 … +63: it may lie past the 16th).
    const int32_t offset = static_cast<int32_t>(session().current(static_cast<uint16_t>(sel_)).tick) -
                           static_cast<int32_t>(refStep_ * kSixteenth);  // wrapped or not: the modulo below
    const int32_t loop = static_cast<int32_t>(loop_);
    EditNote v = base();
    if (length_) {
      uint32_t end = lengthCoarseTick(pb);
      lengthRules(v, end);
      setEnd(v, end);
      anchor_ = end;
      refStep_ = end / kSixteenth;
    } else {
      const uint32_t total = loop_ / kSixteenth;
      const uint32_t step = arduinoMap(pb, kPbMin, kPbMax, 0, static_cast<int32_t>(total) - 1);
      // Whole 16ths, keeping F3's offset (owner 2026-11-07: a start F3 had put past its 16th — 1.2.4.47 and on — fell
      // back 48 ticks under F2, and F3 was rewritten).
      int32_t start = static_cast<int32_t>(step * kSixteenth) + offset;
      start %= loop;
      if (start < 0) start += loop;
      v.tick = static_cast<uint32_t>(start);
      refStep_ = step;
    }
    if (commit(v)) drive(kMotorFine, ms);
  }
  void fine(uint8_t cc, uint32_t ms) {
    const bool travel = echo(2, cc, ms);
    if (!geometryReady(ms)) return;
    if (travel || (hadFine_ && cc == lastFine_ && ms - lastFineMs_ < 80)) {
      why_ = Why::AtRest;
      return;
    }
    lastFine_ = cc;
    lastFineMs_ = ms;
    hadFine_ = true;
    EditNote v = base();
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
    commit(v);  // F2 stays: it shows the reference 16th F3 offsets from
  }
  /// F4 while NOTELEN is held: the velocity (1–127; owner 2026-11-08, not in legacy); 0 mutes the note, keeping its
  /// velocity, and any other value unmutes it there.
  void velocity(uint8_t cc, uint32_t ms) {
    const bool travel = echo(3, cc, ms);
    if (!travel) velocityMoved_ = true;
    if (!geometryReady(ms)) return;
    const uint8_t was = session().current(static_cast<uint16_t>(sel_)).velocity;
    const uint8_t vel = (cc & 0x7F) == 0 ? static_cast<uint8_t>(was | kVelocityMuted) : (cc & 0x7F);
    if (travel || was == vel) {
      why_ = Why::NoChange;
      return;
    }
    EditNote v = base();
    v.velocity = vel;
    commit(v);
  }
  /// Velocity mode (NOTELEN held): F4 is the velocity; `motor` writes F4 to it (a long hold, the hand not on F4).
  void velocityBegin(uint32_t ms, bool motor) {
    if (!editing() || velocity_) return;
    velocity_ = true;
    velocityMoved_ = false;
    if (motor) drive(kMotorPitch, ms);
  }
  /// NOTELEN released: F4 is the pitch again, its motor back there. True: velocity mode ended without F4 moved by
  /// hand (a long press: `Surface` creates a note; owner 2026-11-08).
  bool velocityDone(uint32_t ms) {
    if (!velocity_) return false;
    velocity_ = false;
    if (editing()) drive(kMotorPitch, ms);
    return !velocityMoved_;
  }
  bool velocityMode() const { return velocity_; }
  void pitch(uint8_t cc, uint32_t ms) {
    const bool travel = echo(3, cc, ms);
    if (!geometryReady(ms)) return;
    if (travel || session().current(static_cast<uint16_t>(sel_)).pitch == (cc & 0x7F)) {  // no change
      why_ = Why::NoChange;
      return;
    }
    EditNote v = base();
    v.pitch = cc & 0x7F;
    commit(v);
  }

  // ---- Buttons --------------------------------------------------------------------------------------------------
  /// NOTELEN short: start ↔ end editing.
  void toggleLength(uint32_t ms) {
    if (!editing()) return;
    length_ = !length_;
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
  /// NOTELEN short: length mode on the selected note; with none selected (an empty 16th) a note is created at the
  /// bracket instead (owner 2026-11-08).
  void lengthOrCreate(uint32_t ms) {
    EditNote n;
    if (editing() && !selected(n))
      create(ms);
    else
      toggleLength(ms);
  }
  /// NOTELEN double: delete the selected note, else create one at the bracket.
  void deleteOrCreate(uint32_t ms) {
    if (!editing()) return;
    open_ = false;
    EditNote n;
    if (selected(n)) {
      n.deleted = 1;
      const EditChange c{static_cast<uint16_t>(sel_), n};
      if (session().apply(&c, 1, focus()) == EditResult::Ok) {
        pick(-1);
        drive(kMotorAll, ms);
      }
      return;
    }
    create(ms);
  }
  /// A note at the bracket (C3, velocity 80, a 32nd — a step button's a 16th; the window's channel), unless one starts
  /// within `kSnapTicks`.
  void create(uint32_t ms, uint32_t length = kSixteenth / 2, bool stacked = false) {
    if (!editing()) return;
    open_ = false;
    const uint16_t size = session().size();
    uint8_t channel = 0;
    for (uint16_t i = 0; i < size; ++i) {
      const EditNote& e = session().current(i);
      if (i == 0) channel = e.channel;
      if (e.deleted) continue;
      const uint32_t d1 = (e.tick + loop_ - bracket_) % loop_, d2 = (bracket_ + loop_ - e.tick) % loop_;
      if (!stacked && (d1 < d2 ? d1 : d2) <= kSnapTicks) return;  // a note is there already
    }
    session().settle();  // the selected note's results stay before the new note's step
    const uint32_t id = e_->noteIds().peek();
    e_->noteIds().advance();
    uint16_t at = 0;
    if (session().create(EditNote{id, bracket_ % loop_, length, 60, 80, channel, 0}, focus(), at) ==
        EditResult::Ok) {
      pick(static_cast<int16_t>(at));
      length_ = false;
      refStep_ = (bracket_ % loop_) / kSixteenth;  // F3's reference: the new note's 16th (a step button moved the bracket)
      armedMs_ = ms;  // a new selection: the geometry faders wait the grace period (legacy)
      armed_ = false;
      drive(kMotorAll, ms);
    }
  }
  // ---- Step buttons (slice D) ---------------------------------------------------------------------------------------
  /// Step `k`'s tick: that 16th of the bar holding the bracket. False: outside the window or the loop.
  bool stepTick(uint8_t k, uint32_t& tick) const {
    if (!editing() || k >= 16) return false;
    tick = bracket_ / kBar * kBar + k * kSixteenth;
    return tick < loop_ && tick >= first_ && tick < first_ + ticks_;
  }
  /// A step's press: its first note, or the next after the selected one (the cycle); an empty step gets a 16th C4.
  void stepPress(uint8_t k, uint32_t ms) {
    uint32_t tick;
    if (!stepTick(k, tick)) return;
    int16_t stack[32];
    const uint8_t n = stackAt(tick, stack);
    if (n == 0) {
      bracket_ = tick;
      create(ms, kSixteenth);
      return;
    }
    uint8_t idx = 0;
    for (uint8_t i = 0; i < n; ++i)
      if (stack[i] == sel_) idx = static_cast<uint8_t>((i + 1) % n);
    choose(stack[idx], ms);
  }
  /// A step's double: its selected note (its first when the selection is elsewhere) is deleted.
  void stepDelete(uint8_t k, uint32_t ms) {
    uint32_t tick;
    if (!stepTick(k, tick)) return;
    int16_t stack[32];
    const uint8_t n = stackAt(tick, stack);
    if (n == 0) return;
    bool on = false;
    for (uint8_t i = 0; i < n; ++i) on |= stack[i] == sel_;
    if (!on) choose(stack[0], ms);
    deleteOrCreate(ms);
  }
  /// A step held (the parameter lock): its note — the selected one when it is there, else its first — is the
  /// selection the faders act on until `lockEnd`. False: an empty step (nothing locked).
  bool lockBegin(uint8_t k, uint32_t ms) {
    uint32_t tick;
    if (lock_ || !stepTick(k, tick)) return false;
    int16_t stack[32];
    const uint8_t n = stackAt(tick, stack);
    if (n == 0) return false;
    int16_t pickNote = stack[0];
    for (uint8_t i = 0; i < n; ++i)
      if (stack[i] == sel_) pickNote = sel_;
    lock_ = true;
    lockSel_ = sel_;
    lockBracket_ = bracket_;
    lockRef_ = refStep_;
    lockLength_ = length_;
    choose(pickNote, ms);
    return true;
  }
  /// The step released: the lock's changes are one undo step (closed now), and the previous selection is back.
  void lockEnd(uint32_t ms) {
    if (!lock_) return;
    lock_ = false;
    open_ = false;
    if (!editing()) return;
    EditNote n;
    const int16_t back = lockSel_ >= 0 && !session().current(static_cast<uint16_t>(lockSel_)).deleted ? lockSel_ : -1;
    const bool same = back >= 0 && back == sel_;  // the locked note was the selection: it stays where the lock left it
    pick(back);
    if (same) {
      follow(base());  // the bracket and F3's reference 16th stay with the moved note (owner 2026-11-10: they jumped back)
    } else {
      bracket_ = lockBracket_;
      refStep_ = lockRef_;
      length_ = lockLength_ && selected(n);
    }
    armed_ = true;
    drive(kMotorAll, ms);
  }
  bool locked() const { return lock_; }
  /// Copy / paste: the selected note onto step `k` at its offset in the 16th, a new note beside those there. False:
  /// nothing selected, outside the window, or refused.
  bool copyNote(uint8_t k, uint32_t ms) {
    EditNote n;
    uint32_t tick;
    if (!selected(n) || !stepTick(k, tick)) return false;
    EditNote c = n;
    c.tick = tick + n.tick % kSixteenth;
    if (c.tick >= loop_ || c.tick >= first_ + ticks_) return false;
    c.noteId = e_->noteIds().peek();
    e_->noteIds().advance();
    c.deleted = 0;
    open_ = false;
    session().settle();
    uint16_t at = 0;
    if (session().create(c, focus(), at) != EditResult::Ok) return false;
    drive(kMotorSelect, ms);  // F1's stack for the selection may have grown
    return true;
  }
  /// Copy / paste: bar `from` of the 8-bar page holding the bracket over bar `to` (its notes deleted, `from`'s added at
  /// the same offsets). False: the same bar, outside the window or the loop, too many notes, or refused.
  bool copyBar(uint8_t from, uint8_t to, uint32_t ms) {
    if (!editing() || from == to || from >= 8 || to >= 8) return false;
    const uint32_t page = bracket_ / (8 * kBar) * (8 * kBar);
    const uint32_t src = page + from * kBar, dst = page + to * kBar;
    const auto inside = [&](uint32_t t) { return t < loop_ && t >= first_ && t < first_ + ticks_; };
    if (!inside(src) || !inside(dst)) return false;
    uint16_t del = 0, add = 0;
    const uint16_t size = session().size();
    for (uint16_t i = 0; i < size; ++i) {  // the target bar's notes go
      const EditNote& e = session().current(i);
      if (e.deleted || e.tick < dst || e.tick >= dst + kBar) continue;
      if (del == kMaxPaste) return false;
      paste_[del] = EditChange{i, e};
      paste_[del++].value.deleted = 1;
    }
    for (uint16_t i = 0; i < size; ++i) {  // the source bar's come
      const EditNote& e = session().current(i);
      if (e.deleted || e.tick < src || e.tick >= src + kBar) continue;
      const uint32_t t = e.tick - src + dst;
      if (!inside(t)) continue;
      if (del + add == kMaxPaste) return false;
      EditNote c = e;
      c.tick = t;
      c.noteId = e_->noteIds().peek();
      e_->noteIds().advance();
      paste_[del + add++] = EditChange{0, c};
    }
    if (del + add == 0) return false;
    open_ = false;
    session().settle();
    uint16_t first = 0;
    if (session().applyAdding(paste_, del, add, focus(), first) != EditResult::Ok) return false;
    EditNote n;
    if (sel_ >= 0 && !selected(n)) pick(-1);  // the selection was in the target bar
    drive(kMotorAll, ms);
    return true;
  }
  /// The encoder's push while step `k` is held (slice E): a 16th C4 there, stacked beside the notes it holds.
  void stepAdd(uint8_t k, uint32_t ms) {
    uint32_t tick;
    if (!stepTick(k, tick)) return;
    bracket_ = tick;
    create(ms, kSixteenth, true);
  }
  /// The encoder's double push while a step is held (slice E): the selected note is deleted.
  void deleteSelected(uint32_t ms) {
    EditNote n;
    if (selected(n)) deleteOrCreate(ms);
  }

  // ---- The encoder (slice E) ------------------------------------------------------------------------------------
  enum class Nudge : uint8_t { Pitch, Move, Length, Velocity };
  /// The selected note changed by `d` (already accelerated): pitch in semitones, start / length in ticks, velocity.
  void nudge(Nudge what, int32_t d, uint32_t ms) {
    EditNote n;
    if (d == 0 || !selected(n)) return;
    EditNote v = n;
    const auto clamp = [](int32_t x, int32_t lo, int32_t hi) { return x < lo ? lo : x > hi ? hi : x; };
    uint8_t mask = 0;
    switch (what) {
      case Nudge::Pitch:
        v.pitch = static_cast<uint8_t>(clamp(n.pitch + d, 0, 127));
        mask = velocity_ ? 0 : kMotorPitch;
        break;
      case Nudge::Move: {
        const int32_t loop = static_cast<int32_t>(loop_);
        int32_t t = (static_cast<int32_t>(n.tick) + d) % loop;
        v.tick = static_cast<uint32_t>(t < 0 ? t + loop : t);
        mask = kMotorSelect | kMotorCoarse | kMotorFine;
        break;
      }
      case Nudge::Length:
        v.length = static_cast<uint32_t>(clamp(static_cast<int32_t>(n.length) + d, 1, static_cast<int32_t>(loop_)));
        mask = length_ ? kMotorCoarse | kMotorFine : 0;
        break;
      case Nudge::Velocity:
        v.velocity = static_cast<uint8_t>(clamp(velocityOf(n.velocity) + d, 1, 127) | (n.velocity & kVelocityMuted));
        mask = velocity_ ? kMotorPitch : 0;
        break;
    }
    if (!commit(v)) return;
    if (what == Nudge::Move) refStep_ = v.tick / kSixteenth;  // F3 offsets from the note's 16th again
    if (mask != 0) drive(mask, ms);
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
  /// The notes starting in `tick`'s 16th inside the window, in F1's stack order (start, then pitch).
  uint8_t stackAt(uint32_t tick, int16_t* stack) {
    uint8_t n = 0;
    const uint16_t size = session().size();
    const uint32_t step = tick / kSixteenth;
    for (uint16_t i = 0; i < size && n < 32; ++i) {
      const EditNote& e = session().current(i);
      if (e.deleted || e.tick < first_ || e.tick >= first_ + ticks_ || e.tick / kSixteenth != step) continue;
      uint8_t k = n++;
      for (; k > 0 && before(e, session().current(static_cast<uint16_t>(stack[k - 1]))); --k) stack[k] = stack[k - 1];
      stack[k] = static_cast<int16_t>(i);
    }
    return n;
  }
  /// A step button's selection: as F1's, the geometry faders armed at once.
  void choose(int16_t note, uint32_t ms) {
    pick(note);
    bracket_ = session().current(static_cast<uint16_t>(sel_)).tick;
    length_ = false;
    refStep_ = bracket_ / kSixteenth;
    armed_ = true;
    open_ = false;
    drive(kMotorAll, ms);
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
  /// The selected note as it is now: a fader changes it from there.
  EditNote base() const { return session().current(static_cast<uint16_t>(sel_)); }
  /// True: the note changed (its step taken, replaced or taken away).
  bool commit(const EditNote& v) {
    if (v == session().current(static_cast<uint16_t>(sel_))) {
      // A value that changes nothing takes no undo step (a fader's echo at rest): the bracket follows the note (owner
      // 2026-11-06: it stayed a 16th on, at the end).
      why_ = Why::NoChange;
      follow(v);
      return false;
    }
    uint32_t focusOut;
    const bool replace = open_ && sel_ == openNote_ && session().depth() == openDepth_;
    if (replace) {
      session().undo(focusOut);  // the note's step: replaced by the new value
      open_ = false;
      if (v == session().current(static_cast<uint16_t>(sel_))) {  // back at its value before the step: none
        why_ = Why::Acted;
        follow(v);
        return true;
      }
    }
    const EditChange c{static_cast<uint16_t>(sel_), v};
    const EditResult r = session().apply(&c, 1, focus());
    if (r != EditResult::Ok) {
      if (replace && session().redo(focusOut) == EditResult::Ok) open_ = true;  // the note keeps its step
      why_ = Why::Refused;
      refusal_ = static_cast<uint8_t>(r);
      return false;
    }
    why_ = Why::Acted;
    open_ = true;
    openNote_ = sel_;
    openDepth_ = session().depth();
    follow(v);
    return true;
  }
  /// The bracket follows the edited note: its start, or in length mode its end (owner 2026-11-07: it stayed where
  /// length mode began).
  void follow(const EditNote& v) { bracket_ = length_ ? (v.tick + v.length) % loop_ : v.tick; }

  // ---- Motor feedback ------------------------------------------------------------------------------------------------
  struct Motor {
    enum Phase : uint8_t { Idle, On, Gate };
    int32_t value = 0;         // the value written
    int32_t from = 0;          // where the fader was when written (its last message)
    int32_t at = 0;            // the fader's last message
    uint32_t offAt = 0, settleUntil = 0;
    Phase phase = Idle;
    bool want = false, written = false, known = false, arrived = false, fromKnown = false;
    bool sync = false;  // the musician moved it: its value (no trigger) is due, so the DROID's stored start value follows
  };
  static int32_t slackOf(uint8_t i) { return i < 2 ? kCoarseThreshold : 3; }  // pitch bend / CC jitter
  /// How far from the written value a motor may come to rest (measured on the DROID 2026-11-06: F2 written −5780 /
  /// −6940 reported −6162 / −7400, ~2.5 % of the travel).
  static int32_t landingOf(uint8_t i) { return i < 2 ? kMotorLandingPb : kMotorLandingCc; }
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
    if ((mask & kMotorPitch) && note) write(3, !velocity_ ? n.pitch : isMuted(n.velocity) ? 0 : velocityOf(n.velocity), ms);
  }
  /// A fader already at `v` (its last message) is not written: no motor travel, no
  /// window that would swallow the musician's next move (owner 2026-11-06: F3, rewritten on every F2 step, ignored a
  /// move that followed).
  void write(uint8_t i, int32_t v, uint32_t ms) {
    Motor& f = motors_[i];
    const int32_t slack = slackOf(i);
    if (f.known && (f.at > v ? f.at - v : v - f.at) < slack) {  // there already (its stored value is its position)
      f.value = v;
      f.want = false;
      return;
    }
    f.from = f.at;
    f.fromKnown = f.known;
    f.arrived = f.known && (f.at > v ? f.at - v : v - f.at) < slack;
    f.value = v;
    f.want = true;
    f.written = true;
    f.settleUntil = ms + kMotorSettleMs;
  }
  /// The fader's own messages while its motor travels to a written value (every position on the way, measured on
  /// the DROID 2026-11-06): not a move — a message between where the fader was and the written value (until it gets
  /// there), or at that value. The window lasts `kMotorSettleMs` after the write and `kMotorQuietMs` after the
  /// fader's last such message, whichever is later. Any other message is the musician's: the window ends. Records the
  /// fader's position either way.
  bool echo(uint8_t i, int32_t v, uint32_t ms) {
    Motor& f = motors_[i];
    f.at = v;
    f.known = true;
    if (f.written && static_cast<int32_t>(ms - f.settleUntil) < 0) {
      const int32_t slack = slackOf(i), to = f.value;
      const bool there = (v > to ? v - to : to - v) <= landingOf(i);
      const int32_t lo = (f.from < to ? f.from : to) - slack, hi = (f.from > to ? f.from : to) + slack;
      const bool onTheWay = !f.arrived && (!f.fromKnown || (v >= lo && v <= hi));
      if (there || onTheWay) {
        if (there) f.arrived = true;
        if (static_cast<int32_t>(ms + kMotorQuietMs - f.settleUntil) > 0) f.settleUntil = ms + kMotorQuietMs;
        return true;
      }
      f.written = false;  // off the motor's path: the musician's hand
    }
    if (editing()) f.sync = true;  // the musician's position becomes the DROID's stored start value (legacy ValueOnly)
    return false;
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
    if (r == EditResult::Ok) session().cause(sel_);  // the step's own results came back with it
    open_ = false;
    if (r == EditResult::Ok) drive(kMotorAll, ms);
    return true;
  }
  uint32_t focus() const { return (static_cast<uint32_t>(sel_ + 1) << 21) | (bracket_ & 0x1FFFFFu); }
  /// A new selection: the previous selected note's overlap results become the notes' own (sticky, DEC-030; owner
  /// 2026-11-07), then the new one causes.
  void pick(int16_t s) {
    if (s != sel_) session().settle();
    sel_ = s;
    session().cause(sel_);
  }

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
  bool length_ = false, armed_ = false, velocity_ = false, velocityMoved_ = false;
  uint32_t armedMs_ = 0, enteredMs_ = 0;
  int16_t lastCoarse_ = 0;
  uint32_t lastCoarseMs_ = 0;
  bool hadCoarse_ = false;
  uint8_t lastFine_ = 64;
  uint32_t lastFineMs_ = 0;
  bool hadFine_ = false;
  bool open_ = false;  // the selected note has its step: a change replaces it
  static constexpr uint16_t kMaxPaste = 128;  // a bar copy's notes deleted and added together
  EditChange paste_[kMaxPaste];
  bool lock_ = false, lockLength_ = false;  // a step held (slice D): the selection before it
  int16_t lockSel_ = -1;
  uint32_t lockBracket_ = 0, lockRef_ = 0;
  int16_t openNote_ = -1;
  uint8_t openDepth_ = 0;
};

}  // namespace ContentEngine
