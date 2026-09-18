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
#include "Utils/ExternalMemoryFirstAllocator.h"
#include "Globals.h"
#include "CaptureAppendResult.h"
#include "ActiveNoteLedger.h"
#include "OverlapNoteIdSet.h"
#include "PassReclaim.h"
#include "PendingNoteChange.h"
#include "Utils/LoopStopFinalize.h"

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
  uint32_t captureLastAppendTick_ = 0;
  uint16_t captureDisplayRevision = 0;
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
  /// `coverage.lengthTicks != coverage.loopLengthTicks`, and
  /// `publishPreparedPlaybackPairFromWorkingWindow` requires
  /// `isFullLoopMergedPlaybackWindow` -- while the windowed policy never builds a
  /// full-loop window once `shouldAvoidFullVisualRebuild` holds. Past that point the
  /// three views' worth of windowed capacity is still needed (the live-record and
  /// handoff lanes use them), but the bridges, their immutable bases and the prepared
  /// pair can never be published, so reserving them is dead reservation by
  /// construction.
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
  void armOverdubWrapAfterLeavingStart(uint32_t currentPhase);
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
  bool overdubSourceSpanCacheReady() const;
  /// Ready storage that may be reused for the current committed source.
  /// A current published snapshot requires matching published-revision **and**
  /// published-identity stamps (OpenSpec 4.6 Stage 3).
  bool overdubSourceSpanCacheReusable() const;
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
    return rebuildOverdubSourceSpanCache(requestedWindowStart, requestedWindowLength);
  }
  const NoteUtils::DisplayNoteVec& overdubSourceSpanCacheNotesForTest() const {
    return overdubSourceSpanCacheNotes_;
  }
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
  /// One unit is row metadata, a payload/snapshot item, an unused tail row,
  /// or one staged-capture event/chunk-boundary comparison.
  bool stepPreparedCompanionHistory(size_t quantum, uint64_t owner = 0);
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
  bool commitPreparedCaptureAndCompanionsAtomic(PreparedCapturePassTransfer& transferred,
                                               uint64_t owner = 0,
                                               PendingCapturePass* pass = nullptr);
  const EditPassIdList* preparedCompanionHistoryIds(uint64_t owner = 0) const;
  /// Caller has consumed the ID receipt. Recycling ends that receipt's lifetime.
  /// No active allocation/free; returns to Prewarmed within the remaining horizon.
  bool beginRecyclePreparedCompanionHistory(uint64_t owner = 0);
  bool stepRecyclePreparedCompanionHistory(size_t quantum, uint64_t owner = 0);
  bool preparedCompanionHistoryRecycleComplete(uint64_t owner = 0) const;
  uint64_t preparedCompanionHistoryGeneration() const { return preparedCompanionHistory_.preparationGeneration; }
  bool preparedCompanionHistoryPrewarmed(size_t requiredCompanions = 0) const;
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
    PassesResolve = 4,
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
  /// receives the branch actually taken (`None` / `Snapshot` / `Overlay`).
  bool tryGatherPublishedSnapshotEventsInWindow(SessionMidiEventVec& out, uint32_t windowStart,
                                                uint32_t windowLength,
                                                SnapshotWindowGatherPath* pathOut = nullptr) const;
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
  bool canHandoffSessionExitSnapshot() const;
  bool handoffSessionExitSnapshot(CommitReason reason);
  /// FIFO of session-exit receipts. Sized to the stop-job ring so a successor
  /// session is never serialized behind an unconsumed publisher turn. Index 0 is
  /// the oldest pending receipt; the idle publisher consumes that one per turn.
  /// The gate is a free slot; one receipt per open session is enforced by the
  /// handoff itself, which consumes the session identities.
  static constexpr size_t kSessionExitReceiptCapacity = 4;
  size_t sessionExitReceiptCount() const { return sessionExitReceiptCount_; }
  /// DEC-068: no interval-level session-exit tail (ring full, no session, or not a session exit).
  static constexpr size_t kInvalidSessionExitReceipt = static_cast<size_t>(-1);
  /// Capture this interval's own session-exit tail at its stop admission, before any successor
  /// can touch the shared session state. Swaps the identities into a reserved receipt slot
  /// (never copies: the stop boundary must not allocate) and leaves it not ready until
  /// `markSessionExitTailReady`. Returns the receipt's ring index, or
  /// `kInvalidSessionExitReceipt`.
  size_t captureSessionExitTail(CommitReason reason);
  /// Fill the captured tail's final pass id and make it consumable by the publisher.
  bool markSessionExitTailReady(size_t ringIndex, PassId lastPassId);
  /// The captured receipt behind a ring index. Jobs hold ring indexes: offsets shift as
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
  bool hasPendingWrapJournal() const;
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
  /// Closed NoteOn identities in committed events but missing from `dest`
  /// (snapshot NoteRef / resolve miss). Cold path — kept for non-snapshot
  /// source-view origins and diagnostics; visual/adopt call sites unwound.
  void mergeOmittedCommittedNoteOnIdentitiesInto(NoteUtils::DisplayNoteVec& dest) const;
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
  /// Resume wrap-pair append that was deferred during runtime-priority idle slices.
  /// Appends only when `deferredVisualCacheWrapAppendFromRevision_` still equals
  /// `playbackRevision`. A mismatch obtains the current-source frame instead of
  /// appending an earlier last pass (`203406`).
  void applyDeferredVisualCacheWrapAppend();
  bool hasDeferredVisualCacheWrapAppend() const {
    return deferredVisualCacheWrapAppendPending_;
  }
  /// After pass-state change: mark stale and fill short loops via idle slices (same oracle
  /// as PLAYING). Long loops stay idle-deferred — no `VCACHE,full` flatten+append.
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
  /// Stage an already canonicalized capture using one prewarmed transfer row.
  /// This is the active-path form of `stagePreparedCapturePass`; it denies
  /// instead of allocating when the declared horizon is exhausted.
  SealOutcome stagePreparedCapturePassFromActiveAdmission(uint32_t sealedAtTick,
                                                           CommitReason reason);
  /// Stage a job-owned canonical capture. Its source is consumed on success;
  /// later publication must not retire the Loop's independent live capture.
  SealOutcome stagePreparedCapturePassFromActiveAdmission(
      Capture& source, uint32_t sealedAtTick, CommitReason reason);
  /// Wrap-window synthetic note-offs on live capture.store (record/overdub stop policy).
  LoopStopFinalize::Result finalizeCaptureWrapWindowAtStop(uint32_t stopAbsTick);
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

  void clearCaptureOnNewPass();

 private:
  friend class TrackUndo;
  friend void applyLoadedLoopSlotBodyCommit(Loop&, Track&, uint8_t, bool,
                                            PersistedLoopSnapshot&, ImmutableLoopSnapshotRef);

  /// Committed content changed — invalidate derived views.
  void notifyCommittedContentChanged();
  const char* rebuildOverdubSourceSpanCache(uint32_t requestedWindowStart = 0,
                                             uint32_t requestedWindowLength = 0);
  uint32_t overdubSourceWindowLengthTicks() const;
  void resolveOverdubSourceWindow(uint32_t centerPhaseTick, uint32_t& windowStart,
                                  uint32_t& windowLength) const;
  void mergeDisplayNotesIntoOverdubSourceView(const NoteUtils::DisplayNoteVec& candidates);
  bool committedPlaybackNoteOnIdentityValid(NoteId noteId) const;
  void retainValidOverdubSourceViewIdentities();
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

#endif  // LOOP_H
