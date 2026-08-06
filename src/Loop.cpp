//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "Loop.h"
#include "LoopInternal.h"
#include "CommittedEventRange.h"
#include "Utils/LoopMem.h"
#include "Utils/LoopStopFinalize.h"
#include "Utils/CaptureIncrementalSanity.h"
#include "Utils/IntervalProjection.h"
#include "Utils/DisplayWindowUtils.h"
#include "Utils/NoteUtils.h"
#include "Globals.h"
#include "Utils/MemoryMonitor.h"
#include "Utils/DebugSessionCapture.h"
#include "Utils/Diagnostics.h"
#include "Logger.h"
#include <algorithm>
#include <chrono>
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

uint32_t Loop::reconcileLoopLengthWithCommittedPasses(uint32_t candidateLengthTicks) const {
  if (!hasCommittedPasses()) {
    return candidateLengthTicks;
  }
  const uint32_t lastEventTick = findLastCommittedEventTick();
  if (lastEventTick == 0) {
    return candidateLengthTicks;
  }

  constexpr uint32_t ticksPerBar = Config::TICKS_PER_BAR;
  const uint32_t fullBars = lastEventTick / ticksPerBar;
  const uint32_t rem = lastEventTick % ticksPerBar;
  const uint32_t grace = ticksPerBar / 6;
  uint32_t contentLength = 0;
  if (rem <= grace) {
    contentLength = (fullBars > 0 ? fullBars : 1) * ticksPerBar;
  } else if (lastEventTick < ticksPerBar / 2) {
    contentLength = ticksPerBar;
  } else {
    contentLength = (fullBars + 1) * ticksPerBar;
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
    markPassDerivedStale();
    return true;
  }
  for (auto it = passes.overdubPasses.begin(); it != passes.overdubPasses.end(); ++it) {
    if (it->id != id || it->state != CapturePassState::Disabled) {
      continue;
    }
    LoopEventStore::releaseChunkRefs(it->committedChunkIds);
    passes.overdubPasses.erase(it);
    ++playbackRevision;
    markPassDerivedStale();
    return true;
  }
  return false;
}

void Loop::reclaimUnreferencedDisabledCapturePasses(const SlotPassReferences& refs) {
  if (passes.hasRecordPass() && passes.recordPass.state == CapturePassState::Disabled &&
      !refs.referencesCapturePass(passes.recordPass.id)) {
    reclaimDisabledCapturePass(passes.recordPass.id);
  }
  for (size_t i = passes.overdubPasses.size(); i > 0; --i) {
    const OverdubPass& pass = passes.overdubPasses[i - 1];
    if (pass.state == CapturePassState::Disabled && !refs.referencesCapturePass(pass.id)) {
      reclaimDisabledCapturePass(pass.id);
    }
  }
}

void Loop::reclaimUnreferencedDisabledEditPasses(const SlotPassReferences& refs) {
  passes.editPasses.erase(
      std::remove_if(passes.editPasses.begin(), passes.editPasses.end(),
                     [&](const EditPass& editPass) {
                       return editPass.state == EditPassState::Disabled &&
                              !refs.referencesEditPass(editPass.id);
                     }),
      passes.editPasses.end());
}

void Loop::reclaimUnreferencedDisabledPasses(const SlotPassReferences& refs) {
  reclaimUnreferencedDisabledCapturePasses(refs);
  reclaimUnreferencedDisabledEditPasses(refs);
}

void Loop::markPassDerivedStale() {
  passesMaterializedStoreStale_ = true;
  playbackOrderDirty = true;
  visualCacheDirty = true;
  invalidatePlaybackCaches();
}

LoopSnapshotRef Loop::sharePassesSnapshot() const {
  auto snapshot = std::make_shared<PersistedLoopSnapshot>();
  snapshot->loopId = loopId;
  snapshot->startLoopTick = startLoopTick;
  snapshot->loopLengthTicks = loopLengthTicks;
  snapshot->loopStartTick = loopStartTick;
  snapshot->nextPassId = nextPassId_;
  snapshot->nextNoteId = nextNoteId_;
  snapshot->nextMergeSequence = nextMergeSequence_;
  snapshot->lastCommittedPassId = lastCommittedPassId_;
  snapshot->passes = deepClonePasses(passes);
  return snapshot;
}

void Loop::adoptPersistedSnapshot(PersistedLoopSnapshot& snapshot) {
  discardPendingCapturePass();
  discardCapture();
  resetPassTimeline();
  loopId = snapshot.loopId;
  startLoopTick = snapshot.startLoopTick;
  loopLengthTicks = snapshot.loopLengthTicks;
  loopStartTick = snapshot.loopStartTick;
  nextPassId_ = snapshot.nextPassId == 0 ? 1 : snapshot.nextPassId;
  nextNoteId_ = snapshot.nextNoteId == 0 ? 1 : snapshot.nextNoteId;
  nextMergeSequence_ = snapshot.nextMergeSequence;
  lastCommittedPassId_ = snapshot.lastCommittedPassId;
  lastTickInLoop = 0;
  nextEventIndex = 0;
  playbackOrderDirty = true;
  passes = std::move(snapshot.passes);
  snapshot.passes = LoopPasses{};
  loopLengthTicks = reconcileLoopLengthWithCommittedPasses(loopLengthTicks);
  ++playbackRevision;
  discardPassesMaterializedCache();
  markDisplayCachesStale();
}

void Loop::restorePassesSnapshot(const PersistedLoopSnapshot& snapshot) {
  discardPendingCapturePass();
  discardCapture();
  resetPassTimeline();
  loopId = snapshot.loopId;
  startLoopTick = snapshot.startLoopTick;
  loopLengthTicks = snapshot.loopLengthTicks;
  loopStartTick = snapshot.loopStartTick;
  nextPassId_ = snapshot.nextPassId == 0 ? 1 : snapshot.nextPassId;
  nextNoteId_ = snapshot.nextNoteId == 0 ? 1 : snapshot.nextNoteId;
  nextMergeSequence_ = snapshot.nextMergeSequence;
  lastCommittedPassId_ = snapshot.lastCommittedPassId;
  lastTickInLoop = 0;
  nextEventIndex = 0;
  playbackOrderDirty = true;
  passes = deepClonePasses(snapshot.passes);
  ++playbackRevision;
  discardPassesMaterializedCache();
  markDisplayCachesStale();
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

  CommittedChunkIdList publishedIds;
  if (!LoopEventStore::transferCaptureChunkIdsToCommittedChunkIds(publishedIds, captureIds)) {
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
    rebuilt.committedChunkIds = std::move(publishedIds);
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
    rebuilt.committedChunkIds = std::move(publishedIds);
    passes.overdubPasses.push_back(rebuilt);
    lastCommittedPassId_ = rebuilt.id;
  }

  ++playbackRevision;
  discardPassesMaterializedCache();
  markDisplayCachesStale();
}

void Loop::seedRecordPassFromStore(LoopEventStore& store) {
  resetPassTimeline();
  if (store.empty()) {
    discardPassesMaterializedCache();
    return;
  }
  CaptureChunkIdList captureIds;
  store.detachChunksTo(captureIds);
  if (captureIds.empty()) {
    discardPassesMaterializedCache();
    return;
  }
  CommittedChunkIdList publishedIds;
  if (!LoopEventStore::transferCaptureChunkIdsToCommittedChunkIds(publishedIds, captureIds)) {
    LoopEventStore::releaseChunkRefs(captureIds);
    discardPassesMaterializedCache();
    return;
  }
  RecordPass record{};
  record.id = nextPassId_++;
  record.state = CapturePassState::Active;
  record.committedChunkIds = std::move(publishedIds);
  passes.recordPass = std::move(record);
  lastCommittedPassId_ = passes.recordPass.id;
  ++playbackRevision;
  markPassDerivedStale();
  discardPassesMaterializedCache();
  rebuildVisualCacheFromPasses();
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
  markPassDerivedStale();
}

void Loop::beginCapture(CapturePhase phase) {
  discardPendingCapturePass();
  capture.phase = phase;
  capture.store.clear();
  capturePreview.clear();
  captureNextEventIndex = 0;
  captureEventsSortDirty = false;
  captureDedupEventsDropped_ = 0;
  ++captureDisplayRevision;
}

void Loop::discardCapture() {
  capture.store.clear();
  capture.phase = CapturePhase::None;
  captureNextEventIndex = 0;
  captureEventsSortDirty = false;
  capturePreview.clear();
  captureDedupEventsDropped_ = 0;
}

bool Loop::appendCaptureEvent(const MidiEvent& evt) {
  if (capture.phase == CapturePhase::None) {
    return false;
  }
  if (hasPendingCapturePass_) {
    return false;
  }
  if (isDuplicateCaptureEvent(*this, evt)) {
    ++captureDedupEventsDropped_;
    return false;
  }
  if (!capture.store.append(evt)) {
    return false;
  }
  captureEventsSortDirty = true;
  applyCaptureEventToPreview(capturePreview, evt, Config::TICKS_PER_BAR);
  ++captureDisplayRevision;
  return true;
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

size_t Loop::displayEventCountHint() const {
  size_t count = materializedEventCount_;
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

NoteId Loop::allocateNoteId() {
  return nextNoteId_++;
}

void Loop::assignMissingNoteIds(SessionMidiEventVec& events) {
  for (MidiEvent& evt : events) {
    if (evt.isNoteOn() && evt.noteId == kInvalidNoteId) {
      evt.noteId = allocateNoteId();
      logger.log(CAT_TRACK, LOG_WARNING,
                 "assignMissingNoteIds: assigned noteId=%lu tick=%lu pitch=%u",
                 static_cast<unsigned long>(evt.noteId), static_cast<unsigned long>(evt.tick),
                 static_cast<unsigned>(evt.data.noteData.note));
    }
  }
}

void Loop::assignMissingNoteIds(MidiEventVec& events) {
  for (MidiEvent& evt : events) {
    if (evt.isNoteOn() && evt.noteId == kInvalidNoteId) {
      evt.noteId = allocateNoteId();
      logger.log(CAT_TRACK, LOG_WARNING,
                 "assignMissingNoteIds: assigned noteId=%lu tick=%lu pitch=%u",
                 static_cast<unsigned long>(evt.noteId), static_cast<unsigned long>(evt.tick),
                 static_cast<unsigned>(evt.data.noteData.note));
    }
  }
}

void Loop::assignMissingNoteIdsInStore(LoopEventStore& store) {
  store.assignMissingNoteIdsToNoteOns([this]() { return allocateNoteId(); });
}

void Loop::resetPassTimeline() {
  discardPendingCapturePass();
  if (passes.hasRecordPass()) {
    LoopEventStore::releaseChunkRefs(passes.recordPass.committedChunkIds);
    passes.recordPass = RecordPass{};
  }
  for (OverdubPass& pass : passes.overdubPasses) {
    LoopEventStore::releaseChunkRefs(pass.committedChunkIds);
  }
  passes.overdubPasses.clear();
  passes.editPasses.clear();
  nextPassId_ = 1;
  nextNoteId_ = 1;
  nextMergeSequence_ = 0;
  lastCommittedPassId_ = kInvalidPassId;
  playbackRevision = 0;
  editStateDirty_ = false;
  visualCache.clear();
  capturePreview.clear();
  pendingVisualDelta.clear();
  visualCacheDirty = true;
  passesMaterializedStore_.mutStore().clear();
  passesMaterializedStore_.discardEventsCache();
  passesMaterializedStoreStale_ = true;
}

bool Loop::setCapturePassState(PassId id, CapturePassState state) {
  if (passes.hasRecordPass() && passes.recordPass.id == id) {
    if (passes.recordPass.state == state) {
      return true;
    }
    passes.recordPass.state = state;
    ++playbackRevision;
    markPassDerivedStale();
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
    markPassDerivedStale();
    return true;
  }
  return false;
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

  markPassDerivedStale();
  return CommitResult::Committed;
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

void Loop::discardPendingCapturePass() {
  if (!hasPendingCapturePass_) {
    return;
  }
  LoopEventStore::releaseChunkRefs(pendingCapturePass_.committedChunkIds);
  pendingCapturePass_ = PendingCapturePass{};
  hasPendingCapturePass_ = false;
  pendingVisualDelta.clear();
}

namespace {

uint32_t totalVisualBarsForLoop(uint32_t loopLengthTicks) {
  if (loopLengthTicks == 0) {
    return 0;
  }
  return (loopLengthTicks + Config::TICKS_PER_BAR - 1) / Config::TICKS_PER_BAR;
}

void markAllVisualCacheBarsDirty(VisualCache& cache, uint32_t loopLengthTicks) {
  const uint32_t totalBars = totalVisualBarsForLoop(loopLengthTicks);
  if (totalBars == 0) {
    cache.dirtyBars.clear();
    return;
  }
  cache.dirtyBars.assign(totalBars, 1);
}

uint32_t findNextDirtyBar(const VisualBarVec& dirtyBars, uint32_t priorityBar) {
  if (dirtyBars.empty()) {
    return UINT32_MAX;
  }
  const uint32_t totalBars = static_cast<uint32_t>(dirtyBars.size());
  const uint32_t start = priorityBar < totalBars ? priorityBar : 0;
  for (uint32_t offset = 0; offset < totalBars; ++offset) {
    const uint32_t bar = (start + offset) % totalBars;
    if (dirtyBars[bar] != 0) {
      return bar;
    }
  }
  return UINT32_MAX;
}

void removeDisplayNotesOverlappingBars(DisplayNoteVec& notes, uint32_t startBar, uint32_t endBar) {
  notes.erase(std::remove_if(notes.begin(), notes.end(),
                             [&](const NoteUtils::DisplayNote& note) {
                               const uint32_t endTick =
                                   note.endTick >= note.startTick ? note.endTick : note.startTick;
                               const uint32_t noteStartBar =
                                   visualBarForTick(note.startTick, Config::TICKS_PER_BAR);
                               const uint32_t noteEndBar =
                                   visualBarForTick(endTick, Config::TICKS_PER_BAR);
                               return noteStartBar <= endBar && noteEndBar >= startBar;
                             }),
                  notes.end());
}

template <typename MidiEventVector>
void filterMidiEventsToTickWindow(const MidiEventVector& events, MidiEventVector& out,
                                  uint32_t windowStart, uint32_t windowLength,
                                  uint32_t loopLength) {
  out.clear();
  if (events.empty() || loopLength == 0 || windowLength == 0) {
    return;
  }
  out.reserve(events.size());
  for (const MidiEvent& evt : events) {
    const uint32_t rel = IntervalProjection::tickPhaseInLoop(evt.tick, 0, loopLength);
    const uint32_t start = IntervalProjection::tickPhaseInLoop(windowStart, 0, loopLength);
    const uint32_t end =
        IntervalProjection::tickPhaseInLoop(start + windowLength, 0, loopLength);
    bool inWindow = false;
    if (windowLength >= loopLength) {
      inWindow = true;
    } else if (start < end) {
      inWindow = rel >= start && rel < end;
    } else {
      inWindow = rel >= start || rel < end;
    }
    if (inWindow) {
      out.push_back(evt);
    }
  }
}

}  // namespace

LOOP_COLD_MEM void Loop::rebuildVisualCacheIdleSlice(uint8_t maxBarsPerSlice, uint32_t priorityBar) {
  if (!visualCacheDirty || loopLengthTicks == 0 || maxBarsPerSlice == 0) {
    return;
  }

  const uint32_t totalBars = totalVisualBarsForLoop(loopLengthTicks);
  if (totalBars == 0) {
    visualCacheDirty = false;
    visualCache.dirtyBars.clear();
    return;
  }
  if (visualCache.dirtyBars.size() < totalBars) {
    markAllVisualCacheBarsDirty(visualCache, loopLengthTicks);
  }

  const uint32_t startBar = findNextDirtyBar(visualCache.dirtyBars, priorityBar);
  if (startBar == UINT32_MAX) {
    visualCacheDirty = false;
    visualCache.dirtyBars.clear();
    return;
  }

  const uint32_t barsThisSlice =
      std::min<uint32_t>(maxBarsPerSlice, totalBars - startBar);
  const uint32_t endBar = startBar + barsThisSlice - 1;

  constexpr uint32_t kPadBars = 1;
  const uint32_t eventStartBar = startBar > kPadBars ? startBar - kPadBars : 0;
  const uint32_t eventEndBar = std::min(endBar + kPadBars, totalBars - 1);
  const uint32_t windowStart = eventStartBar * Config::TICKS_PER_BAR;
  const uint32_t windowEndTick =
      std::min((eventEndBar + 1) * Config::TICKS_PER_BAR, loopLengthTicks);
  const uint32_t windowLength = windowEndTick > windowStart ? windowEndTick - windowStart : 0;
  if (windowLength == 0) {
    for (uint32_t bar = startBar; bar <= endBar; ++bar) {
      visualCache.dirtyBars[bar] = 0;
    }
    return;
  }

  SessionMidiEventVec flat;
  gatherCommittedEventsInWindow(flat, windowStart, windowLength);
  const NoteUtils::DisplayNoteVec sliceNotes =
      NoteUtils::reconstructDisplayNotes(flat, loopLengthTicks, false);

  removeDisplayNotesOverlappingBars(visualCache.notes, startBar, endBar);
  for (const NoteUtils::DisplayNote& note : sliceNotes) {
    const uint32_t endTick = note.endTick >= note.startTick ? note.endTick : note.startTick;
    const uint32_t noteStartBar = visualBarForTick(note.startTick, Config::TICKS_PER_BAR);
    const uint32_t noteEndBar = visualBarForTick(endTick, Config::TICKS_PER_BAR);
    if (noteStartBar <= endBar && noteEndBar >= startBar) {
      visualCache.notes.push_back(note);
    }
  }

  for (uint32_t bar = startBar; bar <= endBar; ++bar) {
    visualCache.dirtyBars[bar] = 0;
  }

  bool anyDirty = false;
  for (uint8_t flag : visualCache.dirtyBars) {
    if (flag != 0) {
      anyDirty = true;
      break;
    }
  }
  if (!anyDirty) {
    visualCacheDirty = false;
    visualCache.dirtyBars.clear();
    ++visualCache.revision;
  }
}

LOOP_COLD_MEM void Loop::rebuildVisualCacheFromPasses() {
  DIAG_COUNTER_INC(DisplayFullRebuild);
  SessionMidiEventVec flat;
  gatherCommittedEvents(flat);
  materializedEventCount_ = flat.size();
  const NoteUtils::DisplayNoteVec rebuiltNotes =
      NoteUtils::reconstructDisplayNotes(flat, loopLengthTicks, false);
  visualCache.notes.assign(rebuiltNotes.begin(), rebuiltNotes.end());
  visualCache.dirtyBars.clear();
  for (const auto& n : visualCache.notes) {
    const uint32_t endTick = n.endTick >= n.startTick ? n.endTick : n.startTick;
    const uint32_t startBar = visualBarForTick(n.startTick, Config::TICKS_PER_BAR);
    const uint32_t endBar = visualBarForTick(endTick, Config::TICKS_PER_BAR);
    for (uint32_t bar = startBar; bar <= endBar; ++bar) {
      visualCache.markBarDirty(bar);
    }
  }
  ++visualCache.revision;
  visualCacheDirty = false;
}

void Loop::ensureVisualCacheBuilt() {
  if (!visualCacheDirty) {
    return;
  }
  rebuildVisualCacheFromPasses();
}

void Loop::markDisplayCachesStale() {
  invalidatePlaybackCaches();
  visualCacheDirty = true;
  markAllVisualCacheBarsDirty(visualCache, loopLengthTicks);
}

void Loop::invalidateDisplayCaches() {
  if (noteCache_) {
    noteCache_->invalidate();
  }
  visualCache.notes.clear();
  visualCache.dirtyBars.clear();
  visualCacheDirty = true;
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

  PendingCapturePass published = std::move(pendingCapturePass_);
  if (published.phase == CapturePassPhase::Record) {
    RecordPass record{};
    record.id = published.id;
    record.state = CapturePassState::Active;
    record.sealedAtTick = published.sealedAtTick;
    record.committedChunkIds = std::move(published.committedChunkIds);
    passes.recordPass = std::move(record);
  } else {
    OverdubPass overdub{};
    overdub.id = published.id;
    overdub.mergeSequence = published.mergeSequence;
    overdub.state = CapturePassState::Active;
    overdub.sealedAtTick = published.sealedAtTick;
    overdub.committedChunkIds = std::move(published.committedChunkIds);
    passes.overdubPasses.push_back(std::move(overdub));
  }
  lastCommittedPassId_ = published.id;

  pendingCapturePass_ = PendingCapturePass{};
  hasPendingCapturePass_ = false;

  ++playbackRevision;
  pendingVisualDelta.clear();

  capture.store.clear();
  capture.phase = CapturePhase::None;
  captureNextEventIndex = 0;
  captureEventsSortDirty = false;
  capturePreview.clear();

  return true;
}

void Loop::invalidateCaches() {
  if (noteCache_) {
    noteCache_->invalidate();
  }
  eventIndexValid = false;
  visualCacheDirty = true;
  playbackOrderDirty = true;
  passesMaterializedStoreStale_ = true;
}

void Loop::invalidatePlaybackCaches() {
  playbackOrderDirty = true;
  passesMaterializedStore_.discardEventsCache();
  if (noteCache_) {
    noteCache_->invalidate();
  }
  eventIndexValid = false;
}

void Loop::clearCaptureOnNewPass() {
  discardCapture();
}
