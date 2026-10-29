#pragma once

#include <array>
#include <cstdint>
#include <memory>

#include "ActiveNoteLedger.h"
#include "Globals.h"
#include "LoopEventStore.h"
#include "LoopPasses.h"
#include "Utils/PlaybackBuildRefusal.h"
#include "PassReclaim.h"  // task 3.6: RetainedPlaybackSource + the source-role vocabulary
#include "PlaybackMergedMidiEvents.h"
#include "RuntimePlaybackView.h"
#include "RuntimePlaybackViewBuild.h"
#include "RuntimePlaybackBridge.h"
#include "RuntimeCaptureCompanions.h"
#include "RuntimeCompanionOverlapWalk.h"
#include "RuntimeCompanionSpanWalk.h"
#include "CommittedEventRange.h"  // task 3.7a (I2): the retained-source gather's resumable window cursor
#include "WindowPagedStream.h"     // task 3.7a (I3b): the bounded legacy path's page ring
#include "RuntimeCompanionReconstruction.h"
#include "RuntimeCompanionApply.h"
#include "RuntimeCompanionBridgeMap.h"
#include "RuntimeCompanionPasses.h"
#include "RuntimeCanonicalPassInputs.h"
#include "RuntimeNotePairLookup.h"
#include "Utils/WorkingSetLease.h"
#include "Utils/OverdubOverlayLane.h"
#include "Utils/PlaybackSourceReadGuard.h"

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
  /// Record-only: after the terminal closure Offs, copy the frozen raw take into
  /// the same private output through the bounded stop projection. Appended to the
  /// enum so existing phase ranges are unchanged.
  RecordSourceStaging,
  /// Record-only terminal: the take is canonically ordered, identity-complete and
  /// policy-filtered in the private output and awaits publication (S4f.20).
  RecordPrepared,
  /// Record-only terminal: the job owns a capacity-proven staged Record pass and
  /// awaits the atomic publication transaction (S4f.20 completion). Appended so
  /// existing phase ranges are unchanged.
  RecordPassReady,
  /// Record-only terminal: the accepted Record take produced no prepared content
  /// once projection and minimum-length policy ran (all rows dropped, all pairs
  /// filtered, or an empty take). It keeps the inline route's no-content
  /// no pass, no undo, no persistence -- and retires terminally
  /// rather than pending forever. Appended so existing phase ranges are unchanged.
  RecordEmpty,
  /// Record-only: the pass, its `RecordPassAdded` undo entry and the once-only
  /// persistence admission are published; the job awaits bounded retirement.
  RecordPublished,
};

/// A deferred stop holds the FIFO head, and that ring holds eight, so **any** preparation phase that
/// cannot advance turns the user's symptom into "cannot stop overdubbing after around 8 times" -- the
/// presses behind it are admitted until the ring is full, then retained. Two such phases exist on the
/// PLAYING route, and both have a content-preserving escape:
///   * `CanonicalPrefix` builds the canonical *base* pass set. Only the companion/tail representation
///     needs it; the captured rows travel `CanonicalOrder` -> `CanonicalRewrite` -> `PrepareTransfer`
///     without it, so giving it up costs the note-edit representation and never the content.
///   * `CanonicalTail` .. `CompanionBridge` build that representation. Its escape already existed
///     (DEC-070 raw pass only) but was gated on `retargetedFromPlaying`, so a job still bound for
///     PLAYING could stall there unbounded too; the gate is now the phase, not the destination.
/// The distance from a *sticky* failure is what makes this necessary rather than cosmetic:
/// `stepCanonicalPrefix` latches `CanonicalPrefixPhase::Denied` and nothing else ever clears it, and a
/// base whose inputs never become current returns `Denied` on every grain with no report at all.
enum class StopPreparationEscape : uint8_t {
  None = 0,
  /// Give up the canonical base and continue at `CanonicalOrder`.
  AbandonBase,
  /// Give up the companion representation, keep the staged pass, transfer now.
  RawPassOnly,
};

/// Two *no-progress* budgets: the counter resets whenever the phase changes, so a healthy build that
/// advances never approaches one. Both numbers are set from measurement, in both directions.
/// * `CanonicalPrefix` (the canonical **base**): healthy builds measured **471** grains
///   (`session_20261027_140638.log`: 68, 208, 369, 438, 471 over five stops of a 24-bar take); the
///   stalls ran **9,370** and **9,607** without advancing (`session_20261027_160050.log`,
///   `134320.log`). 2,048 is ~4.3x the healthy maximum -- about five seconds of service at the
///   measured cadence -- and abandoning the base costs only the companion representation.
/// * The companion family keeps DEC-070's **512** for a *retargeted* job and keeps its original
///   `retargetedFromPlaying` gate. Extending that bound to jobs still bound for PLAYING was tried and
///   **withdrawn the same day**: it broke `test_4_20_settled_overdub_heap_peak_measurement`'s
///   128-cycle fixture (the job parked at `CompanionBridge` after the forced retire), and the healthy
///   phase counts there reach 233 and grow. The tail is instead escaped *by cause*, not by counting:
///   see `stopCompanionPathIsUnreachable`, which fires deterministically once a job's base was
///   abandoned, because `beginFrozenCanonicalTail` cannot succeed without it.
constexpr uint16_t kCanonicalBaseStallGrainLimit = 2048;
constexpr uint16_t kRetargetedCompanionStallGrainLimit = 512;

/// Is this phase one whose non-advance is bounded by a grain budget? Only the base and DEC-070's
/// original retargeted companion range. The ordering/rewrite phases are excluded everywhere (they
/// produce the rows); `PrepareTransfer`/`UndoCollection` are excluded for a PLAYING job (staging the
/// pass and undo identity are not ours to drop).
inline bool stopPreparationIsBounded(uint8_t phase, bool retargetedFromPlaying,
                                     bool successorIntervalCapturing) {
  // The base is bounded only while a **successor interval is capturing** on the same slot: that is the
  // measured device state (a stop at destination PLAYING whose reentry successor took the shared
  // canonical machinery), and a count-only bound is not safe -- the 128-cycle fixtures legitimately
  // declare hundreds of millions of preparation grains, and a fixed 2,048-grain budget retired their
  // jobs prematurely (measured: `test_shared_capture_finalization` 3 failures, phase 9,
  // `capture_store=empty`). With no successor capturing, the base keeps its old, unbounded treatment.
  if (phase == static_cast<uint8_t>(PendingStopPhase::CanonicalPrefix))
    return successorIntervalCapturing;
  return retargetedFromPlaying &&
         phase >= static_cast<uint8_t>(PendingStopPhase::CanonicalTail) &&
         phase <= static_cast<uint8_t>(PendingStopPhase::CompanionBridge);
}

inline uint16_t stopPreparationStallGrainLimit(uint8_t phase) {
  return phase == static_cast<uint8_t>(PendingStopPhase::CanonicalPrefix)
             ? kCanonicalBaseStallGrainLimit
             : kRetargetedCompanionStallGrainLimit;
}

/// The decision table. Pure and phase-scoped so both boundaries can be pinned natively without
/// manufacturing an unadvanceable staging set on the board.
inline StopPreparationEscape stopPreparationEscape(uint8_t phase, uint16_t grains,
                                                    bool retargetedFromPlaying,
                                                    bool successorIntervalCapturing) {
  if (!stopPreparationIsBounded(phase, retargetedFromPlaying, successorIntervalCapturing))
    return StopPreparationEscape::None;
  if (grains < stopPreparationStallGrainLimit(phase)) return StopPreparationEscape::None;
  return phase == static_cast<uint8_t>(PendingStopPhase::CanonicalPrefix)
             ? StopPreparationEscape::AbandonBase
             : StopPreparationEscape::RawPassOnly;
}

/// Sentinel in the per-job stall phase: "this job's canonical base was abandoned" (DEC-165). A real
/// phase code is 0..11, so the next job's first grain clears it by overwriting, which is exactly the
/// lifetime wanted -- it is the *job's* fact, not the runtime's: a base built later for a successor
/// interval must never make an abandoned job believe its tail became reachable.
constexpr uint8_t kCanonicalBaseAbandonedPhase = 0xFF;

/// Cause-based, not counted: `beginFrozenCanonicalTail` requires a **ready** canonical base, so once a
/// job's base has been abandoned its companion tail can never begin, and waiting on it is waiting
/// forever. This is the deterministic partner of `AbandonBase`: the stop continues on the raw-only
/// route with its staged pass intact, and no grain budget is involved, so a healthy job is unaffected.
inline bool stopCompanionPathIsUnreachable(uint8_t phase, bool retargetedFromPlaying,
                                           bool canonicalBaseAbandoned) {
  return !retargetedFromPlaying && canonicalBaseAbandoned &&
         (phase == static_cast<uint8_t>(PendingStopPhase::CanonicalTail) ||
          phase == static_cast<uint8_t>(PendingStopPhase::CompanionHistory) ||
          phase == static_cast<uint8_t>(PendingStopPhase::CompanionBridge));
}

/// Why the **Record** stop's normalization grain refused. Diagnostic only -- nothing acts on it (DEC-166).
/// The lane has six `Denied` returns and, before this, reported none of them: a Record job parked at
/// `CanonicalOrder` therefore looked exactly like a job nobody serviced, which is what made the second device
/// run (`captures/session_20261027_161759.log`) unattributable while it bricked the stop ring for 32 s.
/// `PreparationDenied` is the terminal one: `RecordPreparation::Step::Denied` deliberately keeps its binding
/// (the private output may already be partially ordered), so that job can never advance on this interval.
enum class RecordNormalizationDenial : uint8_t {
  None = 0,
  /// The job identity/geometry guard at the top of the grain.
  JobIdentity,
  /// The slot's capture-stop owner is not this job (or its generation moved).
  OwnerLost,
  /// The private canonical output cannot take reserved appends.
  OutputNotReady,
  /// No transfer chunks available to bind the preparation role.
  ChunkStarvation,
  /// `RecordPreparation::begin` refused this job's binding.
  RoleBindRefused,
  /// `RecordPreparation::step` returned `Denied` -- sticky by design.
  PreparationDenied,
};

/// Consecutive `Denied` grains at the same phase before the row is raised, once per job. The lane is
/// resumable (`Pressure`/`Pending` consume a preparation grain and still count as progress), so a healthy
/// job never accumulates denials; a parked one accumulates thousands. Chosen low because the report is
/// free (no policy) and the cost of a false report is one line, while the cost of no report is another
/// unattributable device run.
constexpr uint16_t kRecordNormalizationDenialReportLimit = 64;

/// Flash-resident name for the row; defined beside the lane so the literals stay in that cold unit.
const char* recordNormalizationDenialName(RecordNormalizationDenial denial);

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
  /// DEC-165's cause-based escape retires its destination-independent cause: the companion path
  /// can never *begin* on this job, so the companion estimates retire here regardless of
  /// destination. A destination-Playing job that reaches the escape is exactly the case the
  /// STOPPED-gated `retireSkippedCompanionPreparation` refused 100% of the time, which wedged
  /// the FIFO head at `CanonicalTail`/`AtomicTransfer`. The atomic transfer then commits the
  /// staged pass without a successor window (superseded route, taken by cause).
  bool retireCompanionSkippedForBaseAbandonment();
  /// DEC-186: the same retirement contract for a second cause -- a **begun** companion preparation whose
  /// pinned `playbackRevision` moved, so the inline candidate is obsolete and the job retires raw and owes
  /// DEC-172 convergence. One implementation: either way the companion family cannot complete on this job,
  /// so the estimates it would consume are not work.
  bool retireObsoleteCompanionPreparation() { return retireCompanionSkippedForBaseAbandonment(); }
  void setPhase(PendingStopPhase next) { phase = next; }
  bool permitsPlaybackEmission() const {
    return !pending || destination == PendingStopDestination::Playing;
  }
  /// DEC-070: admitted for the PLAYING route and retargeted to STOPPED by a transport stop. Its
  /// interval contracted companion preparation, and the display resolves the overdubbed notes from
  /// that note-edit representation, so the STOPPED publication path must prepare and publish it
  /// (`commitStopped`) instead of skipping it. A job *admitted* for STOPPED never sets this.
  bool retargetedFromPlaying = false;
  /// This job's canonical base was abandoned (superseded or terminally denied), so its companion
  /// path can never begin. **Per job**, not per track: a previous stop's abandonment must never
  /// change how the next stop prepares its own base (the DEC-165 sentinel this replaces was written
  /// to `Track::retargetStallPhase_`, so one supersession made every later stop take the
  /// "canonical base unavailable" shortcut -- measured on device 2026-10-27, session_20261027_231625).
  bool canonicalBaseAbandoned = false;
  /// The canonical **window** (the successor bridge's base) latched denied, but the **prefix** is
  /// ready. The window is only the bridge's input: the tail extends the prefix and the companion
  /// representation the display reads is still derivable, so this is *not* a base abandonment. The
  /// stop keeps the companion path and retires only the bridge estimate (DEC-172 correction, device
  /// `session_20261027_231625.log`: the measured base denials were `where=window,reason=fill_order_denied`,
  /// i.e. exactly this shape, and treating them as base failures is what lost the companion).
  bool canonicalBridgeUnavailable = false;
  /// The commit proof for that no-bridge route: an owner-**superseded** base (a record-return pass
  /// committed while this interval's companion derivation was completing) replaces the canonical
  /// owner revalidation with the lossless-exchange proof
  /// `Loop::preparedCompanionHistoryStoreUnchanged` (DEC-172 correction 3). A window-denied base
  /// (the prefix stayed current) keeps the normal owner proof.
  bool supersededCompanionExchange = false;
  /// DEC-186: this job escaped `CompanionHistory` because its begun companion preparation was pinned to a
  /// `playbackRevision` that moved under it (`SourceRevision`). The pass commits raw; the derived companion
  /// representation is owed through the post-commit generation by DEC-172's slot-level obligation, exactly
  /// like a superseded base. Marked after the commit (never before), so the target is never speculative.
  bool companionRevisionSuperseded = false;
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

/// Why the canonical (ordered) capture build could not be admitted this turn. Two of these are
/// permanent by construction and used to be indistinguishable from "no turn granted":
/// `BindingBusy` (a build of either kind owns the shared index scratch) and `StagingContentMoved`
/// (the binding names *this* interval but its rows or storage epoch moved under it, which is a
/// genuine in-flight invalidation and must never be re-bound). A binding that names a *different*
/// interval is not an invalidation of anything: nothing in flight describes the current rows, so
/// the phase retires it and re-binds -- refusing there wedged `PendingStopPhase::CanonicalOrder`
/// forever, and one wedged job blocks every later stop (`serviceAcceptedInteractionTurn` yields
/// while a job is pending).
enum class FrozenCaptureCanonicalOrderBegin : uint8_t {
  Ready = 0,
  StagingUnavailable,
  StagingNotFrozen,
  FrozenGenerationMoved,
  StagingGenerationMoved,
  BindingBusy,
  StagingContentMoved,
  OpenReservation,
  BuildDenied,
};

/// Flash-resident name for the row; the caller is a cold function, so the literals cost no RAM1.
const char* frozenCaptureCanonicalOrderBeginName(FrozenCaptureCanonicalOrderBegin result);

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

/// DEC-108 §13b / task 3.10 (the review's step 1, *"bound the work of each request"*): **the work an admitted
/// request was bounded to**, recorded where its build began. Every figure is *derived from the entries the fill
/// must inspect* (`sourceEntries`), never asserted: the grain counts come from the contract's own
/// `CoverageServiceContract::deriveGrainsFrom`, and `retainedEventBound` is the window's storage capacity -- the
/// maximum number of events a window can retain (`RuntimePlaybackViewConfig::kWindowAdmissionLimit`). It is read
/// **only while the build it describes is active** (see `activeViewBuildWork`), so a finished build's record can
/// never be mistaken for outstanding work.
struct PlaybackViewAdmittedWork {
  /// The entries the fill must inspect: the gathered window, not the loop.
  uint32_t sourceEntries = 0;
  /// The maximum retained window events (the destination window's own storage capacity).
  uint32_t retainedEventBound = 0;
  uint16_t fillGrains = 0;
  uint16_t orderGrains = 0;
  uint16_t promotionGrains = 0;
  /// The **deadline** this window was admitted against: the phase at which the playhead next needs the interval the
  /// build serves. Retained so that a **later** admission can be checked against it (review step 2) rather than only
  /// against its own margin -- "admission must leave every already-admitted window's deadline feasible".
  uint32_t handoffPhase = 0;
  /// Absolute deadline in a qualified clock epoch. Never reconstruct from a repeating phase.
  /// A zero epoch means this legacy/gated request has no qualified deadline.
  uint64_t admittedTick = 0;
  uint64_t deadlineTick = 0;
  uint32_t clockEpoch = 0;
  uint32_t admittedLoopLength = 0;
  /// DEC-108 §13a / task 3.10 (blocker 3, half 2): the **interval the promise is for**, so a late window can be judged
  /// against the traversal it was admitted for -- and so the miss episode can be keyed on the interval the promise names
  /// rather than on whatever the consumer happens to be reading. `handoffPhase` is that interval's start; this is its
  /// length.
  uint32_t admittedIntervalLength = 0;
  /// **3.15f / C:** true when the admission positioned this demand at a **later** loop occurrence than its first
  /// matching phase (the short-loop case). The ready then waits for the positioned absolute tick; an ordinary
  /// first-occurrence window keeps adopting at its phase as before.
  bool positionedLater = false;
  /// The same saturating sum the contract uses, so a reader cannot disagree with `requiredGrains()`.
  uint32_t requiredGrains() const {
    const uint32_t total = static_cast<uint32_t>(fillGrains) + orderGrains + promotionGrains;
    return total > UINT16_MAX ? UINT16_MAX : total;
  }
};
/// DEC-108 §13(c) / task 3.10 (increment 24): **why a slot's preparation demand is still pending.** The name is the
/// reader-visible answer to "the request was refused -- by what?", so a caller can tell a demand the owner will
/// pick up from one that cannot be served as asked. It is a *reading*: this slice keeps the obligation, it does not
/// retry it (the re-attempt needs a request-side gather buffer, which is the next increment's decision).
enum class PlaybackViewPendingReason : uint8_t {
  /// Nothing is retained: either nothing is owed, or the demand was discharged by an admission.
  None = 0,
  /// The interference budget refused it: admitting would break a promise already made to a window on this track.
  InterferenceBudget,
  /// The declared contract cannot complete the window before its own handoff.
  CannotCompleteBeforeHandoff,
  /// The slot already owes a build, so this demand **coalesces into that obligation** rather than starting a second.
  BuildInFlight,
  /// A prepared successor is already waiting and must be adopted or released before another window is prepared.
  SuccessorAlreadyPrepared,
  /// The retained positional deadline no longer applies because the **clock epoch** changed (a reset, seek, rebase,
  /// clock-source or PPQN transition). The obligation survives; only the promise's position is void.
  DeadlineInvalidatedEpoch,
  /// ... because the loop's **geometry** changed (a different loop length): the phase the deadline was measured
  /// against now describes a different loop. The obligation survives too.
  DeadlineInvalidatedGeometry,
  AwaitingSourceQualification,
  SourceNotCurrent,
  WindowExceedsRetainedCapacity,
  DeadlineExpired,
  /// Task 3.12 / 5.5: the demand survives, and the reason it is still owed is that the interval's own content exceeds
  /// the emission service's declared closure capacity -- the paged compatibility lane refused it **by name** rather
  /// than half-serving it. Terminal for the lane's retry (`committedPlaybackRetryCanProgress`), exactly as an
  /// over-capacity window is: a refusal with a name is an answer, not a demand to re-gather every turn.
  ClosuresExceedDeclaredCapacity,
  /// Task 3.7a / I7: the demand survives, and the reason it is still owed is that the retained revision **pins Active
  /// Note note-edit rows**, whose resolution is per pass layer. The window lane serves those (it resolves them layer by
  /// layer), but the paged compatibility lane cannot page them without serving the unresolved bodies, so it refuses
  /// **by name** and establishes no ring. Terminal for the lane's retry, exactly as the two refusals above: a refusal
  /// with a name is an answer, not a demand to re-page every turn.
  PinnedEditRowsUnresolved,
  /// DEC-136 — private committed-root preparation before Capture-ready adoption: the demand is the **root-backed
  /// successor** a bound Record job's frozen return window needs, and the ordinary rolling envelope can never
  /// admit its own bounded work on this loop geometry (`leadTicks + grains x (denied+1) x advance` exceeds a whole
  /// traversal, so no legal lead exists). It is therefore prepared **privately** behind the still-served frozen
  /// reader and is **owed without a deadline**: no promise is fabricated, the served window is never torn, and
  /// adoption happens only at a real boundary where the completed window covers the playhead. This reason is the
  /// route's whole mark -- the position gates tolerate it only while `rootReturnSuccessorIsOwedForSlot` holds.
  PrivateRootReturnPreparation,
};
/// DEC-108 §13(c) / task 3.10 (increment 24): **the retained pending preparation request** -- the demand a refused
/// request leaves behind, so `PlaybackViewRequestResult::TrackInterferenceBudgetExceeded` is a *promise* and not a
/// dropped request. It stores the demand, never the events: the identity the request was made against, the interval
/// it serves, the absolute deadline it was promised and the bounded cost that request would owe. The admission path
/// that refuses it writes it, the admission that accepts it clears it, and it is read through generation-qualified
/// accessors -- a record retained under an earlier operation generation is never revived (DEC-108 §13(d)).
struct PlaybackViewPendingRequest {
  /// The slot owes a demand whose last attempt was refused. Cleared by an admission and by a coalescing successor
  /// request -- **never** by an epoch or geometry change, which void the deadline, not the obligation.
  bool active = false;
  enum class SourceKind : uint8_t { Borrowed, CommittedSnapshot, CommittedLaunch, LiveCapture };
  SourceKind sourceKind = SourceKind::Borrowed;
  bool positionInvalidated = false;
  /// Replayable live recipe; token is content currency, not source lifetime.
  CaptureProjectionIdentity liveCaptureProjection{};
  int32_t liveProjectionStart = 0;
  uint32_t liveMaximumTick = 0;
  uint16_t liveLeaseGeneration = 0;
  uint16_t countedMisses = 0;
  /// The **operation generation** the demand was retained under (DEC-108 §13(d)): a returning slot must not revive
  /// an earlier operation, so a record whose generation has moved is stale, and a demand made under the new
  /// generation starts a fresh record instead of inheriting this one's deadline.
  uint32_t operationGeneration = 0;
  /// The source identity the requesting caller asked for; newest-wins under coalescing.
  PlaybackViewSourceIdentity source;
  /// The coverage interval that identity serves; newest-wins under coalescing.
  PlaybackCoverageInterval coverage;
  /// The **oldest outstanding deadline** of this demand (DEC-108 §13(a)): the absolute tick at which the served
  /// interval's traversal is due, kept from the earliest coalesced request made under the same clock epoch.
  uint64_t deadlineTick = 0;
  /// The clock epoch `deadlineTick` is qualified in. Zero means the demand holds **no qualified position** (the
  /// caller supplied no stamp), which is unqualified rather than expired.
  uint32_t clockEpoch = 0;
  /// The loop length the deadline was measured against, so a **geometry** change can be told from an epoch change.
  uint32_t loopLengthTicks = 0;
  /// The phase the request was made at -- the origin of the traversal the deadline names.
  uint32_t requestedPhase = 0;
  /// Why this demand is still pending: the last refusal by name, or an invalidation the reader has named.
  PlaybackViewPendingReason reason = PlaybackViewPendingReason::None;
  /// The bounded cost this demand would owe once admitted, derived exactly as an admitted window's is
  /// (`CoverageServiceContract::deriveGrainsFrom`). A high-level busy request cannot
  /// gather into a borrowed source: its cost is the maximum admissible window's
  /// bound, not an observed count. Resolve/count again before actual admission.
  PlaybackViewAdmittedWork work;
  /// The reason the retained positional deadline no longer applies, or `None` while it does (and while the demand
  /// holds no qualified deadline at all). A reading only: it never clears the obligation.
  PlaybackViewPendingReason deadlineInvalidation(uint32_t currentLoopLengthTicks, uint32_t currentEpoch) const {
    if (!active || clockEpoch == 0) return PlaybackViewPendingReason::None;
    if (loopLengthTicks != 0 && currentLoopLengthTicks != loopLengthTicks) {
      return PlaybackViewPendingReason::DeadlineInvalidatedGeometry;
    }
    if (currentEpoch != clockEpoch) return PlaybackViewPendingReason::DeadlineInvalidatedEpoch;
    return PlaybackViewPendingReason::None;
  }
};

struct LoopPlaybackRuntime {
  // The external-first slot allocator is FLASH-resident. Keep the defaulted
  // constructor's value-initialization and fold its one-time body into that
  // cold allocator on the target instead of emitting a large ITCM ctor.
#if defined(__IMXRT1062__)
  __attribute__((always_inline)) LoopPlaybackRuntime() = default;
#else
  LoopPlaybackRuntime() = default;
#endif
  // --- S3.1 working-set lease -------------------------------------------------------------
  // This object *is* the per-slot working set, so it carries its own lease: the slot cannot hold
  // one without this runtime, and the runtime is externally allocated, so the bookkeeping costs
  // no DTCM (a field in the 8-way `Track` array would cost 8x).
  WorkingSetOutcome acquireWorkingSetLease(WorkingSetRole role) { return lease_.acquire(role); }
  WorkingSetOutcome releaseWorkingSetLease(WorkingSetRole role) {
    PlaybackSourceReadGuard guard;
    if (viewBuild.callInProgress() || bridgeWindowBuild.callInProgress() ||
        viewBuild.preparationInProgress() || bridgeWindowBuild.preparationInProgress())
      return WorkingSetOutcome::RefusedBusy;
    return lease_.release(lease_.generation(), role);
  }
  /// The slot runtime leaves the active set: bump the generation so a late release carrying the
  /// previous generation is reported as `StaleGeneration` (S4.2) instead of clearing a successor.
  WorkingSetOutcome releaseAllWorkingSetLeases(
      const RuntimePlaybackViewBuild::PreparationTurn* primary = nullptr,
      const RuntimePlaybackViewBuild::PreparationTurn* bridge = nullptr) {
    PlaybackSourceReadGuard guard;
    if (viewBuild.callInProgress() || bridgeWindowBuild.callInProgress() ||
        (primary && !primary->owns(viewBuild)) || (bridge && !bridge->owns(bridgeWindowBuild)) ||
        (viewBuild.preparationInProgress() && (!primary || !primary->owns(viewBuild))) ||
        (bridgeWindowBuild.preparationInProgress() && (!bridge || !bridge->owns(bridgeWindowBuild))))
      return WorkingSetOutcome::RefusedBusy;
    if (ownsLiveCaptureBuildBorrow()) return WorkingSetOutcome::RefusedBusy;
    return lease_.releaseAll(lease_.generation());
  }

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
  /// Slot-owned RecordPublished presentation identity. Unlike captureStopOwner this
  /// survives FIFO job release and is cleared only after the frozen reader is retired.
  void setPresentationOwner(const void* owner, uint32_t generation) {
    presentationOwner = owner;
    presentationOwnerGeneration = generation;
  }
  bool clearPresentationOwner(const void* owner, uint32_t generation) {
    if (presentationOwner != owner || presentationOwnerGeneration != generation) return false;
    presentationOwner = nullptr;
    presentationOwnerGeneration = 0;
    return true;
  }
  const void* presentationOwner = nullptr;
  uint32_t presentationOwnerGeneration = 0;

  bool holdsWorkingSetRole(WorkingSetRole role) const {
    return lease_.isHeld(lease_.generation(), role);
  }
  WorkingSetRole workingSetRoles() const { return lease_.roles(); }
  uint16_t workingSetGeneration() const { return lease_.generation(); }

  /// S3.1 lease bookkeeping for this slot (roles + generation). No resource ownership.
  WorkingSetLeaseSlot lease_;
  // --- Task 3.6 (DEC-108 items 4, 12(a), 13(a)): the retained source revisions ------------------------------
  /// The slot's retained sources, in explicit **roles** (serving / candidate / retiring) rather than as "one
  /// revision per slot". Each holds descriptor pins the pass-level reclamation consumer reads **and** its own
  /// store references, so neither the reclamation path nor `Loop::resetPassTimeline` can free the bodies a retained
  /// window still reconstructs from. Admission is bounded (`kMaxRetainedPlaybackSources`) and refuses by name
  /// rather than discarding a source that is still needed. Charged to the envelope: two entries, each two vectors
  /// plus one chunk-id list, on the per-slot working set (DEC-108 item 4's source-retention term).
  std::array<RetainedPlaybackSource, kMaxRetainedPlaybackSources> retainedSources{};
  /// True when any role holds a source. The release side is `Track::tryReleaseRetainedPlaybackSource`, and the
  /// retention is a named term of `Track::noLiveOwnerForSlot`.
  bool hasRetainedPlaybackSource() const {
    for (const RetainedPlaybackSource& entry : retainedSources) {
      if (entry.isRetained()) {
        return true;
      }
    }
    return false;
  }
  RetainedPlaybackSource* retainedSourceForRevision(uint32_t revision) {
    for (RetainedPlaybackSource& entry : retainedSources) {
      if (entry.isRetained() && entry.revision == revision) {
        return &entry;
      }
    }
    return nullptr;
  }
  /// Task 3.12/I9: the slot's **serving** source -- the revision the lane serves right now. A slot may hold a
  /// **retiring** source beside the serving one (3.6a's roles, bounded at two), so "the first retained entry" is not
  /// what a reader without a revision wants: it can date the answer by the *previous* revision. Falls back to the
  /// first retained entry when no role is serving, so a reader still gets a source to ask about and that reader's own
  /// revision check refuses by name when it is not the one.
  const RetainedPlaybackSource* servingRetainedSource() const {
    for (const RetainedPlaybackSource& entry : retainedSources) {
      if (entry.role == RetainedSourceRole::Serving) {
        return &entry;
      }
    }
    for (const RetainedPlaybackSource& entry : retainedSources) {
      if (entry.isRetained()) {
        return &entry;
      }
    }
    return nullptr;
  }
  RetainedPlaybackSource* freeRetainedSourceSlot() {
    for (RetainedPlaybackSource& entry : retainedSources) {
      if (!entry.isRetained()) {
        return &entry;
      }
    }
    return nullptr;
  }
  /// Task 3.6c: the **interval index over pinned bodies** for this slot's retained source (see `RetainedIntervalIndex`
  /// for the representation, its bound and its update strategy). Published atomically with the source revision it
  /// describes; a query for another revision is refused by name rather than answered from a stale index.
  RetainedIntervalIndex retainedIndex;
  /// Task 3.6b (AC-5/F-3): the **lag rule's** per-interval accounting for this slot -- which interval is being
  /// traversed, the served and desired revisions, the consecutive missed traversals and the once-per-episode breach
  /// report. Small and per slot; the decision itself is a pure resolution so no consumer ever waits on it.
  PlaybackLagState lagState;
  uint32_t cachedLoopRevision = 0;
  uint32_t cachedTrackGeneration = 0;
  ActiveNoteLedger ledger;
  PlaybackMergedMidiEvents mergedMidiEvents;
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
  /// Task 3.11 (DEC-108 §13d): the **operation generation** the in-flight build was started under. A completion whose
  /// generation is no longer current is a **late completion**: it publishes nothing and releases only its own candidate.
  /// (The identity the caller stamps into the source is free-form, so the operation's own generation is recorded here
  /// where the build begins rather than inferred from it.)
  uint32_t viewBuildOperationGeneration = 0;
  /// DEC-108 §13b / task 3.10 (the review's step 1): the **bounded work of the admitted request**, recorded where
  /// the build begins. Read it through `activeViewBuildWork()`, which returns null once the build it describes is no
  /// longer active -- a finished build owes nothing, and its record must not be read as outstanding work.
  PlaybackViewAdmittedWork viewBuildWork;
  /// **3.15f / C:** the positioned handoff of the completed **ready** successor, carried until adoption. The admitted
  /// work record describes only an active build, so the wait cannot live there; this pair is the ready's own position.
  uint64_t readyPositionedDeadlineTick = 0;
  uint32_t readyPositionedClockEpoch = 0;
  bool readyPositionedLater = false;
  /// **3.15f / C:** whether the ready's positioned handoff has been reached; every adoption route consults it.
  bool readyPositionAllowsAdoption(PlaybackDeadlineClock now) const;
  /// The runtime's own current deadline-clock view (rolling frame), used where the adoption code has no ClockManager.
  PlaybackDeadlineClock currentDeadlineClockForAdoption() const;
  /// **3.15f / C:** a preview ready positioned at a future handoff is owed across committed-revision changes; the
  /// hot path's staleness reset must not destroy it (bounded by the ready's own handoff).
  bool hasPositionedPreviewReadyOwed() const { return readyPositionedLater && previewSourceActive; }
  /// DEC-108 §13(c) / task 3.10 (increment 24): the slot's **retained pending preparation request** -- the demand a
  /// refused request leaves behind (see `PlaybackViewPendingRequest`). It is per-slot state in this externally
  /// allocated object, so the eight-way DTCM `Track` gains nothing; read it through `pendingViewRequest()`, which
  /// reports nothing once the demand is discharged, and through `activeViewBuildWork()` for the *other* half -- the
  /// work a build already owes. Only committed recipes may enter bounded qualification;
  /// borrowed requests cannot be replayed without their original caller.
  PlaybackViewPendingRequest pendingViewRequest_;
  // One experimental episode, not a second active-slot or source-currency owner.
  enum class RollingPhase : uint8_t { Idle, Requested, Ready, Serving, Blocked };
  struct RollingFrame {
    RollingPhase phase = RollingPhase::Idle;
    uint8_t boundView = RuntimePlaybackViewLifecycle::kNoView;
    uint32_t epoch = 0;
    uint32_t generation = 0;
    uint32_t loopLength = 0;
    int32_t projectionStart = 0;
    uint32_t startPhase = 0;
    uint64_t deadlineTick = 0;
    uint64_t lastTick = 0;
    uint64_t boundStorageGeneration = 0;
    /// DEC-147: the span handoff a lead-only refusal already consumed. The next creation pushes its boundary past it,
    /// so a re-position is always strictly later; dies with the frame (cleared on Ready/Serving and on cancel).
    uint32_t refusedHandoffPhase = 0;
    /// How many spans this demand's re-position has already spent. One attempt per span, and a whole traversal of them
    /// is the terminal answer: neither `boundary - phase` (re-derived from the moving playhead on every retry) nor the
    /// playhead's elapsed ticks can bound that cycle -- at the device's 40 ms refusal cadence a tick-based bound allows
    /// ~1,600 attempts per traversal. Counting spans is the quantity the contract is written in. Valid exactly while
    /// `refusedHandoffValid`.
    uint8_t refusedSpanAttempts = 0;
    bool refusedHandoffValid = false;
  } rolling;
  // Window-only pins; never a root lease or another event buffer. Destruction on
  // active transport is forbidden until the lane drains explicit retirement.
  LoopSnapshot::EventWindowCursor coverageSource;
  PlaybackViewPendingRequest frozenViewRequest;
  enum class CoveragePreparation : uint8_t { Idle, Counting, AwaitAdmission, Building, Retiring };
  CoveragePreparation coveragePreparation = CoveragePreparation::Idle;
  /// Task 3.7a (I2): **the retained-source gather** -- how the deferred lane qualifies a demand for a slot whose
  /// published root cannot answer. One bounded page of the interval is gathered per turn (`Track::gatherRetainedSource`
  /// `WindowForSlot`, task 3.7a I1's primitive), so the qualification is a sequence of bounded turns rather than a
  /// materialization, and the gathered window is then admitted through the **same** production admission a requester
  /// uses (`Track::requestPlaybackViewMaintenance`). The refusal name is what left the demand owed, so a receipt or a
  /// fixture can read *why* the retained revision could not serve rather than inferring it.
  enum class RetainedGatherPhase : uint8_t { Idle, Gathering };
  RetainedGatherPhase retainedGatherPhase = RetainedGatherPhase::Idle;
  CommittedEventRange::WindowTraversalCursor retainedGatherCursor{};
  SessionMidiEventVec retainedGatherEvents;
  /// Task 3.7a (I7): the caller's own scratch for the layer being resolved when the retained revision pins Active Note
  /// note-edit rows (the primitive may not hold it: a requester may gather for the same slot through its own local
  /// scratch). Released with the window, and it is **not** the served content -- `retainedGatherEvents` is.
  SessionMidiEventVec retainedGatherEditRows;
  /// Task 3.15d: the preview lane's **interval composition** -- the committed interval with the session's
  /// identity-qualified delta applied, or the session's own effective projection within the interval. The build copies
  /// it across grains, so it is resident here (like the retained gather's window) rather than a caller temporary; the
  /// preview service writes it only when no build is active, so a live build's source is never overwritten. Released
  /// with the retained gather's scratch (`discardRetainedGather`).
  SessionMidiEventVec previewSourceEvents;
  uint32_t retainedGatherUnits = 0;           ///< slices served for the current gather (bounded per demand)
  uint32_t retainedGatherInspectedChunks = 0; ///< chunks those slices copied out of the store
  /// The named refusal that left this slot's demand owed (`None` while a gather is running or nothing was refused).
  enum class RetainedGatherRefusal : uint8_t {
    None,
    NoSource,
    RevisionMismatch,
    BarsUnsupported,
    NoIndex,
    BodiesUnavailable,
    WindowExceedsCapacity,
    DeadlineExpired,
    EpochInvalidated,
    GeometryInvalidated,
  };
  RetainedGatherRefusal retainedGatherRefusal = RetainedGatherRefusal::None;
  bool retainedGatherActive() const { return retainedGatherPhase != RetainedGatherPhase::Idle; }
  /// Drop the gather and release the window it gathered: the demand it serves is *not* dropped here (a refusal retains
  /// or an admission discharges it), and nothing a reader holds is touched.
  void discardRetainedGather();
  /// Task 3.7a (I3b) / task 3.12b (DEC-108 §14(c)): **the bounded legacy compatibility path** for an interval that
  /// cannot fit one window. The pages are produced from the retained revision's pinned bodies off-clock (**one page per
  /// turn**), and a consumer walks them **exactly once** through `pagedCursor`: the producer position is
  /// `CommittedEventRange::WindowTraversalCursor` inside the ring, the consumer position is
  /// `(sourceRevision, nextSequence, offsetInPage)`, and a page is never written after it is published nor reused
  /// before it is drained. Memory is bounded (`kWindowPageCount` fixed-capacity pages) while content does not have to
  /// be -- which is exactly task 5.5's reading: **a storage bound, not a throughput claim.**
  WindowPagedStream pagedWindow;
  WindowPageCursor pagedCursor;
  bool pagedActive = false;
  /// The revision the pages belong to -- an **identity**, never a currency: a revision the loop has left is served by
  /// nobody (the read side refuses it by name).
  uint32_t pagedRevision = 0;
  PlaybackCoverageInterval pagedInterval{};
  /// **What the paged lane last concluded for this slot.** `Prepared` is a *delivery*, not a refusal: the interval is
  /// held as pages for its revision, which is all the window lane can promise for content that cannot fit a window.
  /// The rest are the names a reader needs to tell "waiting for its consumer" from "voided" without inferring it.
  enum class PagedPlaybackOutcome : uint8_t {
    None,
    Prepared,
    /// The ring holds `kWindowPageCount` undrained pages: the consumer must catch up (`RingFull` is not a failure and
    /// never becomes an allocation or an overwrite).
    ProducerWaiting,
    /// The revision pins no reachable body: there is nothing to page, and the demand stays owed to a source.
    NoBodies,
    /// The promise was voided (geometry, epoch, deadline or identity) or the slot's preparation was abandoned.
    Cancelled,
    /// The consumer walked the interval to its end.
    Drained,
    /// Task 3.12 / 5.5: the interval's own content holds more ledger identities open at once than the emission
    /// service's declared closure capacity carries (`ActiveNoteLedger::kMaxOpenNotes`), so the ring is **not
    /// established** for it: serving it would emit note-ons whose closures the ledger must refuse, which is wrong
    /// content rather than a late or missing one. Named, never silent: the demand stays owed.
    ClosuresExceedDeclaredCapacity,
    /// Task 3.7a / I7: the revision **pins Active Note note-edit rows**, which the page producer does not resolve (its
    /// pages are the pinned bodies in traversal order, and a merged or raw application would be a different answer than
    /// the effective-content owner's per-layer one). The ring is **not established**: the pages would carry content the
    /// edited revision does not have, i.e. wrong content rather than a late or missing one. Named, never silent: the
    /// demand stays owed, and the window lane's own per-layer resolution is what serves it when it fits a window.
    PinnedEditRowsUnresolved,
  };
  PagedPlaybackOutcome pagedOutcome = PagedPlaybackOutcome::None;
  uint32_t pagedPagesProduced = 0;
  bool pagedActiveForInterval(uint32_t revision, const PlaybackCoverageInterval& interval) const {
    return pagedActive && pagedRevision == revision && pagedInterval.startPhase == interval.startPhase &&
           pagedInterval.lengthTicks == interval.lengthTicks &&
           pagedInterval.loopLengthTicks == interval.loopLengthTicks;
  }
  /// Release the ring and its pages: the demand it serves is not dropped here (the same rule as the gather's discard).
  void discardPagedWindow();

  /// Task 3.7a (I3c-a): **the paged emission adapter's state** — the walk's view of a paged delivery. The walk's index
  /// is per-pass (`DEC-108 §13(a)`: "a pass is one traversal of the affected interval in the playback projection")
  /// while the ring's ordinal is monotone across passes, so both are held here: `stagedBaseOrdinal`/`stageCount` are
  /// the staged window's ring range, `consumedOrdinal` is how far the walk has advanced *through the ring* (advanced
  /// once per index the walk consumes, emitted or skipped), and `passBaseOrdinal` is the ring ordinal the emitting
  /// pass started at — set at a **pass boundary** the playback projection signals, never inferred: only the
  /// projection knows the playhead wrapped. The refill is bounded per turn (`kPagedEmissionRefillsPerTurn` stages of
  /// `kPagedEmissionStageCapacity` events, i.e. at most one page), and the accounting is what a gate reads.
  /// One stage is **8 events** and a turn stages at most **16** of them: the per-turn copy bound is one page
  /// (8 x 16 = 128, the ring's own page capacity), and a smaller stage means the ring's page is retired closer to
  /// the emission position that drains it (which is what "retirement by emission position" asks for).
  struct PagedEmissionState {
    /// The adapter's unreachable-path fallback: the walk's `eventAt` must return a reference, and this is the one it
    /// returns if it is ever asked for an ordinal the ring cannot answer (a bug, not a path). It lives **here** -- in
    /// the externally allocated runtime -- so hosting it costs no RAM1 `variables`, which a file-level static did
    /// (measured: +32 B, refused by the budget guard).
    MidiEvent unreachableFallback{};
    /// How far the walk has advanced *through the ring* (the ring's stream ordinal): advanced once per index the walk
    /// consumes -- emitted or skipped as behind the frame -- so it is monotone across passes.
    uint32_t consumedOrdinal = 0;
    /// The ring ordinal the *emitting* pass starts at (walk index 0 is this ordinal). Set at a **pass boundary** the
    /// playback projection signals, never inferred: only the projection knows the playhead wrapped.
    uint32_t passBaseOrdinal = 0;
    uint32_t passCount = 0;
    /// Passes in which a turn found the ring empty. A late page loses the events the frame already passed, so a pass
    /// with a dry is never proof and the gate refuses on it -- probation, not optimism.
    uint32_t passesWithDrys = 0;
    /// Named accounting: dry turns, ring reads (the adapter's own bounded work), boundaries seen.
    uint32_t dryTurns = 0;
    uint32_t reads = 0;
    uint32_t boundaries = 0;
    /// Passes whose boundary arrived with **nothing of the next pass published yet** -- the straddle the paged emission
    /// needs and that this ring's depth does not always provide (measured: see the I3c-a evidence). A pass whose wrap
    /// was not straddled is never proof, so the gate refuses on it.
    uint32_t passesUnstraddled = 0;
    /// The ring's `producedOrdinal()` when the producer last **re-armed** -- i.e. the ordinal that ends this pass's
    /// content. The straddle is judged against it (walk-independent): at a pass boundary the ring must already hold a
    /// page *beyond* this pass's content, or the next pass's head is not there when the playhead arrives.
    uint32_t passContentOrdinal = 0;
    /// True while the delivery may be **selected**: a bound delivery whose last pass boundary was straddled and which
    /// has not withdrawn a read. One cached boolean, because the clock path may only read one: it never judges.
    bool admissible = false;
    bool bound = false;
    /// True once the **emission** is what this delivery serves: the producer then re-arms a pass instead of completing
    /// once, because a delivery nobody emits keeps I3b's one-shot completion (and its `Prepared` discharge).
    bool emitting = false;
    bool passRebasePending = false;
    void bindToDelivery() {
      consumedOrdinal = 0;
      passBaseOrdinal = 0;
      passCount = 0;
      passesWithDrys = 0;
      passesUnstraddled = 0;
      passContentOrdinal = 0;
      admissible = false;
      dryTurns = 0;
      reads = 0;
      boundaries = 0;
      bound = true;
      emitting = false;
    }
    void unbind() {
      bound = false;
      emitting = false;
      admissible = false;
    }
    /// Task 3.7a (I3c-b): **may the clock path take this slot's emission from the ring?** One cached boolean the
    /// deferred lane maintains; the hot path loads it and tests it, and judges nothing itself.
    ///
    /// **Open, with the defect that closed it fixed.** The I3c-b fixture found the second pass's sequence drifting past
    /// index 5,000 because the adapter served *everything published* -- and the producer runs ahead into the next pass,
    /// whose events sit in the same tick space. The adapter now bounds the walk by the **emitting pass's own content
    /// end** (`passContentOrdinal`, which is the producer's last re-arm ordinal while that pass is being emitted), and
    /// the acceptance fixture asserts **two whole passes, element-wise**. The residual case -- a producer completing a
    /// further pass inside one window -- cannot arise here: this path exists for content **over `L`** (2,048 events,
    /// i.e. over sixteen pages), so no pass fits the ring's three-page depth. Selection is enabled.
    static constexpr bool kPagedEmissionSelectionEnabled = true;
    bool admittedForEmission() const { return kPagedEmissionSelectionEnabled && bound && admissible; }
    /// The playback projection wrapped: the **next** pass begins where the walk's position is now (the producer
    /// re-traverses the same interval into the same ordinal space, so the walk's index restarts while the ring's
    /// ordinal continues), and this pass's dry record is **closed, not erased**: a pass that ran dry stays visible to
    /// the gate through `passesWithDrys`.
    void notePassBoundary(uint32_t publishedOrdinal) {
      // The boundary is taken at the walk's position and it is **judged**, not just recorded. A boundary that arrives
      // with nothing published beyond *this pass's content* had no next-pass head to hand over, so the events of the
      // new pass that the very next frame crosses are behind it by construction.
      // Task 3.7a (I5): before the producer's **first** completed traversal there is no content end to judge against, and
      // calling that "no head by definition" made admission unreachable from the clock path: the delivery is refused
      // before it can be read, so the producer never re-arms, so every boundary is judged against `0` (measured: four
      // wraps, four violations, `contentEnd == 0`, nothing emitted). What the ring must already hold at that first
      // boundary is the **current** pass's content, ahead of the walk -- the walk-independent question:
      const uint32_t headRequest = passContentOrdinal != 0 ? passContentOrdinal : consumedOrdinal;
      if (publishedOrdinal <= headRequest) {
        ++passesUnstraddled;
      }
      passBaseOrdinal = consumedOrdinal;
      passRebasePending = consumedOrdinal < passContentOrdinal;
      if (dryTurns != 0) {
        ++passesWithDrys;
      }
      dryTurns = 0;
      ++passCount;
      ++boundaries;
      // **Selection, re-earned per pass** (probation, not optimism): a delivery may be served only after a boundary it
      // straddled cleanly. A withdrawal (a read that found the ring empty while emitting) clears the flag immediately,
      // and the next tick takes the resident fallback -- the same content, a different source, no gap and no outage.
      admissible = bound && passesUnstraddled == 0;
    }
    /// The producer re-armed: this pass's content ends at the ring's current ordinal.
    void notePassContent(uint32_t producedOrdinal) { passContentOrdinal = producedOrdinal; }
    /// Named accounting only, and deliberately **not** part of the admission rule: a read that finds nothing ahead of
    /// the walk is legitimate at a pass's event-free tail, and the *harmful* case -- pages arriving so late that the
    /// walk crosses past their events -- is invisible here (a skip, not a dry). What the adapter can judge soundly is
    /// the straddle at a boundary, which is where the measured loss came from.
  };
  PagedEmissionState pagedEmission;
  enum class LaunchPreparation : uint8_t { None, Requested, Building, Ready, Bound, Fallback, FallbackBound };
  LaunchPreparation launchPreparation = LaunchPreparation::None;
  uint32_t launchBoundTick = 0;
  bool ownsLaunchPreparation() const {
    return launchPreparation == LaunchPreparation::Requested || launchPreparation == LaunchPreparation::Building ||
        launchPreparation == LaunchPreparation::Ready || launchPreparation == LaunchPreparation::Fallback;
  }
  bool prewarmRollingViews();
  const RuntimePlaybackView* boundLaunchView(const PlaybackViewSourceIdentity& source,
      uint32_t loopLength, uint32_t phase, uint32_t tick);
  uint32_t coverageEventCount = 0;
  uint32_t coverageSourceUnits = 0;
  bool ownsCoverageSource() const { return !coverageSource.idle(); }
  bool beginCoverageQualification(const LoopSnapshot& snapshot, bool keepCaptureReady = false);
  bool stepCoverageQualification(size_t quantum);
  bool beginQualifiedCoverageBuild(const CoverageServiceContract& contract, PlaybackDeadlineClock clock);
  void retireCoverageSource();
  bool stepCoverageSourceRetirement(size_t quantum);
  bool coverageRoleAvailable(bool keepCaptureReady = false) const;
  /// DEC-194 (report-only): the first failing term of `coverageRoleAvailable`, in its own order.
  PlaybackCoverageRoleRefusal coverageRoleRefusal(bool keepCaptureReady) const;

  /// DEC-108 item 5 / task 3.13: the **preview source contract** for this slot. `previewSourceActive` says the
  /// in-flight or served window belongs to the preview lane, and `previewSourceRevision` is the session revision it
  /// was frozen at -- the lane's own freeze token, never a second committed currency. The flag is how the lane
  /// recognises **its own** work: a cancellation must never cancel a committed build, and the committed lane must
  /// never publish preview content as its own. `previewHandoffOwed` is the exit handoff: the preview source has
  /// yielded, the committed successor is owed, and the preview window keeps serving until that successor is adopted
  /// (the handoff is a boundary, never a gap).
  uint32_t previewSourceRevision = 0;
  bool previewSourceActive = false;
  bool previewHandoffOwed = false;
  /// DEC-108 §13(e) / task 4.5: the **latched deadline-contract fault** for this slot. It is set by the second
  /// consecutive miss and cleared only at an **eligible boundary** (a replacement published, or a different interval),
  /// never silently -- which is what keeps containment distinguishable from meeting the one-pass-late guarantee.
  bool deadlineContractFaultLatched = false;
  /// DEC-108 §13a / task 3.10 (blocker 3, half 2): **which promise** the traversal counts below belong to (its own
  /// admission tick) and **how many of its traversals have been counted as missed**. A new promise starts a new count;
  /// the same promise observed across many turns and many traversals is counted **exactly once per traversal** -- which
  /// is what keeps a per-turn check from turning one missed traversal into a burst of them. This is the *deduplication*:
  /// the **episode** (the allowance and the breach, `lagState`) belongs to the interval, and increment 28 keeps the two
  /// apart so a repeated request for an interval cannot re-open its allowance.
  uint64_t countedMissPromiseTick = 0;
  uint16_t countedMissedTraversals = 0;
  /// Last invalidated promise stamp, diagnostic only; the bool handles legal tick zero.
  uint64_t invalidatedPromiseTick = 0;
  /// Completed candidate retained until qualified boundary recovery. Not an extra
  /// worker grain and never judged using a later timestamp as its completion time.
  bool completedPromisePendingJudgement = false;
  bool admittedPromiseInvalidated = false;
  /// S4f.19 continuity gate, bounded in DEC-070's shape: grains spent in the `RecordPublished`
  /// retirement wait with an unchanged frozen-window build phase, and that phase. A phase change
  /// is progress and resets the count, so a build its writers are still feeding is never cut; a
  /// build no writer can advance -- the device held one at `build_active=1` for 30 s while the
  /// track sat in `TRACK_OVERDUBBING` -- is bounded instead of parking the job, its raw range and
  /// the capture-stop owner, and refusing every later stop on the track. This lives here, not on
  /// `Track`, because `Track` is the pinned eight-way DTCM array (`sizeof(Track) == 384`).
  uint16_t frozenWindowStallGrains = 0;
  uint8_t frozenWindowStallGrain = 0;
  /// DEC-136 completion (S7.5b.5 release edge): grains the `RecordPublished` retirement hold
  /// has spent on an **unchanged** owed root-return successor, and that successor's identity
  /// key -- the owed ready view's storage generation, or zero while the private build is still
  /// in flight. A change (the build advancing, the successor being adopted or replaced)
  /// restarts the count; the bound is the frozen lane's own stall budget, so a successor
  /// nothing can advance still releases the job instead of parking it forever. The same
  /// reason this lives here and not on `Track`: externally allocated per-slot runtime state,
  /// no RAM1 cost (`sizeof(Track) == 384` is untouched).
  uint32_t rootReturnHoldKey = 0;
  uint16_t rootReturnHoldGrains = 0;
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
  // Task 6.4c (DEC-177): the prepared prefix's **committed-history identity**, separated from its window identity
  // (`canonicalPrefixSource`, which carries `playbackRevision`/`snapshotIdentity`). A live successor may legitimately
  // move the window identity; that must not license a replay of the committed history. Recorded when a prefix build
  // completes or is resumed, and re-checked before any resume: the inventory must have grown by exactly one pass and
  // the pass at that position must still be the certified one. A revision bump alone is never a license.
  uint32_t canonicalPrefixCertifiedPasses = 0;
  uint32_t canonicalPrefixCertifiedLastPass = 0;
  bool canonicalPrefixCertified = false;
  /// Report-only: how many prefix builds resumed from the retained committed history instead of replaying it.
  uint32_t canonicalPrefixAppendResumes = 0;
  /// True while the prepared prefix is the explicit retained-history-prefix state (contract above).
  bool canonicalPrefixRetainedHistory = false;
  uint64_t recordingWindowGeneration = 0;
  bool recordingWindowGenerationValid = false;
  // DEC-067 acceptance edge: armed when a deferred stop retires, emitted once the
  // finalization resources (canonical copy target + transfer chunks) are admitted again.
  bool canonicalReleaseMarkerArmed = false;
  // S4f.3 evidence: the identity of the owner whose retirement armed the marker. Held here because
  // `clearCaptureStopOwner` zeroes `captureStopOwnerGeneration`, so by the maintenance turn the identity is
  // otherwise gone. Externally allocated, so this costs the DTCM budget nothing -- only the struct pin.
  uint32_t canonicalReleaseMarkerGeneration = 0;
  // Diagnostic-only interval identity for slot reuse attribution. Unlike the generation, this is the
  // capture interval stamped by `reentry,admit` and lets stopped logs distinguish a seeded predecessor
  // from the mixed leg's successor on the same firmware slot.
  uint64_t canonicalReleaseMarkerInterval = 0;
  // Diagnostics only: why the last first-record bar-1 preparation was denied.
  const char* firstRecordingBarDenial_ = "none";
  enum class CanonicalPrefixPhase : uint8_t { Idle, Preparing, Ready, Denied };
  /// DEC-170 diagnostic: the first false term that made the canonical base build refuse, packed as a 4-bit field beside
  /// the 2-bit prefix phase so the per-slot runtime keeps its I5 pin (no byte is added). Report-only — no admission or
  /// policy reads it; it is reset when the build restarts or completes, so the device `prefix_denied_first` row names
  /// the *original* transition, not the sticky `Denied` latch it left behind.
  enum class CanonicalPrefixDenial : uint8_t {
    None, BridgeActive, CaptureAttached, RevisionMismatch, GenerationMismatch,
    InputGenerationMismatch, StepDenied, InputsBusy, ApplyUnavailable,
  };
  CanonicalPrefixPhase canonicalPrefixPhase : 2 = CanonicalPrefixPhase::Idle;
  CanonicalPrefixDenial prefixFirstDenialReason : 4 = CanonicalPrefixDenial::None;
  bool prefixDeniedFirstReported : 1 = false;
  /// Task 6.4c (DEC-177) — the retained-history-prefix contract, in one predicate (the single authority both the
  /// retention and the discard read):
  ///   * the build is capture-free (no frozen overdub / capture attachment -- the Record lane disqualifies);
  ///   * its normalized output completed (`stateAllowsBinding()`), so the resident arrays are the history itself;
  ///   * the history it represents is certified.
  /// A completed stop may retain **only** this. Nothing that depends on the job's live interval, successor window,
  /// bridge or companion ownership survives -- the caller discards those either way.
  bool canRetainCanonicalHistoryPrefix() const;
  /// Park that state across a retirement: inputs stay resident and certified, the phase returns to `Idle` so the next
  /// stop services its prefix, and adoption happens in that stop's own begin (never by leaving a completed build in
  /// place -- measured wedge, DEC-177 addendum). Returns false when the contract does not hold.
  bool retainCanonicalHistoryPrefix();
  /// Any non-append history change (undo, replacement, a disabled pass) invalidates the retained prefix here.
  void discardRetainedCanonicalHistoryPrefix();
  bool canonicalPrefixHistoryIsRetained() const { return canonicalPrefixRetainedHistory; }
  /// Task 6.4c: certify the committed history the prepared prefix now represents, and test whether the current
  /// inventory is exactly that history plus one appended pass.
  void certifyCanonicalPrefixHistory(const LoopPasses& passes);
  bool canonicalPrefixHistoryIsCertifiedAppend(const LoopPasses& passes) const;
  bool canonicalPrefixNeedsService(const LoopPasses& passes, const PlaybackViewSourceIdentity& source,
                                   uint32_t loopLength, const uint64_t& storageGeneration) const;
  RuntimePlaybackOrderBuildResult stepCanonicalPrefix(const LoopPasses& passes,
      const PlaybackViewSourceIdentity& source, const RuntimeCanonicalPassInputs::CurrentSource& live,
      size_t quantum);
  bool retainCommittedCanonicalTail(Loop& loop, const PlaybackViewSourceIdentity& source);
  bool resumeCanonicalAfterAppend(const LoopPasses& passes, uint32_t priorRevision,
      const PlaybackViewSourceIdentity& source, const RuntimeCanonicalPassInputs::CurrentSource& live,
      bool committedFrozenTail = false, bool certifiedHistoryAppend = false);
  // One existing child per call; shared by live overdub prework and stop.
  RuntimePlaybackOrderBuildResult stepCanonicalBase(const LoopPasses& passes,
      const PlaybackViewSourceIdentity& source, const RuntimeCanonicalPassInputs::CurrentSource& live,
      const EmissionCursor& anchor, size_t quantum);
  uint64_t requiredStopRankedGrains(const PlaybackViewSourceIdentity& source,
      size_t maximumPassCount, size_t historyCount, size_t pendingChangeCount, size_t quantum) const;
  // Uses the same pass merge and view fill/order workers as ordinary playback.
  // Completion publishes an immutable ready base, never changes audible current.
  enum class CanonicalWindowPhase : uint8_t { Idle, Merge, Fill, Order, Publish, Ready, Denied };
  /// DEC-171 diagnostic: the first terminal term that denied the canonical *window* build. The prefix
  /// reporter (`prefixFirstDenialReason`) only names the prefix; on device the base is healthy through the
  /// prefix and only the window is denied, so without this the denial is silent. Report-only — no policy.
  enum class CanonicalWindowDenial : uint8_t {
    None, MergeDenied, FillBeginDenied, FillOrderDenied,
  };
  // Packed 3+3+1 into the single byte `canonicalWindowPhase` previously occupied, so the per-slot runtime
  // keeps its I5 pin (no byte is added).
  CanonicalWindowPhase canonicalWindowPhase : 3 = CanonicalWindowPhase::Idle;
  CanonicalWindowDenial windowFirstDenialReason : 3 = CanonicalWindowDenial::None;
  bool windowDeniedFirstReported : 1 = false;
  uint8_t canonicalWindowIndex = RuntimePlaybackViewLifecycle::kNoView;
  bool canonicalWindowIsBridgeBase = false;
  uint64_t canonicalWindowPassGeneration = 0;
  PlaybackCoverageInterval canonicalWindowCoverage;
  RuntimePlaybackOrderBuildResult stepCanonicalWindow(
      const PlaybackCoverageInterval& coverage, const EmissionCursor& anchor, size_t quantum,
      bool bridgeBase = false);
  const RuntimePlaybackView* preparedCanonicalWindow(const PlaybackViewSourceIdentity& source) const;
  void discardCanonicalWindow();

  // --- Deferred canonical convergence: one obligation per slot ------------------------------
  // A capture stop that retires raw because its canonical derivation was unavailable or became
  // superseded leaves the slot owing the derived canonical/companion representation through the
  // committed generation that stop produced. The obligation is a slot-level state machine
  // (`Idle -> Owed -> Building -> Ready -> Idle`) serviced by a Track owner turn outside the
  // capture-stop FIFO (`Track::serviceDeferredCanonicalConvergence`), and it is never stacked:
  // any newer committed generation coalesces the single obligation forward. It drives the same
  // canonical builders the inline stop uses, so exactly one owner may hold them at a time.
  enum class DeferredConvergencePhase : uint8_t { Idle, Owed, Building, Ready };
  /// Report-only: why the most recent deferred candidate was dropped. No policy reads it.
  enum class DeferredConvergenceDenial : uint8_t { None, BuildDenied, SourceChanged };
  // Packed into one byte so the obligation itself costs only its identity fields.
  DeferredConvergencePhase deferredConvergencePhase : 2 = DeferredConvergencePhase::Idle;
  DeferredConvergenceDenial deferredConvergenceDenial : 2 = DeferredConvergenceDenial::None;
  /// The committed generation (`Loop::committedPassRevision()`) the derived representation is owed
  /// through. Advanced by coalescing, never by a second obligation.
  uint32_t deferredConvergenceTarget = 0;
  /// Loop coverage (ticks) the obligation was recorded for.
  uint32_t deferredConvergenceLength = 0;
  /// The authoritative source identity bound at build admission (`Owed -> Building`). A change to it
  /// (or to the committed generation) while `Building` discards the candidate and re-resolves.
  PlaybackViewSourceIdentity deferredConvergenceSource{};
  bool deferredConvergenceActive() const {
    return deferredConvergencePhase != DeferredConvergencePhase::Idle;
  }
  /// Record (or coalesce into) the single obligation. `targetRevision` is the committed generation the
  /// raw retire produced. Never stacks: a newer target advances the existing obligation.
  void markDeferredConvergenceOwed(uint32_t targetRevision, uint32_t loopLengthTicks);
  /// Universal coalescing: any newer committed generation re-targets the obligation and drops an
  /// in-flight (unpublished) candidate. Returns true when the obligation changed state.
  bool coalesceDeferredConvergence(uint32_t newestCommittedRevision);
  /// An adopted inline canonical result whose committed revision covers the target satisfies the
  /// obligation through that generation (no duplicate later publication). Returns true when cleared.
  bool satisfyDeferredConvergence(uint32_t committedRevision);
  /// Drop the obligation and its candidate entirely (slot clear/load/replacement/reset).
  void cancelDeferredConvergence();
  /// Drop the shared canonical-base builder candidate (prefix inputs + window) and any bound
  /// deferred source identity. Only the current owner of the builders may call this: the inline stop
  /// when it abandons a superseded/denied base, or deferred convergence when it coalesces.
  void discardCanonicalBuildCandidate();
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
  bool reset(bool preserveLedger,
      const RuntimePlaybackViewBuild::PreparationTurn* primary = nullptr,
      const RuntimePlaybackViewBuild::PreparationTurn* bridge = nullptr);
  bool ownsLiveCaptureBuildBorrow() const;
  void cancelLiveCaptureBuildBorrows();
  void cancelStaleLiveCaptureBuildBorrows();

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
  /// True when every capture-staging role this slot needs is admitted. False after
  /// `releaseSurplusCaptureStagings`, so the capture edge re-admits before ingress.
  bool captureStagingsAdmitted() const {
    return viewSet != nullptr && viewSet->captureStagingsPrewarmed();
  }
  /// Return the surplus frozen roles to the pool once this slot owes no interval
  /// any staging. Bounded, non-allocating; refuses while a role is still owed.
  bool releaseSurplusCaptureStagings() {
    return viewSet != nullptr && viewSet->releaseSurplusCaptureStagings();
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
  ///
  /// **3.15e/2b-ii-b — the borrow's collision rule.** `overlayRetention` names whether a retained overdub-overlay
  /// candidate owns the borrowed companion storage. The decision is consulted **before** any companion build:
  /// no candidate → `Allowed`; a retained candidate whose identity still matches → `RefusedRetainedCandidate`
  /// (refused by name, `current` untouched); a stale candidate → `RetireCandidateFirst` (retire it, then continue on
  /// this path). The out-parameter reports the decision so a caller never has to infer a refusal from a bare `false`.
  bool publishStoppedCanonicalOracleView(const LoopPasses& passes,
      const PlaybackViewSourceIdentity& source, const RuntimeCanonicalPassInputs::CurrentSource& live,
      const MidiEvent* oracle, size_t oracleCount, const PlaybackCoverageInterval& coverage,
      const OverdubOverlayLane::CandidateRetention& overlayRetention = {},
      OverdubOverlayLane::OracleRebuildDecision* overlayDecisionOut = nullptr);
  /// **3.15e/2b-ii-b — the overlay lane's owner build.** Continues the borrowed provenance-bearing base fill
  /// (`frozenCompanionPasses`) into the one provenance table, then attaches the append-stable live overlay and keeps it
  /// current as rows arrive. The borrowed instance is released as soon as the base half is copied (the append half does
  /// not reference it), so a pass commit or the stopped oracle can never wait on this lane for the storage.
  ///
  /// No state is added: the builder's own `nextGrain()`/`overlayContinuationActive()` and the candidate view's
  /// metadata describe the phase, and `isOverlayCandidate` names the retained candidate. Nothing consumes the window
  /// and nothing is emitted (slice 3 owns adoption).
  OverdubOverlayLane::Turn stepOverdubOverlayBuild(const OverdubOverlayLane::BuildRequest& request, Loop& loop,
                                                   size_t fillQuantum, size_t orderQuantum);
  /// The retained candidate for this slot, or null. Only the build role can hold it; the identification is the
  /// continuation's own metadata (`OverdubOverlayLane::isOverlayCandidate`), never a stored marker.
  const RuntimePlaybackView* overdubOverlayCandidate() const;
  /// Retire the retained candidate **and** detach its builder, clearing the view so no consumer can read a
  /// complete-looking window whose identity moved. Returns false when there was nothing to retire.
  bool retireOverdubOverlayCandidate();
  /// True when no other owner holds this slot's builder or its prerequisite workers, so the overlay lane may begin a
  /// build without contending. Role arbitration (2b-iii) will replace this by-construction exclusion with a named
  /// decision; until then the lane **defers** rather than competing for one builder.
  bool overdubOverlayBuildExclusive() const;
  /// True while **this lane's** borrow of the one companion instance is in flight: the instance is busy and no other
  /// owner that uses it (canonical window, canonical tail, capture bridge) is in flight. A boundary turn uses this to
  /// release the borrow, so a stop or a wrap commit never waits on background preparation.
  bool overdubOverlayBorrowHeld() const;
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
                          const LedgerBoundarySummary& ledgerBoundary,
                          uint32_t positionedLead = 0);
  bool beginCoverageBuild(const CoverageServiceContract& contract, uint32_t currentPhase,
                          uint32_t handoffPhase, LoopSnapshot::EventWindowCursor& source,
                          const PlaybackViewSourceIdentity& sourceIdentity,
                          const PlaybackCoverageInterval& coverage, const EmissionCursor& cursorAnchor,
                          const LedgerBoundarySummary& ledgerBoundary, uint32_t positionedLead = 0);
  // Establish an empty raw bar-1 view before first-record input. This uses the
  // same fill/order/lifecycle buffers as ordinary playback.
  bool prepareFirstRecordingBar(uint32_t ticksPerBar,
      const RuntimePlaybackViewBuild::PreparationTurn* primary = nullptr,
      const RuntimePlaybackViewBuild::PreparationTurn* bridge = nullptr);
  /// Retire the record-session return window so the next take can establish its
  /// own bar 1. A take that ends without the record-stop handoff (transport Stop
  /// during RECORDING, empty/skipped commit) leaves a raw window held, and a take
  /// that did hand off leaves the bound window held until a root replaces it.
  /// The preparation boundary for the next take owns both releases. A window
  /// whose stop job is still retiring is never released.
  bool releaseFirstRecordingBarReturnWindow(
      const RuntimePlaybackViewBuild::PreparationTurn* primary = nullptr,
      const RuntimePlaybackViewBuild::PreparationTurn* bridge = nullptr);
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
  // the audible handoff. Production supplies `recordedLoop` so a view from an
  // older Capture revision is refused; standalone native fixtures may omit it.
  // No source copy or order rebuild occurs here.
  bool bindFirstRecordingBar(uint32_t loopLengthTicks, const Loop* recordedLoop = nullptr);
  /// S4f.19: bind the retained first-record bar to a **deferred** Record job's
  /// frozen source and final geometry. Job-qualified: refuses a range that is not
  /// ready or whose generation is not `expectedGeneration`, and refuses a final
  /// length shorter than the prepared bar, because raw rows past the final length
  /// would need projection (the onward-window slice) and binding them raw would
  /// emit out-of-loop rows. Leaves the retained window untouched on refusal, and
  /// is a no-op after a successful bind (the role is handed off).
  bool bindDeferredRecordReturnWindow(const LoopEventStore::CaptureRange& range,
                                      uint64_t expectedGeneration, uint32_t loopLengthTicks,
                                      const uint64_t* openAtStopLanes = nullptr);
  /// True when a **pending** deferred job owns this slot's bindable return window, so an
  /// unpublished interval may be served from it. False once the job retires (committed
  /// content then owns the slot) and false for an inline take, which uses the
  /// session-exit publication-gap predicate instead.
  bool hasBoundRecordReturnWindowForPendingJob() const;
  /// True when a pending job owns the interval **and** a view it can serve exists: the bound
  /// bar-1 return window or a range-derived onward window. Ownership alone is not audibility.
  bool hasServableDeferredRecordView() const;
  /// Refresh the retained first-record bar from the live capture store using
  /// the same bounded fill/order workers as ordinary playback preparation.
  bool serviceFirstRecordingBar(const Loop& loop,
                                const PlaybackViewSourceIdentity& sourceIdentity,
                                uint32_t ticksPerBar, size_t entryQuantum = 32);
  /// S4f.19 onward windows: build the playback window that contains `phase` from the
  /// **owned frozen Record source**, through the existing fill/order owners and the
  /// `RecordCoverage` projection. One bounded grain per call, reusing the `build` role
  /// and publishing into `ready`; the caller promotes it through the normal handoff.
  /// Refuses a stale/unready range or a phase outside the loop, leaving both roles as
  /// they were. Returns true while the build is still in flight.
  bool serviceFrozenRecordWindow(const LoopEventStore::CaptureRange& range,
                                 uint64_t expectedGeneration,
                                 const PlaybackViewSourceIdentity& sourceIdentity,
                                 uint32_t loopLengthTicks, uint32_t phase,
                                 uint32_t windowTicks, size_t entryQuantum = 32);
  /// True once the last `serviceFrozenRecordWindow` build completed into `ready`.
  bool frozenRecordWindowReady() const;
  /// S4f.19: return the onward window that covers `phase`, promoting a completed `ready`
  /// build through the audible handoff. Reuses an already-serving onward window across
  /// turns (the bar-1 return window is excluded) so a steady playhead does not rebuild
  /// the same window every turn. Returns nullptr when nothing covers the phase yet.
  const RuntimePlaybackView* adoptFrozenRecordWindow(
      const PlaybackViewSourceIdentity& sourceIdentity, uint32_t loopLengthTicks, uint32_t phase);
  /// True while a view build is in flight, so the raw range is still being read. Retirement
  /// must wait for it to drain rather than free chunks under the reader.
  bool frozenWindowBuildActive() const;
  /// The in-flight build's next grain. A change is the cheap "this build is still advancing"
  /// signal the S4f.19 retirement wait resets its bound on, so a build a writer is still feeding
  /// is never cut while one no writer can advance is bounded instead of parking the job.
  RuntimePlaybackViewBuildGrain frozenWindowBuildGrain() const;
  /// Drop the range-derived onward window (and any in-flight frozen build) once a matching
  /// committed replacement exists. Idempotent; the bar-1 return window is left alone because
  /// it is served until a replacement view is handed off.
  bool releaseFrozenRecordWindow();
  /// Only an admitted coverage build belongs to this lane. Generic shared-builder
  /// owners are excluded; snapshot qualification and retirement are classified
  /// separately by the Track holder scan.
  bool coverageBuildNeedsService() const {
    return viewBuild.coverageOwned() && viewBuild.active() &&
        coveragePreparation != CoveragePreparation::Counting &&
        coveragePreparation != CoveragePreparation::AwaitAdmission;
  }
  RuntimePlaybackViewBuildGrain nextCoverageBuildGrain() const;
  /// DEC-108 §13b / task 3.10 (the review's step 1): the work this slot's **admitted** request was bounded to, valid
  /// only while the build it describes is active. Null once the build has completed or been discarded, so a reader
  /// (a per-track budget, a fixture) can never count a finished build as outstanding work.
  const PlaybackViewAdmittedWork* activeViewBuildWork() const {
    return coverageBuildNeedsService() ? &viewBuildWork : nullptr;
  }
  /// DEC-108 §13(c) / task 3.10 (increment 24): the slot's **retained pending preparation request** -- the demand a
  /// refused request left behind -- or null when the slot owes no demand. Generation qualification is the *reader's*
  /// (see `Track::pendingViewRequest`): this raw view is what the runtime itself keeps.
  const PlaybackViewPendingRequest* pendingViewRequest() const {
    return pendingViewRequest_.active ? &pendingViewRequest_ : nullptr;
  }
  /// Retain (or **coalesce into**) the slot's pending demand after an attempt was refused by name. One record per
  /// slot: the **newest** identity, interval, phase and cost win, while the **oldest outstanding deadline** of the
  /// same operation is kept (DEC-108 §13(a) -- progress toward the oldest deadline is preserved). A deadline from a
  /// different clock epoch, a different loop length, or an earlier operation generation is **not** carried over: a
  /// reset voids the promise's position and a returning slot must not inherit an earlier operation's deadline
  /// (DEC-108 §13(d)). Refusing to retain is never the caller's decision here: only the refusal classes the admission
  /// path chooses reach this call.
  void retainViewPendingRequest(const PlaybackViewSourceIdentity& source,
                                const PlaybackCoverageInterval& coverage, uint32_t requestedPhase,
                                uint32_t operationGeneration, const PlaybackViewAdmittedWork& work,
                                uint64_t deadlineTick, uint32_t clockEpoch, uint32_t loopLengthTicks,
                                PlaybackViewPendingReason reason);
  /// Discharge the slot's demand: the work it asked for is now owed by a live build, or the demand is gone. The
  /// record is cleared whole, so a later reader cannot see a discharged demand's deadline.
  void clearViewPendingRequest() { pendingViewRequest_ = PlaybackViewPendingRequest{}; }
  RuntimePlaybackViewBuildGrain stepCoverageBuildFill(size_t entryQuantum);
  RuntimePlaybackViewBuildGrain stepCoverageBuildOrder(size_t entryQuantum);
  void discardCoverageBuild();

  bool beginFrozenCaptureOrder(uint32_t loopLengthTicks, uint32_t frozenGeneration);
  /// DEC-165: the stop's companion tail can only begin from a **ready** canonical base
  /// (`beginFrozenCanonicalTail` requires it), so a job whose base was abandoned must not wait for a
  /// tail that can never start. Read-only, allocation-free.
  bool canonicalBaseReady() const {
    return canonicalPrefixPhase == CanonicalPrefixPhase::Ready;
  }
  RuntimePlaybackOrderBuildResult stepFrozenCaptureOrder(size_t entryQuantum);
  bool frozenCaptureOrderComplete() const { return frozenCaptureOrder.complete(); }
  /// Why `beginFrozenCaptureCanonicalOrder` refused. The `CanonicalOrder` stop phase had no
  /// reporter at all: five distinct predicates all returned `false`, the only stall notice above
  /// them checks `frozenGeneration` alone, and the notice itself is once-per-Track -- so a job
  /// that could never advance looked exactly like a job nobody serviced. Naming the predicate
  /// costs nothing at rest (a return type, not a field) and is what the device row now carries.
  FrozenCaptureCanonicalOrderBegin beginFrozenCaptureCanonicalOrder(uint32_t frozenGeneration);
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
  /// Owner is the live Track; all source Loops remain in its fixed LoopPool.
  /// No allocation or source query during unconditional teardown cancellation.
  void cancelLiveCaptureBuilds();
  /// Cancel semantic/raw-stale private work even if holder selection excludes it.
  void cancelStaleLiveCaptureBuilds();

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

  /// S4.4: the roles this slot currently holds. Read-only; the declared-cap check needs the track's
  /// holder census and nothing new is stored for it.
  WorkingSetRole workingSetRolesAt(uint8_t slotIndex) const {
    if (slotIndex >= Config::MAX_LOOPS_PER_TRACK) return WorkingSetRole::None;
    const LoopPlaybackRuntimePtr& rt = runtimeBySlot_[slotIndex];
    return rt ? rt->workingSetRoles() : WorkingSetRole::None;
  }
 private:
  std::array<LoopPlaybackRuntimePtr, Config::MAX_LOOPS_PER_TRACK> runtimeBySlot_{};
};
