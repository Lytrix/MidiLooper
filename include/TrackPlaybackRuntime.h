#pragma once

#include <array>
#include <cstdint>
#include <memory>

#include "ActiveNoteLedger.h"
#include "Globals.h"
#include "LoopEventStore.h"
#include "LoopPasses.h"
#include "PlaybackMergedMidiEvents.h"
#include "RuntimePlaybackView.h"
#include "RuntimePlaybackViewBuild.h"
#include "RuntimePlaybackBridge.h"
#include "RuntimeCaptureCompanions.h"
#include "RuntimeCompanionOverlapWalk.h"
#include "RuntimeCompanionSpanWalk.h"
#include "RuntimeCompanionReconstruction.h"
#include "RuntimeCompanionApply.h"
#include "RuntimeCompanionBridgeMap.h"
#include "RuntimeCompanionPasses.h"
#include "RuntimeCanonicalPassInputs.h"
#include "RuntimeNotePairLookup.h"
#include "Utils/WorkingSetLease.h"

class Loop;

/// Fixed metadata for a deferred overdub stop. It owns no event memory: the
/// frozen candidate remains in the view set's frozen capture-staging role while
/// task-2.3 grains consume its reserved capacity. The command epoch makes a
/// transport Stop invalidate a previously selected PLAYING result.
enum class PendingStopDestination : uint8_t { Playing, Stopped };
enum class PendingStopPhase : uint8_t {
  ReservedClosure,
  CanonicalPrefix,
  CanonicalOrder,
  CanonicalRewrite,
  AudibleOrder,
  CanonicalTail,
  PrepareTransfer,
  CompanionHistory,
  /// Bounded, resumable collection of this session's undo identities, then the
  /// prepared OverdubPassAdded entry. Runs only on the prepared PLAYING route
  /// (the STOPPED route skips companion preparation entirely).
  UndoCollection,
  CompanionBridge,
  AtomicTransfer,
  DeferredCleanup,
};

/// Declares the per-turn quantum and fixed closure/storage obligations between
/// a stop command and the atomic transfer. Total service turns are resumable;
/// candidate work length is not an admission ceiling.
struct StopAdmissionContract {
  /// DEC-062: provisional ranked-preparation allowance for integration/HITL.
  /// Separate from legacy order/rewrite credits; it is not a runtime stop
  /// ceiling or a realtime latency claim.
  static constexpr uint16_t kProvisionalRankedPreparationGrains = 6240;
  /// Additional ranked preparation only; excludes reserved closures, canonical
  /// rewrite/transfer, cleanup and denied turns. It is a diagnostic allowance,
  /// not a production stop permit. Uses declared C/H/R horizons and a 64-bit
  /// result; invalid inputs return MAX.
  static uint64_t requiredRankedPreparationGrains(size_t capacity, size_t historyCount,
                                                 size_t pendingChangeCount, size_t quantum);
  /// Same scope after a completed, current sealed prefix is reused.
  static uint64_t requiredRankedTailGrains(size_t capacity, size_t historyCount,
                                          size_t pendingChangeCount, size_t quantum);
  /// Measurement estimate for effective passes (including capture), and actual
  /// published-base events. Capacity still bounds every intermediate candidate,
  /// but the returned service count does not reject a valid stop request.
  static uint64_t requiredRankedTailGrainsForPasses(size_t capacity, size_t maximumPassCount,
      size_t baseEventCount, size_t historyCount, size_t pendingChangeCount, size_t quantum);
  /// Same tail with a completed, current canonical capture order available
  /// for private import. Its initial sort must be charged upstream. Callers
  /// must establish that receipt; this estimate alone never admits a stop.
  static uint64_t requiredPreparedRankedTailGrainsForPasses(size_t capacity, size_t maximumPassCount,
      size_t baseEventCount, size_t historyCount, size_t pendingChangeCount, size_t quantum);
  uint16_t maximumClosureGrains = 0;
  uint16_t maximumOrderGrains = 0;
  uint16_t maximumAtomicTransferGrains = 0;
  uint16_t orderEntryQuantum = 0;

  bool isDeclared() const;
  bool canFinalize(size_t openReservationCount, size_t frozenEventCount) const;
  /// The frozen candidate needs three independently resumable operations. The
  /// result is a service count, not a stop-admission limit.
  uint64_t requiredOrderGrains(size_t frozenEventCount) const;
  uint16_t maximumTotalGrains() const;
};

struct PendingStopAdmission {
  static constexpr uint16_t kClosureGrainsPerTurn = 1;
  static constexpr uint16_t kMaximumClosureGrains =
      RuntimePlaybackViewConfig::kMaxCaptureNoteReservations;

  uint32_t frozenGeneration = 0;
  uint32_t stopTick = 0;
  // Captured at the MIDI-handler state edge and emitted by the deferred owner,
  // never formatted from the handler itself.
  uint32_t transportCommandEpoch = 0;
  uint16_t remainingClosureGrains = 0;
  uint64_t remainingOrderGrains = 0;
  uint64_t remainingRankedPreparationGrains = 0;
  uint16_t rankedPreparationQuantum = 0;
  uint64_t remainingCanonicalPreparationGrains = 0;
  uint16_t canonicalPreparationQuantum = 0;
  uint64_t remainingHistoryRecycleGrains = 0;
  uint16_t historyRecycleQuantum = 0;
  /// Bounded undo-identity collection for this stop. Runs before the atomic
  /// commit on the prepared PLAYING route; a distinct class so a saturated
  /// estimate denies admission explicitly instead of stalling a phase.
  uint64_t remainingUndoCollectionGrains = 0;
  uint16_t undoCollectionQuantum = 0;
  const Loop* historyRecycleOwner = nullptr; // Borrowed from the successful joint commit.
  uint64_t historyRecycleGeneration = 0;
  bool historyRecycleStarted = false;
  PendingStopPhase phase = PendingStopPhase::ReservedClosure;
  PendingStopDestination destination = PendingStopDestination::Playing;
  bool pending = false;
  /// DEC-068: ring index of this interval's session-exit tail, captured at stop admission, or
  /// `Loop::kInvalidSessionExitReceipt`. The retiring job fills its final pass id and releases
  /// it; it never builds one from live session state, which under reentry belongs to a successor.
  size_t sessionExitReceiptRing = static_cast<size_t>(-1);

  void reset();
  /// Value identity of this admitted interval. Copies of the admission (credit
  /// probing, retargeting) keep the identity, unlike an object address, and a
  /// reused ring descriptor gets a fresh one because `begin` refreshes both
  /// fields. Qualifies Loop-owned worker scratch that outlives one grain.
  /// Zero is reserved for "no admitted interval" (the live session path).
  uint64_t intervalToken() const {
    return (static_cast<uint64_t>(frozenGeneration) << 32) | stopTick;
  }
  /// Declares the complete stop tail before the state changes. Each later
  /// grain may close at most `kClosureGrainsPerTurn` reserved notes.
  bool begin(uint32_t generation, uint32_t tick, uint32_t commandEpoch,
             size_t openReservationCount, size_t frozenEventCount,
             const StopAdmissionContract& contract, uint64_t rankedPreparationGrains = 0,
             uint64_t canonicalPreparationGrains = 0, uint64_t historyRecycleGrains = 0,
             uint64_t undoCollectionGrains = 0);
  bool consumeClosureGrain();
  /// Ordering remains a separate bounded grain after every reserved closure
  /// has been made durable in frozen staging. The atomic commit is permitted
  /// only after all preparation counters are exhausted and epoch/destination validate.
  bool consumeOrderGrain();
  /// Separate DEC-062 credits; closures must finish first. Exhaustion is a
  /// budget condition only: the caller must also validate prepared-source readiness.
  bool consumeRankedPreparationGrain();
  /// Separately admitted history/snapshot staging and sealed-capture verification.
  bool consumeCanonicalPreparationGrain();
  /// Post-commit storage recycling has a separate reservation and cannot delay
  /// the atomic commit. Receipt consumption precedes beginning the recycler.
  bool consumeHistoryRecycleGrain();
  /// Separate bounded collection class. Closures must already be durable and the
  /// estimate must have been admitted before the phase runs.
  bool consumeUndoCollectionGrain();
  /// Owner calls only after this stage's worker readiness has been verified.
  /// Release unused estimates in one turn; they are upper bounds, not work.
  bool retireCompletedPreparation();
  /// The recorded causal predecessor is no longer the published root, so no
  /// successor window will be published. Retire every remaining preparation
  /// credit in one place: the accepted capture pass still commits through the
  /// plain route and the unpublished companion preparation recycles back to its
  /// prewarmed horizon. The phase order guarantees closure and order completed.
  bool retireSupersededPreparation();
  /// STOPPED or an empty sealed capture skips audible/companion preparation before
  /// those workers start. Preserve closure/order and command-epoch obligations.
  bool retireSkippedCompanionPreparation(bool emptyCapture);
  void setPhase(PendingStopPhase next) { phase = next; }
  bool permitsPlaybackEmission() const {
    return !pending || destination == PendingStopDestination::Playing;
  }
  /// DEC-070: admitted for the PLAYING route and retargeted to STOPPED by a transport stop. Its
  /// interval contracted companion preparation, and the display resolves the overdubbed notes from
  /// that note-edit representation, so the STOPPED publication path must prepare and publish it
  /// (`commitStopped`) instead of skipping it. A job *admitted* for STOPPED never sets this.
  bool retargetedFromPlaying = false;
  bool finalizationComplete() const {
    return pending && remainingClosureGrains == 0 && remainingOrderGrains == 0 &&
           remainingRankedPreparationGrains == 0 && remainingCanonicalPreparationGrains == 0 &&
           remainingUndoCollectionGrains == 0;
  }
  void requestTransportStop();
  bool canCommit(uint32_t commandEpoch, PendingStopDestination requestedDestination) const;
};

/// Identity retained across deferred turns; a new frozen generation cannot
/// inherit a completed sorter or resume one that still references old rows.
struct FrozenCaptureOrderBinding {
  uint32_t generation = 0;
  uint32_t loopLengthTicks = 0;
  size_t eventCount = 0;
  bool bound = false;

  // Completion belongs to the shared index array at this generation.
  uint64_t storageGeneration = 0;

  bool matches(const CaptureAdmissionStaging& staging, uint64_t currentStorageGeneration) const;
};

/// Prepared audible half of a capture transfer. This is never sufficient by
/// itself to commit canonical content: the owner must validate both halves in
/// the same MIDI-boundary turn, including the current transport command epoch.
struct PlaybackSourceAdmission {
  uint32_t frozenGeneration = 0;
  uint32_t transportCommandEpoch = 0;
  PlaybackViewSourceIdentity baseSource;
  PlaybackViewSourceIdentity targetSource;
  uint8_t candidateIndex = RuntimePlaybackBridgeLifecycle::kNoBridge;
  bool ready = false;
};

/// Task 2.4 slice 2 -- the merged-path consume authority.
///
/// One coherent playback source: the event rows and the order that indexes them were
/// produced by one preparation for one source generation, and the cursor/reanchor seed
/// was resolved inside the same publication turn, so a consumer can never combine
/// `mergedEvents` and `playbackOrder` from different generations.
///
/// The complete descriptor -- storage, identity, coverage, seed, event count and
/// `generation` -- is replaced by a single assignment. A consumer reads it only while
/// `ready`; the off-clock preparation owner builds into the working window, so the
/// published pair's own buffer stays intact while the replacement is prepared.
struct PreparedPlaybackPair {
  bool ready = false;
  /// Bumped by every publication. A consumer adopts the new seed when this changes.
  uint64_t generation = 0;
  PlaybackViewSourceIdentity source;
  PlaybackCoverageInterval coverage;
  /// The immutable fixed rows. Events are stored in emission order and `order` is the
  /// identity permutation, so the pair is consumed through the same fixed-stream
  /// adapter as a published view.
  const FixedPlaybackEventStorage* storage = nullptr;
  size_t eventCount = 0;
  /// Index into `order` of the first event whose phase is strictly after the
  /// publication phase. Part of the pair; the consumer never recomputes it.
  uint16_t cursorSeed = 0;
};

struct LoopPlaybackRuntime {
  // --- S3.1 working-set lease -------------------------------------------------------------
  // This object *is* the per-slot working set, so it carries its own lease: the slot cannot hold
  // one without this runtime, and the runtime is externally allocated, so the bookkeeping costs
  // no DTCM (a field in the 8-way `Track` array would cost 8x).
  WorkingSetOutcome acquireWorkingSetLease(WorkingSetRole role) { return lease_.acquire(role); }
  WorkingSetOutcome releaseWorkingSetLease(WorkingSetRole role) {
    return lease_.release(lease_.generation(), role);
  }
  /// The slot runtime leaves the active set: bump the generation so a late release carrying the
  /// previous generation is reported as `StaleGeneration` (S4.2) instead of clearing a successor.
  WorkingSetOutcome releaseAllWorkingSetLeases() { return lease_.releaseAll(lease_.generation()); }

  /// S4.1: clear **this slot's** open-note ledger. The per-slot form of
  /// `TrackPlaybackRuntime::clearAllLedgers()`; an interval's stop must not clear a sibling slot's
  /// ledger while that sibling is still finalizing.
  void clearLedger() { ledger.clear(); }

  // --- S3.2 liveness predicates, one per owner kind this runtime can hold -------------------
  // `Track::noLiveOwnerForSlot` is the composite; these are the terms, so a fixture can hold one
  // owner kind and prove exactly that term refuses release.
  /// The slot's finalization owner: a capture interval whose stop this runtime is finalizing.
  /// Defensively independent of the ring, because it can be set without a ring entry.
  bool ownsLiveFinalization() const { return captureStopOwner != nullptr; }
  /// S4.2: record the admitted interval's finalization ownership, qualified by its generation.
  /// Ring entries are reused at stable addresses, so the pointer alone cannot tell a live owner from
  /// a stale one.
  void setCaptureStopOwner(PendingStopAdmission& admission, uint32_t generation) {
    captureStopOwner = &admission;
    captureStopOwnerGeneration = generation;
  }
  /// S4.2: clear only the owner this identity names. Generation-qualified and address-qualified, so a
  /// late cleanup on a reused runtime object is a rejected no-op instead of nulling a successor's
  /// marker.
  bool clearCaptureStopOwner(const PendingStopAdmission& admission, uint32_t generation) {
    if (captureStopOwner != &admission) return false;
    if (captureStopOwnerGeneration != generation) return false;
    captureStopOwner = nullptr;
    captureStopOwnerGeneration = 0;
    return true;
  }
  /// A prepared playback handover still being *built* owns this slot's scratch. The published pair
  /// is deliberately not a term: it is the slot's playback authority and stays valid while the slot
  /// sounds, so counting it would make release unreachable for every sounding slot. S4b.2's switch
  /// handover adds its own term when it exists.
  bool ownsInFlightPreparedHandover() const { return preparedPairRequestPending; }
  bool holdsWorkingSetRole(WorkingSetRole role) const {
    return lease_.isHeld(lease_.generation(), role);
  }
  WorkingSetRole workingSetRoles() const { return lease_.roles(); }
  uint16_t workingSetGeneration() const { return lease_.generation(); }

  /// S3.1 lease bookkeeping for this slot (roles + generation). No resource ownership.
  WorkingSetLeaseSlot lease_;
  uint32_t cachedLoopRevision = 0;
  uint32_t cachedTrackGeneration = 0;
  ActiveNoteLedger ledger;
  PlaybackMergedMidiEvents mergedMidiEvents;
  // --- Task 2.4 slice 2: immutable prepared pair -------------------------------
  /// The only merged/order authority the active clock path may consume.
  PreparedPlaybackPair preparedPair;
  /// Fixed, prewarmed storage for the published pair. A separate buffer from
  /// `mergedMidiEvents`, which stays the off-clock preparation's working window so
  /// the published pair survives the replacement build.
  FixedPlaybackEventStorage preparedPairStorage;
  /// Set by a consume miss on the clock path; cleared by the off-clock owner. Never
  /// allocates and never builds.
  bool preparedPairRequestPending = false;
  PlaybackViewSourceIdentity preparedPairRequestedSource;
  uint32_t preparedPairRequestTick = 0;
  /// Generation the clock path has already adopted. A publication changes it, so the
  /// next tick copies the new pair's cursor seed once.
  uint64_t preparedPairConsumedGeneration = 0;
  // Measurement (ARCHITECTURE-REVIEW § phase gate for 2.4). Plain runtime fields,
  // not Diagnostics counters: a new DTCM counter entry measured 4 KB of lost RAM1
  // local allowance.
  uint32_t preparedPairRequestCount = 0;
  uint32_t preparedPairPublishCount = 0;
  uint32_t preparedPairHoldTicks = 0;
  uint32_t preparedPairMaxLatencyTicks = 0;
  uint32_t preparedPairRebuildUnits = 0;
  /// Cold admission only: the fixed pair buffer must exist before a tick consumes it.
  bool prewarmPreparedPlaybackPair();
  /// True only when the published pair is the current coherent source for this
  /// exact revision/generation/snapshot/length. Never builds, never requests.
  bool preparedPairIsCurrent(uint32_t playbackRevision, uint32_t sourceGeneration,
                             const void* snapshotIdentity, uint32_t loopLengthTicks) const;
  /// Record a consume miss. Idempotent per source; does not build or allocate.
  void requestPreparedPlaybackPair(uint32_t playbackRevision, uint32_t sourceGeneration,
                                   const void* snapshotIdentity, uint32_t loopLengthTicks,
                                   uint32_t tick);
  /// Publish a prepared working window + order as the coherent pair. Copies both
  /// members, resolves the cursor seed against `publicationPhase`, then replaces the
  /// whole descriptor in one assignment.
  bool publishPreparedPlaybackPair(const MidiEvent* events, size_t eventCount,
                                   const uint16_t* order, size_t orderCount,
                                   const PlaybackViewSourceIdentity& source,
                                   const PlaybackCoverageInterval& coverage,
                                   uint32_t publicationPhase);
  /// Drop the published pair and any pending request (cache reset / new generation).
  void clearPreparedPlaybackPair();
  /// The clock path consumed a pair that was not current (P0 held while P1 prepares).
  void notePreparedPairHoldTick();
  void resetPreparedPairCounters();
  /// Fixed successor/bridge/capture buffers. A complete, identity-matched
  /// full-loop current view is consumed by ordinary active PLAYING; the legacy
  /// merged vector remains the compatibility fallback for other states until
  /// their independent handoffs exist. Large fixed buffers live in
  /// external-first dynamic storage, not in the Track object graph in RAM1.
  RuntimePlaybackViewSet* viewSet = nullptr;
  /// One persistent, unpublished successor builder. Its destination is always
  /// the lifecycle's build view; current and ready coverage are never passed
  /// to it.
  RuntimePlaybackViewBuild viewBuild;
  /// Orders only a frozen capture candidate. It owns no event memory and can
  /// never touch the current/ready audible views.
  RuntimePlaybackOrderBuilder frozenCaptureOrder;
  /// Canonical capture passes retain stable raw-tick ordering. This builder
  /// shares the frozen staging arrays with `frozenCaptureOrder`, but callers
  /// run the two orders serially: canonical rewrite first, audible order
  /// second.
  RuntimePlaybackOrderBuilder frozenCaptureCanonicalOrder;
  FrozenCaptureOrderBinding frozenCaptureOrderBinding;
  FrozenCaptureOrderBinding frozenCaptureCanonicalOrderBinding;
  uint64_t frozenCaptureOrderStorageGeneration = 0;
  /// Persistent fixed-grain cursor. Production copies canonical raw-tick order
  /// into separately reserved output; the legacy rewrite helper shares it.
  LoopEventStoreRewriteCursor frozenCaptureRewrite;
  const LoopEventStore* frozenCaptureRewriteOwner = nullptr;
  bool frozenCaptureRewriteIsCopy = false;
  Capture canonicalCaptureOutput;
  LoopEventStore::ChunkReservation canonicalCaptureChunks;
  // Borrowed from the admitted Track job until its cleanup completes. A cache
  // reset cannot reclaim that job's accepted source or partially copied output.
  // S4.2: the pointer is qualified by the owner's generation, because ring entries are reused at
  // stable addresses -- a late cleanup on the reused runtime object must not null a successor's
  // marker.
  PendingStopAdmission* captureStopOwner = nullptr;
  uint32_t captureStopOwnerGeneration = 0;
  bool prewarmCanonicalCaptureOutput();
  bool canonicalCaptureOutputAdmitted() const;
  bool replenishCanonicalCaptureChunk();
  /// S2b.4 (invariant 34): return the slot's canonical-copy credits to the pool. Idempotent -- a
  /// second call holding nothing is a no-op success, and re-reserving re-retains the ids. Refuses
  /// while the store is non-empty: the store is capture scratch whose chunks must be released with
  /// it, and `prewarmCanonicalCaptureOutput` requires an empty store anyway.
  bool releaseCanonicalCaptureReservation();
  RuntimePlaybackBridgeStream bridgeStream;
  RuntimePlaybackBridgeStream bridgeWindowSource;
  RuntimePlaybackViewBuild bridgeWindowBuild;
  uint8_t bridgeWindowBuildIndex = RuntimePlaybackViewLifecycle::kNoView;
  uint64_t bridgeWindowContextGeneration = 0;
  uint8_t bridgeWindowContextIndex = RuntimePlaybackBridgeLifecycle::kNoBridge;
  bool serviceBridgeWindow(uint32_t currentPhase, uint32_t windowTicks, size_t quantum);
  const RuntimePlaybackView* publishedBridgeWindow(const PlaybackViewSourceIdentity& source,
                                                   uint32_t phase) const;

  uint32_t bridgeProjectionCycle = 0;
  // Anchor new preparation in the running projection cycle, including wraps
  // serviced through ordinary playback while no bridge stream is selected.
  EmissionCursor currentCycleCursor(uint32_t phase, uint32_t sourceGeneration) const;
  RuntimeCaptureCompanions frozenCaptureCompanions;
  RuntimeNotePairLookup frozenCompanionPairLookup;
  RuntimeNoteDeleteLookup frozenCompanionDeleteLookup;
  RuntimeCompanionOverlapWalk frozenCompanionOverlaps;
  RuntimeCompanionSpanWalk frozenCompanionSpans;
  RuntimeCompanionReconstruction frozenCompanionReconstruction;
  RuntimeCompanionApply frozenCompanionApply;
  RuntimeCompanionBridgeMap frozenCompanionBridgeMap;
  RuntimeCompanionPasses frozenCompanionPasses;
  RuntimeCanonicalPassInputs canonicalPassInputs;
  // Pre-stop preparation owns only derived buffers. The caller retains passes
  // and live counters until discard/reset and invalidates before replacement.
  PlaybackViewSourceIdentity canonicalPrefixSource;
  const LoopPasses* canonicalPrefixPasses = nullptr;
  uint64_t canonicalPrefixGeneration = 0;
  uint32_t canonicalPrefixLength = 0;
  const uint64_t* canonicalPrefixStorageOwner = nullptr;
  uint64_t canonicalPrefixStorageGeneration = 0;
  uint64_t recordingWindowGeneration = 0;
  bool recordingWindowGenerationValid = false;
  // DEC-067 acceptance edge: armed when a deferred stop retires, emitted once the
  // finalization resources (canonical copy target + transfer chunks) are admitted again.
  bool canonicalReleaseMarkerArmed = false;
  // Diagnostics only: why the last first-record bar-1 preparation was denied.
  const char* firstRecordingBarDenial_ = "none";
  enum class CanonicalPrefixPhase : uint8_t { Idle, Preparing, Ready, Denied };
  CanonicalPrefixPhase canonicalPrefixPhase = CanonicalPrefixPhase::Idle;
  bool canonicalPrefixNeedsService(const LoopPasses& passes, const PlaybackViewSourceIdentity& source,
                                   uint32_t loopLength, const uint64_t& storageGeneration) const;
  RuntimePlaybackOrderBuildResult stepCanonicalPrefix(const LoopPasses& passes,
      const PlaybackViewSourceIdentity& source, const RuntimeCanonicalPassInputs::CurrentSource& live,
      size_t quantum);
  bool retainCommittedCanonicalTail(Loop& loop, const PlaybackViewSourceIdentity& source);
  bool resumeCanonicalAfterAppend(const LoopPasses& passes, uint32_t priorRevision,
      const PlaybackViewSourceIdentity& source, const RuntimeCanonicalPassInputs::CurrentSource& live,
      bool committedFrozenTail = false);
  // One existing child per call; shared by live overdub prework and stop.
  RuntimePlaybackOrderBuildResult stepCanonicalBase(const LoopPasses& passes,
      const PlaybackViewSourceIdentity& source, const RuntimeCanonicalPassInputs::CurrentSource& live,
      const EmissionCursor& anchor, size_t quantum);
  uint64_t requiredStopRankedGrains(const PlaybackViewSourceIdentity& source,
      size_t maximumPassCount, size_t historyCount, size_t pendingChangeCount, size_t quantum) const;
  // Uses the same pass merge and view fill/order workers as ordinary playback.
  // Completion publishes an immutable ready base, never changes audible current.
  enum class CanonicalWindowPhase : uint8_t { Idle, Merge, Fill, Order, Publish, Ready, Denied };
  CanonicalWindowPhase canonicalWindowPhase = CanonicalWindowPhase::Idle;
  uint8_t canonicalWindowIndex = RuntimePlaybackViewLifecycle::kNoView;
  bool canonicalWindowIsBridgeBase = false;
  uint64_t canonicalWindowPassGeneration = 0;
  PlaybackCoverageInterval canonicalWindowCoverage;
  RuntimePlaybackOrderBuildResult stepCanonicalWindow(
      const PlaybackCoverageInterval& coverage, const EmissionCursor& anchor, size_t quantum,
      bool bridgeBase = false);
  const RuntimePlaybackView* preparedCanonicalWindow(const PlaybackViewSourceIdentity& source) const;
  void discardCanonicalWindow();
  enum class CanonicalTailPhase : uint8_t { Idle, Normalize, Rows, StartCompanions, Companions, Complete, Denied };
  CanonicalTailPhase canonicalTailPhase = CanonicalTailPhase::Idle;
  PlaybackViewSourceIdentity canonicalTailTarget;
  uint64_t canonicalTailInputGeneration = 0, canonicalTailRowGeneration = 0,
           canonicalTailPassGeneration = 0;
  // Extends an owned, completed prefix; does not grant stop credits or publish.
  // Caller keeps live counters/changes alive and changes immutable until discard;
  // capture IDs, closures and retention must already be final before admission.
  bool beginFrozenCanonicalTail(const RuntimeCanonicalPassInputs::FrozenSource& live,
                               const PendingNoteChangeVec& changes,
                               const PlaybackViewSourceIdentity& target);
  RuntimePlaybackOrderBuildResult stepFrozenCanonicalTail(size_t quantum);
  // Stop service binds the target revision after rows finish: capture adds one
  // revision and nonempty companions add another. Add-only changes add no edits.
  RuntimePlaybackOrderBuildResult stepReservedFrozenCanonicalTail(PendingStopAdmission& stop, size_t quantum);
  bool frozenCanonicalTailReady(const PlaybackViewSourceIdentity& target) const;
  void discardFrozenCanonicalTail();
  RuntimeCaptureBridgeFill frozenCaptureBridgeFill;
  RuntimePlaybackBridgeBuild frozenCaptureBridgeBuild;
  PlaybackSourceAdmission frozenCaptureSourceAdmission;
  enum class CaptureBridgePhase : uint8_t { Idle, Fill, Map, Compile, Complete, Denied };
  CaptureBridgePhase captureBridgePhase = CaptureBridgePhase::Idle;
  // Borrow the exact immutable base, including a canonical ready window.
  // Never re-resolve it through the changing audible-current index mid-build.
  const RuntimePlaybackView* captureBridgeBase = nullptr;
  const RuntimePlaybackView* frozenCaptureBridgeBase() const;
  bool refreshFrozenCompanionBridge(const EmissionCursor& anchor);
  bool canonicalCaptureBridge = false;
  uint64_t captureBridgePreparationGeneration = 0;
  uint64_t captureBridgeMapGeneration = 0;
  // Capture-only diagnostic latch: one reason report per attempted bridge.
  uint16_t captureBridgeBeginFailureMask = 0;

  ~LoopPlaybackRuntime();

  bool isStale(uint32_t loopRevision, uint32_t trackGeneration) const;

  void syncRevision(uint32_t loopRevision, uint32_t trackGeneration);

  /// False while accepted capture owns scratch; retargets it to STOPPED recovery.
  bool reset(bool preserveLedger);

  /// Cold/boot or STOPPED-only allocation. Never call from a MIDI tick.
  /// `companionRowCapacity` is the derived companion-row reserve for this slot
  /// (DEC-066); it is clamped to the architectural row ceiling by the owner.
  /// `fullTakeAdmissible` must be the owning loop's `fullTakePlaybackViewAdmissible()`:
  /// when false the whole-take representation (bridges, their bases and the prepared
  /// pair) is not reserved, because no publisher can ever admit it (RC-MEM-2).
  bool prewarmViews(
      size_t companionRowCapacity = RuntimePlaybackViewConfig::kRecordsPerBridge,
      bool fullTakeAdmissible = true);
  /// Boot reserves only capture staging. Full views/bridges/workers are a
  /// separate cold admission; a staging-only runtime cannot select a bridge.
  bool prewarmCaptureStaging();
  /// Frozen role: the stop worker's immutable admission staging. A job owns this
  /// role from punch-out until its cleanup retires it.
  CaptureAdmissionStaging* captureStagingIfPrewarmed() {
    return viewSet && viewSet->frozenCaptureStaging().storage.isPrewarmed()
        ? &viewSet->frozenCaptureStaging() : nullptr;
  }
  const CaptureAdmissionStaging* captureStagingIfPrewarmed() const {
    return viewSet && viewSet->frozenCaptureStaging().storage.isPrewarmed()
        ? &viewSet->frozenCaptureStaging() : nullptr;
  }
  /// Live role: ingress reservations and appends for the open interval. Never
  /// the worker's frozen source.
  CaptureAdmissionStaging* liveCaptureStagingIfPrewarmed() {
    return viewSet && viewSet->liveCaptureStaging().storage.isPrewarmed()
        ? &viewSet->liveCaptureStaging() : nullptr;
  }
  const CaptureAdmissionStaging* liveCaptureStagingIfPrewarmed() const {
    return viewSet && viewSet->liveCaptureStaging().storage.isPrewarmed()
        ? &viewSet->liveCaptureStaging() : nullptr;
  }
  /// Exchange the live/frozen capture-staging roles after the live role has
  /// been frozen. Constant work; the two stagings never share physical rows.
  bool exchangeCaptureStagingRoles() {
    if (viewSet == nullptr || !viewSet->captureStagingsPrewarmed()) return false;
    viewSet->exchangeCaptureStagingRoles();
    return true;
  }
  /// Non-allocating access for active playback/capture paths. A partially
  /// allocated bundle is not usable: callers must treat it as unavailable
  /// until every view, bridge, and staging buffer has been prewarmed.
  RuntimePlaybackViewSet* viewSetIfPrewarmed() {
    return viewSet != nullptr && viewSet->prewarmed ? viewSet : nullptr;
  }
  const RuntimePlaybackViewSet* viewSetIfPrewarmed() const {
    return viewSet != nullptr && viewSet->prewarmed ? viewSet : nullptr;
  }

  /// STOPPED-state oracle bridge: copy an already-built legacy window into the
  /// published fixed current view. This performs no allocation and never
  /// touches a ready/build successor. Ordinary PLAYING consumes the completed
  /// full-loop view through the fixed-stream adapter.
  bool publishStoppedOracleView(const MidiEvent* source, size_t sourceCount,
                                const PlaybackViewSourceIdentity& sourceIdentity,
                                const PlaybackCoverageInterval& coverage);
  /// STOPPED-only publication from effective canonical passes. Owns events
  /// and pass origins together so ranked capture mapping can use this base.
  bool publishStoppedOracleView(const RuntimeCompanionPasses& baseline,
                                const PlaybackViewSourceIdentity& sourceIdentity,
                                const PlaybackCoverageInterval& coverage);
  /// STOPPED-only construction using idle prewarmed workers. Publishes origins
  /// only when canonical normalization/merge matches the existing oracle.
  bool publishStoppedCanonicalOracleView(const LoopPasses& passes,
      const PlaybackViewSourceIdentity& source, const RuntimeCanonicalPassInputs::CurrentSource& live,
      const MidiEvent* oracle, size_t oracleCount, const PlaybackCoverageInterval& coverage);
  /// Non-allocating active-path lookup. Only a complete, full-loop current
  /// view with the exact canonical identity is eligible for MIDI emission.
  const RuntimePlaybackView* publishedFullLoopView(
      const PlaybackViewSourceIdentity& sourceIdentity, uint32_t loopLengthTicks) const;
  /// Resolve the selected bridge by the effective canonical identity, while
  /// verifying its compiled indexes still refer to the immutable current view.
  const FixedPlaybackBridgeStorage* selectedFullLoopBridge(
      const PlaybackViewSourceIdentity& sourceIdentity, uint32_t loopLengthTicks) const;
  /// Finds the immutable loop view recorded by a bridge. The bridge may use a
  /// prepared canonical window which is ready but not the audible current.
  const RuntimePlaybackView* fullLoopBridgeBase(
      const FixedPlaybackBridgeStorage& bridge, uint32_t loopLengthTicks) const;
  /// Fixed-capacity publication preflight; denial leaves the selected bridge
  /// unchanged. The caller supplies the first affected, not-yet-emitted phase.
  bool canSelectPreparedFullLoopBridge(const PlaybackViewSourceIdentity& sourceIdentity,
                                      uint32_t loopLengthTicks,
                                      const EmissionCursor& earliestAffectedCursor) const;
  /// Track calls only within the same non-interleaved turn as complete preflight.
  void publishPreflightedFullLoopBridge(const EmissionCursor& cursor);
  bool selectPreparedFullLoopBridge(const PlaybackViewSourceIdentity& sourceIdentity,
                                   uint32_t loopLengthTicks,
                                   const EmissionCursor& earliestAffectedCursor);
  /// Prepare the audible half from frozen capture and frozen companion rows.
  /// Begins no canonical transfer and does not replace a selected source.
  bool beginFrozenCaptureBridge(const PlaybackViewSourceIdentity& baseSource,
                                const PlaybackViewSourceIdentity& targetSource,
                                uint32_t frozenGeneration, uint32_t commandEpoch,
                                uint32_t loopLengthTicks, const EmissionCursor& anchor,
                                const PlaybackBridgeRecord* companions, size_t companionCount);
  RuntimePlaybackOrderBuildResult stepFrozenCaptureBridge(size_t entryQuantum);
  RuntimePlaybackOrderBuildResult stepReservedFrozenCaptureBridge(PendingStopAdmission& stop, size_t quantum);
  /// Runtime-owned ranked mapping/compilation from complete typed preparation.
  /// Keep preparation owners frozen through final selection. No active routing.
  bool beginFrozenCompanionBridge(const PlaybackViewSourceIdentity& baseSource,
                                 const PlaybackViewSourceIdentity& targetSource,
                                 uint32_t frozenGeneration, uint32_t commandEpoch,
                                 uint32_t loopLengthTicks, const EmissionCursor& anchor);
  bool frozenBridgePreparationReady() const;
  /// Read-only audible half of atomic stop preflight. Canonical transfer
  /// capacity and companion-row publication must be proven separately.
  bool canSelectReservedFrozenCaptureBridge(const PendingStopAdmission& stop,
      const PlaybackViewSourceIdentity& baseSource, const PlaybackViewSourceIdentity& targetSource,
      uint32_t loopLengthTicks, const EmissionCursor& earliestAffectedCursor) const;
  bool canSelectFrozenCaptureBridge(uint32_t frozenGeneration, uint32_t commandEpoch,
                                   uint32_t loopLengthTicks, const EmissionCursor& earliestAffectedCursor) const;
  bool selectFrozenCaptureBridge(uint32_t frozenGeneration, uint32_t commandEpoch,
                                uint32_t loopLengthTicks, const EmissionCursor& earliestAffectedCursor);
  void discardFrozenCaptureBridge();

  /// Retain physical/logical stream positions between ticks. A changed source
  /// binds after the last fully consumed phase; wrap explicitly starts a new
  /// cycle only after draining the prior tail.
  RuntimePlaybackBridgeStream* prepareFullLoopBridgeStream(
      const PlaybackViewSourceIdentity& sourceIdentity, uint32_t loopLengthTicks,
      uint32_t completedPhase, bool beginNextCycle = false);

  /// Start only a coverage-proven build into the dedicated unpublished buffer.
  /// This is an owner-local admission primitive; task 2.4 supplies the
  /// post-MIDI owner turn that advances its separate fill/order grains.
  bool beginCoverageBuild(const CoverageServiceContract& contract, uint32_t currentPhase,
                          uint32_t handoffPhase, const MidiEvent* source, size_t sourceCount,
                          const PlaybackViewSourceIdentity& sourceIdentity,
                          const PlaybackCoverageInterval& coverage,
                          const EmissionCursor& cursorAnchor,
                          const LedgerBoundarySummary& ledgerBoundary);
  // Establish an empty raw bar-1 view before first-record input. This uses the
  // same fill/order/lifecycle buffers as ordinary playback.
  bool prepareFirstRecordingBar(uint32_t ticksPerBar);
  /// Retire the record-session return window so the next take can establish its
  /// own bar 1. A take that ends without the record-stop handoff (transport Stop
  /// during RECORDING, empty/skipped commit) leaves a raw window held, and a take
  /// that did hand off leaves the bound window held until a root replaces it.
  /// The preparation boundary for the next take owns both releases. A window
  /// whose stop job is still retiring is never released.
  bool releaseFirstRecordingBarReturnWindow();
  /// Select the bound record-stop return window as the audible source while the
  /// first root is still publishing. Stamps the launch source identity (the
  /// window was filled during RECORDING under the pre-stop identity) and marks it
  /// published, then returns it for the caller to consume exactly like a
  /// published full-loop view. Returns null unless the window is bound to this
  /// loop, carries no note whose closure is outside it, and `phase` is inside its
  /// coverage: the window only ever holds the first bar, so the caller asks again
  /// on the next tick and falls back once the playhead leaves it.
  const RuntimePlaybackView* adoptFirstRecordingBarReturnWindow(
      Loop& loop, const PlaybackViewSourceIdentity& sourceIdentity, uint32_t loopLengthTicks,
      uint32_t phase);
  /// Diagnostic label for the last `prepareFirstRecordingBar` denial. Session
  /// capture records it with the "first-record bar-1 preparation unavailable"
  /// warning; it is not a policy input.
  const char* firstRecordingBarDenialLabel() const;
  // Bind final record geometry and make the retained bar-1 view eligible for
  // the normal audible handoff. No source copy or order rebuild occurs here.
  bool bindFirstRecordingBar(uint32_t loopLengthTicks);
  /// Refresh the retained first-record bar from the live capture store using
  /// the same bounded fill/order workers as ordinary playback preparation.
  bool serviceFirstRecordingBar(const Loop& loop,
                                const PlaybackViewSourceIdentity& sourceIdentity,
                                uint32_t ticksPerBar, size_t entryQuantum = 32);
  RuntimePlaybackViewBuildGrain nextCoverageBuildGrain() const;
  RuntimePlaybackViewBuildGrain stepCoverageBuildFill(size_t entryQuantum);
  RuntimePlaybackViewBuildGrain stepCoverageBuildOrder(size_t entryQuantum);
  void discardCoverageBuild();

  bool beginFrozenCaptureOrder(uint32_t loopLengthTicks, uint32_t frozenGeneration);
  RuntimePlaybackOrderBuildResult stepFrozenCaptureOrder(size_t entryQuantum);
  bool frozenCaptureOrderComplete() const { return frozenCaptureOrder.complete(); }
  bool beginFrozenCaptureCanonicalOrder(uint32_t frozenGeneration);
  RuntimePlaybackOrderBuildResult stepFrozenCaptureCanonicalOrder(size_t entryQuantum);
  bool frozenCaptureCanonicalOrderComplete() const {
    return frozenCaptureCanonicalOrder.complete();
  }
  /// Write a completed canonical order into an unpublished capture store in
  /// fixed event quanta. This does not alter the frozen source or any audible
  /// view; callers mark the Loop sorted only after Complete.
  LoopEventStoreRewriteResult stepFrozenCaptureCanonicalRewrite(
      LoopEventStore& captureStore, uint32_t frozenGeneration, size_t eventQuantum);
  LoopEventStoreRewriteResult stepFrozenCaptureCanonicalCopy(
      uint32_t frozenGeneration, size_t eventQuantum);
  void discardFrozenCaptureCanonicalOrder();
  /// The only candidate state eligible for a later atomic capture transfer.
  /// It is generation-matched, has consumed every reserved terminal closure,
  /// and has a completed fixed-storage order. Callers must still validate the
  /// transport command epoch before making it audible.
  bool frozenCaptureCandidateReady(uint32_t frozenGeneration) const;
  void discardFrozenCaptureOrder();
};

struct LoopPlaybackRuntimeDeleter {
  void operator()(LoopPlaybackRuntime* runtime) const noexcept;
};

using LoopPlaybackRuntimePtr = std::unique_ptr<LoopPlaybackRuntime, LoopPlaybackRuntimeDeleter>;

LoopPlaybackRuntime* allocateLoopPlaybackRuntime();

class TrackPlaybackRuntime {
 public:
  void clearAllLedgers();

  LoopPlaybackRuntime& slot(uint8_t slotIndex);

  const LoopPlaybackRuntime& slot(uint8_t slotIndex) const;

  /// Allocate-or-get without null dereference. nullptr if allocation fails.
  LoopPlaybackRuntime* trySlot(uint8_t slotIndex);

  /// Non-allocating peek. nullptr if this slot has no runtime yet.
  LoopPlaybackRuntime* slotIfAllocated(uint8_t slotIndex);
  const LoopPlaybackRuntime* slotIfAllocated(uint8_t slotIndex) const;

  /// S4.1/S4.2: release one slot's runtime working set under its `(slot, generation)` identity.
  ///
  /// The generation is the slot's working-set lease generation, which is the per-slot runtime
  /// generation: a release whose generation does not match is a reported no-op, so a late caller
  /// cannot release a reused slot. A slot runtime's own `reset()` refusal (accepted capture still
  /// owns its scratch) is **preserved and reported** as `RefusedBusy` rather than discarded -- the
  /// pre-S4.1 `resetAll` dropped it. `preserveLedger` selects the reset variant the caller needs.
  WorkingSetOutcome releaseSlot(uint8_t slotIndex, uint16_t generation, bool preserveLedger);

  /// The identity a caller must present to `releaseSlot`.
  uint16_t slotGeneration(uint8_t slotIndex) const;

 private:
  std::array<LoopPlaybackRuntimePtr, Config::MAX_LOOPS_PER_TRACK> runtimeBySlot_{};
};
