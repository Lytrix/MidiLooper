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
    refStep_ = bracket_ / kSixteenth;
    armedMs_ = ms;
    armed_ = false;
    gesture_ = Fader::None;
    f1Seen_ = false;  // the first F1 message selects
  }

  // ---- Faders ---------------------------------------------------------------------------------------------------
  void select(int16_t pb, uint32_t ms) {
    if (!editing() || ticks_ < kSixteenth) return;
    const uint32_t steps = ticks_ / kSixteenth;
    int32_t step = static_cast<int32_t>((static_cast<int64_t>(pb - kPbMin) * (steps - 1)) / (kPbMax - kPbMin));
    if (step < 0) step = 0;
    if (step >= static_cast<int32_t>(steps)) step = static_cast<int32_t>(steps - 1);
    if (f1Seen_ && (pb > f1Pb_ ? pb - f1Pb_ : f1Pb_ - pb) < kF1Deadband) return;  // F1 at rest: the selection stays
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
    (void)ms;
  }
  void coarse(int16_t pb, uint32_t ms) {
    if (!geometryReady(ms)) return;
    const int32_t moved = pb > lastCoarse_ ? pb - lastCoarse_ : lastCoarse_ - pb;
    if (moved < kCoarseThreshold && hadCoarse_ && ms - lastCoarseMs_ < kCoarseRestMs) return;
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
    commit(Fader::Coarse, v, ms);
  }
  void fine(uint8_t cc, uint32_t ms) {
    if (!geometryReady(ms)) return;
    if (hadFine_ && cc == lastFine_ && ms - lastFineMs_ < 80) return;
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
    commit(Fader::Fine, v, ms);
  }
  void pitch(uint8_t cc, uint32_t ms) {
    if (!geometryReady(ms)) return;
    if (session().current(static_cast<uint16_t>(sel_)).pitch == (cc & 0x7F)) return;  // no change
    EditNote v = base(Fader::Pitch, ms);
    v.pitch = cc & 0x7F;
    commit(Fader::Pitch, v, ms);
  }

  // ---- Buttons --------------------------------------------------------------------------------------------------
  /// NOTELEN short: start ↔ end editing.
  void toggleLength() {
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
  }
  /// NOTELEN double: delete the selected note, else create one at the bracket.
  void deleteOrCreate(uint32_t ms) {
    if (!editing()) return;
    gesture_ = Fader::None;
    EditNote n;
    if (selected(n)) {
      n.deleted = 1;
      const EditChange c{static_cast<uint16_t>(sel_), n};
      if (session().apply(&c, 1, focus()) == EditResult::Ok) sel_ = -1;
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
    }
  }
  /// The record button's double / triple while editing.
  bool undo() { return step(true); }
  bool redo() { return step(false); }

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
    if (!editing() || sel_ < 0) return false;
    EditNote n;
    if (!selected(n)) return false;
    return armed_ || ms - armedMs_ >= kGraceMs;
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
  void commit(Fader f, const EditNote& v, uint32_t ms) {
    const EditChange c{static_cast<uint16_t>(sel_), v};
    if (session().apply(&c, 1, focus()) != EditResult::Ok) {
      gesture_ = Fader::None;
      return;
    }
    gesture_ = f;
    gestureNote_ = sel_;
    gestureMs_ = ms;
    gestureDepth_ = session().depth();
    if (!length_) bracket_ = v.tick;
  }
  bool step(bool back) {
    if (!editing()) return false;
    uint32_t f = 0;
    const EditResult r = back ? session().undo(f) : session().redo(f);
    if (r == EditResult::Ok && f != 0) {  // 0: the session start, which recorded no selection — keep the current one
      sel_ = static_cast<int16_t>(static_cast<int32_t>(f >> 21) - 1);
      bracket_ = f & 0x1FFFFFu;
      refStep_ = bracket_ / kSixteenth;
    }
    gesture_ = Fader::None;
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
