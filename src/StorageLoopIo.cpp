//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "StorageLoopIo.h"

#if defined(ARDUINO)
#include <Arduino.h>
#endif

#include "LoopEventStore.h"
#include "Utils/MemoryMonitor.h"
#include "Utils/ExternalMemoryFirstAllocator.h"

namespace {

bool ioWrite(const StorageIo& io, const void* data, size_t size) {
  return io.write && io.write(data, size);
}

bool ioRead(const StorageIo& io, void* data, size_t size) {
  return io.read && io.read(data, size);
}

#if defined(PIO_UNIT_TEST_NATIVE)
size_t g_lastPersistedCapturePassWriteMaxBatchEvents = 0;
#endif

constexpr uint32_t MAX_PERSISTED_CAPTURE_PASS_EVENTS =
    static_cast<uint32_t>(LoopEventStoreConfig::POOL_CHUNK_COUNT) *
    LoopEventStoreConfig::CHUNK_CAPACITY;
constexpr uint32_t PERSISTED_EDITS_TAIL_MARKER = 0x45505433u;  // "EPT3"

uint32_t maxPersistedEventTick(uint32_t loopLengthTicks) {
  if (loopLengthTicks == 0 || loopLengthTicks >= 0x80000000u) {
    return LoopEventStoreConfig::BAR_TICKS;
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
  return LoopEventStore::hasInternalHeapHeadroomForNonCriticalWork(
      MemoryMonitor::getInternalHeapFreeBytes());
#else
  return true;
#endif
}

bool writePersistedCapturePassPayloadChunkStream(const StorageIo& io,
                                                 const ChunkIdList& chunkRefs,
                                                 size_t* maxBatchEvents) {
  std::vector<MidiEvent, ExternalMemoryFirstAllocator<MidiEvent>> batch;
  batch.reserve(LoopEventStoreConfig::CHUNK_CAPACITY);
  for (uint16_t chunkId : chunkRefs) {
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

bool writePersistedCapturePassWire(const StorageIo& io, const PersistedCapturePassWire& wire,
                                   const ChunkIdList& chunkRefs) {
  if (!ioWrite(io, &wire.id, sizeof(wire.id))) return false;
  if (!ioWrite(io, &wire.mergeSequence, sizeof(wire.mergeSequence))) return false;
  if (!ioWrite(io, &wire.stateRaw, sizeof(wire.stateRaw))) return false;
  if (!ioWrite(io, &wire.typeRaw, sizeof(wire.typeRaw))) return false;
  if (!ioWrite(io, &wire.sealedAtTick, sizeof(wire.sealedAtTick))) return false;

  const uint32_t midiCount =
      static_cast<uint32_t>(LoopEventStore::countEventsInChunkIds(chunkRefs));
  if (!ioWrite(io, &midiCount, sizeof(midiCount))) return false;
  size_t maxBatchEvents = 0;
  if (midiCount > 0 &&
      !writePersistedCapturePassPayloadChunkStream(io, chunkRefs, &maxBatchEvents)) {
    return false;
  }
#if defined(PIO_UNIT_TEST_NATIVE)
  g_lastPersistedCapturePassWriteMaxBatchEvents = maxBatchEvents;
#endif
  return true;
}

bool readPersistedCapturePassWire(const StorageIo& io, PersistedCapturePassWire& wire,
                                  ChunkIdList& chunkRefs, uint32_t loopLengthTicks) {
  uint32_t midiCount = 0;
  if (!ioRead(io, &wire.id, sizeof(wire.id))) return false;
  if (!ioRead(io, &wire.mergeSequence, sizeof(wire.mergeSequence))) return false;
  if (!ioRead(io, &wire.stateRaw, sizeof(wire.stateRaw))) return false;
  if (!ioRead(io, &wire.typeRaw, sizeof(wire.typeRaw))) return false;
  if (!ioRead(io, &wire.sealedAtTick, sizeof(wire.sealedAtTick))) return false;
  if (!ioRead(io, &midiCount, sizeof(midiCount))) return false;

  chunkRefs.clear();
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
  for (uint32_t i = 0; i < midiCount; ++i) {
    MidiEvent evt{};
    if (!ioRead(io, &evt, sizeof(evt))) return failStagingRead();
    if (evt.tick > maxTick) return failStagingRead();
    if (!canStagePersistedEvent(staging.size())) return failStagingRead();
    if (!staging.append(evt)) return failStagingRead();
  }
  staging.detachChunksTo(chunkRefs);
  return true;
}

#if defined(PIO_UNIT_TEST_NATIVE)
size_t getLastPersistedCapturePassWriteMaxBatchEvents() {
  return g_lastPersistedCapturePassWriteMaxBatchEvents;
}

void resetPersistedCapturePassWriteStatsForTest() {
  g_lastPersistedCapturePassWriteMaxBatchEvents = 0;
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

bool writePersistedEditPass(const StorageIo& io, const EditPass& editPass) {
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
  if (!ioWrite(io, &editPass.target, sizeof(editPass.target))) return false;
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

bool readPersistedEditPass(const StorageIo& io, EditPass& editPass) {
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
  if (!ioRead(io, &editPass.target, sizeof(editPass.target))) return false;
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
  editPass.addedEvents.reserve(addedCount);
  for (uint32_t i = 0; i < addedCount; ++i) {
    MidiEvent evt{};
    if (!ioRead(io, &evt, sizeof(evt))) return false;
    editPass.addedEvents.push_back(evt);
  }
  return true;
}

bool writePersistedEditsTail(const StorageIo& io, PassId nextPassId,
                             const EditPassVec& editPasses) {
  if (!ioWrite(io, &nextPassId, sizeof(nextPassId))) return false;
  const uint32_t marker = PERSISTED_EDITS_TAIL_MARKER;
  if (!ioWrite(io, &marker, sizeof(marker))) return false;
  const uint32_t editCount = static_cast<uint32_t>(editPasses.size());
  if (!ioWrite(io, &editCount, sizeof(editCount))) return false;
  for (const EditPass& editPass : editPasses) {
    if (!writePersistedEditPass(io, editPass)) return false;
  }
  return true;
}

bool readPersistedEditsTail(const StorageIo& io, PersistedLoopSnapshot& snapshot) {
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
  return true;
}

bool writePersistedLoopSnapshot(const StorageIo& io, const PersistedLoopSnapshot& snapshot) {
  if (!ioWrite(io, &snapshot.loopId, sizeof(snapshot.loopId))) return false;
  if (!ioWrite(io, &snapshot.startLoopTick, sizeof(snapshot.startLoopTick))) return false;
  if (!ioWrite(io, &snapshot.loopLengthTicks, sizeof(snapshot.loopLengthTicks))) return false;
  if (!ioWrite(io, &snapshot.loopStartTick, sizeof(snapshot.loopStartTick))) return false;
  if (!ioWrite(io, &snapshot.nextPassId, sizeof(snapshot.nextPassId))) return false;
  if (!ioWrite(io, &snapshot.nextMergeSequence, sizeof(snapshot.nextMergeSequence))) return false;
  if (!ioWrite(io, &snapshot.lastPublishedPassId, sizeof(snapshot.lastPublishedPassId))) {
    return false;
  }

  uint32_t persistedCount = 0;
  if (snapshot.passes.hasRecordPass()) {
    ++persistedCount;
  }
  persistedCount += static_cast<uint32_t>(snapshot.passes.overdubPasses.size());
  if (!ioWrite(io, &persistedCount, sizeof(persistedCount))) return false;

  if (snapshot.passes.hasRecordPass()) {
    PersistedCapturePassWire wire{};
    wire.id = snapshot.passes.recordPass.id;
    wire.mergeSequence = 0;
    wire.stateRaw = static_cast<uint8_t>(snapshot.passes.recordPass.state);
    wire.typeRaw = 0;
    wire.sealedAtTick = snapshot.passes.recordPass.sealedAtTick;
    if (!writePersistedCapturePassWire(io, wire, snapshot.passes.recordPass.chunkRefs)) {
      return false;
    }
  }
  for (const OverdubPass& pass : snapshot.passes.overdubPasses) {
    PersistedCapturePassWire wire{};
    wire.id = pass.id;
    wire.mergeSequence = pass.mergeSequence;
    wire.stateRaw = static_cast<uint8_t>(pass.state);
    wire.typeRaw = 1;
    wire.sealedAtTick = pass.sealedAtTick;
    if (!writePersistedCapturePassWire(io, wire, pass.chunkRefs)) {
      return false;
    }
  }
  return writePersistedEditsTail(io, snapshot.nextPassId, snapshot.passes.editPasses);
}

bool readPersistedLoopSnapshot(const StorageIo& io, PersistedLoopSnapshot& snapshot) {
  snapshot = PersistedLoopSnapshot{};
  if (!ioRead(io, &snapshot.loopId, sizeof(snapshot.loopId))) return false;
  if (!ioRead(io, &snapshot.startLoopTick, sizeof(snapshot.startLoopTick))) return false;
  if (!ioRead(io, &snapshot.loopLengthTicks, sizeof(snapshot.loopLengthTicks))) return false;
  if (!ioRead(io, &snapshot.loopStartTick, sizeof(snapshot.loopStartTick))) return false;
  if (!ioRead(io, &snapshot.nextPassId, sizeof(snapshot.nextPassId))) return false;
  if (!ioRead(io, &snapshot.nextMergeSequence, sizeof(snapshot.nextMergeSequence))) return false;
  if (!ioRead(io, &snapshot.lastPublishedPassId, sizeof(snapshot.lastPublishedPassId))) {
    return false;
  }

  if (snapshot.loopLengthTicks >= 0x80000000u) {
    snapshot.loopLengthTicks = 0;
  }

  uint32_t passCount = 0;
  if (!ioRead(io, &passCount, sizeof(passCount))) return false;

  snapshot.passes = LoopPasses{};
  for (uint32_t i = 0; i < passCount; ++i) {
    PersistedCapturePassWire wire{};
    ChunkIdList chunkRefs;
    if (!readPersistedCapturePassWire(io, wire, chunkRefs, snapshot.loopLengthTicks)) {
      return false;
    }
    if (wire.typeRaw == 0) {
      RecordPass record{};
      record.id = wire.id;
      record.state = static_cast<CapturePassState>(wire.stateRaw);
      record.sealedAtTick = wire.sealedAtTick;
      record.chunkRefs = std::move(chunkRefs);
      snapshot.passes.recordPass = std::move(record);
    } else {
      OverdubPass overdub{};
      overdub.id = wire.id;
      overdub.mergeSequence = wire.mergeSequence;
      overdub.state = static_cast<CapturePassState>(wire.stateRaw);
      overdub.sealedAtTick = wire.sealedAtTick;
      overdub.chunkRefs = std::move(chunkRefs);
      snapshot.passes.overdubPasses.push_back(std::move(overdub));
    }
  }

  if (snapshot.nextPassId == 0) {
    snapshot.nextPassId = 1;
  }
  return readPersistedEditsTail(io, snapshot);
}

#if !defined(PIO_UNIT_TEST_NATIVE)

#include "Loop.h"

bool writeLoopPersisted(const StorageIo& io, const Loop& loop) {
  if (!ioWrite(io, &loop.loopId, sizeof(loop.loopId))) return false;
  if (!ioWrite(io, &loop.startLoopTick, sizeof(loop.startLoopTick))) return false;
  if (!ioWrite(io, &loop.loopLengthTicks, sizeof(loop.loopLengthTicks))) return false;
  if (!ioWrite(io, &loop.loopStartTick, sizeof(loop.loopStartTick))) return false;
  if (!ioWrite(io, &loop.nextPassId_, sizeof(loop.nextPassId_))) return false;
  if (!ioWrite(io, &loop.nextMergeSequence_, sizeof(loop.nextMergeSequence_))) return false;
  if (!ioWrite(io, &loop.lastPublishedPassId_, sizeof(loop.lastPublishedPassId_))) {
    return false;
  }

  uint32_t persistedCount = 0;
  if (loop.passes.hasRecordPass()) {
    ++persistedCount;
  }
  persistedCount += static_cast<uint32_t>(loop.passes.overdubPasses.size());
  if (!ioWrite(io, &persistedCount, sizeof(persistedCount))) return false;

  if (loop.passes.hasRecordPass()) {
    PersistedCapturePassWire wire{};
    wire.id = loop.passes.recordPass.id;
    wire.mergeSequence = 0;
    wire.stateRaw = static_cast<uint8_t>(loop.passes.recordPass.state);
    wire.typeRaw = 0;
    wire.sealedAtTick = loop.passes.recordPass.sealedAtTick;
    if (!writePersistedCapturePassWire(io, wire, loop.passes.recordPass.chunkRefs)) {
      return false;
    }
  }

  for (const OverdubPass& pass : loop.passes.overdubPasses) {
    PersistedCapturePassWire wire{};
    wire.id = pass.id;
    wire.mergeSequence = pass.mergeSequence;
    wire.stateRaw = static_cast<uint8_t>(pass.state);
    wire.typeRaw = 1;
    wire.sealedAtTick = pass.sealedAtTick;
    if (!writePersistedCapturePassWire(io, wire, pass.chunkRefs)) {
      return false;
    }
  }

  return writePersistedEditsTail(io, loop.nextPassId_, loop.passes.editPasses);
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

void applySnapshotToLoop(Loop& loop, const PersistedLoopSnapshot& snapshot) {
  loop.discardPendingCapturePass();
  loop.discardCapture();
  loop.resetPassTimeline();
  loop.loopId = snapshot.loopId;
  loop.startLoopTick = snapshot.startLoopTick;
  loop.loopLengthTicks = snapshot.loopLengthTicks;
  loop.loopStartTick = snapshot.loopStartTick;
  loop.nextPassId_ = snapshot.nextPassId;
  loop.nextMergeSequence_ = snapshot.nextMergeSequence;
  loop.lastPublishedPassId_ = snapshot.lastPublishedPassId;
  loop.lastTickInLoop = 0;
  loop.nextEventIndex = 0;
  loop.playbackOrderDirty = true;
  loop.passes = snapshot.passes;
  loop.markDisplayCachesStale();
  loop.rebuildVisualCacheFromPasses();
}
