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
// Output admission (OUT-POLY, Q-OUT-2 source-ordered refusal, owner decision 2026-11-05): the composer keeps every
// *effective* source instance that is open (started, not hidden, not yet ended by its NoteOff, an OVL-1 shortening, a
// Q-LEN-1 re-strike or the window look-ahead), selected for output or not, and marks those carried in from the previous
// loop cycle. A NoteOn is selected iff, when the DEC-244 merge reaches it, fewer than T instances are open that were
// either selected in this cycle or carried in (selected or not). Whether an instance is effective and when it ends never
// depends on a selection, and every effective span is shorter than the loop (Q-LEN-1), so the carried-in set is the
// source's effective instances open at the loop end: the in-cycle decisions start from a source-determined state and
// repeat identically every loop from the first steady cycle (design.md, Q-OUT-2 proof). At most T selected instances
// sound (the latest one selected this cycle counted every other), none is cut or restarted to make room, and every
// window holds <= START rows + T entries (CE-B1). Up to kMaxOpen instances are tracked; beyond that an instance is
// refused untracked (counted: `Stats::openOverflow`) and periodicity is no longer guaranteed, while the bound holds.
// The composer never predicts port admission or live occupancy; the track's TerminalScheduler decides what sounds.
//
// Row timeline per run: ticks are monotonic; rows of one loop cycle have ticks < loop length, and a run's NoteOffs for
// notes held across the loop end carry `loop length + position` (the closing run's wrap section). Each run is read as two
// streams: the main stream (ticks < loop) and the wrap stream (ticks >= loop, played at `tick - loop` in the next cycle).
// A cold `begin` counts the wrap-held source notes as sounding at tick 0 (so occlusion at the loop start is exact) but
// opens none of them, so its cycle 0 is not the steady selection: a production cold start resumes from a checkpoint or
// warms up one cycle (`ColdStart`), and the player ignores carried instances it never sent (DEC-148).
//
// Q-LEN-1 (owner decision 2026-11-04, re-strike once per loop): a note held for at least one loop length (its NoteOff
// flagged `kOriginHeldLoop` by the writer) is replayed with effective end min(recorded end, start + loop - 1): its NoteOn
// repeats every loop and each iteration ends one tick before its successor. The flagged NoteOff is never sent against a
// newer iteration. Its source count is one per loop: +1 at each NoteOn, -1 once at the end of each loop sweep (the
// effective span is continuous apart from the restart). Live holds are the scheduler's occupancy, not this rule.
// Resized loops (DEC-257, owner 2026-11-08): a state's loop may differ from the length its runs were recorded at; its
// fit run (`FitRun.h`) names the runs with rows past the end that are not wrapping NoteOffs. Such a run's wrap and
// second-wrap rows act only as NoteOffs of its crossing notes (`wraps`); NoteOns, controllers and NoteOffs past a
// shorter end have no effect. The held-loop rule reads `kOriginHeldLoop` through the fit run (`held`). The fit run
// composes nothing.
// Edit runs (NOTE_EDIT): their HideNote rows hide every row of the named noteId in earlier runs (lineage: a replacement
// keeps the noteId and lives in the edit run). The hide index is shared per manifest identity (`EditIndexStore`, E3.3):
// `begin` pins the identity's slot, lookups are O(log H), and the next `begin` / `end` releases it. EDIT-1 (ratified): an edit run is not a derived occluder; NOTE_EDIT writes its own
// overlap results as explicit hides/replacements. Later overdub passes occlude edited notes as usual, and a note hidden
// by an edit still counts as a source occluder (OVL-8).
// Restart checkpoints: `capture` stores the carry state at a window boundary (sparse source counters <= 56 entries,
// open instances <= 32 with their selection, stamps); `resume` rebuilds the composer there (cursors by binary search on tick), so a mid-loop
// start (undo, redo, slot switch, adoption warm-up) replays at most from the nearest earlier checkpoint instead of the
// loop start. A sounding set that does not fit is refused by name (`capture` false); the caller uses an earlier one.
// Scratch is fixed: 128 x 64 source counters, kMaxOpen open instances, three cursors per run.

#pragma once

#include <cstdint>
#include <climits>

#include "ContentEngine/EditIndexStore.h"
#include "ContentEngine/EngineConfig.h"
#include "ContentEngine/MidiClass.h"
#include "ContentEngine/PassEvent.h"
#include "ContentEngine/SourceRun.h"
#include "ContentEngine/SpanWindow.h"

namespace ContentEngine {

template <typename Pool, typename Runs, uint8_t MaxRuns = 64, uint8_t T = Config::kMaxSoundingPerTrack,
          typename IndexStore = EditIndexStore<Pool, Runs>>
class Composer {
 public:
  static constexpr uint32_t kHideLookahead = 8;  // OVL-6 with OVL-2: a later start within [on, on + 8] hides

  /// Look-ahead work, measured (row reads). Per NoteOn it is bounded by the source rows of all passes within the next
  /// 9 ticks (R8; also across the loop end), plus the run's wrap section (<= notes held at the wrap) only when a later
  /// pass starts a note on that pitch at tick 0.
  struct Stats {
    uint32_t maxLookaheadRowsPerNoteOn;
    uint32_t lookaheadRowsLastWindow;
    uint32_t openOverflow;  // effective instances refused untracked: more than kMaxOpen open at once
    uint32_t continuousLimited;  // continuous-controller rows beyond 32 on one tick, left out of the window
  };
  static constexpr uint8_t kMaxOpen = 48;
  const Stats& stats() const { return stats_; }

  /// `index` is the shared edit-hide index; required only for sources with edit runs.
  void init(const Pool& pool, const Runs& runs, IndexStore* index = nullptr) {
    pool_ = &pool;
    runs_ = &runs;
    index_ = index;
    lease_ = IndexLease{};
    indexResult_ = IndexResult::NotNeeded;
    count_ = 0;
    loop_ = 0;
    windowStart_ = 0;
    windowEnd_ = 0;
    windowOpen_ = false;
    identity_ = 0;
    cycle_ = 0;
    openCount_ = 0;
    stats_ = Stats{};
    lookaheadRows_ = 0;
    for (auto& z : zeroOnsetLast_) z = -1;
  }

  /// False (named, `indexResult()`) when the source has edit runs and their index cannot be pinned: no store, every slot
  /// pinned by other identities (`Busy`), or more hides than a slot holds (`CapacityExceeded`). Nothing is composed.
  bool begin(const SourceRun* source, uint8_t count, uint32_t loopLength, uint32_t identity) {
    return bind(source, count, loopLength, identity, true);
  }

  /// Composes the next window of `windowTicks` ticks into `w`. False at the end of the loop cycle.
  template <uint16_t Cap>
  bool composeNext(SpanWindow<Cap>& w, uint32_t windowTicks) {
    if (!openWindow(w, windowTicks)) return false;
    composeRows(w, 0xFFFFFFFFu);
    return true;
  }

  /// Incremental composition (E5 grains): `openWindow` starts the next window in `w` (false at the end of the cycle);
  /// each `composeRows` processes at most `maxRows` source rows and returns true once the window is complete. The
  /// result equals `composeNext`. `w` belongs to the caller until the window completes; no other composer call may
  /// run in between except `windowOpen()`.
  template <uint16_t Cap>
  bool openWindow(SpanWindow<Cap>& w, uint32_t windowTicks) {
    if (windowStart_ >= loop_ || windowOpen_) return false;
    const uint32_t a = windowStart_;
    windowEnd_ = a + windowTicks < loop_ ? a + windowTicks : loop_;
    w.reset(identity_, a, windowEnd_);
    w.cycleBase = cycle_ * loop_;
    stats_.lookaheadRowsLastWindow = 0;
    ccPos_ = UINT32_MAX;
    for (uint8_t i = 0; i < openCount_; ++i) {
      if (open_[i].selected) w.carry[w.carryCount++] = {open_[i].noteId, open_[i].runKey};
      open_[i].entry = kNoEntry;
    }
    windowOpen_ = true;
    return true;
  }
  /// `maxRows` is a work budget in row units: each merged row costs 1, and every kLookaheadReadsPerUnit look-ahead
  /// reads cost one more (device-measured: a merged row ~4 µs, a look-ahead read ~0.4 µs), so a grain's duration stays
  /// bounded when later passes cluster within the OVL-6 range; such a window takes more grains instead.
  template <uint16_t Cap>
  bool composeRows(SpanWindow<Cap>& w, uint32_t maxRows) {
    if (!windowOpen_) return true;
    Stream s;
    for (uint32_t units = 0; units < maxRows;) {
      if (!nextStream(windowEnd_, s)) {
        endLookahead(w, windowEnd_);
        windowStart_ = windowEnd_;
        windowOpen_ = false;
        return true;
      }
      lookaheadRows_ = 0;
      process(w, s);
      units += 1 + lookaheadRows_ / kLookaheadReadsPerUnit;
    }
    return false;
  }
  static constexpr uint32_t kLookaheadReadsPerUnit = 8;
  bool windowOpen() const { return windowOpen_; }
  /// The positions [0, wrapReach) where notes held across the loop end can sound (the last wrap row's position + 1; the
  /// loop length when a note is held past a second wrap; 0 when none). Later positions compose the same in cycle 0 as
  /// in every later cycle.
  uint32_t wrapReach() const { return wrapReach_; }

  /// Releases the pinned edit-index slot (also done by the next `begin`).
  void end() {
    if (index_ != nullptr) index_->release(lease_);
    lease_ = IndexLease{};
  }
  IndexResult indexResult() const { return indexResult_; }
  IndexStore* indexStore() const { return index_; }

  static constexpr uint8_t kCheckpointActive = 56;
  static constexpr uint8_t kCheckpointOpen = 20;  // the owner's realistic ceiling of open notes per track (2026-11-05)

  /// Carry state at a window boundary. Fixed size; stamped with the source it belongs to.
  struct Checkpoint {
    struct Active {
      uint8_t pitch, run, count, pad;
    };
    struct Note {
      uint32_t noteId;
      uint16_t runKey;
      uint8_t pitch, channel;  // channel 1..16; bit 7: selected for output; bit 6: carried in
    };
    uint32_t identity, loopLength, position, cycle;
    uint8_t runCount, activeCount, openCount;
    bool valid;
    Active active[kCheckpointActive];
    Note open[kCheckpointOpen];
  };

  /// Stores the state at the current window boundary. False (named) when more than 56 (pitch, pass) source counters are
  /// nonzero or more than 32 instances are open; `cp.valid` is then false.
  bool capture(Checkpoint& cp) const {
    cp.valid = false;
    if (windowOpen_) return false;  // only at a window boundary
    cp.identity = identity_;
    cp.loopLength = loop_;
    cp.position = windowStart_ < loop_ ? windowStart_ : 0;
    cp.cycle = windowStart_ < loop_ ? cycle_ : cycle_ + 1;
    cp.runCount = count_;
    cp.activeCount = 0;
    for (uint8_t p = 0; p < 128; ++p)
      for (uint8_t r = 0; r < count_; ++r) {
        if (active_[p][r] == 0) continue;
        if (cp.activeCount >= kCheckpointActive) return false;
        cp.active[cp.activeCount++] = {p, r, active_[p][r], 0};
      }
    if (openCount_ > kCheckpointOpen || stats_.openOverflow != 0) return false;
    cp.openCount = openCount_;
    const bool loopEnd = windowStart_ >= loop_;  // the state of the next cycle's start: everything open is carried in
    for (uint8_t i = 0; i < openCount_; ++i)
      cp.open[i] = {open_[i].noteId, open_[i].runKey, open_[i].pitch,
                    static_cast<uint8_t>(open_[i].channel | (open_[i].selected ? 0x80 : 0) |
                                         (open_[i].carried || loopEnd ? 0x40 : 0))};
    cp.valid = true;
    return true;
  }

  /// Rebuilds the composer at `cp` for the same source. False when `cp` is not valid for this source (identity, loop
  /// length, run count) or the source's hides do not fit.
  bool resume(const SourceRun* source, uint8_t count, uint32_t loopLength, uint32_t identity, const Checkpoint& cp) {
    if (!resumeBind(source, count, loopLength, identity, cp)) return false;
    resumeSeek();
    return true;
  }
  /// `resume` as two grains (each fits g on the device; together they do not): `resumeBind` binds the source and
  /// restores the checkpoint's state, `resumeSeek` positions every stream cursor. Nothing composes in between.
  bool resumeBind(const SourceRun* source, uint8_t count, uint32_t loopLength, uint32_t identity,
                  const Checkpoint& cp) {
    if (!cp.valid || cp.identity != identity || cp.loopLength != loopLength || cp.runCount != count) return false;
    if (!bind(source, count, loopLength, identity, false)) return false;
    for (uint8_t i = 0; i < cp.activeCount; ++i) active_[cp.active[i].pitch][cp.active[i].run] = cp.active[i].count;
    openCount_ = cp.openCount;
    for (uint8_t i = 0; i < cp.openCount; ++i)
      open_[i] = Open{cp.open[i].noteId, cp.open[i].runKey, cp.open[i].pitch,
                      static_cast<uint8_t>(cp.open[i].channel & 0x3F), kNoEntry, (cp.open[i].channel & 0x80) != 0,
                      (cp.open[i].channel & 0x40) != 0};
    windowStart_ = cp.position;
    cycle_ = cp.cycle;
    return true;
  }
  void resumeSeek() {
    const uint32_t p = windowStart_;
    for (uint8_t r = 0; r < count_; ++r) {
      cursor_[r] = firstRowAtOrAfter(r, p);
      if (cursor_[r] < hideEnd_[r]) cursor_[r] = hideEnd_[r];
      if (cursor_[r] > wrapStart_[r]) cursor_[r] = wrapStart_[r];
      uint32_t w = firstRowAtOrAfter(r, loop_ + p);
      wrapCursor_[r] = w < wrapStart_[r] ? wrapStart_[r] : (w > band2Start_[r] ? band2Start_[r] : w);
      const uint32_t b2 = firstRowAtOrAfter(r, 2 * loop_ + p);
      band2Cursor_[r] = b2 < band2Start_[r] ? band2Start_[r] : b2;
      refreshHeads(r);
    }
  }

  /// Renumbers the current cycle (window `cycleBase`) without changing the carried state. Valid for steady state,
  /// which is periodic from cycle 1 on (cold start at any later playback cycle, `coldStart`).
  void rebaseCycle(uint32_t cycle) { cycle_ = cycle; }

  /// Continues into the next loop cycle with every carried state (open instances, source counters).
  bool nextCycle() {
    if (windowStart_ < loop_ || windowOpen_) return false;
    ++cycle_;
    windowStart_ = 0;
    for (uint8_t i = 0; i < openCount_; ++i) open_[i].carried = true;
    for (uint8_t r = 0; r < count_; ++r) {
      cursor_[r] = hideEnd_[r];
      wrapCursor_[r] = wrapStart_[r];
      band2Cursor_[r] = band2Start_[r];
      refreshHeads(r);
    }
    return true;
  }

 private:
  static constexpr uint16_t kNoEntry = 0xFFFF;

  /// An open effective source instance (Q-OUT-2). `selected`: it sounds (START written); otherwise it only counts.
  struct Open {
    uint32_t noteId;
    uint16_t runKey;
    uint8_t pitch;
    uint8_t channel;
    uint16_t entry;  // index of its START in the current window, or kNoEntry when carried in
    bool selected;
    bool carried;  // started in an earlier loop cycle
  };
  /// Instances that occupy an output voice for admission: selected this cycle, or carried in (selected or not).
  uint8_t occupied() const {
    uint8_t n = 0;
    for (uint8_t i = 0; i < openCount_; ++i) n += (open_[i].selected || open_[i].carried) ? 1 : 0;
    return n;
  }
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
  /// Reads only the cached head keys (RAM): one PSRAM read per row, when its cursor advances (`refreshHead`).
  bool nextStream(uint32_t limit, Stream& best) const {
    uint32_t bestKey = kNoHead;
    for (uint8_t r = 0; r < count_; ++r)
      for (uint8_t b = 3; b-- > 0;) {
        const uint32_t k = headKey_[r][b];
        if (k < bestKey) {
          best = Stream{r, b};
          bestKey = k;
        }
      }
    return bestKey != kNoHead && (bestKey >> 2) < limit;
  }

  /// `begin` from tick 0 (`fromStart`: cursors, heads and the wrap-held occluders set here), or the source binding
  /// alone for `resume`, which seeks the cursors and restores the occluders from its checkpoint. The tick-0 onset
  /// order comes from the source and is needed at every later cycle start, so both paths set it.
  bool bind(const SourceRun* source, uint8_t count, uint32_t loopLength, uint32_t identity, bool fromStart) {
    end();
    count_ = count > MaxRuns ? MaxRuns : count;
    loop_ = loopLength;
    for (auto& p : active_)
      for (auto& c : p) c = 0;
    for (auto& z : zeroOnsetLast_) z = -1;
    stats_ = Stats{};
    wrapReach_ = 0;
    fitRun_ = kNoRun;
    fitRows_ = 0;
    for (uint8_t r = 0; r < count_; ++r)
      if (runs_->run(source[r].run).kind == RunKind::kFit) {
        fitRun_ = source[r].run;
        fitRows_ = source[r].rowLimit;
      }
    for (uint8_t r = 0; r < count_; ++r) {
      src_[r] = source[r];
      edit_[r] = runs_->run(src_[r].run).kind == RunKind::kEdit;
      if (runs_->run(src_[r].run).kind == RunKind::kFit) {  // lookups only: every stream empty
        filtered_[r] = false;
        wrapStart_[r] = band2Start_[r] = hideEnd_[r] = src_[r].rowLimit;
        if (fromStart) {
          cursor_[r] = wrapCursor_[r] = band2Cursor_[r] = src_[r].rowLimit;
          refreshHeads(r);
        }
        continue;
      }
      filtered_[r] = fitHas(r, kFitFilterStatus, 0);
      wrapStart_[r] = firstRowAtOrAfter(r, loop_);
      band2Start_[r] = firstRowAtOrAfter(r, 2 * loop_);
      if (filtered_[r]) {
        const uint32_t reach = crossReach(r);  // the crossing notes, not the rows past the end
        if (reach > wrapReach_) wrapReach_ = reach;
      } else if (band2Start_[r] < src_[r].rowLimit) {
        wrapReach_ = loop_;  // a note held past the second wrap: the whole loop
      } else if (wrapStart_[r] < band2Start_[r]) {
        const uint32_t reach = row(r, band2Start_[r] - 1).tick - loop_ + 1;  // rows are in tick order
        if (reach > wrapReach_) wrapReach_ = reach;
      }
      hideEnd_[r] = edit_[r] ? leadingHideRows(r) : 0;
      if (fromStart) {
        cursor_[r] = hideEnd_[r];
        wrapCursor_[r] = wrapStart_[r];
        band2Cursor_[r] = band2Start_[r];
        refreshHeads(r);
      }
      if (edit_[r]) continue;  // EDIT-1: an edit run is not counted as a source occluder
      if (fromStart) {
        for (uint32_t i = wrapStart_[r]; i < src_[r].rowLimit; ++i) {  // wrap-held source notes sound at tick 0
          const PassEvent& e = row(r, i);
          if (cls(e) == 0 && wraps(r, e) && active_[e.data1 & 0x7F][r] < 0xFF) ++active_[e.data1 & 0x7F][r];
        }
      }
      for (uint32_t i = hideEnd_[r]; i < wrapStart_[r] && row(r, i).tick == 0; ++i)  // later passes at tick 0, per pitch
        if (cls(row(r, i)) == 1) zeroOnsetLast_[row(r, i).data1 & 0x7F] = static_cast<int8_t>(r);
    }
    if (index_ != nullptr) {
      indexResult_ = index_->acquire(identity, source, count_, lease_);
    } else {
      indexResult_ = IndexResult::NotNeeded;
      for (uint8_t r = 0; r < count_; ++r)
        if (edit_[r]) indexResult_ = IndexResult::Busy;  // edit runs need the shared index
    }
    if (indexResult_ != IndexResult::Ready && indexResult_ != IndexResult::NotNeeded) {
      count_ = 0;
      return false;
    }
    openCount_ = 0;
    windowStart_ = 0;
    windowOpen_ = false;
    cycle_ = 0;
    identity_ = identity;
    return true;
  }

  /// Head key of a stream: position << 2 | class (DEC-244 order: position, then class), or kNoHead when exhausted.
  void refreshHead(uint8_t r, uint8_t b) {
    uint32_t pos;
    const PassEvent* e;
    headKey_[r][b] = head(r, b, UINT32_MAX, pos, e) ? (pos << 2) | cls(*e) : kNoHead;
    if (b != 0) return;
    // Main stream: also the head's pitch and the next row's tick, so the look-ahead can skip a pass from RAM.
    headPitch_[r] = headKey_[r][0] == kNoHead ? 0 : (e->data1 & 0x7F);
    nextTick_[r] = cursor_[r] + 1 < wrapStart_[r] ? row(r, cursor_[r] + 1).tick : kNoHead;
  }
  /// Edit runs lead with their hide rows (writer order); they are collected by the index, never merged.
  uint32_t leadingHideRows(uint8_t r) const {
    uint32_t lo = 0, hi = src_[r].rowLimit;
    while (lo < hi) {
      const uint32_t mid = lo + (hi - lo) / 2;
      if (row(r, mid).status == kHideNoteStatus)
        lo = mid + 1;
      else
        hi = mid;
    }
    return lo;
  }
  void refreshHeads(uint8_t r) {
    for (uint8_t b = 0; b < 3; ++b) refreshHead(r, b);
  }

  /// Run-timeline tick of note `noteId`'s NoteOff in run `r` if it lies at or before `limitTick` (main stream from the
  /// cursor, then the wrap stream), else UINT32_MAX. Bounded by the rows in that span.
  uint32_t offTickWithin(uint8_t r, uint32_t noteId, uint32_t limitTick) const {
    if (headKey_[r][0] != kNoHead && (headKey_[r][0] >> 2) > limitTick) return UINT32_MAX;  // cached: no read
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
      if (edit_[j]) continue;                                   // EDIT-1
      if (!runs_->startsPitch(src_[j].run, pitch)) continue;  // no NoteOn on this pitch in that pass: nothing to read
      // From the cached head (the pass's next unprocessed row, at or after `on`) and the next row's tick: most passes
      // are decided without reading PSRAM; only a second row inside [on, to] continues from the source.
      const uint32_t k = headKey_[j][0];
      uint32_t i = cursor_[j];
      if (k == kNoHead || (k >> 2) > to) {
        i = wrapStart_[j];
      } else {
        const uint32_t h = k >> 2;
        if ((k & 3u) == 1 && headPitch_[j] == pitch && h >= on && h < off) return true;
        i = nextTick_[j] > to ? wrapStart_[j] : i + 1;
      }
      for (; i < wrapStart_[j]; ++i) {
        ++lookaheadRows_;
        const PassEvent& e = row(j, i);
        if (e.tick > to) break;
        if (e.tick >= on && cls(e) == 1 && e.data1 == pitch && e.tick < off) return true;
      }
      if (to < loop_) continue;
      for (uint32_t i = hideEnd_[j]; i < wrapStart_[j]; ++i) {  // next cycle, positions [1, to - loop]
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

  /// A later edit run (pass order > r) hides `noteId`: shared index of the composed identity. O(log H).
  bool hiddenByEdit(uint32_t noteId, uint8_t r) const {
    return index_ != nullptr && index_->hidden(lease_, noteId, r);
  }

  /// The fit run (DEC-257) holds the row (run `r`, `status`, `noteId`). O(log fit rows).
  bool fitHas(uint8_t r, uint8_t status, uint32_t noteId) const {
    if (fitRun_ == kNoRun) return false;
    const uint32_t i = fitLowerBound(src_[r].run, status, noteId);
    if (i == fitRows_) return false;
    const PassEvent& e = runs_->rowAt(fitRun_, i, *pool_);
    return e.tick == src_[r].run && e.status == status && e.noteId == noteId;
  }
  uint32_t fitLowerBound(uint32_t run, uint8_t status, uint32_t noteId) const {
    uint32_t lo = 0, hi = fitRows_;
    while (lo < hi) {
      const uint32_t mid = lo + (hi - lo) / 2;
      const PassEvent& e = runs_->rowAt(fitRun_, mid, *pool_);
      if (e.tick < run || (e.tick == run && (e.status < status || (e.status == status && e.noteId < noteId))))
        lo = mid + 1;
      else
        hi = mid;
    }
    return lo;
  }
  /// The wrap reach of filtered run `r`: its crossing notes' NoteOffs (`seq`), capped at the loop.
  uint32_t crossReach(uint8_t r) const {
    uint32_t reach = 0;
    for (uint32_t i = fitLowerBound(src_[r].run, kFitCrossStatus, 0); i < fitRows_; ++i) {
      const PassEvent& e = runs_->rowAt(fitRun_, i, *pool_);
      if (e.tick != src_[r].run || e.status != kFitCrossStatus) break;
      const uint32_t x = e.seq >= 2 * loop_ ? loop_ : e.seq - loop_ + 1;
      if (x > reach) reach = x;
    }
    return reach;
  }
  /// A row at or past the loop end has an effect: in a run the fit run filters, only a crossing note's NoteOff.
  bool wraps(uint8_t r, const PassEvent& e) const {
    return !filtered_[r] || (cls(e) == 0 && fitHas(r, kFitCrossStatus, e.noteId));
  }
  /// Q-LEN-1's held-loop status: the writer's flag, toggled by the fit run where the loop length changed it.
  bool held(uint8_t r, const PassEvent& e) const {
    return ((e.origin & kOriginHeldLoop) != 0) != fitHas(r, kFitHeldStatus, e.noteId);
  }

  bool laterPassSounding(uint8_t r, uint8_t pitch) const {
    for (uint8_t j = static_cast<uint8_t>(r + 1); j < count_; ++j)
      if (active_[pitch & 0x7F][j] > 0) return true;
    return false;
  }

  /// Ends open instance `i` at `endPos`: a selected one gets its end (inline, or a TERMINAL when carried in).
  template <uint16_t Cap>
  void endOpen(SpanWindow<Cap>& w, uint8_t i, uint32_t endPos) {
    const Open& a = open_[i];
    if (a.selected && a.entry != kNoEntry) {
      w.entries[a.entry].length = static_cast<uint16_t>(endPos - w.entries[a.entry].tick);
    } else if (a.selected && w.count < Cap) {
      w.entries[w.count++] = SpanEntry{endPos, a.noteId, 0, a.runKey,
                                       static_cast<uint8_t>(0x80 | ((a.channel - 1) & 0x0F)), a.pitch, 0, 1};
      ++w.terminalCount;
    }
    // Order-preserving removal: the list stays in admission order, so the carried state (and the carry list) is a
    // function of the position in a steady cycle, not of how many cycles were composed (checkpoint convergence).
    for (uint8_t k = i; k + 1 < openCount_; ++k) open_[k] = open_[k + 1];
    --openCount_;
  }

  /// A later-pass NoteOn on `pitch` at position `pos` > 0 shortens every open earlier-pass instance on that pitch to
  /// `pos - 1`. At position 0 the only earlier notes sounding are wrap-held ones, which the ratified rule hides outright
  /// (decided at their NoteOn).
  template <uint16_t Cap>
  void shorten(SpanWindow<Cap>& w, uint8_t order, uint8_t pitch, uint32_t pos) {
    if (pos == 0) return;
    for (uint8_t i = 0; i < openCount_;) {
      if (open_[i].pitch == pitch && orderOf(open_[i].runKey) < order)
        endOpen(w, i, pos - 1);  // compacts: re-check index i
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
    refreshHead(r, s.band);
    const uint16_t runKey = makeRunKey(src_[r].run, r);
    if (e.status == kHideNoteStatus) return;  // collected at begin
    if (s.band != 0 && !wraps(r, e)) return;  // DEC-257: past a shorter loop end
    const uint8_t c = cls(e);
    if (c == 0) {
      if (held(r, e)) return;  // Q-LEN-1: the re-strike ends it; its count drops at the sweep end
      if (!edit_[r] && active_[e.data1 & 0x7F][r] > 0) --active_[e.data1 & 0x7F][r];
      for (uint8_t i = 0; i < openCount_; ++i)
        if (open_[i].noteId == e.noteId && open_[i].runKey == runKey) {
          endOpen(w, i, pos);
          break;
        }
      return;
    }
    if (c == 2) {
      // Owner 2026-11-05: at most 32 continuous-controller values per tick reach the window (MidiClass.h). Rows arrive
      // in DEC-244 order, so the same ones are kept every cycle.
      if (controlClass(e.status, e.data1) == ControlClass::Continuous) {
        if (pos != ccPos_) {
          ccPos_ = pos;
          ccCount_ = 0;
        }
        if (ccCount_ >= Config::kMaxContinuousPerTick) {
          ++stats_.continuousLimited;
          return;
        }
        ++ccCount_;
      }
      if (w.count < Cap) {
        w.entries[w.count++] = SpanEntry{pos, e.noteId, kOpenLength, runKey, e.status, e.data1, e.data2, 0};
        ++w.startCount;
      }
      return;
    }
    const uint8_t pitch = e.data1 & 0x7F;
    for (uint8_t i = 0; i < openCount_; ++i)  // Q-LEN-1: the previous iteration ends one tick before this one
      if (open_[i].noteId == e.noteId && open_[i].runKey == runKey) {
        endOpen(w, i, pos == 0 ? 0 : pos - 1);  // at a window's first tick `endLookahead` already ended it
        break;
      }
    lookaheadRows_ = 0;
    const bool hidden = hiddenByEdit(e.noteId, r) || laterPassSounding(r, pitch) ||
                        laterPassStartsWhileSounding(r, e.noteId, pitch, pos) || wrapHeldHiddenAtZero(r, e.noteId, pitch);
    if (lookaheadRows_ > stats_.maxLookaheadRowsPerNoteOn) stats_.maxLookaheadRowsPerNoteOn = lookaheadRows_;
    stats_.lookaheadRowsLastWindow += lookaheadRows_;
    if (!edit_[r]) {  // EDIT-1: an edit run's notes neither shorten earlier passes nor count as occluders
      shorten(w, r, pitch, pos);
      if (active_[pitch][r] < 0xFF) ++active_[pitch][r];
    }
    if (hidden) return;
    // Q-OUT-2: selected iff fewer than T voices are occupied here (selected this cycle, or carried in).
    if (openCount_ >= kMaxOpen) {  // refused and untracked
      ++w.refusedCount;
      ++stats_.openOverflow;
      return;
    }
    const bool selected = occupied() < T && w.count < Cap;
    if (!selected) ++w.refusedCount;
    open_[openCount_++] = Open{e.noteId, runKey, pitch, static_cast<uint8_t>((e.status & 0x0F) + 1),
                               selected ? w.count : kNoEntry, selected, false};
    if (!selected) return;
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
      for (uint32_t i = loopEnd ? hideEnd_[j] : cursor_[j]; i < wrapStart_[j]; ++i) {
        const PassEvent& e = row(j, i);
        if (e.tick != at) break;
        if (cls(e) != 1) continue;
        for (uint8_t k = 0; k < openCount_; ++k)
          if (open_[k].noteId == e.noteId && open_[k].runKey == runKey) {
            endOpen(w, k, b - 1);
            break;
          }
        if (!loopEnd && !edit_[j]) shorten(w, j, e.data1 & 0x7F, b);
      }
    }
    if (!loopEnd) return;
    for (uint8_t j = 0; j < count_; ++j)
      for (uint32_t i = wrapStart_[j]; i < src_[j].rowLimit; ++i) {
        const PassEvent& e = row(j, i);
        if (!edit_[j] && cls(e) == 0 && held(j, e) && wraps(j, e) && active_[e.data1 & 0x7F][j] > 0)
          --active_[e.data1 & 0x7F][j];
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
  static constexpr uint32_t kNoHead = 0xFFFFFFFFu;
  uint32_t headKey_[MaxRuns][3];  // cached stream heads (`refreshHead`), so selecting the next row reads no PSRAM
  uint8_t headPitch_[MaxRuns];    // main-stream head pitch
  uint32_t nextTick_[MaxRuns];    // tick of the main-stream row after the head (kNoHead: none)
  uint32_t hideEnd_[MaxRuns];     // first row after an edit run's leading hide rows (0 for other runs)
  uint8_t count_;
  bool edit_[MaxRuns];
  bool filtered_[MaxRuns];  // DEC-257: the fit run filters this run's rows past the loop end
  RunId fitRun_ = kNoRun;   // the state's fit run, or kNoRun
  uint32_t fitRows_ = 0;
  IndexStore* index_;
  IndexLease lease_;
  IndexResult indexResult_;
  uint8_t active_[128][MaxRuns];  // source notes sounding per (pitch, pass), effective or not (OVL-8)
  Open open_[kMaxOpen];  // effective instances open, in admission order (Q-OUT-2)
  uint8_t openCount_;
  uint32_t loop_;
  uint32_t windowStart_;
  uint32_t windowEnd_;
  bool windowOpen_;
  uint32_t identity_;
  uint32_t cycle_;
  int8_t zeroOnsetLast_[128];  // highest pass order with a NoteOn at tick 0, per pitch (-1: none)
  Stats stats_;
  mutable uint32_t lookaheadRows_;
  uint32_t ccPos_ = UINT32_MAX;  // the position the continuous-controller count belongs to
  uint32_t wrapReach_ = 0;
  uint8_t ccCount_ = 0;
};

}  // namespace ContentEngine
