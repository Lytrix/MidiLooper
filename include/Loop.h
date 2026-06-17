//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

/**
 * @file Loop.h
 * @brief Per-slot loop data: MIDI events, loop geometry, playback state, undo.
 *
 * Each Track holds Loop loops[MAX_LOOPS_PER_TRACK]. The active loop (selected by
 * activeLoopIndex) is used for playback and recording.
 *
 * Undo and cache containers are lazy-allocated to reduce memory at startup.
 */
#ifndef LOOP_H
#define LOOP_H

#pragma once
#include <cstdint>
#include <vector>
#include <deque>
#include <memory>
#include "MidiEvent.h"
#include "LoopEventBuffer.h"
#include "Epoch.h"
#include "VisualCache.h"
#include "TrackState.h"
#include "Utils/MemoryPool.h"
#include "Utils/NoteUtils.h"
#include "Utils/ExtMemAllocator.h"
#include "Globals.h"

// Forward
class Track;

using PooledMidiDeque    = std::deque<MemoryPool::PooledMidiEventVector, ExtMemAllocator<MemoryPool::PooledMidiEventVector>>;
using MidiSnapshotDeque  = std::deque<MidiSnapshotRef, ExtMemAllocator<MidiSnapshotRef>>;
using TrackStateDeque    = std::deque<TrackState,    ExtMemAllocator<TrackState>>;
using Uint32Deque        = std::deque<uint32_t,      ExtMemAllocator<uint32_t>>;
/// Playback order indices (same allocator as midiEvents for PSRAM spillover).
using PlaybackOrderVec = std::vector<size_t, ExtMemAllocator<size_t>>;

/// Loop length/start geometry stored alongside each overdub undo snapshot (per slot).
struct OverdubGeomSnapshot {
  uint32_t loopLengthTicks = 0;
  uint32_t startLoopTick = 0;
  uint32_t loopStartTick = 0;
};
using OverdubGeomDeque = std::deque<OverdubGeomSnapshot, ExtMemAllocator<OverdubGeomSnapshot>>;

struct Loop {
  // Committed loop events (baseline during overdub capture); COW chunk refs for O(1) undo.
  CowLoopEventStore committedEvents;
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
  EpochId nextEpochId_ = 1;
  uint32_t nextMergeSequence_ = 0;

  uint32_t startLoopTick = 0;
  uint32_t loopLengthTicks = 0;
  uint32_t loopStartTick = 0;

  // Playback state
  uint32_t lastTickInLoop = 0;
  uint16_t nextEventIndex = 0;
  bool playbackOrderDirty = true;

  // Undo overdub
  size_t midiEventCountAtLastSnapshot = 0;
  bool overdubSessionOpen = false;
  size_t overdubSessionBaselineEventCount = 0;
  uint32_t overdubSessionBaselineHash = 0;

  // Caches
  mutable bool eventIndexValid = false;

  /// True if the slot holds a committed loop (length and/or events). Silent takes
  /// leave midiEvents empty but loopLengthTicks > 0 after stopRecording.
  bool hasData() const {
    return !committedEvents.empty() || loopLengthTicks > 0 || !capture.store.empty();
  }

  bool hasPendingEpoch() const { return hasPendingEpoch_; }
  const Epoch& pendingEpoch() const { return pendingEpoch_; }
  size_t activeEpochCount() const;

  const MidiEvent& eventAt(size_t index) const { return committedEvents.at(index); }
  size_t eventCount() const { return committedEvents.size(); }

  MidiEventVec& midiEvents() { return committedEvents.mutFlat(); }
  const MidiEventVec& midiEvents() const { return committedEvents.readFlat(); }

  void beginCapture(CapturePhase phase);
  void discardCapture();
  bool appendCaptureEvent(const MidiEvent& evt);
  void commitCapture();
  bool captureActive() const;
  size_t liveEventCount() const;
  /// Sort capture buffer by tick when append order diverges (display/playback/commit).
  bool ensureCaptureEventsSorted();
  /// Committed events plus in-flight capture buffer (sorted by tick) for display/LED reads.
  void buildLiveEventView(MidiEventVec& out) const;

  /// Seal capture into pendingEpoch (prepare only — not visible until publish). M2 wires stop path.
  SealOutcome sealCaptureLayer(uint32_t sealedAtTick);
  /// Publish pendingEpoch as Active (no-alloc visibility step). M2 wires stop path.
  bool publishPendingEpoch();
  void discardPendingEpoch();

  // --- Lazy-allocated members (access via getters) ---

  MidiSnapshotDeque& getMidiHistory() {
    if (!midiHistory_) midiHistory_ = std::make_unique<MidiSnapshotDeque>();
    return *midiHistory_;
  }
  const MidiSnapshotDeque& getMidiHistory() const {
    return const_cast<Loop*>(this)->getMidiHistory();
  }
  bool midiHistoryEmpty() const { return !midiHistory_ || midiHistory_->empty(); }
  size_t midiHistorySize() const { return midiHistory_ ? midiHistory_->size() : 0; }
  const MidiSnapshotDeque* tryGetMidiHistory() const { return midiHistory_ ? midiHistory_.get() : nullptr; }

  MidiSnapshotDeque& getMidiRedoHistory() {
    if (!midiRedoHistory_) midiRedoHistory_ = std::make_unique<MidiSnapshotDeque>();
    return *midiRedoHistory_;
  }
  const MidiSnapshotDeque& getMidiRedoHistory() const {
    return const_cast<Loop*>(this)->getMidiRedoHistory();
  }
  bool midiRedoHistoryEmpty() const { return !midiRedoHistory_ || midiRedoHistory_->empty(); }
  size_t midiRedoHistorySize() const { return midiRedoHistory_ ? midiRedoHistory_->size() : 0; }
  const MidiSnapshotDeque* tryGetMidiRedoHistory() const { return midiRedoHistory_ ? midiRedoHistory_.get() : nullptr; }

  OverdubGeomDeque& getOverdubGeomHistory() {
    if (!overdubGeomHistory_) overdubGeomHistory_ = std::make_unique<OverdubGeomDeque>();
    return *overdubGeomHistory_;
  }
  const OverdubGeomDeque& getOverdubGeomHistory() const {
    return const_cast<Loop*>(this)->getOverdubGeomHistory();
  }
  bool overdubGeomHistoryEmpty() const { return !overdubGeomHistory_ || overdubGeomHistory_->empty(); }
  size_t overdubGeomHistorySize() const { return overdubGeomHistory_ ? overdubGeomHistory_->size() : 0; }
  const OverdubGeomDeque* tryGetOverdubGeomHistory() const { return overdubGeomHistory_ ? overdubGeomHistory_.get() : nullptr; }

  OverdubGeomDeque& getOverdubGeomRedoHistory() {
    if (!overdubGeomRedoHistory_) overdubGeomRedoHistory_ = std::make_unique<OverdubGeomDeque>();
    return *overdubGeomRedoHistory_;
  }
  const OverdubGeomDeque& getOverdubGeomRedoHistory() const {
    return const_cast<Loop*>(this)->getOverdubGeomRedoHistory();
  }
  bool overdubGeomRedoHistoryEmpty() const {
    return !overdubGeomRedoHistory_ || overdubGeomRedoHistory_->empty();
  }
  size_t overdubGeomRedoHistorySize() const { return overdubGeomRedoHistory_ ? overdubGeomRedoHistory_->size() : 0; }
  const OverdubGeomDeque* tryGetOverdubGeomRedoHistory() const { return overdubGeomRedoHistory_ ? overdubGeomRedoHistory_.get() : nullptr; }

  PooledMidiDeque& getClearMidiHistory() {
    if (!clearMidiHistory_) clearMidiHistory_ = std::make_unique<PooledMidiDeque>();
    return *clearMidiHistory_;
  }
  const PooledMidiDeque& getClearMidiHistory() const {
    return const_cast<Loop*>(this)->getClearMidiHistory();
  }
  bool clearMidiHistoryEmpty() const { return !clearMidiHistory_ || clearMidiHistory_->empty(); }
  const PooledMidiDeque* tryGetClearMidiHistory() const { return clearMidiHistory_ ? clearMidiHistory_.get() : nullptr; }

  PooledMidiDeque& getClearMidiRedoHistory() {
    if (!clearMidiRedoHistory_) clearMidiRedoHistory_ = std::make_unique<PooledMidiDeque>();
    return *clearMidiRedoHistory_;
  }
  const PooledMidiDeque& getClearMidiRedoHistory() const {
    return const_cast<Loop*>(this)->getClearMidiRedoHistory();
  }
  bool clearMidiRedoHistoryEmpty() const {
    return !clearMidiRedoHistory_ || clearMidiRedoHistory_->empty();
  }
  const PooledMidiDeque* tryGetClearMidiRedoHistory() const { return clearMidiRedoHistory_ ? clearMidiRedoHistory_.get() : nullptr; }

  TrackStateDeque& getClearStateHistory() {
    if (!clearStateHistory_) clearStateHistory_ = std::make_unique<TrackStateDeque>();
    return *clearStateHistory_;
  }
  const TrackStateDeque& getClearStateHistory() const {
    return const_cast<Loop*>(this)->getClearStateHistory();
  }
  const TrackStateDeque* tryGetClearStateHistory() const { return clearStateHistory_ ? clearStateHistory_.get() : nullptr; }

  TrackStateDeque& getClearStateRedoHistory() {
    if (!clearStateRedoHistory_) clearStateRedoHistory_ = std::make_unique<TrackStateDeque>();
    return *clearStateRedoHistory_;
  }
  const TrackStateDeque& getClearStateRedoHistory() const {
    return const_cast<Loop*>(this)->getClearStateRedoHistory();
  }
  const TrackStateDeque* tryGetClearStateRedoHistory() const { return clearStateRedoHistory_ ? clearStateRedoHistory_.get() : nullptr; }

  Uint32Deque& getClearLengthHistory() {
    if (!clearLengthHistory_) clearLengthHistory_ = std::make_unique<Uint32Deque>();
    return *clearLengthHistory_;
  }
  const Uint32Deque& getClearLengthHistory() const {
    return const_cast<Loop*>(this)->getClearLengthHistory();
  }
  const Uint32Deque* tryGetClearLengthHistory() const { return clearLengthHistory_ ? clearLengthHistory_.get() : nullptr; }

  Uint32Deque& getClearLengthRedoHistory() {
    if (!clearLengthRedoHistory_) clearLengthRedoHistory_ = std::make_unique<Uint32Deque>();
    return *clearLengthRedoHistory_;
  }
  const Uint32Deque& getClearLengthRedoHistory() const {
    return const_cast<Loop*>(this)->getClearLengthRedoHistory();
  }
  const Uint32Deque* tryGetClearLengthRedoHistory() const { return clearLengthRedoHistory_ ? clearLengthRedoHistory_.get() : nullptr; }

  Uint32Deque& getClearStartHistory() {
    if (!clearStartHistory_) clearStartHistory_ = std::make_unique<Uint32Deque>();
    return *clearStartHistory_;
  }
  const Uint32Deque& getClearStartHistory() const {
    return const_cast<Loop*>(this)->getClearStartHistory();
  }
  const Uint32Deque* tryGetClearStartHistory() const { return clearStartHistory_ ? clearStartHistory_.get() : nullptr; }

  Uint32Deque& getClearStartRedoHistory() {
    if (!clearStartRedoHistory_) clearStartRedoHistory_ = std::make_unique<Uint32Deque>();
    return *clearStartRedoHistory_;
  }
  const Uint32Deque& getClearStartRedoHistory() const {
    return const_cast<Loop*>(this)->getClearStartRedoHistory();
  }
  const Uint32Deque* tryGetClearStartRedoHistory() const { return clearStartRedoHistory_ ? clearStartRedoHistory_.get() : nullptr; }

  Uint32Deque& getLoopStartHistory() {
    if (!loopStartHistory_) loopStartHistory_ = std::make_unique<Uint32Deque>();
    return *loopStartHistory_;
  }
  const Uint32Deque& getLoopStartHistory() const {
    return const_cast<Loop*>(this)->getLoopStartHistory();
  }
  bool loopStartHistoryEmpty() const { return !loopStartHistory_ || loopStartHistory_->empty(); }
  const Uint32Deque* tryGetLoopStartHistory() const { return loopStartHistory_ ? loopStartHistory_.get() : nullptr; }

  Uint32Deque& getLoopStartRedoHistory() {
    if (!loopStartRedoHistory_) loopStartRedoHistory_ = std::make_unique<Uint32Deque>();
    return *loopStartRedoHistory_;
  }
  const Uint32Deque& getLoopStartRedoHistory() const {
    return const_cast<Loop*>(this)->getLoopStartRedoHistory();
  }
  bool loopStartRedoHistoryEmpty() const {
    return !loopStartRedoHistory_ || loopStartRedoHistory_->empty();
  }
  const Uint32Deque* tryGetLoopStartRedoHistory() const { return loopStartRedoHistory_ ? loopStartRedoHistory_.get() : nullptr; }

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
  /// Non-const ref even from const Loop: getNotes() mutates cache (mutable-like)
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

  void invalidateCaches() {
    committedEvents.syncFlatToStore();
    if (noteCache_) noteCache_->invalidate();
    eventIndexValid = false;
  }

  /// Playback/display caches only — no flat materialization or chunk rebuild.
  void invalidatePlaybackCaches() {
    playbackOrderDirty = true;
    committedEvents.discardFlatCache();
    if (noteCache_) noteCache_->invalidate();
    eventIndexValid = false;
  }

  void clearAllUndoStacks() {
    discardCapture();
    overdubSessionOpen = false;
    overdubSessionBaselineEventCount = 0;
    overdubSessionBaselineHash = 0;
    if (midiHistory_) midiHistory_->clear();
    if (midiRedoHistory_) midiRedoHistory_->clear();
    if (overdubGeomHistory_) overdubGeomHistory_->clear();
    if (overdubGeomRedoHistory_) overdubGeomRedoHistory_->clear();
    if (clearMidiHistory_) clearMidiHistory_->clear();
    if (clearMidiRedoHistory_) clearMidiRedoHistory_->clear();
    if (clearStateHistory_) clearStateHistory_->clear();
    if (clearStateRedoHistory_) clearStateRedoHistory_->clear();
    if (clearLengthHistory_) clearLengthHistory_->clear();
    if (clearLengthRedoHistory_) clearLengthRedoHistory_->clear();
    if (clearStartHistory_) clearStartHistory_->clear();
    if (clearStartRedoHistory_) clearStartRedoHistory_->clear();
    if (loopStartHistory_) loopStartHistory_->clear();
    if (loopStartRedoHistory_) loopStartRedoHistory_->clear();
  }

  /// Clears overdub / loop-start-edit stacks only. Used when wiping slot content so
  /// "undo clear" snapshots (clear* deques) pushed immediately before are kept.
  void clearOverdubAndLoopEditUndoStacks() {
    if (midiHistory_) midiHistory_->clear();
    if (midiRedoHistory_) midiRedoHistory_->clear();
    if (overdubGeomHistory_) overdubGeomHistory_->clear();
    if (overdubGeomRedoHistory_) overdubGeomRedoHistory_->clear();
    if (loopStartHistory_) loopStartHistory_->clear();
    if (loopStartRedoHistory_) loopStartRedoHistory_->clear();
  }

  /// Clears "undo clear slot" stacks. A new recording supersedes clear undo.
  void clearClearUndoStacks() {
    if (clearMidiHistory_) clearMidiHistory_->clear();
    if (clearMidiRedoHistory_) clearMidiRedoHistory_->clear();
    if (clearStateHistory_) clearStateHistory_->clear();
    if (clearStateRedoHistory_) clearStateRedoHistory_->clear();
    if (clearLengthHistory_) clearLengthHistory_->clear();
    if (clearLengthRedoHistory_) clearLengthRedoHistory_->clear();
    if (clearStartHistory_) clearStartHistory_->clear();
    if (clearStartRedoHistory_) clearStartRedoHistory_->clear();
  }

private:
  std::unique_ptr<MidiSnapshotDeque> midiHistory_;
  std::unique_ptr<MidiSnapshotDeque> midiRedoHistory_;
  std::unique_ptr<OverdubGeomDeque> overdubGeomHistory_;
  std::unique_ptr<OverdubGeomDeque> overdubGeomRedoHistory_;
  std::unique_ptr<PooledMidiDeque> clearMidiHistory_;
  std::unique_ptr<PooledMidiDeque> clearMidiRedoHistory_;
  std::unique_ptr<TrackStateDeque> clearStateHistory_;
  std::unique_ptr<TrackStateDeque> clearStateRedoHistory_;
  std::unique_ptr<Uint32Deque> clearLengthHistory_;
  std::unique_ptr<Uint32Deque> clearLengthRedoHistory_;
  std::unique_ptr<Uint32Deque> clearStartHistory_;
  std::unique_ptr<Uint32Deque> clearStartRedoHistory_;
  std::unique_ptr<Uint32Deque> loopStartHistory_;
  std::unique_ptr<Uint32Deque> loopStartRedoHistory_;
  std::unique_ptr<PlaybackOrderVec> playbackOrder_;
  std::unique_ptr<NoteUtils::CachedNoteList> noteCache_;
  std::unique_ptr<NoteUtils::EventIndex> cachedEventIndex_;
};

#endif  // LOOP_H
