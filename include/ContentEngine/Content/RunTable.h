//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// Global run descriptors of the content engine. A run is an append-only sequence of 16-byte rows on pool pages: the
// first `kInlinePagesPerRun` page ids live in the descriptor, later ones in directory pages drawn from the same pool
// (one directory page lists `kIdsPerDirPage` data pages). Descriptors are pooled across all slots and refcounted by the
// manifests that list them; a released run joins the reclaim queue (release order) and returns its pages through
// bounded `reclaim` calls, never in one call. The last `PreviewRuns` descriptors are preview runs (plan §6.2 fix 1:
// a get-a-loop preview): handed out only by `allocatePreview`, never as content, and the pages they hold are counted
// (`previewPages`) as they are appended and as reclaim returns them, so the reservation ledger keeps them in their own
// partition.

#pragma once

#include <cstdint>

#include "ContentEngine/EngineConfig.h"
#include "ContentEngine/Content/PagePool.h"

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
          uint8_t MaxDirPages = 8, uint16_t PreviewRuns = 0>
class RunTable {
 public:
  static constexpr uint16_t kRunCount = RunCount;
  static constexpr uint16_t kContentRuns = RunCount - PreviewRuns;
  static_assert(PreviewRuns < RunCount, "content runs remain");
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
    uint32_t onsetPitches[4];  // pitches with at least one NoteOn in this run (set on append; composer look-ahead skip)
  };

  void init() {
    for (uint16_t i = 0; i < RunCount; ++i) {
      clear(runs_[i]);
      const uint16_t end = i < kContentRuns ? kContentRuns : RunCount;
      nextFree_[i] = static_cast<RunId>(i + 1 < end ? i + 1 : kNoRun);
    }
    freeHead_ = kContentRuns ? 0 : kNoRun;
    freeCount_ = kContentRuns;
    previewFreeHead_ = PreviewRuns ? kContentRuns : kNoRun;
    previewFreeCount_ = PreviewRuns;
    previewPages_ = 0;
    nextSerial_ = 1;
    reclaimHead_ = reclaimTail_ = kNoRun;
    reclaimRuns_ = 0;
    reclaimUnits_ = 0;
  }

  /// O(1). A new run in `Writing` state with one reference (its writer). `kNoRun` when the table is full; the
  /// reservation ledger reserves descriptors before an interaction is accepted, so a writer never sees that.
  RunId allocate(uint8_t slot, uint8_t kind) { return take(freeHead_, freeCount_, slot, kind); }
  /// A preview run (plan §6.2 fix 1), as `allocate`; `kNoRun` when every preview descriptor is in use.
  RunId allocatePreview(uint8_t slot, uint8_t kind) { return take(previewFreeHead_, previewFreeCount_, slot, kind); }
  bool isPreview(RunId id) const { return id >= kContentRuns && id < RunCount; }
  /// Pages the preview runs hold now (data + directory), reclaimed ones no longer.
  uint32_t previewPages() const { return previewPages_; }
  uint16_t previewFreeCount() const { return previewFreeCount_; }

 private:
  RunId take(RunId& head, uint16_t& count, uint8_t slot, uint8_t kind) {
    if (head == kNoRun) return kNoRun;
    const RunId id = head;
    head = nextFree_[id];
    --count;
    Run& r = runs_[id];
    clear(r);
    r.state = RunState::Writing;
    r.refs = 1;
    r.slot = slot;
    r.kind = kind;
    r.serial = nextSerial_++;
    return id;
  }

 public:

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
      if (isPreview(id)) previewPages_ += needsDir ? 2u : 1u;
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
    if ((row.status & 0xF0) == 0x90 && row.data2 != 0) r.onsetPitches[(row.data1 & 0x7F) >> 5] |= 1u << (row.data1 & 31);
    return AppendResult::Ok;
  }

  PageId pageAt(RunId id, uint32_t index, const Pool& pool) const {
    const Run& r = runs_[id];
    if (index < InlinePages) return r.inlinePages[index];
    const uint32_t k = index - InlinePages;
    return pool.dirIds(r.dirPages[k / kIdsPerDirPage])[k % kIdsPerDirPage];
  }

  /// The run has at least one NoteOn on `pitch` (O(1), RAM).
  bool startsPitch(RunId id, uint8_t pitch) const {
    return (runs_[id].onsetPitches[(pitch & 0x7F) >> 5] >> (pitch & 31)) & 1u;
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

  /// Drops one reference. At zero the run enters `Reclaiming` and joins the reclaim queue (its `nextFree_` link is the
  /// queue link: a reclaiming descriptor is not free). Returns true when this call started reclamation.
  bool release(RunId id) {
    Run& r = runs_[id];
    if (r.refs == 0 || r.state == RunState::Free || r.state == RunState::Reclaiming) return false;
    if (--r.refs != 0) return false;
    r.state = RunState::Reclaiming;
    nextFree_[id] = kNoRun;
    if (reclaimTail_ == kNoRun)
      reclaimHead_ = id;
    else
      nextFree_[reclaimTail_] = id;
    reclaimTail_ = id;
    ++reclaimRuns_;
    reclaimUnits_ += heldPages(id) + 1;
    return true;
  }

  /// Reclaims at most `maxUnits` units from the queue, oldest release first. A unit is one page returned to `pool` (data
  /// pages from the end, then directory pages) or one emptied descriptor freed. Returns the units used. O(maxUnits).
  uint32_t reclaim(Pool& pool, uint32_t maxUnits) {
    uint32_t used = 0;
    while (used < maxUnits && reclaimHead_ != kNoRun) {
      const RunId id = reclaimHead_;
      Run& r = runs_[id];
      if (r.pageCount > 0) {
        pool.release(pageAt(id, static_cast<uint32_t>(r.pageCount - 1), pool), id);
        --r.pageCount;
        if (isPreview(id)) --previewPages_;
      } else if (r.dirPageCount > 0) {
        pool.release(r.dirPages[--r.dirPageCount], id);
        if (isPreview(id)) --previewPages_;
      } else {
        reclaimHead_ = nextFree_[id];
        if (reclaimHead_ == kNoRun) reclaimTail_ = kNoRun;
        --reclaimRuns_;
        clear(r);
        RunId& head = isPreview(id) ? previewFreeHead_ : freeHead_;
        nextFree_[id] = head;
        head = id;
        ++(isPreview(id) ? previewFreeCount_ : freeCount_);
      }
      ++used;
      --reclaimUnits_;
    }
    return used;
  }
  /// Released runs not yet freed, and the units (`reclaim`) they still need.
  uint16_t reclaimPendingRuns() const { return reclaimRuns_; }
  uint32_t reclaimPendingUnits() const { return reclaimUnits_; }

  const Run& run(RunId id) const { return runs_[id]; }
  uint16_t freeCount() const { return freeCount_; }

  /// Pages this run holds (data + directory). Must equal the pool's owner-tag count for the run.
  uint32_t heldPages(RunId id) const { return runs_[id].pageCount + runs_[id].dirPageCount; }

  static constexpr size_t tableBytes() { return sizeof(Run) * RunCount + sizeof(RunId) * RunCount + 16; }

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
    for (auto& m : r.onsetPitches) m = 0;
  }

  Run runs_[RunCount];
  RunId nextFree_[RunCount];  // free list, or reclaim queue for `Reclaiming` runs
  RunId freeHead_;
  uint16_t freeCount_;
  RunId previewFreeHead_;
  uint16_t previewFreeCount_;
  uint32_t previewPages_;
  uint32_t nextSerial_;
  RunId reclaimHead_, reclaimTail_;
  uint16_t reclaimRuns_;
  uint32_t reclaimUnits_;
};

}  // namespace ContentEngine
