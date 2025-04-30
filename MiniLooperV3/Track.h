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
  //byte channel;
  uint32_t startNoteTick;
  uint8_t velocity;
};

struct PairHash {
  template <class T1, class T2>
  std::size_t operator()(const std::pair<T1, T2>& p) const {
    auto h1 = std::hash<T1>{}(p.first);
    auto h2 = std::hash<T2>{}(p.second);
    return h1 ^ (h2 << 1);
  }
};

// ----------------------------------------------
// Display functions
// ----------------------------------------------
struct NoteEvent {
  uint8_t note;
  uint8_t velocity;
  uint32_t startNoteTick;
  uint32_t endNoteTick;
  // bool isNoteOn;
};

// ----------------------------------------------


class Track {
public:
  Track();
  TrackState getState() const;

  const std::vector<MidiEvent>& getEvents() const;     // Getter function to access private variables
  const std::vector<NoteEvent>& getNoteEvents() const;

  void startRecording(uint32_t startLoopTick);
  void stopRecording(uint32_t currentTick);
  void startPlaying();
  void stopPlaying();
  void togglePlayStop();
  void startOverdubbing(uint32_t currentTick);
  void stopOverdubbing(uint32_t currentTick);
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

  uint32_t getStartLoopTick() const;
  uint32_t getLength() const;
  void setLength(uint32_t ticks);

  // Add events here
  void noteOn(uint8_t channel, uint8_t note, uint8_t velocity, uint32_t tick);
  void noteOff(uint8_t channel, uint8_t note, uint8_t velocity, uint32_t tick);;
  bool hasData() const;

  size_t getEventCount() const;
  size_t getNoteEventCount() const;

  void playEvents(uint32_t currentTick, bool isAudible);
  void printNoteEvents() const;
private:
  bool isPlayingBack;  // to be ignored by Teensy central router to be recorded during overdub
  void sendMidiEvent(const MidiEvent& evt);
  bool isMuted() const;
  bool muted = false; 
  TrackState state;

  std::unordered_map<std::pair<uint8_t, uint8_t>, PendingNote, PairHash> pendingNotes; // note + velocity on -> startLoopTick
  std::vector<MidiEvent> events;     // full MIDI stream (optional)
  std::vector<NoteEvent> noteEvents; // finalized notes (only NoteOn/NoteOff)

  uint32_t startLoopTick;
  uint32_t loopLengthTicks;
  uint16_t nextEventIndex;  // New: index of where we are in the event list
};


// extern Track creates a global singleton Track object named track, 
// separate from your TrackManager's Track tracks[NUM_TRACKS].
// This results in:
// Recorded notes going to one Track object (inside TrackManager)
// Display or playback trying to use the other, empty Track object (the global one)v

// extern Track track;

#endif