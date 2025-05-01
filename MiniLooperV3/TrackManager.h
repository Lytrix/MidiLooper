#ifndef TRACKMANAGER_H
#define TRACKMANAGER_H

#include <Arduino.h>
#include "Globals.h"
#include "Track.h"
#include "ClockManager.h"

class TrackManager {
public:
  TrackManager();
  void setup();  // Initialize track manager state

  // --- Track Selection ---
  void setSelectedTrack(uint8_t index);
  uint8_t getSelectedTrackIndex();
  Track& getSelectedTrack();
  Track& getTrack(uint8_t index);
  uint8_t getTrackCount() const;

  // --- Track Updates ---
  void update(uint32_t currentTick);  // Update all tracks and handle pending operations
  void updateAllTracks(uint32_t currentTick);

  // --- Recording ---
  void startRecordingTrack(uint8_t trackIndex, uint32_t currentTick);
  void stopRecordingTrack(uint8_t trackIndex);
  void queueRecordingTrack(uint8_t trackIndex);
  void queueStopRecordingTrack(uint8_t trackIndex);
  void handleQuantizedStart(uint32_t currentTick);
  void handleQuantizedStop(uint32_t currentTick);

  // --- Playback / Overdub ---
  void startPlayingTrack(uint8_t trackIndex);
  void stopPlayingTrack(uint8_t trackIndex);
  void overdubTrack(uint8_t trackIndex);
  void clearTrack(uint8_t trackIndex);

  // --- Mute / Solo ---
  void muteTrack(uint8_t trackIndex);
  void unmuteTrack(uint8_t trackIndex);
  void toggleMuteTrack(uint8_t trackIndex);
  void soloTrack(uint8_t trackIndex);
  void unsoloTrack(uint8_t trackIndex);
  bool isTrackAudible(uint8_t trackIndex) const;
  bool anyTrackSoloed() const;

  // --- Loop Length / Sync ---
  void enableAutoAlign(bool enabled);
  bool isAutoAlignEnabled() const;
  void setMasterLoopLength(uint32_t length);
  uint32_t getMasterLoopLength() const;

  // --- State Accessors ---
  TrackState getTrackState(uint8_t trackIndex) const;
  uint32_t getTrackLength(uint8_t trackIndex) const;

private:
  Track tracks[Config::NUM_TRACKS];

  uint8_t selectedTrack = 0;
  bool autoAlignEnabled = true;
  uint32_t masterLoopLength = 0;

  bool muted[Config::NUM_TRACKS] = {false};
  bool soloed[Config::NUM_TRACKS] = {false};
  bool pendingRecord[Config::NUM_TRACKS] = {false};
  bool pendingStop[Config::NUM_TRACKS] = {false};

  //friend class UI; // Optional: if you have a UI or debug class needing internal access
};

extern TrackManager trackManager;

#endif // TRACKMANAGER_H
