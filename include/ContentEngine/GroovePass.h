//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// The groove pass (DEC-287, groove design `docs/Plans/groove_quantize_design.md` §§ 1–5). A grooved state lists
//
//   [ source prefix … ] [ groove pass G (kind kGroove) ] [ passes made after G … ] [ fit run ]
//
// The prefix is kept and saved but not played (`SourceLease` composes the runs before G empty, in place). G is the
// prefix's *effective content* — every effective note and event, read by the extraction composer (DEC-286: 128 open, no
// output limit) over the whole stored loop — with the notes in scope moved by the groove (`groove::place`). Notes keep
// their noteId, velocity and length; events keep their places; a note whose sounding version comes from an edit pass
// keeps its composed place (no drift). Same-pitch notes moved onto one tick merge (the earliest pass's noteId stays);
// a same-pitch overlap ends the earlier note one tick before the later.
//
// A rebuild's prefix is the old prefix plus the passes after G up to the last record / overdub pass — never G itself,
// so a groove always starts from the original timing and pass order (precedence) is never reordered; edit passes after
// that stay after the new G. Amount 0: no G, the prefix and the passes after it played as they are.
//
// Synchronous here (slice 2): bounded by `kRows` rows and the extraction composer's 128 open notes, named when larger.
// The background build — reservation, waiting, cancelling, generation-qualified publish — is slice 3.

#pragma once

#include <cstdint>

#include "ContentEngine/EngineConfig.h"
#include "ContentEngine/Groove.h"
#include "ContentEngine/ManifestTable.h"
#include "ContentEngine/PassEvent.h"
#include "ContentEngine/SourceRun.h"
#include "ContentEngine/SpanWindow.h"
#include "ContentEngine/TerminalScheduler.h"

namespace ContentEngine {

enum class GrooveResult : uint8_t { Ok, TooLarge, TooManyOpen, Index, DeniedRun, DeniedCapacity };

/// What one groove build is asked: the setting, and the stored loop it counts in (DEC-283: its length and the content's
/// start in it; a range scope's start and length, absolute).
struct GrooveRequest {
  groove::Settings settings;
  uint32_t frame = 0, start = 0;
  uint32_t scopeStart = 0, scopeLength = 0;  // used for scope A / B
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
    c_->extractAll(lent_, kLent);  // every effective note: 128 tracked, 128 selected (DEC-286)
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

  /// G for `prefix[0..n)` (the base state's `identity`), as a sealed run of `slot` with the writer's reference (`out`).
  CE_COLD GrooveResult build(uint8_t slot, uint32_t identity, const RunId* prefix, uint8_t n, const GrooveRequest& q,
                     RunId& out) {
    out = kNoRun;
    if (n == 0 || q.frame == 0) return GrooveResult::TooLarge;
    count_ = 0;
    open_ = 0;
    // The prefix's effective content over the stored loop (its own view identity, DEC-286).
    SourceRun src[64];
    for (uint8_t i = 0; i < n; ++i) src[i] = {prefix[i], runs_->run(prefix[i]).rowCount};
    const uint32_t view = prefixViewIdentity(identity);
    IndexLease lease;
    const IndexResult ir = index_->acquire(view, src, n, lease);
    if (ir != IndexResult::Ready && ir != IndexResult::NotNeeded) return GrooveResult::Index;
    const bool begun = c_->begin(src, n, q.frame, view);
    index_->release(lease);
    if (!begun) return GrooveResult::Index;
    GrooveResult r = GrooveResult::Ok;
    bool first = true;
    for (uint8_t cycle = 0; cycle < 2 && r == GrooveResult::Ok; ++cycle) {  // the second only closes held notes
      if (cycle == 1 && open_ == 0) break;
      if (cycle == 1 && !c_->nextCycle()) break;
      while (r == GrooveResult::Ok && c_->composeNext(*w_, kWindowTicks)) {
        r = collect(cycle == 0 ? 0 : q.frame, cycle == 0);
        if (cycle == 1 && open_ == 0) break;
      }
      first = false;
    }
    (void)first;
    const uint32_t overflow = c_->stats().openOverflow;
    c_->end();
    if (r != GrooveResult::Ok) return r;
    if (overflow != 0) return GrooveResult::TooManyOpen;
    for (uint32_t i = 0; i < count_; ++i)  // still open after two cycles: held, ends a tick before its next start
      if (rec_(i).isNote() && rec_(i).end == kOpen) rec_(i).end = rec_(i).start + q.frame - 1;
    moveAndResolve(q);
    return write(slot, q.frame, out);
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
    return (abs + q.frame - q.scopeStart) % q.frame < q.scopeLength;
  }

  CE_COLD void moveAndResolve(const GrooveRequest& q) {
    const uint32_t frame = q.frame;
    for (uint32_t i = 0; i < count_; ++i) {
      Rec& x = rec_(i);
      if (x.event || x.fromEdit) continue;
      const uint32_t abs = (x.start + q.start) % frame;
      if (!inScope(q, abs)) continue;
      uint32_t to = groove::place(q.settings, abs, frame);
      if (!inScope(q, to)) continue;  // a range's note stays inside its range
      const int32_t d = groove::towards(abs, to, frame);
      const uint32_t len = x.end - x.start;
      x.start = groove::wrap(static_cast<int64_t>(x.start) + d, frame);
      x.end = x.start + len;
    }
    // Same pitch (and channel): sort by (pitch, start, order) — insertion over a nearly sorted list.
    for (uint32_t i = 1; i < count_; ++i) {
      const Rec x = rec_(i);
      uint32_t j = i;
      while (j > 0 && less(x, rec_(j - 1))) {
        rec_(j) = rec_(j - 1);
        --j;
      }
      rec_(j) = x;
    }
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

  /// G's rows: a NoteOn and a NoteOff a note (the NoteOff past the loop end when the note wraps), each event, in tick
  /// order with NoteOffs first at a tick (DEC-244). Written from the end of the buffer down so the records (at its
  /// start) are read before they are overwritten.
  CE_COLD GrooveResult write(uint8_t slot, uint32_t frame, RunId& out) {
    uint32_t rows = 0;
    for (uint32_t i = 0; i < count_; ++i) rows += rec_(i).event ? 1 : 2;
    if (rows > kRows) return GrooveResult::TooLarge;
    // Rows into the buffer's top part: records occupy [0, 2 × count_) rows' space; the rows go to [kRows − rows, kRows).
    if (2 * count_ > kRows - rows) return GrooveResult::TooLarge;
    PassEvent* r = rows_ + (kRows - rows);
    uint32_t n = 0;
    for (uint32_t i = 0; i < count_; ++i) {
      const Rec& x = rec_(i);
      PassEvent on{};
      on.tick = x.start;
      on.noteId = x.noteId;
      on.status = x.status;
      on.data1 = x.data1;
      on.data2 = x.data2;
      r[n++] = on;
      if (x.event) continue;
      PassEvent off{};
      off.tick = x.end;
      off.noteId = x.noteId;
      off.status = static_cast<uint8_t>(0x80 | (x.status & 0x0F));
      off.data1 = x.data1;
      off.origin = x.end - x.start >= frame ? kOriginHeldLoop : 0;
      r[n++] = off;
    }
    heapSort(r, n);
    for (uint32_t i = 0; i < n; ++i) r[i].seq = i;
    const RunId run = runs_->allocate(slot, RunKind::kGroove);
    if (run == kNoRun) return GrooveResult::DeniedRun;
    if (res_->reserve(run, n) != Admission::Ok) {
      runs_->seal(run);
      runs_->release(run);
      return GrooveResult::DeniedCapacity;
    }
    for (uint32_t i = 0; i < n; ++i) res_->writeOwed(run, r[i]);
    runs_->seal(run);
    out = run;
    return GrooveResult::Ok;
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
  CE_COLD static void sift(PassEvent* a, uint32_t i, uint32_t n) {
    for (;;) {
      uint32_t m = i;
      const uint32_t l = 2 * i + 1, r = l + 1;
      if (l < n && before(a[m], a[l])) m = l;
      if (r < n && before(a[m], a[r])) m = r;
      if (m == i) return;
      const PassEvent t = a[i];
      a[i] = a[m];
      a[m] = t;
      i = m;
    }
  }
  CE_COLD static void heapSort(PassEvent* a, uint32_t n) {
    for (uint32_t i = n / 2; i-- > 0;) sift(a, i, n);
    for (uint32_t end = n; end > 1; --end) {
      const PassEvent t = a[0];
      a[0] = a[end - 1];
      a[end - 1] = t;
      sift(a, 0, end - 1);
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
  static constexpr uint8_t kLent = 128;     // the writer's open-note bound
  typename Extraction::Open lent_[kLent];   // the open list lent to the extraction composer
};

}  // namespace ContentEngine
