#ifndef TRACK_H
#define TRACK_H

#include <Arduino.h>
#include <vector>
#include <MIDI.h>
#include <unordered_map>

enum TrackState {
  TRACK_STOPPED,
  TRACK_ARMED,
  TRACK_RECORDING,
  TRACK_STOPPED_RECORDING,
  TRACK_OVERDUBBING,
  TRACK_STOPPED_OVERDUBBING,
  TRACK_PLAYING
};

struct MidiEvent {
  uint32_t tick;
  midi::MidiType type; //get 0x80, 0x90 directly from MIDI.h library
  byte channel;
  byte data1;
  byte data2;
};

struct PendingNote {
  uint32_t startTick;
  uint8_t velocity;
};

// ----------------------------------------------
// Display functions
// ----------------------------------------------
struct NoteEvent {
  uint8_t note;
  uint8_t velocity;
  uint32_t startTick;
  uint32_t endTick;
  // bool isNoteOn;
};

// ----------------------------------------------


class Track {
public:
  Track();
  
  void startRecording(uint32_t startTick);
  void stopRecording();
  void startPlaying();
  void stopPlaying();
  void togglePlayStop();
  void startOverdubbing();
  void stopOverdubbing();
  void clear();

  void recordMidiEvent(midi::MidiType type, byte channel, byte data1, byte data2, uint32_t currentTick);
  
  void toggleMuteTrack();
 

  bool isArmed() const;  
  bool isRecording() const;
  bool isStoppedRecording() const;
  bool isOverdubbing() const;
  bool isStoppedOverdubbing() const;
  bool isPlaying() const;
  bool isStopped() const;

  uint32_t getLength() const;
  void setLength(uint32_t ticks);
  TrackState getState() const;

  const std::vector<MidiEvent>& getEvents() const;     // Getter function to access private variables
  const std::vector<NoteEvent>& getNoteEvents() const;

  // Add events here
  void noteOn(uint8_t note, uint8_t velocity, uint32_t tick);
  void noteOff(uint8_t note, uint8_t velocity, uint32_t tick);;
  bool hasData() const;

  size_t getEventCount() const;

  void playEvents(uint32_t currentTick, bool isAudible);

private:
  bool isPlayingBack;  // to be ignored by Teensy central router to be recorded during overdub
  void sendMidiEvent(const MidiEvent& evt);
  bool isMuted() const;
  bool muted = false; 
  TrackState state;

  
  std::unordered_map<uint8_t, PendingNote> pendingNotes; // note + velocity on -> startTick
  
  std::vector<MidiEvent> events;     // full MIDI stream (optional)
  std::vector<NoteEvent> noteEvents; // finalized notes (only NoteOn/NoteOff)

  uint32_t startTick;
  uint32_t lengthTicks;
  uint16_t playbackIndex;  // New: index of where we are in the event list
};

extern Track track;

#endif
