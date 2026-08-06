//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "LoopPool.h"

#include <cstdlib>
#include <new>

#include "Loop.h"
#include "Utils/InternalHeapFirstAllocator.h"

#ifndef PIO_UNIT_TEST_NATIVE
static_assert(LoopPoolConfig::MAX_LOOPS_PER_TRACK == Config::MAX_LOOPS_PER_TRACK,
              "LoopPool size must match Config::MAX_LOOPS_PER_TRACK");
#endif

namespace {

void destroyLoopArray(Loop* loops, size_t count) {
  if (loops == nullptr || count == 0) {
    return;
  }
  for (size_t i = 0; i < count; ++i) {
    loops[i].~Loop();
  }
}

Loop* allocateLoopArray(size_t count) {
  if (count == 0) {
    return nullptr;
  }
  const size_t bytes = count * sizeof(Loop);
  void* mem = extmem_malloc(bytes);
  if (mem == nullptr) {
    mem = std::malloc(bytes);
  }
  if (mem == nullptr) {
    return nullptr;
  }

  Loop* loops = static_cast<Loop*>(mem);
  for (size_t i = 0; i < count; ++i) {
    new (&loops[i]) Loop();
  }
  return loops;
}

void freeLoopArray(Loop* loops, size_t count) {
  if (loops == nullptr) {
    return;
  }
  destroyLoopArray(loops, count);
  if (isInExternalMemoryPool(loops)) {
    extmem_free(loops);
  } else {
    std::free(loops);
  }
}

}  // namespace

LoopPool::LoopPool() = default;

LoopPool::~LoopPool() {
  freeLoopArray(loops_, LoopPoolConfig::MAX_LOOPS_PER_TRACK);
  loops_ = nullptr;
}

void LoopPool::ensureInitialized() {
  if (loops_ != nullptr) {
    return;
  }
  loops_ = allocateLoopArray(LoopPoolConfig::MAX_LOOPS_PER_TRACK);
  if (loops_ == nullptr) {
    return;
  }
  assignStableIds1To1();
}

void LoopPool::assignStableIds1To1() {
  if (loops_ == nullptr) {
    return;
  }
  for (uint8_t i = 0; i < LoopPoolConfig::MAX_LOOPS_PER_TRACK; ++i) {
    loops_[i].loopId = static_cast<LoopId>(i);
  }
}

Loop& LoopPool::at(size_t poolIndex) {
  ensureInitialized();
  const size_t idx = poolIndex < LoopPoolConfig::MAX_LOOPS_PER_TRACK ? poolIndex : 0;
  return loops_[idx];
}

const Loop& LoopPool::at(size_t poolIndex) const {
  const size_t idx = poolIndex < LoopPoolConfig::MAX_LOOPS_PER_TRACK ? poolIndex : 0;
  return loops_[idx];
}

LoopId LoopPool::loopIdAt(size_t poolIndex) const {
  if (loops_ == nullptr || poolIndex >= LoopPoolConfig::MAX_LOOPS_PER_TRACK) {
    return kInvalidLoopId;
  }
  return loops_[poolIndex].loopId;
}

size_t LoopPool::indexForId(LoopId id) const {
  if (loops_ == nullptr || id == kInvalidLoopId) {
    return NO_POOL_INDEX;
  }
  for (uint8_t i = 0; i < LoopPoolConfig::MAX_LOOPS_PER_TRACK; ++i) {
    if (loops_[i].loopId == id) {
      return i;
    }
  }
  return NO_POOL_INDEX;
}

Loop* LoopPool::findById(LoopId id) {
  const size_t idx = indexForId(id);
  if (idx == NO_POOL_INDEX) {
    return nullptr;
  }
  return &loops_[idx];
}

const Loop* LoopPool::findById(LoopId id) const {
  const size_t idx = indexForId(id);
  if (idx == NO_POOL_INDEX) {
    return nullptr;
  }
  return &loops_[idx];
}
