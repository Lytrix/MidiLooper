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
  void suppressNextOverdubWrap();
  bool consumeSuppressedOverdubWrapCrossing(uint32_t prevPhase, uint32_t currentPhase);
  /// Move unpaired capture NoteOns out of `capture.store` (6E.5). Returns extracted count.
  size_t extractOpenCaptureNoteOns(SessionMidiEventVec& out);
  void pushOverdubSessionPass(PassId passId, EditPassIdList companionIds);
  /// Sealed wraps still on the session cursor (not the undone redo tail).
  void collectOverdubSessionUndoPasses(PassIdList& passIds, EditPassIdList& companionIds) const;
  void dropOverdubSessionRedoTail();
  bool undoOverdubSession();
  bool redoOverdubSession();
  bool canUndoOverdubSession() const;
  bool canRedoOverdubSession() const;
  /// Sealed wraps on the cursor. Live `capture.store` is not a session layer.
  size_t overdubSessionUndoDepth() const;
  size_t overdubSessionRedoDepth() const;
  /// Session-start source view: reset overlap-hold totals, then `rebuildOverdubSourceView`
  /// (`why=open`), then clear pending. Not visual cache.
  void establishOverdubSourceView(uint32_t playheadPhaseTick);
  /// Source/hold resolution breadth in bars. Independent of
  /// `DisplayWindowUtils::kMaxDetailedWindowBars`. Experiment 1 rebuilds this
  /// value (1/2/4/8/16); a timing-passing length is evidence, not policy.
  static constexpr uint32_t kOverdubSourceWindowBars = 16;
  /// Rebuild source notes from prepared window, else per-pass `resolveWindow`.
  /// Not visual cache. `why` is CAP `open` (enter) or `wrap` (after publish).
  void rebuildOverdubSourceView(uint32_t playheadPhaseTick, const char* why = "wrap");
  /// D2: just-in-time merge of this pitch's notes from the source/hold window
  /// (`kOverdubSourceWindowBars`) into the session source view. Not the full loop. Skips noteIds already
  /// present. Optional `newlyMergedPitchNotes` receives only those new rows.
  /// `presentAtHoldOnly` is the note-on snapshot path: merge notes present at
  /// the hold tick, not ahead notes in the same window.
  void ensureOverdubSourceNotesForHold(uint32_t holdPhaseTick, uint8_t pitch,
                                       NoteUtils::DisplayNoteVec* newlyMergedPitchNotes = nullptr,
                                       bool presentAtHoldOnly = false);
  /// RC8 gold (A): NoteIds in `overdubSourceViewNotes` present at the hold tick for pitch.
  /// Does not fill the source window. Clears `out` then inserts.
  void collectOverdubSourceHoldParticipantIds(uint32_t holdPhaseTick, uint8_t pitch,
                                              OverlapNoteIdSet& out) const;
  /// Prepared present-at-S NoteIds for pitch (B). Returns false on prepared miss.
  /// Never `resolveWindow` / cold `resolveState`. Clears `out`.
  bool tryCollectPreparedPresentNoteIdsAtTick(uint32_t tick, uint8_t pitch,
                                              OverlapNoteIdSet& out) const;
  /// Session end / discard. Wrap and stop commit keep the view while the session is open.
  void clearOverdubSourceView();
  bool hasOverdubSourceView() const { return overdubSourceViewEstablished_; }
  uint32_t overdubSourceViewLoopLengthTicks() const { return overdubSourceViewLoopLengthTicks_; }
  const SessionMidiEventVec& overdubSourceViewEvents() const { return overdubSourceViewEvents_; }
  const NoteUtils::DisplayNoteVec& overdubSourceViewNotes() const { return overdubSourceViewNotes_; }

  /// Session pending logical delta (Add/Shorten/Hide) — not a timeline pass.
  void clearPendingNoteChanges();
  bool hasPendingNoteChanges() const { return !pendingNoteChanges_.empty(); }
  const PendingNoteChangeVec& pendingNoteChanges() const { return pendingNoteChanges_; }
  /// Resolve incoming note against hold-candidate ids, then geometry.
  /// Wrap-head off (`endTick < startTick`) occupies `[S, loopLength) ∪ [0, E)` as **one** hold.
  /// Empty `overlapNoteIds` skips span lookup (Gate 3); source-view overlap still consumes.
  /// Returns false when no source view.
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
  /// Merge pending Add/Shorten/Hide into `overdubSourceViewNotes_`. Wrap commit
  /// uses `rebuildOverdubSourceView` instead. Does not clear pending.
  void applyPendingNoteChangesToOverdubSourceView();
  /// Paint overlay: apply pending Hide/Shorten onto a display-note list. Not Add
  /// (live capture already has those). Does not mutate the source view or visual cache.
  void applyPendingNoteChangesToDisplayNotes(NoteUtils::DisplayNoteVec& notes) const;
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
  /// Reconstruct each active overdub pass with overdub wrap pairing and append
  /// display notes that merged reconstruct omitted.
  void appendOverdubPassDisplayNotes(NoteUtils::DisplayNoteVec& notes) const;
  /// Rebuild up to `maxBarsPerSlice` dirty bars, preferring `priorityBar`.
  /// When `maxBarDistanceFromPriority` is finite, skip dirty bars outside that neighborhood
  /// (PLAYING viewport backfill); pass UINT32_MAX for full-loop idle backfill when stopped.
  void rebuildVisualCacheIdleSlice(uint8_t maxBarsPerSlice, uint32_t priorityBar,
                                   uint32_t maxBarDistanceFromPriority = UINT32_MAX);
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
  bool overdubWrapSuppressNext_ = false;
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
