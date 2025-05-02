#ifndef TRACK_H
#define TRACK_H

#include <Arduino.h>
#include <vector>
#include <MIDI.h>
#include <unordered_map>

// Track states with clear transitions
enum TrackState {
  TRACK_EMPTY,            // Initial state Empty track
  TRACK_STOPPED,          // No recording or playback
  TRACK_ARMED,            // Ready to start recording
  TRACK_RECORDING,        // Recording first layer
  TRACK_STOPPED_RECORDING,// First layer recorded, ready for playback or overdub
  TRACK_PLAYING,          // Playing back recorded content
  TRACK_OVERDUBBING,      // Recording additional layers while playing
  TRACK_STOPPED_OVERDUBBING, // Additional layer recorded, back to playback
  NUM_TRACK_STATES        // Always helpful to validate range
};

// MIDI event structure
struct MidiEvent {
  uint32_t tick;
  midi::MidiType type;  // MIDI event type (e.g., 0x80, 0x90)
  byte channel;
  byte data1;
  byte data2;
};

// Pending note structure
struct PendingNote {
  uint32_t startNoteTick;
  uint8_t velocity;
};

// Hash function for pair (used in unordered_map)
struct PairHash {
  template <class T1, class T2>
  std::size_t operator()(const std::pair<T1, T2>& p) const {
    auto h1 = std::hash<T1>{}(p.first);
    auto h2 = std::hash<T2>{}(p.second);
    return h1 ^ (h2 << 1);
  }
};

// Finalized note event structure (NoteOn/NoteOff)
struct NoteEvent {
  uint8_t note;
  uint8_t velocity;
  uint32_t startNoteTick;
  uint32_t endNoteTick;
};

class Track {
public:
  Track();

  // State management
  TrackState getState() const;
  bool setState(TrackState newState);  // Returns true if transition was valid
  bool isValidStateTransition(TrackState newState) const;
  const char* getStateName(TrackState state);

  // Getter functions for events
  const std::vector<MidiEvent>& getEvents() const;
  const std::vector<NoteEvent>& getNoteEvents() const;

  // Recording control
  void startRecording(uint32_t startLoopTick);
  void stopRecording(uint32_t currentTick);

  // Playback control
  void startPlaying(uint32_t currentTick);
  void stopPlaying();
  void togglePlayStop();

  // Overdubbing control
  void startOverdubbing(uint32_t currentTick);
  void stopOverdubbing(uint32_t currentTick);

  // Track management
  void clear();
  void toggleMuteTrack();

  // MIDI events
  void recordMidiEvents(midi::MidiType type, byte channel, byte data1, byte data2, uint32_t currentTick);
  void playMidiEvents(uint32_t currentTick, bool isAudible);
  void printNoteEvents() const;

  // Note events
  void noteOn(uint8_t channel, uint8_t note, uint8_t velocity, uint32_t tick);
  void noteOff(uint8_t channel, uint8_t note, uint8_t velocity, uint32_t tick);
  bool hasData() const;

  // Event counters
  size_t getMidiEventCount() const;
  size_t getNoteEventCount() const;

  // Track length control
  uint32_t getStartLoopTick() const;
  uint32_t getLength() const;
  void setLength(uint32_t ticks);

  // Track state checks
  bool isEmpty() const;
  bool isArmed() const;
  bool isRecording() const;
  bool isStoppedRecording() const;
  bool isOverdubbing() const;
  bool isStoppedOverdubbing() const;
  bool isPlaying() const;
  bool isStopped() const;
  bool isMuted() const;

private:
  bool isPlayingBack;  // Flag to ignore playback events during overdub
  void sendMidiEvent(const MidiEvent& evt);

  // Track data
  bool muted;
  TrackState trackState;
  uint32_t startLoopTick;
  uint32_t loopLengthTicks;
  uint32_t lastTickInLoop;
  uint16_t nextEventIndex;

  // Event storage
  std::unordered_map<std::pair<uint8_t, uint8_t>, PendingNote, PairHash> pendingNotes;
  std::vector<MidiEvent> midiEvents;
  std::vector<NoteEvent> noteEvents;

  // State management
  bool transitionState(TrackState newState);  // Internal state transition method
};

#endif
