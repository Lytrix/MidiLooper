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
#include "LoopSnapshot.h"
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

// --- LoopSnapshot body format version 1 (LSV1) — DEC-057 ---

struct Loop;

constexpr size_t kLsv1LoopSnapshotMidiEventBytes = sizeof(MidiEvent);

enum class LoopSnapshotBodyFormatProbeResult : uint8_t {
  Legacy = 0,
  SupportedV1 = 1,
  UnsupportedVersion = 2,
  Truncated = 3,
};

/// First-four-byte probe only. Absence of `LSV1` ⇒ Legacy.
LoopSnapshotBodyFormatProbeResult probeLoopSnapshotBodyFormat(const uint8_t* data, size_t size);

struct LoopSnapshotBodyFormatV1ReadResult {
  bool ok = false;
  bool legacy = false;
  bool unsupportedVersion = false;
  PersistedLoopSnapshot metadata{};
  PersistedLoopSnapshot persistedOracle{};
  bool hasPersistedOracle = false;
  ImmutableLoopSnapshotRef loopSnapshot;
};

/// Parse + validate LSV1 bytes. Never publishes; returns candidate snapshot or failure.
bool readLoopSnapshotBodyFormatV1(const uint8_t* data, size_t size,
                                  LoopSnapshotBodyFormatV1ReadResult& out);

size_t measureLoopSnapshotBodyFormatV1Bytes(const PersistedLoopSnapshot& metadata,
                                              const PersistedLoopSnapshot& persistedOracle,
                                              const LoopSnapshot& loopSnapshot);

bool writeLoopSnapshotBodyFormatV1(const StorageIo& io,
                                     const PersistedLoopSnapshot& metadata,
                                     const PersistedLoopSnapshot& persistedOracle,
                                     const LoopSnapshot& loopSnapshot);

/// True when save should emit LSV1 dual-write (current published root available).
bool loopUsesLoopSnapshotBodyFormatV1ForSave(const Loop& loop);

/// LSV1 save preamble: body header + `LoopMetadata` + `PersistedLoopSnapshot` section frame.
bool writeLoopSnapshotBodyFormatV1SavePreamble(const StorageIo& io,
                                                 const PersistedLoopSnapshot& metadata,
                                                 uint32_t persistedOracleSectionBytes);

/// LSV1 save tail: `LoopSnapshot` section only (after streamed oracle payload).
bool writeLoopSnapshotBodyFormatV1SaveTail(const StorageIo& io,
                                           const LoopSnapshot& loopSnapshot);

/// RC-P1: bounded grains for deferred LSV1 snapshot tail save (ExecutionBounds row 14).
constexpr uint32_t kLsv1SaveIoChunkBytes = 512u;
constexpr uint32_t kLsv1SaveOrderingFlushBatch = 16u;

enum class LoopSnapshotBodyFormatV1SaveStepResult : uint8_t {
  MoreWork = 0,
  Completed = 1,
  Failed = 2,
};

struct LoopSnapshotBodyFormatV1SaveScratch;

/// PSRAM-backed scratch reused across deferred `LoopSnapshotTail` slices (opaque payload).
LoopSnapshotBodyFormatV1SaveScratch* acquireLoopSnapshotBodyFormatV1SaveScratch();

void resetLoopSnapshotBodyFormatV1SaveScratch(LoopSnapshotBodyFormatV1SaveScratch& scratch);

void beginLoopSnapshotBodyFormatV1SaveGather(const LoopSnapshot& loopSnapshot,
                                               LoopSnapshotBodyFormatV1SaveScratch& scratch);

/// One classified grain per call when \p maxGrains is 1. \p deadlineUs is a safety stop only.
LoopSnapshotBodyFormatV1SaveStepResult stepLoopSnapshotBodyFormatV1SaveTail(
    const StorageIo& io, const LoopSnapshot& loopSnapshot,
    LoopSnapshotBodyFormatV1SaveScratch& scratch, uint32_t deadlineUs, uint32_t maxGrains);

enum class LoopSnapshotBodyFormatV1LoadStepResult : uint8_t {
  MoreWork = 0,
  Completed = 1,
  Failed = 2,
};

struct LoopSnapshotBodyFormatV1LoadParseState {
  size_t pos = 0;
  bool headerValidated = false;
  bool expectPersistedOracle = false;
  bool sawLoopMetadata = false;
  bool sawPersistedLoopSnapshot = false;
  bool sawLoopSnapshot = false;
  enum class Phase : uint8_t {
    ScanSections = 0,
    PersistedOracle = 1,
    LoopSnapshotPayload = 2,
  };
  Phase phase = Phase::ScanSections;
  size_t activePayloadStart = 0;
  size_t activePayloadEnd = 0;
  PersistedLoopParseState persistedParseState{};
  PersistedLoopSnapshot metadata{};
  ImmutableLoopSnapshotRef loopSnapshot;
};

/// Resumable LSV1 slot-body parse for LoadLoopJob. Never publishes.
LoopSnapshotBodyFormatV1LoadStepResult stepLoopSnapshotBodyFormatV1LoadParse(
    const uint8_t* data, size_t size, PersistedLoopSnapshot& persistedOracleOut,
    LoopSnapshotBodyFormatV1LoadParseState& state, uint32_t deadlineUs = 0,
    uint32_t maxGrains = 0);

class Track;
/// LoadLoopJob publish: legacy rebuilds GUS from passes; LSV1 stores published snapshot only.
void applyLoadedLoopSlotBodyCommit(Loop& loop, Track& track, uint8_t slotIndex,
                                   bool usesLoopSnapshotBodyFormatV1,
                                   PersistedLoopSnapshot& persistedOracle,
                                   ImmutableLoopSnapshotRef loadedLoopSnapshot);

#if defined(PIO_UNIT_TEST_NATIVE)
uint32_t testRebuildSlotFromLoopContentCallCount();
void testResetRebuildSlotFromLoopContentCallCount();
void testRecordRebuildSlotFromLoopContentCall();
size_t testMeasureLoopSnapshotGatheredPayloadBytes(const LoopSnapshot& loopSnapshot);
bool testWriteLoopSnapshotGatheredPayloadToIo(const StorageIo& io,
                                              const LoopSnapshot& loopSnapshot);
bool testWriteLoopSnapshotGatheredPayloadByteSlice(const StorageIo& io,
                                                   const LoopSnapshot& loopSnapshot,
                                                   size_t& byteOffset, size_t maxBytes);
#endif

void applySnapshotToLoop(Loop& loop, PersistedLoopSnapshot& snapshot);
/// Apply persisted geometry/ids only; leave passes unloaded until full SD restore.
void applyLoopSlotMetadataToLoop(Loop& loop, const PersistedLoopSnapshot& metadata);

size_t measureLoopSlotFileBytes(const Loop& loop);
bool writeLoopPersisted(const StorageIo& io, const Loop& loop);
bool readLoopPersisted(const StorageIo& io, Loop& loop);
