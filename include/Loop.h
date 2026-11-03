//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

/**
 * @file Loop.h
 * @brief Per-slot loop data: MIDI events, loop geometry, playback state, undo.
 *
 * committed passes MIDI lives in passes (recordPass, overdubPasses). Note edits are stored
 * in passes.editPasses and materialized via LoopPasses::materialize for playback/display.
 * Committed length/start revisions live in passes.loopGeometries.
 */
#ifndef LOOP_H
#define LOOP_H

#pragma once
#include "Utils/PlaybackBuildRefusal.h"
#include <cstdint>
#include <vector>
#include <memory>
#include "MidiEvent.h"
#include "MidiEvent.h"
#include "LoopEventBuffer.h"
#include "StorageLoopIo.h"
#include "EditPass.h"
#include "LoopPasses.h"
#include "LoopSnapshot.h"
#include "CommittedEventRange.h"
#include "VisualCache.h"
#include "TrackState.h"
#include "Utils/NoteUtils.h"
#include "Utils/CoveragePrerequisite.h"
#include "LoopContentResolution.h"
#include "Utils/ExternalMemoryFirstAllocator.h"
#include "Globals.h"
#include "CaptureAppendResult.h"
#include "ActiveNoteLedger.h"
#include "OverlapNoteIdSet.h"
#include "PassReclaim.h"
#include "PendingNoteChange.h"
#include "Utils/LoopStopFinalize.h"

/// DEC-192 (report-only): why the last publication request for this Loop was not treated as the same in-flight
/// inventory. A free function, deliberately: `sizeof(Loop)` is pinned and a diagnostic byte must not move it.
PublicationIdentityRefusal loopLastPublicationIdentityRefusal(const Loop& loop);

class RuntimeCaptureCompanions;
class RuntimeCanonicalPassInputs;
struct CaptureAdmissionStaging;
class Track;
class NoteEditCurrentState;
struct PlaybackMergedMidiEvents;

using PlaybackOrderVec = std::vector<size_t, ExternalMemoryFirstAllocator<size_t>>;
using PrewarmedCommittedChunkIdPool =
    std::vector<CommittedChunkIdList, ExternalMemoryFirstAllocator<CommittedChunkIdList>>;

struct PublishedSnapshotBox;
struct PendingSnapshotPublishBox;
struct SessionExitSnapshotHandoff;

/// Why a prepared companion history cannot advance. **Diagnostic only** (DEC-186): the classification is the
/// same predicate set `beginPreparedCompanionHistory` / `stepPreparedCompanionHistory` already use, so it can
/// never disagree with behaviour and adds no state. Before this, a job parked in `CompanionHistory` reported
/// nothing at all: the device capture `session_20261028_220707.log` serviced 50,121 grains of that phase in
/// ~32 s without one row naming the refusing predicate.
enum class CompanionPreparationRefusal : uint8_t {
  None = 0,
  /// The horizon is not `Prewarmed`, so `begin` cannot bind it: an in-flight/`Denied`/foreign preparation.
  PhaseNotPrewarmed,
  /// A begun preparation belongs to another interval (successor or released owner).
  OwnerMismatch,
  RowsCapacity,
  RowLimit,
  CompanionCapacity,
  CompanionIdCapacity,
  EditCapacity,
  RowsNotReady,
  PassIdExhausted,
  SnapshotBoxMissing,
  SnapshotCapacity,
  SnapshotLimit,
  StagedPassPreparing,
  CanonicalInputsUnbound,
  CanonicalInputsNoChunks,
  CanonicalInputsChunkOverflow,
  CanonicalInputsNotFrozenOverdub,
  CanonicalInputsOtherCapture,
  /// The begun preparation's pinned identities moved. One value per term of
  /// `preparedCompanionHistorySourceCurrent`, because that is the predicate a parked job trips.
  SourceNotCurrent,
  SourceSnapshotGeneration,
  SourceSnapshotCount,
  SourceStorageGeneration,
  SourceRevision,
  SourceHistoryCount,
  SourceFirstId,
  SourceRowGeneration,
  SourceRowsNotReady,
  SourceCapturePassReleased,
  SourceCapturePassId,
  SourceCaptureChunks,
  SourceInputsGeneration,
  SourceInputsNotFrozenOverdub,
  SourceInputsOtherCapture,
  /// The phase machine itself gave up. One value per internal denial site, because each is a different
  /// mechanism (content shape, capacity, or frozen-capture verification).
  PhaseDenied,
  DeniedRowIdOutOfRange,
  DeniedRowPayloadCapacity,
  DeniedRowNotATransform,
  DeniedCaptureChunkUnreadable,
  DeniedCaptureEventCount,
  DeniedCaptureEventMismatch,
  /// The job's declared canonical-preparation grain credit is spent.
  GrainCreditExhausted,
  /// The step ran but the phase never advanced (a `Denied` spin or a foreign ready horizon).
  StepNoProgress,
};

/// Overdub-session totals for `#CAP,DIAG,oh` (`add`/`shorten`/`hide` last). Incremented on note-off; emitted at stop.
struct OverlapHoldTotals {
  uint32_t noteOffs = 0;
  uint32_t emptySets = 0;
  uint32_t maxIds = 0;
  uint32_t overflows = 0;
  uint32_t lookedUp = 0;
  uint32_t maxExamined = 0;
  uint32_t sumExamined = 0;
  uint32_t maxLookupUs = 0;
  uint32_t sumLookupUs = 0;
  uint32_t add = 0;
  uint32_t shorten = 0;
  uint32_t hide = 0;
  // Consume candidate attribution: which selection path reached each candidate.
  // scanOnlyCandidates = window/JIT scan found it and its id was absent from the
  // occupy set. lateNoteCandidates = id was in the set but no source-view note
  // existed at lookup time (JIT merge materialized it). idsWithoutNotes = id in
  // the set with no selected note at all.
  uint32_t scanOnlyCandidates = 0;
  uint32_t lateNoteCandidates = 0;
  uint32_t idsWithoutNotes = 0;
};

struct Loop {
  /// Protected post-publication reclamation of this Loop's disabled passes.
  /// Prewarmed key probes take at most 13 comparisons; a unit moves or releases
  /// one row/reference. The enclosing Track excludes concurrent stack writers.
  class DisabledPassReclaim {
   public:
    enum class Result : uint8_t { Pending, Complete, Denied };
    bool prewarm(size_t maximumKeys);
    bool begin(Loop& loop, const GlobalUndoStack& stack, uint8_t slotIndex);
    Result step(size_t maximumUnits);
    bool complete() const { return phase_ == Phase::Complete; }
    size_t completedUnits() const { return units_; }
    size_t reclaimedPasses() const { return reclaimed_; }
   private:
    enum class Phase : uint8_t { Idle, GatherRecord, GatherOverdub, GatherEdit, SortSetup,
      SortCopy, References, Record, Overdub, OverdubTail, Edit, EditTail, Complete, Denied };
    using Keys = std::vector<uint64_t, ExternalMemoryFirstAllocator<uint64_t>>;
    Keys keys_, scratch_;
    std::vector<uint8_t, ExternalMemoryFirstAllocator<uint8_t>> pinned_;
    Loop* loop_ = nullptr;
    const GlobalUndoStack* stack_ = nullptr;
    const UndoEntry* entries_ = nullptr;
    size_t stackSize_ = 0, stackCursor_ = 0;
    UndoEntryId nextEntryId_ = 0;
    uint64_t generation_ = 0;
    uint32_t revision_ = 0;
    uint8_t slot_ = 0, referencePart_ = 0;
    size_t count_ = 0, cursor_ = 0, write_ = 0, entry_ = 0, reference_ = 0;
    size_t width_ = 1, left_ = 0, middle_ = 0, right_ = 0, a_ = 0, b_ = 0, out_ = 0;
    size_t units_ = 0, reclaimed_ = 0;
    bool changed_ = false;
    Phase phase_ = Phase::Idle;
    void mark(PassId id, bool edit);
    bool referenced(PassId id, bool edit) const;
    size_t find(uint64_t key) const;
    void nextReference();
    void invalidateStorage();
  };

  Loop();
  ~Loop();
  Loop(const Loop&) = delete;
  Loop& operator=(const Loop&) = delete;

  Capture capture;
  // Non-owning borrow of an admitted stop job's sealed raw capture extent. The
  // owner identity makes release conditional, so a stale job can never drop a
  // successor job's binding.
  const LoopEventStore::CaptureRange* borrowedSealedCapture_ = nullptr;
  const void* borrowedSealedCaptureOwner_ = nullptr;
  uint64_t borrowedSealedCaptureGeneration_ = 0;
  const PendingNoteChangeVec* borrowedPendingChanges_ = nullptr;
  const void* borrowedPendingChangesOwner_ = nullptr;
  const CapturePreview* borrowedPreview_ = nullptr;
  const void* borrowedPreviewOwner_ = nullptr;
  uint16_t captureNextEventIndex = 0;
  bool captureEventsSortDirty = false;
  bool captureHasLastAppendTick_ = false;
  // Maximum of accepted live rows, also used to conservatively detect out-of-order input.
  // Reuses the append-order scalar; no extra per-Loop storage.
  uint32_t captureMaximumTick_ = 0;
  uint32_t captureMaximumTick() const { return capture.store.empty() ? 0 : captureMaximumTick_; }
  uint16_t captureDisplayRevision = 0;
  /// E2 Record source receipt (not committed revision or display currency).
  uint64_t captureContentGeneration() const { return captureContentGeneration_; }
  CaptureProjectionIdentity liveCaptureProjectionIdentity() const {
    return {capture.phase.generation(), startLoopTick, loopStartTick,
            loopLengthTicks, static_cast<CapturePhase>(capture.phase)};
  }
  uint32_t captureDedupEventsDropped_ = 0;

  LoopId loopId = kInvalidLoopId;
  uint32_t playbackRevision = 0;
  // Invalidates borrowed pass/history storage even when audible revision is
  // unchanged or reset. Runtime-only; never restored from persisted content.
  uint64_t canonicalStorageGeneration = 0;
  LoopPasses passes;
  PassId nextPassId_ = 1;
  NoteId nextNoteId_ = 1;
  bool editStateDirty_ = false;
  bool hasPendingCapturePass_ = false;
  PendingCapturePass pendingCapturePass_;
  /// STOPPED-only reservoir for canonical ownership transfer. A deferred
  /// overdub stop moves one prepared list into `pendingCapturePass_`; it never
  /// creates or grows a committed-chunk vector while transport is active.
  PrewarmedCommittedChunkIdPool prewarmedCommittedChunkIdPool_;
  struct PreparedCompanionHistory {
    enum class Phase : uint8_t { Cold, Prewarmed, CopySnapshot, CopyRow, CopyPayload, Append, Trim, VerifyCapture, Ready, Committed, RecycleIds, RecycleSnapshot, RecycleRows, RecycleGrow, Denied };
    EditPassVec rows;
    EditPassIdList companionIds, snapshotIds;
    size_t snapshotCount = 0, snapshotIndex = 0;
    size_t rowLimit = 0, snapshotLimit = 0;
    uint64_t snapshotGeneration = 0;
    uint64_t preparationGeneration = 0;
    size_t historyCount = 0, companionCapacity = 0, targetCount = 0, row = 0, event = 0;
    uint64_t storageGeneration = 0, rowGeneration = 0;
    uint32_t revision = 0;
    PassId firstId = 0, capturePassId = kInvalidPassId;
    const RuntimeCaptureCompanions* source = nullptr;
    const CaptureAdmissionStaging* capture = nullptr;
    const RuntimeCanonicalPassInputs* captureInputs = nullptr;
    /// OpenSpec 5.12 / D8: the staged pass this preparation verifies its capture chunks
    /// against. Bound by address at `beginPreparedCompanionHistory`, so a deferred job's
    /// receipt survives a successor's capture start, and a successor's own staging cannot be
    /// confused with a predecessor's. Null when the preparation carries no frozen capture.
    PendingCapturePass* capturePass = nullptr;
    uint64_t captureInputsGeneration = 0;
    const uint16_t* captureChunks = nullptr;
    size_t captureChunkCount = 0, captureChunk = 0, captureEvent = 0, comparedEvents = 0;
    /// Interval qualification of an in-flight preparation: the owning
    /// `PendingStopAdmission::intervalToken()`, required by every consumer while
    /// a receipt is live (`Cold`/`Prewarmed` are receipt-free and
    /// owner-agnostic). A successor interval can therefore neither rebind nor
    /// consume a predecessor's preparation, while a copy of the owning
    /// admission (credit probing, retargeting) still identifies the same one.
    /// Zero means the live session path.
    uint64_t owner = 0;
    Phase phase = Phase::Cold;
  } preparedCompanionHistory_;
  bool preparedCompanionHistorySourceCurrent() const;
  bool preparedCompanionHistoryOwnedBy(uint64_t owner) const;

  VisualCache visualCache;
  bool deferredVisualCacheWrapAppendPending_ = false;
  /// `playbackRevision` at the moment wrap-append was first deferred. Append
  /// resumes only when this still matches the current revision (`203406`).
  uint32_t deferredVisualCacheWrapAppendFromRevision_ = 0;
  CapturePreview capturePreview;
  /// Seal handoff: capture rows kept alive past `capturePreview.clear()` until the committed
  /// representation the painter selects proves it covers `sealedCapturePreviewRevision_`.
  /// Each seal retains uncovered rows and merges this pass's closed preview; it never
  /// discards retained rows merely because a new pass is sealing (`120321`). `Loop` owns
  /// these; display only consumes them.
  NoteUtils::DisplayNoteVec sealedCapturePreviewNotes_;
  uint32_t sealedCapturePreviewRevision_ = 0;
  /// 5.12 display-only tier: rows retired from a closed session's sealed handoff. Read by the
  /// frame union alone; the four consuming paths keep reading `sealedCapturePreviewNotes_`.
  NoteUtils::DisplayNoteVec retiredSealedDisplayNotes_;
  uint32_t retiredSealedDisplayRevision_ = 0;
  VisualCacheDelta pendingVisualDelta;
  bool visualCacheDirty = true;
  size_t materializedEventCount_ = 0;
  uint32_t nextMergeSequence_ = 0;
  uint8_t nextOverdubSessionIndex_ = 1;
  uint8_t currentOverdubSessionIndex_ = kUngroupedOverdubSessionIndex;
  PassId lastCommittedPassId_ = kInvalidPassId;
  bool hasLastOverdubSeal_ = false;
  PassId lastOverdubSealPassId_ = kInvalidPassId;
  uint32_t lastOverdubSealPriorRevision_ = 0;
  uint32_t lastOverdubSealPlaybackRevision_ = 0;
  EditPassIdList lastOverdubSealCompanionIds_;
  /// Display overlay only. Appended across wraps; not used for merged-stream
  /// admission. Transitional bridge so frame can realize effective Hide/Shorten.
  EditPassIdList sealedCompanionOverlayIds_;

  uint32_t startLoopTick = 0;
  uint32_t loopLengthTicks = 0;
  uint32_t loopStartTick = 0;

  uint32_t lastTickInLoop = 0;
  uint16_t nextEventIndex = 0;
  bool playbackOrderDirty = true;

  mutable bool eventIndexValid = false;

  bool hasData() const {
    return hasCommittedPasses() || loopLengthTicks > 0 || !capture.store.empty();
  }

  bool hasPendingCapturePass() const { return hasPendingCapturePass_; }
  const PendingCapturePass& pendingCapturePass() const { return pendingCapturePass_; }

  /// Move the staged capture pass out of the loop producer into `out`, leaving the loop with no
  /// pending-pass binding (OpenSpec 5.12 / D8: a staged pass is owned by the interval that staged
  /// it, and a deferred job must not depend on loop-global producer state after its stop
  /// admission).
  ///
  /// Contract:
  ///   success -> `out` owns the complete receipt and its chunk-list storage; `hasPendingCapturePass()`
  ///              is false and nothing in the loop aliases the pass;
  ///   failure -> `out` is unchanged and the loop keeps its pending pass. The only failure is
  ///              "nothing is staged", so this is non-fallible once a pass exists.
  /// Non-allocating: the chunk list is moved by swap, never copied.
  /// The caller owns the moved chunk references and must either transfer them to a canonical pass
  /// or release them with `LoopEventStore::releaseChunkRefs`.
  bool takePendingCapturePass(PendingCapturePass& out);

  size_t activeCapturePassCount() const;

  bool hasCommittedPasses() const;
  uint32_t findLastCommittedEventTick() const;
  /// Bar-aligned span of committed pass MIDI (0 when no committed passes).
  uint32_t committedBarAlignedContentLengthTicks() const;
  /// When committed passes MIDI exists, never return a length below content-derived bars.
  uint32_t reconcileLoopLengthWithCommittedPasses(uint32_t candidateLengthTicks) const;

  void mergeActiveCapturePasses(MidiEventVec& out) const;
  void mergeActiveCapturePasses(SessionMidiEventVec& out) const;
  static void resetCommittedPitchQueryWork();
  static uint32_t committedEventsFullMaterializeCount();

  /// True when synchronous full visual-cache rebuild should be avoided (unbounded cost).
  bool shouldAvoidFullVisualRebuild(uint32_t loopLength) const;

  /// True when a whole-loop ("full take") playback view can actually become
  /// authoritative for this loop, which is what entitles a slot to reserve the
  /// whole-take playback representation (RC-MEM-2).
  ///
  /// Every full-take publisher requires an exactly full-loop coverage interval --
  /// `publishStoppedOracleView` / `publishStoppedCanonicalOracleView` reject
  /// `coverage.lengthTicks != coverage.loopLengthTicks` -- while the windowed policy never
  /// builds a full-loop window once `shouldAvoidFullVisualRebuild` holds. Past that point the
  /// three views' worth of windowed capacity is still needed (the live-record and
  /// handoff lanes use them), but the bridges and their immutable bases can never be
  /// published, so reserving them is dead reservation by construction.
  bool fullTakePlaybackViewAdmissible() const {
    return !shouldAvoidFullVisualRebuild(loopLengthTicks);
  }

  /// Current-root snapshot edit-view copy; clears `out` on miss (OpenSpec 6.3.6).
  void copyEffectiveCommittedEvents(SessionMidiEventVec& out) const;
  /// Range query of committed content (chunk window + optional edit projection).
  /// Active Note rows apply per committed pass layer, then layers merge.
  /// Does not flatten the loop. Companion fast path copies a ready Overdub
  /// projection cache only; a miss uses the windowed apply path. Full companion
  /// rebuild is not admitted from this gather.
  void copyEffectiveCommittedEventsInRange(SessionMidiEventVec& out, uint32_t windowStart,
                                           uint32_t windowLength,
                                           bool includeActiveEditRows = true) const;
  /// Committed window plus live capture.store events in the same window.
  void copyEffectiveCommittedEventsInWindowWithCapture(SessionMidiEventVec& out,
                                                       uint32_t windowStart,
                                                       uint32_t windowLength) const;
  /// Pass replay flatten plus live capture.store merge (playback during record/overdub).
  void copyEffectiveCommittedEventsWithCapture(SessionMidiEventVec& flat) const;
  void copyEffectiveCommittedEventsWithCapture(MidiEventVec& flat) const;
  void rematerializeEditView(LoopEventStore& store) const;
  bool tryCopyCurrentSnapshotEditViewEvents(SessionMidiEventVec& out) const;
  /// Current-root edit-view copy with a single publication observation: on
  /// success `observedIdentity` is the snapshot pointer used for the copy.
  bool tryCopyCurrentSnapshotEditViewEvents(SessionMidiEventVec& out,
                                            const void*& observedIdentity) const;
  /// Windowed display fallback: current-root span notes in `[windowStart,
  /// windowStart + windowLength)`, else false.
  bool tryCopyCurrentSnapshotDisplayNotesInWindow(NoteUtils::DisplayNoteVec& out,
                                                  uint32_t windowStart,
                                                  uint32_t windowLength) const;
  /// Idle validate source: current-root span-filtered snapshot events, else
  /// `mergeActiveCapturePasses`.
  void copyIdleValidateEvents(MidiEventVec& out) const;
  bool tryCopyNoteEditOpenedSnapshotEvents(bool openedOnCurrentSnapshot,
                                           uint32_t openedSnapshotRevision,
                                           SessionMidiEventVec& out) const;
  bool tryCopyNoteEditOpenedSnapshotEvents(bool openedOnCurrentSnapshot,
                                           uint32_t openedSnapshotRevision,
                                           MidiEventVec& out) const;
  void copyNoteEditCommitBase(const EditPassIdList& sessionEditPassIds, SessionMidiEventVec& out,
                              bool openedOnCurrentSnapshot = false,
                              uint32_t openedSnapshotRevision = 0) const;
  void copyNoteEditCommitBase(const EditPassIdList& sessionEditPassIds, MidiEventVec& out,
                              bool openedOnCurrentSnapshot = false,
                              uint32_t openedSnapshotRevision = 0) const;

  EditPassId saveNoteEditPass(uint8_t editPassIndex, EditPass row,
                              EditPassType passType = EditPassType::Note,
                              bool deferDerivedInvalidate = false,
                              bool skipHeapReserveCheck = false);
  PassId saveLoopGeometry(LoopGeometry row);
  bool setLoopGeometryState(PassId id, LoopGeometryState state);
  EditPassIdList replaceNoteEditPass(uint8_t editPassIndex,
                                     const EditPassIdList& staleEditPassIds,
                                     EditPassVec rows);

  void disableEditPasses(const EditPassIdList& ids);
  void enableEditPasses(const EditPassIdList& ids);
  void materializeExcludingEditPassIds(const EditPassIdList& excludeIds,
                                       MidiEventVec& out) const;
  void materializeExcludingEditPassIds(const EditPassIdList& excludeIds,
                                       SessionMidiEventVec& out) const;

  void markEditStateDirty() { editStateDirty_ = true; }
  bool isEditStateDirty() const { return editStateDirty_; }
  void clearEditStateDirty() { editStateDirty_ = false; }

  LoopSnapshotRef sharePassesSnapshot() const;
  /// SD load: move chunk refs from snapshot into live passes (snapshot consumed).
  void adoptPersistedSnapshot(PersistedLoopSnapshot& snapshot);
  /// Undo restore: deep-clone passes so live loop does not alias snapshot chunks.
  void restorePassesSnapshot(const PersistedLoopSnapshot& snapshot);

  void beginCapture(CapturePhase phase, uint32_t playheadPhaseTick = 0);
  void discardCapture();
  /// Stop worker only: retire one copied raw-input chunk, preserving preview/session.
  bool retireCopiedCaptureSourceGrain(bool closeCapture = false);

  /// Readable capture material. The producer and the job-owned finalizing source
  /// are exposed separately so consumers are not coupled to the currently
  /// guaranteed mutual exclusion: a successor interval can only begin while the
  /// admitted job still owns its sealed range.
  struct CaptureReadSource {
    const LoopEventStore* producer = nullptr;
    const LoopEventStore::CaptureRange* finalizing = nullptr;
    uint64_t finalizingGeneration = 0;
    bool producerHasRows() const { return producer != nullptr && !producer->empty(); }
    bool finalizingReady() const { return finalizing != nullptr && finalizing->ready(); }
    bool any() const { return producerHasRows() || finalizingReady(); }
  };
  CaptureReadSource captureReadSource() const;
  /// Job-identified non-owning borrow of an admitted stop job's sealed raw
  /// capture extent. `owner` is that job's admission identity. Loop consumers may
  /// read the borrowed range but must never mutate, reorder, clear or release it.
  bool borrowSealedCaptureRange(const LoopEventStore::CaptureRange& range, const void* owner);
  /// Freeze the producer's content receipt with an admitted job-owned extent.
  /// A failed cut leaves `frozenContentGeneration` and producer currency intact.
  bool sealCaptureRangeForJob(LoopEventStore::CaptureRange& range,
                              uint64_t& frozenContentGeneration);
  /// Clears the borrow only when it still belongs to `owner`, so a stale job can
  /// never drop a successor job's binding.
  bool releaseSealedCaptureRangeBorrow(const void* owner);
  bool hasSealedCaptureBorrow() const { return borrowedSealedCapture_ != nullptr; }
  /// Sorted scratch copy of the readable capture interval. Never mutates a
  /// borrowed sealed range: ordering is applied to the output only.
  bool copyCaptureIntervalEvents(SessionMidiEventVec& out) const;
  /// Last captured NoteOn identity for a pitch. Resolves against the finalizing
  /// (sealed) interval first and the producer second, so a physical NoteOff for a
  /// reservation created before sealing still resolves its NoteId. No allocation.
  bool findLastCaptureNoteOnIdentity(uint8_t channel, uint8_t note, NoteId& out) const;
  /// Closes the producer phase after a job-owned range has been fully retired.
  void closeProducerCapturePhase();
  /// Overdub session start S (`beginCapture` playheadPhaseTick). UINT32_MAX = no session.
  uint32_t playheadPhaseTick = UINT32_MAX;
  /// Open this interval's session. `forceNew` (DEC-068) starts a fresh session even while a
  /// predecessor's tail is still draining, so a successor never joins the predecessor's
  /// session state; the predecessor's identities live in its captured receipt.
  void openOverdubSession(uint32_t sessionPlayheadPhaseTick, bool forceNew = false);
  void closeOverdubSession();
  /// Post-publication cleanup under exclusive protected-stop ownership. Snapshot
  /// exit work must already be retained; this never discards an owned graph.
  struct SessionClose {
    enum class Phase : uint8_t { Idle, Passes, Companions, LiveEvents, SourceEvents,
      SourceNotes, Finish, Complete, Denied };
    const Loop* owner = nullptr;
    uint64_t generation = 0;
    size_t units = 0;
    Phase phase = Phase::Idle;
  };
  bool beginCloseOverdubSession(SessionClose& work);
  SessionClose::Phase stepCloseOverdubSession(SessionClose& work, size_t quantum);
  void syncNextOverdubSessionIndexFromPasses();
  bool hasOverdubSession() const { return playheadPhaseTick != UINT32_MAX; }
  /// --- Touched coverage (design D3, task 4.7): **marks only**, no derivation, no allocation. --------
  /// NoteOn marks the pressed tile. Held advancement marks each newly crossed tile. A completed span
  /// seals every tile it touches, including the wrap case. All three are O(tiles touched) and return the
  /// number of newly required tiles (0 in the common "already required" case).
  uint8_t markOverdubCoverageNoteOn(uint32_t phaseTick);
  uint8_t markOverdubCoverageHeldAdvance(uint32_t phaseTick);
  /// `endTick` is exclusive-in-loop: a wrapped note (end before start) is measured through the loop end.
  uint8_t markOverdubCoverageSpan(uint32_t startTick, uint32_t endTick);
  bool overdubCoverageSessionActive() const { return overdubCoverage_.sessionActive; }
  /// True when the tile holding `phaseTick` is prepared for the current source generation.
  bool overdubCoveragePhasePrepared(uint32_t phaseTick) const;
  /// True when every tile the span touches is prepared for the current source generation.
  bool overdubCoverageSpanPrepared(uint32_t startTick, uint32_t lengthTicks) const;

  /// **Worker turn** (task 4.6/4.7): advance one declared grain of the next owed tile and publish it
  /// atomically when its derive completes. Returns what the turn did, so a caller can report "nothing
  /// owed" separately from "advanced" and "published". Derives nothing else and allocates nothing.
  enum class OverdubCoverageTurn : uint8_t { Idle, Advanced, Completed };
  OverdubCoverageTurn serviceOverdubCoverageTurn(size_t eventBudget);
  uint16_t overdubCoveragePreparedCountForTest() const {
    return static_cast<uint16_t>(CoveragePrerequisite::popcount64(overdubCoverage_.prepared));
  }
  uint8_t overdubCoverageOwedCountForTest() const { return overdubCoverage_.owedCount(); }
  /// DEC-237 Slice 3: the coverage worker still owes a tile. The display producer is served only when
  /// this is false, so coverage correctness never waits behind display preparation.
  bool hasOwedOverdubCoverage() const { return overdubCoverage_.owedCount() != 0; }
  /// DEC-237 3c1: the earliest consumer horizon among owed coverage completions, or UINT32_MAX when no
  /// owed completion has a closed span yet (occupancy-only holds are not urgent until their note-off).
  /// The due tick comes from the consumer that created the completion, never from the scheduler.
  uint32_t overdubCoverageEarliestDueTick() const;
  /// DEC-237 3c1: true when an owed completion's due tick is within `leadTicks` of the current loop
  /// phase. The scheduler lets coverage win this owner opportunity; otherwise the staged display grain
  /// may take it. A completion with no closed span is not urgent; the seal deadline remains its backstop.
  bool overdubCoverageRequiredNow(uint32_t currentPhase, uint32_t leadTicks) const;
  uint8_t overdubCoverageDeferredCompletionsForTest() const {
    return overdubCoverageDeferredCount_;
  }
  uint32_t overdubCoverageClosureTicksForTest() const { return overdubCoverageClosureTicks_; }
  void armOverdubWrapAfterLeavingStart(uint32_t currentPhase);
#if defined(PIO_UNIT_TEST_NATIVE)
  /// Test-only witnesses for the wrap diagnostic; no target path reads them.
  bool overdubWrapArmedForTest() const { return overdubWrapArmed_; }
  bool overdubWrapSuppressNextForTest() const { return overdubWrapSuppressNext_; }
#endif
  /// True when armed and the playhead crosses loop end (phase 0). S is not the seal clock.
  bool shouldCommitOverdubWrap(uint32_t prevPhase, uint32_t currentPhase) const;
  void noteOverdubWrapCommitted();
  void suppressNextOverdubWrap();
  bool consumeSuppressedOverdubWrapCrossing(uint32_t prevPhase, uint32_t currentPhase);
  /// Move unpaired capture NoteOns out of `capture.store` (6E.5). Returns extracted count.
  size_t extractOpenCaptureNoteOns(SessionMidiEventVec& out);
  void pushOverdubSessionPass(PassId passId, EditPassIdList companionIds);
  /// Sealed wraps still on the session cursor (not the undone redo tail).
  void collectOverdubSessionUndoPasses(PassIdList& passIds, EditPassIdList& companionIds) const;
  /// DEC-068/DEC-088: this interval's **own** session identities in `collectOverdubSessionUndoPasses`
  /// shape, read from its session-exit receipt (`sessionExitReceiptByRingIndex`) instead of the shared
  /// live session, which after reentry belongs to a successor. The caller owns the final-pass
  /// invariant: a receipt captured at stop admission predates its own final pass, so the caller
  /// appends it. False when no receipt is staged for that ring index, or it names no pass identity.
  bool gatherSessionExitReceiptUndoIdentities(size_t ringIndex, PassIdList& passIds,
                                             EditPassIdList& companionIds) const;
  struct SessionUndoCollection {
    enum class Phase : uint8_t { Cold, Pass, Companions, FinalPass, FinalCompanions, Complete, Denied };
    const Loop* owner = nullptr;
    uint64_t generation = 0;
    PassIdList* passIds = nullptr;
    EditPassIdList* companionIds = nullptr;
    size_t limit = 0, row = 0, companion = 0, passCount = 0, companionCount = 0;
    uint64_t receiptGeneration = 0;
    size_t receiptCount = 0, receiptIndex = 0;
    PassId finalPassId = kInvalidPassId;
    bool finalReceipt = false, includeFinalPass = false;
    Phase phase = Phase::Cold;
    /// DEC-068: a deferred stop's wrap identities were swapped into its own session-exit receipt
    /// at admission, so the Pass/Companions phases read that receipt instead of the shared live
    /// session lists, which after reentry belong to a successor. Pointers index-aligned with the
    /// receipt's `passIds`/`companionIds`; `receiptIdentity` also replaces the live
    /// `overdubSessionUndoGeneration_` guard, because a successor opening its session must not
    /// deny this interval's own identity collection.
    const PassId* receiptPassIds = nullptr;
    size_t receiptPassIdCount = 0;
    const EditPassIdList* receiptCompanionRows = nullptr;
    size_t receiptCompanionRowCount = 0;
    const void* receiptIdentity = nullptr;
  };
  /// Caller supplies empty, pre-reserved private outputs and keeps exclusive
  /// access until completion/discard. No clearing, allocation or canonical mutation.
  bool beginCollectOverdubSessionUndoPasses(SessionUndoCollection& work,
      PassIdList& passIds, EditPassIdList& companionIds,
      const SessionExitSnapshotHandoff* receipt = nullptr) const;
  SessionUndoCollection::Phase stepCollectOverdubSessionUndoPasses(
      SessionUndoCollection& work, size_t quantum) const;
  bool collectedOverdubSessionUndoPassesCurrent(const SessionUndoCollection& work) const;
  static uint64_t requiredSessionUndoCollectionGrains(size_t rows, size_t companions, size_t quantum);
  /// Adds the final capture ID (unless already last) and the current prepared
  /// companion receipt. Output remains private; does not consume/recycle receipt.
  /// `sessionExitReceiptRing` (DEC-068) selects the interval's own receipt as the
  /// identity source; `kInvalidSessionExitReceipt` keeps the live-session source.
  bool beginCollectOverdubStopUndoPasses(SessionUndoCollection& work, PassIdList& passIds,
      EditPassIdList& companionIds, PassId finalPassId, bool includeFinalPass,
      uint64_t owner = 0, size_t sessionExitReceiptRing = kInvalidSessionExitReceipt);
  static uint64_t requiredSessionStopUndoCollectionGrains(size_t rows, size_t companions,
      size_t finalCompanions, size_t quantum);
  /// Last sealed session layer still on the cursor. False when the cursor is empty.
  bool collectLastOverdubSessionPass(PassId& passId, EditPassIdList& companionIds) const;
  void dropOverdubSessionRedoTail();
  bool undoOverdubSession();
  bool redoOverdubSession();
  bool canUndoOverdubSession() const;
  bool canRedoOverdubSession() const;
  /// Sealed wraps on the cursor. Live `capture.store` is not a session layer.
  size_t overdubSessionUndoDepth() const;
  /// Task 3.14: the pass a **session undo/redo step just changed**, and the companion edit batches it changed with it.
  /// The cursor is what names it, and the two directions read it at opposite moments: `undoOverdubSession` decrements
  /// first, so a step's pass sits at `overdubSessionUndoDepth()`; `redoOverdubSession` reads before incrementing, so a
  /// redo step's pass sits at `overdubSessionUndoDepth() - 1`. Out-of-range answers are named (`kInvalidPassId` / empty)
  /// rather than clamped to a neighbouring step.
  PassId overdubSessionPassIdAt(size_t index) const {
    return index < overdubSessionPassIds_.size() ? overdubSessionPassIds_[index] : kInvalidPassId;
  }
  const EditPassIdList& overdubSessionCompanionIdsAt(size_t index) const {
    static const EditPassIdList kNoCompanions{};
    return index < overdubSessionCompanionIds_.size() ? overdubSessionCompanionIds_[index] : kNoCompanions;
  }
  /// Task 4.20 — the temporary per-overdub-session ID lists, measured by **size *and* capacity**, including the
  /// nested companion lists' capacities. A `clear()` empties the logical list while keeping every nested
  /// internal-heap allocation, so `size` alone cannot answer whether a settled session released anything. The
  /// contract these fields are judged against: they describe only the currently live/finalizing session, so once
  /// its result has transferred into the durable committed/undo representation they must not keep growing.
  struct OverdubSessionRetention {
    size_t passCount = 0;
    size_t passCapacity = 0;
    size_t companionCount = 0;
    size_t companionCapacity = 0;
    /// Sum of the nested `EditPassIdList` capacities — the internal-heap bytes a `clear()` would keep.
    size_t companionNestedCapacity = 0;
    size_t cursor = 0;
  };
  OverdubSessionRetention overdubSessionRetention() const;
  /// Sidebar `O:` — current wrap (`overdubSessionUndoDepth() + 1` while the session is open).
  size_t overdubSessionDisplayDepth() const;
  size_t overdubSessionRedoDepth() const;
  /// Session-start source view: reset overlap-hold totals, then `rebuildOverdubSourceView`
  /// (`why=open`), then clear pending. Not visual cache.
  void establishOverdubSourceView(uint32_t playheadPhaseTick);
  /// Source/hold resolution breadth in bars. Independent of
  /// `DisplayWindowUtils::kMaxDetailedWindowBars`. Experiment 1 rebuilds this
  /// value (1/2/4/8/16); a timing-passing length is evidence, not policy.
  static constexpr uint32_t kOverdubSourceWindowBars = 16;
  /// Rebuild source notes from the span cache (`overdubSourceSpanCacheNotes_`)
  /// using the source window filter. A current published `LoopSnapshot` is the
  /// committed source; the span cache is a stamped materialization of that
  /// root. Reuse requires `overdubSourceSpanCacheReusable()`. Without a current
  /// root, fill stays prepared → prior → covering visual → passes (DEC-052
  /// fallback). Not visual-cache ownership. `why` is CAP `open` / `wrap` /
  /// `undo` / `redo` / `idle`.
  void rebuildOverdubSourceView(uint32_t playheadPhaseTick, const char* why = "wrap");
  /// Append display notes from `lastCommittedPassId()` chunks into the existing
  /// source view and restamp the span cache to the current `playbackRevision`.
  /// Does not merge that pass into `visualCache` (205138 wrap-2 205→271).
  /// Walks only that pass. Source-view merge skips NoteIds already present.
  /// Wrap-held heads/tails stay on wrap-edge display append. Idle slice is
  /// the visualCache fill for new wrap body notes.
  void appendLastCommittedOverdubPassToSourceView();
  /// Append candidates missing from `visualCache.notes` by valid `NoteId`
  /// (pitch+start only for invalid-ID rows). Same-ID geometry is replaced.
  void mergeDisplayNotesIntoVisualCache(const NoteUtils::DisplayNoteVec& candidates);
  /// Task 7.1: bounded effective-display derive for the stored-content verification dump. Resolves
  /// one requested window through the same one-bar padded source window the session view uses.
  void resolveEffectiveDisplayNotesInWindow(uint32_t requestedStart, uint32_t requestedLength,
                                            NoteUtils::DisplayNoteVec& out) const;
  /// D2 note-off consume: just-in-time merge of this pitch from the source/hold
  /// cache (`overdubSourceSpanCacheNotes_`) into the session source view.
  /// Skips noteIds already present. Optional `newlyMergedPitchNotes` receives
  /// only those new rows. `presentAtHoldOnly` keeps notes present at the hold
  /// tick (not ahead notes in the same window). Note-on occupy does not call
  /// this (Phase 3). Empty occupy still walks source-view notes.
  void ensureOverdubSourceNotesForHold(uint32_t holdPhaseTick, uint8_t pitch,
                                       NoteUtils::DisplayNoteVec* newlyMergedPitchNotes = nullptr,
                                       bool presentAtHoldOnly = false);
  /// RC8 gold (A): NoteIds in `overdubSourceViewNotes` present at the hold tick for pitch.
  /// Does not fill the source window. Clears `out` then inserts.
  void collectOverdubSourceHoldParticipantIds(uint32_t holdPhaseTick, uint8_t pitch,
                                              OverlapNoteIdSet& out) const;
  /// Prepared present-at-S NoteIds for pitch (B). RC8 hold on prepared `NoteSpan`s.
  /// Returns false on prepared miss or live loop-length mismatch. Never
  /// `resolveWindow` / cold `resolveState`. Clears `out`.
  bool tryCollectPreparedPresentNoteIdsAtTick(uint32_t tick, uint8_t pitch,
                                              OverlapNoteIdSet& out) const;
  /// Note-on occupy: every open NoteId on `(channel, pitch)` via `forEachActive`.
  /// Never `ledger.noteId()` (compatibility newest-only). Never
  /// `ensureOverdubSourceNotesForHold` / `resolveWindow`. Clears `out`.
  void collectOverdubNoteOnParticipantIds(uint8_t pitch, uint8_t channel,
                                          const ActiveNoteLedger& ledger,
                                          OverlapNoteIdSet& out) const;
  /// Session end / discard. Wrap and stop commit keep the view while the session is open.
  void clearOverdubSourceView();
  /// Build source span cache outside overdub entry when the loop is idle/clean.
  /// No-op when `overdubSourceSpanCacheReusable()` or when capture/session
  /// state is active.
  void prewarmOverdubSourceSpanCache();
  bool hasOverdubSourceView() const { return overdubSourceViewEstablished_; }
  uint32_t overdubSourceViewLoopLengthTicks() const { return overdubSourceViewLoopLengthTicks_; }
  const SessionMidiEventVec& overdubSourceViewEvents() const { return overdubSourceViewEvents_; }
  const NoteUtils::DisplayNoteVec& overdubSourceViewNotes() const { return overdubSourceViewNotes_; }
  /// Bind the identity-existence predicate to a complete full-loop committed
  /// playback stream. A partial window unbinds so the filter cannot declare
  /// identities nonexistent. Span geometry stays; identified source-view notes
  /// whose NoteOn is absent are dropped from the source-view copy only.
  void replaceCommittedPlaybackNoteOnIdentities(const PlaybackMergedMidiEvents& merged);
  void clearCommittedPlaybackNoteOnIdentities();
  bool overdubSourceSpanCacheReady() const;  /// Ready storage that may be reused for the current committed source.
  /// A current published snapshot requires matching published-revision **and**
  /// published-identity stamps (OpenSpec 4.6 Stage 3).
  bool overdubSourceSpanCacheReusable() const;
  /// **Window-aware reuse** (design D3/D4). Authoritative for `windowStart..+windowLength` only while
  /// the cache's own coverage contains that interval and its identity is current. Both consumers -- the
  /// wrap/idle refresh and the note-hold merge -- ask this, so neither re-derives content it holds nor
  /// serves content it does not.
  bool overdubSourceSpanCacheCoversWindow(uint32_t windowStart, uint32_t windowLength) const;  /// CONTRACTS.md C3.1 -- the qualified origins of the overdub source span cache. Exactly one
  /// origin is **non-committed**: `PrivatePredecessor` is a sealed Record custody extent that has
  /// not published. Its rows may satisfy the overlap/hold participant query, and they must never be
  /// read as committed currency: no root or `pass` reader, no save, no `Snapshot`/`Prepared`
  /// attribution. The pre-existing origins keep their literals unchanged.
  enum class SourceOrigin : uint8_t {
    None,
    Snapshot,
    Prepared,
    Prior,
    Visual,
    Passes,
    PrivatePredecessor,
  };
  static const char* sourceOriginName(SourceOrigin origin);
  SourceOrigin overdubSourceOrigin() const { return overdubSourceSpanCacheOrigin_; }
  /// C3.2: one grain admits at most this many units, where one unit is exactly one sealed-range
  /// row read, one pass-local pair decision, or one pending-change upsert. It is the contract's
  /// named `kDeferredStopPreparationQuantum` -- `Config::DEFERRED_STOP_PREPARATION_QUANTUM`, one
  /// definition, so the two lanes cannot drift apart.
  static constexpr size_t kDeferredStopPreparationQuantum = Config::DEFERRED_STOP_PREPARATION_QUANTUM;
  static constexpr size_t kPrivateSourceQuantum = kDeferredStopPreparationQuantum;
  /// C3.4's deadline edge: the built source is required before this interval's seal, so the seal
  /// drives the materialization itself (bounded by the hold capacity in declared grains) and denies
  /// by name only what is still unreachable. Never a silent seal without the decision.
  void materializeDeferredPrivateHoldsAtDeadline();
  /// The fixed open-pair table the scan may touch in one grain.
  static constexpr size_t kPrivateSourceOpenPairCapacity = 16;
  /// C3.4: retained hold intents a deferred materialization may owe at once. A hold that cannot
  /// be retained is denied by name instead of silently dropped.
  static constexpr size_t kPrivateSourceHoldCapacity = 4;
  /// The Track-owned custody receipt this scan reads, supplied by the receipt's owner. The Loop
  /// holds no Track type: the receipt's stable address is the identity and its frozen E2 content
  /// generation is the supersession stamp.
  struct PrivatePredecessorBinding {
    const LoopEventStore::CaptureRange* range = nullptr;
    const void* receipt = nullptr;
    LoopId loopId = kInvalidLoopId;
    uint64_t interval = 0;
    uint64_t captureContentGeneration = 0;
  };
  enum class PrivateSourceStep : uint8_t { Idle, Pending, Complete, Pressure, Denied };
  enum class PrivateSourceDeny : uint8_t {
    None,
    IdentityChanged,
    SourceUnavailable,
    PairCapacity,
    OutputUnavailable,
    HoldCapacity,
  };
  static const char* privateSourceDenyName(PrivateSourceDeny deny);
  /// One bounded grain of private-predecessor preparation. Never blocks, never allocates more
  /// than one declared staging increment, and never writes `passes`, a root or `playbackRevision`.
  PrivateSourceStep stepPrivatePredecessorSource(const PrivatePredecessorBinding& binding,
                                                 size_t quantum = kPrivateSourceQuantum);
  /// C3.5: clear, slot switch, load, transport stop, record re-entry -- and the predecessor's own
  /// publication -- drop the cursor and every row it staged, so a private prefix can never be
  /// mixed with a committed suffix.
  void discardPrivatePredecessorSource();
  bool privatePredecessorSourcePending() const;
  bool privatePredecessorSourceComplete() const;
  size_t privateSourceLastUnits() const { return privateSource_.unitsLast; }
  PrivateSourceDeny privateSourceDeny() const { return privateSource_.deny; }
  size_t privateSourceStagedRows() const { return privateSource_.rowsRead; }
  size_t privateSourceDeferredHolds() const { return privateHoldCount_; }
  /// C3.4: the built private source is required before this interval's
  /// `sealPendingNoteChangesToEditPasses`. While it is owed the seal applies committed content
  /// only and reports the denial by name: losing a decision is a defect, applying a wrong one is
  /// worse.
  /// C3.4's seal deadline. A pending scan is owed, and so is a **retained hold**: a supersession
  /// drops the scan but keeps the intents, and they are materialized against the committed origin.
  /// Reading only the cursor phase here sealed B without A's transform while holding A's decision.
  bool privateSourceOwedForSeal() const {
    return privatePredecessorSourcePending() || privateHoldCount_ > 0;
  }
  /// Sticky: set when the seal found the source owed, or when a dependent intent could not be
  /// retained. Only the session's own cancellation clears it.
  bool privateSourceSealDenied() const { return privateSourceSealDenied_; }
  /// C3.1: qualify the session's private predecessor without spending a unit of work.
  bool bindPrivatePredecessorSourceOnly(const PrivatePredecessorBinding& binding);
  /// The outcome of the most recent grain. It survives a `discard` on purpose: a denial names
  /// itself to the caller that provoked it.
  PrivateSourceStep privateSourceLastStep() const { return privateSourceLastStep_; }
  uint64_t privateSourceInterval() const { return privateSource_.interval; }
  size_t privateSourceStagedNotes() const { return privateSource_.notesStaged; }
#if defined(PIO_UNIT_TEST_NATIVE)
  void replaceOverdubSourceViewNotesForTest(NoteUtils::DisplayNoteVec notes);
  void pushPendingNoteChangeForTest(PendingNoteChange change);
  bool overdubSourceSpanCacheReadyForTest() const { return overdubSourceSpanCacheReady(); }
  bool overdubSourceSpanCacheReusableForTest() const {
    return overdubSourceSpanCacheReusable();
  }
  uint32_t overdubSourceSpanCachePublishedRevisionForTest() const {
    return overdubSourceSpanCachePublishedRevision_;
  }
  const void* overdubSourceSpanCachePublishedIdentityForTest() const {
    return overdubSourceSpanCachePublishedIdentity_;
  }
  const char* rebuildOverdubSourceSpanCacheForTest(uint32_t requestedWindowStart = 0,
                                                   uint32_t requestedWindowLength = 0) {
    const char* result =
        rebuildOverdubSourceSpanCache(requestedWindowStart, requestedWindowLength);
    // Touched coverage (tasks 4.7/4.8): the production writer is the tile worker. This seam stands in
    // for the worker having published every tile of a **whole-loop** rebuild, so older fixtures that ask
    // for a ready full-loop source keep testing their own subject.
    if (overdubCoverage_.sessionActive && loopLengthTicks > 0 &&
        requestedWindowLength >= loopLengthTicks) {
      uint32_t maxSpan = 0;
      for (const NoteUtils::DisplayNote& note : overdubSourceSpanCacheNotes_) {
        const uint32_t span = overdubSourceSpanOfNote(note);
        if (span > maxSpan) {
          maxSpan = span;
        }
      }
      overdubCoverageClosureTicks_ = maxSpan;
      advanceOverdubCoverageClosure(CoveragePrerequisite::tileLengthTicks());
      const uint16_t tiles = CoveragePrerequisite::tileCount(loopLengthTicks);
      for (uint16_t tile = 0; tile < tiles; ++tile) {
        overdubCoverage_.publishPrepared(tile);
      }
      overdubSourceSpanCacheFullLoop_ = true;
      overdubSourceSpanCacheCoverageStart_ = 0;
      overdubSourceSpanCacheCoverageLength_ = loopLengthTicks;
      recutOverdubSourceView();
    }
    return result;
  }
  const NoteUtils::DisplayNoteVec& overdubSourceSpanCacheNotesForTest() const {
    return overdubSourceSpanCacheNotes_;
  }
  /// Task 4.4: the touched-coverage state for this slot's overdub session.
  CoveragePrerequisite::CoverageState& overdubCoverageForTest() { return overdubCoverage_; }
  /// Task 4.4 seam: drive the committed-content edge so a fixture can assert that a generation change
  /// keeps required coverage and re-owes prepared coverage.
  void notifyCommittedContentChangedForTest() { notifyCommittedContentChanged(); }
  /// Task 4.3 seam: arm/read the span-cache derive's cost. Observation only.
  static void beginOverdubSpanDeriveMeasureForTest();
  static uint32_t overdubSpanDeriveOpsForTest();
  /// The live session's own pending set, not the admitted job's borrowed view. A fixture that runs
  /// two intervals in one slot needs to read the newer session's decisions.
  const PendingNoteChangeVec& livePendingNoteChangesForTest() const { return pendingNoteChanges_; }
  /// Release the span cache's retained capacity so the next staging increment is a real
  /// allocation: the fixture's only way to put the private scan's point-of-use reserve under test.
  void resetOverdubSourceSpanCacheCapacityForTest() {
    overdubSourceSpanCacheNotes_.clear();
    overdubSourceSpanCacheNotes_.shrink_to_fit();
  }
  /// Same, for the session view: the fixture's way to put the view's own staging increment under
  /// test (the store is otherwise already sized by earlier fixtures).
  void resetOverdubSourceViewCapacityForTest() {
    overdubSourceViewNotes_.clear();
    overdubSourceViewNotes_.shrink_to_fit();
  }
  /// Fault injection for the supersession class C3.5 must catch: a committed revision bump that does
  /// not travel through `notifyCommittedContentChanged` (the shape `publishPreparedCompanionHistory`
  /// had). The cursor must treat ANY revision move as an identity change on its next grain.
  void bumpPlaybackRevisionWithoutNotifyForTest() { ++playbackRevision; }
#endif

  /// Session pending logical delta (Add/Shorten/Hide) — not a timeline pass.
  void clearPendingNoteChanges();
  bool hasPendingNoteChanges() const {
    return !pendingNoteChanges().empty();
  }
  /// Readable pending-change set: the live session's own set, or the admitted
  /// stop job's owned set while that job borrows it. Read consumers (display
  /// geometry) must keep seeing the interval's changes after the live session
  /// has moved on; writers always use the live member directly.
  const PendingNoteChangeVec& pendingNoteChanges() const {
    return borrowedPendingChanges_ != nullptr ? *borrowedPendingChanges_ : pendingNoteChanges_;
  }
  /// Job-identified non-owning borrow of an admitted stop job's owned changes.
  bool borrowPendingNoteChanges(const PendingNoteChangeVec& changes, const void* owner);
  /// Clears the borrow only while it still identifies `owner`, so a stale job can
  /// never drop a successor's binding.
  bool releasePendingNoteChangesBorrow(const void* owner);
  /// Move the live pending-change set into job ownership. O(1) swap; the live
  /// session then owns the job's prewarmed empty vector for its successor.
  void swapPendingNoteChanges(PendingNoteChangeVec& other) {
    pendingNoteChanges_.swap(other);
    overdubCompletionOverflowCount_ = 0;  // owed rows travel with their set; the live session owes none
  }
  /// Readable capture preview for display consumers: the live session's preview,
  /// or the admitted stop job's owned preview while that job borrows it. Writers
  /// (capture ingress, wrap) keep using the `capturePreview` member directly.
  /// Out of line (flash) so 40+ display call sites do not inline the branch into
  /// ITCM, which would cost a whole 32 KB RAM1 bank.
  const CapturePreview& capturePreviewView() const;
  /// Job-identified non-owning borrow of an admitted stop job's owned preview.
  bool borrowCapturePreview(const CapturePreview& preview, const void* owner);
  /// Clears the borrow only while it still identifies `owner`.
  bool releaseCapturePreviewBorrow(const void* owner);
  /// Move the live capture preview into job ownership. Constant work: owned
  /// buffers are exchanged, never copied, and no allocation occurs.
  void swapCapturePreview(CapturePreview& other) {
    capturePreview.swapWith(other);
  }
  /// Resolve incoming note against hold-candidate ids, then geometry.
  /// Wrap-head off (`endTick < startTick`) occupies `[S, loopLength) ∪ [0, E)` as **one** hold.
  /// Empty `overlapNoteIds` skips id lookup and uses source-view geometric completion.
  /// Non-empty ids are completed geometrically and resolved by id lookup only.
  /// `occupancyResolvedNoteIds` skips identities already resolved at this hold's NoteOn.
  /// Returns false when no source view.
  bool accumulatePendingNoteChangesForIncomingNote(uint8_t channel, uint8_t pitch, uint8_t velocity,
                                                   uint32_t startTick, uint32_t endTick,
                                                   NoteId incomingNoteId,
                                                   const OverlapNoteIdSet& overlapNoteIds,
                                                   const OverlapNoteIdSet* occupancyResolvedNoteIds =
                                                       nullptr);
  /// Occupy On / playback cross: authoritative Shorten/Hide into `pendingNoteChanges_`.
  /// `A.startTick < B.startTick` Shortens A to `B.startTick - 1`; otherwise Hide.
  /// A ledger-selected note is transformed only when it is present at `holdPhaseTick`.
  /// Filter that set before geometric fallback (`134436` n=1 a=0 wrap remnant).
  /// When occupy ids are empty or none survive the filter, fall back to present-at-hold
  /// participants (`010926` norowid, `104129` ledger n=0).
  /// Resolved identities are written to `occupancyResolvedNoteIds` when non-null.
  void resolveOccupancyAtNoteOn(uint8_t channel, uint8_t pitch, uint8_t velocity,
                                const OverlapNoteIdSet& overlapNoteIds, uint32_t holdPhaseTick,
                                OverlapNoteIdSet* occupancyResolvedNoteIds = nullptr);
  /// Apply pending Shorten/Hide onto `visualCache.notes`.
  /// Used after idle slice / wrap append so wrap first paint is not raw committed.
  void applyPendingNoteChangesToVisualCacheNotes();
  /// Pair one linear incoming `[S, E)` against an explicit source-note list (Shorten/Hide only).
  void accumulatePendingNoteChangesFromSourceNotes(const NoteUtils::DisplayNoteVec& sourceNotes,
                                                   uint8_t channel, uint8_t pitch, uint8_t velocity,
                                                   uint32_t startTick, uint32_t endTick,
                                                   NoteId incomingNoteId);
  /// Encode pending Shorten/Hide into EditPass rows (call after OverdubPass publish).
  /// Clears pending. Occupancy transforms seal as Length / Delete companions.
  /// STOPPED-only allocation; total history rows and copied Create payloads
  /// must fit the fixed preparation horizons. Both vector sides are reserved;
  /// canonical history relocation advances its generation before moving rows.
  bool prewarmPreparedCompanionHistory(size_t companionCapacity);
  /// Upper bound on the companion rows one stop interval on this loop can produce, clamped
  /// to the architectural row ceiling. Every companion row is a Delete/Length transform
  /// keyed by a *source* note identity, so the count is bounded by the source notes the
  /// interval can occupy; canonical payload event count is a sound, generous proxy, with
  /// the open-note reservation capacity as slack for identities whose payloads were
  /// synthesized rather than stored. Cheap: one walk over the canonical rows, no
  /// allocation, safe at a cold admission point.
  size_t companionRowHorizonBound() const;
  static uint64_t requiredPreparedCompanionHistoryGrains(size_t historyRows, size_t historyPayloadEvents,
                                                        size_t companionCapacity, size_t quantum, size_t snapshotIds = 0,
                                                        size_t captureEvents = 0, size_t captureChunks = 0);
  /// Frozen row/capture owners must remain alive until logical discard.
  /// Optional captureInputs enables bounded verification of the staged sealed
  /// capture against its raw frozen order. Required for Track's joint handoff;
  /// the input owner must also remain alive and immutable until discard.
  /// `owner` is the admitted stop job that will consume this preparation (the
  /// live session path passes nullptr); it is the identity every accessor below
  /// requires while the receipt is live.
  /// `capturePass` (OpenSpec 5.12 / D8) is the staged pass the preparation verifies against.
  /// A deferred job passes its own job-owned receipt (`&job.pendingPass`) and never the Loop
  /// producer slot, which a successor interval may own. The default nullptr means "this caller
  /// names no receipt" -- the live producer path and fixtures -- and the Loop's own staged pass is
  /// then bound, the only receipt that exists there. When `captureInputs` is non-null the resolved
  /// pass must carry a valid id and chunks, so a deferred job that owns nothing is denied instead
  /// of silently binding a successor's receipt.
  bool beginPreparedCompanionHistory(const RuntimeCaptureCompanions& rows,
                                     const CaptureAdmissionStaging& capture,
                                     const RuntimeCanonicalPassInputs* captureInputs = nullptr,
                                     uint64_t owner = 0,
                                     PendingCapturePass* capturePass = nullptr);
  /// DEC-186 diagnostic: **which** guard would refuse `beginPreparedCompanionHistory` with these arguments.
  /// Same order, same predicates, no side effect; `None` means the call would be admitted. `begin` itself
  /// consults this, so a reported reason is the reason that actually fired.
  CompanionPreparationRefusal preparedCompanionHistoryBeginRefusal(
      const RuntimeCaptureCompanions& rows, const CaptureAdmissionStaging& capture,
      const RuntimeCanonicalPassInputs* captureInputs, uint64_t owner,
      PendingCapturePass* capturePass) const;
  /// DEC-186 diagnostic: why a job already in `CompanionHistory` made no progress this turn. Distinguishes
  /// an un-admittable horizon from a begun preparation whose pinned source moved.
  CompanionPreparationRefusal whyCompanionPreparationCannotAdvance(
      const RuntimeCaptureCompanions& rows, const CaptureAdmissionStaging& capture,
      const RuntimeCanonicalPassInputs* captureInputs, uint64_t owner,
      PendingCapturePass* capturePass) const;
  /// DEC-186 diagnostic: the single term of `preparedCompanionHistorySourceCurrent` that is false, in the
  /// same order as that predicate. `None` means the source is current.
  CompanionPreparationRefusal preparedCompanionHistorySourceRefusal() const;
  /// DEC-186 cause predicate: a **begun** preparation is pinned to a `playbackRevision` that has since moved.
  /// This is exactly `CompanionPreparationRefusal::SourceRevision` for an in-flight preparation, stated as one
  /// step so the escape cannot read a `Cold`/`Prewarmed` horizon as stale. Pure read.
  bool preparedCompanionHistoryRevisionMoved() const;
  /// Flash-resident name for the reason. Never allocates.
  static const char* companionPreparationRefusalName(CompanionPreparationRefusal refusal);
  /// One unit is row metadata, a payload/snapshot item, an unused tail row,
  /// or one staged-capture event/chunk-boundary comparison.
  /// `outRefusal` (DEC-186, diagnostic) names the predicate when this call refuses; it never changes the result.
  bool stepPreparedCompanionHistory(size_t quantum, uint64_t owner = 0,
                                    CompanionPreparationRefusal* outRefusal = nullptr);
  bool preparedCompanionHistoryReady(uint64_t owner = 0) const;
  bool preparedCompanionCaptureVerified(const RuntimeCanonicalPassInputs& inputs,
                                        uint64_t owner = 0) const;
  bool preparedCompanionHistoryCanStep(const RuntimeCaptureCompanions& rows,
      const CaptureAdmissionStaging& capture, const RuntimeCanonicalPassInputs& inputs,
      uint64_t owner = 0) const;
  bool preparedCompanionHistoryMatches(const RuntimeCaptureCompanions& rows,
                                       const CaptureAdmissionStaging& capture,
                                       uint64_t owner = 0) const;
  const EditPassVec* preparedCompanionHistory(uint64_t owner = 0) const;
  /// Canonical history half only: caller must preflight capture/audible handoff too.
  /// Retains retired history and the undo receipt until acknowledged recycling
  /// or STOPPED prewarm/discard. Reuse consumes the initially reserved row horizon.
  bool commitPreparedCompanionHistoryAtomic(uint64_t owner = 0);
  /// Joint canonical preflight. Audible source/epoch/ledger checks belong to Track.
  /// `pass` (OpenSpec 5.12 / D8) is the interval's staged receipt; a deferred job passes its
  /// job-owned pass, and nullptr means "resolve it from this preparation / the live producer slot".
  bool canCommitPreparedCaptureAndCompanions(uint64_t owner = 0,
                                             const PendingCapturePass* pass = nullptr) const;
  bool canCommitPreparedCaptureAndCompanionsSupersession(uint64_t owner = 0,
                                                         const PendingCapturePass* pass = nullptr) const;
  bool commitPreparedCaptureAndCompanionsAtomic(PreparedCapturePassTransfer& transferred,
                                               uint64_t owner = 0,
                                               PendingCapturePass* pass = nullptr);
  /// DEC-172 correction 3: the same atomic commit under the owner-supersession proof (the horizon's
  /// gate is `preparedCompanionHistorySupersessionPublishProof`, which tolerates a moved revision).
  bool commitPreparedCaptureAndCompanionsSupersession(PreparedCapturePassTransfer& transferred,
                                                      uint64_t owner = 0,
                                                      PendingCapturePass* pass = nullptr);
  void commitPreparedCaptureAndCompanionsBody(PreparedCapturePassTransfer& transferred,
                                              PendingCapturePass& staged, bool& transferFailed);
  const EditPassIdList* preparedCompanionHistoryIds(uint64_t owner = 0) const;
  /// Caller has consumed the ID receipt. Recycling ends that receipt's lifetime.
  /// No active allocation/free; returns to Prewarmed within the remaining horizon.
  bool beginRecyclePreparedCompanionHistory(uint64_t owner = 0);
  bool stepRecyclePreparedCompanionHistory(size_t quantum, uint64_t owner = 0);
  bool preparedCompanionHistoryRecycleComplete(uint64_t owner = 0) const;
  uint64_t preparedCompanionHistoryGeneration() const { return preparedCompanionHistory_.preparationGeneration; }
  bool preparedCompanionHistoryPrewarmed(size_t requiredCompanions = 0) const;
  /// DEC-172 correction 3: the superseded-commit publish proof. The prepared horizon copied the
  /// edit-pass store at `begin` and publishes by *exchanging* its buffered rows back, so the
  /// exchange loses nothing only while the store is contractually identical to what was copied
  /// (same size, same storage generation). Revision identity is deliberately NOT required --
  /// the whole point is to publish a completed interval derivation across a revision a
  /// record-return pass commit moved -- but a store that grew or was re-allocated since the copy
  /// fails this proof and keeps the superseded discard route.
  bool preparedCompanionHistoryStoreUnchanged(uint64_t owner = 0) const;
  const EditPassIdList* preparedCompanionHistorySupersessionIds(uint64_t owner = 0) const;
  /// DEC-172 correction 3: the full owner-supersession publish proof. Same address bindings as
  /// `preparedCompanionHistorySourceCurrent` **minus the revision/`firstId` identity** (a
  /// record-return pass commit may have moved those -- that is precisely the case it serves), plus
  /// the lossless-exchange state (store size and storage generation unchanged since the copy).
  bool preparedCompanionHistorySupersessionPublishProof(uint64_t owner = 0) const;
  static uint64_t requiredPreparedCompanionRecycleGrains(size_t rowCapacity,
      size_t retiredPayloadEvents, size_t companionIds, size_t snapshotIds, size_t quantum);
  /// True while this interval holds a preparation that is neither Cold nor back
  /// at its prewarmed horizon: the states a superseded publication must return to
  /// Prewarmed (by recycling) or discard.
  bool preparedCompanionHistoryInFlight(uint64_t owner = 0) const;
  /// Return an *unpublished* preparation to its prewarmed horizon after its
  /// publication was superseded. Allocation-free: the horizon was reserved when
  /// the preparation began, and every copied row/snapshot id is dropped and the
  /// horizon re-synced to the current canonical history, so the next interval can
  /// begin without a STOPPED-only prewarm. Refuses (leaving the preparation
  /// untouched) when the reserved storage no longer satisfies the horizon.
  bool restoreSupersededPreparedCompanionHistory(uint64_t owner = 0);
  void discardPreparedCompanionHistory(uint64_t owner = 0); // Logical discard only; retains storage.
  EditPassIdList sealPendingNoteChangesToEditPasses(
      bool updateOverdubSourceView = true);
  /// Merge pending Add/Shorten/Hide into `overdubSourceViewNotes_`. Wrap commit
  /// uses `appendLastCommittedOverdubPassToSourceView`. Full rebuild is idle /
  /// later, not the wrap clock tick. Does not clear pending.
  void applyPendingNoteChangesToOverdubSourceView();
  /// Apply NOTE_EDIT overlay Hide/Shorten onto `overdubSourceViewNotes_` by NoteId.
  /// Reuses occupy pending clip. Does not add occupy pending rows.
  void applyNoteEditCurrentStateToOverdubSourceView(const NoteEditCurrentState& currentState);
  /// Apply pending effective Hide/Shorten onto a display-note list. Not Add
  /// (live capture already has those). Hide matches `noteId` or same-pitch overlapping
  /// span on the committed prefix only — notes at `liveCaptureBegin` and after are
  /// playhead tails and must keep growing to currentTick. Does not mutate source view.
  /// Sealed companion Hide/Shorten still project while companion IDs exist, including
  /// after the selected representation reaches the seal revision, so a Hide target
  /// is not re-admitted at the same pitch+start. Overlay Hide or Shorten also
  /// drops a longer same-pitch+start closed sibling of the kept short row
  /// (`123236`, `000402`), including sealed-preview suffix remnants. Hide
  /// also drops a closed remnant at the hidden source pitch+start when the
  /// new occupy is still an open hold (`001145`). A later same-pitch occupy
  /// that starts earlier and covers an inner shortened row drops that inner
  /// (`002708`) only when that earlier row is the shortest keeper at its
  /// start (`003622`). Open capture holds are kept. Occupying Adds start at hold,
  /// not at the shortened source start. Sealed overlay applies first; this-lap
  /// pending then wins for the same NoteId (`002200`). Hide absorbs an earlier
  /// Length of the same NoteId. `liveCaptureBegin` remains Hide-by-overlap
  /// prefix only. Hide-by-overlap does not apply to a current closed
  /// `capturePreview` identity (`204112`). A current closed `capturePreview`
  /// identity is not remnant-eligible (`185822`). Gather upsert during
  /// capture is unchanged (`150650`).
  void applyPendingNoteChangesToDisplayNotes(
      NoteUtils::DisplayNoteVec& notes,
      size_t liveCaptureBegin = static_cast<size_t>(-1)) const;
  /// True when display still has sealed companion overlay IDs to project.
  bool hasSealedCompanionOverlay() const;
  const EditPassIdList& sealedCompanionOverlayIds() const;
  /// True when the selected committed representation is still behind the last seal.
  bool sealedCompanionOverlayRequired() const;
  const OverlapHoldTotals& overlapHoldTotals() const { return overlapHoldTotals_; }
  /// Derive `overlapHoldTotals_` add/shorten/hide from current `pendingNoteChanges_`.
  /// Same occupancy classification as On/Off. Diagnostic accounting only.
  void recountOverlapHoldTotalsFromPending();
  /// One `#CAP,DIAG,oh` (`add`/`shorten`/`hide` last). Observational only — wrap
  /// seal and overdub-stop seal. SESSION_CAPTURE / FLASHMEM only.
  void emitOverlapHoldTotals() const;

  CaptureAppendResult appendCaptureEventWithResult(const MidiEvent& evt);
  bool appendCaptureEvent(const MidiEvent& evt);
  /// Remove the open capture note-on for channel/note (overdub overlap restore on stop).
  bool removeOpenCaptureNoteOn(uint8_t channel, uint8_t note);
  /// Returns true when the active capture contains a NoteOff for (channel,note) after onTick.
  bool captureHasNoteOffAfter(uint8_t channel, uint8_t note, uint32_t onTick) const;
  CommitResult commitCapturePass(CommitReason reason, uint32_t sealedAtTick);
  bool setCapturePassState(PassId id, CapturePassState state);
  void resetPassTimeline();
  PassId lastCommittedPassId() const { return lastCommittedPassId_; }
  /// One-shot overdub-seal receipt for the next incremental merged-stream admission.
  /// `priorPlaybackRevision` is `playbackRevision` before `commitCapturePass`.
  /// Consumed by `tryAdmitLastOverdubSealIntoMergedEvents` or discarded on full gather.
  void recordLastOverdubSeal(PassId passId, const EditPassIdList& companionIds,
                             uint32_t priorPlaybackRevision);
  void clearLastOverdubSeal();
  /// Drops the one-shot merged-stream admit receipt only. Last-wrap companion
  /// IDs, overlay IDs, and `lastOverdubSealPlaybackRevision_` stay until
  /// `clearLastOverdubSeal()`. Snapshot-current does not release a Hide that
  /// is still present at the same pitch+start.
  void clearLastOverdubSealAdmitReceipt();
  /// True while the one-shot seal admit receipt is pending. Task 2.4 slice 2 reads
  /// this on the clock path as a non-allocating "the merged window's source changed"
  /// signal, so a pair request is raised instead of a synchronous order rebuild.
  bool hasPendingOverdubSealAdmission() const { return hasLastOverdubSeal_; }
  /// Admit `recordLastOverdubSeal` into an existing full-loop merged stream. Returns
  /// true only when the receipt, pass, companions, and geometry are all proven.
  /// Consumes or discards the receipt. Does not gather committed passes.
  bool tryAdmitLastOverdubSealIntoMergedEvents(PlaybackMergedMidiEvents& merged);
#if defined(PIO_UNIT_TEST_NATIVE)
  bool hasLastOverdubSealForTest() const { return hasLastOverdubSeal_; }
  uint32_t lastOverdubSealPlaybackRevisionForTest() const {
    return lastOverdubSealPlaybackRevision_;
  }
  const EditPassIdList& lastOverdubSealCompanionIdsForTest() const {
    return lastOverdubSealCompanionIds_;
  }
  const EditPassIdList& sealedCompanionOverlayIdsForTest() const {
    return sealedCompanionOverlayIds_;
  }
  void storePublishedSnapshotForTest(ImmutableLoopSnapshotRef snapshot, uint32_t revision,
                                     uint32_t coveredPassRevision = kSnapshotCoverageCurrent) {
    // DEC-073: an explicit revision stamp that is older than the live one is evidence the caller is
    // installing a root for an earlier state; without explicit coverage that root is not current.
    if (coveredPassRevision == kSnapshotCoverageCurrent && revision != 0 &&
        revision != playbackRevision) {
      coveredPassRevision = kSnapshotCoverageUnset;
    }
    storePublishedSnapshot(std::move(snapshot), revision, coveredPassRevision);
  }
  const LoopSnapshotChangeSet* pendingWrapJournalChangesForTest() const;
  /// Wrap-publication REDs: snapshot-change translations by any producer (pending row, EditPass row, pass event),
  /// and an injection that makes the next wrap's publication obligation fail to queue.
  static uint32_t snapshotChangeTranslationsForTest();
  static void failNextWrapPublicationObligationForTest();
  /// Task 6.4: the box's enumerated pass queue and coverage watermark, so a fixture can prove receipt
  /// coalescing enumerates exactly what sequential consumption does.
  std::vector<PassId> pendingSnapshotQueuedPassIdsForTest() const;
  uint32_t pendingSnapshotCoveredPassRevisionForTest() const;
  /// Queue the last committed pass for publication **without** a session exit, wrap journal or construction -- the
  /// pending shape the owner's gate cannot start on a playing track (a live-overdub pass id awaiting its turn).
  /// Selector fixtures only: no production path can build it without a live overdub session.
  bool queueLastCommittedPassForPublishForTest();
  /// Fingerprint of the authoritative (committed) pass-change fragment
  /// progress. DEC-047 transaction fixture: it must be unchanged across a
  /// failed fragment and advance only after successful incorporation.
  struct SnapshotFragmentProgressForTest {
    bool active = false;
    size_t batchSize = 0;
    size_t batchIndex = 0;
    size_t nextStoreIndex = 0;
    EventId nextEventId = kInvalidEventId;
    uint32_t nextCanonicalOrder = 0;
    uint8_t openNoteCount = 0;
    bool cursorComplete = false;
  };
  SnapshotFragmentProgressForTest snapshotFragmentProgressForTest() const;
  bool snapshotFragmentBlockedForTest() const;
  uint16_t snapshotFragmentConsecutiveFailuresForTest() const;
  /// Fault-inject the next pass-change fragment construction after `units`
  /// applied construction units. Consumed by the publisher on first use.
  void failNextSnapshotFragmentAfterUnitsForTest(
      size_t units, LoopSnapshotFailureReason reason = LoopSnapshotFailureReason::Injected) {
    snapshotFragmentFailAfterUnitsForTest_ = units;
    snapshotFragmentFailReasonForTest_ = reason;
  }
  size_t snapshotFragmentFailureInjectionForTest() const {
    return snapshotFragmentFailAfterUnitsForTest_;
  }
  LoopSnapshotFailureReason snapshotFragmentFailureReasonForTest() const {
    return snapshotFragmentFailReasonForTest_;
  }
  void clearSnapshotFragmentFailureInjectionForTest() {
    snapshotFragmentFailAfterUnitsForTest_ = static_cast<size_t>(-1);
    snapshotFragmentFailReasonForTest_ = LoopSnapshotFailureReason::Injected;
  }
#endif
  ImmutableLoopSnapshotRef publishedSnapshot() const;
  /// DEC-073: true only when the published root's `coveredPassRevision` equals the loop's current
  /// committed-pass revision (`committedPassRevision()`). A root selected from, or built for, an
  /// older committed-pass state is never current, no matter which revision was reached afterwards.
  /// The persistence gate (RC2) adds its own "publisher is settled" requirement on top.
  bool publishedSnapshotIsCurrent() const;
  /// DEC-108 *Checkpoint parity for undo and save* / task 4.1 (F-5): the **named** parity of the published root against
  /// the loop's current committed-pass revision -- `Current`, `OwesReconciliation` (behind the committed passes: a
  /// rebuild is owed), `CoverageUnrecorded` (no coverage recorded: conservatively not current) or `NoCheckpoint`. It is
  /// the same rule `publishedSnapshotIsCurrent()` applies, named, so a lagging checkpoint is *reported* rather than
  /// merely not-current, and it reads the loop's own figures only: neither undo nor save consults the window's lag.
  CheckpointParity checkpointParity() const;
  /// Non-owning identity of the current published root, or nullptr when none is
  /// selected or the published root is stale versus `playbackRevision`.
  const void* publishedSnapshotIdentity() const;
  /// Revision captured when the published root was stored. Zero when no root exists.
  uint32_t publishedSnapshotRevision() const;
  /// DEC-073: the committed-pass revision the published root was enumerated from
  /// (`kSnapshotCoverageUnset` when no root or no recorded coverage).
  uint32_t publishedSnapshotCoveredRevision() const;
  /// DEC-073 RC2: true when the published root is a *settled* representation of the committed
  /// passes -- coverage-current and no snapshot request still pending. This is the persistence
  /// admission predicate: an LSV1 body may embed the selected root only when it is settled,
  /// otherwise the save writes the passes-authoritative body and the loader rebuilds.
  bool hasSettledPublishedSnapshotForSave() const;
  /// DEC-073: revision of the publisher's enumerable committed-pass/companion inventory. Every
  /// inventory change bumps `playbackRevision`, so this is an alias today; it is named separately
  /// because the coverage comparison is about the pass inventory, not about any content revision.
  uint32_t committedPassRevision() const;

  /// Seal handoff (`150650`). Between the seal and the moment a committed tier carries the
  /// sealed pass, neither Snapshot nor VisualCache can paint it, so the frame would fall back
  /// to the previous pass. These rows cover that seam.
  ///
  /// Retention is a lifetime refinement of the existing Capture draw tier, not a new tier:
  /// `Loop` owns the rows, display only consumes them, and `VisualCache` merely reports
  /// coverage.
  const NoteUtils::DisplayNoteVec& sealedCapturePreviewNotes() const {
    return sealedCapturePreviewNotes_;
  }
  bool hasSealedCapturePreview() const { return !sealedCapturePreviewNotes_.empty(); }
  const NoteUtils::DisplayNoteVec& retiredSealedDisplayNotes() const {
    return retiredSealedDisplayNotes_;
  }
  bool hasRetiredSealedDisplayNotes() const { return !retiredSealedDisplayNotes_.empty(); }
  bool retiredSealedDisplayCovered() const;
  /// True when the frame must still paint the retired rows: at least one of them is neither covered
  /// by nor *replaced* in the committed cache.
  ///
  /// This is deliberately not `!retiredSealedDisplayCovered()`. The cache proves coverage by exact
  /// span, but the clean-adopt rule (`010217`) *replaces* a wrap-shaped successor with the linear
  /// row for the same pitch/start, so a retired wrap row is never "covered" while the committed
  /// side already holds the authoritative variant of that identity. Painting the retired copy would
  /// undo the adopt (`test_draw_source_od2_clean_adopt_keeps_linear_tail`). So the display gate asks
  /// whether the identity is represented at all, while the release rule keeps asking for coverage.
  bool retiredSealedDisplayNeededForFrame() const;
  void releaseRetiredSealedDisplayIfCovered();
  void clearRetiredSealedDisplayNotes();
  void retireSealedCapturePreviewForDisplay();
  /// Retains uncovered sealed rows, then merges this pass's closed preview before
  /// `capturePreview.clear()`. Release stays `releaseSealedCapturePreviewIfCovered`.
  void retainCapturePreviewAtSeal();
  void retainCapturePreviewAtSeal(const CapturePreview& source);
  /// True when `visualCache` has consumed the sealed revision and contains each
  /// sealed row. A current published snapshot is not membership proof (`012714`
  /// wrap 2: snap=1 dropped sealed=47). Revision without those rows is also not
  /// coverage (`170537`).
  bool sealedCapturePreviewIsCovered() const;
  /// Drops the retained rows once `sealedCapturePreviewIsCovered()` holds. Paint reads the
  /// predicate directly, so a late release cannot show stale rows.
  void releaseSealedCapturePreviewIfCovered();
  /// Unconditional drop for paths that invalidate the content the rows describe.
  void clearSealedCapturePreview();
  /// Branch actually taken by `tryGatherPublishedSnapshotEventsInWindow`.
  /// `None` = no published root (caller may run compatibility gather).
  /// `Snapshot` = current-root membership. `Overlay` = stale-root overlay.
  enum class SnapshotWindowGatherPath : uint8_t {
    None = 0,
    Snapshot = 1,
    Overlay = 2,
    /// DEC-143: no published root yet, so the window came from the immutable published Record pass alone.
    RecordOverlay = 3,
  };
  /// Source that last wrote, or last refused to write, an idle visual-cache slice.
  /// `None` = no accepted fill (including incomplete budget). Overlay is never
  /// an accepted source; `lastVisualCacheIdleOverlayRejected()` is the reject.
  enum class VisualCacheIdleGatherSource : uint8_t {
    None = 0,
    Snapshot = 1,
    Prepared = 2,
    /// Session-exit C1-a compatibility gather only (OpenSpec 6.2.2).
    Fallback = 3,
    /// LCR pass resolve when snapshot/prepared miss and compat is not allowed.
    /// LCR pass resolve when snapshot/prepared miss and compat is not allowed. Capped by
    /// `LoopContentResolution::kBoundedDisplaySynchronousEventCap`; above the cap the slice
    /// never selects this source.
    PassesResolve = 4,
    /// C5.2a/P-D bounded display continuation published the notes for this window.
    BoundedContinuation = 5,
  };
  VisualCacheIdleGatherSource lastVisualCacheIdleGatherSource() const {
    return lastVisualCacheIdleGatherSource_;
  }
  bool lastVisualCacheIdleOverlayRejected() const { return lastVisualCacheIdleOverlayRejected_; }
  bool lastVisualCacheIdleCopiedSnapshotNotes() const {
    return lastVisualCacheIdleCopiedSnapshotNotes_;
  }

  /// Fill `out` from published snapshot event membership in
  /// `[windowStart, windowStart + windowLength)`. A stale published root still
  /// supplies that window; unpublished session / pending-publish passes and
  /// their companions overlay on top. Returns false only when no published
  /// snapshot exists so the caller can use compatibility gather. `pathOut`
  /// receives the branch actually taken (`None` / `Snapshot` / `Overlay` / `RecordOverlay`).
  /// `inspectedEntriesOut`, when supplied, receives the number of snapshot membership references the walk
  /// **looked at** for the window's bars -- including the references it rejected -- which is the inspected work
  /// the window's preparation costs. It depends on the window and on the content *inside* it, never on the
  /// loop's unrelated content (fixture 2.10).
  ///
  /// **DEC-143 — a published Record slice is read material before its root publishes.** `publishedRecordOverlay`
  /// is the caller's own statement that this slot's Record take crossed the immutable `RecordPublished` edge and
  /// its root has not published yet (Track owns that edge; the Loop cannot see the presentation). While it holds
  /// and no published snapshot exists, the window is filled from the take's **sealed extent** — the extent this Loop
  /// already holds a read borrow on, keyed on the presentation record's identity — and reported as `RecordOverlay`.
  /// A pass's chunk-id list is deliberately not a source: undo/redo re-materialises the pass while the presentation
  /// is still the active reader, so those ids can already be recycled. Mutable live capture and live overdub content
  /// are never included by this branch, and once a root supplies the window the branch is `Snapshot` again, so the
  /// overlay is superseded rather than merged.
  bool tryGatherPublishedSnapshotEventsInWindow(SessionMidiEventVec& out, uint32_t windowStart,
                                                uint32_t windowLength,
                                                SnapshotWindowGatherPath* pathOut = nullptr,
                                                uint32_t* inspectedEntriesOut = nullptr,
                                                bool publishedRecordOverlay = false) const;
  /// Snapshot window plus live `capture.store` events in the same window.
  /// Returns false only when no published snapshot exists. `pathOut` is the
  /// underlying snapshot-window branch (`None` / `Snapshot` / `Overlay`).
  bool tryGatherPublishedSnapshotEventsInWindowWithCapture(
      SessionMidiEventVec& out, uint32_t windowStart, uint32_t windowLength,
      SnapshotWindowGatherPath* pathOut = nullptr) const;
  /// Record/overdub session-exit only. Wraps stay provisional. Overdub requires a parent.
  /// Completes construction in this call. Stop/idle paths use `queue` + `step`.
  bool tryPublishSessionExitSnapshot(CommitReason reason,
                                     const EditPassIdList& companionIds = {});
  /// Queue session-exit snapshot work without constructing. Safe on the stop path.
  // Cold prewarm reserves the receipt ring; the stopping handoff only swaps ownership.
  bool prewarmSessionExitSnapshotHandoff();
  /// Task 4.20: ring **existence** only (the one external allocation). Admission that needs the ring
  /// to be there — the overdub transfer horizon — must not ask for a free slot: that would couple
  /// `OverdubStart` to the snapshot publisher's cadence.
  bool ensureSessionExitReceiptRing();
  bool canHandoffSessionExitSnapshot() const;
  bool handoffSessionExitSnapshot(CommitReason reason);
  /// FIFO of session-exit receipts. Sized to the stop-job ring so a successor
  /// session is never serialized behind an unconsumed publisher turn. Index 0 is
  /// the oldest pending receipt; the idle publisher consumes that one per turn.
  /// The gate is a free slot; one receipt per open session is enforced by the
  /// handoff itself, which consumes the session identities.
  /// Capacity bump (owner request): 4 -> 8 so the raised FIFO (8) and custody slab (9) are reachable;
  /// this ring, not the transfer horizon, was the binding owner at four concurrent successors.
  static constexpr size_t kSessionExitReceiptCapacity = 8;
  /// Task 4.20: the reserved publication obligation record. It lives in the same external allocation
  /// as the ring, one slot past it, and is never reachable through `sessionExitReceiptHead_`/`Count_`
  /// — it is an obligation the interval admitted *before* it went live, never a receipt slot.
  static constexpr size_t kReservedSessionExitReceipt = kSessionExitReceiptCapacity;
  static constexpr size_t kSessionExitReceiptSlots = kSessionExitReceiptCapacity + 1;
  size_t sessionExitReceiptCount() const { return sessionExitReceiptCount_; }
  /// DEC-068: no interval-level session-exit tail (ring full, no session, or not a session exit).
  static constexpr size_t kInvalidSessionExitReceipt = static_cast<size_t>(-1);
  /// Task 4.20 ownership state machine of the single reserved obligation. `Empty -> Reserved` is the
  /// claim at overdub admission; `Reserved -> Populated` is the atomic transfer at that interval's
  /// stop admission; `Populated -> Publishing` is its final pass committing; `Publishing -> Empty`
  /// is the publisher moving it through the receipt path, exactly once. Occupied at admission is a
  /// **named bounded refusal**, never a second slot and never a silent queue.
  enum class SessionExitPublishClaim : uint8_t {
    Empty = 0,
    Reserved = 1,
    Populated = 2,
    Publishing = 3,
  };
  SessionExitPublishClaim sessionExitPublishClaimState() const {
    return sessionExitPublishClaimState_;
  }
  /// The session the reservation is held for; zero while reserved but not yet open.
  uint64_t sessionExitPublishClaimIdentity() const;
  /// Reserve the future session-exit publication obligation. Called once per interval at overdub
  /// admission, before the interval is live. Refuses (and reports) when the single obligation is
  /// already reserved, populated or publishing — the caller then names that refusal.
  bool claimSessionExitPublishOwner();
  /// DEC-191: resolve an **orphaned** reservation. A `Reserved` claim is payload-free by construction
  /// (`claimSessionExitPublishOwner` zeroes the record), and `markSessionExitTailReady` only accepts a
  /// `Populated` record, so without this terminal an ownerless `Reserved` claim never leaves the state and the
  /// slot's Start is refused forever. Returns true only when it actually moved the claim to `Empty`; a record
  /// that holds committed identities is never released.
  bool releaseOrphanedSessionExitReservation();
  /// Release a payload-free reservation. `sessionIdentity` 0 releases a reservation whose session
  /// never opened (an aborted start); a populated/publishing obligation is never dropped here
  /// because it owns committed identities the publisher still owes.
  bool releaseSessionExitPublishOwner(uint64_t sessionIdentity);
  /// Task 4.20: the atomic transfer at the stop admission. Precondition: the reservation is
  /// `Reserved` **for this session**. Swaps this interval's pass/companion identities into the
  /// reserved record (never copies, never allocates), leaves both session vectors empty, and
  /// advances `Reserved -> Populated`. One owner before, one owner after.
  bool transferSessionExitToReservedPublishOwner(uint64_t sessionIdentity);
  /// Whether this interval's tail has a durable destination right now: a free receipt slot, or the
  /// reservation held for the live session. This is what the stop admission and the successor-stop
  /// capacity answer must ask instead of `canHandoffSessionExitSnapshot()`.
  bool canCaptureSessionExitTail() const;
  /// `Populated` or `Publishing`: the record owns the obligation and the publisher still owes it.
  bool hasReservedSessionExitObligation() const;
  /// What the reserved record itself holds: the payload and readiness of the obligation, so a fixture
  /// can prove one owner before and after the transfer instead of inferring it from a later state.
  struct ReservedSessionExitRetention {
    size_t passCount = 0;
    size_t companionCount = 0;
    size_t companionRowCount = 0;
    bool pending = false;
    bool ready = false;
  };
  ReservedSessionExitRetention reservedSessionExitRetention() const;
  /// Capture this interval's own session-exit tail at its stop admission, before any successor
  /// can touch the shared session state. Swaps the identities into a free receipt slot, or — when
  /// the ring is full — into this interval's reserved obligation record (never copies: the stop
  /// boundary must not allocate) and leaves it not ready until `markSessionExitTailReady`. Returns
  /// the receipt's ring index, `kReservedSessionExitReceipt`, or `kInvalidSessionExitReceipt`.
  size_t captureSessionExitTail(CommitReason reason);
  /// Fill the captured tail's final pass id and make it consumable by the publisher.
  bool markSessionExitTailReady(size_t ringIndex, PassId lastPassId);
  /// Task 6.4: release ring capacity without the publisher. A ready receipt is an inert identity list,
  /// so adjacent ready receipts at the FIFO head are coalesced into one (order kept), and a `Publishing`
  /// reserved obligation then moves into the freed slot (`Publishing -> Empty`). Touches neither the
  /// pending publish box nor any not-ready receipt, so an in-flight stop job's bindings stay intact.
  /// Ring occupancy is then bounded by outstanding stop jobs, not by unpublished sessions.
  void settleReadySessionExitReceipts();
  /// earlier receipts drain, ring slots do not.
  SessionExitSnapshotHandoff* sessionExitReceiptByRingIndex(size_t ringIndex);
  const SessionExitSnapshotHandoff* sessionExitReceiptByRingIndex(size_t ringIndex) const;
  const SessionExitSnapshotHandoff* sessionExitReceiptAt(size_t index) const;
  const SessionExitSnapshotHandoff* oldestPendingSessionExitReceipt() const {
    return sessionExitReceiptAt(0);
  }
  bool hasPendingSessionExitReceipt() const { return sessionExitReceiptCount_ != 0; }
  bool queueSessionExitSnapshot(CommitReason reason, const EditPassIdList& companionIds = {});
  /// OpenSpec 5.1 Pin A: bind deferred after-root attach to this GUS entry id
  /// (`UndoEntryId`; 0 clears). No-op when no session-exit pending box exists.
  void setPendingSessionExitUndoAttachEntryId(uint32_t entryId);
  uint32_t pendingSessionExitUndoAttachEntryId() const;
  /// Firmware registers `TrackUndo::attachAfterPublishedSnapshot` here. Native
  /// Loop-only tests leave the hook null (attach is a no-op).
  using SessionExitPublishedUndoAttachFn =
      void (*)(Loop& loop, ImmutableLoopSnapshotRef published, uint32_t entryId);
  static void setSessionExitPublishedUndoAttachFn(SessionExitPublishedUndoAttachFn fn);
  void notifySessionExitPublishedUndoAttach(ImmutableLoopSnapshotRef published,
                                            uint32_t entryId);
  /// Queue publication of every active committed pass. Explicit selected-slot
  /// hydration only — adopt/restore do not call this (`023222`). Does not
  /// construct. Empty loops clear pending and do not queue. Clears the published
  /// root, so playback holds empty until the replacement publishes.
  bool queueLoadedSnapshotPublish();
  /// Rebuild the published root from every active committed pass, on the normal
  /// session-exit publication lane, **without** clearing the published root:
  /// playback keeps the previous source until the replacement lands, so this never
  /// produces a gap. Used after an undo/redo that changed committed passes, where
  /// the captured before/after root may be absent (the session-exit after-attach is
  /// asynchronous) or may not represent the resulting pass set — leaving a stale or
  /// current-but-incomplete root that is visible on the display and silent in
  /// playback.
  bool requestCommittedPassesRepublish();
  /// Task 3.14c: the same request, but enumerated against an **empty** parent instead of the
  /// published root's diff. A note-edit undo/redo changes the companion overlay only: the
  /// committed passes are unchanged, so their diff against the published root is empty and a
  /// republish has nothing to freeze. A rebuild enumerates every active committed pass from
  /// scratch (plus the active overlays), which both retracts an overlay that a disabled batch no
  /// longer contributes and still represents an unchanged pass. The published root stays
  /// selected and audible until the replacement lands. Costs one full pass enumeration, the same
  /// work a load/restore publication pays, and only on an edit-pass edge.
  bool requestCommittedPassesRebuild();
  /// Queue bounded wrap-journal fragments from settled pending mutations. Wrap path only.
  bool queueOverdubWrapSnapshotJournal(PassId wrapPassId);
  /// Record the `playbackRevision` a queued wrap journal will represent once complete.
  /// Called after the wrap seal, when the revision is final. Unstamped journals stay
  /// unselectable so a candidate is never marked current for a revision it lacks.
  void stampWrapSnapshotJournalRevision(PassId wrapPassId, uint32_t targetRevision);
  /// Apply at most `maximumChanges` pending snapshot construction units. Idle only.
  /// One unit is one dirty bar of the in-flight change. `allowStartConstruction`
  /// false advances an in-flight candidate only. `playheadPhaseTick` orders bars
  /// by soonest arrival.
  bool stepPendingSnapshotPublish(size_t maximumChanges, bool allowStartConstruction = true,
                                  uint32_t playheadPhaseTick = 0,
                                  uint32_t liveRecordCompletedTicks = 0);
  bool hasPendingSnapshotPublish() const;
  bool hasInFlightSnapshotConstruction() const;
  /// Units the in-flight construction has completed (saturating; 0 when none). Qualification row progress only.
  uint16_t inFlightSnapshotConstructionUnits() const;
  bool hasPendingWrapJournal() const;
  /// A live overdub session-record entry (or its inventory hand-over) the publication owner has not incorporated:
  /// durable owed wrap publication work, independent of any Clock-built artifact (owner ruling 2026-11-03).
  bool hasWrapPublicationOwed() const;
  /// The wrap Clock's whole publication duty: O(1) -- stamp the post-seal revision the session record now reaches.
  void stampWrapPublicationObligation();
  /// Before a session record is cleared without a session exit: move what is not yet incorporated to the inventory.
  void handOverSessionRecordToInventory();
  size_t pendingSnapshotQueuedPassCount() const;
  size_t pendingSnapshotAppliedPassCount() const;
  PassId pendingSnapshotConstructionPassId() const;
  size_t pendingSnapshotWrapJournalCount() const;
  uint8_t pendingSnapshotWrapFragmentIndex() const;
  uint8_t pendingSnapshotWrapFragmentCount() const;
  uint32_t pendingSnapshotWrapParentRevision() const;
  bool pendingSnapshotSessionExitQueued() const;
  /// True when playback rebuild may use session-exit compatibility gather: no
  /// published root yet while session-exit snapshot publication is queued/in flight.
  bool isPlaybackSessionExitPublicationGap() const;
  bool pendingSnapshotFromLoadedPasses() const;
  const EditPassIdList* pendingSnapshotCompanionIds() const;
  LoadedSnapshotHydrationPhase loadedSnapshotHydrationPhase() const;
  uint32_t pendingSnapshotCandidateLengthTicks() const;
  /// Last idle-step child durations. Capture-serial only; 0 in normal builds.
  uint32_t lastSnapshotIdleStartUs() const;
  uint32_t lastSnapshotIdlePassChangeUs() const;
  uint32_t lastSnapshotIdleStepUs() const;
  uint32_t lastSnapshotIdleFinishUs() const;
  uint32_t lastSnapshotIdlePublishUs() const;
  uint32_t lastSnapshotIdleProbeUs() const;
  uint32_t lastSnapshotIdleEnqueueUs() const;
  uint32_t lastSnapshotIdleDiscardUs() const;
  uint32_t lastSnapshotIdleEmitUs() const;
  uint32_t lastSnapshotIdleRefillUs() const;
  uint32_t lastSnapshotIdleEventScanUs() const;
  uint32_t lastSnapshotIdleNoteLookupUs() const;
  uint32_t lastSnapshotIdleChangeBuildUs() const;
  uint32_t lastSnapshotIdleConstructUs() const;
  void addSnapshotIdleEmitUs(uint32_t durationUs);
  /// Exclusive end tick of first-record live construction already applied.
  uint32_t liveRecordAppliedCompletedTicks() const;
  /// First unconsumed capture.store index for live-record snapshot publication.
  size_t liveRecordAppliedEventIndex() const;
  uint8_t liveRecordOpenNoteCount() const;
  ImmutableLoopSnapshotRef unpublishedSnapshot() const;
  const LoopSnapshotChangeSet* pendingSnapshotConstructionChanges() const;
  /// True when a live-record parent exists with tick room, or none exists yet.
  bool liveRecordCanApplyCompletedTicks(uint32_t completedTicks) const;
  /// Settled NOTE_EDIT close batch. In-session durable checkpoints stay provisional.
  bool tryPublishNoteEditSnapshot(const EditPassIdList& editPassIds);
  bool captureActive() const;
  size_t liveEventCount() const;
  /// Committed pass event count + active capture size; builds visual cache if needed. Display/LED only.
  size_t displayEventCountHint() const;
  bool ensureCaptureEventsSorted();
  /// Accept completion of the fixed-grain canonical rewrite performed from a
  /// frozen capture staging source. The caller has already written every
  /// capture row in stable raw-tick order; sealing must not fall back to the
  /// legacy full-store sort.
  void noteCaptureEventsCanonicalized(uint32_t lastTick);
  /// Fallback/full rebuild pairs each capture pass (`resolveDisplayNotes`)
  /// before merging rows. Snapshot gather still reconstructs that event vector.
  void rebuildVisualCacheFromPasses();
  /// Reconstruct each active record/overdub pass with `overdubPassWrapPairing` and
  /// append notes that merged record+overdub reconstruct omitted (015618).
  /// Skip a candidate when its valid `NoteId` is already present. `wrapHeldOnly`
  /// keeps wrap-held heads/tails and drops interior body notes (204132).
  /// A linear same-start remnant is replaced by wrap-held geometry (`225200`).
  /// Source view uses `wrapHeldOnly=false`. One pass at a time.
  void appendOverdubPassWrapPairedNotes(NoteUtils::DisplayNoteVec& notes,
                                        bool wrapHeldOnly = false) const;
  /// Visual-cache entry: wrap-held heads/tails only. Interior wrap-paired
  /// body notes stay off the piano-roll list (204132 / NOTE_EDIT open).
  void appendOverdubPassDisplayNotes(NoteUtils::DisplayNoteVec& notes) const;
  /// DEC-237 Slice 2 — active committed passes in display order (record first, then active overdubs
  /// with content). The staged display producer uses this as its per-pass staging count.
  uint16_t activeCommittedPassCount() const;
  /// DEC-237 Slice 2 — true when this active pass has a committed chunk whose span intersects the loop
  /// boundary window (`closureTicks` on each side). Passes that cannot produce a wrap-held row are
  /// skipped by the producer, so an unedited chain stages only the passes that actually wrap.
  bool activeCommittedPassHasWrapContent(uint16_t passIndex, uint32_t closureTicks) const;
  /// DEC-237 Slice 2 — stage the wrap-held rows of exactly one active committed pass, in the same display
  /// order. One pass per call; the producer iterates so a candidate never re-reads every pass per tile.
  /// Returns false when `passIndex` is outside the active committed inventory.
  bool appendOverdubWrapNotesForPass(uint16_t passIndex, NoteUtils::DisplayNoteVec& notes) const;
  /// Closed NoteOn identities in committed events but missing from `dest`
  /// (snapshot NoteRef / resolve miss). Cold path — kept for non-snapshot
  /// source-view origins and diagnostics; visual/adopt call sites unwound.
  void mergeOmittedCommittedNoteOnIdentitiesInto(NoteUtils::DisplayNoteVec& dest) const;
  /// Boot playback-set readiness: the loop has a display source for its **first paint window**
  /// (`maxBars` from bar 0). A current published root paints directly; a rootless slot needs a
  /// non-empty visual cache whose window bars are clean. An empty slot is ready (nothing to paint).
  bool visualCacheFirstWindowReady(uint8_t maxBars) const;
  /// Rebuild up to `maxBarsPerSlice` dirty bars, preferring `priorityBar`.
  /// When `maxBarDistanceFromPriority` is finite, skip dirty bars outside that neighborhood
  /// (PLAYING viewport backfill); pass UINT32_MAX for full-loop idle backfill when stopped.
  /// `allowOverdubWrapAppend` controls whether wrap-pair append is allowed in this slice.
  /// `constrainFallbackGather` applies a bounded fallback mode for post-stop drain windows.
  void rebuildVisualCacheIdleSlice(uint8_t maxBarsPerSlice, uint32_t priorityBar,
                                   uint32_t maxBarDistanceFromPriority = UINT32_MAX,
                                   bool allowOverdubWrapAppend = true,
                                   bool constrainFallbackGather = false,
                                   size_t fallbackGatherEventBudget = 0);
  /// C5.2a/P-D: one bounded grain of the display-note continuation for this loop, stepped by the
  /// derived-view maintenance lane before the slice. Admission is the exact request (loop instance,
  /// loopId, loop length, `playbackRevision`, pass count); a changed request is a qualified reset.
  /// Above `LoopContentResolution::kBoundedDisplaySynchronousEventCap` this is the only content
  /// source the slice may use. Returns true when a grain advanced.
  bool stepVisualCacheSourceContinuation();
  /// C5.6 — **qualified committed display content for one requested interval**, or `false` when no
  /// source is independently qualified for it right now. This is the composition contract's provider
  /// side: every candidate proves its own qualification, and the caller may present the result only
  /// for the interval it asked for.
  ///   - `BoundedContinuation`: `boundedDisplayReady` for this exact request — the publish edge, so an
  ///     incomplete/private tail is unreadable by construction;
  ///   - `RetainedEnvelope`: `boundedDisplayRetainedReady` — the promoted generation is still an exact
  ///     live prefix of this pass set, so its rows *are* the deterministic output of the current passes
  ///     even though `playbackRevision` moved (a wrap appends after the prefix);
  ///   - `PreparedWindow`: `tryResolvePreparedWindow` for this exact window, refused unless the kept
  ///     index is stamped with the live `playbackRevision` and loop length;
  ///   - `VisualCache`: only when `visualCache.builtFromRevision == playbackRevision` (content-currency
  ///     authority, DEC-108) *and* the requested window is covered by the cache's clean bars.
  /// Returns false rather than stretching any source beyond its coverage; `out` is then empty.
  enum class QualifiedDisplaySource : uint8_t {
    None,
    BoundedContinuation,
    RetainedEnvelope,
    PreparedWindow,
    VisualCache
  };
  bool collectQualifiedCommittedDisplayNotes(uint32_t windowStart, uint32_t windowLength,
                                             NoteUtils::DisplayNoteVec& out) const;
  /// Current, covered cache projection only; true includes an authoritative empty interval.
  /// False (stale/partial/invalid or reserve refusal) leaves `out` empty. Never gathers MIDI.
  bool tryCopyCurrentVisualCacheDisplayNotesInWindow(uint32_t windowStart, uint32_t windowLength,
                                                    NoteUtils::DisplayNoteVec& out) const;
  QualifiedDisplaySource lastQualifiedDisplaySource() const {
    return lastQualifiedDisplaySource_;
  }
  /// Resume wrap-pair append that was deferred during runtime-priority idle slices.
  /// Appends only when `deferredVisualCacheWrapAppendFromRevision_` still equals
  /// `playbackRevision`. A mismatch obtains the current-source frame instead of
  /// appending an earlier last pass (`203406`).
  /// Completes a deferred wrap append. `allowWholeLoopResolve` is the owner's admission for the
  /// unbounded `resolveDisplayNotes(passes, ...)` fallback: false keeps the append pending (lossless
  /// deferral) instead of rebuilding the whole cache on a timing-critical turn.
  void applyDeferredVisualCacheWrapAppend(bool allowWholeLoopResolve = true);
  bool hasDeferredVisualCacheWrapAppend() const {
    return deferredVisualCacheWrapAppendPending_;
  }
  /// After pass-state change: mark stale and fill short loops via idle slices (same oracle
  /// as PLAYING). Long loops stay idle-deferred — no `VCACHE,full` flatten+append.
  /// DEC-188: identical bounded work for **every** loop length -- mark, then at most one paint window
  /// (4 slices x 4 bars) of drain, with the remainder left to the idle lane. There is deliberately no
  /// short-loop full-fill branch and no long-loop early return.
  void refreshVisualCacheAfterPassStateChange();
  /// RC-N1: if settled pitch is present at the mover start, drop the previous home pitch there.
  /// `retainedEndTicks` are same-pitch same-start sibling ends that must survive (152627).
  void retireSupersededPitchDisplayNote(uint8_t previousPitch, uint32_t startTick, uint32_t endTick,
                                        uint8_t settledPitch, const uint32_t* retainedEndTicks = nullptr,
                                        size_t retainedEndTickCount = 0);
  /// After pass flatten drops a committed mover that snapshot spans still have (`232309`).
  void keepCommittedMoverInVisualCache(NoteId noteId, uint8_t pitchHint);
  void ensureVisualCacheBuilt();
  void markDisplayCachesStale();
  /// Dirty only bars touched by `committedPassId` and companion edit rows. Used by overdub stop
  /// and overdub wrap publish. No whole-loop reconstruct.
  void markAffectedDisplayCacheRanges(PassId committedPassId, const EditPassIdList& companionIds);
  /// RC-E: adopt overdub-stop composed frame as a partial visual cache (window fresh, rest dirty).
  void adoptComposedDisplayNotesFromViewport(const DisplayNoteVec& notes);
  /// Note + visual caches only — does not disturb playback order or materialized pass view.
  void invalidateDisplayCaches();
  void shiftActiveCapturePassTicks(int64_t delta);
  size_t nativeTestLiveEventCount() const { return liveEventCount(); }
  void seedRecordPassFromStore(LoopEventStore& store);
  void commitStopFinalizeFromStore(LoopEventStore& merged);

  SealOutcome sealCapture(uint32_t sealedAtTick,
                          CommitReason reason = CommitReason::RecordStop);
  /// Stage an already canonicalized and finalized capture as a pending pass.
  /// This is preparation work: it seals/moves capture chunk references and
  /// reserves canonical pass capacity. The later transfer is allocation-free.
  SealOutcome stagePreparedCapturePass(uint32_t sealedAtTick, CommitReason reason);
  /// STOPPED-only capacity admission for a bounded run of active overdub
  /// commits. The supplied horizon is deliberately independent of capture
  /// event count; every row gets fixed chunk-reference capacity.
  bool prewarmActiveOverdubTransferHorizon(
      uint16_t horizon = PassConfig::ACTIVE_OVERDUB_TRANSFER_HORIZON);
  /// True only when an active overdub may consume one fully prepared
  /// canonical-transfer row without growing canonical storage.
  bool hasActiveOverdubTransferAdmission() const;
  /// Task 6.4: the horizon as counts, so stop admission can charge each outstanding job its own row.
  /// Rows are prewarmed chunk-id rows (taken at `PrepareTransfer`); headroom is pass-vector capacity
  /// (consumed at the atomic commit). Both are 0 while a legacy pending capture pass owns the slot.
  size_t activeOverdubTransferRowCount() const;
  size_t activeOverdubPassHeadroom() const;
  /// Task 6.4: low-water test for the deferred top-up of a live overdub chain's horizon.
  bool activeOverdubTransferHorizonLow() const;
  /// Stage an already canonicalized capture using one prewarmed transfer row.
  /// This is the active-path form of `stagePreparedCapturePass`; it denies
  /// instead of allocating when the declared horizon is exhausted.
  SealOutcome stagePreparedCapturePassFromActiveAdmission(uint32_t sealedAtTick,
                                                           CommitReason reason);
  /// Stage a job-owned canonical capture. Its source is consumed on success;
  /// later publication must not retire the Loop's independent live capture.
  SealOutcome stagePreparedCapturePassFromActiveAdmission(
      Capture& source, uint32_t sealedAtTick, CommitReason reason);
  /// Begin bounded staging directly into a job-owned receipt using admitted storage.
  /// Source must be independent of the live capture. Both owners and generation
  /// outlive the cursor. Partial output must not be read or published.
  SealOutcome beginPreparedCapturePassFromActiveAdmission(
      Capture& source, PendingCapturePass& pass, LoopEventStoreCommitCursor& cursor,
      const uint64_t& generation, uint32_t sealedAtTick);
  /// Cold, idempotent capacity admission for a prepared pass whose source holds
  /// `chunks` chunk references. Sizes the next prewarmed ownership row to that
  /// count so a Record interval is not capped at the fixed overdub transfer row.
  /// Allocation happens here, once, before the bounded transfer starts; the
  /// transfer itself never grows the row. Returns false without an admission or
  /// beyond the pool bound.
  bool prewarmPreparedPassTransferChunks(size_t chunks);
  LoopEventStoreRewriteResult stepPreparedCapturePass(
      Capture& source, PendingCapturePass& pass, LoopEventStoreCommitCursor& cursor,
      size_t quantum);
  /// Wrap-window synthetic note-offs on live capture.store (record/overdub stop policy).
  LoopStopFinalize::Result finalizeCaptureWrapWindowAtStop(uint32_t stopAbsTick);
  /// Legacy inline stop projection: invalidate live Capture before its store
  /// rewrites or filters events against the final geometry.
  void projectCaptureAtRecordStop(uint32_t loopLengthTicks);
  /// May grow canonical pass storage. Call only while non-real-time preparation is allowed.
  /// A successful call establishes the capacity required by transferPreparedCapturePass().
  bool preparePendingCapturePassTransfer();
  /// True when the pending pass can change canonical ownership without growing pass storage.
  bool canTransferPendingCapturePassWithoutAllocation() const;
  /// OpenSpec 5.12 / D8: capacity preflight and transfer for a *specified* staged pass, so an
  /// interval can commit its own job-owned receipt without the Loop producer slot carrying it.
  bool canTransferCapturePassWithoutAllocation(const PendingCapturePass& pass) const;
  /// Move an already sealed pass into canonical ownership without scanning events or allocating.
  /// `transferred` receives the pass identity and seal metadata; chunk refs move directly
  /// into the canonical pass, and `pass` is consumed. Callers remain responsible for deferred
  /// cache/UI cleanup.
  bool transferCapturePass(PendingCapturePass& pass, PreparedCapturePassTransfer& transferred);
  /// Move an already sealed pass into canonical ownership without scanning events or allocating.
  /// `transferred` receives the pass identity and seal metadata; chunk refs move directly
  /// into the canonical pass. Callers remain responsible for deferred cache/UI cleanup.
  bool transferPreparedCapturePass(PreparedCapturePassTransfer& transferred);
  /// Perform the bounded canonical ownership handoff. This moves a
  /// capacity-proven pending pass and retires mutable capture ownership, but
  /// deliberately does not publish derived content, persist, build a bridge,
  /// or perform display/source cleanup.
  bool commitPreparedCapturePassAtomic(PreparedCapturePassTransfer& transferred);
  /// Same handoff for a specified interval-owned pass (OpenSpec 5.12 / D8).
  bool commitPreparedCapturePassAtomic(PreparedCapturePassTransfer& transferred,
                                       PendingCapturePass& pass);
  /// Atomic completion after `transferPreparedCapturePass()`: retire the
  /// mutable capture ownership and invalidate derived identities only. It
  /// deliberately does not retain display rows, build views, persist, or run
  /// any content-sized cleanup.
  void completePreparedCapturePassAtomic(bool retiresLiveCapture = true);
  /// Non-atomic display/source cleanup retained for legacy STOPPED callers.
  /// A deferred active-transport owner must schedule this separately.
  void completePreparedCapturePassDeferredCleanup();
  void completePreparedCapturePassDeferredCleanup(CapturePreview& source);
  bool commitPendingCapturePass(CommitReason reason = CommitReason::RecordStop);
  void discardPendingCapturePass();

  bool reclaimDisabledCapturePass(PassId id);
  uint16_t reclaimUnreferencedDisabledCapturePasses(const SlotPassReferences& refs);
  uint16_t reclaimUnreferencedDisabledEditPasses(const SlotPassReferences& refs);
  void reclaimUnreferencedDisabledPasses(const SlotPassReferences& refs);

  PlaybackOrderVec& getPlaybackOrder() {
    if (!playbackOrder_) playbackOrder_ = std::make_unique<PlaybackOrderVec>();
    return *playbackOrder_;
  }
  const PlaybackOrderVec& getPlaybackOrder() const {
    return const_cast<Loop*>(this)->getPlaybackOrder();
  }
  bool playbackOrderEmpty() const { return !playbackOrder_ || playbackOrder_->empty(); }
  size_t playbackOrderSize() const { return playbackOrder_ ? playbackOrder_->size() : 0; }

  NoteUtils::CachedNoteList& getNoteCache() {
    if (!noteCache_) noteCache_ = std::make_unique<NoteUtils::CachedNoteList>();
    return *noteCache_;
  }
  NoteUtils::CachedNoteList& getNoteCache() const {
    return const_cast<Loop*>(this)->getNoteCache();
  }

  NoteUtils::EventIndex& getCachedEventIndex() {
    if (!cachedEventIndex_) cachedEventIndex_ = std::make_unique<NoteUtils::EventIndex>();
    return *cachedEventIndex_;
  }
  const NoteUtils::EventIndex& getCachedEventIndex() const {
    return const_cast<Loop*>(this)->getCachedEventIndex();
  }

  void invalidateCaches();
  void invalidatePlaybackCaches();

  NoteId allocateNoteId();
  /// Admission-only lookahead. Callers may reserve fixed capture capacity for
  /// this identity before mutating pending-note or canonical capture state;
  /// the accepted append then consumes it through `allocateNoteId()`.
  NoteId peekNextNoteId() const { return nextNoteId_; }
  void assignMissingNoteIds(SessionMidiEventVec& events);
  void assignMissingNoteIds(MidiEventVec& events);
  void assignMissingNoteIdsInStore(LoopEventStore& store);
  void assignMissingNoteIdsInStoreWindow(LoopEventStore& store, size_t beginIndex,
                                         uint32_t includeUntil);
  /// Fill kInvalidNoteId on active record/overdub committed chunks. Same allocateNoteId
  /// as InStore. In-place; chunk ids unchanged so playback refs stay valid.
  void assignMissingNoteIdsInCommittedCapturePasses();

  enum class CommittedNoteIdPreparationResult : uint8_t {
    Idle, Progress, Complete, Superseded, Fault
  };
  /// Caller-owned scalar progress only; the Loop must outlive all calls.
  class CommittedNoteIdPreparation {
   public:
    void discard() { *this = {}; }
    size_t inspectedChunks() const { return inspectedChunks_; }
    uint32_t assignedIds() const { return assignedIds_; }
   private:
    friend class Loop;
    const Loop* loop_ = nullptr;
    LoopId loopId_ = kInvalidLoopId;
    uint64_t canonicalGeneration_ = 0, captureGeneration_ = 0;
    uint32_t revision_ = 0;
    PassId recordId_ = kInvalidPassId;
    size_t overdubCount_ = 0, pass_ = 0, chunk_ = 0, inspectedChunks_ = 0;
    uint32_t assignedIds_ = 0;
    CommittedNoteIdPreparationResult result_ = CommittedNoteIdPreparationResult::Idle;
  };
  /// Acceptance never scans or assigns. One step is one pass transition or one
  /// committed chunk (at most CHUNK_CAPACITY rows); no retained chunk/root refs.
  void beginCommittedNoteIdPreparation(CommittedNoteIdPreparation& cursor) const;
  CommittedNoteIdPreparationResult stepCommittedNoteIdPreparation(
      CommittedNoteIdPreparation& cursor);
  bool committedNoteIdPreparationComplete(const CommittedNoteIdPreparation& cursor) const;

  enum class CommittedSnapshotInventoryPreparationResult : uint8_t {
    Idle, Progress, Complete, Superseded, Fault
  };
  /// Nonowning scalar cursor. Loop and output headers must outlive calls; caller
  /// excludes writes/reuse/parallel cursors. Discard never clears the outputs.
  class CommittedSnapshotInventoryPreparation {
   public:
    void discard() { *this = {}; }
    size_t inspectedRows() const { return inspectedRows_; }
    /// Captured coverage only: usable solely with a currently Complete query.
    uint32_t coveredPassRevision() const { return revision_; }
   private:
    friend class Loop;
    enum class Phase : uint8_t {
      CountRecord, CountOverdub, CountEdit, ReserveCapture, ReserveCompanion,
      EmitRecord, EmitOverdub, EmitEdit
    };
    const Loop* loop_ = nullptr;
    const std::vector<PassId>* captures_ = nullptr;
    const EditPassIdList* companions_ = nullptr;
    const PassId* captureData_ = nullptr;
    const EditPassId* companionData_ = nullptr;
    LoopId loopId_ = kInvalidLoopId;
    uint64_t canonicalGeneration_ = 0;
    uint32_t revision_ = 0, loopLength_ = 0;
    PassId recordId_ = kInvalidPassId;
    size_t overdubCount_ = 0, editCount_ = 0, row_ = 0, inspectedRows_ = 0;
    size_t captureCount_ = 0, companionCount_ = 0;
    size_t captureSize_ = 0, companionSize_ = 0;
    size_t captureCapacity_ = 0, companionCapacity_ = 0;
    Phase phase_ = Phase::CountRecord;
    CommittedSnapshotInventoryPreparationResult result_ =
        CommittedSnapshotInventoryPreparationResult::Idle;
  };
  /// Begin is metadata-only and requires empty outputs. A grant inspects one
  /// pass metadata row, reserves one EMPTY vector, or advances one phase.
  /// Reservations copy zero elements; allocator byte cost/timing is unqualified.
  /// No root/pending-owner mutation, no live entry/main dispatch yet.
  void beginCommittedSnapshotInventoryPreparation(
      CommittedSnapshotInventoryPreparation& cursor,
      std::vector<PassId>& captures, EditPassIdList& companions) const;
  CommittedSnapshotInventoryPreparationResult stepCommittedSnapshotInventoryPreparation(
      CommittedSnapshotInventoryPreparation& cursor,
      std::vector<PassId>& captures, EditPassIdList& companions) const;
  CommittedSnapshotInventoryPreparationResult committedSnapshotInventoryPreparationResult(
      const CommittedSnapshotInventoryPreparation& cursor,
      const std::vector<PassId>& captures, const EditPassIdList& companions) const;
#if defined(PIO_UNIT_TEST_NATIVE)
  // Fixture-only STOPPED legacy cleanup; never called by the adoption commands.
  void clearPendingSnapshotPublishForTest() { clearPendingSnapshotPublish(); }
  /// Test-only: the pending publication's progress (applied passes, queued passes, active fragment). Read-only.
  bool pendingSnapshotProgressForTest(size_t& applied, size_t& queued, bool& fragment) const;
#endif

  enum class CommittedSnapshotInventoryAdoptionMode : uint8_t { Loaded, Republish };
  enum class PendingSnapshotInventoryDestinationResult : uint8_t { Ready, NeedsHeader, Busy };
  enum class CommittedSnapshotInventoryAdoptionResult : uint8_t {
    Adopted, SourceNotReady, SourceFault, SourceSuperseded, EmptyInventory,
    InvalidMode, InvalidGeometry, NeedsHeader, DestinationBusy, NeedsCompanionCapacity
  };
  /// Observation never allocates. Prepare allocates only an absent default header;
  /// allocation latency/failure is not qualified. Neither command clears old work.
  PendingSnapshotInventoryDestinationResult pendingSnapshotInventoryDestinationResult() const;
  PendingSnapshotInventoryDestinationResult prepareFreshPendingSnapshotInventoryDestination();
  /// Unused prerequisite: caller keeps this same live Loop, box, cursor and output
  /// headers alive/unmoved and excludes unstamped writes, competing inventory or
  /// prepared workers, publisher/queue/wrap/undo callbacks, semantic/header readers,
  /// borrowed overlays, weak resurrection and reset/reuse/address ABA. Scalar
  /// guards are not a universal quiescence or lifetime token. Complete must qualify
  /// on this exact Loop/outputs. Success exchanges only headers; caller owns the
  /// returned empty buffers and disposes of them separately. No allocation/copy,
  /// payload retirement or root ownership change; every refusal is observational.
  CommittedSnapshotInventoryAdoptionResult adoptCommittedSnapshotInventory(
      CommittedSnapshotInventoryPreparation& cursor, std::vector<PassId>& captures,
      EditPassIdList& companions, CommittedSnapshotInventoryAdoptionMode mode);

  void clearCaptureOnNewPass();

 private:
#if defined(PIO_UNIT_TEST_NATIVE)
  friend struct PendingSnapshotRetirementTestAccess; // Read-only owned-box observation.
#endif
  bool committedNoteIdPreparationSourceMatches(const CommittedNoteIdPreparation& cursor) const;
  bool committedSnapshotInventoryPreparationMatches(
      const CommittedSnapshotInventoryPreparation& cursor,
      const std::vector<PassId>& captures, const EditPassIdList& companions) const;
  // Separate from pass/history currency: once exhausted, zero is terminal and
  // cannot compare equal to a valid borrowed Capture receipt.
  uint64_t captureContentGeneration_ = 1;
  void advanceCaptureContentGeneration();
  friend class TrackUndo;
  /// Task 3.14c: the publication-lane request both public forms share. A request replaces an older
  /// in-flight one rather than being dropped: its enumeration is newer, so the last edge before the
  /// idle lane runs publishes at its own coverage.
  bool requestCommittedPassesOnPublicationLane(bool rebuildFromPassInventory);
  friend LoopSlotBodyApply applyLoadedLoopSlotBodyCommit(Loop&, Track&, uint8_t, bool,
                                                         PersistedLoopSnapshot&,
                                                         ImmutableLoopSnapshotRef);

  /// Drop the private scan (cursor and staged rows) but keep the retained hold intents.
  void discardPrivateSourceScan();
  /// Committed content changed — invalidate derived views.
  void notifyCommittedContentChanged();
  const char* rebuildOverdubSourceSpanCache(uint32_t requestedWindowStart = 0,
                                             uint32_t requestedWindowLength = 0);
  /// **Touched-coverage producer** (tasks 4.7/4.8). The worker is the span cache's *only* writer while a
  /// session is active: the grain that completes a tile publishes that tile's candidate notes **and** its
  /// prepared bit in one step, so "prepared" and "has content" cannot diverge. `derived` is the
  /// closure-padded derive for the tile's lookup window; rows that do not intersect the tile are not
  /// published. Never publishes a partial tile.
  void publishPreparedOverdubCoverageTile(uint16_t tile, const NoteUtils::DisplayNoteVec& derived,
                                          const SessionMidiEventVec& committedEvents,
                                          uint32_t tileStart, uint32_t tileLength);
  /// The view is a **read-only cut** of the cache: it never derives and never writes the cache. A private
  /// predecessor scan keep the cache whole; otherwise the cache holds exactly the published tiles.
  void recutOverdubSourceView();
  /// Seed the closure from the committed content at the session-open edge. The derive needs the maximum
  /// committed note span to pad both sides of a tile; without it, a note whose Off lies beyond its own
  /// tile's window is omitted by every tile. One committed-event pass, no derive.
  void seedOverdubCoverageClosureFromCommittedContent();
  /// Conservative closure (design D3, task 4.3): the maximum committed note span this slot has observed.
  /// Never shrinks inside a session, saturates to the loop, and is seeded from the full-loop prewarm when
  /// one exists at the session-open edge.
  void advanceOverdubCoverageClosure(uint32_t noteSpanTicks);
  /// Seed prepared coverage from a current full-loop prewarm at the session-open edge: the cache already
  /// describes the committed source for this generation, so no tile needs deriving. Content and provenance
  /// are the prewarm's own (C3.2/C3.5 stay intact for a private-origin cache, which is never adopted).
  void adoptPrewarmedOverdubCoverage();
  /// One deferred overlap completion (design D3's `SpanFinalization` / `OccupancyAtPhase` obligations).
  /// Parameters live here because the consequence is applied later, on the worker lane; the source data is
  /// re-derived from the prepared tiles rather than stored per obligation.
  struct DeferredOverdubCompletion {
    uint32_t startTick = 0;
    uint32_t endTick = 0;
    NoteId noteId = kInvalidNoteId;
    uint8_t channel = 0;
    uint8_t pitch = 0;
    uint8_t velocity = 0;
    bool occupancyOwed = false;
    bool spanOwed = false;
  };
  static constexpr uint8_t kDeferredOverdubCompletionCapacity = 16;
  /// Record (or merge into) the bounded deferred completion for a note. False on a full set: the caller
  /// then answers synchronously against a bounded local window rather than dropping the decision.
  bool retainDeferredOverdubCompletion(const DeferredOverdubCompletion& completion);
  /// Apply deferred completions whose tiles are now prepared, in obligation order (occupancy before its
  /// own span), exactly once. Runs on the worker/turn lane, never on the MIDI path. `maxDischarge` 0 means
  /// "drain everything ready"; a non-zero value bounds one turn (DEC-217) and leaves the rest owed.
  uint8_t dischargeDeferredOverdubCompletions(uint8_t maxDischarge = 0);
  /// The seal/deadline edge: a completion still owed when the interval ends is answered synchronously
  /// from a windowed derive of its own span, so no decision is dropped and none is applied twice.
  void materializeDeferredOverdubCompletionsAtDeadline();
  /// A completion owed beyond the full set, rebuilt from its own `Add` row (span owed, no occupancy:
  /// a note with a retained occupancy hold merges into that entry and never overflows).
  static DeferredOverdubCompletion overflowedCompletion(const PendingNoteChange& row);
  /// Worker lane: move owed overflow rows into the set's freed capacity. Never on the MIDI path.
  void promoteOverflowedOverdubCompletions();
#if defined(PIO_UNIT_TEST_NATIVE)
 public:
  /// DEC-217 witness: queue one owed completion with no prerequisite, so the per-turn bound can be
  /// asserted without replaying a full note hold across an unprepared tile.
  bool retainDeferredOverdubCompletionForTest(uint8_t channel, uint8_t pitch, uint32_t startTick,
                                              uint32_t endTick) {
    DeferredOverdubCompletion completion{};
    completion.channel = channel;
    completion.pitch = pitch;
    completion.startTick = startTick;
    completion.endTick = endTick;
    completion.noteId = kInvalidNoteId;
    return retainDeferredOverdubCompletion(completion);
  }
  /// DEC-237 3c1 witness: a completion with a closed span, so its consumer due tick is known.
  bool retainCoverageDeadlineForTest(uint8_t channel, uint8_t pitch, uint32_t startTick,
                                     uint32_t endTick) {
    DeferredOverdubCompletion completion{};
    completion.channel = channel;
    completion.pitch = pitch;
    completion.startTick = startTick;
    completion.endTick = endTick;
    completion.noteId = kInvalidNoteId;
    completion.spanOwed = true;
    return retainDeferredOverdubCompletion(completion);
  }
  uint8_t dischargeDeferredOverdubCompletionsForTest(uint8_t maxDischarge) {
    return dischargeDeferredOverdubCompletions(maxDischarge);
  }
  uint16_t overdubCompletionOverflowForTest() const { return overdubCompletionOverflowCount_; }
  void materializeDeferredOverdubCompletionsAtDeadlineForTest() {
    materializeDeferredOverdubCompletionsAtDeadline();
  }

 private:
#endif
  bool applyDeferredOverdubCompletion(const DeferredOverdubCompletion& completion);
  /// Apply the overlap consequence for one completed note against an explicit candidate source. Shared by
  /// the immediate note-off path, the discharge lane and the deadline fallback, so all three decisions
  /// have one implementation.
  bool applyOverlapConsequenceForCompletedNote(uint8_t channel, uint8_t pitch, uint8_t velocity,
                                               uint32_t startTick, uint32_t endTick,
                                               NoteId incomingNoteId,
                                               const OverlapNoteIdSet& overlapNoteIds,
                                               const OverlapNoteIdSet* occupancyResolvedNoteIds,
                                               bool pushAddChange,
                                               const NoteUtils::DisplayNoteVec* candidates = nullptr);
  uint32_t overdubSourceSpanOfNote(const NoteUtils::DisplayNote& note) const;
  uint32_t overdubSourceWindowLengthTicks() const;
  void resolveOverdubSourceWindow(uint32_t centerPhaseTick, uint32_t& windowStart,
                                  uint32_t& windowLength) const;
  /// Widen a requested source window to the interval the windowed derive actually reads: one bar of
  /// **loop-aware** pad on each side, capped at the whole loop. A clamped pad loses a wrap note whose
  /// note-on lies outside the window (measured: a wrap window resolved 0 of 3 notes), so the pad is
  /// taken modulo the loop.
  void resolvePaddedOverdubSourceWindow(uint32_t requestedStart, uint32_t requestedLength,
                                       uint32_t& deriveStart, uint32_t& deriveLength) const;
  void mergeDisplayNotesIntoOverdubSourceView(const NoteUtils::DisplayNoteVec& candidates);
  /// Merge committed `NoteOn` identities that pass-local pairing omitted for this window (a duplicate On
  /// at one tick, or an On whose Off is outside its own pass layer). The source view must carry the same
  /// identities playback and undo use, so the worker applies this per published tile.
  void mergeOmittedCommittedNoteOnIdentitiesForNotes(NoteUtils::DisplayNoteVec& dest,
                                                     const SessionMidiEventVec& events) const;
  bool committedPlaybackNoteOnIdentityValid(NoteId noteId) const;
  void retainValidOverdubSourceViewIdentities();
  /// --- CONTRACTS.md C3.2/C3.4: the private-predecessor source cursor -------------------------
  /// Bounded, resumable, interval- and revision-qualified. It lives in the Loop rather than in the
  /// custody receipt because the receipt's target size is contract-pinned (C2.1: 120 bytes, and
  /// `sizeof(CaptureStopJobs)` is pinned at 29,600 in `Track::placementPins`), while the work this
  /// cursor does is the Loop's own derived view. Identity is (receipt, loopId, interval, frozen
  /// content generation); `playbackRevision` is the supersession stamp.
  enum PrivateSourcePhase : uint8_t { kPrivateUnbound = 0, kPrivateRows = 1, kPrivateHolds = 2,
                                     kPrivateComplete = 3 };
  struct PrivateSourceHold {
    uint32_t holdPhaseTick = 0;
    uint8_t channel = 0;
    uint8_t pitch = 0;
    uint8_t velocity = 0;
  };
  struct PrivateSourceCursorState {
    const LoopEventStore::CaptureRange* range = nullptr;
    const void* receipt = nullptr;
    LoopId loopId = kInvalidLoopId;
    uint64_t interval = 0;
    uint64_t captureContentGeneration = 0;
    uint32_t playbackRevision = 0;
    uint32_t loopLengthTicks = 0;
    uint32_t startLoopTick = 0;
    size_t chunk = 0;
    uint16_t event = 0;
    size_t rowsRead = 0;
    size_t notesStaged = 0;
    size_t unitsLast = 0;
    size_t holdIndex = 0;
    bool holdLoaded = false;
    /// One derived-view refresh per materialization episode, so holds are resolved against the
    /// source that answers *now* (the private prefix, or the committed origin after supersession).
    bool holdViewRefreshed = false;
    uint8_t phase = kPrivateUnbound;
    PrivateSourceDeny deny = PrivateSourceDeny::None;
  };
  bool bindPrivatePredecessorSource(const PrivatePredecessorBinding& binding,
                                    PrivateSourceStep& outcome);
  PrivateSourceStep stepPrivatePredecessorRows(size_t quantum);
  PrivateSourceStep stepPrivatePredecessorHolds(size_t quantum);
  /// One charged increment on either store. A refusal is a named `Pressure`; nothing is grown
  /// uncharged, and every append that follows a proven slot cannot fail.
  bool ensureNoteCapacity(NoteUtils::DisplayNoteVec& notes, size_t needed);
  bool ensurePrivateStageCapacity();
  bool ensurePrivateViewCapacity(size_t needed);
  /// Prove both stores have room for one more pair *before* a row is consumed.
  bool ensurePrivateSourceCapacity();
  /// Identity/revision qualification. ANY identity or revision move is a supersession: the scan is
  /// dropped whole (never continued, never re-stamped) and retained intents stay.
  bool qualifyPrivatePredecessorSource(const PrivatePredecessorBinding& binding);
  NoteUtils::DisplayNote* privateStagedNoteAt(size_t index, const MidiEvent& row);
  NoteUtils::DisplayNote* privateFindOpenStagedNote(const MidiEvent& row);
  bool appendPrivatePair(const MidiEvent& row);
  bool closePrivatePair(const MidiEvent& row);
  bool finishPrivateRowScan();
  /// True when this interval's live pending set already carries `noteId` as its own accepted note.
  bool privateOwnAcceptedNote(NoteId noteId) const;
  bool retainPrivateHoldIntent(uint8_t channel, uint8_t pitch, uint8_t velocity,
                               uint32_t holdPhaseTick);
  void notePrivateSourceDenied(PrivateSourceDeny deny);
  /// The last grain's outcome. It survives `discard` on purpose: a denial names itself to the
  /// caller that provoked it.
  PrivateSourceStep privateSourceLastStep_ = PrivateSourceStep::Idle;
  PrivateSourceCursorState privateSource_{};
  PrivateSourceHold privateHolds_[kPrivateSourceHoldCapacity]{};
  size_t privateHoldCount_ = 0;
  OverlapNoteIdSet privateHoldTargets_{};
  size_t privateHoldTargetCursor_ = 0;
  bool privateHoldMaterializing_ = false;
  bool privateSourceSealDenied_ = false;
  SourceOrigin overdubSourceSpanCacheOrigin_ = SourceOrigin::None;
  /// Open On/Off pairing for the scan: one entry per row still awaiting its Off. Fixed storage, so
  /// a pair capacity overflow is a named denial rather than an allocation.
  static constexpr size_t kPrivateOpenPairCapacity = kPrivateSourceOpenPairCapacity;
  struct PrivateOpenPair {
    NoteId noteId = kInvalidNoteId;
    uint8_t pitch = 0;
    /// The staged note this On produced, so its Off closes the pair in constant time instead of
    /// scanning the prefix (C3.2: one pair decision is one unit of fixed cost).
    uint32_t stagedIndex = 0;
  };
  PrivateOpenPair privateOpenPairs_[kPrivateOpenPairCapacity]{};
  size_t privateOpenPairCount_ = 0;
  SessionMidiEventVec overdubSourceViewEvents_;
  NoteUtils::DisplayNoteVec overdubSourceViewNotes_;
  /// Full-loop span cache for note-off consume hydration. Built at source-view
  /// establish/rebuild and updated with pending Add/Shorten/Hide.
  NoteUtils::DisplayNoteVec overdubSourceSpanCacheNotes_;
  uint32_t overdubSourceSpanCacheLoopLengthTicks_ = 0;
  uint32_t overdubSourceSpanCachePlaybackRevision_ = 0;
  uint32_t overdubSourceSpanCachePublishedRevision_ = 0;
  /// Published root identity when the span cache was filled from `"snapshot"`;
  /// nullptr for fallback fills. OpenSpec 4.6 Stage 3 freshness key.
  const void* overdubSourceSpanCachePublishedIdentity_ = nullptr;
  bool overdubSourceSpanCacheValid_ = false;
  bool overdubSourceSpanCacheFullLoop_ = false;
  /// The interval the span cache's notes actually cover (the padded derive, not the request). Reuse is
  /// judged against this: an established cache is authoritative only while its coverage contains the
  /// interval the caller will evaluate. `fullLoop` still means "covers the whole loop" for the prewarm
  /// and the canonical-preparation gate.
  uint32_t overdubSourceSpanCacheCoverageStart_ = 0;
  uint32_t overdubSourceSpanCacheCoverageLength_ = 0;
  /// Touched-coverage tiles for this slot's overdub session (design D3, task 4.4). Required is session
  /// state and survives a source-generation change; prepared is invalidated with it. Lives here -- the
  /// session is per loop -- and never in `Track`, whose 384-byte DTCM pin forbids a member.
  CoveragePrerequisite::CoverageState overdubCoverage_;
  /// Worker cursor: the tile currently being prepared and its resumable per-pass traversal.
  LoopContentResolution::TilePrepCursor overdubCoverageCursor_{};
  uint16_t overdubCoverageCursorTile_ = CoveragePrerequisite::kMaxTiles;
  uint32_t overdubCoverageCursorGeneration_ = 0;
  /// Conservative closure pad for a tile's lookup window (design D3). Seeded from the session-open
  /// prewarm when one exists, grown by published notes, never shrunk inside a session.
  uint32_t overdubCoverageClosureTicks_ = 0;
  /// Bounded deferred overlap completions (one per note, coalesced by note identity).
  DeferredOverdubCompletion deferredOverdubCompletions_[kDeferredOverdubCompletionCapacity]{};
  uint8_t overdubCoverageDeferredCount_ = 0;
  /// Completed notes whose consequence is owed beyond the full set: `Add` rows flagged `consequenceOwed`.
  /// Fits the padding before the next word, so the pinned `sizeof(Loop)` is unchanged.
  uint16_t overdubCompletionOverflowCount_ = 0;
  uint32_t overdubSourceViewLoopLengthTicks_ = 0;
  bool overdubSourceViewEstablished_ = false;
  bool fallbackGatherSessionActive_ = false;
  uint32_t fallbackGatherSessionWindowStart_ = 0;
  uint32_t fallbackGatherSessionWindowLength_ = 0;
  uint32_t fallbackGatherSessionPlaybackRevision_ = 0;
  bool fallbackGatherSessionIncludeActiveEditRows_ = true;
  CommittedEventRange::WindowTraversalCursor fallbackGatherSessionCursor_{};
  SessionMidiEventVec fallbackGatherSessionEvents_;
  bool fallbackGatherSessionTraversalComplete_ = false;
  VisualCacheIdleGatherSource lastVisualCacheIdleGatherSource_ = VisualCacheIdleGatherSource::None;
  bool lastVisualCacheIdleOverlayRejected_ = false;
  bool lastVisualCacheIdleCopiedSnapshotNotes_ = false;
  /// Which provider `collectQualifiedCommittedDisplayNotes` last served (or None). One byte; the display
  /// reads it only to name the source in the capture-gated `DIAG,disp_turn` row. `mutable` because the
  /// provider itself is a **read** of the loop's committed content: the display owner holds a `const Loop&`
  /// and this stamp is the diagnostic that read leaves behind, never content.
  mutable QualifiedDisplaySource lastQualifiedDisplaySource_ = QualifiedDisplaySource::None;
  /// Set only while `refreshVisualCacheAfterPassStateChange` fills idle slices.
  bool visualCachePassStateRefreshActive_ = false;
  /// Non-owning. Null means the identity filter is inactive. Points at
  /// `LoopPlaybackRuntime::mergedMidiEvents` after a full-loop rebuild.
  const PlaybackMergedMidiEvents* committedPlaybackMergedForIdentity_ = nullptr;
  PendingNoteChangeVec pendingNoteChanges_;
  OverlapHoldTotals overlapHoldTotals_;
  bool overdubWrapArmed_ = false;
  bool overdubWrapSuppressNext_ = false;
  std::vector<PassId> overdubSessionPassIds_;
  std::vector<EditPassIdList> overdubSessionCompanionIds_;
  size_t overdubSessionCursor_ = 0;
  uint64_t overdubSessionUndoGeneration_ = 0;
  SessionMidiEventVec overdubSessionLiveUndoEvents_;

  void freeActiveCapturePassChunks();
  /// Shared staging implementation. `prewarmedChunkIds` is supplied only by
  /// the active admission path and already has enough ownership-row capacity
  /// for the capture chunks it is allowed to detach.
  SealOutcome stagePreparedCapturePassWithChunkStorage(
      Capture& source, uint32_t sealedAtTick, CommitReason reason, CommittedChunkIdList* prewarmedChunkIds);
  bool preparedCompanionRecycleSourceCurrent() const;
  void publishPreparedCompanionHistory(); // No-fail publication after joint preflight.
  void markPassDerivedStale();
  void resetFallbackGatherSession();
  bool fallbackGatherSessionMatches(uint32_t windowStart, uint32_t windowLength,
                                    uint32_t playbackRevision,
                                    bool includeActiveEditRows) const;
  bool advanceFallbackGatherSession(size_t eventBudget, uint32_t windowStart,
                                    uint32_t windowLength, bool includeActiveEditRows,
                                    SessionMidiEventVec& out);
  /// RC-E attribution: cached note coverage vs loop bars at a rebuild or staleness boundary.
  void emitVisualCacheState(const char* phase, int32_t gatheredEvents) const;
  void emitVisualCacheIdleGatherPath() const;

  std::unique_ptr<PlaybackOrderVec> playbackOrder_;
  std::unique_ptr<NoteUtils::CachedNoteList> noteCache_;
  std::unique_ptr<NoteUtils::EventIndex> cachedEventIndex_;
  std::unique_ptr<PublishedSnapshotBox> publishedSnapshotBox_;
  std::unique_ptr<PendingSnapshotPublishBox> pendingSnapshotPublishBox_;
  /// Externally allocated ring of session-exit receipts; the type stays
  /// incomplete here, so the deleter is defined beside the box definition.
  struct SessionExitReceiptsDeleter {
    void operator()(SessionExitSnapshotHandoff* receipts) const;
  };
  std::unique_ptr<SessionExitSnapshotHandoff[], SessionExitReceiptsDeleter>
      sessionExitReceipts_;
  size_t sessionExitReceiptHead_ = 0;
  size_t sessionExitReceiptCount_ = 0;
  /// One receipt per open session: the handoff swaps the session identities out,
  /// so a second handoff for the same session would queue an empty successor.
  /// Cleared by `openOverdubSession`.
  bool sessionExitHandedOff_ = false;
  /// Task 4.20: the reserved obligation's ownership state. Deliberately a single byte placed in the
  /// padding that already follows `sessionExitHandedOff_` before the 8-aligned generation below, so
  /// the measured `sizeof(Loop)` shell does not grow: the obligation's payload and session identity
  /// live in the externally allocated record beside the ring, not in the shell.
  SessionExitPublishClaim sessionExitPublishClaimState_ = SessionExitPublishClaim::Empty;
  /// The reserved obligation record, one slot past the FIFO ring. Null until
  /// `ensureSessionExitReceiptRing()` has run; never reachable via head/count.
  SessionExitSnapshotHandoff* reservedSessionExit();
  const SessionExitSnapshotHandoff* reservedSessionExit() const;
  /// `Publishing -> Empty`, exactly once: swap the ready reserved record into the receipt ring and
  /// clear the record. Returns false when no free slot exists yet (nothing changes).
  bool moveReservedSessionExitIntoReceiptRing();
  uint64_t pendingSnapshotCompanionGeneration_ = 0; // Clear/swap ABA guard; appends use size.
#if defined(PIO_UNIT_TEST_NATIVE)
  size_t snapshotFragmentFailAfterUnitsForTest_ = static_cast<size_t>(-1);
  LoopSnapshotFailureReason snapshotFragmentFailReasonForTest_ =
      LoopSnapshotFailureReason::Injected;
#endif

  void clearPublishedSnapshot();
  /// `revision` is the `playbackRevision` the snapshot content represents. Zero
  /// selects the loop's current `playbackRevision` (session-exit publication, where
  /// the candidate is complete as of now).
  /// `coveredPassRevision` (DEC-073) is the committed-pass revision the content was enumerated
  /// from. A deferred publication (session exit, wrap) must pass its own watermark; synchronous
  /// publication passes `kSnapshotCoverageCurrent`. `kSnapshotCoverageUnset` records no coverage,
  /// which is conservatively never current.
  void storePublishedSnapshot(ImmutableLoopSnapshotRef snapshot, uint32_t revision = 0,
                              uint32_t coveredPassRevision = kSnapshotCoverageUnset);
  void clearPendingSnapshotPublish();
  void discardUnpublishedSnapshotIfNotSessionExit();
  void enqueueLiveOverdubPasses();
  void appendPendingSnapshotCompanionId(EditPassId id);
  void applyPendingSnapshotCompanionsToDisplayNotes(NoteUtils::DisplayNoteVec& notes) const;
};

/// 4.2 checkpoint parity: the effective committed events of a **staged** pass state, without a Loop instance.
/// Same projection the playback fallback uses (`Loop::copyEffectiveCommittedEventsInRange` delegates here).
void copyEffectiveCommittedEventsFromPasses(const LoopPasses& passes, uint32_t loopLengthTicks,
                                            uint32_t windowStart, uint32_t windowLength,
                                            SessionMidiEventVec& out,
                                            bool includeActiveEditRows = false);

#endif  // LOOP_H
