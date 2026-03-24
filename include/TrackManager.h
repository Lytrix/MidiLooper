//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#ifndef TRACKMANAGER_H
#define TRACKMANAGER_H

#include <Arduino.h>
#include "Globals.h"
#include "Track.h"
#include "ClockManager.h"
#include "MidiLedManager.h"

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
  void stopRecordingTrack(uint8_t trackIndex);
  /// Queue recording into slotIndex. refSlotForPhase = 0..n-1 uses that slot's loop wrap for start time; 0xFF = next bar line.
  void queueRecordingTrack(uint8_t trackIndex, uint8_t slotIndex, uint8_t refSlotForPhase = 0xFF);
  void clearQueuedRecordingTrack(uint8_t trackIndex, uint8_t slotIndex);
  bool isRecordingQueued(uint8_t trackIndex, uint8_t slotIndex) const;
  bool hasQueuedRecordingTrack(uint8_t trackIndex) const;
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
  void setActiveLoopIndex(uint8_t trackIndex, uint8_t index);
  void setLayeredSlotHeld(uint8_t trackIndex, uint8_t slotIndex, bool held);

  // --- LED Management ---
  void updateLedsDeferred();   // Call from main loop - decoupled from clock path
  void updateLeds(uint32_t currentTick);
  void forceLedUpdate(uint32_t currentTick);
  void clearLeds();

private:
  Track tracks[Config::NUM_TRACKS];
  MidiLedManager* ledManager;  // LED controller for Droid B32

  uint8_t selectedTrack = 0;
  bool autoAlignEnabled = false;
  uint32_t masterLoopLength = 0;

  bool muted[Config::NUM_TRACKS] = {false};
  bool soloed[Config::NUM_TRACKS] = {false};
  bool pendingRecord[Config::NUM_TRACKS] = {false};
  bool pendingRecordSlot[Config::NUM_TRACKS][Config::MAX_LOOPS_PER_TRACK] = {{false}};
  /// Clock tick when queueRecordingTrack was last called for this track (skip same-tick quantized start).
  uint32_t pendingRecordQueuedAtTick[Config::NUM_TRACKS];
  /// Slot index whose loop phase defines punch-in instant; 0xFF means use global bar boundary only.
  uint8_t pendingRecordRefSlot[Config::NUM_TRACKS];
  bool pendingStop[Config::NUM_TRACKS] = {false};
  bool heldLayerSlot[Config::NUM_TRACKS][Config::MAX_LOOPS_PER_TRACK] = {{false}};

  //friend class UI; // Optional: if you have a UI or debug class needing internal access
};

extern TrackManager trackManager;

#endif // TRACKMANAGER_H
