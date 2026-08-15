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
    ++playbackRevision;
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
    ++playbackRevision;
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
  return DisplayWindowUtils::kMaxDetailedWindowBars * Config::TICKS_PER_BAR;
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

LOOP_COLD_MEM void Loop::establishOverdubSourceView(uint32_t playheadPhaseTick) {
  overlapHoldTotals_ = {};
  overdubSourceViewEvents_.clear();
  overdubSourceViewNotes_.clear();
  overdubSourceViewLoopLengthTicks_ = loopLengthTicks;
  if (loopLengthTicks == 0) {
    overdubSourceViewEstablished_ = true;
    clearPendingNoteChanges();
    return;
  }
  uint32_t windowStart = 0;
  uint32_t windowLength = 0;
  resolveOverdubSourceWindow(playheadPhaseTick, windowStart, windowLength);
  ResolutionCostCounters windowCounters;
  if (LoopContentResolution::tryResolvePreparedWindow(
          passes.editPasses, loopLengthTicks, windowStart, windowLength, playbackRevision,
          overdubSourceViewEvents_, &windowCounters)) {
#if defined(SESSION_CAPTURE) && defined(ARDUINO)
    const uint32_t reconstructStartUs = micros();
#endif
    overdubSourceViewNotes_ =
        NoteUtils::reconstructDisplayNotes(overdubSourceViewEvents_, loopLengthTicks, false);
#if defined(SESSION_CAPTURE) && defined(ARDUINO)
    const uint32_t reconstructUs = micros() - reconstructStartUs;
    const uint32_t windowUs = static_cast<uint32_t>(windowCounters.elapsedMicros);
    char line[192];
    snprintf(line, sizeof(line),
             "#CAP,%lu,DIAG,lcr,6c,win=%lu,proj=%lu,tot=%lu,ev=%u,notes=%u",
             static_cast<unsigned long>(micros()), static_cast<unsigned long>(windowUs),
             static_cast<unsigned long>(reconstructUs),
             static_cast<unsigned long>(windowUs + reconstructUs),
             static_cast<unsigned>(overdubSourceViewEvents_.size()),
             static_cast<unsigned>(overdubSourceViewNotes_.size()));
    DebugSessionCapture::appendCaptureTextLine(line);
#endif
    overdubSourceViewEstablished_ = true;
    clearPendingNoteChanges();
    return;
  }
  // Idle slice_clean already holds committed DisplayNotes (043822: 1799 notes, bars 0–67).
  // Copy that list; do not flatten or reconstruct. Dirty/empty cache falls through to a
  // windowed chunk walk (overlap can fill notes later via ensureOverdubSourceNotesForHold).
  if (DisplayWindowUtils::committedDisplayVisualCacheAuthoritative(visualCacheDirty,
                                                                   !visualCache.notes.empty())) {
    overdubSourceViewNotes_ = visualCache.notes;
    overdubSourceViewEstablished_ = true;
    clearPendingNoteChanges();
    return;
  }
  copyEffectiveCommittedEventsInRange(overdubSourceViewEvents_, windowStart, windowLength);
  overdubSourceViewEstablished_ = true;
  clearPendingNoteChanges();
}

LOOP_COLD_MEM void Loop::clearOverdubSourceView() {
  overdubSourceViewEvents_.clear();
  overdubSourceViewNotes_.clear();
  overdubSourceViewLoopLengthTicks_ = 0;
  overdubSourceViewEstablished_ = false;
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
    establishOverdubSourceView(playheadPhaseTick);
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
  if (passes.hasRecordPass() && passes.recordPass.id == preserveId) {
    preservePhase = CapturePassPhase::Record;
  } else {
    for (const OverdubPass& pass : passes.overdubPasses) {
      if (pass.state == CapturePassState::Active && pass.id == preserveId) {
        preserveMergeSeq = pass.mergeSequence;
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
  const SealOutcome seal = sealCapture(sealedAtTick);
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

SealOutcome Loop::sealCapture(uint32_t sealedAtTick) {
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
    minLenPairsRemoved = static_cast<uint32_t>(
        CaptureIncrementalSanity::removePairsShorterThanNoteMinLength(
            capture.store, loopLengthTicks, noteMinLengthTicks, noteMinLengthRemoveEnabled));
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

bool Loop::commitPendingCapturePass() {
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
    overdub.committedChunkIds = std::move(pendingPass.committedChunkIds);
    passes.overdubPasses.push_back(std::move(overdub));
  }
  lastCommittedPassId_ = pendingPass.id;

  pendingCapturePass_ = PendingCapturePass{};
  hasPendingCapturePass_ = false;

  ++playbackRevision;
  pendingVisualDelta.clear();

  capture.store.clear();
  capture.phase = CapturePhase::None;
  captureNextEventIndex = 0;
  captureEventsSortDirty = false;
  capturePreview.clear();
  clearOverdubSourceView();

  return true;
}
