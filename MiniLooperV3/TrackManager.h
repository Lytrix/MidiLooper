// TrackManager.h

#ifndef TRACKMANAGER_H
#define TRACKMANAGER_H

#include "Globals.h"
#include <Arduino.h>
#include "Track.h"

class TrackManager {
public:
  TrackManager();

  void updateAllTracks(uint32_t currentTick);

  void startRecordingTrack(uint8_t trackIndex, uint32_t currentTick);
  void stopRecordingTrack(uint8_t trackIndex);

  void queueRecordingTrack(uint8_t trackIndex);
  void handleQuantizedStart(uint32_t currentTick);
  
  void queueStopRecordingTrack(uint8_t trackIndex);
  void handleQuantizedStop(uint32_t currentTick);

  void startPlayingTrack(uint8_t trackIndex);
  void stopPlayingTrack(uint8_t trackIndex);

  void overdubTrack(uint8_t trackIndex);

  void clearTrack(uint8_t trackIndex);

  void muteTrack(uint8_t trackIndex);
  void unmuteTrack(uint8_t trackIndex);
  void toggleMuteTrack(uint8_t trackIndex);

  void soloTrack(uint8_t trackIndex);
  void unsoloTrack(uint8_t trackIndex);

  bool isTrackAudible(uint8_t trackIndex) const;
  bool anyTrackSoloed() const;

  // Set fixed length for other tracks
  void enableAutoAlign(bool enabled);
  bool isAutoAlignEnabled() const;

  void setMasterLoopLength(uint32_t length);
  uint32_t getMasterLoopLength() const;

  bool autoAlignEnabled;
  uint32_t masterLoopLength;

  TrackState getTrackState(uint8_t trackIndex) const;
  uint32_t getTrackLength(uint8_t trackIndex) const;

  Track& getTrack(uint8_t trackIndex);      
  uint8_t getTrackCount() const;


  bool pendingRecord[NUM_TRACKS];
  bool pendingStop[NUM_TRACKS];

private:
  Track tracks[NUM_TRACKS];
  bool muted[NUM_TRACKS];
  bool soloed[NUM_TRACKS];
  
};

extern TrackManager trackManager;

#endif
