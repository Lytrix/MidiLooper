//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#ifndef TRACK_H
#define TRACK_H

#pragma once
#include <cstdint>
#include <Arduino.h>
#include <vector>
#include <unordered_map>  // For pendingNotes
#include <deque>          // For undo
#include "MidiEvent.h"
#include "MidiHandler.h"
#include "Utils/NoteUtils.h"    // For CachedNoteList

class TrackUndo; // Forward declaration

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

/**
 * @class Track
 * @brief Manages the lifecycle, storage, playback, and undo history of a MIDI track.
 *
 * A Track maintains a sequence of MidiEvent objects for recording, playback, and overdubbing.
 * It uses a state machine (TrackState) to transition between empty, recording, stopped,
 * playing, and overdubbing modes. PendingNote structures buffer incoming NoteOn events
 * until their corresponding NoteOff, ensuring proper timing and ordering. Loop length
 * and quantization helpers define the track's playback boundaries.
 *
 * Undo history is maintained via friend class TrackUndo, which snapshots the midiEvents
 * vector to allow undoing overdubs or clears. Track also supports muting, clearing,
 * and sending all-notes-off commands.
 */
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


  // Helpers for stopRecording
  uint32_t quantizeStart(uint32_t originalStart) const;
  void shiftMidiEvents(int32_t offset);
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

  // Track management
  void clear();
  void toggleMuteTrack();
  
  // MIDI event validation
  void validateAndCleanupMidiEvents();  // Manual validation

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

  // Track length control
  uint32_t getStartLoopTick() const;
  uint32_t getLoopLength() const;
  void setLoopLength(uint32_t ticks);
  
  // Simple loop length change - no MIDI event modification
  void setLoopLengthWithWrapping(uint32_t newLoopLength);
  
  // Loop start point control - for loop editing
  uint32_t getLoopStartTick() const;
  void setLoopStartTick(uint32_t startTick);
  
  // Combined loop start/end editing with validation
  void setLoopStartAndEnd(uint32_t startTick, uint32_t endTick);
  
  // Get effective loop end based on start + length
  uint32_t getLoopEndTick() const;
  
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

  /// Immutable access to midiEvents (for const Track)
  const std::vector<MidiEvent>& getMidiEvents() const { return midiEvents; }

  // ==========================================
  // OPTIMIZATION: Cached Note Access
  // ==========================================
  
  /// Get cached display notes - avoids expensive reconstructNotes() calls
  const std::vector<NoteUtils::DisplayNote>& getCachedNotes() const {
    return noteCache.getNotes(midiEvents, loopLengthTicks);
  }
  
  /// Get cached event index - avoids expensive index rebuilding
  const NoteUtils::EventIndex& getCachedEventIndex() const {
    if (!eventIndexValid) {
      cachedEventIndex = NoteUtils::buildEventIndex(midiEvents);
      eventIndexValid = true;
    }
    return cachedEventIndex;
  }
  
  /// Invalidate caches when MIDI events change
  void invalidateCaches() {
    noteCache.invalidate();
    eventIndexValid = false;
  }

private:
  friend class TrackUndo;
  friend class StorageManager;  // Allow StorageManager to access private members for loading
  bool isPlayingBack;  // Flag to ignore playback events during overdub
  void sendMidiEvent(const MidiEvent& evt);

  // Track data
  bool muted;
  TrackState trackState;
  uint32_t startLoopTick;
  uint32_t loopLengthTicks;
  uint32_t loopStartTick;  // Loop start point offset for loop editing
  uint32_t lastTickInLoop;
  uint16_t nextEventIndex;
  static const uint32_t TICKS_PER_BAR;

  // Event storage
  std::unordered_map<std::pair<uint8_t, uint8_t>, PendingNote, PairHash> pendingNotes;
  std::vector<MidiEvent> midiEvents;
  
  // State management
  bool transitionState(TrackState newState);  // Internal state transition method
  
  // Undo management
  std::deque<std::vector<MidiEvent>> midiHistory;
  size_t midiEventCountAtLastSnapshot = 0;
  // Undo clear track control
  std::deque<std::vector<MidiEvent>> clearMidiHistory;
  std::deque<TrackState> clearStateHistory;
  std::deque<uint32_t> clearLengthHistory;
  std::deque<uint32_t> clearStartHistory;  // Loop start point history for clear undo

  // Redo management
  std::deque<std::vector<MidiEvent>> midiRedoHistory;
  // Redo clear track control
  std::deque<std::vector<MidiEvent>> clearMidiRedoHistory;
  std::deque<TrackState> clearStateRedoHistory;
  std::deque<uint32_t> clearLengthRedoHistory;
  std::deque<uint32_t> clearStartRedoHistory;  // Loop start point redo for clear
  
  // Loop start point undo/redo (separate from clear)
  std::deque<uint32_t> loopStartHistory;
  std::deque<uint32_t> loopStartRedoHistory;

  // ==========================================
  // OPTIMIZATION: Performance Caches
  // ==========================================
  
  /// Cached note list to avoid expensive reconstructNotes() calls
  mutable NoteUtils::CachedNoteList noteCache;
  
  /// Cached event index for fast event lookup  
  mutable NoteUtils::EventIndex cachedEventIndex;
  mutable bool eventIndexValid = false;

  // Dynamic note wrapping - handled in logic layer, not data storage

};

#include "TrackUndo.h"

#endif
