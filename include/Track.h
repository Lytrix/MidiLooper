#ifndef TRACK_H
#define TRACK_H

#include <Arduino.h>
#include <vector>
#include <MIDI.h>
#include <unordered_map>  // For pendingNotes
#include <deque>          // For undo

// Track states with clear transitions
enum TrackState {
  TRACK_EMPTY,              // Initial state Empty track
  TRACK_STOPPED,            // No recording or playback
  TRACK_ARMED,              // Ready to start recording
  TRACK_RECORDING,          // Recording first layer
  TRACK_STOPPED_RECORDING,  // First layer recorded, ready for playback or overdub
  TRACK_PLAYING,            // Playing back recorded content
  TRACK_OVERDUBBING,        // Recording additional layers while playing
  NUM_TRACK_STATES          // Always helpful to validate range
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
  uint8_t note;            // MIDI note number
  uint8_t channel;         // MIDI channel
  uint32_t startNoteTick;  // tick when note-on occurred
  uint8_t velocity;        // note-on velocity
};

// Hash function for pair (used in unordered_map)
struct PairHash {
  template<class T1, class T2>
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
  // For loading state from SD card else the state machine will corrupt the state
  void forceSetState(TrackState newState); 

  // Getter functions for events
  const std::vector<MidiEvent>& getEvents() const;
  const std::vector<NoteEvent>& getNoteEvents() const;

  // Helpers for stopRecording
  uint32_t quantizeStart(uint32_t originalStart) const;
  void shiftMidiEvents(int32_t offset);
  void shiftNoteEvents(int32_t offset);
  uint32_t findLastEventTick() const;
  uint32_t computeLoopLengthTicks(uint32_t lastEventTick) const;

  // For any notes still in pendingNotes, emit a NoteOff at offAbsTick
  void finalizePendingNotes(uint32_t offAbsTick);
  void resetPlaybackState(uint32_t currentTick);
  
  // Recording control
  void startRecording(uint32_t startLoopTick);
  void stopRecording(uint32_t currentTick);

  // Playback control
  void startPlaying(uint32_t currentTick);
  void stopPlaying();
  void togglePlayStop();

  // Overdubbing control
  void startOverdubbing(uint32_t currentTick);
  void stopOverdubbing();
  bool canUndoOverdub() const;
  void undoOverdub();

  // Track management
  void clear();
  void toggleMuteTrack();

  // Undo control
  void undoClearTrack();
  void pushClearTrackSnapshot();
  bool canUndoClearTrack() const;

  // MIDI events
  void recordMidiEvents(midi::MidiType type, byte channel, byte data1, byte data2, uint32_t currentTick);
  void playMidiEvents(uint32_t currentTick, bool isAudible);
  void printNoteEvents() const;
  /// Send an "All Notes Off" (CC 123) on every channel and clear any pending notes.
  void sendAllNotesOff();

  // Note events
  void noteOn(uint8_t channel, uint8_t note, uint8_t velocity, uint32_t tick);
  void noteOff(uint8_t channel, uint8_t note, uint8_t velocity, uint32_t tick);
  bool hasData() const;

  // Event counters
  size_t getMidiEventCount() const;
  size_t getNoteEventCount() const;
  
  // Undo functions
  size_t getUndoCount() const; // Number of available undo steps
  bool canUndo() const; // Returns true if there is at least one undoable state
  void popLastUndo();   // Remove and return the last undo snapshot

  // Read-only access to undo history (optional)
  const std::vector<MidiEvent>& peekLastMidiSnapshot() const; // Peek at the latest MIDI snapshot (const view)
  const std::vector<NoteEvent>& peekLastNoteSnapshot() const; // Peek at the latest NoteEvent snapshot (const view)
  std::deque<std::vector<MidiEvent>>& getMidiHistory() { return midiHistory; }
  std::deque<std::vector<NoteEvent>>& getNoteHistory() { return noteHistory; }
  const std::vector<MidiEvent>& getCurrentMidiSnapshot() const;
  const std::vector<NoteEvent>& getCurrentNoteSnapshot() const;

  void  pushUndoSnapshot();  // For initial start overdub.

  // Track length control
  uint32_t getStartLoopTick() const;
  uint32_t getLength() const;
  void setLength(uint32_t ticks);
  
  // Tempo accessors
  static uint32_t getTicksPerBar();

  // Track state checks
  bool isEmpty() const;
  bool isArmed() const;
  bool isRecording() const;
  bool isStoppedRecording() const;
  bool isOverdubbing() const;
  bool isPlaying() const;
  bool isStopped() const;
  bool isMuted() const;

  // Add to public section of Track to be able to save the events
  std::vector<MidiEvent>& getMidiEvents() { return midiEvents; }
  std::vector<NoteEvent>& getNoteEvents() { return noteEvents; }
  
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
  static const uint32_t TICKS_PER_BAR;

  // Event storage
  std::unordered_map<std::pair<uint8_t, uint8_t>, PendingNote, PairHash> pendingNotes;
  std::vector<MidiEvent> midiEvents;
  std::vector<NoteEvent> noteEvents;
  
  // Undo management
  bool hasNewEventsSinceSnapshot = false;  // flips to true on any new event
  bool suppressNextSnapshot = false; // If undo has been executed during the loop
  std::deque<std::vector<MidiEvent>> midiHistory;  // snapshots before each overdub
  std::deque<std::vector<NoteEvent>> noteHistory;
  size_t midiEventCountAtLastSnapshot = 0;
  size_t noteEventCountAtLastSnapshot = 0;
  
  // State management
  bool transitionState(TrackState newState);  // Internal state transition method

  // Undo clear track control
  std::deque<std::vector<MidiEvent>> clearMidiHistory;
  std::deque<std::vector<NoteEvent>> clearNoteHistory;
  std::deque<TrackState> clearStateHistory; // Track state history for clear track
  std::deque<uint32_t> clearLengthHistory; // Track length history for clear track
  
};

#endif
