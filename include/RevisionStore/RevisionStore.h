//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
#pragma once

// PROTOTYPE -- openspec/changes/revision-manifest-prototype (stage A). Not wired to firmware.
//
// Canonical loop content as fixed-capacity pages, immutable committed passes and tiny immutable revision
// manifests. Every table is sized at compile time; nothing here allocates after construction.
//
//   EventPage        fixed array of PassEvent rows; pages move between a free stack and pass runs by index.
//   PassRun          the content of one pass (Record, overdub, or a compacted prefix). Mutable only while its
//                    single writer appends; immutable once committed (sorted by tick, hides by noteId).
//   RevisionManifest the ordered run list that IS committed revision N. Created whole at commit, never edited.
//
// A derived window (`composeWindow`) is stamped with the manifest it read, never with a demanded revision.

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
  /// Hides every Midi row carrying `noteId` in the runs *before* this run in a manifest (overlap-resolve hook).
  HideNote = 1,
};

/// 16-byte canonical row. `seq` is the append ordinal inside the run: immutable through seal and compaction (a
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

enum class PassKind : uint8_t { Record = 0, Overdub = 1, Compacted = 2 };

enum class AdmitResult : uint8_t { Ok, DeniedNoRun, DeniedNoPage, DeniedNoManifest, DeniedManifestFull, DeniedWriterBusy };
/// A capacity denial latches: the pass is stopped, every later append returns the same reason, the committed
/// revision is untouched, and the writer may still commit the rows captured so far or abandon them (all pages return).
enum class AppendResult : uint8_t { Ok, DeniedNoPage, DeniedRunFull, DeniedNotWriting };
enum class ComposeStatus : uint8_t { Complete, OutputFull, NoManifest };
enum class CompactResult : uint8_t { Ok, NothingToCompact, DeniedNoRun, DeniedNoPage, DeniedNoManifest, DeniedWriterBusy };

/// What a derived window can prove about itself: the manifest it read and that manifest's revision.
/// `revision` is logical (compaction keeps it); `identity` is physical and never reused, unlike the `manifest` slot.
struct ComposeStamp {
  ManifestId manifest = kNoManifest;
  uint32_t identity = 0;
  uint32_t revision = 0;
  uint32_t count = 0;
  ComposeStatus status = ComposeStatus::NoManifest;
};

/// Compile-time capacities. The target sizing is an open question of the investigation (design.md D7).
template <uint16_t PageCount, uint16_t EventsPerPage, uint16_t RunCount, uint16_t PagesPerRun, uint16_t ManifestCount,
          uint8_t RunsPerManifest, uint8_t HistoryDepth>
struct Capacity {
  static constexpr uint16_t kPageCount = PageCount;
  static constexpr uint16_t kEventsPerPage = EventsPerPage;
  static constexpr uint16_t kRunCount = RunCount;
  static constexpr uint16_t kPagesPerRun = PagesPerRun;
  static constexpr uint16_t kManifestCount = ManifestCount;
  static constexpr uint8_t kRunsPerManifest = RunsPerManifest;
  static constexpr uint8_t kHistoryDepth = HistoryDepth;
};

template <typename Cap>
class Store {
 public:
  struct EventPage {
    PassEvent rows[Cap::kEventsPerPage];
  };

  struct PassRun {
    PageId pages[Cap::kPagesPerRun];
    uint16_t pageCount = 0;
    uint32_t midiCount = 0;
    uint32_t hideCount = 0;
    uint16_t refs = 0;
    PassKind kind = PassKind::Record;
    bool committed = false;
    uint32_t rowCount() const { return midiCount + hideCount; }
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
      runs_[i] = PassRun{};
      runLive_[i] = false;
    }
    for (uint16_t i = 0; i < Cap::kManifestCount; ++i) {
      manifests_[i] = RevisionManifest{};
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
  }

  // ---- Pool accounting ---------------------------------------------------------------------------------------------
  uint16_t freePageCount() const { return freePageTop_; }
  uint16_t liveRunCount() const {
    uint16_t n = 0;
    for (uint16_t i = 0; i < Cap::kRunCount; ++i) n += runLive_[i] ? 1 : 0;
    return n;
  }
  /// Pages held by live runs. Conservation: `ownedPageCount() + freePageCount() == kPageCount`.
  uint32_t ownedPageCount() const {
    uint32_t n = 0;
    for (uint16_t i = 0; i < Cap::kRunCount; ++i) n += runLive_[i] ? runs_[i].pageCount : 0;
    return n;
  }
  uint16_t liveManifestCount() const {
    uint16_t n = 0;
    for (uint16_t i = 0; i < Cap::kManifestCount; ++i) n += manifests_[i].live ? 1 : 0;
    return n;
  }

  // ---- Single writer -----------------------------------------------------------------------------------------------
  /// Admission is one run descriptor, one page and the manifest slot its commit will need. Commit cannot fail.
  AdmitResult beginPass(PassKind kind, uint32_t loopLengthTicks = 0) {
    if (writer_ != kNoRun) return AdmitResult::DeniedWriterBusy;
    if (kind == PassKind::Overdub && current_ == kNoManifest) return AdmitResult::DeniedNoManifest;
    if (kind == PassKind::Overdub && manifests_[current_].runCount >= Cap::kRunsPerManifest) {
      return AdmitResult::DeniedManifestFull;  // compaction owed before another pass can be admitted
    }
    const RunId run = takeRun();
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
    r = PassRun{};
    r.kind = kind;
    r.pages[0] = page;
    r.pageCount = 1;
    writer_ = run;
    writerManifest_ = manifest;
    writerStop_ = AppendResult::Ok;
    if (kind == PassKind::Record) loopLengthTicks_ = loopLengthTicks;
    return AdmitResult::Ok;
  }

  AppendResult appendMidi(uint32_t tick, uint8_t status, uint8_t data1, uint8_t data2, uint32_t noteId) {
    PassEvent e;
    e.tick = tick;
    e.status = status;
    e.data1 = data1;
    e.data2 = data2;
    e.noteId = noteId;
    e.kind = PassEventKind::Midi;
    return appendRow(e);
  }

  AppendResult appendHide(uint32_t noteId) {
    PassEvent e;
    e.noteId = noteId;
    e.kind = PassEventKind::HideNote;
    return appendRow(e);
  }

  bool writing() const { return writer_ != kNoRun; }
  /// `Ok` while the writer can append; otherwise the latched capacity reason that stopped it.
  AppendResult writerStop() const { return writerStop_; }

  /// Seal the writer's run and make the next revision current. The previous manifest stays intact for its readers.
  ManifestId commitPass() {
    if (writer_ == kNoRun) return kNoManifest;
    PassRun& r = runs_[writer_];
    sealRun(r);
    r.committed = true;
    RevisionManifest& m = manifests_[writerManifest_];
    m.runCount = 0;
    uint32_t parentRevision = 0;
    if (r.kind == PassKind::Overdub && current_ != kNoManifest) {
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

  /// Compose `[fromTick, toTick)` of `id` into `out`, ordered by (tick, equal-tick class, run order, seq).
  /// Hidden notes are dropped.
  /// Cost is bounded by the runs in the manifest and the rows inside the interval, not by the loop length.
  ComposeStamp composeWindow(ManifestId id, uint32_t fromTick, uint32_t toTick, PassEvent* out, uint32_t capacity) const {
    ComposeStamp stamp;
    if (id == kNoManifest || !manifests_[id].live) return stamp;
    const RevisionManifest& m = manifests_[id];
    stamp.manifest = id;
    stamp.identity = m.identity;
    stamp.revision = m.revision;
    stamp.status = ComposeStatus::Complete;
    uint32_t cursor[Cap::kRunsPerManifest];
    for (uint8_t i = 0; i < m.runCount; ++i) {
      cursor[i] = lowerBoundTick(runs_[m.runs[i]], fromTick);
    }
    for (;;) {
      uint8_t best = 0xFF;
      const PassEvent* bestRow = nullptr;
      for (uint8_t i = 0; i < m.runCount; ++i) {
        const PassRun& r = runs_[m.runs[i]];
        while (cursor[i] < r.midiCount) {
          const PassEvent& row = rowAt(r, cursor[i]);
          if (row.tick >= toTick) {
            cursor[i] = r.midiCount;
            break;
          }
          if (!hiddenByLaterRun(m, i, row.noteId)) break;
          ++cursor[i];
        }
        if (cursor[i] >= r.midiCount) continue;
        const PassEvent& row = rowAt(r, cursor[i]);
        if (bestRow == nullptr || tickClassLess(row, *bestRow)) {  // full tie: the earlier run wins (run order)
          best = i;
          bestRow = &row;
        }
      }
      if (bestRow == nullptr) break;
      if (stamp.count >= capacity) {
        stamp.status = ComposeStatus::OutputFull;
        break;
      }
      out[stamp.count++] = *bestRow;
      ++cursor[best];
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

  // ---- Compaction: same revision, fewer runs -----------------------------------------------------------------------
  /// Merge the first `runCount` runs of the current manifest into one run and select a manifest with the *same*
  /// revision. Off-clock maintenance: playback keeps whichever manifest it pinned. Prefix-only, so every hide inside
  /// the prefix targets rows inside it and can be applied and dropped.
  CompactResult compactCurrentPrefix(uint8_t runCount) {
    if (writer_ != kNoRun) return CompactResult::DeniedWriterBusy;
    if (current_ == kNoManifest) return CompactResult::NothingToCompact;
    const RevisionManifest& src = manifests_[current_];
    if (runCount < 2 || runCount > src.runCount) return CompactResult::NothingToCompact;
    const RunId out = takeRun();
    if (out == kNoRun) return CompactResult::DeniedNoRun;
    const ManifestId dst = takeManifest();
    if (dst == kNoManifest) {
      runLive_[out] = false;
      return CompactResult::DeniedNoManifest;
    }
    PassRun& merged = runs_[out];
    merged = PassRun{};
    merged.kind = PassKind::Compacted;
    uint32_t cursor[Cap::kRunsPerManifest] = {};
    uint32_t seq = 0;
    for (;;) {
      uint8_t best = 0xFF;
      const PassEvent* bestRow = nullptr;
      for (uint8_t i = 0; i < runCount; ++i) {
        const PassRun& r = runs_[src.runs[i]];
        while (cursor[i] < r.midiCount && hiddenWithinPrefix(src, i, runCount, rowAt(r, cursor[i]).noteId)) {
          ++cursor[i];
        }
        if (cursor[i] >= r.midiCount) continue;
        const PassEvent& row = rowAt(r, cursor[i]);
        if (bestRow == nullptr || tickClassLess(row, *bestRow)) {
          best = i;
          bestRow = &row;
        }
      }
      if (bestRow == nullptr) break;
      PassEvent copy = *bestRow;
      copy.seq = seq++;
      if (!pushRow(merged, copy)) {
        releaseRunPages(merged);
        runLive_[out] = false;
        manifests_[dst].live = false;
        return CompactResult::DeniedNoPage;
      }
      ++merged.midiCount;
      ++cursor[best];
    }
    merged.committed = true;
    RevisionManifest& m = manifests_[dst];
    m.revision = src.revision;
    m.loopLengthTicks = src.loopLengthTicks;
    m.runCount = 0;
    m.runs[m.runCount++] = out;
    ++merged.refs;
    for (uint8_t i = runCount; i < src.runCount; ++i) {
      m.runs[m.runCount++] = src.runs[i];
      ++runs_[src.runs[i]].refs;
    }
    replaceHistoryAtCursor(dst);
    return CompactResult::Ok;
  }

 private:
  static PassEvent& rowRef(EventPage* pages, const PassRun& r, uint32_t index) {
    return pages[r.pages[index / Cap::kEventsPerPage]].rows[index % Cap::kEventsPerPage];
  }
  const PassEvent& rowAt(const PassRun& r, uint32_t index) const {
    return pages_[r.pages[index / Cap::kEventsPerPage]].rows[index % Cap::kEventsPerPage];
  }

  PageId takePage() { return freePageTop_ == 0 ? kNoPage : freePages_[--freePageTop_]; }
  void returnPage(PageId id) { freePages_[freePageTop_++] = id; }

  RunId takeRun() {
    for (uint16_t i = 0; i < Cap::kRunCount; ++i) {
      if (!runLive_[i]) {
        runLive_[i] = true;
        runs_[i] = PassRun{};
        return static_cast<RunId>(i);
      }
    }
    return kNoRun;
  }

  ManifestId takeManifest() {
    for (uint16_t i = 0; i < Cap::kManifestCount; ++i) {
      if (!manifests_[i].live) {
        manifests_[i] = RevisionManifest{};
        manifests_[i].live = true;
        manifests_[i].identity = nextIdentity_++;
        return static_cast<ManifestId>(i);
      }
    }
    return kNoManifest;
  }

  bool pushRow(PassRun& r, PassEvent row) {
    const uint32_t index = r.rowCount();
    if (index >= static_cast<uint32_t>(r.pageCount) * Cap::kEventsPerPage) {
      if (r.pageCount >= Cap::kPagesPerRun) return false;
      const PageId page = takePage();
      if (page == kNoPage) return false;
      r.pages[r.pageCount++] = page;
    }
    rowRef(pages_, r, index) = row;
    return true;
  }

  AppendResult appendRow(PassEvent row) {
    if (writer_ == kNoRun) return AppendResult::DeniedNotWriting;
    if (writerStop_ != AppendResult::Ok) return writerStop_;
    PassRun& r = runs_[writer_];
    const uint32_t index = r.rowCount();
    if (index >= static_cast<uint32_t>(r.pageCount) * Cap::kEventsPerPage && r.pageCount >= Cap::kPagesPerRun) {
      return writerStop_ = AppendResult::DeniedRunFull;
    }
    row.seq = index;
    if (!pushRow(r, row)) return writerStop_ = AppendResult::DeniedNoPage;
    if (row.kind == PassEventKind::Midi) {
      ++r.midiCount;
    } else {
      ++r.hideCount;
    }
    return AppendResult::Ok;
  }

  // Seal: Midi rows first sorted by (tick, class, seq), then HideNote rows by noteId. In place, no allocation.
  static bool midiBefore(const PassEvent& a, const PassEvent& b) {
    if (a.kind != b.kind) return a.kind == PassEventKind::Midi;
    if (a.kind == PassEventKind::Midi) {
      if (tickClassLess(a, b)) return true;
      if (tickClassLess(b, a)) return false;
      return a.seq < b.seq;
    }
    return a.noteId < b.noteId;
  }

  void sealRun(PassRun& r) {
    const uint32_t n = r.rowCount();
    // Heapsort over the virtual row index; the comparator also partitions Midi before HideNote.
    for (uint32_t i = n / 2; i-- > 0;) siftDown(r, i, n);
    for (uint32_t end = n; end-- > 1;) {
      swapRows(r, 0, end);
      siftDown(r, 0, end);
    }
  }
  void swapRows(PassRun& r, uint32_t a, uint32_t b) {
    PassEvent t = rowRef(pages_, r, a);
    rowRef(pages_, r, a) = rowRef(pages_, r, b);
    rowRef(pages_, r, b) = t;
  }
  void siftDown(PassRun& r, uint32_t root, uint32_t n) {
    for (;;) {
      uint32_t child = 2 * root + 1;
      if (child >= n) return;
      if (child + 1 < n && midiBefore(rowAt(r, child), rowAt(r, child + 1))) ++child;
      if (!midiBefore(rowAt(r, root), rowAt(r, child))) return;
      swapRows(r, root, child);
      root = child;
    }
  }

  uint32_t lowerBoundTick(const PassRun& r, uint32_t tick) const {
    uint32_t lo = 0;
    uint32_t hi = r.midiCount;
    while (lo < hi) {
      const uint32_t mid = lo + (hi - lo) / 2;
      if (rowAt(r, mid).tick < tick) {
        lo = mid + 1;
      } else {
        hi = mid;
      }
    }
    return lo;
  }

  bool runHides(const PassRun& r, uint32_t noteId) const {
    uint32_t lo = r.midiCount;
    uint32_t hi = r.rowCount();
    while (lo < hi) {
      const uint32_t mid = lo + (hi - lo) / 2;
      const uint32_t v = rowAt(r, mid).noteId;
      if (v == noteId) return true;
      if (v < noteId) {
        lo = mid + 1;
      } else {
        hi = mid;
      }
    }
    return false;
  }

  bool hiddenByLaterRun(const RevisionManifest& m, uint8_t runIndex, uint32_t noteId) const {
    if (noteId == 0) return false;
    for (uint8_t j = runIndex + 1; j < m.runCount; ++j) {
      if (runs_[m.runs[j]].hideCount != 0 && runHides(runs_[m.runs[j]], noteId)) return true;
    }
    return false;
  }

  bool hiddenWithinPrefix(const RevisionManifest& m, uint8_t runIndex, uint8_t prefix, uint32_t noteId) const {
    if (noteId == 0) return false;
    for (uint8_t j = runIndex + 1; j < prefix; ++j) {
      if (runs_[m.runs[j]].hideCount != 0 && runHides(runs_[m.runs[j]], noteId)) return true;
    }
    return false;
  }

  void releaseRunPages(PassRun& r) {
    for (uint16_t i = 0; i < r.pageCount; ++i) returnPage(r.pages[i]);
    r.pageCount = 0;
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
};

}  // namespace RevisionStore
