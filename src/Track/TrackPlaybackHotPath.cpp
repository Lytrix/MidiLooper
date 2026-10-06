//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "TrackInternal.h"

#include "ClockManager.h"
#include "EditManager.h"
#include "Globals.h"
#include "Logger.h"
#include "LoopEditManager.h"
#include "LoopEventStore.h"
#include "MidiConfig.h"
#include "MidiHandler.h"
#include "TickPhase.h"
#include "Utils/DebugSessionCapture.h"
#include "Utils/IntervalProjection.h"
#include "Utils/MidiNoteName.h"
#include "Utils/PlaybackCursorAdvance.h"
#include "Utils/RuntimeTimingTelemetry.h"
#include "Utils/TrackMem.h"
#include "PlaybackMergedMidiEvents.h"

TRACK_COLD_MEM void Track::reanchorPlaybackProjection(uint32_t currentTick, bool preserveLoopPhaseOrigin) {
  Loop& loop = getActiveLoop();
  if (LoopPlaybackRuntime* runtime = playbackRuntime.slotIfAllocated(activeLoopIndex)) {
    runtime->bridgeStream.reset();
    if (!preserveLoopPhaseOrigin) ++runtime->bridgeProjectionCycle;
  }
  loop.nextEventIndex = 0;
  loop.lastTickInLoop = UINT32_MAX;
  if (!preserveLoopPhaseOrigin) {
    loop.startLoopTick = 0;
    // Fresh transport: play from storage tick 0; drop record-time display offset so
    // playhead, LEDs, and playbackEventPhase share one coordinate frame.
    loop.loopStartTick = 0;
    projectionCycleStartTick = static_cast<int32_t>(currentTick);
    for (uint8_t slotIndex = 0; slotIndex < Config::MAX_LOOPS_PER_TRACK; ++slotIndex) {
      getLoop(slotIndex).lastTickInLoop = UINT32_MAX;
    }
    // Keep LOOP_EDIT baseline aligned with the playback frame so preview depart cannot
    // restore a stale SD loopStartTick onto the live loop (session_20260717_234742).
    if (editManager.isLoopEditSession()) {
      loopEditManager.onGlobalGeometryRestored(*this);
    }
  } else if (loop.loopLengthTicks > 0) {
    const uint32_t phase =
        tickPhaseInLoop(currentTick, loop.startLoopTick, loop.loopLengthTicks);
    projectionCycleStartTick =
        static_cast<int32_t>(currentTick) - static_cast<int32_t>(phase);
  }
}
void Track::rebuildPlaybackOrder() {
  Loop& loop = getActiveLoop();
  LoopPlaybackRuntime* runtime = playbackRuntime.slotIfAllocated(activeLoopIndex);
  if (runtime == nullptr) {
    return;
  }
  const uint32_t currentTick = clockManager.getCurrentTick();
  const bool rebuilt =
      ensurePlaybackMergedMidiEventsBuilt(*this, loop, *runtime, true, currentTick);
  if (rebuilt) {
    reconcilePlaybackLedgerAfterFullLoopRebuild(loop, *runtime, currentTick);
  }
  const ProjectionContext playbackContext = makePlaybackContext(*this, loop, currentTick);
  ::rebuildPlaybackOrder(loop, runtime->mergedMidiEvents.mergedEvents, playbackContext);
}
struct PlaybackJamFilterCtx {
  const Track* track = nullptr;
  const Loop* loop = nullptr;
};

void playbackCursorAdvanceSend(void* ctx, const MidiEvent& evt, uint8_t slotIndex) {
  Track* track = static_cast<Track*>(ctx);
  if (!track->applyPlaybackLedgerEvent(evt, slotIndex)) {
    return;
  }
  track->sendMidiEvent(evt, slotIndex);
}

void playbackCursorAdvanceApplyLedger(void* ctx, const MidiEvent& evt, uint8_t slotIndex) {
  Track* track = static_cast<Track*>(ctx);
  (void)track->applyPlaybackLedgerEvent(evt, slotIndex);
}

void playbackCursorAdvanceSendCapture(void* ctx, const MidiEvent& evt, uint8_t slotIndex) {
  // Live capture echo only. Occupy reads committed playback Entries; capture
  // Off/On must not last-write the same (channel, pitch) slot (221334).
  Track* track = static_cast<Track*>(ctx);
  track->sendMidiEvent(evt, slotIndex);
}

bool playbackCursorAdvanceJamFilter(void* ctx, uint32_t storageTick) {
  const auto* jam = static_cast<const PlaybackJamFilterCtx*>(ctx);
  return jam->track->isStorageTickInJamRegion(storageTick, *jam->loop);
}

namespace {

/// Wrap-tick ledger catch-up: apply (prev, S] from lastCommittedPassId() only.
TRACK_COLD_MEM __attribute__((noinline)) void applyCommittedOverdubPassPlaybackInterval(Track& track, Loop& loop,
                                                              uint8_t slotIndex,
                                                              const PlaybackTickFrame& frame,
                                                              PlaybackJamFilterCtx& jamCtx) {
  if (frame.playbackContext == nullptr) {
    return;
  }
  const PassId passId = loop.lastCommittedPassId();
  if (passId == kInvalidPassId) {
    return;
  }
  const OverdubPass* wrapPass = nullptr;
  for (const OverdubPass& pass : loop.passes.overdubPasses) {
    if (pass.id == passId) {
      wrapPass = &pass;
      break;
    }
  }
  if (wrapPass == nullptr || wrapPass->committedChunkIds.empty()) {
    return;
  }
  SessionMidiEventVec passEvents;
  LoopEventStore::appendChunkRefEvents(wrapPass->committedChunkIds, passEvents);
  const uint32_t loopLength = frame.playbackContext->loopLength;
  for (const MidiEvent& evt : passEvents) {
    const uint32_t evPhase = IntervalProjection::playbackEventPhase(evt.tick, loopLength);
    if (!IntervalProjection::didPlaybackEventCross(frame.atLoopStart, frame.prevTickInLoop, evPhase,
                                                   frame.tickInLoop)) {
      continue;
    }
    if (!playbackCursorAdvanceJamFilter(&jamCtx, evt.tick)) {
      continue;
    }
    playbackCursorAdvanceSend(&track, evt, slotIndex);
  }
}

TRACK_COLD_MEM __attribute__((noinline)) bool catchUpOverdubWrapPlaybackLedger(
    Track& track, Loop& loop, LoopPlaybackRuntime& runtime, uint8_t slotIndex, uint32_t currentTick,
    PlaybackTickFrame& frame, ProjectionContext& playbackContext,
    const PlaybackEventStream& committedStream, PlaybackCursorAdvanceState& mergedAdvance,
    PlaybackEmitPolicy mergedPolicy, PlaybackJamFilterCtx& jamCtx, uint8_t midiChannel) {
  (void)runtime;
  (void)currentTick;
  (void)playbackContext;
  loop.armOverdubWrapAfterLeavingStart(frame.tickInLoop);
  bool mergedIntervalApplied = false;
  if (loop.shouldCommitOverdubWrap(frame.prevTickInLoop, frame.tickInLoop)) {
    (void)advancePlaybackCursorAcrossOverdubWrap(
        mergedAdvance, frame, mergedPolicy, committedStream,
        playbackCursorAdvanceSend, &track, slotIndex, playbackCursorAdvanceJamFilter, &jamCtx,
        midiChannel, playbackCursorAdvanceApplyLedger);
    mergedIntervalApplied = true;
  }
#if defined(SESSION_CAPTURE)
  uint32_t wrapSealUs = 0;
  const uint32_t wrapSealStartUs = micros();
#endif
  if (track.maybeCommitOverdubWrap(frame.prevTickInLoop, frame.tickInLoop)) {
#if defined(SESSION_CAPTURE)
    wrapSealUs = micros() - wrapSealStartUs;
    const uint32_t wrapPassApplyStartUs = micros();
#endif
    applyCommittedOverdubPassPlaybackInterval(track, loop, slotIndex, frame, jamCtx);
#if defined(SESSION_CAPTURE)
    const uint32_t wrapPassApplyUs = micros() - wrapPassApplyStartUs;
    const uint32_t wrapSrcAppendStartUs = micros();
#endif
    loop.appendLastCommittedOverdubPassToSourceView();
#if defined(SESSION_CAPTURE)
    const uint32_t wrapSrcAppendUs = micros() - wrapSrcAppendStartUs;
    SC_ODUB_WRAP_TICK(wrapSealUs + wrapPassApplyUs + wrapSrcAppendUs, wrapSealUs, wrapPassApplyUs,
                      wrapSrcAppendUs, g_overdubWrapCommitUs, g_overdubWrapSealPendingUs,
                      g_overdubWrapBeginCaptureUs);
#endif
  }
#if defined(SESSION_CAPTURE)
  else {
    (void)wrapSealStartUs;
    (void)wrapSealUs;
  }
#endif
  return mergedIntervalApplied;
}

}  // namespace

bool Track::isStorageTickInJamRegion(uint32_t evTick, const Loop& loop) const {
  if (!jamPlaybackActive || jamLength == 0) {
    return true;
  }
  if (jamStartTick == UINT32_MAX) {
    return true;
  }
  const uint32_t jamEnd = jamStartTick + jamLength;
  if (jamEnd <= loop.loopLengthTicks) {
    return evTick >= jamStartTick && evTick < jamEnd;
  }
  return (evTick >= jamStartTick) || (evTick < jamEnd - loop.loopLengthTicks);
}

void Track::playCommittedLoopMidi(uint8_t slotIndex, uint32_t currentTick,
                                  PlaybackMidiTarget target) {
  const bool isActive = (target == PlaybackMidiTarget::ActiveSlot);
  Loop& loop = getLoop(slotIndex);
  // This is the MIDI tick. Runtime creation and view prewarm happen from
  // setup/load admission; do not turn a missed prewarm into a heap allocation
  // in the MIDI service interval.
  LoopPlaybackRuntime* runtimePtr = playbackRuntime.slotIfAllocated(slotIndex);
  if (runtimePtr == nullptr) {
    return;
  }
  LoopPlaybackRuntime& runtime = *runtimePtr;

  // Ordinary PLAYING consumes the selected immutable bridge source. Rolling
  // windows below use that same compiled stream; its indexed cursor remains
  // available across source handoff or a pending refill. Capture, layered and
  // edited playback retain their existing paths until their separate gates.
  const bool fixedViewEligible =
      isActive && isPlaying() && !editManager.isNoteEditActive();
  // Task 3.7a (I5)'s ITCM budget slice: the identity this turn compares against, and the bridge/view that carry it, are
  // resolved in flash. The clock path keeps three values and the call; the identity is still the turn's own (five later
  // uses depend on it), so it is written back rather than fetched again.
  PlaybackViewSourceIdentity expectedFixedSource{};
  const FixedPlaybackBridgeStorage* fixedBridge = nullptr;
  const RuntimePlaybackView* fixedView = nullptr;
  resolveFixedPlaybackSourceForTurn(slotIndex, loop, fixedViewEligible, expectedFixedSource, fixedBridge,
                                    fixedView);

  if (fixedView == nullptr && fixedViewEligible && !isJamPlaybackActive() && !isJamming()) {
    const uint32_t phase = IntervalProjection::tickPhaseInProjectionCycle(
        currentTick, projectionCycleStartTick, loop.loopLengthTicks);
    fixedView = runtime.boundLaunchView(expectedFixedSource, loop.loopLengthTicks, phase, currentTick);
  }

  const RuntimePlaybackView* rollingView = nullptr;
#if PLAYBACK_WINDOW_ROLLING_ENABLED
  if (fixedView == nullptr && fixedViewEligible) {
    const auto resolved = resolveRollingPlaybackWindow(slotIndex, currentTick,
        clockManager.getPlaybackDeadlineClock());
    rollingView = resolved.view;
    if (rollingView != nullptr) fixedView = rollingView;
  }
#endif

  if (runtime.isStale(loop.playbackRevision, playbackGeneration)) {
    // A fixed view already proved its exact generation and source. Do not
    // erase it merely because the legacy runtime cache has not been touched
    // since STOPPED prewarm.
    // S4f.19: a pending deferred Record job owns its bound bar-1 return window, and
    // `reset` refuses precisely to preserve it — so resetting here would abort the
    // interval's only unpublished source. Skip the destructive reset for that owner and
    // sync the revision; the window is served below.
    if (runtime.cachedTrackGeneration != playbackGeneration && fixedView == nullptr &&
        !runtime.hasBoundRecordReturnWindowForPendingJob()) {
      if (!runtime.reset(true)) return;
    }
    // A bound rolling interval retains its cursor/version until its end.
    if (rollingView == nullptr) {
      loop.nextEventIndex = 0;
      if (isActive) loop.captureNextEventIndex = 0;
    }
    runtime.syncRevision(loop.playbackRevision, playbackGeneration);
  }

  if (fixedView == nullptr && fixedBridge == nullptr && fixedViewEligible &&
      (loop.isPlaybackSessionExitPublicationGap() ||
       runtime.hasBoundRecordReturnWindowForPendingJob())) {
    // The first take is not yet backed by a published root: either the session-exit
    // root is still publishing (the audible ~0.6 s inline gap), or a deferred Record
    // job owns the interval and its pass is unpublished (S4f.19). In both cases the
    // bar-1 return window was bound to the final geometry, so serve the launch from it
    // while the playhead is inside that coverage and let the published root replace it.
    const uint32_t launchPhase = IntervalProjection::tickPhaseInProjectionCycle(
        currentTick, projectionCycleStartTick, loop.loopLengthTicks);
    fixedView = runtime.adoptFirstRecordingBarReturnWindow(loop, expectedFixedSource,
                                                           loop.loopLengthTicks, launchPhase);
  }

  // S4f.19 onward: the bar-1 return window covers only the first bar. Past it the
  // unpublished interval is served from a window built over the job's own frozen source,
  // so traversal beyond bar one and a wrap before publication stay audible without a
  // full-take build on the MIDI edge. FLASH-resident: the MIDI edge keeps only the call.
  if (fixedView == nullptr && fixedBridge == nullptr && fixedViewEligible &&
      runtime.hasBoundRecordReturnWindowForPendingJob()) {
    fixedView = adoptOrServiceOnwardRecordWindow(loop, runtime, expectedFixedSource, currentTick);
  }

#if RUNTIME_TIMING_ENABLED
  const uint32_t rebuildStartUs = micros();
#endif
  // Task 2.4 slice 2 -- request/consume.
  //
  // The merged fallback consumes only an immutable `PreparedPlaybackPair`: one
  // coherent (events, order, cursor seed) triple published atomically by the
  // off-clock owner. A source change therefore *requests* a replacement and keeps
  // consuming the last coherent pair, instead of folding accepted capture into the
  // window this tick is emitting and rebuilding the order underneath the cursor.
  //
  // The pair path covers the full-loop window, which is where the reachable active
  // rebuild (seal admission) is concentrated. A long-loop windowed gather keeps its
  // own coverage handoff; that path is recorded as its own slice rather than
  // absorbed here.
  const bool pairApplicable =
      fixedView == nullptr && !loop.shouldAvoidFullVisualRebuild(loop.loopLengthTicks);
  const PreparedPlaybackPair* livePair = nullptr;
  bool pairHeld = false;
  if (pairApplicable) {
    const PreparedPairConsumeDecision decision = resolvePreparedPairConsume(
        runtime, loop, loop.playbackRevision, playbackGeneration, loop.publishedSnapshotIdentity(),
        loop.hasPendingOverdubSealAdmission(), currentTick);
    livePair = decision.pair;
    pairHeld = decision.held;
  }
  // The legacy in-place builder is now reachable only as (a) the STOPPED/off-clock
  // oracle, (b) the long-loop windowed path, and (c) the cold first audible turn of a
  // slot with no pair yet. The detector counts it, so case (c) cannot grow silently.
  // Task 3.7a (I3c-b): **the paged delivery's selection.** A slot with no served window, no bridge and no prepared pair
  // is exactly the shape that falls back to the merged store today; when the deferred lane has **admitted** a paged
  // delivery for this slot -- one cached boolean it maintains off-clock, so this path loads and tests and judges
  // nothing -- the ring serves the tick instead, and the unbounded merged build is not made at all. Nothing is
  // produced, allocated or waited for here: a ring with nothing ahead of the walk ends the turn, and the deferred lane
  // is what continues it.
  const bool pagedDeliveryBound = runtime.pagedEmission.bound;
  // Task 3.7a (I5): **the delivery's admission input comes first, in the turn that uses it.** The boundary judgement is
  // what admits a delivery, so it runs before the selection that reads the admission, and it is judged for a delivery
  // that is merely **bound** -- never gated on the admission it produces (the I3c lesson, one layer down). It is one
  // flash call that returns at once unless a delivery is bound: it recomputes this turn's projection itself, because
  // hoisting those projections inline cost the ITCM **+256 B** on an edge with an 80 B allowance (see the fix's record).
  if (pagedDeliveryBound) {
    servicePagedPassBoundaryForTurn(slotIndex, currentTick);
  }
  const bool pagedEmission = fixedView == nullptr && livePair == nullptr && fixedBridge == nullptr &&
                             runtime.pagedEmission.admittedForEmission();
  const bool useMergedFallback = fixedView == nullptr && livePair == nullptr && !pagedEmission;
  const SessionMidiEventVec* mergedEvents = nullptr;
  if (useMergedFallback) {
    mergedEvents = buildMergedPlaybackFallbackCold(*this, loop, runtime, currentTick);
  }
#if RUNTIME_TIMING_ENABLED
  RUNTIME_TIMING_ADD_TRACK_PATH_REBUILD(micros() - rebuildStartUs);
#endif
  const size_t eventCount =
      fixedBridge != nullptr
          ? fixedBridge->baseEventCount + fixedBridge->insertions.eventCount
          : (fixedView != nullptr
                 ? fixedView->storage.eventCount
                 : (livePair != nullptr ? livePair->eventCount
                                        : (mergedEvents != nullptr ? mergedEvents->size()
                                                                   : (pagedEmission ? 1u : 0u))));
  if (eventCount == 0 && fixedBridge == nullptr
#if PLAYBACK_WINDOW_ROLLING_ENABLED
      && runtime.rolling.phase == LoopPlaybackRuntime::RollingPhase::Idle
#endif
  ) {
    return;
  }

#if RUNTIME_TIMING_ENABLED
  const uint32_t orderSendBeforeUs = RUNTIME_TIMING_TRACK_PATH_SEND_US();
  const uint32_t orderStartUs = micros();
#endif
  ProjectionContext playbackContext = makePlaybackContext(*this, loop, currentTick);
  if (useMergedFallback && loop.playbackOrderDirty) {
#if defined(SESSION_CAPTURE)
    const uint32_t playbackOrderStartUs = micros();
#endif
    ::rebuildPlaybackOrder(loop, *mergedEvents, playbackContext);
#if defined(SESSION_CAPTURE)
    SC_PLAYBACK_ORDER(micros() - playbackOrderStartUs,
                      static_cast<uint32_t>(mergedEvents->size()));
#endif
    reanchorPlaybackIndex(loop, *mergedEvents, loop.getPlaybackOrder(), playbackContext);
  }

  const uint32_t tickInLoop = IntervalProjection::tickPhaseInProjectionCycle(
      currentTick, projectionCycleStartTick, loop.loopLengthTicks);

  const PlaybackEmitPolicy mergedPolicy =
      isActive ? PlaybackEmitPolicy::ActiveCommitted : PlaybackEmitPolicy::LayeredSlot;
  PlaybackJamFilterCtx jamCtx{this, &loop};
  RuntimePlaybackBridgeStream* bridgeStream = fixedBridge != nullptr
      ? runtime.prepareFullLoopBridgeStream(expectedFixedSource, loop.loopLengthTicks,
                                             loop.lastTickInLoop)
      : nullptr;
  if (fixedBridge != nullptr && bridgeStream == nullptr) return;

  const bool wrappedBackward = IntervalProjection::didDisplayPlayheadWrapBackward(
      tickInLoop, loop.lastTickInLoop, loop.loopStartTick, loop.loopLengthTicks);
  if (wrappedBackward) {
    if (bridgeStream != nullptr) {
      // Consume the old cycle's remaining due events before rewinding either
      // physical cursor. This also handles a clock step that skips over wrap.
      const PlaybackTickFrame tail{&playbackContext, loop.loopLengthTicks - 1,
                                    loop.lastTickInLoop, false};
      (void)advancePlaybackCursor(
          {&bridgeStream->playbackIndex, nullptr}, tail, mergedPolicy, bridgeStream->stream(),
          playbackCursorAdvanceSend, this, slotIndex, playbackCursorAdvanceJamFilter, &jamCtx,
          midiChannel, playbackCursorAdvanceApplyLedger);
      bridgeStream = runtime.prepareFullLoopBridgeStream(
          expectedFixedSource, loop.loopLengthTicks, UINT32_MAX, true);
    } else {
      // Overdub/ordinary playback still advances the same cycle identity used
      // when a later prepared bridge joins this running stream.
      ++runtime.bridgeProjectionCycle;
    }
    if (isActive) {
      projectionCycleStartTick = IntervalProjection::advanceProjectionCycleStartTickOnWrap(
          projectionCycleStartTick, loop.loopLengthTicks);
      loop.nextEventIndex = 0;
      loop.captureNextEventIndex = 0;
      logger.trace("Loop wrapped, resetting index");
    } else {
      loop.nextEventIndex = 0;
    }
  }

  const uint32_t prevTickInLoop = loop.lastTickInLoop;
  const bool atLoopStart =
      IntervalProjection::isPlaybackCatchUpWindow(prevTickInLoop, tickInLoop);
  // The **pass boundary** is judged earlier in this turn, by `servicePagedPassBoundaryForTurn` -- above the selection
  // that reads the admission it produces. Judging it here as well would count every boundary twice (measured: the
  // bootstrap fixture read `passCount == 2` after one frame), and it is the *place* that matters: a delivery with no
  // other source is refused above this point, so the judgement has to come before it.

  if (prevTickInLoop == UINT32_MAX && tickInLoop > Config::TICKS_PER_BAR) {
    loop.nextEventIndex = 0;
    runtime.syncRevision(loop.playbackRevision, playbackGeneration);
#if RUNTIME_TIMING_ENABLED
    RUNTIME_TIMING_ADD_TRACK_PATH_ORDER(RUNTIME_TIMING_TRACK_PATH_EXCLUSIVE(
        micros() - orderStartUs, orderSendBeforeUs));
#endif
    return;
  }

  if (livePair != nullptr) {
    // Adopt the pair's cursor seed on the first tick after a publication. The seed is
    // part of the pair (resolved inside the publication turn), so this is a copy, not
    // a content-sized reanchor. A wrap already rewound the cursor; the new cycle must
    // start at 0 rather than at a pre-wrap phase.
    if (runtime.preparedPairConsumedGeneration != livePair->generation) {
      if (!wrappedBackward) {
        loop.nextEventIndex = livePair->cursorSeed;
      }
      runtime.preparedPairConsumedGeneration = livePair->generation;
    }
    if (pairHeld) {
      runtime.notePreparedPairHoldTick();
    }
  }

  PlaybackTickFrame frame{&playbackContext, tickInLoop, prevTickInLoop, atLoopStart};
  MergedPlaybackStreamCtx mergedCtx{};
  if (useMergedFallback) {
    mergedCtx = MergedPlaybackStreamCtx{&loop.getPlaybackOrder(), mergedEvents};
  }
  const auto* emissionWindow = fixedBridge != nullptr
      ? runtime.publishedBridgeWindow(expectedFixedSource, tickInLoop) : nullptr;
  if (emissionWindow != nullptr) {
    // The compiled bridge and the window contain the same cycle stream, and a **logarithmic seek** excludes the already
    // serviced interval on a handoff. The seek is the largest movable block on this edge (I5's ITCM budget slice): it
    // lives in flash now, so the clock path keeps only the call.
    seekBridgeWindowPastServicedInterval(emissionWindow, prevTickInLoop, tickInLoop, loop);
  }
  FixedPlaybackStreamCtx fixedCtx{emissionWindow ? &emissionWindow->storage
      : fixedView != nullptr ? &fixedView->storage
      : livePair != nullptr ? livePair->storage
                            : nullptr};
  PagedEmissionStreamCtx pagedCtx{this, &runtime, slotIndex};
  const PlaybackEventStream committedStream =
      pagedEmission ? makePagedEmissionStream(pagedCtx)
      : emissionWindow ? makeFixedPlaybackStream(fixedCtx)
      : bridgeStream != nullptr ? bridgeStream->stream()
                              : (fixedView != nullptr || livePair != nullptr
                                     ? makeFixedPlaybackStream(fixedCtx)
                                     : makeMergedPlaybackStream(mergedCtx));
  PlaybackCursorAdvanceState mergedAdvance{
      !emissionWindow && bridgeStream != nullptr ? &bridgeStream->playbackIndex : &loop.nextEventIndex,
      (fixedView != nullptr || livePair != nullptr) ? nullptr : &loop.playbackOrderDirty};

  // Wrap-tick ledger catch-up lives in FLASHMEM. Occupy reads Entry.noteId;
  // this is not a special occupy path.
  bool mergedIntervalApplied = false;
  if (isActive && trackState == TRACK_OVERDUBBING) {
    mergedIntervalApplied = catchUpOverdubWrapPlaybackLedger(
        *this, loop, runtime, slotIndex, currentTick, frame, playbackContext, committedStream,
        mergedAdvance, mergedPolicy, jamCtx, midiChannel);
  }
  loop.lastTickInLoop = tickInLoop;

  if (!mergedIntervalApplied) {
    (void)advancePlaybackCursor(
        mergedAdvance, frame, mergedPolicy, committedStream,
        playbackCursorAdvanceSend, this, slotIndex,
        isActive ? playbackCursorAdvanceJamFilter : nullptr, isActive ? &jamCtx : nullptr,
        midiChannel, isActive ? playbackCursorAdvanceApplyLedger : nullptr);
  }

  if (emissionWindow && bridgeStream) bridgeStream->acknowledgeThroughPhase(tickInLoop);

  // Once STOPPING freezes the capture generation, its terminal closures and
  // canonical ordering belong exclusively to the deferred stop owner. Do not
  // let the live cursor sort that mutable store on a MIDI service turn.
  if (isActive && trackState == TRACK_OVERDUBBING &&
      loop.capture.phase == CapturePhase::Overdub && !loop.capture.store.empty()) {
    if (loop.ensureCaptureEventsSorted()) {
      reanchorCaptureIndex(loop);
    }
    PlaybackCursorAdvanceState captureAdvance{&loop.captureNextEventIndex, nullptr};
    (void)advancePlaybackCursor(
        captureAdvance, frame, PlaybackEmitPolicy::ActiveCaptureOverdub,
        makeCapturePlaybackStream(loop), playbackCursorAdvanceSendCapture, this, slotIndex,
        playbackCursorAdvanceJamFilter, &jamCtx, midiChannel);
  }

  runtime.syncRevision(loop.playbackRevision, playbackGeneration);
#if RUNTIME_TIMING_ENABLED
  RUNTIME_TIMING_ADD_TRACK_PATH_ORDER(RUNTIME_TIMING_TRACK_PATH_EXCLUSIVE(
      micros() - orderStartUs, orderSendBeforeUs));
#endif
}

void Track::playMidiEvents(uint32_t currentTick, bool emitMidiOutput) {
  if (transportStopOutputBarrier_ ||
      (hasPendingDeferredOverdubStop() && !captureStopJob().admission.permitsPlaybackEmission())) return;
  if (isStoppedRecording()) {
    return;
  }
  if (!hasAudiblePlaybackSource(activeLoopIndex)) {
    return;
  }
  playbackEmitMidiOutput_ = emitMidiOutput && !muted;
  playCommittedLoopMidi(activeLoopIndex, currentTick, PlaybackMidiTarget::ActiveSlot);
}

void Track::playMidiEventsForSlot(uint8_t slotIndex, uint32_t currentTick, bool emitMidiOutput) {
  if (transportStopOutputBarrier_ ||
      (hasPendingDeferredOverdubStop() && !captureStopJob().admission.permitsPlaybackEmission())) return;
  if (slotIndex >= Config::MAX_LOOPS_PER_TRACK) {
    return;
  }
  if (isStoppedRecording()) {
    return;
  }
  if (!hasAudiblePlaybackSource(slotIndex)) {
    return;
  }
  playbackEmitMidiOutput_ = emitMidiOutput && !muted;
  playCommittedLoopMidi(slotIndex, currentTick, PlaybackMidiTarget::LayeredSlot);
}

// FLASHMEM: the onward service is cold relative to the emit loop (it runs once per
// window per interval) and RAM1 is at its bank boundary, so the MIDI edge keeps only its
// call.
TRACK_COLD_MEM const RuntimePlaybackView* Track::adoptOrServiceOnwardRecordWindow(
    Loop& loop, LoopPlaybackRuntime& runtime,
    const PlaybackViewSourceIdentity& sourceIdentity, uint32_t currentTick) {
  const CaptureStopJob* job = captureStopJobHead();
  if (job == nullptr || job->mode != CapturePhase::Record || !job->ownsRawRange) {
    return nullptr;
  }
  const uint32_t launchPhase = IntervalProjection::tickPhaseInProjectionCycle(
      currentTick, projectionCycleStartTick, loop.loopLengthTicks);
  const RuntimePlaybackView* view =
      runtime.adoptFrozenRecordWindow(sourceIdentity, loop.loopLengthTicks, launchPhase);
  if (view != nullptr) return view;
  // One bounded build grain; the next turn adopts the completed window.
  (void)runtime.serviceFrozenRecordWindow(
      job->rawRange, job->rawRange.generation(), sourceIdentity, loop.loopLengthTicks,
      launchPhase, Config::TICKS_PER_BAR, MidiServiceDrain::kCanonicalPrefixInputQuantum);
  return nullptr;
}

// FLASHMEM: the admission check is a cold predicate on the MIDI edge (it is called
// once per turn, alongside the FLASH-resident session-exit gap predicate), and RAM1 is
// at its bank boundary.
TRACK_COLD_MEM bool Track::hasAudiblePlaybackSource(uint8_t slotIndex) const {
  if (slotIndex >= Config::MAX_LOOPS_PER_TRACK || !loopsAllocated()) {
    return false;
  }
  const Loop& loop = getLoop(slotIndex);
  if (loop.loopLengthTicks == 0) {
    return false;
  }
  if (loop.hasCommittedPasses()) {
    return true;
  }
  // S4f.19: an unpublished Record interval is audible from its retained bar-1 return
  // window, but only while a pending deferred job owns that window. Without it the
  // interval has no source yet and must stay silent rather than fabricate one.
  const LoopPlaybackRuntime* runtime = playbackRuntime.slotIfAllocated(slotIndex);
  return runtime != nullptr && runtime->hasServableDeferredRecordView();
}

namespace {

uint8_t remappedPlaybackChannel(const MidiEvent& evt, uint8_t trackMidiChannel) {
  const bool isChannelMessage = evt.type == midi::NoteOn || evt.type == midi::NoteOff ||
                                evt.type == midi::ControlChange || evt.type == midi::PitchBend ||
                                evt.type == midi::AfterTouchChannel || evt.type == midi::ProgramChange;
  if (isChannelMessage && (evt.channel == 0 || (evt.channel >= 1 && evt.channel <= 16))) {
    return trackMidiChannel;
  }
  return evt.channel;
}

}  // namespace

bool Track::applyPlaybackLedgerEvent(const MidiEvent& evt, uint8_t playbackSlotIndex) {
  if (!isAudiblePlaybackActive()) {
    return false;
  }
  if (playbackSlotIndex >= Config::MAX_LOOPS_PER_TRACK) {
    return false;
  }
  const uint8_t channel = remappedPlaybackChannel(evt, midiChannel);
  LoopPlaybackRuntime* runtime = playbackRuntime.slotIfAllocated(playbackSlotIndex);
  return runtime != nullptr && runtime->ledger.applyPlaybackEvent(channel, evt);
}

void Track::sendMidiEvent(const MidiEvent& evt, uint8_t playbackSlotIndex) {
  if (!isAudiblePlaybackActive()) return;
  if (playbackSlotIndex >= Config::MAX_LOOPS_PER_TRACK) return;
#if RUNTIME_TIMING_ENABLED
  const uint32_t sendStartUs = micros();
#endif
  ignorePlaybackMidiInput = true;  // Suppress playback-echo MIDI during overdub capture
  MidiEvent evtCopy = evt;
  evtCopy.channel = remappedPlaybackChannel(evt, midiChannel);

  if (evt.isNoteOn() && trackState == TRACK_OVERDUBBING) {
    collectOverlapHoldPlaybackNoteOn(evt.noteId, evt.data.noteData.note);
  }

  // Hot path: logging every loop note at DEBUG blocks USB Serial for milliseconds and freezes the UI.
  // Use LOG_TRACE so deep MIDI tracing is opt-in (Logger at TRACE + CAT_MIDI on).
  // Timing-path review, item 4: **the gate comes first**. The note-name lookup and the octave arithmetic used
  // to be evaluated for every played note only for `log()` to discard the line, which is the common case (the
  // runtime level is `LOG_DEBUG`). The line itself is unchanged when tracing is on.
  if ((evt.isNoteOn() || evt.isNoteOff()) && Logger::isEnabled(CAT_MIDI, LOG_TRACE)) {
    logger.log(CAT_MIDI, LOG_TRACE, "Loop SEND: %s ch=%d %s%d (note %d) vel=%d tick=%lu",
               evt.isNoteOn() ? "NoteOn" : "NoteOff",
               evtCopy.channel, MidiNoteName::pitchClassName(evtCopy.data.noteData.note),
               MidiNoteName::octave(evtCopy.data.noteData.note),
               evtCopy.data.noteData.note, evtCopy.data.noteData.velocity, evt.tick);
  }
  // A Stop can arrive while an internal-clock interval is already walking its
  // due cursor. Recheck the volatile barrier directly before the physical send.
  if (playbackEmitMidiOutput_ && !transportStopOutputBarrier_) {
#if defined(PIO_UNIT_TEST_NATIVE)
    // 3.7a (I4): the send sink records the pair this line hands the handler -- the same value, at the
    // same point, after this function's own gates and remap. Native-only (see Track.h); when the
    // capacity is outrun the drop is counted rather than the stream silently shortened.
    if (sentPlaybackMidiCountForTest_ < kSentPlaybackMidiCapacityForTest) {
      sentPlaybackMidiForTest_[sentPlaybackMidiCountForTest_].event = evtCopy;
      sentPlaybackMidiForTest_[sentPlaybackMidiCountForTest_].slotIndex = playbackSlotIndex;
      ++sentPlaybackMidiCountForTest_;
    } else {
      ++sentPlaybackMidiDroppedForTest_;
    }
#endif
    midiHandler.sendRealtimePlaybackEvent(evtCopy);
#if RUNTIME_TIMING_ENABLED
    if (evt.isNoteOn() || evt.isNoteOff()) {
      const uint32_t sentUs = micros();
      RUNTIME_TIMING_RECORD_NOTE_SEND_LATENESS(evt.isNoteOn(), sentUs, evt.tick,
          resolveTrackIndexForPersistence(*this), playbackSlotIndex,
          evtCopy.channel, evtCopy.data.noteData.note);
    }
#endif
  }
  ignorePlaybackMidiInput = false;  // Reset playback state
#if RUNTIME_TIMING_ENABLED
  RUNTIME_TIMING_ADD_TRACK_PATH_SEND(micros() - sendStartUs);
#endif
}

#if defined(PIO_UNIT_TEST_NATIVE)
// 3.7a (I4): the MIDI edge's own record. Native-only, so the firmware keeps the hot path unchanged
// (no call, no buffer, and the post-link RAM1 line is byte-identical).
size_t Track::sentPlaybackMidiCountForTest() const { return sentPlaybackMidiCountForTest_; }

const Track::SentPlaybackMidiForTest& Track::sentPlaybackMidiAtForTest(size_t index) const {
  static const SentPlaybackMidiForTest kEmpty{};
  return index < sentPlaybackMidiCountForTest_ ? sentPlaybackMidiForTest_[index] : kEmpty;
}

size_t Track::sentPlaybackMidiDroppedForTest() const { return sentPlaybackMidiDroppedForTest_; }

void Track::clearSentPlaybackMidiForTest() {
  sentPlaybackMidiCountForTest_ = 0;
  sentPlaybackMidiDroppedForTest_ = 0;
}
#endif

TRACK_COLD_MEM __attribute__((noinline)) void Track::silenceTrackMidiOutput() {
  if ((midiChannel >= MidiConfig::LED_CHANNEL_MIN &&
       midiChannel <= MidiConfig::LED_CHANNEL_MAX) ||
      (midiChannel >= MidiConfig::RECORD_EXCLUDE_MIN &&
       midiChannel <= MidiConfig::RECORD_EXCLUDE_MAX)) {
    return;
  }
  midiHandler.sendControlChange(midiChannel, 123, 0);
}

TRACK_COLD_MEM __attribute__((noinline)) void Track::silenceSlotMidiOutput(uint8_t slotIndex) {
  if (slotIndex >= Config::MAX_LOOPS_PER_TRACK) {
    return;
  }
  uint16_t uniqueCount = 0;
  uint16_t uniqueKeys[ActiveNoteLedger::kMaxOpenNotes];
  LoopPlaybackRuntime* runtime = playbackRuntime.slotIfAllocated(slotIndex);
  if (runtime == nullptr) {
    return;
  }
  runtime->ledger.forEachActive(
      [&](uint8_t channel, uint8_t note, const ActiveNoteLedger::Entry&) {
        const uint16_t key =
            (static_cast<uint16_t>(channel) << 8) | static_cast<uint16_t>(note);
        for (uint16_t i = 0; i < uniqueCount; ++i) {
          if (uniqueKeys[i] == key) {
            return;
          }
        }
        if (uniqueCount < ActiveNoteLedger::kMaxOpenNotes) {
          uniqueKeys[uniqueCount++] = key;
        }
        midiHandler.sendMidiEvent(MidiEvent::NoteOff(0, channel, note, 0));
      });
}

TRACK_COLD_MEM __attribute__((noinline)) void Track::sendAllNotesOff() {
  // Control Change 123 = All Notes Off. Skip controller-only channels so we do
  // not clear DROID LEDs/buttons/faders when transport stops.
  for (uint8_t ch = 1; ch <= 16; ++ch) {
    if ((ch >= MidiConfig::LED_CHANNEL_MIN && ch <= MidiConfig::LED_CHANNEL_MAX) ||
        (ch >= MidiConfig::RECORD_EXCLUDE_MIN && ch <= MidiConfig::RECORD_EXCLUDE_MAX)) {
      continue;
    }
    midiHandler.sendControlChange(ch, 123, 0);
  }
  playbackRuntime.clearAllLedgers();
  // also clear any half-open pending notes so they don't get forced later
  pendingNotes.clear();
  logger.logTrackEvent("All Notes Off sent", clockManager.getCurrentTick());
}
