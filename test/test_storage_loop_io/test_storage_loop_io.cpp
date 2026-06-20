//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include <cstring>
#include <unity.h>
#include <vector>

#include "../../src/LoopEventStore.cpp"
#include "../../src/StorageLoopIo.cpp"
#include "LoopPasses.h"
#include "EditPass.h"
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

RecordPass makeRecordPassWire(PassId id, uint32_t mergeSequence, CapturePassState state,
                              uint8_t typeRaw, uint32_t tick) {
  LoopEventStore capture;
  TEST_ASSERT_TRUE(capture.append(MidiEvent::NoteOn(tick, 1, 60, 100)));
  ChunkIdList refs;
  capture.detachChunksTo(refs);

  RecordPass pass{};
  pass.id = id;
  pass.state = state;
  pass.chunkRefs = std::move(refs);
  (void)mergeSequence;
  (void)typeRaw;
  return pass;
}

OverdubPass makeOverdubPassWire(PassId id, uint32_t mergeSequence, CapturePassState state,
                                uint32_t tick) {
  LoopEventStore capture;
  TEST_ASSERT_TRUE(capture.append(MidiEvent::NoteOn(tick, 1, 60, 100)));
  ChunkIdList refs;
  capture.detachChunksTo(refs);

  OverdubPass pass{};
  pass.id = id;
  pass.mergeSequence = mergeSequence;
  pass.state = state;
  pass.chunkRefs = std::move(refs);
  return pass;
}

}  // namespace

void test_write_read_loop_snapshot_roundtrip() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();

  PersistedLoopSnapshot original{};
  original.loopId = 3;
  original.loopLengthTicks = 768;
  original.loopStartTick = 12;
  original.nextPassId = 8;
  original.nextMergeSequence = 1;
  original.lastPublishedPassId = 7;
  original.passes.recordPass = makeRecordPassWire(7, 0, CapturePassState::Active, 0, 10);

  std::vector<uint8_t> buffer;
  MemoryStorageIo mem(&buffer);
  TEST_ASSERT_TRUE(writePersistedLoopSnapshot(mem.io(), original));

  PersistedLoopSnapshot restored{};
  mem.resetRead();
  TEST_ASSERT_TRUE(readPersistedLoopSnapshot(mem.io(), restored));

  TEST_ASSERT_EQUAL(original.loopId, restored.loopId);
  TEST_ASSERT_EQUAL(original.loopLengthTicks, restored.loopLengthTicks);
  TEST_ASSERT_EQUAL(original.loopStartTick, restored.loopStartTick);
  TEST_ASSERT_EQUAL(original.nextPassId, restored.nextPassId);
  TEST_ASSERT_EQUAL(original.nextMergeSequence, restored.nextMergeSequence);
  TEST_ASSERT_EQUAL(original.lastPublishedPassId, restored.lastPublishedPassId);
  TEST_ASSERT_TRUE(restored.passes.hasRecordPass());
  TEST_ASSERT_EQUAL(7u, restored.passes.recordPass.id);
  TEST_ASSERT_EQUAL(static_cast<uint8_t>(CapturePassState::Active),
                    static_cast<uint8_t>(restored.passes.recordPass.state));

  MidiEventVec flat;
  LoopEventStore::appendFlattenedChunkIds(restored.passes.recordPass.chunkRefs, flat);
  TEST_ASSERT_EQUAL(1u, flat.size());
  TEST_ASSERT_EQUAL(10u, flat[0].tick);
}

void test_write_read_disabled_take_preserved() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();

  PersistedLoopSnapshot original{};
  original.loopId = 1;
  original.loopLengthTicks = 1536;
  original.nextPassId = 3;
  original.nextMergeSequence = 2;
  original.lastPublishedPassId = 1;
  original.passes.recordPass = makeRecordPassWire(1, 0, CapturePassState::Active, 0, 5);
  original.passes.overdubPasses.push_back(
      makeOverdubPassWire(2, 1, CapturePassState::Disabled, 20));

  std::vector<uint8_t> buffer;
  MemoryStorageIo mem(&buffer);
  TEST_ASSERT_TRUE(writePersistedLoopSnapshot(mem.io(), original));

  PersistedLoopSnapshot restored{};
  mem.resetRead();
  TEST_ASSERT_TRUE(readPersistedLoopSnapshot(mem.io(), restored));

  TEST_ASSERT_EQUAL(1u, restored.passes.overdubPasses.size());
  TEST_ASSERT_EQUAL(static_cast<uint8_t>(CapturePassState::Disabled),
                    static_cast<uint8_t>(restored.passes.overdubPasses[0].state));
}

void test_pending_take_not_persisted() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();

  PersistedLoopSnapshot original{};
  original.loopId = 0;
  original.passes.recordPass = makeRecordPassWire(1, 0, CapturePassState::Active, 0, 10);

  std::vector<uint8_t> buffer;
  MemoryStorageIo mem(&buffer);
  TEST_ASSERT_TRUE(writePersistedLoopSnapshot(mem.io(), original));

  PersistedLoopSnapshot restored{};
  mem.resetRead();
  TEST_ASSERT_TRUE(readPersistedLoopSnapshot(mem.io(), restored));

  TEST_ASSERT_TRUE(restored.passes.hasRecordPass());
  TEST_ASSERT_EQUAL(1u, restored.passes.recordPass.id);
}

void test_write_read_edits_tail_roundtrip() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();

  PersistedLoopSnapshot original{};
  original.loopId = 2;
  original.loopLengthTicks = 768;
  original.nextPassId = 3;
  original.passes.recordPass = makeRecordPassWire(7, 0, CapturePassState::Active, 0, 10);

  EditPass editPass{};
  editPass.id = 1;
  editPass.noteEditPassIndex = 0;
  editPass.kind = EditPassKind::NoteEdit;
  editPass.state = EditPassState::Active;
  EditChange del;
  del.type = EditChangeType::DeleteNote;
  del.target = {1, 60, 10, 20};
  editPass.changes.push_back(del);
  original.passes.editPasses.push_back(editPass);

  std::vector<uint8_t> buffer;
  MemoryStorageIo mem(&buffer);
  TEST_ASSERT_TRUE(writePersistedLoopSnapshot(mem.io(), original));

  PersistedLoopSnapshot restored{};
  mem.resetRead();
  TEST_ASSERT_TRUE(readPersistedLoopSnapshot(mem.io(), restored));

  TEST_ASSERT_EQUAL(original.nextPassId, restored.nextPassId);
  TEST_ASSERT_EQUAL(1u, restored.passes.editPasses.size());
  TEST_ASSERT_EQUAL(1u, restored.passes.editPasses[0].id);
  TEST_ASSERT_EQUAL(static_cast<uint8_t>(EditChangeType::DeleteNote),
                    static_cast<uint8_t>(restored.passes.editPasses[0].changes[0].type));
}

int main(int /*argc*/, char** /*argv*/) {
  UNITY_BEGIN();
  RUN_TEST(test_write_read_loop_snapshot_roundtrip);
  RUN_TEST(test_write_read_disabled_take_preserved);
  RUN_TEST(test_pending_take_not_persisted);
  RUN_TEST(test_write_read_edits_tail_roundtrip);
  return UNITY_END();
}
