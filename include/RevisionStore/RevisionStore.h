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

/// 16-byte committed source row (DEC-245 layer 1). A run keeps Midi rows and HideNote rows in separate regions; a
/// HideNote row hides every Midi row carrying `noteId` in earlier passes (overlap-resolve, NOTE_EDIT).
/// `seq` is the append ordinal inside the source pass and `origin` the source pass's ordinal inside a compacted run
/// (0 in an ordinary run): both survive compaction unchanged, so provenance and DEC-244 order are kept.
/// SysEx is not representable.
struct PassEvent {
  uint32_t tick = 0;
  uint32_t noteId = 0;
  uint32_t seq = 0;
  uint8_t status = 0;
  uint8_t data1 = 0;
  uint8_t data2 = 0;
  uint8_t origin = 0;
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
enum class PassKind : uint8_t { Record = 0, Overdub = 1, Compacted = 2, Edit = 3, EditIndex = 4 };

enum class AdmitResult : uint8_t { Ok, DeniedNoRun, DeniedNoPage, DeniedNoManifest, DeniedManifestFull, DeniedWriterBusy };
/// A capacity denial latches: the pass is stopped, every later append returns the same reason, the committed
/// revision is untouched, and the writer may still commit the rows captured so far or abandon them (all pages return).
enum class AppendResult : uint8_t { Ok, DeniedNoPage, DeniedRunFull, DeniedNotWriting };
/// `IndexOwed`: the manifest carries hides, the caller asked for `HideLookup::IndexOnly` (the real-time mode) and the
/// edit index for this exact manifest is not published yet. Composition never builds it; keep the previous window.
enum class ComposeStatus : uint8_t { Complete, OutputFull, NoManifest, IndexOwed };
/// How a composition resolves hides. `IndexOnly`: through the manifest's edit index, else `IndexOwed` (real time).
/// `Scan`: per row over the later runs' hide regions (the slow reference, kept as the host oracle).
/// `IndexOrScan`: the index when published, else the scan (off-line convenience).
enum class HideLookup : uint8_t { IndexOrScan, IndexOnly, Scan };
/// Step outcome of the edit-index worker. `CapacityDenied`: every slot holds an index some reader still pins.
enum class SealStep : uint8_t { Pending, Sealed, Failed };
enum class IndexStep : uint8_t { Idle, Pending, Published, DeniedNoPage, CapacityDenied };
/// C3d window producer results. `Ready` carries the identity actually read. Nothing here builds an import or an
/// index, and there is no scan fallback: a missing prerequisite is a named answer.
enum class WindowResult : uint8_t { Idle, Pending, Ready, IndexOwed, ImportOwed, CapacityDenied, SourceMoved };
struct WindowStamp {
  uint32_t identity = 0;  // the manifest identity actually read
  uint32_t revision = 0;
  uint32_t fromTick = 0;
  uint32_t toTick = 0;
  uint32_t count = 0;
};
/// Answer to a reader's demand for a manifest's edit index (C2a).
enum class IndexRequest : uint8_t { NotNeeded, Ready, Building, Owed, IndexCapacityDenied };
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

/// DEC-245 layer 2 policy for a composed window. `revision()` is the pure effective revision (hides and
/// replacements) and the Stage-C oracle surface: it equals production's passes-derived effective content
/// (device, C3a). `fullRebuildSnapshotCompat()` reproduces one legacy publication path only -- the full-rebuild
/// snapshot (`requestCommittedPassesRebuild`), with zero-length suppression and cross-pass identical-event collapse --
/// kept as a regression oracle for that path, not as production semantics (production's passes view and its
/// incremental snapshot keep the rows). `source()` is layer 1 (no hides), for provenance and multiset checks.
struct EffectivePolicy {
  bool applyHides = true;
  bool suppressZeroLength = false;
  bool collapseCrossRunDuplicates = false;
  HideLookup hideLookup = HideLookup::IndexOrScan;
  static EffectivePolicy revision() { return {}; }
  static EffectivePolicy fullRebuildSnapshotCompat() { return {true, true, true, HideLookup::IndexOrScan}; }
  static EffectivePolicy source() { return {false, false, false, HideLookup::IndexOrScan}; }
  EffectivePolicy with(HideLookup lookup) const {
    EffectivePolicy p = *this;
    p.hideLookup = lookup;
    return p;
  }
};

/// What a derived window can prove about itself: the manifest it read and that manifest's revision.
/// `revision` is logical (compaction keeps it); `identity` is physical and never reused, unlike the `manifest` slot.
struct ComposeStamp {
  ManifestId manifest = kNoManifest;
  uint32_t identity = 0;
  uint32_t revision = 0;
  uint32_t count = 0;
  ComposeStatus status = ComposeStatus::NoManifest;
  bool usedIndex = false;  // hides were resolved through the edit index published for this manifest identity
};

/// Compile-time capacities.
/// `PagesPerRun`: max data pages of one run (beyond `kInlinePages`, ids live in directory pages from the pool).
/// `SegmentsPerRun`: sorted Midi segments a run may hold (a capture wrap costs one); beyond it the run falls back to a
/// commit-time sort, counted by `slowSeals()`. `HidePagesPerRun`: unused since B.5d (HideNote rows are page-listed
/// like Midi rows, up to `PagesPerRun` pages), kept so existing capacity declarations still compile.
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
  static_assert(sizeof(PassEvent) * Cap::kEventsPerPage >= 4 * Cap::kRunsPerManifest,
                "an origin page must hold one pass serial per run of a full manifest");

 public:
  using Pool = PagePool<Cap::kPageCount, Cap::kEventsPerPage>;
  static constexpr uint8_t kInlinePages = 4;
  static constexpr uint32_t kIdsPerDirPage = Pool::kIdsPerDirPage;
  static constexpr uint16_t kDirPagesPerRun = static_cast<uint16_t>(
      Cap::kPagesPerRun <= kInlinePages ? 0 : (Cap::kPagesPerRun - kInlinePages + kIdsPerDirPage - 1) / kIdsPerDirPage);
  static constexpr uint8_t kHideInlinePages = 1;
  static constexpr uint16_t kHideDirPagesPerRun = static_cast<uint16_t>(
      Cap::kPagesPerRun <= kHideInlinePages ? 0
                                            : (Cap::kPagesPerRun - kHideInlinePages + kIdsPerDirPage - 1) / kIdsPerDirPage);

  struct PassRun {
    PageId inlinePages[kInlinePages];         // data pages 0..3
    PageId dirPages[kDirPagesPerRun ? kDirPagesPerRun : 1];  // ids of data pages 4.. live in these pool pages
    PageId hideInline[kHideInlinePages];      // HideNote rows (by noteId, then origin, once committed)
    PageId hideDirPages[kHideDirPagesPerRun ? kHideDirPagesPerRun : 1];
    uint32_t segStart[Cap::kSegmentsPerRun];  // segment s = [segStart[s], segStart[s+1] or midiCount)
    uint16_t pageCount;                       // data pages
    uint16_t dirPageCount;
    uint16_t hidePageCount;
    uint16_t hideDirPageCount;
    uint32_t midiCount;
    uint32_t hideCount;
    uint8_t segCount;
    bool needsSort;  // segment budget exceeded while appending: commit sorts (counted by slowSeals)
    uint16_t refs;
    PassKind kind;
    bool committed;
    uint32_t serial;     // monotonic pass identity, never reused (provenance)
    PageId originPage;   // compacted runs: page holding the source passes' serials, indexed by `origin`
    uint8_t originCount;
    uint32_t rowCount() const { return midiCount + hideCount; }
    uint32_t segEnd(uint8_t s) const { return s + 1 < segCount ? segStart[s + 1] : midiCount; }
    void clear(PassKind k) {  // scalars only: the arrays are read only below their counts
      pageCount = dirPageCount = hidePageCount = hideDirPageCount = 0;
      midiCount = hideCount = 0;
      segCount = 0;
      needsSort = false;
      refs = 0;
      kind = k;
      committed = false;
      serial = 0;
      originPage = kNoPage;
      originCount = 0;
    }
  };

  struct RevisionManifest {
    uint32_t identity = 0;  // monotonic per created manifest, never reused; 0 = none
    uint32_t revision = 0;
    uint32_t loopLengthTicks = 0;
    RunId runs[Cap::kRunsPerManifest];
    uint8_t runCount = 0;
    uint16_t refs = 0;
    uint16_t readerPins = 0;  // pins taken by readers (a subset of refs); an index is reclaimable only at zero
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
      n += static_cast<uint32_t>(runs_[i].pageCount) + runs_[i].dirPageCount + runs_[i].hidePageCount +
           runs_[i].hideDirPageCount + (runs_[i].originPage != kNoPage ? 1u : 0u);
    }
    return n;
  }
  uint16_t liveRunCount() const {
    uint16_t n = 0;
    for (uint16_t i = 0; i < Cap::kRunCount; ++i) n += runLive_[i] ? 1 : 0;
    return n;
  }
  uint16_t freeRunCount() const { return static_cast<uint16_t>(Cap::kRunCount - liveRunCount()); }
  uint16_t freeManifestCount() const { return static_cast<uint16_t>(Cap::kManifestCount - liveManifestCount()); }
  uint16_t liveManifestCount() const {
    uint16_t n = 0;
    for (uint16_t i = 0; i < Cap::kManifestCount; ++i) n += manifests_[i].live ? 1 : 0;
    return n;
  }
  /// Commits that fell back to a sort because a run exceeded its segment budget. Expected 0 for capture input.
  uint32_t slowSeals() const { return slowSeals_; }
  /// Serial of the source pass a row of run `id` came from (its own serial unless the run is compacted).
  uint32_t sourceSerialOf(RunId id, uint8_t origin) const { return originEntry(id, origin) & ~kEditOriginBit; }
  /// Whether that source pass was an Edit pass. Kept through compaction: the production projection keeps edit
  /// replacement rows out of its cross-pass collapse (C1 matrix).
  bool sourceIsEdit(RunId id, uint8_t origin) const { return (originEntry(id, origin) & kEditOriginBit) != 0; }
  /// Visit every committed source row of manifest `id` (DEC-245 layer 1): `f(row, sourceSerial, isHide)`, in storage
  /// order. Off-line use (tests, persistence, diagnostics); cost is the manifest's whole content.
  template <typename F>
  void forEachSourceRow(ManifestId id, F&& f) const {
    const RevisionManifest& m = manifests_[id];
    for (uint8_t i = 0; i < m.runCount; ++i) {
      const RunId runId = m.runs[i];
      const PassRun& r = runs_[runId];
      for (uint32_t k = 0; k < r.midiCount; ++k) f(midiAt(r, k), sourceSerialOf(runId, midiAt(r, k).origin), false);
      for (uint32_t k = 0; k < r.hideCount; ++k) f(hideAt(r, k), sourceSerialOf(runId, hideAt(r, k).origin), true);
    }
  }
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
    if (writerManifest_ != kNoManifest) manifests_[writerManifest_].live = false;
    writer_ = kNoRun;
    writerStop_ = AppendResult::Ok;
    writerManifest_ = kNoManifest;
  }

  // ---- Staged candidate (C3b): runs built privately, published as ONE manifest -----------------------------------
  //
  // A staged run is sealed but belongs to no manifest, so readers keep seeing the published manifest until
  // `publishStaged()` makes the whole candidate current atomically (it replaces the content, like a Record).

  AdmitResult beginStagedRun(PassKind kind, uint32_t loopLengthTicks) {
    if (writer_ != kNoRun) return AdmitResult::DeniedWriterBusy;
    if (stagedCount_ >= Cap::kRunsPerManifest) return AdmitResult::DeniedManifestFull;
    const RunId run = takeRun(kind);
    if (run == kNoRun) return AdmitResult::DeniedNoRun;
    const PageId page = pool_.take();
    if (page == kNoPage) {
      runLive_[run] = false;
      return AdmitResult::DeniedNoPage;
    }
    runs_[run].inlinePages[0] = page;
    runs_[run].pageCount = 1;
    writer_ = run;
    writerManifest_ = kNoManifest;
    writerStop_ = AppendResult::Ok;
    stagedLoopLength_ = loopLengthTicks;
    return AdmitResult::Ok;
  }
  bool sealStagedRun() {
    uint32_t unbounded = UINT32_MAX;
    return stepSealStagedRun(unbounded) == SealStep::Sealed;
  }
  /// Bounded seal of the open staged run (C4a): the commit-time sort of an over-budget run and the hide sort are a
  /// resumable heapsort, one sift per unit of `budget`. `Pending` until the run is sealed and staged.
  SealStep stepSealStagedRun(uint32_t& budget) {
    if (writer_ == kNoRun || writerManifest_ != kNoManifest) return SealStep::Failed;
    PassRun& r = runs_[writer_];
    if (seal_.phase == SealPhase::None) {
      seal_.phase = r.needsSort ? SealPhase::MidiHeapify : SealPhase::HideHeapify;
      if (r.needsSort) ++slowSeals_;
      seal_.cursor = (r.needsSort ? r.midiCount : r.hideCount) / 2;
    }
    while (seal_.phase != SealPhase::Done) {
      const bool hides = seal_.phase == SealPhase::HideHeapify || seal_.phase == SealPhase::HideExtract;
      const uint32_t n = hides ? r.hideCount : r.midiCount;
      if (seal_.phase == SealPhase::MidiHeapify || seal_.phase == SealPhase::HideHeapify) {
        if (seal_.cursor == 0) {
          seal_.phase = hides ? SealPhase::HideExtract : SealPhase::MidiExtract;
          seal_.cursor = n;
          continue;
        }
        if (budget == 0) return SealStep::Pending;
        --budget;
        siftDown(r, --seal_.cursor, n, hides);
        continue;
      }
      if (seal_.cursor <= 1) {
        if (hides) {
          seal_.phase = SealPhase::Done;
        } else {
          r.segCount = r.midiCount == 0 ? 0 : 1;
          r.segStart[0] = 0;
          r.needsSort = false;
          seal_.phase = SealPhase::HideHeapify;
          seal_.cursor = r.hideCount / 2;
        }
        continue;
      }
      if (budget == 0) return SealStep::Pending;
      --budget;
      const uint32_t end = --seal_.cursor;
      const PassEvent t = rowRef(r, 0, hides);
      rowRef(r, 0, hides) = rowRef(r, end, hides);
      rowRef(r, end, hides) = t;
      siftDown(r, 0, end, hides);
    }
    seal_ = SealJob{};
    runs_[writer_].committed = true;
    staged_[stagedCount_++] = writer_;
    writer_ = kNoRun;
    writerStop_ = AppendResult::Ok;
    return SealStep::Sealed;
  }
  /// Make the staged runs the current manifest (revision = previous + 1). `kNoManifest` (nothing changed) when no
  /// manifest slot is free; the caller cancels.
  ManifestId publishStaged() {
    if (writer_ != kNoRun || stagedCount_ == 0) return kNoManifest;
    const ManifestId id = takeManifest();
    if (id == kNoManifest) return kNoManifest;
    RevisionManifest& m = manifests_[id];
    for (uint8_t i = 0; i < stagedCount_; ++i) {
      m.runs[m.runCount++] = staged_[i];
      ++runs_[staged_[i]].refs;
    }
    m.revision = current_ == kNoManifest ? 1 : manifests_[current_].revision + 1;
    m.loopLengthTicks = stagedLoopLength_;
    stagedCount_ = 0;
    pushHistory(id);
    return id;
  }
  /// Drop the candidate: the open staged run and every sealed staged run; all their pages return.
  void cancelStaged() {
    seal_ = SealJob{};
    if (writer_ != kNoRun && writerManifest_ == kNoManifest) {
      releaseRunPages(runs_[writer_]);
      runLive_[writer_] = false;
      writer_ = kNoRun;
      writerStop_ = AppendResult::Ok;
    }
    for (uint8_t i = 0; i < stagedCount_; ++i) {
      releaseRunPages(runs_[staged_[i]]);
      runLive_[staged_[i]] = false;
    }
    stagedCount_ = 0;
  }
  uint8_t stagedRunCount() const { return stagedCount_; }
  /// Staged (unpublished) run `i`, and in-place access to its rows: a private candidate may still be annotated
  /// (C4a: NoteOff ids are stamped after the pass is sealed, in tick order). Never valid for a published run.
  RunId stagedRun(uint8_t i) const { return staged_[i]; }
  const PassEvent& stagedRow(RunId id, uint32_t index) const { return midiAt(runs_[id], index); }
  void setStagedRowNoteId(RunId id, uint32_t index, uint32_t noteId) { midiRef(runs_[id], index).noteId = noteId; }
  bool stagedWriterOpen() const { return writer_ != kNoRun && writerManifest_ == kNoManifest; }

  // ---- Readers -----------------------------------------------------------------------------------------------------
  ManifestId current() const { return current_; }
  uint32_t identityOf(ManifestId id) const { return id == kNoManifest ? 0 : manifests_[id].identity; }
  const RevisionManifest& manifest(ManifestId id) const { return manifests_[id]; }
  const PassRun& run(RunId id) const { return runs_[id]; }

  /// A reader pins one manifest generation; it stays composable until unpinned, whatever commits meanwhile.
  ManifestId pinCurrent() { return pin(current_); }
  /// A reader pin: keeps the manifest and its published edit index alive (C2a).
  ManifestId pin(ManifestId id) {
    if (id == kNoManifest || !manifests_[id].live) return kNoManifest;
    ++manifests_[id].refs;
    ++manifests_[id].readerPins;
    return id;
  }
  void unpin(ManifestId id) {
    if (id == kNoManifest) return;
    if (manifests_[id].readerPins != 0) --manifests_[id].readerPins;
    releaseManifest(id);
  }

  /// Compose `[fromTick, toTick)` of `id` into `out` in DEC-244 order, projected by `policy` (DEC-245 layer 2):
  /// by default hides and replacements apply; `EffectivePolicy::source()` returns the committed source rows.
  /// Heap merge over the manifest's segments with cached keys: O(log cursors) per row, comparisons never touch pages.
  /// Main-loop only (uses the store's scratch merge state, not the stack); never from an interrupt.
  ComposeStamp composeWindow(ManifestId id, uint32_t fromTick, uint32_t toTick, PassEvent* out, uint32_t capacity,
                             EffectivePolicy policy = EffectivePolicy::revision()) const {
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
    merge.index = nullptr;
    if (policy.applyHides && manifestHasHides(id)) {
      const EditIndexSlot* slot = policy.hideLookup == HideLookup::Scan ? nullptr : findIndexSlot(id);
      if (slot == nullptr && policy.hideLookup == HideLookup::IndexOnly) {
        stamp.status = ComposeStatus::IndexOwed;
        return stamp;
      }
      if (slot != nullptr) {
        merge.index = slot;
        stamp.usedIndex = true;
      } else {
        collectHideRuns(m, 0, m.runCount, merge);
      }
    }
    for (uint8_t i = 0; i < m.runCount; ++i) {
      const PassRun& r = runs_[m.runs[i]];
      for (uint8_t s = 0; s < r.segCount; ++s) {
        merge.add(i, lowerBoundTick(r, r.segStart[s], r.segEnd(s), fromTick), r.segEnd(s));
      }
    }
    for (uint16_t k = 0; k < merge.count; ++k) {
      if (advanceToVisible(m, merge, k, toTick)) heapPush(m, merge, k);
    }
    // The current equal-tick group's run indices, for the two cross-row projections.
    uint8_t* groupRun = composeGroupRun_;
    uint8_t* groupOrigin = composeGroupOrigin_;
    uint32_t groupStart = 0;
    uint32_t groupCount = 0;
    while (merge.heapSize != 0) {
      const uint16_t k = heapPop(m, merge);
      const PassEvent& row = midiAt(runs_[m.runs[merge.run[k]]], merge.at[k]);
      const uint8_t runIndex = merge.run[k];
      ++merge.at[k];
      if (advanceToVisible(m, merge, k, toTick)) heapPush(m, merge, k);
      if (groupCount == 0 || out[groupStart].tick != row.tick) {
        groupStart = stamp.count;
        groupCount = 0;
      }
      // Production collapses between capture passes only; edit replacement rows neither collapse nor are collapsed
      // against. Decided per row from its source pass, so compaction does not change it.
      if (policy.collapseCrossRunDuplicates && !sourceIsEdit(m.runs[runIndex], row.origin) &&
          collapsesWithEarlierRun(m, out + groupStart, groupRun, groupOrigin, groupCount, row, runIndex)) {
        continue;
      }
      if (policy.suppressZeroLength && equalTickClass(row) == 1 && row.noteId != 0) {
        uint32_t off = groupCount;
        for (uint32_t g = 0; g < groupCount; ++g) {
          const PassEvent& e = out[groupStart + g];
          // Its own NoteOff, or an unattributed NoteOff of the same lane on the same tick (production attribution
          // leaves a zero-length note's NoteOff unpaired: it precedes the NoteOn at an equal tick). A legato NoteOff of
          // the previous note carries that note's id and is not taken.
          const bool sameLane = e.status == static_cast<uint8_t>((row.status & 0x0F) | 0x80) ||
                                e.status == static_cast<uint8_t>((row.status & 0x0F) | 0x90);
          if (equalTickClass(e) == 0 &&
              (e.noteId == row.noteId || (e.noteId == 0 && sameLane && e.data1 == row.data1))) {
            off = g;
            break;
          }
        }
        if (off != groupCount) {  // drop the pair: remove its NoteOff, skip this NoteOn
          for (uint32_t g = off; g + 1 < groupCount; ++g) {
            out[groupStart + g] = out[groupStart + g + 1];
            if (g + 1 < kGroupCapacity) {
              groupRun[g] = groupRun[g + 1];
              groupOrigin[g] = groupOrigin[g + 1];
            }
          }
          --groupCount;
          --stamp.count;
          continue;
        }
      }
      if (stamp.count >= capacity) {
        stamp.status = ComposeStatus::OutputFull;
        break;
      }
      out[stamp.count++] = row;
      if (groupCount < kGroupCapacity) {
        groupRun[groupCount] = runIndex;
        groupOrigin[groupCount] = row.origin;
      }
      ++groupCount;
    }
    return stamp;
  }

  // ---- Edit index (B.5d): derived, manifest-qualified, built off the edge ----------------------------------------
  //
  // Precedence (explicit, independent of how the index is built): a hide's position is (run index in the manifest,
  // source origin). A Midi row is hidden iff the latest hide of its noteId sits at a strictly later position than
  // the row. The index stores, per noteId, that latest position; it is keyed by manifest *identity*, so a reader
  // of a manifest that compaction replaced keeps its own index.

  bool manifestHasHides(ManifestId id) const {
    if (id == kNoManifest) return false;
    const RevisionManifest& m = manifests_[id];
    for (uint8_t i = 0; i < m.runCount; ++i) {
      if (runs_[m.runs[i]].hideCount != 0) return true;
    }
    return false;
  }
  bool editIndexReady(ManifestId id) const { return findIndexSlot(id) != nullptr; }
  /// The current manifest carries hides and has no published index yet.
  bool editIndexOwed() const { return manifestHasHides(current_) && !editIndexReady(current_); }

  /// A reader's demand for `id`'s index. Records the demand (the worker builds it next) and answers what it can see
  /// now. `IndexCapacityDenied`: every slot is building or holds an index that some reader pins; nothing is evicted.
  IndexRequest requestIndex(ManifestId id) {
    if (!manifestHasHides(id)) return IndexRequest::NotNeeded;
    if (editIndexReady(id)) return IndexRequest::Ready;
    if (indexJob_.active && indexJob_.manifest == id) return IndexRequest::Building;
    demanded_ = id;
    demandedIdentity_ = manifests_[id].identity;
    return reclaimableSlot() != nullptr || indexJob_.active ? IndexRequest::Owed : IndexRequest::IndexCapacityDenied;
  }

  /// Advance the edit-index worker by about `maxRows` hide rows (a run of equal noteIds is finished as a unit). A job
  /// builds the manifest it started on and publishes for exactly that identity, whatever became current meanwhile.
  /// Target of a new job: the demanded manifest if it is still live and unresolved, else the current one if owed.
  IndexStep stepEditIndex(uint32_t maxRows) {
    if (!indexJob_.active) {
      const ManifestId target = nextIndexTarget();
      if (target == kNoManifest) return IndexStep::Idle;
      EditIndexSlot* slot = reclaimableSlot();
      if (slot == nullptr) return IndexStep::CapacityDenied;
      if (!beginIndexJob(target, *slot)) return IndexStep::DeniedNoPage;
    }
    const RevisionManifest& m = manifests_[indexJob_.manifest];
    uint32_t done = 0;
    while (done < maxRows) {
      uint32_t noteId = 0;
      bool any = false;
      for (uint8_t c = 0; c < indexJob_.cursorCount; ++c) {
        const PassRun& r = runs_[m.runs[indexJob_.runIndex[c]]];
        if (indexJob_.at[c] >= r.hideCount) continue;
        const uint32_t v = hideAt(r, indexJob_.at[c]).noteId;
        if (!any || v < noteId) noteId = v;
        any = true;
      }
      if (!any) return publishIndexJob();
      uint32_t latest = 0;
      for (uint8_t c = 0; c < indexJob_.cursorCount; ++c) {
        const uint8_t runIndex = indexJob_.runIndex[c];
        const PassRun& r = runs_[m.runs[runIndex]];
        while (indexJob_.at[c] < r.hideCount && hideAt(r, indexJob_.at[c]).noteId == noteId) {
          const uint32_t pos = (static_cast<uint32_t>(runIndex) << 8) | hideAt(r, indexJob_.at[c]).origin;
          if (pos > latest) latest = pos;
          ++indexJob_.at[c];
          ++done;
        }
      }
      PassEvent entry;
      entry.noteId = noteId;
      entry.tick = latest;
      if (pushMidi(runs_[indexJob_.out], entry) != AppendResult::Ok) {
        endIndexJob(false);
        return IndexStep::DeniedNoPage;
      }
    }
    return IndexStep::Pending;
  }

  /// Explicit cancellation of the build in progress (never implicit).
  void cancelEditIndexBuild() {
    if (indexJob_.active) endIndexJob(false);
  }

  // ---- C3d: resumable window producer (revision() / source() projections) -------------------------------------
  //
  // Request = (manifest, expected identity, expected revision, interval). A manifest that is gone or no longer has
  // that identity/revision is `SourceMoved`; no current manifest is `ImportOwed`; hides without a published index for
  // that exact manifest are `IndexOwed` (the index is never built here). The job pins the manifest, so it finishes
  // honestly as the identity it started on even if another manifest becomes current meanwhile.

  WindowResult beginWindow(ManifestId id, uint32_t expectedIdentity, uint32_t expectedRevision, uint32_t fromTick,
                           uint32_t toTick, PassEvent* out, uint32_t capacity, bool applyHides = true) {
    if (window_.active) return WindowResult::CapacityDenied;  // one producer at a time
    if (id == kNoManifest) return WindowResult::ImportOwed;
    if (!manifests_[id].live || manifests_[id].identity != expectedIdentity ||
        manifests_[id].revision != expectedRevision) {
      return WindowResult::SourceMoved;
    }
    const EditIndexSlot* slot = nullptr;
    if (applyHides && manifestHasHides(id)) {
      slot = findIndexSlot(id);
      if (slot == nullptr) return WindowResult::IndexOwed;
    }
    pin(id);
    const RevisionManifest& m = manifests_[id];
    Merge& merge = windowMerge_;
    merge.reset();
    merge.limit = m.runCount;
    merge.index = slot;
    for (uint8_t i = 0; i < m.runCount; ++i) {
      const PassRun& r = runs_[m.runs[i]];
      for (uint8_t s = 0; s < r.segCount; ++s) {
        merge.add(i, lowerBoundTick(r, r.segStart[s], r.segEnd(s), fromTick), r.segEnd(s));
      }
    }
    for (uint16_t k = 0; k < merge.count; ++k) {
      if (advanceToVisible(m, merge, k, toTick)) heapPush(m, merge, k);
    }
    window_.active = true;
    window_.manifest = id;
    window_.out = out;
    window_.capacity = capacity;
    window_.toTick = toTick;
    window_.stamp = WindowStamp{m.identity, m.revision, fromTick, toTick, 0};
    return WindowResult::Pending;
  }

  /// Emit at most `maxRows` rows. `Ready` when the interval is complete (stamp = what was read); `CapacityDenied` if
  /// the caller's buffer cannot hold the window (the job ends; nothing is labelled complete).
  WindowResult stepWindow(uint32_t maxRows) {
    if (!window_.active) return WindowResult::Idle;
    const RevisionManifest& m = manifests_[window_.manifest];
    Merge& merge = windowMerge_;
    for (uint32_t n = 0; n < maxRows; ++n) {
      if (merge.heapSize == 0) {
        endWindow();
        return WindowResult::Ready;
      }
      if (window_.stamp.count >= window_.capacity) {
        endWindow();
        return WindowResult::CapacityDenied;
      }
      const uint16_t k = heapPop(m, merge);
      window_.out[window_.stamp.count++] = midiAt(runs_[m.runs[merge.run[k]]], merge.at[k]);
      ++merge.at[k];
      if (advanceToVisible(m, merge, k, window_.toTick)) heapPush(m, merge, k);
    }
    if (merge.heapSize == 0) {
      endWindow();
      return WindowResult::Ready;
    }
    return WindowResult::Pending;
  }

  void cancelWindow() {
    if (window_.active) endWindow();
  }
  bool windowActive() const { return window_.active; }
  const WindowStamp& windowStamp() const { return window_.stamp; }

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

  /// Compaction planner (B.5c): start a job on the first history-closed range of layered runs (never the base run at
  /// index 0) whose input fits `maxInputRows`, so the copy in flight stays bounded. A range that would re-merge a large
  /// merged run therefore starts after it (LSM-style levels). `NothingToCompact` when no such range exists.
  CompactResult planCompaction(uint32_t maxInputRows) {
    if (job_.active) return CompactResult::DeniedBusy;
    if (current_ == kNoManifest) return CompactResult::NothingToCompact;
    const RevisionManifest& m = manifests_[current_];
    for (uint8_t first = 1; first + 1 < m.runCount; ++first) {
      const uint8_t count = largestClosedRangeFrom(first);
      uint8_t fitting = 0;
      uint32_t rows = 0;
      uint32_t origins = 0;
      for (uint8_t i = 0; i < count; ++i) {
        const PassRun& r = runs_[m.runs[first + i]];
        rows += r.midiCount;
        origins += r.originPage == kNoPage ? 1u : r.originCount;
        if (rows > maxInputRows || origins > kMaxOrigins) break;
        fitting = static_cast<uint8_t>(i + 1);
      }
      if (fitting >= 2) return beginCompaction(first, fitting);
    }
    return CompactResult::NothingToCompact;
  }

  /// Start merging runs [first, first + count) of the current manifest. Off-clock maintenance, step by step; a writer,
  /// readers and undo may run meanwhile. The source manifest is pinned until the job ends. Lossless (DEC-245): every
  /// source row and every hide is copied with its source pass as `origin` and its `seq` unchanged, so the committed
  /// source multiset, provenance and every manifest's effective output are preserved.
  CompactResult beginCompaction(uint8_t first, uint8_t count,
                                CompactionScope scope = CompactionScope::HistoryClosedOnly) {
    if (job_.active) return CompactResult::DeniedBusy;
    if (current_ == kNoManifest) return CompactResult::NothingToCompact;
    const RevisionManifest& src = manifests_[current_];
    if (count < 2 || first + count > src.runCount) return CompactResult::NothingToCompact;
    if (scope == CompactionScope::HistoryClosedOnly && !rangeIsHistoryClosed(first, count)) {
      return CompactResult::NotHistoryClosed;
    }
    uint32_t origins = 0;
    for (uint8_t i = first; i < first + count; ++i) {
      const PassRun& r = runs_[src.runs[i]];
      origins += r.originPage == kNoPage ? 1u : r.originCount;
    }
    if (origins > kMaxOrigins) return CompactResult::NothingToCompact;
    const RunId out = takeRun(PassKind::Compacted);
    if (out == kNoRun) return CompactResult::DeniedNoRun;
    const ManifestId dst = takeManifest();
    if (dst == kNoManifest) {
      runLive_[out] = false;
      return CompactResult::DeniedNoManifest;
    }
    const PageId originPage = pool_.take();
    if (originPage == kNoPage) {
      runLive_[out] = false;
      manifests_[dst].live = false;
      return CompactResult::DeniedNoPage;
    }
    // Origin table: the source passes' serials, in run order, flattening already-compacted runs.
    PassRun& merged = runs_[out];
    merged.originPage = originPage;
    merged.originCount = 0;
    uint32_t* serials = reinterpret_cast<uint32_t*>(&pool_.row(originPage, 0));
    for (uint8_t i = first; i < first + count; ++i) {
      const RunId id = src.runs[i];
      job_.originBase[i] = merged.originCount;
      const uint8_t n = runs_[id].originPage == kNoPage ? 1 : runs_[id].originCount;
      for (uint8_t o = 0; o < n; ++o) serials[merged.originCount++] = originEntry(id, o);
    }
    job_.active = true;
    job_.source = current_;
    job_.first = first;
    job_.end = static_cast<uint8_t>(first + count);
    job_.out = out;
    job_.dst = dst;
    ++manifests_[current_].refs;
    Merge& merge = job_.merge;
    merge.reset();
    merge.limit = job_.end;  // no hide runs collected: compaction copies every row (lossless)
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
      copy.origin = static_cast<uint8_t>(job_.originBase[merge.run[k]] + copy.origin);
      ++merge.at[k];
      if (advanceToVisible(src, merge, k, UINT32_MAX)) heapPush(src, merge, k);
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
      manifests_[i].readerPins = 0;
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
    nextSerial_ = 1;
    stagedCount_ = 0;
    stagedLoopLength_ = 0;
    slowSeals_ = 0;
    seal_ = SealJob{};
    job_.active = false;
    indexJob_.active = false;
    window_.active = false;
    for (uint8_t i = 0; i < kIndexSlots; ++i) indexSlots_[i] = EditIndexSlot{};
    demanded_ = kNoManifest;
    demandedIdentity_ = 0;
  }

 private:
  static constexpr uint16_t kMaxCursors = static_cast<uint16_t>(Cap::kRunsPerManifest) * Cap::kSegmentsPerRun;
  static constexpr uint32_t kEditOriginBit = 0x80000000u;  // origin-table entry: serial | (source was an Edit pass)
  uint32_t originEntry(RunId id, uint8_t origin) const {
    const PassRun& r = runs_[id];
    if (r.originPage == kNoPage) return r.serial | (r.kind == PassKind::Edit ? kEditOriginBit : 0u);
    return reinterpret_cast<const uint32_t*>(&pool_.row(r.originPage, 0))[origin];
  }
  /// Same-tick class inversions are fixed in place while appending, but only across this many rows.
  static constexpr uint32_t kMaxLocalShift = 32;
  /// Source passes one compacted run can name (origin is a byte; the table is one page of uint32 serials).
  static constexpr uint32_t kMaxOrigins =
      (sizeof(PassEvent) * Cap::kEventsPerPage / 4) < 255 ? (sizeof(PassEvent) * Cap::kEventsPerPage / 4) : 255;
  /// Equal-tick group size tracked for the cross-row projections; beyond it, collapse sees only the first rows.
  static constexpr uint32_t kGroupCapacity = 256;

  // Production's cross-pass collapse (`midiEventMatchesSnapshot`), between capture passes: a row matches an earlier-run row on the same tick
  // with the same type and channel; CCs also match number and value; notes also match pitch, and a NoteOn never matches
  // another NoteOn with a different id (NoteOffs carry no id in production, so they match by pitch).
  // "Earlier pass" is earlier by (run index, origin): inside a compacted run, an earlier source pass.
  bool collapsesWithEarlierRun(const RevisionManifest& m, const PassEvent* group, const uint8_t* groupRun,
                               const uint8_t* groupOrigin, uint32_t groupCount, const PassEvent& row,
                               uint8_t runIndex) const {
    const uint32_t tracked = groupCount < kGroupCapacity ? groupCount : kGroupCapacity;
    for (uint32_t g = 0; g < tracked; ++g) {
      const PassEvent& e = group[g];
      const bool earlier = groupRun[g] < runIndex || (groupRun[g] == runIndex && groupOrigin[g] < row.origin);
      if (!earlier || e.status != row.status) continue;
      if (sourceIsEdit(m.runs[groupRun[g]], groupOrigin[g])) continue;
      const uint8_t type = row.status & 0xF0;
      if (type == 0xB0) {
        if (e.data1 == row.data1 && e.data2 == row.data2) return true;
        continue;
      }
      if (type == 0x80 || type == 0x90) {
        if (e.data1 != row.data1) continue;
        const bool bothOn = equalTickClass(e) == 1 && equalTickClass(row) == 1;
        if (bothOn && e.noteId != 0 && row.noteId != 0 && e.noteId != row.noteId) continue;
        return true;
      }
      return true;
    }
    return false;
  }

  struct EditIndexSlot;

  /// Cursors over segments plus a binary min-heap of cursor indices (DEC-244 order) and the runs that carry hides.
  struct Merge {
    uint32_t at[kMaxCursors];
    uint32_t end[kMaxCursors];
    // Cached DEC-244 key of each cursor's current row, so heap comparisons never re-read rows from (PSRAM) pages:
    // (tick << 8) | (class << 6) | run index, then seq.
    uint64_t key[kMaxCursors];
    uint64_t seq[kMaxCursors];  // (origin << 32) | seq: inside a compacted run, source pass order then append order
    uint8_t run[kMaxCursors];  // index into the manifest's run list
    uint16_t heap[kMaxCursors];
    uint8_t hideRuns[Cap::kRunsPerManifest];
    uint16_t count = 0;
    uint16_t heapSize = 0;
    uint8_t hideRunCount = 0;
    uint8_t limit = 0;  // hides are consulted from runs before this index
    const EditIndexSlot* index = nullptr;  // when set, hides resolve through the edit index
    void reset() {
      count = heapSize = 0;
      hideRunCount = 0;
      index = nullptr;
    }
    void add(uint8_t runIndex, uint32_t from, uint32_t to) {
      at[count] = from;
      end[count] = to;
      run[count] = runIndex;
      ++count;
    }
  };

  enum class SlotState : uint8_t { Empty, Building, Ready };
  struct EditIndexSlot {
    SlotState state = SlotState::Empty;
    uint32_t identity = 0;  // the manifest identity this index resolves (Building: the one being built)
    ManifestId manifest = kNoManifest;
    RunId run = kNoRun;  // rows: noteId, tick = latest hide position
  };
  static constexpr uint8_t kIndexSlots = 2;

  struct IndexJob {
    bool active = false;
    uint32_t identity = 0;
    ManifestId manifest = kNoManifest;
    RunId out = kNoRun;
    EditIndexSlot* slot = nullptr;
    uint8_t cursorCount = 0;
    uint8_t runIndex[Cap::kRunsPerManifest];
    uint32_t at[Cap::kRunsPerManifest];
  };

  const EditIndexSlot* findIndexSlot(ManifestId id) const {
    if (id == kNoManifest) return nullptr;
    const uint32_t identity = manifests_[id].identity;
    for (uint8_t i = 0; i < kIndexSlots; ++i) {
      if (indexSlots_[i].state == SlotState::Ready && indexSlots_[i].identity == identity &&
          indexSlots_[i].manifest == id) {
        return &indexSlots_[i];
      }
    }
    return nullptr;
  }

  // An Empty slot, else a Ready slot whose manifest no reader pins and that is not the current manifest's index;
  // else a Ready unpinned slot of the current manifest. Building slots and reader-pinned slots are never reclaimed.
  EditIndexSlot* reclaimableSlot() {
    EditIndexSlot* fallback = nullptr;
    for (uint8_t i = 0; i < kIndexSlots; ++i) {
      EditIndexSlot& slot = indexSlots_[i];
      if (slot.state == SlotState::Empty) return &slot;
      if (slot.state != SlotState::Ready || manifests_[slot.manifest].readerPins != 0) continue;
      if (slot.manifest != current_) return &slot;
      fallback = &slot;
    }
    return fallback;
  }

  ManifestId nextIndexTarget() const {
    if (demanded_ != kNoManifest && manifests_[demanded_].live && manifests_[demanded_].identity == demandedIdentity_ &&
        manifestHasHides(demanded_) && !editIndexReady(demanded_)) {
      return demanded_;
    }
    return editIndexOwed() ? current_ : kNoManifest;
  }

  bool indexLookup(const EditIndexSlot& slot, uint32_t noteId, uint32_t& latest) const {
    const PassRun& r = runs_[slot.run];
    uint32_t lo = 0;
    uint32_t hi = r.midiCount;
    while (lo < hi) {
      const uint32_t mid = lo + (hi - lo) / 2;
      const PassEvent& e = midiAt(r, mid);
      if (e.noteId == noteId) {
        latest = e.tick;
        return true;
      }
      if (e.noteId < noteId) {
        lo = mid + 1;
      } else {
        hi = mid;
      }
    }
    return false;
  }

  void clearSlot(EditIndexSlot& slot) {
    if (slot.state == SlotState::Ready) {
      releaseRunPages(runs_[slot.run]);
      runLive_[slot.run] = false;
      releaseManifest(slot.manifest);
    }
    slot.state = SlotState::Empty;
    slot.manifest = kNoManifest;
    slot.identity = 0;
  }

  bool beginIndexJob(ManifestId target, EditIndexSlot& slot) {
    clearSlot(slot);  // only ever called on a reclaimable slot
    const RunId out = takeRun(PassKind::EditIndex);
    if (out == kNoRun) return false;
    indexJob_.active = true;
    indexJob_.manifest = target;
    indexJob_.identity = manifests_[target].identity;
    indexJob_.out = out;
    indexJob_.slot = &slot;
    indexJob_.cursorCount = 0;
    slot.state = SlotState::Building;
    slot.identity = indexJob_.identity;
    slot.manifest = target;
    ++manifests_[target].refs;  // the job (then the published slot) holds the manifest it resolves
    const RevisionManifest& m = manifests_[target];
    for (uint8_t i = 0; i < m.runCount; ++i) {
      if (runs_[m.runs[i]].hideCount == 0) continue;
      indexJob_.runIndex[indexJob_.cursorCount] = i;
      indexJob_.at[indexJob_.cursorCount] = 0;
      ++indexJob_.cursorCount;
    }
    return true;
  }

  void endIndexJob(bool published) {
    if (!published) {
      releaseRunPages(runs_[indexJob_.out]);
      runLive_[indexJob_.out] = false;
      releaseManifest(indexJob_.manifest);
      indexJob_.slot->state = SlotState::Empty;
      indexJob_.slot->manifest = kNoManifest;
    }
    indexJob_.active = false;
  }

  // O(1) state transition: Building(identity) -> Ready(identity), for the identity the job started on.
  IndexStep publishIndexJob() {
    EditIndexSlot& slot = *indexJob_.slot;
    slot.state = SlotState::Ready;
    slot.run = indexJob_.out;
    runs_[indexJob_.out].committed = true;
    if (demanded_ == indexJob_.manifest) demanded_ = kNoManifest;
    endIndexJob(true);
    return IndexStep::Published;
  }

  struct CompactionJob {
    bool active = false;
    ManifestId source = kNoManifest;
    uint8_t first = 0;
    uint8_t end = 0;  // one past the last compacted run
    RunId out = kNoRun;
    ManifestId dst = kNoManifest;
    uint8_t originBase[Cap::kRunsPerManifest];  // origin offset of each source run in the merged run
    Merge merge;
  };

  void collectHideRuns(const RevisionManifest& m, uint8_t from, uint8_t to, Merge& merge) const {
    merge.hideRunCount = 0;
    for (uint8_t i = from; i < to; ++i) {
      if (runs_[m.runs[i]].hideCount != 0) merge.hideRuns[merge.hideRunCount++] = i;
    }
  }

  /// A row (run `runIndex`, source `origin`) is hidden by any hide in a later run, or by a hide inside its own
  /// (compacted) run that came from a later source pass.
  bool hidden(const RevisionManifest& m, const Merge& merge, uint8_t runIndex, uint8_t origin, uint32_t noteId) const {
    if (noteId == 0) return false;
    if (merge.index != nullptr) {
      uint32_t latest = 0;
      return indexLookup(*merge.index, noteId, latest) &&
             latest > ((static_cast<uint32_t>(runIndex) << 8) | origin);
    }
    for (uint8_t h = 0; h < merge.hideRunCount; ++h) {
      const uint8_t j = merge.hideRuns[h];
      if (j >= merge.limit || j < runIndex) continue;
      if (runHides(runs_[m.runs[j]], noteId, j == runIndex ? origin + 1u : 0u)) return true;
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
      if (!hidden(m, merge, merge.run[k], row.origin, row.noteId)) {
        merge.key[k] = (static_cast<uint64_t>(row.tick) << 8) | (static_cast<uint64_t>(equalTickClass(row)) << 6) |
                       merge.run[k];
        merge.seq[k] = (static_cast<uint64_t>(row.origin) << 32) | row.seq;
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

  // A page list: the first `inlineCount` page ids inline, the rest in directory pages taken from the pool.
  PageId listPage(const PageId* inl, uint8_t inlineCount, const PageId* dir, uint32_t p) const {
    if (p < inlineCount) return inl[p];
    const uint32_t q = p - inlineCount;
    return pool_.dirEntry(dir[q / kIdsPerDirPage], q % kIdsPerDirPage);
  }
  AppendResult listAdd(PageId* inl, uint8_t inlineCount, PageId* dir, uint16_t& count, uint16_t& dirCount) {
    if (count >= Cap::kPagesPerRun) return AppendResult::DeniedRunFull;
    const uint32_t p = count;
    if (p >= inlineCount && (p - inlineCount) % kIdsPerDirPage == 0) {
      const PageId d = pool_.take();
      if (d == kNoPage) return AppendResult::DeniedNoPage;
      dir[dirCount++] = d;
    }
    const PageId page = pool_.take();
    if (page == kNoPage) return AppendResult::DeniedNoPage;
    if (p < inlineCount) {
      inl[p] = page;
    } else {
      const uint32_t q = p - inlineCount;
      pool_.dirEntry(dir[q / kIdsPerDirPage], q % kIdsPerDirPage) = page;
    }
    ++count;
    return AppendResult::Ok;
  }
  void listRelease(const PageId* inl, uint8_t inlineCount, const PageId* dir, uint16_t& count, uint16_t& dirCount) {
    for (uint32_t p = 0; p < count; ++p) pool_.give(listPage(inl, inlineCount, dir, p));
    for (uint16_t i = 0; i < dirCount; ++i) pool_.give(dir[i]);
    count = 0;
    dirCount = 0;
  }
  PageId dataPage(const PassRun& r, uint32_t p) const { return listPage(r.inlinePages, kInlinePages, r.dirPages, p); }
  PageId hidePage(const PassRun& r, uint32_t p) const {
    return listPage(r.hideInline, kHideInlinePages, r.hideDirPages, p);
  }
  PassEvent& midiRef(PassRun& r, uint32_t index) {
    return pool_.row(dataPage(r, index / Cap::kEventsPerPage), index % Cap::kEventsPerPage);
  }
  const PassEvent& midiAt(const PassRun& r, uint32_t index) const {
    return pool_.row(dataPage(r, index / Cap::kEventsPerPage), index % Cap::kEventsPerPage);
  }
  PassEvent& hideRef(PassRun& r, uint32_t index) {
    return pool_.row(hidePage(r, index / Cap::kEventsPerPage), index % Cap::kEventsPerPage);
  }
  const PassEvent& hideAt(const PassRun& r, uint32_t index) const {
    return pool_.row(hidePage(r, index / Cap::kEventsPerPage), index % Cap::kEventsPerPage);
  }

  RunId takeRun(PassKind kind) {
    for (uint16_t i = 0; i < Cap::kRunCount; ++i) {
      if (!runLive_[i]) {
        runLive_[i] = true;
        runs_[i].clear(kind);
        runs_[i].serial = nextSerial_++;
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
        m.readerPins = 0;
        m.runCount = 0;
        m.revision = 0;
        m.loopLengthTicks = 0;
        m.identity = nextIdentity_++;
        return static_cast<ManifestId>(i);
      }
    }
    return kNoManifest;
  }

  AppendResult addDataPage(PassRun& r) {
    return listAdd(r.inlinePages, kInlinePages, r.dirPages, r.pageCount, r.dirPageCount);
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
      const AppendResult added =
          listAdd(r.hideInline, kHideInlinePages, r.hideDirPages, r.hidePageCount, r.hideDirPageCount);
      if (added != AppendResult::Ok) return added;
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
    if (hides) return x.noteId != y.noteId ? x.noteId < y.noteId : x.origin < y.origin;
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

  /// A hide for `noteId` whose source origin is at least `minOrigin` (hides sorted by noteId, then origin).
  bool runHides(const PassRun& r, uint32_t noteId, uint32_t minOrigin) const {
    uint32_t lo = 0;
    uint32_t hi = r.hideCount;
    while (lo < hi) {  // first hide with noteId >= target
      const uint32_t mid = lo + (hi - lo) / 2;
      if (hideAt(r, mid).noteId < noteId) {
        lo = mid + 1;
      } else {
        hi = mid;
      }
    }
    for (; lo < r.hideCount && hideAt(r, lo).noteId == noteId; ++lo) {
      if (hideAt(r, lo).origin >= minOrigin) return true;
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
    {
      // Carry every hide with its source origin (lossless; a hide may target runs before the range or rows of an
      // earlier source pass inside it). Bounded by the hide rows, not the content.
      const RevisionManifest& src = manifests_[job_.source];
      for (uint8_t i = job_.first; i < job_.end; ++i) {
        const PassRun& r = runs_[src.runs[i]];
        for (uint32_t h = 0; h < r.hideCount; ++h) {
          PassEvent hide = hideAt(r, h);
          hide.origin = static_cast<uint8_t>(job_.originBase[i] + hide.origin);
          if (pushHide(merged, hide) != AppendResult::Ok) {
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
    listRelease(r.inlinePages, kInlinePages, r.dirPages, r.pageCount, r.dirPageCount);
    listRelease(r.hideInline, kHideInlinePages, r.hideDirPages, r.hidePageCount, r.hideDirPageCount);
    if (r.originPage != kNoPage) pool_.give(r.originPage);
    r.originPage = kNoPage;

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
  uint32_t nextSerial_ = 1;
  RunId staged_[Cap::kRunsPerManifest];
  uint8_t stagedCount_ = 0;
  uint32_t stagedLoopLength_ = 0;
  uint32_t slowSeals_ = 0;
  enum class SealPhase : uint8_t { None, MidiHeapify, MidiExtract, HideHeapify, HideExtract, Done };
  struct SealJob {
    SealPhase phase = SealPhase::None;
    uint32_t cursor = 0;
  } seal_;
  CompactionJob job_;
  mutable Merge composeScratch_;
  struct WindowJob {
    bool active = false;
    ManifestId manifest = kNoManifest;
    PassEvent* out = nullptr;
    uint32_t capacity = 0;
    uint32_t toTick = 0;
    WindowStamp stamp;
  };
  WindowJob window_;
  Merge windowMerge_;
  void endWindow() {
    window_.active = false;
    unpin(window_.manifest);
  }
  EditIndexSlot indexSlots_[kIndexSlots];
  IndexJob indexJob_;
  ManifestId demanded_ = kNoManifest;
  uint32_t demandedIdentity_ = 0;
  mutable uint8_t composeGroupRun_[kGroupCapacity];
  mutable uint8_t composeGroupOrigin_[kGroupCapacity];
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
