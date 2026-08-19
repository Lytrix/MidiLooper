//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <new>

#include "LoopPasses.h"
#include "EditPass.h"
#include "MidiEvent.h"
#include "LoopEventStore.h"
#include "Utils/InternalHeapFirstAllocator.h"

#include <cstdlib>
#include <memory>

struct StorageIo {
  std::function<bool(const void*, size_t)> write;
  std::function<bool(void*, size_t)> read;
  /// Optional non-consuming read. Used to probe the additive GEO1 tail without
  /// eating the next bundle field on cards that predate LoopGeometry.
  std::function<bool(void*, size_t)> peek;
};

/// v6 loop slot file body in RAM (capture passes + editPasses + loopGeometries + OSI1 tails).
struct PersistedLoopSnapshot {
  LoopId loopId = kInvalidLoopId;
  uint32_t startLoopTick = 0;
  uint32_t loopLengthTicks = 0;
  uint32_t loopStartTick = 0;
  PassId nextPassId = 1;
  NoteId nextNoteId = 1;
  uint32_t nextMergeSequence = 0;
  PassId lastCommittedPassId = kInvalidPassId;
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
                                   const CommittedChunkIdList& committedChunkIds);
bool readCapturePassSlotFileHeader(const StorageIo& io, CapturePassSlotFileHeader& passHeader,
                                  CommittedChunkIdList& committedChunkIds, uint32_t loopLengthTicks);
bool writePersistedEditsTail(const StorageIo& io, PassId nextPassId,
                             const EditPassVec& editPasses,
                             const LoopGeometryVec& loopGeometries,
                             const CommittedOverdubPassVec& overdubPasses);

#if defined(PIO_UNIT_TEST_NATIVE)
size_t getLastPersistedCapturePassWriteMaxBatchEvents();
size_t getLastPersistedCapturePassReadMaxBatchEvents();
void resetPersistedCapturePassWriteStatsForTest();
#endif

bool writePersistedLoopSnapshot(const StorageIo& io, const PersistedLoopSnapshot& snapshot);
/// When \p legacyDeferredHeaderWithoutNoteId is true, reads the pre-v6 deferred-save header
/// (no nextNoteId field between nextPassId and nextMergeSequence).
bool readPersistedLoopSnapshot(const StorageIo& io, PersistedLoopSnapshot& snapshot,
                               bool legacyDeferredHeaderWithoutNoteId = false);

/// Resumable parse of a buffered snapshot payload (Phase A.6). Grains:
/// snapshot header, capture-pass header, one event batch (CHUNK_CAPACITY),
/// edits-tail header, one edit pass, geometry-tail header, or one LoopGeometry.
/// Mid-pass batches respect \p deadlineUs
/// so a large capture pass cannot monopolize the main loop (session_20260718_223130).
/// \p deadlineUs == 0 means unlimited; otherwise stop when micros() >= deadlineUs.
enum class PersistedLoopParseStepResult : uint8_t {
  MoreWork = 0,
  Completed = 1,
  Failed = 2,
};

struct PersistedLoopParseState {
  size_t pos = 0;
  bool headerDone = false;
  bool legacyWithoutNoteId = false;
  bool triedLegacyFallback = false;
  uint32_t passCount = 0;
  uint32_t passesDone = 0;
  bool passHeaderDone = false;
  bool passReadyToFinalize = false;
  CapturePassSlotFileHeader activePassHeader{};
  uint32_t passMidiRemaining = 0;
  std::unique_ptr<LoopEventStore> passStaging;
  /// Off-stack parse scratch — never stack MidiEvent[CHUNK_CAPACITY] on FLASHMEM frames.
  /// Prefer external memory pool (internal heap is ~7KB under multi-track PLAYING).
  struct MidiEventBatchDeleter {
    void operator()(MidiEvent* ptr) const noexcept {
      if (!ptr) {
        return;
      }
      if (isInExternalMemoryPool(ptr)) {
        extmem_free(ptr);
        return;
      }
      std::free(ptr);
    }
  };
  std::unique_ptr<MidiEvent[], MidiEventBatchDeleter> passEventBatch;
  bool editsHeaderDone = false;
  uint32_t editCount = 0;
  uint32_t editsDone = 0;
  bool geometryHeaderDone = false;
  uint32_t geometryCount = 0;
  uint32_t geometriesDone = 0;
};

PersistedLoopParseStepResult stepPersistedLoopSnapshotParse(
    const uint8_t* data, size_t size, PersistedLoopSnapshot& snapshot,
    PersistedLoopParseState& state, uint32_t deadlineUs = 0,
    uint32_t maxGrains = 0);

/// Release committed chunk refs owned by a staging snapshot (before discard).
void releasePersistedLoopSnapshotChunks(PersistedLoopSnapshot& snapshot);
/// Advance the read cursor past a persisted loop snapshot without heap allocation.
bool skipPersistedLoopSnapshotPayload(const StorageIo& io,
                                      bool legacyDeferredHeaderWithoutNoteId = false);
/// Read loop slot geometry/header only; skip capture pass and edit payloads (boot metadata hydrate).
bool readPersistedLoopSnapshotHeader(const StorageIo& io, PersistedLoopSnapshot& snapshot,
                                     bool legacyDeferredHeaderWithoutNoteId = false);

/// Byte length of loop slot file body produced by writePersistedLoopSnapshot / writeLoopPersisted.
size_t measureLoopSnapshotSlotFileBytes(const PersistedLoopSnapshot& snapshot);

struct Loop;
void applySnapshotToLoop(Loop& loop, PersistedLoopSnapshot& snapshot);
/// Apply persisted geometry/ids only; leave passes unloaded until full SD restore.
void applyLoopSlotMetadataToLoop(Loop& loop, const PersistedLoopSnapshot& metadata);

#if !defined(PIO_UNIT_TEST_NATIVE)
size_t measureLoopSlotFileBytes(const Loop& loop);
bool writeLoopPersisted(const StorageIo& io, const Loop& loop);
bool readLoopPersisted(const StorageIo& io, Loop& loop);
#endif
