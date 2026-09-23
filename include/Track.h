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
#include "Utils/MidiServiceDrain.h"
#include "Utils/NoteUtils.h"    // For CachedNoteList
#include "Utils/MemoryPool.h"   // For pooled MIDI event vectors
#include "TrackState.h"
#include "TrackDisplayState.h"
#include "Loop.h"
#include "LoopEventStore.h"
#include "GlobalUndoStack.h"
#include "TrackPlaybackRuntime.h"
#include "Slot.h"
#include "LoopPool.h"
#include "Globals.h"
#include "OverlapNoteIdSet.h"

class TrackUndo; // Forward declaration
class TrackUndoPublication; // Track-owned protected stop undo transaction

// Pending note structure
struct PendingNote {
  uint8_t note;            // MIDI note number
  uint8_t channel;         // MIDI channel
  uint32_t startNoteTick;  // tick when note-on occurred
  uint8_t velocity;        // note-on velocity
  OverlapNoteIdSet overlapNoteIds;  // hold-duration candidates; keep after playback off
  /// Identities resolved at this hold's NoteOn. NoteOff skips them by identity.
  OverlapNoteIdSet occupancyResolvedNoteIds;
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

  /// Final joint handoff for an already staged frozen overdub. The staging owner
  /// must have rewritten this capture from the frozen source before detachment.
  /// Does not finish session-exit undo, cleanup or Track state transitions.
  static bool commitPreparedOverdubStopBridge(Loop& loop, LoopPlaybackRuntime& runtime,
      PendingStopAdmission& stop, const uint32_t& generation, const EmissionCursor& cursor,
      PreparedCapturePassTransfer& transferred,
      PendingCapturePass* pendingPass = nullptr);

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
  /// First-take RECORDING only. Writes `loopLengthTicks` with the same bar
  /// quantize as `prepareRecordStop`. Snapshot idle waits until the playhead
  /// has left bar 1.
  void updateGrowingRecordLoopLength(uint32_t currentTick);
  void resetLoopSlotAfterEmptyCapture(uint8_t slotIndex);

  // For any notes still in pendingNotes, emit a NoteOff at offAbsTick
  void finalizePendingNotes(uint32_t offAbsTick);
  /// Loop-relative phase for capture (overdub/stop); shared by live capture and finalize.
  uint32_t capturePhaseTick(uint32_t absTick) const;
  /// Append capture NoteOff at loop phase; returns false if capture inactive or append failed.
  bool appendCaptureNoteOffAtPhase(uint8_t channel, uint8_t note, uint32_t phaseTick);
  /// Consume one already admitted terminal closure without searching the
  /// mutable capture store. Deferred `OVERDUB_STOPPING` grains use this
  /// generation-matched reservation path.
  bool appendReservedCaptureNoteOffAtPhase(CaptureNoteReservation& reservation,
                                           uint32_t phaseTick);
  void resetPlaybackState(uint32_t currentTick);
  /// Reset per-slot playback indices so enabling/unmuting starts at the right phase.
  void resetPlaybackStateForSlot(uint8_t slotIndex, uint32_t currentTick);
  /// Reanchor playback traversal to the current cycle. Does not invalidate a
  /// still-valid full-loop merged representation.
  void reanchorPlaybackCycle(uint32_t currentTick);
  
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
#if defined(SESSION_CAPTURE)
  void printStoppedServiceStatusForHitl();
  void printStoppedCaptureContentForHitl();
#endif
  /// Arm gated PLAYING MIDI polls after overdub→PLAYING. Not all PLAYING.
  void armPlayingMidiDrainAfterOverdubStop();
  bool playingMidiDrainAfterOverdubStopActive() const;
  void notePlayingMidiDrainAfterOverdubStopIdle();
  /// Measurement only: time loop() prefix after undo→PLAYING. Does not arm MIDI drain.
  void armLoopPrefixMeasureAfterUndo();
  bool loopPrefixMeasureAfterUndoActive() const;
  void noteLoopPrefixMeasureAfterUndo();
  /// DEC-038 038.1: seal completed pairs at S, publish, beginCapture, stay OVERDUBBING.
  /// Returns true only when a capture pass was committed (`CommitResult::Committed`).
  bool commitOverdubWrap();
  bool maybeCommitOverdubWrap(uint32_t prevPhase, uint32_t currentPhase);

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
  /// SESSION_CAPTURE: re-arm the one-shot dump so the selected track’s transport STOPPED
  /// can emit effective / frame after an earlier overdub stop consumed the boot arm.
  void rearmDeferredStoredMidiVerification();
#if defined(PIO_UNIT_TEST_NATIVE)
  /// Native observability for the stored-MIDI verification queue lifecycle.
  bool deferredStoredVerificationPendingForTest() const;
  bool deferredStoredVerificationArmedForTest() const;
  void setDeferredStoredVerificationArmedForTest(bool armed);
  uint32_t deferredStoredVerificationOpportunityCountForTest() const;
#endif
  /// Idle maintenance: deferred full validate + session REVT flush (non-blocking stop path).
  // Called only by the single selected VisualCache prefix child. Never allocates.
  bool processDeferredCanonicalPrefix();
  void processDeferredIdleMaintenance(
      uint32_t nowMs, bool postOverdubDrainWindowActive,
      MidiServiceDrain::DeferredIdleOwner deferredOwner,
      uint8_t snapshotConstructionUnits = MidiServiceDrain::kSnapshotIdleConstructionUnitsPerTurn);
  /// One-shot `#CAP,DIAG,stored_notes` from a clean visual cache. SESSION_CAPTURE only.
  /// Gate 0 diagnosis; disable once later-stage overlap validation proofs exist.
  void maybeLogStoredNoteCount();
  /// Arm sliced `#CAP,DIAG,lcr` on the selected track when idle (Stage 9 device gate). SESSION_CAPTURE only.
  void maybeQueueContentResolutionDeviceGate();
  /// Touch playback runtime and loop playback order for one slot (boot/load prewarm).
  /// Establish every reserve that keeps the later deferred stop grains allocation-free.
  /// Returns false when any required reserve failed, which is capture-admission denial:
  /// the caller must not enter RECORDING. Boot/load callers may ignore the result.
  bool prewarmPlaybackForSlot(uint8_t slotIndex, bool prepareCapture = true);
  /// Full merged-MIDI build for a slot (LoopEnd / NextGrid launch prep). Not for boot prewarm.
  void ensurePlaybackMergedEventsForSlot(uint8_t slotIndex);
  /// Task 2.4 slice 2: satisfy a pending prepared-pair request off the MIDI tick.
  /// Runs the existing gather + order builder, publishes the immutable
  /// `PreparedPlaybackPair` in one atomic authority change and clears the request.
  /// Services at most one slot (the first pending) per call; returns true when one
  /// was serviced. Never call from the clock path.
  bool servicePendingPreparedPlaybackPair();
  /// True when any slot has an unsatisfied prepared-pair request (off-clock owner hint).
  bool hasPendingPreparedPlaybackPairRequest() const;
  /// Read-only measurement accessors for the prepared-pair conversion
  /// (ARCHITECTURE-REVIEW § phase gate for task 2.4).
  struct PreparedPairMetrics {
    uint32_t requestCount = 0;
    uint32_t publishCount = 0;
    uint32_t holdTicks = 0;
    uint32_t maxLatencyTicks = 0;
    uint32_t rebuildUnits = 0;
    uint64_t publishedGeneration = 0;
    size_t publishedEventCount = 0;
  };
  PreparedPairMetrics preparedPairMetrics(uint8_t slotIndex) const;
  void resetPreparedPairMetrics();
  /// Task 2.4 instrumentation: count of content-sized playback-order builds (merged
  /// gather/assign, order resize/phase fill/sort, full reanchor) that ran while a track was
  /// timing-critical. Must be zero on the active clock path; the builder is the STOPPED oracle.
  static void resetPlaybackOrderHotRebuildCount();
  static uint32_t playbackOrderHotRebuildCount();
  /// True when primary playback window matches current loop revision (safe LoopEnd activate).
  bool isPlaybackMergedMidiEventsReadyForSlot(uint8_t slotIndex) const;
  /// Drop cached playback merge buffers for all slots (frees extmem during capture).
  void releasePlaybackMergedMidiEventsMemory();
  /// Phase 1B — release rebuildable playback windows when not referenced this tick.
  bool tryReleasePlaybackMergedMidiEventsMemory();
  /// Phase 1B — drop revision-keyed materialized events scratch when note edit does not need it.
  bool tryClearCommittedMidiScratch();

  // MIDI events
  /// Returns false when capture admission rejects the event before Loop state mutates.
  bool recordMidiEvents(midi::MidiType type, byte channel, byte data1, byte data2,
                        uint32_t currentTick);
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
  /// Close the ISR-visible MIDI emission barrier before a transport Stop fan-out.
  /// A later Start, record, or overdub entry reopens it.
  void suppressMidiOutputForTransportStop() { transportStopOutputBarrier_ = true; }

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
  /// Overdub finalization is pending while committed playback remains audible.
  bool isOverdubStopping() const;
  /// The frozen stop transaction can continue after the track has resumed its
  /// prepared PLAYING window. It remains an admission barrier until its
  /// companion metadata and bridge publication have completed.
  bool hasPendingDeferredOverdubStop() const;
  /// S4f.5: true while an interaction is accepted and not yet executed. Consumers that mean
  /// "a stop is in flight" must consult this alongside `hasPendingDeferredOverdubStop`,
  /// because the accepted request exists before any job does.
  bool hasAcceptedUnexecutedInteraction() const;
  /// S4f.8 evidence: waits counted on the accepted stop (0 when none is latched).
  uint32_t acceptedStopWaitCount() const;
  bool isPlaying() const;
  /// True for every state whose committed MIDI playback must keep advancing.
  bool isAudiblePlaybackActive() const;
  bool isStopped() const;
  bool isMuted() const;

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
  bool hasPendingSnapshotPublishOnAnySlot() const;
  bool hasInFlightSnapshotConstructionOnAnySlot() const;
  /// True when any slot has pending publication and the published root lags
  /// `playbackRevision`. Boolean backlog-present only; not a size or cost.
  bool hasSnapshotPublicationBacklogOnAnySlot() const;
  bool hasPendingSessionExitSnapshotOnAnySlot() const;
  Loop& loopWithPendingSnapshotPublish();

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
    SessionMidiEventVec committedEvents;
    const_cast<Loop&>(loop).copyEffectiveCommittedEvents(committedEvents);
    return loop.getNoteCache().getNotes(committedEvents, loop.loopLengthTicks);
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
      SessionMidiEventVec committedEvents;
      loop.copyEffectiveCommittedEvents(committedEvents);
      loop.getCachedEventIndex() = NoteUtils::buildEventIndex(committedEvents);
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
#if defined(PIO_UNIT_TEST_NATIVE)
  friend struct TrackCaptureStopTestAccess;
#endif
  friend class StorageManager;  // Allow StorageManager to access private members for loading
  bool ignorePlaybackMidiInput;  // Ignore playback-echo MIDI during overdub capture
  bool playbackEmitMidiOutput_ = false;
  // `handleTransportStop()` fans CC123 over every track while the internal-clock
  // interval may still be executing. This volatile barrier is checked at the
  // final Track MIDI emission point, so no due NoteOn can pass that fan-out.
  volatile bool transportStopOutputBarrier_ = false;
  bool applyPlaybackLedgerEvent(const MidiEvent& evt, uint8_t playbackSlotIndex);
  void catchUpCommittedPlaybackLedgerToPhase(uint32_t occupyPhase);
  void sendMidiEvent(const MidiEvent& evt, uint8_t playbackSlotIndex);
  /// Task 2.4 slice 2: publish the slot's current working merged window + order as
  /// the coherent prepared pair. Off-clock only; false when the working window and
  /// its order are not a complete full-loop pair.
  bool publishPreparedPlaybackPairFromWorkingWindow(uint8_t slotIndex);
  /// One slot's pending pair request (the scan in `servicePendingPreparedPlaybackPair`).
  bool servicePendingPreparedPlaybackPairSlot(uint8_t slotIndex);
  void snapshotOverlapHoldCandidates(PendingNote& pending);
  void collectOverlapHoldPlaybackNoteOn(NoteId noteId, uint8_t pitch);

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
  bool playingMidiDrainAfterOverdubStop_ = false;
  bool playingMidiDrainAfterOverdubStopIdleNoted_ = false;
  uint32_t overdubDrainFallbackRetryAtMs_ = 0;
  bool loopPrefixMeasureAfterUndo_ = false;
  bool loopPrefixMeasureAfterUndoNoted_ = false;
  bool overdubStopDebtProbeArmed_ = false;
  /// DEC-069: a wrap crossing happened while a deferred stop job owned the slot's canonical
  /// revision, so the pass commit is owed to the first turn after that job retires.
  bool overdubWrapCommitPending_ = false;
  /// DEC-070 diagnostic: the head deferred stop job hit a predicate it can never satisfy, so it
  /// keeps its finalization resources forever. Noted once per job.
  bool deferredStopStallNoted_ = false;
  /// DEC-070 bounded fallback: grains spent in one phase of a retargeted job's companion
  /// preparation, so a path that cannot complete falls back to the raw pass instead of parking.
  uint16_t retargetStallGrains_ = 0;
  uint8_t retargetStallPhase_ = 0;
  uint32_t overdubStopDebtProbeArmedUs_ = 0;
  bool overdubStopVisualDirtyBefore_ = false;
  bool overdubStopVisualDirtyAfter_ = false;
  uint16_t overdubStopDirtyBarsBefore_ = 0;
  uint16_t overdubStopDirtyBarsAfter_ = 0;
  bool overdubStopSavePendingBefore_ = false;
  bool overdubStopSavePendingAfter_ = false;
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
#if defined(PIO_UNIT_TEST_NATIVE)
  /// Counts accepted `queueDeferredStoredMidiVerification()` calls (native-only) so
  /// the pending-survives-redundant-queue regression can assert one opportunity.
  uint32_t deferredStoredVerificationOpportunityCount = 0;
#endif
  uint8_t storedNoteCountLoggedMask_ = 0;
  uint8_t deferredStoredVerificationPhase = 0;
  size_t deferredStoredVerificationCursor = 0;
  SessionMidiEventVec deferredStoredVerificationEvents;
#if defined(SESSION_CAPTURE)
  NoteUtils::DisplayNoteVec deferredStoredVerificationNotes;
#endif
  bool deferredFullMidiValidate = false;
  uint32_t deferredValidateQueuedAtMs = 0;
  uint32_t playbackGeneration = 0;
  int32_t projectionCycleStartTick = 0;
  bool useQueuedStart = false;
  int32_t queuedStartTick = 0;
  uint32_t queuedStartQueuedAtTick = UINT32_MAX;
  uint32_t queuedStartGridTicks = Config::TICKS_PER_16TH_STEP;
  TrackPlaybackRuntime playbackRuntime;
  using PendingNoteMap = std::unordered_map<std::pair<uint8_t, uint8_t>, PendingNote, PairHash>;
  // The existing worker's stopped interval. Live input never owns this map.
  // Staging/Loop session migration must finish before multiple jobs are admitted.
  struct CaptureStopJob {
    PendingStopAdmission admission;
    Loop* owner = nullptr;
    LoopId loopId = kInvalidLoopId;
    uint8_t slot = 0;
    uint32_t phaseTick = 0;
    uint32_t loopLengthTicks = 0;
    uint64_t interval = 0;
    /// Job-owned sealed raw capture extent. Ownership transfers from the Loop
    /// producer at admission; the Loop keeps only a job-identified read borrow.
    LoopEventStore::CaptureRange rawRange;
    bool ownsRawRange = false;
    /// Job-owned pending note changes (Add/Shorten/Hide intents). Swapped out of
    /// the live session at admission; the Loop keeps a read borrow for display.
    PendingNoteChangeVec pendingChanges;
    /// Job-owned capture preview. Swapped out of the live session at admission so
    /// the deferred cleanup seals this interval's preview, not a successor's.
    CapturePreview preview;
    /// Job-owned overdub-session undo baseline (published root at session open).
    /// Moved out of the live session at admission, so a successor session's
    /// beginOverdubSession cannot rewrite this interval's undo entry baseline.
    ImmutableLoopSnapshotRef beforePublished;
    /// DEC-073: committed-pass coverage of `beforePublished` at capture time.
    uint32_t beforePublishedCoverage = kSnapshotCoverageUnset;
    bool hasBeforePublished = false;
    /// Causal predecessor identity: the published source this interval succeeded,
    /// resolved once at admission (punch-out) and pinned for the job's lifetime.
    /// A later grain must resolve this interval's successor semantics against the
    /// state it actually succeeded, never against whatever a successor has
    /// published by then. `causalBaseSnapshot` keeps `causalBase.snapshotIdentity`
    /// alive; a rejected pin denies admission instead of recording a dangling
    /// predecessor.
    PlaybackViewSourceIdentity causalBase;
    ImmutableLoopSnapshotRef causalBaseSnapshot;
    bool causalBaseRecorded = false;
    /// Bounded, resumable collection of this session's undo identities plus the
    /// prepared OverdubPassAdded entry built from the job's own slot/baseline.
    Loop::SessionUndoCollection undoCollection;
    PassIdList undoPassIds;
    EditPassIdList undoCompanionIds;
    UndoEntry undoEntry;
    bool undoCollectionStarted = false;
    bool undoCollectable = false;
    bool undoCollected = false;
    /// Protected undo publication state: the transaction is begun and prepared
    /// before the atomic commit, then drained in bounded DeferredCleanup turns.
    bool undoPublicationStarted = false;
    bool undoPublicationReady = false;
    /// OpenSpec 5.12 / D8: this interval's staged capture pass. Ownership moves out of the
    /// Loop producer slot in this job's own `PrepareTransfer` turn and is consumed only by
    /// this job's terminal transfer. A successor interval therefore stages its own pass into
    /// the (now empty) Loop slot without the `AlreadyPending` collision that a shared scalar
    /// produced, and a deferred reader resolves the receipt through the job, never through
    /// `Loop::pendingCapturePass()`. `ownsPendingPass` is the ownership receipt: at most one
    /// owner holds a given staged pass, and two intervals may each hold one concurrently.
    PendingCapturePass pendingPass;
    bool ownsPendingPass = false;
    PendingNoteMap pendingNotes;
  };
  // Stable descriptors only: shared canonical/companion worker scratch is not
  // duplicated. Additional admission stays guarded until session isolation.
  struct PendingInteractionLatch {
    enum class Kind : uint8_t { None = 0, Stop, Switch, Start };
    Kind kind = Kind::None;
    PendingStopDestination destination = PendingStopDestination::Playing;
    uint8_t slot = 0;
    uint8_t switchTarget = 0;
    uint64_t interval = 0;
    uint32_t phaseTick = 0;
    uint64_t transportEpoch = 0;
    /// S4f.8: the accepted stop's waits are counted as scheduling, never as refusals.
    uint32_t waitCount = 0;
    /// S4f.2/S2b.6: report the wait once per episode rather than once per deferred turn.
    bool waitReported = false;
    bool isSet() const { return kind != Kind::None; }
    /// A repeat of the same accepted interaction is a no-op (S4f.1). A distinct operation is
    /// not a duplicate merely because the interval matches.
    bool matches(Kind otherKind, uint8_t otherSlot, uint8_t otherTarget,
                 uint64_t otherInterval) const {
      return kind == otherKind && slot == otherSlot && switchTarget == otherTarget &&
             interval == otherInterval;
    }
    void clear() { *this = PendingInteractionLatch{}; }
  };
  // The accepted interaction lives in the externally allocated ring object (S3.0). A by-value
  // member of `Track` would be DTCM: the extra unique_ptr alone measured 8 B x 8 tracks = 64 B of
  // RAM1 locals and took the padding line from 872 to 856, below the documented floor.
  struct CaptureStopJobs {
    static constexpr size_t kCapacity = 4;
    /// Out-of-line and FLASH-resident. `CaptureStopJob` owns a `PendingCapturePass`
    /// (OpenSpec 5.12 / D8), so the ring's constructor is non-trivial; the implicit aggregate
    /// form was emitted into ITCM and cost one 32 KB FlexRAM bank on this tree's RAM1 edge.
    /// Default-initialization is value-initialization here: every member has an initializer or a
    /// non-trivial default constructor.
    CaptureStopJobs();
    CaptureStopJob entries[kCapacity];
    size_t head = 0;
    size_t count = 0;
    /// S4f.1: the track's accepted-but-not-yet-executed interaction. One instance, not a ring
    /// descriptor: it consumes none of `entries`/`count` and is never stacked.
    PendingInteractionLatch interaction;
  };
  struct CaptureStopJobsDeleter {
    void operator()(CaptureStopJobs* jobs) const;
  };
  std::unique_ptr<CaptureStopJobs, CaptureStopJobsDeleter> captureStopJobs_;
  bool prewarmCaptureStopJobs();
  /// S2b.2: the reserve set a slot needs at its own overdub start once boot/load stops prewarming a
  /// committed-but-inactive slot. FLASH-resident cold path.
  bool admitOverdubStartWorkingSet();
  /// S4f.1: the accepted interaction, stored in the externally allocated ring object. Null when
  /// the ring has not been admitted yet.
  PendingInteractionLatch* interactionLatch() {
    return captureStopJobs_ ? &captureStopJobs_->interaction : nullptr;
  }
  const PendingInteractionLatch* interactionLatch() const {
    return captureStopJobs_ ? &captureStopJobs_->interaction : nullptr;
  }
  /// S3.0/S4f.1: the latch is part of the ring's cold admission, so no action edge allocates one.
  bool prewarmInteractionLatch();
  /// Latch an accepted stop for `slot`/`interval`; a same-identity repeat is a no-op.
  void latchAcceptedStop(PendingStopDestination destination, uint8_t slot, uint64_t interval);
  /// Consume the stop obligation of a successfully admitted (or synchronously completed) stop.
  void consumeAcceptedStopObligation();
  /// S4f.2: one deferred turn of the accepted interaction. Returns true when it ran a grain.
  bool driveAcceptedInteraction();
  /// S4f.2: the dispatch decision at the top of `processDeferredIdleMaintenance` -- the latch is
  /// driven only when no admitted job owns the turn, so an accepted request can never stack a
  /// second stop job. A named production entry so the decision itself is directly assertable
  /// without linking the whole maintenance body's storage dependencies.
  bool serviceAcceptedInteractionTurn();
  /// S2b.6: the stop-side park. Re-runs the interval slot's canonical reservation and reports
  /// the wait as scheduling; never cancels the accepted request.
  void parkAcceptedStopForCanonicalOutput(uint8_t slot);
  /// Protected undo transaction. Externally allocated: it carries an UndoEntry
  /// plus retirement storage, and it must outlive the admitted job through its
  /// post-commit drain, so it cannot be DTCM-resident or job-owned.
  struct ProtectedUndoDeleter {
    void operator()(TrackUndoPublication* undo) const;
  };
  std::unique_ptr<TrackUndoPublication, ProtectedUndoDeleter> protectedUndo_;
  bool prewarmProtectedUndo();
  TrackUndoPublication* protectedUndo();
  /// FIFO head: the stable descriptor the one worker is servicing. Null when the
  /// ring is empty. A grain must bind this once and use it throughout, so a later
  /// head advance can never redirect work already in progress.
  CaptureStopJob* captureStopJobHead();
  const CaptureStopJob* captureStopJobHead() const;
  /// OpenSpec 5.12 / D8: the staged pass the head job owns, or nullptr when it owns none.
  /// Every deferred reader of the interval's receipt resolves it through here while the pass
  /// lives in the job; only the live producer path reads `Loop::pendingCapturePass()`.
  PendingCapturePass* ownedPendingCapturePass();
  const PendingCapturePass* ownedPendingCapturePass() const;
  /// FIFO tail: the descriptor a new stop interval is admitted into. Null when
  /// the ring is full, which is the explicit exhaustion result.
  CaptureStopJob* captureStopJobTail();
  /// Head descriptor for callers that have already proven a pending job exists.
  CaptureStopJob& captureStopJob();
  const CaptureStopJob& captureStopJob() const;
  size_t captureStopQueueDepth() const { return captureStopJobs_ ? captureStopJobs_->count : 0; }
  static constexpr size_t captureStopQueueCapacity() { return CaptureStopJobs::kCapacity; }
  /// Retire one grain of a job's raw capture input. Uses the job-owned sealed
  /// range when the job owns one, else the legacy producer path.
  bool retireJobCaptureSourceGrain(CaptureStopJob& job, Loop& loop, bool closeCapture);
  /// One bounded turn of the deferred undo-identity collection. Returns true when
  /// the job's prepared entry is ready (or the session had nothing to record).
  bool stepDeferredUndoCollection(CaptureStopJob& job, Loop& loop, PendingStopAdmission& stop);
  /// One bounded turn of the protected transaction's prepare (begin + detach
  /// redo). Returns true when the entry is Ready to publish at the atomic commit.
  bool stepDeferredUndoPublication(CaptureStopJob& job, PendingStopAdmission& stop);
  /// One bounded turn of the protected transaction's post-commit drain. Returns
  /// true once the transaction is back to Idle.
  bool stepProtectedUndoDrain();
  uint64_t captureInterval_ = 0;
  GlobalUndoStack undoStack;
  UndoLoopGeometry recordCaptureBaselineGeometry_{};
  bool hasRecordCaptureBaselineGeometry_ = false;
  /// Published root at overdub session open (GUS OverdubPassAdded before — Pin B).
  ImmutableLoopSnapshotRef overdubSessionBeforePublished_;
  /// DEC-073: committed-pass coverage of `overdubSessionBeforePublished_` at capture time.
  uint32_t overdubSessionBeforePublishedCoverage_ = kSnapshotCoverageUnset;
  bool hasOverdubSessionBeforePublished_ = false;
  MidiEventVec committedMidiScratch_;
  uint32_t committedMidiScratchRevision_ = UINT32_MAX;
  const void* committedMidiScratchPublishedIdentity_ = nullptr;
  static const uint32_t TICKS_PER_BAR;

  void resetDeferredRecordRevts();
  void queueDeferredRecordRevts();
  void processDeferredRecordRevts(size_t maxEventsPerSlice = 64);

  void resetDeferredStoredMidiVerification();
  void processDeferredStoredMidiVerification(size_t maxEventsPerSlice = 64);
  void processDeferredContentResolutionDeviceGate();
  uint16_t countVisualCacheDirtyBars(const Loop& loop) const;
  void armOverdubStopDebtProbe(bool visualDirtyBeforeStop, uint16_t dirtyBarsBeforeStop,
                               bool savePendingBeforeStop, const Loop& loop);
  void emitOverdubStopDebtProbeIfArmed(const Loop& loop, bool idleVisualSliceRan,
                                       uint8_t idleBarsPerSlice, uint16_t idleDirtyBefore,
                                       uint16_t idleDirtyAfter);

  /// S4f.0 (DEC-078 item 3): admission is slot-parameterised. The deferred drive binds the
  /// interval's own slot and interval, never whatever `activeLoopIndex` happens to be at drive
  /// time, so the DEC-067 guard chain, the staging, the canonical output and `job.slot` all
  /// resolve the interval's slot.
  bool beginDeferredOverdubStopForSlot(PendingStopDestination destination, uint32_t currentTick,
                                       uint8_t slot, uint64_t interval);
  /// Live-edge wrapper: admits against the currently active slot and live interval.
  bool beginDeferredOverdubStop(PendingStopDestination destination, uint32_t currentTick);
  static bool appendReservedCaptureNoteOffForOwner(
      Loop& loop, CaptureAdmissionStaging& staging,
      CaptureNoteReservation& reservation, uint32_t phaseTick);
  bool processDeferredOverdubStopGrain();
  void requestDeferredOverdubStopToStopped();

  /// Record-stop prep: raw length → clamp → finalizePendingNotes → dropEvents (exact order).
  /// Returns rawLength for truncation rewind. guardLabel is the caller name for the clamp warning.
  uint32_t prepareRecordStop(uint32_t currentTick, const char* guardLabel);
  /// Record-stop prep plus pending-note clear for stop-path callers.
  uint32_t prepareRecordStopAndClearPendingNotes(uint32_t currentTick, const char* guardLabel);

  /// In-edit overdub fold. true = stop fully completed; caller must return immediately.
  bool handleNoteEditFold(bool endInPlaying, uint32_t currentTick, uint32_t closeTick,
                          uint32_t stopStartUs);

  void syncSlotRefsFromPool();

  // Per-slot loop refs + pooled loop storage (heap-allocated to avoid BSS overflow)
  Slot slots_[Config::MAX_LOOPS_PER_TRACK]{};
  LoopPool loopPool_;

  // Event storage (pending notes during recording - target is active loop)
  PendingNoteMap pendingNotes;
  // Notes received while ARMED before external MIDI Start (downbeat pre-roll)
  std::unordered_map<std::pair<uint8_t, uint8_t>, PendingNote, PairHash> armedPreRollNotes;
  
  // State management
  bool transitionState(TrackState newState);  // Internal state transition method

  void rebuildPlaybackOrder();

  enum class PlaybackMidiTarget { ActiveSlot, LayeredSlot };

  void playCommittedLoopMidi(uint8_t slotIndex, uint32_t currentTick, PlaybackMidiTarget target);

  bool isStorageTickInJamRegion(uint32_t storageTick, const Loop& loop) const;

  friend void playbackCursorAdvanceSend(void* ctx, const MidiEvent& evt, uint8_t slotIndex);
  friend void playbackCursorAdvanceApplyLedger(void* ctx, const MidiEvent& evt, uint8_t slotIndex);
  friend void playbackCursorAdvanceSendCapture(void* ctx, const MidiEvent& evt, uint8_t slotIndex);
  friend bool playbackCursorAdvanceJamFilter(void* ctx, uint32_t storageTick);

};

#include "TrackUndo.h"

#endif
