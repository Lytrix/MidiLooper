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
#include "Utils/PlaybackBuildRefusal.h"
#include "TrackState.h"
#include "TrackDisplayState.h"
#include "Loop.h"
#include "Utils/TrackMem.h"
#include "Utils/RecordSourceStaging.h"
#include "Utils/RecordPreparationRole.h"
#include "GlobalUndoStack.h"
// Included for `TRACK_COLD_MEM` (task 3.15g accessors are defined out of line and FLASH-placed).
#include "LoopEventStore.h"
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
/// DEC-178 evidence: how many deferred stop grains the last pending-stop owner turn delivered (1 = the old cadence,
/// >1 = a backlog burst). Read-only; no production path consults it.
uint8_t stopGrainBurstLastTurnGrains();

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
  bool startRecording(uint32_t startLoopTick);
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
      uint8_t snapshotConstructionUnits = MidiServiceDrain::kSnapshotIdleConstructionUnitsPerTurn,
      /// True while any track is recording or audibly playing (the same predicate `main.cpp` uses for
      /// the load/save gates). Unbounded children are named-deferred on such a turn (tasks 2.3-2.5).
      bool timingCritical = false);
  /// CONTRACTS.md C3.1: qualify the active overdub session's private predecessor at the session-open
  /// edge. Binds the identity only; performs no content work, so it costs nothing on the start edge
  /// and the deferred grain still owns every unit.
  bool bindPrivatePredecessorSourceForOverdub(Loop& loop);
  /// CONTRACTS.md C3.2/C3.3: ONE bounded grain of the active slot's private-predecessor source
  /// preparation. The receipt this Track owns is the whole identity, so the Loop is handed a
  /// binding and never a Track type; a receipt that published, retired or moved its interval
  /// discards the cursor instead of being read.
  Loop::PrivateSourceStep stepPrivatePredecessorSourceGrain(Loop& loop);
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
  bool prewarmPlaybackForSlot(uint8_t slotIndex, bool prepareCapture = true, bool restamp = false,
                              bool skipViews = false);
  /// DEC-231: identity-qualified resumable view preparation. The Record edge may only validate/adopt
  /// (`viewPrewarmReadyForSlot`); `stepViewPrewarmForIdle` is the off-edge owner that advances one
  /// natural unit per idle turn.
  uint32_t viewPrewarmIdentityForSlot(uint8_t slotIndex) const;
  bool viewPrewarmReadyForSlot(uint8_t slotIndex);
  void requestViewPrewarmForSlot(uint8_t slotIndex);
  bool stepViewPrewarmForSlot(uint8_t slotIndex, uint8_t maxUnits = 1, uint32_t budgetUs = 5000);
  bool stepViewPrewarmForIdle();
  bool hasViewPrewarmPending() const;
  uint16_t viewPrewarmProgressForSlot(uint8_t slotIndex) const;
#if defined(PIO_UNIT_TEST_NATIVE)
  /// Host-only seam: existing fixtures drive the state machine synchronously in `startRecording`.
  /// The edge REDs set this to true to exercise the production strict path (no fallback).
  static bool& viewPrewarmStrictForTest();
#endif
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
    /// Task 3.10 (the review's step 1): the gathered window holds **more entries than a window can retain**
    /// (`RuntimePlaybackViewConfig::kWindowAdmissionLimit`), so its work cannot be bounded by the window that would
    /// have to hold it. It is **refused rather than truncated** -- accepted content is never silently dropped.
    WindowExceedsRetainedCapacity,
    /// Task 3.10 (the review's step 2): admitting this window would **break the promise already made** to a window on
    /// this track -- after charging all outstanding work, some already-admitted window could no longer reach its own
    /// deadline. The request is refused by name, so it can be re-issued when the interference clears; it is never
    /// silently dropped and the existing window's deadline is never traded away.
    TrackInterferenceBudgetExceeded,
    DeferredSourceQualification,
  };
  enum class PlaybackViewTurnResult : uint8_t { Idle, GrainServed, BuildComplete, GrainDenied };

  /// Enforced admission requires a qualified clock stamp; the default is explicitly unqualified.
  /// Phase and stamp must describe the same instant/projection. Reset, seek or re-projection
  /// must invalidate the epoch before a pending deadline is checked again.
  PlaybackViewRequestResult requestPlaybackViewMaintenance(
      uint8_t slotIndex, const MidiEvent* source, size_t sourceCount,
      const PlaybackViewSourceIdentity& sourceIdentity, const PlaybackCoverageInterval& coverage,
      const EmissionCursor& cursorAnchor, const LedgerBoundarySummary& ledgerBoundary,
      uint32_t currentPhase, uint32_t handoffPhase, PlaybackDeadlineClock clock = {});
  bool hasPendingPlaybackViewMaintenance() const;
  enum class RollingOutcome : uint8_t { Disabled, Excluded, Busy, Requested, Waiting, Ready, Serving, Expired, Rejected };
  RollingOutcome serviceRollingPlaybackWindow(PlaybackDeadlineClock clock);
  /// windowed-playback-generation 3.7e: **why the rolling frame did not adopt a bound launch window.** The seed is a
  /// silent "no" for every slot that carries no launch at all (the ordinary case), but a slot whose launch exists and
  /// was *not* adopted is a decided refusal, and a device run must be able to name the term instead of inferring it from
  /// flags. `win_seed_refused` carries this ordinal in `rq`; `launch_seed_not_bound` carries the launch row's own
  /// `prep` beside it.
  ///
  /// The witness is the **commit's own bind** (`launchBoundTick`), not the handoff view's `launchPreparation`:
  /// `boundLaunchView` makes the launch's expiry/invalidation sticky by clearing `launchPreparation` while leaving the
  /// bound view and the bind's tick intact, and that sticky term answers "may the *handoff* still serve this view",
  /// which is a different question from "did this slot's playback start inside the window the commit bound".
  enum class LaunchSeedRefusal : uint8_t {
    Adopted = 0,
    /// No launch on this slot at all: nothing ever bound this slot's playback to a launch window (the ordinary
    /// demoted/restored slot). Not reported.
    NoLaunch = 1,
    /// A launch exists but is in a **live** preparation this seed cannot read as a window: `Requested`, `Building`,
    /// `Ready`, `Fallback` or `FallbackBound`. A `Ready` launch here means the switch committed **without binding it**,
    /// which is the commit-side gap this seed exposes.
    NotBound = 2,
    /// The bound window no longer names the loop: slot view role, storage generation, source origin, generation,
    /// revision, snapshot identity, coverage geometry, or a promise/owner/builder holding the slot.
    Identity = 3,
    /// The window names the loop but its coverage does not contain the playhead.
    Uncovered = 4,
    /// The playhead has left the traversal the bind named: the window's interval ended before this turn. The next
    /// traversal covers the same phases from **newer** content, so re-serving this view would be serving a stale read.
    Expired = 5,
  };
  struct RollingResolution { RollingOutcome outcome; const RuntimePlaybackView* view; };
  /// The **slot index of a loop this track owns** (`255` when it owns none of them), so a caller that holds a `Loop&`
  /// -- the deferred owners do, through their selectors -- can name it on a row without duplicating the address walk.
  uint8_t playbackSlotIndexForLoop(const Loop& loop) const;
  RollingResolution resolveRollingPlaybackWindow(uint8_t slot, uint32_t tick, PlaybackDeadlineClock clock);
  void cancelRollingPlaybackWindow(uint8_t slot);
  // Off-clock initial launch preparation; no absolute promise before binding.
  bool preparePlaybackLaunchForSlot(uint8_t slotIndex);
  bool playbackLaunchReadyForSlot(uint8_t slotIndex) const;
  bool bindPlaybackLaunchForSlot(uint8_t slotIndex, uint32_t tick);
  /// windowed-playback-generation 3.7e — **the rolling frame adopts the launch window it already owns.** A committed
  /// switch target binds its launch's phase-0 two-bar window (`bindPlaybackLaunchForSlot`) and then starts playback at
  /// that window's own phase; the rolling frame, however, began `Idle`, so it opened a fresh demand, deferred to source
  /// qualification and charged a whole-loop build against a rolling span's handoff (device
  /// `captures/i5-s37d-device-32bar-10`: `win_requested rq=12` -> `handoff_unreachable` x12 -> `win_blocked_by_budget`).
  /// This seeds the frame to serve the **current** view when -- and only when -- that view's slot, generation, revision,
  /// snapshot identity and coverage all still name the loop as it is now, so a stale window can never masquerade as
  /// served. It publishes nothing, builds nothing and cancels nothing; when any identity has moved it names the
  /// refused term (`LaunchSeedRefusal`) and the lane keeps its ordinary demand path.
  LaunchSeedRefusal seedRollingFrameFromBoundLaunch(uint8_t slotIndex, uint32_t generation,
                                                    const PlaybackDeadlineClock& clock, uint32_t phase);
  void cancelPlaybackLaunchForSlot(uint8_t slotIndex, bool includeBound = false);
  /// windowed-playback-generation, the switch leg's launch-path diagnostic: the **launch gate row** for one target
  /// slot and one exit token. Every exit of `preparePlaybackLaunchForSlot` (and the decisions its caller takes before
  /// reaching it) used to return silently, which is why five device runs could not tell "nothing was owed"
  /// from "the source was refused"; this reports the exit **once per state change** with the target's identity and the
  /// inputs that decided it. The flags are assembled here, from this slot's own state, so a call site names the exit
  /// and (only where it alone knows them) the two republish bits.
  /// **Qualification-only**: `SC_LAUNCH_GATE` is the emitter seam and expands to nothing unless the capture build also
  /// opens the rolling lane's gate. It observes; it never admits, refuses or reorders anything.
  void reportPlaybackLaunchGateRow(uint8_t slotIndex, const char* exit, bool sourceRequestAttempted = false,
                                   bool sourceRequestAccepted = false);
  /// The three decisions `TrackManager::servicePendingSlotProvision` takes **before** reaching
  /// `preparePlaybackLaunchForSlot`, named as an enum so the caller's exit tokens are owned by the reporting TU exactly
  /// like every other exit's are. Not cosmetic: the tokens are DTCM string literals, and three of them added from the
  /// slot matrix's translation unit crossed the linker's `ALIGN(1024)` edge and cost 4 KB of the RAM1 local allowance
  /// (the pattern `.cursor/rules/Teensy-RAM1-Bank-Overflow.mdc` records; that TU has no progmem carve-out).
  enum class PlaybackLaunchCallerExit : uint8_t {
    ProvisionRefused,  ///< the target's inbound occupancy was refused; the switch stays pending.
    NotReachedLegacy,  ///< not an immutable-launch target (the active slot, or a runtime length below the bound).
    NotReachedOccupied,///< not an immutable launch, and the legacy merged path already owed nothing: never reached.
  };
  void reportPlaybackLaunchCallerExit(uint8_t slotIndex, PlaybackLaunchCallerExit exit);
  /// The `Snapshot` deferred owner's **gate row** (the same qualification-only instrument as the launch gate, one owner
  /// upstream): which loop `loopWithPendingSnapshotPublish` selected, what that loop's own pending terms are, and which
  /// of the turn's gates refused. It reports **once per state change** for this track, like the launch gate, and reads
  /// only state that decided the turn -- it neither admits nor refuses anything.
  /// `selectedSlot` is the slot the caller passed to `reportSnapshotOwnerGateRow` (the loop the selector returned, bound
  /// beside `activeLoopIndex` so a reader sees whether the target slot ever got the turn); `stepAdmits` is
  /// `allowSnapshotIdleStep`, `clauseAdmits` its four-term clause, `idleAllowed` their conjunction and `eligible` the
  /// composed `snapshotEligible`.
  void reportSnapshotOwnerGateRow(uint8_t selectedSlot, uint8_t activeSlot, uint8_t constructionUnits,
                                  bool liveRecordPastFirstBar, bool visualCacheNeedsFirstPaint, bool stepAdmits,
                                  bool clauseAdmits, bool idleAllowed, bool eligible);
  /// The **rolling demand path**'s gate row (the same qualification-only instrument as the launch and snapshot gates,
  /// on the third silent family of the demand side). Every exit of `serviceRollingPlaybackWindow` returns a typed
  /// `RollingOutcome` to its caller with no row, so on hardware "this track owed no window" and "the window was
  /// refused -- and by which term" were indistinguishable (F-16). The flags are assembled here, from the **active
  /// slot's own state**, so a call site names the exit and only the facts it alone knows: whether the clock it was
  /// handed is stamped, whether this tick fell outside the projection, and the request result the exit observed.
  /// Reports **once per state change**, never per turn. It observes; it never admits, refuses, reorders or retires
  /// anything.
  /// `clockEpochStamped` is `clock.epoch != 0` for the clock the turn was called with; `reprojectedTick` and
  /// `requestResult` are the two facts only the call site knows. Everything else is read from the **active slot's own
  /// state**.
  void reportPlaybackRollingGateRow(const char* exit, bool clockEpochStamped = false, bool reprojectedTick = false,
                                    uint8_t requestResult = 0);
  /// DEC-108 §13b / task 3.10 (the review's step 2): the **per-track outstanding-work budget** -- the sum of every
  /// active build's **full** admitted grains on this track. Full, not remaining, is deliberate: it is an **upper
  /// bound** on what is still owed (a served grain is charged again), which is the safe direction for a budget. A
  /// tighter figure needs the build's remaining-grain count, which is not observable today.
  uint16_t playbackViewOutstandingGrains(uint8_t excludingSlot = Config::MAX_LOOPS_PER_TRACK) const;
  /// DEC-108 §13b / task 3.10 (the review's step 2): whether admitting `candidateGrains` of new work leaves **every
  /// already-admitted window on this track** able to reach its own deadline. Each window is charged its own grains
  /// plus **all** the other outstanding work (the candidate included) -- the deliberate, EDF-free "charge all sibling
  /// work" rule -- so no later request can invalidate an older window's admitted bound.
  bool playbackViewAdmissionKeepsAdmittedDeadlines(const CoverageServiceContract& contract, uint32_t currentPhase,
                                                   uint32_t loopLengthTicks, uint16_t candidateGrains,
                                                   PlaybackDeadlineClock clock = {}) const;
  /// DEC-136 — private committed-root preparation before Capture-ready adoption. `true` only for the demand that
  /// is the **root-backed successor of a bound Record job's frozen return window** for the interval the playhead
  /// is in: the job's frozen source must qualify, the demand's source must be the current published root, a
  /// complete non-raw Capture window built from that job's token must be served or owed, and the requested
  /// coverage must contain the playhead. It is a *classification* only — the route's waiver additionally requires
  /// `ordinaryAdmissionCannotServeDemand`, and no other demand may take the position-free path.
  bool rootReturnSuccessorIsOwedForSlot(uint8_t slotIndex, const PlaybackViewPendingRequest& request,
                                        PlaybackDeadlineClock clock) const;
  /// DEC-136 — the envelope's own impossibility, as a pure rule: no legal lead admits this demand's own bounded
  /// work on this loop geometry, because the lead test reserves `leadTicks` *plus* `grains x (denied+1)` turns at
  /// `maximumPhaseAdvancePerTurn`. `false` for an undeclared contract (an unproven envelope grants nothing) and
  /// for any loop long enough to serve the cost through the ordinary path.
  bool ordinaryAdmissionCannotServeDemand(const CoverageServiceContract& contract, uint32_t loopLengthTicks) const;
  /// DEC-108 §13(c) / task 3.10 (increment 24): the slot's **retained pending preparation request** -- the demand a
  /// refused request left behind -- or null when the slot owes none. **Generation-qualified** (DEC-108 §13(d)): a
  /// record retained under an earlier operation generation is never revived, so a returning slot does not resume a
  /// demand from an operation it no longer runs. A demand no build owes yet is deliberately **not** counted by
  /// `playbackViewOutstandingGrains`: it owes no grain until it is admitted.
  const PlaybackViewPendingRequest* pendingViewRequest(uint8_t slotIndex) const;
  /// The reason the slot's retained positional deadline no longer applies (`DeadlineInvalidatedEpoch` or
  /// `DeadlineInvalidatedGeometry`), or `None` while it does. A **reading**: the obligation survives an epoch or a
  /// geometry change -- only the promise's position is void, and the miss it causes is reported, never compensated by
  /// an inline rebuild (DEC-108 §13(c)).
  PlaybackViewPendingReason pendingViewRequestInvalidation(uint8_t slotIndex, PlaybackDeadlineClock clock) const;
  /// Task 4.5 (the moving closure): **the phase this slot's playback has already served** in the pass that is running,
  /// in the terms the handoff's closure plan uses. `UINT32_MAX` means nothing has been served -- the transport is
  /// stopped, the slot is not the one playing, or no position exists yet -- so an edit's NoteOff is never called
  /// "behind" on the strength of a stopped clock. The lane's own projection is the fallback when the merged walk has
  /// not stamped `lastTickInLoop` (a demoted slot is served by interval windows, not by that walk).
  uint32_t playbackClosureFloorForSlot(uint8_t slotIndex) const;
  /// DEC-108 §13b / task 3.10: serve **one declared grain** of this track's owed window work. `currentPhase` is
  /// the playhead's phase and `cursor` the caller's rotation position across holders: the selection is the shared
  /// policy (`MidiServiceDrain::selectPlaybackHolder`) -- holders rotate, the earliest outstanding handoff is
  /// served first, and the cursor advances even when the chosen holder cannot progress.
  ///
  /// Passing no cursor keeps the pre-3.10 single-holder behaviour (the scan starts at slot 0 every call), which is
  /// why the existing single-owing-slot fixtures need no change; a caller that serves more than one holder passes
  /// its own cursor so the rotation persists across turns.
  #if defined(PIO_UNIT_TEST_NATIVE)
  /// Test-only witness: MIDI turns that selected the live overlay window (slice 3).
  static uint32_t overlayLiveSelectionCount();
  static void setOverlayEdgeDisabledForTest(bool disabled);
  /// **3.15f/C diagnostic:** the exact adoption terms witnessed at the last clock-edge adoption attempt. `firstFalse`
  /// is 0 when all terms held; otherwise the first false term in evaluation order (1 published, 2 incomplete,
  /// 3 positioned tick not reached, 4 not Preview, 5 revision, 6 coverage invalid, 7 coverage loop length,
  /// 8 coverage does not contain the phase, 9 adoptPreparedPlaybackWindow refusal (`refusal`), 10 current role moved,
  /// 11 no ready role). No target code reads it.
  struct PreviewAdoptionWitness {
    uint8_t firstFalse = 0;
    uint8_t refusal = 0;
    uint8_t current = 255;
    uint8_t ready = 255;
    uint32_t previewRevision = 0;
    uint32_t readyRevision = 0;
    uint32_t phase = 0;
    uint32_t loopLength = 0;
    uint32_t coverageStart = 0;
    uint32_t coverageLength = 0;
    uint64_t deadline = 0;
    uint64_t admitted = 0;
    uint32_t workEpoch = 0;
    bool positionedLater = false;
    bool readyRaw = false;
  };
  static PreviewAdoptionWitness previewAdoptionWitnessForTest();
#endif
  static void resetPlaybackLaneMeasurement();
  static void samplePlaybackLaneOpportunity(PlaybackDeadlineClock clock, bool running, bool eligible,
                                            PlaybackViewTurnResult result, uint32_t workerUs);
  PlaybackViewTurnResult stepPlaybackViewMaintenance(
      uint32_t currentPhase = 0, MidiServiceDrain::PlaybackHolderCursor* cursor = nullptr,
      bool transportRunning = false, PlaybackDeadlineClock clock = {},
      PlaybackDeadlineClock (*completionClockReader)() = nullptr);
  /// Sweep all promises, including completed candidates, independently of worker eligibility.
  void serviceAdmittedDeadlineMisses(PlaybackDeadlineClock clock);
  /// Explicit boundary recovery. Keeps the original promise/history; checks the new
  /// projection, source and operation identity before releasing a completed candidate.
  bool revalidateCompletedPlaybackWindow(uint8_t slotIndex, uint32_t currentPhase,
                                         PlaybackDeadlineClock clock);
  /// DEC-194 (report-only): the first false precondition of the revalidation, in its own order.
  PlaybackRevalidateRefusal revalidateCompletedPlaybackWindowRefusal(
      uint8_t slotIndex, uint32_t currentPhase, PlaybackDeadlineClock clock) const;
  /// **DEC-202 -- a late completion is retired, not held.** `completedPromisePendingJudgement` is a *transient*, not a
  /// role lock: while it is set the slot can neither build nor be served by the window lane, and the only clearer
  /// (`revalidateCompletedPlaybackWindow`) is legal only at the candidate's own start phase one loop after its deadline.
  /// This turn judges when that is legal and otherwise releases the completion -- discarding the candidate and freeing
  /// its role for the next demand. The late window is never served: a window built for a past traversal is not content
  /// a later one may present as current. Returns true when a candidate was retired.
  bool retireUnjudgeableCompletedPromise(uint8_t slotIndex, uint32_t currentPhase,
                                         PlaybackDeadlineClock clock);
  /// DEC-108 item 1 / task 3.2: the **interval window the owner published** for a slot -- its identity and the
  /// coverage interval it serves -- or `false` when none is authoritative. Read-only, and deliberately read by
  /// no production consumer yet: adoption is 3.4 (identity on the consumers), 3.5 (readiness predicate) and 3.7
  /// DEC-108 §13b / task 3.10: **what this track offers** the next playback-maintenance opportunity -- its most
  /// urgent owing holder (`eligible` + `handoffLeadTicks`), read without serving anything. The production caller
  /// builds every track's offer and lets the shared policy choose **one track** per turn, so the lowest-index track
  /// with work no longer monopolises the turn; the offer is also what a fixture can assert on.
  MidiServiceDrain::PlaybackHolder playbackWindowServiceOffer(uint32_t currentPhase,
      PlaybackDeadlineClock clock = {}) const;
  /// The holder set the offer and the turn both read: this track's owing slots, each with its urgency. Public only
  /// so the two callers in this owner's own translation unit agree on one description of a holder.
  void fillPlaybackHolders(uint32_t currentPhase,
                           MidiServiceDrain::PlaybackHolder* holders, PlaybackDeadlineClock clock = {}) const;
  /// (the consumer switch), so this slice publishes without changing what any existing consumer reads.
  bool publishedPlaybackWindowForSlot(uint8_t slotIndex, PlaybackViewSourceIdentity& identityOut,
                                      PlaybackCoverageInterval& coverageOut) const;
  /// Qualify this slot's admitted, closure-complete Record stop for the
  /// requester's exact capture interval. Read-only; refusal leaves outputs untouched.
  bool qualifiedFrozenRecordCaptureSourceForSlot(
      uint8_t slotIndex, uint64_t expectedCaptureInterval,
      RuntimePlaybackViewSource& sourceOut,
      PlaybackViewSourceIdentity& identityOut) const;
  /// DEC-108 item 5 / task 3.4: the published window's **identity** -- `(slot, playbackGeneration,
  /// playbackRevision, interval)` -- which is what a consumer compares. `false` when no window is published.
  bool publishedPlaybackWindowIdentityForSlot(uint8_t slotIndex, PlaybackWindowIdentity& out) const;
  /// Task 3.4: **adopt** a published successor as the slot's current window, atomically (the lifecycle's own
  /// handoff). This is the consumer-facing edge task 3.7 calls at an eligible boundary; until then nothing in
  /// production calls it, and a prepared successor sits in the ready role without touching the window that is
  /// being served.
  /// Task 3.15g: `refusalOut` is diagnostics only -- the same checks in the same order, reported instead of hidden.
  /// 0 adopted, 1 no runtime, 2 no view set, 3 the current role is unset, 4 the ready role is unset, 5 the ready view
  /// is incomplete, 6 the lifecycle's preparation handoff declined with every readable term holding. A null pointer
  /// reproduces the previous signature exactly, so no behaviour depends on it.
  bool adoptPreparedPlaybackWindow(uint8_t slotIndex, uint8_t* refusalOut = nullptr);
  /// Task 3.15g: the root-return adoption diagnostics, on the existing `win_frozen_readiness` receipt. Cold (flash)
  /// and transition-cadenced: it emits only when the adoption mask/answer changes for this slot.
  /// Task 3.15g: `fromRetirementHold` names the observing edge, as a bit rather than a string pointer (a pointer
  /// argument cost 64 B of ITCM here, and this image has 16 B of headroom).
  void reportRootReturnAdoptionForSlot(uint8_t slotIndex, const Loop& loop, uint32_t phase,
                                       uint64_t frozenCaptureToken, bool attempted, bool adopted, uint8_t why,
                                       bool fromRetirementHold = false);
  /// 5.2 — first-bar handoff diagnostic (session capture only): the selected track's active-slot serving route
  /// for this tick, on the `win_frozen_readiness` family as `route`. Route: 0 none, 1 bridge, 2 view, 3 paged,
  /// 4 merged, 7 no audible source, 8 stale-runtime reset. One row per change of bar/route/view/ready/rolling.
  void reportFirstBarRouteForSlot(uint8_t slotIndex, uint32_t currentTick, uint8_t route,
                                  const RuntimePlaybackView* view);
  /// The retirement hold's own observation: same receipt family, one-argument call.
  void reportRootReturnHoldForSlot(uint8_t slotIndex);
  /// Session-capture observation after a completed immutable-view cursor turn; no serial on the edge.
  void observePlaybackReadForTurn(uint8_t slotIndex, const FixedPlaybackEventStorage* storage,
                                  const FixedPlaybackBridgeStorage* bridge = nullptr);
  void observePlaybackSnapshotAvailability(uint8_t slotIndex, uint32_t tick);
  /// DEC-108 item 1 / task 3.2: the **interval policy** -- which interval a slot's window serves. A pure
  /// resolution from the loop's geometry and the playhead, one policy for every length (DEC-242): the two-bar window
  /// at the playhead, clamped by the loop, that the live build already uses
  /// (DEC-108 item 8's "prepare ahead of the playhead"; "centred on the playhead" is the present shape of that
  /// rule, not its contract). Public so the resolver, its fixtures and its readers agree on one policy.
  static bool playbackWindowInterval(uint32_t loopLengthTicks, uint32_t playheadPhase,
                                     PlaybackCoverageInterval& out);
  /// **Slice 3.15e/2b-ii — the per-overdub overlay demand.** What the overlay lane owes for a slot, derived from **live
  /// state only** (there is no stored record, so no stale demand can exist): `owed` while an overdub is recording rows on
  /// top of committed content, `coverage` from the committed route's own window policy at the playhead (the overlay replaces
  /// exactly what that route would serve), and `captureGeneration` the identity a prepared window must carry — the same
  /// value the arbitration's overlay claim compares (`PlaybackDemandClaims::overlay`).
  struct OverdubOverlayDemand {
    bool owed = false;
    /// The committed base the overlay would carry exists (a slot with no committed content has nothing to overlay).
    bool baseCommitted = false;
    uint64_t captureGeneration = 0;
    uint32_t playbackRevision = 0;
    PlaybackCoverageInterval coverage{};
  };
  TRACK_COLD_MEM OverdubOverlayDemand overdubOverlayDemandForSlot(uint8_t slotIndex, uint32_t playheadPhase) const;
  /// **Slice 3.15e/2b-ii-b — whether a retained overlay candidate still answers this slot's demand.** A pure reading of
  /// the candidate's own `source`/`coverage` against the live demand; it adds no state and is the single identity rule
  /// the lane turn and the stopped oracle both consume (`OverdubOverlayLane::candidateRetention`). A retired candidate,
  /// a demand that ended or any of revision / capture generation / coverage moving makes the answer `stale`.
  TRACK_COLD_MEM OverdubOverlayLane::CandidateRetention overdubOverlayRetentionForSlot(
      uint8_t slotIndex, uint32_t playheadPhase) const;
  /// **Slice 3.15e/2b-ii-b — the overlay lane's production owner turn.** Run on the same per-track, off-clock visit as
  /// the NOTE_EDIT preview service (`main.cpp`, the `PlaybackViewMaintenance` owner): retire a candidate whose identity
  /// moved before anything else can reuse the borrowed companion storage, otherwise observe the prerequisite and, on
  /// `BaseReady`, drive one grain of the borrowed base fill → continuation → append-stable tail. It never takes a role
  /// and never adopts the candidate as `current`; the edge may bind it as a live source, and the `ActiveCaptureOverdub`
  /// echo runs only on turns where the lane does not serve. The outcome is **named** so a caller (and the fixtures) can
  /// tell progress from a wait, a refusal or a deferral instead of inferring it.
  TRACK_COLD_MEM OverdubOverlayLane::Turn serviceOverdubOverlayWindow();
  /// DEC-108 item 1 / task 3.2: ask the owner to prepare **the window this slot needs**, resolved from the
  /// selected `LoopSnapshot` -- the caller names a **slot and a phase**, never events. `scratch` is the
  /// caller's bounded buffer for the enumeration; the owner neither allocates nor keeps state for it. Returns
  /// `Admitted` when the build is begun (then drive `stepPlaybackViewMaintenance`), or one of the named
  /// refusals above, which are non-destructive.
  /// Caller scratch is exclusive to this slot and immutable until the build's
  /// fill completes/cancels. Busy requests retain demand without touching it;
  /// their pending cost is a capacity bound, recomputed by the next gather.
  PlaybackViewRequestResult requestPlaybackWindow(uint8_t slotIndex, uint32_t playheadPhase,
                                                 SessionMidiEventVec& scratch, PlaybackDeadlineClock clock = {});
  /// Bounded replayable committed request. Caller supplies a qualified slot phase
  /// and matching absolute stamp; no gather, allocation, or caller buffer lease.
  PlaybackViewRequestResult requestCommittedPlaybackWindow(uint8_t slotIndex,
      const PlaybackCoverageInterval& coverage, uint32_t currentPhase, PlaybackDeadlineClock clock);
  /// Ordinary active-slot projection only; jam and queued launch require their
  /// own qualified frame and are refused rather than borrowing the active phase.
  PlaybackViewRequestResult requestCommittedPlaybackWindow(uint8_t slotIndex,
      const PlaybackCoverageInterval& coverage, PlaybackDeadlineClock clock);
  /// 1.4 production-demand seam: ask for the window the **active demoted slot** needs on the control-owner
  /// turn, with the qualified transport clock and the turn's playhead phase. A whole-take (undemoted) slot
  /// returns `NotServable` and keeps its existing route; a `Ready` window is left alone. The request is
  /// additive: the legacy bridge/merged fallbacks stay reachable while the lane is not ready.
  PlaybackViewRequestResult requestActiveCommittedPlaybackWindow(uint32_t playheadPhase,
                                                                PlaybackDeadlineClock clock);
  /// The same demand for a named slot, used at the **record-stop finalize** (before the PLAYING transition) so
  /// the first interval is built within the lead rather than starting on the first PLAYING owner turn.
  PlaybackViewRequestResult requestCommittedPlaybackWindowForSlot(uint8_t slotIndex,
                                                                 uint32_t playheadPhase,
                                                                 PlaybackDeadlineClock clock);
  bool rootQualificationWaitsForCaptureReady(uint8_t slotIndex,
      const LoopPlaybackRuntime& runtime, PlaybackDeadlineClock clock) const;
  /// Task 3.15g: a retained Committed demand is *for* the current published root, so a stamp captured before that
  /// root published is never the truth. Refresh its identity fields from the live root (revision, generation,
  /// published-snapshot identity, loop length) before the retry gate judges it. Identity only: the reason, the
  /// position and the coverage are untouched.
  void refreshRetainedDemandIdentityForSlot(uint8_t slotIndex);
  bool committedPlaybackRetryCanProgress(uint8_t slotIndex, PlaybackDeadlineClock clock) const;
  /// DEC-193 (report-only): the **first failing term** of `committedPlaybackRetryCanProgress`, named once per change by
  /// the retry turn. One implementation of the gate's order; no decision reads the name.
  PlaybackBuildRefusal playbackBuildGateRefusal(uint8_t slotIndex, PlaybackDeadlineClock clock) const;
  PlaybackViewTurnResult stepCommittedPlaybackRetry(uint8_t slotIndex, PlaybackDeadlineClock clock);

  /// Task 3.15h — **coverage-build drop trace, and why it exists**. The board's stall is the job → `build+job` → `job`
  /// cycle: the lane hands the coverage work to a builder and the builder disappears again without publishing, so the
  /// same unsatisfied work returns to the job. The owner-set trace (`refreshRetainedDemandIdentityForSlot`) is
  /// turn-cadenced, so it can name *that* the builder vanished but never *where* or *why*: the release happens inside a
  /// turn, at one of the sites below. Each site names itself through this reason, and the row carries the state the
  /// builder was in when it was released -- whether it had made retryable progress, whether the drop published, and
  /// which record owns the work afterwards. `RetryableNoProgress` (bit 7 of `adoptResult`) is the falsifiable term:
  /// a builder that had done work, published nothing, and handed the identical demand back is the defect shape.
  enum class CoverageBuildDropReason : uint8_t {
    None = 0,
    /// `applyPlaybackViewTransition` released the candidate: a slot transition, not a build decision.
    CandidateReleased,
    /// `cancelPlaybackLaunchForSlot` released an active launch worker.
    LaunchCancelled,
    /// `cancelRollingPlaybackWindow` released a rolling frame's admitted builder.
    WindowCancelled,
    /// `stepCommittedPlaybackRetry` invalidated the frozen demand, then coalesced the build back into `pending`.
    DemandInvalidated,
    /// The declared contract could not complete before the handoff (`CannotCompleteBeforeHandoff`).
    HandoffUnreachable,
    /// The build's own source no longer matched the requested root (current-root path).
    SourceSuperseded,
    /// The build's own source no longer matched the requested root (frozen Record-return path).
    RecordReturnSourceSuperseded,
    /// A grain was denied while the coverage source was owned: the builder could not continue on its own source.
    GrainDeniedWhileOwned,
    /// The overdub lifecycle released the slot's coverage work.
    OverdubLifecycle,
    /// Completion adjudication refused publication (a late completion, or a refused role).
    CompletionAdjudicated,
    /// The builder finished its last grain and no release site ran: `active()` is false for `Phase::Complete`, so the
    /// owner set loses the build bit while the obligation is still owed. Named from the ownership transition itself.
    CompletedUnpublished,
    /// The build bit cleared with the builder neither active nor complete, so the release happened on a path this trace
    /// does not cover. The row reports the phase and counts it was left in rather than guessing a site.
    ReleasedWithoutSite,
    /// The root-backed qualification was begun for this demand (the build's **creation** transition).
    QualificationStarted,
    /// The qualified source was admitted and the build's grains began (the second half of creation).
    BuildAdmitted,
    /// A completed build became the slot's **ready successor**.
    PublishedAsReady,
    /// A completed build became the slot's **served window**.
    PublishedAsCurrent,
    /// A completed build could not become the ready successor -- the ready role already held a complete view.
    PublishReadyRefused,
    /// A completed build could not become the served window -- the current role refused the swap.
    PublishCurrentRefused,
    /// **DEC-202** -- a completion that arrived outside its own window was retired instead of held: the promise could
    /// not be judged at this boundary, so its role was released for the next demand rather than locked forever.
    CompletionUnjudgeable,
  };
  /// The builder's state before a release site runs, packed for one row: bit0 active, bits1-3 `nextGrain()`,
  /// bit4 coverage-owned, bit5 the build role is the completed destination, bit6 live-capture borrow,
  /// bits8-15 qualified events (saturated), bits16-23 ordered events (saturated).
  uint32_t probeCoverageBuildDrop(uint8_t slotIndex) const;
  /// Emit one `build_drop` row for a release that has just happened. Deduped on (reason, before-state, owner set) so a
  /// repeating no-progress release reports its first instance and every change, never a flood.
  void traceCoverageBuildDrop(uint8_t slotIndex, CoverageBuildDropReason reason, uint32_t before);
  /// Task 3.7a (I2): **the window lane's source currency for a slot** -- the current published root, or the retained
  /// revision the slot admitted for exactly the revision it now carries. This is the source a *window* may be built
  /// from; it deliberately does not replace `Loop::publishedSnapshotIsCurrent()` where that answer is the checkpoint /
  /// effective-content authority's (DEC-114), and it changes nothing a gate row reports about the root itself.
  bool playbackWindowSourceIsCurrentForSlot(uint8_t slotIndex) const;
  /// Task 3.7a (I2): begin the **retained-source gather** for this slot's pending demand -- freezing the demand, exactly
  /// as `beginCoverageQualification` does for the root-backed path -- or refuse by name with the demand left owed and
  /// the reason recorded (`LoopPlaybackRuntime::retainedGatherRefusal`). Off-clock only.
  bool beginRetainedSourceGatherForSlot(uint8_t slotIndex);
  /// Task 3.7a (I2): serve **one bounded slice** of that gather, and on the completing slice admit the gathered window
  /// through the same production admission a requester uses. The demand's **own** deadline is validated first, so a
  /// promise is honoured or refused by name -- never silently re-granted. Off-clock only.
  PlaybackViewTurnResult stepRetainedSourceGatherForSlot(uint8_t slotIndex, PlaybackDeadlineClock clock);
  /// Task 3.7a (I3b) / task 3.12 (DEC-108 §14(c)): **the bounded legacy compatibility path's consumer entry**. The
  /// deferred lane prepares an interval that cannot fit one window as `WindowPagedStream` pages (one page per turn,
  /// from the same retained revision and the same pinned bodies the gather reads); this entry walks them, exactly once,
  /// through the runtime's own cursor. It never waits: `NeedsPage` tells the caller to produce, and a cursor from a
  /// revision the loop has left is refused by name (`StaleRevision`) rather than served. Cold: the emission adapter
  /// that drives this per page (I3c) is where the hot-path call belongs.
  enum class PagedWindowConsumeResult : uint8_t {
    /// Events were copied into the caller's buffer; `emittedOut` is their count (0 is possible for a page that holds
    /// no events while the traversal has not completed).
    Emitted,
    /// The cursor's next sequence is not published yet: the caller produces. **Never a wait.**
    NeedsPage,
    /// The loop has left the revision these pages belong to: identity, not currency.
    StaleRevision,
    /// The interval was walked to its end (the source was complete and every page drained).
    Exhausted,
    NoRuntime,
    /// No pages are held for this slot (never prepared, or already released).
    NoPages,
  };
  PagedWindowConsumeResult consumePagedPlaybackWindowForSlot(uint8_t slotIndex, MidiEvent* out, size_t capacity,
                                                             size_t& emittedOut);
  /// Task 3.7a (I3b): the over-`L` trigger's own beginning: **prepare the frozen demand's interval as pages** from the
  /// retained revision's pinned bodies. Idempotent for an interval whose pages are still held (a re-demand must not
  /// drop what its consumer has not walked). Refuses by name with the demand left owed -- the caller keeps the previous
  /// refusal, never a silent drop. Off-clock only.
  bool beginPagedPlaybackForSlot(uint8_t slotIndex);
  /// Task 3.7a (I3b): **one turn of that preparation** -- at most one page (`RingFull` stops the turn rather than
  /// allocating or overwriting), the frozen demand's own promise validated first exactly as the gather's is, and
  /// completion reported as the named delivery (`LoopPlaybackRuntime::PagedPlaybackOutcome::Prepared`). Off-clock only.
  PlaybackViewTurnResult stepPagedPlaybackForSlot(uint8_t slotIndex, PlaybackDeadlineClock clock);
  /// Task 3.7a (I3c-a): **may this slot's emission come from its paged delivery?** Every answer is named, and only
  /// `Admissible` is a yes: a delivery whose interval spans the whole loop is `CannotStraddleTheWrap` (its wrap is the
  /// production window, so no pass can be prepared while the playhead is elsewhere -- row 27's contract row requires
  /// rejecting active windowed selection rather than half-serving it), and a delivery whose pass has not been observed
  /// **dry-free** is `Unproven` (a late page loses the events the frame already passed, so probation decides). This is
  /// the predicate a selection gate reads; it never selects anything itself, and the hot path is unchanged by it.
  enum class PagedEmissionDeliveryState : uint8_t {
    /// Nothing prepared for this slot's current revision, nothing bound, or no runtime.
    NoDelivery,
    /// The loop has left the revision these pages belong to: identity, not currency.
    StaleRevision,
    /// The interval has no re-arm room (its length reaches the loop's), so a pass cannot be prepared across its wrap.
    CannotStraddleTheWrap,
    /// The delivery stands, but no dry-free pass has been observed: not proven, so not selected.
    Unproven,
    /// A pass completed with the producer ahead of the playhead throughout: the delivery may be selected.
    Admissible,
    /// The interval's own content holds more ledger identities open at once than the emission service's declared
    /// closure capacity (`ActiveNoteLedger::kMaxOpenNotes`) can carry, so the emission is **refused by name** rather
    /// than half-served, and the ring is never established for it. Serving it would emit note-ons the ledger must
    /// refuse to close (its documented table-full policy: the On is emitted, the identity is not recorded), and the
    /// note-off that cannot resolve is an orphan the ledger refuses -- a stuck note, which is *wrong content*. That is
    /// the I3c-b lesson applied at the density the declared service cannot carry; task 3.12's repair of 5.5's clause.
    ClosuresExceedDeclaredCapacity,
  };
  PagedEmissionDeliveryState pagedEmissionDeliveryStateForSlot(uint8_t slotIndex) const;
  /// Task 3.7a (I3c): **the pass boundary, serviced in flash.** The clock path only decides *that* a paged delivery is
  /// bound (one load and one test) and calls here; the crossing test, the interval's own start phase and the boundary
  /// judgement live off-ITCM, because RAM1's padding sat 32 B under its floor when they were inline.
  void servicePagedPassBoundaryForSlot(uint8_t slotIndex, bool wrappedBackward, bool atLoopStart,
                                       uint32_t prevTickInLoop, uint32_t tickInLoop);
  /// Task 3.7a (I5): **the judgement, with its own projection, in flash.** The admission's input must be computed and
  /// judged *before* the selection that reads it, and doing that inline cost the ITCM +256 B; so the clock path pays one
  /// load, one test and one call, and this function recomputes the turn's projection itself, off-ITCM.
  void servicePagedPassBoundaryForTurn(uint8_t slotIndex, uint32_t currentTick);
  /// Task 3.15e: **the owed tail's drain, on the clock path's own ITCM turn.** The boundary judgement above is flash;
  /// the drain itself stays here because it calls `advancePlaybackCursor` (ITCM) -- a flash-resident drain needs a
  /// flash->ITCM veneer, and the image built with one did not boot (see the boot matrix in `PROJECT_STATE`). It is
  /// called on the same turn, immediately after the boundary judgement, with the tick the walk started from.
  void servicePagedOwedTailTurn(uint8_t slotIndex, uint32_t currentTick, uint32_t prevTickInLoop);
  /// The declared `CoverageServiceContract` this owner is admitted under. Single-sourced: the declaration,
  /// Task 3.7a (I5)'s ITCM budget slice: **the fixed-source identity and the bridge/view that carry it, resolved in
  /// flash.** The identity is written back because five later decisions in the turn compare against it; the two lookups
  /// and their gates are the same ones the clock path used to run inline.
  void resolveFixedPlaybackSourceForTurn(uint8_t slotIndex, const Loop& loop, bool eligible,
                                         PlaybackViewSourceIdentity& sourceOut,
                                         const FixedPlaybackBridgeStorage*& bridgeOut,
                                         const RuntimePlaybackView*& viewOut);
#if PLAYBACK_WINDOW_ROLLING_ENABLED
  /// Qualification-image source selection in flash. Preserves fixed-view, launch-view, then rolling-window priority.
  void resolveQualificationPlaybackSourceForTurn(
      uint8_t slotIndex, Loop& loop, bool eligible, uint32_t currentTick,
      PlaybackViewSourceIdentity& sourceOut, const FixedPlaybackBridgeStorage*& bridgeOut,
      const RuntimePlaybackView*& viewOut, bool& rollingViewSelectedOut, bool& overlayLiveSelectedOut);
  /// Task 3.15d (slice 2): the NOTE_EDIT preview seam -- true when a current `Preview` window (adopted, or the one
  /// already served) covers the playhead and is the turn's route. Out of line and FLASH-resident so the ITCM resolver
  /// only pays one call. Returns false for every state that is not a servable session preview, which is how the merged
  /// reader stays the compatibility fallback.
  bool resolveNoteEditPreviewSourceForTurn(uint8_t slotIndex, const Loop& loop, uint32_t currentTick,
                                          PlaybackViewSourceIdentity& sourceOut,
                                          const FixedPlaybackBridgeStorage*& bridgeOut,
                                          const RuntimePlaybackView*& viewOut);
#endif
#if PLAYBACK_WINDOW_ROLLING_ENABLED && (defined(PIO_UNIT_TEST_NATIVE) || \
    (defined(SESSION_CAPTURE) && PLAYBACK_WINDOW_ROLLING_QUALIFICATION))
  /// Stack-only, read-only selected-slot receipt. No source owner is borrowed.
  struct RollingRecordStatus {
    uint64_t clockTick = 0, boundStorage = 0, currentStorage = 0;
    uint64_t captureContentToken = 0, liveCaptureToken = 0;
    uint32_t epoch = 0, framePeriod = 0, sourceGeneration = 0;
    int32_t frameOrigin = 0;
    uint32_t coverageStart = 0, coverageLength = 0, coveragePeriod = 0;
    uint32_t rows = 0, cursor = 0, ledgerCount = 0, rawCount = 0, rawMax = 0;
    uint32_t firstTick = UINT32_MAX, lastTick = UINT32_MAX;
    NoteId firstNoteId = kInvalidNoteId, lastNoteId = kInvalidNoteId;
    uint8_t track = 0, slot = 0, state = 0, rollingPhase = 0;
    uint8_t boundRole = UINT8_MAX, currentRole = UINT8_MAX, sourceOrigin = UINT8_MAX;
    bool valid = false, published = false, complete = false;
    bool recording = false, stopResponsibility = false;
    uint32_t frameEpoch = 0, requestEpoch = 0;
    bool requestActive = false, requestInvalidated = false;
    uint8_t readyRole = UINT8_MAX, buildRole = UINT8_MAX;
    uint8_t readyFlags = 0, buildFlags = 0, workerFlags = 0;
  };
  static RollingRecordStatus readRollingRecordStatus();
#if defined(SESSION_CAPTURE) && PLAYBACK_WINDOW_ROLLING_QUALIFICATION
  static void printRollingRecordStatus(uint32_t requestId);
#endif
#endif
  /// Task 3.7a (I5)'s ITCM budget slice: **the bridge window's handoff seek, in flash.** The compiled bridge and the
  /// window carry the same cycle stream, so the seek is logarithmic in the window's event count and only has to exclude
  /// the interval this turn's predecessor already serviced. It is called once per handoff, never per event.
  void seekBridgeWindowPastServicedInterval(const RuntimePlaybackView* window, uint32_t prevTickInLoop,
                                            uint32_t tickInLoop, Loop& loop);
  /// its derivation and the measurement still owed are recorded where it is defined, and the grain quanta
  /// below are the sizes its declared grain counts come from.
  static CoverageServiceContract playbackViewCoverageContract();
  /// The owner's declared work quantum per grain (DEC-108 item 1: "a fixed work quantum"). Public so the
  /// contract's derivation and its fixtures read the same declaration the owner serves.
  static constexpr size_t kPlaybackViewFillQuantum = 64;
  static constexpr size_t kPlaybackViewOrderQuantum = 48;
  /// DEC-108 §13b / task 3.10 (**the review's step 4**): **`Delta-max`** -- the permitted phase advance between two of
  /// this owner's opportunities, the per-turn term of the review's `Delta-max x H x bounded per-track work` formula.
  ///
  /// **Measured, and set from the measurement** (device leg 2026-10-04, the baseline preset on the authorized track 8
  /// slots 4/5: 68 352 lane opportunities on the internal clock, 5 888 under the host clock). The quantity this field
  /// bounds is the advance across a turn **whose predecessor was eligible** -- work was owed and the lane's next turn
  /// was delayed -- which the schema-2 sampler separates from idle intervals (the conflation that once let a
  /// "starvation" reading stand for a scheduling gap):
  ///   * internal clock, default tempo: p50 **0**, p90 **9**, p99 **15**, **max 17** ticks;
  ///   * host clock at **300 BPM** (the fastest declared tempo): p50 **8**, p90 **16**, p99 **48**, **max 48** ticks
  ///     -- ~2.8x the internal maximum, as a tick-denominated unit implies, and 66.7 ms of wall-clock.
  /// The declared value is **64 ticks: at or above both measured maxima with a stated margin** (1.33x the worst, at the
  /// fastest declared tempo; 3.8x the internal maximum). The first declaration here was 16 (2x the measured p50), and
  /// **this measurement falsified it** -- two internal rows and six 300-BPM rows exceeded it -- which is why the value
  /// sits on the measured maximum rather than on a margin over the median.
  /// The seconds-scale **starvation** episodes (2 138 / 4 355 / 8 520 ticks, earlier runs) did not appear in these two
  /// (0 saturated gaps; the largest *all-dispatch* gap was 366 ticks with an **ineligible** predecessor, i.e. idle
  /// time). They remain the lag rule's, not this term's: a bound on one turn cannot hold a run of *lost* turns.
  /// A declared, permitted value -- not a measured WCET, and one device run per tempo. Public so the declaration, its
  /// fixtures and any reader agree on one figure.
  static constexpr uint32_t kPlaybackViewPermittedAdvancePerTurn = 64;
  /// S4b.6: does this slot's **inbound occupancy** (its playback runtime) exist? The switch commit path
  /// asks this instead of allocating: `ensurePlaybackMergedEventsForSlot` intentionally never takes the
  /// runtime, so a switch to a slot without occupancy stays pending until the off-clock provisioning owner
  /// (`TrackManager::provisionSlotForSwitch`) has taken it. Answered without allocating.
  bool hasPlaybackOccupancyForSlot(uint8_t slotIndex) const;
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
  /// Task 3.12/I9: the slot's **serving** source (the revision the lane serves now). A slot can hold a retiring source
  /// beside the serving one, so a reader that compares a source's revision with the loop's current one -- the window
  /// readiness holder and `playbackWindowSourceIsCurrentForSlot` -- must ask for this one, not for whichever entry sits
  /// Task 3.12/I9: the slot's **serving** retained revision (0 when none serves). A slot can hold a retiring source
  /// beside the serving one, so a reader that compares a source's revision with the loop's current one -- the window
  /// readiness holder and `playbackWindowSourceIsCurrentForSlot` -- must ask for this one, not for whichever entry sits
  /// first in the role array. A revision read, so a reader costs no source copy.
  uint32_t servingRetainedRevisionForSlot(uint8_t slotIndex) const;
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
  /// Task 3.7a (I1): **serve an interval from the retained revision's pinned bodies**, directed by the index above.
  ///
  /// This is the window consumer's source for a slot with **no published root**: the retained revision is the source
  /// (DEC-108 §10), so a build can be owed where the published root cannot answer -- and readiness therefore names the
  /// build owner rather than `published_source` for such a slot. The revision asked for is answered **by name** when it
  /// is not the retained one: the one-pass-late allowance is the caller's rule (task 3.6b), never this primitive's.
  ///
  /// **One bounded slice per call** through the existing resumable body primitive
  /// (`CommittedEventRange::appendWindowBudgeted`), so the inspected work is the slice's own: an interval the index
  /// proves empty is answered **without inspecting a single chunk**, which is what the index exists for. On the
  /// completing slice the destination is the interval's events in tick order -- the same shape the fallback's
  /// `Loop::copyEffectiveCommittedEventsInRange` produces for the same revision, which is the differential oracle.
  /// The retained revision's own **Active Note note-edit rows** (task 3.7a/I7) are resolved **per pass layer**, which is
  /// the effective-content owner's rule and the reason they are not applied flat: `Loop::copyEffectiveCommittedEventsInRange`
  /// applies each committed list separately and then merges, because a merged-then-apply pass lets a LIFO delete pair a
  /// hidden On with another pass's Off. Each layer is traversed, tick-sorted, edited and merged in that same order, one
  /// layer per call, so the destination is the owner's own answer for this revision -- which is the differential oracle.
  /// `editRowsOut` is the caller's own scratch for that layer (never shared between two live callers of this primitive).
  enum class RetainedWindowGatherResult : uint8_t {
    Served,
    NoRuntime,
    /// The slot holds no committed content or no usable length.
    NotServable,
    /// Nothing is retained for this slot: there is no revision to serve from, so the **source** is what is owed.
    NoRetainedSource,
    /// The retained source is not the revision asked for.
    RevisionMismatch,
    /// The loop's bar count is outside the indexed range: refused rather than presented as window-bounded.
    BarsUnsupported,
    /// The retained revision pins capture passes but none of their bodies is reachable in the pass set: refused rather
    /// than serving an empty window as content.
    PinnedBodiesUnavailable,
    /// No interval index is published for this revision, so the skip cannot be claimed; the caller owes a scan it must
    /// account for rather than present as window-directed.
    NoIndex,
  };
  struct RetainedWindowGather {
    RetainedWindowGatherResult result = RetainedWindowGatherResult::NoRuntime;
    bool complete = false;             ///< the interval has been fully traversed (true on the first call for an empty one)
    uint32_t indexedBars = 0;          ///< bars of the interval the pinned bodies cover, per the index
    uint32_t skippedBars = 0;          ///< bars skipped **without touching a chunk**, per the index
    uint32_t inspectedChunks = 0;      ///< chunks this slice copied out of the store (0 when the index proved emptiness)
    uint32_t eventsAppended = 0;       ///< events this slice copied out of the store (the per-layer path's destination
                                       ///< for them is the caller's scratch, merged into `out` when the layer completes)
    uint32_t pinnedLists = 0;          ///< pinned capture passes whose bodies this slice enumerated
    uint32_t pinnedPassesMissing = 0;  ///< pinned passes no longer present in the set: reported, never guessed
    bool indexAnsweredEmpty = false;   ///< true when the index alone answered the interval as empty
  };
  RetainedWindowGather gatherRetainedSourceWindowForSlot(uint8_t slotIndex, uint32_t startPhase,
                                                         uint32_t lengthTicks, uint32_t expectedRevision,
                                                         CommittedEventRange::WindowTraversalCursor& cursor,
                                                         SessionMidiEventVec& out, SessionMidiEventVec& editRowsOut);
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
  /// Identity-qualified private Record base; never claims committed currency.
  bool hasPrivateRecordPredecessorForOverdub() const;
  /// Immediate, non-owning preview reads from the bounded live stop-ring window. Never retain.
  /// DEC-172 convergence service and obligation probe, public for the all-stopped idle owner: the
  /// service call takes at most one slot's obligation per invocation, and the probe is read-only.
  bool serviceDeferredCanonicalConvergence();
  bool hasDeferredConvergenceObligation() const;
  size_t captureStopPreviewCount() const;
  /// True when this slot has any admitted stop preview (Record or Overdub) the display may compose.
  /// The display content hold uses this to keep an accepted preview paint while the committed source
  /// publication is still pending.
  bool hasCaptureStopPreviewForSlot(uint8_t slot) const;
  const CapturePreview* captureStopPreviewForSlot(uint8_t slot, size_t ordinal,
                                                  uint32_t* closureTick = nullptr) const;
  /// DEC-233 decision 5: the input-closed producer's preview with its immutable closure phase, so the
  /// display can compose a stopped source instead of a live playhead tail. Display-only; never retained.
  const CapturePreview* inputClosedOverdubPreviewForSlot(uint8_t slot, uint32_t* closureTick) const;
  /// Complete stopped Record preview, qualified by the live job's slot/LoopId. Display only.
  const CapturePreview* recordStopPreviewForSlot(uint8_t slot, uint32_t* closureTick = nullptr) const;
  /// Retain/coalesce intent only after ordinary admission against the private base is denied.
  /// Uses the existing interaction Start kind; true means the request remains owned here.
  bool retainOverdubStartAfterRecord();
  bool hasPendingOverdubStart() const;
  void cancelPendingOverdubStart();
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
  /// Task 3.7a (I3): the same presence question for **committed** content only -- what the capture admission's
  /// accounting may count as the interval's inherited content. A preview window (the note-edit session's uncommitted
  /// projection, task 3.13) is excluded by its identity's origin, and a take is therefore never charged for content the
  /// session has not committed.
  bool committedPlaybackWindowServesSlot(uint8_t slotIndex) const;
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
  /// Task 3.15d: the two phases the admission keeps apart. `playheadPhase` is where the transport is **now** and is
  /// what the lead is measured from; `intervalPhase` is the phase whose interval the caller wants composed. They are
  /// the same value for the session's first request (the interval the committed route is reading), and they differ for
  /// a cadence request positioned ahead of the playhead. Passing the interval's own phase as the playhead would
  /// declare `loopLength - halfWindow` of lead instead of the real distance and admit work that cannot land.
  PlaybackViewRequestResult requestPreviewWindow(uint8_t slotIndex, uint32_t previewRevision,
                                                 const MidiEvent* source, size_t sourceCount,
                                                 uint32_t playheadPhase, uint32_t intervalPhase,
                                                 PlaybackDeadlineClock clock = {});
  /// Task 3.15d: compose the preview's **interval source** -- the committed interval content with the session's
  /// identity-qualified delta applied when a current published root exists, or the session's own effective projection
  /// within the interval when the session rematerialized (no current root). `out` is the caller's **resident** scratch:
  /// the build copies it across grains, so it must outlive the request (the slot runtime owns one). Returns false when
  /// the interval cannot be stated or a current root cannot be gathered -- the caller then leaves the lane untouched.
  bool composePreviewIntervalSourceForSlot(uint8_t slotIndex, const PlaybackCoverageInterval& coverage,
                                           SessionMidiEventVec& out);
  /// The preview lane's cancellation. A mutated or removed source cancels the in-flight preview build and invalidates
  /// its operation identity, so a stale completion publishes nothing; a session exit additionally makes the
  /// **committed successor owed** (the handoff). The served window is never torn: at an exit the preview window keeps
  /// serving until the committed successor is adopted.
  PreviewCancelOutcome cancelPreviewWindowForSlot(uint8_t slotIndex, PreviewWindowCancelReason reason);
  /// True when the served window for a slot came from the **preview** source contract (task 3.13).
  bool servedPlaybackWindowIsPreviewForSlot(uint8_t slotIndex) const;
  /// True when the exit handoff is owed for a slot: the preview source has yielded and a committed window is required.
  bool previewWindowHandoffOwedForSlot(uint8_t slotIndex) const;
  /// Task 3.7a (I3): whether the preview lane still holds this slot's preview source -- a live in-flight or superseded
  /// preview preparation. It is what "the re-focus yielded the preview" is asserted with: the **lifecycle hook** yields
  /// it at the focus change, and the requester's own non-selected branch is the backstop for a skipped hook, so a fixture
  /// that only drove the requester could not tell the two apart.
  bool previewWindowSourceActiveForSlot(uint8_t slotIndex) const;
  size_t playbackWindowEventCountForSlot(uint8_t slotIndex) const;
  /// Task 3.7a (I3): the committed-only count (see `committedPlaybackWindowServesSlot`).
  size_t committedPlaybackWindowEventCountForSlot(uint8_t slotIndex) const;
  /// DEC-108 item 1 / task 4.3: **the ordered event stream the served window holds**, in emission order --
  /// `storage.events[storage.order[cursor]]`, the same rendering the fixed-view emission path uses (`cursor` is a
  /// position in that order, `0 .. playbackWindowEventCountForSlot()-1`). It reads the window that is **served**,
  /// never a prepared successor, and it is read-only, allocation-free and O(1). Nothing in production consumes it
  /// yet: the emission path is task 3.7's switch, so this exists to let the parity proof read what the lane
  /// Task 3.7a (I3) / task 3.13: **the preview requester's production caller** -- the revision-advance seam. Run on the
  /// focus lane's own deferred turn for the **selected** track: when a note-edit session is active on a demoted loop
  /// and the session's preview revision has moved past the one this lane froze, the superseded preparation is cancelled
  /// **by name** (`SourceMutated`) and the new revision is requested from the session's own projection. A session that
  /// has ended yields the source and **owes** the committed handoff; a track that is no longer the selected one yields
  /// it without owing anything. O(1) when nothing changed: one revision comparison, no allocation, no clock read.
  void servicePreviewWindowForNoteEditSession();
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
  /// Task 4.5 (**DEC-108 §13f**): **the owner-level "no serving route at all" predicate**, asked by the MIDI turn only
  /// after route selection has ruled out the bridge, a bound view, the prepared pair and the paged ring. What remains is
  /// the retained merged fallback -- the one always-selectable branch -- so "no route" means **its own bounded window
  /// does not cover this phase** (a short loop's store is whole-loop and covers by construction; a demoted loop serves a
  /// window, and a window whose build was refused this turn covers nothing). Containment is then asked as it always was:
  /// a published window that still covers the phase keeps the slot on `ServeLastCoherentRevision`, and only a slot with
  /// neither reaches the **approved last resort**. Cold by construction: merged-fallback turns only, and it owns the
  /// projection arithmetic the MIDI edge must not pay for.
  LastResortOutcome containPlaybackTurnIfNoRouteForSlot(uint8_t slotIndex, uint32_t currentTick, bool isActiveSlot);
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
  /// DEC-241: main-loop build of a merged fallback an interrupt-context tick owed (see TrackManager).
  void serviceOwedTickPlaybackFallback(uint8_t slotIndex);
#if defined(PIO_UNIT_TEST_NATIVE)
/// **DEC-143's cost criterion, observable from a native suite.** The merged playback fallback may rebuild only when the
/// published identity it is built from changed; a maintenance turn that finds the same identity must reuse. Counted at
/// the rebuild point itself so a suite can prove "bounded work", not merely "no silence".
unsigned playbackMergedRebuildCountForTest();
void resetPlaybackMergedRebuildCountForTest();
/// DEC-241: whether an interrupt-context tick owes the main loop this slot's merged fallback, and how many
/// interrupt ticks found no consumable fallback.
bool mergedFallbackOwedForTest(uint8_t trackIndex, uint8_t slotIndex);
uint32_t mergedFallbackTickMissesForTest();
#endif
#if defined(PIO_UNIT_TEST_NATIVE)
  /// 3.7a (I4): **the MIDI edge's own record of what committed playback put on the wire.**
  ///
  /// `sendMidiEvent` appends here at the production send point -- after its own gates
  /// (`playbackEmitMidiOutput_`, the transport-stop barrier, the slot range) and after the playback
  /// channel remap -- so a host fixture reads the emitted stream *through the production path*
  /// instead of beside it: the pair recorded is the pair the handler is handed on the next line.
  /// Bounded: a drive that outruns the capacity is counted (`sentPlaybackMidiDroppedForTest()`),
  /// never silently truncated. Native-only; the firmware carries neither the buffer nor the call.
  struct SentPlaybackMidiForTest {
    MidiEvent event;        ///< As handed to `MidiHandler`: the playback channel is already remapped.
    uint8_t slotIndex = 0;  ///< The slot the playback walk named.
  };
  static constexpr size_t kSentPlaybackMidiCapacityForTest = 8192;
  size_t sentPlaybackMidiCountForTest() const;
  const SentPlaybackMidiForTest& sentPlaybackMidiAtForTest(size_t index) const;
  size_t sentPlaybackMidiDroppedForTest() const;
  void clearSentPlaybackMidiForTest();
#endif
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
  /// Read-only identity of the head deferred stop job, for display-source attribution. The display must
  /// not need the private job type to say whether the committed base is lagging behind a pending pass.
  struct PendingDeferredStopIdentity {
    bool active = false;
    uint8_t slot = 0;
    uint8_t phase = 0;
    uint32_t generation = 0;
  };
  PendingDeferredStopIdentity pendingDeferredStopIdentity() const {
    const CaptureStopJob* job = captureStopJobHead();
    PendingDeferredStopIdentity id{};
    if (job != nullptr) {
      id.active = true;
      id.slot = job->slot;
      id.phase = static_cast<uint8_t>(job->admission.phase);
      id.generation = job->admission.frozenGeneration;
    }
    return id;
  }
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
  /// DEC-233: a matching overdub Stop owns `slot` with its input already closed but no job bound yet.
  /// The interval is still the latch's own identity (the press does not advance `captureInterval_`), so
  /// the switch/recovery/orphan owners can treat it as a live obligation without a job.
  bool hasRetainedInputClosedStop(uint8_t slot) const;
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
  /// Task 4.20 device census (2026-10-27): what the persistent content owners actually hold, by class, so a
  /// capture can correlate internal-heap loss with capture/edit pass growth instead of guessing. Cardinality is
  /// reported as counts and resident size as **capacity × element size**, because capacity is what the heap pays
  /// for. `internalBytes` covers the classes whose allocators are internal-heap-first; `externalBytes` covers the
  /// ones that are external-first on the device (`CommittedChunkIdAllocator` chunk directories, the overdub pass
  /// vector), so a class that merely *looks* big in the census cannot be blamed for internal-heap loss.
  struct AllocationCensus {
    uint16_t capturePasses = 0;
    uint16_t editPasses = 0;
    uint16_t undoEntries = 0;
    uint16_t slotsWithPasses = 0;
    uint32_t internalBytes = 0;
    uint32_t editPassBytes = 0;
    uint32_t undoIdBytes = 0;
    uint32_t sessionBytes = 0;
    uint32_t externalBytes = 0;
  };
  /// DEC-189: read-only snapshot of the two lanes that can strand a publication -- the deferred stop job's
  /// phase and unspent estimates, and the selected loop's publication progress. Nothing here allocates or
  /// mutates; it exists so one direct-serial row per second can prove a lane is not advancing (`phase`,
  /// `applied` and `queued` are flat) without depending on the capture ring.
  struct StallCensus {
    bool stopPending = false;
    uint8_t stopMode = 0;       // CapturePhase (Record = 2, Overdub = 6 ... see enum)
    uint8_t stopPhase = 0;      // PendingStopPhase
    uint32_t stopGeneration = 0;
    uint64_t orderGrains = 0;
    uint64_t canonicalGrains = 0;
    uint64_t rankedGrains = 0;
    uint64_t undoGrains = 0;
    bool publishPending = false;
    uint32_t publishQueued = 0;
    uint32_t publishApplied = 0;
    bool publishInFlight = false;
    bool hasCommittedPasses = false;
    uint32_t loopLengthTicks = 0;
  };
  StallCensus stallCensus() const;

  /// Bounded, read-only walk of the active slot (the loop that is being recorded) plus a per-slot count of
  /// committed passes. No allocation, no mutation; safe on an idle turn and cold (FLASH) by construction.
  AllocationCensus allocationCensus() const;
  /// Off-clock Snapshot eligibility for a background Track while another Track is realtime.
  /// Ordinary PLAYING/STOPPED_RECORDING hydration excludes pending stop jobs; a
  /// stopped RecordPublished job waiting for its queued whole-root handoff may
  /// borrow the existing one-unit background Snapshot turn.
  bool canServiceBackgroundSnapshotOwnerTurn() const;
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

  /// **DEC-143 — a published Record slice is read material before its root publishes.** True only after this slot's
  /// take crossed the immutable `RecordPublished` edge and while its presentation owns the reader; a pre-publication
  /// take still sits behind the matching live finalizer and must not reach the merged playback representation.
  bool hasPublishedRecordPresentationForSlot(uint8_t slot) const;
  /// DEC-160: a live Capture or a sealed custody receipt for this slot/LoopId whose interval
  /// is later than `interval`. Read-only, allocation-free; it grants no priority. When true, an
  /// empty predecessor may release only its own obligation and must not reset shared geometry.
  bool hasLiveOrSealedSuccessorForSlot(uint8_t slot, LoopId loopId, uint64_t interval) const;
  /// DEC-160 item 1: apply a deferred empty-take geometry policy once this Track owes no
  /// capture and no custody. Bounded: one slot's reset per turn. Clears the obligation when a
  /// successor published content, so it never rewrites a successor's geometry.
  void settleEmptyRecordGeometry();

private:
  /// Existing physical-retirement eligibility, shared with descriptor reuse; no mutation.
  bool canRetireRetainedPlaybackSource(uint8_t slotIndex, const RetainedPlaybackSource& source) const;
  /// Off-clock Snapshot eligibility for an enabled playing slot with no source producer.
  /// Shared by slot selection and admission; never starts work or changes a claim.
  bool playbackSourceRecoveryNeededForSlot(uint8_t slotIndex) const;
  friend class TrackUndo;
#if defined(PIO_UNIT_TEST_NATIVE)
  friend struct TrackCaptureStopTestAccess;
  friend struct TrackPlaybackTestAccess;
#endif
  friend class StorageManager;  // Allow StorageManager to access private members for loading
  bool ignorePlaybackMidiInput;  // Ignore playback-echo MIDI during overdub capture
  bool playbackEmitMidiOutput_ = false;
  // `handleTransportStop()` fans CC123 over every track while the internal-clock
  // interval may still be executing. This volatile barrier is checked at the
  // final Track MIDI emission point, so no due NoteOn can pass that fan-out.
  volatile bool transportStopOutputBarrier_ = false;
#if defined(PIO_UNIT_TEST_NATIVE)
  /// 3.7a (I4): the native-only send sink behind `sentPlaybackMidiCountForTest()`. By value, so the
  /// eight-way `Track` array pays it eight times -- which is why it exists only in the host build
  /// (`placementPins()`'s `sizeof(Track) == 384` is a firmware-configuration pin and cannot see it).
  SentPlaybackMidiForTest sentPlaybackMidiForTest_[kSentPlaybackMidiCapacityForTest];
  size_t sentPlaybackMidiCountForTest_ = 0;
  size_t sentPlaybackMidiDroppedForTest_ = 0;
#endif
  bool applyPlaybackLedgerEvent(const MidiEvent& evt, uint8_t playbackSlotIndex);
  void catchUpCommittedPlaybackLedgerToPhase(uint32_t occupyPhase);
  void sendMidiEvent(const MidiEvent& evt, uint8_t playbackSlotIndex);
  /// The selected Snapshot owner turn; shared with RecordPublished retirement so its current root can catch up.
  void processSnapshotOwnerTurn(Loop& snapshotLoop, uint8_t snapshotConstructionUnits);
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
  /// DEC-227 (Slice 4 of the 2026-10-30 plan): the current stop job's companion-family start, in
  /// 262.144 ms units (`micros() >> 18`), or 0 when the job is not in the family. One byte fills the
  /// existing padding before the next 32-bit member, so `sizeof(Track)` is unchanged (build pin).
  /// A job that exceeds `kCompanionWorkBudgetUs` retires raw and owes DEC-172 convergence.
  uint8_t companionWorkStartUnit_ = 0;
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
  /// Task 3.15g -- the slot-owned presentation record. One per slot, in the externally
  /// allocated ring storage, so its address is stable for the whole lifetime of the Loop's
  /// read borrow (`Loop::borrowSealedCaptureRange` keys on the record's own identity).
  ///
  /// The sealed raw capture extent lives **here**, not in the interaction job, because the
  /// two obligations end at different points: the immutable Record slice is finished at
  /// `RecordPublished` (so the interaction job must be popped and its finalizer role
  /// released, or the next slice's closure is blocked behind it), while the frozen reader
  /// is still owed a root-backed successor and must keep serving the same bytes afterwards.
  /// One record per slot is also the coalescing rule: presentation work is keyed by the
  /// authoritative generation of the extent that a slot currently owes, never stacked one
  /// heavyweight job per slice.
  enum class CaptureCustodyState : uint8_t { Free, Sealed, Retiring };
  struct CaptureStopPresentation {
    /// The sealed extent. Reserved-empty at rest; sealed by the admission that takes the
    /// slot. Released grain by grain and re-armed by `serviceCaptureStopDescriptorReservations`.
    LoopEventStore::CaptureRange rawRange;
    Loop* owner = nullptr;
    LoopId loopId = kInvalidLoopId;
    /// The interval this record presents, and the E2 Capture content identity frozen before
    /// the range cut. Both are copied once at admission and never rewritten afterwards.
    uint64_t interval = 0;
    uint64_t frozenCaptureContentGeneration = 0;
    uint32_t loopLengthTicks = 0;
    /// The **interval-derived presentation generation**, by contract:
    ///
    ///   `generation == presentationGenerationFor(interval)`
    ///
    /// It is not an independent counter and must never be stamped from another source (an
    /// admission's frozen counter, a range generation, a slot generation). Its single job is to
    /// let a reader that names an interval tell a reused record from the one it was handed:
    /// `Track::qualifiedFrozenRecordCaptureSourceForSlot` re-derives it from the interval it is
    /// asked about and refuses any record whose stamp disagrees, so the two sides are one
    /// derivation rather than two conventions.
    ///
    /// The record's own **address** is the Loop's borrow identity: it is allocated once with this
    /// struct and never moved, which is exactly what `Loop::borrowSealedCaptureRange` needs of its
    /// `owner` key.
    uint32_t generation = 0;
    uint8_t slot = 0;
    /// True after RecordPublished has released the interaction job and the frozen reader remains
    /// to be retired by the presentation lane.
    bool retirementActive = false;
    bool ownsExtent = false;
    bool armed = false;
    CaptureCustodyState state = CaptureCustodyState::Free;
    uint8_t obligations = 0;
    uint8_t custodyIndex = 0;
  };
  /// The **one** derivation of `CaptureStopPresentation::generation`. The stamping sites (both Record
  /// admissions) and the reader that validates it (`qualifiedFrozenRecordCaptureSourceForSlot`) call
  /// this, so "generation" cannot drift back into a second meaning.
  static constexpr uint32_t presentationGenerationFor(uint64_t interval) {
    return static_cast<uint32_t>(interval);
  }
  struct CaptureStopJob {
    ~CaptureStopJob();
    PendingStopAdmission admission;
    Loop* owner = nullptr;
    LoopId loopId = kInvalidLoopId;
    uint8_t slot = 0;
    CapturePhase mode = CapturePhase::None;
    uint8_t frozenStagingIndex = UINT8_MAX;
    /// DEC-166: the last named refusal from this job's Record-normalization grain -- bookkeeping for the
    /// device row and the native fixture, never a gate. It sits here because this is the 1-byte slot the
    /// struct already had as padding before `phaseTick`: `sizeof(CaptureStopJobs)` is unchanged (33,088 B,
    /// pinned), which is why the reporter costs nothing at rest.
    RecordNormalizationDenial lastNormalizationDenial = RecordNormalizationDenial::None;
    uint32_t phaseTick = 0;
    uint32_t loopLengthTicks = 0;
    uint64_t interval = 0;
    /// Task 3.15g: the slot-owned presentation record holding this interval's sealed
    /// extent. Armed at admission; deliberately **not** released at `RecordPublished`, so
    /// the frozen reader keeps serving after this job is popped and the extent's address
    /// never changes under the Loop's borrow. Null for a take that sealed no extent.
    CaptureStopPresentation* presentation = nullptr;
    /// E2: Loop Capture content identity frozen before the admitted range cut. The
    /// presentation record carries its own copy for reads that outlive this job.
    uint64_t frozenCaptureContentGeneration = 0;
    /// Task 3.15g: the sealed extent is reached as `presentation->rawRange` at every
    /// finalization site -- a plain member access, not an accessor: a call at each of the
    /// ~27 sites measured 784 B of ITCM against the reviewed 840 B padding floor.
    /// True while the presentation record still owes extent release to the pool.
    bool ownsRawRange() const { return presentation != nullptr && presentation->ownsExtent; }
    void clearOwnedRawRange() {
      if (presentation != nullptr) presentation->ownsExtent = false;
    }
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
    /// UndoCollection retention bound (DEC-070's shape for an obligation whose inputs cannot be
    /// recomputed): consecutive turns this job's session-identity collection was refused or denied.
    /// Exhaustion falls back to the interval's own session-exit receipt instead of parking the
    /// accepted stop forever, which is what device `session_20261026_204731` showed.
    uint16_t undoCollectionRetentions = 0;
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
    /// NO transport epoch (architect decision 2026-09-23). The drive re-derives the *destination* from
    /// live transport state and validates `(kind, slot, interval)` before acting, so a stored counter
    /// would never be compared and would add no protection. Transport *history* is covered by
    /// **consumption plus identity validation**, not by an epoch: an accepted stop is consumed by the
    /// admission that binds it, by the transport-stop entry that retargets or completes it, or by a
    /// synchronous completion -- so a stop accepted before a transport stop can never later be driven
    /// with a PLAYING destination.
    /// DEC-233 amends the boundary half: an overdub Stop closes input on its press and stamps
    /// `phaseTick` there, and every retry closes at that stored musical phase rather than re-deriving
    /// it from the retry clock (the current absolute tick is scheduling/diagnostic only).
    /// S4f.8: the accepted stop's waits are counted as scheduling, never as refusals.
    uint32_t waitCount = 0;
    /// S4f.2/S2b.6: report the wait once per episode rather than once per deferred turn.
    bool waitReported = false;
    CapturePhase mode = CapturePhase::None;
    /// DEC-233 decision 3: a successor Start arrived while this Stop was retained on genuinely
    /// exhausted physical capture storage. One bounded bit of continuation metadata -- never a
    /// second interval, reservation or worker. The Stop stays authoritative; the intent transfers
    /// into the existing Start carrier only after the Stop binds, using the post-binding identity.
    bool successorStart = false;
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
    static constexpr size_t kCapacity = 8;
    /// Out-of-line and FLASH-resident. `CaptureStopJob` owns a `PendingCapturePass`
    /// (OpenSpec 5.12 / D8), so the ring's constructor is non-trivial; the implicit aggregate
    /// form was emitted into ITCM and cost one 32 KB FlexRAM bank on this tree's RAM1 edge.
    /// Default-initialization is value-initialization here: every member has an initializer or a
    /// non-trivial default constructor.
    CaptureStopJobs();
    ~CaptureStopJobs();
    CaptureStopJob entries[kCapacity];
    size_t head = 0;
    size_t count = 0;
    /// Task 3.15g: one slot-owned presentation record per slot. Indexed by slot, not by
    /// ring position, because the obligation is a property of the slot's served interval --
    /// that is also what makes presentation work coalesce by the authoritative generation
    /// of the extent the slot currently owes instead of stacking per slice.
    ///
    /// Externally allocated with the ring (this struct is EXTRAM and FLASH-constructed), so
    /// per-slot `CaptureRange` ownership here costs external pool bytes, no DTCM and no RAM1.
    struct PresentationHandle {
      uint64_t interval = 0;
      uint8_t custodyIndex = UINT8_MAX;
    };
    PresentationHandle presentations[Config::MAX_LOOPS_PER_TRACK];
    /// CONTRACTS.md C1.1 — pre-admitted per-interval custody. One record per **sealed interval**,
    /// allocated with this ring so the address the Loop borrows never moves, and keyed by the
    /// interval rather than the slot so a successor can seal while a predecessor is still retiring.
    ///
    /// Presentations are generation-qualified handles; only custodies own extents.
    ///
    /// Capacity is derived, not chosen: `K_trace` 4 (A, B, C and the next Stop) plus one receipt
    /// still retiring while the next interval seals. Price and charge site:
    /// `openspec/changes/immediate-record-overdub-handoff/evidence/CONTRACT-PRICE.md`, `CAP-DERIVATION.md`.
    using CustodyState = CaptureCustodyState;
    /// One bit per logical consumer; each bit has exactly one acquirer and one release edge, and the
    /// extent may not start releasing until the mask is zero (C1.5).
    enum CustodyObligation : uint8_t {
      kCustodyFinalizer = 1u << 0,
      kCustodyReader = 1u << 1,
      kCustodyPersistence = 1u << 2,
      kCustodyUndo = 1u << 3,
      kCustodyDisplay = 1u << 4,
    };
    static constexpr size_t kCustodyCapacity = 9;
    using CustodyReceipt = CaptureStopPresentation;
    CustodyReceipt custodies[kCustodyCapacity];
    /// DEC-160 item 1, second half: a Record interval that retained no row records here that
    /// its slot still owes the empty-take geometry policy, because a live or sealed successor
    /// existed when the result landed. 0 = nothing owed. The policy is applied by
    /// `settleEmptyRecordGeometry` once the slot owes nobody, so an overdub successor that also
    /// ends empty cannot leave a silent non-zero loop behind, and a successor that published
    /// content simply clears the obligation untouched.
    uint64_t emptyGeometryOwed[Config::MAX_LOOPS_PER_TRACK] = {};
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
  bool prewarmCaptureStopJobs(uint8_t captureSlot = UINT8_MAX);
  CaptureStopPresentation* freeCaptureCustody();
  const CaptureStopPresentation* captureCustodyForInterval(uint8_t index, uint64_t interval,
                                                         const Loop& loop) const;
  bool hasPendingStopForInterval(uint64_t interval) const;
  void releaseCustodyObligation(CaptureStopPresentation& receipt, uint8_t bit);
  bool bindCaptureStopWorker(CaptureStopJob& job);
  bool hasCaptureStopWorkerForSlot(uint8_t slot) const;
  bool captureIntervalPermitsPlayback(uint64_t interval) const;
  /// Return the surplus frozen staging roles once this Track owes no interval any
  /// staging. Bounded per turn; keeps the two baseline roles resident so a slot that
  /// recorded once does not carry the A/B/C overlap capacity for the session.
  void releaseSurplusCaptureStaging();
  /// DEC-108 item 1: the window owner's two private halves -- one grain's outcome (and its named row), and
  /// the row itself. Private because the owner's surface is the three calls above; nothing else may advance a
  /// grain or report one.
  PlaybackViewTurnResult finishPlaybackViewGrain(uint8_t slotIndex, RuntimePlaybackViewBuildGrain grain);
  /// DEC-108 item 1: install a completed build as this slot's current interval window (atomic). False when
  /// there is nothing completed to publish, which is a refusal rather than an error.
  bool publishCompletedPlaybackWindow(uint8_t slotIndex);
  void reportPlaybackViewMaintenanceRow(uint8_t slotIndex, Loop& loop, const char* path);
  /// DEC-108 §13b / task 3.9: the **readiness outcome's name**, so a receipt says which case it is rather than an
  /// ordinal -- the same if/else form the holder names use, for the `.data` reason recorded there.
  static const char* playbackWindowReadinessLabel(PlaybackWindowReadiness readiness);
  /// DEC-108 §13b / task 3.9: report the slot's **readiness receipt** by the two delivery rules, **cadence-bounded**
  /// so this diagnostic cannot evict the evidence it explains. Event-shaped: called where a readiness outcome is
  /// reached (a denied grain, a completion), never per grain.
  void reportPlaybackWindowReadinessReceipt(uint8_t slotIndex, uint32_t currentPhase);
  /// The **frozen Record-return**'s source-qualified receipt: emitted on the frozen owner's own completion
  /// transition, naming the window that became ready (its revision, source generation and capture content token) and
  /// the interval it covers. The generic `reportPlaybackWindowReadinessReceipt` above names the slot's *committed*
  /// readiness, which for a first Record take is `not_servable`; this row is what tells a reader **which** window
  /// became ready. Cadence-bounded by its own counter (so a frozen completion is never starved by the shared
  /// `win_readiness` cadence), never per grain.
  void reportFrozenRecordReadinessReceipt(uint8_t slotIndex, uint32_t currentPhase,
                                          const RuntimePlaybackView& ready);
  /// DEC-108 §13a/§13(e) / task 3.10 (**blocker 3, half 2**): what **one missed traversal** means -- the allowance
  /// (`one_pass_late`), then the named breach and the latch. The one implementation, reached from both judgements a miss
  /// can arrive from: the revision rule and the **deadline the admission promised**. It never publishes, adopts,
  /// rebuilds or extends a deadline.
  void noteMissedTraversalForSlot(uint8_t slotIndex, Loop& loop);
  /// DEC-108 §13(c) / task 3.10 (increment 24): the admission path's own retention call -- one shape for every "not
  /// now" refusal. It derives the demand's bounded cost from the entries the caller gathered (the contract's own
  /// `deriveGrainsFrom`, never an asserted count), computes the absolute deadline exactly as an admitted window's is,
  /// and hands both to the slot's runtime to retain or coalesce. It is **not** a scheduler: nothing is retried here.
  void retainRefusedPlaybackViewRequest(uint8_t slotIndex, LoopPlaybackRuntime* runtime, size_t sourceCount,
                                        const PlaybackViewSourceIdentity& sourceIdentity,
                                        const PlaybackCoverageInterval& coverage, uint32_t currentPhase,
                                        uint32_t handoffPhase, PlaybackDeadlineClock clock,
                                        PlaybackViewPendingReason reason);
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
  /// Task 4.20: reserve this interval's future session-exit publication obligation at overdub
  /// admission (FLASH-resident cold path; the ITCM caller must not carry the refusal text). Refuses by
  /// name when a predecessor's obligation is still owned.
  bool claimOverdubSessionExitObligation();
  /// DEC-191: the **cheap** half of overdub-start admission -- no allocation, no prewarm, no mutation. Used by the
  /// retained-Start carrier so an unchanged refusal costs a preflight instead of the full preparation body.
  /// `refusalOut` names why it refused (0 = admissible).
  bool overdubStartPreflight(uint8_t& refusalOut);
  /// DEC-191: resolve an ownerless `Reserved` session-exit claim from the **ownership** lane, so recovery never
  /// depends on a later Start press. Cheap, non-allocating, idempotent; false when nothing moved.
  bool settleOrphanedSessionExitReservation();
  /// The aborted-start rollback: release a reservation that never reached a live interval.
  void releaseOverdubSessionExitObligation();
  /// Task 6.4: whether a pending stop job still owns the reserved session-exit record. No owner means the
  /// obligation's interval already retired, so it can never be marked ready again and must be published.
  bool hasLiveOwnerOfReservedSessionExitObligation(const Loop& loop) const;
  /// Task 6.4: the transfer horizon must hold one row and one pass slot for every outstanding overdub
  /// job on `loop` plus this stop; the instantaneous `hasActiveOverdubTransferAdmission()` let several
  /// jobs share the last row, and the extra ones wedged at `PrepareTransfer`. FLASH-resident.
  bool overdubTransferAdmitsAnotherStop(const Loop& loop) const;
  /// Task 6.4: top up a live overdub chain's transfer horizon on a turn with no stop job pending, the
  /// same precondition the start-time prewarm relies on. Returns true when it spent the turn.
  bool replenishLiveOverdubTransferHorizon();
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
    // P-A step 2 + P-C: measured ARM 29,600 B (29,472 before P-C). Eight 120-byte slot
    // extents became 16-byte generation-qualified handles (-832 B) while the five
    // 120-byte interval receipts own all extents. P-C adds the causal interval to the undo
    // publication the job carries (`UndoEntry` 132 -> 144, `TrackUndoPublication`
    // 596 -> 648, +64) and the per-slot empty-geometry debt (+64). Track remains 384 B;
    // the post-link guard owns RAM1 placement.
    // Capacity bump (owner request): FIFO 4 -> 8 jobs and custody 5 -> 9 receipts. Measured
    // 29,632 -> 33,088 B = +4 x 744 B job + 4 x 120 B receipt. Track stays 384 B; the ring is
    // external and the post-link guard owns RAM1 placement.
    static_assert(sizeof(CaptureStopJobs) == 33088,
                  "capacity bump: target external stop ring is 33,088 B; remeasure before changing");
#endif
  }
  /// Latch an accepted stop for `slot`/`interval`; a same-identity repeat is a no-op.
  void latchAcceptedStop(PendingStopDestination destination, uint8_t slot, uint64_t interval);
  /// DEC-233 decision 1: close the retained interval's input on the Stop press -- state only. It never
  /// seals, transfers, closes the session, clears held notes/pending changes/preview or moves the interval
  /// identity; admission still advances the identity exactly once when it binds.
  void closeOverdubInputForRetainedStop();
  /// DEC-233 decision 4: retarget the retained Stop to STOPPED for a transport stop, preserving its
  /// first phase. Returns true when a retained Stop was retargeted (the obligation stays owned).
  bool retargetRetainedStopToStopped();
  /// DEC-233 decision 3: hand the retained Stop off before a successor Start mutates the live role
  /// (staging reset, preview, session-exit claim). True means the Stop bound and the Start may proceed.
  bool handOffRetainedStopBeforeSuccessorStart(uint32_t currentTick);
  /// DEC-233 decision 3: bounded successor intent retained beside a Stop whose physical handoff is
  /// genuinely exhausted. It never replaces the Stop and stacks nothing.
  void retainSuccessorStartIntent();
  /// DEC-233 decision 3: transfer a retained successor intent into the existing Start carrier once the
  /// Stop has bound, using the post-binding interval identity.
  void transferSuccessorStartIntent(uint8_t slot);
  /// DEC-233 decision 2: reserve an **empty, unowned** canonical worker output. A populated store or a
  /// foreign owner is left untouched (the wait stays observable); never clears content.
  bool replenishEmptyCanonicalCaptureOutput(uint8_t slot);
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
  bool serviceCaptureStopPresentations();
  /// Deferred canonical convergence: the slot-level owner turn that services at most one slot's
  /// `Owed`/`Building`/`Ready` obligation per call. Routed from `processDeferredIdleMaintenance`
  /// on the Snapshot lane, before its publication gates, and never from
  /// `processDeferredOverdubStopGrain`, so convergence can never hold the capture-stop FIFO head.
  /// Returns true when this turn took the obligation slot (bound, stepped, discarded or discharged);
  /// "still owed, no authority" is also reported as taken because it ends the scan for this turn.

  bool publishRecordPresentation(CaptureStopJob& job);
  CaptureStopPresentation* captureStopPresentationForSlot(uint8_t slot);
  const CaptureStopPresentation* captureStopPresentationForSlot(uint8_t slot) const;
  bool hasRecordReaderOwnerForSlot(uint8_t slot) const;
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
  /// DEC-166: `denial`, when given, names the refusing predicate on a `Denied` return (and `None`
  /// otherwise). Diagnostic only: no predicate, credit or phase changes because of it.
  LoopEventStoreRewriteResult stepDeferredRecordNormalization(
      CaptureStopJob& job, RecordNormalizationDenial* denial = nullptr);
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
  /// DEC-233: `phaseTick` is the interval's stored musical close boundary (`UINT32_MAX` derives it
  /// from `currentTick`), and `inputClosedRetry` marks a retry of an already input-closed interval:
  /// it bypasses only the completed input-state transition, never resource or identity preflight.
  bool beginDeferredOverdubStopForSlot(PendingStopDestination destination, uint32_t currentTick,
                                       uint8_t slot, uint64_t interval,
                                       uint32_t phaseTick = UINT32_MAX,
                                       bool inputClosedRetry = false);
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
  // Task 3.15e slice 3: the live overlay window's provenance-aware emission/ledger callbacks.
  friend void playbackCursorAdvanceSendOverlay(void* ctx, const MidiEvent& evt, uint8_t slotIndex);
  friend void playbackCursorAdvanceApplyLedgerOverlay(void* ctx, const MidiEvent& evt, uint8_t slotIndex);
  friend bool playbackCursorAdvanceJamFilter(void* ctx, uint32_t storageTick);

};

#include "TrackUndo.h"

#endif

/// DEC-191: how many retained-Start attempts the cheap preflight stopped (test/telemetry read of a RAM2 counter).
uint32_t overdubStartPreflightRefusalCount();
uint32_t overdubStartBodyEntryCount();
