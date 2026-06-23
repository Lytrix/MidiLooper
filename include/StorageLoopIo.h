//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>

#include "LoopPasses.h"
#include "EditPass.h"

struct StorageIo {
  std::function<bool(const void*, size_t)> write;
  std::function<bool(void*, size_t)> read;
};

/// v5 on-wire loop block (capture passes + editPasses tail).
struct PersistedLoopSnapshot {
  LoopId loopId = kInvalidLoopId;
  uint32_t startLoopTick = 0;
  uint32_t loopLengthTicks = 0;
  uint32_t loopStartTick = 0;
  PassId nextPassId = 1;
  uint32_t nextMergeSequence = 0;
  PassId lastPublishedPassId = kInvalidPassId;
  LoopPasses passes;
};

using LoopSnapshotRef = std::shared_ptr<PersistedLoopSnapshot>;

/// Legacy v4 take wire entry (recordPass / overdubPass on disk).
struct PersistedCapturePassWire {
  PassId id = kInvalidPassId;
  uint32_t mergeSequence = 0;
  uint8_t stateRaw = 0;
  uint8_t typeRaw = 0;
  uint32_t sealedAtTick = 0;
};

bool writePersistedCapturePassWire(const StorageIo& io, const PersistedCapturePassWire& wire,
                                   const ChunkIdList& chunkRefs);
bool readPersistedCapturePassWire(const StorageIo& io, PersistedCapturePassWire& wire,
                                  ChunkIdList& chunkRefs, uint32_t loopLengthTicks);
bool writePersistedEditsTail(const StorageIo& io, PassId nextPassId,
                             const EditPassVec& editPasses);

#if defined(PIO_UNIT_TEST_NATIVE)
size_t getLastPersistedCapturePassWriteMaxBatchEvents();
void resetPersistedCapturePassWriteStatsForTest();
#endif

bool writePersistedLoopSnapshot(const StorageIo& io, const PersistedLoopSnapshot& snapshot);
bool readPersistedLoopSnapshot(const StorageIo& io, PersistedLoopSnapshot& snapshot);

struct Loop;
void applySnapshotToLoop(Loop& loop, const PersistedLoopSnapshot& snapshot);

#if !defined(PIO_UNIT_TEST_NATIVE)
bool writeLoopPersisted(const StorageIo& io, const Loop& loop);
bool readLoopPersisted(const StorageIo& io, Loop& loop);
#endif
