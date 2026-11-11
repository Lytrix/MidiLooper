//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// The fit run of a resized loop (DEC-257, owner 2026-11-08: loop length is a play window; notes keep their recorded
// lengths and wrap at the loop end in force; notes past a shorter end are kept, not played).
//
// Content runs stay as recorded; a resized state lists them with one run of kind `kFit` holding lookups, sorted by
// (tick = the content run's position in the state's run list, status, noteId). A position, not a RunId (DEC-285): a
// state's run list keeps its order through save / load, slot copy and get a loop, while RunIds are reassigned — a
// lookup naming a RunId pointed at nothing (or another run) after a load and a note past the end came back. `build`'s
// `content` is in the state's order, the fit run listed after it:
// * filter (`kFitFilterStatus`, noteId 0): the run has rows at or past the loop end that are not NoteOffs of notes
//   crossing it — NoteOns or controllers past the end, NoteOffs of notes starting there. `SpanWindowComposer` then reads that
//   run's rows at or past the end only as NoteOffs of its cross notes.
// * cross (`kFitCrossStatus`, `seq` the NoteOff's tick): a note of a filtered run that starts before the end and ends at
//   or past it — it wraps, its NoteOff `tick − loop` into the next cycle.
// * held (`kFitHeldStatus`): a note starting before the end whose held-loop status (length ≥ loop, Q-LEN-1) differs
//   from its `kOriginHeldLoop` flag at this length.
// `build` scans every row of the content runs once (at most `kMaxScanRows`, else TooLarge), holds at most `kMaxOpen`
// notes open per run and `kMaxEntries` lookups (else refused by name), and writes nothing when no lookup is needed (a
// resize that changes no wrap: `None`).
// `rotate` (owner 2026-11-08: LOOP_EDIT's F1 moves the loop start by rotating the content) copies a content run with
// every note `shift` ticks earlier, wrapped at the loop: a note keeps its length, one crossing the new seam wraps (its
// NoteOff past the loop end), NoteOns and controllers kept past a shorter end keep their ticks. Lossless: rotating by
// `loop − shift` gives the rows back. Shares the lookups' scratch (one resize or rotation at a time). No allocation.
// The window (DEC-283, owner 2026-11-10: "the selected bars should always stay absolute"): a state's content is its
// stored loop rotated `start` ticks earlier, the stored loop `frame` ticks long; a fit run's last row says so
// (`kFitWindowStatus`), written by `build` when the window is not the plain one (start 0, the frame the loop).

#pragma once

#include <cstdint>

#include "ContentEngine/EngineConfig.h"
#include "ContentEngine/Composition/Groove.h"
#include "ContentEngine/Content/PassEvent.h"
#include "ContentEngine/Content/RowReservations.h"

namespace ContentEngine {

enum class FitResult : uint8_t { Ok, None, DeniedRun, DeniedCapacity, TooLarge, TooManyOpen };

/// A loop's play window in its stored notes (DEC-283): the content's tick 0 lies `start` ticks into the stored loop,
/// which is `frame` ticks long (at least the loop).
struct LoopWindow {
  uint32_t start = 0, frame = 0;
  // DEC-284 amendment 3: the two last ranges of the step / bar buttons (A the older, B the newer), each a start in the
  // stored loop and a length; length 0: none.
  uint32_t aStart = 0, aLength = 0, bStart = 0, bLength = 0;
  uint32_t groove = 0;  // DEC-287: the packed groove setting of the state's groove pass (0: none)
  uint32_t grooveShape = 0;  // DEC-290: a file template's shape (steps | step << 16; 0: a built-in)
  RunId tableRun = kNoRun;   // DEC-290: the fit run holding its template and label rows (copied into a rebuilt one)
  // The GROOVE window's columns (owner 2026-11-11, `kFitColumnsStatus`): A's and B's packed settings and the column
  // heard (1 OFF, 2 A, 3 B); heard 0: none stored.
  uint32_t columnA = 0, columnB = 0;
  uint8_t heard = 0;
};
/// The window of a state listing `runs[0..n)` at loop `loop`: its fit run's window row, else start 0 and the frame the
/// loop.
/// A file template stored in fit run `fit` (DEC-290): its table and label. False: none there.
template <typename Runs, typename Pool>
bool readTemplate(Runs& runs, Pool& pool, RunId fit, uint32_t shape, groove::Table& t, char* label) {
  if (fit == kNoRun || shape == 0) return false;
  t = groove::Table{};
  t.steps = static_cast<uint16_t>(shape & 0xFFFF);
  t.step = static_cast<uint16_t>(shape >> 16);
  if (t.steps == 0 || t.steps > groove::Table::kMaxSteps) return false;
  if (label != nullptr) label[0] = 0;
  const auto& d = runs.run(fit);
  for (uint32_t r = 0; r < d.rowCount; ++r) {
    const PassEvent& e = runs.rowAt(fit, r, pool);
    if (e.tick != 0xFFFFFFFFu) continue;
    if (e.status == kFitTemplateStatus) {
      const uint16_t v[5] = {static_cast<uint16_t>(e.noteId), static_cast<uint16_t>(e.noteId >> 16),
                             static_cast<uint16_t>(e.seq), static_cast<uint16_t>(e.seq >> 16),
                             static_cast<uint16_t>(e.data1 | (e.data2 << 8))};
      for (uint8_t k = 0; k < 5; ++k) {
        const uint32_t i = e.origin * 5u + k;
        if (i < groove::Table::kMaxSteps) t.off[i] = static_cast<int16_t>(v[k]);
      }
    } else if (e.status == kFitLabelStatus && label != nullptr && e.origin < 2) {
      const char c[10] = {static_cast<char>(e.noteId), static_cast<char>(e.noteId >> 8), static_cast<char>(e.noteId >> 16),
                          static_cast<char>(e.noteId >> 24), static_cast<char>(e.seq), static_cast<char>(e.seq >> 8),
                          static_cast<char>(e.seq >> 16), static_cast<char>(e.seq >> 24), static_cast<char>(e.data1),
                          static_cast<char>(e.data2)};
      for (uint8_t k = 0; k < 10; ++k) label[e.origin * 10 + k] = c[k];
      label[20] = 0;
    }
  }
  return true;
}

template <typename Runs, typename Pool>
LoopWindow windowOf(Runs& runs, Pool& pool, const RunId* list, uint8_t n, uint32_t loop) {
  LoopWindow w{};
  w.frame = loop;
  for (uint8_t i = 0; i < n; ++i) {
    const auto& d = runs.run(list[i]);
    if (d.kind != RunKind::kFit) continue;
    for (uint32_t r = d.rowCount; r > 0; --r) {  // the marker rows sort last; every one read (DEC-287: no fixed count)
      const PassEvent& e = runs.rowAt(list[i], r - 1, pool);
      if (e.tick != 0xFFFFFFFFu) break;
      if (e.status == kFitWindowStatus) {
        w.start = e.seq;
        w.frame = e.noteId;
      } else if (e.status == kFitRangeAStatus) {
        w.aStart = e.seq;
        w.aLength = e.noteId;
      } else if (e.status == kFitRangeBStatus) {
        w.bStart = e.seq;
        w.bLength = e.noteId;
      } else if (e.status == kFitGrooveStatus) {
        w.groove = e.seq;
        w.grooveShape = e.noteId == 1 ? 0 : e.noteId;
      } else if (e.status == kFitTemplateStatus) {
        w.tableRun = list[i];
      } else if (e.status == kFitColumnsStatus) {
        w.columnA = e.seq;
        w.columnB = e.noteId;
        w.heard = e.data1;
      }
    }
  }
  if (w.frame < loop) w.frame = loop;
  if (w.frame != 0) w.start %= w.frame;
  return w;
}

template <typename Pool, typename Runs, typename Res>
class FitBuilder {
 public:
  static constexpr uint32_t kMaxScanRows = 32768;
  static constexpr uint16_t kMaxEntries = 256;
  static constexpr uint16_t kMaxOpen = Config::kMaxOpenNotesPerWriter;
  static constexpr uint8_t kMaxRuns = 32;
  static constexpr uint16_t kRotateOpen = 64;  // notes open at once per rotation segment

  /// The rows of content run `src` rotated `shift` ticks earlier at loop `loop` (0 < shift < loop), in a new sealed run
  /// of the same kind for `slot` (`out`, the writer's reference). Its leading HideNote rows come first, unchanged; the
  /// rest in tick order, NoteOffs first at a tick — three passes over `src` (after the shift, before it, past the end),
  /// merged. A NoteOff whose NoteOn the run does not hold is dropped.
  FitResult rotate(Pool& pool, Runs& runs, Res& res, uint8_t slot, RunId src, uint32_t loop, uint32_t shift,
                   RunId& out) {
    out = kNoRun;
    const auto& d = runs.run(src);
    if (d.rowCount > kMaxScanRows) return FitResult::TooLarge;
    const RunId run = runs.allocate(slot, d.kind);
    if (run == kNoRun) return FitResult::DeniedRun;
    if (res.reserve(run, d.rowCount) != Admission::Ok) {
      runs.seal(run);
      runs.release(run);
      return FitResult::DeniedCapacity;
    }
    uint32_t i = 0;
    for (; i < d.rowCount && runs.rowAt(src, i, pool).status == kHideNoteStatus; ++i)
      res.writeOwed(run, runs.rowAt(src, i, pool));
    Cursor c[3];
    for (uint8_t k = 0; k < 3; ++k) {
      c[k] = Cursor{i, 0, false, PassEvent{}, 0};
      if (!advance(pool, runs, src, d.rowCount, loop, shift, k, c[k])) return refuse(res, runs, run);
    }
    for (;;) {
      int8_t best = -1;
      for (uint8_t k = 0; k < 3; ++k)
        if (c[k].has && (best < 0 || c[k].key < c[best].key)) best = static_cast<int8_t>(k);
      if (best < 0) break;
      res.writeOwed(run, c[best].next);
      if (!advance(pool, runs, src, d.rowCount, loop, shift, static_cast<uint8_t>(best), c[best]))
        return refuse(res, runs, run);
    }
    res.forgive(run);  // dropped orphan NoteOffs
    runs.seal(run);
    out = run;
    return FitResult::Ok;
  }

  /// The rows of run `src` unchanged in a new sealed run of the same kind for `slot` (`out`, the writer's reference): a
  /// slot copy (owner 2026-11-10; a state's runs are its slot's, so another slot takes its own).
  FitResult copy(Pool& pool, Runs& runs, Res& res, uint8_t slot, RunId src, RunId& out) {
    out = kNoRun;
    const auto& d = runs.run(src);
    if (d.rowCount > kMaxScanRows) return FitResult::TooLarge;
    const RunId run = runs.allocate(slot, d.kind);
    if (run == kNoRun) return FitResult::DeniedRun;
    if (res.reserve(run, d.rowCount) != Admission::Ok) {
      runs.seal(run);
      runs.release(run);
      return FitResult::DeniedCapacity;
    }
    for (uint32_t i = 0; i < d.rowCount; ++i) res.writeOwed(run, runs.rowAt(src, i, pool));
    runs.seal(run);
    out = run;
    return FitResult::Ok;
  }

  /// The fit run of `content[0..n)` (no fit run among them) at loop length `loop`, for `slot`: `fit` is a sealed run
  /// with the writer's reference, or kNoRun when the result is not Ok. `window` (DEC-283): the state's window, written
  /// as the last row unless it is the plain one.
  FitResult build(Pool& pool, Runs& runs, Res& res, uint8_t slot, const RunId* content, uint8_t n, uint32_t loop,
                  RunId& fit, LoopWindow window = LoopWindow{}, const groove::Table* table = nullptr,
                  const char* label = nullptr) {
    fit = kNoRun;
    if (n > kMaxRuns) return FitResult::TooLarge;
    uint32_t rows = 0;
    for (uint8_t i = 0; i < n; ++i) rows += runs.run(content[i]).rowCount;
    if (rows > kMaxScanRows) return FitResult::TooLarge;
    count_ = 0;
    for (uint8_t i = 0; i < n; ++i) {  // in the state's order: the lookups sort by position
      const FitResult r = scan(pool, runs, content[i], i, loop);
      if (r != FitResult::Ok) return r;
    }
    const bool windowed = window.start != 0 || (window.frame != 0 && window.frame != loop);
    const bool rangeA = window.aLength != 0;
    const bool rangeB = window.bLength != 0;
    const bool grooved = window.groove != 0;
    // DEC-290: a file template's rows — given (a new groove) or copied from the base's fit run (a rebuild).
    const bool fileTemplate = grooved && window.grooveShape != 0 && (table != nullptr || window.tableRun != kNoRun);
    const uint16_t steps = static_cast<uint16_t>(window.grooveShape & 0xFFFF);
    const uint32_t templateRows = fileTemplate ? (steps + 4u) / 5u + 2u : 0u;
    const bool columns = window.heard != 0;
    const uint32_t marks = (windowed ? 1u : 0u) + (rangeA ? 1u : 0u) + (rangeB ? 1u : 0u) + (grooved ? 1u : 0u) +
                           templateRows + (columns ? 1u : 0u);
    if (count_ == 0 && marks == 0) return FitResult::None;
    const RunId run = runs.allocate(slot, RunKind::kFit);
    if (run == kNoRun) return FitResult::DeniedRun;
    if (res.reserve(run, count_ + marks) != Admission::Ok) {
      runs.seal(run);
      runs.release(run);
      return FitResult::DeniedCapacity;
    }
    for (uint16_t i = 0; i < count_; ++i) {
      PassEvent e{};
      e.tick = entries_[i].run;
      e.noteId = entries_[i].noteId;
      e.seq = entries_[i].offTick;
      e.status = entries_[i].status;
      res.writeOwed(run, e);
    }
    const auto mark = [&](uint8_t status, uint32_t start, uint32_t length) {
      PassEvent e{};
      e.tick = 0xFFFFFFFFu;
      e.noteId = length;
      e.seq = start;
      e.status = status;
      res.writeOwed(run, e);
    };
    if (windowed) mark(kFitWindowStatus, window.start, window.frame);  // sorted: window, range A, range B, groove
    if (rangeA) mark(kFitRangeAStatus, window.aStart, window.aLength);
    if (rangeB) mark(kFitRangeBStatus, window.bStart, window.bLength);
    if (grooved) mark(kFitGrooveStatus, window.groove, window.grooveShape != 0 ? window.grooveShape : 1);
    if (fileTemplate) {
      groove::Table t;
      char name[21] = {};
      if (table != nullptr) {
        t = *table;
        for (uint8_t k = 0; label != nullptr && k < 20 && label[k] != 0; ++k) name[k] = label[k];
      } else {
        readTemplate(runs, pool, window.tableRun, window.grooveShape, t, name);
      }
      for (uint32_t row = 0; row * 5 < steps; ++row) {
        PassEvent e{};
        e.tick = 0xFFFFFFFFu;
        e.status = kFitTemplateStatus;
        const auto v = [&](uint32_t k) { return static_cast<uint16_t>(row * 5 + k < steps ? t.off[row * 5 + k] : 0); };
        e.noteId = v(0) | (static_cast<uint32_t>(v(1)) << 16);
        e.seq = v(2) | (static_cast<uint32_t>(v(3)) << 16);
        e.data1 = static_cast<uint8_t>(v(4));
        e.data2 = static_cast<uint8_t>(v(4) >> 8);
        e.origin = static_cast<uint8_t>(row);
        res.writeOwed(run, e);
      }
      for (uint8_t row = 0; row < 2; ++row) {
        const char* c = name + row * 10;
        PassEvent e{};
        e.tick = 0xFFFFFFFFu;
        e.status = kFitLabelStatus;
        e.noteId = static_cast<uint8_t>(c[0]) | (static_cast<uint8_t>(c[1]) << 8) | (static_cast<uint8_t>(c[2]) << 16) |
                   (static_cast<uint32_t>(static_cast<uint8_t>(c[3])) << 24);
        e.seq = static_cast<uint8_t>(c[4]) | (static_cast<uint8_t>(c[5]) << 8) | (static_cast<uint8_t>(c[6]) << 16) |
                (static_cast<uint32_t>(static_cast<uint8_t>(c[7])) << 24);
        e.data1 = static_cast<uint8_t>(c[8]);
        e.data2 = static_cast<uint8_t>(c[9]);
        e.origin = row;
        res.writeOwed(run, e);
      }
    }
    if (columns) {  // the GROOVE window's columns (owner 2026-11-11): the last row (statuses in order)
      PassEvent e{};
      e.tick = 0xFFFFFFFFu;
      e.status = kFitColumnsStatus;
      e.seq = window.columnA;
      e.noteId = window.columnB;
      e.data1 = window.heard;
      res.writeOwed(run, e);
    }
    runs.seal(run);
    fit = run;
    return FitResult::Ok;
  }

 private:
  struct Entry {
    uint16_t run;  // the content run's position in the state's run list
    uint8_t status;
    uint32_t noteId, offTick;
  };
  struct Open {
    uint32_t noteId, tick;
  };
  struct Cursor {
    uint32_t i;
    uint16_t open;
    bool has;
    PassEvent next;
    uint32_t key;  // new tick << 2 | class (DEC-244: NoteOffs first at a tick)
  };
  /// The segment of a NoteOn or controller at `t`: 0 after the shift, 1 before it, 2 at or past the loop end.
  static uint8_t segment(uint32_t t, uint32_t loop, uint32_t shift) { return t >= loop ? 2 : t >= shift ? 0 : 1; }
  static uint32_t moved(uint32_t t, uint8_t seg, uint32_t loop, uint32_t shift) {
    return seg == 2 ? t : seg == 0 ? t - shift : t + loop - shift;
  }
  /// Cursor `k`'s next row of its segment (false: more than `kRotateOpen` of its notes open).
  bool advance(Pool& pool, Runs& runs, RunId src, uint32_t rows, uint32_t loop, uint32_t shift, uint8_t k, Cursor& c) {
    uint32_t* ids = ids_[k];
    for (; c.i < rows; ++c.i) {
      const PassEvent& e = runs.rowAt(src, c.i, pool);
      if (e.status == kHideNoteStatus) continue;
      const uint8_t t = e.status & 0xF0;
      const bool off = t == 0x80 || (t == 0x90 && e.data2 == 0);
      uint8_t cls = 2;
      if (off) {
        uint16_t j = 0;
        while (j < c.open && ids[j] != e.noteId) ++j;
        if (j == c.open) continue;
        ids[j] = ids[--c.open];
        cls = 0;
      } else {
        if (segment(e.tick, loop, shift) != k) continue;
        if (t == 0x90) {
          if (c.open == kRotateOpen) return false;
          ids[c.open++] = e.noteId;
          cls = 1;
        }
      }
      c.next = e;
      c.next.tick = moved(e.tick, k, loop, shift);
      c.key = (c.next.tick << 2) | cls;
      c.has = true;
      ++c.i;
      return true;
    }
    c.has = false;
    return true;
  }
  FitResult refuse(Res& res, Runs& runs, RunId run) {
    res.forgive(run);  // the rows it will not write
    runs.seal(run);
    runs.release(run);
    return FitResult::TooManyOpen;
  }

  bool push(uint16_t run, uint8_t status, uint32_t noteId, uint32_t offTick) {
    if (count_ == kMaxEntries) return false;
    entries_[count_++] = Entry{run, status, noteId, offTick};
    return true;
  }

  FitResult scan(Pool& pool, Runs& runs, RunId id, uint8_t position, uint32_t loop) {
    const uint16_t first = count_;
    bool filter = false;
    uint16_t open = 0;
    const uint32_t rowCount = runs.run(id).rowCount;
    for (uint32_t i = 0; i < rowCount; ++i) {
      const PassEvent& e = runs.rowAt(id, i, pool);
      if (e.status == kHideNoteStatus) continue;
      const uint8_t t = e.status & 0xF0;
      const bool off = t == 0x80 || (t == 0x90 && e.data2 == 0);
      if (t == 0x90 && !off) {
        if (e.tick >= loop) filter = true;
        if (open == kMaxOpen) return FitResult::TooManyOpen;
        open_[open++] = Open{e.noteId, e.tick};
        continue;
      }
      if (!off) {
        if (e.tick >= loop) filter = true;  // a controller past the end
        continue;
      }
      uint16_t k = 0;
      while (k < open && open_[k].noteId != e.noteId) ++k;
      if (k == open) {
        if (e.tick >= loop) filter = true;
        continue;
      }
      const uint32_t on = open_[k].tick;
      open_[k] = open_[--open];
      if (on >= loop) continue;  // starts past the end (its NoteOn set the filter): kept, not played
      if (e.tick >= loop && !push(position, kFitCrossStatus, e.noteId, e.tick)) return FitResult::TooLarge;
      const bool flagged = (e.origin & kOriginHeldLoop) != 0, held = e.tick - on >= loop;
      if (flagged != held && !push(position, kFitHeldStatus, e.noteId, 0)) return FitResult::TooLarge;
    }
    if (!filter) {  // every row past the end wraps a crossing note: nothing to filter, no cross rows needed
      uint16_t w = first;
      for (uint16_t i = first; i < count_; ++i)
        if (entries_[i].status != kFitCrossStatus) entries_[w++] = entries_[i];
      count_ = w;
    } else if (!push(position, kFitFilterStatus, 0, 0)) {
      return FitResult::TooLarge;
    }
    for (uint16_t i = static_cast<uint16_t>(first + 1); i < count_; ++i) {  // (status, noteId) within the run
      const Entry x = entries_[i];
      uint16_t j = i;
      for (; j > first && (entries_[j - 1].status > x.status ||
                           (entries_[j - 1].status == x.status && entries_[j - 1].noteId > x.noteId));
           --j)
        entries_[j] = entries_[j - 1];
      entries_[j] = x;
    }
    return FitResult::Ok;
  }

  union {
    Entry entries_[kMaxEntries];
    uint32_t ids_[3][kRotateOpen];  // a rotation's open notes per segment
  };
  Open open_[kMaxOpen];
  uint16_t count_ = 0;
};

}  // namespace ContentEngine
