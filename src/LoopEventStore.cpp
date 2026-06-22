//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "LoopEventStore.h"

#include <algorithm>
#include <cstring>
#include <limits>
#include <new>

#include "LoopPasses.h"

#if defined(ARDUINO)
#include "Logger.h"
#endif

namespace {

void* poolAlloc(size_t bytes) {
#if defined(ARDUINO) && defined(__IMXRT1062__)
  void* p = extmem_malloc(bytes);
  if (p) {
    return p;
  }
#endif
  return std::malloc(bytes);
}

void poolFree(void* p) {
  if (!p) {
    return;
  }
#if defined(ARDUINO) && defined(__IMXRT1062__)
  const uintptr_t addr = reinterpret_cast<uintptr_t>(p);
  if (addr >= 0x70000000u && addr < 0x78000000u) {
    extmem_free(p);
    return;
  }
#endif
  std::free(p);
}

}  // namespace

#if defined(ARDUINO) && defined(__IMXRT1062__)
extern "C" {
void* extmem_malloc(size_t size);
void extmem_free(void* ptr);
}
#endif

EventChunk* LoopEventStore::pool_ = nullptr;
bool LoopEventStore::poolUsed_[LoopEventStoreConfig::POOL_CHUNK_COUNT] = {};
bool LoopEventStore::poolReady_ = false;

void LoopEventStore::initPool() {
  if (poolReady_) {
    return;
  }
  const size_t bytes =
      static_cast<size_t>(LoopEventStoreConfig::POOL_CHUNK_COUNT) * sizeof(EventChunk);
  pool_ = static_cast<EventChunk*>(poolAlloc(bytes));
  if (!pool_) {
    return;
  }
  std::memset(pool_, 0, bytes);
  std::memset(poolUsed_, 0, sizeof(poolUsed_));
  poolReady_ = true;
}

void LoopEventStore::resetPoolForTests() {
  if (pool_) {
    poolFree(pool_);
  }
  pool_ = nullptr;
  poolReady_ = false;
  std::memset(poolUsed_, 0, sizeof(poolUsed_));
}

uint16_t LoopEventStore::usedChunkCount() {
  if (!poolReady_) {
    return 0;
  }
  uint16_t count = 0;
  for (uint16_t i = 0; i < LoopEventStoreConfig::POOL_CHUNK_COUNT; ++i) {
    if (poolUsed_[i]) {
      ++count;
    }
  }
  return count;
}

uint16_t LoopEventStore::freeChunkCount() {
  return LoopEventStoreConfig::POOL_CHUNK_COUNT - usedChunkCount();
}

bool LoopEventStore::canAllocChunkWithReserve() {
  return freeChunkCount() > PassConfig::CHUNK_RESERVE;
}

uint16_t LoopEventStore::allocChunk() {
  if (!poolReady_) {
    initPool();
  }
  if (!pool_) {
    return UINT16_MAX;
  }
  for (uint16_t i = 0; i < LoopEventStoreConfig::POOL_CHUNK_COUNT; ++i) {
    if (!poolUsed_[i]) {
      poolUsed_[i] = true;
      pool_[i].used = 0;
      pool_[i].firstTick = 0;
      pool_[i].lastTick = 0;
      return i;
    }
  }
  return UINT16_MAX;
}

void LoopEventStore::freeChunk(uint16_t id) {
  if (id >= LoopEventStoreConfig::POOL_CHUNK_COUNT || !pool_) {
    return;
  }
  pool_[id].used = 0;
  poolUsed_[id] = false;
}

EventChunk& LoopEventStore::chunk(uint16_t id) { return pool_[id]; }

const EventChunk& LoopEventStore::chunk(uint16_t id) const { return pool_[id]; }

bool LoopEventStore::appendToTailChunk(const MidiEvent& evt) {
  if (!poolReady_) {
    initPool();
  }
  if (!pool_) {
    return false;
  }

  uint16_t tailId = UINT16_MAX;
  if (!chunkIds_.empty()) {
    tailId = chunkIds_.back();
  }

  if (tailId == UINT16_MAX || chunk(tailId).used >= LoopEventStoreConfig::CHUNK_CAPACITY) {
    tailId = allocChunk();
    if (tailId == UINT16_MAX) {
#if defined(ARDUINO)
      logger.log(CAT_TRACK, LOG_WARNING,
                 "LoopEventStore chunk pool exhausted (events=%zu chunks=%zu)",
                 static_cast<size_t>(eventCount_), static_cast<size_t>(chunkIds_.size()));
#endif
      return false;
    }
    chunkIds_.push_back(tailId);
  }

  EventChunk& tail = chunk(tailId);
  const size_t globalIndex = eventCount_;
  tail.events[tail.used] = evt;
  if (tail.used == 0) {
    tail.firstTick = evt.tick;
  }
  tail.lastTick = evt.tick;
  ++tail.used;
  ++eventCount_;

  if (!barIndexDirty_) {
    if (globalIndex > 0 && evt.tick < lastAppendedTick_) {
      barIndexDirty_ = true;
    } else {
      const uint32_t bar = evt.tick / LoopEventStoreConfig::BAR_TICKS;
      if (barFirstIndices_.size() <= bar) {
        barFirstIndices_.resize(bar + 1, NO_EVENT_INDEX);
      }
      if (barFirstIndices_[bar] == NO_EVENT_INDEX) {
        barFirstIndices_[bar] = globalIndex;
      }
    }
  }
  lastAppendedTick_ = evt.tick;
  return true;
}

bool LoopEventStore::append(const MidiEvent& evt) { return appendToTailChunk(evt); }

const MidiEvent& LoopEventStore::at(size_t globalIndex) const {
  static const MidiEvent kEmpty{};
  if (!pool_ || globalIndex >= size()) {
    return kEmpty;
  }
  size_t cursor = 0;
  for (uint16_t id : chunkIds_) {
    const EventChunk& c = chunk(id);
    if (globalIndex < cursor + c.used) {
      return c.events[globalIndex - cursor];
    }
    cursor += c.used;
  }
  return kEmpty;
}

size_t LoopEventStore::lowerBoundIndex(uint32_t tick) const {
  size_t globalIndex = 0;
  for (uint16_t id : chunkIds_) {
    const EventChunk& c = chunk(id);
    if (c.used == 0) {
      continue;
    }
    if (c.lastTick < tick) {
      globalIndex += c.used;
      continue;
    }
    for (uint16_t i = 0; i < c.used; ++i) {
      if (c.events[i].tick >= tick) {
        return globalIndex + i;
      }
    }
    globalIndex += c.used;
  }
  return size();
}

const BarIndexVec& LoopEventStore::barFirstIndices() const {
  if (barIndexDirty_) {
    rebuildBarIndex();
  }
  return barFirstIndices_;
}

size_t LoopEventStore::firstIndexForBar(uint32_t bar) const {
  if (barIndexDirty_) {
    rebuildBarIndex();
  }
  if (bar >= barFirstIndices_.size()) {
    return size();
  }
  for (size_t b = bar; b < barFirstIndices_.size(); ++b) {
    if (barFirstIndices_[b] != NO_EVENT_INDEX) {
      return barFirstIndices_[b];
    }
  }
  return size();
}

void LoopEventStore::clear() {
  for (uint16_t id : chunkIds_) {
    freeChunk(id);
  }
  chunkIds_.clear();
  eventCount_ = 0;
  barFirstIndices_.clear();
  barIndexDirty_ = false;
  lastAppendedTick_ = 0;
}

void LoopEventStore::detachChunksTo(ChunkIdList& dest) {
  for (uint16_t id : dest) {
    freeChunk(id);
  }
  dest.clear();
  dest.swap(chunkIds_);
  chunkIds_.clear();
  eventCount_ = 0;
  markBarIndexDirty();
  lastAppendedTick_ = 0;
}

void LoopEventStore::adoptChunkIds(ChunkIdList& ids) {
  clear();
  chunkIds_.swap(ids);
  ids.clear();
  eventCount_ = 0;
  for (uint16_t id : chunkIds_) {
    eventCount_ += chunk(id).used;
  }
  if (!chunkIds_.empty()) {
    lastAppendedTick_ = chunk(chunkIds_.back()).lastTick;
  }
  markBarIndexDirty();
}

void LoopEventStore::adoptAll(LoopEventStore& other) {
  clear();
  chunkIds_.swap(other.chunkIds_);
  eventCount_ = other.eventCount_;
  barFirstIndices_.swap(other.barFirstIndices_);
  barIndexDirty_ = other.barIndexDirty_;
  lastAppendedTick_ = other.lastAppendedTick_;
  other.chunkIds_.clear();
  other.eventCount_ = 0;
  other.barFirstIndices_.clear();
  other.barIndexDirty_ = false;
  other.lastAppendedTick_ = 0;
}

void LoopEventStore::mergeFrom(LoopEventStore& other) {
  if (other.empty()) {
    return;
  }
  if (empty()) {
    adoptAll(other);
    return;
  }

  const size_t leftCount = size();
  const size_t rightCount = other.size();
  size_t leftIndex = 0;
  size_t rightIndex = 0;

  LoopEventStore merged;

  while (leftIndex < leftCount || rightIndex < rightCount) {
    const bool takeLeft = rightIndex >= rightCount ||
                          (leftIndex < leftCount &&
                           at(leftIndex).tick <= other.at(rightIndex).tick);
    if (takeLeft) {
      if (!merged.append(at(leftIndex))) {
        break;
      }
      ++leftIndex;
    } else {
      if (!merged.append(other.at(rightIndex))) {
        break;
      }
      ++rightIndex;
    }
  }

  other.clear();
  adoptAll(merged);
}

void LoopEventStore::flatten(MidiEventVec& out) const {
  out.clear();
  out.reserve(size());
  for (uint16_t id : chunkIds_) {
    const EventChunk& c = chunk(id);
    out.insert(out.end(), c.events, c.events + c.used);
  }
}

void LoopEventStore::appendFlattenedChunkId(uint16_t id, MidiEventVec& out) {
  if (id >= LoopEventStoreConfig::POOL_CHUNK_COUNT || !pool_ || !poolUsed_[id]) {
    return;
  }
  const EventChunk& c = pool_[id];
  out.reserve(out.size() + c.used);
  out.insert(out.end(), c.events, c.events + c.used);
}

void LoopEventStore::appendFlattenedChunkIds(const ChunkIdList& ids, MidiEventVec& out) {
  const size_t prevSize = out.size();
  size_t extra = 0;
  for (uint16_t id : ids) {
    if (id >= LoopEventStoreConfig::POOL_CHUNK_COUNT || !pool_ || !poolUsed_[id]) {
      continue;
    }
    extra += pool_[id].used;
  }
  out.reserve(prevSize + extra);
  for (uint16_t id : ids) {
    appendFlattenedChunkId(id, out);
  }
}

size_t LoopEventStore::countEventsInChunkIds(const ChunkIdList& ids) {
  size_t total = 0;
  for (uint16_t id : ids) {
    if (id >= LoopEventStoreConfig::POOL_CHUNK_COUNT || !pool_ || !poolUsed_[id]) {
      continue;
    }
    total += pool_[id].used;
  }
  return total;
}

void LoopEventStore::loadFromFlat(const MidiEventVec& events) {
  clear();
  for (const MidiEvent& evt : events) {
    if (!append(evt)) {
      break;
    }
  }
  if (!events.empty()) {
    lastAppendedTick_ = events.back().tick;
  }
}

void LoopEventStore::shiftAllTicks(int64_t delta) {
  if (delta == 0 || empty()) {
    return;
  }

  int64_t minT = std::numeric_limits<int64_t>::max();
  for (uint16_t id : chunkIds_) {
    const EventChunk& c = chunk(id);
    for (uint16_t i = 0; i < c.used; ++i) {
      const int64_t shifted = static_cast<int64_t>(c.events[i].tick) + delta;
      if (shifted < minT) {
        minT = shifted;
      }
    }
  }

  const int64_t bump = (minT < 0) ? -minT : 0;
  const int64_t total = delta + bump;
  if (total == 0) {
    return;
  }

  for (uint16_t id : chunkIds_) {
    EventChunk& c = chunk(id);
    for (uint16_t i = 0; i < c.used; ++i) {
      c.events[i].tick = static_cast<uint32_t>(static_cast<int64_t>(c.events[i].tick) + total);
    }
    if (c.used > 0) {
      c.firstTick = c.events[0].tick;
      c.lastTick = c.events[c.used - 1].tick;
    }
  }
  markBarIndexDirty();
}

void LoopEventStore::dropEventsAtOrBeyondTick(uint32_t tickLimit) {
  if (empty()) {
    return;
  }

  ChunkIdList kept;
  kept.reserve(chunkIds_.size());
  for (uint16_t id : chunkIds_) {
    EventChunk& c = chunk(id);
    uint16_t write = 0;
    for (uint16_t read = 0; read < c.used; ++read) {
      const MidiEvent& evt = c.events[read];
      if (evt.tick >= tickLimit) {
        continue;
      }
      if (write != read) {
        c.events[write] = evt;
      }
      ++write;
    }

    if (write == 0) {
      freeChunk(id);
      continue;
    }

    c.used = write;
    c.firstTick = c.events[0].tick;
    c.lastTick = c.events[write - 1].tick;
    kept.push_back(id);
  }

  chunkIds_.swap(kept);
  eventCount_ = 0;
  for (uint16_t id : chunkIds_) {
    eventCount_ += chunk(id).used;
  }
  markBarIndexDirty();
}

std::shared_ptr<LoopEventStore> LoopEventStore::cloneShared() const {
  auto copy = std::make_shared<LoopEventStore>();
  for (uint16_t id : chunkIds_) {
    const EventChunk& src = chunk(id);
    for (uint16_t i = 0; i < src.used; ++i) {
      copy->append(src.events[i]);
    }
  }
  return copy;
}

void LoopEventStore::markBarIndexDirty() {
  barIndexDirty_ = true;
}

void LoopEventStore::rebuildBarIndex() const {
  barFirstIndices_.clear();
  if (eventCount_ == 0) {
    barIndexDirty_ = false;
    return;
  }

  size_t globalIndex = 0;
  for (uint16_t id : chunkIds_) {
    const EventChunk& c = chunk(id);
    for (uint16_t i = 0; i < c.used; ++i) {
      const uint32_t bar = c.events[i].tick / LoopEventStoreConfig::BAR_TICKS;
      if (barFirstIndices_.size() <= bar) {
        barFirstIndices_.resize(bar + 1, NO_EVENT_INDEX);
      }
      if (barFirstIndices_[bar] == NO_EVENT_INDEX) {
        barFirstIndices_[bar] = globalIndex;
      }
      ++globalIndex;
    }
  }
  barIndexDirty_ = false;
}
