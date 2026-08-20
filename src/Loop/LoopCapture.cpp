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
#include "Utils/Diagnostics.h"
#include "Utils/DisplayWindowUtils.h"
#include "Utils/IntervalProjection.h"
#include "Utils/LoopMem.h"
#include "Utils/LoopStopFinalize.h"
#include "Utils/MemoryMonitor.h"
#include "Utils/NoteUtils.h"
#include "PlaybackMergedMidiEvents.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

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
  if (passes.hasRecordPass() && passes.recordPass.id == id) {
    if (passes.recordPass.state == state) {
      return true;
    }
    passes.recordPass.state = state;
    LoopContentResolution::setPreparedCapturePassState(id, state);
    ++playbackRevision;
    LoopContentResolution::restampPreparedPlaybackRevision(playbackRevision);
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

void Loop::assignMissingNoteIdsInStore(LoopEventStore& store) {
  store.assignMissingNoteIdsToNoteOns([this]() { return allocateNoteId(); });
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

void Loop::mergeDisplayNotesIntoOverdubSourceView(const NoteUtils::DisplayNoteVec& candidates) {
  for (const NoteUtils::DisplayNote& candidate : candidates) {
    if (candidate.noteId == kInvalidNoteId) {
      continue;
    }
    bool found = false;
    for (const NoteUtils::DisplayNote& existing : overdubSourceViewNotes_) {
      if (existing.noteId == candidate.noteId) {
        found = true;
        break;
      }
    }
    if (!found) {
      overdubSourceViewNotes_.push_back(candidate);
    }
  }
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
  overdubSourceSpanCacheValid_ = true;
  overdubSourceViewLoopLengthTicks_ = loopLengthTicks;
  overdubSourceViewEstablished_ = true;
}
#endif

LOOP_COLD_MEM void Loop::establishOverdubSourceView(uint32_t playheadPhaseTick) {
  overlapHoldTotals_ = {};
  rebuildOverdubSourceView(playheadPhaseTick, "open");
  clearPendingNoteChanges();
}

LOOP_COLD_MEM bool Loop::overdubSourceSpanCacheReady() const {
  if (!overdubSourceSpanCacheValid_) {
    return false;
  }
  if (overdubSourceSpanCacheLoopLengthTicks_ != loopLengthTicks) {
    return false;
  }
  return overdubSourceSpanCachePlaybackRevision_ == playbackRevision;
}

LOOP_COLD_MEM void Loop::rebuildOverdubSourceSpanCache() {
  NoteUtils::DisplayNoteVec priorSpanCacheNotes;
  const bool keepPriorSpanCache =
      hasOverdubSession() && overdubSourceSpanCacheValid_ &&
      overdubSourceSpanCacheLoopLengthTicks_ == loopLengthTicks &&
      !overdubSourceSpanCacheNotes_.empty();
  if (keepPriorSpanCache) {
    priorSpanCacheNotes.swap(overdubSourceSpanCacheNotes_);
  }
  overdubSourceSpanCacheNotes_.clear();
  overdubSourceSpanCacheLoopLengthTicks_ = loopLengthTicks;
  overdubSourceSpanCachePlaybackRevision_ = playbackRevision;
  overdubSourceSpanCacheValid_ = true;
  if (loopLengthTicks == 0) {
    return;
  }
  const bool copiedPreparedSpans = LoopContentResolution::tryCopyPreparedSpansToDisplayNotes(
      playbackRevision, overdubSourceSpanCacheNotes_, nullptr, loopLengthTicks, false);
  if (!copiedPreparedSpans) {
    if (hasOverdubSession()) {
      // Overdub start/wrap must stay deterministic. Reuse prior cache notes when prepared spans
      // are unavailable instead of forcing a full materialize or fresh allocation on this
      // timing-critical path.
      if (!priorSpanCacheNotes.empty()) {
        overdubSourceSpanCacheNotes_.swap(priorSpanCacheNotes);
      }
    } else {
      SessionMidiEventVec fullResolvedEvents;
      passes.materializeToEventVector(fullResolvedEvents, loopLengthTicks);
      overdubSourceSpanCacheNotes_ =
          NoteUtils::reconstructDisplayNotes(fullResolvedEvents, loopLengthTicks, false, false);
      appendOverdubPassWrapPairedNotes(overdubSourceSpanCacheNotes_);
    }
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
}

LOOP_COLD_MEM void Loop::rebuildOverdubSourceView(uint32_t playheadPhaseTick, const char* why) {
  if (why == nullptr || why[0] == '\0') {
    why = "wrap";
  }
#if defined(SESSION_CAPTURE) && defined(ARDUINO)
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
  uint32_t windowStart = 0;
  uint32_t windowLength = 0;
  resolveOverdubSourceWindow(playheadPhaseTick, windowStart, windowLength);
  const char* from = "cache";
  if (!overdubSourceSpanCacheReady()) {
    rebuildOverdubSourceSpanCache();
  }
#if defined(SESSION_CAPTURE) && defined(ARDUINO)
  const uint32_t filterStartUs = micros();
#endif
  auto noteOnTickInWindow = [&](uint32_t tick) {
    if (windowLength >= loopLengthTicks) {
      return true;
    }
    const uint32_t windowEnd = windowStart + windowLength;
    if (windowEnd <= loopLengthTicks) {
      return tick >= windowStart && tick < windowEnd;
    }
    const uint32_t wrappedEnd = windowEnd % loopLengthTicks;
    return tick >= windowStart || tick < wrappedEnd;
  };
  for (const NoteUtils::DisplayNote& note : overdubSourceSpanCacheNotes_) {
    if (note.noteId == kInvalidNoteId) {
      continue;
    }
    if (!noteOnTickInWindow(note.startTick)) {
      continue;
    }
    overdubSourceViewNotes_.push_back(note);
  }
#if defined(PIO_UNIT_TEST_NATIVE)
  copyEffectiveCommittedEventsInRange(overdubSourceViewEvents_, windowStart, windowLength);
#endif
  overdubSourceViewEstablished_ = true;
#if defined(SESSION_CAPTURE) && defined(ARDUINO)
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
      snprintf(skipLine, sizeof(skipLine),
               "#CAP,%lu,DIAG,lcr,srcskip,reason=stale_identity_stream,built=%u,loop=%u",
               static_cast<unsigned long>(micros()),
               static_cast<unsigned>(committedPlaybackMergedForIdentity_->builtFromRevision),
               static_cast<unsigned>(playbackRevision));
      DebugSessionCapture::appendCaptureTextLine(skipLine);
    }
  }
#endif
  retainValidOverdubSourceViewIdentities();
#if defined(SESSION_CAPTURE) && defined(ARDUINO)
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
      snprintf(dropLine, sizeof(dropLine), "#CAP,%lu,DIAG,lcr,srcdrop,id=%u,reason=identity_invalid",
               static_cast<unsigned long>(micros()), static_cast<unsigned>(id));
      DebugSessionCapture::appendCaptureTextLine(dropLine);
      ++dropLogged;
    }
    for (size_t i = 0; i < beforeCount && deltaLogged < kMaxSrcDeltaLogs; ++i) {
      const NoteId id = beforeIds[i];
      if (sourceViewHasId(id)) {
        continue;
      }
      const char* reason = "missing_cache_span";
      if (idInList(id, beforeRetainIds, beforeRetainCount)) {
        reason = "identity_invalid";
      } else if (LoopContentResolution::preparedCheckpointHasNoteId(id)) {
        reason = "cache_filter";
      }
      char deltaLine[112];
      snprintf(deltaLine, sizeof(deltaLine), "#CAP,%lu,DIAG,lcr,srcdelta,id=%u,reason=%s",
               static_cast<unsigned long>(micros()), static_cast<unsigned>(id), reason);
      DebugSessionCapture::appendCaptureTextLine(deltaLine);
      ++deltaLogged;
    }
    char afterLine[112];
    snprintf(afterLine, sizeof(afterLine),
             "#CAP,%lu,DIAG,lcr,srcafter,why=%s,from=%s,before=%u,copy=%u,after=%u",
             static_cast<unsigned long>(micros()), why, from, static_cast<unsigned>(beforeCount),
             static_cast<unsigned>(beforeRetainCount),
             static_cast<unsigned>(overdubSourceViewNotes_.size()));
    DebugSessionCapture::appendCaptureTextLine(afterLine);
  }
#endif
#if defined(SESSION_CAPTURE) && defined(ARDUINO)
  char line[256];
  snprintf(line, sizeof(line),
           "#CAP,%lu,DIAG,lcr,src,why=%s,from=%s,win=0,proj=%lu,tot=%lu,ev=%u,notes=%u,bars=%u,"
           "live=%lu,prep=%lu",
           static_cast<unsigned long>(micros()), why, from,
           static_cast<unsigned long>(filterUs), static_cast<unsigned long>(filterUs),
           static_cast<unsigned>(overdubSourceViewEvents_.size()),
           static_cast<unsigned>(overdubSourceViewNotes_.size()),
           static_cast<unsigned>(kOverdubSourceWindowBars),
           static_cast<unsigned long>(loopLengthTicks),
           static_cast<unsigned long>(LoopContentResolution::deviceGateLoopLengthTicks()));
  DebugSessionCapture::appendCaptureTextLine(line);
#else
  (void)from;
  (void)why;
#endif
}

LOOP_COLD_MEM void Loop::clearOverdubSourceView() {
  overdubSourceViewEvents_.clear();
  overdubSourceViewNotes_.clear();
  overdubSourceViewLoopLengthTicks_ = 0;
  overdubSourceViewEstablished_ = false;
}

LOOP_COLD_MEM void Loop::prewarmOverdubSourceSpanCache() {
  if (captureActive() || hasPendingCapturePass_ || hasOverdubSession()) {
    return;
  }
  if (loopLengthTicks == 0 || !hasCommittedPasses()) {
    return;
  }
  if (overdubSourceSpanCacheReady()) {
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

void Loop::beginCapture(CapturePhase phase, uint32_t playheadPhaseTick) {
  discardPendingCapturePass();
  capture.phase = phase;
  capture.store.clear();
  capturePreview.clear();
  captureNextEventIndex = 0;
  captureEventsSortDirty = false;
  captureDedupEventsDropped_ = 0;
  ++captureDisplayRevision;
  if (phase == CapturePhase::Overdub) {
    // Wrap beginCapture must keep the session source view. Re-establish after
    // invalidateCaches drops this-session notes (235407 inner / same-start-longer).
    if (!overdubSourceViewEstablished_) {
      establishOverdubSourceView(playheadPhaseTick);
    }
  } else {
    clearOverdubSourceView();
  }
}

void Loop::discardCapture() {
  capture.store.clear();
  capture.phase = CapturePhase::None;
  captureNextEventIndex = 0;
  captureEventsSortDirty = false;
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
  LoopEventStoreAppendDeny storeDeny = LoopEventStoreAppendDeny::None;
  if (!capture.store.append(evt, &storeDeny)) {
    result.reason =
        storeDeny == LoopEventStoreAppendDeny::PoolExhausted
            ? CaptureAppendDenyReason::PoolAlloc
            : CaptureAppendDenyReason::StoreOther;
    return result;
  }
  captureEventsSortDirty = true;
  applyCaptureEventToPreview(capturePreview, evt, Config::TICKS_PER_BAR, loopLengthTicks);
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
  for (const MidiEvent& evt : flat) {
    if (!removed && evt.isNoteOn() && evt.channel == channel &&
        evt.data.noteData.note == note && evt.tick == openTick &&
        evt.data.noteData.velocity > 0) {
      removed = true;
      continue;
    }
    kept.push_back(evt);
  }
  if (!removed) {
    return false;
  }

  capture.store.clear();
  if (!kept.empty()) {
    capture.store.loadFromEvents(kept);
  }
  captureEventsSortDirty = false;
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
  MidiEventVec flat;
  passes.materializeToEventVector(flat, loopLengthTicks);
  size_t count = flat.size();
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
  sortCaptureStoreByTick(capture.store);
  captureEventsSortDirty = false;
  return true;
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

CommitResult Loop::commitCapturePass(CommitReason reason, uint32_t sealedAtTick) {
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
    const uint32_t heap = MemoryMonitor::getInternalHeapFreeBytes();
    emitStage("seal", 0, heap, heap, "skipped_empty");
    emitStage("publish", 0, heap, heap, "not_run");
    discardCapture();
    return CommitResult::Skipped;
  }

  const uint32_t sealHeapBefore = MemoryMonitor::getInternalHeapFreeBytes();
  const uint32_t sealStartUs = traceMicros();
  const SealOutcome seal = sealCapture(sealedAtTick, reason);
  const uint32_t sealDurationUs = traceMicros() - sealStartUs;
  const uint32_t sealHeapAfter = MemoryMonitor::getInternalHeapFreeBytes();
  emitStage("seal", sealDurationUs, sealHeapBefore, sealHeapAfter, sealOutcomeLabel(seal));
  if (seal != SealOutcome::Ok) {
    emitStage("publish", 0, sealHeapAfter, sealHeapAfter, "not_run");
    return CommitResult::SealFailed;
  }

  const uint32_t publishHeapBefore = MemoryMonitor::getInternalHeapFreeBytes();
  const uint32_t publishStartUs = traceMicros();
  if (!commitPendingCapturePass()) {
    const uint32_t publishDurationUs = traceMicros() - publishStartUs;
    const uint32_t publishHeapAfter = MemoryMonitor::getInternalHeapFreeBytes();
    emitStage("publish", publishDurationUs, publishHeapBefore, publishHeapAfter, "failed");
    return CommitResult::SealFailed;
  }
  const uint32_t publishDurationUs = traceMicros() - publishStartUs;
  const uint32_t publishHeapAfter = MemoryMonitor::getInternalHeapFreeBytes();
  emitStage("publish", publishDurationUs, publishHeapBefore, publishHeapAfter, "ok");

  notifyCommittedContentChanged();
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

LoopStopFinalize::Result Loop::finalizeCaptureWrapWindowAtStop(uint32_t stopAbsTick) {
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
  return LoopStopFinalize::finalizeWrapWindowOnStore(capture.store, loopLengthTicks,
                                                     openTailCloseTick);
}

SealOutcome Loop::sealCapture(uint32_t sealedAtTick, CommitReason reason) {
  if (hasPendingCapturePass_) {
    return SealOutcome::AlreadyPending;
  }
  if (capture.store.empty()) {
    return SealOutcome::SkippedEmpty;
  }
  if (!LoopEventStore::canAllocChunkWithReserve()) {
    return SealOutcome::PoolExhausted;
  }

  ensureCaptureEventsSorted();
  assignMissingNoteIdsInStore(capture.store);

  const char* phaseLabel = capturePhaseLabel(capture.phase);
  uint32_t minLenPairsRemoved = 0;
  uint32_t wrapSyntheticOffs = 0;

  if (loopLengthTicks > 0 &&
      (capture.phase == CapturePhase::Record || capture.phase == CapturePhase::Overdub)) {
    const LoopStopFinalize::Result fin = finalizeCaptureWrapWindowAtStop(sealedAtTick);
    wrapSyntheticOffs = static_cast<uint32_t>(fin.syntheticOffsInserted);
    // Q16 min-length is hot stop only — not OverdubWrap. Wrap must keep
    // completed short pairs (185831: 60 On@0 Off@8) so loop-head playback
    // can apply phase-0 NoteOns.
    if (reason != CommitReason::OverdubWrap) {
      minLenPairsRemoved = static_cast<uint32_t>(
          CaptureIncrementalSanity::removePairsShorterThanNoteMinLength(
              capture.store, loopLengthTicks, noteMinLengthTicks, noteMinLengthRemoveEnabled));
    }
    CaptureIncrementalSanity::verifyCaptureHotStop(capture.store, loopLengthTicks);
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

  const CapturePassPhase phase =
      effectiveCapturePassPhase(capture.phase, passes.hasRecordPass());

  pendingCapturePass_ = PendingCapturePass{};
  pendingCapturePass_.id = nextPassId_++;
  pendingCapturePass_.mergeSequence = nextMergeSequence_++;
  pendingCapturePass_.phase = phase;
  pendingCapturePass_.sealedAtTick = sealedAtTick;
  pendingCapturePass_.overdubSessionIndex =
      (phase == CapturePassPhase::Overdub && hasOverdubSession())
          ? currentOverdubSessionIndex_
          : kUngroupedOverdubSessionIndex;
  if (!capture.store.detachChunksToCommittedChunkIds(pendingCapturePass_.committedChunkIds)) {
    pendingCapturePass_ = PendingCapturePass{};
    return SealOutcome::FailedValidation;
  }

  if (pendingCapturePass_.committedChunkIds.empty()) {
    pendingCapturePass_ = PendingCapturePass{};
    return SealOutcome::FailedValidation;
  }

  pendingVisualDelta.clear();
  hasPendingCapturePass_ = true;
  return SealOutcome::Ok;
}

LOOP_COLD_MEM bool Loop::commitPendingCapturePass() {
  if (!hasPendingCapturePass_) {
    return false;
  }

  PendingCapturePass pendingPass = std::move(pendingCapturePass_);
  if (pendingPass.phase == CapturePassPhase::Record) {
    RecordPass record{};
    record.id = pendingPass.id;
    record.state = CapturePassState::Active;
    record.sealedAtTick = pendingPass.sealedAtTick;
    record.committedChunkIds = std::move(pendingPass.committedChunkIds);
    passes.recordPass = std::move(record);
  } else {
    OverdubPass overdub{};
    overdub.id = pendingPass.id;
    overdub.mergeSequence = pendingPass.mergeSequence;
    overdub.state = CapturePassState::Active;
    overdub.sealedAtTick = pendingPass.sealedAtTick;
    overdub.overdubSessionIndex = pendingPass.overdubSessionIndex;
    overdub.committedChunkIds = std::move(pendingPass.committedChunkIds);
    passes.overdubPasses.push_back(std::move(overdub));
  }
  lastCommittedPassId_ = pendingPass.id;

  pendingCapturePass_ = PendingCapturePass{};
  hasPendingCapturePass_ = false;

  bool spanCacheCanRestamp = hasOverdubSession() && overdubSourceSpanCacheValid_ &&
                             overdubSourceSpanCacheLoopLengthTicks_ == loopLengthTicks;
  if (spanCacheCanRestamp && pendingPass.phase == CapturePassPhase::Overdub &&
      !passes.overdubPasses.empty()) {
    SessionMidiEventVec committedPassEvents;
    LoopEventStore::appendChunkRefEvents(passes.overdubPasses.back().committedChunkIds,
                                         committedPassEvents);
    if (!committedPassEvents.empty()) {
      for (const MidiEvent& evt : committedPassEvents) {
        if (!evt.isNoteOff()) {
          continue;
        }
        const uint8_t pitch = evt.data.noteData.note;
        for (const MidiEvent& later : committedPassEvents) {
          if (!later.isNoteOn() || later.channel != evt.channel ||
              later.data.noteData.note != pitch) {
            continue;
          }
          if (later.tick <= evt.tick) {
            continue;
          }
          if (NoteUtils::isHeadTailWrappedPair(later.tick, evt.tick, loopLengthTicks)) {
            spanCacheCanRestamp = false;
            break;
          }
        }
        if (!spanCacheCanRestamp) {
          break;
        }
      }

      NoteUtils::DisplayNoteVec committedPassNotes =
          NoteUtils::reconstructDisplayNotes(committedPassEvents, loopLengthTicks, false, false, true);
      for (const NoteUtils::DisplayNote& candidate : committedPassNotes) {
        if (candidate.noteId == kInvalidNoteId) {
          continue;
        }
        bool found = false;
        for (const NoteUtils::DisplayNote& existing : overdubSourceSpanCacheNotes_) {
          if (existing.noteId == candidate.noteId) {
            found = true;
            break;
          }
        }
        if (!found) {
          overdubSourceSpanCacheNotes_.push_back(candidate);
        }
      }
      for (const MidiEvent& evt : committedPassEvents) {
        if (!evt.isNoteOn() || evt.noteId == kInvalidNoteId) {
          continue;
        }
        bool present = false;
        for (const NoteUtils::DisplayNote& note : overdubSourceSpanCacheNotes_) {
          if (note.noteId == evt.noteId) {
            present = true;
            break;
          }
        }
        if (!present) {
          spanCacheCanRestamp = false;
          break;
        }
      }
    }
  }

  ++playbackRevision;
  if (spanCacheCanRestamp) {
    overdubSourceSpanCachePlaybackRevision_ = playbackRevision;
  }
  pendingVisualDelta.clear();

  capture.store.clear();
  capture.phase = CapturePhase::None;
  captureNextEventIndex = 0;
  captureEventsSortDirty = false;
  capturePreview.clear();
  // Wrap and stop commit keep the session source view. Clearing here makes
  // applyPendingNoteChangesToOverdubSourceView a no-op and beginCapture
  // re-establishes from a dirty cache (000417 src,why=open every wrap).
  // closeOverdubSession / discardCapture clear the view.
  if (!hasOverdubSession()) {
    clearOverdubSourceView();
  }

  return true;
}
