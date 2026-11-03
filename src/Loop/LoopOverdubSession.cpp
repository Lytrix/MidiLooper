//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "Loop.h"

#include "LoopInternal.h"
#include "OverlapNoteIdObservation.h"
#include "LoopSnapshotBoxes.h"
#include "Utils/DisplayWindowUtils.h"
#include "Utils/IntervalProjection.h"
#include "Utils/LoopMem.h"
#include "Utils/NoteUtils.h"
#include "Utils/LaneGapSpans.h"
#if defined(SESSION_CAPTURE)
#include "Utils/DebugSessionCapture.h"
#endif
#include <algorithm>

namespace {
/// Empty participant set for a completion whose candidates come from the geometric scan. File-local so the
/// per-note path does not depend on a Track translation unit's constant.
const OverlapNoteIdSet kNoOverlapIds{};
/// DEC-217: how many owed overdub completions one worker turn may discharge. One publication makes every
/// span inside its tile ready at once and each discharge may derive its own closure window, so draining the
/// queue on that turn stacked them (measured 513,812 us of a 546,958 us turn). The rest stay owed and drain
/// on later turns; the spec requires persistence and exactly-once, not same-turn completion, and
/// `materializeDeferredOverdubCompletionsAtDeadline()` answers any remainder at the seal.
constexpr uint8_t kDeferredDischargePerTurn = 1;
}  // namespace

LOOP_COLD_MEM void Loop::syncNextOverdubSessionIndexFromPasses() {
  uint8_t maxIndex = kUngroupedOverdubSessionIndex;
  for (const OverdubPass& pass : passes.overdubPasses) {
    if (pass.overdubSessionIndex > maxIndex) {
      maxIndex = pass.overdubSessionIndex;
    }
  }
  if (maxIndex == kUngroupedOverdubSessionIndex || maxIndex == 255) {
    nextOverdubSessionIndex_ = 1;
  } else {
    nextOverdubSessionIndex_ = static_cast<uint8_t>(maxIndex + 1);
  }
  currentOverdubSessionIndex_ = kUngroupedOverdubSessionIndex;
}

Loop::OverdubCoverageTurn Loop::serviceOverdubCoverageTurn(size_t eventBudget) {
#if defined(SESSION_CAPTURE)
  // DEC-216 follow-up: total cost of this worker turn, including the tile derive below.
  const uint32_t coverageStartUs = micros();
#endif
  if (!overdubCoverage_.sessionActive || loopLengthTicks == 0) {
    LANE_COV_COUNT(TurnNoSession);
    return OverdubCoverageTurn::Idle;
  }
  // CONTRACTS.md C3.1/C3.5: a private-predecessor scan owns this cache from its first grain until its
  // deferred holds are materialized, and publishing a committed tile over it would mix a private prefix
  // with committed content in either direction. The worker yields; the tile stays owed.
  if (privatePredecessorSourcePending()) {
    LANE_COV_COUNT(TurnPrivateYield);
    return OverdubCoverageTurn::Idle;
  }
  const uint16_t from = overdubCoverageCursorTile_ >= CoveragePrerequisite::kMaxTiles
                            ? 0
                            : overdubCoverageCursorTile_;
  const uint16_t tile = overdubCoverage_.nextOwedTile(from);
  if (tile >= CoveragePrerequisite::kMaxTiles) {
    LANE_COV_COUNT(TurnNoOwed);
    overdubCoverageCursorTile_ = CoveragePrerequisite::kMaxTiles;
    // DEC-217: the publication turn is not the only turn that may discharge. A queue that is still owed
    // after its tile published must keep draining on the following turns, or the one-per-turn bound would
    // turn into "one per publication". Recut only when a consequence actually landed.
    if (dischargeDeferredOverdubCompletions(kDeferredDischargePerTurn) != 0) {
      recutOverdubSourceView();
      return OverdubCoverageTurn::Completed;
    }
    return OverdubCoverageTurn::Idle;
  }
  // A different tile, or a generation move, starts that tile from its own beginning: progress is never
  // carried across tiles or across source generations.
  if (tile != overdubCoverageCursorTile_ ||
      overdubCoverageCursorGeneration_ != overdubCoverage_.generation) {
    overdubCoverageCursorTile_ = tile;
    overdubCoverageCursorGeneration_ = overdubCoverage_.generation;
    overdubCoverageCursor_ = LoopContentResolution::TilePrepCursor{};
  }
  const uint32_t tileLength = CoveragePrerequisite::tileLengthTicks();
  const uint32_t windowStart = static_cast<uint32_t>(tile) * tileLength;
  if (windowStart >= loopLengthTicks) {
    // A tile beyond the loop (an envelope edge) cannot be prepared; mark it so it is not re-selected.
    overdubCoverageCursorTile_ = CoveragePrerequisite::kMaxTiles;
    return OverdubCoverageTurn::Idle;
  }
  const uint32_t windowLength = std::min(tileLength, loopLengthTicks - windowStart);
  // Conservative closure (design D3): the tile's derive reads backward by the maximum committed note span,
  // so a note that starts before the tile and sustains into it is paired with its real geometry. The tile
  // is the *publication* unit; this lookup window is only what the derive reads.
  uint32_t lookupStart = windowStart;
  uint32_t lookupLength = windowLength;
  CoveragePrerequisite::closureWindowAround(windowStart, windowLength, overdubCoverageClosureTicks_,
                                             loopLengthTicks, lookupStart, lookupLength);
  if (!LoopContentResolution::stepTilePreparation(passes, loopLengthTicks, lookupStart, lookupLength,
                                                 eventBudget, overdubCoverageCursor_)) {
    LANE_COV_COUNT(TurnAdvanced);
    return OverdubCoverageTurn::Advanced;
  }
  // Atomic publication (task 4.8): the grain that completes the tile publishes its content **and** its
  // prepared bit together. The worker is the span cache's only writer while a session is active.
#if defined(SESSION_CAPTURE)
  const uint32_t prepareUs = micros() - coverageStartUs;
#endif
  publishPreparedOverdubCoverageTile(tile, overdubCoverageCursor_.notes,
                                     overdubCoverageCursor_.rawEvents, windowStart, windowLength);
  overdubCoverage_.publishPrepared(tile);
  LANE_COV_COUNT(TurnPublished);
  overdubCoverageCursorTile_ = CoveragePrerequisite::kMaxTiles;
#if defined(SESSION_CAPTURE)
  const uint32_t publishUs = micros() - coverageStartUs - prepareUs;
#endif
  // Consequences that were owed for a tile now prepared are discharged on the same turn that made their
  // coverage available, then the view is recut from the published tiles.
  (void)dischargeDeferredOverdubCompletions(kDeferredDischargePerTurn);
#if defined(SESSION_CAPTURE)
  const uint32_t dischargeUs = micros() - coverageStartUs - prepareUs - publishUs;
#endif
  recutOverdubSourceView();
#if defined(SESSION_CAPTURE)
  // DEC-216 follow-up: name the step that makes a tile-completion turn expensive. Measured shape before this
  // row: every `loop_rem,idle_maint` above 50 ms in the live overdub chain coincides with `lcr,src,why=tile`,
  // and the same turn is one `idle_turn` unit of up to 262,727 us while `lcr,src` itself reports only 2,570 us.
  // Tier-A (`DIAG,idle_turn,`) so it survives the timing-critical flush; one row per expensive completion.
  const uint32_t coverageUs = micros() - coverageStartUs;
  if (coverageUs >= 8000u) {
    const uint32_t recutUs = coverageUs - prepareUs - publishUs - dischargeUs;
    char coverageLine[128];
    snprintf_P(coverageLine, sizeof(coverageLine),
               PSTR("#CAP,%lu,DIAG,idle_turn,cov=tile,total=%lu,prep=%lu,pub=%lu,disch=%lu,recut=%lu,"
                    "notes=%u,ev=%u"),
               (unsigned long)micros(), (unsigned long)coverageUs, (unsigned long)prepareUs,
               (unsigned long)publishUs, (unsigned long)dischargeUs, (unsigned long)recutUs,
               static_cast<unsigned>(overdubCoverageCursor_.notes.size()),
               static_cast<unsigned>(overdubCoverageCursor_.rawEvents.size()));
    DebugSessionCapture::appendCaptureTextLine(coverageLine);
  }
#endif
  return OverdubCoverageTurn::Completed;
}

bool Loop::overdubCoveragePhasePrepared(uint32_t phaseTick) const {
  if (!overdubCoverage_.sessionActive) {
    return false;
  }
  return overdubCoverage_.tilePrepared(
      CoveragePrerequisite::tileIndexForPhase(phaseTick, loopLengthTicks));
}

bool Loop::overdubCoverageSpanPrepared(uint32_t startTick, uint32_t lengthTicks) const {
  if (!overdubCoverage_.sessionActive) {
    return false;
  }
  return overdubCoverage_.spanPrepared(startTick, lengthTicks);
}

uint32_t Loop::overdubSourceSpanOfNote(const NoteUtils::DisplayNote& note) const {
  if (loopLengthTicks == 0) {
    return 0;
  }
  if (note.endTick >= note.startTick) {
    return note.endTick - note.startTick;
  }
  return loopLengthTicks - note.startTick + note.endTick;
}

void Loop::adoptPrewarmedOverdubCoverage() {
  if (!overdubCoverage_.sessionActive || loopLengthTicks == 0) {
    return;
  }
  // A private-prefix cache is never adopted as a committed source (C3.1/C3.5).
  bool usable = overdubSourceSpanCacheOrigin_ != SourceOrigin::PrivatePredecessor &&
                overdubSourceSpanCacheValid_ &&
                overdubSourceSpanCacheLoopLengthTicks_ == loopLengthTicks &&
                overdubSourceSpanCachePlaybackRevision_ == playbackRevision &&
                overdubSourceSpanCacheFullLoop_;
  if (usable && publishedSnapshotIsCurrent()) {
    usable = overdubSourceSpanCachePublishedRevision_ == publishedSnapshotRevision() &&
             overdubSourceSpanCachePublishedIdentity_ != nullptr &&
             overdubSourceSpanCachePublishedIdentity_ == publishedSnapshotIdentity();
  }
  if (!usable) {
    // No current full-loop cache: the session starts from empty coverage and the worker publishes the
    // tiles the player touches. An old generation's content is never served.
    overdubSourceSpanCacheNotes_.clear();
    overdubSourceSpanCacheValid_ = false;
    overdubSourceSpanCacheFullLoop_ = false;
    overdubSourceSpanCacheOrigin_ = SourceOrigin::None;
    return;
  }
  // The prewarm already describes the committed source for this generation, so the whole loop is
  // prepared without a derive. Its maximum note span seeds closure for the tiles published later.
  uint32_t maxSpan = 0;
  for (const NoteUtils::DisplayNote& note : overdubSourceSpanCacheNotes_) {
    const uint32_t span = overdubSourceSpanOfNote(note);
    if (span > maxSpan) {
      maxSpan = span;
    }
  }
  overdubCoverageClosureTicks_ = maxSpan;
  advanceOverdubCoverageClosure(CoveragePrerequisite::tileLengthTicks());
  const uint16_t count = CoveragePrerequisite::tileCount(loopLengthTicks);
  for (uint16_t tile = 0; tile < count; ++tile) {
    overdubCoverage_.publishPrepared(tile);
  }
  overdubSourceSpanCacheCoverageStart_ = 0;
  overdubSourceSpanCacheCoverageLength_ = loopLengthTicks;
}

void Loop::advanceOverdubCoverageClosure(uint32_t noteSpanTicks) {
  if (noteSpanTicks == 0) {
    return;
  }
  if (noteSpanTicks >= loopLengthTicks) {
    overdubCoverageClosureTicks_ = loopLengthTicks;
    return;
  }
  if (noteSpanTicks > overdubCoverageClosureTicks_) {
    overdubCoverageClosureTicks_ = noteSpanTicks;
  }
}

void Loop::publishPreparedOverdubCoverageTile(uint16_t tile, const NoteUtils::DisplayNoteVec& derived,
                                              const SessionMidiEventVec& committedEvents,
                                              uint32_t tileStart, uint32_t tileLength) {
  if (!overdubCoverage_.sessionActive || loopLengthTicks == 0) {
    return;
  }
  overdubSourceSpanCacheLoopLengthTicks_ = loopLengthTicks;
  overdubSourceSpanCachePlaybackRevision_ = playbackRevision;
  overdubSourceSpanCacheOrigin_ = SourceOrigin::Passes;
  overdubSourceSpanCacheValid_ = true;
  // This tile's rows: the derive's notes that intersect the tile, plus the committed identities the
  // pass-local pairing omitted. Closure rows outside the tile are read for pairing only: they belong to
  // their own tile's publication, and publishing them here would make this grain publish content it does
  // not own.
  NoteUtils::DisplayNoteVec tileNotes;
  tileNotes.reserve(derived.size());
  for (const NoteUtils::DisplayNote& note : derived) {
    if (note.noteId == kInvalidNoteId) {
      continue;
    }
    if (!DisplayWindowUtils::noteIntersectsWindow(note.startTick, note.endTick, tileStart, tileLength,
                                                  loopLengthTicks)) {
      continue;
    }
    tileNotes.push_back(note);
  }
  mergeOmittedCommittedNoteOnIdentitiesForNotes(tileNotes, committedEvents);
  for (const NoteUtils::DisplayNote& note : tileNotes) {
    if (note.noteId == kInvalidNoteId) {
      continue;
    }
    if (!DisplayWindowUtils::noteIntersectsWindow(note.startTick, note.endTick, tileStart, tileLength,
                                                  loopLengthTicks)) {
      continue;
    }
    LoopContentResolution::upsertResolvedDisplayNotes(overdubSourceSpanCacheNotes_, {note});
    advanceOverdubCoverageClosure(overdubSourceSpanOfNote(note));
  }
  // The cache's extent is the prepared tile set, not a contiguous pair.
  const uint16_t count = CoveragePrerequisite::tileCount(loopLengthTicks);
  uint64_t allTiles = count >= 64 ? ~uint64_t(0) : ((uint64_t(1) << count) - 1u);
  const bool fullLoop = count != 0 && (overdubCoverage_.prepared & allTiles) == allTiles;
  overdubSourceSpanCacheFullLoop_ = fullLoop;
  overdubSourceSpanCacheCoverageStart_ = 0;
  overdubSourceSpanCacheCoverageLength_ = fullLoop ? loopLengthTicks : 0;
  (void)tile;
}

bool Loop::retainDeferredOverdubCompletion(const DeferredOverdubCompletion& completion) {
  for (uint8_t i = 0; i < overdubCoverageDeferredCount_; ++i) {
    DeferredOverdubCompletion& existing = deferredOverdubCompletions_[i];
    if (existing.channel == completion.channel && existing.pitch == completion.pitch &&
        existing.startTick == completion.startTick) {
      existing.endTick = completion.endTick != 0 ? completion.endTick : existing.endTick;
      existing.velocity = completion.velocity;
      existing.occupancyOwed = existing.occupancyOwed || completion.occupancyOwed;
      existing.spanOwed = existing.spanOwed || completion.spanOwed;
      if (existing.noteId == kInvalidNoteId) {
        existing.noteId = completion.noteId;
      }
      return true;
    }
  }
  if (overdubCoverageDeferredCount_ >= kDeferredOverdubCompletionCapacity) {
    return false;
  }
  deferredOverdubCompletions_[overdubCoverageDeferredCount_++] = completion;
  return true;
}

uint32_t LOOP_COLD_MEM Loop::overdubCoverageEarliestDueTick() const {
  uint32_t earliest = UINT32_MAX;
  for (uint8_t i = 0; i < overdubCoverageDeferredCount_; ++i) {
    const DeferredOverdubCompletion& completion = deferredOverdubCompletions_[i];
    if (!completion.occupancyOwed && !completion.spanOwed) {
      continue;
    }
    // An occupancy-only hold has no closed span yet; its consumer horizon is the eventual note-off.
    if (completion.endTick == 0 || completion.endTick == completion.startTick) {
      continue;
    }
    if (completion.endTick < earliest) {
      earliest = completion.endTick;
    }
  }
  return earliest;
}

bool LOOP_COLD_MEM Loop::overdubCoverageRequiredNow(uint32_t currentPhase,
                                                     uint32_t leadTicks) const {
  const uint32_t dueTick = overdubCoverageEarliestDueTick();
  if (dueTick == UINT32_MAX || loopLengthTicks == 0) {
    return false;
  }
  const uint32_t phase = currentPhase % loopLengthTicks;
  const uint32_t untilDue = (dueTick + loopLengthTicks - phase) % loopLengthTicks;
  return untilDue <= leadTicks;
}

uint8_t Loop::dischargeDeferredOverdubCompletions(uint8_t maxDischarge) {  if (!overdubCoverage_.sessionActive || loopLengthTicks == 0) {
    return 0;
  }
  promoteOverflowedOverdubCompletions();
  LANE_COV_OBSERVE(overdubCoverageDeferredCount_);
  LANE_COV_COUNT(DischargeCalls);
  uint8_t discharged = 0;
  for (uint8_t i = 0; i < overdubCoverageDeferredCount_;) {
    DeferredOverdubCompletion& completion = deferredOverdubCompletions_[i];
    const bool occupancyReady =
        !completion.occupancyOwed ||
        overdubCoverage_.tilePrepared(CoveragePrerequisite::tileIndexForPhase(completion.startTick,
                                                                             loopLengthTicks));
    const bool spanLengthReady = completion.endTick != completion.startTick;
    const uint32_t spanLength =
        spanLengthReady
            ? (completion.endTick >= completion.startTick
                   ? completion.endTick - completion.startTick
                   : loopLengthTicks - completion.startTick + completion.endTick)
            : 0;
    const bool spanReady =
        !completion.spanOwed || overdubCoverage_.spanPrepared(completion.startTick, spanLength);
    if (!occupancyReady || !spanReady) {
      if (!occupancyReady) LANE_COV_COUNT(BlockedOccupancy);
      if (!spanReady) LANE_COV_COUNT(BlockedSpan);
      ++i;
      continue;
    }
    (void)applyDeferredOverdubCompletion(completion);
    LANE_COV_COUNT(Discharged);
    // Remove by swap-and-pop: the order of independent completions does not carry meaning, and a note's
    // occupancy is always applied before its own span inside `applyDeferredOverdubCompletion`.
    if (i + 1 < overdubCoverageDeferredCount_) {
      deferredOverdubCompletions_[i] =
          deferredOverdubCompletions_[overdubCoverageDeferredCount_ - 1];
    }
    --overdubCoverageDeferredCount_;
    ++discharged;
    // DEC-217: the queue is the grain boundary. One publication makes *every* span inside its tile
    // ready at once, and each discharge may derive its own closure window, so draining the queue on that
    // turn stacked them: measured `disch=513,812 us` of a 546,958 us turn
    // (`session_20261030_122722.log`). The rest stay owed and drain on later turns; the spec requires
    // persistence and exactly-once, never same-turn completion, and the seal deadline is the backstop.
    if (maxDischarge != 0 && discharged >= maxDischarge) break;
  }
  LANE_COV_OBSERVE(overdubCoverageDeferredCount_);
  return discharged;
}

void Loop::materializeDeferredOverdubCompletionsAtDeadline() {
  // The interval is ending. Every completion still owed is answered from a *windowed* derive of its own
  // span, never from a partial view: a decision is not dropped at the deadline, and it is applied once.
  LANE_WRAP_COUNT(WrapSetEntering, overdubCoverageDeferredCount_);
  LANE_WRAP_COUNT(WrapOverflowEntering, overdubCompletionOverflowCount_);
  {
    LANE_WRAP_PHASE(WrapDeadlineSet);
    for (uint8_t i = 0; i < overdubCoverageDeferredCount_; ++i) {
      LANE_WRAP_COUNT(WrapDeadlineEvaluated, 1);
      (void)applyDeferredOverdubCompletion(deferredOverdubCompletions_[i]);
    }
  }
  overdubCoverageDeferredCount_ = 0;
  LANE_WRAP_PHASE(WrapDeadlineOverflow);
  // The completions owed beyond the full set are answered by the same rule. By index: an answer appends
  // transforms, never another owed row, so the rows read here stay valid and none is answered twice.
  for (size_t i = 0; overdubCompletionOverflowCount_ != 0 && i < pendingNoteChanges_.size(); ++i) {
    if (!pendingNoteChanges_[i].consequenceOwed) continue;
    pendingNoteChanges_[i].consequenceOwed = false;
    --overdubCompletionOverflowCount_;
    LANE_WRAP_COUNT(WrapDeadlineEvaluated, 1);
    (void)applyDeferredOverdubCompletion(overflowedCompletion(pendingNoteChanges_[i]));
  }
  overdubCompletionOverflowCount_ = 0;
}

Loop::DeferredOverdubCompletion Loop::overflowedCompletion(const PendingNoteChange& row) {
  DeferredOverdubCompletion completion{};
  completion.startTick = row.startTick;
  completion.endTick = row.endTick;
  completion.noteId = row.noteId;
  completion.channel = row.channel;
  completion.pitch = row.pitch;
  completion.velocity = row.velocity;
  completion.spanOwed = true;
  return completion;
}

void Loop::promoteOverflowedOverdubCompletions() {
  // Worker lane only: refill the capacity the discharges freed from the rows owed beyond the full set, in
  // row order. The scan is skipped entirely while nothing is owed, and a count with no flagged row left
  // (the rows moved to a stop job with their set) is retired here rather than rescanned every turn.
  if (overdubCompletionOverflowCount_ == 0 ||
      overdubCoverageDeferredCount_ >= kDeferredOverdubCompletionCapacity) {
    return;
  }
  bool anyOwed = false;
  for (PendingNoteChange& row : pendingNoteChanges_) {
    if (!row.consequenceOwed) continue;
    anyOwed = true;
    if (!retainDeferredOverdubCompletion(overflowedCompletion(row))) return;  // full again
    row.consequenceOwed = false;
    if (--overdubCompletionOverflowCount_ == 0) return;
  }
  if (!anyOwed) overdubCompletionOverflowCount_ = 0;
}

bool Loop::applyDeferredOverdubCompletion(const DeferredOverdubCompletion& completion) {
  if (loopLengthTicks == 0 || completion.endTick == completion.startTick) {
    return false;
  }
  NoteUtils::DisplayNoteVec local;
  // The discharge lane answers from the **committed tile content** (the cache), which is what the worker
  // published for this generation; the view is a display cut and may not have been recut yet on the same
  // turn. If the span's tiles are still not all prepared, the deadline/discharge derives exactly the
  // span's closure window -- a bounded window, never a pass walk.
  const NoteUtils::DisplayNoteVec* candidates = &overdubSourceSpanCacheNotes_;
  const uint32_t spanLength =
      completion.endTick >= completion.startTick
          ? completion.endTick - completion.startTick
          : loopLengthTicks - completion.startTick + completion.endTick;
  if (!overdubCoverage_.spanPrepared(completion.startTick, spanLength)) {
    LANE_WRAP_COUNT(WrapDeadlineDerives, 1);
    uint32_t lookupStart = 0;
    uint32_t lookupLength = 0;
    CoveragePrerequisite::closureWindowAround(completion.startTick, spanLength,
                                               overdubCoverageClosureTicks_, loopLengthTicks,
                                               lookupStart, lookupLength);
    LoopContentResolution::resolveDisplayNotesInWindow(passes, loopLengthTicks, lookupStart,
                                                       lookupLength, local);
    candidates = &local;
  }
  OverlapNoteIdSet resolved;
  if (completion.occupancyOwed) {
    OverlapNoteIdSet participants;
    for (const NoteUtils::DisplayNote& note : *candidates) {
      if (note.note != completion.pitch || note.noteId == kInvalidNoteId) {
        continue;
      }
      if (!OverlapNoteIdObservation::displayNotePresentAtHold(note.startTick, note.endTick,
                                                              completion.startTick,
                                                              loopLengthTicks)) {
        continue;
      }
      (void)participants.insert(note.noteId);
    }
    resolveOccupancyAtNoteOn(completion.channel, completion.pitch, completion.velocity, participants,
                             completion.startTick, &resolved);
  }
  return applyOverlapConsequenceForCompletedNote(
      completion.channel, completion.pitch, completion.velocity, completion.startTick,
      completion.endTick, completion.noteId, kNoOverlapIds,
      completion.occupancyOwed ? &resolved : nullptr, false, candidates);
}

uint8_t Loop::markOverdubCoverageNoteOn(uint32_t phaseTick) {
  if (!overdubCoverage_.sessionActive) {
    return 0;
  }
  return overdubCoverage_.markNoteOn(phaseTick);
}

uint8_t Loop::markOverdubCoverageHeldAdvance(uint32_t phaseTick) {
  if (!overdubCoverage_.sessionActive) {
    return 0;
  }
  return overdubCoverage_.markHeldAdvance(phaseTick);
}

uint8_t Loop::markOverdubCoverageSpan(uint32_t startTick, uint32_t endTick) {
  if (!overdubCoverage_.sessionActive || loopLengthTicks == 0) {
    return 0;
  }
  const uint32_t start = startTick % loopLengthTicks;
  const uint32_t end = endTick % loopLengthTicks;
  // A note that closes before its own start wrapped the loop; its span runs through the loop end.
  const uint32_t length = end > start ? (end - start) : (loopLengthTicks - start + end);
  return overdubCoverage_.markSpanSealed(start, length == 0 ? 1u : length);
}

LOOP_COLD_MEM void Loop::openOverdubSession(uint32_t sessionPlayheadPhaseTick, bool forceNew) {
  if (hasOverdubSession() && !forceNew) {
    return;
  }
  // CONTRACTS.md C3.5: a new overdub interval cancels the previous interval's private-predecessor
  // cursor with its session. Nothing staged for the last interval may be read as this one's source.
  discardPrivatePredecessorSource();
  // Record-stop may still hold sealed rows when visual has not consumed that
  // revision. Those rows describe the previous session; wrapping them into this
  // one leaves a 336→336 hold beside the occupy suffix (`001647`).
  clearSealedCapturePreview();
  playheadPhaseTick = sessionPlayheadPhaseTick;
  // Touched coverage is per overdub session (design D3): a new session starts with no required tiles for
  // its source generation. Nothing is derived here -- acceptance marks demand later.
  overdubCoverage_.beginSession(loopLengthTicks, playbackRevision);
  overdubCoverageDeferredCount_ = 0;
  overdubCompletionOverflowCount_ = 0;
  overdubCoverageClosureTicks_ = 0;
  seedOverdubCoverageClosureFromCommittedContent();
  adoptPrewarmedOverdubCoverage();
  currentOverdubSessionIndex_ = nextOverdubSessionIndex_;
  if (nextOverdubSessionIndex_ == 255) {
    nextOverdubSessionIndex_ = 1;
  } else {
    ++nextOverdubSessionIndex_;
  }
  overdubWrapArmed_ = false;
  overdubWrapSuppressNext_ = false;
  ++overdubSessionUndoGeneration_;
  // Task 4.20: the session-opening owner stamps the identity the reservation was taken for. Admission
  // runs before the session exists, so the claim is deliberately anonymous until this edge; the stop
  // admission then refuses any transfer whose identity does not match.
  if (sessionExitPublishClaimState_ == SessionExitPublishClaim::Reserved) {
    if (SessionExitSnapshotHandoff* record = reservedSessionExit(); record != nullptr) {
      record->sessionIdentity = overdubSessionUndoGeneration_;
    }
  }
  handOverSessionRecordToInventory();
  overdubSessionPassIds_.clear();
  overdubSessionCompanionIds_.clear();
  overdubSessionCursor_ = 0;
  overdubSessionLiveUndoEvents_.clear();
  // A successor session may hand off even while an unconsumed receipt sits in
  // the ring; the flag is per session, not per ring.
  sessionExitHandedOff_ = false;
}

LOOP_COLD_MEM bool Loop::markSessionExitTailReady(size_t ringIndex, PassId lastPassId) {
  SessionExitSnapshotHandoff* receipt =
      ringIndex == kReservedSessionExitReceipt ? reservedSessionExit()
                                               : sessionExitReceiptByRingIndex(ringIndex);
  if (receipt == nullptr || !receipt->pending || receipt->ready) {
    return false;
  }
  receipt->lastPassId = lastPassId;
  receipt->ready = true;
  // Task 4.20: the reserved record's obligation is complete, so the publisher may move it through
  // the receipt path. `Populated -> Publishing` is the only transition out of a populated record.
  if (ringIndex == kReservedSessionExitReceipt) {
    if (sessionExitPublishClaimState_ != SessionExitPublishClaim::Populated) return false;
    sessionExitPublishClaimState_ = SessionExitPublishClaim::Publishing;
  }
  return true;
}

LOOP_COLD_MEM void Loop::closeOverdubSession() {
  // Task 4.20: an interval that ends without taking a tail of its own (the legacy synchronous stop,
  // a cancelled interval) must not leave its reservation behind — the next start would be refused by
  // a claim nobody owns. Only a payload-free reservation of *this* session is released here; a
  // populated or publishing record owns identities the publisher still owes, and a reservation held
  // for a live successor is not this close's to release.
  (void)releaseSessionExitPublishOwner(overdubSessionUndoGeneration_);
  retireSealedCapturePreviewForDisplay();
  ++overdubSessionUndoGeneration_;
  playheadPhaseTick = UINT32_MAX;
  currentOverdubSessionIndex_ = kUngroupedOverdubSessionIndex;
  overdubWrapArmed_ = false;
  overdubWrapSuppressNext_ = false;
  handOverSessionRecordToInventory();
  overdubSessionPassIds_.clear();
  overdubSessionCompanionIds_.clear();
  overdubSessionCursor_ = 0;
  overdubSessionLiveUndoEvents_.clear();
  overdubCoverage_.endSession();
  overdubCoverageDeferredCount_ = 0;
  overdubCompletionOverflowCount_ = 0;
  overdubCoverageClosureTicks_ = 0;
  clearOverdubSourceView();
  discardUnpublishedSnapshotIfNotSessionExit();
}

LOOP_COLD_MEM bool Loop::beginCloseOverdubSession(SessionClose& work) {
  using Phase = SessionClose::Phase;
  if ((work.phase != Phase::Idle && work.phase != Phase::Complete) ||
      !hasOverdubSession() || overdubSessionUndoGeneration_ == UINT64_MAX ||
      (pendingSnapshotPublishBox_ && !pendingSnapshotSessionExitQueued())) return false;
  work = {};
  work.owner = this;
  work.generation = ++overdubSessionUndoGeneration_;
  work.phase = Phase::Passes;
  return true;
}

LOOP_COLD_MEM Loop::SessionClose::Phase Loop::stepCloseOverdubSession(
    SessionClose& work, size_t quantum) {
  using Phase = SessionClose::Phase;
  if (work.owner != this || !quantum) return Phase::Denied;
  if (work.generation != overdubSessionUndoGeneration_ ||
      (pendingSnapshotPublishBox_ && !pendingSnapshotSessionExitQueued())) {
    work.phase = Phase::Denied;
    return work.phase;
  }
  while (quantum-- && work.phase >= Phase::Passes && work.phase <= Phase::Finish) {
    ++work.units;
    switch (work.phase) {
      case Phase::Passes:
        if (!overdubSessionPassIds_.empty()) overdubSessionPassIds_.pop_back();
        else work.phase = Phase::Companions;
        break;
      case Phase::Companions:
        if (overdubSessionCompanionIds_.empty()) work.phase = Phase::LiveEvents;
        else if (!overdubSessionCompanionIds_.back().empty())
          overdubSessionCompanionIds_.back().pop_back();
        else overdubSessionCompanionIds_.pop_back();
        break;
      case Phase::LiveEvents:
        if (!overdubSessionLiveUndoEvents_.empty()) overdubSessionLiveUndoEvents_.pop_back();
        else work.phase = Phase::SourceEvents;
        break;
      case Phase::SourceEvents:
        if (!overdubSourceViewEvents_.empty()) overdubSourceViewEvents_.pop_back();
        else work.phase = Phase::SourceNotes;
        break;
      case Phase::SourceNotes:
        if (!overdubSourceViewNotes_.empty()) overdubSourceViewNotes_.pop_back();
        else work.phase = Phase::Finish;
        break;
      case Phase::Finish:
        playheadPhaseTick = UINT32_MAX;
        currentOverdubSessionIndex_ = kUngroupedOverdubSessionIndex;
        overdubWrapArmed_ = overdubWrapSuppressNext_ = false;
        overdubSessionCursor_ = 0;
        overdubSourceViewLoopLengthTicks_ = 0;
        overdubSourceViewEstablished_ = false;
        retireSealedCapturePreviewForDisplay();
        work.phase = Phase::Complete;
        break;
      default: break;
    }
  }
  return work.phase;
}

LOOP_COLD_MEM void Loop::armOverdubWrapAfterLeavingStart(uint32_t currentPhase) {
  if (!hasOverdubSession() || overdubWrapArmed_ || overdubWrapSuppressNext_) {
    return;
  }
  if (currentPhase != playheadPhaseTick) {
    overdubWrapArmed_ = true;
  }
}

LOOP_COLD_MEM bool Loop::shouldCommitOverdubWrap(uint32_t prevPhase, uint32_t currentPhase) const {
  if (!hasOverdubSession() || !overdubWrapArmed_ || loopLengthTicks == 0) {
    return false;
  }
  // DEC-051: seal when playhead crosses loop end (phase 0). S stays session identity.
  return IntervalProjection::didPlayheadCrossPhase(prevPhase, currentPhase, 0, loopLengthTicks);
}

LOOP_COLD_MEM void Loop::noteOverdubWrapCommitted() {
  overdubWrapArmed_ = false;
  overdubWrapSuppressNext_ = false;
}

LOOP_COLD_MEM void Loop::suppressNextOverdubWrap() {
  overdubWrapArmed_ = false;
  overdubWrapSuppressNext_ = true;
}

LOOP_COLD_MEM bool Loop::consumeSuppressedOverdubWrapCrossing(uint32_t prevPhase,
                                                             uint32_t currentPhase) {
  if (!hasOverdubSession() || !overdubWrapSuppressNext_ || loopLengthTicks == 0) {
    return false;
  }
  if (!IntervalProjection::didPlayheadCrossPhase(prevPhase, currentPhase, 0, loopLengthTicks)) {
    return false;
  }
  overdubWrapSuppressNext_ = false;
  overdubWrapArmed_ = false;
  return true;
}

LOOP_COLD_MEM size_t Loop::extractOpenCaptureNoteOns(SessionMidiEventVec& out) {
  out.clear();
  if (!captureActive() || capture.store.empty() || loopLengthTicks == 0) {
    return 0;
  }
  SessionMidiEventVec flat;
  capture.store.copyEventsTo(flat);
  if (flat.empty()) {
    return 0;
  }
  // Preview pairs in append order (wrap-held On@2976 + Off@96). Display
  // materialize / playback sort the store, so findOpenNoteOns on ticks treats
  // that On as open when a later same-pitch On is still held (014937).
  std::vector<char> extractAt(flat.size(), 0);
  bool usedPreview = false;
  for (const uint32_t previewIndex : capturePreview.openNoteIndices) {
    if (previewIndex >= capturePreview.notes.size() ||
        previewIndex >= capturePreview.noteStates.size() ||
        !capturePreview.noteStates[previewIndex].open) {
      continue;
    }
    usedPreview = true;
    const NoteUtils::DisplayNote& note = capturePreview.notes[previewIndex];
    for (size_t i = flat.size(); i > 0; --i) {
      const size_t eventIndex = i - 1;
      if (extractAt[eventIndex] != 0) {
        continue;
      }
      const MidiEvent& evt = flat[eventIndex];
      if (evt.isNoteOn() && evt.data.noteData.note == note.note && evt.tick == note.startTick) {
        extractAt[eventIndex] = 1;
        break;
      }
    }
  }
  if (!usedPreview) {
    const std::vector<NoteUtils::OpenNoteOn> opens =
        NoteUtils::findOpenNoteOns(flat, loopLengthTicks);
    for (const NoteUtils::OpenNoteOn& open : opens) {
      if (open.eventIndex < flat.size()) {
        extractAt[open.eventIndex] = 1;
      }
    }
  }
  SessionMidiEventVec kept;
  kept.reserve(flat.size());
  for (size_t eventIndex = 0; eventIndex < flat.size(); ++eventIndex) {
    if (extractAt[eventIndex] != 0) {
      out.push_back(flat[eventIndex]);
    } else {
      kept.push_back(flat[eventIndex]);
    }
  }
  if (out.empty()) {
    return 0;
  }
  advanceCaptureContentGeneration();
  capture.store.clear();
  if (!kept.empty()) {
    capture.store.loadFromEvents(kept);
  }
  captureEventsSortDirty = true;
  ensureCaptureEventsSorted();
  rebuildCapturePreviewFromStore(*this);
  ++captureDisplayRevision;
  return out.size();
}

LOOP_COLD_MEM void Loop::pushOverdubSessionPass(PassId passId, EditPassIdList companionIds) {
  if (passId == kInvalidPassId) {
    return;
  }
  dropOverdubSessionRedoTail();
  ++overdubSessionUndoGeneration_;
  overdubSessionPassIds_.push_back(passId);
  overdubSessionCompanionIds_.push_back(std::move(companionIds));
  overdubSessionCursor_ = overdubSessionPassIds_.size();
}

LOOP_COLD_MEM Loop::OverdubSessionRetention Loop::overdubSessionRetention() const {
  OverdubSessionRetention out;
  out.passCount = overdubSessionPassIds_.size();
  out.passCapacity = overdubSessionPassIds_.capacity();
  out.companionCount = overdubSessionCompanionIds_.size();
  out.companionCapacity = overdubSessionCompanionIds_.capacity();
  for (const EditPassIdList& ids : overdubSessionCompanionIds_) {
    out.companionNestedCapacity += ids.capacity();
  }
  out.cursor = overdubSessionCursor_;
  return out;
}

LOOP_COLD_MEM bool Loop::collectLastOverdubSessionPass(PassId& passId,
                                                      EditPassIdList& companionIds) const {
  passId = kInvalidPassId;
  companionIds.clear();
  if (overdubSessionCursor_ == 0 || overdubSessionPassIds_.empty()) {
    return false;
  }
  const size_t index = overdubSessionCursor_ - 1;
  if (index >= overdubSessionPassIds_.size()) {
    return false;
  }
  passId = overdubSessionPassIds_[index];
  if (passId == kInvalidPassId) {
    return false;
  }
  if (index < overdubSessionCompanionIds_.size()) {
    companionIds = overdubSessionCompanionIds_[index];
  }
  return true;
}

LOOP_COLD_MEM void Loop::collectOverdubSessionUndoPasses(PassIdList& passIds,
                                                        EditPassIdList& companionIds) const {
  passIds.clear();
  companionIds.clear();
  for (size_t i = 0; i < overdubSessionCursor_ && i < overdubSessionPassIds_.size(); ++i) {
    const PassId passId = overdubSessionPassIds_[i];
    if (passId == kInvalidPassId) {
      continue;
    }
    passIds.push_back(passId);
    if (i < overdubSessionCompanionIds_.size()) {
      for (const EditPassId id : overdubSessionCompanionIds_[i]) {
        companionIds.push_back(id);
      }
    }
  }
}

LOOP_COLD_MEM bool Loop::gatherSessionExitReceiptUndoIdentities(size_t ringIndex, PassIdList& passIds,
                                                              EditPassIdList& companionIds) const {
  passIds.clear();
  companionIds.clear();
  // DEC-068: the receipt is this interval's own; it left the shared session at its stop admission, so
  // it is the only source that cannot describe a successor that reentered the session meanwhile.
  const SessionExitSnapshotHandoff* receipt = sessionExitReceiptByRingIndex(ringIndex);
  if (receipt == nullptr) return false;
  passIds.reserve(receipt->passIds.size());
  for (const PassId id : receipt->passIds) {
    if (id != kInvalidPassId) passIds.push_back(id);
  }
  for (const EditPassIdList& row : receipt->companionIds) {
    for (const EditPassId companionId : row) {
      if (companionId != kInvalidEditPassId) companionIds.push_back(companionId);
    }
  }
  return !passIds.empty();
}

LOOP_COLD_MEM uint64_t Loop::requiredSessionUndoCollectionGrains(
    size_t rows, size_t companions, size_t quantum) {
  if (!quantum || rows > 2048 || companions > 2048) return UINT64_MAX;
  // One pass visit, one companion-row boundary per valid pass, each companion,
  // and a final completion visit. Invalid pass rows need only the pass visit.
  return 1 + (uint64_t(rows) * 2 + companions) / quantum;
}

LOOP_COLD_MEM bool Loop::beginCollectOverdubSessionUndoPasses(
    SessionUndoCollection& work, PassIdList& passIds, EditPassIdList& companionIds,
    const SessionExitSnapshotHandoff* receipt) const {
  using Phase = SessionUndoCollection::Phase;
  const bool receiptSourced = receipt != nullptr;
  const size_t limit = receiptSourced
                           ? receipt->passIds.size()
                           : std::min(overdubSessionCursor_, overdubSessionPassIds_.size());
  if ((work.phase >= Phase::Pass && work.phase <= Phase::FinalCompanions) || limit > 2048 ||
      !passIds.empty() || !companionIds.empty() || passIds.capacity() < limit ||
      static_cast<const void*>(&passIds) == static_cast<const void*>(&companionIds)) return false;
  work = {};
  work.owner = this; work.generation = overdubSessionUndoGeneration_;
  work.passIds = &passIds; work.companionIds = &companionIds; work.limit = limit;
  if (receiptSourced) {
    work.receiptPassIds = receipt->passIds.data();
    work.receiptPassIdCount = receipt->passIds.size();
    work.receiptCompanionRows = receipt->companionIds.data();
    work.receiptCompanionRowCount = receipt->companionIds.size();
    work.receiptIdentity = receipt;
  }
  work.phase = Phase::Pass;
  return true;
}

LOOP_COLD_MEM bool Loop::collectedOverdubSessionUndoPassesCurrent(
    const SessionUndoCollection& work) const {
  return work.phase == SessionUndoCollection::Phase::Complete && work.owner == this &&
      (work.receiptIdentity != nullptr || work.generation == overdubSessionUndoGeneration_) &&
      work.passIds && work.companionIds &&
      (!work.finalReceipt || (work.receiptGeneration == preparedCompanionHistory_.preparationGeneration &&
          preparedCompanionHistory_.storageGeneration == canonicalStorageGeneration &&
          preparedCompanionHistory_.revision == playbackRevision && preparedCompanionHistory_.firstId == nextPassId_ &&
          (preparedCompanionHistory_.phase == PreparedCompanionHistory::Phase::Ready ||
           preparedCompanionHistory_.phase == PreparedCompanionHistory::Phase::Committed) &&
          work.receiptCount == preparedCompanionHistory_.companionIds.size())) &&
      work.passIds->size() == work.passCount && work.companionIds->size() == work.companionCount;
}

LOOP_COLD_MEM Loop::SessionUndoCollection::Phase Loop::stepCollectOverdubSessionUndoPasses(
    SessionUndoCollection& work, size_t quantum) const {
  using Phase = SessionUndoCollection::Phase;
  // A call on the wrong owner or with no quantum must not cancel valid work.
  if (!quantum || work.owner != this) return Phase::Denied;
  if (work.phase == Phase::Complete && collectedOverdubSessionUndoPassesCurrent(work)) return Phase::Complete;
  if ((work.phase < Phase::Pass || work.phase > Phase::FinalCompanions) ||
      (work.finalReceipt && (work.receiptGeneration != preparedCompanionHistory_.preparationGeneration ||
          preparedCompanionHistory_.storageGeneration != canonicalStorageGeneration ||
          preparedCompanionHistory_.revision != playbackRevision || preparedCompanionHistory_.firstId != nextPassId_ ||
          (preparedCompanionHistory_.phase != PreparedCompanionHistory::Phase::Ready &&
           preparedCompanionHistory_.phase != PreparedCompanionHistory::Phase::Committed) ||
          work.receiptCount != preparedCompanionHistory_.companionIds.size())) ||
      (work.receiptIdentity == nullptr && work.generation != overdubSessionUndoGeneration_) ||
      !work.passIds || !work.companionIds ||
      work.passIds->size() != work.passCount || work.companionIds->size() != work.companionCount) {
    work.phase = Phase::Denied;
    return work.phase;
  }
  // DEC-068: a deferred interval reads its own session-exit receipt, never the live session lists
  // (a successor may already own those). Both sources are index-aligned row lists.
  const PassId* sessionPassIds =
      work.receiptIdentity != nullptr ? work.receiptPassIds : overdubSessionPassIds_.data();
  const size_t sessionPassIdCount =
      work.receiptIdentity != nullptr ? work.receiptPassIdCount : overdubSessionPassIds_.size();
  const EditPassIdList* sessionCompanionRows =
      work.receiptIdentity != nullptr ? work.receiptCompanionRows : overdubSessionCompanionIds_.data();
  const size_t sessionCompanionRowCount =
      work.receiptIdentity != nullptr ? work.receiptCompanionRowCount
                                      : overdubSessionCompanionIds_.size();
  while (quantum-- && work.phase != Phase::Complete) {
    if (work.phase == Phase::Pass) {
      if (work.row == work.limit) {
        work.phase = work.finalReceipt ? Phase::FinalPass : Phase::Complete;
        continue;
      }
      const PassId id = work.row < sessionPassIdCount ? sessionPassIds[work.row] : kInvalidPassId;
      if (id == kInvalidPassId) { ++work.row; continue; }
      if (work.passCount == work.passIds->capacity()) { work.phase = Phase::Denied; break; }
      work.passIds->push_back(id); ++work.passCount;
      work.companion = 0; work.phase = Phase::Companions;
    } else if (work.phase == Phase::Companions) {
      if (work.row >= sessionCompanionRowCount ||
          work.companion == sessionCompanionRows[work.row].size()) {
        ++work.row; work.phase = Phase::Pass;
      } else {
        if (work.companionCount == 2048 || work.companionCount == work.companionIds->capacity()) {
          work.phase = Phase::Denied; break;
        }
        work.companionIds->push_back(sessionCompanionRows[work.row][work.companion++]);
        ++work.companionCount;
      }
    } else if (work.phase == Phase::FinalPass) {
      if (work.includeFinalPass && work.finalPassId != kInvalidPassId &&
          (work.passIds->empty() || work.passIds->back() != work.finalPassId)) {
        if (work.passCount == 2048 || work.passCount == work.passIds->capacity()) {
          work.phase = Phase::Denied; break;
        }
        work.passIds->push_back(work.finalPassId); ++work.passCount;
      }
      work.phase = Phase::FinalCompanions;
    } else {
      if (work.receiptIndex == work.receiptCount) { work.phase = Phase::Complete; continue; }
      if (work.companionCount == 2048 || work.companionCount == work.companionIds->capacity()) {
        work.phase = Phase::Denied; break;
      }
      work.companionIds->push_back(preparedCompanionHistory_.companionIds[work.receiptIndex++]);
      ++work.companionCount;
    }
  }
  return work.phase;
}

LOOP_COLD_MEM void Loop::dropOverdubSessionRedoTail() {
  if (overdubSessionCursor_ < overdubSessionPassIds_.size()) {
    ++overdubSessionUndoGeneration_;
    overdubSessionPassIds_.resize(overdubSessionCursor_);
    overdubSessionCompanionIds_.resize(overdubSessionCursor_);
  }
  overdubSessionLiveUndoEvents_.clear();
}

LOOP_COLD_MEM bool Loop::canUndoOverdubSession() const {
  if (!hasOverdubSession()) {
    return false;
  }
  if (overdubSessionCursor_ > 0) {
    return true;
  }
  return capture.phase == CapturePhase::Overdub && !capture.store.empty();
}

LOOP_COLD_MEM bool Loop::canRedoOverdubSession() const {
  if (!hasOverdubSession()) {
    return false;
  }
  if (capture.store.empty() && !overdubSessionLiveUndoEvents_.empty()) {
    return true;
  }
  return overdubSessionCursor_ < overdubSessionPassIds_.size();
}

LOOP_COLD_MEM size_t Loop::overdubSessionUndoDepth() const {
  return overdubSessionCursor_;
}

LOOP_COLD_MEM size_t Loop::overdubSessionDisplayDepth() const {
  if (!hasOverdubSession()) {
    return 0;
  }
  return overdubSessionUndoDepth() + 1;
}

LOOP_COLD_MEM size_t Loop::overdubSessionRedoDepth() const {
  size_t depth = overdubSessionPassIds_.size() - overdubSessionCursor_;
  if (capture.store.empty() && !overdubSessionLiveUndoEvents_.empty()) {
    ++depth;
  }
  return depth;
}

LOOP_COLD_MEM bool Loop::undoOverdubSession() {
  if (!hasOverdubSession()) {
    return false;
  }
  if (overdubSessionCursor_ > 0) {
    clearLastOverdubSeal();
    ++overdubSessionUndoGeneration_;
    --overdubSessionCursor_;
    const PassId passId = overdubSessionPassIds_[overdubSessionCursor_];
    disableEditPasses(overdubSessionCompanionIds_[overdubSessionCursor_]);
    const bool ok = setCapturePassState(passId, CapturePassState::Disabled);
    if (ok && hasOverdubSourceView()) {
      rebuildOverdubSourceView(playheadPhaseTick, "undo");
    }
    onOverdubSessionRecordCursorMoved();
    return ok;
  }
  if (capture.phase == CapturePhase::Overdub && !capture.store.empty()) {
    overdubSessionLiveUndoEvents_.clear();
    capture.store.copyEventsTo(overdubSessionLiveUndoEvents_);
    advanceCaptureContentGeneration();
    capture.store.clear();
    captureEventsSortDirty = false;
    rebuildCapturePreviewFromStore(*this);
    ++captureDisplayRevision;
    suppressNextOverdubWrap();
    return true;
  }
  return false;
}

LOOP_COLD_MEM bool Loop::redoOverdubSession() {
  if (!hasOverdubSession()) {
    return false;
  }
  if (overdubSessionCursor_ < overdubSessionPassIds_.size()) {
    clearLastOverdubSeal();
    const PassId passId = overdubSessionPassIds_[overdubSessionCursor_];
    enableEditPasses(overdubSessionCompanionIds_[overdubSessionCursor_]);
    const bool ok = setCapturePassState(passId, CapturePassState::Active);
    if (ok) {
      ++overdubSessionUndoGeneration_;
      ++overdubSessionCursor_;
      if (hasOverdubSourceView()) {
        rebuildOverdubSourceView(playheadPhaseTick, "redo");
      }
      onOverdubSessionRecordCursorMoved();
    }
    return ok;
  }
  if (capture.store.empty() && !overdubSessionLiveUndoEvents_.empty()) {
    advanceCaptureContentGeneration();
    capture.store.loadFromEvents(overdubSessionLiveUndoEvents_);
    overdubSessionLiveUndoEvents_.clear();
    captureEventsSortDirty = false;
    rebuildCapturePreviewFromStore(*this);
    ++captureDisplayRevision;
    return true;
  }
  return false;
}
