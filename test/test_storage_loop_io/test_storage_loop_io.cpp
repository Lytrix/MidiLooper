//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include <cstring>
#include <unity.h>
#include <vector>

#include "../../src/Logger.cpp"
#include "../../src/Utils/NoteUtils.cpp"
#include "../../src/LoopEventStore.cpp"
#include "../../src/EditManager/EditApply.cpp"
#include "../../src/Loop/LoopPasses.cpp"
#include "../../src/Loop/LoopContentHistory.cpp"
#include "../../src/StorageLoopIo.cpp"
#include "../test_support/MemoryMonitorNativeDeps.cpp"
#include "../../src/Utils/LoopEventValidation.cpp"
#include "../../src/Loop.cpp"
#include "../test_support/LoopCaptureTestDeps.cpp"
#include "LoopPasses.h"
#include "NoteEditFocus.h"
#include "StorageLoopIo.h"
#include "Loop.h"
#include "LoopContentHistory.h"
#include "EditPass.h"
#include "../test_support/CommittedChunkIdTestHelpers.h"
#include "MidiEvent.h"

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
        [this](void* data, size_t size) { return peek(data, size); },
    };
  }

  void resetRead() { readPos_ = 0; }
  size_t readPosition() const { return readPos_; }

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

  bool peek(void* data, size_t size) {
    if (readPos_ + size > buffer_->size()) {
      return false;
    }
    std::memcpy(data, buffer_->data() + readPos_, size);
    return true;
  }

  std::vector<uint8_t>* buffer_;
  size_t readPos_ = 0;
};

RecordPass makeRecordPassWithEvents(PassId id, uint32_t mergeSequence, CapturePassState state,
                              uint8_t typeRaw, uint32_t tick) {
  LoopEventStore capture;
  TEST_ASSERT_TRUE(capture.append(MidiEvent::NoteOn(tick, 1, 60, 100)));
  CommittedChunkIdList committedChunkIds;
  TEST_ASSERT_TRUE(transferCaptureStoreToCommittedChunkIds(capture, committedChunkIds));

  RecordPass pass{};
  pass.id = id;
  pass.state = state;
  pass.committedChunkIds = std::move(committedChunkIds);
  (void)mergeSequence;
  (void)typeRaw;
  return pass;
}

OverdubPass makeOverdubPassWithEvents(PassId id, uint32_t mergeSequence, CapturePassState state,
                                uint32_t tick) {
  LoopEventStore capture;
  TEST_ASSERT_TRUE(capture.append(MidiEvent::NoteOn(tick, 1, 60, 100)));
  CommittedChunkIdList committedChunkIds;
  TEST_ASSERT_TRUE(transferCaptureStoreToCommittedChunkIds(capture, committedChunkIds));

  OverdubPass pass{};
  pass.id = id;
  pass.mergeSequence = mergeSequence;
  pass.state = state;
  pass.committedChunkIds = std::move(committedChunkIds);
  return pass;
}

RecordPass makeRecordPassWithEventCount(PassId id, CapturePassState state, size_t eventCount) {
  LoopEventStore capture;
  for (size_t i = 0; i < eventCount; ++i) {
    const uint32_t tick = static_cast<uint32_t>(i);
    const uint8_t note = static_cast<uint8_t>(48u + (i % 12u));
    TEST_ASSERT_TRUE(capture.append(MidiEvent::NoteOn(tick, 1, note, 100)));
  }
  CommittedChunkIdList committedChunkIds;
  TEST_ASSERT_TRUE(transferCaptureStoreToCommittedChunkIds(capture, committedChunkIds));

  RecordPass pass{};
  pass.id = id;
  pass.state = state;
  pass.committedChunkIds = std::move(committedChunkIds);
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

  CommittedChunkIdList committedChunkIds;
  TEST_ASSERT_TRUE(transferCaptureStoreToCommittedChunkIds(capture, committedChunkIds));

  RecordPass pass{};
  pass.id = id;
  pass.state = state;
  pass.committedChunkIds = std::move(committedChunkIds);
  return pass;
}

EditPass makePitchEditPass(uint8_t ch, uint8_t note, uint32_t start, uint32_t end,
                           uint8_t pitch) {
  EditPass row{};
  row.passType = EditPassType::Note;
  row.actionType = EditActionType::Update;
  row.propertyType = EditPropertyType::Pitch;
  row.targetNoteId = 42;
  row.pitch = pitch;
  return row;
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
  original.nextNoteId = 12;
  original.nextMergeSequence = 1;
  original.lastCommittedPassId = 7;
  original.passes.recordPass = makeRecordPassWithEvents(7, 0, CapturePassState::Active, 0, 10);

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
  TEST_ASSERT_EQUAL(original.nextNoteId, restored.nextNoteId);
  TEST_ASSERT_EQUAL(original.nextMergeSequence, restored.nextMergeSequence);
  TEST_ASSERT_EQUAL(original.lastCommittedPassId, restored.lastCommittedPassId);
  TEST_ASSERT_TRUE(restored.passes.hasRecordPass());
  TEST_ASSERT_EQUAL(7u, restored.passes.recordPass.id);
  TEST_ASSERT_EQUAL(static_cast<uint8_t>(CapturePassState::Active),
                    static_cast<uint8_t>(restored.passes.recordPass.state));

  MidiEventVec flat;
  LoopEventStore::appendChunkRefEvents(restored.passes.recordPass.committedChunkIds, flat);
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
  original.lastCommittedPassId = 1;
  original.passes.recordPass = makeRecordPassWithEvents(1, 0, CapturePassState::Active, 0, 5);
  original.passes.overdubPasses.push_back(
      makeOverdubPassWithEvents(2, 1, CapturePassState::Disabled, 20));

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
  original.passes.recordPass = makeRecordPassWithEvents(1, 0, CapturePassState::Active, 0, 10);

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
  original.passes.recordPass = makeRecordPassWithEvents(7, 0, CapturePassState::Active, 0, 10);

  EditPass editPass{};
  editPass.id = 1;
  editPass.passType = EditPassType::Note;
  editPass.editPassIndex = 0;
  editPass.actionType = EditActionType::Delete;
  editPass.propertyType = EditPropertyType::None;
  editPass.state = EditPassState::Active;
  editPass.targetNoteId = 42;
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
  TEST_ASSERT_EQUAL(restored.passes.editPasses[0].targetNoteId, editPass.targetNoteId);
  TEST_ASSERT_TRUE(restored.passes.loopGeometries.empty());
}

void test_write_read_loop_geometry_tail_roundtrip() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();

  PersistedLoopSnapshot original{};
  original.loopId = 4;
  original.loopLengthTicks = 1536;
  original.loopStartTick = 96;
  original.startLoopTick = 0;
  original.nextPassId = 5;
  original.passes.recordPass = makeRecordPassWithEvents(1, 0, CapturePassState::Active, 0, 10);

  LoopGeometry geometry{};
  geometry.id = 4;
  geometry.loopStartTick = 96;
  geometry.loopLengthTicks = 1536;
  geometry.startLoopTick = 0;
  geometry.beforeLoopStartTick = 0;
  geometry.beforeLoopLengthTicks = 768;
  geometry.beforeStartLoopTick = 0;
  geometry.state = LoopGeometryState::Active;
  original.passes.loopGeometries.push_back(geometry);

  std::vector<uint8_t> buffer;
  MemoryStorageIo mem(&buffer);
  TEST_ASSERT_TRUE(writePersistedLoopSnapshot(mem.io(), original));

  PersistedLoopSnapshot restored{};
  mem.resetRead();
  TEST_ASSERT_TRUE(readPersistedLoopSnapshot(mem.io(), restored));

  TEST_ASSERT_EQUAL(1u, restored.passes.loopGeometries.size());
  TEST_ASSERT_EQUAL(4u, restored.passes.loopGeometries[0].id);
  TEST_ASSERT_EQUAL(96u, restored.passes.loopGeometries[0].loopStartTick);
  TEST_ASSERT_EQUAL(1536u, restored.passes.loopGeometries[0].loopLengthTicks);
  TEST_ASSERT_EQUAL(0u, restored.passes.loopGeometries[0].startLoopTick);
  TEST_ASSERT_EQUAL(0u, restored.passes.loopGeometries[0].beforeLoopStartTick);
  TEST_ASSERT_EQUAL(768u, restored.passes.loopGeometries[0].beforeLoopLengthTicks);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(LoopGeometryState::Active),
                          static_cast<uint8_t>(restored.passes.loopGeometries[0].state));
}

void test_legacy_snapshot_without_geometry_tail_loads() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();

  PersistedLoopSnapshot original{};
  original.loopId = 3;
  original.loopLengthTicks = 768;
  original.nextPassId = 2;
  original.passes.recordPass = makeRecordPassWithEvents(1, 0, CapturePassState::Active, 0, 10);

  std::vector<uint8_t> buffer;
  MemoryStorageIo mem(&buffer);
  TEST_ASSERT_TRUE(writePersistedLoopSnapshot(mem.io(), original));
  TEST_ASSERT_TRUE(buffer.size() >= 8u);
  buffer.resize(buffer.size() - 8u);

  PersistedLoopSnapshot restored{};
  mem.resetRead();
  TEST_ASSERT_TRUE(readPersistedLoopSnapshot(mem.io(), restored));
  TEST_ASSERT_TRUE(restored.passes.hasRecordPass());
  TEST_ASSERT_TRUE(restored.passes.loopGeometries.empty());
}

void test_save_loop_geometry_assigns_id_and_marks_dirty() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();

  Loop loop;
  loop.loopLengthTicks = 768;
  loop.loopStartTick = 0;
  loop.startLoopTick = 0;
  loop.nextPassId_ = 2;
  LoopGeometry row;
  row.loopStartTick = 48;
  row.loopLengthTicks = 1536;
  row.startLoopTick = 0;
  row.beforeLoopStartTick = 0;
  row.beforeLoopLengthTicks = 768;
  row.beforeStartLoopTick = 0;
  const PassId id = loop.saveLoopGeometry(row);
  TEST_ASSERT_EQUAL(2u, id);
  TEST_ASSERT_EQUAL(3u, loop.nextPassId_);
  TEST_ASSERT_EQUAL(1u, loop.passes.loopGeometries.size());
  TEST_ASSERT_EQUAL(48u, loop.passes.loopGeometries[0].loopStartTick);
  TEST_ASSERT_EQUAL(1536u, loop.passes.loopGeometries[0].loopLengthTicks);
  TEST_ASSERT_EQUAL(768u, loop.passes.loopGeometries[0].beforeLoopLengthTicks);
  TEST_ASSERT_TRUE(loop.isEditStateDirty());
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
  const PassId lastCommittedPassId = kInvalidPassId;
  const uint32_t passCount = 0;
  const uint32_t editCount = 1;

  appendRaw(buffer, loopId);
  appendRaw(buffer, startLoopTick);
  appendRaw(buffer, loopLengthTicks);
  appendRaw(buffer, loopStartTick);
  appendRaw(buffer, nextPassId);
  appendRaw(buffer, nextMergeSequence);
  appendRaw(buffer, lastCommittedPassId);
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
  struct LegacyNoteRef {
    uint8_t channel = 0;
    uint8_t note = 0;
    uint32_t startTick = 0;
    uint32_t endTick = 0;
  };
  const LegacyNoteRef target{1, 60, 10, 20};
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
  snapshot.passes.recordPass = makeRecordPassWithEvents(1, 0, CapturePassState::Active, 0, 10);

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
  original.passes.recordPass = makeRecordPassWithEvents(1, 0, CapturePassState::Active, 0, 10);

  EditPass editPass{};
  editPass.id = 1;
  editPass.passType = EditPassType::Note;
  editPass.actionType = EditActionType::Delete;
  editPass.propertyType = EditPropertyType::None;
  editPass.state = EditPassState::Active;
  editPass.targetNoteId = 42;
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
  const PassId lastCommittedPassId = kInvalidPassId;
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
  appendRaw(buffer, lastCommittedPassId);
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

  const size_t maxReadBatchEvents = getLastPersistedCapturePassReadMaxBatchEvents();
  TEST_ASSERT_EQUAL_UINT32(LoopEventStoreConfig::CHUNK_CAPACITY,
                           static_cast<uint32_t>(maxReadBatchEvents));

  MidiEventVec flat;
  LoopEventStore::appendChunkRefEvents(restored.passes.recordPass.committedChunkIds, flat);
  TEST_ASSERT_EQUAL(expectedEventCount, flat.size());
  TEST_ASSERT_EQUAL(0u, flat.front().tick);
  TEST_ASSERT_EQUAL(static_cast<uint32_t>(expectedEventCount - 1u), flat.back().tick);
}

void test_capture_pass_read_uses_chunk_stream_batch_bound() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  resetPersistedCapturePassWriteStatsForTest();

  PersistedLoopSnapshot original{};
  original.loopId = 10;
  original.loopLengthTicks = 8192;
  original.nextPassId = 2;
  // Exactly two full batches + remainder — read max must hit CHUNK_CAPACITY.
  const size_t expectedEventCount =
      static_cast<size_t>(LoopEventStoreConfig::CHUNK_CAPACITY) * 2u + 5u;
  original.passes.recordPass =
      makeRecordPassWithEventCount(1, CapturePassState::Active, expectedEventCount);

  std::vector<uint8_t> buffer;
  MemoryStorageIo mem(&buffer);
  TEST_ASSERT_TRUE(writePersistedLoopSnapshot(mem.io(), original));

  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  resetPersistedCapturePassWriteStatsForTest();

  PersistedLoopSnapshot restored{};
  mem.resetRead();
  TEST_ASSERT_TRUE(readPersistedLoopSnapshot(mem.io(), restored));

  const size_t maxReadBatchEvents = getLastPersistedCapturePassReadMaxBatchEvents();
  TEST_ASSERT_EQUAL_UINT32(LoopEventStoreConfig::CHUNK_CAPACITY,
                           static_cast<uint32_t>(maxReadBatchEvents));

  MidiEventVec flat;
  LoopEventStore::appendChunkRefEvents(restored.passes.recordPass.committedChunkIds, flat);
  TEST_ASSERT_EQUAL(expectedEventCount, flat.size());
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
  original.lastCommittedPassId = 1;
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
  LoopEventStore::appendChunkRefEvents(restored.passes.recordPass.committedChunkIds, restoredFlat);
  TEST_ASSERT_EQUAL(expectedEventCount, restoredFlat.size());
  TEST_ASSERT_EQUAL(0u, restoredFlat.front().tick);
  TEST_ASSERT_EQUAL(expectedLastTick, restoredFlat.back().tick);

  Loop reloadedLoop;
  applySnapshotToLoop(reloadedLoop, restored);
  TEST_ASSERT_EQUAL(expectedLoopLengthTicks, reloadedLoop.loopLengthTicks);
  TEST_ASSERT_TRUE(reloadedLoop.hasCommittedPasses());
  reloadedLoop.ensureVisualCacheBuilt();
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
  original.lastCommittedPassId = 1;
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

void test_save_note_edit_pass_marks_edit_dirty() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();

  Loop loop;
  loop.loopLengthTicks = 768;
  loop.passes.recordPass = makeRecordPassWithEvents(1, 0, CapturePassState::Active, 0, 10);
  loop.nextPassId_ = 2;

  TEST_ASSERT_FALSE(loop.isEditStateDirty());
  const EditPassId editId =
      loop.saveNoteEditPass(0, makePitchEditPass(1, 60, 10, 20, 67));
  TEST_ASSERT_NOT_EQUAL(kInvalidEditPassId, editId);
  TEST_ASSERT_TRUE(loop.isEditStateDirty());
}

void test_simulated_exit_flush_clears_edit_dirty() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();

  Loop loop;
  loop.loopLengthTicks = 768;
  loop.passes.recordPass = makeRecordPassWithEvents(1, 0, CapturePassState::Active, 0, 10);
  loop.nextPassId_ = 2;
  loop.saveNoteEditPass(0, makePitchEditPass(1, 60, 10, 20, 67));
  TEST_ASSERT_TRUE(loop.isEditStateDirty());

  PersistedLoopSnapshot snapshot{};
  snapshot.loopId = 1;
  snapshot.loopLengthTicks = loop.loopLengthTicks;
  snapshot.nextPassId = loop.nextPassId_;
  snapshot.passes.recordPass = loop.passes.recordPass;
  snapshot.passes.editPasses = loop.passes.editPasses;

  std::vector<uint8_t> buffer;
  MemoryStorageIo mem(&buffer);
  TEST_ASSERT_TRUE(writePersistedLoopSnapshot(mem.io(), snapshot));

  loop.clearEditStateDirty();
  TEST_ASSERT_FALSE(loop.isEditStateDirty());

  PersistedLoopSnapshot restored{};
  mem.resetRead();
  TEST_ASSERT_TRUE(readPersistedLoopSnapshot(mem.io(), restored));
  TEST_ASSERT_EQUAL(1u, restored.passes.editPasses.size());
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(EditPropertyType::Pitch),
                          static_cast<uint8_t>(restored.passes.editPasses[0].propertyType));
}

void test_legacy_deferred_header_without_note_id_reads() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();

  PersistedLoopSnapshot original{};
  original.loopId = 9;
  original.loopLengthTicks = 768;
  original.nextPassId = 2;
  original.nextNoteId = 12;
  original.nextMergeSequence = 1;
  original.lastCommittedPassId = 1;
  original.passes.recordPass = makeRecordPassWithEvents(1, 0, CapturePassState::Active, 0, 10);

  std::vector<uint8_t> v6Buffer;
  MemoryStorageIo v6Mem(&v6Buffer);
  TEST_ASSERT_TRUE(writePersistedLoopSnapshot(v6Mem.io(), original));

  const size_t nextNoteIdOffset =
      sizeof(LoopId) + (sizeof(uint32_t) * 3u) + sizeof(PassId);
  std::vector<uint8_t> legacyBuffer = v6Buffer;
  legacyBuffer.erase(legacyBuffer.begin() + static_cast<std::ptrdiff_t>(nextNoteIdOffset),
                     legacyBuffer.begin() + static_cast<std::ptrdiff_t>(nextNoteIdOffset + 4u));

  MemoryStorageIo legacyMem(&legacyBuffer);
  PersistedLoopSnapshot restored{};
  TEST_ASSERT_FALSE(readPersistedLoopSnapshot(legacyMem.io(), restored, false));
  legacyMem.resetRead();
  TEST_ASSERT_TRUE(readPersistedLoopSnapshot(legacyMem.io(), restored, true));
  TEST_ASSERT_EQUAL(original.loopLengthTicks, restored.loopLengthTicks);
  TEST_ASSERT_TRUE(restored.passes.hasRecordPass());
  TEST_ASSERT_EQUAL(1u, restored.nextNoteId);

  Loop reloadedLoop;
  applySnapshotToLoop(reloadedLoop, restored);
  TEST_ASSERT_TRUE(reloadedLoop.hasCommittedPasses());
  reloadedLoop.ensureVisualCacheBuilt();
  TEST_ASSERT_FALSE(reloadedLoop.visualCache.notes.empty());
}

void test_zero_loop_length_with_committed_events_loads_and_reconciles() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();

  PersistedLoopSnapshot original{};
  original.loopId = 1;
  original.loopLengthTicks = 0;
  original.loopStartTick = 0;
  original.nextPassId = 2;
  original.nextNoteId = 2;
  original.passes.recordPass = makeRecordPassWithEvents(1, 0, CapturePassState::Active, 0, 5584);

  std::vector<uint8_t> buffer;
  MemoryStorageIo mem(&buffer);
  TEST_ASSERT_TRUE(writePersistedLoopSnapshot(mem.io(), original));

  PersistedLoopSnapshot restored{};
  mem.resetRead();
  TEST_ASSERT_TRUE(readPersistedLoopSnapshot(mem.io(), restored));

  Loop loop;
  applySnapshotToLoop(loop, restored);
  TEST_ASSERT_TRUE(loop.hasCommittedPasses());
  TEST_ASSERT_EQUAL_UINT32(loop.reconcileLoopLengthWithCommittedPasses(0), loop.loopLengthTicks);
}

void test_measure_loop_snapshot_slot_file_bytes_matches_buffer() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();

  PersistedLoopSnapshot snapshot{};
  snapshot.loopId = 2;
  snapshot.loopLengthTicks = Config::TICKS_PER_BAR * 2u;
  snapshot.nextPassId = 2;
  snapshot.passes.recordPass = makeRecordPassWithEvents(1, 0, CapturePassState::Active, 0, 0);

  std::vector<uint8_t> buffer;
  MemoryStorageIo mem(&buffer);
  TEST_ASSERT_TRUE(writePersistedLoopSnapshot(mem.io(), snapshot));
  TEST_ASSERT_EQUAL(buffer.size(), measureLoopSnapshotSlotFileBytes(snapshot));
}

void test_read_loop_snapshot_header_skips_pass_payload() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();

  PersistedLoopSnapshot original{};
  original.loopId = 3;
  original.loopLengthTicks = Config::TICKS_PER_BAR * 16u;
  original.loopStartTick = 12;
  original.nextPassId = 4;
  original.nextNoteId = 9;
  original.passes.recordPass =
      makeRecordPassWithEvents(1, 0, CapturePassState::Active, 0, Config::TICKS_PER_BAR);

  std::vector<uint8_t> buffer;
  MemoryStorageIo mem(&buffer);
  TEST_ASSERT_TRUE(writePersistedLoopSnapshot(mem.io(), original));

  PersistedLoopSnapshot headerOnly{};
  mem.resetRead();
  TEST_ASSERT_TRUE(readPersistedLoopSnapshotHeader(mem.io(), headerOnly));
  TEST_ASSERT_EQUAL_UINT32(static_cast<uint32_t>(buffer.size()),
                           static_cast<uint32_t>(mem.readPosition()));
  TEST_ASSERT_EQUAL_UINT8(3, headerOnly.loopId);
  TEST_ASSERT_EQUAL_UINT32(Config::TICKS_PER_BAR * 16u, headerOnly.loopLengthTicks);
  TEST_ASSERT_EQUAL_UINT32(12, headerOnly.loopStartTick);
  TEST_ASSERT_TRUE(headerOnly.passes.overdubPasses.empty());
  TEST_ASSERT_FALSE(headerOnly.passes.hasRecordPass());
}

void test_apply_loop_slot_metadata_without_passes() {
  PersistedLoopSnapshot metadata{};
  metadata.loopId = 2;
  metadata.loopLengthTicks = Config::TICKS_PER_BAR * 4u;
  metadata.loopStartTick = 48;
  metadata.nextPassId = 5;
  metadata.nextNoteId = 7;

  Loop loop;
  applyLoopSlotMetadataToLoop(loop, metadata);
  TEST_ASSERT_EQUAL_UINT32(Config::TICKS_PER_BAR * 4u, loop.loopLengthTicks);
  TEST_ASSERT_EQUAL_UINT32(48, loop.loopStartTick);
  TEST_ASSERT_EQUAL_UINT8(2, loop.loopId);
  TEST_ASSERT_FALSE(loop.hasCommittedPasses());
  TEST_ASSERT_TRUE(loop.hasData());
}

void test_step_persisted_loop_snapshot_parse_resumes() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();

  PersistedLoopSnapshot original{};
  original.loopId = 11;
  original.loopLengthTicks = Config::TICKS_PER_BAR * 4u;
  original.nextPassId = 3;
  original.nextNoteId = 2;
  original.passes.recordPass =
      makeRecordPassWithEvents(1, 0, CapturePassState::Active, 0, 0);
  original.passes.overdubPasses.push_back(
      makeOverdubPassWithEvents(2, 1, CapturePassState::Active, Config::TICKS_PER_BAR));

  std::vector<uint8_t> buffer;
  MemoryStorageIo mem(&buffer);
  TEST_ASSERT_TRUE(writePersistedLoopSnapshot(mem.io(), original));

  PersistedLoopSnapshot staged{};
  PersistedLoopParseState state{};
  uint32_t steps = 0;
  PersistedLoopParseStepResult result = PersistedLoopParseStepResult::MoreWork;
  while (result == PersistedLoopParseStepResult::MoreWork && steps < 64) {
    // maxGrains=1 forces resume across calls (native micros() does not advance).
    result = stepPersistedLoopSnapshotParse(buffer.data(), buffer.size(), staged, state, 0, 1);
    ++steps;
  }
  TEST_ASSERT_EQUAL(static_cast<int>(PersistedLoopParseStepResult::Completed),
                    static_cast<int>(result));
  TEST_ASSERT_TRUE(steps >= 2);
  TEST_ASSERT_EQUAL_UINT8(11, staged.loopId);
  TEST_ASSERT_TRUE(staged.passes.hasRecordPass());
  TEST_ASSERT_EQUAL(1, staged.passes.overdubPasses.size());

  Loop loop;
  applySnapshotToLoop(loop, staged);
  TEST_ASSERT_TRUE(loop.hasCommittedPasses());
  releasePersistedLoopSnapshotChunks(staged);
}

void test_serialize_reload_content_undo_units_match() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();

  PersistedLoopSnapshot original{};
  original.loopId = 7;
  original.loopLengthTicks = 1536;
  original.loopStartTick = 48;
  original.nextPassId = 5;
  original.passes.recordPass = makeRecordPassWithEvents(1, 0, CapturePassState::Active, 0, 10);
  OverdubPass overdub{};
  overdub.id = 2;
  overdub.mergeSequence = 1;
  overdub.state = CapturePassState::Active;
  original.passes.overdubPasses.push_back(overdub);
  EditPass editPass{};
  editPass.id = 3;
  editPass.passType = EditPassType::Note;
  editPass.editPassIndex = 0;
  editPass.actionType = EditActionType::Update;
  editPass.propertyType = EditPropertyType::Length;
  editPass.state = EditPassState::Active;
  editPass.targetNoteId = 1;
  original.passes.editPasses.push_back(editPass);
  LoopGeometry geometry{};
  geometry.id = 4;
  geometry.loopStartTick = 48;
  geometry.loopLengthTicks = 1536;
  geometry.beforeLoopLengthTicks = 768;
  geometry.state = LoopGeometryState::Active;
  original.passes.loopGeometries.push_back(geometry);

  std::vector<ContentUndoUnit> before;
  deriveEffectiveContentUndoUnits(original.passes, before);

  std::vector<uint8_t> buffer;
  MemoryStorageIo mem(&buffer);
  TEST_ASSERT_TRUE(writePersistedLoopSnapshot(mem.io(), original));
  PersistedLoopSnapshot restored{};
  mem.resetRead();
  TEST_ASSERT_TRUE(readPersistedLoopSnapshot(mem.io(), restored));

  std::vector<ContentUndoUnit> after;
  deriveEffectiveContentUndoUnits(restored.passes, after);
  TEST_ASSERT_EQUAL(before.size(), after.size());
  TEST_ASSERT_EQUAL(4u, after.size());
  for (size_t i = 0; i < after.size(); ++i) {
    TEST_ASSERT_EQUAL(static_cast<int>(before[i].kind), static_cast<int>(after[i].kind));
    TEST_ASSERT_EQUAL(before[i].primaryPassId, after[i].primaryPassId);
  }
  releasePersistedLoopSnapshotChunks(restored);
}

int main(int /*argc*/, char** /*argv*/) {
  UNITY_BEGIN();
  RUN_TEST(test_write_read_loop_snapshot_roundtrip);
  RUN_TEST(test_write_read_disabled_take_preserved);
  RUN_TEST(test_pending_take_not_persisted);
  RUN_TEST(test_write_read_edits_tail_roundtrip);
  RUN_TEST(test_write_read_loop_geometry_tail_roundtrip);
  RUN_TEST(test_legacy_snapshot_without_geometry_tail_loads);
  RUN_TEST(test_save_loop_geometry_assigns_id_and_marks_dirty);
  RUN_TEST(test_legacy_edit_tail_v4_rejected);
  RUN_TEST(test_legacy_deferred_header_without_note_id_reads);
  RUN_TEST(test_zero_loop_length_with_committed_events_loads_and_reconciles);
  RUN_TEST(test_apply_snapshot_preserves_start_loop_tick);
  RUN_TEST(test_truncated_edit_tail_fails_read);
  RUN_TEST(test_corrupt_scoped_edit_tail_fails_read);
  RUN_TEST(test_capture_pass_write_uses_chunk_stream_batch_bound);
  RUN_TEST(test_capture_pass_read_uses_chunk_stream_batch_bound);
  RUN_TEST(test_64_bar_record_snapshot_reloads_after_reboot_simulation);
  RUN_TEST(test_64_bar_save_completes_at_ram2_floor_with_bounded_batch);
  RUN_TEST(test_save_note_edit_pass_marks_edit_dirty);
  RUN_TEST(test_simulated_exit_flush_clears_edit_dirty);
  RUN_TEST(test_measure_loop_snapshot_slot_file_bytes_matches_buffer);
  RUN_TEST(test_read_loop_snapshot_header_skips_pass_payload);
  RUN_TEST(test_apply_loop_slot_metadata_without_passes);
  RUN_TEST(test_step_persisted_loop_snapshot_parse_resumes);
  RUN_TEST(test_serialize_reload_content_undo_units_match);
  return UNITY_END();
}
