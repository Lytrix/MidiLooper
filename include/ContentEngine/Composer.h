//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// Span-window composer (fixed-memory-engine E3). Merges the runs of one source (a manifest's runs in pass order, each
// read up to a row limit, so a writer's open prefix composes like a sealed run) in DEC-244 order and writes span windows
// in sequence, carrying state from one window to the next. Effective content is derived here (CE-E, OVL-1..8), never
// stored:
//   * an earlier-pass NoteOn is hidden when a later pass has a note sounding on its pitch, or starts one on its pitch
//     within the next 8 ticks (equal start, OVL-2; minimum-length remnant, OVL-6);
//   * a later-pass NoteOn shortens every admitted earlier-pass note sounding on its pitch to one tick before it (OVL-1;
//     at a window's first tick the end goes into the previous window, by looking one tick past each window's end);
//   * channel never matters for overlap (OVL-5); sources are counted per (pitch, pass) whether effective or not (OVL-8).
// Output admission (OUT-POLY): at most T admitted notes sound; a NoteOn beyond them is counted, not written. So every
// window holds <= START rows + T entries (CE-B1). The composer never predicts port admission or live occupancy; the
// track's TerminalScheduler decides what actually sounds.
//
// Preconditions of this slice (named): each run's rows are tick-monotonic within one loop cycle (no wrap-held notes, no
// owed-commit runs spanning cycles). Scratch is fixed: 128 x 64 source counters, T admitted notes, one cursor per run.

#pragma once

#include <cstdint>

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

  void init(const Pool& pool, const Runs& runs) {
    pool_ = &pool;
    runs_ = &runs;
    count_ = 0;
    loop_ = 0;
    windowStart_ = 0;
    identity_ = 0;
    admittedCount_ = 0;
  }

  void begin(const SourceRun* source, uint8_t count, uint32_t loopLength, uint32_t identity) {
    count_ = count > MaxRuns ? MaxRuns : count;
    for (uint8_t i = 0; i < count_; ++i) {
      src_[i] = source[i];
      cursor_[i] = 0;
    }
    for (auto& p : active_)
      for (auto& c : p) c = 0;
    admittedCount_ = 0;
    loop_ = loopLength;
    windowStart_ = 0;
    identity_ = identity;
  }

  /// Composes the next window of `windowTicks` ticks into `w`. False at the end of the loop.
  template <uint16_t Cap>
  bool composeNext(SpanWindow<Cap>& w, uint32_t windowTicks) {
    if (windowStart_ >= loop_) return false;
    const uint32_t a = windowStart_;
    const uint32_t b = a + windowTicks < loop_ ? a + windowTicks : loop_;
    w.reset(identity_, a, b);
    for (uint8_t i = 0; i < admittedCount_; ++i) {
      w.carry[w.carryCount++] = {admitted_[i].noteId, admitted_[i].runKey};
      admitted_[i].entry = kNoEntry;
    }
    uint8_t r;
    while ((r = nextRun(b)) != kNoRunIndex) process(w, r);
    endLookahead(w, b);
    windowStart_ = b;
    return true;
  }

 private:
  static constexpr uint8_t kNoRunIndex = 0xFF;
  static constexpr uint16_t kNoEntry = 0xFFFF;

  struct Admitted {
    uint32_t noteId;
    uint16_t runKey;
    uint8_t pitch;
    uint8_t channel;
    uint16_t entry;  // index of its START in the current window, or kNoEntry when carried in
  };

  const PassEvent& row(uint8_t r, uint32_t i) const { return runs_->rowAt(src_[r].run, i, *pool_); }

  static uint8_t cls(const PassEvent& e) {
    const uint8_t t = e.status & 0xF0;
    if (t == 0x80 || (t == 0x90 && e.data2 == 0)) return 0;
    return t == 0x90 ? 1 : 2;
  }

  /// The run whose next row comes first in DEC-244 order among rows with tick < `limit`.
  uint8_t nextRun(uint32_t limit) const {
    uint8_t best = kNoRunIndex;
    for (uint8_t r = 0; r < count_; ++r) {
      if (cursor_[r] >= src_[r].rowLimit) continue;
      const PassEvent& e = row(r, cursor_[r]);
      if (e.tick >= limit) continue;
      if (best == kNoRunIndex) {
        best = r;
        continue;
      }
      const PassEvent& x = row(best, cursor_[best]);
      if (e.tick != x.tick ? e.tick < x.tick : cls(e) < cls(x)) best = r;  // equal class: lower pass order wins
    }
    return best;
  }

  bool laterPassStartsSoon(uint8_t r, uint8_t pitch, uint32_t from, uint32_t to) const {
    for (uint8_t j = static_cast<uint8_t>(r + 1); j < count_; ++j)
      for (uint32_t i = cursor_[j]; i < src_[j].rowLimit; ++i) {
        const PassEvent& e = row(j, i);
        if (e.tick > to) break;
        if (e.tick >= from && cls(e) == 1 && e.data1 == pitch) return true;
      }
    return false;
  }

  bool laterPassSounding(uint8_t r, uint8_t pitch) const {
    for (uint8_t j = static_cast<uint8_t>(r + 1); j < count_; ++j)
      if (active_[pitch & 0x7F][j] > 0) return true;
    return false;
  }

  template <uint16_t Cap>
  void endAdmitted(SpanWindow<Cap>& w, uint8_t i, uint32_t endTick) {
    Admitted& a = admitted_[i];
    if (a.entry != kNoEntry) {
      w.entries[a.entry].length = static_cast<uint16_t>(endTick - w.entries[a.entry].tick);
    } else if (w.count < Cap) {
      w.entries[w.count++] = SpanEntry{endTick, a.noteId, 0, a.runKey,
                                       static_cast<uint8_t>(0x80 | ((a.channel - 1) & 0x0F)), a.pitch, 0, 1};
      ++w.terminalCount;
    }
    admitted_[i] = admitted_[--admittedCount_];
  }

  /// A later-pass NoteOn on `pitch` at `tick` shortens every admitted earlier-pass note on that pitch to `tick - 1`.
  template <uint16_t Cap>
  void shorten(SpanWindow<Cap>& w, uint8_t order, uint8_t pitch, uint32_t tick) {
    for (uint8_t i = 0; i < admittedCount_;) {
      if (admitted_[i].pitch == pitch && orderOf(admitted_[i].runKey) < order)
        endAdmitted(w, i, tick - 1);  // compacts: re-check index i
      else
        ++i;
    }
  }

  template <uint16_t Cap>
  void process(SpanWindow<Cap>& w, uint8_t r) {
    const PassEvent& e = row(r, cursor_[r]++);
    const uint16_t runKey = makeRunKey(src_[r].run, r);
    const uint8_t c = cls(e);
    if (c == 0) {
      if (active_[e.data1 & 0x7F][r] > 0) --active_[e.data1 & 0x7F][r];
      for (uint8_t i = 0; i < admittedCount_; ++i)
        if (admitted_[i].noteId == e.noteId && admitted_[i].runKey == runKey) {
          endAdmitted(w, i, e.tick);
          break;
        }
      return;
    }
    if (c == 2) {
      if (w.count < Cap) {
        w.entries[w.count++] = SpanEntry{e.tick, e.noteId, kOpenLength, runKey, e.status, e.data1, e.data2, 0};
        ++w.startCount;
      }
      return;
    }
    const uint8_t pitch = e.data1 & 0x7F;
    const bool hidden = laterPassSounding(r, pitch) || laterPassStartsSoon(r, pitch, e.tick, e.tick + kHideLookahead);
    shorten(w, r, pitch, e.tick);
    if (active_[pitch][r] < 0xFF) ++active_[pitch][r];
    if (hidden) return;
    if (admittedCount_ >= T || w.count >= Cap) {
      ++w.refusedCount;
      return;
    }
    admitted_[admittedCount_++] = Admitted{e.noteId, runKey, pitch, static_cast<uint8_t>((e.status & 0x0F) + 1), w.count};
    w.entries[w.count++] = SpanEntry{e.tick, e.noteId, kOpenLength, runKey, e.status, e.data1, e.data2, 0};
    ++w.startCount;
  }

  /// NoteOns exactly at the next window's first tick shorten admitted notes to this window's last tick (OVL-1).
  template <uint16_t Cap>
  void endLookahead(SpanWindow<Cap>& w, uint32_t b) {
    if (b >= loop_) return;
    for (uint8_t j = 0; j < count_; ++j)
      for (uint32_t i = cursor_[j]; i < src_[j].rowLimit; ++i) {
        const PassEvent& e = row(j, i);
        if (e.tick != b) break;
        if (cls(e) == 1) shorten(w, j, e.data1 & 0x7F, b);
      }
  }

  const Pool* pool_;
  const Runs* runs_;
  SourceRun src_[MaxRuns];
  uint32_t cursor_[MaxRuns];
  uint8_t count_;
  uint8_t active_[128][MaxRuns];  // source notes sounding per (pitch, pass), effective or not (OVL-8)
  Admitted admitted_[T];
  uint8_t admittedCount_;
  uint32_t loop_;
  uint32_t windowStart_;
  uint32_t identity_;
};

}  // namespace ContentEngine
