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
#include "TrackState.h"
#include "Utils/MemoryPool.h"
#include "Utils/NoteUtils.h"
#include "Globals.h"

// Forward
class Track;

using PooledMidiDeque = std::deque<MemoryPool::PooledMidiEventVector>;
using TrackStateDeque = std::deque<TrackState>;
using Uint32Deque = std::deque<uint32_t>;

struct Loop {
  // Sequence data
  std::vector<MidiEvent> midiEvents;
  uint32_t startLoopTick = 0;
  uint32_t loopLengthTicks = 0;
  uint32_t loopStartTick = 0;

  // Playback state
  uint32_t lastTickInLoop = 0;
  uint16_t nextEventIndex = 0;
  bool playbackOrderDirty = true;

  // Undo overdub
  size_t midiEventCountAtLastSnapshot = 0;

  // Caches
  mutable bool eventIndexValid = false;

  bool hasData() const { return !midiEvents.empty(); }

  // --- Lazy-allocated members (access via getters) ---

  PooledMidiDeque& getMidiHistory() {
    if (!midiHistory_) midiHistory_ = std::make_unique<PooledMidiDeque>();
    return *midiHistory_;
  }
  const PooledMidiDeque& getMidiHistory() const {
    return const_cast<Loop*>(this)->getMidiHistory();
  }
  bool midiHistoryEmpty() const { return !midiHistory_ || midiHistory_->empty(); }
  size_t midiHistorySize() const { return midiHistory_ ? midiHistory_->size() : 0; }

  PooledMidiDeque& getMidiRedoHistory() {
    if (!midiRedoHistory_) midiRedoHistory_ = std::make_unique<PooledMidiDeque>();
    return *midiRedoHistory_;
  }
  const PooledMidiDeque& getMidiRedoHistory() const {
    return const_cast<Loop*>(this)->getMidiRedoHistory();
  }
  bool midiRedoHistoryEmpty() const { return !midiRedoHistory_ || midiRedoHistory_->empty(); }
  size_t midiRedoHistorySize() const { return midiRedoHistory_ ? midiRedoHistory_->size() : 0; }

  PooledMidiDeque& getClearMidiHistory() {
    if (!clearMidiHistory_) clearMidiHistory_ = std::make_unique<PooledMidiDeque>();
    return *clearMidiHistory_;
  }
  const PooledMidiDeque& getClearMidiHistory() const {
    return const_cast<Loop*>(this)->getClearMidiHistory();
  }
  bool clearMidiHistoryEmpty() const { return !clearMidiHistory_ || clearMidiHistory_->empty(); }

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

  TrackStateDeque& getClearStateHistory() {
    if (!clearStateHistory_) clearStateHistory_ = std::make_unique<TrackStateDeque>();
    return *clearStateHistory_;
  }
  const TrackStateDeque& getClearStateHistory() const {
    return const_cast<Loop*>(this)->getClearStateHistory();
  }

  TrackStateDeque& getClearStateRedoHistory() {
    if (!clearStateRedoHistory_) clearStateRedoHistory_ = std::make_unique<TrackStateDeque>();
    return *clearStateRedoHistory_;
  }
  const TrackStateDeque& getClearStateRedoHistory() const {
    return const_cast<Loop*>(this)->getClearStateRedoHistory();
  }

  Uint32Deque& getClearLengthHistory() {
    if (!clearLengthHistory_) clearLengthHistory_ = std::make_unique<Uint32Deque>();
    return *clearLengthHistory_;
  }
  const Uint32Deque& getClearLengthHistory() const {
    return const_cast<Loop*>(this)->getClearLengthHistory();
  }

  Uint32Deque& getClearLengthRedoHistory() {
    if (!clearLengthRedoHistory_) clearLengthRedoHistory_ = std::make_unique<Uint32Deque>();
    return *clearLengthRedoHistory_;
  }
  const Uint32Deque& getClearLengthRedoHistory() const {
    return const_cast<Loop*>(this)->getClearLengthRedoHistory();
  }

  Uint32Deque& getClearStartHistory() {
    if (!clearStartHistory_) clearStartHistory_ = std::make_unique<Uint32Deque>();
    return *clearStartHistory_;
  }
  const Uint32Deque& getClearStartHistory() const {
    return const_cast<Loop*>(this)->getClearStartHistory();
  }

  Uint32Deque& getClearStartRedoHistory() {
    if (!clearStartRedoHistory_) clearStartRedoHistory_ = std::make_unique<Uint32Deque>();
    return *clearStartRedoHistory_;
  }
  const Uint32Deque& getClearStartRedoHistory() const {
    return const_cast<Loop*>(this)->getClearStartRedoHistory();
  }

  Uint32Deque& getLoopStartHistory() {
    if (!loopStartHistory_) loopStartHistory_ = std::make_unique<Uint32Deque>();
    return *loopStartHistory_;
  }
  const Uint32Deque& getLoopStartHistory() const {
    return const_cast<Loop*>(this)->getLoopStartHistory();
  }
  bool loopStartHistoryEmpty() const { return !loopStartHistory_ || loopStartHistory_->empty(); }

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

  std::vector<size_t>& getPlaybackOrder() {
    if (!playbackOrder_) playbackOrder_ = std::make_unique<std::vector<size_t>>();
    return *playbackOrder_;
  }
  const std::vector<size_t>& getPlaybackOrder() const {
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
    if (noteCache_) noteCache_->invalidate();
    eventIndexValid = false;
  }

  void clearAllUndoStacks() {
    if (midiHistory_) midiHistory_->clear();
    if (midiRedoHistory_) midiRedoHistory_->clear();
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

private:
  std::unique_ptr<PooledMidiDeque> midiHistory_;
  std::unique_ptr<PooledMidiDeque> midiRedoHistory_;
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
  std::unique_ptr<std::vector<size_t>> playbackOrder_;
  std::unique_ptr<NoteUtils::CachedNoteList> noteCache_;
  std::unique_ptr<NoteUtils::EventIndex> cachedEventIndex_;
};

#endif  // LOOP_H
