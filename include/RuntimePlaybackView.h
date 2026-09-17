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

struct PlaybackViewSourceIdentity {
  uint32_t playbackRevision = 0;
  uint32_t sourceGeneration = 0;
  const void* snapshotIdentity = nullptr;

  bool operator==(const PlaybackViewSourceIdentity& other) const {
    return playbackRevision == other.playbackRevision &&
           sourceGeneration == other.sourceGeneration &&
           snapshotIdentity == other.snapshotIdentity;
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
  PlaybackViewSourceIdentity source;
  EmissionCursor cursorAnchor;
  PlaybackCoverageInterval coverage;
  LedgerBoundarySummary ledgerBoundary;
  bool rawCoveragePrepared = false;
  // Bind already ordered raw coverage once final geometry is known. No scan.
  bool bindRawCoverage(uint32_t loopLengthTicks);
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

  bool prewarm();
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
