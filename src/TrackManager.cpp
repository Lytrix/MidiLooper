//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "Globals.h"
#include <cstdint>
#include "ClockManager.h"
#include "TrackManager.h"
#include "StorageManager.h"
#include "LooperState.h"
#include "Logger.h"
#include "EditManager.h"
#include "DisplayManager.h"
#include "TrackDisplayState.h"

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
      if (!isSlotEnabled(t, s) && !track.loopForSlot(s).hasCommittedPasses()) {
        continue;
      }
      track.prewarmPlaybackForSlot(s);
    }
  }
}

void TrackManager::prewarmSelectedDisplayVisualCache() {
  const uint8_t trackIndex = getSelectedTrackIndex();
  Track& track = getTrack(trackIndex);
  const uint8_t slot = getSelectedSlotIndex(trackIndex);
  Loop& loop = track.getLoop(slot);
  if (!loop.hasCommittedPasses() && loop.loopLengthTicks == 0) {
    return;
  }
  // PLAYING / stop tail: idle maintenance owns rebuild — read stale notes until then.
  if (track.isPlaying() || track.isStoppedRecording()) {
    return;
  }
  if (loop.shouldAvoidFullVisualRebuild(loop.loopLengthTicks)) {
    loop.rebuildVisualCacheIdleSlice(4, 0);
    return;
  }
  loop.ensureVisualCacheBuilt();
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

bool TrackManager::anyTrackRecordingOrOverdubbing() const {
  for (uint8_t i = 0; i < Config::NUM_TRACKS; ++i) {
    if (tracks[i].isRecording() || tracks[i].isOverdubbing()) {
      return true;
    }
  }
  return false;
}

bool TrackManager::anyPlayingMidiDrainAfterOverdubStop() const {
  for (uint8_t i = 0; i < Config::NUM_TRACKS; ++i) {
    if (tracks[i].playingMidiDrainAfterOverdubStopActive()) {
      return true;
    }
  }
  return false;
}

bool TrackManager::isSelectedTrack(const Track& track) const {
  return &track == &tracks[selectedTrack];
}

TrackState TrackManager::getTrackState(uint8_t trackIndex) const {
  if (trackIndex >= Config::NUM_TRACKS) return TRACK_STOPPED;
  const Track& track = tracks[trackIndex];
  const uint8_t selectedSlot = slotStateMachine.getSelectedSlotIndex(trackIndex);
  return resolveDisplayTrackState(
      track.getState(), track.getSlotOpState(selectedSlot), track.hasDataInSlot(selectedSlot),
      track.hasCommittedPassesInSlot(selectedSlot), pendingRecord[trackIndex],
      isRecordingQueued(trackIndex, selectedSlot));
}

uint32_t TrackManager::getTrackLength(uint8_t trackIndex) const {
  return (trackIndex < Config::NUM_TRACKS) ? tracks[trackIndex].getLoopLength() : 0;
}

void TrackManager::onBootSlotLoadComplete() {
  const uint8_t trackIdx = selectedTrack;
  const uint8_t previewSlot = getPreviewSlotIndex(trackIdx);
  displayManager.invalidateForSlotChange(trackIdx, previewSlot, previewSlot);
  editManager.reenterEditSessionForFocusChange(tracks[trackIdx], previewSlot);
  forceMidiLedUpdate(clockManager.getCurrentTick());
}

void TrackManager::beginBootLoad() {
  bootLoadInProgress_ = true;
}

void TrackManager::endBootLoad() {
  bootLoadInProgress_ = false;
}

void TrackManager::setSelectedTrack(uint8_t index) {
  if (index >= Config::NUM_TRACKS) {
    return;
  }
  const bool trackChanged = (index != selectedTrack);
  if (trackChanged) {
    editManager.beforeSelectedTrackChange(tracks[selectedTrack]);
  }
  selectedTrack = index;
  if (!bootLoadInProgress_) {
    forceMidiLedUpdate(clockManager.getCurrentTick());
  }
  if (trackChanged) {
    editManager.onTrackChanged(tracks[selectedTrack]);
    const uint8_t focusSlot = getSelectedSlotIndex(index);
    StorageManager::prioritizeLoopSlotRestoreForFocus(index, focusSlot);
    displayManager.invalidateForSlotChange(index, focusSlot, focusSlot);
  }
  if (!bootLoadInProgress_ && trackChanged) {
    StorageManager::requestWorkspaceFooterPersistWhenSafe();
    StorageManager::requestDeferredSaveState(looperState.getLooperState());
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
