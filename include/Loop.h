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
#include "VisualCache.h"
#include "TrackState.h"
#include "Utils/NoteUtils.h"
#include "Utils/ExternalMemoryFirstAllocator.h"
#include "Globals.h"
#include "CaptureAppendResult.h"
#include "OverlapNoteIdSet.h"
#include "PassReclaim.h"
#include "PendingNoteChange.h"
#include "Utils/LoopStopFinalize.h"

class Track;

using PlaybackOrderVec = std::vector<size_t, ExternalMemoryFirstAllocator<size_t>>;

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
};

struct Loop {
  Capture capture;
  uint16_t captureNextEventIndex = 0;
  bool captureEventsSortDirty = false;
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
  CapturePreview capturePreview;
  VisualCacheDelta pendingVisualDelta;
  bool visualCacheDirty = true;
  size_t materializedEventCount_ = 0;
  uint32_t nextMergeSequence_ = 0;
  PassId lastCommittedPassId_ = kInvalidPassId;

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
  void gatherCommittedEventsInWindow(SessionMidiEventVec& out, uint32_t windowStart,
                                     uint32_t windowLength) const;
  void gatherCommittedEventsInWindow(MidiEventVec& out, uint32_t windowStart,
                                     uint32_t windowLength) const;
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
  /// Range query of committed content (chunk window + edit rows). Does not flatten the loop.
  void copyEffectiveCommittedEventsInRange(SessionMidiEventVec& out, uint32_t windowStart,
                                           uint32_t windowLength) const;
  uint32_t effectiveEventStoreRevision() const { return effectiveEventStoreRevision_; }

  SessionMidiEventVec& midiEvents();
  const SessionMidiEventVec& midiEvents() const;

  void rematerializeEditView(LoopEventStore& store) const;

  EditPassId saveNoteEditPass(uint8_t editPassIndex, EditPass row,
                              EditPassType passType = EditPassType::Note);
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
  bool hasOverdubSession() const { return playheadPhaseTick != UINT32_MAX; }
  void armOverdubWrapAfterLeavingStart(uint32_t currentPhase);
  bool shouldCommitOverdubWrap(uint32_t prevPhase, uint32_t currentPhase) const;
  void noteOverdubWrapCommitted();
  /// Move unpaired capture NoteOns out of `capture.store` (6E.5). Returns extracted count.
  size_t extractOpenCaptureNoteOns(SessionMidiEventVec& out);
  void pushOverdubSessionPass(PassId passId, EditPassIdList companionIds);
  void dropOverdubSessionRedoTail();
  bool undoOverdubSession();
  bool redoOverdubSession();
  bool canUndoOverdubSession() const;
  bool canRedoOverdubSession() const;
  size_t overdubSessionUndoDepth() const;
  size_t overdubSessionRedoDepth() const;
  /// Establish overdubSourceView: consume prepared LCR window when ready, else copy clean
  /// visualCache.notes (3b), else a windowed chunk walk.
  void establishOverdubSourceView(uint32_t playheadPhaseTick);
  /// D2: merge hold-window display notes into the session source view for overlap lookup.
  void ensureOverdubSourceNotesForHold(uint32_t holdPhaseTick, uint8_t pitch);
  void clearOverdubSourceView();
  bool hasOverdubSourceView() const { return overdubSourceViewEstablished_; }
  uint32_t overdubSourceViewLoopLengthTicks() const { return overdubSourceViewLoopLengthTicks_; }
  const SessionMidiEventVec& overdubSourceViewEvents() const { return overdubSourceViewEvents_; }
  const NoteUtils::DisplayNoteVec& overdubSourceViewNotes() const { return overdubSourceViewNotes_; }

  /// Session pending logical delta (Add/Shorten/Hide) — not a timeline pass.
  void clearPendingNoteChanges();
  bool hasPendingNoteChanges() const { return !pendingNoteChanges_.empty(); }
  const PendingNoteChangeVec& pendingNoteChanges() const { return pendingNoteChanges_; }
  /// Resolve incoming note against hold-candidate ids, then geometry + [S, E).
  /// Wrap-head off (`endTick < startTick`) consumes the tail `[S, loopLength)` only.
  /// Empty `overlapNoteIds` skips span lookup (Add only). Returns false when no source view.
  bool accumulatePendingNoteChangesForIncomingNote(uint8_t channel, uint8_t pitch, uint8_t velocity,
                                                   uint32_t startTick, uint32_t endTick,
                                                   NoteId incomingNoteId,
                                                   const OverlapNoteIdSet& overlapNoteIds);
  /// Pair one linear incoming `[S, E)` against an explicit source-note list (Shorten/Hide only).
  void accumulatePendingNoteChangesFromSourceNotes(const NoteUtils::DisplayNoteVec& sourceNotes,
                                                   uint8_t channel, uint8_t pitch, uint8_t velocity,
                                                   uint32_t startTick, uint32_t endTick,
                                                   NoteId incomingNoteId);
  /// Encode pending Shorten/Hide into EditPass rows (call after OverdubPass publish). Clears pending.
  EditPassIdList sealPendingNoteChangesToEditPasses();
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
  bool captureActive() const;
  size_t liveEventCount() const;
  /// Committed pass event count + active capture size; builds visual cache if needed. Display/LED only.
  size_t displayEventCountHint() const;
  bool ensureCaptureEventsSorted();
  void mergeMaterializedPassesWithCapture(MidiEventVec& out) const;
  void mergeMaterializedPassesWithCapture(SessionMidiEventVec& out) const;
  void rebuildVisualCacheFromPasses();
  /// Rebuild up to `maxBarsPerSlice` dirty bars, preferring `priorityBar`.
  /// When `maxBarDistanceFromPriority` is finite, skip dirty bars outside that neighborhood
  /// (PLAYING viewport backfill); pass UINT32_MAX for full-loop idle backfill when stopped.
  void rebuildVisualCacheIdleSlice(uint8_t maxBarsPerSlice, uint32_t priorityBar,
                                   uint32_t maxBarDistanceFromPriority = UINT32_MAX);
  void ensureVisualCacheBuilt();
  void markDisplayCachesStale();
  /// 6B: dirty only bars touched by `committedPassId` and companion edit rows. No whole-loop reconstruct.
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

  SealOutcome sealCapture(uint32_t sealedAtTick);
  /// Wrap-window synthetic note-offs on live capture.store (record/overdub stop policy).
  LoopStopFinalize::Result finalizeCaptureWrapWindowAtStop(uint32_t stopAbsTick);
  bool commitPendingCapturePass();
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
  uint32_t overdubSourceWindowLengthTicks() const;
  void resolveOverdubSourceWindow(uint32_t centerPhaseTick, uint32_t& windowStart,
                                  uint32_t& windowLength) const;
  void mergeDisplayNotesIntoOverdubSourceView(const NoteUtils::DisplayNoteVec& candidates);
  SessionMidiEventVec overdubSourceViewEvents_;
  NoteUtils::DisplayNoteVec overdubSourceViewNotes_;
  uint32_t overdubSourceViewLoopLengthTicks_ = 0;
  bool overdubSourceViewEstablished_ = false;
  PendingNoteChangeVec pendingNoteChanges_;
  OverlapHoldTotals overlapHoldTotals_;
  bool overdubWrapArmed_ = false;
  std::vector<PassId> overdubSessionPassIds_;
  std::vector<EditPassIdList> overdubSessionCompanionIds_;
  size_t overdubSessionCursor_ = 0;
  SessionMidiEventVec overdubSessionLiveUndoEvents_;

  void freeActiveCapturePassChunks();
  void markPassDerivedStale();
  /// RC-E attribution: cached note coverage vs loop bars at a rebuild or staleness boundary.
  void emitVisualCacheState(const char* phase, int32_t gatheredEvents) const;

  std::unique_ptr<PlaybackOrderVec> playbackOrder_;
  std::unique_ptr<NoteUtils::CachedNoteList> noteCache_;
  std::unique_ptr<NoteUtils::EventIndex> cachedEventIndex_;
};

#endif  // LOOP_H
