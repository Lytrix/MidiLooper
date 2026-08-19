//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "StorageLoopIo.h"

#if defined(ARDUINO)
#include <Arduino.h>
#define STORAGE_LOOP_IO_MEM FLASHMEM
#else
#define STORAGE_LOOP_IO_MEM
#endif

#include "LoopEventStore.h"
#include "Utils/MemoryMonitor.h"
#include "Utils/ExternalMemoryFirstAllocator.h"
#include "Utils/InternalHeapFirstAllocator.h"

#include <cstdlib>
#include <cstring>
#include <memory>
#include <new>
#include <vector>

namespace {

bool ioWrite(const StorageIo& io, const void* data, size_t size) {
  return io.write && io.write(data, size);
}

bool ioRead(const StorageIo& io, void* data, size_t size) {
  return io.read && io.read(data, size);
}

bool ioPeek(const StorageIo& io, void* data, size_t size) {
  return io.peek && io.peek(data, size);
}

#if defined(PIO_UNIT_TEST_NATIVE)
size_t g_lastPersistedCapturePassWriteMaxBatchEvents = 0;
size_t g_lastPersistedCapturePassReadMaxBatchEvents = 0;
#endif

constexpr uint32_t MAX_PERSISTED_CAPTURE_PASS_EVENTS =
    static_cast<uint32_t>(LoopEventStoreConfig::POOL_CHUNK_COUNT) *
    LoopEventStoreConfig::CHUNK_CAPACITY;
constexpr uint32_t PERSISTED_EDITS_TAIL_MARKER = 0x45505433u;  // "EPT3"
constexpr uint32_t PERSISTED_GEOMETRY_TAIL_MARKER = 0x314F4547u;  // "GEO1"
constexpr uint32_t PERSISTED_OVERDUB_SESSION_INDEX_TAIL_MARKER = 0x3149534Fu;  // "OSI1"
constexpr uint32_t MAX_PERSISTED_LOOP_GEOMETRIES = 4096;
constexpr uint32_t MAX_PERSISTED_OVERDUB_SESSION_INDEXES = 4096;

uint32_t maxPersistedEventTick(uint32_t loopLengthTicks) {
  if (loopLengthTicks >= 0x80000000u) {
    return LoopEventStoreConfig::BAR_TICKS;
  }
  if (loopLengthTicks == 0) {
    // Geometry may be zeroed while MIDI remains (e.g. LoopBoundaryChange undo); allow load then reconcile.
    return UINT32_MAX;
  }
  return loopLengthTicks + LoopEventStoreConfig::BAR_TICKS;
}

bool canStagePersistedEvent(size_t stagedEventCount) {
  if ((stagedEventCount % LoopEventStoreConfig::CHUNK_CAPACITY) != 0) {
    return true;
  }
  if (!LoopEventStore::canAllocChunkWithReserve()) {
    return false;
  }
#if defined(ARDUINO)
  // Chunk pool is EXTMEM. RAM1 floor is for mid_pass/capture — not SD LoadLoopJob
  // staging under multi-track PLAYING (~7KB locals free; session_20260718_234625).
  if (LoopEventStore::isSdLoadStaging()) {
    return true;
  }
  return LoopEventStore::hasInternalHeapHeadroomForNonCriticalWork(
      MemoryMonitor::getInternalHeapFreeBytes());
#else
  return true;
#endif
}

bool writePersistedCapturePassPayloadChunkStream(const StorageIo& io,
                                                 const CommittedChunkIdList& committedChunkIds,
                                                 size_t* maxBatchEvents) {
  std::vector<MidiEvent, ExternalMemoryFirstAllocator<MidiEvent>> batch;
  batch.reserve(LoopEventStoreConfig::CHUNK_CAPACITY);
  for (uint16_t chunkId : committedChunkIds) {
    batch.clear();
    LoopEventStore::appendChunkRefEvent(chunkId, batch);
    if (batch.empty()) {
      continue;
    }
    if (maxBatchEvents && batch.size() > *maxBatchEvents) {
      *maxBatchEvents = batch.size();
    }
    if (!ioWrite(io, batch.data(), batch.size() * sizeof(MidiEvent))) {
      return false;
    }
#if defined(ARDUINO)
    yield();
#endif
  }
  return true;
}

}  // namespace

bool writeCapturePassSlotFileHeader(const StorageIo& io, const CapturePassSlotFileHeader& passHeader,
                                   const CommittedChunkIdList& committedChunkIds) {
  if (!ioWrite(io, &passHeader.id, sizeof(passHeader.id))) return false;
  if (!ioWrite(io, &passHeader.mergeSequence, sizeof(passHeader.mergeSequence))) return false;
  if (!ioWrite(io, &passHeader.stateRaw, sizeof(passHeader.stateRaw))) return false;
  if (!ioWrite(io, &passHeader.typeRaw, sizeof(passHeader.typeRaw))) return false;
  if (!ioWrite(io, &passHeader.sealedAtTick, sizeof(passHeader.sealedAtTick))) return false;

  const uint32_t midiCount =
      static_cast<uint32_t>(LoopEventStore::countEventsInChunkIds(committedChunkIds));
  if (!ioWrite(io, &midiCount, sizeof(midiCount))) return false;
  size_t maxBatchEvents = 0;
  if (midiCount > 0 &&
      !writePersistedCapturePassPayloadChunkStream(io, committedChunkIds, &maxBatchEvents)) {
    return false;
  }
#if defined(PIO_UNIT_TEST_NATIVE)
  g_lastPersistedCapturePassWriteMaxBatchEvents = maxBatchEvents;
#endif
  return true;
}

bool STORAGE_LOOP_IO_MEM readCapturePassSlotFileHeader(const StorageIo& io, CapturePassSlotFileHeader& passHeader,
                                  CommittedChunkIdList& committedChunkIds, uint32_t loopLengthTicks) {
  uint32_t midiCount = 0;
  if (!ioRead(io, &passHeader.id, sizeof(passHeader.id))) return false;
  if (!ioRead(io, &passHeader.mergeSequence, sizeof(passHeader.mergeSequence))) return false;
  if (!ioRead(io, &passHeader.stateRaw, sizeof(passHeader.stateRaw))) return false;
  if (!ioRead(io, &passHeader.typeRaw, sizeof(passHeader.typeRaw))) return false;
  if (!ioRead(io, &passHeader.sealedAtTick, sizeof(passHeader.sealedAtTick))) return false;
  if (!ioRead(io, &midiCount, sizeof(midiCount))) return false;

  committedChunkIds.clear();
  if (midiCount == 0) {
    return true;
  }
  if (midiCount > MAX_PERSISTED_CAPTURE_PASS_EVENTS) {
    return false;
  }

  LoopEventStore staging;
  auto failStagingRead = [&staging]() {
    staging.clear();
    return false;
  };
  const uint32_t maxTick = maxPersistedEventTick(loopLengthTicks);
  // Phase 2: batched ioRead (mirrors writePersistedCapturePassPayloadChunkStream).
  std::vector<MidiEvent, ExternalMemoryFirstAllocator<MidiEvent>> batch;
  batch.reserve(LoopEventStoreConfig::CHUNK_CAPACITY);
  size_t maxBatchEvents = 0;
  uint32_t remaining = midiCount;
  while (remaining > 0) {
    const uint32_t batchCount =
        remaining > LoopEventStoreConfig::CHUNK_CAPACITY
            ? LoopEventStoreConfig::CHUNK_CAPACITY
            : remaining;
    batch.resize(batchCount);
    if (!ioRead(io, batch.data(), static_cast<size_t>(batchCount) * sizeof(MidiEvent))) {
      return failStagingRead();
    }
    if (batchCount > maxBatchEvents) {
      maxBatchEvents = batchCount;
    }
    for (uint32_t i = 0; i < batchCount; ++i) {
      const MidiEvent& evt = batch[i];
      if (evt.tick > maxTick) return failStagingRead();
      if (!canStagePersistedEvent(staging.size())) return failStagingRead();
      if (!staging.append(evt)) return failStagingRead();
    }
    remaining -= batchCount;
#if defined(ARDUINO)
    yield();
#endif
  }
#if defined(PIO_UNIT_TEST_NATIVE)
  g_lastPersistedCapturePassReadMaxBatchEvents = maxBatchEvents;
#endif
  if (!staging.detachChunksToCommittedChunkIds(committedChunkIds)) {
    return failStagingRead();
  }
  return true;
}

#if defined(PIO_UNIT_TEST_NATIVE)
size_t getLastPersistedCapturePassWriteMaxBatchEvents() {
  return g_lastPersistedCapturePassWriteMaxBatchEvents;
}

size_t getLastPersistedCapturePassReadMaxBatchEvents() {
  return g_lastPersistedCapturePassReadMaxBatchEvents;
}

void resetPersistedCapturePassWriteStatsForTest() {
  g_lastPersistedCapturePassWriteMaxBatchEvents = 0;
  g_lastPersistedCapturePassReadMaxBatchEvents = 0;
}
#endif

bool isValidPassTypeRaw(uint8_t raw) {
  return raw <= static_cast<uint8_t>(EditPassType::Audio);
}

bool isValidActionTypeRaw(uint8_t raw) {
  return raw <= static_cast<uint8_t>(EditActionType::Delete);
}

bool isValidPropertyTypeRaw(uint8_t raw) {
  return raw <= static_cast<uint8_t>(EditPropertyType::Value);
}

bool isValidEditPassStateRaw(uint8_t raw) {
  return raw <= static_cast<uint8_t>(EditPassState::Disabled);
}

bool STORAGE_LOOP_IO_MEM writePersistedEditPass(const StorageIo& io, const EditPass& editPass) {
  const uint8_t passTypeRaw = static_cast<uint8_t>(editPass.passType);
  const uint8_t stateRaw = static_cast<uint8_t>(editPass.state);
  const uint8_t actionTypeRaw = static_cast<uint8_t>(editPass.actionType);
  const uint8_t propertyTypeRaw = static_cast<uint8_t>(editPass.propertyType);
  if (!ioWrite(io, &editPass.id, sizeof(editPass.id))) return false;
  if (!ioWrite(io, &passTypeRaw, sizeof(passTypeRaw))) return false;
  if (!ioWrite(io, &editPass.editPassIndex, sizeof(editPass.editPassIndex))) return false;
  if (!ioWrite(io, &stateRaw, sizeof(stateRaw))) return false;
  if (!ioWrite(io, &actionTypeRaw, sizeof(actionTypeRaw))) return false;
  if (!ioWrite(io, &propertyTypeRaw, sizeof(propertyTypeRaw))) return false;
  if (!ioWrite(io, &editPass.targetNoteId, sizeof(editPass.targetNoteId))) return false;
  if (!ioWrite(io, &editPass.startTick, sizeof(editPass.startTick))) return false;
  if (!ioWrite(io, &editPass.endTick, sizeof(editPass.endTick))) return false;
  if (!ioWrite(io, &editPass.pitch, sizeof(editPass.pitch))) return false;
  if (!ioWrite(io, &editPass.velocity, sizeof(editPass.velocity))) return false;
  const uint32_t addedCount = static_cast<uint32_t>(editPass.addedEvents.size());
  if (!ioWrite(io, &addedCount, sizeof(addedCount))) return false;
  if (addedCount > 0 &&
      !ioWrite(io, editPass.addedEvents.data(), addedCount * sizeof(MidiEvent))) {
    return false;
  }
  return true;
}

bool STORAGE_LOOP_IO_MEM readPersistedEditPass(const StorageIo& io, EditPass& editPass) {
  uint8_t passTypeRaw = 0;
  uint8_t stateRaw = 0;
  uint8_t actionTypeRaw = 0;
  uint8_t propertyTypeRaw = 0;
  uint32_t addedCount = 0;
  if (!ioRead(io, &editPass.id, sizeof(editPass.id))) return false;
  if (!ioRead(io, &passTypeRaw, sizeof(passTypeRaw))) return false;
  if (!isValidPassTypeRaw(passTypeRaw)) return false;
  if (!ioRead(io, &editPass.editPassIndex, sizeof(editPass.editPassIndex))) return false;
  if (!ioRead(io, &stateRaw, sizeof(stateRaw))) return false;
  if (!isValidEditPassStateRaw(stateRaw)) return false;
  if (!ioRead(io, &actionTypeRaw, sizeof(actionTypeRaw))) return false;
  if (!isValidActionTypeRaw(actionTypeRaw)) return false;
  if (!ioRead(io, &propertyTypeRaw, sizeof(propertyTypeRaw))) return false;
  if (!isValidPropertyTypeRaw(propertyTypeRaw)) return false;
  if (!ioRead(io, &editPass.targetNoteId, sizeof(editPass.targetNoteId))) return false;
  if (!ioRead(io, &editPass.startTick, sizeof(editPass.startTick))) return false;
  if (!ioRead(io, &editPass.endTick, sizeof(editPass.endTick))) return false;
  if (!ioRead(io, &editPass.pitch, sizeof(editPass.pitch))) return false;
  if (!ioRead(io, &editPass.velocity, sizeof(editPass.velocity))) return false;
  if (!ioRead(io, &addedCount, sizeof(addedCount))) return false;
  editPass.passType = static_cast<EditPassType>(passTypeRaw);
  editPass.state = static_cast<EditPassState>(stateRaw);
  editPass.actionType = static_cast<EditActionType>(actionTypeRaw);
  editPass.propertyType = static_cast<EditPropertyType>(propertyTypeRaw);
  editPass.addedEvents.clear();
  if (addedCount == 0) {
    return true;
  }
  // Batch read (CHUNK_CAPACITY), same bound as capture-pass payload.
  editPass.addedEvents.reserve(addedCount);
  std::vector<MidiEvent, ExternalMemoryFirstAllocator<MidiEvent>> batch;
  batch.reserve(LoopEventStoreConfig::CHUNK_CAPACITY);
  uint32_t remaining = addedCount;
  while (remaining > 0) {
    const uint32_t batchCount =
        remaining > LoopEventStoreConfig::CHUNK_CAPACITY
            ? LoopEventStoreConfig::CHUNK_CAPACITY
            : remaining;
    batch.resize(batchCount);
    if (!ioRead(io, batch.data(), static_cast<size_t>(batchCount) * sizeof(MidiEvent))) {
      return false;
    }
    for (uint32_t i = 0; i < batchCount; ++i) {
      editPass.addedEvents.push_back(batch[i]);
    }
    remaining -= batchCount;
  }
  return true;
}

bool STORAGE_LOOP_IO_MEM writePersistedGeometryRecord(const StorageIo& io, const LoopGeometry& geometry) {
  if (!ioWrite(io, &geometry.id, sizeof(geometry.id))) return false;
  if (!ioWrite(io, &geometry.loopStartTick, sizeof(geometry.loopStartTick))) return false;
  if (!ioWrite(io, &geometry.loopLengthTicks, sizeof(geometry.loopLengthTicks))) return false;
  if (!ioWrite(io, &geometry.startLoopTick, sizeof(geometry.startLoopTick))) return false;
  if (!ioWrite(io, &geometry.beforeLoopStartTick, sizeof(geometry.beforeLoopStartTick))) {
    return false;
  }
  if (!ioWrite(io, &geometry.beforeLoopLengthTicks, sizeof(geometry.beforeLoopLengthTicks))) {
    return false;
  }
  if (!ioWrite(io, &geometry.beforeStartLoopTick, sizeof(geometry.beforeStartLoopTick))) {
    return false;
  }
  const uint8_t stateRaw = static_cast<uint8_t>(geometry.state);
  return ioWrite(io, &stateRaw, sizeof(stateRaw));
}

bool STORAGE_LOOP_IO_MEM readPersistedGeometryRecord(const StorageIo& io, LoopGeometry& geometry) {
  uint8_t stateRaw = 0;
  if (!ioRead(io, &geometry.id, sizeof(geometry.id))) return false;
  if (!ioRead(io, &geometry.loopStartTick, sizeof(geometry.loopStartTick))) return false;
  if (!ioRead(io, &geometry.loopLengthTicks, sizeof(geometry.loopLengthTicks))) return false;
  if (!ioRead(io, &geometry.startLoopTick, sizeof(geometry.startLoopTick))) return false;
  if (!ioRead(io, &geometry.beforeLoopStartTick, sizeof(geometry.beforeLoopStartTick))) {
    return false;
  }
  if (!ioRead(io, &geometry.beforeLoopLengthTicks, sizeof(geometry.beforeLoopLengthTicks))) {
    return false;
  }
  if (!ioRead(io, &geometry.beforeStartLoopTick, sizeof(geometry.beforeStartLoopTick))) {
    return false;
  }
  if (!ioRead(io, &stateRaw, sizeof(stateRaw))) return false;
  if (stateRaw > static_cast<uint8_t>(LoopGeometryState::Disabled)) {
    return false;
  }
  geometry.state = static_cast<LoopGeometryState>(stateRaw);
  return true;
}

bool STORAGE_LOOP_IO_MEM writePersistedGeometryTail(const StorageIo& io, const LoopGeometryVec& loopGeometries) {
  const uint32_t marker = PERSISTED_GEOMETRY_TAIL_MARKER;
  if (!ioWrite(io, &marker, sizeof(marker))) return false;
  const uint32_t geometryCount = static_cast<uint32_t>(loopGeometries.size());
  if (!ioWrite(io, &geometryCount, sizeof(geometryCount))) return false;
  for (const LoopGeometry& geometry : loopGeometries) {
    if (!writePersistedGeometryRecord(io, geometry)) return false;
  }
  return true;
}

bool STORAGE_LOOP_IO_MEM probePersistedGeometryTail(const StorageIo& io, bool& present) {
  present = false;
  uint32_t marker = 0;
  if (io.peek) {
    if (!ioPeek(io, &marker, sizeof(marker))) {
      return true;
    }
    if (marker != PERSISTED_GEOMETRY_TAIL_MARKER) {
      return true;
    }
    if (!ioRead(io, &marker, sizeof(marker))) {
      return false;
    }
    present = true;
    return true;
  }
  if (!ioRead(io, &marker, sizeof(marker))) {
    return true;
  }
  if (marker != PERSISTED_GEOMETRY_TAIL_MARKER) {
    return false;
  }
  present = true;
  return true;
}

bool STORAGE_LOOP_IO_MEM readPersistedGeometryTail(const StorageIo& io, LoopGeometryVec& loopGeometries) {
  loopGeometries.clear();
  bool present = false;
  if (!probePersistedGeometryTail(io, present)) {
    return false;
  }
  if (!present) {
    return true;
  }
  uint32_t geometryCount = 0;
  if (!ioRead(io, &geometryCount, sizeof(geometryCount))) {
    return false;
  }
  if (geometryCount > MAX_PERSISTED_LOOP_GEOMETRIES) {
    return false;
  }
  loopGeometries.reserve(geometryCount);
  for (uint32_t i = 0; i < geometryCount; ++i) {
    LoopGeometry geometry{};
    if (!readPersistedGeometryRecord(io, geometry)) {
      return false;
    }
    loopGeometries.push_back(geometry);
  }
  return true;
}

bool STORAGE_LOOP_IO_MEM skipPersistedGeometryTail(const StorageIo& io) {
  bool present = false;
  if (!probePersistedGeometryTail(io, present)) {
    return false;
  }
  if (!present) {
    return true;
  }
  uint32_t geometryCount = 0;
  if (!ioRead(io, &geometryCount, sizeof(geometryCount))) {
    return false;
  }
  if (geometryCount > MAX_PERSISTED_LOOP_GEOMETRIES) {
    return false;
  }
  for (uint32_t i = 0; i < geometryCount; ++i) {
    LoopGeometry geometry{};
    if (!readPersistedGeometryRecord(io, geometry)) {
      return false;
    }
  }
  return true;
}

bool STORAGE_LOOP_IO_MEM writePersistedOverdubSessionIndexTail(
    const StorageIo& io, const CommittedOverdubPassVec& overdubPasses) {
  const uint32_t marker = PERSISTED_OVERDUB_SESSION_INDEX_TAIL_MARKER;
  if (!ioWrite(io, &marker, sizeof(marker))) return false;
  const uint32_t count = static_cast<uint32_t>(overdubPasses.size());
  if (!ioWrite(io, &count, sizeof(count))) return false;
  for (const OverdubPass& pass : overdubPasses) {
    if (!ioWrite(io, &pass.id, sizeof(pass.id))) return false;
    if (!ioWrite(io, &pass.overdubSessionIndex, sizeof(pass.overdubSessionIndex))) {
      return false;
    }
  }
  return true;
}

bool STORAGE_LOOP_IO_MEM probePersistedOverdubSessionIndexTail(const StorageIo& io, bool& present) {
  present = false;
  uint32_t marker = 0;
  if (io.peek) {
    if (!ioPeek(io, &marker, sizeof(marker))) {
      return true;
    }
    if (marker != PERSISTED_OVERDUB_SESSION_INDEX_TAIL_MARKER) {
      return true;
    }
    if (!ioRead(io, &marker, sizeof(marker))) {
      return false;
    }
    present = true;
    return true;
  }
  if (!ioRead(io, &marker, sizeof(marker))) {
    return true;
  }
  if (marker != PERSISTED_OVERDUB_SESSION_INDEX_TAIL_MARKER) {
    return false;
  }
  present = true;
  return true;
}

bool STORAGE_LOOP_IO_MEM applyPersistedOverdubSessionIndexTail(const StorageIo& io,
                                                              CommittedOverdubPassVec& overdubPasses) {
  bool present = false;
  if (!probePersistedOverdubSessionIndexTail(io, present)) {
    return false;
  }
  if (!present) {
    return true;
  }
  uint32_t count = 0;
  if (!ioRead(io, &count, sizeof(count))) {
    return false;
  }
  if (count > MAX_PERSISTED_OVERDUB_SESSION_INDEXES) {
    return false;
  }
  for (uint32_t i = 0; i < count; ++i) {
    PassId passId = kInvalidPassId;
    uint8_t overdubSessionIndex = kUngroupedOverdubSessionIndex;
    if (!ioRead(io, &passId, sizeof(passId))) {
      return false;
    }
    if (!ioRead(io, &overdubSessionIndex, sizeof(overdubSessionIndex))) {
      return false;
    }
    for (OverdubPass& pass : overdubPasses) {
      if (pass.id == passId) {
        pass.overdubSessionIndex = overdubSessionIndex;
        break;
      }
    }
  }
  return true;
}

bool STORAGE_LOOP_IO_MEM skipPersistedOverdubSessionIndexTail(const StorageIo& io) {
  bool present = false;
  if (!probePersistedOverdubSessionIndexTail(io, present)) {
    return false;
  }
  if (!present) {
    return true;
  }
  uint32_t count = 0;
  if (!ioRead(io, &count, sizeof(count))) {
    return false;
  }
  if (count > MAX_PERSISTED_OVERDUB_SESSION_INDEXES) {
    return false;
  }
  for (uint32_t i = 0; i < count; ++i) {
    PassId passId = kInvalidPassId;
    uint8_t overdubSessionIndex = kUngroupedOverdubSessionIndex;
    if (!ioRead(io, &passId, sizeof(passId))) {
      return false;
    }
    if (!ioRead(io, &overdubSessionIndex, sizeof(overdubSessionIndex))) {
      return false;
    }
  }
  return true;
}

bool STORAGE_LOOP_IO_MEM writePersistedEditsTail(const StorageIo& io, PassId nextPassId,
                             const EditPassVec& editPasses,
                             const LoopGeometryVec& loopGeometries,
                             const CommittedOverdubPassVec& overdubPasses) {
  if (!ioWrite(io, &nextPassId, sizeof(nextPassId))) return false;
  const uint32_t marker = PERSISTED_EDITS_TAIL_MARKER;
  if (!ioWrite(io, &marker, sizeof(marker))) return false;
  const uint32_t editCount = static_cast<uint32_t>(editPasses.size());
  if (!ioWrite(io, &editCount, sizeof(editCount))) return false;
  for (const EditPass& editPass : editPasses) {
    if (!writePersistedEditPass(io, editPass)) return false;
  }
  if (!writePersistedGeometryTail(io, loopGeometries)) {
    return false;
  }
  return writePersistedOverdubSessionIndexTail(io, overdubPasses);
}

bool STORAGE_LOOP_IO_MEM readPersistedEditsTail(const StorageIo& io, PersistedLoopSnapshot& snapshot) {
  if (!ioRead(io, &snapshot.nextPassId, sizeof(snapshot.nextPassId))) {
    return false;
  }
  uint32_t marker = 0;
  if (!ioRead(io, &marker, sizeof(marker))) {
    return false;
  }
  if (marker != PERSISTED_EDITS_TAIL_MARKER) {
    return false;
  }
  uint32_t editCount = 0;
  if (!ioRead(io, &editCount, sizeof(editCount))) {
    return false;
  }
  snapshot.passes.editPasses.clear();
  snapshot.passes.editPasses.reserve(editCount);
  for (uint32_t i = 0; i < editCount; ++i) {
    EditPass editPass{};
    if (!readPersistedEditPass(io, editPass)) return false;
    snapshot.passes.editPasses.push_back(std::move(editPass));
  }
  if (snapshot.nextPassId == 0) {
    snapshot.nextPassId = 1;
  }
  if (!readPersistedGeometryTail(io, snapshot.passes.loopGeometries)) {
    return false;
  }
  return applyPersistedOverdubSessionIndexTail(io, snapshot.passes.overdubPasses);
}

bool writePersistedLoopSnapshot(const StorageIo& io, const PersistedLoopSnapshot& snapshot) {
  if (!ioWrite(io, &snapshot.loopId, sizeof(snapshot.loopId))) return false;
  if (!ioWrite(io, &snapshot.startLoopTick, sizeof(snapshot.startLoopTick))) return false;
  if (!ioWrite(io, &snapshot.loopLengthTicks, sizeof(snapshot.loopLengthTicks))) return false;
  if (!ioWrite(io, &snapshot.loopStartTick, sizeof(snapshot.loopStartTick))) return false;
  if (!ioWrite(io, &snapshot.nextPassId, sizeof(snapshot.nextPassId))) return false;
  if (!ioWrite(io, &snapshot.nextNoteId, sizeof(snapshot.nextNoteId))) return false;
  if (!ioWrite(io, &snapshot.nextMergeSequence, sizeof(snapshot.nextMergeSequence))) return false;
  if (!ioWrite(io, &snapshot.lastCommittedPassId, sizeof(snapshot.lastCommittedPassId))) {
    return false;
  }

  uint32_t persistedCount = 0;
  if (snapshot.passes.hasRecordPass()) {
    ++persistedCount;
  }
  persistedCount += static_cast<uint32_t>(snapshot.passes.overdubPasses.size());
  if (!ioWrite(io, &persistedCount, sizeof(persistedCount))) return false;

  if (snapshot.passes.hasRecordPass()) {
    CapturePassSlotFileHeader passHeader{};
    passHeader.id = snapshot.passes.recordPass.id;
    passHeader.mergeSequence = 0;
    passHeader.stateRaw = static_cast<uint8_t>(snapshot.passes.recordPass.state);
    passHeader.typeRaw = 0;
    passHeader.sealedAtTick = snapshot.passes.recordPass.sealedAtTick;
    if (!writeCapturePassSlotFileHeader(io, passHeader, snapshot.passes.recordPass.committedChunkIds)) {
      return false;
    }
  }
  for (const OverdubPass& pass : snapshot.passes.overdubPasses) {
    CapturePassSlotFileHeader passHeader{};
    passHeader.id = pass.id;
    passHeader.mergeSequence = pass.mergeSequence;
    passHeader.stateRaw = static_cast<uint8_t>(pass.state);
    passHeader.typeRaw = 1;
    passHeader.sealedAtTick = pass.sealedAtTick;
    if (!writeCapturePassSlotFileHeader(io, passHeader, pass.committedChunkIds)) {
      return false;
    }
  }
  return writePersistedEditsTail(io, snapshot.nextPassId, snapshot.passes.editPasses,
                                snapshot.passes.loopGeometries, snapshot.passes.overdubPasses);
}

size_t measureLoopSnapshotSlotFileBytes(const PersistedLoopSnapshot& snapshot) {
  size_t nbytes = 0;
  StorageIo io{
      [&nbytes](const void* data, size_t size) -> bool {
        nbytes += size;
        return true;
      },
      nullptr};
  if (!writePersistedLoopSnapshot(io, snapshot)) {
    return 0;
  }
  return nbytes;
}

bool readPersistedLoopSnapshot(const StorageIo& io, PersistedLoopSnapshot& snapshot,
                               bool legacyDeferredHeaderWithoutNoteId) {
  snapshot = PersistedLoopSnapshot{};
  if (!ioRead(io, &snapshot.loopId, sizeof(snapshot.loopId))) return false;
  if (!ioRead(io, &snapshot.startLoopTick, sizeof(snapshot.startLoopTick))) return false;
  if (!ioRead(io, &snapshot.loopLengthTicks, sizeof(snapshot.loopLengthTicks))) return false;
  if (!ioRead(io, &snapshot.loopStartTick, sizeof(snapshot.loopStartTick))) return false;
  if (!ioRead(io, &snapshot.nextPassId, sizeof(snapshot.nextPassId))) return false;
  if (legacyDeferredHeaderWithoutNoteId) {
    snapshot.nextNoteId = 1;
  } else if (!ioRead(io, &snapshot.nextNoteId, sizeof(snapshot.nextNoteId))) {
    return false;
  }
  if (!ioRead(io, &snapshot.nextMergeSequence, sizeof(snapshot.nextMergeSequence))) return false;
  if (!ioRead(io, &snapshot.lastCommittedPassId, sizeof(snapshot.lastCommittedPassId))) {
    return false;
  }

  if (snapshot.loopLengthTicks >= 0x80000000u) {
    snapshot.loopLengthTicks = 0;
  }

  uint32_t passCount = 0;
  if (!ioRead(io, &passCount, sizeof(passCount))) return false;

  snapshot.passes = LoopPasses{};
  for (uint32_t i = 0; i < passCount; ++i) {
    CapturePassSlotFileHeader passHeader{};
    CommittedChunkIdList committedChunkIds;
    if (!readCapturePassSlotFileHeader(io, passHeader, committedChunkIds, snapshot.loopLengthTicks)) {
      return false;
    }
    if (passHeader.typeRaw == 0) {
      RecordPass record{};
      record.id = passHeader.id;
      record.state = static_cast<CapturePassState>(passHeader.stateRaw);
      record.sealedAtTick = passHeader.sealedAtTick;
      record.committedChunkIds = std::move(committedChunkIds);
      snapshot.passes.recordPass = std::move(record);
    } else {
      OverdubPass overdub{};
      overdub.id = passHeader.id;
      overdub.mergeSequence = passHeader.mergeSequence;
      overdub.state = static_cast<CapturePassState>(passHeader.stateRaw);
      overdub.sealedAtTick = passHeader.sealedAtTick;
      overdub.committedChunkIds = std::move(committedChunkIds);
      snapshot.passes.overdubPasses.push_back(std::move(overdub));
    }
  }

  if (snapshot.nextPassId == 0) {
    snapshot.nextPassId = 1;
  }
  if (snapshot.nextNoteId == 0) {
    snapshot.nextNoteId = 1;
  }
  return readPersistedEditsTail(io, snapshot);
}

void STORAGE_LOOP_IO_MEM releasePersistedLoopSnapshotChunks(PersistedLoopSnapshot& snapshot) {
  if (snapshot.passes.hasRecordPass()) {
    LoopEventStore::releaseChunkRefs(snapshot.passes.recordPass.committedChunkIds);
    snapshot.passes.recordPass = RecordPass{};
  }
  for (OverdubPass& pass : snapshot.passes.overdubPasses) {
    LoopEventStore::releaseChunkRefs(pass.committedChunkIds);
  }
  snapshot.passes.overdubPasses.clear();
  snapshot.passes.editPasses.clear();
  snapshot.passes.loopGeometries.clear();
  snapshot = PersistedLoopSnapshot{};
}

namespace {

uint32_t parseClockUs() {
#if defined(ARDUINO)
  return micros();
#else
  return 0;
#endif
}

bool parseBudgetExhausted(uint32_t deadlineUs) {
  return deadlineUs != 0 && parseClockUs() >= deadlineUs;
}

StorageIo STORAGE_LOOP_IO_MEM bufferIoFromParseState(const uint8_t* data, size_t size,
                                                     PersistedLoopParseState& state) {
  return StorageIo{[data, size, &state](const void*, size_t) { return false; },
                   [data, size, &state](void* out, size_t n) {
                     if (state.pos + n > size) {
                       return false;
                     }
                     std::memcpy(out, data + state.pos, n);
                     state.pos += n;
                     return true;
                   },
                   [data, size, &state](void* out, size_t n) {
                     if (state.pos + n > size) {
                       return false;
                     }
                     std::memcpy(out, data + state.pos, n);
                     return true;
                   }};
}

bool STORAGE_LOOP_IO_MEM readSnapshotGeometryHeader(const StorageIo& io, PersistedLoopSnapshot& snapshot,
                                bool legacyWithoutNoteId, uint32_t& passCountOut) {
  releasePersistedLoopSnapshotChunks(snapshot);
  snapshot.loopId = kInvalidLoopId;
  snapshot.startLoopTick = 0;
  snapshot.loopLengthTicks = 0;
  snapshot.loopStartTick = 0;
  snapshot.nextPassId = 1;
  snapshot.nextNoteId = 1;
  snapshot.nextMergeSequence = 0;
  snapshot.lastCommittedPassId = kInvalidPassId;
  snapshot.passes = LoopPasses{};
  if (!ioRead(io, &snapshot.loopId, sizeof(snapshot.loopId))) return false;
  if (!ioRead(io, &snapshot.startLoopTick, sizeof(snapshot.startLoopTick))) return false;
  if (!ioRead(io, &snapshot.loopLengthTicks, sizeof(snapshot.loopLengthTicks))) return false;
  if (!ioRead(io, &snapshot.loopStartTick, sizeof(snapshot.loopStartTick))) return false;
  if (!ioRead(io, &snapshot.nextPassId, sizeof(snapshot.nextPassId))) return false;
  if (legacyWithoutNoteId) {
    snapshot.nextNoteId = 1;
  } else if (!ioRead(io, &snapshot.nextNoteId, sizeof(snapshot.nextNoteId))) {
    return false;
  }
  if (!ioRead(io, &snapshot.nextMergeSequence, sizeof(snapshot.nextMergeSequence))) return false;
  if (!ioRead(io, &snapshot.lastCommittedPassId, sizeof(snapshot.lastCommittedPassId))) {
    return false;
  }
  if (snapshot.loopLengthTicks >= 0x80000000u) {
    snapshot.loopLengthTicks = 0;
  }
  uint32_t passCount = 0;
  if (!ioRead(io, &passCount, sizeof(passCount))) return false;
  passCountOut = passCount;
  return true;
}

}  // namespace

PersistedLoopParseStepResult STORAGE_LOOP_IO_MEM stepPersistedLoopSnapshotParse(
    const uint8_t* data, size_t size, PersistedLoopSnapshot& snapshot,
    PersistedLoopParseState& state, uint32_t deadlineUs, uint32_t maxGrains) {
  if (data == nullptr && size != 0) {
    return PersistedLoopParseStepResult::Failed;
  }

  uint32_t grains = 0;
  auto grainDone = [&]() -> bool {
    ++grains;
    if (maxGrains != 0 && grains >= maxGrains) {
      return true;
    }
    return parseBudgetExhausted(deadlineUs);
  };

  auto failAndReset = [&]() {
    if (state.passStaging) {
      state.passStaging->clear();
      state.passStaging.reset();
    }
    releasePersistedLoopSnapshotChunks(snapshot);
    state = PersistedLoopParseState{};
    return PersistedLoopParseStepResult::Failed;
  };

  auto finalizeActivePass = [&]() -> bool {
    CommittedChunkIdList committedChunkIds;
    if (state.passStaging) {
      if (!state.passStaging->detachChunksToCommittedChunkIds(committedChunkIds)) {
        state.passStaging->clear();
        state.passStaging.reset();
        return false;
      }
      state.passStaging.reset();
    }
    const CapturePassSlotFileHeader& passHeader = state.activePassHeader;
    if (passHeader.typeRaw == 0) {
      RecordPass record{};
      record.id = passHeader.id;
      record.state = static_cast<CapturePassState>(passHeader.stateRaw);
      record.sealedAtTick = passHeader.sealedAtTick;
      record.committedChunkIds = std::move(committedChunkIds);
      snapshot.passes.recordPass = std::move(record);
    } else {
      OverdubPass overdub{};
      overdub.id = passHeader.id;
      overdub.mergeSequence = passHeader.mergeSequence;
      overdub.state = static_cast<CapturePassState>(passHeader.stateRaw);
      overdub.sealedAtTick = passHeader.sealedAtTick;
      overdub.committedChunkIds = std::move(committedChunkIds);
      snapshot.passes.overdubPasses.push_back(std::move(overdub));
    }
    state.passHeaderDone = false;
    state.passMidiRemaining = 0;
    state.activePassHeader = CapturePassSlotFileHeader{};
    ++state.passesDone;
    return true;
  };

  if (!state.headerDone) {
    if (parseBudgetExhausted(deadlineUs)) {
      return PersistedLoopParseStepResult::MoreWork;
    }
    const size_t startPos = state.pos;
    StorageIo io = bufferIoFromParseState(data, size, state);
    uint32_t passCount = 0;
    if (!readSnapshotGeometryHeader(io, snapshot, state.legacyWithoutNoteId, passCount)) {
      if (!state.legacyWithoutNoteId && !state.triedLegacyFallback) {
        state.triedLegacyFallback = true;
        state.legacyWithoutNoteId = true;
        state.pos = startPos;
        releasePersistedLoopSnapshotChunks(snapshot);
        io = bufferIoFromParseState(data, size, state);
        if (!readSnapshotGeometryHeader(io, snapshot, true, passCount)) {
          return failAndReset();
        }
      } else {
        return failAndReset();
      }
    }
    state.passCount = passCount;
    state.passesDone = 0;
    state.headerDone = true;
    // Timed LoadLoopJob parse: one grain per turn — never fall through into pass/batch
    // on the same stack frame (234625 hang after full 57KB read).
    if (deadlineUs != 0 || maxGrains != 0) {
      (void)grainDone();
      return PersistedLoopParseStepResult::MoreWork;
    }
    if (grainDone()) {
      return PersistedLoopParseStepResult::MoreWork;
    }
  }

  while (state.passesDone < state.passCount) {
    if (state.passReadyToFinalize) {
      if (parseBudgetExhausted(deadlineUs)) {
        return PersistedLoopParseStepResult::MoreWork;
      }
      if (!finalizeActivePass()) {
        return failAndReset();
      }
      state.passReadyToFinalize = false;
      // Always yield after detach/finalize — can be hundreds of ms on large passes
      // (session_20260718_223713: ~295ms spikes after batch trains).
      return PersistedLoopParseStepResult::MoreWork;
    }

    if (!state.passHeaderDone) {
      if (parseBudgetExhausted(deadlineUs)) {
        return PersistedLoopParseStepResult::MoreWork;
      }
      StorageIo io = bufferIoFromParseState(data, size, state);
      CapturePassSlotFileHeader& passHeader = state.activePassHeader;
      uint32_t midiCount = 0;
      if (!ioRead(io, &passHeader.id, sizeof(passHeader.id)) ||
          !ioRead(io, &passHeader.mergeSequence, sizeof(passHeader.mergeSequence)) ||
          !ioRead(io, &passHeader.stateRaw, sizeof(passHeader.stateRaw)) ||
          !ioRead(io, &passHeader.typeRaw, sizeof(passHeader.typeRaw)) ||
          !ioRead(io, &passHeader.sealedAtTick, sizeof(passHeader.sealedAtTick)) ||
          !ioRead(io, &midiCount, sizeof(midiCount))) {
        return failAndReset();
      }
      if (midiCount > MAX_PERSISTED_CAPTURE_PASS_EVENTS) {
        return failAndReset();
      }
      state.passMidiRemaining = midiCount;
      state.passHeaderDone = true;
      if (midiCount == 0) {
        state.passReadyToFinalize = true;
        return PersistedLoopParseStepResult::MoreWork;
      }
      state.passStaging = std::make_unique<LoopEventStore>();
      // Yield after pass header + staging alloc — batch append/seal is the next turn.
      if (deadlineUs != 0 || maxGrains != 0) {
        (void)grainDone();
        return PersistedLoopParseStepResult::MoreWork;
      }
      if (grainDone()) {
        return PersistedLoopParseStepResult::MoreWork;
      }
    }

    const uint32_t maxTick = maxPersistedEventTick(snapshot.loopLengthTicks);
    while (state.passMidiRemaining > 0) {
      if (parseBudgetExhausted(deadlineUs)) {
        return PersistedLoopParseStepResult::MoreWork;
      }
      if (!state.passStaging) {
        return failAndReset();
      }
      StorageIo io = bufferIoFromParseState(data, size, state);
      const uint32_t batchCount =
          state.passMidiRemaining > LoopEventStoreConfig::CHUNK_CAPACITY
              ? LoopEventStoreConfig::CHUNK_CAPACITY
              : state.passMidiRemaining;
      if (!state.passEventBatch) {
        const size_t bytes =
            static_cast<size_t>(LoopEventStoreConfig::CHUNK_CAPACITY) * sizeof(MidiEvent);
        void* raw = extmem_malloc(bytes);
        if (!raw) {
          raw = std::malloc(bytes);
        }
        if (!raw) {
          return failAndReset();
        }
        state.passEventBatch.reset(static_cast<MidiEvent*>(raw));
      }
      MidiEvent* const batch = state.passEventBatch.get();
      if (!ioRead(io, batch, static_cast<size_t>(batchCount) * sizeof(MidiEvent))) {
        return failAndReset();
      }
      for (uint32_t i = 0; i < batchCount; ++i) {
        const MidiEvent& evt = batch[i];
        if (evt.tick > maxTick) {
          return failAndReset();
        }
        if (!canStagePersistedEvent(state.passStaging->size())) {
          return failAndReset();
        }
        if (!state.passStaging->append(evt)) {
          return failAndReset();
        }
      }
      state.passMidiRemaining -= batchCount;
      if (deadlineUs != 0 || maxGrains != 0) {
        if (state.passMidiRemaining == 0) {
          state.passReadyToFinalize = true;
        }
        (void)grainDone();
        return PersistedLoopParseStepResult::MoreWork;
      }
    }

    state.passReadyToFinalize = true;
    return PersistedLoopParseStepResult::MoreWork;
  }

  if (!state.editsHeaderDone) {
    if (parseBudgetExhausted(deadlineUs)) {
      return PersistedLoopParseStepResult::MoreWork;
    }
    StorageIo io = bufferIoFromParseState(data, size, state);
    if (!ioRead(io, &snapshot.nextPassId, sizeof(snapshot.nextPassId))) {
      return failAndReset();
    }
    uint32_t marker = 0;
    if (!ioRead(io, &marker, sizeof(marker)) || marker != PERSISTED_EDITS_TAIL_MARKER) {
      return failAndReset();
    }
    uint32_t editCount = 0;
    if (!ioRead(io, &editCount, sizeof(editCount))) {
      return failAndReset();
    }
    state.editCount = editCount;
    state.editsDone = 0;
    snapshot.passes.editPasses.clear();
    snapshot.passes.editPasses.reserve(editCount);
    state.editsHeaderDone = true;
    // Timed parse: never fall through into reading all edit passes on this stack frame.
    if (deadlineUs != 0 || maxGrains != 0) {
      (void)grainDone();
      return PersistedLoopParseStepResult::MoreWork;
    }
    if (grainDone()) {
      return PersistedLoopParseStepResult::MoreWork;
    }
  }

  while (state.editsDone < state.editCount) {
    if (parseBudgetExhausted(deadlineUs)) {
      return PersistedLoopParseStepResult::MoreWork;
    }
    StorageIo io = bufferIoFromParseState(data, size, state);
    EditPass editPass{};
    if (!readPersistedEditPass(io, editPass)) {
      return failAndReset();
    }
    snapshot.passes.editPasses.push_back(std::move(editPass));
    ++state.editsDone;
    if (grainDone()) {
      return PersistedLoopParseStepResult::MoreWork;
    }
  }

  if (!state.geometryHeaderDone) {
    if (state.pos >= size) {
      state.geometryCount = 0;
      state.geometriesDone = 0;
      state.geometryHeaderDone = true;
    } else {
      if (parseBudgetExhausted(deadlineUs)) {
        return PersistedLoopParseStepResult::MoreWork;
      }
      StorageIo io = bufferIoFromParseState(data, size, state);
      uint32_t marker = 0;
      if (!ioRead(io, &marker, sizeof(marker)) || marker != PERSISTED_GEOMETRY_TAIL_MARKER) {
        return failAndReset();
      }
      uint32_t geometryCount = 0;
      if (!ioRead(io, &geometryCount, sizeof(geometryCount))) {
        return failAndReset();
      }
      if (geometryCount > MAX_PERSISTED_LOOP_GEOMETRIES) {
        return failAndReset();
      }
      state.geometryCount = geometryCount;
      state.geometriesDone = 0;
      snapshot.passes.loopGeometries.clear();
      snapshot.passes.loopGeometries.reserve(geometryCount);
      state.geometryHeaderDone = true;
      if (deadlineUs != 0 || maxGrains != 0) {
        (void)grainDone();
        return PersistedLoopParseStepResult::MoreWork;
      }
      if (grainDone()) {
        return PersistedLoopParseStepResult::MoreWork;
      }
    }
  }

  while (state.geometriesDone < state.geometryCount) {
    if (parseBudgetExhausted(deadlineUs)) {
      return PersistedLoopParseStepResult::MoreWork;
    }
    StorageIo io = bufferIoFromParseState(data, size, state);
    LoopGeometry geometry{};
    if (!readPersistedGeometryRecord(io, geometry)) {
      return failAndReset();
    }
    snapshot.passes.loopGeometries.push_back(geometry);
    ++state.geometriesDone;
    if (grainDone()) {
      return PersistedLoopParseStepResult::MoreWork;
    }
  }

  if (snapshot.nextPassId == 0) {
    snapshot.nextPassId = 1;
  }
  if (snapshot.nextNoteId == 0) {
    snapshot.nextNoteId = 1;
  }
  if (state.pos < size) {
    StorageIo io = bufferIoFromParseState(data, size, state);
    if (!applyPersistedOverdubSessionIndexTail(io, snapshot.passes.overdubPasses)) {
      return failAndReset();
    }
  }
  return PersistedLoopParseStepResult::Completed;
}

bool skipPersistedEditPassPayload(const StorageIo& io) {
  uint8_t passTypeRaw = 0;
  uint8_t stateRaw = 0;
  uint8_t actionTypeRaw = 0;
  uint8_t propertyTypeRaw = 0;
  uint32_t addedCount = 0;
  PassId passId = kInvalidPassId;
  uint8_t editPassIndex = 0;
  NoteId targetNoteId = 0;
  uint32_t startTick = 0;
  uint32_t endTick = 0;
  uint8_t pitch = 0;
  uint8_t velocity = 0;
  if (!ioRead(io, &passId, sizeof(passId))) return false;
  if (!ioRead(io, &passTypeRaw, sizeof(passTypeRaw))) return false;
  if (!isValidPassTypeRaw(passTypeRaw)) return false;
  if (!ioRead(io, &editPassIndex, sizeof(editPassIndex))) return false;
  if (!ioRead(io, &stateRaw, sizeof(stateRaw))) return false;
  if (!isValidEditPassStateRaw(stateRaw)) return false;
  if (!ioRead(io, &actionTypeRaw, sizeof(actionTypeRaw))) return false;
  if (!isValidActionTypeRaw(actionTypeRaw)) return false;
  if (!ioRead(io, &propertyTypeRaw, sizeof(propertyTypeRaw))) return false;
  if (!isValidPropertyTypeRaw(propertyTypeRaw)) return false;
  if (!ioRead(io, &targetNoteId, sizeof(targetNoteId))) return false;
  if (!ioRead(io, &startTick, sizeof(startTick))) return false;
  if (!ioRead(io, &endTick, sizeof(endTick))) return false;
  if (!ioRead(io, &pitch, sizeof(pitch))) return false;
  if (!ioRead(io, &velocity, sizeof(velocity))) return false;
  if (!ioRead(io, &addedCount, sizeof(addedCount))) return false;
  if (addedCount == 0) {
    return true;
  }
  std::vector<MidiEvent, ExternalMemoryFirstAllocator<MidiEvent>> batch;
  batch.reserve(LoopEventStoreConfig::CHUNK_CAPACITY);
  uint32_t remaining = addedCount;
  while (remaining > 0) {
    const uint32_t batchCount =
        remaining > LoopEventStoreConfig::CHUNK_CAPACITY
            ? LoopEventStoreConfig::CHUNK_CAPACITY
            : remaining;
    batch.resize(batchCount);
    if (!ioRead(io, batch.data(), static_cast<size_t>(batchCount) * sizeof(MidiEvent))) {
      return false;
    }
    remaining -= batchCount;
  }
  return true;
}

bool skipPersistedEditsTailPayload(const StorageIo& io) {
  uint32_t nextPassId = 0;
  uint32_t marker = 0;
  if (!ioRead(io, &nextPassId, sizeof(nextPassId))) return false;
  if (!ioRead(io, &marker, sizeof(marker))) return false;
  if (marker != PERSISTED_EDITS_TAIL_MARKER) return false;
  uint32_t editCount = 0;
  if (!ioRead(io, &editCount, sizeof(editCount))) return false;
  for (uint32_t i = 0; i < editCount; ++i) {
    if (!skipPersistedEditPassPayload(io)) return false;
  }
  if (!skipPersistedGeometryTail(io)) {
    return false;
  }
  return skipPersistedOverdubSessionIndexTail(io);
}

bool skipCapturePassSlotFilePayload(const StorageIo& io, uint32_t loopLengthTicks) {
  CapturePassSlotFileHeader passHeader{};
  uint32_t midiCount = 0;
  if (!ioRead(io, &passHeader.id, sizeof(passHeader.id))) return false;
  if (!ioRead(io, &passHeader.mergeSequence, sizeof(passHeader.mergeSequence))) return false;
  if (!ioRead(io, &passHeader.stateRaw, sizeof(passHeader.stateRaw))) return false;
  if (!ioRead(io, &passHeader.typeRaw, sizeof(passHeader.typeRaw))) return false;
  if (!ioRead(io, &passHeader.sealedAtTick, sizeof(passHeader.sealedAtTick))) return false;
  if (!ioRead(io, &midiCount, sizeof(midiCount))) return false;
  if (midiCount > MAX_PERSISTED_CAPTURE_PASS_EVENTS) {
    return false;
  }
  if (midiCount == 0) {
    return true;
  }
  const uint32_t maxTick = maxPersistedEventTick(loopLengthTicks);
  std::vector<MidiEvent, ExternalMemoryFirstAllocator<MidiEvent>> batch;
  batch.reserve(LoopEventStoreConfig::CHUNK_CAPACITY);
  uint32_t remaining = midiCount;
  while (remaining > 0) {
    const uint32_t batchCount =
        remaining > LoopEventStoreConfig::CHUNK_CAPACITY
            ? LoopEventStoreConfig::CHUNK_CAPACITY
            : remaining;
    batch.resize(batchCount);
    if (!ioRead(io, batch.data(), static_cast<size_t>(batchCount) * sizeof(MidiEvent))) {
      return false;
    }
    for (uint32_t i = 0; i < batchCount; ++i) {
      if (batch[i].tick > maxTick) {
        return false;
      }
    }
    remaining -= batchCount;
#if defined(ARDUINO)
    yield();
#endif
  }
  return true;
}

bool skipPersistedLoopSnapshotPayload(const StorageIo& io, bool legacyDeferredHeaderWithoutNoteId) {
  PersistedLoopSnapshot ignored{};
  return readPersistedLoopSnapshotHeader(io, ignored, legacyDeferredHeaderWithoutNoteId);
}

bool readPersistedLoopSnapshotHeader(const StorageIo& io, PersistedLoopSnapshot& snapshot,
                                     bool legacyDeferredHeaderWithoutNoteId) {
  snapshot = PersistedLoopSnapshot{};
  if (!ioRead(io, &snapshot.loopId, sizeof(snapshot.loopId))) return false;
  if (!ioRead(io, &snapshot.startLoopTick, sizeof(snapshot.startLoopTick))) return false;
  if (!ioRead(io, &snapshot.loopLengthTicks, sizeof(snapshot.loopLengthTicks))) return false;
  if (!ioRead(io, &snapshot.loopStartTick, sizeof(snapshot.loopStartTick))) return false;
  if (!ioRead(io, &snapshot.nextPassId, sizeof(snapshot.nextPassId))) return false;
  if (legacyDeferredHeaderWithoutNoteId) {
    snapshot.nextNoteId = 1;
  } else if (!ioRead(io, &snapshot.nextNoteId, sizeof(snapshot.nextNoteId))) {
    return false;
  }
  if (!ioRead(io, &snapshot.nextMergeSequence, sizeof(snapshot.nextMergeSequence))) return false;
  if (!ioRead(io, &snapshot.lastCommittedPassId, sizeof(snapshot.lastCommittedPassId))) {
    return false;
  }
  if (snapshot.loopLengthTicks >= 0x80000000u) {
    snapshot.loopLengthTicks = 0;
  }

  uint32_t passCount = 0;
  if (!ioRead(io, &passCount, sizeof(passCount))) return false;
  for (uint32_t i = 0; i < passCount; ++i) {
    if (!skipCapturePassSlotFilePayload(io, snapshot.loopLengthTicks)) {
      return false;
    }
  }
  return skipPersistedEditsTailPayload(io);
}

#if !defined(PIO_UNIT_TEST_NATIVE)

#include "Loop.h"

bool writeLoopPersisted(const StorageIo& io, const Loop& loop) {
  if (!ioWrite(io, &loop.loopId, sizeof(loop.loopId))) return false;
  if (!ioWrite(io, &loop.startLoopTick, sizeof(loop.startLoopTick))) return false;
  if (!ioWrite(io, &loop.loopLengthTicks, sizeof(loop.loopLengthTicks))) return false;
  if (!ioWrite(io, &loop.loopStartTick, sizeof(loop.loopStartTick))) return false;
  if (!ioWrite(io, &loop.nextPassId_, sizeof(loop.nextPassId_))) return false;
  if (!ioWrite(io, &loop.nextNoteId_, sizeof(loop.nextNoteId_))) return false;
  if (!ioWrite(io, &loop.nextMergeSequence_, sizeof(loop.nextMergeSequence_))) return false;
  if (!ioWrite(io, &loop.lastCommittedPassId_, sizeof(loop.lastCommittedPassId_))) {
    return false;
  }

  uint32_t persistedCount = 0;
  if (loop.passes.hasRecordPass()) {
    ++persistedCount;
  }
  persistedCount += static_cast<uint32_t>(loop.passes.overdubPasses.size());
  if (!ioWrite(io, &persistedCount, sizeof(persistedCount))) return false;

  if (loop.passes.hasRecordPass()) {
    CapturePassSlotFileHeader passHeader{};
    passHeader.id = loop.passes.recordPass.id;
    passHeader.mergeSequence = 0;
    passHeader.stateRaw = static_cast<uint8_t>(loop.passes.recordPass.state);
    passHeader.typeRaw = 0;
    passHeader.sealedAtTick = loop.passes.recordPass.sealedAtTick;
    if (!writeCapturePassSlotFileHeader(io, passHeader, loop.passes.recordPass.committedChunkIds)) {
      return false;
    }
  }

  for (const OverdubPass& pass : loop.passes.overdubPasses) {
    CapturePassSlotFileHeader passHeader{};
    passHeader.id = pass.id;
    passHeader.mergeSequence = pass.mergeSequence;
    passHeader.stateRaw = static_cast<uint8_t>(pass.state);
    passHeader.typeRaw = 1;
    passHeader.sealedAtTick = pass.sealedAtTick;
    if (!writeCapturePassSlotFileHeader(io, passHeader, pass.committedChunkIds)) {
      return false;
    }
  }

  return writePersistedEditsTail(io, loop.nextPassId_, loop.passes.editPasses,
                                loop.passes.loopGeometries, loop.passes.overdubPasses);
}

size_t measureLoopSlotFileBytes(const Loop& loop) {
  size_t nbytes = 0;
  StorageIo io{
      [&nbytes](const void* data, size_t size) -> bool {
        nbytes += size;
        return true;
      },
      nullptr};
  if (!writeLoopPersisted(io, loop)) {
    return 0;
  }
  return nbytes;
}

bool readLoopPersisted(const StorageIo& io, Loop& loop) {
  PersistedLoopSnapshot snapshot{};
  if (!readPersistedLoopSnapshot(io, snapshot)) {
    return false;
  }
  applySnapshotToLoop(loop, snapshot);
  return true;
}

#endif  // !PIO_UNIT_TEST_NATIVE

#include "Loop.h"

void applySnapshotToLoop(Loop& loop, PersistedLoopSnapshot& snapshot) {
  loop.adoptPersistedSnapshot(snapshot);
}

void applyLoopSlotMetadataToLoop(Loop& loop, const PersistedLoopSnapshot& metadata) {
  if (metadata.loopId != kInvalidLoopId) {
    loop.loopId = metadata.loopId;
  }
  loop.startLoopTick = metadata.startLoopTick;
  loop.loopLengthTicks = metadata.loopLengthTicks;
  loop.loopStartTick = metadata.loopStartTick;
  loop.nextPassId_ = metadata.nextPassId != 0 ? metadata.nextPassId : 1;
  loop.nextNoteId_ = metadata.nextNoteId != 0 ? metadata.nextNoteId : 1;
  loop.nextMergeSequence_ = metadata.nextMergeSequence;
  loop.lastCommittedPassId_ = metadata.lastCommittedPassId;
}
