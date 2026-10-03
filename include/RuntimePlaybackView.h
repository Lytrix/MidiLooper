//  Copyright (c) 2026 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "ActiveNoteLedger.h"
#include "MidiEvent.h"

/// Fixed storage owned by one slot's playback runtime.  The capacities are a
/// pre-transport admission limit, never a reason to grow a vector from the
/// MIDI tick.  Active use is wired in task 2.4 after the resumable builder and
/// coverage contracts are present.
namespace RuntimePlaybackViewConfig {
constexpr size_t kViewCount = 3;
constexpr size_t kBridgeCount = 2;
constexpr size_t kEventsPerView = 2048;
constexpr size_t kRecordsPerBridge = 2048;
constexpr size_t kCaptureStagingEvents = 2048;
constexpr size_t kMaxCaptureNoteReservations = ActiveNoteLedger::kMaxOpenNotes;

/// DEC-108 §14 / task 3.12: the **admission limit** for a window. `L` is the decided accounting unit's ceiling, and it
/// is the window's **storage capacity itself** -- `kEventsPerView`, with **no partition multiplier** -- which is the
/// sliding window-span invariant's first half: what is admitted is exactly what a window can hold.
constexpr size_t kWindowAdmissionLimit = kEventsPerView;

/// The **reserved closure capacity** counted *inside* `L`: the note-offs and generated closures a window must be able to
/// emit for the note-ons it holds, so "every accepted event is served" cannot be defeated by a window that is full of
/// note-ons. **Provisional declared value** (DEC-108 §14 / item 12(c) requires the reserve to live inside the limit but
/// leaves its size to measurement): 128 covers the per-bar worst case of the two-bar comparison span; task 6.3's sweeps
/// are what may move it, and nothing here claims it is measured.
constexpr size_t kWindowClosureReserve = 128;

/// The **admission accounting** (DEC-108 §14, task 3.12): the capture interval's **effective** events, aggregated across
/// the slot's overdub passes, plus **outstanding reservations** (accepted note-ons whose closures are still owed) and the
/// reserved closure capacity -- all inside `L`. It is deliberately **preallocated and bounded**: adding an event is
/// arithmetic, nothing allocates, no loop content is scanned and the effective-content resolver is never invoked, so it
/// is usable at acceptance (the MIDI edge). That is also why it can only ever *refuse by name*: capacity pressure is
/// reported before acceptance, and **interaction acceptance (DEC-078) is never a subject of this limit** -- a stop,
/// switch or undo request is not content and has no place in these counters.
struct WindowAdmissionAccounting {
  /// Effective events already accepted into this window's interval (post-edit count for restored content, live count for
  /// content being accepted).
  size_t acceptedEffectiveEvents = 0;
  /// Accepted note-ons whose note-off/closure is still owed: they occupy the window now and are released as they close.
  size_t outstandingReservations = 0;
  /// The reserve `kWindowClosureReserve` is *inside* `L`; kept as a field so a caller can see it in one number.
  size_t closureReserve = kWindowClosureReserve;

  /// What the window is committed to right now: accepted content plus everything still owed for it.
  size_t committed() const { return acceptedEffectiveEvents + outstandingReservations + closureReserve; }

  /// The one predicate: does the window still fit `additionalEffectiveEvents` **effective** events? `false` is capacity
  /// pressure, reported before acceptance -- never a truncation and never a deadline fault (DEC-108 §14: over-capacity
  /// is a supported compatibility case).
  bool fits(size_t additionalEffectiveEvents) const {
    return committed() + additionalEffectiveEvents <= kWindowAdmissionLimit;
  }

  /// Admit `effectiveEvents` effective events, or refuse **by name**. Admission is the only mutation, and it happens
  /// only when the predicate already said yes, so nothing is ever half-accepted.
  enum class Outcome : uint8_t { Admitted, OverCapacity };
  Outcome admit(size_t effectiveEvents) {
    if (!fits(effectiveEvents)) {
      return Outcome::OverCapacity;
    }
    acceptedEffectiveEvents += effectiveEvents;
    return Outcome::Admitted;
  }
  /// A **new note-on** occupies **two** units inside `L` -- its own row and the closing row it will owe -- which is
  /// what "the reserved capacity for note-off and generated closures counted inside `L`" means at the edge; every
  /// other accepted event occupies one.
  static constexpr size_t kUnitsForNoteOn = 2;
  static constexpr size_t kUnitsForPlainEvent = 1;

  /// The MIDI edge's **one** decision, as a pure function of the accounting: may one *new* event of this kind still
  /// be accepted? A `false` is capacity pressure reported **before** acceptance -- never a truncation and never a
  /// deadline fault. It is a function of the value rather than of stored state, so the refusal can be named and
  /// tested without reaching a log.
  bool admitsNewEvent(bool isNoteOn) const {
    return fits(isNoteOn ? kUnitsForNoteOn : kUnitsForPlainEvent);
  }

  /// Close an admitted note-on. This moves one unit **already inside `L`** from owed to accepted, so `committed()`
  /// is unchanged and the closure is always admissible: that is the "an accepted note's closure remains guaranteed
  /// even when further input is refused" clause as arithmetic, and it is why a closure is the one thing the edge
  /// never refuses. There is deliberately **no** mutator for a new event: the edge derives this value from the
  /// capture it describes, so the accounting can never drift away from that capture.
  void closeReservation() {
    if (outstandingReservations > 0) {
      --outstandingReservations;
    }
    ++acceptedEffectiveEvents;
  }

  /// A conservative bound for an interval whose exact count is not available at acceptance: the per-bar ceiling times
  /// the bars the interval intersects, **clamped by `L`** -- the task's "per-bar counts only as a conservative bound
  /// over every intersected bar". It can only over-estimate, so a caller that refuses on it refuses early; a caller that
  /// wants the exact count uses the effective count above.
  static size_t conservativeBoundForBars(size_t barsIntersected, size_t eventsPerBarCeiling) {
    const size_t bound = barsIntersected * eventsPerBarCeiling;
    return bound > kWindowAdmissionLimit ? kWindowAdmissionLimit : bound;
  }

  /// The sliding window-span invariant's arithmetic, as one check a fixture can drive: for the configured span `W`
  /// (a bar count), every `W`-wide interval -- a wrap-crossing one included, which is the same width by definition --
  /// must fit `L` when its content is at the per-bar ceiling the window is provisioned for. "Fits" is the conservative
  /// bound's own question, so this returns the bound and the caller compares; it is not a second predicate.
  static size_t boundForSpan(size_t spanBars, size_t eventsPerBarCeiling) {
    return conservativeBoundForBars(spanBars, eventsPerBarCeiling);
  }
};

// The sliding window-span invariant's first half, pinned where it can never drift: the admission limit **is** the
// window's storage capacity, so there is no partition multiplier to get out of step with it.
static_assert(kWindowAdmissionLimit == kEventsPerView,
              "DEC-108 §14: window storage capacity is exactly the admission limit L (no partition multiplier)");
static_assert(kWindowClosureReserve < kWindowAdmissionLimit,
              "the closure reserve is *inside* L, not an addition to it");
static_assert(kEventsPerView <= UINT16_MAX, "fixed playback order uses uint16_t indexes");
}  // namespace RuntimePlaybackViewConfig

/// Identifies an event *instance* that was emitted from a projection cycle.
/// A source event alone is insufficient because the same source event may fire
/// again after a loop wrap.
struct EmissionCursor {
  uint32_t projectionCycle = 0;
  uint32_t phase = 0;
  uint16_t samePhaseOrdinal = 0;
  uint32_t eventProvenance = 0;
  uint32_t effectiveSourceGeneration = 0;

  bool operator==(const EmissionCursor& other) const {
    return projectionCycle == other.projectionCycle && phase == other.phase &&
           samePhaseOrdinal == other.samePhaseOrdinal &&
           eventProvenance == other.eventProvenance &&
           effectiveSourceGeneration == other.effectiveSourceGeneration;
  }
};

/// DEC-108 item 5 / task 3.13: which **source contract** a view's content came from. Live NOTE_EDIT preview keeps a
/// preview source of its own, so a consumer that requires committed currency can refuse it on this field alone --
/// "preview never masquerades as committed currency" stated at the identity level rather than left to a comment.
enum class PlaybackWindowOrigin : uint8_t {
  /// Committed content: the loop's committed passes at the loop's own revision.
  Committed = 0,
  /// Live preview content from the edit session, carrying the session's **preview revision** as its currency.
  Preview = 1,
};

struct PlaybackViewSourceIdentity {
  uint32_t playbackRevision = 0;
  uint32_t sourceGeneration = 0;
  const void* snapshotIdentity = nullptr;
  /// Task 3.13: the source contract this content came from. It is part of equality, so a preview source can never
  /// compare equal to a committed one at the same revision and generation.
  PlaybackWindowOrigin origin = PlaybackWindowOrigin::Committed;

  bool operator==(const PlaybackViewSourceIdentity& other) const {
    return playbackRevision == other.playbackRevision &&
           sourceGeneration == other.sourceGeneration &&
           snapshotIdentity == other.snapshotIdentity && origin == other.origin;
  }
};

/// Half-open modular interval [startPhase, startPhase + lengthTicks).
struct PlaybackCoverageInterval {
  uint32_t startPhase = 0;
  uint32_t lengthTicks = 0;
  uint32_t loopLengthTicks = 0;

  bool contains(uint32_t phase) const;
  bool isValid() const { return loopLengthTicks > 0 && lengthTicks > 0 && lengthTicks <= loopLengthTicks; }
};

/// DEC-108 item 5 / task 3.4: the identity a published window carries, and the only currency a consumer may
/// compare. `playbackRevision` is the sole **content-currency** authority (`Loop::committedPassRevision()` is
/// its alias), `playbackGeneration` guards **ownership** changes and stale deferred completions (never a second
/// currency), `interval` carries the loop geometry, and the slot says whose window this is. The snapshot identity
/// stays provenance/lifetime protection inside `PlaybackViewSourceIdentity`, never another definition of
/// "current".
struct PlaybackWindowIdentity {
  uint8_t slot = UINT8_MAX;
  uint32_t playbackGeneration = 0;
  uint32_t playbackRevision = 0;
  PlaybackCoverageInterval interval{};
  /// Task 3.13: the **origin** is part of the window's identity. A preview window at the same slot, generation,
  /// revision and interval is therefore *not equal* to the committed window it stands in front of -- which is the
  /// spec's "preview is identified as preview rather than as a committed window", and what lets a consumer that
  /// requires committed currency refuse it without inspecting anything else.
  PlaybackWindowOrigin origin = PlaybackWindowOrigin::Committed;

  bool operator==(const PlaybackWindowIdentity& other) const {
    return slot == other.slot && playbackGeneration == other.playbackGeneration &&
           playbackRevision == other.playbackRevision && origin == other.origin &&
           interval.startPhase == other.interval.startPhase &&
           interval.lengthTicks == other.interval.lengthTicks &&
           interval.loopLengthTicks == other.interval.loopLengthTicks;
  }
  bool operator!=(const PlaybackWindowIdentity& other) const { return !(*this == other); }
  bool isValid() const { return slot != UINT8_MAX && playbackRevision != 0 && interval.isValid(); }
};

/// AC-5 / F-3 (task 3.6b): the **lag decision** for one traversal of an affected interval. The rule is total -- every
/// input yields one of these three, so there is **no waiting outcome** and a consumer can never be blocked by a missed
/// deadline. `PreviousOnePass` means the previous revision stays authoritative for *that* traversal; the swap happens
/// at a later eligible boundary, never mid-consumption.
enum class PlaybackLagDecision : uint8_t {
  /// The traversal may be served at the loop's current revision: either the served window already carries it, or a
  /// complete current window is published for the boundary (which adopts it).
  Current,
  /// A first miss: the previous revision remains authoritative for this traversal, and `missedTraversals == 1`.
  PreviousOnePass,
  /// A second (or later) consecutive miss: the **named** contract breach. The previous revision is still served --
  /// containment is deliberate, and the lag is never silently reset (DEC-108 item 13(e)).
  BreachSecondMiss,
};

/// DEC-108 §13(e)/(f) / task 4.5: the **containment** decision at the moment of serving, once a deadline-contract fault
/// is latched. It is total, and it deliberately has **no** waiting and no rebuilding outcome: the clock is never
/// blocked, no partial content is published, and no unbounded rebuild is ever performed.
enum class ContinuityContainment : uint8_t {
  /// No fault is latched: the traversal is served normally (or one pass late, which is the allowance, not a fault).
  Nominal,
  /// The fault is latched and valid coverage remains: the **last coherent revision** keeps being served through the
  /// admitted bounded path while preparation keeps progressing toward the eligible boundary.
  ServeLastCoherentRevision,
  /// The fault is latched and **no valid coverage remains at all**: the approved last resort -- targeted note closure
  /// plus affected-slot silence. Never a global transport stop, never an unbounded rebuild.
  LastResortTargetedSilence,
};

/// The containment rule as one **pure** resolution, so "containment is not a successful fallback" is a property of the
/// table rather than of a caller's control flow: an unlatched fault is nominal; a latched fault with remaining coverage
/// continues the last coherent revision; a latched fault with none takes the approved last resort.
inline ContinuityContainment resolveContinuityContainment(bool faultLatched, bool validCoverageRemains) {
  if (!faultLatched) {
    return ContinuityContainment::Nominal;
  }
  return validCoverageRemains ? ContinuityContainment::ServeLastCoherentRevision
                              : ContinuityContainment::LastResortTargetedSilence;
}

/// The per-interval accounting behind the lag rule: which interval is being traversed, the revisions involved, how many
/// consecutive traversals missed, and whether this episode's breach has been reported (once per episode).
struct PlaybackLagState {
  uint32_t intervalStart = 0;
  uint32_t intervalLength = 0;
  uint32_t servedRevision = 0;
  uint32_t desiredRevision = 0;
  uint8_t missedTraversals = 0;
  uint8_t breachReports = 0;
  bool breachReported = false;

  /// The episode ends when a traversal is served at the current revision, or when the interval changes: the allowance
  /// is per pass **and per interval**, never a slot-wide counter a later edit could inherit.
  void resetEpisode(uint32_t start, uint32_t length) {
    intervalStart = start;
    intervalLength = length;
    missedTraversals = 0;
    breachReports = 0;
    breachReported = false;
  }
  bool coversInterval(uint32_t start, uint32_t length) const {
    return intervalStart == start && intervalLength == length;
  }
};

/// DEC-108 §13d / task 3.11: the **lifecycle transitions** whose shared-view ownership rules the spec states. Each row
/// says what happens to the in-flight operation, the prepared successor, the operation's identity, the served window
/// and the frozen source -- one place, so "ownership survives transitions" is checkable rather than implied.
enum class PlaybackViewTransition : uint8_t {
  /// A new edit: the frozen builder source is retained and the successor is coalesced (one prepared successor at a time).
  NewEdit,
  /// A slot switch: inbound coverage is prepared before binding, and outbound resources are retained until their readers
  /// and closures finish.
  SlotSwitch,
  /// Selecting another track: an existing worker is **never retargeted** -- it keeps its slot, its identity and its work.
  SelectedTrackChange,
  /// A record/overdub stop with a successor start: the predecessor's frozen ownership is preserved and the successor
  /// never borrows its buffers.
  StopWithSuccessorStart,
  /// Transport stop: obsolete **lookahead** is cancelled, while accepted content and finalization are preserved.
  TransportStop,
  /// Clear or load: the old operation's **identity is invalidated** and its source is retained until cancellation and
  /// readers finish.
  ClearOrLoad,
  /// A completion that arrives after its operation was invalidated or its resources retired: it is **rejected for
  /// publication** and releases only what it still owns.
  LateCompletion,
};

/// What a transition does to each element of the shared view state. One field per clause of the spec's requirement, so a
/// reader can check the table against the sentence rather than against the code's control flow.
struct PlaybackViewTransitionEffect {
  /// The operation keeps its identity and may still publish (false means its lookahead is cancelled or it is late).
  bool operationContinues = true;
  /// The in-flight candidate's resources are released (never published).
  bool candidateReleased = false;
  /// A prepared successor must not be adopted and its storage returns to the free roles.
  bool preparedSuccessorRetired = false;
  /// A new operation identity: an earlier operation can never match again (a returning slot revives nothing).
  bool identityInvalidated = false;
  /// The served window stays readable throughout: **no** transition may tear it.
  bool servedWindowStaysReadable = true;
  /// The frozen source stays pinned and owned until its readers and closure obligations finish.
  bool frozenSourceHeld = true;
};

/// The table itself, as one **pure** resolution: every lifecycle transition (and any future one) has exactly one row,
/// and the served window's readability is not a parameter -- it is the invariant the spec demands of all of them.
inline PlaybackViewTransitionEffect resolvePlaybackViewTransition(PlaybackViewTransition transition) {
  PlaybackViewTransitionEffect effect{};
  if (transition == PlaybackViewTransition::TransportStop) {
    // Obsolete lookahead goes, accepted content and finalization stay.
    effect.operationContinues = false;
    effect.candidateReleased = true;
    effect.preparedSuccessorRetired = true;
    return effect;
  }
  if (transition == PlaybackViewTransition::ClearOrLoad) {
    effect.operationContinues = false;
    effect.candidateReleased = true;
    effect.preparedSuccessorRetired = true;
    effect.identityInvalidated = true;
    return effect;
  }
  if (transition == PlaybackViewTransition::LateCompletion) {
    // It is late because the identity already moved; it holds nothing new.
    effect.operationContinues = false;
    effect.candidateReleased = true;
    effect.identityInvalidated = true;
    effect.frozenSourceHeld = false;
    return effect;
  }
  // NewEdit, SlotSwitch, SelectedTrackChange, StopWithSuccessorStart: the operation continues with its identity, the
  // frozen source is held, and the successor -- if any -- is coalesced rather than retired.
  return effect;
}
/// DEC-108 §13(g) / task 4.5: the **cross-generation closure reconciliation** for one version handoff, computed
/// **off-clock** and bounded. It is a value, not a running process: the plan is written once and the clock path only
/// ever reads it. Every list is bounded by `ActiveNoteLedger::kMaxOpenNotes` and nothing allocates.
///
/// The three fates a sounding note can have at a handoff are separated by construction, which is how the spec's clauses
/// become checkable rather than implied:
///   - **removed or invalidated** by the replacement -> `closures` (its note-off is required at handoff);
///   - **re-issued** by the replacement -> `suppressedRetriggers` (the replacement's note-on must NOT be emitted: a note
///     that remains unchanged is never retriggered);
///   - **neither** -> preserved untouched (`preservedCount`), which is also where a note that is simply sustained across
///     the handoff lands.
struct ClosureSettlement {
  static constexpr size_t kMaxKeys = ActiveNoteLedger::kMaxOpenNotes;
  /// Sounding keys to close at handoff, as `(channel << 8) | note`.
  std::array<uint16_t, kMaxKeys> closures{};
  uint16_t closureCount = 0;
  /// Sounding keys the replacement re-issues: its note-ons for these are suppressed.
  std::array<uint16_t, kMaxKeys> suppressedRetriggers{};
  uint16_t suppressedRetriggerCount = 0;
  /// Sounding keys the replacement neither removes nor re-issues: untouched, neither closed nor retriggered.
  uint16_t preservedCount = 0;
  /// The incoming content carried **no events at all**: "no new events", so nothing is closed on its account.
  bool eventEmptyWindow = false;

  bool closesNothing() const { return closureCount == 0; }
};

/// The reconciliation's one **interpretive** rule, stated where it is decided rather than buried in a loop: absence from
/// the incoming content is evidence of removal **only when that content is authoritative for the whole loop**. A window
/// is a *slice* of the loop, so a sustained note whose onset lies outside the slice is still sounding -- which is exactly
/// why "an event-empty window means *no new events*" and must never silence it.
inline bool handoffAbsenceMeansRemoval(bool incomingIsWholeLoop, bool incomingIsEmpty) {
  return !incomingIsEmpty && incomingIsWholeLoop;
}


struct LedgerBoundarySummary {
  std::array<ActiveNoteLedger::Entry, ActiveNoteLedger::kMaxOpenNotes> openNotes{};
  uint16_t count = 0;
  bool overflowed = false;

  void clear();
  void capture(const ActiveNoteLedger& ledger);
  bool matches(const ActiveNoteLedger& ledger) const;
};

/// One preallocated event/order pair. `eventCount` is bounded by `capacity`;
/// callers must treat a full buffer as an admission denial, never grow it.
struct FixedPlaybackEventStorage {
  MidiEvent* events = nullptr;
  uint16_t* order = nullptr;
  uint32_t* phases = nullptr;
  uint16_t* mergeScratch = nullptr;
  uint8_t* retained = nullptr;
  size_t capacity = 0;
  size_t eventCount = 0;

  FixedPlaybackEventStorage() = default;
  FixedPlaybackEventStorage(const FixedPlaybackEventStorage&) = delete;
  FixedPlaybackEventStorage& operator=(const FixedPlaybackEventStorage&) = delete;
  ~FixedPlaybackEventStorage();

  bool prewarm(size_t requestedCapacity);
  void release();
  void clear() { eventCount = 0; }
  bool isPrewarmed() const {
    return events != nullptr && order != nullptr && phases != nullptr && mergeScratch != nullptr &&
           retained != nullptr && capacity != 0;
  }
  bool canAppend(size_t count = 1) const { return isPrewarmed() && count <= capacity - eventCount; }
  bool append(const MidiEvent& event);
  bool discardEvent(size_t index) {
    if (retained == nullptr || index >= eventCount) return false;
    retained[index] = 0;
    return true;
  }
  bool eventRetained(size_t index) const {
    return retained != nullptr && index < eventCount && retained[index] != 0;
  }
};

struct PlaybackPassEventOrigin { uint16_t pass; uint16_t event; };

struct RuntimePlaybackView {
  // Reusing a physical window invalidates borrowed indexes even if its source
  // and coverage happen to repeat after a complete loop cycle.
  uint64_t storageGeneration = 0;
  FixedPlaybackEventStorage storage;
  PlaybackPassEventOrigin* passOrigins = nullptr;
  // Position in the complete canonical source merge, independent of window filtering.
  uint16_t* canonicalRanks = nullptr;
  bool canonicalRanksReady = false;
  PlaybackViewSourceIdentity passOriginSource;
  bool passOriginsReady = false;
  ~RuntimePlaybackView();
  void release();
  PlaybackViewSourceIdentity source;
  EmissionCursor cursorAnchor;
  PlaybackCoverageInterval coverage;
  LedgerBoundarySummary ledgerBoundary;
  bool rawCoveragePrepared = false;
  // Bind already ordered raw coverage once final geometry is known. No scan.
  bool bindRawCoverage(uint32_t loopLengthTicks);
  /// S4f.19: project a prepared raw bar onto the interval's **final** loop geometry
  /// before binding it. Applies the record stop tick policy (an overflowing NoteOn is
  /// remapped into the loop, any other overflowing row is dropped), closes a note left
  /// open at the loop end with one synthetic Off at `loopLengthTicks - 1` carrying the
  /// NoteOn's identity, clamps the coverage to the final loop, and re-orders the
  /// retained rows canonically (phase, NoteOff < NoteOn < other). Bounded by the view
  /// role capacity -- the prepared window is at most one bar. Refuses a view that is not
  /// prepared/complete, so a partially built window is never bound.
  /// Lane-map words for the MIDI (channel, note) domain: 16 x 128 bits. A caller that
  /// knows which lanes were open at the stop supplies this bitmap so the projection
  /// closes only those lanes; a lane left open here has its real Off in the take.
  static constexpr size_t kOpenLaneWords = 32;
  bool projectRawCoverageToLoopLength(uint32_t loopLengthTicks,
                                      const uint64_t* openAtStopLanes = nullptr);
  // Set by `bindRawCoverage`: this view is the record-session return window, the
  // first take's only audible source until its first root publishes. It is
  // preserved across playback-cache metadata resets and released by the next
  // take's preparation boundary.
  bool firstRecordReturnWindow = false;
  bool sourceFilled = false;
  bool complete = false;
  bool published = false;

  bool prewarm(size_t eventCapacity);
  void resetMetadata();
};

enum class PlaybackBridgeOperation : uint8_t { Insert, Suppress, Replace };

/// An immutable overlay row.  It carries the replacement event for Insert and
/// Replace; Suppress ignores `event` and removes `eventProvenance`.
struct PlaybackBridgeRecord {
  PlaybackBridgeOperation operation = PlaybackBridgeOperation::Insert;
  uint32_t eventProvenance = 0;
  MidiEvent event{};
};

// Closure state uses the compiler's existing ledger matching and cycle rules.
// The copied Off remains meaningful after the base window is retired. Its time
// is measured from phase zero of the bridge anchor's projection cycle.
struct PlaybackHeldNoteClosure {
  MidiEvent originalOff{};
  uint64_t relativeTick = UINT64_MAX;
  uint32_t nextOnPhase = UINT32_MAX;
  uint8_t flags = 0; // Affected, original Off found, inherited from a predecessor.
  uint32_t rank = 0;
  bool resolved() const { return (flags & 3) == 3 && relativeTick != UINT64_MAX; }
};

struct FixedPlaybackBridgeStorage {
  uint64_t storageGeneration = 0;
  PlaybackBridgeRecord* records = nullptr;
  // Prepared indexes refer to the immutable base, never copy or mutate it.
  // eventProvenance on Suppress/Replace is a storage index in baseSource.
  // Admission cycle, the next two cycles, then the canonical steady stream.
  // A retained Off can cross one wrap; a changed pair can cross another.
  uint16_t* baseOrder = nullptr;
  uint16_t* nextCycleBaseOrder = nullptr;
  uint16_t* secondCycleBaseOrder = nullptr;
  uint16_t* steadyBaseOrder = nullptr;
  uint16_t* currentCycleInsertionOrder = nullptr;
  uint16_t* nextCycleInsertionOrder = nullptr;
  uint16_t* secondCycleInsertionOrder = nullptr;
  uint8_t* insertionCycleMask = nullptr;  // Bits 0..3 select the prepared cycle indexes.
  uint8_t* baseRetained = nullptr;
  // Effective-cycle gates indexed by canonical rank; metadata covers later windows too.
  uint8_t* canonicalCycleMasks = nullptr;
  size_t canonicalRankCount = 0;
  bool canonicalMasksReady = false;
  std::array<uint32_t, 4> affectedPitches{};
  std::array<uint32_t, 4> affectedIdentities{};
  void markAffected(const MidiEvent& event);
  bool matchesAffectedLedger(const ActiveNoteLedger& ledger) const;
  uint32_t* insertionProvenance = nullptr;
  uint32_t* insertionRanks = nullptr;
  uint32_t* baseRanks = nullptr;
  uint16_t* rankedBaseOrder = nullptr;
  uint16_t* baseOrderScratch = nullptr;
  size_t baseCapacity = 0;
  size_t baseEventCount = 0;
  size_t nextCycleBaseEventCount = 0;
  size_t secondCycleBaseEventCount = 0;
  size_t steadyBaseEventCount = 0;
  size_t currentCycleInsertionCount = 0;
  size_t nextCycleInsertionCount = 0;
  size_t secondCycleInsertionCount = 0;
  size_t steadyInsertionCount = 0;
  std::array<PlaybackHeldNoteClosure, ActiveNoteLedger::kMaxOpenNotes> heldClosures{};
  size_t heldClosureCount = 0; // Same indexes as the owned ledgerBoundary.
  const PlaybackHeldNoteClosure* heldClosure(size_t ledgerIndex) const {
    return complete && streamPrepared && ledgerIndex < heldClosureCount &&
        heldClosures[ledgerIndex].resolved() ? &heldClosures[ledgerIndex] : nullptr;
  }
  bool protectsOpenNotes = false;
  bool hasRetainedClosure = false;
  uint32_t retainedClosurePhaseLimit = UINT32_MAX;
  uint32_t preparedAdmissionPhase = 0;
  FixedPlaybackEventStorage insertions;
  PlaybackViewSourceIdentity baseSource;
  uint64_t baseStorageGeneration = 0;
  bool streamPrepared = false;
  size_t capacity = 0;
  size_t recordCount = 0;
  PlaybackViewSourceIdentity source;
  PlaybackCoverageInterval coverage;
  EmissionCursor cursorAnchor;
  LedgerBoundarySummary ledgerBoundary;
  bool complete = false;
  bool selected = false;

  FixedPlaybackBridgeStorage() = default;
  FixedPlaybackBridgeStorage(const FixedPlaybackBridgeStorage&) = delete;
  FixedPlaybackBridgeStorage& operator=(const FixedPlaybackBridgeStorage&) = delete;
  ~FixedPlaybackBridgeStorage();

  bool prewarm(size_t requestedCapacity);
  void release();
  void resetMetadata();
  bool append(const PlaybackBridgeRecord& record);
};

/// Reservation made before an accepted captured NoteOn mutates Loop capture
/// state.  Credits are consumed by later task-2.3 staging/finalization grains.
struct CaptureNoteReservation {
  NoteId noteId = kInvalidNoteId;
  uint8_t channel = 0;
  uint8_t note = 0;
  uint16_t stagingCredits = 0;
  uint16_t bridgeCredits = 0;
  uint16_t stopFinalizationCredits = 0;
  size_t noteOnStagingIndex = SIZE_MAX;
  bool open = false;
};

struct CaptureAdmissionStaging {
  FixedPlaybackEventStorage storage;
  uint32_t captureGeneration = 0;
  uint32_t frozenGeneration = 0;
  bool frozen = false;
  size_t reservedStagingCredits = 0;
  size_t reservedBridgeCredits = 0;
  size_t reservedStopFinalizationCredits = 0;
  std::array<CaptureNoteReservation, RuntimePlaybackViewConfig::kMaxCaptureNoteReservations>
      reservations{};

  bool prewarm();
  void reset();
  /// Freeze the accepted input generation before a wrap or deferred stop.  A
  /// frozen staging area has fixed backing storage and may only consume the
  /// reservations made before the freeze; it cannot admit a new NoteOn.
  bool freeze(uint32_t generation);
  size_t openReservationCount() const;
  /// Reserve one independent non-note event before it reaches canonical
  /// capture. Its storage row is consumed by the append itself; its bridge
  /// credit remains held until this generation retires.
  bool reserveIndependentEvent();
  void releaseIndependentEvent();
  /// Returns one reserved captured note that still needs a terminal closure.
  /// The caller closes it after appending that closure to both canonical and
  /// staging storage.  This is deliberately an O(kMaxOpenNotes) bounded scan,
  /// not an iterator over Track's dynamic pending-note map.
  CaptureNoteReservation* firstOpenReservation();
  CaptureNoteReservation* reserve(NoteId noteId, uint8_t channel, uint8_t note,
                                  uint16_t stagingCredits, uint16_t bridgeCredits,
                                  uint16_t stopFinalizationCredits);
  CaptureNoteReservation* matchingOpenReservation(NoteId noteId, uint8_t channel, uint8_t note);
  /// Re-admit a NoteOn carried across an overdub wrap into a fresh capture
  /// generation. It reserves the successor pass's terminal closure before
  /// recording the fixed staging row.
  bool appendContinuationNoteOn(const MidiEvent& event);
  /// Appends the terminal event reserved by an accepted NoteOn, then retires
  /// exactly that reservation. This remains valid after `freeze()`: a
  /// deferred stop may consume closure capacity but cannot admit new input.
  bool appendReservedNoteOff(const MidiEvent& event, bool retainPair = true);
  void close(CaptureNoteReservation& reservation);

 private:
  bool hasCredits(uint16_t stagingCredits, uint16_t bridgeCredits,
                  uint16_t stopFinalizationCredits) const;
};

/// Owns the only permitted rotation of the three fixed views.  In particular,
/// an incomplete build never becomes ready at a handoff, and the retired
/// current buffer stays idle until that build has completed.
enum class PlaybackViewHandoffMode : uint8_t { Audible, Preparation };

struct RuntimePlaybackViewLifecycle {
  static constexpr uint8_t kNoView = UINT8_MAX;

  uint8_t current = 0;
  uint8_t ready = 1;
  uint8_t build = 2;
  uint8_t idle = kNoView;
  // Optional immutable return coverage, using one of the same three views.
  uint8_t retained = kNoView;
  bool retainCurrent(std::array<RuntimePlaybackView, RuntimePlaybackViewConfig::kViewCount>& views);
  // Install a completed preparation view as the non-audible current view.
  // Used at first-record start to establish bar 1 before any input arrives.
  bool installPreparedCurrent(std::array<RuntimePlaybackView, RuntimePlaybackViewConfig::kViewCount>& views);
  // Replace a retained preparation view with its completed successor. The old
  // view becomes the next build buffer, so accepted source updates never mutate
  // a view that is being traversed.
  bool replaceRetainedWithReady(std::array<RuntimePlaybackView, RuntimePlaybackViewConfig::kViewCount>& views);
  // Owner must discard any in-flight builder and preflight source/ledger first.
  // Reuses the existing views; subsequent coverage needs a fresh build.
  bool handoffToRetained(std::array<RuntimePlaybackView, RuntimePlaybackViewConfig::kViewCount>& views);

  void reset();
  bool handoff(std::array<RuntimePlaybackView, RuntimePlaybackViewConfig::kViewCount>& views,
               PlaybackViewHandoffMode mode = PlaybackViewHandoffMode::Audible);
  bool publishCompletedBuildAsReady(
      std::array<RuntimePlaybackView, RuntimePlaybackViewConfig::kViewCount>& views,
      PlaybackViewHandoffMode mode = PlaybackViewHandoffMode::Audible);
  /// DEC-108 item 1 (windowed-playback task 3.2): install a **completed build** as the current **interval
  /// window** when no authoritative window exists yet -- the demoted slot's first window, whose only
  /// representation is windowed, so `publishCompletedBuildAsReady`'s retained/idle successor requirement can
  /// never be met. The invariant that matters is kept and checked: **only a complete view is ever installed**,
  /// exactly as `handoff` requires of its ready view. The swap is the same atomic pair `handoff` performs -- the
  /// retired window becomes the next build buffer, so a consumer that read it before the call saw complete
  /// content and no read straddles the replacement. It refuses only when `retained` is the view that would be
  /// recycled, because a retained view must stay readable for its own lane.
  bool publishCompletedBuildAsCurrentWindow(
      std::array<RuntimePlaybackView, RuntimePlaybackViewConfig::kViewCount>& views);
  /// DEC-108 item 5 / task 3.4 (the windowed lane's successor): install a completed build as the **ready
  /// successor** while a window is being served. `publishCompletedBuildAsReady` treats the prewarmed shell at
  /// `ready` as an existing successor and refuses; a shell is not a successor -- it is not complete, so `handoff`
  /// can never adopt it -- and this variant accepts it while keeping the rule that matters: **one real successor
  /// at a time**, and only a **complete** view is ever installed. The served `current` window is not touched, so
  /// a consumer read cannot straddle the replacement; the swap happens later, in `handoffReadyCoverage`.
  bool publishCompletedBuildAsReadySuccessor(
      std::array<RuntimePlaybackView, RuntimePlaybackViewConfig::kViewCount>& views);
  bool buildIsPending() const { return build != kNoView; }
  bool hasReadySuccessor() const { return ready != kNoView; }
};

/// Selected and candidate bridges have independent fixed buffers.  Beginning a
/// new candidate is the only mutable operation; the selected bridge remains
/// readable until an eligible candidate atomically replaces it.
struct RuntimePlaybackBridgeLifecycle {
  static constexpr uint8_t kNoBridge = UINT8_MAX;

  uint8_t selected = kNoBridge;
  uint8_t candidate = 0;

  void reset();
  bool beginCandidate(
      std::array<FixedPlaybackBridgeStorage, RuntimePlaybackViewConfig::kBridgeCount>& bridges);
  /// No-fail publication; caller has checked candidate, source and coverage.
  void publishCandidate(std::array<FixedPlaybackBridgeStorage, RuntimePlaybackViewConfig::kBridgeCount>& bridges);
  bool selectCandidate(
      std::array<FixedPlaybackBridgeStorage, RuntimePlaybackViewConfig::kBridgeCount>& bridges,
      const EmissionCursor& earliestAffectedCursor);
};

/// Per-slot bundle.  Allocation happens only in `prewarm`; an active playback
/// tick may inspect these buffers but must never call `prewarm` or `release`.
struct RuntimePlaybackViewSet {
  std::array<RuntimePlaybackView, RuntimePlaybackViewConfig::kViewCount> views;
  std::array<FixedPlaybackBridgeStorage, RuntimePlaybackViewConfig::kBridgeCount> bridges;
  // Immutable source bases belong to selected/candidate bridges, independently
  // of the three rolling emission windows. Cleanup may release preparation
  // workers without invalidating a source still supplying future windows.
  std::array<RuntimePlaybackView, RuntimePlaybackViewConfig::kBridgeCount> bridgeBases;
  // Two independently prewarmed admission stagings. Live ingress owns the
  // `live` role for the open interval; the one admitted stop job borrows the
  // `frozen` role. Punch-out exchanges the two indexes in constant work, so the
  // job keeps its immutable frozen rows while a successor interval can own a
  // separate ingress buffer. One staging is never shared between the two roles.
  std::array<CaptureAdmissionStaging, 2> captureStagings;
  uint8_t liveStagingIndex = 0;
  uint8_t frozenStagingIndex = 1;
  CaptureAdmissionStaging& liveCaptureStaging() { return captureStagings[liveStagingIndex]; }
  const CaptureAdmissionStaging& liveCaptureStaging() const {
    return captureStagings[liveStagingIndex];
  }
  CaptureAdmissionStaging& frozenCaptureStaging() { return captureStagings[frozenStagingIndex]; }
  const CaptureAdmissionStaging& frozenCaptureStaging() const {
    return captureStagings[frozenStagingIndex];
  }
  /// Constant-work role exchange. The caller has frozen the live role first.
  void exchangeCaptureStagingRoles() {
    const uint8_t previousLive = liveStagingIndex;
    liveStagingIndex = frozenStagingIndex;
    frozenStagingIndex = previousLive;
  }
  bool captureStagingsPrewarmed() const {
    return liveCaptureStaging().storage.isPrewarmed() &&
           frozenCaptureStaging().storage.isPrewarmed();
  }
  RuntimePlaybackViewLifecycle lifecycle;
  RuntimePlaybackBridgeLifecycle bridgeLifecycle;
  bool prewarmed = false;
  /// Whether `prewarm` reserved the whole-take representation (the two bridges and their
  /// immutable bases). RC-MEM-2: every full-take publication requires an exactly full-loop
  /// coverage interval (`publishStoppedOracleView`, `publishStoppedCanonicalOracleView`,
  /// `publishPreparedPlaybackPairFromWorkingWindow` -> `isFullLoopMergedPlaybackWindow`), so a
  /// loop that cannot produce one must not reserve the capacity -- and must be refused, not
  /// merely unable, if a full-take publication is ever attempted. False means the windowed
  /// representation is the only authoritative one for this slot.
  bool wholeTakeReserved = false;

  /// Reserve this slot's playback representation. `reserveWholeTake` must be the owning
  /// loop's `fullTakePlaybackViewAdmissible()` answer; false keeps the three rolling views
  /// and both capture stagings (the windowed lanes and capture admission need them) and
  /// leaves the bridges, their bases and the prepared pair unreserved.
  bool prewarm(bool reserveWholeTake = true);
  /// True when the whole-take representation was reserved and can still be published.
  bool wholeTakeViewsReserved() const { return wholeTakeReserved; }
  /// Drop the whole-take representation once the owning loop can no longer publish one.
  /// A record arm on an *empty* slot has no length yet, so `prewarm` legitimately reserves
  /// the bundle; by the time the loop is known to be long (the next cold admission, e.g.
  /// overdub start) the reservation must be released rather than kept for the session.
  /// Returns true when the reservation is absent afterwards. Refuses while a bridge is
  /// selected, because that bridge is the live authoritative representation.
  bool releaseWholeTakeViews();
  void resetMetadata();
  // Invalidate ordinary candidates while retaining the non-audible first-record
  // return view and its source identity. Used by cache refresh during capture.
  void resetMetadataPreserveRetained();
  /// Preserve exactly one view across a metadata reset and make it the current
  /// view, leaving the other two as build/idle. `keep` must be a valid view index.
  void resetMetadataPreserveView(uint8_t keep);
  /// Index of the bound record-session return window, or `kNoView`. It is either
  /// the retained view (unlaunched) or the current view (after the record-stop
  /// handoff, which releases `retained`).
  uint8_t firstRecordReturnWindowIndex() const;
  bool handoffReadyCoverage(PlaybackViewHandoffMode mode = PlaybackViewHandoffMode::Audible);
  bool publishCompletedBuildAsReady(PlaybackViewHandoffMode mode = PlaybackViewHandoffMode::Audible);
  bool beginCandidateBridge();
  bool selectCandidateBridge(const EmissionCursor& earliestAffectedCursor);
  size_t fixedExternalBytes() const;
};
