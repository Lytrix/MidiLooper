//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "Globals.h"
#include <cstdint>
#include "ClockManager.h"
#include "TrackManager.h"
#include "StorageManager.h"
#include "LooperState.h"
#include "Logger.h"
#include "MidiHandler.h"
#include "NoteEditManager.h"
#include "PassReclaim.h"

TrackManager trackManager;

TrackManager::TrackManager() {
  // Initialize MidiLedManager
  ledManager = new MidiLedManager(midiHandler);
  
  for (uint8_t i = 0; i < Config::NUM_TRACKS; i++) {
    pendingRecord[i] = false;
    pendingRecordQueuedAtTick[i] = UINT32_MAX;
    pendingRecordRefSlot[i] = Config::INVALID_LOOP_SLOT;
    for (uint8_t s = 0; s < Config::MAX_LOOPS_PER_TRACK; ++s) {
      pendingRecordSlot[i][s] = false;
      heldLayerSlot[i][s] = false;
      // Default: single-slot mode plays slot 0.
      slotEnabled[i][s] = false;
      slotMuted[i][s] = false;
      pendingSlotEnabled[i][s] = false;
      pendingHoldActive[i][s] = false;
    }
    pendingStop[i] = false;
    muted[i] = false;
    soloed[i] = false;
    // Default slot enabled: 0 (keeps behavior predictable on empty project load).
    slotEnabled[i][0] = true;
    pendingHoldCount[i] = 0;
    pendingMultiSlotCommit[i] = false;
    pendingMultiSlotQueuedAtTick[i] = UINT32_MAX;
    pendingEnabledSetReplacement[i] = false;
  }
  autoAlignEnabled = false;
  masterLoopLength = 0;
}

TrackManager::~TrackManager() {
  delete ledManager;
}

void TrackManager::allocateLoopsEarly() {
  for (uint8_t i = 0; i < Config::NUM_TRACKS; i++) {
    tracks[i].ensureLoopsAllocated();
  }
}

void TrackManager::prewarmPlaybackRuntime() {
  for (uint8_t t = 0; t < Config::NUM_TRACKS; ++t) {
    Track& track = tracks[t];
    track.ensureLoopsAllocated();
    for (uint8_t s = 0; s < Config::MAX_LOOPS_PER_TRACK; ++s) {
      if (!isSlotEnabled(t, s) && !track.loopForSlot(s).hasPublishedEvents()) {
        continue;
      }
      track.prewarmPlaybackForSlot(s);
    }
  }
}

// Recording & Overdubbing ------------------------------------

void TrackManager::startRecordingTrack(uint8_t trackIndex, uint32_t currentTick) {
  if (trackIndex >= Config::NUM_TRACKS) return;

  // Captures write into the active slot; ensure the target slot is enabled/unmuted.
  {
    const uint8_t slot = tracks[trackIndex].getActiveLoopIndex();
    slotEnabled[trackIndex][slot] = true;
    slotMuted[trackIndex][slot] = false;
  }

  // Only allow recording if the clock is running (internal or external)
  if (!clockManager.shouldQuantizeRecordStart()) {
    Track& tr = tracks[trackIndex];
    pendingRecord[trackIndex] = true;
    pendingRecordQueuedAtTick[trackIndex] = UINT32_MAX;
    pendingRecordRefSlot[trackIndex] = Config::INVALID_LOOP_SLOT;
    uint8_t slot = tr.getActiveLoopIndex();
    for (uint8_t s = 0; s < Config::MAX_LOOPS_PER_TRACK; ++s) {
      pendingRecordSlot[trackIndex][s] = false;
    }
    pendingRecordSlot[trackIndex][slot] = true;
    // PLAYING/OVERDUBBING cannot transition to TRACK_ARMED; keep state and use pendingRecord only
    // (getTrackState maps pending + PLAYING/OVERDUBBING -> ARMED for UI).
    const bool keepStateForUiArm = tr.isPlaying() || tr.isOverdubbing();
    if (!keepStateForUiArm && !tr.setState(TRACK_ARMED)) {
      pendingRecord[trackIndex] = false;
      pendingRecordQueuedAtTick[trackIndex] = UINT32_MAX;
      pendingRecordRefSlot[trackIndex] = Config::INVALID_LOOP_SLOT;
      for (uint8_t s = 0; s < Config::MAX_LOOPS_PER_TRACK; ++s) {
        pendingRecordSlot[trackIndex][s] = false;
      }
      logger.log(CAT_TRACK, LOG_WARNING, "Track %d: could not arm for record (state=%s)",
                 trackIndex, tr.getStateName(tr.getState()));
      return;
    }
    logger.log(CAT_TRACK, LOG_INFO, "Track %d armed, waiting for clock to start recording", trackIndex);
    return;
  }
  tracks[trackIndex].startRecording(currentTick);
}

void TrackManager::stopRecordingTrack(uint8_t trackIndex) {
  Serial.println("stopRecordingTrack called");
  if (trackIndex >= Config::NUM_TRACKS) return;


  tracks[trackIndex].stopRecording(clockManager.getCurrentTick());
  uint32_t recordedLength = tracks[trackIndex].getLoopLength();
  
  if (masterLoopLength == 0) {
    setMasterLoopLength(recordedLength);  // First loop sets master length
  }

  if (autoAlignEnabled) {
    tracks[trackIndex].setLoopLength(masterLoopLength);
  }
  Serial.println("Saving state after recording");
  StorageManager::requestDeferredSaveState(looperState.getLooperState());
}

void TrackManager::queueRecordingTrack(uint8_t trackIndex, uint8_t slotIndex,
                                       uint8_t refSlotForPhase) {
  if (trackIndex >= Config::NUM_TRACKS || slotIndex >= Config::MAX_LOOPS_PER_TRACK) return;

  // Target slot should be part of playback set while recording.
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
    if (t.isRecording() || t.isArmed() || pendingRecord[i]) {
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
    t.setState(t.hasData() ? TRACK_STOPPED : TRACK_EMPTY);
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
  const Loop& loop = track.getActiveLoop();
  if (track.isOverdubbing() && loop.capture.phase == CapturePhase::Overdub) {
    return;
  }
  const uint8_t slot = track.getActiveLoopIndex();
  slotEnabled[trackIndex][slot] = true;
  slotMuted[trackIndex][slot] = false;
  track.startOverdubbing(clockManager.getCurrentTick());
}

// Quantized Actions ------------------------------------------

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

    tracks[i].setActiveLoopIndex(targetSlot);
    startRecordingTrack(i, currentTick);
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

// Playback Control -------------------------------------------

void TrackManager::startPlayingTrack(uint8_t trackIndex) {
  if (trackIndex < Config::NUM_TRACKS) tracks[trackIndex].startPlaying(clockManager.getCurrentTick());
}

void TrackManager::stopPlayingTrack(uint8_t trackIndex) {
  if (trackIndex < Config::NUM_TRACKS) tracks[trackIndex].stopPlaying();
}

void TrackManager::handleTransportStop() {
  uint32_t currentTick = clockManager.getCurrentTick();
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
    if (t.isRecording()) {
      // Stop recording BEFORE sendAllNotesOff(): finalizePendingNotes() must record
      // note-offs for still-held notes, but sendAllNotesOff() clears pendingNotes,
      // which left the last note-on orphaned so validation removed it.
      t.stopRecordingToStopped(currentTick);
      t.sendAllNotesOff();
      uint32_t recordedLength = t.getLoopLength();
      if (recordedLength > 0 && masterLoopLength == 0) {
        setMasterLoopLength(recordedLength);
      }
      if (autoAlignEnabled) {
        t.setLoopLength(masterLoopLength);
      }
    } else if (t.isOverdubbing()) {
      t.sendAllNotesOff();
      t.stopOverdubbingToStopped();
    } else if (t.isPlaying()) {
      t.stopPlaying();  // sends All Notes Off internally
    } else if (t.isArmed()) {
      t.sendAllNotesOff();
      t.setState(t.hasData() ? TRACK_STOPPED : TRACK_EMPTY);
    } else {
      t.sendAllNotesOff();
    }
  }
  forceLedUpdate(currentTick);
  StorageManager::saveState(looperState.getLooperState());
}

void TrackManager::clearTrack(uint8_t trackIndex) {
  if (trackIndex < Config::NUM_TRACKS) tracks[trackIndex].clear();

  // Check if all tracks are empty
  bool allEmpty = true;
  for (uint8_t i = 0; i < Config::NUM_TRACKS; ++i) {
    if (!tracks[i].isEmpty()) {
      allEmpty = false;
      break;
    }
  }
  if (allEmpty) {
    masterLoopLength = 0;
  }
}

// Mute / Solo ------------------------------------------------

void TrackManager::muteTrack(uint8_t trackIndex) {
  if (trackIndex < Config::NUM_TRACKS) muted[trackIndex] = true;
}

void TrackManager::unmuteTrack(uint8_t trackIndex) {
  if (trackIndex < Config::NUM_TRACKS) muted[trackIndex] = false;
}

void TrackManager::toggleMuteTrack(uint8_t trackIndex) {
  if (trackIndex < Config::NUM_TRACKS) muted[trackIndex] = !muted[trackIndex];
}

void TrackManager::soloTrack(uint8_t trackIndex) {
  if (trackIndex < Config::NUM_TRACKS) soloed[trackIndex] = true;
}

void TrackManager::unsoloTrack(uint8_t trackIndex) {
  if (trackIndex < Config::NUM_TRACKS) soloed[trackIndex] = false;
}

void TrackManager::toggleSoloTrack(uint8_t trackIndex) {
  if (trackIndex >= Config::NUM_TRACKS) return;
  if (soloed[trackIndex]) {
    for (uint8_t i = 0; i < Config::NUM_TRACKS; i++) {
      soloed[i] = false;
    }
  } else {
    for (uint8_t i = 0; i < Config::NUM_TRACKS; i++) {
      soloed[i] = (i == trackIndex);
    }
  }
}

bool TrackManager::anyTrackSoloed() const {
  for (uint8_t i = 0; i < Config::NUM_TRACKS; i++) {
    if (soloed[i]) return true;
  }
  return false;
}

bool TrackManager::isTrackSoloed(uint8_t trackIndex) const {
  return trackIndex < Config::NUM_TRACKS && soloed[trackIndex];
}

bool TrackManager::isTrackAudible(uint8_t trackIndex) const {
  if (trackIndex >= Config::NUM_TRACKS) return false;
  if (tracks[trackIndex].isMuted()) return false;
  if (anyTrackSoloed() && !soloed[trackIndex]) return false;
  return true;
}

// Master Loop Length -----------------------------------------

void TrackManager::enableAutoAlign(bool enabled) {
  autoAlignEnabled = enabled;
}

bool TrackManager::isAutoAlignEnabled() const {
  return autoAlignEnabled;
}

void TrackManager::setMasterLoopLength(uint32_t length) {
  masterLoopLength = length;
}

uint32_t TrackManager::getMasterLoopLength() const {
  return masterLoopLength;
}

// Track Info Accessors ---------------------------------------

TrackState TrackManager::getTrackState(uint8_t trackIndex) const {
  if (trackIndex >= Config::NUM_TRACKS) return TRACK_STOPPED;
  TrackState s = tracks[trackIndex].getState();
  if (pendingRecord[trackIndex] &&
      (s == TRACK_PLAYING || s == TRACK_OVERDUBBING)) {
    return TRACK_ARMED;
  }
  return s;
}

uint32_t TrackManager::getTrackLength(uint8_t trackIndex) const {
  return (trackIndex < Config::NUM_TRACKS) ? tracks[trackIndex].getLoopLength() : 0;
}

uint8_t TrackManager::getActiveLoopIndex(uint8_t trackIndex) const {
  return (trackIndex < Config::NUM_TRACKS) ? tracks[trackIndex].getActiveLoopIndex() : 0;
}

void TrackManager::finalizeCaptureAndSelectSlot(uint8_t trackIndex, uint8_t newSlot, uint32_t currentTick) {
  if (trackIndex >= Config::NUM_TRACKS || newSlot >= Config::MAX_LOOPS_PER_TRACK) return;

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
    StorageManager::saveState(looperState.getLooperState());
  } else if (t.isOverdubbing()) {
    t.stopOverdubbing();
  }

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
    if (!t.isPlaying()) t.startPlaying(currentTick);
  } else if (t.isPlaying() && !otherAudibleHasData) {
    // Only stop the track if no other enabled slot is audible.
    t.stopPlaying();
  }
  // Keep UI focus on the slot that received the capture so piano roll / LEDs match the new audio.
  setSelectedSlotIndex(trackIndex, captureSlot);
  forceLedUpdate(currentTick);
}

void TrackManager::setActiveLoopIndex(uint8_t trackIndex, uint8_t index) {
  if (trackIndex < Config::NUM_TRACKS) {
    Track& t = tracks[trackIndex];
    const uint8_t prev = t.getActiveLoopIndex();
    if (prev != index && (t.isRecording() || t.isOverdubbing())) {
      finalizeCaptureAndSelectSlot(trackIndex, index, clockManager.getCurrentTick());
      return;
    }
    t.setActiveLoopIndex(index);
    forceLedUpdate(clockManager.getCurrentTick());
  }
}

void TrackManager::setLayeredSlotHeld(uint8_t trackIndex, uint8_t slotIndex, bool held) {
  if (trackIndex >= Config::NUM_TRACKS || slotIndex >= Config::MAX_LOOPS_PER_TRACK) return;
  heldLayerSlot[trackIndex][slotIndex] = held;
}

bool TrackManager::isSlotEnabled(uint8_t trackIndex, uint8_t slotIndex) const {
  if (trackIndex >= Config::NUM_TRACKS || slotIndex >= Config::MAX_LOOPS_PER_TRACK) return false;
  return slotEnabled[trackIndex][slotIndex];
}

bool TrackManager::isSlotMuted(uint8_t trackIndex, uint8_t slotIndex) const {
  if (trackIndex >= Config::NUM_TRACKS || slotIndex >= Config::MAX_LOOPS_PER_TRACK) return false;
  return slotMuted[trackIndex][slotIndex];
}

void TrackManager::setSlotEnabled(uint8_t trackIndex, uint8_t slotIndex, bool enabled) {
  if (trackIndex >= Config::NUM_TRACKS || slotIndex >= Config::MAX_LOOPS_PER_TRACK) return;
  slotEnabled[trackIndex][slotIndex] = enabled;
}

void TrackManager::setSlotMuted(uint8_t trackIndex, uint8_t slotIndex, bool mutedValue) {
  if (trackIndex >= Config::NUM_TRACKS || slotIndex >= Config::MAX_LOOPS_PER_TRACK) return;
  slotMuted[trackIndex][slotIndex] = mutedValue;
}

void TrackManager::toggleSlotMuted(uint8_t trackIndex, uint8_t slotIndex) {
  if (trackIndex >= Config::NUM_TRACKS || slotIndex >= Config::MAX_LOOPS_PER_TRACK) return;
  slotMuted[trackIndex][slotIndex] = !slotMuted[trackIndex][slotIndex];
}

uint8_t TrackManager::countEnabledSlots(uint8_t trackIndex) const {
  if (trackIndex >= Config::NUM_TRACKS) return 0;
  uint8_t c = 0;
  for (uint8_t s = 0; s < Config::MAX_LOOPS_PER_TRACK; ++s) {
    if (slotEnabled[trackIndex][s]) c++;
  }
  return c;
}

void TrackManager::beginSlotSelectionHold(uint8_t trackIndex, uint8_t slotIndex) {
  if (trackIndex >= Config::NUM_TRACKS || slotIndex >= Config::MAX_LOOPS_PER_TRACK) return;
  if (pendingHoldActive[trackIndex][slotIndex]) return;

  // First hold armed: start a fresh pending enabled set.
  if (pendingHoldCount[trackIndex] == 0) {
    for (uint8_t s = 0; s < Config::MAX_LOOPS_PER_TRACK; ++s) {
      pendingSlotEnabled[trackIndex][s] = false;
      pendingHoldActive[trackIndex][s] = false;
    }
    pendingMultiSlotCommit[trackIndex] = false;
    pendingMultiSlotQueuedAtTick[trackIndex] = UINT32_MAX;
    pendingEnabledSetReplacement[trackIndex] = false;
  }

  pendingHoldActive[trackIndex][slotIndex] = true;
  pendingHoldCount[trackIndex]++;

  // Include this slot in the pending enabled set.
  pendingSlotEnabled[trackIndex][slotIndex] = true;
  // Default: newly enabled slots start unmuted.
  pendingMultiSlotCommit[trackIndex] = false;  // cancel any in-progress commit build
}

void TrackManager::endSlotSelectionHold(uint8_t trackIndex, uint8_t slotIndex, uint32_t nowTick) {
  if (trackIndex >= Config::NUM_TRACKS || slotIndex >= Config::MAX_LOOPS_PER_TRACK) return;
  if (!pendingHoldActive[trackIndex][slotIndex]) return;

  pendingHoldActive[trackIndex][slotIndex] = false;
  if (pendingHoldCount[trackIndex] > 0) pendingHoldCount[trackIndex]--;

  // Commit when the last held slot is released.
  if (pendingHoldCount[trackIndex] == 0) {
    bool anyPending = false;
    for (uint8_t s = 0; s < Config::MAX_LOOPS_PER_TRACK; ++s) {
      if (pendingSlotEnabled[trackIndex][s]) { anyPending = true; break; }
    }
    if (anyPending) {
      pendingMultiSlotCommit[trackIndex] = true;
      pendingMultiSlotQueuedAtTick[trackIndex] = nowTick;
    } else {
      // Nothing selected: keep existing enabled set.
      for (uint8_t s = 0; s < Config::MAX_LOOPS_PER_TRACK; ++s) pendingSlotEnabled[trackIndex][s] = false;
    }
  }
}

void TrackManager::cancelSlotSelectionHold(uint8_t trackIndex) {
  if (trackIndex >= Config::NUM_TRACKS) return;

  pendingHoldCount[trackIndex] = 0;
  pendingMultiSlotCommit[trackIndex] = false;
  pendingMultiSlotQueuedAtTick[trackIndex] = UINT32_MAX;

  for (uint8_t s = 0; s < Config::MAX_LOOPS_PER_TRACK; ++s) {
    pendingHoldActive[trackIndex][s] = false;
    pendingSlotEnabled[trackIndex][s] = false;
  }
}

void TrackManager::setPendingEnabledSetReplacement(uint8_t trackIndex, bool enabled) {
  if (trackIndex >= Config::NUM_TRACKS) return;
  pendingEnabledSetReplacement[trackIndex] = enabled;
}

uint8_t TrackManager::getSelectedSlotIndex(uint8_t trackIndex) const {
  return slotStateMachine.getSelectedSlotIndex(trackIndex);
}

void TrackManager::setSelectedSlotIndex(uint8_t trackIndex, uint8_t slotIndex) {
  slotStateMachine.setSelectedSlotIndex(trackIndex, slotIndex);
  if (trackIndex == selectedTrack) {
    forceLedUpdate(clockManager.getCurrentTick());
  }
}

void TrackManager::requestSlotSwitch(uint8_t trackIndex,
                                      uint8_t slotIndex,
                                      SlotQuantization quantization,
                                      uint32_t queuedAtTick) {
  slotStateMachine.requestPendingSlotSwitch(trackIndex, slotIndex, quantization, queuedAtTick);
}

void TrackManager::clearPendingSlotSwitch(uint8_t trackIndex) {
  slotStateMachine.clearPendingSlotSwitch(trackIndex);
}

void TrackManager::setSelectedTrack(uint8_t index) {
  if (index < Config::NUM_TRACKS) {
    selectedTrack = index;
    // Force LED update when track changes
    forceLedUpdate(clockManager.getCurrentTick());
    // Notify NoteEditManager of track change for loop length CC feedback
    noteEditManager.onTrackChanged(tracks[selectedTrack]);
  }
}

uint8_t TrackManager::getSelectedTrackIndex() {
  return selectedTrack;
}

Track& TrackManager::getSelectedTrack() {
  return tracks[selectedTrack];
}

Track& TrackManager::getTrack(uint8_t index) {
  return tracks[index];
}

uint8_t TrackManager::getTrackCount() const {
  return Config::NUM_TRACKS;
}

void TrackManager::setup() {
  // Allocate Loop arrays (deferred from Track ctor to avoid static-init crash)
  // Loops allocated in allocateLoopsEarly() at start of setup
  // Set default MIDI output channel per track (track 1 = ch 1, track 2 = ch 2, etc.)
  for (uint8_t i = 0; i < Config::NUM_TRACKS; i++) {
    tracks[i].setMidiChannel(i + 1);
  }
}

void TrackManager::advanceJamTicks(uint32_t delta) {
  for (uint8_t i = 0; i < Config::NUM_TRACKS; i++) {
    tracks[i].advanceJamTick(delta);
  }
}

void TrackManager::updateAllTracks(uint32_t currentTick) {
  handlePendingRecordStart(currentTick);

  for (uint8_t i = 0; i < Config::NUM_TRACKS; i++) {
    // Commit multi-hold selection (enabled set + start at next 16th).
    if (pendingMultiSlotCommit[i] &&
        currentTick != pendingMultiSlotQueuedAtTick[i] &&
        (currentTick % Config::TICKS_PER_16TH_STEP) == 0) {

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
          if (slotEnabled[i][s] && tracks[i].hasDataInSlot(s)) { selectedSlot = s; break; }
        }
      }
      slotStateMachine.setSelectedSlotIndex(i, selectedSlot);
      setActiveLoopIndex(i, selectedSlot);
      // Reset playback indices so all newly enabled slots start at the commit phase.
      tracks[i].resetPlaybackStateForSlot(selectedSlot, currentTick);
      for (uint8_t s = 0; s < Config::MAX_LOOPS_PER_TRACK; ++s) {
        if (slotEnabled[i][s] && !slotMuted[i][s] && tracks[i].hasDataInSlot(s)) {
          tracks[i].resetPlaybackStateForSlot(s, currentTick);
        }
      }

      // Ensure playback is running if we have any audible slot data.
      if (anyEnabledUnmutedWithData && !tracks[i].isPlaying() && !tracks[i].isOverdubbing()) {
        // Track state machine may block EMPTY->PLAYING; we keep this guard minimal.
        startPlayingTrack(i);
      }

      forceLedUpdate(currentTick);
    }

    // Commit a pending quantized slot switch (Phase 1: playback switching only).
    if (tracks[i].isPlaying() && slotStateMachine.hasPendingSlotSwitch(i) &&
        slotStateMachine.shouldCommitPendingSlotSwitch(i, tracks[i], currentTick)) {
      const uint8_t targetSlot = slotStateMachine.getPendingSlotIndex(i);
      if (targetSlot < Config::MAX_LOOPS_PER_TRACK && tracks[i].hasDataInSlot(targetSlot)) {
        slotStateMachine.clearPendingSlotSwitch(i);
        setActiveLoopIndex(i, targetSlot);
        tracks[i].resetPlaybackState(currentTick);

        // If this slot switch came from a "select single slot" gesture, replace enabled set.
        if (pendingEnabledSetReplacement[i]) {
          for (uint8_t s = 0; s < Config::MAX_LOOPS_PER_TRACK; ++s) {
            slotEnabled[i][s] = (s == targetSlot);
            slotMuted[i][s] = false;
          }
          pendingEnabledSetReplacement[i] = false;
          forceLedUpdate(currentTick);
        }
      } else {
        // Safety: pending target no longer has loop data, cancel it.
        slotStateMachine.clearPendingSlotSwitch(i);
        pendingEnabledSetReplacement[i] = false;
      }
    }

    if (pendingStop[i]) {
      stopRecordingTrack(i);
      pendingStop[i] = false;
    }

    bool audible = isTrackAudible(i);
    uint32_t playTick = tracks[i].getEffectivePlaybackTick(currentTick);
    uint8_t activeSlot = tracks[i].getActiveLoopIndex();

    // Primary (active) slot playback.
    if (slotEnabled[i][activeSlot] && !slotMuted[i][activeSlot]) {
      tracks[i].playMidiEvents(playTick, audible);
    }

    // Additional enabled slots.
    for (uint8_t s = 0; s < Config::MAX_LOOPS_PER_TRACK; ++s) {
      if (s == activeSlot) continue;
      if (slotEnabled[i][s] && !slotMuted[i][s]) {
        tracks[i].playMidiEventsForSlot(s, playTick, audible);
      }
    }
  }
  
}

void TrackManager::refreshTrackAndLoopSelectLeds() {
  if (!ledManager) return;
  static constexpr uint8_t VEL_SELECTED_SLOT = 127;

  bool trackHasData[Config::NUM_TRACKS];
  for (uint8_t i = 0; i < Config::NUM_TRACKS; i++) {
    trackHasData[i] = false;
    for (uint8_t s = 0; s < Config::MAX_LOOPS_PER_TRACK; ++s) {
      if (tracks[i].hasDataInSlot(s)) {
        trackHasData[i] = true;
        break;
      }
    }
  }

  const uint8_t focusSlot = getSelectedSlotIndex(selectedTrack);
  uint8_t slotVelocities[Config::MAX_LOOPS_PER_TRACK] = {0};
  Track& st = tracks[selectedTrack];

  for (uint8_t s = 0; s < Config::MAX_LOOPS_PER_TRACK; ++s) {
    const bool focus = (s == focusSlot);
    const bool hasData = st.hasDataInSlot(s);

    SlotOpState opState = st.getSlotOpState(s);
    if (opState == SlotOpState::SLOT_OP_RECORDING || opState == SlotOpState::SLOT_OP_OVERDUBBING) {
      slotVelocities[s] = 96;
      continue;
    }
    if (isRecordingQueued(selectedTrack, s)) {
      slotVelocities[s] = 96;
      continue;
    }

    if (!hasData) {
      const bool armedHere = (st.getActiveLoopIndex() == s && st.isArmed());
      if (armedHere) {
        slotVelocities[s] = 80;
      } else if (focus) {
        slotVelocities[s] = 40;
      } else {
        slotVelocities[s] = 0;
      }
      continue;
    }

    const bool enabled = slotEnabled[selectedTrack][s];
    const bool mutedSlot = slotMuted[selectedTrack][s];
    if (!enabled) {
      slotVelocities[s] = 32;
    } else if (mutedSlot) {
      slotVelocities[s] = 16;
    } else if (st.isPlaying() || st.isOverdubbing()) {
      slotVelocities[s] = 48;
    } else {
      slotVelocities[s] = 32;
    }
  }

  // UI rule: exactly one selected slot highlight.
  if (focusSlot < Config::MAX_LOOPS_PER_TRACK) {
    slotVelocities[focusSlot] = VEL_SELECTED_SLOT;
  }

  ledManager->updateTrackSelectLeds(selectedTrack, trackHasData, focusSlot, slotVelocities);
}

// Called from main loop (not clock path) - decouples LED updates from playback timing
void TrackManager::updateLedsDeferred() {
  if (!ledManager) return;
  Track& selTrack = getSelectedTrack();
  const uint8_t displaySlot = getSelectedSlotIndex(selectedTrack);
  uint32_t currentTick = clockManager.getCurrentTick();
  // LEDs for a slot use global transport phase in that slot's loop, except when jam playback
  // is driving the active loop — then use the effective tick so the grid matches what you hear.
  uint32_t ledPhaseTick = currentTick;
  if (selTrack.isJamPlaybackActive() && selTrack.isJamming() &&
      displaySlot == selTrack.getActiveLoopIndex()) {
    ledPhaseTick = selTrack.getEffectivePlaybackTick(currentTick);
  }
  ledManager->updateLeds(selTrack, ledPhaseTick, displaySlot);
  if (selTrack.getLoopLengthForSlot(displaySlot) > 0) {
    ledManager->updateCurrentTick(selTrack, ledPhaseTick, displaySlot);
  }
  refreshTrackAndLoopSelectLeds();
}

// --- LED Management ---

void TrackManager::updateLeds(uint32_t currentTick) {
  if (ledManager) {
    ledManager->updateLeds(getSelectedTrack(), currentTick, getSelectedSlotIndex(selectedTrack));
  }
}

void TrackManager::forceLedUpdate(uint32_t currentTick) {
  if (ledManager) {
    ledManager->forceUpdate(getSelectedTrack(), currentTick, getSelectedSlotIndex(selectedTrack));
    refreshTrackAndLoopSelectLeds();
  }
}

void TrackManager::clearLeds() {
  if (ledManager) {
    ledManager->clearAllLeds();  // This now also clears the current tick indicator
  }
}

void TrackManager::reclaimUnreferencedDisabledPasses() {
  for (uint8_t trackIndex = 0; trackIndex < Config::NUM_TRACKS; ++trackIndex) {
    Track& track = tracks[trackIndex];
    PassReferenceSet refs{};
    collectReferencedPasses(track.getGlobalUndoStack(), refs);
    for (uint8_t slotIndex = 0; slotIndex < Config::MAX_LOOPS_PER_TRACK; ++slotIndex) {
      track.getLoop(slotIndex).reclaimUnreferencedDisabledPasses(refs.slots[slotIndex]);
    }
  }
}

