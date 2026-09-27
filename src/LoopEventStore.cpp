//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "LoopEventStore.h"

#include <algorithm>
#include <cstring>
#include <limits>
#include <new>

#include "LoopPasses.h"
#include "PersistenceQueue.h"
#if defined(ARDUINO)
#include "Utils/MemoryMonitor.h"
#include "Logger.h"
#endif

#if defined(__IMXRT1062__)
#include <Arduino.h>
#define LOOP_EVENT_STORE_COLD_MEM FLASHMEM
#else
#define LOOP_EVENT_STORE_COLD_MEM
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

bool probeCommittedChunkIdBytes(size_t count) {
  if (count == 0) {
    return true;
  }
  const size_t bytes = count * sizeof(uint16_t);
  void* mem = poolAlloc(bytes);
  if (!mem) {
    return false;
  }
  poolFree(mem);
  return true;
}

}  // namespace

#if defined(ARDUINO) && defined(__IMXRT1062__)
extern "C" {
void* extmem_malloc(size_t size);
void extmem_free(void* ptr);
}
#endif

EventChunk* LoopEventStore::poolBlocks_[LoopEventStoreConfig::POOL_BLOCK_COUNT] = {};
#if defined(__IMXRT1062__)
DMAMEM bool LoopEventStore::poolUsed_[LoopEventStoreConfig::POOL_CHUNK_COUNT] = {};
DMAMEM ChunkLifecycleState LoopEventStore::poolLifecycle_[LoopEventStoreConfig::POOL_CHUNK_COUNT] =
    {};
DMAMEM uint16_t LoopEventStore::poolChunkRefCount_[LoopEventStoreConfig::POOL_CHUNK_COUNT] = {};
#else
bool LoopEventStore::poolUsed_[LoopEventStoreConfig::POOL_CHUNK_COUNT] = {};
ChunkLifecycleState LoopEventStore::poolLifecycle_[LoopEventStoreConfig::POOL_CHUNK_COUNT] = {};
uint16_t LoopEventStore::poolChunkRefCount_[LoopEventStoreConfig::POOL_CHUNK_COUNT] = {};
#endif
bool LoopEventStore::poolReady_ = false;
uint64_t LoopEventStore::poolLifetime_ = 1;
uint8_t LoopEventStore::sdLoadStagingDepth_ = 0;
uint8_t LoopEventStore::ephemeralSealDepth_ = 0;

void LoopEventStore::enterSdLoadStaging() {
  if (sdLoadStagingDepth_ < 255) {
    ++sdLoadStagingDepth_;
  }
}

void LoopEventStore::leaveSdLoadStaging() {
  if (sdLoadStagingDepth_ > 0) {
    --sdLoadStagingDepth_;
  }
}

bool LoopEventStore::isSdLoadStaging() { return sdLoadStagingDepth_ > 0; }

void LoopEventStore::enterEphemeralSeal() {
  if (ephemeralSealDepth_ < 255) {
    ++ephemeralSealDepth_;
  }
}

void LoopEventStore::leaveEphemeralSeal() {
  if (ephemeralSealDepth_ > 0) {
    --ephemeralSealDepth_;
  }
}

bool LoopEventStore::isEphemeralSeal() { return ephemeralSealDepth_ > 0; }

void LoopEventStore::initPool() {
  if (poolReady_) {
    return;
  }
  // RC-MEM-5: no boot reservation. The chunk array is opened block by block in `allocChunk`,
  // so a run that peaks well below `POOL_CHUNK_COUNT` never backs the whole 2.5 MB array. The
  // admission ceiling is unchanged; only the backing storage became lazy.
  for (uint16_t block = 0; block < LoopEventStoreConfig::POOL_BLOCK_COUNT; ++block) {
    poolBlocks_[block] = nullptr;
  }
  std::memset(poolUsed_, 0, sizeof(poolUsed_));
  for (uint16_t i = 0; i < LoopEventStoreConfig::POOL_CHUNK_COUNT; ++i) {
    poolLifecycle_[i] = ChunkLifecycleState::Free;
    poolChunkRefCount_[i] = 0;
  }
  poolReady_ = true;
  // Persistence tracking must not survive pool re-init (EXTMEM can outlive soft reset).
  PersistenceQueue::resetForTests();
}

void LoopEventStore::resetPoolForTests() {
  ++poolLifetime_;
  for (uint16_t block = 0; block < LoopEventStoreConfig::POOL_BLOCK_COUNT; ++block) {
    if (poolBlocks_[block] != nullptr) {
      poolFree(poolBlocks_[block]);
      poolBlocks_[block] = nullptr;
    }
  }
  poolReady_ = false;
  std::memset(poolUsed_, 0, sizeof(poolUsed_));
  std::memset(poolLifecycle_, 0, sizeof(poolLifecycle_));
  std::memset(poolChunkRefCount_, 0, sizeof(poolChunkRefCount_));
  sdLoadStagingDepth_ = 0;
  ephemeralSealDepth_ = 0;
  PersistenceQueue::resetForTests();
}

uint16_t LoopEventStore::poolBlockCount() {
  uint16_t count = 0;
  for (uint16_t block = 0; block < LoopEventStoreConfig::POOL_BLOCK_COUNT; ++block) {
    if (poolBlocks_[block] != nullptr) {
      ++count;
    }
  }
  return count;
}

size_t LoopEventStore::poolBackedBytes() {
  return static_cast<size_t>(poolBlockCount()) *
         LoopEventStoreConfig::POOL_CHUNKS_PER_BLOCK * sizeof(EventChunk);
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

bool LoopEventStore::hasInternalHeapHeadroomForNonCriticalWork(uint32_t freeHeapBytes) {
  return freeHeapBytes >= LoopEventStoreConfig::INTERNAL_HEAP_SAFETY_FLOOR_BYTES;
}

uint32_t LoopEventStore::internalHeapSafetyFloorBytes() {
  return LoopEventStoreConfig::INTERNAL_HEAP_SAFETY_FLOOR_BYTES;
}

ChunkLifecycleState LoopEventStore::chunkLifecycleState(uint16_t id) {
  if (id >= LoopEventStoreConfig::POOL_CHUNK_COUNT) {
    return ChunkLifecycleState::Free;
  }
  return poolLifecycle_[id];
}

uint16_t LoopEventStore::chunkReferenceCount(uint16_t id) {
  if (id >= LoopEventStoreConfig::POOL_CHUNK_COUNT) {
    return 0;
  }
  return poolChunkRefCount_[id];
}

void LoopEventStore::retainChunkReference(uint16_t id) {
  if (id >= LoopEventStoreConfig::POOL_CHUNK_COUNT) {
    return;
  }
  ++poolChunkRefCount_[id];
}

void LoopEventStore::releaseChunkReference(uint16_t id) {
  if (id >= LoopEventStoreConfig::POOL_CHUNK_COUNT || poolChunkRefCount_[id] == 0) {
    return;
  }
  --poolChunkRefCount_[id];
}

void LOOP_EVENT_STORE_COLD_MEM LoopEventStore::releaseChunkRefs(const CaptureChunkIdList& refs) {
  for (uint16_t id : refs) {
    releaseChunkReference(id);
    tryFreeChunk(id);
  }
}

void LOOP_EVENT_STORE_COLD_MEM LoopEventStore::releaseChunkRefs(const CommittedChunkIdList& refs) {
  for (uint16_t id : refs) {
    releaseChunkReference(id);
    tryFreeChunk(id);
  }
}

bool LOOP_EVENT_STORE_COLD_MEM LoopEventStore::hasHeadroomForCommittedChunkIdList(size_t count) {
  if (count == 0) {
    return true;
  }
#if defined(PIO_UNIT_TEST_NATIVE) || !defined(ARDUINO)
  (void)count;
  return true;
#else
  // Never call getExternalMemoryPoolFreeBytes() here — sm_malloc_stats_pool walks the
  // entire PSRAM pool (~300ms on 8MB; session_20260718_224607 parse_us ≈295ms per finalize).
  // probeCommittedChunkIdBytes() in tryAssignCommittedChunkIds is the real alloc gate.
  const size_t bytes = count * sizeof(uint16_t);
  if (MemoryMonitor::isExternalMemoryPoolAvailable()) {
    return true;
  }
  return MemoryMonitor::getInternalHeapFreeBytes() >= bytes;
#endif
}

bool LOOP_EVENT_STORE_COLD_MEM LoopEventStore::tryAssignCommittedChunkIds(CommittedChunkIdList& dest,
                                                        const uint16_t* ids, size_t count) {
  releaseChunkRefs(dest);
  dest.clear();
  if (count == 0) {
    return true;
  }
  if (!hasHeadroomForCommittedChunkIdList(count)) {
    return false;
  }
  if (!probeCommittedChunkIdBytes(count)) {
    return false;
  }
  CommittedChunkIdList built;
  for (size_t i = 0; i < count; ++i) {
    built.push_back(ids[i]);
    if (built.size() != i + 1) {
      releaseChunkRefs(built);
      dest.clear();
      return false;
    }
  }
  dest = std::move(built);
  return true;
}

bool LOOP_EVENT_STORE_COLD_MEM LoopEventStore::tryCopyCommittedChunkIds(CommittedChunkIdList& dest,
                                                        const CommittedChunkIdList& src) {
  if (src.empty()) {
    releaseChunkRefs(dest);
    dest.clear();
    return true;
  }
  return tryAssignCommittedChunkIds(dest, src.data(), src.size());
}

bool LOOP_EVENT_STORE_COLD_MEM LoopEventStore::deepCloneCommittedChunkIds(
    CommittedChunkIdList& dest, const CommittedChunkIdList& src) {
  releaseChunkRefs(dest);
  dest.clear();
  if (src.empty()) {
    return true;
  }
  SessionMidiEventVec flat;
  appendChunkRefEvents(src, flat);
  if (flat.empty()) {
    return false;
  }
  LoopEventStore staging;
  staging.loadFromEvents(flat);
  if (staging.empty()) {
    return false;
  }
  CommittedChunkIdList cloned;
  if (!staging.detachChunksToCommittedChunkIds(cloned)) {
    return false;
  }
  dest = std::move(cloned);
  return true;
}

bool LOOP_EVENT_STORE_COLD_MEM LoopEventStore::transferCaptureChunkIdsToCommittedChunkIds(CommittedChunkIdList& dest,
                                                        CaptureChunkIdList& src) {
  if (!tryAssignCommittedChunkIds(dest, src.data(), src.size())) {
    return false;
  }
  src.clear();
  return true;
}

void LoopEventStore::sealChunk(uint16_t id) {
  if (id >= LoopEventStoreConfig::POOL_CHUNK_COUNT ||
      poolLifecycle_[id] != ChunkLifecycleState::Recording) {
    return;
  }
  poolLifecycle_[id] = ChunkLifecycleState::Sealed;
  if (isSdLoadStaging()) {
    (void)PersistenceQueue::markChunkPersistedFromSdLoad(id);
  } else if (isEphemeralSeal()) {
    // Derived-view copies must not enter mid_pass.
  } else {
    PersistenceQueue::admitSealedChunk(id);
  }
}

bool LoopEventStore::isChunkSealed(uint16_t id) {
  return id < LoopEventStoreConfig::POOL_CHUNK_COUNT &&
         poolLifecycle_[id] == ChunkLifecycleState::Sealed;
}

void LoopEventStore::tryFreeChunk(uint16_t id) {
  if (id >= LoopEventStoreConfig::POOL_CHUNK_COUNT || !poolReady_ || poolChunkRefCount_[id] != 0) {
    return;
  }
  EventChunk* block = chunkBlock(id);
  if (block != nullptr) {
    EventChunk& retired = block[id % LoopEventStoreConfig::POOL_CHUNKS_PER_BLOCK];
    retired.used = 0;
    retired.firstTick = 0;
    retired.lastTick = 0;
  }
  poolUsed_[id] = false;
  poolLifecycle_[id] = ChunkLifecycleState::Free;
  PersistenceQueue::onChunkFreed(id);
}

uint16_t LoopEventStore::allocChunk() {
  if (!poolReady_) {
    initPool();
  }
  if (!poolReady_) {
    return UINT16_MAX;
  }
  for (uint16_t i = 0; i < LoopEventStoreConfig::POOL_CHUNK_COUNT; ++i) {
    if (!poolUsed_[i]) {
      // RC-MEM-5: open this id's block on first use. Opening is the only allocation in the
      // pool now, and it happens before the id is marked used, so `chunkSlot(i)` is always
      // safe for a used id. A block that cannot be opened denies the chunk exactly as the
      // whole-pool allocation failure used to.
      const uint16_t blockIndex = i / LoopEventStoreConfig::POOL_CHUNKS_PER_BLOCK;
      if (poolBlocks_[blockIndex] == nullptr) {
        const size_t bytes =
            static_cast<size_t>(LoopEventStoreConfig::POOL_CHUNKS_PER_BLOCK) * sizeof(EventChunk);
        EventChunk* block = static_cast<EventChunk*>(poolAlloc(bytes));
        if (block == nullptr) {
          return UINT16_MAX;
        }
        std::memset(block, 0, bytes);
        poolBlocks_[blockIndex] = block;
      }
      poolUsed_[i] = true;
      EventChunk& opened = chunkSlot(i);
      opened.used = 0;
      opened.firstTick = 0;
      opened.lastTick = 0;
      poolLifecycle_[i] = ChunkLifecycleState::Recording;
      return i;
    }
  }
  return UINT16_MAX;
}

void LoopEventStore::freeChunk(uint16_t id) {
  releaseChunkReference(id);
  tryFreeChunk(id);
}

EventChunk& LoopEventStore::chunk(uint16_t id) { return chunkSlot(id); }

const EventChunk& LoopEventStore::chunk(uint16_t id) const { return chunkSlot(id); }

bool LoopEventStore::hasSealedChunks() const {
  for (uint16_t id : chunkIds_) {
    if (isChunkSealed(id)) {
      return true;
    }
  }
  return false;
}

bool LoopEventStore::appendToTailChunk(const MidiEvent& evt, LoopEventStoreAppendDeny* denyOut,
                                      ChunkReservation* reservation) {
  if (denyOut != nullptr) {
    *denyOut = LoopEventStoreAppendDeny::None;
  }
  if (capturePoolLifetime_ && capturePoolLifetime_ != poolLifetime_) {
    if (denyOut) *denyOut = LoopEventStoreAppendDeny::PoolUnavailable;
    return false;
  }
  if (!poolReady_) {
    initPool();
  }
  if (!poolReady_) {
    if (denyOut != nullptr) {
      *denyOut = LoopEventStoreAppendDeny::PoolUnavailable;
    }
    return false;
  }
  // A tick beyond any supported loop length is a torn/uninitialised value, not content. Refuse it here:
  // letting it through sizes `barFirstIndices_` to `tick / BAR_TICKS + 1`, which for a near-UINT32_MAX
  // tick is a ~22 MB single request that aborts the allocator (`#MEM_EXT_REFUSED bytes=22369620`).
  // Refusing names the reason instead of silently clamping, so the corrupt producer stays visible.
  if (evt.tick > LoopEventStoreConfig::MAX_INDEXED_TICKS) {
    if (denyOut != nullptr) {
      *denyOut = LoopEventStoreAppendDeny::TickOutOfRange;
    }
#if defined(ARDUINO)
    logger.log(CAT_TRACK, LOG_WARNING, "LoopEventStore refused out-of-range tick %lu (max %lu)",
               static_cast<unsigned long>(evt.tick),
               static_cast<unsigned long>(LoopEventStoreConfig::MAX_INDEXED_TICKS));
#endif
    return false;
  }

  uint16_t tailId = UINT16_MAX;
  if (!chunkIds_.empty()) {
    tailId = chunkIds_.back();
    if (isChunkSealed(tailId)) {
      tailId = UINT16_MAX;
    } else if (chunk(tailId).used >= LoopEventStoreConfig::CHUNK_CAPACITY) {
      sealChunk(tailId);
      tailId = UINT16_MAX;
    }
  }

  if (tailId == UINT16_MAX) {
    tailId = reservation ? reservation->take() : allocChunk();
    if (tailId == UINT16_MAX) {
      if (denyOut != nullptr) {
        *denyOut = LoopEventStoreAppendDeny::PoolExhausted;
      }
#if defined(ARDUINO)
      logger.log(CAT_TRACK, LOG_WARNING,
                 "LoopEventStore chunk pool exhausted (events=%zu chunks=%zu)",
                 static_cast<size_t>(eventCount_), static_cast<size_t>(chunkIds_.size()));
#endif
      return false;
    }
    if (!reservation) retainChunkReference(tailId); // Reserved ownership moves without a ref walk.
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
    // SD load staging: defer bar index — InternalHeapFirstAllocator resize under
    // multi-track PLAYING can abort() when RAM1 is near empty (234625).
    if (isSdLoadStaging()) {
      barIndexDirty_ = true;
    } else if (globalIndex > 0 && evt.tick < lastAppendedTick_) {
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

  if (tail.used >= LoopEventStoreConfig::CHUNK_CAPACITY) {
    sealChunk(tailId);
  }
  return true;
}

bool LoopEventStore::append(const MidiEvent& evt) { return appendToTailChunk(evt, nullptr); }

bool LoopEventStore::append(const MidiEvent& evt, LoopEventStoreAppendDeny* denyOut) {
  return appendToTailChunk(evt, denyOut);
}

const MidiEvent& LoopEventStore::at(size_t globalIndex) const {
  static const MidiEvent kEmpty{};
  if (!poolReady_ || globalIndex >= size()) {
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

bool LOOP_EVENT_STORE_COLD_MEM LoopEventStore::ownsRewriteCursor(
    const LoopEventStoreRewriteCursor& cursor) const {
  return cursor.owner == this && cursor.ownershipGeneration == ownershipGeneration_ &&
      cursor.poolLifetime == poolLifetime_;
}

bool LOOP_EVENT_STORE_COLD_MEM LoopEventStore::beginRewriteFromOrderedSource(LoopEventStoreRewriteCursor& cursor,
                                                    size_t sourceCount) const {
  cursor = LoopEventStoreRewriteCursor{};
  if (!poolReady_ || sourceCount > eventCount_) {
    return false;
  }
  cursor.owner = this;
  cursor.ownershipGeneration = ownershipGeneration_;
  cursor.poolLifetime = poolLifetime_;
  cursor.targetCount = sourceCount;
  cursor.trimRemaining = sourceCount;
  cursor.active = true;
  return true;
}

// Maintenance grain: keep the bounded rewrite worker in flash, preserving ITCM
// for capture ingress and avoiding another 32 KiB RAM1 allocation bank.
LoopEventStoreRewriteResult LOOP_EVENT_STORE_COLD_MEM LoopEventStore::stepRewriteFromOrderedSource(
    LoopEventStoreRewriteCursor& cursor, const MidiEvent* source,
    const uint16_t* orderedIndices, size_t sourceCount, size_t sourceCapacity,
    size_t eventQuantum) {
  if (cursor.owner != this || cursor.ownershipGeneration != ownershipGeneration_ ||
      cursor.poolLifetime != poolLifetime_) return LoopEventStoreRewriteResult::Denied;
  if (!cursor.active) {
    return cursor.sourceIndex == sourceCount && cursor.targetCount == sourceCount
               ? LoopEventStoreRewriteResult::Complete
               : LoopEventStoreRewriteResult::Denied;
  }
  if ((sourceCount != 0 && (source == nullptr || orderedIndices == nullptr)) ||
      sourceCount != cursor.targetCount || sourceCapacity < sourceCount || eventQuantum == 0 ||
      cursor.sourceIndex > sourceCount ||
      cursor.chunkIndex > chunkIds_.size()) {
    return LoopEventStoreRewriteResult::Denied;
  }

  if (cursor.trimming) {
    if (cursor.chunkIndex >= chunkIds_.size()) {
      eventCount_ = sourceCount;
      lastAppendedTick_ = sourceCount == 0 ? 0 : source[orderedIndices[sourceCount - 1]].tick;
      markBarIndexDirty();
      cursor.active = false;
      return LoopEventStoreRewriteResult::Complete;
    }
    EventChunk& destination = chunk(chunkIds_[cursor.chunkIndex]);
    if (cursor.trimRemaining >= destination.used) {
      cursor.trimRemaining -= destination.used;
      ++cursor.chunkIndex;
      return LoopEventStoreRewriteResult::Pending;
    }
    if (cursor.trimRemaining != 0) {
      destination.used = static_cast<uint16_t>(cursor.trimRemaining);
      destination.lastTick = destination.events[destination.used - 1].tick;
      cursor.trimRemaining = 0;
      ++cursor.chunkIndex;
      return LoopEventStoreRewriteResult::Pending;
    }
    const uint16_t retired = chunkIds_[cursor.chunkIndex];
    releaseChunkReference(retired);
    chunkIds_.erase(chunkIds_.begin() + static_cast<ptrdiff_t>(cursor.chunkIndex));
    return LoopEventStoreRewriteResult::Pending;
  }

  size_t remaining = eventQuantum;
  while (remaining > 0 && cursor.sourceIndex < sourceCount) {
    if (cursor.chunkIndex >= chunkIds_.size()) {
      return LoopEventStoreRewriteResult::Denied;
    }
    EventChunk& destination = chunk(chunkIds_[cursor.chunkIndex]);
    if (cursor.eventIndexInChunk >= destination.used) {
      ++cursor.chunkIndex;
      cursor.eventIndexInChunk = 0;
      continue;
    }
    const uint16_t sourceIndex = orderedIndices[cursor.sourceIndex];
    if (sourceIndex >= sourceCapacity) {
      return LoopEventStoreRewriteResult::Denied;
    }
    const MidiEvent& replacement = source[sourceIndex];
    destination.events[cursor.eventIndexInChunk] = replacement;
    if (cursor.eventIndexInChunk == 0) {
      destination.firstTick = replacement.tick;
    }
    destination.lastTick = replacement.tick;
    ++cursor.sourceIndex;
    ++cursor.eventIndexInChunk;
    --remaining;
  }

  if (cursor.sourceIndex != sourceCount) {
    return LoopEventStoreRewriteResult::Pending;
  }
  cursor.trimming = true;
  cursor.chunkIndex = 0;
  cursor.eventIndexInChunk = 0;
  return LoopEventStoreRewriteResult::Pending;
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
  ++ownershipGeneration_;
  if (!capturePoolLifetime_ || capturePoolLifetime_ == poolLifetime_) {
    for (uint16_t id : chunkIds_) freeChunk(id);
  }
  capturePoolLifetime_ = 0;
  chunkIds_.clear();
  eventCount_ = 0;
  barFirstIndices_.clear();
  barIndexDirty_ = false;
  lastAppendedTick_ = 0;
}

void LOOP_EVENT_STORE_COLD_MEM LoopEventStore::detachChunksTo(CaptureChunkIdList& dest) {
  releaseChunkRefs(dest);
  dest.clear();

  for (uint16_t id : chunkIds_) {
    if (chunk(id).used > 0) {
      sealChunk(id);
    }
  }

  CaptureChunkIdList transferred;
  transferred.swap(chunkIds_);
  for (uint16_t id : transferred) {
    releaseChunkReference(id);
  }
  eventCount_ = 0;
  markBarIndexDirty();
  lastAppendedTick_ = 0;

  dest.swap(transferred);
  for (uint16_t id : dest) {
    retainChunkReference(id);
  }
}

bool LOOP_EVENT_STORE_COLD_MEM LoopEventStore::beginCommittedChunkTransfer(
    LoopEventStoreCommitCursor& cursor, CommittedChunkIdList& destination,
    const uint64_t& generation) {
  if (cursor.active_ || !destination.empty() || destination.capacity() < chunkIds_.size() ||
      (!chunkIds_.empty() && !reservedAppendReady()) ||
      (capturePoolLifetime_ && capturePoolLifetime_ != poolLifetime_)) return false;
  cursor = LoopEventStoreCommitCursor{};
  cursor.owner_ = this;
  cursor.destination_ = &destination;
  cursor.destinationData_ = destination.data();
  cursor.generation_ = &generation;
  cursor.expectedGeneration_ = generation;
  cursor.poolLifetime_ = poolLifetime_;
  cursor.ownershipGeneration_ = ++ownershipGeneration_;
  cursor.remainingChunks_ = chunkIds_.size();
  cursor.remainingEvents_ = eventCount_;
  cursor.active_ = true;
  return true;
}

LoopEventStoreRewriteResult LOOP_EVENT_STORE_COLD_MEM LoopEventStore::stepCommittedChunkTransfer(
    LoopEventStoreCommitCursor& cursor, size_t quantum) {
  if (cursor.owner_ != this || !cursor.destination_ || !cursor.generation_ ||
      *cursor.generation_ != cursor.expectedGeneration_ ||
      cursor.ownershipGeneration_ != ownershipGeneration_ || cursor.poolLifetime_ != poolLifetime_ ||
      cursor.destination_->data() != cursor.destinationData_ ||
      cursor.destination_->size() != cursor.transferred_ ||
      chunkIds_.size() != cursor.remainingChunks_ || eventCount_ != cursor.remainingEvents_)
    return LoopEventStoreRewriteResult::Denied;
  if (cursor.complete_) return LoopEventStoreRewriteResult::Complete;
  if (!cursor.active_ || quantum == 0) return LoopEventStoreRewriteResult::Denied;
  auto& destination = *cursor.destination_;
  while (quantum) {
    if (!chunkIds_.empty()) {
      const uint16_t id = chunkIds_.back();
      if (!poolReady_ || id >= LoopEventStoreConfig::POOL_CHUNK_COUNT || !poolUsed_[id] ||
          poolChunkRefCount_[id] == 0 || destination.size() == destination.capacity())
        return LoopEventStoreRewriteResult::Denied;
      const auto& source = chunk(id);
      if (source.used > eventCount_) return LoopEventStoreRewriteResult::Denied;
      if (source.used) sealChunk(id);
      // Existing reference ownership moves; no release/retain churn and no row copy.
      destination.push_back(id);
      chunkIds_.pop_back();
      eventCount_ -= source.used;
      --cursor.remainingChunks_;
      cursor.remainingEvents_ = eventCount_;
      ++cursor.transferred_;
      cursor.ownershipGeneration_ = ++ownershipGeneration_;
      lastAppendedTick_ = chunkIds_.empty() ? 0 : chunk(chunkIds_.back()).lastTick;
      markBarIndexDirty();
    } else if (cursor.reverseIndex_ < destination.size() / 2) {
      std::swap(destination[cursor.reverseIndex_],
                destination[destination.size() - 1 - cursor.reverseIndex_]);
      ++cursor.reverseIndex_;
      cursor.ownershipGeneration_ = ++ownershipGeneration_;
    } else {
      break;
    }
    --quantum;
  }
  if (chunkIds_.empty() && cursor.reverseIndex_ == destination.size() / 2) {
    cursor.active_ = false;
    cursor.complete_ = true;
    return LoopEventStoreRewriteResult::Complete;
  }
  return LoopEventStoreRewriteResult::Pending;
}

bool LOOP_EVENT_STORE_COLD_MEM LoopEventStore::detachChunksToCommittedChunkIds(CommittedChunkIdList& dest) {
  releaseChunkRefs(dest);
  dest.clear();

  // Prepared callers already own directory capacity. Keep the synchronous API
  // until their worker phases migrate, but share the bounded transfer primitive.
  if (dest.capacity() >= chunkIds_.size() && (chunkIds_.empty() || reservedAppendReady())) {
    const uint64_t generation = ownershipGeneration_;
    LoopEventStoreCommitCursor cursor;
    if (!beginCommittedChunkTransfer(cursor, dest, generation)) return false;
    LoopEventStoreRewriteResult result;
    do { result = stepCommittedChunkTransfer(cursor, 1); }
    while (result == LoopEventStoreRewriteResult::Pending);
    return result == LoopEventStoreRewriteResult::Complete;
  }

  for (uint16_t id : chunkIds_) {
    if (chunk(id).used > 0) {
      sealChunk(id);
    }
  }

  const size_t count = chunkIds_.size();
  if (count == 0) {
    eventCount_ = 0;
    markBarIndexDirty();
    lastAppendedTick_ = 0;
    return true;
  }

  // Legacy stores without complete capture/persistence prewarming retain their
  // synchronous path, including the existing preallocated-directory shortcut.
  if (dest.capacity() >= count) {
    for (uint16_t id : chunkIds_) dest.push_back(id);
  } else {
    CommittedChunkIdList committedChunkIds;
    if (!tryAssignCommittedChunkIds(committedChunkIds, chunkIds_.data(), count)) return false;
    dest = std::move(committedChunkIds);
  }

  for (uint16_t id : chunkIds_) {
    releaseChunkReference(id);
  }
  chunkIds_.clear();
  eventCount_ = 0;
  markBarIndexDirty();
  lastAppendedTick_ = 0;

  for (uint16_t id : dest) {
    retainChunkReference(id);
  }
  return true;
}

void LoopEventStore::adoptChunkIds(CaptureChunkIdList& ids) {
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

void LoopEventStore::copyEventsTo(MidiEventVec& out) const {
  out.clear();
  out.reserve(size());
  for (uint16_t id : chunkIds_) {
    const EventChunk& c = chunk(id);
    out.insert(out.end(), c.events, c.events + c.used);
  }
}

void LOOP_EVENT_STORE_COLD_MEM LoopEventStore::appendChunkRefEvent(uint16_t id, MidiEventVec& out) {
  if (id >= LoopEventStoreConfig::POOL_CHUNK_COUNT || !poolReady_ || !poolUsed_[id]) {
    return;
  }
  const EventChunk& c = chunkSlot(id);
  out.reserve(out.size() + c.used);
  out.insert(out.end(), c.events, c.events + c.used);
}

void LOOP_EVENT_STORE_COLD_MEM LoopEventStore::appendChunkRefEvent(
    uint16_t id, std::vector<MidiEvent, ExternalMemoryFirstAllocator<MidiEvent>>& out) {
  if (id >= LoopEventStoreConfig::POOL_CHUNK_COUNT || !poolReady_ || !poolUsed_[id]) {
    return;
  }
  const EventChunk& c = chunkSlot(id);
  out.reserve(out.size() + c.used);
  out.insert(out.end(), c.events, c.events + c.used);
}

uint16_t LoopEventStore::chunkUsedCount(uint16_t id) {
  if (id >= LoopEventStoreConfig::POOL_CHUNK_COUNT || !poolReady_ || !poolUsed_[id]) {
    return 0;
  }
  return chunkSlot(id).used;
}

bool LOOP_EVENT_STORE_COLD_MEM LoopEventStore::beginReadSealedChunks(
    LoopEventStoreReadCursor& cursor, const uint16_t* refs, size_t refCount,
    MidiEvent* output, size_t capacity, const uint64_t& generation) {
  if (cursor.active_) return false;
  cursor.discard();
  if ((refCount && !refs) || (capacity && !output)) return false;
  cursor.refs_ = refs;
  cursor.refCount_ = refCount;
  cursor.output_ = output;
  cursor.capacity_ = capacity;
  cursor.generation_ = &generation;
  cursor.expected_ = generation;
  cursor.active_ = true;
  return true;
}

LoopEventStoreReadResult LOOP_EVENT_STORE_COLD_MEM LoopEventStore::stepReadSealedChunks(
    LoopEventStoreReadCursor& cursor, size_t quantum) {
  if (!cursor.generation_ || *cursor.generation_ != cursor.expected_) {
    cursor.discard();
    return LoopEventStoreReadResult::Denied;
  }
  if (cursor.complete_) return LoopEventStoreReadResult::Complete;
  if (!cursor.active_) return LoopEventStoreReadResult::Denied;
  while (quantum && cursor.chunk_ < cursor.refCount_) {
    --quantum;
    const uint16_t id = cursor.refs_[cursor.chunk_];
    if (id >= LoopEventStoreConfig::POOL_CHUNK_COUNT || !poolReady_ ||
        !poolUsed_[id] || !isChunkSealed(id)) {
      cursor.discard();
      return LoopEventStoreReadResult::Denied;
    }
    const EventChunk& chunk = chunkSlot(id);
    if (cursor.event_ == chunk.used) {
      ++cursor.chunk_;
      cursor.event_ = 0;
    } else {
      if (cursor.count_ == cursor.capacity_) {
        cursor.discard();
        return LoopEventStoreReadResult::Denied;
      }
      cursor.output_[cursor.count_++] = chunk.events[cursor.event_++];
    }
  }
  if (cursor.chunk_ != cursor.refCount_) return LoopEventStoreReadResult::Pending;
  cursor.active_ = false;
  cursor.complete_ = true;
  return LoopEventStoreReadResult::Complete;
}

void LOOP_EVENT_STORE_COLD_MEM LoopEventStore::appendChunkRefEventRange(uint16_t id, uint32_t begin,
                                                                       uint32_t endExclusive,
                                                                       MidiEventVec& out) {
  if (id >= LoopEventStoreConfig::POOL_CHUNK_COUNT || !poolReady_ || !poolUsed_[id]) {
    return;
  }
  const EventChunk& c = chunkSlot(id);
  if (begin >= c.used) {
    return;
  }
  if (endExclusive > c.used) {
    endExclusive = c.used;
  }
  if (begin >= endExclusive) {
    return;
  }
  out.reserve(out.size() + (c.used - begin));
  out.insert(out.end(), c.events + begin, c.events + endExclusive);
}

void LOOP_EVENT_STORE_COLD_MEM LoopEventStore::appendChunkRefEventRange(
    uint16_t id, uint32_t begin, uint32_t endExclusive,
    std::vector<MidiEvent, ExternalMemoryFirstAllocator<MidiEvent>>& out) {
  if (id >= LoopEventStoreConfig::POOL_CHUNK_COUNT || !poolReady_ || !poolUsed_[id]) {
    return;
  }
  const EventChunk& c = chunkSlot(id);
  if (begin >= c.used) {
    return;
  }
  if (endExclusive > c.used) {
    endExclusive = c.used;
  }
  if (begin >= endExclusive) {
    return;
  }
  out.reserve(out.size() + (c.used - begin));
  out.insert(out.end(), c.events + begin, c.events + endExclusive);
}

void LOOP_EVENT_STORE_COLD_MEM LoopEventStore::appendChunkRefEvents(const CaptureChunkIdList& ids, MidiEventVec& out) {
  const size_t prevSize = out.size();
  size_t extra = 0;
  for (uint16_t id : ids) {
    if (id >= LoopEventStoreConfig::POOL_CHUNK_COUNT || !poolReady_ || !poolUsed_[id]) {
      continue;
    }
    extra += chunkSlot(id).used;
  }
  out.reserve(prevSize + extra);
  for (uint16_t id : ids) {
    appendChunkRefEvent(id, out);
  }
}

void LOOP_EVENT_STORE_COLD_MEM LoopEventStore::appendChunkRefEvents(const CommittedChunkIdList& ids, MidiEventVec& out) {
  const size_t prevSize = out.size();
  size_t extra = 0;
  for (uint16_t id : ids) {
    if (id >= LoopEventStoreConfig::POOL_CHUNK_COUNT || !poolReady_ || !poolUsed_[id]) {
      continue;
    }
    extra += chunkSlot(id).used;
  }
  out.reserve(prevSize + extra);
  for (uint16_t id : ids) {
    appendChunkRefEvent(id, out);
  }
}

void LOOP_EVENT_STORE_COLD_MEM LoopEventStore::appendChunkRefEvents(
    const CaptureChunkIdList& ids,
    std::vector<MidiEvent, ExternalMemoryFirstAllocator<MidiEvent>>& out) {
  const size_t prevSize = out.size();
  size_t extra = 0;
  for (uint16_t id : ids) {
    if (id >= LoopEventStoreConfig::POOL_CHUNK_COUNT || !poolReady_ || !poolUsed_[id]) {
      continue;
    }
    extra += chunkSlot(id).used;
  }
  out.reserve(prevSize + extra);
  for (uint16_t id : ids) {
    appendChunkRefEvent(id, out);
  }
}

void LOOP_EVENT_STORE_COLD_MEM LoopEventStore::appendChunkRefEvents(
    const CommittedChunkIdList& ids,
    std::vector<MidiEvent, ExternalMemoryFirstAllocator<MidiEvent>>& out) {
  const size_t prevSize = out.size();
  size_t extra = 0;
  for (uint16_t id : ids) {
    if (id >= LoopEventStoreConfig::POOL_CHUNK_COUNT || !poolReady_ || !poolUsed_[id]) {
      continue;
    }
    extra += chunkSlot(id).used;
  }
  out.reserve(prevSize + extra);
  for (uint16_t id : ids) {
    appendChunkRefEvent(id, out);
  }
}

bool LOOP_EVENT_STORE_COLD_MEM LoopEventStore::chunkTickSpan(uint16_t id, uint32_t& firstTick,
                                                             uint32_t& lastTick) {
  if (id >= LoopEventStoreConfig::POOL_CHUNK_COUNT || !poolReady_ || !poolUsed_[id]) {
    return false;
  }
  const EventChunk& c = chunkSlot(id);
  if (c.used == 0) {
    return false;
  }
  firstTick = c.firstTick;
  lastTick = c.lastTick;
  return true;
}

size_t LOOP_EVENT_STORE_COLD_MEM LoopEventStore::countEventsInChunkIds(const CaptureChunkIdList& ids) {
  size_t total = 0;
  for (uint16_t id : ids) {
    if (id >= LoopEventStoreConfig::POOL_CHUNK_COUNT || !poolReady_ || !poolUsed_[id]) {
      continue;
    }
    total += chunkSlot(id).used;
  }
  return total;
}

size_t LOOP_EVENT_STORE_COLD_MEM LoopEventStore::countEventsInChunkIds(
    const CommittedChunkIdList& ids) {
  size_t total = 0;
  for (uint16_t id : ids) {
    if (id >= LoopEventStoreConfig::POOL_CHUNK_COUNT || !poolReady_ || !poolUsed_[id]) {
      continue;
    }
    total += chunkSlot(id).used;
  }
  return total;
}

void LoopEventStore::loadFromEvents(const MidiEventVec& events) {
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
    if (isChunkSealed(id)) {
      continue;
    }
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
    if (isChunkSealed(id)) {
      continue;
    }
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

  if (hasSealedChunks()) {
    bool needsRebuild = false;
    for (uint16_t id : chunkIds_) {
      if (!isChunkSealed(id)) {
        continue;
      }
      const EventChunk& c = chunk(id);
      bool hasKept = false;
      bool hasDropped = false;
      for (uint16_t i = 0; i < c.used; ++i) {
        if (c.events[i].tick >= tickLimit) {
          hasDropped = true;
        } else {
          hasKept = true;
        }
      }
      if (hasKept && hasDropped) {
        needsRebuild = true;
        break;
      }
    }
    if (needsRebuild) {
      MidiEventVec flat;
      copyEventsTo(flat);
      MidiEventVec kept;
      kept.reserve(flat.size());
      for (const MidiEvent& evt : flat) {
        if (evt.tick < tickLimit) {
          kept.push_back(evt);
        }
      }
      clear();
      loadFromEvents(kept);
      return;
    }
  }

  CaptureChunkIdList kept;
  kept.reserve(chunkIds_.size());
  for (uint16_t id : chunkIds_) {
    if (isChunkSealed(id)) {
      bool allDropped = true;
      const EventChunk& sealed = chunk(id);
      for (uint16_t i = 0; i < sealed.used; ++i) {
        if (sealed.events[i].tick < tickLimit) {
          allDropped = false;
          break;
        }
      }
      if (allDropped) {
        freeChunk(id);
        continue;
      }
      kept.push_back(id);
      continue;
    }

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

LOOP_EVENT_STORE_COLD_MEM void LoopEventStore::mapNoteOnsAtOrBeyondTickIntoLoop(
    uint32_t loopLength) {
  if (empty() || loopLength == 0) {
    return;
  }

  if (hasSealedChunks()) {
    bool needsRebuild = false;
    for (uint16_t id : chunkIds_) {
      const EventChunk& c = chunk(id);
      for (uint16_t i = 0; i < c.used; ++i) {
        if (c.events[i].isNoteOn() && c.events[i].tick >= loopLength) {
          needsRebuild = true;
          break;
        }
      }
      if (needsRebuild) {
        break;
      }
    }
    if (needsRebuild) {
      MidiEventVec flat;
      copyEventsTo(flat);
      for (MidiEvent& evt : flat) {
        projectRecordStopTick(evt, loopLength);
      }
      clear();
      loadFromEvents(flat);
      dropEventsAtOrBeyondTick(loopLength);
      return;
    }
  }

  for (uint16_t id : chunkIds_) {
    if (isChunkSealed(id)) {
      continue;
    }
    EventChunk& c = chunk(id);
    for (uint16_t i = 0; i < c.used; ++i) {
      projectRecordStopTick(c.events[i], loopLength);
    }
    if (c.used > 0) {
      c.firstTick = c.events[0].tick;
      c.lastTick = c.events[c.used - 1].tick;
    }
  }
  markBarIndexDirty();
  dropEventsAtOrBeyondTick(loopLength);
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
      // Defensive bound: adopted content (SD load, snapshot restore, deep clone) bypasses `append`, so
      // an out-of-range tick could still reach here. Skipping it keeps the index finite; the event stays
      // in its chunk and is reported by the append path's refusal at the producer.
      if (bar > LoopEventStoreConfig::MAX_BAR_INDEX) {
        ++globalIndex;
        continue;
      }
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

LOOP_EVENT_STORE_COLD_MEM void LoopEventStore::assignMissingNoteIdsToNoteOnsInChunkIds(
    const uint16_t* chunkIds, size_t count, NoteIdSupplier supplier, void* ctx) {
  if (!poolReady_ || chunkIds == nullptr || count == 0 || supplier == nullptr) {
    return;
  }
  for (size_t i = 0; i < count; ++i) {
    const uint16_t id = chunkIds[i];
    if (id >= LoopEventStoreConfig::POOL_CHUNK_COUNT || !poolUsed_[id]) {
      continue;
    }
    EventChunk& ec = chunkSlot(id);
    for (uint16_t j = 0; j < ec.used; ++j) {
      MidiEvent& evt = ec.events[j];
      if (evt.isNoteOn() && evt.noteId == kInvalidNoteId) {
        evt.noteId = supplier(ctx);
      }
    }
  }
}

#if defined(PIO_UNIT_TEST_NATIVE) && !defined(PERSISTENCE_QUEUE_CPP_INCLUDED)
#define PERSISTENCE_QUEUE_CPP_INCLUDED
#include "PersistenceQueue.cpp"
#endif

bool LOOP_EVENT_STORE_COLD_MEM LoopEventStore::readSealedChunkEvent(
    uint16_t id, size_t index, MidiEvent& out, bool& atEnd) {
  if (!poolReady_ || id >= LoopEventStoreConfig::POOL_CHUNK_COUNT || !poolUsed_[id] ||
      !isChunkSealed(id) || index > chunkSlot(id).used) return false;
  atEnd = index == chunkSlot(id).used;
  if (!atEnd) out = chunkSlot(id).events[index];
  return true;
}

bool LOOP_EVENT_STORE_COLD_MEM LoopEventStore::releaseLastCommittedChunkRef(
    CommittedChunkIdList& refs) {
  if (refs.empty()) return false;
  const uint16_t id = refs.back();
  refs.pop_back();
  releaseChunkReference(id);
  tryFreeChunk(id);
  return true;
}

// Capture range admission is cold. The action edge only exchanges two already
// allocated ownership directories and seals the old tail (all earlier chunks
// were sealed on append). No tail contents or chunk list are copied.
bool LoopEventStore::reserveCaptureTail() {
  if (eventCount_ || (!chunkIds_.empty() &&
      (!capturePoolLifetime_ || capturePoolLifetime_ != poolLifetime_))) return false;
  if (!chunkIds_.empty()) {
    return chunkIds_.size() == 1 && chunk(chunkIds_.front()).used == 0 &&
        chunkIds_.capacity() >= LoopEventStoreConfig::POOL_CHUNK_COUNT;
  }
  initPool();
  if (!poolReady_) return false;
  if (!PersistenceQueue::prewarm()) return false;
  chunkIds_.reserve(LoopEventStoreConfig::POOL_CHUNK_COUNT);
  const uint16_t id = allocChunk();
  if (id == UINT16_MAX) return false;
  retainChunkReference(id);
  chunkIds_.push_back(id);
  capturePoolLifetime_ = poolLifetime_;
  barIndexDirty_ = true;
  return true;
}

bool LOOP_EVENT_STORE_COLD_MEM LoopEventStore::canSealCaptureRange(
    const CaptureRange& destination) const {
  const auto& next = destination.storage_;
  // A producer that allocated lazily has no capture lifetime yet; it is still a
  // current-pool store and may be cut. Only a *stale* non-zero lifetime denies.
  if (&next == this || destination.sealed_ || destination.releasing_ ||
      (capturePoolLifetime_ && capturePoolLifetime_ != poolLifetime_) ||
      next.capturePoolLifetime_ != poolLifetime_ ||
      chunkIds_.empty() || next.chunkIds_.size() != 1 || next.eventCount_ ||
      next.chunkIds_.capacity() < LoopEventStoreConfig::POOL_CHUNK_COUNT) return false;
  const uint16_t spare = next.chunkIds_.front();
  if (!poolReady_ || !poolUsed_[spare] || poolChunkRefCount_[spare] != 1 ||
      poolLifecycle_[spare] != ChunkLifecycleState::Recording || chunkSlot(spare).used) return false;
  const uint16_t tail = chunkIds_.back();
  return poolUsed_[tail] && poolChunkRefCount_[tail] == 1;
}

bool LOOP_EVENT_STORE_COLD_MEM LoopEventStore::resetCaptureExtentPreservingReservedStorage() {
  // After a range cut the producer already owns exactly the pre-reserved empty
  // directory the successor interval must append into. Releasing it here would
  // force a cold re-reservation from the begin-capture path.
  if (eventCount_ != 0 || chunkIds_.size() != 1) return false;
  const uint16_t id = chunkIds_.front();
  if (capturePoolLifetime_ != poolLifetime_ || !poolReady_ || !poolUsed_[id] ||
      poolChunkRefCount_[id] != 1 || poolLifecycle_[id] != ChunkLifecycleState::Recording ||
      chunkSlot(id).used != 0) {
    return false;
  }
  barIndexDirty_ = true;
  lastAppendedTick_ = 0;
  return true;
}

bool LoopEventStore::sealCaptureRange(CaptureRange& destination) {
  if (!canSealCaptureRange(destination)) return false;
  auto& next = destination.storage_;
  const uint16_t tail = chunkIds_.back();
  destination.tailEnd_ = chunkSlot(tail).used;
  destination.eventCount_ = eventCount_;
  sealChunk(tail);
  ++ownershipGeneration_;
  ++next.ownershipGeneration_;
  chunkIds_.swap(next.chunkIds_);
  std::swap(eventCount_, next.eventCount_);
  std::swap(lastAppendedTick_, next.lastAppendedTick_);
  barIndexDirty_ = next.barIndexDirty_ = true;
  // The producer now owns the destination's reserved empty directory, so it is
  // again a capture-pool store. The destination keeps its own lifetime (set by
  // reserveTail), which is what lets its releaseGrain free the transferred chunks.
  capturePoolLifetime_ = poolLifetime_;
  ++destination.generation_;
  destination.sealed_ = true;
  return true;
}

LoopEventStore::CaptureRange::~CaptureRange() {
  // Object destruction is not an interval-path operation.
  releaseGrain(LoopEventStoreConfig::POOL_CHUNK_COUNT);
}

bool LoopEventStore::CaptureRange::reserveTail() {
  return !sealed_ && !releasing_ && storage_.reserveCaptureTail();
}

bool LoopEventStore::CaptureRange::ready() const {
  return sealed_ && !releasing_ && poolReady_ && storage_.capturePoolLifetime_ == poolLifetime_;
}

bool LoopEventStore::CaptureRange::read(uint64_t generation, size_t chunkIndex,
    uint16_t eventIndex, MidiEvent& out, bool& atEnd) const {
  if (!ready() || generation != generation_ || chunkIndex >= storage_.chunkIds_.size()) return false;
  const uint16_t id = storage_.chunkIds_[chunkIndex];
  const uint16_t end = chunkIndex + 1 == storage_.chunkIds_.size()
      ? tailEnd_ : LoopEventStoreConfig::CHUNK_CAPACITY;
  if (!poolUsed_[id] || !poolChunkRefCount_[id] || !isChunkSealed(id) || chunkSlot(id).used != end) return false;
  atEnd = eventIndex >= end;
  if (!atEnd) out = chunkSlot(id).events[eventIndex];
  return true;
}

bool LoopEventStore::CaptureRange::releaseGrain(size_t quantum) {
  if (!quantum) return storage_.chunkIds_.empty();
  if (!releasing_) {
    ++generation_;
    releasing_ = true;
    sealed_ = false;
  }
  while (quantum-- && !storage_.chunkIds_.empty()) {
    const uint16_t id = storage_.chunkIds_.back();
    storage_.chunkIds_.pop_back();
    if (storage_.capturePoolLifetime_ == poolLifetime_) storage_.freeChunk(id);
  }
  if (!storage_.chunkIds_.empty()) return false;
  storage_.eventCount_ = eventCount_ = 0;
  storage_.lastAppendedTick_ = 0;
  storage_.capturePoolLifetime_ = 0;
  storage_.barIndexDirty_ = true;
  tailEnd_ = 0;
  releasing_ = false;
  return true;
}


bool LOOP_EVENT_STORE_COLD_MEM LoopEventStore::prewarmCaptureDirectory() {
  if (capturePoolLifetime_ && capturePoolLifetime_ != poolLifetime_ && !chunkIds_.empty()) return false;
  initPool();
  if (!poolReady_ || !PersistenceQueue::prewarm()) return false;
  chunkIds_.reserve(LoopEventStoreConfig::POOL_CHUNK_COUNT);
  capturePoolLifetime_ = poolLifetime_;
  barIndexDirty_ = true;
  return true;
}

bool LOOP_EVENT_STORE_COLD_MEM LoopEventStore::reservedAppendReady() const {
  return poolReady_ && capturePoolLifetime_ == poolLifetime_ &&
      chunkIds_.capacity() >= LoopEventStoreConfig::POOL_CHUNK_COUNT;
}

bool LOOP_EVENT_STORE_COLD_MEM LoopEventStore::appendReserved(
    const MidiEvent& event, ChunkReservation& reservation) {
  if (!reservedAppendReady() || reservation.lifetime_ != poolLifetime_) return false;
  // A prior read may have rebuilt the optional bar index. Invalidate it rather
  // than growing it inside a reserved append.
  barIndexDirty_ = true;
  return appendToTailChunk(event, nullptr, &reservation);
}

LoopEventStore::ChunkReservation::~ChunkReservation() {
  releaseGrain(LoopEventStoreConfig::POOL_CHUNK_COUNT);
}

size_t LOOP_EVENT_STORE_COLD_MEM LoopEventStore::ChunkReservation::availableChunks() const {
  return lifetime_ == poolLifetime_ ? count_ : 0;
}

bool LOOP_EVENT_STORE_COLD_MEM LoopEventStore::ChunkReservation::reserveChunks(size_t chunks) {
  if (!poolReady_ || chunks > chunks_.size() ||
      (lifetime_ && lifetime_ != poolLifetime_)) return false;
  lifetime_ = poolLifetime_;
  const size_t previous = count_;
  LoopEventStore allocator;
  while (count_ < chunks) {
    const uint16_t id = allocator.allocChunk();
    if (id == UINT16_MAX) {
      while (count_ > previous) {
        const uint16_t rollback = chunks_[--count_];
        releaseChunkReference(rollback);
        tryFreeChunk(rollback);
      }
      return false;
    }
    retainChunkReference(id);
    chunks_[count_++] = id;
  }
  return true;
}

uint16_t LOOP_EVENT_STORE_COLD_MEM LoopEventStore::ChunkReservation::take() {
  if (!poolReady_ || lifetime_ != poolLifetime_ || !count_) return UINT16_MAX;
  const uint16_t id = chunks_[count_ - 1];
  if (!poolUsed_[id] || poolChunkRefCount_[id] != 1 || chunkSlot(id).used ||
      poolLifecycle_[id] != ChunkLifecycleState::Recording) return UINT16_MAX;
  --count_; // Transfer the existing reference to the output store.
  return id;
}

bool LOOP_EVENT_STORE_COLD_MEM LoopEventStore::ChunkReservation::releaseGrain(size_t quantum) {
  while (quantum-- && count_) {
    const uint16_t id = chunks_[--count_];
    if (lifetime_ == poolLifetime_) {
      releaseChunkReference(id);
      tryFreeChunk(id);
    }
  }
  if (count_) return false;
  lifetime_ = 0;
  return true;
}


bool LOOP_EVENT_STORE_COLD_MEM LoopEventStore::releaseCaptureChunkGrain() {
  ++ownershipGeneration_;
  if (!chunkIds_.empty()) {
    const uint16_t id = chunkIds_.back();
    chunkIds_.pop_back();
    if (!capturePoolLifetime_ || capturePoolLifetime_ == poolLifetime_) {
      eventCount_ -= chunk(id).used;
      freeChunk(id);
    }
  }
  barIndexDirty_ = true;
  if (!chunkIds_.empty()) return false;
  eventCount_ = 0;
  lastAppendedTick_ = 0;
  capturePoolLifetime_ = 0;
  return true;
}
