//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
#pragma once

// PROTOTYPE -- openspec/changes/revision-manifest-prototype. Not wired to production playback.
//
// Canonical loop content as fixed-capacity pages, immutable committed passes and tiny immutable revision
// manifests. Every table is sized at compile time; nothing here allocates after construction.
//
//   EventPage        fixed array of PassEvent rows; pages move between a free stack and pass runs by index.
//   PassRun          the content of one pass (Record, overdub, edit, or a compacted prefix). Mutable only while its
//                    single writer appends. Its Midi rows are kept as a few sorted segments *while appending*, so
//                    commit does not sort them (B.4: a seal-time sort cost 134 ms at 16,640 rows). HideNote rows live
//                    in their own small region, the only part sorted at commit.
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
  DeniedWriterBusy,  // kept for API stability; compaction no longer excludes a writer
  DeniedBusy,        // another compaction job is active
  Superseded,        // the current manifest no longer starts with the compacted runs; result discarded
};
/// Step outcome of a bounded compaction job.
enum class CompactStep : uint8_t { Idle, Pending, Published, Superseded, DeniedNoPage };

/// What a derived window can prove about itself: the manifest it read and that manifest's revision.
/// `revision` is logical (compaction keeps it); `identity` is physical and never reused, unlike the `manifest` slot.
struct ComposeStamp {
  ManifestId manifest = kNoManifest;
  uint32_t identity = 0;
  uint32_t revision = 0;
  uint32_t count = 0;
  ComposeStatus status = ComposeStatus::NoManifest;
};

/// Compile-time capacities. The target sizing is task B.5.
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

template <typename Cap>
class Store {
 public:
  /// Raw row storage: constructing the store does not touch the rows (B.4: zero-filling 1 MiB cost 42 ms).
  struct EventPage {
    alignas(PassEvent) unsigned char raw[sizeof(PassEvent) * Cap::kEventsPerPage];
    PassEvent& row(uint32_t i) { return reinterpret_cast<PassEvent*>(raw)[i]; }
    const PassEvent& row(uint32_t i) const { return reinterpret_cast<const PassEvent*>(raw)[i]; }
  };

  struct PassRun {
    PageId pages[Cap::kPagesPerRun];         // Midi rows, virtual index [0, midiCount)
    PageId hidePages[Cap::kHidePagesPerRun];  // HideNote rows, virtual index [0, hideCount), by noteId once committed
    uint32_t segStart[Cap::kSegmentsPerRun];  // segment s = [segStart[s], segStart[s+1] or midiCount)
    uint16_t pageCount;
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
    void clear(PassKind k) {  // scalars only: the page/segment arrays are read only below their counts
      pageCount = hidePageCount = 0;
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

  Store() { reset(); }

  void reset() {
    for (uint16_t i = 0; i < Cap::kPageCount; ++i) {
      freePages_[i] = static_cast<PageId>(Cap::kPageCount - 1 - i);
    }
    freePageTop_ = Cap::kPageCount;
    for (uint16_t i = 0; i < Cap::kRunCount; ++i) {
      runs_[i].clear(PassKind::Record);
      runLive_[i] = false;
    }
    for (uint16_t i = 0; i < Cap::kManifestCount; ++i) {
      manifests_[i].live = false;
      manifests_[i].refs = 0;
      manifests_[i].runCount = 0;
    }
    for (uint8_t i = 0; i < Cap::kHistoryDepth; ++i) {
      history_[i] = kNoManifest;
    }
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

  // ---- Pool accounting ---------------------------------------------------------------------------------------------
  uint16_t freePageCount() const { return freePageTop_; }
  /// Pages held by live runs. Conservation: `ownedPageCount() + freePageCount() == kPageCount`.
  uint32_t ownedPageCount() const {
    uint32_t n = 0;
    for (uint16_t i = 0; i < Cap::kRunCount; ++i) {
      n += runLive_[i] ? static_cast<uint32_t>(runs_[i].pageCount) + runs_[i].hidePageCount : 0;
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
    const PageId page = takePage();
    if (page == kNoPage) {
      runLive_[run] = false;
      manifests_[manifest].live = false;
      return AdmitResult::DeniedNoPage;
    }
    PassRun& r = runs_[run];
    r.pages[0] = page;
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
  /// Cost is bounded by the segments in the manifest and the rows inside the interval, not by the loop length.
  ComposeStamp composeWindow(ManifestId id, uint32_t fromTick, uint32_t toTick, PassEvent* out, uint32_t capacity) const {
    ComposeStamp stamp;
    if (id == kNoManifest || !manifests_[id].live) return stamp;
    const RevisionManifest& m = manifests_[id];
    stamp.manifest = id;
    stamp.identity = m.identity;
    stamp.revision = m.revision;
    stamp.status = ComposeStatus::Complete;
    Cursors cursors;
    for (uint8_t i = 0; i < m.runCount; ++i) {
      const PassRun& r = runs_[m.runs[i]];
      for (uint8_t s = 0; s < r.segCount; ++s) {
        cursors.add(i, lowerBoundTick(r, r.segStart[s], r.segEnd(s), fromTick), r.segEnd(s));
      }
    }
    for (;;) {
      uint16_t best = kNoCursor;
      for (uint16_t k = 0; k < cursors.count; ++k) {
        const PassRun& r = runs_[m.runs[cursors.run[k]]];
        while (cursors.at[k] < cursors.end[k]) {
          const PassEvent& row = midiAt(r, cursors.at[k]);
          if (row.tick >= toTick) {
            cursors.at[k] = cursors.end[k];
            break;
          }
          if (!hiddenByLaterRun(m, cursors.run[k], m.runCount, row.noteId)) break;
          ++cursors.at[k];
        }
        if (cursors.at[k] >= cursors.end[k]) continue;
        if (best == kNoCursor || cursorBefore(m, cursors, k, best)) best = k;
      }
      if (best == kNoCursor) break;
      if (stamp.count >= capacity) {
        stamp.status = ComposeStatus::OutputFull;
        break;
      }
      out[stamp.count++] = midiAt(runs_[m.runs[cursors.run[best]]], cursors.at[best]);
      ++cursors.at[best];
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
  /// Start merging the first `prefixRuns` runs of the current manifest. Off-clock maintenance, step by step; a writer,
  /// readers and undo may run meanwhile. The source manifest is pinned until the job ends.
  CompactResult beginCompaction(uint8_t prefixRuns) {
    if (job_.active) return CompactResult::DeniedBusy;
    if (current_ == kNoManifest) return CompactResult::NothingToCompact;
    const RevisionManifest& src = manifests_[current_];
    if (prefixRuns < 2 || prefixRuns > src.runCount) return CompactResult::NothingToCompact;
    const RunId out = takeRun(PassKind::Compacted);
    if (out == kNoRun) return CompactResult::DeniedNoRun;
    const ManifestId dst = takeManifest();
    if (dst == kNoManifest) {
      runLive_[out] = false;
      return CompactResult::DeniedNoManifest;
    }
    job_.active = true;
    job_.source = current_;
    job_.prefix = prefixRuns;
    job_.out = out;
    job_.dst = dst;
    job_.seq = 0;
    ++manifests_[current_].refs;
    job_.cursors.count = 0;
    for (uint8_t i = 0; i < prefixRuns; ++i) {
      const PassRun& r = runs_[src.runs[i]];
      for (uint8_t s = 0; s < r.segCount; ++s) job_.cursors.add(i, r.segStart[s], r.segEnd(s));
    }
    runs_[out].segCount = 1;
    runs_[out].segStart[0] = 0;
    return CompactResult::Ok;
  }

  bool compacting() const { return job_.active; }

  /// Advance the job by at most `maxRows` source rows (copied or dropped as hidden). Publishes when done: if the
  /// current manifest still starts with the compacted runs, a manifest {compacted, the current's remaining runs} with
  /// the current's revision replaces it; otherwise the result is discarded (`Superseded`).
  CompactStep stepCompaction(uint32_t maxRows) {
    if (!job_.active) return CompactStep::Idle;
    const RevisionManifest& src = manifests_[job_.source];
    PassRun& merged = runs_[job_.out];
    for (uint32_t done = 0; done < maxRows; ++done) {
      uint16_t best = kNoCursor;
      for (uint16_t k = 0; k < job_.cursors.count; ++k) {
        if (job_.cursors.at[k] >= job_.cursors.end[k]) continue;
        if (best == kNoCursor || cursorBefore(src, job_.cursors, k, best)) best = k;
      }
      if (best == kNoCursor) return finishCompaction();
      const PassEvent& row = midiAt(runs_[src.runs[job_.cursors.run[best]]], job_.cursors.at[best]);
      ++job_.cursors.at[best];
      if (hiddenByLaterRun(src, job_.cursors.run[best], job_.prefix, row.noteId)) continue;
      PassEvent copy = row;
      copy.seq = job_.seq++;
      if (pushMidi(merged, copy) != AppendResult::Ok) {
        endCompaction(false);
        return CompactStep::DeniedNoPage;
      }
    }
    return CompactStep::Pending;
  }

  void abandonCompaction() {
    if (job_.active) endCompaction(false);
  }

  /// Synchronous convenience (tests, off-line): begin + run every step.
  CompactResult compactCurrentPrefix(uint8_t runCount) {
    const CompactResult begun = beginCompaction(runCount);
    if (begun != CompactResult::Ok) return begun;
    switch (stepCompaction(UINT32_MAX)) {
      case CompactStep::Published: return CompactResult::Ok;
      case CompactStep::Superseded: return CompactResult::Superseded;
      default: return CompactResult::DeniedNoPage;
    }
  }

 private:
  static constexpr uint16_t kNoCursor = 0xFFFFu;
  static constexpr uint16_t kMaxCursors = static_cast<uint16_t>(Cap::kRunsPerManifest) * Cap::kSegmentsPerRun;
  /// Same-tick class inversions are fixed in place while appending, but only across this many rows.
  static constexpr uint32_t kMaxLocalShift = 32;

  struct Cursors {
    uint32_t at[kMaxCursors];
    uint32_t end[kMaxCursors];
    uint8_t run[kMaxCursors];  // index into the manifest's run list
    uint16_t count = 0;
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
    uint8_t prefix = 0;
    RunId out = kNoRun;
    ManifestId dst = kNoManifest;
    uint32_t seq = 0;
    Cursors cursors;
  };

  // DEC-244 tie-break between two cursors' current rows: (tick, class), then run order, then append order.
  bool cursorBefore(const RevisionManifest& m, const Cursors& c, uint16_t a, uint16_t b) const {
    const PassEvent& ra = midiAt(runs_[m.runs[c.run[a]]], c.at[a]);
    const PassEvent& rb = midiAt(runs_[m.runs[c.run[b]]], c.at[b]);
    if (tickClassLess(ra, rb)) return true;
    if (tickClassLess(rb, ra)) return false;
    if (c.run[a] != c.run[b]) return c.run[a] < c.run[b];
    return ra.seq < rb.seq;
  }

  PassEvent& midiRef(PassRun& r, uint32_t index) {
    return pages_[r.pages[index / Cap::kEventsPerPage]].row(index % Cap::kEventsPerPage);
  }
  const PassEvent& midiAt(const PassRun& r, uint32_t index) const {
    return pages_[r.pages[index / Cap::kEventsPerPage]].row(index % Cap::kEventsPerPage);
  }
  PassEvent& hideRef(PassRun& r, uint32_t index) {
    return pages_[r.hidePages[index / Cap::kEventsPerPage]].row(index % Cap::kEventsPerPage);
  }
  const PassEvent& hideAt(const PassRun& r, uint32_t index) const {
    return pages_[r.hidePages[index / Cap::kEventsPerPage]].row(index % Cap::kEventsPerPage);
  }

  PageId takePage() { return freePageTop_ == 0 ? kNoPage : freePages_[--freePageTop_]; }
  void returnPage(PageId id) { freePages_[freePageTop_++] = id; }

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

  AppendResult pushMidi(PassRun& r, const PassEvent& row) {
    const uint32_t index = r.midiCount;
    if (index >= static_cast<uint32_t>(r.pageCount) * Cap::kEventsPerPage) {
      if (r.pageCount >= Cap::kPagesPerRun) return AppendResult::DeniedRunFull;
      const PageId page = takePage();
      if (page == kNoPage) return AppendResult::DeniedNoPage;
      r.pages[r.pageCount++] = page;
    }
    midiRef(r, index) = row;
    ++r.midiCount;
    return AppendResult::Ok;
  }

  AppendResult pushHide(PassRun& r, const PassEvent& row) {
    const uint32_t index = r.hideCount;
    if (index >= static_cast<uint32_t>(r.hidePageCount) * Cap::kEventsPerPage) {
      if (r.hidePageCount >= Cap::kHidePagesPerRun) return AppendResult::DeniedRunFull;
      const PageId page = takePage();
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

  /// Hidden by a run after `runIndex` and before `limit` in `m` (the whole manifest, or a compaction prefix).
  bool hiddenByLaterRun(const RevisionManifest& m, uint8_t runIndex, uint8_t limit, uint32_t noteId) const {
    if (noteId == 0) return false;
    for (uint8_t j = runIndex + 1; j < limit; ++j) {
      if (runs_[m.runs[j]].hideCount != 0 && runHides(runs_[m.runs[j]], noteId)) return true;
    }
    return false;
  }

  CompactStep finishCompaction() {
    const RevisionManifest& src = manifests_[job_.source];
    bool prefixMatches = current_ != kNoManifest && manifests_[current_].runCount >= job_.prefix;
    for (uint8_t i = 0; prefixMatches && i < job_.prefix; ++i) {
      prefixMatches = manifests_[current_].runs[i] == src.runs[i];
    }
    if (!prefixMatches) {
      endCompaction(false);
      return CompactStep::Superseded;
    }
    PassRun& merged = runs_[job_.out];
    merged.committed = true;
    if (merged.midiCount == 0) merged.segCount = 0;
    const RevisionManifest& cur = manifests_[current_];
    RevisionManifest& m = manifests_[job_.dst];
    m.revision = cur.revision;
    m.loopLengthTicks = cur.loopLengthTicks;
    m.runCount = 0;
    m.runs[m.runCount++] = job_.out;
    ++merged.refs;
    for (uint8_t i = job_.prefix; i < cur.runCount; ++i) {
      m.runs[m.runCount++] = cur.runs[i];
      ++runs_[cur.runs[i]].refs;
    }
    const ManifestId dst = job_.dst;
    endCompaction(true);
    replaceHistoryAtCursor(dst);
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
    for (uint16_t i = 0; i < r.pageCount; ++i) returnPage(r.pages[i]);
    for (uint16_t i = 0; i < r.hidePageCount; ++i) returnPage(r.hidePages[i]);
    r.pageCount = 0;
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

  void replaceHistoryAtCursor(ManifestId id) {
    ++manifests_[id].refs;
    const ManifestId old = history_[historyCursor_];
    history_[historyCursor_] = id;
    current_ = id;
    releaseManifest(old);
  }

  EventPage pages_[Cap::kPageCount];
  PageId freePages_[Cap::kPageCount];
  uint16_t freePageTop_ = 0;
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
};

}  // namespace RevisionStore
