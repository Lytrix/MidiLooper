//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
#pragma once

// PROTOTYPE -- openspec/changes/revision-manifest-prototype. Not wired to production playback.
//
// Canonical loop content as fixed-capacity pages, immutable committed passes and tiny immutable revision
// manifests. Every table is sized at compile time; nothing here allocates after construction.
//
//   PagePool         fixed array of pages; a free stack of PageIds. One pool can serve many stores (slots).
//   PassRun          the content of one pass (Record, overdub, edit, or a compacted range). Mutable only while its
//                    single writer appends. Midi rows are kept as a few sorted segments *while appending*, so commit
//                    does not sort them. HideNote rows live in their own small region, the only part sorted at commit.
//                    Page ids: the first few inline, the rest in directory pages taken from the pool.
//   RevisionManifest the ordered run list that IS committed revision N. Created whole at commit, never edited.
//
// A derived window (`composeWindow`) is stamped with the manifest it read, never with a demanded revision.
// Order (DEC-244): tick, equal-tick class (NoteOff < NoteOn < other), run order in the manifest, append order.

#include <cstddef>
#include <cstdint>

namespace RevisionStore {

using PageId = uint16_t;
using RunId = uint16_t;
using ManifestId = uint16_t;
constexpr PageId kNoPage = 0xFFFFu;
constexpr RunId kNoRun = 0xFFFFu;
constexpr ManifestId kNoManifest = 0xFFFFu;

enum class PassEventKind : uint8_t {
  /// A MIDI message at `tick`.
  Midi = 0,
  /// Hides every Midi row carrying `noteId` in the runs *before* this run in a manifest (overlap-resolve, NOTE_EDIT).
  HideNote = 1,
};

/// 16-byte canonical row. `seq` is the append ordinal inside the run: immutable through commit and compaction (a
/// compacted run renumbers in composed order), and the tie-break for rows on one tick. SysEx is not representable.
struct PassEvent {
  uint32_t tick = 0;
  uint32_t noteId = 0;
  uint32_t seq = 0;
  uint8_t status = 0;
  uint8_t data1 = 0;
  uint8_t data2 = 0;
  PassEventKind kind = PassEventKind::Midi;
};
static_assert(sizeof(PassEvent) == 16, "PassEvent is a fixed 16-byte row");

/// Production's equal-tick class (`LoopEventStore` canonical order, `NoteUtils::midiEventChronologicalLess`):
/// NoteOff (incl. NoteOn velocity 0) < NoteOn < every other message.
inline uint8_t equalTickClass(const PassEvent& e) {
  const uint8_t type = e.status & 0xF0;
  if (type == 0x80 || (type == 0x90 && e.data2 == 0)) return 0;
  return type == 0x90 ? 1 : 2;
}

/// Composition key without the run index: (tick, equal-tick class). Run order and `seq` break the remaining ties.
inline bool tickClassLess(const PassEvent& a, const PassEvent& b) {
  if (a.tick != b.tick) return a.tick < b.tick;
  return equalTickClass(a) < equalTickClass(b);
}

/// An Edit run (NOTE_EDIT) composes like an overdub: HideNote rows for the targets plus their replacement rows.
enum class PassKind : uint8_t { Record = 0, Overdub = 1, Compacted = 2, Edit = 3 };

enum class AdmitResult : uint8_t { Ok, DeniedNoRun, DeniedNoPage, DeniedNoManifest, DeniedManifestFull, DeniedWriterBusy };
/// A capacity denial latches: the pass is stopped, every later append returns the same reason, the committed
/// revision is untouched, and the writer may still commit the rows captured so far or abandon them (all pages return).
enum class AppendResult : uint8_t { Ok, DeniedNoPage, DeniedRunFull, DeniedNotWriting };
enum class ComposeStatus : uint8_t { Complete, OutputFull, NoManifest };
enum class CompactResult : uint8_t {
  Ok,
  NothingToCompact,
  DeniedNoRun,
  DeniedNoPage,
  DeniedNoManifest,
  DeniedWriterBusy,   // kept for API stability; compaction no longer excludes a writer
  DeniedBusy,         // another compaction job is active
  Superseded,         // the current manifest no longer holds the compacted runs; result discarded
  NotHistoryClosed,   // some history entry holds part of the range: compacting would keep two copies alive
};
/// Step outcome of a bounded compaction job.
enum class CompactStep : uint8_t { Idle, Pending, Published, Superseded, DeniedNoPage };

/// Which ranges a compaction may take. `HistoryClosedOnly` (the default) admits a range only when every history entry
/// holds all of it at the same positions or none of it, so publishing frees the originals at once (B.5: compacting
/// runs that history still names kept both copies alive, 2-3x content). `AllowCopies` is for tests and off-line use.
enum class CompactionScope : uint8_t { HistoryClosedOnly, AllowCopies };

/// What a derived window can prove about itself: the manifest it read and that manifest's revision.
/// `revision` is logical (compaction keeps it); `identity` is physical and never reused, unlike the `manifest` slot.
struct ComposeStamp {
  ManifestId manifest = kNoManifest;
  uint32_t identity = 0;
  uint32_t revision = 0;
  uint32_t count = 0;
  ComposeStatus status = ComposeStatus::NoManifest;
};

/// Compile-time capacities.
/// `PagesPerRun`: max data pages of one run (beyond `kInlinePages`, ids live in directory pages from the pool).
/// `SegmentsPerRun`: sorted Midi segments a run may hold (a capture wrap costs one); beyond it the run falls back to a
/// commit-time sort, counted by `slowSeals()`. `HidePagesPerRun`: pages for a run's HideNote rows.
template <uint16_t PageCount, uint16_t EventsPerPage, uint16_t RunCount, uint16_t PagesPerRun, uint16_t ManifestCount,
          uint8_t RunsPerManifest, uint8_t HistoryDepth, uint8_t SegmentsPerRun = 8, uint8_t HidePagesPerRun = 4>
struct Capacity {
  static constexpr uint16_t kPageCount = PageCount;
  static constexpr uint16_t kEventsPerPage = EventsPerPage;
  static constexpr uint16_t kRunCount = RunCount;
  static constexpr uint16_t kPagesPerRun = PagesPerRun;
  static constexpr uint16_t kManifestCount = ManifestCount;
  static constexpr uint8_t kRunsPerManifest = RunsPerManifest;
  static constexpr uint8_t kHistoryDepth = HistoryDepth;
  static constexpr uint8_t kSegmentsPerRun = SegmentsPerRun;
  static constexpr uint8_t kHidePagesPerRun = HidePagesPerRun;
};

/// Fixed page pool. Raw row storage: constructing it does not touch the rows. Shareable by several stores.
template <uint16_t PageCount, uint16_t EventsPerPage>
class PagePool {
 public:
  static constexpr uint16_t kPageCount = PageCount;
  static constexpr uint16_t kEventsPerPage = EventsPerPage;
  static constexpr uint32_t kPageBytes = sizeof(PassEvent) * EventsPerPage;
  /// Page ids a directory page holds.
  static constexpr uint32_t kIdsPerDirPage = kPageBytes / sizeof(PageId);

  PagePool() { reset(); }
  void reset() {
    for (uint16_t i = 0; i < PageCount; ++i) free_[i] = static_cast<PageId>(PageCount - 1 - i);
    top_ = PageCount;
  }
  PageId take() { return top_ == 0 ? kNoPage : free_[--top_]; }
  void give(PageId id) { free_[top_++] = id; }
  uint16_t freeCount() const { return top_; }

  PassEvent& row(PageId page, uint32_t i) { return reinterpret_cast<PassEvent*>(pages_[page].raw)[i]; }
  const PassEvent& row(PageId page, uint32_t i) const { return reinterpret_cast<const PassEvent*>(pages_[page].raw)[i]; }
  PageId& dirEntry(PageId page, uint32_t i) { return reinterpret_cast<PageId*>(pages_[page].raw)[i]; }
  PageId dirEntry(PageId page, uint32_t i) const { return reinterpret_cast<const PageId*>(pages_[page].raw)[i]; }

 private:
  struct Page {
    alignas(PassEvent) unsigned char raw[kPageBytes];
  };
  Page pages_[PageCount];
  PageId free_[PageCount];
  uint16_t top_ = 0;
};

/// One loop's (slot's) runs, manifests and history over a page pool it may share with other slots.
template <typename Cap>
class StoreCore {
  static_assert(Cap::kRunsPerManifest <= 64, "the cached merge key holds the run index in 6 bits");

 public:
  using Pool = PagePool<Cap::kPageCount, Cap::kEventsPerPage>;
  static constexpr uint8_t kInlinePages = 4;
  static constexpr uint32_t kIdsPerDirPage = Pool::kIdsPerDirPage;
  static constexpr uint16_t kDirPagesPerRun = static_cast<uint16_t>(
      Cap::kPagesPerRun <= kInlinePages ? 0 : (Cap::kPagesPerRun - kInlinePages + kIdsPerDirPage - 1) / kIdsPerDirPage);

  struct PassRun {
    PageId inlinePages[kInlinePages];         // data pages 0..3
    PageId dirPages[kDirPagesPerRun ? kDirPagesPerRun : 1];  // ids of data pages 4.. live in these pool pages
    PageId hidePages[Cap::kHidePagesPerRun];  // HideNote rows, by noteId once committed
    uint32_t segStart[Cap::kSegmentsPerRun];  // segment s = [segStart[s], segStart[s+1] or midiCount)
    uint16_t pageCount;                       // data pages
    uint16_t dirPageCount;
    uint16_t hidePageCount;
    uint32_t midiCount;
    uint32_t hideCount;
    uint8_t segCount;
    bool needsSort;  // segment budget exceeded while appending: commit sorts (counted by slowSeals)
    uint16_t refs;
    PassKind kind;
    bool committed;
    uint32_t rowCount() const { return midiCount + hideCount; }
    uint32_t segEnd(uint8_t s) const { return s + 1 < segCount ? segStart[s + 1] : midiCount; }
    void clear(PassKind k) {  // scalars only: the arrays are read only below their counts
      pageCount = dirPageCount = hidePageCount = 0;
      midiCount = hideCount = 0;
      segCount = 0;
      needsSort = false;
      refs = 0;
      kind = k;
      committed = false;
    }
  };

  struct RevisionManifest {
    uint32_t identity = 0;  // monotonic per created manifest, never reused; 0 = none
    uint32_t revision = 0;
    uint32_t loopLengthTicks = 0;
    RunId runs[Cap::kRunsPerManifest];
    uint8_t runCount = 0;
    uint16_t refs = 0;
    bool live = false;
  };

  explicit StoreCore(Pool& pool) : pool_(pool) { clearTables(); }
  StoreCore(const StoreCore&) = delete;
  StoreCore& operator=(const StoreCore&) = delete;

  /// Return every page this store holds to the pool and forget all runs, manifests and history.
  void releaseAll() {
    for (uint16_t i = 0; i < Cap::kRunCount; ++i) {
      if (runLive_[i]) releaseRunPages(runs_[i]);
    }
    clearTables();
  }

  // ---- Pool accounting ---------------------------------------------------------------------------------------------
  uint16_t freePageCount() const { return pool_.freeCount(); }
  /// Pages held by this store's live runs (data, directory and hide pages).
  uint32_t ownedPageCount() const {
    uint32_t n = 0;
    for (uint16_t i = 0; i < Cap::kRunCount; ++i) {
      if (!runLive_[i]) continue;
      n += static_cast<uint32_t>(runs_[i].pageCount) + runs_[i].dirPageCount + runs_[i].hidePageCount;
    }
    return n;
  }
  uint16_t liveRunCount() const {
    uint16_t n = 0;
    for (uint16_t i = 0; i < Cap::kRunCount; ++i) n += runLive_[i] ? 1 : 0;
    return n;
  }
  uint16_t liveManifestCount() const {
    uint16_t n = 0;
    for (uint16_t i = 0; i < Cap::kManifestCount; ++i) n += manifests_[i].live ? 1 : 0;
    return n;
  }
  /// Commits that fell back to a sort because a run exceeded its segment budget. Expected 0 for capture input.
  uint32_t slowSeals() const { return slowSeals_; }
  /// Data page `p` of run `id` (diagnostics and tests).
  PageId dataPageOf(RunId id, uint16_t p) const { return dataPage(runs_[id], p); }

  // ---- Single writer -----------------------------------------------------------------------------------------------
  /// Admission is one run descriptor, one page and the manifest slot its commit will need. Commit cannot fail.
  AdmitResult beginPass(PassKind kind, uint32_t loopLengthTicks = 0) {
    if (writer_ != kNoRun) return AdmitResult::DeniedWriterBusy;
    const bool layered = kind == PassKind::Overdub || kind == PassKind::Edit;
    if (layered && current_ == kNoManifest) return AdmitResult::DeniedNoManifest;
    if (layered && manifests_[current_].runCount >= Cap::kRunsPerManifest) {
      return AdmitResult::DeniedManifestFull;  // compaction owed before another pass can be admitted
    }
    const RunId run = takeRun(kind);
    if (run == kNoRun) return AdmitResult::DeniedNoRun;
    const ManifestId manifest = takeManifest();
    if (manifest == kNoManifest) {
      runLive_[run] = false;
      return AdmitResult::DeniedNoManifest;
    }
    const PageId page = pool_.take();
    if (page == kNoPage) {
      runLive_[run] = false;
      manifests_[manifest].live = false;
      return AdmitResult::DeniedNoPage;
    }
    PassRun& r = runs_[run];
    r.inlinePages[0] = page;
    r.pageCount = 1;
    writer_ = run;
    writerManifest_ = manifest;
    writerStop_ = AppendResult::Ok;
    if (kind == PassKind::Record) loopLengthTicks_ = loopLengthTicks;
    return AdmitResult::Ok;
  }

  AppendResult appendMidi(uint32_t tick, uint8_t status, uint8_t data1, uint8_t data2, uint32_t noteId) {
    if (writer_ == kNoRun) return AppendResult::DeniedNotWriting;
    if (writerStop_ != AppendResult::Ok) return writerStop_;
    PassRun& r = runs_[writer_];
    PassEvent e;
    e.tick = tick;
    e.status = status;
    e.data1 = data1;
    e.data2 = data2;
    e.noteId = noteId;
    e.kind = PassEventKind::Midi;
    e.seq = r.rowCount();
    const AppendResult pushed = pushMidi(r, e);
    if (pushed != AppendResult::Ok) return writerStop_ = pushed;
    placeNewMidiRow(r);
    return AppendResult::Ok;
  }

  AppendResult appendHide(uint32_t noteId) {
    if (writer_ == kNoRun) return AppendResult::DeniedNotWriting;
    if (writerStop_ != AppendResult::Ok) return writerStop_;
    PassRun& r = runs_[writer_];
    PassEvent e;
    e.noteId = noteId;
    e.kind = PassEventKind::HideNote;
    e.seq = r.rowCount();
    const AppendResult pushed = pushHide(r, e);
    if (pushed != AppendResult::Ok) return writerStop_ = pushed;
    return AppendResult::Ok;
  }

  bool writing() const { return writer_ != kNoRun; }
  /// `Ok` while the writer can append; otherwise the latched capacity reason that stopped it.
  AppendResult writerStop() const { return writerStop_; }

  /// Commit the writer's run and make the next revision current. Midi rows are already sorted segments; only the
  /// HideNote region is ordered here. The previous manifest stays intact for its readers.
  ManifestId commitPass() {
    if (writer_ == kNoRun) return kNoManifest;
    PassRun& r = runs_[writer_];
    sealRun(r);
    r.committed = true;
    RevisionManifest& m = manifests_[writerManifest_];
    m.runCount = 0;
    uint32_t parentRevision = 0;
    if ((r.kind == PassKind::Overdub || r.kind == PassKind::Edit) && current_ != kNoManifest) {
      const RevisionManifest& parent = manifests_[current_];
      for (uint8_t i = 0; i < parent.runCount; ++i) {
        m.runs[m.runCount++] = parent.runs[i];
        ++runs_[parent.runs[i]].refs;
      }
      parentRevision = parent.revision;
    } else if (current_ != kNoManifest) {
      parentRevision = manifests_[current_].revision;  // a new Record replaces the content, the revision still advances
    }
    m.runs[m.runCount++] = writer_;
    ++r.refs;
    m.revision = parentRevision + 1;
    m.loopLengthTicks = loopLengthTicks_;
    const ManifestId committed = writerManifest_;
    writer_ = kNoRun;
    writerStop_ = AppendResult::Ok;
    writerManifest_ = kNoManifest;
    pushHistory(committed);
    return committed;
  }

  /// Drop the writer's uncommitted run. Nothing committed is touched.
  void abandonPass() {
    if (writer_ == kNoRun) return;
    releaseRunPages(runs_[writer_]);
    runLive_[writer_] = false;
    manifests_[writerManifest_].live = false;
    writer_ = kNoRun;
    writerStop_ = AppendResult::Ok;
    writerManifest_ = kNoManifest;
  }

  // ---- Readers -----------------------------------------------------------------------------------------------------
  ManifestId current() const { return current_; }
  uint32_t identityOf(ManifestId id) const { return id == kNoManifest ? 0 : manifests_[id].identity; }
  const RevisionManifest& manifest(ManifestId id) const { return manifests_[id]; }
  const PassRun& run(RunId id) const { return runs_[id]; }

  /// A reader pins one manifest generation; it stays composable until unpinned, whatever commits meanwhile.
  ManifestId pinCurrent() {
    if (current_ == kNoManifest) return kNoManifest;
    ++manifests_[current_].refs;
    return current_;
  }
  void unpin(ManifestId id) { releaseManifest(id); }

  /// Compose `[fromTick, toTick)` of `id` into `out` in DEC-244 order. Hidden notes are dropped.
  /// Heap merge over the manifest's segments with cached keys: O(log cursors) per row, comparisons never touch pages.
  /// Main-loop only (uses the store's scratch merge state, not the stack); never from an interrupt.
  ComposeStamp composeWindow(ManifestId id, uint32_t fromTick, uint32_t toTick, PassEvent* out, uint32_t capacity) const {
    ComposeStamp stamp;
    if (id == kNoManifest || !manifests_[id].live) return stamp;
    const RevisionManifest& m = manifests_[id];
    stamp.manifest = id;
    stamp.identity = m.identity;
    stamp.revision = m.revision;
    stamp.status = ComposeStatus::Complete;
    Merge& merge = composeScratch_;
    merge.reset();
    merge.limit = m.runCount;
    collectHideRuns(m, 0, m.runCount, merge);
    for (uint8_t i = 0; i < m.runCount; ++i) {
      const PassRun& r = runs_[m.runs[i]];
      for (uint8_t s = 0; s < r.segCount; ++s) {
        merge.add(i, lowerBoundTick(r, r.segStart[s], r.segEnd(s), fromTick), r.segEnd(s));
      }
    }
    for (uint16_t k = 0; k < merge.count; ++k) {
      if (advanceToVisible(m, merge, k, toTick)) heapPush(m, merge, k);
    }
    while (merge.heapSize != 0) {
      const uint16_t k = heapPop(m, merge);
      if (stamp.count >= capacity) {
        stamp.status = ComposeStatus::OutputFull;
        break;
      }
      out[stamp.count++] = midiAt(runs_[m.runs[merge.run[k]]], merge.at[k]);
      ++merge.at[k];
      if (advanceToVisible(m, merge, k, toTick)) heapPush(m, merge, k);
    }
    return stamp;
  }

  // ---- Undo / redo: selection of an existing manifest --------------------------------------------------------------
  bool canUndo() const { return historyCursor_ > 0; }
  bool canRedo() const { return historyCursor_ + 1 < historyCount_; }
  bool undo() {
    if (!canUndo() || writer_ != kNoRun) return false;
    --historyCursor_;
    current_ = history_[historyCursor_];
    return true;
  }
  bool redo() {
    if (!canRedo() || writer_ != kNoRun) return false;
    ++historyCursor_;
    current_ = history_[historyCursor_];
    return true;
  }

  // ---- Compaction: same revision, fewer runs, in bounded steps -----------------------------------------------------
  /// True when every history entry holds runs [first, first + count) of the current manifest at the same positions,
  /// or none of them.
  bool rangeIsHistoryClosed(uint8_t first, uint8_t count) const {
    if (current_ == kNoManifest) return false;
    const RevisionManifest& src = manifests_[current_];
    if (first + count > src.runCount) return false;
    for (uint8_t h = 0; h < historyCount_; ++h) {
      const RevisionManifest& m = manifests_[history_[h]];
      bool any = false, all = m.runCount >= first + count;
      for (uint8_t i = first; i < first + count; ++i) {
        const bool samePosition = all && m.runs[i] == src.runs[i];
        all = samePosition;
        for (uint8_t j = 0; j < m.runCount && !any; ++j) any = m.runs[j] == src.runs[i];
      }
      if (any && !all) return false;
    }
    return true;
  }

  /// The largest history-closed range of at least two runs starting at `first`, as a count; 0 if none.
  uint8_t largestClosedRangeFrom(uint8_t first) const {
    if (current_ == kNoManifest) return 0;
    for (int count = manifests_[current_].runCount - first; count >= 2; --count) {
      if (rangeIsHistoryClosed(first, static_cast<uint8_t>(count))) return static_cast<uint8_t>(count);
    }
    return 0;
  }

  /// Start merging runs [first, first + count) of the current manifest. Off-clock maintenance, step by step; a writer,
  /// readers and undo may run meanwhile. The source manifest is pinned until the job ends. With `first > 0` (e.g.
  /// every overdub but not the Record) the large base run is never copied, and the range's HideNote rows are carried
  /// into the merged run because they may target runs before the range.
  CompactResult beginCompaction(uint8_t first, uint8_t count,
                                CompactionScope scope = CompactionScope::HistoryClosedOnly) {
    if (job_.active) return CompactResult::DeniedBusy;
    if (current_ == kNoManifest) return CompactResult::NothingToCompact;
    const RevisionManifest& src = manifests_[current_];
    if (count < 2 || first + count > src.runCount) return CompactResult::NothingToCompact;
    if (scope == CompactionScope::HistoryClosedOnly && !rangeIsHistoryClosed(first, count)) {
      return CompactResult::NotHistoryClosed;
    }
    const RunId out = takeRun(PassKind::Compacted);
    if (out == kNoRun) return CompactResult::DeniedNoRun;
    const ManifestId dst = takeManifest();
    if (dst == kNoManifest) {
      runLive_[out] = false;
      return CompactResult::DeniedNoManifest;
    }
    job_.active = true;
    job_.source = current_;
    job_.first = first;
    job_.end = static_cast<uint8_t>(first + count);
    job_.out = out;
    job_.dst = dst;
    job_.seq = 0;
    ++manifests_[current_].refs;
    Merge& merge = job_.merge;
    merge.reset();
    merge.limit = job_.end;
    collectHideRuns(src, first, job_.end, merge);
    for (uint8_t i = first; i < job_.end; ++i) {
      const PassRun& r = runs_[src.runs[i]];
      for (uint8_t s = 0; s < r.segCount; ++s) merge.add(i, r.segStart[s], r.segEnd(s));
    }
    for (uint16_t k = 0; k < merge.count; ++k) {
      if (advanceToVisible(src, merge, k, UINT32_MAX)) heapPush(src, merge, k);
    }
    runs_[out].segCount = 1;
    runs_[out].segStart[0] = 0;
    return CompactResult::Ok;
  }

  bool compacting() const { return job_.active; }

  /// Advance the job by at most `maxRows` copied rows. Publishes when done: every history entry that still holds the
  /// compacted runs at the same positions is replaced by one holding the merged run instead (same revision), so the
  /// old runs are freed once no reader pins them. If the current manifest is not among them the result is discarded
  /// (`Superseded`).
  CompactStep stepCompaction(uint32_t maxRows) {
    if (!job_.active) return CompactStep::Idle;
    const RevisionManifest& src = manifests_[job_.source];
    PassRun& merged = runs_[job_.out];
    Merge& merge = job_.merge;
    for (uint32_t done = 0; done < maxRows; ++done) {
      if (merge.heapSize == 0) return finishCompaction();
      const uint16_t k = heapPop(src, merge);
      PassEvent copy = midiAt(runs_[src.runs[merge.run[k]]], merge.at[k]);
      ++merge.at[k];
      if (advanceToVisible(src, merge, k, UINT32_MAX)) heapPush(src, merge, k);
      copy.seq = job_.seq++;
      if (pushMidi(merged, copy) != AppendResult::Ok) {
        endCompaction(false);
        return CompactStep::DeniedNoPage;
      }
    }
    return merge.heapSize == 0 ? finishCompaction() : CompactStep::Pending;
  }

  void abandonCompaction() {
    if (job_.active) endCompaction(false);
  }

  /// Synchronous convenience (tests, off-line): begin + run every step. Copies allowed unless asked otherwise.
  CompactResult compactCurrentPrefix(uint8_t runCount, CompactionScope scope = CompactionScope::AllowCopies) {
    const CompactResult begun = beginCompaction(0, runCount, scope);
    if (begun != CompactResult::Ok) return begun;
    switch (stepCompaction(UINT32_MAX)) {
      case CompactStep::Published: return CompactResult::Ok;
      case CompactStep::Superseded: return CompactResult::Superseded;
      default: return CompactResult::DeniedNoPage;
    }
  }

 protected:
  void clearTables() {
    for (uint16_t i = 0; i < Cap::kRunCount; ++i) {
      runs_[i].clear(PassKind::Record);
      runLive_[i] = false;
    }
    for (uint16_t i = 0; i < Cap::kManifestCount; ++i) {
      manifests_[i].live = false;
      manifests_[i].refs = 0;
      manifests_[i].runCount = 0;
    }
    for (uint8_t i = 0; i < Cap::kHistoryDepth; ++i) history_[i] = kNoManifest;
    historyCount_ = 0;
    historyCursor_ = 0;
    current_ = kNoManifest;
    writer_ = kNoRun;
    writerManifest_ = kNoManifest;
    writerStop_ = AppendResult::Ok;
    loopLengthTicks_ = 0;
    nextIdentity_ = 1;
    slowSeals_ = 0;
    job_.active = false;
  }

 private:
  static constexpr uint16_t kMaxCursors = static_cast<uint16_t>(Cap::kRunsPerManifest) * Cap::kSegmentsPerRun;
  /// Same-tick class inversions are fixed in place while appending, but only across this many rows.
  static constexpr uint32_t kMaxLocalShift = 32;

  /// Cursors over segments plus a binary min-heap of cursor indices (DEC-244 order) and the runs that carry hides.
  struct Merge {
    uint32_t at[kMaxCursors];
    uint32_t end[kMaxCursors];
    // Cached DEC-244 key of each cursor's current row, so heap comparisons never re-read rows from (PSRAM) pages:
    // (tick << 8) | (class << 6) | run index, then seq.
    uint64_t key[kMaxCursors];
    uint32_t seq[kMaxCursors];
    uint8_t run[kMaxCursors];  // index into the manifest's run list
    uint16_t heap[kMaxCursors];
    uint8_t hideRuns[Cap::kRunsPerManifest];
    uint16_t count = 0;
    uint16_t heapSize = 0;
    uint8_t hideRunCount = 0;
    uint8_t limit = 0;  // hides are consulted from runs before this index
    void reset() { count = heapSize = 0; hideRunCount = 0; }
    void add(uint8_t runIndex, uint32_t from, uint32_t to) {
      at[count] = from;
      end[count] = to;
      run[count] = runIndex;
      ++count;
    }
  };

  struct CompactionJob {
    bool active = false;
    ManifestId source = kNoManifest;
    uint8_t first = 0;
    uint8_t end = 0;  // one past the last compacted run
    RunId out = kNoRun;
    ManifestId dst = kNoManifest;
    uint32_t seq = 0;
    Merge merge;
  };

  void collectHideRuns(const RevisionManifest& m, uint8_t from, uint8_t to, Merge& merge) const {
    merge.hideRunCount = 0;
    for (uint8_t i = from; i < to; ++i) {
      if (runs_[m.runs[i]].hideCount != 0) merge.hideRuns[merge.hideRunCount++] = i;
    }
  }

  bool hidden(const RevisionManifest& m, const Merge& merge, uint8_t runIndex, uint32_t noteId) const {
    if (noteId == 0) return false;
    for (uint8_t h = 0; h < merge.hideRunCount; ++h) {
      const uint8_t j = merge.hideRuns[h];
      if (j > runIndex && j < merge.limit && runHides(runs_[m.runs[j]], noteId)) return true;
    }
    return false;
  }

  // Move cursor k to its next row that is visible and before `toTick`; false when the segment is done for this merge.
  bool advanceToVisible(const RevisionManifest& m, Merge& merge, uint16_t k, uint32_t toTick) const {
    const PassRun& r = runs_[m.runs[merge.run[k]]];
    while (merge.at[k] < merge.end[k]) {
      const PassEvent& row = midiAt(r, merge.at[k]);
      if (row.tick >= toTick) {
        merge.at[k] = merge.end[k];
        return false;
      }
      if (!hidden(m, merge, merge.run[k], row.noteId)) {
        merge.key[k] = (static_cast<uint64_t>(row.tick) << 8) | (static_cast<uint64_t>(equalTickClass(row)) << 6) |
                       merge.run[k];
        merge.seq[k] = row.seq;
        return true;
      }
      ++merge.at[k];
    }
    return false;
  }

  // DEC-244 order between two cursors' current rows: (tick, class, run order), then append order. Cached keys only.
  static bool cursorBefore(const RevisionManifest&, const Merge& c, uint16_t a, uint16_t b) {
    if (c.key[a] != c.key[b]) return c.key[a] < c.key[b];
    return c.seq[a] < c.seq[b];
  }

  void heapPush(const RevisionManifest& m, Merge& c, uint16_t k) const {
    uint16_t i = c.heapSize++;
    c.heap[i] = k;
    while (i > 0) {
      const uint16_t parent = static_cast<uint16_t>((i - 1) / 2);
      if (!cursorBefore(m, c, c.heap[i], c.heap[parent])) break;
      const uint16_t t = c.heap[i];
      c.heap[i] = c.heap[parent];
      c.heap[parent] = t;
      i = parent;
    }
  }

  uint16_t heapPop(const RevisionManifest& m, Merge& c) const {
    const uint16_t top = c.heap[0];
    c.heap[0] = c.heap[--c.heapSize];
    uint16_t i = 0;
    for (;;) {
      const uint16_t l = static_cast<uint16_t>(2 * i + 1), r = static_cast<uint16_t>(2 * i + 2);
      uint16_t best = i;
      if (l < c.heapSize && cursorBefore(m, c, c.heap[l], c.heap[best])) best = l;
      if (r < c.heapSize && cursorBefore(m, c, c.heap[r], c.heap[best])) best = r;
      if (best == i) break;
      const uint16_t t = c.heap[i];
      c.heap[i] = c.heap[best];
      c.heap[best] = t;
      i = best;
    }
    return top;
  }

  PageId dataPage(const PassRun& r, uint32_t p) const {
    if (p < kInlinePages) return r.inlinePages[p];
    const uint32_t q = p - kInlinePages;
    return pool_.dirEntry(r.dirPages[q / kIdsPerDirPage], q % kIdsPerDirPage);
  }
  PassEvent& midiRef(PassRun& r, uint32_t index) {
    return pool_.row(dataPage(r, index / Cap::kEventsPerPage), index % Cap::kEventsPerPage);
  }
  const PassEvent& midiAt(const PassRun& r, uint32_t index) const {
    return pool_.row(dataPage(r, index / Cap::kEventsPerPage), index % Cap::kEventsPerPage);
  }
  PassEvent& hideRef(PassRun& r, uint32_t index) {
    return pool_.row(r.hidePages[index / Cap::kEventsPerPage], index % Cap::kEventsPerPage);
  }
  const PassEvent& hideAt(const PassRun& r, uint32_t index) const {
    return pool_.row(r.hidePages[index / Cap::kEventsPerPage], index % Cap::kEventsPerPage);
  }

  RunId takeRun(PassKind kind) {
    for (uint16_t i = 0; i < Cap::kRunCount; ++i) {
      if (!runLive_[i]) {
        runLive_[i] = true;
        runs_[i].clear(kind);
        return static_cast<RunId>(i);
      }
    }
    return kNoRun;
  }

  ManifestId takeManifest() {
    for (uint16_t i = 0; i < Cap::kManifestCount; ++i) {
      if (!manifests_[i].live) {
        RevisionManifest& m = manifests_[i];
        m.live = true;
        m.refs = 0;
        m.runCount = 0;
        m.revision = 0;
        m.loopLengthTicks = 0;
        m.identity = nextIdentity_++;
        return static_cast<ManifestId>(i);
      }
    }
    return kNoManifest;
  }

  // Append a data page id to run `r`: inline first, then directory pages (a new directory page when one fills).
  AppendResult addDataPage(PassRun& r) {
    if (r.pageCount >= Cap::kPagesPerRun) return AppendResult::DeniedRunFull;
    const uint32_t p = r.pageCount;
    if (p >= kInlinePages && (p - kInlinePages) % kIdsPerDirPage == 0) {
      const PageId dir = pool_.take();
      if (dir == kNoPage) return AppendResult::DeniedNoPage;
      r.dirPages[r.dirPageCount++] = dir;
    }
    const PageId page = pool_.take();
    if (page == kNoPage) return AppendResult::DeniedNoPage;
    if (p < kInlinePages) {
      r.inlinePages[p] = page;
    } else {
      const uint32_t q = p - kInlinePages;
      pool_.dirEntry(r.dirPages[q / kIdsPerDirPage], q % kIdsPerDirPage) = page;
    }
    ++r.pageCount;
    return AppendResult::Ok;
  }

  AppendResult pushMidi(PassRun& r, const PassEvent& row) {
    const uint32_t index = r.midiCount;
    if (index >= static_cast<uint32_t>(r.pageCount) * Cap::kEventsPerPage) {
      const AppendResult added = addDataPage(r);
      if (added != AppendResult::Ok) return added;
    }
    midiRef(r, index) = row;
    ++r.midiCount;
    return AppendResult::Ok;
  }

  AppendResult pushHide(PassRun& r, const PassEvent& row) {
    const uint32_t index = r.hideCount;
    if (index >= static_cast<uint32_t>(r.hidePageCount) * Cap::kEventsPerPage) {
      if (r.hidePageCount >= Cap::kHidePagesPerRun) return AppendResult::DeniedRunFull;
      const PageId page = pool_.take();
      if (page == kNoPage) return AppendResult::DeniedNoPage;
      r.hidePages[r.hidePageCount++] = page;
    }
    hideRef(r, index) = row;
    ++r.hideCount;
    return AppendResult::Ok;
  }

  // The newest Midi row is at midiCount - 1. Keep the open segment sorted by (tick, class, seq):
  //  - not before its predecessor: stays where it is;
  //  - same tick as its predecessor (a class inversion such as NoteOn then NoteOff on one tick): shifted back past the
  //    greater-class rows of that tick, if there are at most kMaxLocalShift of them;
  //  - otherwise (a tick decrease, e.g. the capture wrap): opens a new segment; past the segment budget the run is
  //    marked for a commit-time sort.
  void placeNewMidiRow(PassRun& r) {
    const uint32_t index = r.midiCount - 1;
    if (r.segCount == 0) {
      r.segStart[0] = 0;
      r.segCount = 1;
      return;
    }
    if (r.needsSort) return;
    const uint32_t open = r.segStart[r.segCount - 1];
    if (index == open) return;
    const PassEvent row = midiAt(r, index);
    if (!tickClassLess(row, midiAt(r, index - 1))) return;
    if (row.tick == midiAt(r, index - 1).tick) {
      uint32_t at = index;
      while (at > open && index - at < kMaxLocalShift && tickClassLess(row, midiAt(r, at - 1))) --at;
      if (at == open || !tickClassLess(row, midiAt(r, at - 1))) {
        for (uint32_t k = index; k > at; --k) midiRef(r, k) = midiAt(r, k - 1);
        midiRef(r, at) = row;
        return;
      }
    }
    if (r.segCount < Cap::kSegmentsPerRun) {
      r.segStart[r.segCount++] = index;
    } else {
      r.needsSort = true;
    }
  }

  // Commit: hides by noteId (a small region); Midi rows only when the segment budget overflowed (counted).
  void sealRun(PassRun& r) {
    if (r.needsSort) {
      ++slowSeals_;
      heapSort(r, r.midiCount, false);
      r.segCount = r.midiCount == 0 ? 0 : 1;
      r.segStart[0] = 0;
      r.needsSort = false;
    }
    heapSort(r, r.hideCount, true);
  }

  PassEvent& rowRef(PassRun& r, uint32_t i, bool hides) { return hides ? hideRef(r, i) : midiRef(r, i); }
  bool rowLess(PassRun& r, uint32_t a, uint32_t b, bool hides) {
    const PassEvent& x = rowRef(r, a, hides);
    const PassEvent& y = rowRef(r, b, hides);
    if (hides) return x.noteId < y.noteId;
    if (tickClassLess(x, y)) return true;
    if (tickClassLess(y, x)) return false;
    return x.seq < y.seq;
  }
  void heapSort(PassRun& r, uint32_t n, bool hides) {
    for (uint32_t i = n / 2; i-- > 0;) siftDown(r, i, n, hides);
    for (uint32_t end = n; end-- > 1;) {
      const PassEvent t = rowRef(r, 0, hides);
      rowRef(r, 0, hides) = rowRef(r, end, hides);
      rowRef(r, end, hides) = t;
      siftDown(r, 0, end, hides);
    }
  }
  void siftDown(PassRun& r, uint32_t root, uint32_t n, bool hides) {
    for (;;) {
      uint32_t child = 2 * root + 1;
      if (child >= n) return;
      if (child + 1 < n && rowLess(r, child, child + 1, hides)) ++child;
      if (!rowLess(r, root, child, hides)) return;
      const PassEvent t = rowRef(r, root, hides);
      rowRef(r, root, hides) = rowRef(r, child, hides);
      rowRef(r, child, hides) = t;
      root = child;
    }
  }

  uint32_t lowerBoundTick(const PassRun& r, uint32_t lo, uint32_t hi, uint32_t tick) const {
    while (lo < hi) {
      const uint32_t mid = lo + (hi - lo) / 2;
      if (midiAt(r, mid).tick < tick) {
        lo = mid + 1;
      } else {
        hi = mid;
      }
    }
    return lo;
  }

  bool runHides(const PassRun& r, uint32_t noteId) const {
    uint32_t lo = 0;
    uint32_t hi = r.hideCount;
    while (lo < hi) {
      const uint32_t mid = lo + (hi - lo) / 2;
      const uint32_t v = hideAt(r, mid).noteId;
      if (v == noteId) return true;
      if (v < noteId) {
        lo = mid + 1;
      } else {
        hi = mid;
      }
    }
    return false;
  }

  bool holdsCompactedRange(ManifestId id) const {
    if (id == kNoManifest) return false;
    const RevisionManifest& src = manifests_[job_.source];
    const RevisionManifest& m = manifests_[id];
    if (m.runCount < job_.end) return false;
    for (uint8_t i = job_.first; i < job_.end; ++i) {
      if (m.runs[i] != src.runs[i]) return false;
    }
    return true;
  }

  // {m.runs[0, first), merged, m.runs[end, ...)} at m's revision, into slot `dst`.
  void buildCompactedManifest(ManifestId dst, const RevisionManifest& m) {
    RevisionManifest& out = manifests_[dst];
    out.revision = m.revision;
    out.loopLengthTicks = m.loopLengthTicks;
    out.runCount = 0;
    for (uint8_t i = 0; i < job_.first; ++i) {
      out.runs[out.runCount++] = m.runs[i];
      ++runs_[m.runs[i]].refs;
    }
    out.runs[out.runCount++] = job_.out;
    ++runs_[job_.out].refs;
    for (uint8_t i = job_.end; i < m.runCount; ++i) {
      out.runs[out.runCount++] = m.runs[i];
      ++runs_[m.runs[i]].refs;
    }
  }

  CompactStep finishCompaction() {
    if (!holdsCompactedRange(current_)) {
      endCompaction(false);
      return CompactStep::Superseded;
    }
    PassRun& merged = runs_[job_.out];
    if (job_.first > 0) {
      // Carry the range's hides: they may target runs before the range. Bounded by the hide rows, not the content.
      const RevisionManifest& src = manifests_[job_.source];
      for (uint8_t i = job_.first; i < job_.end; ++i) {
        const PassRun& r = runs_[src.runs[i]];
        for (uint32_t h = 0; h < r.hideCount; ++h) {
          if (pushHide(merged, hideAt(r, h)) != AppendResult::Ok) {
            endCompaction(false);
            return CompactStep::DeniedNoPage;
          }
        }
      }
      heapSort(merged, merged.hideCount, true);
    }
    merged.committed = true;
    if (merged.midiCount == 0) merged.segCount = 0;
    // The current entry uses the reserved slot; other matching history entries take a slot if one is free (if not,
    // they keep the old runs: still correct, only less memory is returned).
    buildCompactedManifest(job_.dst, manifests_[current_]);
    const ManifestId currentDst = job_.dst;
    for (uint8_t i = 0; i < historyCount_; ++i) {
      if (i == historyCursor_ || !holdsCompactedRange(history_[i])) continue;
      const ManifestId dst = takeManifest();
      if (dst == kNoManifest) break;
      buildCompactedManifest(dst, manifests_[history_[i]]);
      replaceHistoryAt(i, dst);
    }
    endCompaction(true);
    replaceHistoryAt(historyCursor_, currentDst);
    return CompactStep::Published;
  }

  void endCompaction(bool published) {
    if (!published) {
      releaseRunPages(runs_[job_.out]);
      runLive_[job_.out] = false;
      manifests_[job_.dst].live = false;
    }
    const ManifestId source = job_.source;
    job_.active = false;
    releaseManifest(source);
  }

  void releaseRunPages(PassRun& r) {
    for (uint16_t p = 0; p < r.pageCount; ++p) pool_.give(dataPage(r, p));
    for (uint16_t i = 0; i < r.dirPageCount; ++i) pool_.give(r.dirPages[i]);
    for (uint16_t i = 0; i < r.hidePageCount; ++i) pool_.give(r.hidePages[i]);
    r.pageCount = 0;
    r.dirPageCount = 0;
    r.hidePageCount = 0;
  }

  void releaseRun(RunId id) {
    PassRun& r = runs_[id];
    if (r.refs == 0 || --r.refs != 0) return;
    releaseRunPages(r);
    runLive_[id] = false;
  }

  void releaseManifest(ManifestId id) {
    if (id == kNoManifest) return;
    RevisionManifest& m = manifests_[id];
    if (m.refs == 0 || --m.refs != 0) return;
    for (uint8_t i = 0; i < m.runCount; ++i) releaseRun(m.runs[i]);
    m.live = false;
    m.runCount = 0;
  }

  // History owns one reference per entry. A commit after undo drops the redo branch; overflow drops the oldest.
  void pushHistory(ManifestId id) {
    ++manifests_[id].refs;
    while (historyCount_ > historyCursor_ + 1) {
      releaseManifest(history_[--historyCount_]);
      history_[historyCount_] = kNoManifest;
    }
    if (historyCount_ == Cap::kHistoryDepth) {
      releaseManifest(history_[0]);
      for (uint8_t i = 1; i < historyCount_; ++i) history_[i - 1] = history_[i];
      --historyCount_;
    }
    history_[historyCount_++] = id;
    historyCursor_ = static_cast<uint8_t>(historyCount_ - 1);
    current_ = id;
  }

  void replaceHistoryAt(uint8_t index, ManifestId id) {
    ++manifests_[id].refs;
    const ManifestId old = history_[index];
    history_[index] = id;
    if (index == historyCursor_) current_ = id;
    releaseManifest(old);
  }

  Pool& pool_;
  PassRun runs_[Cap::kRunCount];
  bool runLive_[Cap::kRunCount];
  RevisionManifest manifests_[Cap::kManifestCount];
  ManifestId history_[Cap::kHistoryDepth];
  uint8_t historyCount_ = 0;
  uint8_t historyCursor_ = 0;
  ManifestId current_ = kNoManifest;
  RunId writer_ = kNoRun;
  ManifestId writerManifest_ = kNoManifest;
  AppendResult writerStop_ = AppendResult::Ok;
  uint32_t loopLengthTicks_ = 0;
  uint32_t nextIdentity_ = 1;
  uint32_t slowSeals_ = 0;
  CompactionJob job_;
  mutable Merge composeScratch_;
};

/// A store with its own page pool (one slot, tests, the probe). `reset()` empties both.
template <typename Cap>
class Store : private PagePool<Cap::kPageCount, Cap::kEventsPerPage>, public StoreCore<Cap> {
  using OwnPool = PagePool<Cap::kPageCount, Cap::kEventsPerPage>;

 public:
  using EventPage = PassEvent[Cap::kEventsPerPage];  // size reference for diagnostics
  Store() : OwnPool(), StoreCore<Cap>(static_cast<OwnPool&>(*this)) {}
  void reset() {
    OwnPool::reset();
    this->clearTables();
  }
};

}  // namespace RevisionStore
