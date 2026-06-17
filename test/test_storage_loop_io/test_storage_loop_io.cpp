//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include <cstring>
#include <unity.h>
#include <vector>

#include "../../src/LoopEventStore.cpp"
#include "../../src/StorageLoopIo.cpp"
#include "Epoch.h"
#include "StorageLoopIo.h"

namespace {

class MemoryStorageIo {
 public:
  explicit MemoryStorageIo(std::vector<uint8_t>* buffer) : buffer_(buffer) {}

  StorageIo io() {
    return StorageIo{
        [this](const void* data, size_t size) { return write(data, size); },
        [this](void* data, size_t size) { return read(data, size); },
    };
  }

  void resetRead() { readPos_ = 0; }

 private:
  bool write(const void* data, size_t size) {
    const auto* bytes = static_cast<const uint8_t*>(data);
    buffer_->insert(buffer_->end(), bytes, bytes + size);
    return true;
  }

  bool read(void* data, size_t size) {
    if (readPos_ + size > buffer_->size()) {
      return false;
    }
    std::memcpy(data, buffer_->data() + readPos_, size);
    readPos_ += size;
    return true;
  }

  std::vector<uint8_t>* buffer_;
  size_t readPos_ = 0;
};

Epoch makeEpochWithNote(EpochId id, uint32_t mergeSequence, EpochState state, EpochKind kind,
                        uint32_t tick) {
  LoopEventStore capture;
  TEST_ASSERT_TRUE(capture.append(MidiEvent::NoteOn(tick, 1, 60, 100)));
  ChunkIdList refs;
  capture.detachChunksTo(refs);

  Epoch epoch{};
  epoch.id = id;
  epoch.mergeSequence = mergeSequence;
  epoch.state = state;
  epoch.kind = kind;
  epoch.chunkRefs = std::move(refs);
  return epoch;
}

}  // namespace

void test_write_read_loop_snapshot_roundtrip() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();

  PersistedLoopSnapshot original{};
  original.loopId = 3;
  original.loopLengthTicks = 768;
  original.loopStartTick = 12;
  original.nextEpochId = 8;
  original.nextMergeSequence = 1;
  original.lastPublishedEpochId = 7;
  original.epochs.push_back(makeEpochWithNote(7, 0, EpochState::Active, EpochKind::Record, 10));

  std::vector<uint8_t> buffer;
  MemoryStorageIo mem(&buffer);
  TEST_ASSERT_TRUE(writePersistedLoopSnapshot(mem.io(), original));

  PersistedLoopSnapshot restored{};
  mem.resetRead();
  TEST_ASSERT_TRUE(readPersistedLoopSnapshot(mem.io(), restored));

  TEST_ASSERT_EQUAL(original.loopId, restored.loopId);
  TEST_ASSERT_EQUAL(original.loopLengthTicks, restored.loopLengthTicks);
  TEST_ASSERT_EQUAL(original.loopStartTick, restored.loopStartTick);
  TEST_ASSERT_EQUAL(original.nextEpochId, restored.nextEpochId);
  TEST_ASSERT_EQUAL(original.nextMergeSequence, restored.nextMergeSequence);
  TEST_ASSERT_EQUAL(original.lastPublishedEpochId, restored.lastPublishedEpochId);
  TEST_ASSERT_EQUAL(1u, restored.epochs.size());
  TEST_ASSERT_EQUAL(7u, restored.epochs[0].id);
  TEST_ASSERT_EQUAL(static_cast<uint8_t>(EpochState::Active),
                    static_cast<uint8_t>(restored.epochs[0].state));

  MidiEventVec flat;
  LoopEventStore::appendFlattenedChunkIds(restored.epochs[0].chunkRefs, flat);
  TEST_ASSERT_EQUAL(1u, flat.size());
  TEST_ASSERT_EQUAL(10u, flat[0].tick);
}

void test_write_read_disabled_epoch_preserved() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();

  PersistedLoopSnapshot original{};
  original.loopId = 1;
  original.loopLengthTicks = 1536;
  original.nextEpochId = 3;
  original.nextMergeSequence = 2;
  original.lastPublishedEpochId = 1;
  original.epochs.push_back(makeEpochWithNote(1, 0, EpochState::Active, EpochKind::Record, 5));
  original.epochs.push_back(
      makeEpochWithNote(2, 1, EpochState::Disabled, EpochKind::Overdub, 20));

  std::vector<uint8_t> buffer;
  MemoryStorageIo mem(&buffer);
  TEST_ASSERT_TRUE(writePersistedLoopSnapshot(mem.io(), original));

  PersistedLoopSnapshot restored{};
  mem.resetRead();
  TEST_ASSERT_TRUE(readPersistedLoopSnapshot(mem.io(), restored));

  TEST_ASSERT_EQUAL(2u, restored.epochs.size());
  TEST_ASSERT_EQUAL(static_cast<uint8_t>(EpochState::Disabled),
                    static_cast<uint8_t>(restored.epochs[1].state));
}

void test_pending_epoch_not_persisted() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();

  PersistedLoopSnapshot original{};
  original.loopId = 0;
  original.epochs.push_back(makeEpochWithNote(1, 0, EpochState::Active, EpochKind::Record, 10));
  original.epochs.push_back(makeEpochWithNote(2, 1, EpochState::Pending, EpochKind::Overdub, 99));

  std::vector<uint8_t> buffer;
  MemoryStorageIo mem(&buffer);
  TEST_ASSERT_TRUE(writePersistedLoopSnapshot(mem.io(), original));

  PersistedLoopSnapshot restored{};
  mem.resetRead();
  TEST_ASSERT_TRUE(readPersistedLoopSnapshot(mem.io(), restored));

  TEST_ASSERT_EQUAL(1u, restored.epochs.size());
  TEST_ASSERT_EQUAL(1u, restored.epochs[0].id);
}

int main(int /*argc*/, char** /*argv*/) {
  UNITY_BEGIN();
  RUN_TEST(test_write_read_loop_snapshot_roundtrip);
  RUN_TEST(test_write_read_disabled_epoch_preserved);
  RUN_TEST(test_pending_epoch_not_persisted);
  return UNITY_END();
}
