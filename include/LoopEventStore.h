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
}  // namespace LoopEventStoreConfig

struct EventChunk {
  MidiEvent events[LoopEventStoreConfig::CHUNK_CAPACITY];
  uint16_t used = 0;
  uint32_t firstTick = 0;
  uint32_t lastTick = 0;
};

using ChunkIdList = std::vector<uint16_t, ExtMemAllocator<uint16_t>>;

/// Append-only fixed-size event chunks backed by a global PSRAM pool.
class LoopEventStore {
 public:
  static void initPool();
  static void resetPoolForTests();

  LoopEventStore() = default;
  LoopEventStore(const LoopEventStore&) = delete;
  LoopEventStore& operator=(const LoopEventStore&) = delete;

  bool append(const MidiEvent& evt);
  size_t size() const;
  bool empty() const { return size() == 0; }
  const MidiEvent& at(size_t globalIndex) const;

  void clear();

  /// Move chunk ownership from other into this (other cleared). O(chunks).
  void adoptAll(LoopEventStore& other);

  /// Merge sorted events from other into this via new chunk list. O(events).
  void mergeFrom(LoopEventStore& other);

  void flatten(MidiEventVec& out) const;
  void loadFromFlat(const MidiEventVec& events);

  std::shared_ptr<LoopEventStore> cloneShared() const;

  const ChunkIdList& chunkIds() const { return chunkIds_; }

 private:
  ChunkIdList chunkIds_;

  static EventChunk* pool_;
  static bool poolUsed_[LoopEventStoreConfig::POOL_CHUNK_COUNT];
  static bool poolReady_;

  uint16_t allocChunk();
  void freeChunk(uint16_t id);
  EventChunk& chunk(uint16_t id);
  const EventChunk& chunk(uint16_t id) const;
  bool appendToTailChunk(const MidiEvent& evt);
};
