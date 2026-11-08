//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// The fit run of a resized loop (DEC-257, owner 2026-11-08: loop length is a play window; notes keep their recorded
// lengths and wrap at the loop end in force; notes past a shorter end are kept, not played).
//
// Content runs stay as recorded; a resized state lists them with one run of kind `kFit` holding lookups, sorted by
// (tick = the content RunId, status, noteId):
// * filter (`kFitFilterStatus`, noteId 0): the run has rows at or past the loop end that are not NoteOffs of notes
//   crossing it — NoteOns or controllers past the end, NoteOffs of notes starting there. `Composer` then reads that
//   run's rows at or past the end only as NoteOffs of its cross notes.
// * cross (`kFitCrossStatus`, `seq` the NoteOff's tick): a note of a filtered run that starts before the end and ends at
//   or past it — it wraps, its NoteOff `tick − loop` into the next cycle.
// * held (`kFitHeldStatus`): a note starting before the end whose held-loop status (length ≥ loop, Q-LEN-1) differs
//   from its `kOriginHeldLoop` flag at this length.
// `build` scans every row of the content runs once (at most `kMaxScanRows`, else TooLarge), holds at most `kMaxOpen`
// notes open per run and `kMaxEntries` lookups (else refused by name), and writes nothing when no lookup is needed (a
// resize that changes no wrap: `None`). No allocation.

#pragma once

#include <cstdint>

#include "ContentEngine/EngineConfig.h"
#include "ContentEngine/PassEvent.h"
#include "ContentEngine/RowReservations.h"

namespace ContentEngine {

enum class FitResult : uint8_t { Ok, None, DeniedRun, DeniedCapacity, TooLarge, TooManyOpen };

template <typename Pool, typename Runs, typename Res>
class FitBuilder {
 public:
  static constexpr uint32_t kMaxScanRows = 32768;
  static constexpr uint16_t kMaxEntries = 256;
  static constexpr uint16_t kMaxOpen = Config::kMaxOpenNotesPerWriter;
  static constexpr uint8_t kMaxRuns = 32;

  /// The fit run of `content[0..n)` (no fit run among them) at loop length `loop`, for `slot`: `fit` is a sealed run
  /// with the writer's reference, or kNoRun when the result is not Ok.
  FitResult build(Pool& pool, Runs& runs, Res& res, uint8_t slot, const RunId* content, uint8_t n, uint32_t loop,
                  RunId& fit) {
    fit = kNoRun;
    if (n > kMaxRuns) return FitResult::TooLarge;
    RunId order[kMaxRuns];
    uint32_t rows = 0;
    for (uint8_t i = 0; i < n; ++i) {
      uint8_t k = i;
      for (; k > 0 && order[k - 1] > content[i]; --k) order[k] = order[k - 1];
      order[k] = content[i];
      rows += runs.run(content[i]).rowCount;
    }
    if (rows > kMaxScanRows) return FitResult::TooLarge;
    count_ = 0;
    for (uint8_t i = 0; i < n; ++i) {
      const FitResult r = scan(pool, runs, order[i], loop);
      if (r != FitResult::Ok) return r;
    }
    if (count_ == 0) return FitResult::None;
    const RunId run = runs.allocate(slot, RunKind::kFit);
    if (run == kNoRun) return FitResult::DeniedRun;
    if (res.reserve(run, count_) != Admission::Ok) {
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
    runs.seal(run);
    fit = run;
    return FitResult::Ok;
  }

 private:
  struct Entry {
    RunId run;
    uint8_t status;
    uint32_t noteId, offTick;
  };
  struct Open {
    uint32_t noteId, tick;
  };

  bool push(RunId run, uint8_t status, uint32_t noteId, uint32_t offTick) {
    if (count_ == kMaxEntries) return false;
    entries_[count_++] = Entry{run, status, noteId, offTick};
    return true;
  }

  FitResult scan(Pool& pool, Runs& runs, RunId id, uint32_t loop) {
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
      if (e.tick >= loop && !push(id, kFitCrossStatus, e.noteId, e.tick)) return FitResult::TooLarge;
      const bool flagged = (e.origin & kOriginHeldLoop) != 0, held = e.tick - on >= loop;
      if (flagged != held && !push(id, kFitHeldStatus, e.noteId, 0)) return FitResult::TooLarge;
    }
    if (!filter) {  // every row past the end wraps a crossing note: nothing to filter, no cross rows needed
      uint16_t w = first;
      for (uint16_t i = first; i < count_; ++i)
        if (entries_[i].status != kFitCrossStatus) entries_[w++] = entries_[i];
      count_ = w;
    } else if (!push(id, kFitFilterStatus, 0, 0)) {
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

  Entry entries_[kMaxEntries];
  Open open_[kMaxOpen];
  uint16_t count_ = 0;
};

}  // namespace ContentEngine
