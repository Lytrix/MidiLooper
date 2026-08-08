//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "TrackManager.h"

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
