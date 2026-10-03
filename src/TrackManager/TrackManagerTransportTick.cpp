//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "TrackManager.h"

#include "ClockManager.h"
#include "DisplayManager.h"
#include "Logger.h"
#include "LooperState.h"
#include "StorageManager.h"
#include "Utils/EdgeEffects.h"
#include "Utils/PlaybackMidiOutput.h"
#include "Utils/RuntimeTimingTelemetry.h"

namespace {

/// The edge's deferred-effect queue (timing-path review, item 1). `DMAMEM` because it is mutable state and this
/// tree's rule is that mutable state never grows DTCM (`scripts/ram1_budget.json`'s variables line is checked
/// byte-for-byte; `PersistenceWorkItemPersist.cpp` records the same reason). Zero-initialised, which is exactly
/// the queue's empty state, so nothing has to run at boot to arm it.
#if defined(__IMXRT1062__)
#define EDGE_EFFECT_QUEUE_MEM DMAMEM
#else
#define EDGE_EFFECT_QUEUE_MEM
#endif
EDGE_EFFECT_QUEUE_MEM EdgeEffectQueue gEdgeEffectQueue;

/// Effects the edge wanted to record and could not. Reported by the drain once per growth, so the loss is
/// bounded and **named** rather than silent (`DMAMEM` for the same reason as the queue).
EDGE_EFFECT_QUEUE_MEM uint32_t gReportedDroppedEdgeEffects = 0;

}  // namespace

void TrackManager::startPlayingTrack(uint8_t trackIndex) {
  if (trackIndex < Config::NUM_TRACKS) {
    StorageManager::prioritizeLoopSlotRestoreForFocus(trackIndex,
                                                      tracks[trackIndex].getActiveLoopIndex());
    tracks[trackIndex].startPlaying(clockManager.getCurrentTick());
  }
}

void TrackManager::stopPlayingTrack(uint8_t trackIndex) {
  if (trackIndex >= Config::NUM_TRACKS) {
    return;
  }
  tracks[trackIndex].stopPlaying();
  cancelPendingSlotSwitch(trackIndex);
  tracks[trackIndex].clearQueuedPlaybackStart();
}

void TrackManager::handleTransportStop() {
  uint32_t currentTick = clockManager.getCurrentTick();
  // Publish the output barrier for every track before CC123 fan-out. Sending
  // all-notes-off to every channel can take several milliseconds; an internal
  // clock ISR must not emit a due playback event during that interval.
  for (uint8_t i = 0; i < Config::NUM_TRACKS; ++i) {
    tracks[i].suppressMidiOutputForTransportStop();
  }
  for (uint8_t i = 0; i < Config::NUM_TRACKS; ++i) {
    Track& t = tracks[i];
    // Always clear queued quantized actions when transport stops.
    pendingRecord[i] = false;
    pendingRecordQueuedAtTick[i] = UINT32_MAX;
    pendingRecordRefSlot[i] = Config::INVALID_LOOP_SLOT;
    for (uint8_t s = 0; s < Config::MAX_LOOPS_PER_TRACK; ++s) {
      pendingRecordSlot[i][s] = false;
      heldLayerSlot[i][s] = false;
    }
    pendingStop[i] = false;
    cancelPendingSlotSwitch(i);
    tracks[i].clearQueuedPlaybackStart();
    if (t.isRecording()) {
      // Stop recording BEFORE sendAllNotesOff(): finalizePendingNotes() must record
      // note-offs for still-held notes, but sendAllNotesOff() clears pendingNotes,
      // which left the last note-on orphaned so validation removed it.
      // S4f.21: capture the interval's slot before the stop moves state, then adopt the
      // **job-qualified** geometry. The transport stop is the same continuation as
      // `stopRecordingTrack`, so it holds the same obligation instead of latching the stop edge's
      // predicted length (an all-filtered take publishes zero).
      const uint8_t captureSlot = t.getActiveLoopIndex();
      t.stopRecordingToStopped(currentTick);
      t.sendAllNotesOff();
      uint32_t recordedLength = 0;
      if (t.recordStopGeometryForSlot(captureSlot, recordedLength) ==
          Track::RecordStopGeometryState::Final) {
        applyRecordStopGeometry(i, captureSlot, recordedLength);
      } else {
        retainRecordStopGeometryObligation(i, captureSlot);
      }
    } else if (t.isOverdubbing() || t.hasPendingDeferredOverdubStop()) {
      // Same order as record: finalize held notes before sendAllNotesOff()
      // clears pendingNotes. [`122152`] wrap_synth Off@168 was an orphaned On.
      t.stopOverdubbingToStopped();
      t.sendAllNotesOff();
    } else if (t.isPlaying()) {
      t.stopPlaying();  // sends All Notes Off internally
    } else if (t.isArmed()) {
      t.sendAllNotesOff();
      if (t.hasAnySlotData()) {
        t.setState(TRACK_STOPPED);
      } else {
        t.setState(TRACK_EMPTY);
      }
    } else {
      t.sendAllNotesOff();
    }
  }
  forceMidiLedUpdate(currentTick);
  if (StorageManager::shouldQueueCurrentWorkspaceSave()) {
    StorageManager::admitGlobalMeta();
  }
  StorageManager::requestDeferredSaveState(looperState.getLooperState());
}

void TrackManager::queueBarPlaybackStart(uint8_t trackIndex, int32_t storageTick,
                                         uint32_t queuedAtTick) {
  if (trackIndex >= Config::NUM_TRACKS) {
    return;
  }
  cancelPendingSlotSwitch(trackIndex);
  tracks[trackIndex].queuePlaybackStartAtGrid(storageTick, queuedAtTick);
}

void TrackManager::advanceJamTicks(uint32_t delta) {
  for (uint8_t i = 0; i < Config::NUM_TRACKS; i++) {
    tracks[i].advanceJamTick(delta);
  }
}

void TrackManager::updateAllTracks(uint32_t currentTick) {
#if RUNTIME_TIMING_ENABLED
  const uint32_t tracksStartUs = micros();
#endif
  handlePendingRecordStart(currentTick);
  // S4f.21: adopt any Record-stop geometry whose interval has since been published or superseded.
  // The obligation is created at the stop edge, where the geometry was still a prediction.
  servicePendingRecordStopGeometry();

  for (uint8_t i = 0; i < Config::NUM_TRACKS; i++) {
#if RUNTIME_TIMING_ENABLED
    RUNTIME_TIMING_BEGIN_TRACK_PATH_NESTED();
    const uint32_t trackStartUs = micros();
    const uint32_t slotSendBeforeUs = RUNTIME_TIMING_TRACK_PATH_SEND_US();
    const uint32_t slotStartUs = micros();
#endif
    // Commit multi-hold selection (enabled set + start at next 16th).
    if (pendingMultiSlotCommit[i] &&
        currentTick != pendingMultiSlotQueuedAtTick[i] &&
        (currentTick % Config::TICKS_PER_16TH_STEP) == 0) {
      if (tracks[i].isRecording() || tracks[i].isOverdubbing() ||
          tracks[i].hasPendingDeferredOverdubStop() || pendingRecord[i]) {
        pendingMultiSlotCommit[i] = false;
        pendingMultiSlotQueuedAtTick[i] = UINT32_MAX;
        for (uint8_t s = 0; s < Config::MAX_LOOPS_PER_TRACK; ++s) {
          pendingSlotEnabled[i][s] = false;
        }
#if RUNTIME_TIMING_ENABLED
        RUNTIME_TIMING_ADD_TRACK_PATH_SLOT(RUNTIME_TIMING_TRACK_PATH_EXCLUSIVE(
            micros() - slotStartUs, slotSendBeforeUs));
        const uint32_t trackDurationUs = micros() - trackStartUs;
        RUNTIME_TIMING_NOTE_TRACK_UPDATE(i, trackDurationUs);
        RUNTIME_TIMING_COMMIT_TRACK_PATH_NESTED(trackDurationUs);
#endif
        continue;
      }

      // Apply pendingSlotEnabled -> slotEnabled and default unmute.
      bool anyEnabledUnmutedWithData = false;
      for (uint8_t s = 0; s < Config::MAX_LOOPS_PER_TRACK; ++s) {
        slotEnabled[i][s] = pendingSlotEnabled[i][s];
        // Keep mute if the slot stays enabled, but default to unmuted for new selection.
        slotMuted[i][s] = false;
        if (slotEnabled[i][s] && !slotMuted[i][s] && tracks[i].hasDataInSlot(s)) {
          anyEnabledUnmutedWithData = true;
        }
      }
      pendingMultiSlotCommit[i] = false;
      pendingMultiSlotQueuedAtTick[i] = UINT32_MAX;
      for (uint8_t s = 0; s < Config::MAX_LOOPS_PER_TRACK; ++s) pendingSlotEnabled[i][s] = false;

      // Choose active slot: prefer selected slot if it is enabled and has data.
      uint8_t selectedSlot = slotStateMachine.getSelectedSlotIndex(i);
      if (!slotEnabled[i][selectedSlot] || !tracks[i].hasDataInSlot(selectedSlot)) {
        selectedSlot = 0;
        for (uint8_t s = 0; s < Config::MAX_LOOPS_PER_TRACK; ++s) {
          if (slotEnabled[i][s] && tracks[i].hasDataInSlot(s)) {
            selectedSlot = s;
            break;
          }
        }
      }
      slotStateMachine.setSelectedSlotIndex(i, selectedSlot);
      // S4b.5: the stop path has already decided; it lands the write at the non-re-entrant commit point rather
      // than through the guarded entry, which would divert a stop-path write into a capture switch.
      applyActiveLoopIndex(i, selectedSlot);
      // Reset playback indices so all newly enabled slots start at the commit phase.
      tracks[i].resetPlaybackStateForSlot(selectedSlot, currentTick);
      for (uint8_t s = 0; s < Config::MAX_LOOPS_PER_TRACK; ++s) {
        if (slotEnabled[i][s] && !slotMuted[i][s] && tracks[i].hasDataInSlot(s)) {
          tracks[i].resetPlaybackStateForSlot(s, currentTick);
        }
      }

      // Ensure playback is running if we have any audible slot data.
      if (anyEnabledUnmutedWithData && !tracks[i].isAudiblePlaybackActive()) {
        // Track state machine may block EMPTY->PLAYING; we keep this guard minimal.
        startPlayingTrack(i);
      }

      // Timing-path review, item 1: the LED refresh is deferred rather than done on the tick edge (see
      // `Utils/EdgeEffects.h`). The commit itself -- the enabled set, the active slot, the resets -- is exactly
      // the state work that must stay here. No row for this kind: the edge never printed one.
      recordEdgeEffect(EdgeEffectKind::MultiSlotCommit, i, 0, 0, 0, 0, kEdgeEffectLedRefresh);
    }

    if (tracks[i].isRecording() && !tracks[i].isPlaying()) {
      tracks[i].updateGrowingRecordLoopLength(currentTick);
    }

    const bool audible = isTrackAudible(i);
    const uint32_t playTick = tracks[i].getEffectivePlaybackTick(currentTick);

    // Commit before playMidiEvents so lastTickInLoop still reflects the previous tick.
    if (tracks[i].isPlaying() && slotStateMachine.hasPendingSlotSwitch(i) &&
        slotStateMachine.shouldCommitPendingSlotSwitch(i, tracks[i], playTick)) {
      const uint8_t targetSlot = slotStateMachine.getPendingSlotIndex(i);
      if (targetSlot >= Config::MAX_LOOPS_PER_TRACK) {
        cancelPendingSlotSwitch(i);
      } else if (!tracks[i].hasCommittedPassesInSlot(targetSlot)) {
        // HEADER_READY / SD still loading: keep pending until Commit.
        // hasDataInSlot is true from metadata length alone — launching that empty 64-bar
        // slot then letting LoadLoopJob adopt into the *active* playing loop hard-faults
        // on fast track→slot3 (session_20260718_205809).
        if (!StorageManager::hasLoopSlotPayloadOnSdInRam(i, targetSlot)) {
          recordEdgeEffect(EdgeEffectKind::SlotSwitchCancelledNoCommittedMidi, i, 0, targetSlot, 0, 0,
                           kEdgeEffectLog);
          cancelPendingSlotSwitch(i);
        }
      } else if (!tracks[i].isPlaybackMergedMidiEventsReadyForSlot(targetSlot) &&
                 tracks[i].getLoop(targetSlot).shouldAvoidFullVisualRebuild(
                     tracks[i].getLoop(targetSlot).loopLengthTicks)) {
        // Long loop just Committed: do not full-gather on the clock path (210001).
        // Main prewarms after LoadLoopJob Commit; keep pending until window is ready.
      } else if (!tracks[i].hasPlaybackOccupancyForSlot(targetSlot)) {
        // S4b.6 (a) / S4b.7: the inbound **occupancy** is absent, so the switch stays **pending** rather
        // than committing into a slot whose runtime does not exist -- `ensurePlaybackMergedEventsForSlot`
        // deliberately cannot take it here, and `slotIfAllocated` alone would leave a silent dead switch.
        // `requestSlotSwitch` already marked the request for the deferred provisioning owner
        // (`provisionSlotForSwitch`), so the next eligible lane turn takes the occupancy off-clock and the
        // commit lands.
      } else if (tracks[i].hasDataInSlot(targetSlot)) {
        const uint8_t previousPlaying = getPlayingSlotIndex(i);
        clearPendingSlotSwitch(i);
        // Wire silence before swapping the audible slot (same pattern as overdub→play).
        tracks[i].sendAllNotesOff();
        tracks[i].ensurePlaybackMergedEventsForSlot(targetSlot);
        // S4b.5: the commit is the actor here (the pending switch was just consumed), so it lands the write at
        // the non-re-entrant commit point: the guarded entry could otherwise divert it into a capture switch.
        applyActiveLoopIndex(i, targetSlot);
        const Loop& targetLoop = tracks[i].getLoop(targetSlot);
        tracks[i].clearQueuedPlaybackStart();
        tracks[i].queuePlaybackStartAtGrid(static_cast<int32_t>(targetLoop.loopStartTick),
                                            currentTick);
        tracks[i].commitQueuedPlaybackStart(currentTick);
        // Timing-path review, item 1: the commit row, the panel's invalidation and the LED refresh become one
        // record the main loop drains. The row's text, its field order and the pair the panel is told about
        // (`previousPlaying` -> the preview slot) are unchanged -- only where they run is.
        uint8_t commitWork = kEdgeEffectLog;
        if (i == selectedTrack) {
          commitWork |= kEdgeEffectLedRefresh;
          if (targetSlot != previousPlaying) {
            commitWork |= kEdgeEffectDisplayInvalidate;
          }
        }
        recordEdgeEffect(EdgeEffectKind::LoopEndCommit, i, previousPlaying, targetSlot,
                         targetLoop.loopStartTick, targetLoop.loopLengthTicks, commitWork);

        // If this slot switch came from a "select single slot" gesture, replace enabled set.
        if (pendingEnabledSetReplacement[i]) {
          for (uint8_t s = 0; s < Config::MAX_LOOPS_PER_TRACK; ++s) {
            slotEnabled[i][s] = (s == targetSlot);
            slotMuted[i][s] = false;
          }
          pendingEnabledSetReplacement[i] = false;
          // The replacement's own LED refresh, same deferral (and no row: it had none).
          recordEdgeEffect(EdgeEffectKind::EnabledSetReplacement, i, 0, targetSlot, 0, 0,
                           kEdgeEffectLedRefresh);
        }
      } else {
        // Safety: pending target no longer has loop data, cancel it.
        recordEdgeEffect(EdgeEffectKind::SlotSwitchCancelledNoRamData, i, 0, targetSlot, 0, 0,
                         kEdgeEffectLog);
        cancelPendingSlotSwitch(i);
      }
    }

    // Bar-queued playback start (no pending slot switch).
    if (tracks[i].isPlaying() && !slotStateMachine.hasPendingSlotSwitch(i) &&
        tracks[i].shouldCommitQueuedPlaybackStart(currentTick)) {
      tracks[i].commitQueuedPlaybackStart(currentTick);
    }

    if (pendingStop[i]) {
      stopRecordingTrack(i);
      pendingStop[i] = false;
    }

    const uint8_t activeSlot = tracks[i].getActiveLoopIndex();

#if RUNTIME_TIMING_ENABLED
    RUNTIME_TIMING_ADD_TRACK_PATH_SLOT(RUNTIME_TIMING_TRACK_PATH_EXCLUSIVE(
        micros() - slotStartUs, slotSendBeforeUs));
#endif

    // Primary (active) slot playback. Mute/solo/slot-mute suppress MIDI send, not the engine.
    if (PlaybackMidiOutput::engineShouldRun(slotEnabled[i][activeSlot])) {
      tracks[i].playMidiEvents(
          playTick, PlaybackMidiOutput::shouldSend(audible, slotMuted[i][activeSlot]));
    }

    // Additional enabled slots.
    for (uint8_t s = 0; s < Config::MAX_LOOPS_PER_TRACK; ++s) {
      if (s == activeSlot) continue;
      if (PlaybackMidiOutput::engineShouldRun(slotEnabled[i][s])) {
        tracks[i].playMidiEventsForSlot(
            s, playTick, PlaybackMidiOutput::shouldSend(audible, slotMuted[i][s]));
      }
    }
#if RUNTIME_TIMING_ENABLED
    const uint32_t trackDurationUs = micros() - trackStartUs;
    RUNTIME_TIMING_NOTE_TRACK_UPDATE(i, trackDurationUs);
    RUNTIME_TIMING_COMMIT_TRACK_PATH_NESTED(trackDurationUs);
#endif
  }
#if RUNTIME_TIMING_ENABLED
  RUNTIME_TIMING_NOTE_TRACKS_UPDATE(micros() - tracksStartUs);
#endif
}

/// The edge's recording half (timing-path review, item 1). Deliberately **not** inlined into `updateAllTracks`:
/// six call sites in the ISR path each paying for a 16-byte record build would be code the tick edge does not
/// need, and the record itself is a handful of stores plus the queue's masked publish. The return value is
/// intentionally dropped -- a full queue is counted by the queue and named by the drain, and the edge has
/// nothing better to do about it than the drain does.
__attribute__((noinline)) void TrackManager::recordEdgeEffect(EdgeEffectKind kind, uint8_t track,
                                                              uint8_t fromSlot, uint8_t toSlot,
                                                              uint32_t loopStartTick,
                                                              uint32_t loopLengthTicks, uint8_t work) {
  EdgeEffectRecord effect;
  effect.kind = static_cast<uint8_t>(kind);
  effect.work = work;
  effect.track = track;
  effect.fromSlot = fromSlot;
  effect.toSlot = toSlot;
  effect.loopStartTick = loopStartTick;
  effect.loopLengthTicks = loopLengthTicks;
  (void)gEdgeEffectQueue.record(effect);
}

/// Timing-path review, item 1: everything the tick edge used to *say* or *refresh* happens here, in the main
/// loop, in the order the edge decided it. `FLASHMEM` because this is main-loop work and the edge's own
/// footprint is what ITCM is for; the row texts are unchanged, which is what keeps the evidence readable to the
/// host tooling that greps for them.
///
/// **Ordering note, stated rather than implied:** a row the edge used to print is now printed up to one
/// `loop()` iteration later (the drain sits beside `processDroidUsbHostOutbound` at the top of the loop, before
/// the capture flush), so its `millis()` prefix and its position in the serial stream move by that much. The
/// *device-time* relationships the acceptance rows are read for are untouched: nothing here is a timing edge.
FLASHMEM void TrackManager::drainEdgeEffects() {
  EdgeEffectRecord effect;
  // Bounded per call: at most one queue's worth, even if the edge keeps producing while this runs.
  for (uint16_t turn = 0; turn < EdgeEffectQueue::kCapacity; ++turn) {
    if (!gEdgeEffectQueue.takeOldest(effect)) {
      break;
    }
    const uint8_t track = effect.track;
    const bool wantsLed = (effect.work & kEdgeEffectLedRefresh) != 0u;
    if (effect.kind == static_cast<uint8_t>(EdgeEffectKind::LoopEndCommit)) {
      if ((effect.work & kEdgeEffectLog) != 0u) {
        logger.info("LoopEnd playback commit track=%u %u->%u start=%lu len=%lu",
                    static_cast<unsigned>(track), static_cast<unsigned>(effect.fromSlot),
                    static_cast<unsigned>(effect.toSlot),
                    static_cast<unsigned long>(effect.loopStartTick),
                    static_cast<unsigned long>(effect.loopLengthTicks));
      }
      if ((effect.work & kEdgeEffectDisplayInvalidate) != 0u) {
        displayManager.invalidateForSlotChange(track, effect.fromSlot, getPreviewSlotIndex(track));
      }
    } else if (effect.kind == static_cast<uint8_t>(EdgeEffectKind::SlotSwitchCancelledNoCommittedMidi)) {
      logger.info("LoopEnd playback commit cancelled track=%u target=%u (no committed MIDI)",
                  static_cast<unsigned>(track), static_cast<unsigned>(effect.toSlot));
    } else if (effect.kind == static_cast<uint8_t>(EdgeEffectKind::SlotSwitchCancelledNoRamData)) {
      logger.info("LoopEnd playback commit cancelled track=%u target=%u (no RAM data)",
                  static_cast<unsigned>(track), static_cast<unsigned>(effect.toSlot));
    }
    // The LED refresh is the one effect whose payload is live state rather than the recorded decision, so it
    // takes the *current* tick -- the same call shape every other `forceMidiLedUpdate` site uses.
    if (wantsLed) {
      forceMidiLedUpdate(clockManager.getCurrentTick());
    }
  }
  const uint32_t dropped = gEdgeEffectQueue.droppedCount();
  if (dropped != gReportedDroppedEdgeEffects) {
    gReportedDroppedEdgeEffects = dropped;
    logger.log(CAT_STATE, LOG_WARNING,
               "Edge effects dropped (%lu): the tick edge produced more than %u before a main-loop drain",
               static_cast<unsigned long>(dropped), static_cast<unsigned>(EdgeEffectQueue::kCapacity));
  }
}
