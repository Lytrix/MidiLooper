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
    reconcilePlaybackLedgerAfterFullLoopRebuild(loop, *runtime);
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
    PlaybackTickFrame& frame, ProjectionContext& playbackContext, MergedPlaybackStreamCtx& mergedCtx,
    PlaybackCursorAdvanceState& mergedAdvance, PlaybackEmitPolicy mergedPolicy,
    PlaybackJamFilterCtx& jamCtx, uint8_t midiChannel) {
  (void)runtime;
  (void)currentTick;
  (void)playbackContext;
  loop.armOverdubWrapAfterLeavingStart(frame.tickInLoop);
  bool mergedIntervalApplied = false;
  if (loop.shouldCommitOverdubWrap(frame.prevTickInLoop, frame.tickInLoop)) {
    (void)advancePlaybackCursorAcrossOverdubWrap(
        mergedAdvance, frame, mergedPolicy, makeMergedPlaybackStream(mergedCtx),
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
  const PlaybackViewSourceIdentity expectedFixedSource{
      loop.playbackRevision, playbackGeneration, loop.publishedSnapshotIdentity()};
  const FixedPlaybackBridgeStorage* fixedBridge =
      fixedViewEligible
          ? runtime.selectedFullLoopBridge(expectedFixedSource, loop.loopLengthTicks)
          : nullptr;
  const RuntimePlaybackView* fixedView =
      fixedBridge != nullptr ? runtime.fullLoopBridgeBase(*fixedBridge, loop.loopLengthTicks)
          : fixedViewEligible ? runtime.publishedFullLoopView(expectedFixedSource, loop.loopLengthTicks)
                              : nullptr;

  if (runtime.isStale(loop.playbackRevision, playbackGeneration)) {
    // A fixed view already proved its exact generation and source. Do not
    // erase it merely because the legacy runtime cache has not been touched
    // since STOPPED prewarm.
    if (runtime.cachedTrackGeneration != playbackGeneration && fixedView == nullptr) {
      if (!runtime.reset(true)) return;
    }
    loop.nextEventIndex = 0;
    if (isActive) {
      loop.captureNextEventIndex = 0;
    }
    runtime.syncRevision(loop.playbackRevision, playbackGeneration);
  }

#if RUNTIME_TIMING_ENABLED
  const uint32_t rebuildStartUs = micros();
#endif
  bool rebuilt = false;
  if (fixedView == nullptr) {
    rebuilt = ensurePlaybackMergedMidiEventsBuilt(*this, loop, runtime, false, currentTick);
    if (rebuilt) {
      reconcilePlaybackLedgerAfterFullLoopRebuild(loop, runtime);
    }
  }
#if RUNTIME_TIMING_ENABLED
  RUNTIME_TIMING_ADD_TRACK_PATH_REBUILD(micros() - rebuildStartUs);
#endif
  const SessionMidiEventVec* mergedEvents =
      fixedView == nullptr ? &runtime.mergedMidiEvents.mergedEvents : nullptr;
  const size_t eventCount = fixedBridge != nullptr
                               ? fixedBridge->baseEventCount + fixedBridge->insertions.eventCount
                               : (fixedView != nullptr ? fixedView->storage.eventCount : mergedEvents->size());
  if (eventCount == 0 && fixedBridge == nullptr) {
    return;
  }

#if RUNTIME_TIMING_ENABLED
  const uint32_t orderSendBeforeUs = RUNTIME_TIMING_TRACK_PATH_SEND_US();
  const uint32_t orderStartUs = micros();
#endif
  ProjectionContext playbackContext = makePlaybackContext(*this, loop, currentTick);
  if (fixedView == nullptr && loop.playbackOrderDirty) {
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

  if (IntervalProjection::didDisplayPlayheadWrapBackward(tickInLoop, loop.lastTickInLoop,
                                                       loop.loopStartTick, loop.loopLengthTicks)) {
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

  if (prevTickInLoop == UINT32_MAX && tickInLoop > Config::TICKS_PER_BAR) {
    loop.nextEventIndex = 0;
    runtime.syncRevision(loop.playbackRevision, playbackGeneration);
#if RUNTIME_TIMING_ENABLED
    RUNTIME_TIMING_ADD_TRACK_PATH_ORDER(RUNTIME_TIMING_TRACK_PATH_EXCLUSIVE(
        micros() - orderStartUs, orderSendBeforeUs));
#endif
    return;
  }

  PlaybackTickFrame frame{&playbackContext, tickInLoop, prevTickInLoop, atLoopStart};
  MergedPlaybackStreamCtx mergedCtx{};
  if (fixedView == nullptr) {
    mergedCtx = MergedPlaybackStreamCtx{&loop.getPlaybackOrder(), mergedEvents};
  }
  const auto* emissionWindow = fixedBridge != nullptr
      ? runtime.publishedBridgeWindow(expectedFixedSource, tickInLoop) : nullptr;
  if (emissionWindow) {
    // The compiled bridge and the window contain the same cycle stream. A
    // logarithmic seek excludes the already serviced interval on a handoff.
    const auto& storage = emissionWindow->storage;
    size_t first = 0, end = storage.eventCount;
    if (prevTickInLoop != UINT32_MAX && prevTickInLoop <= tickInLoop) {
      while (first < end) {
        const size_t middle = first + (end - first) / 2;
        if (storage.phases[storage.order[middle]] <= prevTickInLoop) first = middle + 1;
        else end = middle;
      }
    }
    loop.nextEventIndex = first;
  }
  FixedPlaybackStreamCtx fixedCtx{emissionWindow ? &emissionWindow->storage
      : fixedView != nullptr ? &fixedView->storage : nullptr};
  const PlaybackEventStream committedStream =
      emissionWindow ? makeFixedPlaybackStream(fixedCtx)
      : bridgeStream != nullptr ? bridgeStream->stream()
                              : (fixedView != nullptr ? makeFixedPlaybackStream(fixedCtx)
                                                      : makeMergedPlaybackStream(mergedCtx));
  PlaybackCursorAdvanceState mergedAdvance{
      !emissionWindow && bridgeStream != nullptr ? &bridgeStream->playbackIndex : &loop.nextEventIndex,
      fixedView != nullptr ? nullptr : &loop.playbackOrderDirty};

  // Wrap-tick ledger catch-up lives in FLASHMEM. Occupy reads Entry.noteId;
  // this is not a special occupy path.
  bool mergedIntervalApplied = false;
  if (isActive && trackState == TRACK_OVERDUBBING) {
    mergedIntervalApplied = catchUpOverdubWrapPlaybackLedger(
        *this, loop, runtime, slotIndex, currentTick, frame, playbackContext, mergedCtx,
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
  Loop& loop = getActiveLoop();
  if (!loop.hasCommittedPasses() || loop.loopLengthTicks == 0) {
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
  Loop& loop = getLoop(slotIndex);
  if (!loop.hasCommittedPasses() || loop.loopLengthTicks == 0) {
    return;
  }
  playbackEmitMidiOutput_ = emitMidiOutput && !muted;
  playCommittedLoopMidi(slotIndex, currentTick, PlaybackMidiTarget::LayeredSlot);
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
  if (evt.isNoteOn() || evt.isNoteOff()) {
    const char* noteNames[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    int octave = (evtCopy.data.noteData.note / 12) - 1;
    const char* noteName = noteNames[evtCopy.data.noteData.note % 12];
    logger.log(CAT_MIDI, LOG_TRACE, "Loop SEND: %s ch=%d %s%d (note %d) vel=%d tick=%lu",
               evt.isNoteOn() ? "NoteOn" : "NoteOff",
               evtCopy.channel, noteName, octave,
               evtCopy.data.noteData.note, evtCopy.data.noteData.velocity, evt.tick);
  }
  // A Stop can arrive while an internal-clock interval is already walking its
  // due cursor. Recheck the volatile barrier directly before the physical send.
  if (playbackEmitMidiOutput_ && !transportStopOutputBarrier_) {
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
