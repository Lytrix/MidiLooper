//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "TrackInternal.h"

#include <Arduino.h>
#include <algorithm>
#include <vector>

#include "ClockManager.h"
#include "EditManager.h"
#include "Globals.h"
#include "TickPhase.h"
#include "TrackManager.h"
#include "Utils/Diagnostics.h"
#include "Utils/IntervalProjection.h"
#include "Utils/MemoryMonitor.h"
#include "Utils/DebugSessionCapture.h"
#include "Utils/ExternalPoolAccounting.h"
#include "Utils/RuntimeTimingTelemetry.h"
#include "PlaybackMergedMidiEvents.h"
#include "Utils/CommittedMidiScratch.h"

extern TrackManager trackManager;

namespace {

size_t mergedPlaybackStreamSize(const void* ctx) {
  return static_cast<const MergedPlaybackStreamCtx*>(ctx)->order->size();
}

bool mergedPlaybackStreamValid(const void* ctx, uint16_t cursor) {
  const auto* stream = static_cast<const MergedPlaybackStreamCtx*>(ctx);
  if (static_cast<size_t>(cursor) >= stream->order->size()) {
    return true;
  }
  return (*stream->order)[cursor] < stream->merged->size();
}

const MidiEvent& mergedPlaybackStreamEventAt(const void* ctx, uint16_t cursor) {
  const auto* stream = static_cast<const MergedPlaybackStreamCtx*>(ctx);
  return (*stream->merged)[(*stream->order)[cursor]];
}

uint32_t mergedPlaybackStreamPhase(const MidiEvent& evt, const ProjectionContext& playbackContext) {
  return IntervalProjection::playbackEventPhase(evt.tick, playbackContext.loopLength);
}

size_t fixedPlaybackStreamSize(const void* ctx) {
  return static_cast<const FixedPlaybackStreamCtx*>(ctx)->storage->eventCount;
}

bool fixedPlaybackStreamValid(const void* ctx, uint16_t cursor) {
  const auto* stream = static_cast<const FixedPlaybackStreamCtx*>(ctx);
  if (static_cast<size_t>(cursor) >= stream->storage->eventCount) {
    return true;
  }
  return stream->storage->order[cursor] < stream->storage->eventCount;
}

const MidiEvent& fixedPlaybackStreamEventAt(const void* ctx, uint16_t cursor) {
  const auto* stream = static_cast<const FixedPlaybackStreamCtx*>(ctx);
  return stream->storage->events[stream->storage->order[cursor]];
}

size_t capturePlaybackStreamSize(const void* ctx) {
  return static_cast<const Loop*>(ctx)->capture.store.size();
}

const MidiEvent& capturePlaybackStreamEventAt(const void* ctx, uint16_t cursor) {
  return static_cast<const Loop*>(ctx)->capture.store.at(cursor);
}

uint32_t capturePlaybackStreamPhase(const MidiEvent& evt, const ProjectionContext& playbackContext) {
  return IntervalProjection::projectPlaybackEventPhase(evt.tick, playbackContext);
}

uint8_t playbackRebuildTrackIndex(const Track& track) {
  const uint8_t trackCount = trackManager.getTrackCount();
  for (uint8_t trackIndex = 0; trackIndex < trackCount; ++trackIndex) {
    if (&track == &trackManager.getTrack(trackIndex)) {
      return trackIndex;
    }
  }
  return 255;
}

uint8_t playbackRebuildSlotIndex(const Track& track, const Loop& loop) {
  if (!track.loopsAllocated()) {
    return 255;
  }
  for (uint8_t slotIndex = 0; slotIndex < Config::MAX_LOOPS_PER_TRACK; ++slotIndex) {
    if (&loop == &track.getLoop(slotIndex)) {
      return slotIndex;
    }
  }
  return 255;
}

bool anyTimingCriticalTrackActive() {
  const uint8_t trackCount = trackManager.getTrackCount();
  for (uint8_t trackIndex = 0; trackIndex < trackCount; ++trackIndex) {
    const Track& candidate = trackManager.getTrack(trackIndex);
    if (candidate.isRecording() || candidate.isAudiblePlaybackActive()) {
      return true;
    }
  }
  return false;
}

/// Task 2.4 instrumentation: a content-sized playback-order build ran while a track was
/// timing-critical. The active clock path must consume a prepared source instead; this
/// counter is what a native test asserts against, so a new synchronous fallback fails.
///
/// Placed in RAM2 (DMAMEM) on device: the tree sits one 32 KB FlexRAM bank / one DTCM
/// alignment edge from the RAM1 local allowance, and a 4-byte .bss addition here measured
/// 4 KB of lost headroom (locals 6,400 -> 2,304). It is read only by tests and diagnostics,
/// never on the per-event path.
#if defined(__IMXRT1062__)
#define PLAYBACK_ORDER_METRIC_MEM DMAMEM
#else
#define PLAYBACK_ORDER_METRIC_MEM
#endif
PLAYBACK_ORDER_METRIC_MEM uint32_t g_playbackOrderHotRebuildCount = 0;

inline void noteContentSizedPlaybackBuildOnActiveTransport() {
  if (anyTimingCriticalTrackActive()) {
    ++g_playbackOrderHotRebuildCount;
  }
}

}  // namespace

void Track::resetPlaybackOrderHotRebuildCount() { g_playbackOrderHotRebuildCount = 0; }

uint32_t Track::playbackOrderHotRebuildCount() { return g_playbackOrderHotRebuildCount; }

ProjectionContext makePlaybackContext(const Track& track, const Loop& loop, uint32_t currentTick) {
  return IntervalProjection::buildPlaybackProjectionContext(
      loop.loopLengthTicks,
      IntervalProjection::makeFullLoopPlaybackProjectionInterval(loop.loopLengthTicks),
      static_cast<int32_t>(currentTick), track.getProjectionCycleStartTick(),
      static_cast<int32_t>(loop.loopStartTick), track.hasQueuedPlaybackStart(),
      track.getQueuedStartTick());
}

/// After a mid-pass playback-order rebuild, point nextEventIndex at the current playhead.
void reanchorPlaybackIndex(Loop& loop, const SessionMidiEventVec& mergedEvents, const PlaybackOrderVec& order,
                           const ProjectionContext& playbackContext) {
  noteContentSizedPlaybackBuildOnActiveTransport();
  if (loop.lastTickInLoop == UINT32_MAX) {
    loop.nextEventIndex = 0;
    return;
  }
  size_t idx = 0;
  while (idx < order.size()) {
    const MidiEvent& e = mergedEvents[order[idx]];
    const uint32_t evPhase =
        IntervalProjection::playbackEventPhase(e.tick, playbackContext.loopLength);
    if (evPhase > loop.lastTickInLoop) break;
    ++idx;
  }
  loop.nextEventIndex = static_cast<uint16_t>(idx);
}

void reanchorFixedPlaybackIndex(Loop& loop, const FixedPlaybackEventStorage& storage,
                                const ProjectionContext& playbackContext) {
  if (loop.lastTickInLoop == UINT32_MAX) {
    loop.nextEventIndex = 0;
    return;
  }
  size_t idx = 0;
  while (idx < storage.eventCount) {
    const MidiEvent& event = storage.events[storage.order[idx]];
    const uint32_t eventPhase =
        IntervalProjection::playbackEventPhase(event.tick, playbackContext.loopLength);
    if (eventPhase > loop.lastTickInLoop) {
      break;
    }
    ++idx;
  }
  loop.nextEventIndex = static_cast<uint16_t>(idx);
}

bool ensurePlaybackMergedMidiEventsBuilt(Track& track, Loop& loop, LoopPlaybackRuntime& runtime,
                               bool /*allowHeavyBuild*/, uint32_t currentTick) {
  // Projection boundary (linear-loop-tick-storage): mergedEvents are read-only input to
  // playback order + MIDI send. Playback mergedMidiEvents is committed-only (224719):
  // live capture must not fold into this representation. NOTE_EDIT uses session store
  // (Tier 2) — full replace, no materialized underlay. Outside NOTE_EDIT, the published
  // LoopSnapshot is the committed source when a published root exists (current or
  // stale-plus-unpublished overlay); otherwise chunk-ref merge of committed passes
  // (DEC-016). Long loops use windowed gather — full-loop gather after
  // LoadLoopJob Commit hard-faults (session_20260718_210532 / 210001).
  static bool mergedMidiEventsBuildInProgress = false;
  if (mergedMidiEventsBuildInProgress) {
    return false;
  }
  const bool noteEditPreview = editManager.isNoteEditActive() &&
                               &track == &trackManager.getSelectedTrack();
  const uint32_t windowRevision = noteEditPreview ? editManager.sessionPlaybackPreviewRevision()
                                                    : loop.playbackRevision;
  const void* snapshotIdentity =
      noteEditPreview ? nullptr : loop.publishedSnapshotIdentity();
  const bool longLoop = loop.shouldAvoidFullVisualRebuild(loop.loopLengthTicks);
  uint32_t playhead = 0;
  if (loop.loopLengthTicks > 0) {
    playhead = IntervalProjection::tickPhaseInProjectionCycle(
        currentTick, track.getProjectionCycleStartTick(), loop.loopLengthTicks);
  }

  if (!longLoop) {
    if (runtime.mergedMidiEvents.builtFromRevision == windowRevision &&
        runtime.mergedMidiEvents.builtFromSnapshotIdentity == snapshotIdentity) {
      DIAG_COUNTER_INC(PlaybackDeferredReuse);
      return false;
    }
  } else if (runtime.mergedMidiEvents.builtFromRevision == windowRevision &&
             runtime.mergedMidiEvents.builtFromSnapshotIdentity == snapshotIdentity &&
             !runtime.mergedMidiEvents.empty() &&
             runtime.mergedMidiEvents.windowLengthTicks > 0) {
    const uint32_t winStart = runtime.mergedMidiEvents.windowStartTick;
    const uint32_t winEnd = winStart + runtime.mergedMidiEvents.windowLengthTicks;
    const uint32_t margin = Config::TICKS_PER_BAR / 2;
    if (playhead + margin >= winStart && playhead < winEnd) {
      DIAG_COUNTER_INC(PlaybackDeferredReuse);
      return false;
    }
  }

  if (!noteEditPreview && !longLoop &&
      loop.tryAdmitLastOverdubSealIntoMergedEvents(runtime.mergedMidiEvents)) {
    loop.playbackOrderDirty = true;
#if defined(SESSION_CAPTURE)
    DebugSessionCapture::playbackRebuild(
        0, runtime.mergedMidiEvents.windowStartTick, runtime.mergedMidiEvents.windowLengthTicks,
        windowRevision, playbackRebuildTrackIndex(track), playbackRebuildSlotIndex(track, loop), 1,
        "incremental");
#endif
    return false;
  }
  loop.clearLastOverdubSealAdmitReceipt();

  noteContentSizedPlaybackBuildOnActiveTransport();
  mergedMidiEventsBuildInProgress = true;
#if RUNTIME_TIMING_ENABLED || defined(SESSION_CAPTURE)
  const uint32_t playbackBuildStartUs = micros();
#endif
  DIAG_COUNTER_INC(PlaybackMergedMidiEventsRebuild);
  const void* usedSnapshotIdentity = nullptr;
  Loop::SnapshotWindowGatherPath gatherPath = Loop::SnapshotWindowGatherPath::None;
  if (noteEditPreview) {
    const MidiEventVec& preview = editManager.sessionMidiEvents();
    runtime.mergedMidiEvents.mergedEvents.assign(preview.begin(), preview.end());
    runtime.mergedMidiEvents.windowStartTick = 0;
    runtime.mergedMidiEvents.windowLengthTicks = loop.loopLengthTicks;
  } else if (longLoop) {
    // Two bars centered on playhead — enough for LoopEnd launch + clock catch-up.
    constexpr uint32_t kMergedMidiEventsGatherBars = 2;
    const uint32_t winLen = kMergedMidiEventsGatherBars * Config::TICKS_PER_BAR;
    uint32_t winStart = playhead > (winLen / 2) ? playhead - (winLen / 2) : 0;
    if (winStart + winLen > loop.loopLengthTicks) {
      winStart = loop.loopLengthTicks > winLen ? loop.loopLengthTicks - winLen : 0;
    }
    if (loop.tryGatherPublishedSnapshotEventsInWindow(runtime.mergedMidiEvents.mergedEvents,
                                                      winStart, winLen, &gatherPath)) {
      usedSnapshotIdentity = snapshotIdentity;
    } else {
      runtime.mergedMidiEvents.mergedEvents.clear();
    }
    runtime.mergedMidiEvents.windowStartTick = winStart;
    runtime.mergedMidiEvents.windowLengthTicks = winLen;
  } else {
    if (loop.tryGatherPublishedSnapshotEventsInWindow(runtime.mergedMidiEvents.mergedEvents, 0,
                                                      loop.loopLengthTicks, &gatherPath)) {
      usedSnapshotIdentity = snapshotIdentity;
    } else {
      runtime.mergedMidiEvents.mergedEvents.clear();
    }
    runtime.mergedMidiEvents.windowStartTick = 0;
    runtime.mergedMidiEvents.windowLengthTicks = loop.loopLengthTicks;
  }
  runtime.mergedMidiEvents.builtFromRevision = windowRevision;
  runtime.mergedMidiEvents.builtFromSnapshotIdentity = usedSnapshotIdentity;
  loop.playbackOrderDirty = true;
#if RUNTIME_TIMING_ENABLED || defined(SESSION_CAPTURE)
  const uint32_t playbackBuildUs = micros() - playbackBuildStartUs;
#endif
  DIAG_TIMING_RECORD(PlaybackBuild, playbackBuildUs);
  RUNTIME_TIMING_RECORD_PLAYBACK_REBUILD(
      playbackBuildUs, runtime.mergedMidiEvents.windowStartTick,
      runtime.mergedMidiEvents.windowLengthTicks, windowRevision,
      playbackRebuildTrackIndex(track), playbackRebuildSlotIndex(track, loop),
      noteEditPreview
          ? RuntimeTimingTelemetry::PlaybackRebuildPath::NoteEditPreview
          : (gatherPath == Loop::SnapshotWindowGatherPath::Snapshot
                 ? RuntimeTimingTelemetry::PlaybackRebuildPath::PublishedSnapshot
                 : (gatherPath == Loop::SnapshotWindowGatherPath::Overlay
                        ? RuntimeTimingTelemetry::PlaybackRebuildPath::StaleRootOverlay
                        : RuntimeTimingTelemetry::PlaybackRebuildPath::
                              GatherMissHoldEmpty)),
      playbackBuildStartUs, loop.lastTickInLoop, playhead,
      static_cast<uint32_t>(runtime.mergedMidiEvents.mergedEvents.size()));
  mergedMidiEventsBuildInProgress = false;
  return true;
}

TRACK_INTERNAL_MEM __attribute__((noinline)) void reconcilePlaybackLedgerAfterFullLoopRebuild(
    Loop& loop, LoopPlaybackRuntime& runtime) {
  loop.replaceCommittedPlaybackNoteOnIdentities(runtime.mergedMidiEvents);
  if (!isFullLoopMergedPlaybackWindow(runtime.mergedMidiEvents, loop.loopLengthTicks)) {
    return;
  }
  runtime.ledger.eraseOpenNotesMissingFromCommittedNoteOns(
      runtime.mergedMidiEvents.mergedEvents.data(), runtime.mergedMidiEvents.mergedEvents.size());
}
TRACK_INTERNAL_MEM __attribute__((noinline)) void rebuildPlaybackOrder(
    Loop& loop, const SessionMidiEventVec& mergedEvents, const ProjectionContext& playbackContext) {
  noteContentSizedPlaybackBuildOnActiveTransport();
  PlaybackOrderVec& playbackOrder = loop.getPlaybackOrder();
  playbackOrder.resize(mergedEvents.size());
  for (size_t i = 0; i < mergedEvents.size(); i++) {
    playbackOrder[i] = i;
  }
  std::vector<uint32_t, ExternalMemoryFirstAllocator<uint32_t>> sortPhases(mergedEvents.size());
  for (size_t i = 0; i < mergedEvents.size(); ++i) {
    sortPhases[i] =
        IntervalProjection::playbackEventPhase(mergedEvents[i].tick, playbackContext.loopLength);
  }
  std::sort(playbackOrder.begin(), playbackOrder.end(), [&](size_t a, size_t b) {
    if (sortPhases[a] != sortPhases[b]) {
      return sortPhases[a] < sortPhases[b];
    }
    return mergedEvents[a].tick < mergedEvents[b].tick;
  });
  // Equal-phase Off before On — same keys as NoteUtils::sortMidiEventsChronologically.
  // Keep those keys out of the std::sort lambda so the ITCM sort instantiation does not grow.
  for (size_t i = 0; i < playbackOrder.size();) {
    size_t groupEnd = i + 1;
    while (groupEnd < playbackOrder.size() &&
           sortPhases[playbackOrder[groupEnd]] == sortPhases[playbackOrder[i]] &&
           mergedEvents[playbackOrder[groupEnd]].tick == mergedEvents[playbackOrder[i]].tick) {
      ++groupEnd;
    }
    for (size_t a = i + 1; a < groupEnd; ++a) {
      const size_t inserted = playbackOrder[a];
      const int insertedOrder =
          mergedEvents[inserted].isNoteOff() ? 0 : (mergedEvents[inserted].isNoteOn() ? 1 : 2);
      size_t k = a;
      while (k > i) {
        const size_t prev = playbackOrder[k - 1];
        const int prevOrder =
            mergedEvents[prev].isNoteOff() ? 0 : (mergedEvents[prev].isNoteOn() ? 1 : 2);
        if (prevOrder <= insertedOrder) {
          break;
        }
        playbackOrder[k] = prev;
        --k;
      }
      playbackOrder[k] = inserted;
    }
    i = groupEnd;
  }
  loop.playbackOrderDirty = false;
}
void reanchorCaptureIndex(Loop& loop) {
  if (loop.lastTickInLoop == UINT32_MAX) {
    loop.captureNextEventIndex = 0;
    return;
  }
  size_t idx = 0;
  while (idx < loop.capture.store.size()) {
    const MidiEvent& e = loop.capture.store.at(idx);
    if (e.tick >= loop.loopLengthTicks || e.tick > loop.lastTickInLoop) break;
    ++idx;
  }
  loop.captureNextEventIndex = static_cast<uint16_t>(idx);
}
PlaybackEventStream makeMergedPlaybackStream(MergedPlaybackStreamCtx& ctx) {
  return PlaybackEventStream{&ctx, mergedPlaybackStreamSize, mergedPlaybackStreamValid,
                             mergedPlaybackStreamEventAt, mergedPlaybackStreamPhase};
}

PlaybackEventStream makeFixedPlaybackStream(FixedPlaybackStreamCtx& ctx) {
  return PlaybackEventStream{&ctx, fixedPlaybackStreamSize, fixedPlaybackStreamValid,
                             fixedPlaybackStreamEventAt, mergedPlaybackStreamPhase};
}

PlaybackEventStream makeCapturePlaybackStream(Loop& loop) {
  return PlaybackEventStream{&loop, capturePlaybackStreamSize, nullptr,
                             capturePlaybackStreamEventAt, capturePlaybackStreamPhase};
}
void Track::reanchorPlaybackCycle(uint32_t currentTick) {
  Loop& loop = getActiveLoop();
  if (LoopPlaybackRuntime* runtime = playbackRuntime.slotIfAllocated(activeLoopIndex)) {
    runtime->bridgeStream.reset();
  }
  loop.nextEventIndex = 0;
  if (loop.loopLengthTicks == 0) {
    loop.lastTickInLoop = 0;
    projectionCycleStartTick = static_cast<int32_t>(currentTick);
    return;
  }
  const uint32_t phase =
      tickPhaseInLoop(currentTick, loop.startLoopTick, loop.loopLengthTicks);
  projectionCycleStartTick = static_cast<int32_t>(currentTick) - static_cast<int32_t>(phase);
  loop.lastTickInLoop = phase;
}

TRACK_COLD_MEM void Track::resetPlaybackState(uint32_t currentTick) {
  Loop& loop = getActiveLoop();
  loop.nextEventIndex = 0;
  invalidatePlaybackMergedMidiEvents(true);
  if (loop.loopLengthTicks == 0) {
    loop.lastTickInLoop = 0;
    projectionCycleStartTick = static_cast<int32_t>(currentTick);
    return;
  }
  const uint32_t phase =
      tickPhaseInLoop(currentTick, loop.startLoopTick, loop.loopLengthTicks);
  projectionCycleStartTick = static_cast<int32_t>(currentTick) - static_cast<int32_t>(phase);
  loop.lastTickInLoop = phase;
}

void Track::resetPlaybackStateForSlot(uint8_t slotIndex, uint32_t currentTick) {
  if (slotIndex >= Config::MAX_LOOPS_PER_TRACK) return;
  Loop& loop = getLoop(slotIndex);
  if (loop.loopLengthTicks == 0) return;
  loop.nextEventIndex = 0;
  const uint32_t phase =
      tickPhaseInLoop(currentTick, loop.startLoopTick, loop.loopLengthTicks);
  if (slotIndex == activeLoopIndex) {
    projectionCycleStartTick = static_cast<int32_t>(currentTick) - static_cast<int32_t>(phase);
  }
  loop.lastTickInLoop = phase;
  invalidatePlaybackMergedMidiEvents(true);
}

TRACK_COLD_MEM void Track::invalidatePlaybackMergedMidiEvents(bool preserveLedger) {
  // The job borrows generation-qualified scratch. Preserve it until STOPPED
  // recovery has consumed accepted input; terminal cleanup retries this reset.
  if (hasPendingDeferredOverdubStop()) {
    suppressMidiOutputForTransportStop();
    requestDeferredOverdubStopToStopped();
    return;
  }
  bumpPlaybackGeneration();
  playbackRuntime.resetAll(preserveLedger);
}
TRACK_COLD_MEM bool Track::prewarmPlaybackForSlot(uint8_t slotIndex, bool prepareCapture) {
  ensureLoopsAllocated();
  if (slotIndex >= Config::MAX_LOOPS_PER_TRACK) {
    return false;
  }
  // `ok` is the capture-admission answer: every reserve that exists to keep the later
  // deferred stop grains allocation-free must have been established. Callers that are
  // admitting a take must act on it; the boot/load caller may ignore it.
  bool ok = true;
  ExternalPoolAccounting::beginTag(ExternalPoolAccounting::TagCaptureStopJobs);
  ok = prewarmCaptureStopJobs() && ok;
  ExternalPoolAccounting::endTag();
  Loop& loop = loopForSlot(slotIndex);
  LoopPlaybackRuntime* runtime = playbackRuntime.trySlot(slotIndex);
  if (runtime != nullptr) {
    // This is a boot/load or pre-transport path. The hot MIDI path deliberately
    // has no equivalent allocation call.
    if (prepareCapture) {
      // Derived companion-row reserve for this slot (DEC-066): a loop whose canonical
      // payloads are sparse does not reserve the full row horizon, while a dense loop
      // still reaches the ceiling. The clamp lives in the owner.
      ExternalPoolAccounting::beginTag(ExternalPoolAccounting::TagViewPrewarm);
      ok = runtime->prewarmViews(loop.companionRowHorizonBound()) && ok;
      ExternalPoolAccounting::endTag();
    }
    // Boot reserves no capture staging (DEC-065). Staging is provisioned by capture
    // admission - the record arm's `prewarmViews` above, or `Track::startOverdubbing`
    // for a loaded slot - so a slot that never records does not carry two
    // 2,048-event roles resident from boot. Boot still establishes the runtime slot,
    // the stop-descriptor ring, the transfer horizon and the canonical copy
    // directory, which is what makes admission deterministic.
    ExternalPoolAccounting::beginTag(ExternalPoolAccounting::TagCanonicalOutput);
    ok = runtime->prewarmCanonicalCaptureOutput() && ok;
    ExternalPoolAccounting::endTag();
  } else if (prepareCapture) {
    // Without a runtime slot there is no staging and no canonical output, so the take
    // cannot be admitted on the terms this reserve exists to guarantee. `trySlot`
    // only returns null when the pool and the heap both refused the runtime itself.
    ok = false;
  }
#if defined(SESSION_CAPTURE)
  DebugSessionCapture::capturePrewarm(resolveTrackIndexForPersistence(*this), slotIndex,
      prepareCapture, runtime && runtime->canonicalCaptureOutputAdmitted(),
      runtime ? runtime->canonicalCaptureChunks.availableChunks() : 0,
      LoopEventStore::freeChunkCount(), MemoryMonitor::getInternalHeapFreeBytes());
#endif
  // Canonical ownership transfer is a pre-capture resource.  Explicit capture
  // preparation (the RECORDING entry point) must reserve the target loop's
  // horizon even when other tracks are already playing; otherwise the later
  // overdub command is rejected before it can enter the deferred stop path.
  // Boot-only staging keeps the all-stopped guard and never grows during an
  // active transport turn.
  if (prepareCapture || !anyTimingCriticalTrackActive()) {
    ExternalPoolAccounting::beginTag(ExternalPoolAccounting::TagTransferHorizon);
    ok = loop.prewarmActiveOverdubTransferHorizon() && ok;
    ExternalPoolAccounting::endTag();
    // Companion rows are part of the same deferred stop admission. Reserve
    // the complete fixed row horizon while the loop is cold so stop grains
    // never grow vectors or relocate canonical history.
    if (prepareCapture) {
      const size_t usedRows = std::min(loop.passes.editPasses.size(),
                                       RuntimePlaybackViewConfig::kRecordsPerBridge);
      // The grant is the smaller of the remaining row ceiling and this loop's derived
      // companion-row bound, so the preparation horizon never exceeds what this loop's
      // source notes can produce.
      const size_t companionCapacity =
          std::min(RuntimePlaybackViewConfig::kRecordsPerBridge - usedRows,
                   loop.companionRowHorizonBound());
      ExternalPoolAccounting::beginTag(ExternalPoolAccounting::TagCompanionHistory);
      ok = loop.prewarmPreparedCompanionHistory(companionCapacity) && ok;
      ExternalPoolAccounting::endTag();
    }
  }
  ExternalPoolAccounting::beginTag(ExternalPoolAccounting::TagPlaybackOrder);
  (void)loop.getPlaybackOrder();
  ExternalPoolAccounting::endTag();
  return ok;
}

void Track::ensurePlaybackMergedEventsForSlot(uint8_t slotIndex) {
  ensureLoopsAllocated();
  if (slotIndex >= Config::MAX_LOOPS_PER_TRACK) {
    return;
  }
  Loop& loop = loopForSlot(slotIndex);
  LoopPlaybackRuntime* runtime = playbackRuntime.trySlot(slotIndex);
  if (runtime == nullptr) {
    return;
  }
  (void)loop.getPlaybackOrder();
  // Build destination merged MIDI before LoopEnd commit so launch is a cache hit.
  if (loop.hasCommittedPasses() && loop.loopLengthTicks > 0) {
    const uint32_t currentTick = clockManager.getCurrentTick();
    const bool rebuilt =
        ensurePlaybackMergedMidiEventsBuilt(*this, loop, *runtime, true, currentTick);
    if (rebuilt) {
      reconcilePlaybackLedgerAfterFullLoopRebuild(loop, *runtime);
    }
    if (loop.playbackOrderDirty) {
      const ProjectionContext playbackContext = makePlaybackContext(*this, loop, currentTick);
      ::rebuildPlaybackOrder(loop, runtime->mergedMidiEvents.mergedEvents, playbackContext);
    }
    // The full vector/order build is the STOPPED oracle until task 2.4 moves
    // the active clock path to the fixed view. Publish its completed window
    // only while every track is stopped; active transport must never turn the
    // oracle copy into a hidden rebuild on a different, parked track.
    if (!anyTimingCriticalTrackActive()) {
      const PlaybackCoverageInterval coverage{runtime->mergedMidiEvents.windowStartTick,
                                              runtime->mergedMidiEvents.windowLengthTicks,
                                              loop.loopLengthTicks};
      const PlaybackViewSourceIdentity sourceIdentity{
          runtime->mergedMidiEvents.builtFromRevision, playbackGeneration,
          runtime->mergedMidiEvents.builtFromSnapshotIdentity};
      const RuntimeCanonicalPassInputs::CurrentSource live{loop.playbackRevision, playbackGeneration,
          loop.loopLengthTicks, loop.canonicalStorageGeneration};
      if (!runtime->publishStoppedCanonicalOracleView(loop.passes, sourceIdentity, live,
              runtime->mergedMidiEvents.mergedEvents.data(), runtime->mergedMidiEvents.mergedEvents.size(), coverage)) {
        (void)runtime->publishStoppedOracleView(runtime->mergedMidiEvents.mergedEvents.data(),
                                                runtime->mergedMidiEvents.mergedEvents.size(),
                                                sourceIdentity, coverage);
      }
    }
  }
}

bool Track::isPlaybackMergedMidiEventsReadyForSlot(uint8_t slotIndex) const {
  if (slotIndex >= Config::MAX_LOOPS_PER_TRACK || !loopsAllocated()) {
    return false;
  }
  const Loop& loop = loopForSlot(slotIndex);
  if (!loop.hasCommittedPasses() || loop.loopLengthTicks == 0) {
    return false;
  }
  const LoopPlaybackRuntime* runtime = playbackRuntime.slotIfAllocated(slotIndex);
  if (runtime == nullptr) {
    return false;
  }
  return runtime->mergedMidiEvents.builtFromRevision == loop.playbackRevision &&
         !runtime->mergedMidiEvents.mergedEvents.empty();
}

void Track::releasePlaybackMergedMidiEventsMemory() {
  playbackRuntime.resetAll(false);
}

TRACK_COLD_MEM bool Track::tryReleasePlaybackMergedMidiEventsMemory() {
  if (isRecording() || isOverdubbing() || hasPendingDeferredOverdubStop() || getState() == TRACK_ARMED) {
    return false;
  }

  const uint8_t trackIndex = resolveTrackIndexForPersistence(*this);
  const bool selected = trackManager.isSelectedTrack(*this);
  const bool noteEditActive = editManager.isNoteEditActive();
  const bool transportActive = isPlaying();
  bool reclaimed = false;

  for (uint8_t slot = 0; slot < Config::MAX_LOOPS_PER_TRACK; ++slot) {
    Loop& loop = loopForSlot(slot);
    if (!trackManager.isSlotEnabled(trackIndex, slot) && !loop.hasCommittedPasses()) {
      continue;
    }

    // Never allocate here — under Low pressure after a 57KB LoadLoopJob parse, trySlot can
    // fail and slot() null-derefs (session_20260719_000205: silence after parse_leave before
    // apply_enter on the next frame's reclaim).
    LoopPlaybackRuntime* runtime = playbackRuntime.slotIfAllocated(slot);
    if (runtime == nullptr || runtime->mergedMidiEvents.empty()) {
      continue;
    }

    const bool noteEditPreview =
        noteEditActive && selected && slot == getActiveLoopIndex();
    if (noteEditPreview) {
      continue;
    }

    const uint32_t windowRevision = loop.playbackRevision;
    const bool windowStale = runtime->mergedMidiEvents.builtFromRevision != windowRevision;
    if (transportActive && !windowStale) {
      continue;
    }

    runtime->mergedMidiEvents.clear();
    reclaimed = true;
  }
  return reclaimed;
}

TRACK_COLD_MEM bool Track::tryClearCommittedMidiScratch() {
  if (editManager.isNoteEditActive() && trackManager.isSelectedTrack(*this)) {
    return false;
  }
  if (committedMidiScratch_.empty()) {
    return false;
  }
  CommittedMidiScratch::invalidate(committedMidiScratch_, committedMidiScratchRevision_,
                                   committedMidiScratchPublishedIdentity_);
  return true;
}
