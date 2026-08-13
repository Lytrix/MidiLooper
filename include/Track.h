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
#include "Utils/DeferredValidatePolicy.h"
#include "Utils/NoteUtils.h"    // For CachedNoteList
#include "Utils/MemoryPool.h"   // For pooled MIDI event vectors
#include "TrackState.h"
#include "TrackDisplayState.h"
#include "Loop.h"
#include "GlobalUndoStack.h"
#include "TrackPlaybackRuntime.h"
#include "Slot.h"
#include "LoopPool.h"
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
  bool hasCommittedPassesInSlot(uint8_t slotIndex) const;
  uint32_t quantizeTransportRecordLength(uint32_t rawLength) const;
  uint32_t computeRecordStopLengthTicks(uint32_t rawLength, uint32_t lastEventTick) const;
  void resetLoopSlotAfterEmptyCapture(uint8_t slotIndex);

  // For any notes still in pendingNotes, emit a NoteOff at offAbsTick
  void finalizePendingNotes(uint32_t offAbsTick);
  /// Loop-relative phase for capture (overdub/stop); shared by live capture and finalize.
  uint32_t capturePhaseTick(uint32_t absTick) const;
  /// Append capture NoteOff at loop phase; returns false if capture inactive or append failed.
  bool appendCaptureNoteOffAtPhase(uint8_t channel, uint8_t note, uint32_t phaseTick);
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
  void startPlaying(uint32_t currentTick, bool preserveLoopPhaseOrigin = false);
  void reanchorPlaybackProjection(uint32_t currentTick, bool preserveLoopPhaseOrigin = false);
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
  void validateAndCleanupMidiEvents(uint32_t openTailCloseTick = UINT32_MAX);  // Full loop (cold path)
  /// Wrap-window only: record/overdub stop hot path. Schedules deferred full validate.
  void finalizeLoopAtStop(uint32_t openTailCloseTick = UINT32_MAX,
                          bool scheduleDeferredFullValidate = true);
  CommitResult finalizeCommitSideEffects(CommitResult result, CommitReason reason,
                                         uint32_t closeTick);
  /// Seal capture then shared finalize only — never transport, playback, editor, or pending buffers.
  CommitResult commitCaptureForStop(CommitReason reason, uint32_t commitTick, uint32_t closeTick);
  /// Queue the stored-MIDI verification dump; draining happens in idle maintenance, never on the
  /// MIDI-dispatched stop path.
  void queueDeferredStoredMidiVerification();
  /// Idle maintenance: deferred full validate + session REVT flush (non-blocking stop path).
  void processDeferredIdleMaintenance(uint32_t nowMs);
  /// Touch playback runtime and loop playback order for one slot (boot/load prewarm).
  void prewarmPlaybackForSlot(uint8_t slotIndex);
  /// Full merged-MIDI build for a slot (LoopEnd / NextGrid launch prep). Not for boot prewarm.
  void ensurePlaybackMergedEventsForSlot(uint8_t slotIndex);
  /// True when primary playback window matches current loop revision (safe LoopEnd activate).
  bool isPlaybackMergedMidiEventsReadyForSlot(uint8_t slotIndex) const;
  /// Drop cached playback merge buffers for all slots (frees extmem during capture).
  void releasePlaybackMergedMidiEventsMemory();
  /// Phase 1B — release rebuildable playback windows when not referenced this tick.
  bool tryReleasePlaybackMergedMidiEventsMemory();
  /// Phase 1B — drop revision-keyed materialized events scratch when note edit does not need it.
  bool tryClearCommittedMidiScratch();

  // MIDI events
  void recordMidiEvents(midi::MidiType type, byte channel, byte data1, byte data2, uint32_t currentTick);
  /// Advance committed playback. `emitMidiOutput` sends on this track's MIDI channel
  /// and output ports; cursor and ledger always run.
  void playMidiEvents(uint32_t currentTick, bool emitMidiOutput);
  void playMidiEventsForSlot(uint8_t slotIndex, uint32_t currentTick, bool emitMidiOutput);
  /// Silence this track's MIDI channel on the output ports. Does not clear ledger or pendingNotes.
  void silenceTrackMidiOutput();
  void silenceSlotMidiOutput(uint8_t slotIndex);
  void printNoteEvents() const;
  /// Send an "All Notes Off" (CC 123) on every channel and clear any pending notes.
  void sendAllNotesOff();

  // Note events
  void noteOn(uint8_t channel, uint8_t note, uint8_t velocity, uint32_t tick);
  void noteOff(uint8_t channel, uint8_t note, uint8_t velocity, uint32_t tick);
  bool hasData() const { return getActiveLoop().hasData(); }
  bool hasDataInSlot(uint8_t slotIndex) const;
  /// True when any slot on this track has loop data (committed, length, or capture).
  bool hasAnySlotData() const;
  /// After slot-scoped mutation (clear, empty record stop): EMPTY only when no slot has data.
  void reconcileTransportStateAfterSlotMutation();

  // Event counters
  size_t getMidiEventCount() const { return getActiveLoop().liveEventCount(); }

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
  uint32_t getPlaybackGeneration() const { return playbackGeneration; }
  void bumpPlaybackGeneration() { ++playbackGeneration; }
  void invalidatePlaybackMergedMidiEvents(bool preserveLedger = false);

  /// Rolling projection cycle origin (D13) — one per track.
  int32_t getProjectionCycleStartTick() const { return projectionCycleStartTick; }
  void setProjectionCycleStartTick(int32_t tick) { projectionCycleStartTick = tick; }

  /// One-shot queued restart at next grid tick (D14); mutually exclusive with pending slot switch.
  void queuePlaybackStartAtGrid(int32_t startTick, uint32_t queuedAtTick);
  void clearQueuedPlaybackStart();
  bool hasQueuedPlaybackStart() const { return useQueuedStart; }
  int32_t getQueuedStartTick() const { return queuedStartTick; }
  bool shouldCommitQueuedPlaybackStart(uint32_t currentTick) const;
  void commitQueuedPlaybackStart(uint32_t commitTick);
  void setQueuedStartGridTicks(uint32_t gridTicks) { queuedStartGridTicks = gridTicks; }
  uint32_t getQueuedStartGridTicks() const { return queuedStartGridTicks; }

  // Tempo accessors
  static uint32_t getTicksPerBar();

  // MIDI output channel (1-16, default per track index)
  uint8_t getMidiChannel() const;
  void setMidiChannel(uint8_t ch);

  // Active loop slot (0-7); slot 0 = current data until D10 multi-slot storage
  uint8_t getActiveLoopIndex() const;
  void setActiveLoopIndex(uint8_t index);

  GlobalUndoStack& getGlobalUndoStack() { return undoStack; }
  const GlobalUndoStack& getGlobalUndoStack() const { return undoStack; }

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
  SessionMidiEventVec& getMidiEvents() { return getActiveLoop().midiEvents(); }

  /// Immutable access to midiEvents (for const Track)
  const SessionMidiEventVec& getMidiEvents() const { return getActiveLoop().midiEvents(); }

  /// Legacy internal-heap view of committed/materialized events (revision-keyed copy for NOTE_EDIT APIs).
  MidiEventVec& legacyMidiEventsFromCommitted();
  const MidiEventVec& legacyMidiEventsFromCommitted() const;

  /// Note-edit session projection store when active, else legacy committed scratch.
  MidiEventVec& editAwareMidiEvents();
  const MidiEventVec& editAwareMidiEvents() const;

  /// Access loop by slot index (adapter — resolves Slot → LoopId → LoopPool).
  Loop& getLoop(uint8_t index);
  const Loop& getLoop(uint8_t index) const;

  /// Resolve slot ref to pooled loop storage.
  Loop& loopForSlot(uint8_t slotIndex);
  const Loop& loopForSlot(uint8_t slotIndex) const;

  LoopId loopIdForSlot(uint8_t slotIndex) const;
  const Slot& slotRef(uint8_t slotIndex) const;

  /// Allocate LoopPool if not yet done (deferred from ctor to avoid static-init crash)
  void ensureLoopsAllocated();
  bool loopsAllocated() const { return loopPool_.initialized(); }

  /// Active loop (used for playback, recording, display)
  Loop& getActiveLoop() { return getLoop(activeLoopIndex); }
  const Loop& getActiveLoop() const { return getLoop(activeLoopIndex); }

  // ==========================================
  // OPTIMIZATION: Cached Note Access
  // ==========================================
  
  /// Get cached display notes - avoids expensive reconstructNotes() calls
  const std::vector<NoteUtils::DisplayNote, ExternalMemoryFirstAllocator<NoteUtils::DisplayNote>>&
  getCachedNotes() const {
    return getActiveLoop().getNoteCache().getNotes(editAwareMidiEvents(), getActiveLoop().loopLengthTicks);
  }

  /// Per-slot cached notes for display (e.g. follow selected slot while activeLoopIndex is capture phase).
  uint32_t getLoopLengthForSlot(uint8_t slotIndex) const { return getLoop(slotIndex).loopLengthTicks; }
  uint32_t getLoopStartTickForSlot(uint8_t slotIndex) const { return getLoop(slotIndex).loopStartTick; }
  const std::vector<NoteUtils::DisplayNote, ExternalMemoryFirstAllocator<NoteUtils::DisplayNote>>&
  getCachedNotesForSlot(uint8_t slotIndex) const {
    const Loop& loop = getLoop(slotIndex);
    return loop.getNoteCache().getNotes(loop.midiEvents(), loop.loopLengthTicks);
  }
  const DisplayNoteVec& getVisualNotesForSlot(uint8_t slotIndex) const {
    const Loop& loop = getLoop(slotIndex);
    if (!isPlaying() && !isStoppedRecording()) {
      Loop& mutLoop = const_cast<Loop&>(loop);
      if (!mutLoop.shouldAvoidFullVisualRebuild(loop.loopLengthTicks)) {
        mutLoop.ensureVisualCacheBuilt();
      }
    }
    return loop.visualCache.notes;
  }
  
  /// Get cached event index - avoids expensive index rebuilding
  const NoteUtils::EventIndex& getCachedEventIndex() const {
    Loop& loop = const_cast<Loop&>(getActiveLoop());
    if (!loop.eventIndexValid) {
      loop.getCachedEventIndex() = NoteUtils::buildEventIndex(loop.midiEvents());
      loop.eventIndexValid = true;
    }
    return loop.getCachedEventIndex();
  }
  
  /// Loop / playback derived caches only (no note-edit preview revision bumps).
  void invalidateLoopDerivedCaches();

  /// Invalidate caches when MIDI events change (includes flat sync when dirty).
  void invalidateCaches(bool refreshPlaybackPreview = true);

  /// Chunk mutations on hot paths — no flat sync.
  void invalidatePlaybackCaches() {
    getActiveLoop().invalidatePlaybackCaches();
  }

private:
  friend class TrackUndo;
  friend class StorageManager;  // Allow StorageManager to access private members for loading
  bool ignorePlaybackMidiInput;  // Ignore playback-echo MIDI during overdub capture
  bool playbackEmitMidiOutput_ = false;
  void sendMidiEvent(const MidiEvent& evt, uint8_t playbackSlotIndex);

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
  uint16_t recordAddedNoteOnCount;  // note-ons this overdub pass (memory log at overdub stop)
  bool deferredRecordRevtsPending = false;
  bool deferredRecordRevtChunkScan = false;
  size_t deferredRecordRevtCursor = 0;
  SessionMidiEventVec deferredRecordRevtEvents;
  CommittedChunkIdList deferredRecordRevtChunkRefs;
  size_t deferredRecordRevtChunkCursor = 0;
  SessionMidiEventVec deferredRecordRevtChunkEvents;
  size_t deferredRecordRevtChunkEventCursor = 0;
  bool deferredStoredVerificationPending = false;
  uint8_t deferredStoredVerificationPhase = 0;
  size_t deferredStoredVerificationCursor = 0;
  SessionMidiEventVec deferredStoredVerificationEvents;
  bool deferredFullMidiValidate = false;
  uint32_t deferredValidateQueuedAtMs = 0;
  uint32_t playbackGeneration = 0;
  int32_t projectionCycleStartTick = 0;
  bool useQueuedStart = false;
  int32_t queuedStartTick = 0;
  uint32_t queuedStartQueuedAtTick = UINT32_MAX;
  uint32_t queuedStartGridTicks = Config::TICKS_PER_16TH_STEP;
  TrackPlaybackRuntime playbackRuntime;
  GlobalUndoStack undoStack;
  UndoLoopGeometry recordCaptureBaselineGeometry_{};
  bool hasRecordCaptureBaselineGeometry_ = false;
  MidiEventVec committedMidiScratch_;
  uint32_t committedMidiScratchRevision_ = UINT32_MAX;
  static const uint32_t TICKS_PER_BAR;

  void resetDeferredRecordRevts();
  void queueDeferredRecordRevts();
  void processDeferredRecordRevts(size_t maxEventsPerSlice = 64);

  void resetDeferredStoredMidiVerification();
  void processDeferredStoredMidiVerification(size_t maxEventsPerSlice = 64);

  /// Record-stop prep: raw length → clamp → finalizePendingNotes → dropEvents (exact order).
  /// Returns rawLength for truncation rewind. guardLabel is the caller name for the clamp warning.
  uint32_t prepareRecordStop(uint32_t currentTick, const char* guardLabel);

  /// In-edit overdub fold. true = stop fully completed; caller must return immediately.
  bool handleNoteEditFold(bool endInPlaying, uint32_t currentTick, uint32_t closeTick,
                          uint32_t stopStartUs);

  void syncSlotRefsFromPool();

  // Per-slot loop refs + pooled loop storage (heap-allocated to avoid BSS overflow)
  Slot slots_[Config::MAX_LOOPS_PER_TRACK]{};
  LoopPool loopPool_;

  // Event storage (pending notes during recording - target is active loop)
  std::unordered_map<std::pair<uint8_t, uint8_t>, PendingNote, PairHash> pendingNotes;
  // Notes received while ARMED before external MIDI Start (downbeat pre-roll)
  std::unordered_map<std::pair<uint8_t, uint8_t>, PendingNote, PairHash> armedPreRollNotes;
  
  // State management
  bool transitionState(TrackState newState);  // Internal state transition method

  void rebuildPlaybackOrder();

  enum class PlaybackMidiTarget { ActiveSlot, LayeredSlot };

  void playCommittedLoopMidi(uint8_t slotIndex, uint32_t currentTick, PlaybackMidiTarget target);

  bool isStorageTickInJamRegion(uint32_t storageTick, const Loop& loop) const;

  friend void playbackCursorAdvanceSend(void* ctx, const MidiEvent& evt, uint8_t slotIndex);
  friend bool playbackCursorAdvanceJamFilter(void* ctx, uint32_t storageTick);

};

#include "TrackUndo.h"

#endif
