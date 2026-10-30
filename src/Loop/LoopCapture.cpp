//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "Loop.h"

#include "Globals.h"
#include "LoopContentResolution.h"
#include "LoopInternal.h"
#include "Logger.h"
#include "LoopPasses.h"
#include "Utils/CaptureIncrementalSanity.h"
#include "Utils/DebugSessionCapture.h"
#include "Utils/RuntimeTimingTelemetry.h"
#include "Utils/Diagnostics.h"
#include "Utils/DisplayWindowUtils.h"
#include "Utils/IntervalProjection.h"
#include "Utils/SnapshotNoteConversion.h"
#include "Utils/LoopMem.h"
#include "Utils/LoopStopFinalize.h"
#include "Utils/NoteUtils.h"
#include "Utils/WindowCoverage.h"
#include "EditApply.h"
#include "PlaybackMergedMidiEvents.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <vector>

// Task 4.3 seam: the production span-cache derive reports its own cost so a fixture can assert that the
// realtime path is the **windowed** resolve and not the whole-pass one. Observation only -- reset/read
// from a test, never consulted by production logic. RAM2 so it cannot move the RAM1 line.
#if defined(__IMXRT1062__)
DMAMEM static uint32_t sOverdubSpanDeriveOps;
DMAMEM static uint8_t sOverdubSpanDeriveMeasuring;
#else
static uint32_t sOverdubSpanDeriveOps;
static uint8_t sOverdubSpanDeriveMeasuring;
#endif

namespace {
void noteOverdubSpanDeriveOps(const ResolutionCostCounters& counters) {
  if (sOverdubSpanDeriveMeasuring != 0) {
    sOverdubSpanDeriveOps = static_cast<uint32_t>(counters.resolutionOperations);
  }
}
}  // namespace

#if defined(PIO_UNIT_TEST_NATIVE)
void Loop::beginOverdubSpanDeriveMeasureForTest() {
  sOverdubSpanDeriveOps = 0;
  sOverdubSpanDeriveMeasuring = 1;
}

uint32_t Loop::overdubSpanDeriveOpsForTest() {
  sOverdubSpanDeriveMeasuring = 0;
  return sOverdubSpanDeriveOps;
}
#endif

bool Loop::hasCommittedPasses() const {
  if (passes.hasRecordPass() && passes.recordPass.state == CapturePassState::Active &&
      !passes.recordPass.committedChunkIds.empty()) {
    return true;
  }
  for (const OverdubPass& pass : passes.overdubPasses) {
    if (pass.state == CapturePassState::Active && !pass.committedChunkIds.empty()) {
      return true;
    }
  }
  return false;
}

uint32_t Loop::findLastCommittedEventTick() const {
  uint32_t lastTick = 0;
  // Use chunk lastTick metadata only — never copyEventsTo into MidiEventVec (InternalHeap).
  // Adopt/reconcile during focus LoadLoopJob Commit while PLAYING otherwise abort()s
  // when RAM1 cannot hold a 256-event scratch (session_20260718_235129: silence after
  // parse edits grain, before apply_us).
  auto scanChunkRefs = [&lastTick](const CommittedChunkIdList& committedChunkIds) {
    for (uint16_t chunkId : committedChunkIds) {
      uint32_t firstTick = 0;
      uint32_t chunkLast = 0;
      if (!LoopEventStore::chunkTickSpan(chunkId, firstTick, chunkLast)) {
        continue;
      }
      (void)firstTick;
      if (chunkLast > lastTick) {
        lastTick = chunkLast;
      }
    }
  };

  if (passes.hasRecordPass() && passes.recordPass.state == CapturePassState::Active &&
      !passes.recordPass.committedChunkIds.empty()) {
    scanChunkRefs(passes.recordPass.committedChunkIds);
  }
  for (const OverdubPass& pass : passes.overdubPasses) {
    if (pass.state == CapturePassState::Active && !pass.committedChunkIds.empty()) {
      scanChunkRefs(pass.committedChunkIds);
    }
  }
  return lastTick;
}

uint32_t Loop::committedBarAlignedContentLengthTicks() const {
  if (!hasCommittedPasses()) {
    return 0;
  }
  const uint32_t lastEventTick = findLastCommittedEventTick();
  if (lastEventTick == 0) {
    return 0;
  }

  constexpr uint32_t ticksPerBar = Config::TICKS_PER_BAR;
  const uint32_t fullBars = lastEventTick / ticksPerBar;
  const uint32_t rem = lastEventTick % ticksPerBar;
  const uint32_t grace = ticksPerBar / 6;
  if (rem <= grace) {
    return (fullBars > 0 ? fullBars : 1) * ticksPerBar;
  }
  if (lastEventTick < ticksPerBar / 2) {
    return ticksPerBar;
  }
  return (fullBars + 1) * ticksPerBar;
}

uint32_t Loop::reconcileLoopLengthWithCommittedPasses(uint32_t candidateLengthTicks) const {
  if (!hasCommittedPasses()) {
    return candidateLengthTicks;
  }
  const uint32_t contentLength = committedBarAlignedContentLengthTicks();
  if (contentLength == 0) {
    return candidateLengthTicks;
  }

  if (candidateLengthTicks == 0 || candidateLengthTicks < contentLength) {
    return contentLength;
  }
  return candidateLengthTicks;
}

void Loop::freeActiveCapturePassChunks() {
  if (passes.hasRecordPass() && passes.recordPass.state == CapturePassState::Active) {
    LoopEventStore::releaseChunkRefs(passes.recordPass.committedChunkIds);
    passes.recordPass.committedChunkIds.clear();
    passes.recordPass.id = kInvalidPassId;
  }
  for (OverdubPass& pass : passes.overdubPasses) {
    if (pass.state != CapturePassState::Active) {
      continue;
    }
    LoopEventStore::releaseChunkRefs(pass.committedChunkIds);
    pass.committedChunkIds.clear();
  }
  passes.overdubPasses.erase(
      std::remove_if(passes.overdubPasses.begin(), passes.overdubPasses.end(),
                     [](const OverdubPass& pass) {
                       return pass.state == CapturePassState::Active;
                     }),
      passes.overdubPasses.end());
}

bool Loop::reclaimDisabledCapturePass(PassId id) {
  if (id == kInvalidPassId) {
    return false;
  }
  if (passes.hasRecordPass() && passes.recordPass.id == id &&
      passes.recordPass.state == CapturePassState::Disabled) {
    LoopEventStore::releaseChunkRefs(passes.recordPass.committedChunkIds);
    passes.recordPass.committedChunkIds.clear();
    passes.recordPass.id = kInvalidPassId;
    ++playbackRevision;
    notifyCommittedContentChanged();
    return true;
  }
  for (auto it = passes.overdubPasses.begin(); it != passes.overdubPasses.end(); ++it) {
    if (it->id != id || it->state != CapturePassState::Disabled) {
      continue;
    }
    LoopEventStore::releaseChunkRefs(it->committedChunkIds);
    passes.overdubPasses.erase(it);
    ++playbackRevision;
    notifyCommittedContentChanged();
    return true;
  }
  return false;
}

uint16_t Loop::reclaimUnreferencedDisabledCapturePasses(const SlotPassReferences& refs) {
  uint16_t reclaimed = 0;
  if (passes.hasRecordPass() && passes.recordPass.state == CapturePassState::Disabled &&
      !refs.referencesCapturePass(passes.recordPass.id)) {
    if (reclaimDisabledCapturePass(passes.recordPass.id)) {
      ++reclaimed;
    }
  }
  for (size_t i = passes.overdubPasses.size(); i > 0; --i) {
    const OverdubPass& pass = passes.overdubPasses[i - 1];
    if (pass.state == CapturePassState::Disabled && !refs.referencesCapturePass(pass.id)) {
      if (reclaimDisabledCapturePass(pass.id)) {
        ++reclaimed;
      }
    }
  }
  return reclaimed;
}

bool Loop::setCapturePassState(PassId id, CapturePassState state) {
  // Disabling a pass removes its rows from the committed representation, so the sealed capture
  // handoff can never be proven covered again (`sealedCapturePreviewIsCovered` reads the committed
  // cache) and `releaseSealedCapturePreviewIfCovered` would keep it forever. That is the same
  // hazard `resetPassTimeline` names; here it is reached from an undo/redo of a record or overdub
  // entry, which left the removed overdub painted on the OLED (`content-verify-65`) because
  // `applyDrawSourceOverlays` unions the uncovered sealed rows into every frame. Drop the handoff
  // with the pass state; the pass refresh rebuilds the committed representation from `passes`.
  if (passes.hasRecordPass() && passes.recordPass.id == id) {
    if (passes.recordPass.state == state) {
      return true;
    }
    passes.recordPass.state = state;
    LoopContentResolution::setPreparedCapturePassState(id, state);
    ++playbackRevision;
    LoopContentResolution::restampPreparedPlaybackRevision(playbackRevision);
    if (state == CapturePassState::Disabled) {
      clearSealedCapturePreview();
      clearRetiredSealedDisplayNotes();
    }
    notifyCommittedContentChanged();
    return true;
  }
  for (OverdubPass& pass : passes.overdubPasses) {
    if (pass.id != id) {
      continue;
    }
    if (pass.state == state) {
      return true;
    }
    pass.state = state;
    LoopContentResolution::setPreparedCapturePassState(id, state);
    ++playbackRevision;
    LoopContentResolution::restampPreparedPlaybackRevision(playbackRevision);
    if (state == CapturePassState::Disabled) {
      clearSealedCapturePreview();
      clearRetiredSealedDisplayNotes();
    }
    notifyCommittedContentChanged();
    return true;
  }
  return false;
}

size_t Loop::activeCapturePassCount() const {
  size_t count = 0;
  if (passes.hasRecordPass() && passes.recordPass.state == CapturePassState::Active) {
    ++count;
  }
  for (const OverdubPass& pass : passes.overdubPasses) {
    if (pass.state == CapturePassState::Active) {
      ++count;
    }
  }
  return count;
}

LOOP_COLD_MEM void Loop::assignMissingNoteIdsInStore(LoopEventStore& store) {
  bool invalidated = false;
  store.assignMissingNoteIdsToNoteOns([this, &store, &invalidated]() {
    if (&store == &capture.store && !invalidated) {
      advanceCaptureContentGeneration();
      invalidated = true;
    }
    return allocateNoteId();
  });
}

LOOP_COLD_MEM void Loop::assignMissingNoteIdsInStoreWindow(LoopEventStore& store, size_t beginIndex,
                                             uint32_t includeUntil) {
  bool invalidated = false;
  store.assignMissingNoteIdsToNoteOnsInTickWindow(beginIndex, includeUntil,
      [this, &store, &invalidated]() {
        if (&store == &capture.store && !invalidated) {
          advanceCaptureContentGeneration();
          invalidated = true;
        }
        return allocateNoteId();
      });
}

namespace {

struct AssignCommittedPassNoteIdCtx {
  Loop* loop;
  uint32_t* assigned;
};

NoteId assignCommittedPassNoteId(void* ctx) {
  auto* assignCtx = static_cast<AssignCommittedPassNoteIdCtx*>(ctx);
  ++*assignCtx->assigned;
  return assignCtx->loop->allocateNoteId();
}

struct OverdubSealTraceCtx {
  bool enabled = false;
  uint32_t timelineStartUs = 0;
  size_t eventCount = 0;
  size_t chunkRefCount = 0;
};

LOOP_COLD_MEM OverdubSealTraceCtx beginOverdubSealTrace(const Loop& loop, CommitReason reason) {
  OverdubSealTraceCtx ctx{};
#if defined(SESSION_CAPTURE)
  ctx.enabled = reason == CommitReason::OverdubStop || reason == CommitReason::OverdubStopToStopped;
  if (!ctx.enabled) {
    return ctx;
  }
  ctx.timelineStartUs = traceMicros();
  ctx.eventCount = stopPathEventCount(loop);
  ctx.chunkRefCount = stopPathChunkRefCount(loop);
#else
  (void)loop;
  (void)reason;
#endif
  return ctx;
}

LOOP_COLD_MEM void emitOverdubSealTraceStage(const OverdubSealTraceCtx& ctx, const char* stage,
                                             uint32_t durationUs, const char* outcome) {
#if defined(SESSION_CAPTURE)
  if (!ctx.enabled) {
    return;
  }
  const uint32_t elapsedUs = traceMicros() - ctx.timelineStartUs;
  SC_ODUB_STOP_STAGE(stage, elapsedUs, durationUs, DebugSessionCapture::kUnsampledHeapBytes,
                     DebugSessionCapture::kUnsampledHeapBytes, ctx.eventCount,
                     ctx.chunkRefCount, outcome);
#else
  (void)ctx;
  (void)stage;
  (void)durationUs;
  (void)outcome;
#endif
}

struct OverdubPublishTraceCtx {
  bool enabled = false;
  uint32_t timelineStartUs = 0;
  size_t eventCount = 0;
  size_t chunkRefCount = 0;
};

LOOP_COLD_MEM OverdubPublishTraceCtx beginOverdubPublishTrace(const Loop& loop,
                                                              CommitReason reason) {
  OverdubPublishTraceCtx ctx{};
#if defined(SESSION_CAPTURE)
  ctx.enabled = reason == CommitReason::OverdubStop ||
                reason == CommitReason::OverdubStopToStopped;
  if (!ctx.enabled) {
    return ctx;
  }
  ctx.timelineStartUs = traceMicros();
  ctx.eventCount = stopPathEventCount(loop);
  ctx.chunkRefCount = stopPathChunkRefCount(loop);
#else
  (void)loop;
  (void)reason;
#endif
  return ctx;
}

LOOP_COLD_MEM void emitOverdubPublishTraceStage(const OverdubPublishTraceCtx& ctx,
                                                const char* stage, uint32_t durationUs,
                                                const char* outcome) {
#if defined(SESSION_CAPTURE)
  if (!ctx.enabled) {
    return;
  }
  const uint32_t elapsedUs = traceMicros() - ctx.timelineStartUs;
  SC_ODUB_STOP_STAGE(stage, elapsedUs, durationUs, DebugSessionCapture::kUnsampledHeapBytes,
                     DebugSessionCapture::kUnsampledHeapBytes, ctx.eventCount,
                     ctx.chunkRefCount, outcome);
#else
  (void)ctx;
  (void)stage;
  (void)durationUs;
  (void)outcome;
#endif
}

constexpr size_t kMaxEventsForSpanCacheRestamp = 2048;
constexpr uint32_t kWrapPairWindowTicks = 768;

LOOP_COLD_MEM bool hasLaterWrappedHeadTailPairByScan(const SessionMidiEventVec& events,
                                                      const MidiEvent& noteOffEvent,
                                                      uint32_t loopLengthTicks) {
  const uint8_t pitch = noteOffEvent.data.noteData.note;
  for (const MidiEvent& later : events) {
    if (!later.isNoteOn() || later.channel != noteOffEvent.channel ||
        later.data.noteData.note != pitch) {
      continue;
    }
    if (later.tick <= noteOffEvent.tick) {
      continue;
    }
    if (NoteUtils::isHeadTailWrappedPair(later.tick, noteOffEvent.tick, loopLengthTicks)) {
      return true;
    }
  }
  return false;
}

LOOP_COLD_MEM bool capturePassContainsWrappedHeadTailPairs(const SessionMidiEventVec& events,
                                                            uint32_t loopLengthTicks) {
  if (loopLengthTicks == 0 || events.empty()) {
    return false;
  }
  const uint32_t wrapWindowTicks =
      kWrapPairWindowTicks > loopLengthTicks ? loopLengthTicks : kWrapPairWindowTicks;
  const uint32_t tailStart =
      loopLengthTicks > wrapWindowTicks ? loopLengthTicks - wrapWindowTicks : 0;
  if (tailStart > 0) {
    std::vector<uint16_t> tailNoteOnKeys;
    tailNoteOnKeys.reserve(events.size());
    for (const MidiEvent& evt : events) {
      if (!evt.isNoteOn() || evt.tick < tailStart || evt.tick >= loopLengthTicks) {
        continue;
      }
      const uint16_t key =
          static_cast<uint16_t>((static_cast<uint16_t>(evt.channel) << 8u) |
                                static_cast<uint16_t>(evt.data.noteData.note));
      tailNoteOnKeys.push_back(key);
    }
    if (tailNoteOnKeys.empty()) {
      return false;
    }
    std::sort(tailNoteOnKeys.begin(), tailNoteOnKeys.end());
    tailNoteOnKeys.erase(std::unique(tailNoteOnKeys.begin(), tailNoteOnKeys.end()),
                         tailNoteOnKeys.end());
    for (const MidiEvent& evt : events) {
      if (!evt.isNoteOff() || evt.tick >= wrapWindowTicks) {
        continue;
      }
      const uint16_t key =
          static_cast<uint16_t>((static_cast<uint16_t>(evt.channel) << 8u) |
                                static_cast<uint16_t>(evt.data.noteData.note));
      if (std::binary_search(tailNoteOnKeys.begin(), tailNoteOnKeys.end(), key)) {
        return true;
      }
    }
    return false;
  }
  for (const MidiEvent& evt : events) {
    if (!evt.isNoteOff()) {
      continue;
    }
    if (hasLaterWrappedHeadTailPairByScan(events, evt, loopLengthTicks)) {
      return true;
    }
  }
  return false;
}

LOOP_COLD_MEM void collectSortedSpanCacheNoteIds(const NoteUtils::DisplayNoteVec& notes,
                                                 std::vector<NoteId>& out) {
  out.clear();
  out.reserve(notes.size());
  for (const NoteUtils::DisplayNote& note : notes) {
    if (note.noteId != kInvalidNoteId) {
      out.push_back(note.noteId);
    }
  }
  std::sort(out.begin(), out.end());
  out.erase(std::unique(out.begin(), out.end()), out.end());
}

LOOP_COLD_MEM bool insertSortedUniqueNoteId(std::vector<NoteId>& noteIds, NoteId noteId) {
  const auto it = std::lower_bound(noteIds.begin(), noteIds.end(), noteId);
  if (it != noteIds.end() && *it == noteId) {
    return false;
  }
  noteIds.insert(it, noteId);
  return true;
}

}  // namespace

LOOP_COLD_MEM void Loop::assignMissingNoteIdsInCommittedCapturePasses() {
  if (!hasCommittedPasses()) {
    return;
  }
  uint32_t assigned = 0;
  AssignCommittedPassNoteIdCtx assignCtx{this, &assigned};
  if (passes.hasRecordPass() && passes.recordPass.state == CapturePassState::Active &&
      !passes.recordPass.committedChunkIds.empty()) {
    const CommittedChunkIdList& chunkIds = passes.recordPass.committedChunkIds;
    LoopEventStore::assignMissingNoteIdsToNoteOnsInChunkIds(
        chunkIds.data(), chunkIds.size(), assignCommittedPassNoteId, &assignCtx);
  }
  for (OverdubPass& pass : passes.overdubPasses) {
    if (pass.state != CapturePassState::Active || pass.committedChunkIds.empty()) {
      continue;
    }
    const CommittedChunkIdList& chunkIds = pass.committedChunkIds;
    LoopEventStore::assignMissingNoteIdsToNoteOnsInChunkIds(
        chunkIds.data(), chunkIds.size(), assignCommittedPassNoteId, &assignCtx);
  }
  if (assigned > 0) {
    logger.log(CAT_TRACK, LOG_WARNING,
               "assignMissingNoteIdsInCommittedCapturePasses: assigned %lu",
               static_cast<unsigned long>(assigned));
  }
}

LOOP_COLD_MEM void Loop::beginCommittedNoteIdPreparation(
    CommittedNoteIdPreparation& cursor) const {
  cursor.discard();
  cursor.loop_ = this;
  cursor.loopId_ = loopId;
  cursor.canonicalGeneration_ = canonicalStorageGeneration;
  cursor.captureGeneration_ = captureContentGeneration();
  cursor.revision_ = playbackRevision;
  cursor.recordId_ = passes.recordPass.id;
  cursor.overdubCount_ = passes.overdubPasses.size();
  cursor.result_ = CommittedNoteIdPreparationResult::Progress;
}

LOOP_COLD_MEM bool Loop::committedNoteIdPreparationSourceMatches(
    const CommittedNoteIdPreparation& cursor) const {
  return cursor.loop_ == this && cursor.loopId_ == loopId &&
      cursor.canonicalGeneration_ == canonicalStorageGeneration &&
      cursor.captureGeneration_ == captureContentGeneration() &&
      cursor.revision_ == playbackRevision && cursor.recordId_ == passes.recordPass.id &&
      cursor.overdubCount_ == passes.overdubPasses.size();
}

LOOP_COLD_MEM bool Loop::committedNoteIdPreparationComplete(
    const CommittedNoteIdPreparation& cursor) const {
  return cursor.result_ == CommittedNoteIdPreparationResult::Complete &&
      committedNoteIdPreparationSourceMatches(cursor);
}

LOOP_COLD_MEM Loop::CommittedNoteIdPreparationResult Loop::stepCommittedNoteIdPreparation(
    CommittedNoteIdPreparation& cursor) {
  using Result = CommittedNoteIdPreparationResult;
  if (cursor.result_ == Result::Idle || cursor.result_ == Result::Superseded ||
      cursor.result_ == Result::Fault) return cursor.result_;
  // Validate before reacquiring a directory, including completed receipts.
  if (!committedNoteIdPreparationSourceMatches(cursor)) {
    cursor.result_ = Result::Superseded;
    return cursor.result_;
  }
  if (cursor.result_ == Result::Complete) return cursor.result_;
  const CommittedChunkIdList* refs = nullptr;
  if (cursor.pass_ == 0) {
    if (passes.hasRecordPass() && passes.recordPass.state == CapturePassState::Active)
      refs = &passes.recordPass.committedChunkIds;
  } else if (cursor.pass_ <= cursor.overdubCount_) {
    const OverdubPass& pass = passes.overdubPasses[cursor.pass_ - 1];
    if (pass.state == CapturePassState::Active) refs = &pass.committedChunkIds;
  } else {
    cursor.result_ = Result::Complete;
    return cursor.result_;
  }
  // Empty/disabled passes and directory ends consume their own grant.
  if (!refs || cursor.chunk_ >= refs->size()) {
    ++cursor.pass_;
    cursor.chunk_ = 0;
    return Result::Progress;
  }
  const uint16_t id = (*refs)[cursor.chunk_];
  MidiEvent probe;
  bool atEnd = false;
  if (LoopEventStore::chunkUsedCount(id) > LoopEventStoreConfig::CHUNK_CAPACITY ||
      !LoopEventStore::readSealedChunkEvent(id, 0, probe, atEnd)) {
    cursor.result_ = Result::Fault;
    return cursor.result_;
  }
  // Existing in-place canonical identity policy, now one fixed-cap chunk.
  AssignCommittedPassNoteIdCtx assignCtx{this, &cursor.assignedIds_};
  LoopEventStore::assignMissingNoteIdsToNoteOnsInChunkIds(
      &id, 1, assignCommittedPassNoteId, &assignCtx);
  ++cursor.chunk_;
  ++cursor.inspectedChunks_;
  return Result::Progress;
}

void Loop::shiftActiveCapturePassTicks(int64_t delta) {
  if (delta == 0 || !hasCommittedPasses()) {
    return;
  }
  if (passes.hasRecordPass() && passes.recordPass.state == CapturePassState::Active &&
      !passes.recordPass.committedChunkIds.empty()) {
    MidiEventVec flat;
    LoopEventStore::appendChunkRefEvents(passes.recordPass.committedChunkIds, flat);
    if (!flat.empty()) {
      LoopEventStore staging;
      staging.loadFromEvents(flat);
      staging.shiftAllTicks(delta);
      LoopEventStore temp;
      temp.adoptAll(staging);
      LoopEventStore::releaseChunkRefs(passes.recordPass.committedChunkIds);
      passes.recordPass.committedChunkIds.clear();
      CaptureChunkIdList captureIds;
      temp.detachChunksTo(captureIds);
      LoopEventStore::transferCaptureChunkIdsToCommittedChunkIds(passes.recordPass.committedChunkIds,
                                                         captureIds);
    }
  }
  for (OverdubPass& pass : passes.overdubPasses) {
    if (pass.state != CapturePassState::Active || pass.committedChunkIds.empty()) {
      continue;
    }
    MidiEventVec flat;
    LoopEventStore::appendChunkRefEvents(pass.committedChunkIds, flat);
    if (flat.empty()) {
      continue;
    }
    LoopEventStore staging;
    staging.loadFromEvents(flat);
    staging.shiftAllTicks(delta);
    LoopEventStore temp;
    temp.adoptAll(staging);
    LoopEventStore::releaseChunkRefs(pass.committedChunkIds);
    pass.committedChunkIds.clear();
    CaptureChunkIdList captureIds;
    temp.detachChunksTo(captureIds);
    LoopEventStore::transferCaptureChunkIdsToCommittedChunkIds(pass.committedChunkIds, captureIds);
  }
  for (EditPass& editPass : passes.editPasses) {
    if (editPass.state != EditPassState::Active) {
      continue;
    }
    if (editPass.passType != EditPassType::Note) {
      continue;
    }
    editPass.startTick =
        static_cast<uint32_t>(static_cast<int64_t>(editPass.startTick) + delta);
    editPass.endTick =
        static_cast<uint32_t>(static_cast<int64_t>(editPass.endTick) + delta);
    for (MidiEvent& evt : editPass.addedEvents) {
      evt.tick = static_cast<uint32_t>(static_cast<int64_t>(evt.tick) + delta);
    }
  }
  ++playbackRevision;
  notifyCommittedContentChanged();
}

uint32_t Loop::overdubSourceWindowLengthTicks() const {
  return kOverdubSourceWindowBars * Config::TICKS_PER_BAR;
}

void Loop::resolveEffectiveDisplayNotesInWindow(uint32_t requestedStart, uint32_t requestedLength,
                                                NoteUtils::DisplayNoteVec& out) const {
  uint32_t deriveStart = 0;
  uint32_t deriveLength = 0;
  resolvePaddedOverdubSourceWindow(requestedStart, requestedLength, deriveStart, deriveLength);
  LoopContentResolution::resolveDisplayNotesInWindow(passes, loopLengthTicks, deriveStart,
                                                     deriveLength, out);
}

void Loop::resolvePaddedOverdubSourceWindow(uint32_t requestedStart, uint32_t requestedLength,
                                            uint32_t& deriveStart, uint32_t& deriveLength) const {
  const uint32_t loopLen = loopLengthTicks;
  if (loopLen == 0) {
    deriveStart = 0;
    deriveLength = 0;
    return;
  }
  // A whole-loop request is already the whole loop: no pad can widen it.
  if (requestedLength == 0 || requestedLength >= loopLen) {
    deriveStart = 0;
    deriveLength = loopLen;
    return;
  }
  const uint32_t pad = Config::TICKS_PER_BAR;
  const uint32_t padded = requestedLength + 2 * pad;
  if (padded >= loopLen) {
    deriveStart = 0;
    deriveLength = loopLen;
    return;
  }
  // Loop-aware: the pad wraps rather than clamping at 0, so a window near either edge can read the
  // fragment on the other side of the wrap.
  deriveStart = (requestedStart + loopLen - (pad % loopLen)) % loopLen;
  deriveLength = padded;
}

void Loop::resolveOverdubSourceWindow(uint32_t centerPhaseTick, uint32_t& windowStart,
                                      uint32_t& windowLength) const {
  const uint32_t loopLen = loopLengthTicks;
  windowLength = overdubSourceWindowLengthTicks();
  if (loopLen == 0 || windowLength >= loopLen) {
    windowStart = 0;
    windowLength = loopLen;
    return;
  }
  windowStart =
      DisplayWindowUtils::resolveCenteredWindowStart(centerPhaseTick, windowLength, loopLen);
}

LOOP_COLD_MEM void Loop::recordLastOverdubSeal(PassId passId, const EditPassIdList& companionIds,
                                               uint32_t priorPlaybackRevision) {
  if (passId == kInvalidPassId) {
    clearLastOverdubSeal();
    return;
  }
  hasLastOverdubSeal_ = true;
  lastOverdubSealPassId_ = passId;
  lastOverdubSealPriorRevision_ = priorPlaybackRevision;
  lastOverdubSealPlaybackRevision_ = playbackRevision;
  lastOverdubSealCompanionIds_ = companionIds;
  EditPassIdList overlayIds;
  overlayIds.reserve(sealedCompanionOverlayIds_.size() + companionIds.size());
  for (const EditPassId id : sealedCompanionOverlayIds_) {
    bool active = false;
    for (const EditPass& row : passes.editPasses) {
      if (row.id == id && row.state == EditPassState::Active) {
        active = true;
        break;
      }
    }
    if (active) {
      overlayIds.push_back(id);
    }
  }
  for (const EditPassId id : companionIds) {
    bool already = false;
    for (const EditPassId existing : overlayIds) {
      if (existing == id) {
        already = true;
        break;
      }
    }
    if (!already) {
      overlayIds.push_back(id);
    }
  }
  sealedCompanionOverlayIds_.swap(overlayIds);
  stampWrapSnapshotJournalRevision(passId, playbackRevision);
}

LOOP_COLD_MEM void Loop::clearLastOverdubSealAdmitReceipt() {
  hasLastOverdubSeal_ = false;
  lastOverdubSealPassId_ = kInvalidPassId;
  lastOverdubSealPriorRevision_ = 0;
}

LOOP_COLD_MEM void Loop::clearLastOverdubSeal() {
  clearLastOverdubSealAdmitReceipt();
  lastOverdubSealPlaybackRevision_ = 0;
  lastOverdubSealCompanionIds_.clear();
  sealedCompanionOverlayIds_.clear();
}

namespace {

bool mergedStreamHasNoteOnId(const SessionMidiEventVec& events, NoteId noteId) {
  if (noteId == kInvalidNoteId) {
    return false;
  }
  for (const MidiEvent& evt : events) {
    if (evt.isNoteOn() && evt.noteId == noteId) {
      return true;
    }
  }
  return false;
}

bool mergedStreamContainsEvent(const SessionMidiEventVec& events, const MidiEvent& candidate) {
  // Non-note rows have no NoteId. Coincident controllers (including distinct
  // values for the same CC) are separate events, not identity duplicates.
  // The revision-qualified seal receipt already prevents admitting a pass twice.
  if (!candidate.isNoteOn() && !candidate.isNoteOff()) return false;
  for (const MidiEvent& existing : events) {
    if (candidate.noteId != kInvalidNoteId) {
      if (existing.noteId == candidate.noteId && existing.isNoteOn() == candidate.isNoteOn() &&
          existing.isNoteOff() == candidate.isNoteOff()) {
        return true;
      }
      continue;
    }
    if (existing.tick == candidate.tick && existing.type == candidate.type &&
        existing.channel == candidate.channel && existing.isNoteOn() == candidate.isNoteOn() &&
        existing.isNoteOff() == candidate.isNoteOff()) {
      if (candidate.isNoteOn() || candidate.isNoteOff()) {
        if (existing.data.noteData.note == candidate.data.noteData.note) {
          return true;
        }
      } else {
        return true;
      }
    }
  }
  return false;
}

}  // namespace

LOOP_COLD_MEM bool Loop::tryAdmitLastOverdubSealIntoMergedEvents(PlaybackMergedMidiEvents& merged) {
  if (!hasLastOverdubSeal_) {
    return false;
  }
  const bool geometryOk = !merged.empty() &&
                          isFullLoopMergedPlaybackWindow(merged, loopLengthTicks) &&
                          merged.builtFromRevision == lastOverdubSealPriorRevision_ &&
                          lastOverdubSealPlaybackRevision_ == playbackRevision &&
                          lastOverdubSealPassId_ == lastCommittedPassId_ &&
                          lastOverdubSealPassId_ != kInvalidPassId;
  if (!geometryOk) {
    clearLastOverdubSealAdmitReceipt();
    return false;
  }
  const OverdubPass* sealPass = nullptr;
  for (const OverdubPass& pass : passes.overdubPasses) {
    if (pass.id == lastOverdubSealPassId_) {
      sealPass = &pass;
      break;
    }
  }
  if (sealPass == nullptr || sealPass->state != CapturePassState::Active) {
    clearLastOverdubSealAdmitReceipt();
    return false;
  }
  EditPassVec companionRows;
  companionRows.reserve(lastOverdubSealCompanionIds_.size());
  for (const EditPassId companionId : lastOverdubSealCompanionIds_) {
    const EditPass* row = nullptr;
    for (const EditPass& editPass : passes.editPasses) {
      if (editPass.id == companionId) {
        row = &editPass;
        break;
      }
    }
    if (row == nullptr || row->state != EditPassState::Active) {
      clearLastOverdubSealAdmitReceipt();
      return false;
    }
    companionRows.push_back(*row);
  }
  SessionMidiEventVec passEvents;
  LoopEventStore::appendChunkRefEvents(sealPass->committedChunkIds, passEvents);
  for (const EditPass& row : companionRows) {
    if (row.actionType != EditActionType::Delete && row.actionType != EditActionType::Update) {
      continue;
    }
    if (!mergedStreamHasNoteOnId(merged.mergedEvents, row.targetNoteId) &&
        !mergedStreamHasNoteOnId(passEvents, row.targetNoteId)) {
      clearLastOverdubSealAdmitReceipt();
      return false;
    }
  }
  for (const MidiEvent& evt : passEvents) {
    if (!mergedStreamContainsEvent(merged.mergedEvents, evt)) {
      merged.mergedEvents.push_back(evt);
    }
  }
  if (!companionRows.empty()) {
    applyNoteEditPassSequence(merged.mergedEvents, companionRows, loopLengthTicks);
  }
  merged.builtFromRevision = playbackRevision;
  merged.builtFromSnapshotIdentity = nullptr;
  clearLastOverdubSealAdmitReceipt();
  return true;
}

namespace {

/// Reconstruct/project emits wrap-held as split head+tail with the same NoteId.
/// Source-view occupy needs one sounding span (`end < start`) so
/// `displayNotePresentAtHold` can unwrap. Same-id merge would otherwise keep
/// only the tail and miss hs=0.
LOOP_COLD_MEM void collapseSplitHeadTailToWrapHeld(NoteUtils::DisplayNoteVec& notes,
                                                   uint32_t loopLength) {
  if (loopLength < 2 || notes.size() < 2) {
    return;
  }
  const uint32_t tailStart = NoteUtils::wrapTailStartTick(loopLength);
  const uint32_t loopEnd = loopLength - 1;
  std::vector<char> used(notes.size(), 0);
  NoteUtils::DisplayNoteVec collapsed;
  collapsed.reserve(notes.size());
  for (size_t i = 0; i < notes.size(); ++i) {
    if (used[i] != 0 || notes[i].noteId == kInvalidNoteId) {
      continue;
    }
    const NoteUtils::DisplayNote& a = notes[i];
    int partner = -1;
    for (size_t j = i + 1; j < notes.size(); ++j) {
      if (used[j] != 0 || notes[j].noteId != a.noteId) {
        continue;
      }
      const NoteUtils::DisplayNote& b = notes[j];
      const bool aTail = a.startTick >= tailStart && a.endTick == loopEnd;
      const bool bTail = b.startTick >= tailStart && b.endTick == loopEnd;
      const bool aHead = a.endTick > a.startTick && a.startTick < tailStart;
      const bool bHead = b.endTick > b.startTick && b.startTick < tailStart;
      if ((aTail && bHead) || (bTail && aHead)) {
        partner = static_cast<int>(j);
        break;
      }
    }
    if (partner < 0) {
      continue;
    }
    used[i] = 1;
    used[static_cast<size_t>(partner)] = 1;
    const NoteUtils::DisplayNote& b = notes[static_cast<size_t>(partner)];
    const NoteUtils::DisplayNote& tail = (a.startTick >= tailStart) ? a : b;
    const NoteUtils::DisplayNote& head = (a.startTick >= tailStart) ? b : a;
    NoteUtils::DisplayNote wrapHeld = tail;
    wrapHeld.endTick = head.endTick;
    collapsed.push_back(wrapHeld);
  }
  for (size_t i = 0; i < notes.size(); ++i) {
    if (used[i] == 0) {
      collapsed.push_back(notes[i]);
    }
  }
  notes = std::move(collapsed);
}

LOOP_COLD_MEM void mergeDisplayNotesByNoteId(NoteUtils::DisplayNoteVec& dest,
                                            const NoteUtils::DisplayNoteVec& candidates) {
  for (const NoteUtils::DisplayNote& candidate : candidates) {
    if (candidate.noteId == kInvalidNoteId) {
      continue;
    }
    bool found = false;
    for (const NoteUtils::DisplayNote& existing : dest) {
      if (existing.noteId == candidate.noteId) {
        found = true;
        break;
      }
    }
    if (!found) {
      dest.push_back(candidate);
    }
  }
}

bool gatherSnapshotFirstCommittedEvents(const Loop& loop, SessionMidiEventVec& out) {
  const void* observedIdentity = nullptr;
  return loop.tryCopyCurrentSnapshotEditViewEvents(out, observedIdentity);
}

bool gatherConstructionCommittedEvents(const Loop& loop, SessionMidiEventVec& out) {
  out.clear();
  if (gatherSnapshotFirstCommittedEvents(loop, out)) {
    return true;
  }
  // Source-cache construction is a cold, full-loop operation. When no current
  // snapshot exists, use the explicit pass-layer oracle so a terminal untagged
  // NoteOff cannot make its valid NoteOn identity disappear from overlap
  // resolution.
  if (loop.loopLengthTicks == 0) {
    return false;
  }
  loop.copyEffectiveCommittedEventsInRange(out, 0, loop.loopLengthTicks, true);
  return !out.empty();
}

LOOP_COLD_MEM bool displayNotesContainNoteId(const NoteUtils::DisplayNoteVec& notes, NoteId noteId) {
  if (noteId == kInvalidNoteId) {
    return false;
  }
  for (const NoteUtils::DisplayNote& note : notes) {
    if (note.noteId == noteId) {
      return true;
    }
  }
  return false;
}

/// Reconstruct (`finishOpenNotes=false`) defers an untagged Off at `L-1` for a
/// tail-window On (`174151` id=112 Off@5375). That is a projection miss, not
/// committed-content absence. Pair with the same untagged-Off / LIFO owner as
/// playback (`ActiveNoteLedger::applyPlaybackEvent`) and add the omitted row.
LOOP_COLD_MEM void mergeOmittedCommittedNoteOnIdentities(NoteUtils::DisplayNoteVec& dest,
                                                         const SessionMidiEventVec& events) {
  ActiveNoteLedger pairing;
  for (const MidiEvent& evt : events) {
    if (evt.isNoteOn()) {
      pairing.applyPlaybackEvent(evt.channel, evt);
      continue;
    }
    if (!evt.isNoteOff()) {
      continue;
    }
    const uint8_t pitch = evt.data.noteData.note;
    NoteId closedId = kInvalidNoteId;
    uint32_t startTick = 0;
    uint8_t velocity = 0;
    pairing.forEachActive([&](uint8_t channel, uint8_t note, const ActiveNoteLedger::Entry& entry) {
      if (channel != evt.channel || note != pitch) {
        return;
      }
      if (evt.noteId != kInvalidNoteId && entry.noteId != evt.noteId) {
        return;
      }
      closedId = entry.noteId;
      startTick = entry.startTick;
      velocity = entry.velocity;
    });
    if (!pairing.applyPlaybackEvent(evt.channel, evt)) {
      continue;
    }
    if (closedId == kInvalidNoteId || displayNotesContainNoteId(dest, closedId)) {
      continue;
    }
    NoteUtils::DisplayNote note{};
    note.noteId = closedId;
    note.note = pitch;
    note.velocity = velocity;
    note.startTick = startTick;
    note.endTick = evt.tick;
    dest.push_back(note);
  }
}

}  // namespace

LOOP_COLD_MEM void Loop::seedOverdubCoverageClosureFromCommittedContent() {
  if (loopLengthTicks == 0) {
    return;
  }
  // The minimum pad is one tile: it covers an ordinary note that starts inside one tile and ends inside
  // the next, so a session always has a usable forward pad even before it learns the real maximum.
  advanceOverdubCoverageClosure(CoveragePrerequisite::tileLengthTicks());
  SessionMidiEventVec events;
  if (!gatherConstructionCommittedEvents(*this, events) || events.empty()) {
    return;
  }
  ActiveNoteLedger pairing;
  uint32_t maxSpan = 0;
  for (const MidiEvent& evt : events) {
    if (evt.isNoteOn()) {
      pairing.applyPlaybackEvent(evt.channel, evt);
      continue;
    }
    if (!evt.isNoteOff()) {
      continue;
    }
    uint32_t startTick = 0;
    pairing.forEachActive([&](uint8_t channel, uint8_t note, const ActiveNoteLedger::Entry& entry) {
      if (channel != evt.channel || note != evt.data.noteData.note) {
        return;
      }
      if (evt.noteId != kInvalidNoteId && entry.noteId != evt.noteId) {
        return;
      }
      startTick = entry.startTick;
    });
    if (!pairing.applyPlaybackEvent(evt.channel, evt)) {
      continue;
    }
    const uint32_t span =
        evt.tick >= startTick ? (evt.tick - startTick) : (loopLengthTicks - startTick + evt.tick);
    if (span > maxSpan) {
      maxSpan = span;
    }
  }
  advanceOverdubCoverageClosure(maxSpan);
}

LOOP_COLD_MEM void Loop::mergeOmittedCommittedNoteOnIdentitiesInto(
    NoteUtils::DisplayNoteVec& dest) const {
  if (loopLengthTicks == 0) {
    return;
  }
  SessionMidiEventVec committedEvents;
  if (!gatherConstructionCommittedEvents(*this, committedEvents)) {
    return;
  }
  mergeOmittedCommittedNoteOnIdentities(dest, committedEvents);
  // Raw-event merge can reintroduce sealed Delete targets (`202713`).
  applyPendingNoteChangesToDisplayNotes(dest, dest.size());
}

LOOP_COLD_MEM void Loop::mergeOmittedCommittedNoteOnIdentitiesForNotes(
    NoteUtils::DisplayNoteVec& dest, const SessionMidiEventVec& events) const {
  if (loopLengthTicks == 0 || events.empty()) {
    return;
  }
  mergeOmittedCommittedNoteOnIdentities(dest, events);
}

LOOP_COLD_MEM void Loop::appendLastCommittedOverdubPassToSourceView() {
  if (lastCommittedPassId_ == kInvalidPassId || loopLengthTicks == 0) {
    return;
  }
  // Tasks 4.7/4.8: this used to be a **second writer** of the session source -- it merged the just
  // committed pass into the view *and* restamped the span cache whole-loop, beside the worker that is now
  // the only writer. The commit already invalidated the generation, so the worker republishes the
  // required tiles, content and bit together. What this edge still owes is closure: the pass's own note
  // spans widen the tile lookup pad so a long note that starts outside a touched tile is still paired.
  for (const OverdubPass& pass : passes.overdubPasses) {
    if (pass.id != lastCommittedPassId_ || pass.committedChunkIds.empty()) {
      continue;
    }
    SessionMidiEventVec passEvents;
    LoopEventStore::appendChunkRefEvents(pass.committedChunkIds, passEvents);
    if (passEvents.empty()) {
      return;
    }
    NoteUtils::DisplayNoteVec passNotes =
        NoteUtils::reconstructDisplayNotes(passEvents, loopLengthTicks, false, false, true);
    for (const NoteUtils::DisplayNote& note : passNotes) {
      advanceOverdubCoverageClosure(overdubSourceSpanOfNote(note));
    }
    return;
  }
}

LOOP_COLD_MEM void Loop::mergeDisplayNotesIntoOverdubSourceView(
    const NoteUtils::DisplayNoteVec& candidates) {
  mergeDisplayNotesByNoteId(overdubSourceViewNotes_, candidates);
}

LOOP_COLD_MEM __attribute__((noinline)) bool Loop::committedPlaybackNoteOnIdentityValid(
    NoteId noteId) const {
  if (committedPlaybackMergedForIdentity_ == nullptr) {
    return true;
  }
  // Identity filtering is only valid for the current loop revision. A stale
  // merged stream must not prune source-view notes during wrap rebuild fallback.
  if (committedPlaybackMergedForIdentity_->builtFromRevision != playbackRevision) {
    return true;
  }
  if (noteId == kInvalidNoteId) {
    return true;
  }
  const PlaybackMergedMidiEvents& merged = *committedPlaybackMergedForIdentity_;
  // Construction binds only a full-loop stream. Re-check here so a later
  // windowed gather into the same struct cannot prune against a partial window.
  if (!isFullLoopMergedPlaybackWindow(merged, loopLengthTicks)) {
    return true;
  }
  for (const MidiEvent& evt : merged.mergedEvents) {
    if (evt.isNoteOn() && evt.noteId == noteId) {
      return true;
    }
  }
  return false;
}

LOOP_COLD_MEM __attribute__((noinline)) void Loop::retainValidOverdubSourceViewIdentities() {
  if (committedPlaybackMergedForIdentity_ == nullptr || !overdubSourceViewEstablished_) {
    return;
  }
  size_t write = 0;
  for (size_t read = 0; read < overdubSourceViewNotes_.size(); ++read) {
    const NoteUtils::DisplayNote& note = overdubSourceViewNotes_[read];
    if (note.noteId != kInvalidNoteId && !committedPlaybackNoteOnIdentityValid(note.noteId)) {
      continue;
    }
    if (write != read) {
      overdubSourceViewNotes_[write] = overdubSourceViewNotes_[read];
    }
    ++write;
  }
  overdubSourceViewNotes_.resize(write);
}

LOOP_COLD_MEM __attribute__((noinline)) void Loop::replaceCommittedPlaybackNoteOnIdentities(
    const PlaybackMergedMidiEvents& merged) {
  if (!isFullLoopMergedPlaybackWindow(merged, loopLengthTicks)) {
    committedPlaybackMergedForIdentity_ = nullptr;
    return;
  }
  committedPlaybackMergedForIdentity_ = &merged;
  retainValidOverdubSourceViewIdentities();
}

LOOP_COLD_MEM __attribute__((noinline)) void Loop::clearCommittedPlaybackNoteOnIdentities() {
  committedPlaybackMergedForIdentity_ = nullptr;
}

#if defined(PIO_UNIT_TEST_NATIVE)
void Loop::replaceOverdubSourceViewNotesForTest(NoteUtils::DisplayNoteVec notes) {
  overdubSourceViewNotes_ = std::move(notes);
  overdubSourceSpanCacheNotes_ = overdubSourceViewNotes_;
  overdubSourceSpanCacheLoopLengthTicks_ = loopLengthTicks;
  overdubSourceSpanCachePlaybackRevision_ = playbackRevision;
  overdubSourceSpanCachePublishedRevision_ = 0;
  overdubSourceSpanCachePublishedIdentity_ = nullptr;
  overdubSourceSpanCacheValid_ = true;
  overdubSourceSpanCacheFullLoop_ = false;
  overdubSourceViewLoopLengthTicks_ = loopLengthTicks;
  overdubSourceViewEstablished_ = true;
}

void Loop::pushPendingNoteChangeForTest(PendingNoteChange change) {
  pendingNoteChanges_.push_back(std::move(change));
}
#endif

LOOP_COLD_MEM void Loop::establishOverdubSourceView(uint32_t playheadPhaseTick) {
  // CONTRACTS.md C3.5: this is the session (re)open edge, so a predecessor that published or was
  // replaced before it cancels the cursor and its staged rows here -- a new interval never inherits
  // the previous interval's private prefix.
  discardPrivatePredecessorSource();
  overlapHoldTotals_ = {};
  // **One producer, two drivers** (tasks 4.7/4.8). The open path requires the window it is about to
  // expose and drives the same worker the deferred owner drives, under a declared grain cap. It does not
  // run a second derive: every tile it prepares is published content + prepared bit together.
  if (overdubCoverage_.sessionActive && loopLengthTicks > 0) {
    // **Overlap coverage only** (design D3). The display has its own committed base
    // (`CommittedDisplayBase`), so the open edge does not prewarm the whole loop here: it requires the
    // window around the playhead and lets acceptance/hold marks widen coverage as the player touches
    // regions. Viewport movement never enlarges this set.
    uint32_t openStart = 0;
    uint32_t openLength = 0;
    resolveOverdubSourceWindow(playheadPhaseTick, openStart, openLength);
    if (openLength > 0) {
      overdubCoverage_.markSpanSealed(openStart, openLength);
      constexpr uint8_t kOpenCoverageMaxGrains = 8;
      for (uint8_t grain = 0;
           grain < kOpenCoverageMaxGrains && overdubCoverage_.owedCount() > 0; ++grain) {
        if (serviceOverdubCoverageTurn(CoveragePrerequisite::kGrainEventsPerTurn) ==
            OverdubCoverageTurn::Idle) {
          break;
        }
      }
    }
  }
  rebuildOverdubSourceView(playheadPhaseTick, "open");
  clearPendingNoteChanges();
}

LOOP_COLD_MEM void Loop::recutOverdubSourceView() {
  // The view is a read-only cut of the cache; recutting after a publication is the only "refresh" the
  // session needs. The playhead is only the diagnostic's label -- the cut is tile-scoped, not windowed.
  rebuildOverdubSourceView(lastTickInLoop == UINT32_MAX ? 0 : lastTickInLoop, "tile");
}

LOOP_COLD_MEM bool Loop::overdubSourceSpanCacheReady() const {
  // CONTRACTS.md C3.2: a private-predecessor scan in progress is not a source. `fullLoop` is not
  // raised for it until its deferred holds are materialized, so no reader can take a partial
  // private prefix as a complete span.
  if (privatePredecessorSourcePending()) {
    return false;
  }
  // Touched coverage (tasks 4.7/4.8): during a session the source is ready when **no required tile is
  // owed** -- an empty required set is a complete answer. There is no whole-loop requirement any more;
  // every tile the player touched has been published atomically by the worker.
  if (overdubCoverage_.sessionActive) {
    return overdubCoverage_.owedCount() == 0;
  }
  if (!overdubSourceSpanCacheValid_) {
    return false;
  }
  if (overdubSourceSpanCacheLoopLengthTicks_ != loopLengthTicks) {
    return false;
  }
  if (!overdubSourceSpanCacheFullLoop_) {
    return false;
  }
  return overdubSourceSpanCachePlaybackRevision_ == playbackRevision;
}

LOOP_COLD_MEM bool Loop::overdubSourceSpanCacheReusable() const {
  return overdubSourceSpanCacheCoversWindow(0, loopLengthTicks);
}

/// Window-aware reuse: the cache is authoritative for `windowStart..+windowLength` only while its own
/// coverage contains that interval **and** its identity is current. This is the predicate both consumers
/// use — the wrap/idle refresh and the note-hold merge — so neither re-derives content it already holds,
/// and neither serves content it does not.
///
/// Under touched coverage the coverage unit is the **tile**: a window is servable when every tile it
/// touches is prepared for the current generation (design D3). A private-predecessor prefix answers as a
/// whole cache while the scan owns it (C3.1/C3.5).
LOOP_COLD_MEM bool Loop::overdubSourceSpanCacheCoversWindow(uint32_t windowStart,
                                                            uint32_t windowLength) const {
  if (!overdubSourceSpanCacheValid_ || loopLengthTicks == 0) {
    return false;
  }
  if (overdubSourceSpanCacheLoopLengthTicks_ != loopLengthTicks) {
    return false;
  }
  if (overdubSourceSpanCachePlaybackRevision_ != playbackRevision) {
    return false;
  }
  if (publishedSnapshotIsCurrent()) {
    if (overdubSourceSpanCachePublishedRevision_ != publishedSnapshotRevision() ||
        overdubSourceSpanCachePublishedIdentity_ == nullptr ||
        overdubSourceSpanCachePublishedIdentity_ != publishedSnapshotIdentity()) {
      return false;
    }
  }
  if (overdubSourceSpanCacheOrigin_ == SourceOrigin::PrivatePredecessor) {
    return overdubSourceSpanCacheFullLoop_;
  }
  if (overdubCoverage_.sessionActive) {
    if (windowLength == 0) {
      return false;
    }
    const uint16_t count = CoveragePrerequisite::tileCount(loopLengthTicks);
    if (count == 0) {
      return false;
    }
    const uint16_t first =
        CoveragePrerequisite::tileIndexForPhase(windowStart, loopLengthTicks);
    const uint16_t last = CoveragePrerequisite::tileIndexForPhase(
        windowStart + windowLength - 1u, loopLengthTicks);
    for (uint16_t steps = 0; steps < count; ++steps) {
      const uint16_t tile = static_cast<uint16_t>((first + steps) % count);
      if (!overdubCoverage_.tilePrepared(tile)) {
        return false;
      }
      if (tile == last) {
        break;
      }
    }
    return true;
  }
  return WindowCoverage::windowCoversWindow(overdubSourceSpanCacheCoverageStart_,
                                            overdubSourceSpanCacheCoverageLength_, windowStart,
                                            windowLength, loopLengthTicks);
}

LOOP_COLD_MEM const char* Loop::rebuildOverdubSourceSpanCache(uint32_t requestedWindowStart,
                                                              uint32_t requestedWindowLength) {
  NoteUtils::DisplayNoteVec priorSpanCacheNotes;
  // CONTRACTS.md C3.1/C3.5: a private-predecessor scan owns this cache from its first grain until
  // its deferred holds are materialized (both the row phase and the hold phase). Its staged prefix is
  // carried through the rebuild unchanged -- re-deriving it from a committed origin would mix a
  // private prefix with a committed suffix, which is forbidden even when both agree.
  // The scan owns the cache from the moment it is bound -- including before it has staged its first
  // row. Re-deriving a committed origin into it while a private prefix is being built is exactly the
  // mixing C3.5 forbids, in either direction.
  const bool privateCacheOwned =
      (privateSource_.phase == kPrivateRows || privateSource_.phase == kPrivateHolds) &&
      overdubSourceSpanCacheLoopLengthTicks_ == loopLengthTicks;
  // A private-origin cache is NEVER carried as `Prior`: that path exists for committed caches, and
  // re-seeding it would relabel private rows as a committed origin while dropping the committed notes
  // the rebuild had just derived. After a supersession the committed chain re-derives from its root.
  const bool keepPriorSpanCache =
      privateCacheOwned ||
      (overdubSourceSpanCacheOrigin_ != SourceOrigin::PrivatePredecessor && hasOverdubSession() &&
       overdubSourceSpanCacheValid_ &&
       overdubSourceSpanCacheLoopLengthTicks_ == loopLengthTicks &&
       overdubSourceSpanCacheFullLoop_ && !overdubSourceSpanCacheNotes_.empty());
  if (keepPriorSpanCache) {
    priorSpanCacheNotes.swap(overdubSourceSpanCacheNotes_);
  }
  overdubSourceSpanCacheNotes_.clear();
  overdubSourceSpanCacheLoopLengthTicks_ = loopLengthTicks;
  overdubSourceSpanCachePlaybackRevision_ = playbackRevision;
  overdubSourceSpanCachePublishedRevision_ = 0;
  overdubSourceSpanCachePublishedIdentity_ = nullptr;
  overdubSourceSpanCacheValid_ = true;
  overdubSourceSpanCacheFullLoop_ = false;
  if (loopLengthTicks == 0) {
    overdubSourceSpanCacheOrigin_ = SourceOrigin::None;
    return "empty";
  }
  if (requestedWindowLength == 0) {
    requestedWindowStart = 0;
    requestedWindowLength = loopLengthTicks;
  }
  SourceOrigin sourceOrigin = SourceOrigin::Passes;
  // What this cache's notes actually cover. Reuse is judged against it (see `overdubSourceSpanCacheReusable`):
  // an established cache is authoritative only while its coverage contains the interval the caller will
  // evaluate. Never claim more than was derived.
  uint32_t coverageStart = requestedWindowStart;
  uint32_t coverageLength = requestedWindowLength;
  const ImmutableLoopSnapshotRef snapshot = publishedSnapshot();
  const bool usePublishedSnapshot = !privateCacheOwned && snapshot &&
                                    publishedSnapshotIsCurrent();
  if (privateCacheOwned) {
    // The private prefix is this cache's only content: no committed reader may attribute it, so
    // both published stamps stay unset and `fullLoop` stays false until the scan completes.
    overdubSourceSpanCacheNotes_.swap(priorSpanCacheNotes);
    sourceOrigin = SourceOrigin::PrivatePredecessor;
  } else if (usePublishedSnapshot) {
    const uint32_t firstBar = requestedWindowStart / Config::TICKS_PER_BAR;
    const uint32_t lastWindowTick = requestedWindowStart + requestedWindowLength - 1u;
    const uint32_t lastBar = lastWindowTick / Config::TICKS_PER_BAR;
    if (requestedWindowLength >= loopLengthTicks) {
      const uint32_t barCount =
          (loopLengthTicks + Config::TICKS_PER_BAR - 1u) / Config::TICKS_PER_BAR;
      appendSnapshotNotesForBarRange(*snapshot, 0, barCount - 1u,
                                     overdubSourceSpanCacheNotes_);
    } else {
      appendSnapshotNotesForBarRange(*snapshot, firstBar, lastBar,
                                     overdubSourceSpanCacheNotes_);
    }
    overdubSourceSpanCacheFullLoop_ = requestedWindowLength >= loopLengthTicks;
    coverageStart = overdubSourceSpanCacheFullLoop_ ? 0u : requestedWindowStart;
    coverageLength = overdubSourceSpanCacheFullLoop_ ? loopLengthTicks : requestedWindowLength;
    sourceOrigin = SourceOrigin::Snapshot;
  } else {
    const bool copiedPreparedSpans = LoopContentResolution::tryCopyPreparedSpansToDisplayNotes(
      playbackRevision, overdubSourceSpanCacheNotes_, nullptr, loopLengthTicks,
#if defined(SESSION_CAPTURE) && defined(ARDUINO)
      true
#else
      false
#endif
  );
  if (copiedPreparedSpans) {
    overdubSourceSpanCacheFullLoop_ = true;
    coverageStart = 0;
    coverageLength = loopLengthTicks;
    sourceOrigin = SourceOrigin::Prepared;
  } else if (hasOverdubSession() && !priorSpanCacheNotes.empty()) {
    // A prior cache is reusable only when it was proven to cover the full loop. A
    // nonempty partial seed is not a valid answer for a later source window.
    overdubSourceSpanCacheNotes_.swap(priorSpanCacheNotes);
    overdubSourceSpanCacheFullLoop_ = true;
    coverageStart = 0;
    coverageLength = loopLengthTicks;
    sourceOrigin = SourceOrigin::Prior;
  } else if (hasOverdubSession() &&
             !visualCache.notes.empty() &&
             visualCacheCoversWindow(visualCacheDirty, visualCache.dirtyBars,
                                     requestedWindowStart, requestedWindowLength,
                                     loopLengthTicks, Config::TICKS_PER_BAR)) {
    // Prepared spans can miss immediately after publish. Use the latest committed
    // display list only when it covers the requested source interval.
    overdubSourceSpanCacheNotes_ = visualCache.notes;
    overdubSourceSpanCacheFullLoop_ = !visualCacheDirty;
    coverageStart = overdubSourceSpanCacheFullLoop_ ? 0u : requestedWindowStart;
    coverageLength = overdubSourceSpanCacheFullLoop_ ? loopLengthTicks : requestedWindowLength;
    sourceOrigin = SourceOrigin::Visual;
  } else {
    // An uncovered visual cache is not a valid overlap source. Reconstruct from committed passes so
    // overlap resolution never receives a partial span.
    //
    // **Windowed, not whole-loop** (task 4.2/4.3): the callers of this cache evaluate one *window*
    // (`resolveOverdubSourceWindow`, 16 bars), and both consumers filter by that window anyway. Deriving
    // the whole pass here was the 201 ms `lcr,pairrows,at=passes` span inside the idle owner turn.
    //
    // The pad is **loop-aware**: a window near either loop edge must be able to read the fragment on the
    // other side, or a wrap note whose note-on lies outside the window can never be paired (measured:
    // a clamped pad resolved 0 of 3 notes for a wrap window). A clamped pad -- the visual-cache slice's
    // shape -- is not sufficient here.
    const uint32_t padTicks = Config::TICKS_PER_BAR;
    uint32_t deriveStart = 0;
    uint32_t deriveLength = 0;
    resolvePaddedOverdubSourceWindow(requestedWindowStart, requestedWindowLength, deriveStart,
                                     deriveLength);
    ResolutionCostCounters deriveCounters;
    LoopContentResolution::resolveDisplayNotesInWindow(passes, loopLengthTicks, deriveStart,
                                                       deriveLength,
                                                       overdubSourceSpanCacheNotes_,
                                                       &deriveCounters);
    noteOverdubSpanDeriveOps(deriveCounters);
    appendOverdubPassWrapPairedNotes(overdubSourceSpanCacheNotes_);
    overdubSourceSpanCacheFullLoop_ = deriveLength >= loopLengthTicks;
    coverageStart = overdubSourceSpanCacheFullLoop_ ? 0u : deriveStart;
    coverageLength = overdubSourceSpanCacheFullLoop_ ? loopLengthTicks : deriveLength;
    sourceOrigin = SourceOrigin::Passes;
  }
  }
  if (loopLengthTicks > 0) {
    // Span-list miss is not committed-content absence for prepared/visual/passes/
    // prior origins (`174151` id=112). A current full-loop snapshot must not be
    // healed from materialize — membership RC-A owns published NoteRefs
    // (consumer Stage 2 unwind). Bounded snapshot windows still skip merge
    // (`test_snapshot_source_uses_bounded_overdub_window`).
    if (sourceOrigin != SourceOrigin::Snapshot && sourceOrigin != SourceOrigin::PrivatePredecessor) {
      SessionMidiEventVec committedEvents;
      if (gatherConstructionCommittedEvents(*this, committedEvents)) {
        // Upsert missing NoteIds from pass resolve (honors sealed Delete/Length),
        // not raw reconstruct — reconstruct re-admits Hidden shorts (`202713`).
        if (sourceOrigin != SourceOrigin::Passes) {
          NoteUtils::DisplayNoteVec committedNotes;
          // Windowed for the same reason as the seed above: this merge recovers committed `NoteId`s for
          // the interval the caller will evaluate, and deriving the whole pass here was the second
          // whole-loop resolve on the idle owner turn.
          uint32_t mergeStart = 0;
          uint32_t mergeLength = 0;
          resolvePaddedOverdubSourceWindow(requestedWindowStart, requestedWindowLength, mergeStart,
                                           mergeLength);
          ResolutionCostCounters mergeCounters;
          LoopContentResolution::resolveDisplayNotesInWindow(passes, loopLengthTicks, mergeStart,
                                                             mergeLength, committedNotes,
                                                             &mergeCounters);
          noteOverdubSpanDeriveOps(mergeCounters);
          appendOverdubPassWrapPairedNotes(committedNotes);
          mergeDisplayNotesByNoteId(overdubSourceSpanCacheNotes_, committedNotes);
        }
        mergeOmittedCommittedNoteOnIdentities(overdubSourceSpanCacheNotes_, committedEvents);
        if (!overdubSourceSpanCacheNotes_.empty()) {
          overdubSourceSpanCacheFullLoop_ = true;
        }
      }
    }
    // Sealed Hide/Shorten + pending must apply after any snapshot or merge seed
    // (`202713` OD2 ghosts) — Stage 4 KEEP.
    applyPendingNoteChangesToDisplayNotes(overdubSourceSpanCacheNotes_,
                                          overdubSourceSpanCacheNotes_.size());
  }
  for (const PendingNoteChange& change : pendingNoteChanges_) {
    if (change.kind != PendingNoteChangeKind::Add || change.noteId == kInvalidNoteId) {
      continue;
    }
    bool found = false;
    for (const NoteUtils::DisplayNote& existing : overdubSourceSpanCacheNotes_) {
      if (existing.noteId == change.noteId) {
        found = true;
        break;
      }
    }
    if (!found) {
      NoteUtils::DisplayNote note{};
      note.noteId = change.noteId;
      note.note = change.pitch;
      note.velocity = change.velocity;
      note.startTick = change.startTick;
      note.endTick = change.endTick;
      overdubSourceSpanCacheNotes_.push_back(note);
    }
  }
  for (const PendingNoteChange& change : pendingNoteChanges_) {
    if (change.kind != PendingNoteChangeKind::Shorten &&
        change.kind != PendingNoteChangeKind::Hide) {
      continue;
    }
    for (auto it = overdubSourceSpanCacheNotes_.begin(); it != overdubSourceSpanCacheNotes_.end();) {
      if (it->noteId != change.noteId) {
        ++it;
        continue;
      }
      if (change.kind == PendingNoteChangeKind::Hide) {
        it = overdubSourceSpanCacheNotes_.erase(it);
        continue;
      }
      it->startTick = change.startTick;
      it->endTick = change.endTick;
      ++it;
    }
  }
  // Snapshot-current roots already bake companions into NoteRefs, but merge
  // above can reintroduce pre-companion rows — apply companion Delete/Length
  // whenever companion ids are still pending on the loop.
  applyPendingSnapshotCompanionsToDisplayNotes(overdubSourceSpanCacheNotes_);
  overdubSourceSpanCachePublishedRevision_ =
      sourceOrigin == SourceOrigin::Snapshot ? publishedSnapshotRevision() : 0;
  overdubSourceSpanCachePublishedIdentity_ =
      sourceOrigin == SourceOrigin::Snapshot ? publishedSnapshotIdentity() : nullptr;
  // The origin is recorded, not inferred from the notes: a private predecessor's rows are visibly
  // non-committed even though they sit in the same full-loop cache.
  overdubSourceSpanCacheOrigin_ = sourceOrigin;
  overdubSourceSpanCacheCoverageStart_ = loopLengthTicks == 0 ? 0u : (coverageStart % loopLengthTicks);
  overdubSourceSpanCacheCoverageLength_ = coverageLength;
  return sourceOriginName(sourceOrigin);
}

LOOP_COLD_MEM void Loop::applyPendingSnapshotCompanionsToDisplayNotes(
    NoteUtils::DisplayNoteVec& notes) const {
  // Gather applies companionIds onto snapshot MIDI events. This cache is
  // DisplayNotes and already overlays pending Hide/Shorten in place. Converting
  // the cache to On/Off and reconstructing drops wrap-paired identities
  // (test_wrap_rebuild_with_hide_companion_drops_record_from_source_view).
  const EditPassIdList* companionIds = pendingSnapshotCompanionIds();
  if (companionIds == nullptr || companionIds->empty()) {
    return;
  }
  for (const EditPassId companionId : *companionIds) {
    const EditPass* row = nullptr;
    for (const EditPass& candidate : passes.editPasses) {
      if (candidate.id == companionId && candidate.state == EditPassState::Active &&
          candidate.passType == EditPassType::Note) {
        row = &candidate;
        break;
      }
    }
    if (row == nullptr) {
      continue;
    }
    switch (row->actionType) {
      case EditActionType::Delete:
        for (auto it = notes.begin(); it != notes.end();) {
          if (it->noteId == row->targetNoteId) {
            it = notes.erase(it);
          } else {
            ++it;
          }
        }
        break;
      case EditActionType::Update:
        for (NoteUtils::DisplayNote& note : notes) {
          if (note.noteId != row->targetNoteId) {
            continue;
          }
          switch (row->propertyType) {
            case EditPropertyType::Pitch:
              note.note = row->pitch;
              break;
            case EditPropertyType::Velocity:
              note.velocity = row->velocity;
              break;
            case EditPropertyType::NoteRange:
              note.startTick = row->startTick;
              note.endTick = row->endTick;
              break;
            case EditPropertyType::Length:
              note.endTick = row->endTick;
              break;
            case EditPropertyType::Tick:
              note.startTick = row->startTick;
              break;
            default:
              break;
          }
        }
        break;
      case EditActionType::Create:
        for (const MidiEvent& evt : row->addedEvents) {
          if (!evt.isNoteOn() || evt.noteId == kInvalidNoteId ||
              displayNotesContainNoteId(notes, evt.noteId)) {
            continue;
          }
          NoteUtils::DisplayNote created{};
          created.noteId = evt.noteId;
          created.note = evt.data.noteData.note;
          created.velocity = evt.data.noteData.velocity;
          created.startTick = evt.tick;
          created.endTick = evt.tick;
          for (const MidiEvent& off : row->addedEvents) {
            if (!off.isNoteOff()) {
              continue;
            }
            if (off.noteId == evt.noteId ||
                (off.noteId == kInvalidNoteId && off.data.noteData.note == created.note)) {
              created.endTick = off.tick;
              break;
            }
          }
          notes.push_back(created);
        }
        break;
    }
  }
}

LOOP_COLD_MEM void Loop::rebuildOverdubSourceView(uint32_t playheadPhaseTick, const char* why) {
  RuntimeTimingTelemetry::IdleMaintChildScope idleChild(2);
  if (why == nullptr || why[0] == '\0') {
    why = "wrap";
  }
#if defined(SESSION_CAPTURE) && defined(ARDUINO) && SESSION_CAPTURE_LCR
  constexpr size_t kSrcDiagMaxIds = 48;
  NoteId beforeIds[kSrcDiagMaxIds];
  size_t beforeCount = 0;
  const bool logSrcWrapDiag = (std::strcmp(why, "wrap") == 0);
  if (logSrcWrapDiag) {
    for (const NoteUtils::DisplayNote& note : overdubSourceViewNotes_) {
      if (note.noteId == kInvalidNoteId) {
        continue;
      }
      if (beforeCount >= kSrcDiagMaxIds) {
        break;
      }
      beforeIds[beforeCount++] = note.noteId;
    }
    char beforeLine[96];
    snprintf(beforeLine, sizeof(beforeLine), "#CAP,%lu,DIAG,lcr,srcbefore,why=%s,count=%u",
             static_cast<unsigned long>(micros()), why, static_cast<unsigned>(beforeCount));
    DebugSessionCapture::appendCaptureTextLine(beforeLine);
  }
#else
  const bool logSrcWrapDiag = false;
#endif
  overdubSourceViewEvents_.clear();
  overdubSourceViewNotes_.clear();
  overdubSourceViewLoopLengthTicks_ = loopLengthTicks;
  if (loopLengthTicks == 0) {
    overdubSourceViewEstablished_ = true;
    return;
  }
#if defined(SESSION_CAPTURE) && defined(ARDUINO)
  const uint32_t filterStartUs = micros();
#endif
  // **Read-only cut** (tasks 4.7/4.8). The worker is the span cache's only writer while a session is
  // active, so the view is exactly what has been published: the prepared tiles' candidates composed by
  // `NoteId`, or the private-predecessor prefix while that scan owns the cache. Nothing here derives and
  // nothing here writes the cache; the moving playhead window is no longer a correctness mechanism.
  //
  // A cache stamped for another generation is not a source: the commit edge clears it, and this guard
  // catches any revision move that bypassed that edge (a raw bump, a load, an undo). Serving it would be
  // exactly the stale-geometry failure the generation stamp exists to prevent.
  const bool cacheCurrent = overdubSourceSpanCacheValid_ &&
                            overdubSourceSpanCacheLoopLengthTicks_ == loopLengthTicks &&
                            overdubSourceSpanCachePlaybackRevision_ == playbackRevision;
  if (!cacheCurrent) {
    overdubSourceViewEstablished_ = true;
    return;
  }
  // **Overlap domain only** (design D3): the union of the tiles the worker has published for this
  // generation, composed by `NoteId`. There is no playhead window and no display role here; the display's
  // committed base is `CommittedDisplayBase`, so viewport movement cannot enlarge overlap coverage and
  // overlap coverage movement cannot change what is drawable.
  for (const NoteUtils::DisplayNote& note : overdubSourceSpanCacheNotes_) {
    if (note.noteId == kInvalidNoteId) {
      continue;
    }
    overdubSourceViewNotes_.push_back(note);
  }
  collapseSplitHeadTailToWrapHeld(overdubSourceViewNotes_, loopLengthTicks);
#if defined(PIO_UNIT_TEST_NATIVE)
  copyEffectiveCommittedEventsInRange(overdubSourceViewEvents_, 0, loopLengthTicks);
#endif
  overdubSourceViewEstablished_ = true;
  const char* from = overdubSourceSpanCacheOrigin_ == SourceOrigin::PrivatePredecessor ? "private"
                                                                                      : "tiles";
#if defined(SESSION_CAPTURE) && defined(ARDUINO) && SESSION_CAPTURE_LCR
  const uint32_t filterUs = micros() - filterStartUs;
  NoteId beforeRetainIds[kSrcDiagMaxIds];
  size_t beforeRetainCount = 0;
  if (logSrcWrapDiag) {
    for (const NoteUtils::DisplayNote& note : overdubSourceViewNotes_) {
      if (note.noteId == kInvalidNoteId || beforeRetainCount >= kSrcDiagMaxIds) {
        continue;
      }
      beforeRetainIds[beforeRetainCount++] = note.noteId;
    }
    if (committedPlaybackMergedForIdentity_ != nullptr &&
        committedPlaybackMergedForIdentity_->builtFromRevision != playbackRevision) {
      char skipLine[128];
      snprintf_P(skipLine, sizeof(skipLine),
                 PSTR("#CAP,%lu,DIAG,lcr,srcskip,reason=stale_identity_stream,built=%u,loop=%u"),
               static_cast<unsigned long>(micros()),
               static_cast<unsigned>(committedPlaybackMergedForIdentity_->builtFromRevision),
               static_cast<unsigned>(playbackRevision));
      DebugSessionCapture::appendCaptureTextLine(skipLine);
    }
  }
#endif
  retainValidOverdubSourceViewIdentities();
  // This interval's Hide/Shorten decisions ride on top of the committed cut; the interval's own **Add**
  // rows do not. A pending Add is the note being recorded, not a committed overlap source: putting it in
  // the candidate domain makes a later consequence target the session's own note (measured: a live row
  // disappeared from the display frame because a pending Hide removed it). The seal still merges Adds
  // through `applyPendingNoteChangesToOverdubSourceView` when the interval becomes committed content.
  applyPendingNoteChangesToDisplayNotes(overdubSourceViewNotes_, static_cast<size_t>(-1));
#if defined(SESSION_CAPTURE) && defined(ARDUINO) && SESSION_CAPTURE_LCR
  if (logSrcWrapDiag) {
    auto sourceViewHasId = [this](NoteId id) {
      for (const NoteUtils::DisplayNote& note : overdubSourceViewNotes_) {
        if (note.noteId == id) {
          return true;
        }
      }
      return false;
    };
    auto idInList = [](NoteId id, const NoteId* ids, size_t count) {
      for (size_t i = 0; i < count; ++i) {
        if (ids[i] == id) {
          return true;
        }
      }
      return false;
    };
    uint32_t deltaLogged = 0;
    uint32_t dropLogged = 0;
    constexpr uint32_t kMaxSrcDeltaLogs = 16;
    constexpr uint32_t kMaxSrcDropLogs = 16;
    for (size_t i = 0; i < beforeRetainCount && dropLogged < kMaxSrcDropLogs; ++i) {
      const NoteId id = beforeRetainIds[i];
      if (sourceViewHasId(id)) {
        continue;
      }
      char dropLine[112];
      snprintf_P(dropLine, sizeof(dropLine),
                 PSTR("#CAP,%lu,DIAG,lcr,srcdrop,id=%u,reason=identity_invalid"),
               static_cast<unsigned long>(micros()), static_cast<unsigned>(id));
      DebugSessionCapture::appendCaptureTextLine(dropLine);
      ++dropLogged;
    }
    for (size_t i = 0; i < beforeCount && deltaLogged < kMaxSrcDeltaLogs; ++i) {
      const NoteId id = beforeIds[i];
      if (sourceViewHasId(id)) {
        continue;
      }
      const char* reason = PSTR("missing_cache_span");
      if (idInList(id, beforeRetainIds, beforeRetainCount)) {
        reason = PSTR("identity_invalid");
      } else if (LoopContentResolution::preparedCheckpointHasNoteId(id)) {
        reason = PSTR("cache_filter");
      }
      char deltaLine[112];
      snprintf_P(deltaLine, sizeof(deltaLine), PSTR("#CAP,%lu,DIAG,lcr,srcdelta,id=%u,reason=%s"),
               static_cast<unsigned long>(micros()), static_cast<unsigned>(id), reason);
      DebugSessionCapture::appendCaptureTextLine(deltaLine);
      ++deltaLogged;
    }
    char afterLine[112];
    snprintf_P(afterLine, sizeof(afterLine),
               PSTR("#CAP,%lu,DIAG,lcr,srcafter,why=%s,from=%s,before=%u,copy=%u,after=%u"),
             static_cast<unsigned long>(micros()), why, from, static_cast<unsigned>(beforeCount),
             static_cast<unsigned>(beforeRetainCount),
             static_cast<unsigned>(overdubSourceViewNotes_.size()));
    DebugSessionCapture::appendCaptureTextLine(afterLine);
  }
#endif
#if defined(SESSION_CAPTURE) && defined(ARDUINO) && SESSION_CAPTURE_LCR
  char line[256];
  snprintf_P(line, sizeof(line),
             PSTR("#CAP,%lu,DIAG,lcr,src,why=%s,from=%s,win=0,proj=%lu,tot=%lu,ev=%u,notes=%u,"
                  "bars=%u,live=%lu,prep=%lu,root_rev=%lu,cache_rev=%lu,play_rev=%lu,"
                  "cache_play=%lu"),
           static_cast<unsigned long>(micros()), why, from,
           static_cast<unsigned long>(filterUs), static_cast<unsigned long>(filterUs),
           static_cast<unsigned>(overdubSourceViewEvents_.size()),
           static_cast<unsigned>(overdubSourceViewNotes_.size()),
           static_cast<unsigned>(kOverdubSourceWindowBars),
           static_cast<unsigned long>(loopLengthTicks),
           static_cast<unsigned long>(LoopContentResolution::deviceGateLoopLengthTicks()),
           static_cast<unsigned long>(publishedSnapshotRevision()),
           static_cast<unsigned long>(overdubSourceSpanCachePublishedRevision_),
           static_cast<unsigned long>(playbackRevision),
           static_cast<unsigned long>(overdubSourceSpanCachePlaybackRevision_));
  DebugSessionCapture::appendCaptureTextLine(line);
  unsigned viewCount = 0;
  constexpr unsigned kPreparedSourceViewDumpLimit = 48;
  for (const NoteUtils::DisplayNote& note : overdubSourceViewNotes_) {
    if (note.noteId == kInvalidNoteId) {
      continue;
    }
    if (viewCount < kPreparedSourceViewDumpLimit) {
      char spanLine[192];
      snprintf_P(spanLine, sizeof(spanLine),
                 PSTR("#CAP,%lu,DIAG,lcr,srcspan,at=view,id=%lu,p=%u,s=%lu,e=%lu,g=%s"),
                 static_cast<unsigned long>(micros()), static_cast<unsigned long>(note.noteId),
                 static_cast<unsigned>(note.note), static_cast<unsigned long>(note.startTick),
                 static_cast<unsigned long>(note.endTick),
                 (note.endTick < note.startTick) ? PSTR("wrap") : PSTR("linear"));
      DebugSessionCapture::appendCaptureTextLine(spanLine);
    }
    ++viewCount;
  }
  snprintf_P(line, sizeof(line), PSTR("#CAP,%lu,DIAG,lcr,srcspan,at=view,count=%u"),
             static_cast<unsigned long>(micros()), viewCount);
  DebugSessionCapture::appendCaptureTextLine(line);
#else
  (void)from;
  (void)why;
#endif
}

LOOP_COLD_MEM void Loop::clearOverdubSourceView() {
  // CONTRACTS.md C3.5: this is the one cancellation edge for the session source -- session close,
  // Record re-entry, clear, load and slot switch all arrive here -- so the private-predecessor
  // cursor and every row it staged go with it.
  discardPrivatePredecessorSource();
  overdubSourceViewEvents_.clear();
  overdubSourceViewNotes_.clear();
  overdubSourceViewLoopLengthTicks_ = 0;
  overdubSourceViewEstablished_ = false;
}

LOOP_COLD_MEM void Loop::prewarmOverdubSourceSpanCache() {
  RuntimeTimingTelemetry::IdleMaintChildScope idleChild(5);
  if (captureActive() || hasPendingCapturePass_ || hasOverdubSession()) {
    return;
  }
  if (loopLengthTicks == 0 || !hasCommittedPasses()) {
    return;
  }
  if (overdubSourceSpanCacheReusable()) {
    return;
  }
  // When the prepared gate is still building for this loop length, avoid a full
  // fallback rebuild in the same idle turn and let the gate finish first.
  if (!LoopContentResolution::preparedWindowReady(playbackRevision) &&
      LoopContentResolution::deviceGateActive() &&
      LoopContentResolution::deviceGateLoopLengthTicks() == loopLengthTicks) {
    return;
  }
  rebuildOverdubSourceSpanCache();
}

void Loop::clearPendingNoteChanges() {
  pendingNoteChanges_.clear();
}

LOOP_COLD_MEM void Loop::retainCapturePreviewAtSeal() {
  retainCapturePreviewAtSeal(capturePreview);
}

LOOP_COLD_MEM void Loop::retainCapturePreviewAtSeal(const CapturePreview& source) {
  // Uncovered sealed rows stay. Only releaseSealedCapturePreviewIfCovered may drop them
  // (`120321` wrap 2: clear() replaced lap-1 sealed with lap-2 only).
  sealedCapturePreviewRevision_ = playbackRevision;
  if (source.notes.empty()) {
    return;
  }
  for (size_t index = 0; index < source.notes.size(); ++index) {
    // An open hold continues through the next capture pass and remains owned
    // by the live capture tier. The sealed handoff owns closed rows only;
    // retaining an open row here duplicates it beside that live continuation.
    if (index < source.noteStates.size() && source.noteStates[index].open) {
      continue;
    }
    // Draw-side upsert keeps this handoff from double-painting a fresh row.
    DisplayWindowUtils::upsertDisplayNoteAtPitchStart(sealedCapturePreviewNotes_,
                                                      source.notes[index]);
  }
}

LOOP_COLD_MEM bool Loop::sealedCapturePreviewIsCovered() const {
  if (sealedCapturePreviewNotes_.empty()) {
    return true;
  }
  // A current snapshot is not membership proof. `012714` wrap 2: snap=1 released
  // sealed=47 and the frame dropped 148→129; bars 2–3 had no draw_src diff.
  // Revision alone is also not membership: a clean cache that predates the
  // sealed rows must keep the handoff (`170537`).
  if (visualCacheDirty || visualCache.builtFromRevision < sealedCapturePreviewRevision_) {
    return false;
  }
  for (const NoteUtils::DisplayNote& sealedRow : sealedCapturePreviewNotes_) {
    if (!DisplayWindowUtils::sealedCapturePreviewRowCoveredByVisualCache(
            sealedRow.noteId, sealedRow.note, sealedRow.startTick, sealedRow.endTick,
            visualCache.notes)) {
      return false;
    }
  }
  return true;
}

LOOP_COLD_MEM bool Loop::retiredSealedDisplayCovered() const {
  if (retiredSealedDisplayNotes_.empty()) {
    return true;
  }
  if (visualCacheDirty || visualCache.builtFromRevision < retiredSealedDisplayRevision_) {
    return false;
  }
  for (const NoteUtils::DisplayNote& row : retiredSealedDisplayNotes_) {
    if (!DisplayWindowUtils::sealedCapturePreviewRowCoveredByVisualCache(
            row.noteId, row.note, row.startTick, row.endTick, visualCache.notes)) {
      return false;
    }
  }
  return true;
}

LOOP_COLD_MEM bool Loop::retiredSealedDisplayNeededForFrame() const {
  if (retiredSealedDisplayNotes_.empty()) {
    return false;
  }
  if (visualCacheDirty) {
    // The cache cannot be trusted to represent anything yet, so the retired rows are the frame.
    return true;
  }
  for (const NoteUtils::DisplayNote& row : retiredSealedDisplayNotes_) {
    bool represented = false;
    for (const NoteUtils::DisplayNote& committed : visualCache.notes) {
      if (committed.note == row.note && committed.startTick == row.startTick) {
        represented = true;
        break;
      }
    }
    if (!represented) {
      // Nothing authoritative holds this identity, so the retired row is still the only source.
      return true;
    }
  }
  return false;
}

LOOP_COLD_MEM void Loop::releaseRetiredSealedDisplayIfCovered() {
  if (retiredSealedDisplayNotes_.empty() || !retiredSealedDisplayCovered()) {
    return;
  }
  clearRetiredSealedDisplayNotes();
}

LOOP_COLD_MEM void Loop::clearRetiredSealedDisplayNotes() {
  retiredSealedDisplayNotes_.clear();
  retiredSealedDisplayRevision_ = 0;
}

LOOP_COLD_MEM void Loop::retireSealedCapturePreviewForDisplay() {
  if (sealedCapturePreviewNotes_.empty()) {
    return;
  }
  for (const NoteUtils::DisplayNote& row : sealedCapturePreviewNotes_) {
    DisplayWindowUtils::upsertDisplayNoteAtPitchStart(retiredSealedDisplayNotes_, row);
  }
  if (retiredSealedDisplayRevision_ == 0 ||
      sealedCapturePreviewRevision_ < retiredSealedDisplayRevision_) {
    retiredSealedDisplayRevision_ = sealedCapturePreviewRevision_;
  }
  clearSealedCapturePreview();
}

LOOP_COLD_MEM void Loop::releaseSealedCapturePreviewIfCovered() {
  if (sealedCapturePreviewNotes_.empty() || !sealedCapturePreviewIsCovered()) {
    return;
  }
  clearSealedCapturePreview();
}

LOOP_COLD_MEM void Loop::clearSealedCapturePreview() {
  sealedCapturePreviewNotes_.clear();
  sealedCapturePreviewRevision_ = 0;
}

void Loop::advanceCaptureContentGeneration() {
  if (captureContentGeneration_ != 0) {
    captureContentGeneration_ = captureContentGeneration_ == UINT64_MAX
                                   ? 0 : captureContentGeneration_ + 1;
  }
}

LOOP_COLD_MEM bool Loop::sealCaptureRangeForJob(LoopEventStore::CaptureRange& range,
                                                 uint64_t& frozenContentGeneration) {
  if (!capture.store.sealCaptureRange(range)) return false;
  frozenContentGeneration = captureContentGeneration_;
  advanceCaptureContentGeneration();
  return true;
}

LOOP_COLD_MEM void Loop::beginCapture(CapturePhase phase, uint32_t playheadPhaseTick) {
  discardPendingCapturePass();
  advanceCaptureContentGeneration();
  capture.phase = phase;
  // A producer that already owns the pre-reserved successor directory (the
  // state left by a sealed capture range cut) must keep it: releasing it here
  // would force a cold re-reservation from the capture-start path.
  if (!capture.store.resetCaptureExtentPreservingReservedStorage()) {
    capture.store.clear();
  }
  capturePreview.clear();
  captureNextEventIndex = 0;
  captureEventsSortDirty = false;
  captureHasLastAppendTick_ = false;
  captureMaximumTick_ = 0;
  captureDedupEventsDropped_ = 0;
  ++captureDisplayRevision;
  if (phase == CapturePhase::Overdub) {
    // Touched coverage is per overdub session. Production always opens the session first; a capture
    // edge that reaches here without one (a direct capture-entry fixture, or a legacy caller) opens the
    // coverage state for this loop length rather than running without a source model.
    if (!overdubCoverage_.sessionActive && loopLengthTicks > 0) {
      overdubCoverage_.beginSession(loopLengthTicks, playbackRevision);
      seedOverdubCoverageClosureFromCommittedContent();
      adoptPrewarmedOverdubCoverage();
    }
    // Wrap beginCapture must keep the session source view. Re-establish after
    // invalidateCaches drops this-session notes (235407 inner / same-start-longer).
    if (!overdubSourceViewEstablished_) {
      establishOverdubSourceView(playheadPhaseTick);
    }
  } else {
    clearOverdubSourceView();
  }
}

LOOP_COLD_MEM bool Loop::retireCopiedCaptureSourceGrain(bool closeCapture) {
  if (!capture.store.empty()) advanceCaptureContentGeneration();
  const bool complete = capture.store.releaseCaptureChunkGrain();
  ++captureDisplayRevision;
  if (complete) {
    if (closeCapture) capture.phase = CapturePhase::None;
    captureNextEventIndex = 0;
    captureEventsSortDirty = false;
    captureHasLastAppendTick_ = false;
    captureMaximumTick_ = 0;
  }
  return complete;
}

LOOP_COLD_MEM Loop::CaptureReadSource Loop::captureReadSource() const {
  CaptureReadSource source;
  source.producer = &capture.store;
  if (borrowedSealedCapture_ != nullptr && borrowedSealedCapture_->ready()) {
    source.finalizing = borrowedSealedCapture_;
    source.finalizingGeneration = borrowedSealedCaptureGeneration_;
  }
  return source;
}

LOOP_COLD_MEM bool Loop::borrowSealedCaptureRange(const LoopEventStore::CaptureRange& range,
                                                  const void* owner) {
  if (owner == nullptr || !range.ready()) return false;
  borrowedSealedCapture_ = &range;
  borrowedSealedCaptureOwner_ = owner;
  borrowedSealedCaptureGeneration_ = range.generation();
  return true;
}

LOOP_COLD_MEM bool Loop::releaseSealedCaptureRangeBorrow(const void* owner) {
  if (borrowedSealedCapture_ == nullptr || borrowedSealedCaptureOwner_ != owner) return false;
  borrowedSealedCapture_ = nullptr;
  borrowedSealedCaptureOwner_ = nullptr;
  borrowedSealedCaptureGeneration_ = 0;
  return true;
}

LOOP_COLD_MEM bool Loop::borrowPendingNoteChanges(const PendingNoteChangeVec& changes,
                                                 const void* owner) {
  if (owner == nullptr) return false;
  borrowedPendingChanges_ = &changes;
  borrowedPendingChangesOwner_ = owner;
  return true;
}

LOOP_COLD_MEM bool Loop::releasePendingNoteChangesBorrow(const void* owner) {
  if (borrowedPendingChanges_ == nullptr || borrowedPendingChangesOwner_ != owner) return false;
  borrowedPendingChanges_ = nullptr;
  borrowedPendingChangesOwner_ = nullptr;
  return true;
}

LOOP_COLD_MEM const CapturePreview& Loop::capturePreviewView() const {
  // A predecessor borrow must not mask input accepted by a new producer. Until that producer
  // has a preview, the borrowed interval remains the continuity source as before.
  return !capturePreview.notes.empty() || borrowedPreview_ == nullptr ? capturePreview : *borrowedPreview_;
}


LOOP_COLD_MEM bool Loop::borrowCapturePreview(const CapturePreview& preview, const void* owner) {
  if (owner == nullptr) return false;
  borrowedPreview_ = &preview;
  borrowedPreviewOwner_ = owner;
  return true;
}

LOOP_COLD_MEM bool Loop::releaseCapturePreviewBorrow(const void* owner) {
  if (borrowedPreview_ == nullptr || borrowedPreviewOwner_ != owner) return false;
  borrowedPreview_ = nullptr;
  borrowedPreviewOwner_ = nullptr;
  return true;
}

LOOP_COLD_MEM bool Loop::copyCaptureIntervalEvents(SessionMidiEventVec& out) const {
  out.clear();
  const CaptureReadSource source = captureReadSource();
  if (source.producerHasRows()) {
    const_cast<Loop*>(this)->ensureCaptureEventsSorted();
    capture.store.copyEventsTo(out);
  }
  if (source.finalizingReady()) {
    SessionMidiEventVec sealedRows;
    const auto* range = source.finalizing;
    const size_t chunks = range->chunkCount();
    for (size_t chunkIndex = 0; chunkIndex < chunks; ++chunkIndex) {
      for (uint16_t eventIndex = 0;; ++eventIndex) {
        MidiEvent event;
        bool atEnd = false;
        if (!range->read(source.finalizingGeneration, chunkIndex, eventIndex, event, atEnd)) break;
        if (atEnd) break;
        sealedRows.push_back(event);
      }
    }
    if (!sealedRows.empty()) {
      out.insert(out.end(), sealedRows.begin(), sealedRows.end());
    }
  }
  if (out.empty()) return false;
  // Ordering is scratch-only. A borrowed sealed range is immutable to Loop, so
  // the sort can never reorder or rewrite the admitted job's owned extent.
  sortSessionEventsByTick(out);
  return true;
}

LOOP_COLD_MEM bool Loop::findLastCaptureNoteOnIdentity(uint8_t channel, uint8_t note,
                                                      NoteId& out) const {
  bool found = false;
  const auto* range = borrowedSealedCapture_;
  if (range != nullptr && range->ready()) {
    const size_t chunks = range->chunkCount();
    for (size_t chunkIndex = 0; chunkIndex < chunks; ++chunkIndex) {
      for (uint16_t eventIndex = 0;; ++eventIndex) {
        MidiEvent event;
        bool atEnd = false;
        if (!range->read(borrowedSealedCaptureGeneration_, chunkIndex, eventIndex, event, atEnd)) {
          break;
        }
        if (atEnd) break;
        if (event.isNoteOn() && event.channel == channel &&
            event.data.noteData.note == note && event.noteId != kInvalidNoteId) {
          out = event.noteId;
          found = true;
        }
      }
    }
  }
  if (found) return true;
  for (size_t index = capture.store.size(); index > 0; --index) {
    const MidiEvent& prior = capture.store.at(index - 1);
    if (prior.isNoteOn() && prior.channel == channel &&
        prior.data.noteData.note == note && prior.noteId != kInvalidNoteId) {
      out = prior.noteId;
      return true;
    }
  }
  return false;
}

LOOP_COLD_MEM void Loop::closeProducerCapturePhase() {
  capture.phase = CapturePhase::None;
  captureNextEventIndex = 0;
  captureEventsSortDirty = false;
  captureHasLastAppendTick_ = false;
  captureMaximumTick_ = 0;
}

void Loop::discardCapture() {
  if (capture.phase != CapturePhase::None || !capture.store.empty())
    advanceCaptureContentGeneration();
  capture.store.clear();
  capture.phase = CapturePhase::None;
  captureNextEventIndex = 0;
  captureEventsSortDirty = false;
  captureHasLastAppendTick_ = false;
  captureMaximumTick_ = 0;
  capturePreview.clear();
  captureDedupEventsDropped_ = 0;
  clearOverdubSourceView();
  clearPendingNoteChanges();
}

CaptureAppendResult Loop::appendCaptureEventWithResult(const MidiEvent& evt) {
  CaptureAppendResult result;
  if (capture.phase == CapturePhase::None) {
    result.reason = CaptureAppendDenyReason::PhaseNone;
    return result;
  }
  if (hasPendingCapturePass_) {
    result.reason = CaptureAppendDenyReason::PendingPass;
    return result;
  }
  // G2: with overdubSourceView, reverse-tick capture dedup is not overlap authority
  // (multi-wrap same-phase ticks must append; geometry resolves vs the source view).
  if (!hasOverdubSourceView() && isDuplicateCaptureEvent(*this, evt)) {
    ++captureDedupEventsDropped_;
    result.reason = CaptureAppendDenyReason::Duplicate;
    return result;
  }
  MidiEvent toAppend = evt;
  if (toAppend.isNoteOn() && toAppend.noteId == kInvalidNoteId) {
    toAppend.noteId = allocateNoteId();
  } else if (toAppend.isNoteOff() && toAppend.noteId == kInvalidNoteId) {
    for (size_t i = capture.store.size(); i > 0; --i) {
      const MidiEvent& prior = capture.store.at(i - 1);
      if (!prior.isNoteOn() || prior.channel != toAppend.channel ||
          prior.data.noteData.note != toAppend.data.noteData.note) {
        continue;
      }
      toAppend.noteId = prior.noteId;
      break;
    }
  }
  LoopEventStoreAppendDeny storeDeny = LoopEventStoreAppendDeny::None;
  if (!capture.store.append(toAppend, &storeDeny)) {
    result.reason =
        storeDeny == LoopEventStoreAppendDeny::PoolExhausted
            ? CaptureAppendDenyReason::PoolAlloc
            : CaptureAppendDenyReason::StoreOther;
    return result;
  }
  advanceCaptureContentGeneration();
  if (captureHasLastAppendTick_) {
    if (evt.tick < captureMaximumTick_) {
      captureEventsSortDirty = true;
    }
  } else {
    captureHasLastAppendTick_ = true;
  }
  captureMaximumTick_ = std::max(captureMaximumTick_, evt.tick);
  // Preview identity must mirror the stored capture event. `toAppend` carries
  // assigned NoteIds for NoteOn/NoteOff; using the raw input event drops that
  // identity and lets same-start siblings collide in tail growth.
  applyCaptureEventToPreview(capturePreview, toAppend, Config::TICKS_PER_BAR, loopLengthTicks);
  ++captureDisplayRevision;
  overdubSessionLiveUndoEvents_.clear();
  result.accepted = true;
  result.reason = CaptureAppendDenyReason::Accepted;
  return result;
}

bool Loop::appendCaptureEvent(const MidiEvent& evt) {
  return appendCaptureEventWithResult(evt).accepted;
}

bool Loop::removeOpenCaptureNoteOn(uint8_t channel, uint8_t note) {
  if (!captureActive() || capture.store.empty() || loopLengthTicks == 0) {
    return false;
  }
  ensureCaptureEventsSorted();

  SessionMidiEventVec flat;
  capture.store.copyEventsTo(flat);
  if (flat.empty()) {
    return false;
  }

  const std::vector<NoteUtils::OpenNoteOn> opens =
      NoteUtils::findOpenNoteOns(flat, loopLengthTicks);
  uint32_t openTick = UINT32_MAX;
  for (const NoteUtils::OpenNoteOn& open : opens) {
    if (open.note != note) {
      continue;
    }
    for (const MidiEvent& evt : flat) {
      if (evt.isNoteOn() && evt.channel == channel && evt.data.noteData.note == note &&
          evt.tick == open.tick) {
        openTick = open.tick;
        break;
      }
    }
    if (openTick != UINT32_MAX) {
      break;
    }
  }
  if (openTick == UINT32_MAX) {
    return false;
  }

  SessionMidiEventVec kept;
  kept.reserve(flat.size() - 1);
  bool removed = false;
  uint32_t maximumTick = 0;
  uint32_t previousTick = 0;
  bool sortDirty = false;
  for (const MidiEvent& evt : flat) {
    if (!removed && evt.isNoteOn() && evt.channel == channel &&
        evt.data.noteData.note == note && evt.tick == openTick &&
        evt.data.noteData.velocity > 0) {
      removed = true;
      continue;
    }
    if (!kept.empty() && evt.tick < previousTick) sortDirty = true;
    previousTick = evt.tick;
    maximumTick = std::max(maximumTick, evt.tick);
    kept.push_back(evt);
  }
  if (!removed) {
    return false;
  }

  advanceCaptureContentGeneration();
  capture.store.clear();
  if (!kept.empty()) {
    capture.store.loadFromEvents(kept);
  }
  captureEventsSortDirty = sortDirty;
  captureHasLastAppendTick_ = !kept.empty();
  captureMaximumTick_ = maximumTick;
  rebuildCapturePreviewFromStore(*this);
  ++captureDisplayRevision;
  return true;
}

bool Loop::captureHasNoteOffAfter(uint8_t channel, uint8_t note, uint32_t onTick) const {
  if (!captureActive() || capture.store.empty()) {
    return false;
  }
  SessionMidiEventVec flat;
  capture.store.copyEventsTo(flat);
  for (const MidiEvent& evt : flat) {
    if (evt.isNoteOff() && evt.channel == channel && evt.data.noteData.note == note &&
        evt.tick > onTick) {
      return true;
    }
  }
  return false;
}

size_t Loop::liveEventCount() const {
  SessionMidiEventVec snapshotEvents;
  if (tryCopyCurrentSnapshotEditViewEvents(snapshotEvents)) {
    size_t count = snapshotEvents.size();
    if (captureActive()) {
      count += capture.store.size();
    }
    return count;
  }
  SessionMidiEventVec activeEvents;
  mergeActiveCapturePasses(activeEvents);
  size_t count = activeEvents.size();
  if (captureActive()) {
    count += capture.store.size();
  }
  return count;
}

bool Loop::captureActive() const {
  return capture.phase != CapturePhase::None;
}

bool Loop::ensureCaptureEventsSorted() {
  if (!captureEventsSortDirty) {
    return false;
  }
  advanceCaptureContentGeneration();
  uint32_t sortedLastTick = 0;
  sortCaptureStoreByTick(capture.store, &sortedLastTick);
  captureHasLastAppendTick_ = !capture.store.empty();
  captureMaximumTick_ = sortedLastTick;
  captureEventsSortDirty = false;
  return true;
}

void Loop::noteCaptureEventsCanonicalized(uint32_t lastTick) {
  captureHasLastAppendTick_ = !capture.store.empty();
  captureMaximumTick_ = captureHasLastAppendTick_ ? lastTick : 0;
  captureEventsSortDirty = false;
}

void Loop::clearCaptureOnNewPass() {
  discardCapture();
}

void Loop::commitStopFinalizeFromStore(LoopEventStore& merged) {
  const PassId preserveId = lastCommittedPassId_;
  CapturePassPhase preservePhase = CapturePassPhase::Overdub;
  uint32_t preserveMergeSeq = 0;
  uint8_t preserveOverdubSessionIndex = kUngroupedOverdubSessionIndex;
  if (passes.hasRecordPass() && passes.recordPass.id == preserveId) {
    preservePhase = CapturePassPhase::Record;
  } else {
    for (const OverdubPass& pass : passes.overdubPasses) {
      if (pass.state == CapturePassState::Active && pass.id == preserveId) {
        preserveMergeSeq = pass.mergeSequence;
        preserveOverdubSessionIndex = pass.overdubSessionIndex;
        break;
      }
    }
  }

  CaptureChunkIdList captureIds;
  merged.detachChunksTo(captureIds);
  if (captureIds.empty()) {
    return;
  }

  CommittedChunkIdList committedChunkIds;
  if (!LoopEventStore::transferCaptureChunkIdsToCommittedChunkIds(committedChunkIds, captureIds)) {
    LoopEventStore::releaseChunkRefs(captureIds);
    return;
  }

  freeActiveCapturePassChunks();

  if (preservePhase == CapturePassPhase::Record) {
    RecordPass rebuilt{};
    rebuilt.id = (preserveId != kInvalidPassId) ? preserveId : nextPassId_++;
    if (rebuilt.id >= nextPassId_) {
      nextPassId_ = rebuilt.id + 1;
    }
    rebuilt.state = CapturePassState::Active;
    rebuilt.committedChunkIds = std::move(committedChunkIds);
    passes.recordPass = rebuilt;
    lastCommittedPassId_ = rebuilt.id;
  } else {
    OverdubPass rebuilt{};
    rebuilt.id = (preserveId != kInvalidPassId) ? preserveId : nextPassId_++;
    if (rebuilt.id >= nextPassId_) {
      nextPassId_ = rebuilt.id + 1;
    }
    rebuilt.mergeSequence = preserveMergeSeq;
    rebuilt.overdubSessionIndex = preserveOverdubSessionIndex;
    rebuilt.state = CapturePassState::Active;
    rebuilt.committedChunkIds = std::move(committedChunkIds);
    passes.overdubPasses.push_back(rebuilt);
    lastCommittedPassId_ = rebuilt.id;
  }

  ++playbackRevision;
  notifyCommittedContentChanged();
}

void Loop::seedRecordPassFromStore(LoopEventStore& store) {
  resetPassTimeline();
  if (store.empty()) {
    notifyCommittedContentChanged();
    return;
  }
  CaptureChunkIdList captureIds;
  store.detachChunksTo(captureIds);
  if (captureIds.empty()) {
    notifyCommittedContentChanged();
    return;
  }
  CommittedChunkIdList committedChunkIds;
  if (!LoopEventStore::transferCaptureChunkIdsToCommittedChunkIds(committedChunkIds, captureIds)) {
    LoopEventStore::releaseChunkRefs(captureIds);
    notifyCommittedContentChanged();
    return;
  }
  RecordPass record{};
  record.id = nextPassId_++;
  record.state = CapturePassState::Active;
  record.committedChunkIds = std::move(committedChunkIds);
  passes.recordPass = std::move(record);
  lastCommittedPassId_ = passes.recordPass.id;
  ++playbackRevision;
  notifyCommittedContentChanged();
  if (loopLengthTicks > 0) {
    rebuildVisualCacheFromPasses();
  }
}

LOOP_COLD_MEM CommitResult Loop::commitCapturePass(CommitReason reason, uint32_t sealedAtTick) {
  const bool emitStopStage = reason == CommitReason::RecordStop ||
                             reason == CommitReason::RecordStopToStopped;
  const uint32_t commitStartUs = traceMicros();
  const size_t stopStageEventCount = stopPathEventCount(*this);
  const size_t stopStageChunkRefCount = stopPathChunkRefCount(*this);
  auto emitStage = [&](const char* stage, uint32_t durationUs,
                       uint32_t heapBefore, uint32_t heapAfter, const char* outcome) {
    if (!emitStopStage) {
      return;
    }
    const uint32_t elapsedUs = traceMicros() - commitStartUs;
    SC_REC_STOP_STAGE(stage, elapsedUs, durationUs, heapBefore, heapAfter,
                      stopStageEventCount, stopStageChunkRefCount, outcome);
    if (stage != nullptr && std::strcmp(stage, "publish") == 0 && outcome != nullptr &&
        std::strcmp(outcome, "ok") == 0) {
      Diagnostics::emitArchitectureMetricsSnapshot();
    }
  };

  if (capture.store.empty()) {
    emitStage("seal", 0, DebugSessionCapture::kUnsampledHeapBytes,
              DebugSessionCapture::kUnsampledHeapBytes, "skipped_empty");
    emitStage("publish", 0, DebugSessionCapture::kUnsampledHeapBytes,
              DebugSessionCapture::kUnsampledHeapBytes, "not_run");
    discardCapture();
    return CommitResult::Skipped;
  }

  const uint32_t sealStartUs = traceMicros();
  const SealOutcome seal = sealCapture(sealedAtTick, reason);
  const uint32_t sealDurationUs = traceMicros() - sealStartUs;
  emitStage("seal", sealDurationUs, DebugSessionCapture::kUnsampledHeapBytes,
            DebugSessionCapture::kUnsampledHeapBytes, sealOutcomeLabel(seal));
  if (seal != SealOutcome::Ok) {
    emitStage("publish", 0, DebugSessionCapture::kUnsampledHeapBytes,
              DebugSessionCapture::kUnsampledHeapBytes, "not_run");
    return CommitResult::SealFailed;
  }

  const uint32_t publishStartUs = traceMicros();
#if defined(SESSION_CAPTURE)
  const OverdubPublishTraceCtx publishTrace = beginOverdubPublishTrace(*this, reason);
  const uint32_t publishPendingStartUs = traceMicros();
#endif
  if (!commitPendingCapturePass(reason)) {
#if defined(SESSION_CAPTURE)
    emitOverdubPublishTraceStage(publishTrace, "seal_fx_publish_pending_pass",
                                 traceMicros() - publishPendingStartUs, "failed");
#endif
    const uint32_t publishDurationUs = traceMicros() - publishStartUs;
    emitStage("publish", publishDurationUs, DebugSessionCapture::kUnsampledHeapBytes,
              DebugSessionCapture::kUnsampledHeapBytes, "failed");
    return CommitResult::SealFailed;
  }
#if defined(SESSION_CAPTURE)
  emitOverdubPublishTraceStage(publishTrace, "seal_fx_publish_pending_pass",
                               traceMicros() - publishPendingStartUs, "ok");
#endif
  const uint32_t publishDurationUs = traceMicros() - publishStartUs;
  emitStage("publish", publishDurationUs, DebugSessionCapture::kUnsampledHeapBytes,
            DebugSessionCapture::kUnsampledHeapBytes, "ok");

  // Every committed capture pass changes painted content, not only a wrap: the display's cache/build range
  // marking is what makes a clean cache rebuild. `OverdubWrap` was the only reason this ran here, so a normal
  // stop commit left `visualCacheDirty` set with empty `dirtyBars` and the idle slice cleared it without
  // rebuilding -- a painted-note count that stayed flat while passes grew. The call is idempotent and the
  // track-level stop owners already mark their own ranges.
  markAffectedDisplayCacheRanges(lastCommittedPassId_, EditPassIdList{});
  return CommitResult::Committed;
}

void Loop::discardPendingCapturePass() {
  if (!hasPendingCapturePass_) {
    return;
  }
  LoopEventStore::releaseChunkRefs(pendingCapturePass_.committedChunkIds);
  pendingCapturePass_ = PendingCapturePass{};
  hasPendingCapturePass_ = false;
  pendingVisualDelta.clear();
}

LOOP_COLD_MEM bool Loop::takePendingCapturePass(PendingCapturePass& out) {
  if (!hasPendingCapturePass_) {
    return false;  // out untouched: the documented failure postcondition.
  }
  // Chunk references move with the receipt. `swap` exchanges storage pointers, so this is a
  // pointer exchange and never an element copy -- a move-construct would reallocate here because
  // `CommittedChunkIdAllocator` is not `is_always_equal` (see `CommittedChunkIdAllocator`).
  // Release whatever the caller's previous receipt owned first: it is being replaced.
  LoopEventStore::releaseChunkRefs(out.committedChunkIds);
  out = pendingCapturePass_;                     // scalars only; chunk list stays in the loop
  out.committedChunkIds.swap(pendingCapturePass_.committedChunkIds);
  // The loop now holds the caller's previous (released, empty) list. Assign a fresh receipt with
  // no allocation, in case the caller's list still holds storage.
  pendingCapturePass_ = PendingCapturePass{};
  hasPendingCapturePass_ = false;
  pendingVisualDelta.clear();
  return true;
}

LOOP_COLD_MEM void Loop::projectCaptureAtRecordStop(uint32_t finalLengthTicks) {
  if (capture.store.empty() || finalLengthTicks == 0) return;
  // Projection may rewrite in-place and drop rows in the same store call.
  advanceCaptureContentGeneration();
  capture.store.mapNoteOnsAtOrBeyondTickIntoLoop(finalLengthTicks);
}

LOOP_COLD_MEM LoopStopFinalize::Result Loop::finalizeCaptureWrapWindowAtStop(uint32_t stopAbsTick) {
  if (capture.store.empty() || loopLengthTicks == 0) {
    return {};
  }
  if (capture.phase != CapturePhase::Record && capture.phase != CapturePhase::Overdub) {
    return {};
  }
  uint32_t openTailCloseTick = UINT32_MAX;
  if (startLoopTick != UINT32_MAX) {
    openTailCloseTick = IntervalProjection::tickPhaseInLoop(stopAbsTick, startLoopTick,
                                                            loopLengthTicks);
  }
  const auto result = LoopStopFinalize::finalizeWrapWindowOnStore(
      capture.store, loopLengthTicks, openTailCloseTick);
  if (result.syntheticOffsInserted != 0) advanceCaptureContentGeneration();
  return result;
}

LOOP_COLD_MEM SealOutcome Loop::sealCapture(uint32_t sealedAtTick, CommitReason reason) {
  const OverdubSealTraceCtx sealTrace = beginOverdubSealTrace(*this, reason);

  if (hasPendingCapturePass_) {
    emitOverdubSealTraceStage(sealTrace, "seal_precheck", 0, "already_pending");
    return SealOutcome::AlreadyPending;
  }
  if (capture.store.empty()) {
    emitOverdubSealTraceStage(sealTrace, "seal_precheck", 0, "skipped_empty");
    return SealOutcome::SkippedEmpty;
  }
  if (!LoopEventStore::canAllocChunkWithReserve()) {
    emitOverdubSealTraceStage(sealTrace, "seal_precheck", 0, "pool_exhausted");
    return SealOutcome::PoolExhausted;
  }

#if defined(SESSION_CAPTURE)
  const uint32_t sortStartUs = traceMicros();
#endif
  const bool sorted = ensureCaptureEventsSorted();
#if defined(SESSION_CAPTURE)
  emitOverdubSealTraceStage(sealTrace, "seal_sort_capture_store", traceMicros() - sortStartUs,
                            sorted ? "sorted" : "already_sorted");
#endif

#if defined(SESSION_CAPTURE)
  const uint32_t assignIdsStartUs = traceMicros();
#endif
  if (reason != CommitReason::OverdubWrap) {
    assignMissingNoteIdsInStore(capture.store);
  }
#if defined(SESSION_CAPTURE)
  emitOverdubSealTraceStage(sealTrace, "seal_assign_note_ids", traceMicros() - assignIdsStartUs,
                            "ok");
#endif

  const char* phaseLabel = capturePhaseLabel(capture.phase);
  uint32_t minLenPairsRemoved = 0;
  uint32_t wrapSyntheticOffs = 0;

  if (loopLengthTicks > 0 &&
      (capture.phase == CapturePhase::Record || capture.phase == CapturePhase::Overdub)) {
#if defined(SESSION_CAPTURE)
    const uint32_t finalizeWrapStartUs = traceMicros();
#endif
    const LoopStopFinalize::Result fin = finalizeCaptureWrapWindowAtStop(sealedAtTick);
#if defined(SESSION_CAPTURE)
    emitOverdubSealTraceStage(sealTrace, "seal_finalize_wrap_window",
                              traceMicros() - finalizeWrapStartUs, "ok");
#endif
    wrapSyntheticOffs = static_cast<uint32_t>(fin.syntheticOffsInserted);
    // Q16 min-length is hot stop only — not OverdubWrap. Wrap must keep
    // completed short pairs (185831: 60 On@0 Off@8) so loop-head playback
    // can apply phase-0 NoteOns.
    if (reason != CommitReason::OverdubWrap) {
#if defined(SESSION_CAPTURE)
      const uint32_t minLengthStartUs = traceMicros();
#endif
      minLenPairsRemoved = static_cast<uint32_t>(
          CaptureIncrementalSanity::removePairsShorterThanNoteMinLength(
              capture.store, loopLengthTicks, noteMinLengthTicks, noteMinLengthRemoveEnabled,
              [](void* context) {
                static_cast<Loop*>(context)->advanceCaptureContentGeneration();
              }, this));
#if defined(SESSION_CAPTURE)
      emitOverdubSealTraceStage(sealTrace, "seal_remove_min_length_pairs",
                                traceMicros() - minLengthStartUs,
                                noteMinLengthRemoveEnabled ? "enabled" : "disabled");
#endif
    }
#if defined(SESSION_CAPTURE)
    const uint32_t verifyHotStopStartUs = traceMicros();
#endif
    CaptureIncrementalSanity::verifyCaptureHotStop(capture.store, loopLengthTicks);
#if defined(SESSION_CAPTURE)
    emitOverdubSealTraceStage(sealTrace, "seal_verify_hot_stop",
                              traceMicros() - verifyHotStopStartUs,
                              capture.store.empty() ? "failed_empty" : "ok");
#endif
    if (capture.store.empty()) {
      return SealOutcome::FailedValidation;
    }
  }

  SC_CAPTURE_CLEANUP(phaseLabel, "dedup", captureDedupEventsDropped_);
  SC_CAPTURE_CLEANUP(phaseLabel, "minlen", minLenPairsRemoved);
  SC_CAPTURE_CLEANUP(phaseLabel, "wrap_synth", wrapSyntheticOffs);
#if defined(SESSION_CAPTURE)
  if (wrapSyntheticOffs > 0) {
    logger.info("Seal wrap synth offs: count=%u", wrapSyntheticOffs);
  }
#endif
  captureDedupEventsDropped_ = 0;

  return stagePreparedCapturePass(sealedAtTick, reason);
}

LOOP_COLD_MEM SealOutcome Loop::stagePreparedCapturePass(uint32_t sealedAtTick,
                                                          CommitReason reason) {
  return stagePreparedCapturePassWithChunkStorage(capture, sealedAtTick, reason, nullptr);
}

LOOP_COLD_MEM bool Loop::prewarmActiveOverdubTransferHorizon(uint16_t horizon) {
  // Task 4.20: the horizon admission needs the receipt ring to *exist* — the reserved obligation is
  // allocated with it — never a free receipt slot. Requiring a free slot here coupled `OverdubStart` to
  // the snapshot publisher's cadence, so a settled shape whose receipts had not drained refused a start
  // although nothing structural was exhausted (measured: cycle 16, `receipts=8/8 admission=0`).
  if (horizon == 0 || !ensureSessionExitReceiptRing()) {
    return false;
  }

  // All allocations happen before an active overdub starts. The pass vector
  // and each ownership row use external-first allocators, keeping their
  // capacity outside the MIDI path and away from RAM1 globals.
  const size_t requiredPassRows = passes.overdubPasses.size() + horizon;
  if (passes.overdubPasses.capacity() < requiredPassRows) {
    passes.overdubPasses.reserve(requiredPassRows);
  }
  if (prewarmedCommittedChunkIdPool_.capacity() < horizon) {
    prewarmedCommittedChunkIdPool_.reserve(horizon);
  }
  while (prewarmedCommittedChunkIdPool_.size() < horizon) {
    prewarmedCommittedChunkIdPool_.emplace_back();
  }
  for (CommittedChunkIdList& row : prewarmedCommittedChunkIdPool_) {
    if (row.capacity() < PassConfig::ACTIVE_OVERDUB_TRANSFER_CHUNKS) {
      row.reserve(PassConfig::ACTIVE_OVERDUB_TRANSFER_CHUNKS);
    }
  }
  return hasActiveOverdubTransferAdmission();
}

LOOP_COLD_MEM bool Loop::hasActiveOverdubTransferAdmission() const {
  if (hasPendingCapturePass_ || prewarmedCommittedChunkIdPool_.empty()) {
    return false;
  }
  const CommittedChunkIdList& nextRow = prewarmedCommittedChunkIdPool_.back();
  return passes.overdubPasses.capacity() > passes.overdubPasses.size() &&
         nextRow.empty() &&
         nextRow.capacity() >= PassConfig::ACTIVE_OVERDUB_TRANSFER_CHUNKS;
}

LOOP_COLD_MEM size_t Loop::activeOverdubTransferRowCount() const {
  if (hasPendingCapturePass_) return 0;
  size_t rows = 0;
  for (const CommittedChunkIdList& row : prewarmedCommittedChunkIdPool_) {
    if (row.empty() && row.capacity() >= PassConfig::ACTIVE_OVERDUB_TRANSFER_CHUNKS) ++rows;
  }
  return rows;
}

LOOP_COLD_MEM size_t Loop::activeOverdubPassHeadroom() const {
  if (hasPendingCapturePass_) return 0;
  return passes.overdubPasses.capacity() - passes.overdubPasses.size();
}

LOOP_COLD_MEM bool Loop::activeOverdubTransferHorizonLow() const {
  if (hasPendingCapturePass_) return false;
  constexpr size_t kLowWater = PassConfig::ACTIVE_OVERDUB_TRANSFER_HORIZON / 2;
  return activeOverdubTransferRowCount() < kLowWater || activeOverdubPassHeadroom() < kLowWater;
}

LOOP_COLD_MEM SealOutcome Loop::stagePreparedCapturePassFromActiveAdmission(
    uint32_t sealedAtTick, CommitReason reason) {
  return stagePreparedCapturePassFromActiveAdmission(capture, sealedAtTick, reason);
}

LOOP_COLD_MEM SealOutcome Loop::stagePreparedCapturePassFromActiveAdmission(
    Capture& source, uint32_t sealedAtTick, CommitReason reason) {
  if (!hasActiveOverdubTransferAdmission()) {
    return SealOutcome::PoolExhausted;
  }
  if (source.store.empty()) return SealOutcome::SkippedEmpty;
  // Active transfer may only use the admitted directory. Reject before moving
  // its row or sealing source chunks; the legacy detach fallback can allocate.
  if (source.store.chunkIds().size() > prewarmedCommittedChunkIdPool_.back().capacity()) {
    return SealOutcome::PoolExhausted;
  }

  // Remove the row before staging so no active continuation can consume the
  // same capacity twice. Its vector storage moves into the pending candidate.
  CommittedChunkIdList prewarmedChunkIds =
      std::move(prewarmedCommittedChunkIdPool_.back());
  prewarmedCommittedChunkIdPool_.pop_back();
  return stagePreparedCapturePassWithChunkStorage(source, sealedAtTick, reason,
                                                   &prewarmedChunkIds);
}

LOOP_COLD_MEM SealOutcome Loop::stagePreparedCapturePassWithChunkStorage(
    Capture& source, uint32_t sealedAtTick, CommitReason reason, CommittedChunkIdList* prewarmedChunkIds) {
  if (hasPendingCapturePass_) {
    return SealOutcome::AlreadyPending;
  }
  if (source.store.empty()) {
    return SealOutcome::SkippedEmpty;
  }
  if (prewarmedChunkIds == nullptr && !LoopEventStore::canAllocChunkWithReserve()) {
    return SealOutcome::PoolExhausted;
  }

  const CapturePassPhase phase = effectiveCapturePassPhase(source.phase, passes.hasRecordPass());

  pendingCapturePass_ = PendingCapturePass{};
  if (prewarmedChunkIds != nullptr) {
    pendingCapturePass_.committedChunkIds = std::move(*prewarmedChunkIds);
  }
  pendingCapturePass_.retiresLiveCapture = &source == &capture;
  pendingCapturePass_.id = nextPassId_++;
  pendingCapturePass_.mergeSequence = nextMergeSequence_++;
  pendingCapturePass_.phase = phase;
  pendingCapturePass_.sealedAtTick = sealedAtTick;
  pendingCapturePass_.overdubSessionIndex =
      (phase == CapturePassPhase::Overdub && hasOverdubSession())
          ? currentOverdubSessionIndex_
          : kUngroupedOverdubSessionIndex;
  if (&source == &capture) advanceCaptureContentGeneration();
  if (!source.store.detachChunksToCommittedChunkIds(pendingCapturePass_.committedChunkIds)) {
    pendingCapturePass_ = PendingCapturePass{};
    return SealOutcome::FailedValidation;
  }

  if (pendingCapturePass_.committedChunkIds.empty()) {
    pendingCapturePass_ = PendingCapturePass{};
    return SealOutcome::FailedValidation;
  }

  if (&source == &capture) pendingVisualDelta.clear();
  hasPendingCapturePass_ = true;
  // Legacy staging may establish canonical capacity here. Active admission
  // arrives with that proof from the STOPPED-only horizon and must only verify
  // it; neither branch grows storage at the later ownership handoff.
  const bool transferPrepared = prewarmedChunkIds != nullptr
                                    ? canTransferPendingCapturePassWithoutAllocation()
                                    : preparePendingCapturePassTransfer();
  if (!transferPrepared) {
    discardPendingCapturePass();
    return SealOutcome::FailedValidation;
  }
  if (&source != &capture) source.phase = CapturePhase::None;
  return SealOutcome::Ok;
}

LOOP_COLD_MEM SealOutcome Loop::beginPreparedCapturePassFromActiveAdmission(
    Capture& source, PendingCapturePass& pass, LoopEventStoreCommitCursor& cursor,
    const uint64_t& generation, uint32_t sealedAtTick) {
  if (&source == &capture || pass.id != kInvalidPassId ||
      !pass.committedChunkIds.empty() || cursor.active()) return SealOutcome::AlreadyPending;
  if (!hasActiveOverdubTransferAdmission()) return SealOutcome::PoolExhausted;
  if (source.store.empty()) return SealOutcome::SkippedEmpty;
  auto& row = prewarmedCommittedChunkIdPool_.back();
  if (source.store.chunkIds().size() > row.capacity()) return SealOutcome::PoolExhausted;
  pass.committedChunkIds = std::move(row);
  if (!source.store.beginCommittedChunkTransfer(cursor, pass.committedChunkIds, generation)) {
    row = std::move(pass.committedChunkIds);
    return SealOutcome::FailedValidation;
  }
  prewarmedCommittedChunkIdPool_.pop_back();
  pass.preparationPending = true;
  pass.retiresLiveCapture = false;
  pass.id = nextPassId_++;
  pass.mergeSequence = nextMergeSequence_++;
  pass.phase = effectiveCapturePassPhase(source.phase, passes.hasRecordPass());
  pass.sealedAtTick = sealedAtTick;
  pass.overdubSessionIndex = (pass.phase == CapturePassPhase::Overdub && hasOverdubSession())
      ? currentOverdubSessionIndex_ : kUngroupedOverdubSessionIndex;
  return SealOutcome::Ok;
}

LOOP_COLD_MEM bool Loop::prewarmPreparedPassTransferChunks(size_t chunks) {
  if (chunks == 0 || prewarmedCommittedChunkIdPool_.empty()) {
    return false;
  }
  CommittedChunkIdList& row = prewarmedCommittedChunkIdPool_.back();
  if (!row.empty()) return false;
  // The store owner owns failure-aware growth: a bare `push_back` cannot report a
  // null allocate and a runtime `reserve` instantiation also costs 320 B of ITCM.
  return LoopEventStore::prewarmCommittedChunkIdCapacity(row, chunks);
}

LOOP_COLD_MEM LoopEventStoreRewriteResult Loop::stepPreparedCapturePass(
    Capture& source, PendingCapturePass& pass, LoopEventStoreCommitCursor& cursor,
    size_t quantum) {
  if (pass.id == kInvalidPassId || !cursor.targets(source.store, pass.committedChunkIds))
    return LoopEventStoreRewriteResult::Denied;
  const auto result = source.store.stepCommittedChunkTransfer(cursor, quantum);
  if (result == LoopEventStoreRewriteResult::Complete) {
    pass.preparationPending = false;
    source.phase = CapturePhase::None;
  }
  return result;
}

LOOP_COLD_MEM bool Loop::preparePendingCapturePassTransfer() {
  if (!hasPendingCapturePass_) {
    return false;
  }
  if (pendingCapturePass_.phase == CapturePassPhase::Record) {
    return !passes.hasRecordPass();
  }
  // This is deliberately separate from transferPreparedCapturePass(): vector growth is
  // preparation work and must never be smuggled into a MIDI-boundary transfer grain.
  passes.overdubPasses.reserve(passes.overdubPasses.size() + 1u);
  return passes.overdubPasses.capacity() > passes.overdubPasses.size();
}

LOOP_COLD_MEM bool Loop::canTransferPendingCapturePassWithoutAllocation() const {
  if (!hasPendingCapturePass_) {
    return false;
  }
  return canTransferCapturePassWithoutAllocation(pendingCapturePass_);
}

LOOP_COLD_MEM bool Loop::canTransferCapturePassWithoutAllocation(
    const PendingCapturePass& pass) const {
  if (pass.id == kInvalidPassId || pass.preparationPending) {
    return false;
  }
  if (pass.phase == CapturePassPhase::Record) {
    return !passes.hasRecordPass();
  }
  return passes.overdubPasses.capacity() > passes.overdubPasses.size();
}

LOOP_COLD_MEM bool Loop::transferPreparedCapturePass(PreparedCapturePassTransfer& transferred) {
  if (!hasPendingCapturePass_) {
    return false;
  }
  if (!transferCapturePass(pendingCapturePass_, transferred)) {
    return false;
  }
  hasPendingCapturePass_ = false;
  pendingVisualDelta.clear();
  return true;
}

LOOP_COLD_MEM bool Loop::transferCapturePass(PendingCapturePass& pass,
                                             PreparedCapturePassTransfer& transferred) {
  if (!canTransferCapturePassWithoutAllocation(pass)) {
    return false;
  }

  // Preserve only scalar metadata. Chunk refs move exactly once, from the prepared pass
  // into its canonical owner below.
  transferred = PreparedCapturePassTransfer{};
  transferred.retiresLiveCapture = pass.retiresLiveCapture;
  transferred.id = pass.id;
  transferred.phase = pass.phase;
  transferred.mergeSequence = pass.mergeSequence;
  transferred.sealedAtTick = pass.sealedAtTick;
  transferred.overdubSessionIndex = pass.overdubSessionIndex;

  if (pass.phase == CapturePassPhase::Record) {
    RecordPass record{};
    record.id = pass.id;
    record.state = CapturePassState::Active;
    record.sealedAtTick = pass.sealedAtTick;
    record.committedChunkIds = std::move(pass.committedChunkIds);
    passes.recordPass = std::move(record);
  } else {
    OverdubPass overdub{};
    overdub.id = pass.id;
    overdub.mergeSequence = pass.mergeSequence;
    overdub.state = CapturePassState::Active;
    overdub.sealedAtTick = pass.sealedAtTick;
    overdub.overdubSessionIndex = pass.overdubSessionIndex;
    overdub.committedChunkIds = std::move(pass.committedChunkIds);
    // Capacity was proven above; this placement cannot grow the vector.
    passes.overdubPasses.push_back(std::move(overdub));
  }

  lastCommittedPassId_ = transferred.id;
  // The pass is now canonical. Derived playback/view invalidation stays with the
  // deferred post-commit owner, but readers can use this monotonically increasing
  // identity immediately to reject an old immutable projection.
  ++playbackRevision;
  // Retire the consumed receipt. The loop-slot caller clears `hasPendingCapturePass_`
  // in `transferPreparedCapturePass`; an interval-owned pass is retired by its owner.
  pass = PendingCapturePass{};
  return true;
}

LOOP_COLD_MEM bool Loop::commitPreparedCapturePassAtomic(
    PreparedCapturePassTransfer& transferred) {
  if (!transferPreparedCapturePass(transferred)) {
    return false;
  }
  completePreparedCapturePassAtomic(transferred.retiresLiveCapture);
  return true;
}

LOOP_COLD_MEM bool Loop::commitPreparedCapturePassAtomic(
    PreparedCapturePassTransfer& transferred, PendingCapturePass& pass) {
  if (!transferCapturePass(pass, transferred)) {
    return false;
  }
  completePreparedCapturePassAtomic(transferred.retiresLiveCapture);
  return true;
}

void Loop::completePreparedCapturePassAtomic(bool retiresLiveCapture) {
  // The prepared pass has already detached every mutable chunk. This handoff
  // changes only scalar capture ownership and derived invalidation state.
  if (retiresLiveCapture) {
    if (!capture.store.empty()) advanceCaptureContentGeneration();
    capture.store.clear();
    capture.phase = CapturePhase::None;
    captureNextEventIndex = 0;
    captureEventsSortDirty = false;
    captureHasLastAppendTick_ = false;
    captureMaximumTick_ = 0;
  }
  notifyCommittedContentChanged();
}

LOOP_COLD_MEM void Loop::completePreparedCapturePassDeferredCleanup() {
  completePreparedCapturePassDeferredCleanup(capturePreview);
}

LOOP_COLD_MEM void Loop::completePreparedCapturePassDeferredCleanup(CapturePreview& source) {
  // Seal handoff (150650 / 120321): keep uncovered sealed rows and merge this
  // pass's closed preview. This is intentionally outside the atomic transfer.
  retainCapturePreviewAtSeal(source);
  source.clear();
  // Wrap and stop commit keep the session source view. Clearing here makes
  // applyPendingNoteChangesToOverdubSourceView a no-op and beginCapture
  // re-establishes from a dirty cache (000417 src,why=open every wrap).
  if (&source == &capturePreview && !hasOverdubSession()) {
    clearOverdubSourceView();
  }
}

LOOP_COLD_MEM bool Loop::commitPendingCapturePass(CommitReason reason) {
  const OverdubPublishTraceCtx publishTrace = beginOverdubPublishTrace(*this, reason);
  if (!hasPendingCapturePass_) {
    emitOverdubPublishTraceStage(publishTrace, "seal_publish_precheck", 0, "no_pending");
    return false;
  }

  // sealCapture() establishes capacity while the candidate is prepared. A
  // boundary caller may transfer only a candidate that retained that proof.
  if (!canTransferPendingCapturePassWithoutAllocation()) {
    emitOverdubPublishTraceStage(publishTrace, "seal_publish_prepare_transfer", 0,
                                 "capacity_not_prepared");
    return false;
  }
  PreparedCapturePassTransfer pendingPass{};
#if defined(SESSION_CAPTURE)
  const uint32_t promotePassStartUs = traceMicros();
#endif
  if (!commitPreparedCapturePassAtomic(pendingPass)) {
    emitOverdubPublishTraceStage(publishTrace, "seal_publish_promote_pass", 0, "failed");
    return false;
  }
#if defined(SESSION_CAPTURE)
  emitOverdubPublishTraceStage(publishTrace, "seal_publish_promote_pass",
                               traceMicros() - promotePassStartUs, "ok");
#endif
#if defined(SESSION_CAPTURE)
  const uint32_t spanCacheRestampStartUs = traceMicros();
#endif
  bool spanCacheCanRestamp = hasOverdubSession() && overdubSourceSpanCacheValid_ &&
                             overdubSourceSpanCacheLoopLengthTicks_ == loopLengthTicks;
  bool spanCacheRestampAttempted = false;
  const char* spanCacheRestampOutcome = "skipped";
  if (spanCacheCanRestamp && pendingPass.phase == CapturePassPhase::Overdub &&
      !passes.overdubPasses.empty()) {
    spanCacheRestampAttempted = true;
    const size_t committedPassEventCount =
        LoopEventStore::countEventsInChunkIds(passes.overdubPasses.back().committedChunkIds);
    if (committedPassEventCount > kMaxEventsForSpanCacheRestamp) {
      spanCacheCanRestamp = false;
      spanCacheRestampOutcome = "deferred_event_budget";
    }
    if (spanCacheCanRestamp) {
      SessionMidiEventVec committedPassEvents;
      LoopEventStore::appendChunkRefEvents(passes.overdubPasses.back().committedChunkIds,
                                           committedPassEvents);
      if (!committedPassEvents.empty()) {
        if (capturePassContainsWrappedHeadTailPairs(committedPassEvents, loopLengthTicks)) {
          spanCacheCanRestamp = false;
          spanCacheRestampOutcome = "invalidated_wrap_pair";
        }
        if (spanCacheCanRestamp) {
          std::vector<NoteId> spanCacheNoteIds;
          collectSortedSpanCacheNoteIds(overdubSourceSpanCacheNotes_, spanCacheNoteIds);
          std::vector<NoteId> missingNoteIds;
          missingNoteIds.reserve(committedPassEvents.size());
          for (const MidiEvent& evt : committedPassEvents) {
            if (!evt.isNoteOn() || evt.noteId == kInvalidNoteId) {
              continue;
            }
            if (!std::binary_search(spanCacheNoteIds.begin(), spanCacheNoteIds.end(), evt.noteId)) {
              missingNoteIds.push_back(evt.noteId);
            }
          }
          if (!missingNoteIds.empty()) {
            std::sort(missingNoteIds.begin(), missingNoteIds.end());
            missingNoteIds.erase(std::unique(missingNoteIds.begin(), missingNoteIds.end()),
                                 missingNoteIds.end());
            NoteUtils::DisplayNoteVec committedPassNotes = NoteUtils::reconstructDisplayNotes(
                committedPassEvents, loopLengthTicks, false, false, true);
            for (const NoteUtils::DisplayNote& candidate : committedPassNotes) {
              if (candidate.noteId == kInvalidNoteId) {
                continue;
              }
              if (!std::binary_search(missingNoteIds.begin(), missingNoteIds.end(),
                                      candidate.noteId)) {
                continue;
              }
              if (insertSortedUniqueNoteId(spanCacheNoteIds, candidate.noteId)) {
                overdubSourceSpanCacheNotes_.push_back(candidate);
              }
            }
            for (const NoteId noteId : missingNoteIds) {
              if (std::binary_search(spanCacheNoteIds.begin(), spanCacheNoteIds.end(), noteId)) {
                continue;
              }
              spanCacheCanRestamp = false;
              spanCacheRestampOutcome = "invalidated_missing_id";
              break;
            }
          }
        }
      }
    }
  }
  if (spanCacheRestampAttempted && !spanCacheCanRestamp) {
    // Do not reuse a stale pre-commit cache after a failed restamp.
    overdubSourceSpanCacheValid_ = false;
    overdubSourceSpanCachePublishedRevision_ = 0;
    overdubSourceSpanCachePublishedIdentity_ = nullptr;
  }
#if defined(SESSION_CAPTURE)
  if (spanCacheCanRestamp && spanCacheRestampAttempted) {
    spanCacheRestampOutcome = "ok";
  } else if (!spanCacheRestampAttempted) {
    spanCacheRestampOutcome = "skipped";
  } else if (spanCacheRestampOutcome == nullptr ||
             std::strcmp(spanCacheRestampOutcome, "skipped") == 0) {
    spanCacheRestampOutcome = "invalidated";
  }
  emitOverdubPublishTraceStage(publishTrace, "seal_publish_span_cache_restamp",
                               traceMicros() - spanCacheRestampStartUs,
                               spanCacheRestampOutcome);
#endif

#if defined(SESSION_CAPTURE)
  const uint32_t finalizeStateStartUs = traceMicros();
#endif
  if ((reason == CommitReason::RecordStop ||
       reason == CommitReason::RecordStopToStopped) &&
      passes.hasRecordPass() && passes.recordPass.id == lastCommittedPassId_) {
    queueSessionExitSnapshot(reason, {});
  }
  if (spanCacheCanRestamp) {
    overdubSourceSpanCachePlaybackRevision_ = playbackRevision;
  }
  pendingVisualDelta.clear();
#if defined(SESSION_CAPTURE)
  emitOverdubPublishTraceStage(publishTrace, "seal_publish_finalize_state",
                               traceMicros() - finalizeStateStartUs, "ok");
#endif

#if defined(SESSION_CAPTURE)
  const uint32_t clearCaptureStartUs = traceMicros();
#endif
  completePreparedCapturePassDeferredCleanup();
#if defined(SESSION_CAPTURE)
  emitOverdubPublishTraceStage(publishTrace, "seal_publish_clear_capture",
                               traceMicros() - clearCaptureStartUs, "ok");
#endif

  return true;
}
