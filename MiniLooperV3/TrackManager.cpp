#include "Globals.h"
#include "ClockManager.h"
#include "TrackManager.h"
#include "Logger.h"

TrackManager trackManager;

TrackManager::TrackManager() {
  for (uint8_t i = 0; i < Config::NUM_TRACKS; i++) {
    pendingRecord[i] = false;
    pendingStop[i] = false;
    muted[i] = false;
    soloed[i] = false;
  }
  autoAlignEnabled = true;
  masterLoopLength = 0;
}


// Recording & Overdubbing ------------------------------------

void TrackManager::startRecordingTrack(uint8_t trackIndex, uint32_t currentTick) {
  if (trackIndex >= Config::NUM_TRACKS) return;
  tracks[trackIndex].startRecording(currentTick);
}

void TrackManager::stopRecordingTrack(uint8_t trackIndex) {
  if (trackIndex >= Config::NUM_TRACKS) return;

  uint32_t recordedLength = tracks[trackIndex].getLength();
  tracks[trackIndex].stopRecording(clockManager.getCurrentTick());

  if (masterLoopLength == 0) {
    setMasterLoopLength(recordedLength);  // First loop sets master length
  }

  if (autoAlignEnabled) {
    tracks[trackIndex].setLength(masterLoopLength);
  }
}

void TrackManager::queueRecordingTrack(uint8_t trackIndex) {
  if (trackIndex < Config::NUM_TRACKS) pendingRecord[trackIndex] = true;
}

void TrackManager::queueStopRecordingTrack(uint8_t trackIndex) {
  if (trackIndex < Config::NUM_TRACKS) pendingStop[trackIndex] = true;
}

void TrackManager::overdubTrack(uint8_t trackIndex) {
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
  return anyTrackSoloed() ? soloed[trackIndex] : !muted[trackIndex];
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
  return (trackIndex < Config::NUM_TRACKS) ? tracks[trackIndex].getLength() : 0;
}

void TrackManager::setSelectedTrack(uint8_t index) {
  if (index < Config::NUM_TRACKS) selectedTrack = index;
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
  // Called from ClockManager.internalUpdateClockTick and updateMidiCLockTick
  for (uint8_t i = 0; i < Config::NUM_TRACKS; i++) {
    if (pendingRecord[i]) {
      startRecordingTrack(i, currentTick);
      pendingRecord[i] = false;
    }
    if (pendingStop[i]) {
      stopRecordingTrack(i);
      pendingStop[i] = false;
    }
    bool audible = isTrackAudible(i);
    tracks[i].playMidiEvents(currentTick, audible);
  }
}
