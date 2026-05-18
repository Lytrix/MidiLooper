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
#include "Utils/MemoryPool.h"   // For pooled MIDI event vectors
#include "TrackState.h"
#include "Loop.h"

/// Derived capture role for a loop slot (from TrackState + activeLoopIndex).
enum class SlotOpState : uint8_t {
  SLOT_OP_IDLE = 0,
  SLOT_OP_RECORDING = 1,
  SLOT_OP_OVERDUBBING = 2,
};
#include "Globals.h"

class TrackUndo; // Forward declaration

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
  ~Track();

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
  /// Reset per-slot playback indices so enabling/unmuting starts at the right phase.
  void resetPlaybackStateForSlot(uint8_t slotIndex, uint32_t currentTick);
  
  // Recording control
  void startRecording(uint32_t startLoopTick);
  void stopRecording(uint32_t currentTick);
  void stopRecordingToStopped(uint32_t currentTick);  // Stop recording, end in STOPPED (for MIDI Stop)
  /// When set, next stopRecording() bar-aligns event ticks (immediate punch-in pickup).
  void setAlignLoopOriginOnNextStop(bool v) { alignLoopOriginOnNextStop = v; }

  // Playback control
  void startPlaying(uint32_t currentTick);
  void stopPlaying();
  void togglePlayStop();

  // Overdubbing control
  void startOverdubbing(uint32_t currentTick);
  void stopOverdubbing();
  void stopOverdubbingToStopped();  // Stop overdub, end in STOPPED (for MIDI Stop)

  // Track management
  void clear();
  void toggleMuteTrack();
  
  // MIDI event validation
  void validateAndCleanupMidiEvents();  // Manual validation

  // MIDI events
  void recordMidiEvents(midi::MidiType type, byte channel, byte data1, byte data2, uint32_t currentTick);
  void playMidiEvents(uint32_t currentTick, bool isAudible);
  void playMidiEventsForSlot(uint8_t slotIndex, uint32_t currentTick, bool isAudible);
  void printNoteEvents() const;
  /// Send an "All Notes Off" (CC 123) on every channel and clear any pending notes.
  void sendAllNotesOff();

  // Note events
  void noteOn(uint8_t channel, uint8_t note, uint8_t velocity, uint32_t tick);
  void noteOff(uint8_t channel, uint8_t note, uint8_t velocity, uint32_t tick);
  bool hasData() const { return getActiveLoop().hasData(); }
  bool hasDataInSlot(uint8_t slotIndex) const;

  // Event counters
  size_t getMidiEventCount() const { return getActiveLoop().midiEvents.size(); }

  // Track length control (delegate to active loop)
  uint32_t getStartLoopTick() const { return getActiveLoop().startLoopTick; }
  uint32_t getLoopLength() const { return getActiveLoop().loopLengthTicks; }
  void setLoopLength(uint32_t ticks);
  
  // Simple loop length change - no MIDI event modification
  void setLoopLengthWithWrapping(uint32_t newLoopLength);
  
  // Loop start point control - for loop editing (delegate to active loop)
  uint32_t getLoopStartTick() const { return getActiveLoop().loopStartTick; }
  void setLoopStartTick(uint32_t startTick);
  
  // Combined loop start/end editing with validation
  void setLoopStartAndEnd(uint32_t startTick, uint32_t endTick);
  
  // Get effective loop end based on start + length
  uint32_t getLoopEndTick() const { return getActiveLoop().loopStartTick + getActiveLoop().loopLengthTicks; }

  // Jam state — display focus region, decoupled from loop params
  bool isJamming() const { return jamLength > 0; }
  uint32_t getJamLength() const {
    return jamLength > 0 ? jamLength : getActiveLoop().loopLengthTicks;
  }
  uint32_t getJamStartTick() const {
    return jamStartTick != UINT32_MAX ? jamStartTick : getActiveLoop().loopStartTick;
  }
  void setJam(uint32_t startTick, uint32_t length);
  void clearJam();

  // Jam playback — independent tick for per-track looping within jam region
  void advanceJamTick(uint32_t delta = 1);
  uint32_t getJamTick() const;
  void setJamTick(uint32_t tick);
  bool isJamPlaybackActive() const { return jamPlaybackActive; }
  void setJamPlayback(bool enabled);
  uint32_t getEffectivePlaybackTick(uint32_t currentTick) const;

  // Tempo accessors
  static uint32_t getTicksPerBar();

  // MIDI output channel (1-16, default per track index)
  uint8_t getMidiChannel() const;
  void setMidiChannel(uint8_t ch);

  // Active loop slot (0-7); slot 0 = current data until D10 multi-slot storage
  uint8_t getActiveLoopIndex() const;
  void setActiveLoopIndex(uint8_t index);

  /// Per-slot capture state (only activeLoopIndex can be RECORDING/OVERDUBBING).
  SlotOpState getSlotOpState(uint8_t slotIndex) const;
  /// Active slot receiving MIDI capture, or Config::INVALID_LOOP_SLOT if not recording/overdubbing.
  uint8_t getRecordingFocusSlot() const;

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
  std::vector<MidiEvent, ExtMemAllocator<MidiEvent>>& getMidiEvents() { return getActiveLoop().midiEvents; }

  /// Immutable access to midiEvents (for const Track)
  const std::vector<MidiEvent, ExtMemAllocator<MidiEvent>>& getMidiEvents() const { return getActiveLoop().midiEvents; }

  /// Access loop by index (0 to MAX_LOOPS_PER_TRACK-1)
  Loop& getLoop(uint8_t index);
  const Loop& getLoop(uint8_t index) const;

  /// Allocate Loop array if not yet done (deferred from ctor to avoid static-init crash)
  void ensureLoopsAllocated();

  /// Active loop (used for playback, recording, display)
  Loop& getActiveLoop() { return getLoop(activeLoopIndex); }
  const Loop& getActiveLoop() const { return getLoop(activeLoopIndex); }

  // ==========================================
  // OPTIMIZATION: Cached Note Access
  // ==========================================
  
  /// Get cached display notes - avoids expensive reconstructNotes() calls
  const std::vector<NoteUtils::DisplayNote>& getCachedNotes() const {
    return getActiveLoop().getNoteCache().getNotes(getActiveLoop().midiEvents, getActiveLoop().loopLengthTicks);
  }

  /// Per-slot cached notes for display (e.g. follow selected slot while activeLoopIndex is capture phase).
  uint32_t getLoopLengthForSlot(uint8_t slotIndex) const { return getLoop(slotIndex).loopLengthTicks; }
  uint32_t getLoopStartTickForSlot(uint8_t slotIndex) const { return getLoop(slotIndex).loopStartTick; }
  const std::vector<NoteUtils::DisplayNote>& getCachedNotesForSlot(uint8_t slotIndex) const {
    const Loop& loop = getLoop(slotIndex);
    return loop.getNoteCache().getNotes(loop.midiEvents, loop.loopLengthTicks);
  }
  
  /// Get cached event index - avoids expensive index rebuilding
  const NoteUtils::EventIndex& getCachedEventIndex() const {
    Loop& loop = const_cast<Loop&>(getActiveLoop());
    if (!loop.eventIndexValid) {
      loop.getCachedEventIndex() = NoteUtils::buildEventIndex(loop.midiEvents);
      loop.eventIndexValid = true;
    }
    return loop.getCachedEventIndex();
  }
  
  /// Invalidate caches when MIDI events change
  void invalidateCaches() {
    Loop& loop = getActiveLoop();
    loop.invalidateCaches();
    loop.playbackOrderDirty = true;
  }

private:
  friend class TrackUndo;
  friend class StorageManager;  // Allow StorageManager to access private members for loading
  bool isPlayingBack;  // Flag to ignore playback events during overdub
  void sendMidiEvent(const MidiEvent& evt);

  // Track data
  bool muted;
  uint8_t midiChannel;
  uint8_t activeLoopIndex;  // Which loop slot (0-7) is active
  TrackState trackState;
  uint32_t jamStartTick;   // Jam display region start (UINT32_MAX = inactive)
  uint32_t jamLength;      // Jam display region length (0 = inactive)
  volatile uint32_t jamTick;  // Position within jam region (0 to jamLength-1)
  bool jamPlaybackActive;     // True = track uses jamTick for playback
  bool alignLoopOriginOnNextStop;
  static const uint32_t TICKS_PER_BAR;

  // Per-slot loop storage (heap-allocated to avoid BSS overflow with 8 tracks × 8 loops)
  Loop* loops;

  // Event storage (pending notes during recording - target is active loop)
  std::unordered_map<std::pair<uint8_t, uint8_t>, PendingNote, PairHash> pendingNotes;
  
  // State management
  bool transitionState(TrackState newState);  // Internal state transition method

  void rebuildPlaybackOrder();

};

#include "TrackUndo.h"

#endif
