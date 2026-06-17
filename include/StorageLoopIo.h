//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>

#include "Epoch.h"

/// Byte-oriented I/O adapter for SD File (firmware) or in-memory buffers (native tests).
struct StorageIo {
  std::function<bool(const void*, size_t)> write;
  std::function<bool(void*, size_t)> read;
};

/// v4 on-wire loop block (Active + Disabled epochs only; flattened MIDI per epoch).
struct PersistedLoopSnapshot {
  LoopId loopId = kInvalidLoopId;
  uint32_t startLoopTick = 0;
  uint32_t loopLengthTicks = 0;
  uint32_t loopStartTick = 0;
  EpochId nextEpochId = 1;
  uint32_t nextMergeSequence = 0;
  EpochId lastPublishedEpochId = kInvalidEpochId;
  EpochVec epochs;
};

bool writePersistedEpoch(const StorageIo& io, const Epoch& epoch);
bool readPersistedEpoch(const StorageIo& io, Epoch& epoch);

bool writePersistedLoopSnapshot(const StorageIo& io, const PersistedLoopSnapshot& snapshot);
bool readPersistedLoopSnapshot(const StorageIo& io, PersistedLoopSnapshot& snapshot);

struct Loop;
bool writeLoopPersisted(const StorageIo& io, const Loop& loop);
bool readLoopPersisted(const StorageIo& io, Loop& loop);
