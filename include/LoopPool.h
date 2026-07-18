//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#pragma once

#include <cstddef>
#include <cstdint>

#include "LoopPasses.h"

#if defined(PIO_UNIT_TEST_NATIVE)
namespace LoopPoolConfig {
constexpr uint8_t MAX_LOOPS_PER_TRACK = 8;
}
#else
#include "Globals.h"
namespace LoopPoolConfig {
constexpr uint8_t MAX_LOOPS_PER_TRACK = Config::MAX_LOOPS_PER_TRACK;
}
#endif

struct Loop;

/// Per-track pool of first-class Loop timelines (distinct from the PSRAM event chunk pool).
/// v1: fixed 1:1 mapping — pool index i owns LoopId i.
/// Teensy: slot shells allocated in external memory pool first (~536 B × MAX_LOOPS_PER_TRACK).
class LoopPool {
 public:
  LoopPool();
  ~LoopPool();

  LoopPool(const LoopPool&) = delete;
  LoopPool& operator=(const LoopPool&) = delete;

  void ensureInitialized();
  bool initialized() const { return loops_ != nullptr; }

  size_t size() const { return LoopPoolConfig::MAX_LOOPS_PER_TRACK; }

  Loop& at(size_t poolIndex);
  const Loop& at(size_t poolIndex) const;

  LoopId loopIdAt(size_t poolIndex) const;

  /// Returns nullptr when id is unknown — callers must fall back to pool index.
  Loop* findById(LoopId id);
  const Loop* findById(LoopId id) const;

  /// Resolve pool index for a stable LoopId, or NO_POOL_INDEX when unknown.
  static constexpr size_t NO_POOL_INDEX = static_cast<size_t>(-1);
  size_t indexForId(LoopId id) const;

  /// Assign stable 1:1 ids (pool index == LoopId) after allocation or load.
  void assignStableIds1To1();

 private:
  Loop* loops_ = nullptr;
};
