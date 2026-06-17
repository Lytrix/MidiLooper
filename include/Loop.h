//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

/**
 * @file Loop.h
 * @brief Per-slot loop data: MIDI events, loop geometry, playback state, undo.
 *
 * Each Track holds a LoopPool and Slot refs (1:1 in v1). The active loop slot
 * (selected by activeLoopIndex) resolves to a LoopId and pooled Loop used for
 * playback and recording.
 *
 * Published MIDI lives in Active epochs. Note-edit paths use a materialized
 * editFlat_ buffer until M8 epoch edit cutover.
 */
#ifndef LOOP_H
#define LOOP_H

#pragma once
#include <cstdint>
#include <vector>
#include <memory>
#include "MidiEvent.h"
#include "LoopEventBuffer.h"
#include "Epoch.h"
#include "VisualCache.h"
#include "TrackState.h"
#include "Utils/NoteUtils.h"
#include "Utils/ExtMemAllocator.h"
#include "Globals.h"

// Forward
class Track;

/// Playback order indices (same allocator as midiEvents for PSRAM spillover).
using PlaybackOrderVec = std::vector<size_t, ExtMemAllocator<size_t>>;

struct Loop {
  /// Mutable record/overdub capture (pre-Seal writer).
  CaptureLayer capture;
  uint16_t captureNextEventIndex = 0;
  bool captureEventsSortDirty = false;
  /// Bumped when capture geometry changes outside liveEventCount (e.g. loop-wrap note-offs).
  uint16_t captureDisplayRevision = 0;

  LoopId loopId = kInvalidLoopId;
  uint32_t playbackRevision = 0;
  EpochVec epochs;
  bool hasPendingEpoch_ = false;
  Epoch pendingEpoch_;
  VisualCache visualCache;
  CapturePreview capturePreview;
  VisualCacheDelta pendingVisualDelta;
  bool visualCacheDirty = true;
  EpochId nextEpochId_ = 1;
  uint32_t nextMergeSequence_ = 0;
  EpochId lastPublishedEpochId_ = kInvalidEpochId;

  uint32_t startLoopTick = 0;
  uint32_t loopLengthTicks = 0;
  uint32_t loopStartTick = 0;

  // Playback state
  uint32_t lastTickInLoop = 0;
  uint16_t nextEventIndex = 0;
  bool playbackOrderDirty = true;

  // Caches
  mutable bool eventIndexValid = false;

  /// True if the slot holds a committed loop (length and/or events). Silent takes
  /// leave epochs empty but loopLengthTicks > 0 after stopRecording.
  bool hasData() const {
    return hasPublishedEvents() || loopLengthTicks > 0 || !capture.store.empty();
  }

  bool hasPendingEpoch() const { return hasPendingEpoch_; }
  const Epoch& pendingEpoch() const { return pendingEpoch_; }
  size_t activeEpochCount() const;

  /// True when at least one Active epoch holds chunk refs.
  bool hasPublishedEvents() const;

  /// Flatten all Active epochs (mergeSequence order) into out.
  void flattenActiveEpochs(MidiEventVec& out) const;

  /// Note-edit flat access (M8 bridge): materializes from epochs on first use.
  MidiEventVec& midiEvents();
  const MidiEventVec& midiEvents() const;

  LoopEventStore& mutEditStore();
  const LoopEventStore& readEditStore() const;

  std::shared_ptr<const LoopEventStore> shareEditSnapshot() const;
  void restoreEditSnapshot(const MidiSnapshotRef& snapshot);

  void beginCapture(CapturePhase phase);
  void discardCapture();
  bool appendCaptureEvent(const MidiEvent& evt);
  /// Seal → Publish when capture is non-empty.
  CommitResult commitCaptureData(CommitReason reason, uint32_t sealedAtTick);
  bool setEpochState(EpochId id, EpochState state);
  void resetEpochTimeline();
  EpochId lastPublishedEpochId() const { return lastPublishedEpochId_; }
  bool captureActive() const;
  size_t liveEventCount() const;
  /// Sort capture buffer by tick when append order diverges (display/playback/commit).
  bool ensureCaptureEventsSorted();
  /// Committed events plus in-flight capture buffer (sorted by tick) for display/LED reads.
  void buildLiveEventView(MidiEventVec& out) const;
  /// Rebuild committed visual cache from Active epochs.
  void rebuildVisualCacheFromEpochs();
  /// Lazy rebuild — safe on display/read paths; not on every invalidateCaches().
  void ensureVisualCacheBuilt();
  /// SD load / hot-path chunk edits: no flat sync, no visual rebuild.
  void markDisplayCachesStale();
  /// Remove a capture note-off (e.g. loop-wrap synthetic) before recording the real head off.
  void removeCaptureNoteOffAt(uint8_t channel, uint8_t note, uint32_t tick);
  /// Shift all Active-epoch MIDI by delta (record-stop origin alignment).
  void shiftActiveEpochTicks(int64_t delta);
  /// Write materialized editFlat_ back into Active epochs (stop-path / validation).
  void flushEditStoreToEpochs();
  /// Call after mutating midiEvents() flat buffer so flushEditStoreToEpochs can commit.
  void markEditFlatDirty() { editFlat_.markFlatDirty(); }
#if defined(PIO_UNIT_TEST_NATIVE)
  void nativeTestSyncEditFlatToEpochs(bool allowEmptyClear) { syncEditFlatToEpochs(allowEmptyClear); }
  size_t nativeTestLiveEventCount() const { return liveEventCount(); }
#endif
  /// SD v3/v1/v2 migration: adopt flat store as single Active epoch.
  void importPublishedStore(LoopEventStore& store);
  /// Drop read-path editFlat materialization without writing back to epochs.
  void discardEditFlatMaterialization();
  /// Apply wrap-window stop finalize result back into Active epochs.
  void commitStopFinalizeFromStore(LoopEventStore& merged);

  /// Seal capture into pendingEpoch (prepare only — not visible until publish).
  SealOutcome sealCaptureLayer(uint32_t sealedAtTick);
  /// Publish pendingEpoch as Active (no-alloc visibility step).
  bool publishPendingEpoch();
  void discardPendingEpoch();

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

  void clearCaptureOnNewTake();

 private:
  friend class TrackUndo;

  CowLoopEventStore editFlat_;
  bool editFlatStale_ = true;

  void materializeEditFlatFromEpochs() const;
  void syncEditFlatToEpochs(bool allowEmptyClear = false);
  void freeActiveEpochChunks();
  void markEpochDerivedStale();

  std::unique_ptr<PlaybackOrderVec> playbackOrder_;
  std::unique_ptr<NoteUtils::CachedNoteList> noteCache_;
  std::unique_ptr<NoteUtils::EventIndex> cachedEventIndex_;
};

#endif  // LOOP_H
