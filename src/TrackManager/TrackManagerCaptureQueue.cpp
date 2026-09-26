//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "TrackManager.h"
#include "TrackManagerInternal.h"

#include "ClockManager.h"
#include "Logger.h"
#include "LooperState.h"
#include "PassReclaim.h"
#include "StorageManager.h"
#include "Utils/DebugSessionCapture.h"

namespace {

void requestLoopSlotPersistAndSaveState(const Track& track, uint8_t trackIndex, uint8_t slotIndex) {
  StorageManager::markLoopSlotMaterialDirty(trackIndex, slotIndex);
  StorageManager::admitLoopSlotPersist(trackIndex, slotIndex);
  StorageManager::requestDeferredSaveState(looperState.getLooperState());
}

}  // namespace

bool TrackManager::startRecordingTrack(uint8_t trackIndex, uint32_t currentTick) {
  if (trackIndex >= Config::NUM_TRACKS) return false;

  Track& tr = tracks[trackIndex];
  // S4f.5 / intermediate slice: a latched (accepted, not yet executed) stop is in-flight work just
  // as an admitted job is, so a record start must not move underneath it. Checking only
  // `hasPendingDeferredOverdubStop()` left the latch invisible here and admitted the start on top of
  // an unadmitted stop. S4f.6 replaces this refusal with the accepted-start carrier and the
  // stop > switch > start composition.
  if (tr.hasStopInFlightForSlot(tr.getActiveLoopIndex())) {
    logger.log(CAT_TRACK, LOG_INFO,
               "Track %d: record admission deferred while a stop obligation is live for slot %u",
               trackIndex, static_cast<unsigned>(tr.getActiveLoopIndex()));
    return false;
  }
  const uint8_t slot = tr.getActiveLoopIndex();
  if (tr.hasCommittedPassesInSlot(slot)) {
    logger.log(CAT_TRACK, LOG_WARNING,
               "Track %d: cannot arm slot %u — slot already has committed passes MIDI",
               trackIndex, static_cast<unsigned>(slot) + 1u);
    return false;
  }

  // Captures write into the active slot; ensure the target slot is enabled/unmuted.
  // Stopped record collapses a leftover layered set so only this slot plays after stop.
  {
    const bool replaceLayeredSet = !tr.isAudiblePlaybackActive();
    replaceSingleEnabledSlotWithTarget(slotEnabled, slotMuted, trackIndex, slot,
                                       replaceLayeredSet);
    slotEnabled[trackIndex][slot] = true;
    slotMuted[trackIndex][slot] = false;
  }

  // Only allow recording if the clock is running (internal or external)
  if (!clockManager.shouldQuantizeRecordStart()) {
    pendingRecord[trackIndex] = true;
    pendingRecordQueuedAtTick[trackIndex] = UINT32_MAX;
    pendingRecordRefSlot[trackIndex] = Config::INVALID_LOOP_SLOT;
    for (uint8_t s = 0; s < Config::MAX_LOOPS_PER_TRACK; ++s) {
      pendingRecordSlot[trackIndex][s] = false;
    }
    pendingRecordSlot[trackIndex][slot] = true;
    // PLAYING/OVERDUBBING cannot transition to TRACK_ARMED; keep state and use pendingRecord only
    // (getTrackState maps pending + PLAYING/OVERDUBBING -> ARMED for UI).
    const bool keepStateForUiArm = tr.isAudiblePlaybackActive();
    if (!keepStateForUiArm && !tr.setState(TRACK_ARMED)) {
      pendingRecord[trackIndex] = false;
      pendingRecordQueuedAtTick[trackIndex] = UINT32_MAX;
      pendingRecordRefSlot[trackIndex] = Config::INVALID_LOOP_SLOT;
      for (uint8_t s = 0; s < Config::MAX_LOOPS_PER_TRACK; ++s) {
        pendingRecordSlot[trackIndex][s] = false;
      }
      logger.log(CAT_TRACK, LOG_WARNING, "Track %d: could not arm for record (state=%s)",
                 trackIndex, tr.getStateName(tr.getState()));
      return false;
    }
    logger.log(CAT_TRACK, LOG_INFO, "Track %d armed, waiting for clock to start recording", trackIndex);
    // The request was re-queued above, so it is retained, not started: report false so the caller
    // does not consume it.
    return false;
  }
  tracks[trackIndex].startRecording(currentTick);
  releaseBackgroundPlaybackMergedMidiEventsMemory(trackIndex);
  reclaimUnreferencedDisabledPasses();
  return true;
}

void TrackManager::releaseBackgroundPlaybackMergedMidiEventsMemory(uint8_t captureTrackIndex) {
  if (captureTrackIndex >= Config::NUM_TRACKS) {
    return;
  }
  for (uint8_t i = 0; i < Config::NUM_TRACKS; ++i) {
    if (i != captureTrackIndex) {
      tracks[i].releasePlaybackMergedMidiEventsMemory();
    }
  }
}

void TrackManager::stopRecordingTrack(uint8_t trackIndex) {
  if (trackIndex >= Config::NUM_TRACKS) return;

  tracks[trackIndex].stopRecording(clockManager.getCurrentTick());
  uint32_t recordedLength = tracks[trackIndex].getLoopLength();

  if (masterLoopLength == 0) {
    setMasterLoopLength(recordedLength);  // First loop sets master length
  }

  if (autoAlignEnabled) {
    tracks[trackIndex].setLoopLength(masterLoopLength);
  }
}

void TrackManager::queueRecordingTrack(uint8_t trackIndex, uint8_t slotIndex,
                                       uint8_t refSlotForPhase) {
  if (trackIndex >= Config::NUM_TRACKS || slotIndex >= Config::MAX_LOOPS_PER_TRACK) return;
  if (tracks[trackIndex].hasCommittedPassesInSlot(slotIndex)) {
    logger.log(CAT_TRACK, LOG_WARNING,
               "Track %d: cannot queue record on slot %u — slot already has committed passes MIDI",
               trackIndex, static_cast<unsigned>(slotIndex) + 1u);
    return;
  }

  // Target slot should be part of playback set while recording.
  replaceSingleEnabledSlotWithTarget(slotEnabled, slotMuted, trackIndex, slotIndex);
  slotEnabled[trackIndex][slotIndex] = true;
  slotMuted[trackIndex][slotIndex] = false;

  pendingRecord[trackIndex] = true;
  pendingRecordQueuedAtTick[trackIndex] = clockManager.getCurrentTick();
  uint8_t ref = refSlotForPhase;
  if (ref < Config::MAX_LOOPS_PER_TRACK) {
    const Loop& r = tracks[trackIndex].getLoop(ref);
    if (r.loopLengthTicks == 0) ref = Config::INVALID_LOOP_SLOT;
  }
  pendingRecordRefSlot[trackIndex] = ref;
  for (uint8_t s = 0; s < Config::MAX_LOOPS_PER_TRACK; ++s) {
    pendingRecordSlot[trackIndex][s] = false;
  }
  pendingRecordSlot[trackIndex][slotIndex] = true;
}

void TrackManager::clearQueuedRecordingTrack(uint8_t trackIndex, uint8_t slotIndex) {
  if (trackIndex >= Config::NUM_TRACKS || slotIndex >= Config::MAX_LOOPS_PER_TRACK) return;
  pendingRecordSlot[trackIndex][slotIndex] = false;
  bool anyPending = false;
  for (uint8_t s = 0; s < Config::MAX_LOOPS_PER_TRACK; ++s) {
    if (pendingRecordSlot[trackIndex][s]) {
      anyPending = true;
      break;
    }
  }
  pendingRecord[trackIndex] = anyPending;
  if (!anyPending) {
    pendingRecordQueuedAtTick[trackIndex] = UINT32_MAX;
    pendingRecordRefSlot[trackIndex] = Config::INVALID_LOOP_SLOT;
  }
}

bool TrackManager::isRecordingQueued(uint8_t trackIndex, uint8_t slotIndex) const {
  if (trackIndex >= Config::NUM_TRACKS || slotIndex >= Config::MAX_LOOPS_PER_TRACK) return false;
  return pendingRecordSlot[trackIndex][slotIndex];
}

bool TrackManager::hasQueuedRecordingTrack(uint8_t trackIndex) const {
  return (trackIndex < Config::NUM_TRACKS) ? pendingRecord[trackIndex] : false;
}

bool TrackManager::hasActiveOrPendingCapture() const {
  for (uint8_t i = 0; i < Config::NUM_TRACKS; ++i) {
    const Track& t = tracks[i];
    if (t.isRecording() || t.isArmed() || t.isOverdubbing() || t.hasPendingDeferredOverdubStop() ||
        pendingRecord[i]) {
      return true;
    }
  }
  return false;
}

uint8_t TrackManager::getQueuedRecordingSlot(uint8_t trackIndex) const {
  if (trackIndex >= Config::NUM_TRACKS || !pendingRecord[trackIndex]) {
    return Config::INVALID_LOOP_SLOT;
  }
  for (uint8_t s = 0; s < Config::MAX_LOOPS_PER_TRACK; ++s) {
    if (pendingRecordSlot[trackIndex][s]) return s;
  }
  return Config::INVALID_LOOP_SLOT;
}

void TrackManager::cancelPendingRecordArm(uint8_t trackIndex) {
  if (trackIndex >= Config::NUM_TRACKS) return;
  pendingRecord[trackIndex] = false;
  pendingRecordQueuedAtTick[trackIndex] = UINT32_MAX;
  pendingRecordRefSlot[trackIndex] = Config::INVALID_LOOP_SLOT;
  for (uint8_t s = 0; s < Config::MAX_LOOPS_PER_TRACK; ++s) {
    pendingRecordSlot[trackIndex][s] = false;
  }
  Track& t = tracks[trackIndex];
  if (t.isArmed()) {
    if (t.hasAnySlotData()) {
      t.setState(TRACK_STOPPED);
    } else {
      t.setState(TRACK_EMPTY);
    }
  }
}

void TrackManager::queueStopRecordingTrack(uint8_t trackIndex) {
  if (trackIndex < Config::NUM_TRACKS) pendingStop[trackIndex] = true;
}

void TrackManager::startOverdubbingTrack(uint8_t trackIndex) {
  if (trackIndex >= Config::NUM_TRACKS) {
    return;
  }
  Track& track = tracks[trackIndex];
  // Successor reentry (OpenSpec successor-capture-reentry-unguarding 3.1): a pending
  // deferred stop no longer refuses a new interval. START requires the live admission role
  // to be free, which `Track::startOverdubbing` proves along with every storage/ownership
  // precondition; a later STOP is what the deferred/frozen role bounds. Cross-track
  // maintenance predicates keep consulting `hasPendingDeferredOverdubStop()` unchanged.
  const Loop& loop = track.getActiveLoop();
  if (track.isOverdubbing() && loop.capture.phase == CapturePhase::Overdub) {
    return;
  }
  // S4f.3: an overdub needs an existing loop to overdub onto. A zero-length active loop means the
  // target has no loop yet -- after a switch handoff that is an empty successor, which needs *record*
  // admission (it establishes the length), not an overdub session. Refusing explicitly keeps the two
  // routes distinct instead of opening a session that can never accept an event.
  if (loop.loopLengthTicks == 0) {
    logger.log(CAT_TRACK, LOG_WARNING,
               "Overdub start refused: active loop has no length (slot=%u); the target needs record "
               "admission first",
               static_cast<unsigned>(track.getActiveLoopIndex()));
    return;
  }
  SC_ODUB_STAGE("manager_enter", 0, DebugSessionCapture::kUnsampledHeapBytes,
                DebugSessionCapture::kUnsampledHeapBytes, "ok");
  const uint8_t slot = track.getActiveLoopIndex();
  slotEnabled[trackIndex][slot] = true;
  slotMuted[trackIndex][slot] = false;
#if defined(SESSION_CAPTURE)
  const uint32_t startUs = micros();
#endif
  track.startOverdubbing(clockManager.getCurrentTick());
  // Overdub start is timing-critical. Do not resetAll other tracks' playback runtimes here:
  // session_20260820_153347 truncated immediately after "Overdubbing started", which is the
  // next call in this function. Record start still releases background merged events;
  // Low/Critical pressure uses tryReleasePlaybackMergedMidiEventsMemory.
  // Disabled-pass reclaim also stays off this path (session_20260820_144819).
#if defined(SESSION_CAPTURE)
  SC_ODUB_STAGE("manager_done", micros() - startUs, DebugSessionCapture::kUnsampledHeapBytes,
                DebugSessionCapture::kUnsampledHeapBytes, "ok");
#endif
}

void TrackManager::handlePendingRecordStart(uint32_t currentTick) {
  const uint32_t barTicks = Track::getTicksPerBar();

  for (uint8_t i = 0; i < Config::NUM_TRACKS; i++) {
    if (!pendingRecord[i]) continue;
    if (pendingRecordQueuedAtTick[i] != UINT32_MAX &&
        currentTick == pendingRecordQueuedAtTick[i]) {
      continue;
    }
    uint8_t targetSlot = Config::INVALID_LOOP_SLOT;
    for (uint8_t s = 0; s < Config::MAX_LOOPS_PER_TRACK; ++s) {
      if (pendingRecordSlot[i][s]) {
        targetSlot = s;
        break;
      }
    }
    if (targetSlot >= Config::MAX_LOOPS_PER_TRACK) continue;

    bool shouldStart = false;
    uint8_t refIdx = pendingRecordRefSlot[i];
    if (refIdx >= Config::MAX_LOOPS_PER_TRACK || refIdx == Config::INVALID_LOOP_SLOT) {
      if (barTicks == 0) continue;
      if (currentTick != 0 && (currentTick % barTicks) != 0) continue;
      shouldStart = true;
    } else {
      const Loop& refLoop = tracks[i].getLoop(refIdx);
      uint32_t L = refLoop.loopLengthTicks;
      uint32_t st = refLoop.startLoopTick;
      if (L == 0 || currentTick < st) {
        if (barTicks == 0) continue;
        if (currentTick != 0 && (currentTick % barTicks) != 0) continue;
        shouldStart = true;
      } else {
        uint32_t phase = (currentTick - st) % L;
        if (phase == 0) shouldStart = true;
      }
    }

    if (!shouldStart) continue;

    // S4f.6: consume the request only when the start actually happened. A refusal -- most importantly
    // a live stop obligation, which is exactly the state a switch handoff leaves -- retains the request
    // so the next eligible boundary retries it, instead of stranding it (retained, never executed,
    // never superseded). Until the start succeeds the predecessor's stop is not a reason to discard the
    // successor's request.
    // The target becomes active first, as the contract requires; then the start is attempted and the
    // request is consumed only if it happened.
    tracks[i].setActiveLoopIndex(targetSlot);
    const bool started = startRecordingTrack(i, currentTick);
    if (!started) {
      continue;
    }
    pendingRecordSlot[i][targetSlot] = false;
    pendingRecord[i] = false;
    pendingRecordQueuedAtTick[i] = UINT32_MAX;
    pendingRecordRefSlot[i] = Config::INVALID_LOOP_SLOT;
  }
}

void TrackManager::handleQuantizedStop(uint32_t currentTick) {
  const uint32_t barTicks = Track::getTicksPerBar();
  if (barTicks == 0 || (currentTick != 0 && (currentTick % barTicks) != 0)) return;

  for (uint8_t i = 0; i < Config::NUM_TRACKS; i++) {
    if (pendingStop[i]) {
      stopRecordingTrack(i);
      pendingStop[i] = false;
    }
  }
}

bool TrackManager::finalizeCaptureAndSelectSlot(uint8_t trackIndex, uint8_t newSlot,
                                               uint32_t currentTick) {
  if (trackIndex >= Config::NUM_TRACKS || newSlot >= Config::MAX_LOOPS_PER_TRACK) return false;

  pendingRecord[trackIndex] = false;
  pendingRecordQueuedAtTick[trackIndex] = UINT32_MAX;
  pendingRecordRefSlot[trackIndex] = Config::INVALID_LOOP_SLOT;
  for (uint8_t s = 0; s < Config::MAX_LOOPS_PER_TRACK; ++s) {
    pendingRecordSlot[trackIndex][s] = false;
  }

  Track& t = tracks[trackIndex];
  // Slot that holds the in-progress capture (before stop moves state / active index).
  const uint8_t captureSlot = t.getActiveLoopIndex();

  if (t.isRecording()) {
    t.stopRecordingToStopped(currentTick);
    uint32_t recordedLength = t.getLoopLength();
    if (recordedLength > 0 && masterLoopLength == 0) {
      setMasterLoopLength(recordedLength);
    }
    if (autoAlignEnabled) {
      t.setLoopLength(masterLoopLength);
    }
    // Qualified to this TU's local overload. `TrackInternal.h` also declares a global
    // `requestLoopSlotPersistAndSaveState(Track&, uint8_t, uint8_t)` (defined in
    // `TrackStopTelemetryColdHelpers.cpp`), and the literal argument converts to both signatures, so
    // the call is ambiguous whenever both TUs are visible together -- which is exactly what happens in
    // an include-based native harness. Firmware compiles the TUs separately and never sees it.
    ::requestLoopSlotPersistAndSaveState(t, trackIndex, captureSlot);
  } else if (t.isOverdubbing()) {
    t.stopOverdubbing();
    // S4f.5 / intermediate slice (DEC-078 item 4): the active slot may move only once the interval's
    // stop was *admitted and bound* to it. When the stop was retained on the latch instead, the slot
    // stays put -- otherwise the accepted stop would be driven against a slot that is no longer its
    // own. S4f.3 replaces this with the switch's accepted-interaction carrier and commits the switch
    // after the stop binds. S4b.1's handoff depends on that carrier: without it, moving the active
    // slot here breaks `test_s4f5_accepted_stop_blocks_start_and_switch_until_it_executes`.
    if (t.hasAcceptedUnexecutedInteraction()) return false;
  }
  // S4f.3: an **admitted** stop does not hold the active slot. The deferred job carries its own
  // identity -- `job.owner`, `job.slot`, `job.loopId`, the interval latch and `captureStopOwner` -- and
  // `processDeferredOverdubStopGrain` finalizes from `*job.owner` / `slotIfAllocated(job.slot)`, so the
  // handoff can commit and the successor can start while the predecessor still finalizes. The
  // *unadmitted* stop is still refused above: S4f.5 requires the active slot not to move under an
  // accepted-but-unexecuted stop, whose interval slot the latch has not yet bound.

  t.setActiveLoopIndex(newSlot);
  // A captured slot becomes part of the enabled playback set.
  slotEnabled[trackIndex][newSlot] = true;
  slotMuted[trackIndex][newSlot] = false;

  t.resetPlaybackStateForSlot(newSlot, currentTick);
  const Loop& newLoop = t.getLoop(newSlot);

  bool otherAudibleHasData = false;
  for (uint8_t s = 0; s < Config::MAX_LOOPS_PER_TRACK; ++s) {
    if (s == newSlot) continue;
    if (slotEnabled[trackIndex][s] && !slotMuted[trackIndex][s] && t.hasDataInSlot(s)) {
      otherAudibleHasData = true;
      break;
    }
  }

  if (t.hasDataInSlot(newSlot) && newLoop.loopLengthTicks > 0) {
    if (!t.isAudiblePlaybackActive()) t.startPlaying(currentTick);
  } else if (t.isAudiblePlaybackActive() && !otherAudibleHasData) {
    // Only stop the track if no other enabled slot is audible.
    t.stopPlaying();
  }
  // Keep UI focus on the slot that received the capture so piano roll / LEDs match the new audio.
  setSelectedSlotIndex(trackIndex, captureSlot, SyncPlayback::No);
  forceMidiLedUpdate(currentTick);
  // S4f.3: the switch committed. This is the commit point a carrier consumes at, so the caller learns
  // the target is active instead of having to re-derive it from the active slot index.
  return true;
}
