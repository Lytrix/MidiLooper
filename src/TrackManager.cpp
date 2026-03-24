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

TrackManager trackManager;

TrackManager::TrackManager() {
  // Initialize MidiLedManager
  ledManager = new MidiLedManager(midiHandler);
  
  for (uint8_t i = 0; i < Config::NUM_TRACKS; i++) {
    pendingRecord[i] = false;
    pendingRecordQueuedAtTick[i] = UINT32_MAX;
    pendingRecordRefSlot[i] = 0xFF;
    for (uint8_t s = 0; s < Config::MAX_LOOPS_PER_TRACK; ++s) {
      pendingRecordSlot[i][s] = false;
      heldLayerSlot[i][s] = false;
    }
    pendingStop[i] = false;
    muted[i] = false;
    soloed[i] = false;
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

// Recording & Overdubbing ------------------------------------

void TrackManager::startRecordingTrack(uint8_t trackIndex, uint32_t currentTick) {
  if (trackIndex >= Config::NUM_TRACKS) return;
  // Only allow recording if the clock is running (internal or external)
  if (!clockManager.shouldQuantizeRecordStart()) {
    // Arm the track and set pendingRecord so it will start when the clock starts
    tracks[trackIndex].setState(TRACK_ARMED);
    pendingRecord[trackIndex] = true;
    pendingRecordQueuedAtTick[trackIndex] = UINT32_MAX;
    pendingRecordRefSlot[trackIndex] = 0xFF;
    uint8_t slot = tracks[trackIndex].getActiveLoopIndex();
    for (uint8_t s = 0; s < Config::MAX_LOOPS_PER_TRACK; ++s) {
      pendingRecordSlot[trackIndex][s] = false;
    }
    pendingRecordSlot[trackIndex][slot] = true;
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
  StorageManager::saveState(looperState.getLooperState()); // Save after recording
}

void TrackManager::queueRecordingTrack(uint8_t trackIndex, uint8_t slotIndex,
                                       uint8_t refSlotForPhase) {
  if (trackIndex >= Config::NUM_TRACKS || slotIndex >= Config::MAX_LOOPS_PER_TRACK) return;
  pendingRecord[trackIndex] = true;
  pendingRecordQueuedAtTick[trackIndex] = clockManager.getCurrentTick();
  uint8_t ref = refSlotForPhase;
  if (ref < Config::MAX_LOOPS_PER_TRACK) {
    const Loop& r = tracks[trackIndex].getLoop(ref);
    if (r.loopLengthTicks == 0) ref = 0xFF;
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
    pendingRecordRefSlot[trackIndex] = 0xFF;
  }
}

bool TrackManager::isRecordingQueued(uint8_t trackIndex, uint8_t slotIndex) const {
  if (trackIndex >= Config::NUM_TRACKS || slotIndex >= Config::MAX_LOOPS_PER_TRACK) return false;
  return pendingRecordSlot[trackIndex][slotIndex];
}

bool TrackManager::hasQueuedRecordingTrack(uint8_t trackIndex) const {
  return (trackIndex < Config::NUM_TRACKS) ? pendingRecord[trackIndex] : false;
}

void TrackManager::queueStopRecordingTrack(uint8_t trackIndex) {
  if (trackIndex < Config::NUM_TRACKS) pendingStop[trackIndex] = true;
}

void TrackManager::startOverdubbingTrack(uint8_t trackIndex) {
  if (trackIndex < Config::NUM_TRACKS) {
    tracks[trackIndex].startOverdubbing(clockManager.getCurrentTick());
  }
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
    uint8_t targetSlot = 0xFF;
    for (uint8_t s = 0; s < Config::MAX_LOOPS_PER_TRACK; ++s) {
      if (pendingRecordSlot[i][s]) {
        targetSlot = s;
        break;
      }
    }
    if (targetSlot >= Config::MAX_LOOPS_PER_TRACK) continue;

    bool shouldStart = false;
    uint8_t refIdx = pendingRecordRefSlot[i];
    if (refIdx >= Config::MAX_LOOPS_PER_TRACK || refIdx == 0xFF) {
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
    pendingRecordRefSlot[i] = 0xFF;
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
    pendingRecordRefSlot[i] = 0xFF;
    for (uint8_t s = 0; s < Config::MAX_LOOPS_PER_TRACK; ++s) {
      pendingRecordSlot[i][s] = false;
      heldLayerSlot[i][s] = false;
    }
    pendingStop[i] = false;
    t.sendAllNotesOff();
    if (t.isRecording()) {
      t.stopRecordingToStopped(currentTick);
      uint32_t recordedLength = t.getLoopLength();
      if (recordedLength > 0 && masterLoopLength == 0) {
        setMasterLoopLength(recordedLength);
      }
      if (autoAlignEnabled) {
        t.setLoopLength(masterLoopLength);
      }
    } else if (t.isOverdubbing()) {
      t.stopOverdubbingToStopped();
    } else if (t.isPlaying()) {
      t.stopPlaying();
    } else if (t.isArmed()) {
      t.setState(t.hasData() ? TRACK_STOPPED : TRACK_EMPTY);
    }
  }
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
  pendingRecordRefSlot[trackIndex] = 0xFF;
  for (uint8_t s = 0; s < Config::MAX_LOOPS_PER_TRACK; ++s) {
    pendingRecordSlot[trackIndex][s] = false;
  }

  Track& t = tracks[trackIndex];
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
  t.resetPlaybackState(currentTick);
  const Loop& newLoop = t.getLoop(newSlot);
  if (t.hasDataInSlot(newSlot) && newLoop.loopLengthTicks > 0) {
    if (!t.isPlaying()) {
      t.startPlaying(currentTick);
    }
  } else if (t.isPlaying()) {
    t.stopPlaying();
  }
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
    if (pendingStop[i]) {
      stopRecordingTrack(i);
      pendingStop[i] = false;
    }

    bool audible = isTrackAudible(i);
    uint32_t playTick = tracks[i].getEffectivePlaybackTick(currentTick);
    tracks[i].playMidiEvents(playTick, audible);
    uint8_t activeSlot = tracks[i].getActiveLoopIndex();
    for (uint8_t s = 0; s < Config::MAX_LOOPS_PER_TRACK; ++s) {
      if (heldLayerSlot[i][s] && s != activeSlot) {
        tracks[i].playMidiEventsForSlot(s, playTick, audible);
      }
    }
  }
  
}

// Called from main loop (not clock path) - decouples LED updates from playback timing
void TrackManager::updateLedsDeferred() {
  if (!ledManager) return;
  Track& selTrack = getSelectedTrack();
  uint32_t currentTick = clockManager.getCurrentTick();
  uint32_t selTick = selTrack.getEffectivePlaybackTick(currentTick);
  ledManager->updateLeds(selTrack, selTick);
  if (selTrack.getLoopLength() > 0) {
    ledManager->updateCurrentTick(selTrack, selTick);
  }
  // Track row LEDs (60-67) and loop row LEDs (50-57) for selected track
  bool trackHasData[Config::NUM_TRACKS];
  for (uint8_t i = 0; i < Config::NUM_TRACKS; i++) {
    trackHasData[i] = tracks[i].hasData();
  }
  uint8_t activeIdx = getActiveLoopIndex(selectedTrack);
  bool slotHasData[Config::MAX_LOOPS_PER_TRACK];
  for (uint8_t i = 0; i < Config::MAX_LOOPS_PER_TRACK; i++) {
    slotHasData[i] = tracks[selectedTrack].hasDataInSlot(i);
  }
  ledManager->updateTrackSelectLeds(selectedTrack, trackHasData, activeIdx, slotHasData);
}

// --- LED Management ---

void TrackManager::updateLeds(uint32_t currentTick) {
  if (ledManager) {
    ledManager->updateLeds(getSelectedTrack(), currentTick);
  }
}

void TrackManager::forceLedUpdate(uint32_t currentTick) {
  if (ledManager) {
    ledManager->forceUpdate(getSelectedTrack(), currentTick);
  }
}

void TrackManager::clearLeds() {
  if (ledManager) {
    ledManager->clearAllLeds();  // This now also clears the current tick indicator
  }
}

