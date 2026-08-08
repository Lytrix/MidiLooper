//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "Globals.h"
#include <cstdint>
#include "ClockManager.h"
#include "TrackManager.h"
#include "TrackManagerInternal.h"
#include "StorageManager.h"
#include "LooperState.h"
#include "Logger.h"
#include "MidiHandler.h"
#include "ControlSurfaceManager.h"
#include "EditManager.h"
#include "PassReclaim.h"
#include "DisplayManager.h"
#include "TrackDisplayState.h"
#include "Utils/DebugSessionCapture.h"
#include "Utils/SlotFocusDisplay.h"
#include "Utils/SlotLoopContent.h"
#include "Utils/MemoryPressureLevel.h"

#if defined(__IMXRT1062__)
#define PRESSURE_RECLAIM_MEM FLASHMEM
#else
#define PRESSURE_RECLAIM_MEM
#endif

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

// Recording & Overdubbing — see TrackManagerCaptureQueue.cpp

PRESSURE_RECLAIM_MEM void TrackManager::tryReclaimDerivedViewCachesUnderPressure(MemoryPressureLevel level) {
  if (level < MemoryPressureLevel::Low) {
    return;
  }

  uint8_t trackOrder[Config::NUM_TRACKS];
  for (uint8_t i = 0; i < Config::NUM_TRACKS; ++i) {
    trackOrder[i] = i;
  }
  for (uint8_t i = 0; i + 1 < Config::NUM_TRACKS; ++i) {
    for (uint8_t j = i + 1; j < Config::NUM_TRACKS; ++j) {
      const Track& a = tracks[trackOrder[i]];
      const Track& b = tracks[trackOrder[j]];
      const uint8_t priA = reclaimTrackPriority(trackOrder[i], *this, a);
      const uint8_t priB = reclaimTrackPriority(trackOrder[j], *this, b);
      if (priB < priA) {
        const uint8_t tmp = trackOrder[i];
        trackOrder[i] = trackOrder[j];
        trackOrder[j] = tmp;
      }
    }
  }

  for (uint8_t orderIdx = 0; orderIdx < Config::NUM_TRACKS; ++orderIdx) {
    const uint8_t trackIndex = trackOrder[orderIdx];
    Track& track = tracks[trackIndex];
    const bool selected = isSelectedTrack(track);
    const bool noteEditBlocksSelected =
        editManager.isNoteEditActive() && selected;

    for (uint8_t slot = 0; slot < Config::MAX_LOOPS_PER_TRACK; ++slot) {
      if (noteEditBlocksSelected && slot == track.getActiveLoopIndex()) {
        continue;
      }
      Loop& loop = track.loopForSlot(slot);
      if (!isSlotEnabled(trackIndex, slot) && !loop.hasCommittedPasses()) {
        continue;
      }
      (void)loop.tryDiscardPassesMaterializedCache();
    }

    (void)track.tryClearCommittedMidiScratch();
    (void)track.tryReleasePlaybackMergedMidiEventsMemory();
  }
}

// Playback Control -------------------------------------------

void TrackManager::startPlayingTrack(uint8_t trackIndex) {
  if (trackIndex < Config::NUM_TRACKS) {
    StorageManager::prioritizeLoopSlotRestoreForFocus(trackIndex,
                                                      tracks[trackIndex].getActiveLoopIndex());
    tracks[trackIndex].startPlaying(clockManager.getCurrentTick());
  }
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
    slotStateMachine.clearPendingSlotSwitch(i);
    pendingEnabledSetReplacement[i] = false;
    tracks[i].clearQueuedPlaybackStart();
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

bool TrackManager::anyTrackRecordingOrOverdubbing() const {
  for (uint8_t i = 0; i < Config::NUM_TRACKS; ++i) {
    if (tracks[i].isRecording() || tracks[i].isOverdubbing()) {
      return true;
    }
  }
  return false;
}

bool TrackManager::isSelectedTrack(const Track& track) const {
  return &track == &tracks[selectedTrack];
}

// Track Info Accessors ---------------------------------------

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

void TrackManager::queueBarPlaybackStart(uint8_t trackIndex, int32_t storageTick,
                                         uint32_t queuedAtTick) {
  if (trackIndex >= Config::NUM_TRACKS) {
    return;
  }
  slotStateMachine.clearPendingSlotSwitch(trackIndex);
  pendingEnabledSetReplacement[trackIndex] = false;
  tracks[trackIndex].queuePlaybackStartAtGrid(storageTick, queuedAtTick);
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
      if (tracks[i].isRecording() || tracks[i].isOverdubbing() || pendingRecord[i]) {
        pendingMultiSlotCommit[i] = false;
        pendingMultiSlotQueuedAtTick[i] = UINT32_MAX;
        for (uint8_t s = 0; s < Config::MAX_LOOPS_PER_TRACK; ++s) {
          pendingSlotEnabled[i][s] = false;
        }
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

      forceMidiLedUpdate(currentTick);
    }

    const bool audible = isTrackAudible(i);
    const uint32_t playTick = tracks[i].getEffectivePlaybackTick(currentTick);

    // Commit before playMidiEvents so lastTickInLoop still reflects the previous tick.
    if (tracks[i].isPlaying() && slotStateMachine.hasPendingSlotSwitch(i) &&
        slotStateMachine.shouldCommitPendingSlotSwitch(i, tracks[i], playTick)) {
      const uint8_t targetSlot = slotStateMachine.getPendingSlotIndex(i);
      if (targetSlot >= Config::MAX_LOOPS_PER_TRACK) {
        slotStateMachine.clearPendingSlotSwitch(i);
        pendingEnabledSetReplacement[i] = false;
      } else if (!tracks[i].hasCommittedPassesInSlot(targetSlot)) {
        // HEADER_READY / SD still loading: keep pending until Commit.
        // hasDataInSlot is true from metadata length alone — launching that empty 64-bar
        // slot then letting LoadLoopJob adopt into the *active* playing loop hard-faults
        // on fast track→slot3 (session_20260718_205809).
        if (!StorageManager::hasLoopSlotPayloadOnSdInRam(i, targetSlot)) {
          logger.info("LoopEnd playback commit cancelled track=%u target=%u (no committed MIDI)",
                      static_cast<unsigned>(i), static_cast<unsigned>(targetSlot));
          slotStateMachine.clearPendingSlotSwitch(i);
          pendingEnabledSetReplacement[i] = false;
        }
      } else if (!tracks[i].isPlaybackMergedMidiEventsReadyForSlot(targetSlot) &&
                 tracks[i].getLoop(targetSlot).shouldAvoidFullVisualRebuild(
                     tracks[i].getLoop(targetSlot).loopLengthTicks)) {
        // Long loop just Committed: do not full-gather on the clock path (210001).
        // Main prewarms after LoadLoopJob Commit; keep pending until window is ready.
      } else if (tracks[i].hasDataInSlot(targetSlot)) {
        const uint8_t previousPlaying = getPlayingSlotIndex(i);
        slotStateMachine.clearPendingSlotSwitch(i);
        // Wire silence before swapping the audible slot (same pattern as overdub→play).
        tracks[i].sendAllNotesOff();
        tracks[i].ensurePlaybackMergedEventsForSlot(targetSlot);
        setActiveLoopIndex(i, targetSlot);
        const Loop& targetLoop = tracks[i].getLoop(targetSlot);
        tracks[i].clearQueuedPlaybackStart();
        tracks[i].queuePlaybackStartAtGrid(static_cast<int32_t>(targetLoop.loopStartTick),
                                            currentTick);
        tracks[i].commitQueuedPlaybackStart(currentTick);
        logger.info("LoopEnd playback commit track=%u %u->%u start=%lu len=%lu",
                    static_cast<unsigned>(i), static_cast<unsigned>(previousPlaying),
                    static_cast<unsigned>(targetSlot),
                    static_cast<unsigned long>(targetLoop.loopStartTick),
                    static_cast<unsigned long>(targetLoop.loopLengthTicks));

        if (i == selectedTrack) {
          if (targetSlot != previousPlaying) {
            displayManager.invalidateForSlotChange(i, previousPlaying, getPreviewSlotIndex(i));
          }
          forceMidiLedUpdate(currentTick);
        }

        // If this slot switch came from a "select single slot" gesture, replace enabled set.
        if (pendingEnabledSetReplacement[i]) {
          for (uint8_t s = 0; s < Config::MAX_LOOPS_PER_TRACK; ++s) {
            slotEnabled[i][s] = (s == targetSlot);
            slotMuted[i][s] = false;
          }
          pendingEnabledSetReplacement[i] = false;
          forceMidiLedUpdate(currentTick);
        }
      } else {
        // Safety: pending target no longer has loop data, cancel it.
        logger.info("LoopEnd playback commit cancelled track=%u target=%u (no RAM data)",
                    static_cast<unsigned>(i), static_cast<unsigned>(targetSlot));
        slotStateMachine.clearPendingSlotSwitch(i);
        pendingEnabledSetReplacement[i] = false;
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

