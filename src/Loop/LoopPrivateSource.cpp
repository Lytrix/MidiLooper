//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

/**
 * @file LoopPrivateSource.cpp
 * @brief CONTRACTS.md C3.1-C3.5 -- the private-predecessor source origin and its bounded cursor.
 *
 * A Record interval that has sealed its raw extent but has not published is reachable only as
 * *sealed raw input* (the Track custody receipt it owns). It is not committed content: no root, no
 * `pass`, no `Snapshot`/`Prepared` attribution, no save. Overdub overlap resolution nevertheless
 * needs it as a **source** -- a successor that plays the same pitch over it must shorten/hide the
 * predecessor's note, exactly as it would after publication.
 *
 * This file is that source, prepared under one bound:
 *
 *   * one grain admits at most `kPrivateSourceQuantum` (32) units, and one unit is exactly one
 *     sealed-range row read, one pass-local pair decision or one pending-change upsert;
 *   * the cursor never materializes a whole-take vector (it appends completed pairs to the existing
 *     full-loop span cache), never writes `passes`, a root or `playbackRevision`, and never blocks
 *     a note-on/note-off;
 *   * the identity is `(receipt, loopId, interval, frozen content generation)` and
 *     `playbackRevision` is the supersession stamp, so a predecessor publication discards the
 *     partial and the committed origin answers instead. A private prefix is never mixed with a
 *     committed suffix.
 */

#include "Loop.h"

#include "Utils/IntervalProjection.h"
#include "Utils/LoopMem.h"
#include "Utils/MemoryMonitor.h"
#include "Utils/NoteUtils.h"

namespace {

/// A NoteOn whose Off has not been read yet. `DisplayNote.endTick` is a uint32 tick, so this
/// sentinel is outside every legal loop length and cannot be confused with a real span.
constexpr uint32_t kPrivateOpenEndTick = UINT32_MAX;
/// Bounded staging increment: one reserve per grain at most, so the scan can never turn into an
/// unbounded allocation inside a maintenance turn.
constexpr size_t kPrivateSourceStageIncrement = 8;

}  // namespace

// A branch chain, not a `switch`: a switch over an enum puts its jump table in `.data`, and this
// tree's DTCM is at the align-edge snap (see `.cursor/rules/Teensy-RAM1-Bank-Overflow.mdc`).
LOOP_COLD_MEM const char* Loop::sourceOriginName(SourceOrigin origin) {
  if (origin == SourceOrigin::Snapshot) return "snapshot";
  if (origin == SourceOrigin::Prepared) return "prepared";
  if (origin == SourceOrigin::Prior) return "prior";
  if (origin == SourceOrigin::Visual) return "visual";
  if (origin == SourceOrigin::Passes) return "passes";
  if (origin == SourceOrigin::PrivatePredecessor) return "private_predecessor";
  return "none";
}

LOOP_COLD_MEM const char* Loop::privateSourceDenyName(PrivateSourceDeny deny) {
  if (deny == PrivateSourceDeny::IdentityChanged) return "identity_changed";
  if (deny == PrivateSourceDeny::SourceUnavailable) return "source_unavailable";
  if (deny == PrivateSourceDeny::PairCapacity) return "pair_capacity";
  if (deny == PrivateSourceDeny::OutputUnavailable) return "output_unavailable";
  if (deny == PrivateSourceDeny::HoldCapacity) return "hold_capacity";
  return "none";
}

LOOP_COLD_MEM bool Loop::privatePredecessorSourcePending() const {
  return privateSource_.phase == kPrivateRows || privateSource_.phase == kPrivateHolds;
}

LOOP_COLD_MEM bool Loop::privatePredecessorSourceComplete() const {
  return privateSource_.phase == kPrivateComplete;
}

/// C3.2: one charged, bounded increment on either store, gated at its point of use. A refusal is
/// retryable `Pressure`: no row is consumed, so the cursor's position is exact and every append the
/// grain then makes is already paid for -- nothing grows uncharged, and a refused reserve can never
/// turn into an unchecked allocation deeper in the grain.
LOOP_COLD_MEM bool Loop::ensureNoteCapacity(NoteUtils::DisplayNoteVec& notes, size_t needed) {
  if (notes.capacity() - notes.size() >= needed) {
    return true;
  }
  const size_t grow = needed > kPrivateSourceStageIncrement ? needed : kPrivateSourceStageIncrement;
  const size_t bytes = sizeof(NoteUtils::DisplayNote) * grow;
  if (!MemoryMonitor::admitOperationMemory(MemoryMonitor::OperationKind::SourceViewReserve,
                                           bytes)) {
    return false;
  }
  notes.reserve(notes.size() + grow);
  return notes.capacity() - notes.size() >= needed;
}

LOOP_COLD_MEM bool Loop::ensurePrivateStageCapacity() {
  return ensureNoteCapacity(overdubSourceSpanCacheNotes_, 1);
}

LOOP_COLD_MEM bool Loop::ensurePrivateViewCapacity(size_t needed) {
  return ensureNoteCapacity(overdubSourceViewNotes_, needed == 0 ? 1 : needed);
}

/// One row may stage one pair and publish one view row; both slots are proven before the read.
LOOP_COLD_MEM bool Loop::ensurePrivateSourceCapacity() {
  return ensurePrivateStageCapacity() && ensurePrivateViewCapacity(1);
}

LOOP_COLD_MEM void Loop::notePrivateSourceDenied(PrivateSourceDeny deny) {
  privateSource_.deny = deny;
}

/// Drop the scan (cursor and every row it staged) but keep the retained hold intents: a
/// supersession replaces the *source*, not the decision the source was needed for.
LOOP_COLD_MEM void Loop::discardPrivateSourceScan() {
  const bool privateCache = overdubSourceSpanCacheOrigin_ == SourceOrigin::PrivatePredecessor;
  privateSource_ = PrivateSourceCursorState{};
  privateOpenPairCount_ = 0;
  privateHoldTargets_.clear();
  privateHoldTargetCursor_ = 0;
  privateHoldMaterializing_ = false;
  if (!privateCache) {
    return;
  }
  // The cache *was* this scan's output. Drop it by identity: a committed origin must re-derive
  // from its own root, and the published stamps stay unset so nothing can read it as committed.
  overdubSourceSpanCacheNotes_.clear();
  overdubSourceSpanCacheOrigin_ = SourceOrigin::None;
  overdubSourceSpanCacheLoopLengthTicks_ = 0;
  overdubSourceSpanCachePlaybackRevision_ = 0;
  overdubSourceSpanCachePublishedRevision_ = 0;
  overdubSourceSpanCachePublishedIdentity_ = nullptr;
  overdubSourceSpanCacheValid_ = false;
  overdubSourceSpanCacheFullLoop_ = false;
}

/// C3.5: drop the cursor, every row it staged and every retained intent. Called on clear, slot
/// switch, load, transport stop and record re-entry -- the session is not the one that owed them.
LOOP_COLD_MEM void Loop::discardPrivatePredecessorSource() {
  discardPrivateSourceScan();
  privateHoldCount_ = 0;
  privateSourceSealDenied_ = false;
}

/// C3.5: qualify this grain's binding against the bound identity, then (re)start the scan.
///
/// ANY identity or committed-revision move is a supersession: the scan is dropped whole -- never
/// continued, never re-stamped -- because a private prefix must not be completed by a committed
/// suffix. The retained intents survive, so the dependent decision is re-derived from whatever
/// source now answers rather than being lost or taken from stale rows. A binding that names no
/// source for this Loop (published receipt, another interval, a replaced Loop identity) is not an
/// error: it supersedes and leaves the intents to be materialized against the committed origin.
LOOP_COLD_MEM bool Loop::qualifyPrivatePredecessorSource(
    const PrivatePredecessorBinding& binding) {
  const bool cursorBound = privateSource_.phase != kPrivateUnbound;
  const bool sourceKnown = binding.range != nullptr && binding.receipt != nullptr &&
                           binding.loopId != kInvalidLoopId && binding.loopId == loopId &&
                           loopLengthTicks != 0;
  const bool sameIdentity =
      cursorBound && sourceKnown && privateSource_.receipt == binding.receipt &&
      privateSource_.loopId == binding.loopId && privateSource_.interval == binding.interval &&
      privateSource_.captureContentGeneration == binding.captureContentGeneration;
  const bool revisionCurrent = privateSource_.playbackRevision == playbackRevision;
  if (sameIdentity && revisionCurrent) {
    return true;
  }
  if (cursorBound) {
    // Discard *before* any early return: a cursor bound to a previous identity must never keep
    // reporting a pending source the session can no longer resolve.
    discardPrivateSourceScan();
    notePrivateSourceDenied(PrivateSourceDeny::IdentityChanged);
    privateHoldMaterializing_ = false;
  }
  if (!sourceKnown) {
    return false;
  }
  if (!binding.range->ready() || binding.range->generation() == 0 || binding.range->size() == 0) {
    // A predecessor that sealed no row owes no source. Not a denial, and no reason to keep a scan.
    return false;
  }
  // A fresh scan. It owns the cache while it runs: it starts empty, so a committed prefix can never
  // be completed by a private suffix (or the other way round).
  privateSource_.range = binding.range;
  privateSource_.receipt = binding.receipt;
  privateSource_.loopId = binding.loopId;
  privateSource_.interval = binding.interval;
  privateSource_.captureContentGeneration = binding.captureContentGeneration;
  privateSource_.playbackRevision = playbackRevision;
  privateSource_.loopLengthTicks = loopLengthTicks;
  privateSource_.startLoopTick = startLoopTick;
  privateSource_.phase = kPrivateRows;
  overdubSourceSpanCacheNotes_.clear();
  overdubSourceSpanCacheOrigin_ = SourceOrigin::PrivatePredecessor;
  overdubSourceSpanCacheLoopLengthTicks_ = loopLengthTicks;
  overdubSourceSpanCachePlaybackRevision_ = playbackRevision;
  overdubSourceSpanCachePublishedRevision_ = 0;
  overdubSourceSpanCachePublishedIdentity_ = nullptr;
  overdubSourceSpanCacheValid_ = true;
  overdubSourceSpanCacheFullLoop_ = false;
  return true;
}

LOOP_COLD_MEM bool Loop::bindPrivatePredecessorSourceOnly(const PrivatePredecessorBinding& binding) {
  return qualifyPrivatePredecessorSource(binding);
}

/// Append one note-on as an open pair. The pair's span is completed by its Off; until then the
/// row carries `kPrivateOpenEndTick`, which is what makes the scan resumable with fixed storage.
LOOP_COLD_MEM bool Loop::appendPrivatePair(const MidiEvent& row) {
  if (privateOpenPairCount_ >= kPrivateOpenPairCapacity) {
    return false;
  }
  if (!ensurePrivateStageCapacity()) {
    return false;
  }
  const size_t stagedIndex = overdubSourceSpanCacheNotes_.size();
  NoteUtils::DisplayNote note{};
  note.noteId = row.noteId;
  note.note = row.data.noteData.note;
  note.velocity = row.data.noteData.velocity;
  note.startTick = row.tick;
  note.endTick = kPrivateOpenEndTick;
  overdubSourceSpanCacheNotes_.push_back(note);
  ++privateSource_.notesStaged;
  PrivateOpenPair& open = privateOpenPairs_[privateOpenPairCount_++];
  open.noteId = row.noteId;
  open.pitch = row.data.noteData.note;
  open.stagedIndex = static_cast<uint32_t>(stagedIndex);
  return true;
}

/// The staged note an open pair produced, if the store still holds it at that position.
LOOP_COLD_MEM NoteUtils::DisplayNote* Loop::privateStagedNoteAt(size_t index, const MidiEvent& row) {
  if (index >= overdubSourceSpanCacheNotes_.size()) {
    return nullptr;
  }
  NoteUtils::DisplayNote& note = overdubSourceSpanCacheNotes_[index];
  if (note.endTick != kPrivateOpenEndTick || note.note != row.data.noteData.note) {
    return nullptr;
  }
  if (row.noteId != kInvalidNoteId && note.noteId != kInvalidNoteId &&
      note.noteId != row.noteId) {
    return nullptr;
  }
  return &note;
}

/// Fallback only: a pending-change application inside `rebuildOverdubSourceSpanCache` can erase
/// staged rows while the scan runs, which invalidates a stored index. One ordered scan recovers the
/// pair instead of losing the decision; it is not the steady-state path.
LOOP_COLD_MEM NoteUtils::DisplayNote* Loop::privateFindOpenStagedNote(const MidiEvent& row) {
  for (NoteUtils::DisplayNote& note : overdubSourceSpanCacheNotes_) {
    if (note.endTick != kPrivateOpenEndTick || note.note != row.data.noteData.note) {
      continue;
    }
    if (row.noteId != kInvalidNoteId && note.noteId != kInvalidNoteId &&
        note.noteId != row.noteId) {
      continue;
    }
    return &note;
  }
  return nullptr;
}

/// One pass-local pair decision: close the newest open pair this Off names (identity first, then the
/// lane's newest open pair, which is the same LIFO ownership playback uses for an untagged Off). The
/// match is over the fixed open-pair table and the span is reached through the pair's own staged
/// index, so the decision costs one unit of fixed size regardless of how much the prefix holds.
LOOP_COLD_MEM bool Loop::closePrivatePair(const MidiEvent& row) {
  size_t match = privateOpenPairCount_;
  for (size_t i = privateOpenPairCount_; i > 0; --i) {
    const PrivateOpenPair& open = privateOpenPairs_[i - 1];
    if (open.pitch != row.data.noteData.note) {
      continue;
    }
    if (row.noteId != kInvalidNoteId && open.noteId != kInvalidNoteId &&
        open.noteId != row.noteId) {
      continue;
    }
    match = i - 1;
    break;
  }
  if (match >= privateOpenPairCount_) {
    return true;  // An Off with no open On in this extent: dropped, exactly as reconstruct does.
  }
  const uint32_t stagedIndex = privateOpenPairs_[match].stagedIndex;
  for (size_t i = match; i + 1 < privateOpenPairCount_; ++i) {
    privateOpenPairs_[i] = privateOpenPairs_[i + 1];
  }
  --privateOpenPairCount_;
  NoteUtils::DisplayNote* note = privateStagedNoteAt(stagedIndex, row);
  if (note == nullptr) {
    note = privateFindOpenStagedNote(row);
  }
  if (note == nullptr) {
    return true;  // The On is no longer staged: the Off closes nothing.
  }
  note->endTick = row.tick;
  // The session view is what `collectOverdubSourceHoldParticipantIds` reads, so the closed pair joins
  // it now: a hold can then be resolved without waiting for the next derived-view rebuild. No window
  // filter here on purpose -- the source window follows the playhead, the hold tick is not known yet,
  // and the prefix is bounded by the extent. Its slot was proven before the row was consumed.
  overdubSourceViewNotes_.push_back(*note);
  overdubSourceViewEstablished_ = true;
  return true;
}

/// Close what the extent left open at the loop length, exactly as the pass reconstruct does for a
/// held tail -- and publish those pairs to the session view, so the source the hold query reads is the
/// whole extent and not only the pairs whose Off arrived. The walk is over the fixed open-pair table
/// and the view slots are proven first: a refusal is retryable `Pressure` with nothing mutated.
LOOP_COLD_MEM bool Loop::finishPrivateRowScan() {
  if (!ensurePrivateViewCapacity(privateOpenPairCount_)) {
    notePrivateSourceDenied(PrivateSourceDeny::OutputUnavailable);
    return false;
  }
  for (size_t i = 0; i < privateOpenPairCount_; ++i) {
    NoteUtils::DisplayNote* note = nullptr;
    MidiEvent owner{};
    owner.noteId = privateOpenPairs_[i].noteId;
    owner.data.noteData.note = privateOpenPairs_[i].pitch;
    if (privateOpenPairs_[i].stagedIndex < overdubSourceSpanCacheNotes_.size()) {
      NoteUtils::DisplayNote& candidate =
          overdubSourceSpanCacheNotes_[privateOpenPairs_[i].stagedIndex];
      if (candidate.endTick == kPrivateOpenEndTick && candidate.note == owner.data.noteData.note) {
        note = &candidate;
      }
    }
    if (note == nullptr) {
      note = privateFindOpenStagedNote(owner);
    }
    if (note == nullptr) {
      continue;
    }
    note->endTick = loopLengthTicks;
    overdubSourceViewNotes_.push_back(*note);
    overdubSourceViewEstablished_ = true;
  }
  privateOpenPairCount_ = 0;
  // The scan is real content, but not committed content: the cache is stamped valid so the
  // committed-origin chain does not overwrite the private prefix, and `fullLoop` stays false so
  // `overdubSourceSpanCacheReady()` refuses it until the deferred holds are materialized.
  overdubSourceSpanCacheOrigin_ = SourceOrigin::PrivatePredecessor;
  overdubSourceSpanCacheLoopLengthTicks_ = loopLengthTicks;
  overdubSourceSpanCachePlaybackRevision_ = playbackRevision;
  overdubSourceSpanCachePublishedRevision_ = 0;
  overdubSourceSpanCachePublishedIdentity_ = nullptr;
  overdubSourceSpanCacheValid_ = true;
  if (privateHoldCount_ == 0) {
    privateSource_.phase = kPrivateComplete;
    overdubSourceSpanCacheFullLoop_ = true;
    return true;
  }
  // C3.4: the scan is done, the decisions it owed are not. `fullLoop` stays false until they are
  // materialized, so no reader can take a source that the session's own decisions have not seen.
  privateSource_.phase = kPrivateHolds;
  overdubSourceSpanCacheFullLoop_ = false;
  return true;
}

LOOP_COLD_MEM Loop::PrivateSourceStep Loop::stepPrivatePredecessorRows(size_t quantum) {
  const uint64_t generation = privateSource_.range->generation();
  size_t units = 0;
  while (units < quantum) {
    if (privateSource_.chunk >= privateSource_.range->chunkCount()) {
      if (!finishPrivateRowScan()) {
        privateSource_.unitsLast = units;
        return PrivateSourceStep::Pressure;
      }
      privateSource_.unitsLast = units;
      return PrivateSourceStep::Pending;
    }
    // One row may owe up to two units -- its sealed-range read and, for an Off, its pass-local pair
    // decision -- so the grain stops while both still fit. Nothing is consumed on the way out.
    if (quantum - units < 2) {
      break;
    }
    // Both stores' slots are proven *before* the row is read, so a `Pressure` leaves it unconsumed
    // and every append this row makes is already charged for.
    if (!ensurePrivateSourceCapacity()) {
      notePrivateSourceDenied(PrivateSourceDeny::OutputUnavailable);
      privateSource_.unitsLast = units;
      return PrivateSourceStep::Pressure;
    }
    MidiEvent row{};
    bool atEnd = false;
    if (!privateSource_.range->read(generation, privateSource_.chunk, privateSource_.event, row,
                                    atEnd)) {
      notePrivateSourceDenied(PrivateSourceDeny::SourceUnavailable);
      discardPrivatePredecessorSource();
      privateSource_.unitsLast = units;
      return PrivateSourceStep::Denied;
    }
    if (atEnd) {
      ++privateSource_.chunk;
      privateSource_.event = 0;
      continue;
    }
    ++privateSource_.event;
    ++privateSource_.rowsRead;
    ++units;
    // The stop projection the canonical staging applies: an On at or beyond the final length is
    // remapped into the loop, any other row there is dropped.
    MidiEvent projected = row;
    if (!LoopEventStore::projectRecordStopTick(projected, loopLengthTicks)) {
      continue;
    }
    projected.tick = IntervalProjection::tickPhaseInLoop(
        projected.tick, privateSource_.startLoopTick, loopLengthTicks);
    const bool ok = projected.isNoteOn() ? appendPrivatePair(projected)
                                        : closePrivatePair(projected);
    if (!ok) {
      notePrivateSourceDenied(PrivateSourceDeny::PairCapacity);
      discardPrivatePredecessorSource();
      privateSource_.unitsLast = units;
      return PrivateSourceStep::Denied;
    }
    if (projected.isNoteOff()) {
      ++units;  // one pass-local pair decision, declared separately from its row read
    }
  }
  privateSource_.unitsLast = units;
  return PrivateSourceStep::Pending;
}

LOOP_COLD_MEM Loop::PrivateSourceStep Loop::stepPrivatePredecessorSource(
    const PrivatePredecessorBinding& binding, size_t quantum) {
  privateSource_.unitsLast = 0;
  if (quantum == 0) {
    quantum = kPrivateSourceQuantum;
  }
  privateSource_.deny = PrivateSourceDeny::None;
  // C3.5 first, always: every phase of this producer -- the scan *and* the hold materialization --
  // is identity- and revision-qualified. A superseded binding drops the scan before this grain can
  // read a single row or re-derive a participant, so a decision can never be materialized against a
  // private prefix, or a committed window, that the session has already moved past.
  const bool scanning = qualifyPrivatePredecessorSource(binding);
  PrivateSourceStep outcome = PrivateSourceStep::Idle;
  if (scanning) {
    outcome = privateSource_.phase == kPrivateRows ? stepPrivatePredecessorRows(quantum)
                                                   : stepPrivatePredecessorHolds(quantum);
  } else if (privateHoldCount_ > 0) {
    // Nothing private is owed (published, replaced, or never sealed) but intents are: they are
    // materialized against whatever source answers now. The decision is deferred, never dropped.
    outcome = stepPrivatePredecessorHolds(quantum);
  }
  privateSourceLastStep_ = outcome;
  return outcome;
}

/// C3.4: retain the intent a hold's occupancy decision owes while the source is still being
/// built. Deduplicated by `(channel, pitch, hold tick)`, so a repeated note-on edge cannot stack.
LOOP_COLD_MEM bool Loop::retainPrivateHoldIntent(uint8_t channel, uint8_t pitch, uint8_t velocity,
                                   uint32_t holdPhaseTick) {
  for (size_t i = 0; i < privateHoldCount_; ++i) {
    const PrivateSourceHold& hold = privateHolds_[i];
    if (hold.channel == channel && hold.pitch == pitch &&
        hold.holdPhaseTick == holdPhaseTick) {
      return true;
    }
  }
  if (privateHoldCount_ >= kPrivateSourceHoldCapacity) {
    // Sticky by design: `deny` is cleared at the start of every grain, and the only caller ignores
    // the return value. A dropped decision has to stay visible at the seal deadline.
    notePrivateSourceDenied(PrivateSourceDeny::HoldCapacity);
    privateSourceSealDenied_ = true;
    return false;
  }
  PrivateSourceHold& hold = privateHolds_[privateHoldCount_++];
  hold.channel = channel;
  hold.pitch = pitch;
  hold.velocity = velocity;
  hold.holdPhaseTick = holdPhaseTick;
  return true;
}
