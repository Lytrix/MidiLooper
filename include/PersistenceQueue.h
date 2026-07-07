//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#pragma once

#include <cstdint>

#include "LoopEventStore.h"

/// Persistence progress for a sealed capture chunk (DEC-020 Phase 2).
/// Independent of runtime ChunkLifecycleState.
enum class ChunkPersistenceState : uint8_t {
  NotScheduled = 0,
  Queued,
  Writing,
  Persisted,
};

namespace PersistenceQueue {

/// Sealed chunks waiting for or undergoing SD write, in seal order.
uint16_t queueDepth();

/// Chunks currently in the Writing persistence state.
uint16_t writingChunkCount();

ChunkPersistenceState chunkState(uint16_t chunkId);

/// Admit a newly sealed chunk exactly once. Returns true when newly queued.
bool admitSealedChunk(uint16_t chunkId);

/// Next queued chunk in seal order for the writer (Phase 3+). Returns false when empty.
bool beginWriteQueuedChunk(uint16_t& chunkIdOut);

/// Mark the chunk persisted and remove it from the active queue head.
void markChunkPersisted(uint16_t chunkId);

/// Reset persistence tracking when the pool chunk is reclaimed.
void onChunkFreed(uint16_t chunkId);

/// Test and pool-reset hook.
void resetForTests();

#if defined(PIO_UNIT_TEST_NATIVE)
uint32_t sealSequenceForChunk(uint16_t chunkId);
size_t queuedChunkIds(uint16_t* outIds, size_t maxCount);
#endif

}  // namespace PersistenceQueue
