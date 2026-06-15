//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "LoopEventStore.h"

#include <algorithm>
#include <cstring>
#include <new>

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
      return false;
    }
    chunkIds_.push_back(tailId);
  }

  EventChunk& tail = chunk(tailId);
  tail.events[tail.used] = evt;
  if (tail.used == 0) {
    tail.firstTick = evt.tick;
  }
  tail.lastTick = evt.tick;
  ++tail.used;
  return true;
}

bool LoopEventStore::append(const MidiEvent& evt) { return appendToTailChunk(evt); }

size_t LoopEventStore::size() const {
  size_t total = 0;
  for (uint16_t id : chunkIds_) {
    total += chunk(id).used;
  }
  return total;
}

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

void LoopEventStore::clear() {
  for (uint16_t id : chunkIds_) {
    freeChunk(id);
  }
  chunkIds_.clear();
}

void LoopEventStore::adoptAll(LoopEventStore& other) {
  clear();
  chunkIds_.swap(other.chunkIds_);
  other.chunkIds_.clear();
}

void LoopEventStore::mergeFrom(LoopEventStore& other) {
  if (other.empty()) {
    return;
  }
  if (empty()) {
    adoptAll(other);
    return;
  }

  MidiEventVec left;
  MidiEventVec right;
  flatten(left);
  other.flatten(right);
  other.clear();

  MidiEventVec merged;
  merged.reserve(left.size() + right.size());
  std::merge(left.begin(), left.end(), right.begin(), right.end(), std::back_inserter(merged),
             [](const MidiEvent& a, const MidiEvent& b) { return a.tick < b.tick; });

  clear();
  loadFromFlat(merged);
}

void LoopEventStore::flatten(MidiEventVec& out) const {
  out.clear();
  out.reserve(size());
  for (uint16_t id : chunkIds_) {
    const EventChunk& c = chunk(id);
    out.insert(out.end(), c.events, c.events + c.used);
  }
}

void LoopEventStore::loadFromFlat(const MidiEventVec& events) {
  clear();
  for (const MidiEvent& evt : events) {
    if (!append(evt)) {
      break;
    }
  }
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
