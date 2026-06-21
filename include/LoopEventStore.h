//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#pragma once

#include <cstdint>
#include <cstddef>
#include <memory>
#include <vector>

#include "MidiEvent.h"
#include "Utils/ExtMemAllocator.h"

namespace LoopEventStoreConfig {
constexpr uint16_t CHUNK_CAPACITY = 256;
constexpr uint16_t POOL_CHUNK_COUNT = 512;
constexpr uint32_t BAR_TICKS = 768;
}  // namespace LoopEventStoreConfig

struct EventChunk {
  MidiEvent events[LoopEventStoreConfig::CHUNK_CAPACITY];
  uint16_t used = 0;
  uint32_t firstTick = 0;
  uint32_t lastTick = 0;
};

using ChunkIdList = std::vector<uint16_t, ExtMemAllocator<uint16_t>>;
using BarIndexVec = std::vector<size_t, ExtMemAllocator<size_t>>;

/// Append-only fixed-size event chunks backed by a global PSRAM pool.
class LoopEventStore {
 public:
  static constexpr size_t NO_EVENT_INDEX = static_cast<size_t>(-1);

  static void initPool();
  static void resetPoolForTests();

  static uint16_t usedChunkCount();
  static uint16_t freeChunkCount();
  static bool canAllocChunkWithReserve();

  LoopEventStore() = default;
  LoopEventStore(const LoopEventStore&) = delete;
  LoopEventStore& operator=(const LoopEventStore&) = delete;

  bool append(const MidiEvent& evt);
  size_t size() const { return eventCount_; }
  bool empty() const { return size() == 0; }
  const MidiEvent& at(size_t globalIndex) const;

  /// First global index whose event tick is >= tick, or size() if none.
  size_t lowerBoundIndex(uint32_t tick) const;
  /// First global index in a bar; returns size() when no events at/after that bar.
  size_t firstIndexForBar(uint32_t bar) const;
  /// Debug/inspection access to bar start index table.
  const BarIndexVec& barFirstIndices() const;

  void clear();

  /// Move chunk ownership from other into this (other cleared). O(chunks).
  void adoptAll(LoopEventStore& other);

  /// Merge tick-sorted events from other into this via chunk append (no flatten). O(events).
  void mergeFrom(LoopEventStore& other);

  void flatten(MidiEventVec& out) const;
  /// Append events from chunk refs (read-only; does not mutate ids).
  static void appendFlattenedChunkIds(const ChunkIdList& ids, MidiEventVec& out);
  void loadFromFlat(const MidiEventVec& events);

  /// Shift every event tick by delta; bumps up if any tick would go negative.
  void shiftAllTicks(int64_t delta);
  /// Drop events whose tick is >= tickLimit. Preserves event order.
  void dropEventsAtOrBeyondTick(uint32_t tickLimit);

  std::shared_ptr<LoopEventStore> cloneShared() const;

  const ChunkIdList& chunkIds() const { return chunkIds_; }

  /// Move chunk ownership out of this store into dest (this store cleared). Used by take Seal.
  void detachChunksTo(ChunkIdList& dest);

  /// Take ownership of chunk refs from ids (ids cleared). Used to release pending takes.
  void adoptChunkIds(ChunkIdList& ids);

 private:
  ChunkIdList chunkIds_;
  size_t eventCount_ = 0;
  mutable BarIndexVec barFirstIndices_;
  mutable bool barIndexDirty_ = true;
  uint32_t lastAppendedTick_ = 0;

  static EventChunk* pool_;
  static bool poolUsed_[LoopEventStoreConfig::POOL_CHUNK_COUNT];
  static bool poolReady_;

  uint16_t allocChunk();
  void freeChunk(uint16_t id);
  EventChunk& chunk(uint16_t id);
  const EventChunk& chunk(uint16_t id) const;
  bool appendToTailChunk(const MidiEvent& evt);
  void rebuildBarIndex() const;
  void markBarIndexDirty();
};
