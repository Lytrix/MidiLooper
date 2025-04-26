// Track.h

#ifndef TRACK_H
#define TRACK_H

#include <Arduino.h>
#include <vector>

enum TrackState {
  TRACK_STOPPED,
  TRACK_RECORDING,
  TRACK_PLAYING,
  TRACK_OVERDUBBING
};

enum MidiEventType {
  EVENT_NOTE_ON,
  EVENT_NOTE_OFF,
  EVENT_CONTROL_CHANGE,
  EVENT_PITCH_BEND,
  EVENT_AFTERTOUCH
};

struct MidiEvent {
  uint32_t tick;
  MidiEventType type;
  byte channel;
  byte data1;
  byte data2;
};

class Track {
public:
  Track();

  void process(uint32_t currentTick, bool inAudible);
  void startRecording(uint32_t startTick);
  void stopRecording();
  void startPlaying();
  void stopPlaying();
  void startOverdubbing();
  void clear();

  void recordMidiEvent(MidiEventType type, byte channel, byte data1, byte data2, uint32_t currentTick);
  void playEvents(uint32_t currentTick, bool isAudible);
  

  uint32_t getLength() const;
  
  void setLength(uint32_t ticks);

  void sendMidiEvent(const MidiEvent& evt);
  TrackState getState() const;
private:
  TrackState state;
  uint32_t startTick;
  uint32_t lengthTicks; // how long the loop is (only valid after recording)
  std::vector<MidiEvent> events;
  uint32_t lastTickPlayed;

  
};
extern Track track1;

#endif
