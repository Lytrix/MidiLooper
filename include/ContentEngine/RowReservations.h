//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// Row reservations of the content engine (fixed-memory-engine design clause CE-A1).
//
// A reservation is an *outstanding, unwritten* row an accepted input already owes, e.g. the NoteOff of an accepted
// NoteOn. It belongs to the run the row will be written into (a closing run keeps its own NoteOffs). Rows are converted
// to pages per run: `pagesToAppend(run, outstanding)` is the data + directory pages that run still needs from the pool
// to write everything it owes. The ledger keeps
//
//     content pages free  >=  sum over runs of pagesToAppend(run, outstanding(run))           (invariant R)
//
// so every owed row can be written without asking anything of the pool that another holder could have taken.
// New input is admitted only if its own row *and* the rows it will owe fit on top of that; an owed row is then written
// unconditionally. Capacity is replenished only by pages being free: a completed note's rows stay content, and the next
// NoteOn must find room for its own row and its own NoteOff.

#pragma once

#include <cstdint>

#include "ContentEngine/EngineConfig.h"
#include "ContentEngine/RunTable.h"

namespace ContentEngine {

enum class Admission : uint8_t { Ok, DeniedCapacity, DeniedRunFull, DeniedNotWriting };

template <typename Pool, typename Runs>
class RowReservations {
 public:
  /// `fixedReservePages`: pool pages that belong to other partitions (compaction, edit index) and are never content.
  /// `previewPages` (plan §6.2 fix 1): the preview partition — preview runs (`Runs::isPreview`) take their pages from it
  /// alone, and content never takes its unused pages.
  void init(Pool& pool, Runs& runs, uint32_t fixedReservePages, uint32_t previewPages = 0) {
    pool_ = &pool;
    runs_ = &runs;
    fixedReservePages_ = fixedReservePages;
    previewPartition_ = previewPages;
    for (auto& o : outstanding_) o = 0;
    reservedPages_ = 0;
    previewReserved_ = 0;
  }

  /// Pool pages content may still take: free pages minus the untouched fixed partitions and every preview-partition
  /// page no preview run holds yet (reserved for one or not).
  uint32_t contentFreePages() const {
    const uint32_t previewHeld = runs_->previewPages();
    const uint32_t free = pool_->freeCount(),
                   held = fixedReservePages_ + (previewPartition_ > previewHeld ? previewPartition_ - previewHeld : 0);
    return free > held ? free - held : 0;
  }
  /// Pages the preview partition has left: not held by a preview run nor reserved for one.
  uint32_t previewFreePages() const {
    const uint32_t used = runs_->previewPages() + previewReserved_;
    return previewPartition_ > used ? previewPartition_ - used : 0;
  }

  uint32_t reservedPages() const { return reservedPages_; }
  uint32_t outstanding(RunId run) const { return outstanding_[run]; }

  /// New input: write `row` now into `run` and take on `newObligations` owed rows for the same run.
  /// Refused (nothing written, nothing reserved) unless the row and every row now owed by `run` still fit.
  Admission accept(RunId run, const PassEvent& row, uint16_t newObligations) {
    const auto& r = runs_->run(run);
    if (r.state != RunState::Writing) return Admission::DeniedNotWriting;
    const uint32_t before = need(r, outstanding_[run]);
    const uint32_t total = static_cast<uint32_t>(outstanding_[run]) + 1u + newObligations;
    if (static_cast<uint64_t>(r.rowCount) + total > static_cast<uint64_t>(Runs::kMaxPagesPerRun) * Runs::kRowsPerPage)
      return Admission::DeniedRunFull;
    const uint32_t after = need(r, total);
    if (!fits(run, before, after)) return Admission::DeniedCapacity;
    // Guaranteed by the check above: the append cannot fail.
    const uint32_t others = ledger(run) - before;
    runs_->append(run, row, *pool_);
    outstanding_[run] += newObligations;
    ledger(run) = others + need(runs_->run(run), outstanding_[run]);
    return Admission::Ok;
  }

  /// New input that writes nothing yet: `run` takes on `rows` owed rows (a NOTE_EDIT action reserving the edit run it
  /// will write in grains). Refused (nothing reserved) unless they and every row the run already owes fit.
  Admission reserve(RunId run, uint32_t rows) {
    const auto& r = runs_->run(run);
    if (r.state != RunState::Writing) return Admission::DeniedNotWriting;
    const uint32_t before = need(r, outstanding_[run]);
    const uint32_t total = static_cast<uint32_t>(outstanding_[run]) + rows;
    if (static_cast<uint64_t>(r.rowCount) + total > static_cast<uint64_t>(Runs::kMaxPagesPerRun) * Runs::kRowsPerPage)
      return Admission::DeniedRunFull;
    const uint32_t after = need(r, total);
    if (!fits(run, before, after)) return Admission::DeniedCapacity;
    outstanding_[run] = total;
    ledger(run) = ledger(run) - before + after;
    return Admission::Ok;
  }

  /// Forgives every row `run` still owes: its owner abandoned the work (a superseded NOTE_EDIT rebuild). O(1).
  void forgive(RunId run) {
    ledger(run) -= need(runs_->run(run), outstanding_[run]);
    outstanding_[run] = 0;
  }

  /// Writes one row `run` owes (a NoteOff, a closure). Never refused while the run owes rows: invariant R guarantees the
  /// pages. Returns false only for a caller error (nothing owed), which writes nothing.
  bool writeOwed(RunId run, const PassEvent& row) {
    if (outstanding_[run] == 0) return false;
    const uint32_t before = need(runs_->run(run), outstanding_[run]);
    if (runs_->append(run, row, *pool_) != AppendResult::Ok) return false;  // unreachable while R holds
    --outstanding_[run];
    ledger(run) = ledger(run) - before + need(runs_->run(run), outstanding_[run]);
    return true;
  }

  /// Invariant R plus the cached total, recomputed from scratch. O(runs); tests and diagnostics.
  bool check() const {
    uint32_t sum = 0, preview = 0;
    for (RunId id = 0; id < Runs::kRunCount; ++id) {
      const auto& r = runs_->run(id);
      if (outstanding_[id] != 0 && r.state != RunState::Writing) return false;
      if (r.state == RunState::Writing) (runs_->isPreview(id) ? preview : sum) += need(r, outstanding_[id]);
    }
    return sum == reservedPages_ && contentFreePages() >= reservedPages_ && preview == previewReserved_ &&
           runs_->previewPages() + previewReserved_ <= previewPartition_;
  }

 private:
  /// The pages reserved on `run`'s side: the preview partition's, or content's.
  uint32_t& ledger(RunId run) { return runs_->isPreview(run) ? previewReserved_ : reservedPages_; }
  /// `run`, owing pages `before`, may owe `after`: within the preview partition, or what content has free.
  bool fits(RunId run, uint32_t before, uint32_t after) const {
    if (runs_->isPreview(run))
      return runs_->previewPages() + (previewReserved_ - before) + after <= previewPartition_;
    return contentFreePages() >= (reservedPages_ - before) + after;
  }
  static uint32_t need(const typename Runs::Run& r, uint32_t rows) {
    return pagesToAppend<Runs::kRowsPerPage, Runs::kIdsPerDirPage, Runs::kInlinePages>(r.pageCount, r.rowCount, rows);
  }

  Pool* pool_ = nullptr;
  Runs* runs_ = nullptr;
  uint32_t fixedReservePages_ = 0;
  uint32_t reservedPages_ = 0;
  uint32_t previewPartition_ = 0, previewReserved_ = 0;
  uint32_t outstanding_[Runs::kRunCount];  // a loaded take may owe more than 65,535 rows
};

}  // namespace ContentEngine
