//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "Utils/RuntimeProfiler.h"
#include "Utils/PlaybackTickContext.h"
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
    reconcilePlaybackLedgerAfterFullLoopRebuild(loop, *runtime, currentTick,
                                                playbackClosureServeFloor(*this, loop, currentTick));
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

/// Task 3.15e slice 3 (C5): the live overlay window's emission/ledger callbacks. `eventAt` recorded the row's origin
/// before this call, so the ledger is applied only to committed-origin rows; an overlay row sounds once without
/// committed-pass ownership. Flash-resident (reached through function pointers; no ITCM cost or veneer).
struct OverlayEmitCtx {
  Track* track = nullptr;
  const FixedPlaybackStreamCtx* stream = nullptr;
};

TRACK_COLD_MEM void playbackCursorAdvanceSendOverlay(void* ctx, const MidiEvent& evt, uint8_t slotIndex) {
  auto* overlay = static_cast<OverlayEmitCtx*>(ctx);
  if (overlay->stream->lastOrigin != kLiveOverlayPassOrigin &&
      !overlay->track->applyPlaybackLedgerEvent(evt, slotIndex)) {
    return;
  }
  overlay->track->sendMidiEvent(evt, slotIndex);
}

TRACK_COLD_MEM void playbackCursorAdvanceApplyLedgerOverlay(void* ctx, const MidiEvent& evt, uint8_t slotIndex) {
  auto* overlay = static_cast<OverlayEmitCtx*>(ctx);
  if (overlay->stream->lastOrigin == kLiveOverlayPassOrigin) return;
  (void)overlay->track->applyPlaybackLedgerEvent(evt, slotIndex);
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
  // A pending Record source remains the committed base during an accepted
  // overdub. Ordinary fixed/bridge eligibility stays PLAYING-only above.
  const bool recordReturnEligible =
      isActive && isAudiblePlaybackActive() && !editManager.isNoteEditActive();
  // Task 3.7a (I5)'s ITCM budget slice: the identity this turn compares against, and the bridge/view that carry it, are
  // resolved in flash. The clock path keeps three values and the call; the identity is still the turn's own (five later
  // uses depend on it), so it is written back rather than fetched again.
  PlaybackViewSourceIdentity expectedFixedSource{};
  const FixedPlaybackBridgeStorage* fixedBridge = nullptr;
  const RuntimePlaybackView* fixedView = nullptr;
  bool overlayLiveSelected = false;
#if PLAYBACK_WINDOW_ROLLING_ENABLED
  bool rollingViewSelected = false;
  resolveQualificationPlaybackSourceForTurn(slotIndex, loop, fixedViewEligible, currentTick,
                                            expectedFixedSource, fixedBridge, fixedView,
                                            rollingViewSelected, overlayLiveSelected);
  // Live Record never credits a legacy gather as readiness, including a
  // missing later interval in an otherwise valid multi-bar rolling period.
  if (isRecording() && !rollingViewSelected) return;
#else
  resolveFixedPlaybackSourceForTurn(slotIndex, loop, fixedViewEligible, expectedFixedSource, fixedBridge,
                                    fixedView);
  if (fixedView == nullptr && fixedViewEligible && !isJamPlaybackActive() && !isJamming()) {
    const uint32_t phase = IntervalProjection::tickPhaseInProjectionCycle(
        currentTick, projectionCycleStartTick, loop.loopLengthTicks);
    fixedView = runtime.boundLaunchView(expectedFixedSource, loop.loopLengthTicks, phase, currentTick);
  }
  const bool rollingViewSelected = false;
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
        !runtime.hasBoundRecordReturnWindowForPendingJob() && !runtime.hasPositionedPreviewReadyOwed()) {
      if (!runtime.reset(true)) return;
#if defined(SESSION_CAPTURE)
      if (isActive) reportFirstBarRouteForSlot(slotIndex, currentTick, 8, nullptr);
#endif
    }
    // A bound rolling interval retains its cursor/version until its end.
    if (!rollingViewSelected) {
      loop.nextEventIndex = 0;
      if (isActive) loop.captureNextEventIndex = 0;
    }
    runtime.syncRevision(loop.playbackRevision, playbackGeneration);
  }

  if (fixedView == nullptr && fixedBridge == nullptr && recordReturnEligible &&
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
  if (fixedView == nullptr && fixedBridge == nullptr && recordReturnEligible &&
      runtime.hasBoundRecordReturnWindowForPendingJob()) {
    fixedView = adoptOrServiceOnwardRecordWindow(loop, runtime, expectedFixedSource, currentTick);
  }

#if RUNTIME_TIMING_ENABLED
  const uint32_t rebuildStartUs = micros();
#endif
  // The legacy in-place builder is now reachable only as (a) the STOPPED/off-clock
  // oracle, (b) the long-loop windowed path, and (c) the cold first audible turn of a
  // slot with no served source. The detector counts it, so case (c) cannot grow silently.
  // Task 3.7a (I3c-b): **the paged delivery's selection.** A slot with no served window and no bridge
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
    const uint32_t prevTickForTail = loop.lastTickInLoop;
    servicePagedPassBoundaryForTurn(slotIndex, currentTick);
    servicePagedOwedTailTurn(slotIndex, currentTick, prevTickForTail);
  }
  const bool pagedEmission = fixedView == nullptr && fixedBridge == nullptr &&
                             runtime.pagedEmission.admittedForEmission();
  const bool useMergedFallback = fixedView == nullptr && !pagedEmission;
  const SessionMidiEventVec* mergedEvents = nullptr;
  if (useMergedFallback) {
    // DEC-239 diagnostic: names this tick-path route into the merged build (entered from IRQ_PIT on internal clock).
    PROFILE_SCOPE_LONG(profileScope, RuntimeProfiler::ProfileId::TickMergedFallback_build);
    mergedEvents = selectMergedPlaybackFallbackForTick(*this, loop, runtime, slotIndex, currentTick);
    if (mergedEvents == nullptr) {
      // DEC-241: an interrupt tick with no consumable fallback owes the build and ends the turn, contained.
      (void)containPlaybackTurnIfNoRouteForSlot(slotIndex, currentTick, isActive);
      PlaybackTickContext::noteTickRoute(PlaybackTickContext::TickRoute::None);
      return;
    }
  }

  // ---- Task 4.5: the owner-level **"no serving route at all"** predicate, beside route selection ---------------------
  // 4.5's first wiring was withdrawn because a *resolver rejection* is not "nothing can serve this slot": the bridge, a
  // bound view, the paged ring and the retained merged fallback may each serve the very same turn. So
  // containment is asked **only** when every alternative is out for **this phase** -- never inferred from one route's
  // refusal. Availability, never content: an event-empty source still counts as a route ("no new events" is not silence).
  //
  // The predicate costs the MIDI edge **four integer compares and one call**: the phase, the merged catch-all's own
  // bounded-window coverage and the containment decision all live in FLASH (the first inline form cost +192 B of ITCM,
  // moving the projection arithmetic into the cold owner brought it back to the budget line below).
  const bool anyServingRouteSelected =
      fixedBridge != nullptr || fixedView != nullptr || pagedEmission;
#if defined(SESSION_CAPTURE)
  if (isActive)
    reportFirstBarRouteForSlot(slotIndex, currentTick,
        fixedBridge != nullptr ? 1 : fixedView != nullptr ? 2 : pagedEmission ? 3 : mergedEvents != nullptr ? 4 : 0,
        fixedView);
#endif
  if (!anyServingRouteSelected) {
    // Every reader above is out, so the merged catch-all is the only remaining candidate -- it is the one branch that is
    // always selectable. The cold owner decides whether *its* window covers this phase and asks containment only if not.
    const bool mergedCovers = containPlaybackTurnIfNoRouteForSlot(slotIndex, currentTick, isActive) ==
                              LastResortOutcome::NotNeeded;
    PlaybackTickContext::noteTickRoute(mergedCovers ? PlaybackTickContext::TickRoute::Merged
                                                    : PlaybackTickContext::TickRoute::None);
  } else {
    // playback-tick-consume-only phase 1: the consumed route, in emission priority (report-only).
    PlaybackTickContext::noteTickRoute(fixedBridge != nullptr ? PlaybackTickContext::TickRoute::Bridge
                                       : fixedView != nullptr ? PlaybackTickContext::TickRoute::View
                                                              : PlaybackTickContext::TickRoute::Paged);
  }
#if RUNTIME_TIMING_ENABLED
  RUNTIME_TIMING_ADD_TRACK_PATH_REBUILD(micros() - rebuildStartUs);
#endif
  const size_t eventCount =
      fixedBridge != nullptr
          ? fixedBridge->baseEventCount + fixedBridge->insertions.eventCount
          : (fixedView != nullptr
                 ? fixedView->storage.eventCount
                 : (mergedEvents != nullptr ? mergedEvents->size()
                                            : (pagedEmission ? 1u : 0u)));
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
#if PLAYBACK_WINDOW_ROLLING_ENABLED
  // C3 (DEC-116) layered qualification: a slot that is no longer the active one but is still enabled and
  // sounding is read through **its own** phase origin, not through the Track-level `projectionCycleStartTick`
  // that the switch's `commitQueuedPlaybackStart` re-derives from the incoming loop. With a differing origin
  // delta (768 against a 1,536-tick outgoing loop) the shared anchor drags the outgoing playhead back to
  // phase 0, so a held On@700/Off@800 never crosses at 800: `lastTickInLoop` (767) is then read against the
  // new phase (0), `isPlaybackCatchUpWindow` reports a fresh origin and `didPlaybackEventCross` admits only
  // events at or below phase 0 -- and the displaced region re-triggers on the way back.
  // `Loop::startLoopTick` is the per-slot origin the repo already uses for a non-active slot
  // (`Track::resetPlaybackStateForSlot`, `SlotFocusDisplay::resolvePlayheadStoragePhase`), and this anchor is
  // the same relation those use (`currentTick - phase`). Being a pure function of the tick, the phase and the
  // `lastTickInLoop` this turn writes below stay continuous across the switch: no new state, no buffer, no
  // allocation, no sort and no scan on the clock edge. Active-slot projection authority stays with `Track`.
  const int32_t slotCycleStartTick =
      isActive ? projectionCycleStartTick
               : static_cast<int32_t>(currentTick) -
                     static_cast<int32_t>(IntervalProjection::tickPhaseInLoop(
                         currentTick, loop.startLoopTick, loop.loopLengthTicks));
#endif
  ProjectionContext playbackContext = makePlaybackContext(*this, loop, currentTick);
#if PLAYBACK_WINDOW_ROLLING_ENABLED
  // The context matches the phase above, so the ordering/anchor a consumer reads and the frame this turn
  // emits cannot disagree. (Playback ordering uses `loopLength` only today; this keeps the anchor honest.)
  playbackContext.projectionCycleStartTick = slotCycleStartTick;
  if (rollingViewSelected && isRecording() && fixedView != nullptr &&
      fixedView->source.origin == PlaybackWindowOrigin::Capture)
    playbackContext.loopLength = fixedView->coverage.loopLengthTicks;
#endif
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

#if PLAYBACK_WINDOW_ROLLING_ENABLED
  // C3 (DEC-116) slot-qualified layered phase. The active target's value is unchanged (`slotCycleStartTick`
  // *is* the Track anchor for it, so this is the expression it always had); the default image compiles the
  // other branch, which is the original statement verbatim.
  const uint32_t tickInLoop = IntervalProjection::tickPhaseInProjectionCycle(
      currentTick, slotCycleStartTick, playbackContext.loopLength);
#else
  const uint32_t tickInLoop = IntervalProjection::tickPhaseInProjectionCycle(
      currentTick, projectionCycleStartTick, loop.loopLengthTicks);
#endif

  const PlaybackEmitPolicy mergedPolicy =
      isActive ? PlaybackEmitPolicy::ActiveCommitted : PlaybackEmitPolicy::LayeredSlot;
  PlaybackJamFilterCtx jamCtx{this, &loop};
  RuntimePlaybackBridgeStream* bridgeStream = fixedBridge != nullptr
      ? runtime.prepareFullLoopBridgeStream(expectedFixedSource, loop.loopLengthTicks,
                                             loop.lastTickInLoop)
      : nullptr;
  if (fixedBridge != nullptr && bridgeStream == nullptr) return;

  const bool wrappedBackward = IntervalProjection::didDisplayPlayheadWrapBackward(
      tickInLoop, loop.lastTickInLoop, loop.loopStartTick, playbackContext.loopLength);
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
#if PLAYBACK_WINDOW_ROLLING_ENABLED
      // Live Record has its own explicit playback anchor. Only a qualified
      // full-period growth handoff may replace it; ordinary wraps preserve it.
      if (!(rollingViewSelected && isRecording()))
#endif
        projectionCycleStartTick = IntervalProjection::advanceProjectionCycleStartTickOnWrap(
            projectionCycleStartTick, playbackContext.loopLength);
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
    // The phase was reset (a stop, a queued/grid start or a re-anchored projection) and the playhead is already past
    // the first bar, so this turn only **establishes** the phase: nothing is emitted and no catch-up burst is made.
    // Writing the phase back is what makes it exactly one turn. Without it every later turn re-enters this branch
    // (the previous phase stays unknown) for the remainder of the pass, so the interval is silent until the wrap even
    // though its content is read material -- measured 3.076 s after the wrapped record stop on the qualification image
    // (`captures/i5-dec143-sealed-extent-base-01`: first note on the wire 43.216 s, first turn after the wrap).
    loop.nextEventIndex = 0;
    loop.lastTickInLoop = tickInLoop;
    runtime.syncRevision(loop.playbackRevision, playbackGeneration);
#if RUNTIME_TIMING_ENABLED
    RUNTIME_TIMING_ADD_TRACK_PATH_ORDER(RUNTIME_TIMING_TRACK_PATH_EXCLUSIVE(
        micros() - orderStartUs, orderSendBeforeUs));
#endif
    return;
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
                            : nullptr};
  if (overlayLiveSelected && fixedView != nullptr) {
    fixedCtx.passOrigins = fixedView->passOrigins;
    fixedCtx.readableCount = fixedView->readableEventCount;
  }
  PagedEmissionStreamCtx pagedCtx{this, &runtime, slotIndex};
  const PlaybackEventStream committedStream =
      pagedEmission ? makePagedEmissionStream(pagedCtx)
      : emissionWindow ? makeFixedPlaybackStream(fixedCtx)
      : bridgeStream != nullptr ? bridgeStream->stream()
                              : (fixedView != nullptr
                                     ? (overlayLiveSelected ? makeOverlayPlaybackStream(fixedCtx)
                                                            : makeFixedPlaybackStream(fixedCtx))
                                     : makeMergedPlaybackStream(mergedCtx));
  PlaybackCursorAdvanceState mergedAdvance{
      !emissionWindow && bridgeStream != nullptr ? &bridgeStream->playbackIndex : &loop.nextEventIndex,
      (fixedView != nullptr) ? nullptr : &loop.playbackOrderDirty};

  // Wrap-tick ledger catch-up lives in FLASHMEM. Occupy reads Entry.noteId;
  // this is not a special occupy path.
  bool mergedIntervalApplied = false;
  if (isActive && trackState == TRACK_OVERDUBBING) {
    mergedIntervalApplied = catchUpOverdubWrapPlaybackLedger(
        *this, loop, runtime, slotIndex, currentTick, frame, playbackContext, committedStream,
        mergedAdvance, mergedPolicy, jamCtx, midiChannel);
  }
  loop.lastTickInLoop = tickInLoop;

  OverlayEmitCtx overlayEmit{this, &fixedCtx};
  PlaybackSendFn sendFn = playbackCursorAdvanceSend;
  void* sendCtx = this;
  PlaybackSendFn applyLedgerFn = isActive ? playbackCursorAdvanceApplyLedger : nullptr;
  if (overlayLiveSelected) {
    sendFn = playbackCursorAdvanceSendOverlay;
    sendCtx = &overlayEmit;
    applyLedgerFn = playbackCursorAdvanceApplyLedgerOverlay;
  }
  if (!mergedIntervalApplied) {
    const auto readResult = advancePlaybackCursor(
        mergedAdvance, frame, mergedPolicy, committedStream,
        sendFn, sendCtx, slotIndex,
        isActive ? playbackCursorAdvanceJamFilter : nullptr, isActive ? &jamCtx : nullptr,
        midiChannel, applyLedgerFn);
#if SC_PLAYBACK_READ_WITNESS_ENABLED
    if (readResult == PlaybackAdvanceResult::Completed)
      observePlaybackReadForTurn(slotIndex, fixedCtx.storage, fixedBridge);
#else
    (void)readResult;
#endif
  }

  if (emissionWindow && bridgeStream) bridgeStream->acknowledgeThroughPhase(tickInLoop);

  // Once STOPPING freezes the capture generation, its terminal closures and
  // canonical ordering belong exclusively to the deferred stop owner. Do not
  // let the live cursor sort that mutable store on a MIDI service turn.
  // Slice 3 (C4): while the live overlay window is the selected source, its rows are already in that window in canonical
  // order — the second capture walk and its sort do not run. Every other state keeps today's echo exactly.
  if (!overlayLiveSelected && isActive && trackState == TRACK_OVERDUBBING &&
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
  if (transportStopOutputBarrier_ || !captureIntervalPermitsPlayback(captureInterval_)) return;
  if (isStoppedRecording()) {
    return;
  }
  if (!hasAudiblePlaybackSource(activeLoopIndex)) {
#if defined(SESSION_CAPTURE)
    if (isPlaying()) reportFirstBarRouteForSlot(activeLoopIndex, currentTick, 7, nullptr);
#endif
    return;
  }
  playbackEmitMidiOutput_ = emitMidiOutput && !muted;
  playCommittedLoopMidi(activeLoopIndex, currentTick, PlaybackMidiTarget::ActiveSlot);
}

void Track::playMidiEventsForSlot(uint8_t slotIndex, uint32_t currentTick, bool emitMidiOutput) {
  if (transportStopOutputBarrier_ || !captureIntervalPermitsPlayback(captureInterval_)) return;
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

// FLASHMEM: a MIDI service edge adopts only a complete frozen-source successor;
// preparation belongs to the selected deferred playback-view owner turn.
TRACK_COLD_MEM const RuntimePlaybackView* Track::adoptOrServiceOnwardRecordWindow(
    Loop& loop, LoopPlaybackRuntime& runtime,
    const PlaybackViewSourceIdentity& sourceIdentity, uint32_t currentTick) {
  const CaptureStopPresentation* presentation = captureStopPresentationForSlot(activeLoopIndex);
  // The slot record owns the extent before and after publication. A source-matching copied view
  // remains readable through either phase; only the fallback across revision changes below needs
  // the published presentation's fully qualified content identity.
  if (presentation == nullptr || !presentation->ownsExtent ||
      presentation->owner != &loop || presentation->loopId != loop.loopId) return nullptr;
  const uint32_t phase = IntervalProjection::tickPhaseInProjectionCycle(
      currentTick, projectionCycleStartTick, loop.loopLengthTicks);
  const RuntimePlaybackViewSet* set = runtime.viewSetIfPrewarmed();
  const uint8_t previousCurrent = set != nullptr ? set->lifecycle.current
      : RuntimePlaybackViewLifecycle::kNoView;
  const RuntimePlaybackView* adopted =
      runtime.adoptFrozenRecordWindow(sourceIdentity, loop.loopLengthTicks, phase);
  if (adopted == nullptr && set != nullptr && set->lifecycle.ready < set->views.size()) {
    const RuntimePlaybackView& ready = set->views[set->lifecycle.ready];
    RuntimePlaybackViewSource frozen{};
    PlaybackViewSourceIdentity receipt{};
    // A ready copied before pass publication still owes this boundary. Qualify
    // the live frozen job before adopting, and never relabel it as newer content.
    // The prepared window is adoptable **mid-window**, not only at the bar boundary that prepared it: the current-window
    // progression means its coverage already contains the playhead (a boundary would then never come until the next
    // bar). Two identities are accepted, each kept as its own -- never relabelled:
    //  * `Committed` (post-publication preparation), unchanged from before;
    //  * `Capture` (the sealed extent's own frozen token, pre-publication), qualified through the job's presentation so
    //    a stale or foreign interval can never be adopted. Without this branch the pre-publication window is prepared,
    //    complete, covers the playhead and is still unservable, which is the silence this slice closes.
    // Qualification runs **first**: it is what fills `frozen`, so the Capture identity can only be compared after it
    // (reading the token before this call compares against an empty receipt and silently refuses every frozen window).
    const bool qualified = qualifiedFrozenRecordCaptureSourceForSlot(
        presentation->slot, presentation->interval, frozen, receipt);
    const bool committedReady = ready.source.origin == PlaybackWindowOrigin::Committed &&
        ready.source.sourceGeneration == sourceIdentity.sourceGeneration;
    // The view carries the source *identity*; the receipt carries the same frozen content token the preparation
    // stamped, so a reused or foreign interval cannot match.
    const bool captureReady = qualified && ready.source.origin == PlaybackWindowOrigin::Capture &&
        ready.source.captureContentGeneration != 0 &&
        ready.source.captureContentGeneration == frozen.captureContentToken;
    if (ready.complete && !ready.published && !ready.rawCoveragePrepared &&
        !ready.firstRecordReturnWindow && ready.source.snapshotIdentity == nullptr &&
        ready.coverage.loopLengthTicks == loop.loopLengthTicks && ready.coverage.contains(phase) &&
        qualified && (committedReady || captureReady)) {
      adopted = runtime.adoptFrozenRecordWindow(ready.source, loop.loopLengthTicks, phase);
      if (adopted != nullptr && captureReady)
        runtime.frozenCaptureAdoptedStorage = static_cast<uint16_t>(adopted->storageGeneration);
    }
  }
  if (adopted == nullptr && set != nullptr && previousCurrent < set->views.size()) {
    const RuntimePlaybackView& current = set->views[previousCurrent];
    RuntimePlaybackViewSource frozen{};
    PlaybackViewSourceIdentity receipt{};
    // Record pass publication advances canonical currency, not the sealed
    // range. Keep its already-served copied interval under the original stamp
    // until a complete successor takes over; never adopt a stale ready here.
    // The same two identities as the ready branch above: a `Capture` window adopted mid-window must keep
    // serving on the following ticks, or it sounds for one tick and the bar falls silent (121645). Only
    // the exact window that branch adopted qualifies: a live rolling view can share the frozen token.
    if (current.complete && current.published && !current.rawCoveragePrepared &&
        !current.firstRecordReturnWindow && current.source.snapshotIdentity == nullptr &&
        current.coverage.loopLengthTicks == loop.loopLengthTicks && current.coverage.contains(phase) &&
        qualifiedFrozenRecordCaptureSourceForSlot(presentation->slot, presentation->interval, frozen, receipt)) {
      const bool committedCurrent = current.source.origin == PlaybackWindowOrigin::Committed &&
          current.source.sourceGeneration == sourceIdentity.sourceGeneration;
      const bool captureCurrent = current.source.origin == PlaybackWindowOrigin::Capture &&
          current.source.captureContentGeneration != 0 &&
          current.source.captureContentGeneration == frozen.captureContentToken &&
          runtime.frozenCaptureAdoptedStorage != 0 &&
          static_cast<uint16_t>(current.storageGeneration) == runtime.frozenCaptureAdoptedStorage;
      if (committedCurrent || captureCurrent) adopted = &current;
    }
  }
  // Each copied interval has its own order indices. Keep the crossing floor,
  // but never carry the exhausted previous window's cursor into its successor.
  if (adopted != nullptr && set->lifecycle.current != previousCurrent) loop.nextEventIndex = 0;
  return adopted;
}

// FLASHMEM: the admission check is a cold predicate on the MIDI edge (it is called
// once per turn, alongside the FLASH-resident session-exit gap predicate), and RAM1 is
// at its bank boundary.
TRACK_COLD_MEM bool Track::hasAudiblePlaybackSource(uint8_t slotIndex) const {
  if (slotIndex >= Config::MAX_LOOPS_PER_TRACK || !loopsAllocated()) {
    return false;
  }
  const Loop& loop = getLoop(slotIndex);
#if PLAYBACK_WINDOW_ROLLING_ENABLED
  if (slotIndex == activeLoopIndex && isRecording() && !loop.hasCommittedPasses()) {
    const auto* runtime = playbackRuntime.slotIfAllocated(slotIndex);
    const auto* set = runtime != nullptr ? runtime->viewSetIfPrewarmed() : nullptr;
    if (set && set->lifecycle.current < set->views.size()) {
      const auto& recipe = runtime->pendingViewRequest_;
      auto projection = loop.liveCaptureProjectionIdentity();
      if (projection.phase == CapturePhase::Record)
        projection.loopLengthTicks = recipe.liveCaptureProjection.loopLengthTicks;
      if (recipe.sourceKind != PlaybackViewPendingRequest::SourceKind::LiveCapture ||
          recipe.operationGeneration != playbackGeneration ||
          recipe.liveLeaseGeneration != runtime->workingSetGeneration() ||
          recipe.liveProjectionStart != getProjectionCycleStartTick() ||
          !(recipe.liveCaptureProjection == projection)) return false;
      const auto& current = set->views[set->lifecycle.current];
      return current.complete && current.published && !current.rawCoveragePrepared &&
          !current.firstRecordReturnWindow && loop.capture.phase == CapturePhase::Record &&
          current.source.origin == PlaybackWindowOrigin::Capture &&
          current.source.sourceGeneration == playbackGeneration &&
          current.source.captureContentGeneration != 0 && current.source.snapshotIdentity == nullptr &&
          current.storageGeneration != 0 && current.coverage.isValid() &&
          current.coverage.startPhase % Config::TICKS_PER_BAR == 0 &&
          current.coverage.lengthTicks == Config::TICKS_PER_BAR &&
          current.coverage.loopLengthTicks % Config::TICKS_PER_BAR == 0;
    }
  }
#endif
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
  if (!isAudiblePlaybackActive()
#if PLAYBACK_WINDOW_ROLLING_ENABLED
      && !(isRecording() && hasAudiblePlaybackSource(playbackSlotIndex))
#endif
      ) {
    return false;
  }
  if (playbackSlotIndex >= Config::MAX_LOOPS_PER_TRACK) {
    return false;
  }
  const uint8_t channel = remappedPlaybackChannel(evt, midiChannel);
  LoopPlaybackRuntime* runtime = playbackRuntime.slotIfAllocated(playbackSlotIndex);
#if PLAYBACK_WINDOW_ROLLING_ENABLED
  // A growing Record period can revisit an On whose matching Off now belongs
  // to the next interval. Preserve that sounding identity, not a second On.
  if (runtime && isRecording() && evt.isNoteOn() &&
      runtime->ledger.isActive(channel, evt.data.noteData.note, evt.noteId)) return false;
#endif
  return runtime != nullptr && runtime->ledger.applyPlaybackEvent(channel, evt);
}

void Track::sendMidiEvent(const MidiEvent& evt, uint8_t playbackSlotIndex) {
  if (!isAudiblePlaybackActive()
#if PLAYBACK_WINDOW_ROLLING_ENABLED
      && !(isRecording() && hasAudiblePlaybackSource(playbackSlotIndex))
#endif
      ) return;
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
  // DEC-233 decision 4: a retained, input-closed Stop still owes its held notes to its own job, so a
  // transport-stop CC sweep must not erase that capture state. The ordinary paths keep the old clear
  // (an admitted stop already moved its held notes into the job).
  const PendingInteractionLatch* latch = interactionLatch();
  const bool retainedClosedStop =
      latch != nullptr && latch->kind == PendingInteractionLatch::Kind::Stop &&
      hasRetainedInputClosedStop(latch->slot);
  if (!retainedClosedStop) pendingNotes.clear();
  logger.logTrackEvent("All Notes Off sent", clockManager.getCurrentTick());
}


/// Task 3.15e: the owed tail of a whole-loop paged delivery, drained with the old cycle's tail frame. Defined in this
/// TU on purpose: `advancePlaybackCursor` lives in ITCM, so an intra-region caller needs no flash->ITCM veneer (the
/// image built with the drain in flash did not boot; see the boot matrix in PROJECT_STATE).
void Track::servicePagedOwedTailTurn(uint8_t slotIndex, uint32_t currentTick, uint32_t prevTickInLoop) {
  LoopPlaybackRuntime* runtime = playbackRuntime.slotIfAllocated(slotIndex);
  if (runtime == nullptr || !runtime->pagedEmission.bound || !runtime->pagedEmission.passRebasePending) return;
  Loop& loop = loopForSlot(slotIndex);
  if (runtime->pagedInterval.lengthTicks < loop.loopLengthTicks) return;
  const ProjectionContext playbackContext = makePlaybackContext(*this, loop, currentTick);
  PagedEmissionStreamCtx ctx{this, runtime, slotIndex};
  const PlaybackEventStream stream = makePagedEmissionStream(ctx);
  const PlaybackTickFrame tail{&playbackContext, loop.loopLengthTicks - 1, prevTickInLoop, false};
  const PlaybackEmitPolicy policy =
      slotIndex == getActiveLoopIndex() ? PlaybackEmitPolicy::ActiveCommitted : PlaybackEmitPolicy::LayeredSlot;
  uint16_t cursor = 0;
  (void)advancePlaybackCursor({&cursor, nullptr}, tail, policy, stream, playbackCursorAdvanceSend, this, slotIndex,
                              nullptr, nullptr, getMidiChannel(), playbackCursorAdvanceApplyLedger);
  runtime->pagedEmission.passBaseOrdinal = runtime->pagedEmission.consumedOrdinal;
  runtime->pagedEmission.passRebasePending = false;
}
