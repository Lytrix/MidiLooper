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
#include "Utils/NoteUtils.h"
#include "EditApply.h"
#include "PlaybackMergedMidiEvents.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <vector>

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

void Loop::assignMissingNoteIdsInStoreWindow(LoopEventStore& store, size_t beginIndex,
                                             uint32_t includeUntil) {
  store.assignMissingNoteIdsToNoteOnsInTickWindow(beginIndex, includeUntil,
                                                  [this]() { return allocateNoteId(); });
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

}  // namespace

LOOP_COLD_MEM void Loop::appendLastCommittedOverdubPassToSourceView() {
  if (!overdubSourceViewEstablished_ || lastCommittedPassId_ == kInvalidPassId ||
      loopLengthTicks == 0) {
    return;
  }
  const OverdubPass* wrapPass = nullptr;
  for (const OverdubPass& pass : passes.overdubPasses) {
    if (pass.id == lastCommittedPassId_) {
      wrapPass = &pass;
      break;
    }
  }
  if (wrapPass == nullptr || wrapPass->committedChunkIds.empty()) {
    return;
  }
  SessionMidiEventVec passEvents;
  LoopEventStore::appendChunkRefEvents(wrapPass->committedChunkIds, passEvents);
  if (passEvents.empty()) {
    return;
  }
  NoteUtils::DisplayNoteVec passNotes =
      NoteUtils::reconstructDisplayNotes(passEvents, loopLengthTicks, false, false, true);
  collapseSplitHeadTailToWrapHeld(passNotes, loopLengthTicks);
  mergeDisplayNotesIntoOverdubSourceView(passNotes);
  // Do not merge this pass's isolated reconstruct into visualCache. Per-pass
  // pairing omits record Offs and lengthens body notes (205138: 205 → 271 at
  // wrap 2; idle slice then 260; STOPPED is short). New wrap notes enter
  // visualCache from idle slice / wrap-held display append.
  // Wrap restamp can clear readiness (wrap-pair / missing id) while leaving notes.
  // Idle then rebuildOverdubSourceView("idle") from=passes (200434). Merge the
  // same wrap-pass notes into the existing span cache and restamp so ready()
  // holds. O(pass) only — not a full-loop reconstruct.
  if (overdubSourceSpanCacheValid_ || !overdubSourceSpanCacheNotes_.empty()) {
    mergeDisplayNotesByNoteId(overdubSourceSpanCacheNotes_, passNotes);
    overdubSourceSpanCacheLoopLengthTicks_ = loopLengthTicks;
    overdubSourceSpanCachePlaybackRevision_ = playbackRevision;
    overdubSourceSpanCacheValid_ = true;
    overdubSourceSpanCacheFullLoop_ = true;
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
  if (!overdubSourceSpanCacheFullLoop_) {
    return false;
  }
  return overdubSourceSpanCachePlaybackRevision_ == playbackRevision;
}

LOOP_COLD_MEM const char* Loop::rebuildOverdubSourceSpanCache(uint32_t requestedWindowStart,
                                                              uint32_t requestedWindowLength) {
  NoteUtils::DisplayNoteVec priorSpanCacheNotes;
  const bool keepPriorSpanCache =
      hasOverdubSession() && overdubSourceSpanCacheValid_ &&
      overdubSourceSpanCacheLoopLengthTicks_ == loopLengthTicks &&
      overdubSourceSpanCacheFullLoop_ && !overdubSourceSpanCacheNotes_.empty();
  if (keepPriorSpanCache) {
    priorSpanCacheNotes.swap(overdubSourceSpanCacheNotes_);
  }
  overdubSourceSpanCacheNotes_.clear();
  overdubSourceSpanCacheLoopLengthTicks_ = loopLengthTicks;
  overdubSourceSpanCachePlaybackRevision_ = playbackRevision;
  overdubSourceSpanCacheValid_ = true;
  overdubSourceSpanCacheFullLoop_ = false;
  if (loopLengthTicks == 0) {
    return "empty";
  }
  if (requestedWindowLength == 0) {
    requestedWindowStart = 0;
    requestedWindowLength = loopLengthTicks;
  }
  const char* sourceOrigin = "passes";
  const bool copiedPreparedSpans = LoopContentResolution::tryCopyPreparedSpansToDisplayNotes(
      playbackRevision, overdubSourceSpanCacheNotes_, nullptr, loopLengthTicks, false);
  if (copiedPreparedSpans) {
    overdubSourceSpanCacheFullLoop_ = true;
    sourceOrigin = "prepared";
  } else if (hasOverdubSession() && !priorSpanCacheNotes.empty()) {
    // A prior cache is reusable only when it was proven to cover the full loop. A
    // nonempty partial seed is not a valid answer for a later source window.
    overdubSourceSpanCacheNotes_.swap(priorSpanCacheNotes);
    overdubSourceSpanCacheFullLoop_ = true;
    sourceOrigin = "prior";
  } else if (hasOverdubSession() &&
             !visualCache.notes.empty() &&
             visualCacheCoversWindow(visualCacheDirty, visualCache.dirtyBars,
                                     requestedWindowStart, requestedWindowLength,
                                     loopLengthTicks, Config::TICKS_PER_BAR)) {
    // Prepared spans can miss immediately after publish. Use the latest committed
    // display list only when it covers the requested source interval.
    overdubSourceSpanCacheNotes_ = visualCache.notes;
    overdubSourceSpanCacheFullLoop_ = !visualCacheDirty;
    sourceOrigin = "visual";
  } else {
    // An uncovered visual cache is not a valid overlap source. Reconstruct from
    // committed passes so overlap resolution never receives a partial span.
    SessionMidiEventVec fullResolvedEvents;
    passes.materializeToEventVector(fullResolvedEvents, loopLengthTicks);
    overdubSourceSpanCacheNotes_ =
        NoteUtils::reconstructDisplayNotes(fullResolvedEvents, loopLengthTicks, false, false);
    appendOverdubPassWrapPairedNotes(overdubSourceSpanCacheNotes_);
    overdubSourceSpanCacheFullLoop_ = true;
    sourceOrigin = "passes";
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
  return sourceOrigin;
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
    from = rebuildOverdubSourceSpanCache(windowStart, windowLength);
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
  collapseSplitHeadTailToWrapHeld(overdubSourceViewNotes_, loopLengthTicks);
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

LOOP_COLD_MEM void Loop::retainCapturePreviewAtSeal() {
  sealedCapturePreviewNotes_.clear();
  sealedCapturePreviewRevision_ = playbackRevision;
  if (capturePreview.notes.empty()) {
    return;
  }
  for (size_t index = 0; index < capturePreview.notes.size(); ++index) {
    // Open holds keep sounding into the next pass and the live Capture tier still paints them.
    // Retaining one would double-paint the same note beside its fresh capture row.
    const bool open = std::find(capturePreview.openNoteIndices.begin(),
                                capturePreview.openNoteIndices.end(),
                                static_cast<uint32_t>(index)) !=
                      capturePreview.openNoteIndices.end();
    if (open) {
      continue;
    }
    sealedCapturePreviewNotes_.push_back(capturePreview.notes[index]);
  }
}

LOOP_COLD_MEM bool Loop::sealedCapturePreviewIsCovered() const {
  if (sealedCapturePreviewNotes_.empty()) {
    return true;
  }
  if (publishedSnapshotIsCurrent()) {
    // Snapshot is current, which is also what makes the painter read snapshot membership for
    // every committed bar, so the cache cannot contribute a stale row in the same frame.
    return publishedSnapshotRevision() >= sealedCapturePreviewRevision_;
  }
  // Snapshot is stale, so the cache is the representation that paints.
  return !visualCacheDirty && visualCache.builtFromRevision >= sealedCapturePreviewRevision_;
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

void Loop::beginCapture(CapturePhase phase, uint32_t playheadPhaseTick) {
  discardPendingCapturePass();
  capture.phase = phase;
  capture.store.clear();
  capturePreview.clear();
  captureNextEventIndex = 0;
  captureEventsSortDirty = false;
  captureHasLastAppendTick_ = false;
  captureLastAppendTick_ = 0;
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
  captureHasLastAppendTick_ = false;
  captureLastAppendTick_ = 0;
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
  if (captureHasLastAppendTick_) {
    if (evt.tick < captureLastAppendTick_) {
      captureEventsSortDirty = true;
    }
  } else {
    captureHasLastAppendTick_ = true;
  }
  captureLastAppendTick_ = evt.tick;
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
  uint32_t sortedLastTick = 0;
  sortCaptureStoreByTick(capture.store, &sortedLastTick);
  captureHasLastAppendTick_ = !capture.store.empty();
  captureLastAppendTick_ = sortedLastTick;
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

  notifyCommittedContentChanged();
  if (reason == CommitReason::OverdubWrap) {
    markAffectedDisplayCacheRanges(lastCommittedPassId_, EditPassIdList{});
  }
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
  return LoopStopFinalize::finalizeWrapWindowOnStore(capture.store, loopLengthTicks,
                                                     openTailCloseTick);
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
              capture.store, loopLengthTicks, noteMinLengthTicks, noteMinLengthRemoveEnabled));
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
#if defined(SESSION_CAPTURE)
  const uint32_t detachChunksStartUs = traceMicros();
#endif
  if (!capture.store.detachChunksToCommittedChunkIds(pendingCapturePass_.committedChunkIds)) {
#if defined(SESSION_CAPTURE)
    emitOverdubSealTraceStage(sealTrace, "seal_detach_capture_chunks",
                              traceMicros() - detachChunksStartUs, "failed");
#endif
    pendingCapturePass_ = PendingCapturePass{};
    return SealOutcome::FailedValidation;
  }
#if defined(SESSION_CAPTURE)
  emitOverdubSealTraceStage(sealTrace, "seal_detach_capture_chunks",
                            traceMicros() - detachChunksStartUs,
                            pendingCapturePass_.committedChunkIds.empty() ? "empty" : "ok");
#endif

  if (pendingCapturePass_.committedChunkIds.empty()) {
    pendingCapturePass_ = PendingCapturePass{};
    return SealOutcome::FailedValidation;
  }

  pendingVisualDelta.clear();
  hasPendingCapturePass_ = true;
  emitOverdubSealTraceStage(sealTrace, "seal_stage_complete", 0, "ok");
  return SealOutcome::Ok;
}

LOOP_COLD_MEM bool Loop::commitPendingCapturePass(CommitReason reason) {
  const OverdubPublishTraceCtx publishTrace = beginOverdubPublishTrace(*this, reason);
  if (!hasPendingCapturePass_) {
    emitOverdubPublishTraceStage(publishTrace, "seal_publish_precheck", 0, "no_pending");
    return false;
  }

  PendingCapturePass pendingPass = std::move(pendingCapturePass_);
#if defined(SESSION_CAPTURE)
  const uint32_t promotePassStartUs = traceMicros();
#endif
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
#if defined(SESSION_CAPTURE)
  emitOverdubPublishTraceStage(publishTrace, "seal_publish_promote_pass",
                               traceMicros() - promotePassStartUs, "ok");
#endif
  lastCommittedPassId_ = pendingPass.id;

  pendingCapturePass_ = PendingCapturePass{};
  hasPendingCapturePass_ = false;

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
  ++playbackRevision;
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
  // Seal handoff (150650): the sealed pass reaches Snapshot ~150-220ms later and VisualCache on
  // its next idle slice, so clearing the preview here would leave the frame on the previous pass
  // until then. Keep the rows until a committed tier proves coverage.
  retainCapturePreviewAtSeal();
  capture.store.clear();
  capture.phase = CapturePhase::None;
  captureNextEventIndex = 0;
  captureEventsSortDirty = false;
  captureHasLastAppendTick_ = false;
  captureLastAppendTick_ = 0;
  capturePreview.clear();
  // Wrap and stop commit keep the session source view. Clearing here makes
  // applyPendingNoteChangesToOverdubSourceView a no-op and beginCapture
  // re-establishes from a dirty cache (000417 src,why=open every wrap).
  // closeOverdubSession / discardCapture clear the view.
  if (!hasOverdubSession()) {
    clearOverdubSourceView();
  }
#if defined(SESSION_CAPTURE)
  emitOverdubPublishTraceStage(publishTrace, "seal_publish_clear_capture",
                               traceMicros() - clearCaptureStartUs, "ok");
#endif

  return true;
}
