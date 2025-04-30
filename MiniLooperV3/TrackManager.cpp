// TrackManager.cpp

#include "Globals.h"
#include "ClockManager.h"
#include "TrackManager.h"

TrackManager trackManager;  // initiate class to reuse in ino file.

TrackManager::TrackManager() {
  for (uint8_t i = 0; i < NUM_TRACKS; i++) {
    pendingRecord[i] = false;
    pendingStop[i] = false;
    muted[i] = false;
    soloed[i] = false;
  }
  autoAlignEnabled = true;
  masterLoopLength = 0;
}

void TrackManager::startRecordingTrack(uint8_t trackIndex, uint32_t currentTick) {
    tracks[trackIndex].startRecording(currentTick);
}

void TrackManager::stopRecordingTrack(uint8_t trackIndex) {
  if (trackIndex >= NUM_TRACKS) return;

  uint32_t recordedLength = tracks[trackIndex].getLength();

  if (trackIndex < NUM_TRACKS) {
    tracks[trackIndex].stopRecording(clockManager.getCurrentTick());
  }
  if (masterLoopLength == 0) {
    // First recording ever → become the master loop length
    setMasterLoopLength(recordedLength);
  }

  if (autoAlignEnabled) {
    tracks[trackIndex].setLength(masterLoopLength);
  }
}

void TrackManager::queueRecordingTrack(uint8_t trackIndex) {
  if (trackIndex < NUM_TRACKS) {
    pendingRecord[trackIndex] = true;
  }
}

void TrackManager::queueStopRecordingTrack(uint8_t trackIndex) {
  if (trackIndex < NUM_TRACKS) {
    pendingStop[trackIndex] = true;
  }
}

void TrackManager::handleQuantizedStart(uint32_t currentTick) {
  if (currentTick % ticksPerBar == 0) { // **Start of Bar!**
    for (uint8_t i = 0; i < NUM_TRACKS; i++) {
      if (pendingRecord[i]) {
        startRecordingTrack(i, currentTick);
        pendingRecord[i] = false;
      }
    }
  }
}

void TrackManager::handleQuantizedStop(uint32_t currentTick) {
  if (currentTick % ticksPerBar == 0) { // **Start of Bar!**
    for (uint8_t i = 0; i < NUM_TRACKS; i++) {
      if (pendingStop[i]) {
        stopRecordingTrack(i);
        pendingStop[i] = false;
      }
    }
  }
}

void TrackManager::startPlayingTrack(uint8_t trackIndex) {
  if (trackIndex < NUM_TRACKS) {
    tracks[trackIndex].startPlaying();
  }
}

void TrackManager::stopPlayingTrack(uint8_t trackIndex) {
  if (trackIndex < NUM_TRACKS) {
    tracks[trackIndex].stopPlaying();
  }
}

void TrackManager::overdubTrack(uint8_t trackIndex) {
  if (trackIndex < NUM_TRACKS) {
    tracks[trackIndex].startOverdubbing(clockManager.getCurrentTick());
  }
}

void TrackManager::clearTrack(uint8_t trackIndex) {
  if (trackIndex < NUM_TRACKS) {
    tracks[trackIndex].clear();
  }
}

void TrackManager::muteTrack(uint8_t trackIndex) {
  if (trackIndex < NUM_TRACKS) {
    muted[trackIndex] = true;
  }
}

void TrackManager::unmuteTrack(uint8_t trackIndex) {
  if (trackIndex < NUM_TRACKS) {
    muted[trackIndex] = false;
  }
}

void TrackManager::toggleMuteTrack(uint8_t trackIndex) {
  if (trackIndex < NUM_TRACKS) {
    muted[trackIndex] = !muted[trackIndex];
  }
}

void TrackManager::soloTrack(uint8_t trackIndex) {
  if (trackIndex < NUM_TRACKS) {
    soloed[trackIndex] = true;
  }
}

void TrackManager::unsoloTrack(uint8_t trackIndex) {
  if (trackIndex < NUM_TRACKS) {
    soloed[trackIndex] = false;
  }
}

bool TrackManager::anyTrackSoloed() const {
  for (uint8_t i = 0; i < NUM_TRACKS; i++) {
    if (soloed[i]) {
      return true;
    }
  }
  return false;
}

bool TrackManager::isTrackAudible(uint8_t trackIndex) const {
  if (trackIndex >= NUM_TRACKS) return false;

  if (anyTrackSoloed()) {
    return soloed[trackIndex];
  } else {
    return !muted[trackIndex];
  }
}

void TrackManager::updateAllTracks(uint32_t currentTick) {
  for (uint8_t i = 0; i < NUM_TRACKS; i++) {
    bool audible = isTrackAudible(i);
    tracks[i].playEvents(currentTick, audible);
  }
}


// Set master loop length ---------------------
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

// ---------------------

TrackState TrackManager::getTrackState(uint8_t trackIndex) const {
  if (trackIndex < NUM_TRACKS) {
    return tracks[trackIndex].getState();
  }
  return TRACK_STOPPED; // Default fallback
}

uint32_t TrackManager::getTrackLength(uint8_t trackIndex) const {
  if (trackIndex < NUM_TRACKS) {
    return tracks[trackIndex].getLength();
  }
  return 0;
}

void TrackManager::setSelectedTrack(uint8_t index) {
    if (index < NUM_TRACKS) {
        selectedTrack = index;
    }
}

uint8_t TrackManager::getSelectedTrack() {
    return selectedTrack;
}

Track& TrackManager::getSelectedTrack() {
    return tracks[selectedTrack];
}

Track& TrackManager::getTrack(uint8_t index)
{
    return tracks[index];
}

uint8_t TrackManager::getTrackCount() const
{
    return NUM_TRACKS;
}