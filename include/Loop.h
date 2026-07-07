//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

/**
 * @file Loop.h
 * @brief Per-slot loop data: MIDI events, loop geometry, playback state, undo.
 *
 * Published MIDI lives in passes (recordPass, overdubPasses). Note edits are stored
 * in passes.editPasses and materialized via LoopPasses::materialize for playback/display.
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
#include "PassReclaim.h"

class Track;

using PlaybackOrderVec = std::vector<size_t, ExternalMemoryFirstAllocator<size_t>>;

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
  size_t publishedMaterializedEventCount_ = 0;
  uint32_t nextMergeSequence_ = 0;
  PassId lastPublishedPassId_ = kInvalidPassId;

  uint32_t startLoopTick = 0;
  uint32_t loopLengthTicks = 0;
  uint32_t loopStartTick = 0;

  uint32_t lastTickInLoop = 0;
  uint16_t nextEventIndex = 0;
  bool playbackOrderDirty = true;

  mutable bool eventIndexValid = false;

  bool hasData() const {
    return hasPublishedEvents() || loopLengthTicks > 0 || !capture.store.empty();
  }

  bool hasPendingCapturePass() const { return hasPendingCapturePass_; }
  const PendingCapturePass& pendingCapturePass() const { return pendingCapturePass_; }
  size_t activeCapturePassCount() const;

  bool hasPublishedEvents() const;

  void mergeActiveCapturePasses(MidiEventVec& out) const;
  void mergeActiveCapturePasses(SessionMidiEventVec& out) const;

  SessionMidiEventVec& midiEvents();
  const SessionMidiEventVec& midiEvents() const;

  void rematerializeEditView(LoopEventStore& store) const;

  EditPassId saveNoteEditPass(uint8_t editPassIndex, EditPass row,
                              EditPassType passType = EditPassType::Note);
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

  void beginCapture(CapturePhase phase);
  void discardCapture();
  bool appendCaptureEvent(const MidiEvent& evt);
  CommitResult commitCapturePass(CommitReason reason, uint32_t sealedAtTick);
  bool setCapturePassState(PassId id, CapturePassState state);
  void resetPassTimeline();
  PassId lastPublishedPassId() const { return lastPublishedPassId_; }
  bool captureActive() const;
  size_t liveEventCount() const;
  /// Published pass event count + active capture size; builds visual cache if needed. Display/LED only.
  size_t displayEventCountHint() const;
  bool ensureCaptureEventsSorted();
  void mergeMaterializedPassesWithCapture(MidiEventVec& out) const;
  void mergeMaterializedPassesWithCapture(SessionMidiEventVec& out) const;
  void rebuildVisualCacheFromPasses();
  void rebuildVisualCacheIdleSlice(uint8_t maxBarsPerSlice, uint32_t priorityBar);
  void ensureVisualCacheBuilt();
  void markDisplayCachesStale();
  /// Note + visual caches only — does not disturb playback order or materialized pass view.
  void invalidateDisplayCaches();
  void removeCaptureNoteOffAt(uint8_t channel, uint8_t note, uint32_t tick);
  void shiftActiveCapturePassTicks(int64_t delta);
  size_t nativeTestLiveEventCount() const { return liveEventCount(); }
  void seedRecordPassFromStore(LoopEventStore& store);
  void discardPassesMaterializedCache();
  void discardPublishedFlatCache() { passesMaterializedStore_.discardFlatCache(); }
  void commitStopFinalizeFromStore(LoopEventStore& merged);

  SealOutcome sealCapture(uint32_t sealedAtTick);
  bool publishPendingCapturePass();
  void discardPendingCapturePass();

  bool reclaimDisabledCapturePass(PassId id);
  void reclaimUnreferencedDisabledCapturePasses(const SlotPassReferences& refs);
  void reclaimUnreferencedDisabledEditPasses(const SlotPassReferences& refs);
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

  void ensurePassesMaterializedStore() const { materializeEditViewFromPasses(); }
  bool isPassesMaterializedStoreFresh() const { return !passesMaterializedStoreStale_; }

 private:
  friend class TrackUndo;

  PublishedLoopEventStore passesMaterializedStore_;
  bool passesMaterializedStoreStale_ = true;

  void materializeEditViewFromPasses() const;
  void freeActiveCapturePassChunks();
  void markPassDerivedStale();

  std::unique_ptr<PlaybackOrderVec> playbackOrder_;
  std::unique_ptr<NoteUtils::CachedNoteList> noteCache_;
  std::unique_ptr<NoteUtils::EventIndex> cachedEventIndex_;
};

#endif  // LOOP_H
