//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// Global run descriptors of the content engine. A run is an append-only sequence of 16-byte rows on pool pages: the
// first `kInlinePagesPerRun` page ids live in the descriptor, later ones in directory pages drawn from the same pool
// (one directory page lists `kIdsPerDirPage` data pages). Descriptors are pooled across all slots and refcounted by the
// manifests that list them; a released run returns its pages through bounded reclaim steps, never in one call.

#pragma once

#include <cstdint>

#include "ContentEngine/EngineConfig.h"
#include "ContentEngine/PagePool.h"

namespace ContentEngine {

enum class RunState : uint8_t { Free = 0, Writing, Sealed, Reclaiming };
enum class AppendResult : uint8_t { Ok, NoPage, RunFull, NotWriting };

/// Pages needed to append `rows` more rows to a run that holds `pageCount` data pages and `rowCount` rows: new data
/// pages plus the directory pages those data pages need. Pure and O(1); the reservation ledger is built on it.
template <uint16_t RowsPerPage, uint32_t IdsPerDirPage, uint8_t InlinePages>
constexpr uint32_t pagesToAppend(uint32_t pageCount, uint32_t rowCount, uint32_t rows) {
  const uint32_t room = pageCount * RowsPerPage - rowCount;
  if (rows <= room) return 0;
  const uint32_t dataPages = (rows - room + RowsPerPage - 1) / RowsPerPage;
  // A directory page is opened when data page index i (0-based) satisfies i >= InlinePages and
  // (i - InlinePages) % IdsPerDirPage == 0. Count those indices in [pageCount, pageCount + dataPages).
  auto dirsBelow = [](uint32_t pages) -> uint32_t {
    return pages <= InlinePages ? 0u : (pages - InlinePages + IdsPerDirPage - 1) / IdsPerDirPage;
  };
  return dataPages + dirsBelow(pageCount + dataPages) - dirsBelow(pageCount);
}

template <uint16_t RunCount, typename Pool, uint8_t InlinePages = Config::kInlinePagesPerRun,
          uint8_t MaxDirPages = 8>
class RunTable {
 public:
  static constexpr uint16_t kRunCount = RunCount;
  static constexpr uint8_t kInlinePages = InlinePages;
  static constexpr uint16_t kRowsPerPage = Pool::kRowsPerPage;
  static constexpr uint32_t kIdsPerDirPage = Pool::kIdsPerDirPage;
  static constexpr uint32_t kMaxPagesPerRun = InlinePages + static_cast<uint32_t>(MaxDirPages) * kIdsPerDirPage;
  static_assert(RunCount < kOwnerReservedBase, "run ids double as page owner tags");

  struct Run {
    PageId inlinePages[InlinePages];
    PageId dirPages[MaxDirPages];
    uint32_t rowCount;
    uint32_t serial;
    uint16_t pageCount;
    uint16_t refs;
    uint8_t dirPageCount;
    uint8_t slot;
    uint8_t kind;
    RunState state;
  };

  void init() {
    for (uint16_t i = 0; i < RunCount; ++i) {
      clear(runs_[i]);
      nextFree_[i] = static_cast<RunId>(i + 1 < RunCount ? i + 1 : kNoRun);
    }
    freeHead_ = RunCount ? 0 : kNoRun;
    freeCount_ = RunCount;
    nextSerial_ = 1;
  }

  /// O(1). A new run in `Writing` state with one reference (its writer). `kNoRun` when the table is full; the
  /// reservation ledger reserves descriptors before an interaction is accepted, so a writer never sees that.
  RunId allocate(uint8_t slot, uint8_t kind) {
    if (freeHead_ == kNoRun) return kNoRun;
    const RunId id = freeHead_;
    freeHead_ = nextFree_[id];
    --freeCount_;
    Run& r = runs_[id];
    clear(r);
    r.state = RunState::Writing;
    r.refs = 1;
    r.slot = slot;
    r.kind = kind;
    r.serial = nextSerial_++;
    return id;
  }

  /// Appends one row, taking data/directory pages from `pool` as the run crosses page boundaries. The caller's
  /// reservation decides whether this may be asked; `NoPage` here means that accounting was violated.
  AppendResult append(RunId id, const PassEvent& row, Pool& pool) {
    if (id >= RunCount || runs_[id].state != RunState::Writing) return AppendResult::NotWriting;
    Run& r = runs_[id];
    if (r.rowCount == static_cast<uint32_t>(r.pageCount) * kRowsPerPage) {
      if (r.pageCount >= kMaxPagesPerRun) return AppendResult::RunFull;
      const uint32_t index = r.pageCount;
      const bool needsDir = index >= InlinePages && (index - InlinePages) % kIdsPerDirPage == 0;
      if (pool.freeCount() < (needsDir ? 2u : 1u)) return AppendResult::NoPage;
      if (needsDir) {
        const PageId dir = pool.acquire(id);
        r.dirPages[r.dirPageCount++] = dir;
      }
      const PageId page = pool.acquire(id);
      if (index < InlinePages) {
        r.inlinePages[index] = page;
      } else {
        const uint32_t k = index - InlinePages;
        pool.dirIds(r.dirPages[k / kIdsPerDirPage])[k % kIdsPerDirPage] = page;
      }
      ++r.pageCount;
    }
    pool.rows(pageAt(id, r.rowCount / kRowsPerPage, pool))[r.rowCount % kRowsPerPage] = row;
    ++r.rowCount;
    return AppendResult::Ok;
  }

  PageId pageAt(RunId id, uint32_t index, const Pool& pool) const {
    const Run& r = runs_[id];
    if (index < InlinePages) return r.inlinePages[index];
    const uint32_t k = index - InlinePages;
    return pool.dirIds(r.dirPages[k / kIdsPerDirPage])[k % kIdsPerDirPage];
  }

  const PassEvent& rowAt(RunId id, uint32_t i, const Pool& pool) const {
    return pool.rows(pageAt(id, i / kRowsPerPage, pool))[i % kRowsPerPage];
  }

  /// Writing → Sealed. Rows are immutable from here.
  bool seal(RunId id) {
    if (id >= RunCount || runs_[id].state != RunState::Writing) return false;
    runs_[id].state = RunState::Sealed;
    return true;
  }

  void addRef(RunId id) { ++runs_[id].refs; }

  /// Drops one reference. At zero the run enters `Reclaiming`; its pages return through `reclaimStep`.
  /// Returns true when this call started reclamation.
  bool release(RunId id) {
    Run& r = runs_[id];
    if (r.refs == 0 || r.state == RunState::Free || r.state == RunState::Reclaiming) return false;
    if (--r.refs != 0) return false;
    r.state = RunState::Reclaiming;
    return true;
  }

  /// Returns at most `maxPages` pages of a `Reclaiming` run to `pool`: data pages from the end, then directory pages.
  /// When nothing is left the descriptor is freed and true is returned. Bounded: O(maxPages).
  bool reclaimStep(RunId id, Pool& pool, uint16_t maxPages) {
    Run& r = runs_[id];
    if (r.state != RunState::Reclaiming) return false;
    uint16_t budget = maxPages;
    while (budget > 0 && r.pageCount > 0) {
      const PageId page = pageAt(id, static_cast<uint32_t>(r.pageCount - 1), pool);
      pool.release(page, id);
      --r.pageCount;
      --budget;
    }
    while (budget > 0 && r.pageCount == 0 && r.dirPageCount > 0) {
      pool.release(r.dirPages[--r.dirPageCount], id);
      --budget;
    }
    if (r.pageCount != 0 || r.dirPageCount != 0) return false;
    clear(r);
    nextFree_[id] = freeHead_;
    freeHead_ = id;
    ++freeCount_;
    return true;
  }

  const Run& run(RunId id) const { return runs_[id]; }
  uint16_t freeCount() const { return freeCount_; }

  /// Pages this run holds (data + directory). Must equal the pool's owner-tag count for the run.
  uint32_t heldPages(RunId id) const { return runs_[id].pageCount + runs_[id].dirPageCount; }

  static constexpr size_t tableBytes() { return sizeof(Run) * RunCount + sizeof(RunId) * RunCount + 8; }

 private:
  static void clear(Run& r) {
    for (auto& p : r.inlinePages) p = kNoPage;
    for (auto& p : r.dirPages) p = kNoPage;
    r.rowCount = 0;
    r.serial = 0;
    r.pageCount = 0;
    r.refs = 0;
    r.dirPageCount = 0;
    r.slot = 0xFF;
    r.kind = 0;
    r.state = RunState::Free;
  }

  Run runs_[RunCount];
  RunId nextFree_[RunCount];
  RunId freeHead_;
  uint16_t freeCount_;
  uint32_t nextSerial_;
};

}  // namespace ContentEngine
