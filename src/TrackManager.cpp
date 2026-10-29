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
#include "Utils/DebugSessionCapture.h"
#include "Utils/DisplayWindowUtils.h"

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
#if defined(SESSION_CAPTURE)
  // One external-pool admission sample before and after boot staging prewarm.
  // This is the only pre-transport window where the smalloc free walk is safe.
  DebugSessionCapture::capturePrewarmMemory("before");
#endif
  for (uint8_t t = 0; t < Config::NUM_TRACKS; ++t) {
    Track& track = tracks[t];
    // S3.1: the admission predicate and the lease live with the slot's owner, so the boot pass
    // cannot disagree with the record arm about which slots hold a working set.
    track.prewarmBootPlaybackRuntimeSlots();
  }
#if defined(SESSION_CAPTURE)
  DebugSessionCapture::capturePrewarmMemory("after");
#endif
}

FLASHMEM void TrackManager::requestDeferredPlaybackPrewarm(uint8_t trackIndex, uint8_t slotIndex) {
  deferredPlaybackPrewarmPending_ = true;
  deferredPlaybackPrewarmTrack_ = trackIndex;
  deferredPlaybackPrewarmSlot_ = slotIndex;
}

FLASHMEM bool TrackManager::hasPendingDeferredPlaybackPrewarm() const {
  return deferredPlaybackPrewarmPending_;
}

FLASHMEM bool TrackManager::takeDeferredPlaybackPrewarm(uint8_t& trackIndex, uint8_t& slotIndex) {
  if (!deferredPlaybackPrewarmPending_) {
    return false;
  }
  trackIndex = deferredPlaybackPrewarmTrack_;
  slotIndex = deferredPlaybackPrewarmSlot_;
  deferredPlaybackPrewarmPending_ = false;
  return true;
}

FLASHMEM bool TrackManager::playbackSetVisualCachesReady() const {
  const uint8_t trackCount = getTrackCount();
  for (uint8_t trackIndex = 0; trackIndex < trackCount; ++trackIndex) {
    const Track& track = tracks[trackIndex];
    if (!track.loopsAllocated()) {
      continue;
    }
    const uint8_t slot = getActiveLoopIndex(trackIndex);
    const Loop& loop = track.getLoop(slot);
    if (!loop.visualCacheFirstWindowReady(DisplayWindowUtils::kMaxDetailedWindowBars)) {
      return false;
    }
  }
  return true;
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
    // STOPPING still owns frozen capture state and continues audible playback.
    // It must therefore keep the same cross-track maintenance exclusions as
    // RECORDING and OVERDUBBING until its continuation reaches a destination.
    if (tracks[i].isRecording() || tracks[i].isOverdubbing() ||
        tracks[i].hasPendingDeferredOverdubStop()) {
      return true;
    }
  }
  return false;
}

FLASHMEM __attribute__((noinline)) bool TrackManager::backgroundSnapshotWorkAllowed() const {
  // Unlike the mode-wide capture gate, this one existing Snapshot opportunity
  // may serve a stopped RecordPublished job whose queued root is its retirement
  // prerequisite. Any live capture or other pending stop still excludes it.
  for (uint8_t i = 0; i < Config::NUM_TRACKS; ++i) {
    const Track& track = tracks[i];
    if (track.isRecording() || track.isOverdubbing() ||
        (track.hasPendingDeferredOverdubStop() &&
         !track.canServiceBackgroundSnapshotOwnerTurn())) return false;
  }
  return true;
}

bool TrackManager::anyPlayingMidiDrainAfterOverdubStop() const {
  for (uint8_t i = 0; i < Config::NUM_TRACKS; ++i) {
    if (tracks[i].playingMidiDrainAfterOverdubStopActive()) {
      return true;
    }
  }
  return false;
}

FLASHMEM bool TrackManager::anyLoopPrefixMeasureAfterUndo() const {
  for (uint8_t i = 0; i < Config::NUM_TRACKS; ++i) {
    if (tracks[i].loopPrefixMeasureAfterUndoActive()) {
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
  // A MIDI transport Stop publishes the deferred overdub destination as
  // STOPPED while its cleanup grains continue. Reflect that destination in the
  // status column immediately instead of letting the still-live capture slot
  // operation keep the indicator at O.
  if (!clockManager.isTransportRunning() &&
      (track.getState() == TRACK_OVERDUBBING ||
       track.getState() == TRACK_OVERDUB_STOPPING)) {
    return track.hasDataInSlot(selectedSlot) ? TRACK_STOPPED : TRACK_EMPTY;
  }
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
  // Do not call invalidateForSlotChange here. It runs invalidateDisplayCaches()
  // (clears visualCache.notes) and STOPPED Track::invalidateCaches. 154353 and
  // 161148 then first-painted with DISP visual=0 and paid ~70 s gather+apply.
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
    // A track focus change is not a slot change: the target loop's visual cache belongs to
    // that loop and is revision-checked when read. Clearing it here made every PLAYING track
    // switch pay a fresh gather, so a boot-prewarmed set painted empty after the switch.
    displayManager.onTrackFocusChanged(index, focusSlot);
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
  initializeEdgeEffectsForBoot();
  // Allocate Loop arrays (deferred from Track ctor to avoid static-init crash)
  // Loops allocated in allocateLoopsEarly() at start of setup
  // Set default MIDI output channel per track (track 1 = ch 1, track 2 = ch 2, etc.)
  for (uint8_t i = 0; i < Config::NUM_TRACKS; i++) {
    tracks[i].setMidiChannel(i + 1);
  }
}
