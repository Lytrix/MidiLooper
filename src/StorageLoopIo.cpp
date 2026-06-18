//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "StorageLoopIo.h"

#include "LoopEventStore.h"

namespace {

bool ioWrite(const StorageIo& io, const void* data, size_t size) {
  return io.write && io.write(data, size);
}

bool ioRead(const StorageIo& io, void* data, size_t size) {
  return io.read && io.read(data, size);
}

}  // namespace

bool writePersistedTake(const StorageIo& io, const Take& take) {
  uint8_t stateRaw = static_cast<uint8_t>(take.state);
  uint8_t typeRaw = static_cast<uint8_t>(take.type);
  if (!ioWrite(io, &take.id, sizeof(take.id))) return false;
  if (!ioWrite(io, &take.mergeSequence, sizeof(take.mergeSequence))) return false;
  if (!ioWrite(io, &stateRaw, sizeof(stateRaw))) return false;
  if (!ioWrite(io, &typeRaw, sizeof(typeRaw))) return false;
  if (!ioWrite(io, &take.sealedAtTick, sizeof(take.sealedAtTick))) return false;

  MidiEventVec flat;
  LoopEventStore::appendFlattenedChunkIds(take.chunkRefs, flat);
  const uint32_t midiCount = static_cast<uint32_t>(flat.size());
  if (!ioWrite(io, &midiCount, sizeof(midiCount))) return false;
  if (midiCount > 0 && !ioWrite(io, flat.data(), midiCount * sizeof(MidiEvent))) {
    return false;
  }
  return true;
}

bool readPersistedTake(const StorageIo& io, Take& take) {
  uint8_t stateRaw = 0;
  uint8_t typeRaw = 0;
  uint32_t midiCount = 0;
  if (!ioRead(io, &take.id, sizeof(take.id))) return false;
  if (!ioRead(io, &take.mergeSequence, sizeof(take.mergeSequence))) return false;
  if (!ioRead(io, &stateRaw, sizeof(stateRaw))) return false;
  if (!ioRead(io, &typeRaw, sizeof(typeRaw))) return false;
  if (!ioRead(io, &take.sealedAtTick, sizeof(take.sealedAtTick))) return false;
  if (!ioRead(io, &midiCount, sizeof(midiCount))) return false;

  take.state = static_cast<TakeState>(stateRaw);
  take.type = static_cast<TakeType>(typeRaw);
  take.chunkRefs.clear();

  if (midiCount == 0) {
    return true;
  }

  LoopEventStore staging;
  for (uint32_t i = 0; i < midiCount; ++i) {
    MidiEvent evt{};
    if (!ioRead(io, &evt, sizeof(evt))) return false;
    if (!staging.append(evt)) return false;
  }
  staging.detachChunksTo(take.chunkRefs);
  return true;
}

bool writePersistedLoopSnapshot(const StorageIo& io, const PersistedLoopSnapshot& snapshot) {
  if (!ioWrite(io, &snapshot.loopId, sizeof(snapshot.loopId))) return false;
  if (!ioWrite(io, &snapshot.startLoopTick, sizeof(snapshot.startLoopTick))) return false;
  if (!ioWrite(io, &snapshot.loopLengthTicks, sizeof(snapshot.loopLengthTicks))) return false;
  if (!ioWrite(io, &snapshot.loopStartTick, sizeof(snapshot.loopStartTick))) return false;
  if (!ioWrite(io, &snapshot.nextTakeId, sizeof(snapshot.nextTakeId))) return false;
  if (!ioWrite(io, &snapshot.nextMergeSequence, sizeof(snapshot.nextMergeSequence))) return false;
  if (!ioWrite(io, &snapshot.lastPublishedTakeId, sizeof(snapshot.lastPublishedTakeId))) {
    return false;
  }

  uint32_t persistedCount = 0;
  for (const Take& take : snapshot.takes) {
    if (take.state == TakeState::Pending) {
      continue;
    }
    ++persistedCount;
  }
  if (!ioWrite(io, &persistedCount, sizeof(persistedCount))) return false;

  for (const Take& take : snapshot.takes) {
    if (take.state == TakeState::Pending) {
      continue;
    }
    if (!writePersistedTake(io, take)) return false;
  }
  return true;
}

bool readPersistedLoopSnapshot(const StorageIo& io, PersistedLoopSnapshot& snapshot) {
  snapshot = PersistedLoopSnapshot{};
  if (!ioRead(io, &snapshot.loopId, sizeof(snapshot.loopId))) return false;
  if (!ioRead(io, &snapshot.startLoopTick, sizeof(snapshot.startLoopTick))) return false;
  if (!ioRead(io, &snapshot.loopLengthTicks, sizeof(snapshot.loopLengthTicks))) return false;
  if (!ioRead(io, &snapshot.loopStartTick, sizeof(snapshot.loopStartTick))) return false;
  if (!ioRead(io, &snapshot.nextTakeId, sizeof(snapshot.nextTakeId))) return false;
  if (!ioRead(io, &snapshot.nextMergeSequence, sizeof(snapshot.nextMergeSequence))) return false;
  if (!ioRead(io, &snapshot.lastPublishedTakeId, sizeof(snapshot.lastPublishedTakeId))) {
    return false;
  }

  if (snapshot.loopLengthTicks >= 0x80000000u) {
    snapshot.loopLengthTicks = 0;
  }

  uint32_t takeCount = 0;
  if (!ioRead(io, &takeCount, sizeof(takeCount))) return false;

  snapshot.takes.clear();
  snapshot.takes.reserve(takeCount);
  for (uint32_t i = 0; i < takeCount; ++i) {
    Take take{};
    if (!readPersistedTake(io, take)) return false;
    if (take.state == TakeState::Pending) {
      return false;
    }
    snapshot.takes.push_back(std::move(take));
  }

  if (snapshot.nextTakeId == 0) {
    snapshot.nextTakeId = 1;
  }
  return true;
}

#if !defined(PIO_UNIT_TEST_NATIVE)

#include "Loop.h"

PersistedLoopSnapshot snapshotFromLoop(const Loop& loop) {
  PersistedLoopSnapshot snapshot{};
  snapshot.loopId = loop.loopId;
  snapshot.startLoopTick = loop.startLoopTick;
  snapshot.loopLengthTicks = loop.loopLengthTicks;
  snapshot.loopStartTick = loop.loopStartTick;
  snapshot.nextTakeId = loop.nextTakeId_;
  snapshot.nextMergeSequence = loop.nextMergeSequence_;
  snapshot.lastPublishedTakeId = loop.lastPublishedTakeId_;
  snapshot.takes = loop.takes;
  return snapshot;
}

void applySnapshotToLoop(Loop& loop, const PersistedLoopSnapshot& snapshot) {
  loop.discardPendingTake();
  loop.discardCapture();
  loop.resetTakeTimeline();
  loop.loopId = snapshot.loopId;
  loop.startLoopTick = 0;
  loop.loopLengthTicks = snapshot.loopLengthTicks;
  loop.loopStartTick = snapshot.loopStartTick;
  loop.nextTakeId_ = snapshot.nextTakeId;
  loop.nextMergeSequence_ = snapshot.nextMergeSequence;
  loop.lastPublishedTakeId_ = snapshot.lastPublishedTakeId;
  loop.lastTickInLoop = 0;
  loop.nextEventIndex = 0;
  loop.playbackOrderDirty = true;
  loop.takes = snapshot.takes;
  loop.markDisplayCachesStale();
  loop.rebuildVisualCacheFromTakes();
}

bool writeLoopPersisted(const StorageIo& io, const Loop& loop) {
  return writePersistedLoopSnapshot(io, snapshotFromLoop(loop));
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
