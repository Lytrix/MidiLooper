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
#include "Utils/RecordSourceStaging.h"
#include "Utils/RecordPreparationRole.h"
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
  /// Accepted Record NoteOn identity; moves with the owning pending-note map.
  /// Overdub wrap continues to use its generation-qualified staging reservation.
  NoteId recordNoteId = kInvalidNoteId;
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
  void printStoppedCaptureContentForHitl(uint8_t slotIndex = 0xFF);
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
  /// S4f.5: `slot` is the **interval's** slot. Every Loop, undo and persistence target inside
  /// resolves from it rather than from `activeLoopIndex`, so a later stage may let the active slot
  /// move while this interval's stop is still being committed (S4f.3). Live-edge callers pass
  /// `getActiveLoopIndex()`, which is the interval's slot today.
  CommitResult finalizeCommitSideEffects(CommitResult result, CommitReason reason,
                                         uint32_t closeTick, uint8_t slot);
  /// Seal capture then shared finalize only — never transport, playback, editor, or pending buffers.
  CommitResult commitCaptureForStop(CommitReason reason, uint32_t commitTick, uint32_t closeTick,
                                    uint8_t slot);
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
  /// S3.1: the boot/load pass over this track's admitted slots, so the admission predicate and
  /// the working-set lease share one owner. Establishes descriptors only (`prepareCapture=false`).
  void prewarmBootPlaybackRuntimeSlots();
  /// Touch playback runtime and loop playback order for one slot (boot/load prewarm).
  /// Establish every reserve that keeps the later deferred stop grains allocation-free.
  /// Returns false when any required reserve failed, which is capture-admission denial:
  /// the caller must not enter RECORDING. Boot/load callers may ignore the result.
  bool prewarmPlaybackForSlot(uint8_t slotIndex, bool prepareCapture = true);
  /// Full merged-MIDI build for a slot (LoopEnd / NextGrid launch prep). Not for boot prewarm.
  void ensurePlaybackMergedEventsForSlot(uint8_t slotIndex);
  /// DEC-108 item 1 / `ExecutionBounds.md` rows 27-29: the **window-build/publication owner**. These are the
  /// owner's whole production surface, and all of them are off-clock only -- the MIDI edge, the transport tick
  /// and the clock path read readiness and never call them.
  ///
  /// A refusal is a **returned outcome**, never stored state: the enum value *is* the contract, the acceptance
  /// surface names it where it reports the row, and nothing is added to DTCM, to `Track` or to
  /// `LoopPlaybackRuntime`. The owner deliberately carries **no name table** -- a `switch` or a `const char*`
  /// array is `.data`, and a few added `.data` bytes can snap this target's `ALIGN(1024)` and cost 4 KB of DTCM
  /// (the RAM1 rule's finding, measured again for this slice).
  /// `requestPlaybackViewMaintenance` establishes the obligation from an **already-resolved** source and
  /// admits it under the declared contract (the publication request task 3.2 will issue); a refusal is
  /// non-destructive and leaves any existing obligation intact.
  /// `hasPendingPlaybackViewMaintenance` is the deferred turn's dispatch eligibility; an owed window -- a
  /// build that is begun and unfinished -- *is* the obligation, so no request record is involved.
  /// `stepPlaybackViewMaintenance` serves **at most one declared grain** and never publishes, hands off or
  /// allocates: the consumer-facing publication is task 3.2's slice.
  enum class PlaybackViewRequestResult : uint8_t {
    Admitted,
    NoRuntime,
    BuildInFlight,
    /// A **prepared successor** is already waiting for this slot and must be adopted (or released) before
    /// another window can be prepared. A window merely *being served* is not a bound: preparing its replacement
    /// is the successor lane -- the completed build becomes the ready successor and the served window stays
    /// untouched until an adoption swaps it atomically (task 3.4; the consumer-facing adoption is 3.7).
    SuccessorAlreadyPrepared,
    /// The slot has no committed content of a usable length, so there is no interval to prepare.
    NotServable,
    /// The slot has no published snapshot root: the enumeration this slice serves is the **selected
    /// `LoopSnapshot`**, so a rootless slot is refused rather than served through a compatibility path.
    SourceUnavailable,
    /// The published root is **not current** (the gather would have to overlay unpublished session/pending
    /// passes). Serving that as if it were the revision it names is exactly the staleness DEC-108 forbids, so
    /// this slice refuses; the one-pass-late retention of a *previous* revision is 3.6's.
    SourceNotCurrent,
    ContractUndeclared,
    CannotCompleteBeforeHandoff,
    AdmissionRefused,
  };
  enum class PlaybackViewTurnResult : uint8_t { Idle, GrainServed, BuildComplete, GrainDenied };

  PlaybackViewRequestResult requestPlaybackViewMaintenance(
      uint8_t slotIndex, const MidiEvent* source, size_t sourceCount,
      const PlaybackViewSourceIdentity& sourceIdentity, const PlaybackCoverageInterval& coverage,
      const EmissionCursor& cursorAnchor, const LedgerBoundarySummary& ledgerBoundary,
      uint32_t currentPhase, uint32_t handoffPhase);
  bool hasPendingPlaybackViewMaintenance() const;
  /// DEC-108 §13b / task 3.10: serve **one declared grain** of this track's owed window work. `currentPhase` is
  /// the playhead's phase and `cursor` the caller's rotation position across holders: the selection is the shared
  /// policy (`MidiServiceDrain::selectPlaybackHolder`) -- holders rotate, the earliest outstanding handoff is
  /// served first, and the cursor advances even when the chosen holder cannot progress.
  ///
  /// Passing no cursor keeps the pre-3.10 single-holder behaviour (the scan starts at slot 0 every call), which is
  /// why the existing single-owing-slot fixtures need no change; a caller that serves more than one holder passes
  /// its own cursor so the rotation persists across turns.
  PlaybackViewTurnResult stepPlaybackViewMaintenance(
      uint32_t currentPhase = 0, MidiServiceDrain::PlaybackHolderCursor* cursor = nullptr);
  /// DEC-108 item 1 / task 3.2: the **interval window the owner published** for a slot -- its identity and the
  /// coverage interval it serves -- or `false` when none is authoritative. Read-only, and deliberately read by
  /// no production consumer yet: adoption is 3.4 (identity on the consumers), 3.5 (readiness predicate) and 3.7
  /// DEC-108 §13b / task 3.10: **what this track offers** the next playback-maintenance opportunity -- its most
  /// urgent owing holder (`eligible` + `handoffLeadTicks`), read without serving anything. The production caller
  /// builds every track's offer and lets the shared policy choose **one track** per turn, so the lowest-index track
  /// with work no longer monopolises the turn; the offer is also what a fixture can assert on.
  MidiServiceDrain::PlaybackHolder playbackWindowServiceOffer(uint32_t currentPhase) const;
  /// The holder set the offer and the turn both read: this track's owing slots, each with its urgency. Public only
  /// so the two callers in this owner's own translation unit agree on one description of a holder.
  void fillPlaybackHolders(uint32_t currentPhase,
                           MidiServiceDrain::PlaybackHolder* holders) const;
  /// (the consumer switch), so this slice publishes without changing what any existing consumer reads.
  bool publishedPlaybackWindowForSlot(uint8_t slotIndex, PlaybackViewSourceIdentity& identityOut,
                                      PlaybackCoverageInterval& coverageOut) const;
  /// DEC-108 item 5 / task 3.4: the published window's **identity** -- `(slot, playbackGeneration,
  /// playbackRevision, interval)` -- which is what a consumer compares. `false` when no window is published.
  bool publishedPlaybackWindowIdentityForSlot(uint8_t slotIndex, PlaybackWindowIdentity& out) const;
  /// Task 3.4: **adopt** a published successor as the slot's current window, atomically (the lifecycle's own
  /// handoff). This is the consumer-facing edge task 3.7 calls at an eligible boundary; until then nothing in
  /// production calls it, and a prepared successor sits in the ready role without touching the window that is
  /// being served.
  bool adoptPreparedPlaybackWindow(uint8_t slotIndex);
  /// DEC-108 item 1 / task 3.2: the **interval policy** -- which interval a slot's window serves. A pure
  /// resolution from the loop's geometry, its demotion state and the playhead: whole-loop coverage where the
  /// whole-take view is admissible, else the two-bar window at the playhead that the live build already uses
  /// (DEC-108 item 8's "prepare ahead of the playhead"; "centred on the playhead" is the present shape of that
  /// rule, not its contract). Public so the resolver, its fixtures and its readers agree on one policy.
  static bool playbackWindowInterval(uint32_t loopLengthTicks, bool demoted, uint32_t playheadPhase,
                                     PlaybackCoverageInterval& out);
  /// DEC-108 item 1 / task 3.2: ask the owner to prepare **the window this slot needs**, resolved from the
  /// selected `LoopSnapshot` -- the caller names a **slot and a phase**, never events. `scratch` is the
  /// caller's bounded buffer for the enumeration; the owner neither allocates nor keeps state for it. Returns
  /// `Admitted` when the build is begun (then drive `stepPlaybackViewMaintenance`), or one of the named
  /// refusals above, which are non-destructive.
  PlaybackViewRequestResult requestPlaybackWindow(uint8_t slotIndex, uint32_t playheadPhase,
                                                 SessionMidiEventVec& scratch);
  /// The declared `CoverageServiceContract` this owner is admitted under. Single-sourced: the declaration,
  /// its derivation and the measurement still owed are recorded where it is defined, and the grain quanta
  /// below are the sizes its declared grain counts come from.
  static CoverageServiceContract playbackViewCoverageContract();
  /// The owner's declared work quantum per grain (DEC-108 item 1: "a fixed work quantum"). Public so the
  /// contract's derivation and its fixtures read the same declaration the owner serves.
  static constexpr size_t kPlaybackViewFillQuantum = 64;
  static constexpr size_t kPlaybackViewOrderQuantum = 48;
  /// S4b.6: does this slot's **inbound occupancy** (its playback runtime) exist? The switch commit path
  /// asks this instead of allocating: `ensurePlaybackMergedEventsForSlot` intentionally never takes the
  /// runtime, so a switch to a slot without occupancy stays pending until the off-clock provisioning owner
  /// (`TrackManager::provisionSlotForSwitch`) has taken it. Answered without allocating.
  bool hasPlaybackOccupancyForSlot(uint8_t slotIndex) const;
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
  /// This is the **legacy merged-buffer term**: readiness answered from the live mutable merged window, which
  /// requires that window to be non-empty. Readiness is `playbackWindowReadinessForSlot` (task 3.5); this term
  /// survives only as the switch commit's hold until task 3.7 removes the live build.
  bool isPlaybackMergedMidiEventsReadyForSlot(uint8_t slotIndex) const;
  /// AC-3 / task 3.5: **readiness is a predicate, and its failure names its holder.** The question is exactly
  /// AC-3's -- *does the window for this slot exist, is it complete, and is it current?* -- and the answer is the
  /// window this change **published** (3.2/3.4), never the live merged buffer: a published, complete, current
  /// window is ready **even when its interval legitimately holds no events**, so "empty" is no longer a readiness
  /// term. Each non-ready value names *who owes it* (the build owner, the source, a rebuild, the occupancy owner),
  /// exactly as `hasPendingSnapshotPublish`, the restore-request vocabulary and the S4b.7 hold terminal do; the
  /// holder is rendered by `playbackWindowReadinessHolder` where a row is reported.
  /// Currency (DEC-108 item 5) is `playbackRevision` and nothing else: an older published revision is
  /// `OwesCurrency`, and the **one-pass-late** allowance that would accept it is task 3.6's rule, deliberately not
  /// assumed here. Read-only; callable from a consumer, and answered without allocating.
  enum class PlaybackWindowReadiness : uint8_t {
    /// A complete window is published for this slot and carries the loop's current revision.
    Ready,
    /// No window is published, and a **current published root** exists: the window-build owner owes one
    /// (`requestPlaybackWindow` + the deferred `stepPlaybackViewMaintenance` turns).
    OwesBuild,
    /// No window is published and no **current** published root exists, so no build could gather: the source owes
    /// the root. This is the holder for a rootless or not-current slot.
    OwesSource,
    /// A window is published but lags the current revision: a rebuild is owed (the one-pass-late allowance is 3.6's).
    OwesCurrency,
    /// No committed content or no usable loop length: there is no interval to be ready about.
    NotServable,
    /// No playback runtime for this slot: the occupancy owner owes it (a read-side misuse otherwise).
    NoRuntime,
  };
  PlaybackWindowReadiness playbackWindowReadinessForSlot(uint8_t slotIndex) const;
  /// The **holder's name** for a readiness answer, rendered where a row is reported (no name table: an if/else
  /// chain in FLASH, never a `switch` or a `const char*` array in `.data`, per the owner's own RAM1 rule).
  static const char* playbackWindowReadinessHolder(PlaybackWindowReadiness readiness);
  /// Task 3.6 (DEC-108 items 4, 12(a), 13(a)): **admit a retained source** for a slot's revision — the previous
  /// revision must stay reconstructible while a missed deadline keeps it authoritative, and later intervals must stay
  /// constructible from it with no resident whole-loop root. The admission pins the revision's passes (what the
  /// pass-level reclamation consumer reads) **and** takes the store's own references for its committed bodies (so
  /// `Loop::resetPassTimeline` cannot free or silently alias them), records the identity `(revision, generation)` in a
  /// named role, and is bounded: exceeding the role set is refused by name rather than discarding a source that is
  /// still needed. A re-admission of the same revision refreshes the role and never re-owns the bodies. Off-clock only:
  /// never call from the MIDI clock path.
  enum class PlaybackSourceRetainResult : uint8_t {
    Admitted,
    /// The revision is already retained: the identity is the revision, so ownership is not a count.
    AlreadyRetained,
    NoRuntime,
    NotServable,
    /// The role set is full and every entry is still needed: refused **by name** rather than discarding one.
    RoleCapacityReached,
  };
  PlaybackSourceRetainResult retainPlaybackSourceForSlot(uint8_t slotIndex, uint32_t revision, uint32_t generation);
  /// Read the **serving** retained source (identity, role, pins and owned bodies). Cold: used by fixtures and by the
  /// envelope accounting, never by a timing-critical consumer.
  bool retainedPlaybackSourceForSlot(uint8_t slotIndex, RetainedPlaybackSource& out) const;
  bool hasRetainedPlaybackSourceForSlot(uint8_t slotIndex) const;
  size_t retainedPlaybackSourceCountForSlot(uint8_t slotIndex) const;
  /// The **one release predicate** (the spec's rule: a source is released only here) — the loop's revision has
  /// advanced past it, no window serves it, no builder holds a frozen source, and no closure obligation is
  /// outstanding. Returns true when at least one source was released.
  bool tryReleaseRetainedPlaybackSource(uint8_t slotIndex);
  /// Merge this slot's retained pins into the set the **reclamation consumer** reads, so the pass-level reclaim
  /// cannot free a retained revision's descriptors (a pin held only alongside undo references is not sufficient
  /// protection, and the direct reset path is covered by the retained *ownership* rather than by a veto).
  bool mergeRetainedPlaybackPinsInto(uint8_t slotIndex, SlotPassReferences& into) const;
  /// Task 3.6b (AC-5/F-3): the **lag rule** as one pure resolution -- so a fixture can pin the decision without any
  /// state, and so the rule has exactly one implementation. `servedIsCurrent` is readiness (3.5) seen at the interval's
  /// traversal boundary; `replacementReady` is a complete current window published for that boundary. The result is
  /// **total**: `Current`, `PreviousOnePass`, or the named `BreachSecondMiss` -- never a wait, so a consumer can never
  /// be blocked by a missed deadline.
  static PlaybackLagDecision resolvePlaybackLag(bool servedIsCurrent, bool replacementReady,
                                                uint8_t missedTraversals);
  /// Task 3.6b: the **traversal boundary**. Counts the miss when the interval's replacement is not ready, returns the
  /// decision, and reports the breach **once per episode** (the episode ends at a served traversal or an interval
  /// change). A further edit moves the desired revision and **never resets the allowance** (AC-5). Off-clock only.
  PlaybackLagDecision advancePlaybackLagTraversal(uint8_t slotIndex, uint32_t intervalStart,
                                                  uint32_t intervalLength, uint32_t desiredRevision,
                                                  bool replacementReady);
  PlaybackLagDecision playbackLagDecisionForSlot(uint8_t slotIndex) const;
  uint8_t playbackLagMissedTraversalsForSlot(uint8_t slotIndex) const;
  uint8_t playbackLagBreachReportsForSlot(uint8_t slotIndex) const;
  bool playbackLagBreachReportedForSlot(uint8_t slotIndex) const;
  /// Task 3.6c (DEC-108 item 14(c)): build the **interval index over pinned bodies** for a retained source and publish
  /// it atomically with that source revision. A full rebuild per admitted revision is the update strategy -- it covers
  /// both the old and the new affected ranges by construction and never modifies an index a reader holds.
  enum class RetainedIndexBuildResult : uint8_t {
    Published,
    NoRuntime,
    /// Nothing is retained for this slot, so there are no pinned bodies to index.
    NoRetainedSource,
    /// The loop has no committed content or no usable length.
    NotServable,
    /// The loop's bar count exceeds `kRetainedIndexMaxBars`: refused **by name**, because an index that answered only
    /// part of the loop would present a complete scan as window-bounded. The caller accounts the full scan instead.
    BarsUnsupported,
  };
  RetainedIndexBuildResult buildRetainedIntervalIndexForSlot(uint8_t slotIndex);
  /// Task 3.6c: the **interval query** the index exists for. Answers how many bars of the interval the pinned bodies
  /// cover and how many are skipped **without touching a chunk**, and refuses by name rather than answering from a
  /// stale index (`RevisionMismatch`) or claiming a partial answer (`BarsUnsupported`). The caller enumerates the
  /// covered bars' events through the existing budgeted body primitive.
  enum class RetainedIntervalQueryResult : uint8_t { Indexed, NoIndex, RevisionMismatch, BarsUnsupported };
  RetainedIntervalQueryResult queryRetainedIntervalForSlot(uint8_t slotIndex, uint32_t startPhase,
                                                           uint32_t lengthTicks, uint32_t expectedRevision,
                                                           uint32_t& indexedBarsOut,
                                                           uint32_t& skippedBarsOut) const;
  /// Task 3.11 (DEC-108 §13d): apply a **lifecycle transition** to this slot's shared view state, through the one table
  /// `resolvePlaybackViewTransition` states. It cancels the in-flight candidate and/or retires a prepared successor
  /// when the row says so, invalidates the operation identity when the row says so (so a returning slot revives
  /// nothing), and leaves the served window readable in every row -- no transition may tear it. The frozen source's
  /// hold is the retained source's own business (task 3.6a) and is released only by its one predicate. Off-clock.
  void applyPlaybackViewTransition(uint8_t slotIndex, PlaybackViewTransition transition);
  /// Task 3.11: the slot's **shared-view role state**, as a fixture -- and a later slice -- asserts it: whether the
  /// served window is still readable, whether a prepared successor is waiting, whether a build is owed, and the
  /// operation generation the current work was started under.
  struct PlaybackViewRoleState {
    bool servedWindowReadable = false;
    bool preparedSuccessorWaiting = false;
    bool buildOwed = false;
    uint32_t operationGeneration = 0;
  };
  PlaybackViewRoleState playbackViewRoleStateForSlot(uint8_t slotIndex) const;
  /// Drop cached playback merge buffers for all slots (frees extmem during capture).
  void releasePlaybackMergedMidiEventsMemory();
  /// Phase 1B — release rebuildable playback windows when not referenced this tick.
  bool tryReleasePlaybackMergedMidiEventsMemory();
  /// S4.0 (B6, invariant 33): per-slot liveness. True when the slot has no live owner, so its
  /// working set may be released and a load may be admitted into it. The ring test scans the
  /// **live window only**: retirement clears `owner`/`loopId` but not `slot`, so a whole-array scan
  /// would report a retired entry as an owner forever and block release and load admission.
  bool noLiveOwnerForSlot(uint8_t slot) const;
  /// S3.2 owner kind: an admitted or in-flight capture stop job for `slot`. Scans the ring's
  /// **live window only** — a retired entry outside `[head, head + count)` keeps its stale `slot`
  /// and must not block release (B6). Named so a fixture can hold exactly this owner kind.
  bool hasLiveCaptureStopJobForSlot(uint8_t slot) const;
  /// S3.2 owner kind: an accepted-but-not-yet-executed interaction latched for `slot` (S4f.1).
  bool hasAcceptedInteractionForSlot(uint8_t slot) const;
  /// S4.4: report when an activation would exceed a **declared** envelope cap, so a class held beyond
  /// its declared cardinality is visible as a policy signal rather than silent.
  ///
  /// **The policy is reported, never refused, and never resolved by allocating.** DEC-078 item 1 makes
  /// acceptance unconditional and schedules execution instead ("the user never re-issues"), and a refusal
  /// here would change the admission envelope — which `IMPLEMENTATION_CONTRACT.md` §2 returns to the
  /// architect — while rejecting supported multi-slot flows. The enforcing caps are the lease's own
  /// (`WorkingSetLeaseTable`, `test_working_set_lease`), which refuse at the point of custody.
  ///
  /// Returns the slot that holds the class it reported for, or `kNoWorkingSetClassHolder` when there is
  /// nothing to report. The holder's identity is what makes the signal usable: the census counts raw role
  /// bits, so a slot whose capture has already stopped still holds `LiveCapture` until its working set is
  /// released, and the report must say *which* slot that was rather than leave it to be guessed. Naming
  /// it also keeps the case the contract requires (an outgoing finalizer beside an incoming capture,
  /// DEC-078 item 5) diagnosable; correcting that census is S4b's role-transfer work.
  ///
  /// No storage: the census is read from the per-slot lease state, and the check allocates nothing.
  uint8_t reportEnvelopeBreachIfAny(uint8_t slot, WorkingSetRole role) const;
  /// S4.1: release every slot whose liveness predicate reports no live owner, through the
  /// `(slot, generation)` entry. The predicate-free `TrackPlaybackRuntime::resetAll` discarded each
  /// slot's refusal; this preserves it.
  void releaseEligiblePlaybackRuntimeSlots(bool preserveLedger);
  /// S4.1: clear **only the stopping interval's** slot ledger. Never the track-global sweep — while a
  /// predecessor finalizes on a sibling slot, sweeping would erase its open-note suppression state.
  void clearStoppingIntervalLedger();
  /// S2b.4: the release entry point. It enforces `noLiveOwnerForSlot` itself so a production caller
  /// cannot release underneath a live owner: the guard must not be merely a convention at call sites.
  bool tryReleaseCanonicalCaptureReservation(uint8_t slot);
  /// Phase 1B — drop revision-keyed materialized events scratch when note edit does not need it.
  bool tryClearCommittedMidiScratch();

  // MIDI events
  /// Returns false when capture admission rejects the event before Loop state mutates.
  bool recordMidiEvents(midi::MidiType type, byte channel, byte data1, byte data2,
                        uint32_t currentTick);
  /// DEC-108 §14 / task 3.12c: the window's own committed content for a slot -- the **current published window's**
  /// event count -- or 0 when no window serves the slot yet. O(1): no allocation, no scan and no resolver, so it is
  /// usable at the MIDI edge. It is the term that makes `L` the capacity of the window that actually serves the
  /// slot rather than a guess about its content.
  /// True when a **window serves** this slot: a completed, published window with a valid interval. The admission limit
  /// is what a window must hold, so this is the presence question its gate asks -- and it is not the same as a
  /// non-zero event count, because an empty interval is a legitimately ready window.
  bool playbackWindowServesSlot(uint8_t slotIndex) const;
  /// DEC-108 item 5 / task 3.13: why the preview lane is being cancelled. The spec's four lifecycle events are three
  /// reasons because an in-session undo/redo is a **session mutation** whose result is a new preview revision.
  enum class PreviewWindowCancelReason : uint8_t {
    /// The session's content advanced (an edit, or an in-session undo/redo): the frozen preview revision is gone.
    SourceMutated,
    /// Another track became the previewed one, so this slot no longer has a live preview source.
    SelectedTrackChanged,
    /// The session ended: the preview source **yields**, and the committed successor becomes owed (the handoff).
    SessionExited,
  };
  /// What a preview cancellation actually did -- named, so "there was nothing to cancel" is distinguishable from
  /// "the build was cancelled" and from "the exit handoff is now owed".
  enum class PreviewCancelOutcome : uint8_t {
    NothingToCancel,
    BuildCancelled,
    HandoffOwed,
    BuildCancelledAndHandoffOwed,
    /// The session had already exited: the handoff was owed before this call and is not re-requested.
    AlreadyHandedOff,
  };
  /// DEC-108 item 5 / task 3.13: request **preview** publication through the same immutable deferred discipline as
  /// committed content. The caller supplies the frozen preview projection and the session revision it belongs to:
  /// `previewRevision` is the lane's **freeze token** and its currency, never the loop's committed revision. The
  /// published window's identity carries `origin == Preview`, so a consumer that requires committed currency refuses
  /// it without inspecting anything else.
  PlaybackViewRequestResult requestPreviewWindow(uint8_t slotIndex, uint32_t previewRevision,
                                                 const MidiEvent* source, size_t sourceCount,
                                                 uint32_t currentPhase);
  /// The preview lane's cancellation. A mutated or removed source cancels the in-flight preview build and invalidates
  /// its operation identity, so a stale completion publishes nothing; a session exit additionally makes the
  /// **committed successor owed** (the handoff). The served window is never torn: at an exit the preview window keeps
  /// serving until the committed successor is adopted.
  PreviewCancelOutcome cancelPreviewWindowForSlot(uint8_t slotIndex, PreviewWindowCancelReason reason);
  /// True when the served window for a slot came from the **preview** source contract (task 3.13).
  bool servedPlaybackWindowIsPreviewForSlot(uint8_t slotIndex) const;
  /// True when the exit handoff is owed for a slot: the preview source has yielded and a committed window is required.
  bool previewWindowHandoffOwedForSlot(uint8_t slotIndex) const;
  size_t playbackWindowEventCountForSlot(uint8_t slotIndex) const;
  /// DEC-108 item 1 / task 4.3: **the ordered event stream the served window holds**, in emission order --
  /// `storage.events[storage.order[cursor]]`, the same rendering the fixed-view emission path uses (`cursor` is a
  /// position in that order, `0 .. playbackWindowEventCountForSlot()-1`). It reads the window that is **served**,
  /// never a prepared successor, and it is read-only, allocation-free and O(1). Nothing in production consumes it
  /// yet: the emission path is task 3.7's switch, so this exists to let the parity proof read what the lane
  /// actually produced instead of inferring it from the identity.
  bool playbackWindowEventAtForSlot(uint8_t slotIndex, uint16_t cursor, MidiEvent& out) const;
  /// The window admission accounting for a slot's **live capture interval**: the window's content, plus the rows
  /// DEC-108 §13(e) / task 4.5: the latched **deadline-contract fault** for a slot -- a second consecutive missed
  /// traversal, distinct from meeting the one-pass-late guarantee, cleared only at an eligible boundary.
  bool deadlineContractFaultLatchedForSlot(uint8_t slotIndex) const;
  /// The **containment** decision for a traversal of `phase` on a slot (task 4.5): nominal, continue the last coherent
  /// revision, or the approved last resort. Total, and it never waits -- the clock is never blocked by a missed
  /// deadline, and no branch rebuilds.
  ContinuityContainment containmentForSlot(uint8_t slotIndex, uint32_t phase) const;
  /// What applying the last resort did, named.
  enum class LastResortOutcome : uint8_t {
    /// The slot had neither a latched fault nor exhausted coverage, so nothing was applied: silence is never
    /// opportunistic, and this is the answer a caller can rely on to leave a healthy slot alone.
    NotNeeded,
    /// The affected slot's notes were closed and that slot fell silent.
    SlotSilenced,
  };
  /// Apply the **approved last resort** (DEC-108 §13f) for a slot: targeted note closure plus affected-slot silence
  /// through the existing **slot-scoped** primitive. Both halves are slot-scoped by construction, which is why this can
  /// never become a global transport stop; and it rebuilds nothing, so it can never become an unbounded rebuild.
  LastResortOutcome applyLastResortForSlot(uint8_t slotIndex, uint32_t phase);
  /// DEC-108 §13(g) / task 4.5: the **off-clock closure reconciliation** for one version handoff. It walks the outgoing
  /// ledger's open notes and the incoming revision's note-ons and writes a fixed-size plan: soundings the replacement
  /// removes (to close at handoff), soundings it re-issues (whose note-ons are suppressed, because an unchanged note is
  /// never retriggered), and soundings it leaves alone. Bounded and allocation-free; `incomingIsWholeLoop` is what makes
  /// absence evidence of removal at all (see `handoffAbsenceMeansRemoval`).
  ClosureSettlement planClosureSettlementForSlot(uint8_t slotIndex, const MidiEvent* events, size_t eventCount,
                                                 bool incomingIsWholeLoop) const;
  /// Task 3.14 / DEC-108 §13(g): the **audible handoff for a committed-identity change** -- the edit batch that an
  /// undo disables or a redo re-enables. `identitiesAreRemoved` says the change takes the affected identities away;
  /// otherwise it re-establishes them. A sounding note whose identity is removed closes at handoff; a sounding note
  /// whose identity is re-established has its note-on suppressed, because it never stopped sounding; every other
  /// sounding note is preserved **untouched**, which is what "unchanged sustained notes receive no early NoteOff or
  /// retrigger" means mechanically. Off-clock and bounded: the presence set comes from the edit passes' own added
  /// events and the ledger walk is O(open notes). Returns the settlement it applied, so a caller can assert the counts.
  ClosureSettlement reconcileAfterEditPassStateChange(uint8_t slotIndex, const EditPassIdList& passIds,
                                                      bool identitiesAreRemoved);
  /// Task 3.14: the same handoff when the affected identities live in a **committed pass's own bodies** -- the
  /// overdub-session undo/redo sites, where a session step disables or re-enables one committed pass. The identities are
  /// read a row at a time from the pass's committed chunk references (bounded, allocation-free), and everything else
  /// about the settlement is shared with the edit-batch case: close the removed, suppress the re-established, preserve
  /// the rest. This is what closes the session case's stuck-note risk -- a note can no longer keep sounding after an
  /// undo removed the pass that carried it.
  ClosureSettlement reconcileAfterCommittedPassStateChange(uint8_t slotIndex, const PassId* passIds, size_t passCount,
                                                           bool identitiesAreRemoved);
  /// Emit the settlement's closures **first** -- before any replacement note-on, which is the ordering the spec
  /// requires -- and discharge exactly those ledger obligations. A preserved note is not touched: neither closed nor
  /// retriggered. Returns the number of closures emitted.
  size_t applyClosureSettlementForSlot(uint8_t slotIndex, const ClosureSettlement& settlement);
  /// accepted in this interval, plus the closures they owe, plus the closure reserve -- all inside `L`. Pure
  /// arithmetic over state that already exists, which is what "preallocated, bounded accounting that includes
  /// outstanding reservations" means at the edge: nothing allocates, nothing is scanned, and the effective-content
  /// resolver is never invoked.
  RuntimePlaybackViewConfig::WindowAdmissionAccounting captureIntervalAdmissionForSlot(uint8_t slotIndex) const;
  /// Advance committed playback. `emitMidiOutput` sends on this track's MIDI channel
  /// and output ports; cursor and ledger always run.
  void playMidiEvents(uint32_t currentTick, bool emitMidiOutput);
  void playMidiEventsForSlot(uint8_t slotIndex, uint32_t currentTick, bool emitMidiOutput);
  /// True when this slot has an audible playback source: committed passes, or a
  /// deferred Record interval whose bindable bar-1 return window a pending job owns
  /// (S4f.19). It is the admission predicate for both playback entry points; an
  /// unpublished interval with no bound window emits nothing.
  bool hasAudiblePlaybackSource(uint8_t slotIndex) const;
  /// FLASH-resident S4f.19 onward service: adopt the onward window covering the playhead
  /// for an unpublished deferred Record interval, or advance its build from the job's own
  /// frozen source by one bounded grain. Returns the fixed view to serve, or nullptr.
  const RuntimePlaybackView* adoptOrServiceOnwardRecordWindow(
      Loop& loop, LoopPlaybackRuntime& runtime,
      const PlaybackViewSourceIdentity& sourceIdentity, uint32_t currentTick);
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
  /// S4.5 device diagnostic: the predicate that refused the last Record-stop admission, or nullptr
  /// when it was admitted. A refused stop is latched as an accepted obligation (DEC-067), so the
  /// caller reports this name on the `RECS,stage,deny_reason` row instead of leaving the board silent.
  const char* recordStopDenyReason() const;
  /// S4f.5: true while an interaction is accepted and not yet executed. Consumers that mean
  /// "a stop is in flight" must consult this alongside `hasPendingDeferredOverdubStop`,
  /// because the accepted request exists before any job does.
  bool hasAcceptedUnexecutedInteraction() const;
  /// S4f.5: the interval is still capturing (RECORDING or OVERDUBBING).
  bool hasLiveCaptureInterval() const;
  /// S4f.2/B06: does this track have realtime deferred work worth an idle turn? The scheduler uses this to
  /// skip stopped tracks, and a track holding an **accepted, not-yet-executed interaction** must never be
  /// skipped -- its drive would never be reached, which is the stopped-track reachability gap (B06). This is
  /// also the single place a later stage adds its own eligibility term (a pending switch, a pending
  /// reconstruction) instead of duplicating the scan in `main.cpp`.
  bool hasRealtimeDeferredIdleWork() const;
  /// S4f.5: "a stop is in flight" -- an admitted job owns the slot's finalization resources, **or**
  /// a stop is accepted and not yet executed. The latch exists before any job does, and once S4f.3
  /// lets an accepted stop outlive a state change, `isOverdubbing()` stops being a proxy for it.
  /// Every consumer of `hasPendingDeferredOverdubStop()` that means "a stop is in flight" consults
  /// this instead. `isRecording()` is deliberately not a term: a record interval with no stop
  /// obligation is not a stop in flight (use `hasLiveCaptureInterval()` alongside).
  bool hasStopInFlight() const;
  /// S4f.6: whether a live stop obligation targets **`slot`** specifically. The deferred job carries
  /// `job.slot` and the latch carries `slot`, so a stop that has bound to A must not be read as
  /// blocking a *different* target: after a switch handoff the successor on another slot must be able
  /// to start while the predecessor finalizes.
  bool hasStopInFlightForSlot(uint8_t slot) const;
  /// S4f.21: the geometry result of an interval's Record stop, qualified by the job that owns it.
  /// The stop edge stamps the interval's **predicted** length from the maintained maximum-tick
  /// summary; an all-filtered take publishes zero instead. A caller that latches a master length or
  /// auto-aligns from that stamp can therefore adopt geometry that is never published, so it must
  /// consume this instead: `Final` means no stop is in flight for `slot`, and `loopLengthOut` is the
  /// authoritative committed length (zero for a take that published nothing). `PendingStop` means
  /// an admitted job or a retained obligation still owns the slot's geometry -- the caller holds
  /// its obligation until a later boundary reads `Final`, rather than reading a prediction.
  ///
  /// Deliberately mode-neutral: it reports the slot's stop obligation, not the capture mode, so a
  /// Record stop and an overdub stop resolve the same way.
  enum class RecordStopGeometryState : uint8_t { Final, PendingStop };
  RecordStopGeometryState recordStopGeometryForSlot(uint8_t slot, uint32_t& loopLengthOut) const;
  /// S4f.21: a slot-qualified `setLoopLength`. The auto-align continuation consumes a geometry
  /// result that names **its own** slot, which by then may no longer be the active one, so the
  /// active-slot setter would rewrite the wrong interval's length.
  void setLoopLengthForSlot(uint8_t slot, uint32_t ticks);
  /// S4.1: the slot a **capture work item** must be keyed to -- the interval's own slot, never a
  /// stand-in for it. The live capture's slot *is* the active slot (only it may RECORD/OVERDUB), but a
  /// stop in flight is pinned to ITS interval's slot across an active-slot change (S4f.3), so the active
  /// index must not be substituted for it. Returns false when nothing is capturing and no stop is in
  /// flight, so a caller refuses rather than guesses. Kept here, on the owner, so the rule has one
  /// definition and one fixture instead of being re-derived per consumer.
  bool captureWorkSlot(uint8_t& slotOut) const;
  /// S4.1: the slot an **in-flight stop** is pinned to, by identity rather than by the active index.
  /// A deferred stop keeps its interval's slot across an active-slot change (S4f.3), so a consumer
  /// that means "the interval being finalized" must read this and must **not** substitute
  /// `getActiveLoopIndex()`: that would key the interval's work to whichever slot happens to be active
  /// when the work runs. False when no stop is in flight, which is when the active index is the
  /// authority (the live capture's slot, since only the active slot may RECORD/OVERDUB).
  bool pinnedStopSlot(uint8_t& slotOut) const;
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
  /// S4f.20/S4f.21: the Track-level derived-cache invalidation a committed pass in `slot` owes its
  /// consumers, plus (optionally) the deferred REVT queue for that slot. Slot-qualified because the
  /// deferred Record publication runs while a successor may already own the active slot.
  void invalidateCommittedContentForSlot(uint8_t slot, bool queueRecordRevts);
  /// The slot-qualified body of `invalidateLoopDerivedCaches`; the active-slot form delegates here.
  void invalidateLoopDerivedCachesForSlot(uint8_t slot);

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
  /// S4f.20/S4f.21: the slot whose committed Record pass owes the deferred REVT note-on queue, or
  /// `Config::INVALID_LOOP_SLOT` when nothing is owed. This replaces the boolean pending flag with
  /// the same one byte: the queue is owed by the interval that **published**, and the deferred
  /// publication runs while a successor may already own the active slot, so an active-slot read
  /// would scan the wrong loop (or nothing at all).
  uint8_t deferredRecordRevtsSlot = Config::INVALID_LOOP_SLOT;
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
    CapturePhase mode = CapturePhase::None;
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
    /// The job owns partial and complete pass references. Readers only borrow a
    /// completed receipt; a successor never shares this construction slot.
    PendingCapturePass pendingPass;
    LoopEventStoreCommitCursor passTransfer;
    bool ownsPendingPass = false;
    /// S4f.20: job-owned Record undo geometry. Captured at the interval's Record
    /// bind and consumed fail-closed by the deferred publication, so a live
    /// successor's record start can never rewrite this interval's baseline.
    UndoLoopGeometry recordBeforeGeometry{};
    UndoLoopGeometry recordAfterGeometry{};
    LoopId recordGeometryLoopId = kInvalidLoopId;
    bool hasRecordGeometry = false;
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
    LoopId loopId = kInvalidLoopId;
    uint64_t interval = 0;
    uint32_t phaseTick = 0;
    /// NO transport epoch (architect decision 2026-09-23). The drive re-derives the destination from
    /// live transport state, re-derives the boundary tick from the live clock, and validates
    /// `(kind, slot, interval)` before acting, so a stored counter would never be compared and would
    /// add no protection. Transport *history* is covered by **consumption plus identity validation**,
    /// not by an epoch: an accepted stop is consumed by the admission that binds it, by the
    /// transport-stop entry that retargets or completes it, or by a synchronous completion -- so a stop
    /// accepted before a transport stop can never later be driven with a PLAYING destination.
    /// S4f.8: the accepted stop's waits are counted as scheduling, never as refusals.
    uint32_t waitCount = 0;
    /// S4f.2/S2b.6: report the wait once per episode rather than once per deferred turn.
    bool waitReported = false;
    CapturePhase mode = CapturePhase::None;
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
    /// S4f.18 Record-only source staging. One per track because the worker binds the
    /// FIFO head, so only one job per track stages at a time. External to `Track`
    /// (the ring is externally allocated), so it adds no DTCM field.
    struct RecordStagingReceipt {
      uint64_t sourceGeneration = 0;
      uint32_t frozenGeneration = 0;
      const void* owner = nullptr;
      size_t stagedRows = 0;
      bool bound = false;
    };
    RecordSourceStaging::Cursor recordStaging;
    RecordStagingReceipt recordStagingReceipt;
    /// S4f.18: the per-track Record preparation role (composition cursor + union
    /// scratch). Lives in the externally allocated ring, so `Track` gains no DTCM
    /// field; the ring's reviewed size pin is re-based with its reason.
    RecordPreparation::Role recordPreparation;
  };
  struct CaptureStopJobsDeleter {
    void operator()(CaptureStopJobs* jobs) const;
  };
  std::unique_ptr<CaptureStopJobs, CaptureStopJobsDeleter> captureStopJobs_;
  bool prewarmCaptureStopJobs();
  /// DEC-108 item 1: the window owner's two private halves -- one grain's outcome (and its named row), and
  /// the row itself. Private because the owner's surface is the three calls above; nothing else may advance a
  /// grain or report one.
  PlaybackViewTurnResult finishPlaybackViewGrain(uint8_t slotIndex, RuntimePlaybackViewBuildGrain grain);
  /// DEC-108 item 1: install a completed build as this slot's current interval window (atomic). False when
  /// there is nothing completed to publish, which is a refusal rather than an error.
  bool publishCompletedPlaybackWindow(uint8_t slotIndex);
  void reportPlaybackViewMaintenanceRow(uint8_t slotIndex, Loop& loop, const char* path);
  /// S4.5: finish any stop descriptor the retirement path left un-rearmed and re-arm it, one bounded
  /// grain per idle turn. The ring advances at retirement *before* the source release finishes, so a
  /// release that needs another grain (or a re-reservation that fails that instant) leaves the
  /// descriptor not-`ready` with no later attempt -- and the next admission then denies
  /// `raw_range_unsealable` forever (measured: `captures/s45-deny-20261002-174528-od2`). This owns that
  /// lost obligation without new state: the descriptor's own un-ready range *is* the obligation.
  bool serviceCaptureStopDescriptorReservations();
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
  /// S3.0 placement and size pins (runtime-working-set-ownership). A member context so the private
  /// nested types are nameable and `Track` itself is complete; never called, so it emits no code.
  /// The change sits on the RAM1 FlexRAM bank edge, where one crossed bank costs 32,768 B and drops
  /// the stack into the documented silent boot hang. `Track` is the eight-way DTCM array, so a
  /// by-value member costs 8x: holding the latch as a `std::unique_ptr` here measured 64 B of RAM1
  /// and took padding from 872 to 856, which is why it lives in the externally allocated ring object.
  /// Re-measure with `template <size_t N> struct Probe; Probe<sizeof(X)> p;` before changing a number.
  static void placementPins() {
    // Platform-independent: the latch holds no pointers, so a drift is a contract change.
    static_assert(sizeof(PendingInteractionLatch) == 32,
                  "S3.0: the accepted-interaction latch is one small record per live interval "
                  "(no transport epoch: history is covered by consumption + identity validation)");
    static_assert(std::is_trivially_copyable<PendingInteractionLatch>::value,
                  "S3.0: the latch must stay a POD -- the drive copies it and no edge may allocate");
    static_assert(offsetof(PendingInteractionLatch, slot) == 2,
                  "S3.0: the interval's slot is the latch identity; keep it early and stable");
    static_assert(offsetof(PendingInteractionLatch, interval) == 8,
                  "S3.0: the latch's interval identity offset is pinned with its slot");
    // Absolute sizes are CONFIGURATION-specific: `Track`'s layout depends on build flags
    // (`SESSION_CAPTURE`, `PERF_TELEMETRY`), so a plain `teensy41` build failed this pin. Scope the
    // measured values to the configuration they were measured in. The configuration-independent
    // guard is the post-link RAM1 budget check (`scripts/check_ram1_budget.py`), which runs for every
    // firmware env against a per-env baseline.
#if defined(__IMXRT1062__) && defined(SESSION_CAPTURE) && defined(PERF_TELEMETRY)
    static_assert(sizeof(Track) == 384,
                  "S3.0: Track is the eight-way DTCM array, so a by-value member costs 8x "
                  "(measured for teensy41-capture-serial; re-measure for another configuration)");
    static_assert(sizeof(CaptureStopJobs) == 28936,
                  "S3.0/S4f.18: the externally allocated ring object also carries the latch, the "
                  "Record source-staging cursor/receipt and the per-track Record preparation role; "
                  "its size is a RAM1-relevant decision, not incidental (re-based 2984 -> 3072 for "
                  "staging, then 3072 -> 28808 for the 25,736 B preparation role: composition "
                  "cursor + 24,576 B union scratch, then 28808 -> 28936 for the +32 B per job of "
                  "job-owned Record undo geometry: two 12 B UndoLoopGeometry receipts, the LoopId "
                  "and the presence flag). The role is externally allocated, so this is "
                  "PSRAM, not DTCM; the post-link RAM1 gate is the DTCM authority.");
#endif
  }
  /// Latch an accepted stop for `slot`/`interval`; a same-identity repeat is a no-op.
  void latchAcceptedStop(PendingStopDestination destination, uint8_t slot, uint64_t interval);
  /// Consume the stop obligation of a successfully admitted (or synchronously completed) stop.
  /// `reason` is the consumption edge's own outcome literal (`admitted`, `superseded`, `sync_stop`,
  /// `transport_stop`, …), reported by the latch's sequenced evidence (task 2.2 of
  /// `response-driven-hitl-acceptance`): an obligation consumed between two polls must stay provable, and
  /// the clear itself erases the identity that would prove it.
  void consumeAcceptedStopObligation(const char* reason);
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
  /// S4f.4 transition 3 / S4f.21: retire a job whose interval identity is gone (the slot was
  /// loaded, replaced or cleared underneath it). Releases everything the job owns and retires its
  /// ring entry **without** touching the live interval, so a superseded finalization cannot
  /// resurrect or mutate a successor's content. Returns false while a long extent is still
  /// returning its grains; the prologue re-enters until it is complete.
  bool supersedeDeferredStopJob(CaptureStopJob& job);
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
  /// Prepare at most one job-owned Record terminal Off into reserved output.
  /// Does not admit Record stops, publish, or touch the live capture producer.
  LoopEventStoreRewriteResult stepDeferredRecordClosure(CaptureStopJob& job);
  /// Bounded copy of the job's frozen raw take into the private Record output,
  /// after the terminal closure Offs. Record-only; advances to `CanonicalOrder`.
  LoopEventStoreRewriteResult stepDeferredRecordSourceStaging(CaptureStopJob& job);
  /// Bounded preparation composition over the merged private Record output:
  /// canonical order, identity completion, wrap repair and minimum-length
  /// policy. Record-only; advances to `RecordPrepared` (publication is S4f.20).
  LoopEventStoreRewriteResult stepDeferredRecordNormalization(CaptureStopJob& job);
  /// Bounded staging of the normalized private Record output into the job-owned
  /// transfer receipt. Record-only; advances to `RecordPassReady` (the atomic
  /// publication transaction is S4f.20 completion).
  LoopEventStoreRewriteResult stepDeferredRecordPassTransfer(CaptureStopJob& job);
  /// Record-only atomic publication over `RecordPassReady`: commit the staged pass,
  /// push its Record undo, queue the interval's session-exit snapshot publication
  /// (`Loop::queueSessionExitSnapshot` -- the only enumeration of the committed Record
  /// pass into the pending snapshot box, which the inline stop performed and this route
  /// must own, DEC-094) and admit persistence once. Advances to `RecordPublished`.
  LoopEventStoreRewriteResult stepDeferredRecordPublication(CaptureStopJob& job);
  /// Record-only terminal over `RecordEmpty`: an accepted Record take that
  /// prepared no content keeps the inline route's no-content geometry/undo/
  /// persistence semantics and advances to `RecordPublished` for retirement. It
  /// never publishes a pass and never discards a live successor's producer.
  LoopEventStoreRewriteResult stepDeferredRecordEmptyResult(CaptureStopJob& job);
  /// Record-only bounded retirement of a published job: release its frozen source,
  /// clear the runtime owner and advance the ring. Advances out of the ring.
  LoopEventStoreRewriteResult stepDeferredRecordRetirement(CaptureStopJob& job);
  /// Capture this interval's job-owned Record undo geometry and consume the Track's
  /// inline record-start baseline. Called at the Record bind, never at publication.
  void captureRecordGeometryReceipt(CaptureStopJob& job);

  /// One bounded turn of the deferred undo-identity collection. Returns true when
  /// the job's prepared entry is ready (or the session had nothing to record).
  bool stepDeferredUndoCollection(CaptureStopJob& job, Loop& loop, PendingStopAdmission& stop);
  /// One bounded turn of the protected transaction's prepare (begin + detach
  /// redo). Returns true when the entry is Ready to publish at the atomic commit.
  bool stepDeferredUndoPublication(CaptureStopJob& job, PendingStopAdmission& stop);
  /// Deferred-route only: build this interval's `OverdubPassAdded` entry from its **own
  /// session-exit receipt** (DEC-068) so the two short routes -- a clean stop's zero-row jump
  /// straight to the transfer tail, and the STOPPED destination -- still carry an undo entry and
  /// the sidebar `U:` gains its level. The entry crosses the pass's publication boundary through
  /// the existing protected transaction (DEC-063). Returns false while the interval's final pass
  /// identity is not yet stamped. Never used by the full CompanionHistory/UndoCollection route.
  bool prepareDeferredOverdubUndoEntryFromReceipt(CaptureStopJob& job, Loop& loop,
                                                  PendingStopAdmission& stop);
  /// One bounded turn of the protected transaction's post-commit drain. Returns
  /// true once the transaction is back to Idle.
  bool stepProtectedUndoDrain();
  uint64_t captureInterval_ = 0;
  /// Advance identity only after the capture state transition succeeds.
  bool beginCaptureInterval(TrackState nextState);
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
  void queueDeferredRecordRevts(uint8_t slot);
  /// `allowMerge` false keeps the slice to the bounded chunk walk; the full-merge
  /// fallback is a stop/idle-only cost and is skipped instead.
  void processDeferredRecordRevts(size_t maxEventsPerSlice = 64, bool allowMerge = true);

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
  /// S4f.17: admit a **Record** stop through the same ring/latch drive. Binds the interval's
  /// own slot/interval, its frozen raw range (under a no-partial-cut preflight), its
  /// held-note receipt and its job-owned geometry; the final length comes from the
  /// maintained maximum-tick summary, so no store walk runs at the stop edge. Every
  /// fallible step is resolved before the ownership transfer, and the private prepared
  /// output must already be admitted (reserved at the record start).
  bool beginDeferredRecordStopForSlot(PendingStopDestination destination, uint32_t currentTick,
                                      uint8_t slot, uint64_t interval);
  /// S4f.17: the transport edge of an **admitted** Record stop. The admission has already
  /// frozen the interval and stamped its final length from the maintained summary, so this
  /// is the bounded scalar part of the old inline stop: origin alignment, the truncation
  /// rewind, the loop phase, the destination state and, for PLAYING, the record-stop
  /// playback handoff. It never walks the store and never publishes content: the deferred
  /// worker owns the pass, the undo entry and persistence.
  void completeAdmittedRecordStop(PendingStopDestination destination, uint32_t currentTick,
                                  uint8_t slot, uint32_t rawLength);
  /// Move this interval's held-note receipt into the job. Both admissions (Record and overdub)
  /// transfer through this one helper so the ownership move has a single reviewed home.
  ///
  /// Cost note: the transfer stays out-of-line. `std::unordered_map::swap` is emitted as a
  /// 160 B weak `_Hashtable::swap` that the linker keeps in ITCM and reaches from FLASH
  /// through an 8 B veneer; routing both call sites here did not inline it. The ITCM budget
  /// for this slice was therefore reclaimed elsewhere (`PersistenceDiagnostics::maybeEmitPeriodic`,
  /// moved to FLASH).
  void takeHeldNotesInto(CaptureStopJob& job);
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
