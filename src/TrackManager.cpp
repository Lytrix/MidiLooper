//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "Globals.h"
#include "ClockManager.h"
#include "TrackManager.h"
#include "StorageManager.h"
#include "LooperState.h"
#include "Logger.h"
#include "MidiHandler.h"

TrackManager trackManager;

TrackManager::TrackManager() {
  // Initialize MidiLedManager
  ledManager = new MidiLedManager(midiHandler);
  
  for (uint8_t i = 0; i < Config::NUM_TRACKS; i++) {
    pendingRecord[i] = false;
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

// Recording & Overdubbing ------------------------------------

void TrackManager::startRecordingTrack(uint8_t trackIndex, uint32_t currentTick) {
  if (trackIndex >= Config::NUM_TRACKS) return;
  // Only allow recording if the clock is running (internal or external)
  if (!clockManager.isClockRunning()) {
    // Arm the track and set pendingRecord so it will start when the clock starts
    tracks[trackIndex].setState(TRACK_ARMED);
    pendingRecord[trackIndex] = true;
    logger.log(CAT_TRACK, LOG_INFO, "Track %d armed, waiting for clock to start recording", trackIndex);
    return;
  }
  tracks[trackIndex].startRecording(currentTick);
  tracks[trackIndex].isArmed();  // sets state to TRACK_ARMED and logs
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

void TrackManager::queueRecordingTrack(uint8_t trackIndex) {
  if (trackIndex < Config::NUM_TRACKS) pendingRecord[trackIndex] = true;
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

void TrackManager::handleQuantizedStart(uint32_t currentTick) {
  if (currentTick % ticksPerBar != 0) return;

  for (uint8_t i = 0; i < Config::NUM_TRACKS; i++) {
    if (pendingRecord[i]) {
      startRecordingTrack(i, currentTick);
      pendingRecord[i] = false;
    }
  }
}

void TrackManager::handleQuantizedStop(uint32_t currentTick) {
  if (currentTick % ticksPerBar != 0) return;

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

bool TrackManager::anyTrackSoloed() const {
  for (uint8_t i = 0; i < Config::NUM_TRACKS; i++) {
    if (soloed[i]) return true;
  }
  return false;
}

bool TrackManager::isTrackAudible(uint8_t trackIndex) const {
  if (trackIndex >= Config::NUM_TRACKS) return false;
  return !tracks[trackIndex].isMuted();
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
  return (trackIndex < Config::NUM_TRACKS) ? tracks[trackIndex].getState() : TRACK_STOPPED;
}

uint32_t TrackManager::getTrackLength(uint8_t trackIndex) const {
  return (trackIndex < Config::NUM_TRACKS) ? tracks[trackIndex].getLoopLength() : 0;
}

void TrackManager::setSelectedTrack(uint8_t index) {
  if (index < Config::NUM_TRACKS) {
    selectedTrack = index;
    // Force LED update when track changes
    forceLedUpdate(clockManager.getCurrentTick());
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
  // Tracks are already initialized in their constructor
  // No additional setup needed
}

void TrackManager::updateAllTracks(uint32_t currentTick) {
  // Called from ClockManager.internalUpdateClockTick and updateMidiClockTick
  for (uint8_t i = 0; i < Config::NUM_TRACKS; i++) {
    if (pendingRecord[i]) {
      // Wait for the next bar boundary
      //if (currentTick == 0 || (currentTick % Track::getTicksPerBar()) == 0) {
      if (currentTick == 0 || (currentTick % Track::getTicksPerBar()) == 0) {
        startRecordingTrack(i, currentTick);
        pendingRecord[i] = false;
      }
    }

    if (pendingStop[i]) {
      stopRecordingTrack(i);
      pendingStop[i] = false;
    }

    bool audible = isTrackAudible(i);
    tracks[i].playMidiEvents(currentTick, audible);
  }
  
  // Update LEDs for the selected track
  updateLeds(currentTick);
  
  // Update current tick indicator
  if (ledManager && masterLoopLength > 0) {
    ledManager->updateCurrentTick(currentTick, masterLoopLength);
  }
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

