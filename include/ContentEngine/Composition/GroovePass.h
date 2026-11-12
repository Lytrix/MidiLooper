//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// The groove pass (DEC-287). A grooved state lists
//
//   [ source prefix … ] [ groove pass G (kind kGroove) ] [ passes made after G … ] [ fit run ]
//
// The prefix is kept and saved but not played (`SourceLease` composes the runs before G empty, in place). G is the
// prefix's *effective content* — every effective note and event, read by the extraction composer (128 open, no output
// limit) over the whole stored loop — with the notes in scope moved by the groove (`groove::place`). Notes keep their
// noteId, velocity and length; events keep their places; a note whose sounding version comes from an edit pass keeps
// its composed place (no drift). Same-pitch notes moved onto one tick merge (the earliest pass's noteId stays); a
// same-pitch overlap ends the earlier note one tick before the later.
//
// A rebuild's prefix is the old prefix plus the passes after G up to the last record / overdub pass — never G itself,
// so a groove always starts from the original timing and pass order (precedence) is never reordered; edit passes after
// that stay after the new G. Amount 0: no G, the prefix and the passes after it played as they are.
//
// A background job (DEC-288, background groove build): `start`, then `step` a grain at a time — rows reserved first
// (waiting while pages are held, `TooBig` when it cannot fit), the index and the composer's start, one extraction
// window a grain, the shaping, `kWriteRows` rows a grain — then `Ready`; `SlotTransport::grooveFinish` publishes it
// only on the state it started from. Bounded by `kRows` rows and the extraction composer's 128 open notes, named when
// larger.

#pragma once

#include <cstdint>

#include "ContentEngine/EngineConfig.h"
#include "ContentEngine/Composition/Groove.h"
#include "ContentEngine/Content/ManifestTable.h"
#include "ContentEngine/Content/PassEvent.h"
#include "ContentEngine/Content/SourceRun.h"
#include "ContentEngine/Composition/SpanWindow.h"
#include "ContentEngine/Playback/TerminalScheduler.h"

namespace ContentEngine {

enum class GrooveResult : uint8_t { Ok, TooLarge, TooManyOpen, Index, DeniedRun, DeniedCapacity };

/// What one groove build is asked: the setting, and the stored loop it counts in (its length and the content's
/// start in it; a range scope's start and length, absolute).
struct GrooveRequest {
  groove::Settings settings;
  groove::Table table;  // the template's table (a built-in's, or a groove file's)
  char label[21] = {};  // a file template's name, stored with it
  uint32_t storedTicks = 0, start = 0;
  uint32_t scopeStart = 0, scopeLength = 0;  // used for scope A / B
  // The GROOVE window's columns, stored with the state (heard 0: not from the window, the base's kept).
  uint32_t columnA = 0, columnB = 0;
  uint8_t heard = 0;
};

template <typename Pool, typename Runs, typename Res, typename Index, typename Extraction, uint16_t WindowCap,
          uint32_t Rows>
class GrooveBuilder {
 public:
  static constexpr uint32_t kRows = Rows;  // G's rows at most (notes × 2 + events)
  static constexpr uint32_t kWindowTicks = 768;

  void init(Pool& pool, Runs& runs, Res& res, Index& index, Extraction& composer, SpanWindow<WindowCap>& window,
            PassEvent* rows) {
    pool_ = &pool;
    runs_ = &runs;
    res_ = &res;
    index_ = &index;
    c_ = &composer;
    w_ = &window;
    rows_ = rows;
    c_->init(pool, runs, &index);
    c_->extractAll(lent_, kLent);  // every effective note: 128 tracked, 128 selected
    state_ = State::Idle;  // memory is not constructed: `init` sets the job's fields
    begun_ = false;
    run_ = kNoRun;
    generation_ = grains_ = 0;
  }

  /// The run lists of a rebuild of the state listing `list[0..n)` (no fit run among them): `prefix` (its count
  /// `prefixCount`) and `after` (`afterCount`), per the prefix rule.
  CE_COLD void partition(const RunId* list, uint8_t n, RunId* prefix, uint8_t& prefixCount, RunId* after,
                 uint8_t& afterCount) const {
    int16_t g = -1;
    for (uint8_t i = 0; i < n; ++i)
      if (runs_->run(list[i]).kind == RunKind::kGroove) g = i;
    prefixCount = afterCount = 0;
    if (g < 0) {
      for (uint8_t i = 0; i < n; ++i) prefix[prefixCount++] = list[i];
      return;
    }
    int16_t lastTake = -1;  // the last record / overdub pass after G
    for (uint8_t i = static_cast<uint8_t>(g + 1); i < n; ++i) {
      const uint8_t k = runs_->run(list[i]).kind;
      if (k == RunKind::kRecord || k == RunKind::kOverdub || k == RunKind::kCompacted) lastTake = i;
    }
    for (int16_t i = 0; i < g; ++i) prefix[prefixCount++] = list[i];
    for (int16_t i = g + 1; i <= lastTake; ++i) prefix[prefixCount++] = list[i];  // folded in, in order
    for (int16_t i = lastTake < 0 ? g + 1 : lastTake + 1; i < n; ++i) after[afterCount++] = list[i];
  }

  /// The job's state: `Idle` nothing asked; `Waiting` for rows (the reservation was refused for want
  /// of free pages — held by things that end); `Building` in grains; `Ready` G sealed (or none: the groove removed),
  /// to be published; `TooBig` it cannot fit (more than `kRows` rows, a run's size); `Failed` (index, open notes).
  enum class State : uint8_t { Idle, Waiting, Building, Ready, TooBig, Failed };

  /// Starts a build for `slot` from the state of `identity` whose content runs partition into `prefix` / `after`; any
  /// build in progress is dropped (the newest setting wins). Amount 0: no G — `Ready` at once (the groove removed).
  CE_COLD State start(uint8_t slot, uint32_t identity, const RunId* prefix, uint8_t pc, const RunId* after, uint8_t ac,
                      const GrooveRequest& q) {
    cancel();
    slot_ = slot;
    base_ = identity;
    q_ = q;
    pc_ = pc;
    ac_ = ac;
    for (uint8_t i = 0; i < pc; ++i) prefix_[i] = prefix[i];
    for (uint8_t i = 0; i < ac; ++i) after_[i] = after[i];
    ++generation_;
    if (q.settings.amount == 0) return state_ = State::Ready;
    if (pc == 0 || q.storedTicks == 0) return state_ = State::Failed;
    uint32_t rows = 0;
    for (uint8_t i = 0; i < pc; ++i) rows += runs_->run(prefix[i]).rowCount;
    reserve_ = rows < kRows ? rows : kRows;  // G's rows at most: the prefix's (every effective row comes from one)
    state_ = State::Waiting;
    return state_;
  }
  /// Drops the build: its run, its reservation and the composer's index pin released.
  CE_COLD void cancel() {
    if (begun_) c_->end();
    begun_ = false;
    if (run_ != kNoRun) {
      res_->forgive(run_);
      runs_->seal(run_);
      runs_->release(run_);
    }
    run_ = kNoRun;
    state_ = State::Idle;
  }
  /// One grain: a reservation attempt, the index and the composer's start, one window of the extraction, the shaping,
  /// or up to `kWriteRows` rows written. Returns the state after it.
  CE_COLD State step() {
    switch (state_) {
      case State::Waiting: {
        run_ = runs_->allocate(slot_, RunKind::kGroove);
        if (run_ == kNoRun) return state_;  // no run descriptor free yet: still waiting
        const Admission a = res_->reserve(run_, reserve_);
        if (a != Admission::Ok) {
          runs_->seal(run_);
          runs_->release(run_);
          run_ = kNoRun;
          if (a == Admission::DeniedRunFull) state_ = State::TooBig;
          return state_;
        }
        phase_ = Phase::Prepare;
        return state_ = State::Building;
      }
      case State::Building:
        return build();
      default:
        return state_;
    }
  }
  /// After a publish: the writer's reference to G goes (the manifest holds it); the job is idle.
  void published() {
    if (run_ != kNoRun) runs_->release(run_);
    run_ = kNoRun;
    state_ = State::Idle;
  }
  State state() const { return state_; }
  uint8_t slot() const { return slot_; }
  uint32_t base() const { return base_; }
  uint32_t generation() const { return generation_; }
  const GrooveRequest& request() const { return q_; }
  RunId groovePass() const { return run_; }
  uint8_t prefixCount() const { return pc_; }
  uint8_t afterCount() const { return ac_; }
  RunId prefixRun(uint8_t i) const { return prefix_[i]; }
  RunId afterRun(uint8_t i) const { return after_[i]; }
  /// Diagnostics: grains taken by the last build.
  uint32_t grains() const { return grains_; }
  /// The build's progress in percent (an estimate from its grains: the extraction's windows, the shaping, then
  /// the rows written — the reservation's bound until the rows are known). 0 while waiting, 100 once ready.
  uint8_t progress() const {
    if (state_ == State::Ready) return 100;
    if (state_ != State::Building) return 0;
    const uint32_t windows = (q_.storedTicks + kWindowTicks - 1) / kWindowTicks + 2;  // + prepare, shape
    const uint32_t rows = phase_ == Phase::Write ? rowCount_ : reserve_;
    const uint32_t total = windows + (rows + kWriteRows - 1) / kWriteRows;
    const uint32_t done = phase_ == Phase::Write ? windows + written_ / kWriteRows : grains_;
    const uint32_t p = total == 0 ? 0 : done * 100 / total;
    return static_cast<uint8_t>(p > 99 ? 99 : p);
  }

  static constexpr uint32_t kWriteRows = 512;  // rows written a grain

 private:
  enum class Phase : uint8_t { Prepare, Extract, Close, Shape, Write };
  CE_COLD State build() {
    ++grains_;
    switch (phase_) {
      case Phase::Prepare: {  // the prefix's effective content over the stored loop, under its own view identity
        grains_ = 1;
        count_ = 0;
        open_ = 0;
        written_ = 0;
        for (uint8_t i = 0; i < pc_; ++i) src_[i] = {prefix_[i], runs_->run(prefix_[i]).rowCount};
        const uint32_t view = prefixViewIdentity(base_);
        IndexLease lease;
        const IndexResult ir = index_->acquire(view, src_, pc_, lease);
        if (ir != IndexResult::Ready && ir != IndexResult::NotNeeded) return fail(State::Failed);
        begun_ = c_->begin(src_, pc_, q_.storedTicks, view);
        index_->release(lease);
        if (!begun_) return fail(State::Failed);
        phase_ = Phase::Extract;
        return state_;
      }
      case Phase::Extract: {  // one window of the first cycle
        if (!c_->composeNext(*w_, kWindowTicks)) {
          phase_ = open_ != 0 && c_->nextCycle() ? Phase::Close : Phase::Shape;
          return state_;
        }
        if (collect(0, true) != GrooveResult::Ok) return fail(State::TooBig);
        return state_;
      }
      case Phase::Close: {  // the second cycle only closes notes held over the loop end
        if (open_ == 0 || !c_->composeNext(*w_, kWindowTicks)) {
          phase_ = Phase::Shape;
          return state_;
        }
        collect(q_.storedTicks, false);
        return state_;
      }
      case Phase::Shape: {
        const uint32_t overflow = c_->stats().openOverflow;
        c_->end();
        begun_ = false;
        if (overflow != 0) return fail(State::Failed);
        for (uint32_t i = 0; i < count_; ++i)  // still open after two cycles: held, ends a tick before its next start
          if (rec_(i).isNote() && rec_(i).end == kOpen) rec_(i).end = rec_(i).start + q_.storedTicks - 1;
        moveAndResolve(q_);
        if (!rows(q_.storedTicks)) return fail(State::TooBig);
        phase_ = Phase::Write;
        return state_;
      }
      case Phase::Write: {
        const uint32_t end = written_ + kWriteRows < rowCount_ ? written_ + kWriteRows : rowCount_;
        for (; written_ < end; ++written_) res_->writeOwed(run_, out_[written_]);
        if (written_ < rowCount_) return state_;
        res_->forgive(run_);  // the rows reserved beyond G's
        runs_->seal(run_);
        return state_ = State::Ready;
      }
    }
    return state_;
  }
  State fail(State s) {
    const uint32_t g = grains_;
    cancel();
    grains_ = g;
    return state_ = s;
  }

 private:
  static constexpr uint32_t kOpen = 0xFFFFFFFFu;
  /// A collected record lives in two consecutive rows of the buffer before writing: [start row][end row].
  struct Rec {
    uint32_t start, end, noteId;
    uint8_t status, data1, data2, order;
    bool fromEdit, event;
    bool isNote() const { return !event; }
  };
  // Records are kept in `recs_` (a view over the row buffer: 2 rows = 32 B a record, ample for a Rec).
  Rec& rec_(uint32_t i) { return reinterpret_cast<Rec*>(rows_)[i]; }
  uint32_t recCapacity() const { return kRows / 2; }

  CE_COLD GrooveResult collect(uint32_t base, bool starts) {
    for (uint16_t k = 0; k < w_->count; ++k) {
      const SpanEntry& e = w_->entries[k];
      const uint8_t type = e.status & 0xF0;
      if (e.terminal) {
        for (uint32_t i = 0; i < count_; ++i) {  // its open note
          Rec& x = rec_(i);
          if (x.isNote() && x.end == kOpen && x.noteId == e.noteId && x.order == orderOf(e.runKey)) {
            x.end = base + e.tick;
            --open_;
            break;
          }
        }
        continue;
      }
      if (!starts) continue;
      if (count_ == recCapacity()) return GrooveResult::TooLarge;
      Rec x{};
      x.start = e.tick;
      x.noteId = e.noteId;
      x.status = e.status;
      x.data1 = e.data1;
      x.data2 = e.data2;
      x.order = orderOf(e.runKey);
      x.fromEdit = runs_->run(runIdOf(e.runKey)).kind == RunKind::kEdit;
      x.event = type != 0x90;
      if (x.event) {
        x.end = e.tick;
      } else if (e.length != kOpenLength) {
        x.end = e.tick + e.length;
      } else {
        x.end = kOpen;
        ++open_;
      }
      rec_(count_++) = x;
    }
    return GrooveResult::Ok;
  }

  bool inScope(const GrooveRequest& q, uint32_t abs) const {
    if (q.settings.scope == groove::kFull || q.scopeLength == 0) return true;
    return (abs + q.storedTicks - q.scopeStart) % q.storedTicks < q.scopeLength;
  }

  CE_COLD void moveAndResolve(const GrooveRequest& q) {
    const uint32_t storedTicks = q.storedTicks;
    for (uint32_t i = 0; i < count_; ++i) {
      Rec& x = rec_(i);
      if (x.event || x.fromEdit) continue;
      const uint32_t abs = (x.start + q.start) % storedTicks;
      if (!inScope(q, abs)) continue;
      uint32_t to = groove::place(q.settings, q.table, abs, storedTicks);
      if (!inScope(q, to)) continue;  // a range's note stays inside its range
      const int32_t d = groove::towards(abs, to, storedTicks);
      const uint32_t len = x.end - x.start;
      x.start = groove::wrap(static_cast<int64_t>(x.start) + d, storedTicks);
      x.end = x.start + len;
    }
    // Same pitch (and channel): sorted by (pitch, start, order).
    heapSort(&rec_(0), count_, less);
    uint32_t w = 0;
    for (uint32_t i = 0; i < count_; ++i) {
      Rec x = rec_(i);
      if (w > 0 && x.isNote() && rec_(w - 1).isNote() && samePitch(rec_(w - 1), x)) {
        Rec& p = rec_(w - 1);
        if (p.start == x.start) {  // merged: the earlier pass's noteId stays, the longer end
          if (x.end > p.end) p.end = x.end;
          continue;
        }
        if (p.end >= x.start) p.end = x.start - 1;  // ends one tick before the later
      }
      rec_(w++) = x;
    }
    count_ = w;
  }
  static bool samePitch(const Rec& a, const Rec& b) {
    return (a.status & 0x0F) == (b.status & 0x0F) && a.data1 == b.data1;
  }
  static bool less(const Rec& a, const Rec& b) {
    if (a.event != b.event) return a.event < b.event;  // notes first, then events (their order kept below)
    if (!a.event) {
      if ((a.status & 0x0F) != (b.status & 0x0F)) return (a.status & 0x0F) < (b.status & 0x0F);
      if (a.data1 != b.data1) return a.data1 < b.data1;
    }
    if (a.start != b.start) return a.start < b.start;
    return a.order < b.order;
  }

  /// G's rows: a NoteOn and a NoteOff a note (the NoteOff past the loop end when the note wraps), each event, sorted
  /// in tick order with NoteOffs first at a tick (DEC-244), into the buffer's top (`out_`, `rowCount_`): the records
  /// (at its start) are read before anything overwrites them. False: more rows than reserved or the buffer holds.
  CE_COLD bool rows(uint32_t storedTicks) {
    uint32_t n = 0;
    for (uint32_t i = 0; i < count_; ++i) n += rec_(i).event ? 1 : 2;
    if (n > reserve_ || n > kRows || 2 * count_ > kRows - n) return false;
    out_ = rows_ + (kRows - n);
    uint32_t k = 0;
    for (uint32_t i = 0; i < count_; ++i) {
      const Rec& x = rec_(i);
      PassEvent on{};
      on.tick = x.start;
      on.noteId = x.noteId;
      on.status = x.status;
      on.data1 = x.data1;
      on.data2 = x.data2;
      out_[k++] = on;
      if (x.event) continue;
      PassEvent off{};
      off.tick = x.end;
      off.noteId = x.noteId;
      off.status = static_cast<uint8_t>(0x80 | (x.status & 0x0F));
      off.data1 = x.data1;
      off.origin = x.end - x.start >= storedTicks ? kOriginHeldLoop : 0;
      out_[k++] = off;
    }
    heapSort(out_, k, before);
    for (uint32_t i = 0; i < k; ++i) out_[i].seq = i;
    rowCount_ = k;
    return true;
  }
  /// Row order: tick, then NoteOffs before everything else at a tick (DEC-244), then events, then NoteOns; then noteId.
  static uint8_t cls(const PassEvent& e) {
    const uint8_t t = e.status & 0xF0;
    return t == 0x80 ? 0 : t == 0x90 ? 2 : 1;
  }
  static bool before(const PassEvent& a, const PassEvent& b) {
    if (a.tick != b.tick) return a.tick < b.tick;
    if (cls(a) != cls(b)) return cls(a) < cls(b);
    return a.noteId < b.noteId;
  }
  template <typename X, typename Less>
  static void sift(X* a, uint32_t i, uint32_t n, Less lt) {
    for (;;) {
      uint32_t m = i;
      const uint32_t l = 2 * i + 1, r = l + 1;
      if (l < n && lt(a[m], a[l])) m = l;
      if (r < n && lt(a[m], a[r])) m = r;
      if (m == i) return;
      const X t = a[i];
      a[i] = a[m];
      a[m] = t;
      i = m;
    }
  }
  /// In place, O(n log n), no allocation (the shaping sorts up to 4,096 records, the writing up to 8,192 rows).
  template <typename X, typename Less>
  static void heapSort(X* a, uint32_t n, Less lt) {
    for (uint32_t i = n / 2; i-- > 0;) sift(a, i, n, lt);
    for (uint32_t end = n; end > 1; --end) {
      const X t = a[0];
      a[0] = a[end - 1];
      a[end - 1] = t;
      sift(a, 0, end - 1, lt);
    }
  }

  Pool* pool_ = nullptr;
  Runs* runs_ = nullptr;
  Res* res_ = nullptr;
  Index* index_ = nullptr;
  Extraction* c_ = nullptr;
  SpanWindow<WindowCap>* w_ = nullptr;
  PassEvent* rows_ = nullptr;
  uint32_t count_ = 0, open_ = 0;
  // The job.
  State state_ = State::Idle;
  Phase phase_ = Phase::Prepare;
  bool begun_ = false;
  uint8_t slot_ = 0, pc_ = 0, ac_ = 0;
  uint32_t base_ = 0, generation_ = 0, reserve_ = 0, written_ = 0, rowCount_ = 0, grains_ = 0;
  RunId run_ = kNoRun;
  GrooveRequest q_;
  RunId prefix_[64], after_[64];
  SourceRun src_[64];
  PassEvent* out_ = nullptr;
  static constexpr uint8_t kLent = 128;     // the writer's open-note bound
  typename Extraction::Open lent_[kLent];   // the open list lent to the extraction composer
};

}  // namespace ContentEngine
