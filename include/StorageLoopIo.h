//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>

#include "LoopPasses.h"
#include "EditPass.h"
#include "MidiEvent.h"

struct StorageIo {
  std::function<bool(const void*, size_t)> write;
  std::function<bool(void*, size_t)> read;
};

/// v6 loop slot file body in RAM (capture passes + editPasses tail).
struct PersistedLoopSnapshot {
  LoopId loopId = kInvalidLoopId;
  uint32_t startLoopTick = 0;
  uint32_t loopLengthTicks = 0;
  uint32_t loopStartTick = 0;
  PassId nextPassId = 1;
  NoteId nextNoteId = 1;
  uint32_t nextMergeSequence = 0;
  PassId lastPublishedPassId = kInvalidPassId;
  LoopPasses passes;
};

using LoopSnapshotRef = std::shared_ptr<PersistedLoopSnapshot>;

/// Capture/overdub pass header in loop slot file (v4 take-shaped) (recordPass / overdubPass on disk).
struct CapturePassSlotFileHeader {
  PassId id = kInvalidPassId;
  uint32_t mergeSequence = 0;
  uint8_t stateRaw = 0;
  uint8_t typeRaw = 0;
  uint32_t sealedAtTick = 0;
};

bool writeCapturePassSlotFileHeader(const StorageIo& io, const CapturePassSlotFileHeader& passHeader,
                                   const ChunkIdList& chunkRefs);
bool readCapturePassSlotFileHeader(const StorageIo& io, CapturePassSlotFileHeader& passHeader,
                                  ChunkIdList& chunkRefs, uint32_t loopLengthTicks);
bool writePersistedEditsTail(const StorageIo& io, PassId nextPassId,
                             const EditPassVec& editPasses);

#if defined(PIO_UNIT_TEST_NATIVE)
size_t getLastPersistedCapturePassWriteMaxBatchEvents();
void resetPersistedCapturePassWriteStatsForTest();
#endif

bool writePersistedLoopSnapshot(const StorageIo& io, const PersistedLoopSnapshot& snapshot);
/// When \p legacyDeferredHeaderWithoutNoteId is true, reads the pre-v6 deferred-save header
/// (no nextNoteId field between nextPassId and nextMergeSequence).
bool readPersistedLoopSnapshot(const StorageIo& io, PersistedLoopSnapshot& snapshot,
                               bool legacyDeferredHeaderWithoutNoteId = false);

/// Byte length of loop slot file body produced by writePersistedLoopSnapshot / writeLoopPersisted.
size_t measureLoopSnapshotSlotFileBytes(const PersistedLoopSnapshot& snapshot);

struct Loop;
void applySnapshotToLoop(Loop& loop, const PersistedLoopSnapshot& snapshot);

#if !defined(PIO_UNIT_TEST_NATIVE)
size_t measureLoopSlotFileBytes(const Loop& loop);
bool writeLoopPersisted(const StorageIo& io, const Loop& loop);
bool readLoopPersisted(const StorageIo& io, Loop& loop);
#endif
