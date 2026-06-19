//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include <cstring>
#include <unity.h>
#include <vector>

#include "../../src/LoopEventStore.cpp"
#include "../../src/StorageLoopIo.cpp"
#include "Take.h"
#include "Edit.h"
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

Take makeTakeWithNote(TakeId id, uint32_t mergeSequence, TakeState state, TakeType kind,
                        uint32_t tick) {
  LoopEventStore capture;
  TEST_ASSERT_TRUE(capture.append(MidiEvent::NoteOn(tick, 1, 60, 100)));
  ChunkIdList refs;
  capture.detachChunksTo(refs);

  Take take{};
  take.id = id;
  take.mergeSequence = mergeSequence;
  take.state = state;
  take.type = kind;
  take.chunkRefs = std::move(refs);
  return take;
}

}  // namespace

void test_write_read_loop_snapshot_roundtrip() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();

  PersistedLoopSnapshot original{};
  original.loopId = 3;
  original.loopLengthTicks = 768;
  original.loopStartTick = 12;
  original.nextTakeId = 8;
  original.nextMergeSequence = 1;
  original.lastPublishedTakeId = 7;
  original.takes.push_back(makeTakeWithNote(7, 0, TakeState::Active, TakeType::Record, 10));

  std::vector<uint8_t> buffer;
  MemoryStorageIo mem(&buffer);
  TEST_ASSERT_TRUE(writePersistedLoopSnapshot(mem.io(), original));

  PersistedLoopSnapshot restored{};
  mem.resetRead();
  TEST_ASSERT_TRUE(readPersistedLoopSnapshot(mem.io(), restored));

  TEST_ASSERT_EQUAL(original.loopId, restored.loopId);
  TEST_ASSERT_EQUAL(original.loopLengthTicks, restored.loopLengthTicks);
  TEST_ASSERT_EQUAL(original.loopStartTick, restored.loopStartTick);
  TEST_ASSERT_EQUAL(original.nextTakeId, restored.nextTakeId);
  TEST_ASSERT_EQUAL(original.nextMergeSequence, restored.nextMergeSequence);
  TEST_ASSERT_EQUAL(original.lastPublishedTakeId, restored.lastPublishedTakeId);
  TEST_ASSERT_EQUAL(1u, restored.takes.size());
  TEST_ASSERT_EQUAL(7u, restored.takes[0].id);
  TEST_ASSERT_EQUAL(static_cast<uint8_t>(TakeState::Active),
                    static_cast<uint8_t>(restored.takes[0].state));

  MidiEventVec flat;
  LoopEventStore::appendFlattenedChunkIds(restored.takes[0].chunkRefs, flat);
  TEST_ASSERT_EQUAL(1u, flat.size());
  TEST_ASSERT_EQUAL(10u, flat[0].tick);
}

void test_write_read_disabled_take_preserved() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();

  PersistedLoopSnapshot original{};
  original.loopId = 1;
  original.loopLengthTicks = 1536;
  original.nextTakeId = 3;
  original.nextMergeSequence = 2;
  original.lastPublishedTakeId = 1;
  original.takes.push_back(makeTakeWithNote(1, 0, TakeState::Active, TakeType::Record, 5));
  original.takes.push_back(
      makeTakeWithNote(2, 1, TakeState::Disabled, TakeType::Overdub, 20));

  std::vector<uint8_t> buffer;
  MemoryStorageIo mem(&buffer);
  TEST_ASSERT_TRUE(writePersistedLoopSnapshot(mem.io(), original));

  PersistedLoopSnapshot restored{};
  mem.resetRead();
  TEST_ASSERT_TRUE(readPersistedLoopSnapshot(mem.io(), restored));

  TEST_ASSERT_EQUAL(2u, restored.takes.size());
  TEST_ASSERT_EQUAL(static_cast<uint8_t>(TakeState::Disabled),
                    static_cast<uint8_t>(restored.takes[1].state));
}

void test_pending_take_not_persisted() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();

  PersistedLoopSnapshot original{};
  original.loopId = 0;
  original.takes.push_back(makeTakeWithNote(1, 0, TakeState::Active, TakeType::Record, 10));
  original.takes.push_back(makeTakeWithNote(2, 1, TakeState::Pending, TakeType::Overdub, 99));

  std::vector<uint8_t> buffer;
  MemoryStorageIo mem(&buffer);
  TEST_ASSERT_TRUE(writePersistedLoopSnapshot(mem.io(), original));

  PersistedLoopSnapshot restored{};
  mem.resetRead();
  TEST_ASSERT_TRUE(readPersistedLoopSnapshot(mem.io(), restored));

  TEST_ASSERT_EQUAL(1u, restored.takes.size());
  TEST_ASSERT_EQUAL(1u, restored.takes[0].id);
}

void test_write_read_edits_tail_roundtrip() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();

  PersistedLoopSnapshot original{};
  original.loopId = 2;
  original.loopLengthTicks = 768;
  original.nextEditId = 3;
  original.takes.push_back(makeTakeWithNote(7, 0, TakeState::Active, TakeType::Record, 10));

  Edit edit{};
  edit.id = 1;
  edit.spanIndex = 0;
  edit.state = EditState::Active;
  EditChange del;
  del.type = EditChangeType::DeleteNote;
  del.target = {1, 60, 10, 20};
  edit.changes.push_back(del);
  original.edits.push_back(edit);

  std::vector<uint8_t> buffer;
  MemoryStorageIo mem(&buffer);
  TEST_ASSERT_TRUE(writePersistedLoopSnapshot(mem.io(), original));

  PersistedLoopSnapshot restored{};
  mem.resetRead();
  TEST_ASSERT_TRUE(readPersistedLoopSnapshot(mem.io(), restored));

  TEST_ASSERT_EQUAL(original.nextEditId, restored.nextEditId);
  TEST_ASSERT_EQUAL(1u, restored.edits.size());
  TEST_ASSERT_EQUAL(1u, restored.edits[0].id);
  TEST_ASSERT_EQUAL(static_cast<uint8_t>(EditChangeType::DeleteNote),
                    static_cast<uint8_t>(restored.edits[0].changes[0].type));
}

int main(int /*argc*/, char** /*argv*/) {
  UNITY_BEGIN();
  RUN_TEST(test_write_read_loop_snapshot_roundtrip);
  RUN_TEST(test_write_read_disabled_take_preserved);
  RUN_TEST(test_pending_take_not_persisted);
  RUN_TEST(test_write_read_edits_tail_roundtrip);
  return UNITY_END();
}
