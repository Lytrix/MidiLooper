//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
#pragma once

// Stage C (DEC-246): import a Loop's committed passes into a disposable RevisionStore manifest, as a bounded,
// transactional candidate (C3b).
//
// Production keeps every write (capture, commit, undo, persistence); this only *reads* committed state after it is
// authoritative. The candidate is built privately (staged runs, no manifest) in bounded steps against a frozen source
// key, revalidated, then published atomically; readers keep the previously published manifest until then, and a key
// that moves cancels the candidate (`SourceMoved`) -- it never continues against mixed production state.
//
// Layout of the imported manifest:
//   - one run per active capture pass in commit order (Record, then overdubs by `mergeSequence`), holding that pass's
//     committed chunk rows (DEC-245 layer 1), streamed one sealed event at a time
//     (`LoopEventStore::readSealedChunkEvent`); after a pass is sealed its NoteOff rows are stamped with their note's
//     id by `LanePairer`, production's own attribution rule (EditApply `findNoteOffForOnIndex`): a tick-ordered walk
//     per (channel, pitch) lane, Off before On at an equal tick, LIFO, then each still-open NoteOn takes the unique
//     unpaired earlier NoteOff (the wrap). Append-order pairing diverged from it across the wrap (C4a, device);
//   - ONE Edit run for all active Note edit passes: per target a hide plus its final geometry after production's own
//     `applyNoteEditPass` of that target's rows in edit-pass id order. Edit semantics are never re-implemented here.
//     This collapsed Edit run is a Stage-C shadow-import convenience, valid for parity only: it discards per-edit
//     provenance and order, so it is NOT a candidate long-term representation of edits (stage D/E keeps one immutable
//     edit run per edit, DEC-245).
// Source: committed passes only. A loop whose content exists only as a derived snapshot/root (a boot-restored slot)
// is `NoCommittedSource` -- the import never reconstructs layer 1 from a cache (DEC-245).
// Every step is bounded: no whole-pass copy, no whole-pass pairing, no per-edit scan of the content. The one
// allocation left is production's `applyNoteEditPass` working on one target's few events.

#include <algorithm>
#include <cstdint>

#include "EditApply.h"
#include "EditPass.h"
#include "Loop.h"
#include "LoopEventStore.h"
#include "LoopPasses.h"
#include "MidiEvent.h"
#include "RevisionStore/RevisionStore.h"

namespace RevisionStore {

enum class ImportResult : uint8_t {
  Ok,
  NoCommittedSource,    // no active committed Record pass with rows (e.g. a restored slot that only has a snapshot)
  DeniedCapacity,       // the candidate does not fit the store (checked before anything is written)
  UnsupportedEditPass,  // an active edit pass the importer does not express (ControlChange / Audio)
  SourceMoved,          // the frozen source key changed while the candidate was building: cancelled
  DeniedNoManifest,     // no manifest slot to publish into: cancelled
};

/// Step outcome of an import job.
enum class ImportStep : uint8_t { Idle, Pending, Published, Refused };

struct ImportStats {
  uint32_t captureRuns = 0;
  uint32_t editRuns = 0;
  uint32_t rows = 0;
  uint32_t hides = 0;
  uint32_t unpairedNoteOffs = 0;  // NoteOff rows the pairing did not attribute to a note (kept, id 0)
  uint32_t steps = 0;
};

/// The frozen identity of the production source a candidate is built from.
struct ImportSourceKey {
  LoopId loopId = kInvalidLoopId;
  uint32_t playbackRevision = 0;
  uint32_t committedPassRevision = 0;
  uint32_t inventory = 0;  // FNV-1a over every pass's (id, state, chunk count) and every edit pass's (id, state)
  bool operator==(const ImportSourceKey& o) const {
    return loopId == o.loopId && playbackRevision == o.playbackRevision &&
           committedPassRevision == o.committedPassRevision && inventory == o.inventory;
  }
  bool operator!=(const ImportSourceKey& o) const { return !(*this == o); }
};

namespace detail {

inline uint8_t statusOf(const MidiEvent& e) {
  return static_cast<uint8_t>(static_cast<uint8_t>(e.type) | ((e.channel - 1) & 0x0F));
}
inline uint8_t eventClass(const MidiEvent& e) { return e.isNoteOff() ? 0 : (e.isNoteOn() ? 1 : 2); }
inline void fnv(uint32_t& h, uint32_t v) {
  for (int i = 0; i < 4; ++i) {
    h ^= (v >> (8 * i)) & 0xFF;
    h *= 16777619u;
  }
}

}  // namespace detail

/// Revalidation contract (DEC-246, owner 2026-11-01): the key is computed from bounded **metadata** only -- each
/// pass's id, state and chunk *count*, each edit pass's id and state, and three scalars. It never reads an event row
/// or hashes content, so its cost is O(capture passes + edit passes), part of every import step's <= 2 ms contract.
/// A future field must keep that property (a generation stamp, not a content hash).
inline ImportSourceKey importSourceKeyOf(const Loop& loop) {
  ImportSourceKey key;
  key.loopId = loop.loopId;
  key.playbackRevision = loop.playbackRevision;
  key.committedPassRevision = loop.committedPassRevision();
  uint32_t h = 2166136261u;
  const LoopPasses& passes = loop.passes;
  if (passes.hasRecordPass()) {
    detail::fnv(h, passes.recordPass.id);
    detail::fnv(h, static_cast<uint32_t>(passes.recordPass.state));
    detail::fnv(h, static_cast<uint32_t>(passes.recordPass.committedChunkIds.size()));
  }
  for (const OverdubPass& pass : passes.overdubPasses) {
    detail::fnv(h, pass.id);
    detail::fnv(h, static_cast<uint32_t>(pass.state));
    detail::fnv(h, static_cast<uint32_t>(pass.committedChunkIds.size()));
  }
  for (const EditPass& edit : passes.editPasses) {
    detail::fnv(h, edit.id);
    detail::fnv(h, static_cast<uint32_t>(edit.state));
  }
  key.inventory = h;
  return key;
}

/// Production's NoteOff attribution (EditApply `findNoteOffForOnIndex`) with fixed tables. Feed a pass's note rows in
/// tick order with NoteOffs before NoteOns at an equal tick (DEC-244 store order); then call `wrapStep` until it returns
/// false. Not modelled: `findWrapHeadOffForOnIndex`'s extra preferred-wrap-tail removal on Delete (named limitation).
template <uint16_t MaxOpen, uint16_t MaxUnpaired>
class LanePairer {
 public:
  struct Stats {
    uint32_t openOverflow = 0;      // a NoteOn dropped from the open table (its NoteOff stays unpaired)
    uint32_t unpairedOverflow = 0;  // an unpaired NoteOff not recorded for the wrap step
    uint32_t contestedWraps = 0;    // an open NoteOn whose unique wrap NoteOff another NoteOn already took
    uint32_t unpaired = 0;          // NoteOffs left without a note after the wrap step
  };
  void reset() {
    openCount_ = unpairedCount_ = 0;
    wrapCursor_ = 0;
    stats_ = Stats{};
  }
  void onNoteOn(uint8_t channel, uint8_t pitch, uint32_t tick, uint32_t noteId) {
    if (openCount_ == MaxOpen) {
      for (uint16_t i = 1; i < openCount_; ++i) open_[i - 1] = open_[i];
      --openCount_;
      ++stats_.openOverflow;
    }
    open_[openCount_++] = {channel, pitch, tick, noteId};
  }
  /// The noteId this NoteOff belongs to, or 0 (then it is remembered for the wrap step under `index`).
  uint32_t onNoteOff(uint8_t channel, uint8_t pitch, uint32_t tick, uint32_t index) {
    for (uint16_t i = openCount_; i-- > 0;) {
      if (open_[i].channel == channel && open_[i].pitch == pitch) {
        const uint32_t id = open_[i].noteId;
        for (uint16_t k = i + 1; k < openCount_; ++k) open_[k - 1] = open_[k];
        --openCount_;
        return id;
      }
    }
    if (unpairedCount_ < MaxUnpaired) {
      unpaired_[unpairedCount_++] = {channel, pitch, tick, index, false};
    } else {
      ++stats_.unpairedOverflow;
    }
    return 0;
  }
  /// Resolve one still-open NoteOn: `stamp(offIndex, noteId)` when exactly one unpaired NoteOff of its lane lies
  /// earlier in ticks. Returns false when every open NoteOn has been considered.
  template <typename Stamp>
  bool wrapStep(Stamp&& stamp) {
    if (wrapCursor_ >= openCount_) {
      if (wrapCursor_ == openCount_) {
        for (uint16_t u = 0; u < unpairedCount_; ++u) stats_.unpaired += unpaired_[u].used ? 0 : 1;
        stats_.unpaired += stats_.unpairedOverflow;
        ++wrapCursor_;
      }
      return false;
    }
    const Open& on = open_[wrapCursor_++];
    uint16_t candidate = 0xFFFF;
    uint16_t count = 0;
    bool contested = false;
    for (uint16_t u = 0; u < unpairedCount_; ++u) {
      const Unpaired& off = unpaired_[u];
      if (off.channel != on.channel || off.pitch != on.pitch || off.tick >= on.tick) continue;
      if (off.used) {
        contested = true;
        continue;
      }
      candidate = u;
      ++count;
    }
    if (count == 1) {
      unpaired_[candidate].used = true;
      stamp(unpaired_[candidate].index, on.noteId);
    } else if (count == 0 && contested) {
      ++stats_.contestedWraps;
    }
    return true;
  }
  const Stats& stats() const { return stats_; }

 private:
  struct Open {
    uint8_t channel;
    uint8_t pitch;
    uint32_t tick;
    uint32_t noteId;
  };
  struct Unpaired {
    uint8_t channel;
    uint8_t pitch;
    uint32_t tick;
    uint32_t index;
    bool used;
  };
  Open open_[MaxOpen];
  Unpaired unpaired_[MaxUnpaired];
  uint16_t openCount_ = 0;
  uint16_t unpairedCount_ = 0;
  uint16_t wrapCursor_ = 0;
  Stats stats_;
};

/// A bounded, transactional import of one Loop into `StoreCore<Cap>`. Fixed-capacity state; place it with the store.
template <typename Cap, uint16_t MaxEditTargets = 256, uint16_t MaxEdits = 512, uint16_t MaxOpenNotes = 128>
class ImportJob {
 public:
  static constexpr uint8_t kEventsPerTarget = 4;

  bool active() const { return phase_ != Phase::Idle; }
  /// Diagnostic: the phase the next `step` starts in (0 = idle; 1..7 = Captures .. Publish).
  uint8_t phaseCode() const { return static_cast<uint8_t>(phase_); }
  ImportResult result() const { return result_; }
  const ImportStats& stats() const { return stats_; }
  const ImportSourceKey& key() const { return key_; }

  /// Freeze the source key, plan the passes and edit targets, and check capacity before anything is written.
  /// `Pending` when the candidate is under way; `Refused` with `result()` naming why otherwise (store untouched).
  ImportStep begin(const Loop& loop, StoreCore<Cap>& store) {
    cancel();
    loop_ = &loop;
    store_ = &store;
    stats_ = ImportStats{};
    key_ = importSourceKeyOf(loop);
    const LoopPasses& passes = loop.passes;
    if (!passes.hasRecordPass() || passes.recordPass.state != CapturePassState::Active ||
        passes.recordPass.committedChunkIds.empty()) {
      return refuse(ImportResult::NoCommittedSource);
    }
    // Capture passes in commit order.
    passCount_ = 0;
    lists_[passCount_++] = &passes.recordPass.committedChunkIds;
    const OverdubPass* overdubs[Cap::kRunsPerManifest];
    uint8_t overdubCount = 0;
    for (const OverdubPass& pass : passes.overdubPasses) {
      if (pass.state != CapturePassState::Active) continue;
      if (overdubCount + 2 > Cap::kRunsPerManifest) return refuse(ImportResult::DeniedCapacity);
      overdubs[overdubCount++] = &pass;
    }
    std::sort(overdubs, overdubs + overdubCount,
              [](const OverdubPass* a, const OverdubPass* b) { return a->mergeSequence < b->mergeSequence; });
    for (uint8_t i = 0; i < overdubCount; ++i) lists_[passCount_++] = &overdubs[i]->committedChunkIds;
    // Edit passes in id order, and their distinct targets.
    editCount_ = 0;
    targetCount_ = 0;
    for (const EditPass& edit : passes.editPasses) {
      if (edit.state != EditPassState::Active) continue;
      if (edit.passType != EditPassType::Note) return refuse(ImportResult::UnsupportedEditPass);
      if (editCount_ >= MaxEdits) return refuse(ImportResult::DeniedCapacity);
      edits_[editCount_++] = &edit;
    }
    std::sort(edits_, edits_ + editCount_, [](const EditPass* a, const EditPass* b) { return a->id < b->id; });
    for (uint16_t e = 0; e < editCount_; ++e) {
      const NoteId target = edits_[e]->targetNoteId;
      if (target == kInvalidNoteId || findTarget(target) != kNoTarget) continue;
      if (targetCount_ >= MaxEditTargets) return refuse(ImportResult::DeniedCapacity);
      insertTarget(target);
    }
    for (uint16_t i = 0; i < targetCount_; ++i) targetEventCount_[i] = 0;
    // Capacity, conservatively, before anything is written.
    uint32_t pages = 0;
    for (uint8_t p = 0; p < passCount_; ++p) {
      uint32_t rows = 0;
      for (const uint16_t chunk : *lists_[p]) rows += LoopEventStore::chunkUsedCount(chunk);
      pages += runPagesFor(rows);
    }
    if (targetCount_ != 0) {
      pages += runPagesFor(static_cast<uint32_t>(targetCount_) * kEventsPerTarget) + runPagesFor(targetCount_);
    }
    const uint16_t runsNeeded = static_cast<uint16_t>(passCount_ + (editCount_ != 0 ? 1 : 0));
    if (pages > store.freePageCount() || runsNeeded > store.freeRunCount() || store.freeManifestCount() == 0) {
      return refuse(ImportResult::DeniedCapacity);
    }
    // The candidate starts.
    store.cancelStaged();
    pass_ = 0;
    chunk_ = 0;
    index_ = 0;
    editCursor_ = 0;
    emitCursor_ = 0;
    phase_ = Phase::Captures;
    return ImportStep::Pending;
  }

  /// One bounded step: at most `maxRows` rows read or written (an edit counts as `kEventsPerTarget`). Revalidates the
  /// frozen source key first; a moved key cancels the candidate (`SourceMoved`).
  ImportStep step(uint32_t maxRows) {
    if (phase_ == Phase::Idle) return ImportStep::Idle;
    ++stats_.steps;
    if (importSourceKeyOf(*loop_) != key_) return fail(ImportResult::SourceMoved);
    uint32_t budget = maxRows;
    while (budget != 0) {
      switch (phase_) {
        case Phase::Captures:
          if (!stepCaptures(budget)) return fail(ImportResult::DeniedCapacity);
          break;
        case Phase::PairWalk:
          if (!stepPairWalk(budget)) return fail(ImportResult::DeniedCapacity);
          break;
        case Phase::PairWrap:
          if (!stepPairWrap(budget)) return fail(ImportResult::DeniedCapacity);
          break;
        case Phase::EditApply:
          if (!stepEditApply(budget)) return fail(ImportResult::DeniedCapacity);
          break;
        case Phase::EditSort:
          sortReplacements();
          phase_ = Phase::EditEmit;
          budget = budget > replacementCount_ ? budget - replacementCount_ : 0;
          break;
        case Phase::EditEmit:
          if (!stepEditEmit(budget)) return fail(ImportResult::DeniedCapacity);
          break;
        case Phase::Publish: {
          if (store_->publishStaged() == kNoManifest) return fail(ImportResult::DeniedNoManifest);
          phase_ = Phase::Idle;
          result_ = ImportResult::Ok;
          return ImportStep::Published;
        }
        case Phase::Idle:
          return ImportStep::Idle;
      }
    }
    return ImportStep::Pending;
  }

  /// Explicit cancellation: the staged candidate is dropped, the published manifest is untouched.
  void cancel() {
    if (phase_ != Phase::Idle && store_ != nullptr) store_->cancelStaged();
    phase_ = Phase::Idle;
  }

 private:
  enum class Phase : uint8_t { Idle, Captures, PairWalk, PairWrap, EditApply, EditSort, EditEmit, Publish };
  using Pairer = LanePairer<MaxOpenNotes, MaxOpenNotes * 2>;
  static constexpr uint16_t kNoTarget = 0xFFFF;

  static uint32_t runPagesFor(uint32_t rows) {
    const uint32_t data = (rows + Cap::kEventsPerPage - 1) / Cap::kEventsPerPage + 1;
    return data + 1;  // + one directory page of margin
  }

  ImportStep refuse(ImportResult why) {
    result_ = why;
    phase_ = Phase::Idle;
    return ImportStep::Refused;
  }
  ImportStep fail(ImportResult why) {
    store_->cancelStaged();
    return refuse(why);
  }

  uint16_t findTarget(NoteId id) const {
    uint16_t lo = 0, hi = targetCount_;
    while (lo < hi) {
      const uint16_t mid = static_cast<uint16_t>((lo + hi) / 2);
      if (targets_[mid] == id) return mid;
      if (targets_[mid] < id) {
        lo = static_cast<uint16_t>(mid + 1);
      } else {
        hi = mid;
      }
    }
    return kNoTarget;
  }
  void insertTarget(NoteId id) {
    uint16_t at = targetCount_;
    while (at > 0 && targets_[at - 1] > id) {
      targets_[at] = targets_[at - 1];
      --at;
    }
    targets_[at] = id;
    ++targetCount_;
  }

  static MidiEvent toMidi(const PassEvent& p) {
    MidiEvent e{};
    e.tick = p.tick;
    e.type = static_cast<midi::MidiType>(p.status & 0xF0);
    e.channel = static_cast<uint8_t>((p.status & 0x0F) + 1);
    e.data.noteData.note = p.data1;
    e.data.noteData.velocity = p.data2;
    e.noteId = p.noteId;
    return e;
  }

  // An edit target's row, once its id is final (after pairing).
  bool collectTarget(const PassEvent& row) {
    if (targetCount_ == 0 || row.noteId == kInvalidNoteId) return true;
    const uint16_t t = findTarget(row.noteId);
    if (t == kNoTarget) return true;
    if (targetEventCount_[t] >= kEventsPerTarget) return false;
    targetEvents_[t][targetEventCount_[t]++] = toMidi(row);
    return true;
  }

  bool appendRow(const MidiEvent& e) {
    return store_->appendMidi(e.tick, detail::statusOf(e), e.data.noteData.note, e.data.noteData.velocity,
                              e.noteId) == AppendResult::Ok;
  }

  bool stepCaptures(uint32_t& budget) {
    while (budget != 0) {
      if (pass_ >= passCount_) {
        phase_ = editCount_ != 0 ? Phase::EditApply : Phase::Publish;
        return true;
      }
      if (!store_->stagedWriterOpen()) {
        if (store_->beginStagedRun(pass_ == 0 ? PassKind::Record : PassKind::Overdub, loop_->loopLengthTicks) !=
            AdmitResult::Ok) {
          return false;
        }
      }
      const CommittedChunkIdList& chunks = *lists_[pass_];
      if (chunk_ >= chunks.size()) {
        const SealStep sealed = store_->stepSealStagedRun(budget);  // bounded: an over-budget run sorts in steps
        if (sealed == SealStep::Failed) return false;
        if (sealed == SealStep::Pending) return true;
        ++stats_.captureRuns;
        // Attribute this pass's NoteOffs the production way, walking the sealed run in tick order.
        pairRun_ = store_->stagedRun(static_cast<uint8_t>(store_->stagedRunCount() - 1));
        const auto& run = store_->run(pairRun_);
        pairSegments_ = run.segCount;
        for (uint8_t sgm = 0; sgm < pairSegments_; ++sgm) {
          pairAt_[sgm] = run.segStart[sgm];
          pairEnd_[sgm] = run.segEnd(sgm);
        }
        pairer_.reset();
        phase_ = Phase::PairWalk;
        return true;
      }
      MidiEvent e;
      bool atEnd = false;
      if (!LoopEventStore::readSealedChunkEvent(chunks[chunk_], index_, e, atEnd) || atEnd) {
        ++chunk_;
        index_ = 0;
        continue;
      }
      ++index_;
      --budget;
      if (e.isNoteOff()) e.noteId = kInvalidNoteId;  // attribution is the pair phase's job (production rule)
      if (!appendRow(e)) return false;
      ++stats_.rows;
    }
    return true;
  }

  // Tick-order walk of the sealed run (store order: tick, then NoteOff before NoteOn, then append order).
  bool stepPairWalk(uint32_t& budget) {
    while (budget != 0) {
      uint8_t best = 0xFF;
      for (uint8_t sgm = 0; sgm < pairSegments_; ++sgm) {
        if (pairAt_[sgm] >= pairEnd_[sgm]) continue;
        if (best == 0xFF) {
          best = sgm;
          continue;
        }
        const PassEvent& a = store_->stagedRow(pairRun_, pairAt_[sgm]);
        const PassEvent& b = store_->stagedRow(pairRun_, pairAt_[best]);
        if (tickClassLess(a, b) || (!tickClassLess(b, a) && a.seq < b.seq)) best = sgm;
      }
      if (best == 0xFF) {
        phase_ = Phase::PairWrap;
        return true;
      }
      const uint32_t index = pairAt_[best]++;
      --budget;
      const PassEvent row = store_->stagedRow(pairRun_, index);
      const uint8_t cls = equalTickClass(row);
      const uint8_t channel = static_cast<uint8_t>((row.status & 0x0F) + 1);
      if (cls == 1) {
        pairer_.onNoteOn(channel, row.data1, row.tick, row.noteId);
        if (!collectTarget(row)) return false;
      } else if (cls == 0) {
        const uint32_t id = pairer_.onNoteOff(channel, row.data1, row.tick, index);
        if (id != 0) {
          store_->setStagedRowNoteId(pairRun_, index, id);
          PassEvent stamped = row;
          stamped.noteId = id;
          if (!collectTarget(stamped)) return false;
        }
      }
    }
    return true;
  }

  bool stepPairWrap(uint32_t& budget) {
    bool ok = true;
    while (budget != 0) {
      --budget;
      const bool more = pairer_.wrapStep([&](uint32_t index, uint32_t id) {
        store_->setStagedRowNoteId(pairRun_, index, id);
        PassEvent stamped = store_->stagedRow(pairRun_, index);
        ok = ok && collectTarget(stamped);
      });
      if (!ok) return false;
      if (!more) {
        stats_.unpairedNoteOffs += pairer_.stats().unpaired;
        ++pass_;
        chunk_ = 0;
        index_ = 0;
        phase_ = Phase::Captures;
        return true;
      }
    }
    return true;
  }

  bool stepEditApply(uint32_t& budget) {
    while (budget != 0) {
      if (editCursor_ >= editCount_) {
        phase_ = Phase::EditSort;
        return true;
      }
      const EditPass& edit = *edits_[editCursor_++];
      budget = budget > kEventsPerTarget ? budget - kEventsPerTarget : 0;
      const uint16_t t = findTarget(edit.targetNoteId);
      if (t == kNoTarget) continue;
      MidiEventVec& events = editScratch_;  // reserved with the job (at boot on the device): no per-edit allocation
      events.assign(targetEvents_[t], targetEvents_[t] + targetEventCount_[t]);
      applyNoteEditPass(events, edit, loop_->loopLengthTicks);
      if (events.size() > kEventsPerTarget) return false;
      targetEventCount_[t] = static_cast<uint8_t>(events.size());
      for (uint8_t k = 0; k < targetEventCount_[t]; ++k) {
        targetEvents_[t][k] = events[k];
        if ((events[k].isNoteOn() || events[k].isNoteOff()) && targetEvents_[t][k].noteId == kInvalidNoteId) {
          targetEvents_[t][k].noteId = edit.targetNoteId;
        }
      }
    }
    return true;
  }

  // In-place heapsort of the replacement rows by (tick, class, target order, event order): no allocation.
  void sortReplacements() {
    replacementCount_ = 0;
    for (uint16_t t = 0; t < targetCount_; ++t) {
      for (uint8_t k = 0; k < targetEventCount_[t]; ++k) {
        replacements_[replacementCount_] = targetEvents_[t][k];
        replacementOrder_[replacementCount_] = static_cast<uint32_t>(t) * kEventsPerTarget + k;
        ++replacementCount_;
      }
    }
    auto less = [&](uint32_t a, uint32_t b) {
      const MidiEvent& x = replacements_[a];
      const MidiEvent& y = replacements_[b];
      if (x.tick != y.tick) return x.tick < y.tick;
      if (detail::eventClass(x) != detail::eventClass(y)) return detail::eventClass(x) < detail::eventClass(y);
      return replacementOrder_[a] < replacementOrder_[b];
    };
    auto swap = [&](uint32_t a, uint32_t b) {
      std::swap(replacements_[a], replacements_[b]);
      std::swap(replacementOrder_[a], replacementOrder_[b]);
    };
    auto sift = [&](uint32_t root, uint32_t n) {
      for (;;) {
        uint32_t child = 2 * root + 1;
        if (child >= n) return;
        if (child + 1 < n && less(child, child + 1)) ++child;
        if (!less(root, child)) return;
        swap(root, child);
        root = child;
      }
    };
    for (uint32_t i = replacementCount_ / 2; i-- > 0;) sift(i, replacementCount_);
    for (uint32_t end = replacementCount_; end-- > 1;) {
      swap(0, end);
      sift(0, end);
    }
  }

  bool stepEditEmit(uint32_t& budget) {
    if (!store_->stagedWriterOpen() && emitCursor_ == 0) {
      if (store_->beginStagedRun(PassKind::Edit, loop_->loopLengthTicks) != AdmitResult::Ok) return false;
    }
    while (budget != 0) {
      if (emitCursor_ < targetCount_) {
        if (store_->appendHide(targets_[emitCursor_]) != AppendResult::Ok) return false;
        ++stats_.hides;
      } else if (emitCursor_ < static_cast<uint32_t>(targetCount_) + replacementCount_) {
        if (!appendRow(replacements_[emitCursor_ - targetCount_])) return false;
        ++stats_.rows;
      } else {
        const SealStep sealed = store_->stepSealStagedRun(budget);
        if (sealed == SealStep::Failed) return false;
        if (sealed == SealStep::Pending) return true;
        ++stats_.editRuns;
        phase_ = Phase::Publish;
        return true;
      }
      ++emitCursor_;
      --budget;
    }
    return true;
  }

  Phase phase_ = Phase::Idle;
  MidiEventVec editScratch_ = [] {
    MidiEventVec v;
    v.reserve(16);
    return v;
  }();
  ImportResult result_ = ImportResult::Ok;
  ImportStats stats_;
  ImportSourceKey key_;
  const Loop* loop_ = nullptr;
  StoreCore<Cap>* store_ = nullptr;
  const CommittedChunkIdList* lists_[Cap::kRunsPerManifest];
  uint8_t passCount_ = 0;
  uint8_t pass_ = 0;
  size_t chunk_ = 0;
  size_t index_ = 0;
  Pairer pairer_;
  RunId pairRun_ = kNoRun;
  uint8_t pairSegments_ = 0;
  uint32_t pairAt_[Cap::kSegmentsPerRun];
  uint32_t pairEnd_[Cap::kSegmentsPerRun];
  const EditPass* edits_[MaxEdits];
  uint16_t editCount_ = 0;
  uint16_t editCursor_ = 0;
  NoteId targets_[MaxEditTargets];
  uint16_t targetCount_ = 0;
  MidiEvent targetEvents_[MaxEditTargets][kEventsPerTarget];
  uint8_t targetEventCount_[MaxEditTargets];
  MidiEvent replacements_[MaxEditTargets * kEventsPerTarget];
  uint32_t replacementOrder_[MaxEditTargets * kEventsPerTarget];
  uint32_t replacementCount_ = 0;
  uint32_t emitCursor_ = 0;
};

/// Whole import, run to completion through the bounded job (tests, off-line). On refusal the store's published
/// manifest is untouched and the candidate is dropped.
template <typename Cap>
ImportResult importLoop(const Loop& loop, StoreCore<Cap>& store, ImportStats* statsOut = nullptr) {
  static ImportJob<Cap> job;  // fixed-capacity state; not on the stack
  ImportStep step = job.begin(loop, store);
  while (step == ImportStep::Pending) step = job.step(256);
  if (statsOut != nullptr) *statsOut = job.stats();
  return step == ImportStep::Published ? ImportResult::Ok : job.result();
}

}  // namespace RevisionStore
