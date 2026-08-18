//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "TrackInternal.h"

#include "ClockManager.h"
#include "EditManager.h"
#include "Globals.h"
#include "Logger.h"
#include "LoopEditManager.h"
#include "MidiConfig.h"
#include "MidiHandler.h"
#include "TickPhase.h"
#include "Utils/IntervalProjection.h"
#include "Utils/PlaybackCursorAdvance.h"
#include "Utils/RuntimeTimingTelemetry.h"

void Track::reanchorPlaybackProjection(uint32_t currentTick, bool preserveLoopPhaseOrigin) {
  Loop& loop = getActiveLoop();
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
  LoopPlaybackRuntime& runtime = playbackRuntime.slot(activeLoopIndex);
  const uint32_t currentTick = clockManager.getCurrentTick();
  ensurePlaybackMergedMidiEventsBuilt(*this, loop, runtime, true, currentTick);
  const ProjectionContext playbackContext = makePlaybackContext(*this, loop, currentTick);
  ::rebuildPlaybackOrder(loop, runtime.mergedMidiEvents.mergedEvents, playbackContext);
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

bool playbackCursorAdvanceJamFilter(void* ctx, uint32_t storageTick) {
  const auto* jam = static_cast<const PlaybackJamFilterCtx*>(ctx);
  return jam->track->isStorageTickInJamRegion(storageTick, *jam->loop);
}

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
  LoopPlaybackRuntime& runtime = playbackRuntime.slot(slotIndex);

  if (runtime.isStale(loop.playbackRevision, playbackGeneration)) {
    runtime.reset(true);
    loop.nextEventIndex = 0;
    if (isActive) {
      loop.captureNextEventIndex = 0;
    }
    runtime.syncRevision(loop.playbackRevision, playbackGeneration);
  }

  ensurePlaybackMergedMidiEventsBuilt(*this, loop, runtime, false, currentTick);
  const SessionMidiEventVec& mergedEvents = runtime.mergedMidiEvents.mergedEvents;
  if (mergedEvents.empty()) {
    return;
  }

  ProjectionContext playbackContext = makePlaybackContext(*this, loop, currentTick);
  if (loop.playbackOrderDirty) {
    ::rebuildPlaybackOrder(loop, mergedEvents, playbackContext);
    reanchorPlaybackIndex(loop, mergedEvents, loop.getPlaybackOrder(), playbackContext);
  }

  const uint32_t tickInLoop = IntervalProjection::tickPhaseInProjectionCycle(
      currentTick, projectionCycleStartTick, loop.loopLengthTicks);

  if (IntervalProjection::didDisplayPlayheadWrapBackward(tickInLoop, loop.lastTickInLoop,
                                                       loop.loopStartTick, loop.loopLengthTicks)) {
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
  if (isActive && trackState == TRACK_OVERDUBBING) {
    if (maybeCommitOverdubWrap(prevTickInLoop, tickInLoop)) {
      // Wrap committed a pass and bumped playbackRevision. Rebuild the merged stream
      // and reanchor while lastTickInLoop is still prev so (prev, S] can apply the
      // wrap-committed NoteOn at S before USB occupy. Do not reanchor after lastTick
      // is already S — that skips evPhase <= S.
      ensurePlaybackMergedMidiEventsBuilt(*this, loop, runtime, false, currentTick);
      playbackContext = makePlaybackContext(*this, loop, currentTick);
      if (!runtime.mergedMidiEvents.mergedEvents.empty()) {
        ::rebuildPlaybackOrder(loop, runtime.mergedMidiEvents.mergedEvents, playbackContext);
        reanchorPlaybackIndex(loop, runtime.mergedMidiEvents.mergedEvents, loop.getPlaybackOrder(),
                              playbackContext);
      }
    }
  }
  loop.lastTickInLoop = tickInLoop;
  const bool atLoopStart =
      IntervalProjection::isPlaybackCatchUpWindow(prevTickInLoop, tickInLoop);

  if (prevTickInLoop == UINT32_MAX && tickInLoop > Config::TICKS_PER_BAR) {
    loop.nextEventIndex = 0;
    runtime.syncRevision(loop.playbackRevision, playbackGeneration);
    return;
  }

  PlaybackTickFrame frame{&playbackContext, tickInLoop, prevTickInLoop, atLoopStart};
  MergedPlaybackStreamCtx mergedCtx{&loop.getPlaybackOrder(), &mergedEvents};
  PlaybackCursorAdvanceState mergedAdvance{&loop.nextEventIndex, &loop.playbackOrderDirty};
  const PlaybackEmitPolicy mergedPolicy =
      isActive ? PlaybackEmitPolicy::ActiveCommitted : PlaybackEmitPolicy::LayeredSlot;
  PlaybackJamFilterCtx jamCtx{this, &loop};
  (void)advancePlaybackCursor(
      mergedAdvance, frame, mergedPolicy, makeMergedPlaybackStream(mergedCtx),
      playbackCursorAdvanceSend, this, slotIndex,
      isActive ? playbackCursorAdvanceJamFilter : nullptr, isActive ? &jamCtx : nullptr, midiChannel);

  if (isActive && loop.capture.phase == CapturePhase::Overdub && !loop.capture.store.empty()) {
    if (loop.ensureCaptureEventsSorted()) {
      reanchorCaptureIndex(loop);
    }
    PlaybackCursorAdvanceState captureAdvance{&loop.captureNextEventIndex, nullptr};
    (void)advancePlaybackCursor(
        captureAdvance, frame, PlaybackEmitPolicy::ActiveCaptureOverdub,
        makeCapturePlaybackStream(loop), playbackCursorAdvanceSend, this, slotIndex,
        playbackCursorAdvanceJamFilter, &jamCtx, midiChannel);
  }

  runtime.syncRevision(loop.playbackRevision, playbackGeneration);
}

void Track::playMidiEvents(uint32_t currentTick, bool emitMidiOutput) {
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
  if (trackState != TRACK_PLAYING && trackState != TRACK_OVERDUBBING) {
    return false;
  }
  if (playbackSlotIndex >= Config::MAX_LOOPS_PER_TRACK) {
    return false;
  }
  const uint8_t channel = remappedPlaybackChannel(evt, midiChannel);
  return playbackRuntime.slot(playbackSlotIndex).ledger.applyPlaybackEvent(channel, evt);
}

void Track::sendMidiEvent(const MidiEvent& evt, uint8_t playbackSlotIndex) {
  if (trackState != TRACK_PLAYING && trackState != TRACK_OVERDUBBING) return;
  if (playbackSlotIndex >= Config::MAX_LOOPS_PER_TRACK) return;
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
  if (playbackEmitMidiOutput_) {
    midiHandler.sendMidiEvent(evtCopy);
    if (evt.isNoteOn() || evt.isNoteOff()) {
      RuntimeTimingTelemetry::recordNoteSendLateness(evt.isNoteOn(), micros(), evt.tick);
    }
  }
  ignorePlaybackMidiInput = false;  // Reset playback state
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
  playbackRuntime.slot(slotIndex).ledger.forEachActive(
      [](uint8_t channel, uint8_t note, const ActiveNoteLedger::Entry&) {
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
