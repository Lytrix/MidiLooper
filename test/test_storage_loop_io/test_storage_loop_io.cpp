//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include <cstring>
#include <unity.h>
#include <vector>

#include "../../src/Logger.cpp"
#include "../../src/Utils/NoteUtils.cpp"
#include "../../src/LoopEventStore.cpp"
#include "../../src/EditApply.cpp"
#include "../../src/LoopPasses.cpp"
#include "../../src/StorageLoopIo.cpp"
#include "../../src/Utils/MemoryMonitor.cpp"
#include "../../src/Loop.cpp"
#include "LoopPasses.h"
#include "NoteEditFocus.h"
#include "StorageLoopIo.h"

namespace {

template <typename T>
void appendRaw(std::vector<uint8_t>& buffer, const T& value) {
  const uint8_t* ptr = reinterpret_cast<const uint8_t*>(&value);
  buffer.insert(buffer.end(), ptr, ptr + sizeof(T));
}

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

RecordPass makeRecordPassWithEventCount(PassId id, CapturePassState state, size_t eventCount) {
  LoopEventStore capture;
  for (size_t i = 0; i < eventCount; ++i) {
    const uint32_t tick = static_cast<uint32_t>(i);
    const uint8_t note = static_cast<uint8_t>(48u + (i % 12u));
    TEST_ASSERT_TRUE(capture.append(MidiEvent::NoteOn(tick, 1, note, 100)));
  }
  ChunkIdList refs;
  capture.detachChunksTo(refs);

  RecordPass pass{};
  pass.id = id;
  pass.state = state;
  pass.chunkRefs = std::move(refs);
  return pass;
}

RecordPass makeRecordPassForBars(PassId id, CapturePassState state, uint32_t bars) {
  LoopEventStore capture;
  const uint32_t ticksPerBeat = Config::TICKS_PER_BAR / 4u;
  const uint32_t noteDurationTicks = Config::TICKS_PER_BAR / 8u;
  const uint32_t notesPerBar = 4u;
  for (uint32_t bar = 0; bar < bars; ++bar) {
    for (uint32_t beat = 0; beat < notesPerBar; ++beat) {
      const uint32_t onTick = bar * Config::TICKS_PER_BAR + beat * ticksPerBeat;
      const uint32_t offTick = onTick + noteDurationTicks;
      const uint8_t note = static_cast<uint8_t>(48u + ((bar + beat) % 12u));
      TEST_ASSERT_TRUE(capture.append(MidiEvent::NoteOn(onTick, 1, note, 100)));
      TEST_ASSERT_TRUE(capture.append(MidiEvent::NoteOff(offTick, 1, note, 0)));
    }
  }

  ChunkIdList refs;
  capture.detachChunksTo(refs);

  RecordPass pass{};
  pass.id = id;
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
  original.startLoopTick = 480;
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
  TEST_ASSERT_EQUAL(original.startLoopTick, restored.startLoopTick);
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
  LoopEventStore::appendChunkRefEvents(restored.passes.recordPass.chunkRefs, flat);
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
  editPass.passType = EditPassType::Note;
  editPass.editPassIndex = 0;
  editPass.actionType = EditActionType::Delete;
  editPass.propertyType = EditPropertyType::None;
  editPass.state = EditPassState::Active;
  editPass.target = {1, 60, 10, 20};
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
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(EditPassType::Note),
                          static_cast<uint8_t>(restored.passes.editPasses[0].passType));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(EditActionType::Delete),
                          static_cast<uint8_t>(restored.passes.editPasses[0].actionType));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(EditPropertyType::None),
                          static_cast<uint8_t>(restored.passes.editPasses[0].propertyType));
  TEST_ASSERT_TRUE(noteRefEquals(restored.passes.editPasses[0].target, editPass.target));
}

void test_legacy_edit_tail_v4_rejected() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();

  std::vector<uint8_t> buffer;

  const LoopId loopId = 2;
  const uint32_t startLoopTick = 0;
  const uint32_t loopLengthTicks = 768;
  const uint32_t loopStartTick = 0;
  const PassId nextPassId = 2;
  const uint32_t nextMergeSequence = 0;
  const PassId lastPublishedPassId = kInvalidPassId;
  const uint32_t passCount = 0;
  const uint32_t editCount = 1;

  appendRaw(buffer, loopId);
  appendRaw(buffer, startLoopTick);
  appendRaw(buffer, loopLengthTicks);
  appendRaw(buffer, loopStartTick);
  appendRaw(buffer, nextPassId);
  appendRaw(buffer, nextMergeSequence);
  appendRaw(buffer, lastPublishedPassId);
  appendRaw(buffer, passCount);

  // Legacy v4 edit tail (no EPT3 marker).
  appendRaw(buffer, nextPassId);
  appendRaw(buffer, editCount);

  const EditPassId editPassId = 1;
  const uint8_t noteEditPassIndex = 7;
  const uint8_t stateRaw = static_cast<uint8_t>(EditPassState::Active);
  const uint32_t changeCount = 1;
  appendRaw(buffer, editPassId);
  appendRaw(buffer, noteEditPassIndex);
  appendRaw(buffer, stateRaw);
  appendRaw(buffer, changeCount);

  const uint8_t typeRaw = 1;
  const NoteRef target{1, 60, 10, 20};
  const uint8_t newPitch = 0;
  const uint32_t newStartTick = 0;
  const uint32_t newEndTick = 0;
  const uint32_t addedCount = 0;
  appendRaw(buffer, typeRaw);
  appendRaw(buffer, target);
  appendRaw(buffer, newPitch);
  appendRaw(buffer, newStartTick);
  appendRaw(buffer, newEndTick);
  appendRaw(buffer, addedCount);

  MemoryStorageIo mem(&buffer);
  PersistedLoopSnapshot restored{};
  TEST_ASSERT_FALSE(readPersistedLoopSnapshot(mem.io(), restored));
}

void test_apply_snapshot_preserves_start_loop_tick() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();

  PersistedLoopSnapshot snapshot{};
  snapshot.loopId = 2;
  snapshot.startLoopTick = 1536;
  snapshot.loopLengthTicks = 768;
  snapshot.loopStartTick = 0;
  snapshot.nextPassId = 2;
  snapshot.passes.recordPass = makeRecordPassWire(1, 0, CapturePassState::Active, 0, 10);

  Loop loop;
  applySnapshotToLoop(loop, snapshot);
  TEST_ASSERT_EQUAL(1536u, loop.startLoopTick);
  TEST_ASSERT_EQUAL(768u, loop.loopLengthTicks);
}

void test_truncated_edit_tail_fails_read() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();

  PersistedLoopSnapshot original{};
  original.loopId = 1;
  original.loopLengthTicks = 768;
  original.nextPassId = 3;
  original.passes.recordPass = makeRecordPassWire(1, 0, CapturePassState::Active, 0, 10);

  EditPass editPass{};
  editPass.id = 1;
  editPass.passType = EditPassType::Note;
  editPass.actionType = EditActionType::Delete;
  editPass.propertyType = EditPropertyType::None;
  editPass.state = EditPassState::Active;
  editPass.target = {1, 60, 10, 20};
  original.passes.editPasses.push_back(editPass);

  std::vector<uint8_t> buffer;
  MemoryStorageIo mem(&buffer);
  TEST_ASSERT_TRUE(writePersistedLoopSnapshot(mem.io(), original));

  buffer.resize(buffer.size() - 4u);

  PersistedLoopSnapshot restored{};
  mem.resetRead();
  TEST_ASSERT_FALSE(readPersistedLoopSnapshot(mem.io(), restored));
}

void test_corrupt_scoped_edit_tail_fails_read() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();

  std::vector<uint8_t> buffer;
  const LoopId loopId = 11;
  const uint32_t zero = 0;
  const PassId nextPassId = 2;
  const PassId lastPublishedPassId = kInvalidPassId;
  const uint32_t passCount = 0;
  const uint32_t scopedMarker = 0x45505433u;  // "EPT3"
  const uint32_t editCount = 1;
  const EditPassId editPassId = 1;
  const uint8_t invalidPassType = 0xFF;
  const uint8_t editPassIndex = 0;
  const uint8_t stateRaw = static_cast<uint8_t>(EditPassState::Active);
  const uint8_t actionRaw = static_cast<uint8_t>(EditActionType::Update);
  const uint8_t propertyRaw = static_cast<uint8_t>(EditPropertyType::None);

  appendRaw(buffer, loopId);
  appendRaw(buffer, zero);
  appendRaw(buffer, zero);
  appendRaw(buffer, zero);
  appendRaw(buffer, nextPassId);
  appendRaw(buffer, zero);
  appendRaw(buffer, lastPublishedPassId);
  appendRaw(buffer, passCount);

  appendRaw(buffer, nextPassId);
  appendRaw(buffer, scopedMarker);
  appendRaw(buffer, editCount);
  appendRaw(buffer, editPassId);
  appendRaw(buffer, invalidPassType);
  appendRaw(buffer, editPassIndex);
  appendRaw(buffer, stateRaw);
  appendRaw(buffer, actionRaw);
  appendRaw(buffer, propertyRaw);

  MemoryStorageIo mem(&buffer);
  PersistedLoopSnapshot restored{};
  TEST_ASSERT_FALSE(readPersistedLoopSnapshot(mem.io(), restored));
}

void test_capture_pass_write_uses_chunk_stream_batch_bound() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  resetPersistedCapturePassWriteStatsForTest();

  PersistedLoopSnapshot original{};
  original.loopId = 9;
  original.loopLengthTicks = 4096;
  original.nextPassId = 2;
  const size_t expectedEventCount =
      static_cast<size_t>(LoopEventStoreConfig::CHUNK_CAPACITY) * 3u + 17u;
  original.passes.recordPass =
      makeRecordPassWithEventCount(1, CapturePassState::Active, expectedEventCount);

  std::vector<uint8_t> buffer;
  MemoryStorageIo mem(&buffer);
  TEST_ASSERT_TRUE(writePersistedLoopSnapshot(mem.io(), original));

  const size_t maxBatchEvents = getLastPersistedCapturePassWriteMaxBatchEvents();
  TEST_ASSERT_GREATER_THAN(0u, maxBatchEvents);
  TEST_ASSERT_LESS_OR_EQUAL_UINT32(LoopEventStoreConfig::CHUNK_CAPACITY,
                                   static_cast<uint32_t>(maxBatchEvents));

  PersistedLoopSnapshot restored{};
  mem.resetRead();
  TEST_ASSERT_TRUE(readPersistedLoopSnapshot(mem.io(), restored));
  TEST_ASSERT_TRUE(restored.passes.hasRecordPass());

  MidiEventVec flat;
  LoopEventStore::appendChunkRefEvents(restored.passes.recordPass.chunkRefs, flat);
  TEST_ASSERT_EQUAL(expectedEventCount, flat.size());
  TEST_ASSERT_EQUAL(0u, flat.front().tick);
  TEST_ASSERT_EQUAL(static_cast<uint32_t>(expectedEventCount - 1u), flat.back().tick);
}

void test_64_bar_record_snapshot_reloads_after_reboot_simulation() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();

  constexpr uint32_t kRecordBars = 64u;
  constexpr uint32_t kNotesPerBar = 4u;
  const uint32_t expectedLoopLengthTicks = kRecordBars * Config::TICKS_PER_BAR;
  const size_t expectedEventCount =
      static_cast<size_t>(kRecordBars) * static_cast<size_t>(kNotesPerBar) * 2u;
  const uint32_t expectedLastTick =
      (kRecordBars - 1u) * Config::TICKS_PER_BAR + 3u * (Config::TICKS_PER_BAR / 4u) +
      (Config::TICKS_PER_BAR / 8u);

  PersistedLoopSnapshot original{};
  original.loopId = 12;
  original.startLoopTick = 0;
  original.loopLengthTicks = expectedLoopLengthTicks;
  original.loopStartTick = 0;
  original.nextPassId = 2;
  original.nextMergeSequence = 1;
  original.lastPublishedPassId = 1;
  original.passes.recordPass = makeRecordPassForBars(1, CapturePassState::Active, kRecordBars);

  std::vector<uint8_t> buffer;
  MemoryStorageIo mem(&buffer);
  TEST_ASSERT_TRUE(writePersistedLoopSnapshot(mem.io(), original));

  // Simulate reboot: clear the shared chunk pool before reading persisted bytes.
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();

  PersistedLoopSnapshot restored{};
  mem.resetRead();
  TEST_ASSERT_TRUE(readPersistedLoopSnapshot(mem.io(), restored));
  TEST_ASSERT_EQUAL(12u, restored.loopId);
  TEST_ASSERT_EQUAL(expectedLoopLengthTicks, restored.loopLengthTicks);
  TEST_ASSERT_TRUE(restored.passes.hasRecordPass());
  TEST_ASSERT_EQUAL(1u, restored.passes.recordPass.id);

  MidiEventVec restoredFlat;
  LoopEventStore::appendChunkRefEvents(restored.passes.recordPass.chunkRefs, restoredFlat);
  TEST_ASSERT_EQUAL(expectedEventCount, restoredFlat.size());
  TEST_ASSERT_EQUAL(0u, restoredFlat.front().tick);
  TEST_ASSERT_EQUAL(expectedLastTick, restoredFlat.back().tick);

  Loop reloadedLoop;
  applySnapshotToLoop(reloadedLoop, restored);
  TEST_ASSERT_EQUAL(expectedLoopLengthTicks, reloadedLoop.loopLengthTicks);
  TEST_ASSERT_TRUE(reloadedLoop.hasPublishedEvents());
  TEST_ASSERT_TRUE(!reloadedLoop.visualCache.notes.empty());

  MidiEventVec playbackFlat;
  reloadedLoop.mergeActiveCapturePasses(playbackFlat);
  TEST_ASSERT_EQUAL(expectedEventCount, playbackFlat.size());
  TEST_ASSERT_EQUAL(0u, playbackFlat.front().tick);
  TEST_ASSERT_EQUAL(expectedLastTick, playbackFlat.back().tick);
}

void test_64_bar_save_completes_at_ram2_floor_with_bounded_batch() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  resetPersistedCapturePassWriteStatsForTest();

  constexpr uint32_t kRecordBars = 64u;
  const uint32_t expectedLoopLengthTicks = kRecordBars * Config::TICKS_PER_BAR;
  const uint32_t floorBytes = LoopEventStore::internalHeapSafetyFloorBytes();
  MemoryMonitor::setNativeTestFreeHeap(floorBytes);

  PersistedLoopSnapshot original{};
  original.loopId = 13;
  original.startLoopTick = 0;
  original.loopLengthTicks = expectedLoopLengthTicks;
  original.loopStartTick = 0;
  original.nextPassId = 2;
  original.nextMergeSequence = 1;
  original.lastPublishedPassId = 1;
  original.passes.recordPass = makeRecordPassForBars(1, CapturePassState::Active, kRecordBars);

  std::vector<uint8_t> buffer;
  MemoryStorageIo mem(&buffer);
  TEST_ASSERT_TRUE(writePersistedLoopSnapshot(mem.io(), original));

  const size_t maxBatchEvents = getLastPersistedCapturePassWriteMaxBatchEvents();
  TEST_ASSERT_GREATER_THAN(0u, maxBatchEvents);
  TEST_ASSERT_LESS_OR_EQUAL_UINT32(LoopEventStoreConfig::CHUNK_CAPACITY,
                                   static_cast<uint32_t>(maxBatchEvents));

  PersistedLoopSnapshot restored{};
  mem.resetRead();
  TEST_ASSERT_TRUE(readPersistedLoopSnapshot(mem.io(), restored));
  TEST_ASSERT_EQUAL(13u, restored.loopId);
  TEST_ASSERT_EQUAL(expectedLoopLengthTicks, restored.loopLengthTicks);
  TEST_ASSERT_TRUE(restored.passes.hasRecordPass());

  MemoryMonitor::resetNativeTestFreeHeap();
}

int main(int /*argc*/, char** /*argv*/) {
  UNITY_BEGIN();
  RUN_TEST(test_write_read_loop_snapshot_roundtrip);
  RUN_TEST(test_write_read_disabled_take_preserved);
  RUN_TEST(test_pending_take_not_persisted);
  RUN_TEST(test_write_read_edits_tail_roundtrip);
  RUN_TEST(test_legacy_edit_tail_v4_rejected);
  RUN_TEST(test_apply_snapshot_preserves_start_loop_tick);
  RUN_TEST(test_truncated_edit_tail_fails_read);
  RUN_TEST(test_corrupt_scoped_edit_tail_fails_read);
  RUN_TEST(test_capture_pass_write_uses_chunk_stream_batch_bound);
  RUN_TEST(test_64_bar_record_snapshot_reloads_after_reboot_simulation);
  RUN_TEST(test_64_bar_save_completes_at_ram2_floor_with_bounded_batch);
  return UNITY_END();
}
