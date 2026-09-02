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

class Track;
struct PlaybackMergedMidiEvents;

using PlaybackOrderVec = std::vector<size_t, ExternalMemoryFirstAllocator<size_t>>;

struct PublishedSnapshotBox;
struct PendingSnapshotPublishBox;

/// Overdub-session totals for `#CAP,DIAG,overlap_hold`. Incremented on note-off; emitted at stop.
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
  Loop();
  ~Loop();
  Loop(const Loop&) = delete;
  Loop& operator=(const Loop&) = delete;

  Capture capture;
  uint16_t captureNextEventIndex = 0;
  bool captureEventsSortDirty = false;
  bool captureHasLastAppendTick_ = false;
  uint32_t captureLastAppendTick_ = 0;
  uint16_t captureDisplayRevision = 0;
  uint32_t captureDedupEventsDropped_ = 0;

  LoopId loopId = kInvalidLoopId;
  uint32_t playbackRevision = 0;
  LoopPasses passes;
  PassId nextPassId_ = 1;
  NoteId nextNoteId_ = 1;
  bool editStateDirty_ = false;
  bool hasPendingCapturePass_ = false;
  PendingCapturePass pendingCapturePass_;
  VisualCache visualCache;
  bool deferredVisualCacheWrapAppendPending_ = false;
  CapturePreview capturePreview;
  /// Seal handoff: capture rows kept alive past `capturePreview.clear()` until the committed
  /// representation the painter selects proves it covers `sealedCapturePreviewRevision_`. Copied
  /// once per seal and never rebuilt per frame. `Loop` owns these; display only consumes them.
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
  size_t activeCapturePassCount() const;

  bool hasCommittedPasses() const;
  uint32_t findLastCommittedEventTick() const;
  /// Bar-aligned span of committed pass MIDI (0 when no committed passes).
  uint32_t committedBarAlignedContentLengthTicks() const;
  /// When committed passes MIDI exists, never return a length below content-derived bars.
  uint32_t reconcileLoopLengthWithCommittedPasses(uint32_t candidateLengthTicks) const;

  void mergeActiveCapturePasses(MidiEventVec& out) const;
  void mergeActiveCapturePasses(SessionMidiEventVec& out) const;
  /// Canonical committed-pass event gathering (full loop). Prefer over display-only helpers.
  void gatherCommittedEvents(SessionMidiEventVec& out) const;
  void gatherCommittedEvents(MidiEventVec& out) const;
  static void resetCommittedPitchQueryWork();
  static uint32_t committedEventsFullMaterializeCount();
  /// Windowed committed gathering — wrap-aware chunk skip + event filter.
  /// `includeActiveEditRows` controls whether active note edit rows are projected.
  void gatherCommittedEventsInWindow(SessionMidiEventVec& out, uint32_t windowStart,
                                     uint32_t windowLength,
                                     bool includeActiveEditRows = true) const;
  void gatherCommittedEventsInWindow(MidiEventVec& out, uint32_t windowStart,
                                     uint32_t windowLength,
                                     bool includeActiveEditRows = true) const;
  /// Committed window plus live capture.store events in the same window.
  void gatherCommittedEventsInWindowWithCapture(SessionMidiEventVec& out, uint32_t windowStart,
                                                uint32_t windowLength) const;
  /// DEC-016 policy owner — aliases gatherCommittedEvents (legacy name).
  void gatherCommittedEventsForDerivedView(SessionMidiEventVec& flat) const;
  void gatherCommittedEventsForDerivedView(MidiEventVec& flat) const;
  /// Materialized events policy plus live capture.store merge (playback during record/overdub).
  void gatherCommittedEventsWithCapture(SessionMidiEventVec& flat) const;
  void gatherCommittedEventsWithCapture(MidiEventVec& flat) const;

  /// True when synchronous full visual-cache rebuild should be avoided (unbounded cost).
  bool shouldAvoidFullVisualRebuild(uint32_t loopLength) const;

  /// Lazy full flatten for edit/MIDI paths that still require the entire loop.
  void ensureEffectiveEventStoreCurrent() const;
  void copyEffectiveCommittedEvents(SessionMidiEventVec& out) const;
  /// Range query of committed content (chunk window + optional edit projection).
  /// Does not flatten the loop. Companion fast path copies a ready Overdub
  /// projection cache only; a miss uses the windowed apply path. Full companion
  /// rebuild is not admitted from this gather.
  void copyEffectiveCommittedEventsInRange(SessionMidiEventVec& out, uint32_t windowStart,
                                           uint32_t windowLength,
                                           bool includeActiveEditRows = true) const;
  uint32_t effectiveEventStoreRevision() const { return effectiveEventStoreRevision_; }

  SessionMidiEventVec& midiEvents();
  const SessionMidiEventVec& midiEvents() const;

  void rematerializeEditView(LoopEventStore& store) const;

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
  /// Overdub session start S (`beginCapture` playheadPhaseTick). UINT32_MAX = no session.
  uint32_t playheadPhaseTick = UINT32_MAX;
  void openOverdubSession(uint32_t sessionPlayheadPhaseTick);
  void closeOverdubSession();
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
  /// using the source window filter. Cache fill follows the paint draw-source
  /// predicate (DEC-052): published snapshot when true; otherwise prepared →
  /// prior → covering visual → passes. Not visual-cache ownership.
  /// `why` is CAP `open` / `wrap` / `undo` / `redo` / `idle`.
  void rebuildOverdubSourceView(uint32_t playheadPhaseTick, const char* why = "wrap");
  /// Append display notes from `lastCommittedPassId()` chunks into the existing
  /// source view and restamp the span cache to the current `playbackRevision`.
  /// Does not merge that pass into `visualCache` (205138 wrap-2 205→271).
  /// Walks only that pass. Source-view merge skips NoteIds already present.
  /// Wrap-held heads/tails stay on wrap-edge display append. Idle slice is
  /// the visualCache fill for new wrap body notes.
  void appendLastCommittedOverdubPassToSourceView();
  /// Append candidates missing from `visualCache.notes` by pitch+start. Does not
  /// replace an existing end and does not clear dirty bars.
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
  /// No-op when already ready or when capture/session state is active.
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
#if defined(PIO_UNIT_TEST_NATIVE)
  void replaceOverdubSourceViewNotesForTest(NoteUtils::DisplayNoteVec notes);
  void pushPendingNoteChangeForTest(PendingNoteChange change);
  bool overdubSourceSpanCacheReadyForTest() const { return overdubSourceSpanCacheReady(); }
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
  bool hasPendingNoteChanges() const { return !pendingNoteChanges_.empty(); }
  const PendingNoteChangeVec& pendingNoteChanges() const { return pendingNoteChanges_; }
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
  /// When occupy ids are empty or have no source-view row, fall back to present-at-hold
  /// participants at `holdPhaseTick` (`010926` norowid, `104129` ledger n=0).
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
  EditPassIdList sealPendingNoteChangesToEditPasses(
      bool updateOverdubSourceView = true);
  /// Merge pending Add/Shorten/Hide into `overdubSourceViewNotes_`. Wrap commit
  /// uses `appendLastCommittedOverdubPassToSourceView`. Full rebuild is idle /
  /// later, not the wrap clock tick. Does not clear pending.
  void applyPendingNoteChangesToOverdubSourceView();
  /// Apply pending effective Hide/Shorten onto a display-note list. Not Add
  /// (live capture already has those). Hide matches `noteId` or same-pitch overlapping
  /// span on the committed prefix only — notes at `liveCaptureBegin` and after are
  /// playhead tails and must keep growing to currentTick. Does not mutate source view.
  /// Sealed companion Hide/Shorten still project while companion IDs exist, including
  /// after the selected representation reaches the seal revision, so a Hide target
  /// is not re-admitted at the same pitch+start. Overlay Hide also drops a longer
  /// same-pitch+start sibling of the kept short overdub.
  void applyPendingNoteChangesToDisplayNotes(
      NoteUtils::DisplayNoteVec& notes,
      size_t liveCaptureBegin = static_cast<size_t>(-1)) const;
  /// True when the last seal still has companion EditPass IDs to project.
  bool hasSealedCompanionOverlay() const;
  /// True when the selected committed representation is still behind the last seal.
  bool sealedCompanionOverlayRequired() const;
  const OverlapHoldTotals& overlapHoldTotals() const { return overlapHoldTotals_; }
  /// One `#CAP,DIAG,overlap_hold` at overdub-stop seal. SESSION_CAPTURE / FLASHMEM only.
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
  /// Drops the one-shot merged-stream admit receipt only. Companion IDs and
  /// `lastOverdubSealPlaybackRevision_` stay for display overlay until
  /// `clearLastOverdubSeal()`. Snapshot-current does not release a Hide that
  /// is still present at the same pitch+start.
  void clearLastOverdubSealAdmitReceipt();
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
  void storePublishedSnapshotForTest(ImmutableLoopSnapshotRef snapshot, uint32_t revision) {
    storePublishedSnapshot(std::move(snapshot), revision);
  }
  const LoopSnapshotChangeSet* pendingWrapJournalChangesForTest() const;
#endif
  ImmutableLoopSnapshotRef publishedSnapshot() const;
  /// True when the published root was stored at the current `playbackRevision`.
  bool publishedSnapshotIsCurrent() const;
  /// Non-owning identity of the current published root, or nullptr when none is
  /// selected or the published root is stale versus `playbackRevision`.
  const void* publishedSnapshotIdentity() const;
  /// Revision captured when the published root was stored. Zero when no root exists.
  uint32_t publishedSnapshotRevision() const;

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
  /// Copies the live preview rows once, before the seal clears them.
  void retainCapturePreviewAtSeal();
  /// True when `visualCache` has consumed the sealed revision. A current published
  /// snapshot is not membership proof (`012714` wrap 2: snap=1 dropped sealed=47).
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
    Fallback = 3,
  };
  VisualCacheIdleGatherSource lastVisualCacheIdleGatherSource() const {
    return lastVisualCacheIdleGatherSource_;
  }
  bool lastVisualCacheIdleOverlayRejected() const { return lastVisualCacheIdleOverlayRejected_; }

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
  bool queueSessionExitSnapshot(CommitReason reason, const EditPassIdList& companionIds = {});
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
  void mergeMaterializedPassesWithCapture(MidiEventVec& out) const;
  void mergeMaterializedPassesWithCapture(SessionMidiEventVec& out) const;
  void rebuildVisualCacheFromPasses();
  /// Reconstruct each active overdub pass with `overdubPassWrapPairing` and
  /// append notes that merged record+overdub reconstruct omitted (015618).
  /// Skip a candidate when pitch+start is already present. `wrapHeldOnly`
  /// keeps wrap-held heads/tails and drops interior body notes (204132).
  /// Source view uses `wrapHeldOnly=false`. One pass at a time.
  void appendOverdubPassWrapPairedNotes(NoteUtils::DisplayNoteVec& notes,
                                        bool wrapHeldOnly = false) const;
  /// Visual-cache entry: wrap-held heads/tails only. Interior wrap-paired
  /// body notes stay off the piano-roll list (204132 / NOTE_EDIT open).
  void appendOverdubPassDisplayNotes(NoteUtils::DisplayNoteVec& notes) const;
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
  void discardPassesMaterializedCache();
  /// Phase 1B — discard stale materialized store when owner guards pass.
  bool tryDiscardPassesMaterializedCache();
  void discardPassesMaterializedEventsCache() { passesMaterializedStore_.discardEventsCache(); }
  void commitStopFinalizeFromStore(LoopEventStore& merged);

  SealOutcome sealCapture(uint32_t sealedAtTick,
                          CommitReason reason = CommitReason::RecordStop);
  /// Wrap-window synthetic note-offs on live capture.store (record/overdub stop policy).
  LoopStopFinalize::Result finalizeCaptureWrapWindowAtStop(uint32_t stopAbsTick);
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
  void assignMissingNoteIds(SessionMidiEventVec& events);
  void assignMissingNoteIds(MidiEventVec& events);
  void assignMissingNoteIdsInStore(LoopEventStore& store);
  void assignMissingNoteIdsInStoreWindow(LoopEventStore& store, size_t beginIndex,
                                         uint32_t includeUntil);
  /// Fill kInvalidNoteId on active record/overdub committed chunks. Same allocateNoteId
  /// as InStore. In-place; chunk ids unchanged so playback refs stay valid.
  void assignMissingNoteIdsInCommittedCapturePasses();

  void clearCaptureOnNewPass();

  /// Rebuild passesMaterializedStore_ from committed passes when stale.
  void materializeEditViewFromPasses() const;
  bool isPassesMaterializedStoreFresh() const { return !passesMaterializedStoreStale_; }

 private:
  friend class TrackUndo;

  PassesMaterializedEventStore passesMaterializedStore_;
  bool passesMaterializedStoreStale_ = true;
  uint32_t effectiveEventStoreRevision_ = 0;

  /// Committed content changed — invalidate derived views and rebuild effective store eagerly.
  void notifyCommittedContentChanged();
  void rebuildEffectiveEventStore() const;
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
  SessionMidiEventVec overdubSessionLiveUndoEvents_;

  void freeActiveCapturePassChunks();
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

  void clearPublishedSnapshot();
  /// `revision` is the `playbackRevision` the snapshot content represents. Zero
  /// selects the loop's current `playbackRevision` (session-exit publication, where
  /// the candidate is complete as of now).
  void storePublishedSnapshot(ImmutableLoopSnapshotRef snapshot, uint32_t revision = 0);
  void clearPendingSnapshotPublish();
  void discardUnpublishedSnapshotIfNotSessionExit();
  void enqueueLiveOverdubPasses();
};

#endif  // LOOP_H
