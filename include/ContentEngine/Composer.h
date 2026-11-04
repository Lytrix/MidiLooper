//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// Span-window composer (fixed-memory-engine E3). Merges the runs of one source (a manifest's runs in pass order, each
// read up to a row limit, so a writer's open prefix composes like a sealed run) in DEC-244 order and writes span windows
// in sequence, carrying state from one window to the next and from one loop cycle to the next. Effective content is
// derived here (CE-E, OVL-1..8), never stored:
//   * an earlier-pass NoteOn is hidden when a later pass has a note sounding on its pitch, or starts one on its pitch
//     within the next 8 ticks while the earlier note still sounds (equal start, OVL-2; minimum-length remnant, OVL-6),
//     across the loop end too;
//   * a wrap-held earlier-pass note (its NoteOff lies past the loop end) is hidden when a later pass starts a note on its
//     pitch at tick 0 (ratified occupy rule: head shorten needs hold > 0; Q-OVL-3);
//   * a later-pass NoteOn shortens every admitted earlier-pass note sounding on its pitch to one tick before it (OVL-1;
//     at a window's first tick the end goes into the previous window, by looking one tick past each window's end);
//   * channel never matters for overlap (OVL-5); sources are counted per (pitch, pass) whether effective or not (OVL-8).
// Output admission (OUT-POLY): at most T admitted notes sound; a NoteOn beyond them is counted, not written. So every
// window holds <= START rows + T entries (CE-B1). The composer never predicts port admission or live occupancy; the
// track's TerminalScheduler decides what actually sounds.
//
// Row timeline per run: ticks are monotonic; rows of one loop cycle have ticks < loop length, and a run's NoteOffs for
// notes held across the loop end carry `loop length + position` (the closing run's wrap section). Each run is read as two
// streams: the main stream (ticks < loop) and the wrap stream (ticks >= loop, played at `tick - loop` in the next cycle).
// A cold `begin` counts the wrap-held source notes as sounding at tick 0 (so occlusion at the loop start is exact) but
// admits none of them (DEC-148: never sounded); `nextCycle` continues with everything carried.
//
// Q-LEN-1 (owner decision 2026-11-04, re-strike once per loop): a note held for at least one loop length (its NoteOff
// flagged `kOriginHeldLoop` by the writer) is replayed with effective end min(recorded end, start + loop - 1): its NoteOn
// repeats every loop and each iteration ends one tick before its successor. The flagged NoteOff is never sent against a
// newer iteration. Its source count is one per loop: +1 at each NoteOn, -1 once at the end of each loop sweep (the
// effective span is continuous apart from the restart). Live holds are the scheduler's occupancy, not this rule.
// Not in this slice (named): edit hides; checkpoints for mid-loop starts.
// Scratch is fixed: 128 x 64 source counters, T admitted notes, three cursors per run.

#pragma once

#include <cstdint>
#include <climits>

#include "ContentEngine/EngineConfig.h"
#include "ContentEngine/PassEvent.h"
#include "ContentEngine/SpanWindow.h"

namespace ContentEngine {

struct SourceRun {
  RunId run;
  uint32_t rowLimit;  // rows readable now (a writer's prefix, or the sealed count)
};

template <typename Pool, typename Runs, uint8_t MaxRuns = 64, uint8_t T = Config::kMaxSoundingPerTrack>
class Composer {
 public:
  static constexpr uint32_t kHideLookahead = 8;  // OVL-6 with OVL-2: a later start within [on, on + 8] hides

  /// Look-ahead work, measured (row reads). Per NoteOn it is bounded by the source rows of all passes within the next
  /// 9 ticks (R8; also across the loop end), plus the run's wrap section (<= notes held at the wrap) only when a later
  /// pass starts a note on that pitch at tick 0.
  struct Stats {
    uint32_t maxLookaheadRowsPerNoteOn;
    uint32_t lookaheadRowsLastWindow;
  };
  const Stats& stats() const { return stats_; }

  void init(const Pool& pool, const Runs& runs) {
    pool_ = &pool;
    runs_ = &runs;
    count_ = 0;
    loop_ = 0;
    windowStart_ = 0;
    identity_ = 0;
    cycle_ = 0;
    admittedCount_ = 0;
    stats_ = Stats{};
    lookaheadRows_ = 0;
    for (auto& z : zeroOnsetLast_) z = -1;
  }

  void begin(const SourceRun* source, uint8_t count, uint32_t loopLength, uint32_t identity) {
    count_ = count > MaxRuns ? MaxRuns : count;
    loop_ = loopLength;
    for (auto& p : active_)
      for (auto& c : p) c = 0;
    for (auto& z : zeroOnsetLast_) z = -1;
    stats_ = Stats{};
    for (uint8_t r = 0; r < count_; ++r) {
      src_[r] = source[r];
      wrapStart_[r] = firstRowAtOrAfter(r, loop_);
      band2Start_[r] = firstRowAtOrAfter(r, 2 * loop_);
      cursor_[r] = 0;
      wrapCursor_[r] = wrapStart_[r];
      band2Cursor_[r] = band2Start_[r];
      for (uint32_t i = wrapStart_[r]; i < src_[r].rowLimit; ++i) {  // wrap-held source notes sound at tick 0
        const PassEvent& e = row(r, i);
        if (cls(e) == 0 && active_[e.data1 & 0x7F][r] < 0xFF) ++active_[e.data1 & 0x7F][r];
      }
      for (uint32_t i = 0; i < wrapStart_[r] && row(r, i).tick == 0; ++i)  // later passes starting at tick 0, per pitch
        if (cls(row(r, i)) == 1) zeroOnsetLast_[row(r, i).data1 & 0x7F] = static_cast<int8_t>(r);
    }
    admittedCount_ = 0;
    windowStart_ = 0;
    cycle_ = 0;
    identity_ = identity;
  }

  /// Composes the next window of `windowTicks` ticks into `w`. False at the end of the loop cycle.
  template <uint16_t Cap>
  bool composeNext(SpanWindow<Cap>& w, uint32_t windowTicks) {
    if (windowStart_ >= loop_) return false;
    const uint32_t a = windowStart_;
    const uint32_t b = a + windowTicks < loop_ ? a + windowTicks : loop_;
    w.reset(identity_, a, b);
    w.cycleBase = cycle_ * loop_;
    stats_.lookaheadRowsLastWindow = 0;
    for (uint8_t i = 0; i < admittedCount_; ++i) {
      w.carry[w.carryCount++] = {admitted_[i].noteId, admitted_[i].runKey};
      admitted_[i].entry = kNoEntry;
    }
    Stream s;
    while (nextStream(b, s)) process(w, s);
    endLookahead(w, b);
    windowStart_ = b;
    return true;
  }

  /// Continues into the next loop cycle with every carried state (admitted notes, source counters).
  bool nextCycle() {
    if (windowStart_ < loop_) return false;
    ++cycle_;
    windowStart_ = 0;
    for (uint8_t r = 0; r < count_; ++r) {
      cursor_[r] = 0;
      wrapCursor_[r] = wrapStart_[r];
      band2Cursor_[r] = band2Start_[r];
    }
    return true;
  }

 private:
  static constexpr uint16_t kNoEntry = 0xFFFF;

  struct Admitted {
    uint32_t noteId;
    uint16_t runKey;
    uint8_t pitch;
    uint8_t channel;
    uint16_t entry;  // index of its START in the current window, or kNoEntry when carried in
  };
  struct Stream {
    uint8_t run;
    uint8_t band;  // 0 main (< loop), 1 wrap (< 2 x loop), 2 beyond a second wrap (always a held-loop NoteOff)
  };

  const PassEvent& row(uint8_t r, uint32_t i) const { return runs_->rowAt(src_[r].run, i, *pool_); }

  static uint8_t cls(const PassEvent& e) {
    const uint8_t t = e.status & 0xF0;
    if (t == 0x80 || (t == 0x90 && e.data2 == 0)) return 0;
    return t == 0x90 ? 1 : 2;
  }

  uint32_t firstRowAtOrAfter(uint8_t r, uint32_t tick) const {
    uint32_t lo = 0, hi = src_[r].rowLimit;
    while (lo < hi) {
      const uint32_t mid = lo + (hi - lo) / 2;
      if (row(r, mid).tick < tick)
        lo = mid + 1;
      else
        hi = mid;
    }
    return lo;
  }

  /// Position of a stream's next row in the current cycle, or false when it has none before `limit`.
  /// Band 2 rows are NoteOffs of notes held past a second wrap: always flagged held-loop, so they only need an ordered
  /// position (clamped into the cycle); `process` gives them no effect.
  bool head(uint8_t r, uint8_t band, uint32_t limit, uint32_t& pos, const PassEvent*& e) const {
    const uint32_t i = band == 0 ? cursor_[r] : band == 1 ? wrapCursor_[r] : band2Cursor_[r];
    const uint32_t end = band == 0 ? wrapStart_[r] : band == 1 ? band2Start_[r] : src_[r].rowLimit;
    if (i >= end) return false;
    e = &row(r, i);
    if (band == 0) {
      pos = e->tick;
    } else if (band == 1) {
      pos = e->tick - loop_;
    } else {
      const uint32_t x = e->tick - 2 * loop_;
      pos = x < loop_ ? x : loop_ - 1;
    }
    return pos < limit;
  }

  /// The stream whose next row comes first in DEC-244 order among rows before `limit`: position, class, pass order;
  /// at equal position and class, NoteOffs carried from earlier cycles before a main row of the same run.
  bool nextStream(uint32_t limit, Stream& best) const {
    bool found = false;
    uint32_t bestPos = 0;
    uint8_t bestCls = 0;
    for (uint8_t r = 0; r < count_; ++r)
      for (uint8_t b = 3; b-- > 0;) {
        uint32_t pos;
        const PassEvent* e;
        if (!head(r, b, limit, pos, e)) continue;
        const uint8_t c = cls(*e);
        if (!found || pos < bestPos || (pos == bestPos && c < bestCls)) {
          best = Stream{r, b};
          bestPos = pos;
          bestCls = c;
          found = true;
        }
      }
    return found;
  }

  /// Run-timeline tick of note `noteId`'s NoteOff in run `r` if it lies at or before `limitTick` (main stream from the
  /// cursor, then the wrap stream), else UINT32_MAX. Bounded by the rows in that span.
  uint32_t offTickWithin(uint8_t r, uint32_t noteId, uint32_t limitTick) const {
    for (uint32_t i = cursor_[r]; i < wrapStart_[r]; ++i) {
      ++lookaheadRows_;
      const PassEvent& e = row(r, i);
      if (e.tick > limitTick) return UINT32_MAX;
      if (e.noteId == noteId && cls(e) == 0) return e.tick;
    }
    for (uint32_t i = wrapStart_[r]; i < src_[r].rowLimit; ++i) {
      ++lookaheadRows_;
      const PassEvent& e = row(r, i);
      if (e.tick > limitTick) return UINT32_MAX;
      if (e.noteId == noteId && cls(e) == 0) return e.tick;
    }
    return UINT32_MAX;
  }

  /// OVL-2 / OVL-6: the earlier note (run `r`, NoteOn at position `on`) is hidden if a later pass starts a note on its
  /// pitch at h in [on, on + 8] while it still sounds (h < its NoteOff). h may lie in the next cycle (past the loop end),
  /// where only a wrap-held note still sounds; h = 0 there is Q-OVL-3, handled by `wrapHeldHiddenAtZero`.
  bool laterPassStartsWhileSounding(uint8_t r, uint32_t noteId, uint8_t pitch, uint32_t on) const {
    const uint32_t to = on + kHideLookahead;
    uint32_t off = offTickWithin(r, noteId, to);  // run timeline; UINT32_MAX when it ends after `to`
    if (off > on + loop_ - 1) off = on + loop_ - 1;  // Q-LEN-1 effective end
    for (uint8_t j = static_cast<uint8_t>(r + 1); j < count_; ++j) {
      for (uint32_t i = cursor_[j]; i < wrapStart_[j]; ++i) {
        ++lookaheadRows_;
        const PassEvent& e = row(j, i);
        if (e.tick > to) break;
        if (e.tick >= on && cls(e) == 1 && e.data1 == pitch && e.tick < off) return true;
      }
      if (to < loop_) continue;
      for (uint32_t i = 0; i < wrapStart_[j]; ++i) {  // next cycle, positions [1, to - loop]
        ++lookaheadRows_;
        const PassEvent& e = row(j, i);
        if (e.tick > to - loop_) break;
        if (e.tick > 0 && cls(e) == 1 && e.data1 == pitch && loop_ + e.tick < off) return true;
      }
    }
    return false;
  }

  /// Q-OVL-3 (ratified): a wrap-held earlier note is hidden by a later-pass note starting on its pitch at tick 0. The
  /// per-pitch table makes the common case O(1); only then is the run's wrap section (notes held at the wrap) scanned.
  bool wrapHeldHiddenAtZero(uint8_t r, uint32_t noteId, uint8_t pitch) const {
    if (zeroOnsetLast_[pitch & 0x7F] <= static_cast<int8_t>(r)) return false;
    for (uint32_t i = wrapStart_[r]; i < src_[r].rowLimit; ++i) {
      ++lookaheadRows_;
      if (row(r, i).noteId == noteId && cls(row(r, i)) == 0) return true;
    }
    return false;
  }

  bool laterPassSounding(uint8_t r, uint8_t pitch) const {
    for (uint8_t j = static_cast<uint8_t>(r + 1); j < count_; ++j)
      if (active_[pitch & 0x7F][j] > 0) return true;
    return false;
  }

  template <uint16_t Cap>
  void endAdmitted(SpanWindow<Cap>& w, uint8_t i, uint32_t endPos) {
    Admitted& a = admitted_[i];
    if (a.entry != kNoEntry) {
      w.entries[a.entry].length = static_cast<uint16_t>(endPos - w.entries[a.entry].tick);
    } else if (w.count < Cap) {
      w.entries[w.count++] = SpanEntry{endPos, a.noteId, 0, a.runKey,
                                       static_cast<uint8_t>(0x80 | ((a.channel - 1) & 0x0F)), a.pitch, 0, 1};
      ++w.terminalCount;
    }
    admitted_[i] = admitted_[--admittedCount_];
  }

  /// A later-pass NoteOn on `pitch` at position `pos` > 0 shortens every admitted earlier-pass note on that pitch to
  /// `pos - 1`. At position 0 the only earlier notes sounding are wrap-held ones, which the ratified rule hides outright
  /// (decided at their NoteOn).
  template <uint16_t Cap>
  void shorten(SpanWindow<Cap>& w, uint8_t order, uint8_t pitch, uint32_t pos) {
    if (pos == 0) return;
    for (uint8_t i = 0; i < admittedCount_;) {
      if (admitted_[i].pitch == pitch && orderOf(admitted_[i].runKey) < order)
        endAdmitted(w, i, pos - 1);  // compacts: re-check index i
      else
        ++i;
    }
  }

  template <uint16_t Cap>
  void process(SpanWindow<Cap>& w, const Stream& s) {
    const uint8_t r = s.run;
    uint32_t pos;
    const PassEvent* ep;
    head(r, s.band, UINT32_MAX, pos, ep);
    const PassEvent& e = *ep;
    if (s.band == 0)
      ++cursor_[r];
    else if (s.band == 1)
      ++wrapCursor_[r];
    else
      ++band2Cursor_[r];
    const uint16_t runKey = makeRunKey(src_[r].run, r);
    const uint8_t c = cls(e);
    if (c == 0) {
      if (e.origin & kOriginHeldLoop) return;  // Q-LEN-1: the re-strike ends it; its count drops at the sweep end
      if (active_[e.data1 & 0x7F][r] > 0) --active_[e.data1 & 0x7F][r];
      for (uint8_t i = 0; i < admittedCount_; ++i)
        if (admitted_[i].noteId == e.noteId && admitted_[i].runKey == runKey) {
          endAdmitted(w, i, pos);
          break;
        }
      return;
    }
    if (c == 2) {
      if (w.count < Cap) {
        w.entries[w.count++] = SpanEntry{pos, e.noteId, kOpenLength, runKey, e.status, e.data1, e.data2, 0};
        ++w.startCount;
      }
      return;
    }
    const uint8_t pitch = e.data1 & 0x7F;
    for (uint8_t i = 0; i < admittedCount_; ++i)  // Q-LEN-1: the previous iteration ends one tick before this one
      if (admitted_[i].noteId == e.noteId && admitted_[i].runKey == runKey) {
        endAdmitted(w, i, pos == 0 ? 0 : pos - 1);  // at a window's first tick `endLookahead` already ended it
        break;
      }
    lookaheadRows_ = 0;
    const bool hidden = laterPassSounding(r, pitch) || laterPassStartsWhileSounding(r, e.noteId, pitch, pos) ||
                        wrapHeldHiddenAtZero(r, e.noteId, pitch);
    if (lookaheadRows_ > stats_.maxLookaheadRowsPerNoteOn) stats_.maxLookaheadRowsPerNoteOn = lookaheadRows_;
    stats_.lookaheadRowsLastWindow += lookaheadRows_;
    shorten(w, r, pitch, pos);
    if (active_[pitch][r] < 0xFF) ++active_[pitch][r];
    if (hidden) return;
    if (admittedCount_ >= T || w.count >= Cap) {
      ++w.refusedCount;
      return;
    }
    admitted_[admittedCount_++] = Admitted{e.noteId, runKey, pitch, static_cast<uint8_t>((e.status & 0x0F) + 1), w.count};
    w.entries[w.count++] = SpanEntry{pos, e.noteId, kOpenLength, runKey, e.status, e.data1, e.data2, 0};
    ++w.startCount;
  }

  /// NoteOns exactly at the next window's first tick (the next cycle's tick 0 at the loop end) end admitted notes in
  /// this window: a later pass shortens earlier-pass notes on its pitch (OVL-1; not at tick 0, Q-OVL-3), and a note's
  /// own next iteration ends its previous one (Q-LEN-1). At the loop end the held-loop source counts drop once.
  template <uint16_t Cap>
  void endLookahead(SpanWindow<Cap>& w, uint32_t b) {
    const bool loopEnd = b >= loop_;
    const uint32_t at = loopEnd ? 0 : b;
    for (uint8_t j = 0; j < count_; ++j) {
      const uint16_t runKey = makeRunKey(src_[j].run, j);
      for (uint32_t i = loopEnd ? 0 : cursor_[j]; i < wrapStart_[j]; ++i) {
        const PassEvent& e = row(j, i);
        if (e.tick != at) break;
        if (cls(e) != 1) continue;
        for (uint8_t k = 0; k < admittedCount_; ++k)
          if (admitted_[k].noteId == e.noteId && admitted_[k].runKey == runKey) {
            endAdmitted(w, k, b - 1);
            break;
          }
        if (!loopEnd) shorten(w, j, e.data1 & 0x7F, b);
      }
    }
    if (!loopEnd) return;
    for (uint8_t j = 0; j < count_; ++j)
      for (uint32_t i = wrapStart_[j]; i < src_[j].rowLimit; ++i) {
        const PassEvent& e = row(j, i);
        if (cls(e) == 0 && (e.origin & kOriginHeldLoop) && active_[e.data1 & 0x7F][j] > 0) --active_[e.data1 & 0x7F][j];
      }
  }

  const Pool* pool_;
  const Runs* runs_;
  SourceRun src_[MaxRuns];
  uint32_t cursor_[MaxRuns];      // main stream: rows with tick < loop
  uint32_t wrapStart_[MaxRuns];   // first row with tick >= loop
  uint32_t wrapCursor_[MaxRuns];  // wrap stream: NoteOffs of notes held across the loop end
  uint32_t band2Start_[MaxRuns];  // first row with tick >= 2 x loop (notes held across more than one wrap)
  uint32_t band2Cursor_[MaxRuns];
  uint8_t count_;
  uint8_t active_[128][MaxRuns];  // source notes sounding per (pitch, pass), effective or not (OVL-8)
  Admitted admitted_[T];
  uint8_t admittedCount_;
  uint32_t loop_;
  uint32_t windowStart_;
  uint32_t identity_;
  uint32_t cycle_;
  int8_t zeroOnsetLast_[128];  // highest pass order with a NoteOn at tick 0, per pitch (-1: none)
  Stats stats_;
  mutable uint32_t lookaheadRows_;
};

}  // namespace ContentEngine
