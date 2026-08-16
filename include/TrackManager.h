//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#ifndef TRACKMANAGER_H
#define TRACKMANAGER_H

#include <Arduino.h>
#include "Globals.h"
#include "Track.h"
#include "ClockManager.h"
#include "MidiLedManager.h"
#include "SlotStateMachine.h"
#include "PassReclaim.h"
#include "Utils/MemoryPressureLevel.h"

/// Phase 1 playback policy for slot selection (queued start remains caller-composed).
enum class SyncPlayback : uint8_t { No = 0, Yes = 1 };

/**
 * @class TrackManager
 * @brief Oversees multiple Track instances, handling selection, recording, playback,
 * overdubbing, mute/solo, and loop synchronization.
 *
 * The TrackManager maintains an array of Track objects (one per MIDI track),
 * provides methods to select the active track, start/stop recording, playback,
 * overdub, clear, and manage mute/solo status. It also supports quantized
 * start/stop operations, auto-align to master loop length, and querying track
 * states and lengths.
 *
 * Timing and clock interaction:
 *   - updateAllTracks(currentTick) is driven by the global clock (ClockManager);
 *     it must be called regularly with the current tick to advance each Track's
 *     state machine (playback, overdub, pending NoteOffs, quantized events).
 *   - TrackManager consumes the global tick to schedule recording start/stop,
 *     playback loops, overdub timing, and note finalization in each Track.
 */
class TrackManager {
public:
  TrackManager();
  ~TrackManager();  // Destructor to clean up LED manager
  void setup();
  void allocateLoopsEarly();  // Call at start of setup() before other subsystems consume heap  // Initialize track manager state
  /// Preallocate per-slot playback runtime and playback order for enabled/data slots.
  void prewarmPlaybackRuntime();
  /// Build visual cache for the selected display slot (cold path; defer from setup).
  void prewarmSelectedDisplayVisualCache();

  // --- Track Selection ---
  void setSelectedTrack(uint8_t index);
  uint8_t getSelectedTrackIndex();
  Track& getSelectedTrack();
  Track& getTrack(uint8_t index);
  uint8_t getTrackCount() const;

  // --- Track Updates ---
  void updateAllTracks(uint32_t currentTick);  // Update all tracks and handle pending operations
  void advanceJamTicks(uint32_t delta);         // Advance jamTick for all jam-active tracks

  // --- Recording ---
  void startRecordingTrack(uint8_t trackIndex, uint32_t currentTick);
  /// Free non-capturing tracks' playback merge buffers before capture (multi-track headroom).
  void releaseBackgroundPlaybackMergedMidiEventsMemory(uint8_t captureTrackIndex);
  void stopRecordingTrack(uint8_t trackIndex);
  /// Queue recording into slotIndex. refSlotForPhase is a valid slot index for loop-phase punch-in, or
  /// Config::INVALID_LOOP_SLOT for next bar line only.
  void queueRecordingTrack(uint8_t trackIndex, uint8_t slotIndex,
                           uint8_t refSlotForPhase = Config::INVALID_LOOP_SLOT);
  void clearQueuedRecordingTrack(uint8_t trackIndex, uint8_t slotIndex);
  bool isRecordingQueued(uint8_t trackIndex, uint8_t slotIndex) const;
  bool hasQueuedRecordingTrack(uint8_t trackIndex) const;
  /// True when any track is recording, armed, or has a pending record queue.
  bool hasActiveOrPendingCapture() const;
  /// First slot index with a pending record arm/queue, or Config::INVALID_LOOP_SLOT if none.
  uint8_t getQueuedRecordingSlot(uint8_t trackIndex) const;
  /// Clears pending record flags; if track is TRACK_ARMED, returns to STOPPED or EMPTY.
  void cancelPendingRecordArm(uint8_t trackIndex);
  void queueStopRecordingTrack(uint8_t trackIndex);
  void handlePendingRecordStart(uint32_t currentTick);
  void handleQuantizedStop(uint32_t currentTick);
  /// Stop recording/overdub on current slot, select newSlot, then play if that slot has a loop.
  void finalizeCaptureAndSelectSlot(uint8_t trackIndex, uint8_t newSlot, uint32_t currentTick);

  // --- Playback / Overdub ---
  void startPlayingTrack(uint8_t trackIndex);
  void stopPlayingTrack(uint8_t trackIndex);
  void startOverdubbingTrack(uint8_t trackIndex);
  void handleTransportStop();  // Stop all tracks (MIDI Stop) - recording->stopped, overdub->stopped, etc.
  void clearTrack(uint8_t trackIndex);

  // --- Mute / Solo ---
  void muteTrack(uint8_t trackIndex);
  void unmuteTrack(uint8_t trackIndex);
  void toggleMuteTrack(uint8_t trackIndex);
  void soloTrack(uint8_t trackIndex);
  void unsoloTrack(uint8_t trackIndex);
  /// Exclusive solo: if `trackIndex` is already soloed, clears all solo; otherwise clears all and solos only this track.
  void toggleSoloTrack(uint8_t trackIndex);
  bool isTrackAudible(uint8_t trackIndex) const;
  bool anyTrackSoloed() const;
  bool isTrackSoloed(uint8_t trackIndex) const;
  /// True when any track is RECORDING or OVERDUBBING (multi-track capture gating).
  bool anyTrackRecordingOrOverdubbing() const;
  /// True when any track is in the post-overdub PLAYING MIDI-drain window.
  bool anyPlayingMidiDrainAfterOverdubStop() const;
  /// True when @p track is the UI-selected track (slot focus / display owner).
  bool isSelectedTrack(const Track& track) const;
  /// Low+ advisory reclaim — background-first; owners may no-op when unsafe.
  void tryReclaimDerivedViewCachesUnderPressure(MemoryPressureLevel level);

  // --- Loop Length / Sync ---
  void enableAutoAlign(bool enabled);
  bool isAutoAlignEnabled() const;
  void setMasterLoopLength(uint32_t length);
  uint32_t getMasterLoopLength() const;

  // --- State Accessors ---
  TrackState getTrackState(uint8_t trackIndex) const;
  uint32_t getTrackLength(uint8_t trackIndex) const;

  // --- Active loop slot (per track, 0-7) ---
  uint8_t getActiveLoopIndex(uint8_t trackIndex) const;
  uint8_t getPlayingSlotIndex(uint8_t trackIndex) const;
  void setActiveLoopIndex(uint8_t trackIndex, uint8_t index);
  void setLayeredSlotHeld(uint8_t trackIndex, uint8_t slotIndex, bool held);

  // --- Per-slot playback state (Phase: slot toggles) ---
  bool isSlotEnabled(uint8_t trackIndex, uint8_t slotIndex) const;
  bool isSlotMuted(uint8_t trackIndex, uint8_t slotIndex) const;
  void setSlotEnabled(uint8_t trackIndex, uint8_t slotIndex, bool enabled);
  void setSlotMuted(uint8_t trackIndex, uint8_t slotIndex, bool muted);
  void toggleSlotMuted(uint8_t trackIndex, uint8_t slotIndex);
  uint8_t countEnabledSlots(uint8_t trackIndex) const;

  /// Multi-slot selection: during a multi-hold gesture we build a pending enabled set.
  void beginSlotSelectionHold(uint8_t trackIndex, uint8_t slotIndex);
  void endSlotSelectionHold(uint8_t trackIndex, uint8_t slotIndex, uint32_t nowTick);
  /// Clear any in-progress multi-slot hold/commit state for this track.
  void cancelSlotSelectionHold(uint8_t trackIndex);

  /// When true, the next committed `pendingSlotIndex` switch replaces the enabled slot set with only that slot.
  void setPendingEnabledSetReplacement(uint8_t trackIndex, bool enabled);

  void reclaimUnreferencedDisabledPasses(PassReclaimStats* statsOut = nullptr,
                                         bool diagnosticVisibility = false);

  // --- Slot state machine (selected UI focus + pending quantized switch) ---
  uint8_t getSelectedSlotIndex(uint8_t trackIndex) const;
  uint8_t getPreviewSlotIndex(uint8_t trackIndex) const;
  uint8_t getPendingSlotIndex(uint8_t trackIndex) const;
  bool hasPendingSlotSwitch(uint8_t trackIndex) const;
  bool slotHasLoopContent(uint8_t trackIndex, uint8_t slotIndex, bool restoreFromSd);
  uint8_t getSelectedLoopIndex(uint8_t trackIndex) const;
  Loop& getSelectedLoop(uint8_t trackIndex);
  const Loop& getSelectedLoop(uint8_t trackIndex) const;
  Loop& getSelectedLoop(Track& track);
  const Loop& getSelectedLoop(const Track& track) const;
  void setSelectedSlotIndex(uint8_t trackIndex, uint8_t slotIndex,
                            SyncPlayback syncPlayback = SyncPlayback::Yes);
  /// Boot / SD load: write indices without focus lifecycle hooks.
  void loadTransportSlotIndices(uint8_t trackIndex, uint8_t activeSlot, uint8_t selectedSlot);
  void requestSlotSwitch(uint8_t trackIndex,
                          uint8_t slotIndex,
                          SlotQuantization quantization,
                          uint32_t queuedAtTick);
  /// Piano roll + loop LEDs when preview launch is queued or split focus is reaffirmed.
  void refreshPreviewSlotFocus(uint8_t trackIndex, uint8_t slotIndex);
  /// Bar/16th phase slot: playing slot while transport runs, preview slot when stopped.
  uint8_t getMidiLedPhaseSlotIndex(uint8_t trackIndex) const;
  /// After deferred SD slot restores finish — display, LOOP_EDIT faders, loop row LEDs.
  void onBootSlotLoadComplete();
  void clearPendingSlotSwitch(uint8_t trackIndex);
  /// Queue bar/16th playback restart at next grid; clears any pending slot switch (D14).
  void queueBarPlaybackStart(uint8_t trackIndex, int32_t storageTick, uint32_t queuedAtTick);

  // --- LED Management ---
  void updateMidiLedsDeferred();   // Call from main loop - decoupled from clock path
  void updateMidiLeds(uint32_t currentTick);
  void forceMidiLedUpdate(uint32_t currentTick);
  void clearMidiLeds();

  /// Suppress LED/USB side effects while StorageManager applies boot load footer.
  void beginBootLoad();
  void endBootLoad();

private:
  /// Track row + loop row (notes 60–67, 50–57). Used from updateMidiLedsDeferred and forceMidiLedUpdate
  /// because MidiLedManager::updateLeds / clearAllLeds can turn off loop LEDs without this pass.
  void refreshTrackAndLoopSelectMidiLeds();
  Track tracks[Config::NUM_TRACKS];
  MidiLedManager* ledManager;  // LED controller for Droid B32
  SlotStateMachine slotStateMachine;

  uint8_t selectedTrack = 0;
  bool autoAlignEnabled = false;
  uint32_t masterLoopLength = 0;

  bool muted[Config::NUM_TRACKS] = {false};
  bool soloed[Config::NUM_TRACKS] = {false};
  bool pendingRecord[Config::NUM_TRACKS] = {false};
  bool pendingRecordSlot[Config::NUM_TRACKS][Config::MAX_LOOPS_PER_TRACK] = {{false}};
  /// Clock tick when queueRecordingTrack was last called for this track (skip same-tick quantized start).
  uint32_t pendingRecordQueuedAtTick[Config::NUM_TRACKS];
  /// Slot whose loop phase defines punch-in instant, or Config::INVALID_LOOP_SLOT for bar boundary only.
  uint8_t pendingRecordRefSlot[Config::NUM_TRACKS];
  bool pendingStop[Config::NUM_TRACKS] = {false};
  bool heldLayerSlot[Config::NUM_TRACKS][Config::MAX_LOOPS_PER_TRACK] = {{false}};

  // Slot-level playback (multi-slot enable + per-slot mute)
  bool slotEnabled[Config::NUM_TRACKS][Config::MAX_LOOPS_PER_TRACK] = {{false}};
  bool slotMuted[Config::NUM_TRACKS][Config::MAX_LOOPS_PER_TRACK] = {{false}};

  // Multi-hold selection building a pending enabled set (committed on release quantized)
  bool pendingSlotEnabled[Config::NUM_TRACKS][Config::MAX_LOOPS_PER_TRACK] = {{false}};
  bool pendingHoldActive[Config::NUM_TRACKS][Config::MAX_LOOPS_PER_TRACK] = {{false}};
  uint8_t pendingHoldCount[Config::NUM_TRACKS] = {0};
  bool pendingMultiSlotCommit[Config::NUM_TRACKS] = {false};
  uint32_t pendingMultiSlotQueuedAtTick[Config::NUM_TRACKS] = {UINT32_MAX};

  // If set, the next pending slot switch commit replaces enabled set with the target slot only.
  bool pendingEnabledSetReplacement[Config::NUM_TRACKS] = {false};

  bool bootLoadInProgress_ = false;

  //friend class UI; // Optional: if you have a UI or debug class needing internal access
};

extern TrackManager trackManager;

#endif // TRACKMANAGER_H
