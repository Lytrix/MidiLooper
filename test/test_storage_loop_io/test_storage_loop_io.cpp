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
#include "../../src/StorageManager/PersistedLoopSnapshotRelease.cpp"
#include "../../src/PassReclaim.cpp"
#include "../../src/Track/TrackUndoRetirement.cpp"
#include "../../src/Track/TrackUndoPublication.cpp"
#include "../../src/StorageLoopIoBodyFormatV1.cpp"
#include "../test_support/LoopCaptureTestDeps.cpp"
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
#include "LoopContentResolution.h"
#include "LoopSnapshot.h"
#include "MidiEvent.h"
#include "Utils/NoteUtils.h"

class Track;

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

void test_write_read_overdub_session_index_tail_roundtrip() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();

  PersistedLoopSnapshot original{};
  original.loopId = 8;
  original.loopLengthTicks = 768;
  original.nextPassId = 4;
  original.passes.recordPass = makeRecordPassWithEvents(1, 0, CapturePassState::Active, 0, 10);
  OverdubPass wrap1 = makeOverdubPassWithEvents(2, 1, CapturePassState::Active, 20);
  wrap1.overdubSessionIndex = 1;
  original.passes.overdubPasses.push_back(wrap1);
  OverdubPass wrap2 = makeOverdubPassWithEvents(3, 2, CapturePassState::Active, 40);
  wrap2.overdubSessionIndex = 1;
  original.passes.overdubPasses.push_back(wrap2);

  std::vector<uint8_t> buffer;
  MemoryStorageIo mem(&buffer);
  TEST_ASSERT_TRUE(writePersistedLoopSnapshot(mem.io(), original));

  PersistedLoopSnapshot restored{};
  mem.resetRead();
  TEST_ASSERT_TRUE(readPersistedLoopSnapshot(mem.io(), restored));
  TEST_ASSERT_EQUAL(2u, restored.passes.overdubPasses.size());
  TEST_ASSERT_EQUAL_UINT8(1, restored.passes.overdubPasses[0].overdubSessionIndex);
  TEST_ASSERT_EQUAL_UINT8(1, restored.passes.overdubPasses[1].overdubSessionIndex);

  std::vector<ContentUndoUnit> units;
  deriveEffectiveContentUndoUnits(restored.passes, units);
  TEST_ASSERT_EQUAL(2u, units.size());
  TEST_ASSERT_EQUAL(2u, units[1].passIds.size());
  releasePersistedLoopSnapshotChunks(restored);
}

void test_legacy_snapshot_without_overdub_session_index_tail_stays_ungrouped() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();

  PersistedLoopSnapshot original{};
  original.loopId = 9;
  original.loopLengthTicks = 768;
  original.nextPassId = 4;
  original.passes.recordPass = makeRecordPassWithEvents(1, 0, CapturePassState::Active, 0, 10);
  OverdubPass wrap1 = makeOverdubPassWithEvents(2, 1, CapturePassState::Active, 20);
  wrap1.overdubSessionIndex = 1;
  original.passes.overdubPasses.push_back(wrap1);
  OverdubPass wrap2 = makeOverdubPassWithEvents(3, 2, CapturePassState::Active, 40);
  wrap2.overdubSessionIndex = 1;
  original.passes.overdubPasses.push_back(wrap2);

  std::vector<uint8_t> buffer;
  MemoryStorageIo mem(&buffer);
  TEST_ASSERT_TRUE(writePersistedLoopSnapshot(mem.io(), original));
  const size_t tailBytes = sizeof(uint32_t) + sizeof(uint32_t) +
                           (2u * (sizeof(PassId) + sizeof(uint8_t)));
  TEST_ASSERT_TRUE(buffer.size() >= tailBytes);
  buffer.resize(buffer.size() - tailBytes);

  PersistedLoopSnapshot restored{};
  mem.resetRead();
  TEST_ASSERT_TRUE(readPersistedLoopSnapshot(mem.io(), restored));
  TEST_ASSERT_EQUAL(2u, restored.passes.overdubPasses.size());
  TEST_ASSERT_EQUAL_UINT8(kUngroupedOverdubSessionIndex,
                          restored.passes.overdubPasses[0].overdubSessionIndex);
  TEST_ASSERT_EQUAL_UINT8(kUngroupedOverdubSessionIndex,
                          restored.passes.overdubPasses[1].overdubSessionIndex);

  std::vector<ContentUndoUnit> units;
  deriveEffectiveContentUndoUnits(restored.passes, units);
  TEST_ASSERT_EQUAL(3u, units.size());
  releasePersistedLoopSnapshotChunks(restored);
}

ImmutableLoopSnapshotRef buildLsv1RoundTripSnapshotFixture() {
  const uint32_t loopLengthTicks = 2u * Config::TICKS_PER_BAR;
  auto parent = LoopSnapshot::createEmpty(3, loopLengthTicks, 12, 10, 6);
  LoopSnapshotChangeSet changes;

  SnapshotNoteChange wrapNote{};
  wrapNote.kind = SnapshotChangeKind::Add;
  wrapNote.next = {3, 5, 60, 100, Config::TICKS_PER_BAR - 48, 96};
  changes.notes.push_back(wrapNote);

  SnapshotNoteChange samePitch{};
  samePitch.kind = SnapshotChangeKind::Add;
  samePitch.next = {3, 6, 60, 90, 200, 320};
  changes.notes.push_back(samePitch);

  SnapshotNoteChange otherPitch{};
  otherPitch.kind = SnapshotChangeKind::Add;
  otherPitch.next = {3, 7, 64, 80, 400, 520};
  changes.notes.push_back(otherPitch);

  MidiEvent noteOn = MidiEvent::NoteOn(200, 2, 60, 90);
  noteOn.noteId = 6;
  SnapshotEventChange noteOnChange{};
  noteOnChange.kind = SnapshotChangeKind::Add;
  noteOnChange.next = {3, 1, 0, noteOn};
  changes.events.push_back(noteOnChange);

  MidiEvent noteOff = MidiEvent::NoteOff(320, 2, 60);
  noteOff.noteId = 6;
  SnapshotEventChange noteOffChange{};
  noteOffChange.kind = SnapshotChangeKind::Add;
  noteOffChange.next = {3, 2, 1, noteOff};
  changes.events.push_back(noteOffChange);

  MidiEvent unpairedOn = MidiEvent::NoteOn(700, 2, 64, 80);
  unpairedOn.noteId = 7;
  SnapshotEventChange unpairedOnChange{};
  unpairedOnChange.kind = SnapshotChangeKind::Add;
  unpairedOnChange.next = {3, 3, 2, unpairedOn};
  changes.events.push_back(unpairedOnChange);

  LoopSnapshotConstruction construction(parent, changes);
  while (!construction.complete()) {
    TEST_ASSERT_TRUE(construction.step(128));
  }
  return construction.freeze();
}

ImmutableLoopSnapshotRef buildLsv1SnapshotWithNoteCount(size_t noteCount) {
  const uint32_t loopLengthTicks = 4u * Config::TICKS_PER_BAR;
  auto parent = LoopSnapshot::createEmpty(
      3, loopLengthTicks, 0, static_cast<uint32_t>(noteCount + 10u),
      static_cast<uint32_t>(noteCount * 2u + 10u));
  LoopSnapshotChangeSet changes;

  for (size_t index = 0; index < noteCount; ++index) {
    const NoteId noteId = static_cast<NoteId>(index + 1u);
    const EventId noteOnEventId = static_cast<EventId>(index * 2u + 1u);
    const EventId noteOffEventId = static_cast<EventId>(index * 2u + 2u);
    const uint32_t startTick = static_cast<uint32_t>(index * 10u);
    const uint32_t endTick = startTick + 80u;
    const uint8_t pitch = static_cast<uint8_t>(60u + (index % 12u));

    SnapshotNoteChange noteChange{};
    noteChange.kind = SnapshotChangeKind::Add;
    noteChange.next = {3, noteId, pitch, 90, startTick, endTick};
    changes.notes.push_back(noteChange);

    MidiEvent noteOn = MidiEvent::NoteOn(startTick, 1, pitch, 90);
    noteOn.noteId = noteId;
    SnapshotEventChange noteOnChange{};
    noteOnChange.kind = SnapshotChangeKind::Add;
    noteOnChange.next = {3, noteOnEventId, 0, noteOn};
    changes.events.push_back(noteOnChange);

    MidiEvent noteOff = MidiEvent::NoteOff(endTick, 1, pitch);
    noteOff.noteId = noteId;
    SnapshotEventChange noteOffChange{};
    noteOffChange.kind = SnapshotChangeKind::Add;
    noteOffChange.next = {3, noteOffEventId, 1, noteOff};
    changes.events.push_back(noteOffChange);
  }

  LoopSnapshotConstruction construction(parent, changes);
  while (!construction.complete()) {
    TEST_ASSERT_TRUE(construction.step(128));
  }
  return construction.freeze();
}

class FailAfterWriteStorageIo {
 public:
  explicit FailAfterWriteStorageIo(std::vector<uint8_t>* buffer, size_t failAfterBytes)
      : buffer_(buffer), failAfterBytes_(failAfterBytes) {}

  StorageIo io() {
    return StorageIo{
        [this](const void* data, size_t size) { return write(data, size); },
        nullptr};
  }

 private:
  bool write(const void* data, size_t size) {
    const auto* bytes = static_cast<const uint8_t*>(data);
    size_t index = 0;
    while (index < size) {
      if (bytesWritten_ >= failAfterBytes_) {
        return false;
      }
      const size_t allowed = failAfterBytes_ - bytesWritten_;
      const size_t chunk = (size - index) <= allowed ? (size - index) : allowed;
      buffer_->insert(buffer_->end(), bytes + index, bytes + index + chunk);
      bytesWritten_ += chunk;
      index += chunk;
    }
    return true;
  }

  std::vector<uint8_t>* buffer_;
  size_t failAfterBytes_;
  size_t bytesWritten_ = 0;
};

bool writeGatheredPayloadInByteSlices(const LoopSnapshot& snapshot, size_t chunkBytes,
                                    std::vector<uint8_t>& out,
                                    std::vector<size_t>* offsetProgress) {
  const size_t totalBytes = testMeasureLoopSnapshotGatheredPayloadBytes(snapshot);
  if (totalBytes == 0) {
    return false;
  }
  size_t byteOffset = 0;
  MemoryStorageIo mem(&out);
  if (offsetProgress != nullptr) {
    offsetProgress->clear();
    offsetProgress->push_back(0);
  }
  while (byteOffset < totalBytes) {
    const size_t before = byteOffset;
    if (!testWriteLoopSnapshotGatheredPayloadByteSlice(mem.io(), snapshot, byteOffset,
                                                       chunkBytes)) {
      return false;
    }
    if (byteOffset <= before) {
      return false;
    }
    if (offsetProgress != nullptr) {
      offsetProgress->push_back(byteOffset);
    }
  }
  return byteOffset == totalBytes;
}

size_t findNoteCountForPayloadBytesAtLeast(size_t minBytes) {
  size_t low = 0;
  size_t high = 1;
  while (testMeasureLoopSnapshotGatheredPayloadBytes(*buildLsv1SnapshotWithNoteCount(high)) <
         minBytes) {
    high *= 2u;
    TEST_ASSERT_TRUE(high <= 256u);
  }
  while (low + 1u < high) {
    const size_t mid = low + (high - low) / 2u;
    const size_t bytes =
        testMeasureLoopSnapshotGatheredPayloadBytes(*buildLsv1SnapshotWithNoteCount(mid));
    if (bytes < minBytes) {
      low = mid;
    } else {
      high = mid;
    }
  }
  return high;
}

size_t findNoteCountForExactPayloadBytes(size_t targetBytes) {
  for (size_t count = 0; count <= 256u; ++count) {
    if (testMeasureLoopSnapshotGatheredPayloadBytes(*buildLsv1SnapshotWithNoteCount(count)) ==
        targetBytes) {
      return count;
    }
  }
  return static_cast<size_t>(-1);
}

size_t findNoteCountForPayloadBytesOverWithRemainder(size_t minBytes) {
  for (size_t count = 1; count <= 256u; ++count) {
    const size_t bytes =
        testMeasureLoopSnapshotGatheredPayloadBytes(*buildLsv1SnapshotWithNoteCount(count));
    if (bytes > minBytes && (bytes % kLsv1SaveIoChunkBytes) != 0u) {
      return count;
    }
  }
  TEST_FAIL_MESSAGE("no snapshot payload > minBytes with non-zero 512 remainder");
  return 0;
}

void assertSnapshotsEquivalent(const LoopSnapshot& expected, const LoopSnapshot& actual) {
  TEST_ASSERT_EQUAL(expected.loopId(), actual.loopId());
  TEST_ASSERT_EQUAL(expected.loopLengthTicks(), actual.loopLengthTicks());
  TEST_ASSERT_EQUAL(expected.loopStartTick(), actual.loopStartTick());
  TEST_ASSERT_EQUAL(expected.nextNoteId(), actual.nextNoteId());
  TEST_ASSERT_EQUAL(expected.nextEventId(), actual.nextEventId());

  NoteUtils::DisplayNoteVec expectedNotes;
  NoteUtils::DisplayNoteVec actualNotes;
  LoopContentResolution::resolveDisplayNotes(expected, expectedNotes);
  LoopContentResolution::resolveDisplayNotes(actual, actualNotes);
  TEST_ASSERT_EQUAL(expectedNotes.size(), actualNotes.size());
  for (size_t index = 0; index < expectedNotes.size(); ++index) {
    TEST_ASSERT_EQUAL(expectedNotes[index].noteId, actualNotes[index].noteId);
    TEST_ASSERT_EQUAL_UINT8(expectedNotes[index].note, actualNotes[index].note);
    TEST_ASSERT_EQUAL(expectedNotes[index].startTick, actualNotes[index].startTick);
    TEST_ASSERT_EQUAL(expectedNotes[index].endTick, actualNotes[index].endTick);
  }

  SessionMidiEventVec expectedEvents;
  SessionMidiEventVec actualEvents;
  LoopContentResolution::resolveWindow(expected, 0, expected.loopLengthTicks(), expectedEvents);
  LoopContentResolution::resolveWindow(actual, 0, actual.loopLengthTicks(), actualEvents);
  TEST_ASSERT_EQUAL(expectedEvents.size(), actualEvents.size());
}

void test_lsv1_body_format_roundtrip_dual_write() {
  resetSnapshotAllocCounters();
  const ImmutableLoopSnapshotRef snapshot = buildLsv1RoundTripSnapshotFixture();
  TEST_ASSERT_NOT_NULL(snapshot.get());

  PersistedLoopSnapshot metadata{};
  metadata.loopId = 3;
  metadata.startLoopTick = 480;
  metadata.loopLengthTicks = snapshot->loopLengthTicks();
  metadata.loopStartTick = 12;
  metadata.nextPassId = 8;
  metadata.nextNoteId = snapshot->nextNoteId();
  metadata.nextMergeSequence = 1;
  metadata.lastCommittedPassId = 7;

  PersistedLoopSnapshot persistedOracle{};
  persistedOracle.loopId = metadata.loopId;
  persistedOracle.loopLengthTicks = metadata.loopLengthTicks;
  persistedOracle.loopStartTick = metadata.loopStartTick;
  persistedOracle.nextPassId = metadata.nextPassId;
  persistedOracle.nextNoteId = metadata.nextNoteId;
  persistedOracle.nextMergeSequence = metadata.nextMergeSequence;
  persistedOracle.lastCommittedPassId = metadata.lastCommittedPassId;
  persistedOracle.passes.recordPass =
      makeRecordPassWithEvents(7, 0, CapturePassState::Active, 0, 10);

  std::vector<uint8_t> buffer;
  MemoryStorageIo mem(&buffer);
  TEST_ASSERT_TRUE(
      writeLoopSnapshotBodyFormatV1(mem.io(), metadata, persistedOracle, *snapshot));
  TEST_ASSERT_EQUAL(buffer.size(),
                    measureLoopSnapshotBodyFormatV1Bytes(metadata, persistedOracle, *snapshot));

  testResetRebuildSlotFromLoopContentCallCount();
  LoopSnapshotBodyFormatV1ReadResult readResult{};
  TEST_ASSERT_TRUE(readLoopSnapshotBodyFormatV1(buffer.data(), buffer.size(), readResult));
  TEST_ASSERT_TRUE(readResult.ok);
  TEST_ASSERT_FALSE(readResult.legacy);
  TEST_ASSERT_FALSE(readResult.unsupportedVersion);
  TEST_ASSERT_TRUE(readResult.hasPersistedOracle);
  TEST_ASSERT_EQUAL(metadata.loopId, readResult.metadata.loopId);
  TEST_ASSERT_EQUAL(metadata.startLoopTick, readResult.metadata.startLoopTick);
  TEST_ASSERT_EQUAL(metadata.loopLengthTicks, readResult.metadata.loopLengthTicks);
  TEST_ASSERT_NOT_NULL(readResult.loopSnapshot.get());
  assertSnapshotsEquivalent(*snapshot, *readResult.loopSnapshot);
  TEST_ASSERT_EQUAL(0u, testRebuildSlotFromLoopContentCallCount());
  releasePersistedLoopSnapshotChunks(readResult.persistedOracle);
}

void test_lsv1_stepped_parse_matches_full_read() {
  resetSnapshotAllocCounters();
  const ImmutableLoopSnapshotRef snapshot = buildLsv1RoundTripSnapshotFixture();
  TEST_ASSERT_NOT_NULL(snapshot.get());

  PersistedLoopSnapshot metadata{};
  metadata.loopId = 3;
  metadata.startLoopTick = 480;
  metadata.loopLengthTicks = snapshot->loopLengthTicks();
  metadata.loopStartTick = 12;
  metadata.nextPassId = 8;
  metadata.nextNoteId = snapshot->nextNoteId();
  metadata.nextMergeSequence = 1;
  metadata.lastCommittedPassId = 7;

  PersistedLoopSnapshot persistedOracle{};
  persistedOracle.loopId = metadata.loopId;
  persistedOracle.loopLengthTicks = metadata.loopLengthTicks;
  persistedOracle.loopStartTick = metadata.loopStartTick;
  persistedOracle.nextPassId = metadata.nextPassId;
  persistedOracle.nextNoteId = metadata.nextNoteId;
  persistedOracle.nextMergeSequence = metadata.nextMergeSequence;
  persistedOracle.lastCommittedPassId = metadata.lastCommittedPassId;
  persistedOracle.passes.recordPass =
      makeRecordPassWithEvents(7, 0, CapturePassState::Active, 0, 10);

  std::vector<uint8_t> buffer;
  MemoryStorageIo mem(&buffer);
  TEST_ASSERT_TRUE(
      writeLoopSnapshotBodyFormatV1(mem.io(), metadata, persistedOracle, *snapshot));

  LoopSnapshotBodyFormatV1LoadParseState parseState{};
  PersistedLoopSnapshot steppedOracle{};
  LoopSnapshotBodyFormatV1LoadStepResult stepResult =
      LoopSnapshotBodyFormatV1LoadStepResult::MoreWork;
  while (stepResult == LoopSnapshotBodyFormatV1LoadStepResult::MoreWork) {
    stepResult = stepLoopSnapshotBodyFormatV1LoadParse(buffer.data(), buffer.size(),
                                                       steppedOracle, parseState, 0, 1);
  }
  TEST_ASSERT_EQUAL(LoopSnapshotBodyFormatV1LoadStepResult::Completed, stepResult);

  LoopSnapshotBodyFormatV1ReadResult readResult{};
  TEST_ASSERT_TRUE(readLoopSnapshotBodyFormatV1(buffer.data(), buffer.size(), readResult));
  TEST_ASSERT_TRUE(readResult.ok);
  assertSnapshotsEquivalent(*snapshot, *parseState.loopSnapshot);
  assertSnapshotsEquivalent(*snapshot, *readResult.loopSnapshot);
  TEST_ASSERT_EQUAL(metadata.loopId, steppedOracle.loopId);
  TEST_ASSERT_EQUAL(metadata.startLoopTick, steppedOracle.startLoopTick);
  releasePersistedLoopSnapshotChunks(steppedOracle);
  releasePersistedLoopSnapshotChunks(readResult.persistedOracle);
}

void test_lsv1_payload_byte_slice_quota_and_io_boundaries() {
  resetSnapshotAllocCounters();

  const ImmutableLoopSnapshotRef smallSnapshot = buildLsv1RoundTripSnapshotFixture();
  TEST_ASSERT_NOT_NULL(smallSnapshot.get());
  const size_t smallBytes = testMeasureLoopSnapshotGatheredPayloadBytes(*smallSnapshot);
  TEST_ASSERT_GREATER_THAN(0u, smallBytes);
  TEST_ASSERT_LESS_THAN(kLsv1SaveIoChunkBytes, smallBytes);

  std::vector<uint8_t> smallSliced;
  std::vector<size_t> smallProgress;
  TEST_ASSERT_TRUE(writeGatheredPayloadInByteSlices(*smallSnapshot, kLsv1SaveIoChunkBytes,
                                                    smallSliced, &smallProgress));
  TEST_ASSERT_EQUAL(2u, smallProgress.size());
  TEST_ASSERT_EQUAL(0u, smallProgress.front());
  TEST_ASSERT_EQUAL(smallBytes, smallProgress.back());

  std::vector<uint8_t> smallOracle;
  MemoryStorageIo smallOracleMem(&smallOracle);
  TEST_ASSERT_TRUE(testWriteLoopSnapshotGatheredPayloadToIo(smallOracleMem.io(), *smallSnapshot));
  TEST_ASSERT_EQUAL(smallOracle.size(), smallSliced.size());
  TEST_ASSERT_EQUAL_UINT8_ARRAY(smallOracle.data(), smallSliced.data(), smallOracle.size());

  const size_t crossingNoteCount =
      findNoteCountForPayloadBytesAtLeast(kLsv1SaveIoChunkBytes);
  const ImmutableLoopSnapshotRef crossingSnapshot =
      buildLsv1SnapshotWithNoteCount(crossingNoteCount);
  const size_t crossingBytes = testMeasureLoopSnapshotGatheredPayloadBytes(*crossingSnapshot);
  TEST_ASSERT_GREATER_OR_EQUAL(kLsv1SaveIoChunkBytes, crossingBytes);
  if (crossingNoteCount > 0u) {
    const size_t belowBytes = testMeasureLoopSnapshotGatheredPayloadBytes(
        *buildLsv1SnapshotWithNoteCount(crossingNoteCount - 1u));
    TEST_ASSERT_LESS_THAN(kLsv1SaveIoChunkBytes, belowBytes);
  }
  size_t firstSliceOffset = 0;
  std::vector<uint8_t> crossingFirstSlice;
  MemoryStorageIo crossingFirstSliceMem(&crossingFirstSlice);
  TEST_ASSERT_TRUE(testWriteLoopSnapshotGatheredPayloadByteSlice(
      crossingFirstSliceMem.io(), *crossingSnapshot, firstSliceOffset, kLsv1SaveIoChunkBytes));
  TEST_ASSERT_EQUAL(kLsv1SaveIoChunkBytes, firstSliceOffset);

  const size_t exactNoteCount = findNoteCountForExactPayloadBytes(kLsv1SaveIoChunkBytes);
  if (exactNoteCount != static_cast<size_t>(-1)) {
    const ImmutableLoopSnapshotRef exactSnapshot =
        buildLsv1SnapshotWithNoteCount(exactNoteCount);
    TEST_ASSERT_EQUAL(kLsv1SaveIoChunkBytes,
                      testMeasureLoopSnapshotGatheredPayloadBytes(*exactSnapshot));
    std::vector<uint8_t> exactSliced;
    std::vector<size_t> exactProgress;
    TEST_ASSERT_TRUE(writeGatheredPayloadInByteSlices(*exactSnapshot, kLsv1SaveIoChunkBytes,
                                                      exactSliced, &exactProgress));
    TEST_ASSERT_EQUAL(2u, exactProgress.size());
    TEST_ASSERT_EQUAL(kLsv1SaveIoChunkBytes, exactProgress[1]);
  }

  const size_t largeNoteCount =
      findNoteCountForPayloadBytesOverWithRemainder(kLsv1SaveIoChunkBytes);
  const ImmutableLoopSnapshotRef largeSnapshot =
      buildLsv1SnapshotWithNoteCount(largeNoteCount);
  const size_t largeBytes = testMeasureLoopSnapshotGatheredPayloadBytes(*largeSnapshot);
  TEST_ASSERT_GREATER_THAN(kLsv1SaveIoChunkBytes, largeBytes);

  std::vector<uint8_t> largeSliced;
  std::vector<size_t> largeProgress;
  TEST_ASSERT_TRUE(writeGatheredPayloadInByteSlices(*largeSnapshot, kLsv1SaveIoChunkBytes,
                                                    largeSliced, &largeProgress));
  TEST_ASSERT_GREATER_THAN(2u, largeProgress.size());
  for (size_t index = 1; index < largeProgress.size(); ++index) {
    const size_t delta = largeProgress[index] - largeProgress[index - 1u];
    TEST_ASSERT_GREATER_THAN(0u, delta);
    TEST_ASSERT_LESS_OR_EQUAL(kLsv1SaveIoChunkBytes, delta);
  }
  TEST_ASSERT_NOT_EQUAL(0u, largeBytes % kLsv1SaveIoChunkBytes);
  const size_t finalSliceBytes = largeProgress.back() - largeProgress[largeProgress.size() - 2u];
  TEST_ASSERT_LESS_THAN(kLsv1SaveIoChunkBytes, finalSliceBytes);
  TEST_ASSERT_EQUAL(largeBytes, largeProgress.back());

  std::vector<uint8_t> largeOracle;
  MemoryStorageIo largeOracleMem(&largeOracle);
  TEST_ASSERT_TRUE(testWriteLoopSnapshotGatheredPayloadToIo(largeOracleMem.io(), *largeSnapshot));
  TEST_ASSERT_EQUAL(largeOracle.size(), largeSliced.size());
  TEST_ASSERT_EQUAL_UINT8_ARRAY(largeOracle.data(), largeSliced.data(), largeOracle.size());

  size_t failOffset = 0;
  std::vector<uint8_t> failBuffer;
  FailAfterWriteStorageIo failIo(&failBuffer, 100u);
  TEST_ASSERT_FALSE(testWriteLoopSnapshotGatheredPayloadByteSlice(
      failIo.io(), *largeSnapshot, failOffset, kLsv1SaveIoChunkBytes));
  TEST_ASSERT_EQUAL(0u, failOffset);
  TEST_ASSERT_EQUAL(100u, failBuffer.size());

  size_t zeroQuotaOffset = 0;
  TEST_ASSERT_FALSE(testWriteLoopSnapshotGatheredPayloadByteSlice(
      failIo.io(), *largeSnapshot, zeroQuotaOffset, 0u));
  TEST_ASSERT_EQUAL(0u, zeroQuotaOffset);
}

void test_lsv1_stepped_save_tail_matches_monolithic() {
  resetSnapshotAllocCounters();
  const ImmutableLoopSnapshotRef snapshot = buildLsv1RoundTripSnapshotFixture();
  TEST_ASSERT_NOT_NULL(snapshot.get());

  std::vector<uint8_t> monolithicBuffer;
  MemoryStorageIo monolithicMem(&monolithicBuffer);
  TEST_ASSERT_TRUE(writeLoopSnapshotBodyFormatV1SaveTail(monolithicMem.io(), *snapshot));

  std::vector<uint8_t> steppedBuffer;
  MemoryStorageIo steppedMem(&steppedBuffer);
  LoopSnapshotBodyFormatV1SaveScratch* scratch = acquireLoopSnapshotBodyFormatV1SaveScratch();
  TEST_ASSERT_NOT_NULL(scratch);
  beginLoopSnapshotBodyFormatV1SaveGather(*snapshot, *scratch);
  LoopSnapshotBodyFormatV1SaveStepResult stepResult =
      LoopSnapshotBodyFormatV1SaveStepResult::MoreWork;
  while (stepResult == LoopSnapshotBodyFormatV1SaveStepResult::MoreWork) {
    stepResult =
        stepLoopSnapshotBodyFormatV1SaveTail(steppedMem.io(), *snapshot, *scratch, 0, 1);
  }
  TEST_ASSERT_EQUAL(LoopSnapshotBodyFormatV1SaveStepResult::Completed, stepResult);
  TEST_ASSERT_EQUAL(monolithicBuffer.size(), steppedBuffer.size());
  TEST_ASSERT_EQUAL_UINT8_ARRAY(monolithicBuffer.data(), steppedBuffer.data(),
                                monolithicBuffer.size());
}

void test_lsv1_stepped_save_tail_resumes_write_payload_without_regather() {
  resetSnapshotAllocCounters();
  const size_t noteCount = findNoteCountForPayloadBytesAtLeast(kLsv1SaveIoChunkBytes * 3u);
  const ImmutableLoopSnapshotRef snapshot = buildLsv1SnapshotWithNoteCount(noteCount);
  TEST_ASSERT_NOT_NULL(snapshot.get());

  std::vector<uint8_t> monolithicBuffer;
  MemoryStorageIo monolithicMem(&monolithicBuffer);
  TEST_ASSERT_TRUE(writeLoopSnapshotBodyFormatV1SaveTail(monolithicMem.io(), *snapshot));

  std::vector<uint8_t> steppedBuffer;
  MemoryStorageIo steppedMem(&steppedBuffer);
  LoopSnapshotBodyFormatV1SaveScratch* scratch = acquireLoopSnapshotBodyFormatV1SaveScratch();
  TEST_ASSERT_NOT_NULL(scratch);
  beginLoopSnapshotBodyFormatV1SaveGather(*snapshot, *scratch);

  LoopSnapshotBodyFormatV1SaveStepResult stepResult =
      LoopSnapshotBodyFormatV1SaveStepResult::MoreWork;
  while (stepResult == LoopSnapshotBodyFormatV1SaveStepResult::MoreWork &&
         testLoopSnapshotBodyFormatV1SaveWritePayloadOffset(*scratch) == 0u) {
    stepResult = stepLoopSnapshotBodyFormatV1SaveTail(steppedMem.io(), *snapshot, *scratch, 0, 1);
  }
  TEST_ASSERT_EQUAL(LoopSnapshotBodyFormatV1SaveStepResult::MoreWork, stepResult);
  const uint32_t payloadOffsetAfterFirstSlice =
      testLoopSnapshotBodyFormatV1SaveWritePayloadOffset(*scratch);
  TEST_ASSERT_GREATER_THAN(0u, payloadOffsetAfterFirstSlice);

  while (stepResult == LoopSnapshotBodyFormatV1SaveStepResult::MoreWork) {
    stepResult = stepLoopSnapshotBodyFormatV1SaveTail(steppedMem.io(), *snapshot, *scratch, 0, 1);
  }
  TEST_ASSERT_EQUAL(LoopSnapshotBodyFormatV1SaveStepResult::Completed, stepResult);
  TEST_ASSERT_EQUAL(monolithicBuffer.size(), steppedBuffer.size());
  TEST_ASSERT_EQUAL_UINT8_ARRAY(monolithicBuffer.data(), steppedBuffer.data(),
                                monolithicBuffer.size());
}

void test_lsv1_stepped_save_tail_large_snapshot_matches_monolithic() {
  resetSnapshotAllocCounters();
  const size_t noteCount = findNoteCountForPayloadBytesAtLeast(kLsv1SaveIoChunkBytes + 1u);
  const ImmutableLoopSnapshotRef snapshot = buildLsv1SnapshotWithNoteCount(noteCount);
  TEST_ASSERT_NOT_NULL(snapshot.get());
  TEST_ASSERT_GREATER_THAN(kLsv1SaveIoChunkBytes,
                           testMeasureLoopSnapshotGatheredPayloadBytes(*snapshot));

  std::vector<uint8_t> monolithicBuffer;
  MemoryStorageIo monolithicMem(&monolithicBuffer);
  TEST_ASSERT_TRUE(writeLoopSnapshotBodyFormatV1SaveTail(monolithicMem.io(), *snapshot));

  std::vector<uint8_t> steppedBuffer;
  MemoryStorageIo steppedMem(&steppedBuffer);
  LoopSnapshotBodyFormatV1SaveScratch* scratch = acquireLoopSnapshotBodyFormatV1SaveScratch();
  TEST_ASSERT_NOT_NULL(scratch);
  beginLoopSnapshotBodyFormatV1SaveGather(*snapshot, *scratch);
  LoopSnapshotBodyFormatV1SaveStepResult stepResult =
      LoopSnapshotBodyFormatV1SaveStepResult::MoreWork;
  while (stepResult == LoopSnapshotBodyFormatV1SaveStepResult::MoreWork) {
    stepResult =
        stepLoopSnapshotBodyFormatV1SaveTail(steppedMem.io(), *snapshot, *scratch, 0, 1);
  }
  TEST_ASSERT_EQUAL(LoopSnapshotBodyFormatV1SaveStepResult::Completed, stepResult);
  TEST_ASSERT_EQUAL(monolithicBuffer.size(), steppedBuffer.size());
  TEST_ASSERT_EQUAL_UINT8_ARRAY(monolithicBuffer.data(), steppedBuffer.data(),
                                monolithicBuffer.size());
}

void test_lsv1_load_commit_skips_rebuild() {
  resetSnapshotAllocCounters();
  const ImmutableLoopSnapshotRef snapshot = buildLsv1RoundTripSnapshotFixture();
  TEST_ASSERT_NOT_NULL(snapshot.get());

  PersistedLoopSnapshot metadata{};
  metadata.loopId = 3;
  metadata.loopLengthTicks = snapshot->loopLengthTicks();
  metadata.nextPassId = 8;
  metadata.nextNoteId = snapshot->nextNoteId();

  PersistedLoopSnapshot persistedOracle{};
  persistedOracle.loopId = metadata.loopId;
  persistedOracle.loopLengthTicks = metadata.loopLengthTicks;
  persistedOracle.nextPassId = metadata.nextPassId;
  persistedOracle.nextNoteId = metadata.nextNoteId;
  persistedOracle.passes.recordPass =
      makeRecordPassWithEvents(7, 0, CapturePassState::Active, 0, 10);

  std::vector<uint8_t> buffer;
  MemoryStorageIo mem(&buffer);
  TEST_ASSERT_TRUE(
      writeLoopSnapshotBodyFormatV1(mem.io(), metadata, persistedOracle, *snapshot));

  LoopSnapshotBodyFormatV1LoadParseState parseState{};
  PersistedLoopSnapshot oracle{};
  LoopSnapshotBodyFormatV1LoadStepResult stepResult =
      LoopSnapshotBodyFormatV1LoadStepResult::MoreWork;
  while (stepResult == LoopSnapshotBodyFormatV1LoadStepResult::MoreWork) {
    stepResult = stepLoopSnapshotBodyFormatV1LoadParse(buffer.data(), buffer.size(), oracle,
                                                       parseState, 0, 1);
  }
  TEST_ASSERT_EQUAL(LoopSnapshotBodyFormatV1LoadStepResult::Completed, stepResult);

  Loop loop;
  alignas(16) static unsigned char trackStorage[16] = {};
  Track& track = *reinterpret_cast<Track*>(trackStorage);

  testResetRebuildSlotFromLoopContentCallCount();
  ImmutableLoopSnapshotRef loadedSnapshot = parseState.loopSnapshot;
  parseState.loopSnapshot = nullptr;
  applyLoadedLoopSlotBodyCommit(loop, track, 0, true, oracle, std::move(loadedSnapshot));
  TEST_ASSERT_NOT_NULL(loop.publishedSnapshot().get());
  TEST_ASSERT_TRUE(loop.publishedSnapshotIsCurrent());
  TEST_ASSERT_EQUAL(0u, testRebuildSlotFromLoopContentCallCount());

  testResetRebuildSlotFromLoopContentCallCount();
  PersistedLoopSnapshot legacyOracle = metadata;
  legacyOracle.passes.recordPass =
      makeRecordPassWithEvents(7, 0, CapturePassState::Active, 0, 10);
  applyLoadedLoopSlotBodyCommit(loop, track, 0, false, legacyOracle, nullptr);
  TEST_ASSERT_EQUAL(1u, testRebuildSlotFromLoopContentCallCount());
  releasePersistedLoopSnapshotChunks(legacyOracle);
}

void test_lsv1_probe_legacy_and_unsupported() {
  PersistedLoopSnapshot legacy{};
  legacy.loopId = 1;
  legacy.loopLengthTicks = Config::TICKS_PER_BAR;
  legacy.nextPassId = 2;
  std::vector<uint8_t> buffer;
  MemoryStorageIo mem(&buffer);
  TEST_ASSERT_TRUE(writePersistedLoopSnapshot(mem.io(), legacy));
  TEST_ASSERT_EQUAL(LoopSnapshotBodyFormatProbeResult::Legacy,
                    probeLoopSnapshotBodyFormat(buffer.data(), buffer.size()));

  std::vector<uint8_t> unsupported = {'L', 'S', 'V', '1', 2, 0, 0, 0};
  TEST_ASSERT_EQUAL(LoopSnapshotBodyFormatProbeResult::UnsupportedVersion,
                    probeLoopSnapshotBodyFormat(unsupported.data(), unsupported.size()));

  std::vector<uint8_t> truncated = {'L', 'S', 'V'};
  TEST_ASSERT_EQUAL(LoopSnapshotBodyFormatProbeResult::Truncated,
                    probeLoopSnapshotBodyFormat(truncated.data(), truncated.size()));
}

void test_lsv1_rejects_missing_loop_snapshot_section() {
  const ImmutableLoopSnapshotRef snapshot =
      LoopSnapshot::createEmpty(1, Config::TICKS_PER_BAR, 0, 2, 2);
  PersistedLoopSnapshot metadata{};
  metadata.loopId = 1;
  metadata.loopLengthTicks = Config::TICKS_PER_BAR;
  metadata.nextPassId = 2;
  PersistedLoopSnapshot persistedOracle = metadata;

  std::vector<uint8_t> buffer;
  MemoryStorageIo mem(&buffer);
  TEST_ASSERT_TRUE(
      writeLoopSnapshotBodyFormatV1(mem.io(), metadata, persistedOracle, *snapshot));

  LoopSnapshotBodyFormatV1ReadResult parsed{};
  TEST_ASSERT_TRUE(readLoopSnapshotBodyFormatV1(buffer.data(), buffer.size(), parsed));
  releasePersistedLoopSnapshotChunks(parsed.persistedOracle);

  for (size_t index = 0; index < buffer.size(); ++index) {
    if (buffer[index] == 'L' && index + 11 < buffer.size() &&
        std::memcmp(buffer.data() + index, "LoopSnapshot", 12) == 0) {
      buffer[index] = 'X';
      break;
    }
  }

  LoopSnapshotBodyFormatV1ReadResult rejected{};
  TEST_ASSERT_FALSE(readLoopSnapshotBodyFormatV1(buffer.data(), buffer.size(), rejected));
}

void test_lsv1_unknown_section_is_skipped() {
  const ImmutableLoopSnapshotRef snapshot =
      LoopSnapshot::createEmpty(1, Config::TICKS_PER_BAR, 0, 2, 2);
  PersistedLoopSnapshot metadata{};
  metadata.loopId = 1;
  metadata.loopLengthTicks = Config::TICKS_PER_BAR;
  metadata.nextPassId = 2;
  PersistedLoopSnapshot persistedOracle = metadata;

  std::vector<uint8_t> buffer;
  MemoryStorageIo mem(&buffer);
  TEST_ASSERT_TRUE(
      writeLoopSnapshotBodyFormatV1(mem.io(), metadata, persistedOracle, *snapshot));

  std::vector<uint8_t> withUnknown;
  withUnknown.insert(withUnknown.end(), buffer.begin(), buffer.begin() + 8);
  const char* unknownName = "FutureSection";
  const uint8_t unknownNameLength = static_cast<uint8_t>(std::strlen(unknownName));
  withUnknown.push_back(unknownNameLength);
  withUnknown.insert(withUnknown.end(), unknownName, unknownName + unknownNameLength);
  const uint32_t unknownPayloadLength = 4;
  appendRaw(withUnknown, unknownPayloadLength);
  withUnknown.insert(withUnknown.end(), {1, 2, 3, 4});
  withUnknown.insert(withUnknown.end(), buffer.begin() + 8, buffer.end());

  LoopSnapshotBodyFormatV1ReadResult readResult{};
  TEST_ASSERT_TRUE(
      readLoopSnapshotBodyFormatV1(withUnknown.data(), withUnknown.size(), readResult));
  TEST_ASSERT_TRUE(readResult.ok);
  releasePersistedLoopSnapshotChunks(readResult.persistedOracle);
}

void drainPendingSnapshotPublish(Loop& loop) {
  size_t steps = 0;
  while (loop.hasPendingSnapshotPublish() && steps < 10000) {
    loop.stepPendingSnapshotPublish(1);
    ++steps;
  }
  TEST_ASSERT_FALSE(loop.hasPendingSnapshotPublish());
}

void test_write_loop_persisted_emits_lsv1_when_published_snapshot_current() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  resetSnapshotAllocCounters();

  Loop loop;
  loop.loopId = 3;
  loop.loopLengthTicks = 2u * Config::TICKS_PER_BAR;
  loop.beginCapture(CapturePhase::Record);
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(100, 1, 60, 100)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(200, 1, 60, 0)));
  TEST_ASSERT_EQUAL(CommitResult::Committed,
                    loop.commitCapturePass(CommitReason::RecordStop, 200));
  drainPendingSnapshotPublish(loop);
  TEST_ASSERT_TRUE(loop.publishedSnapshotIsCurrent());
  TEST_ASSERT_TRUE(loopUsesLoopSnapshotBodyFormatV1ForSave(loop));

  std::vector<uint8_t> buffer;
  MemoryStorageIo mem(&buffer);
  TEST_ASSERT_TRUE(writeLoopPersisted(mem.io(), loop));
  TEST_ASSERT_EQUAL(buffer.size(), measureLoopSlotFileBytes(loop));
  TEST_ASSERT_EQUAL(LoopSnapshotBodyFormatProbeResult::SupportedV1,
                    probeLoopSnapshotBodyFormat(buffer.data(), buffer.size()));

  LoopSnapshotBodyFormatV1ReadResult readResult{};
  TEST_ASSERT_TRUE(readLoopSnapshotBodyFormatV1(buffer.data(), buffer.size(), readResult));
  TEST_ASSERT_TRUE(readResult.ok);
  TEST_ASSERT_NOT_NULL(readResult.loopSnapshot.get());
  releasePersistedLoopSnapshotChunks(readResult.persistedOracle);
}

void test_legacy_slot_migrates_to_lsv1_after_loaded_hydration() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  resetSnapshotAllocCounters();

  Loop source;
  source.loopId = 5;
  source.loopLengthTicks = Config::TICKS_PER_BAR;
  source.beginCapture(CapturePhase::Record);
  TEST_ASSERT_TRUE(source.appendCaptureEvent(MidiEvent::NoteOn(50, 1, 64, 100)));
  TEST_ASSERT_EQUAL(CommitResult::Committed,
                    source.commitCapturePass(CommitReason::RecordStop, 100));
  TEST_ASSERT_FALSE(source.publishedSnapshotIsCurrent());
  TEST_ASSERT_FALSE(loopUsesLoopSnapshotBodyFormatV1ForSave(source));

  std::vector<uint8_t> legacyBuffer;
  MemoryStorageIo legacyMem(&legacyBuffer);
  TEST_ASSERT_TRUE(writeLoopPersisted(legacyMem.io(), source));
  TEST_ASSERT_EQUAL(LoopSnapshotBodyFormatProbeResult::Legacy,
                    probeLoopSnapshotBodyFormat(legacyBuffer.data(), legacyBuffer.size()));

  Loop loaded;
  MemoryStorageIo readMem(&legacyBuffer);
  readMem.resetRead();
  TEST_ASSERT_TRUE(readLoopPersisted(readMem.io(), loaded));
  TEST_ASSERT_FALSE(loaded.publishedSnapshotIsCurrent());
  TEST_ASSERT_FALSE(loopUsesLoopSnapshotBodyFormatV1ForSave(loaded));

  TEST_ASSERT_TRUE(loaded.queueLoadedSnapshotPublish());
  drainPendingSnapshotPublish(loaded);
  TEST_ASSERT_TRUE(loaded.publishedSnapshotIsCurrent());
  TEST_ASSERT_TRUE(loopUsesLoopSnapshotBodyFormatV1ForSave(loaded));

  std::vector<uint8_t> migratedBuffer;
  MemoryStorageIo migratedMem(&migratedBuffer);
  TEST_ASSERT_TRUE(writeLoopPersisted(migratedMem.io(), loaded));
  TEST_ASSERT_EQUAL(migratedBuffer.size(), measureLoopSlotFileBytes(loaded));
  TEST_ASSERT_EQUAL(LoopSnapshotBodyFormatProbeResult::SupportedV1,
                    probeLoopSnapshotBodyFormat(migratedBuffer.data(), migratedBuffer.size()));

  LoopSnapshotBodyFormatV1ReadResult readResult{};
  TEST_ASSERT_TRUE(readLoopSnapshotBodyFormatV1(migratedBuffer.data(), migratedBuffer.size(),
                                                readResult));
  TEST_ASSERT_TRUE(readResult.ok);
  TEST_ASSERT_NOT_NULL(readResult.loopSnapshot.get());
  releasePersistedLoopSnapshotChunks(readResult.persistedOracle);
}

void test_write_loop_persisted_stays_legacy_without_published_snapshot() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();

  Loop loop;
  loop.loopId = 4;
  loop.loopLengthTicks = Config::TICKS_PER_BAR;
  loop.beginCapture(CapturePhase::Record);
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(50, 1, 64, 100)));
  TEST_ASSERT_EQUAL(CommitResult::Committed,
                    loop.commitCapturePass(CommitReason::RecordStop, 100));
  TEST_ASSERT_FALSE(loop.publishedSnapshotIsCurrent());
  TEST_ASSERT_FALSE(loopUsesLoopSnapshotBodyFormatV1ForSave(loop));

  std::vector<uint8_t> buffer;
  MemoryStorageIo mem(&buffer);
  TEST_ASSERT_TRUE(writeLoopPersisted(mem.io(), loop));
  TEST_ASSERT_EQUAL(LoopSnapshotBodyFormatProbeResult::Legacy,
                    probeLoopSnapshotBodyFormat(buffer.data(), buffer.size()));
}

/// DEC-073 RC2: the LSV1 body may embed the selected root only when the publisher is *settled*.
/// A coverage-current root with a publication still pending is not settled -- the save must take
/// the passes-authoritative (legacy) body, which the loader rebuilds from the passes. Pre-RC2 this
/// predicate accepted the pending root, and a mid-write committed-state change could leave an LSV1
/// preamble on disk with no snapshot tail.
void test_write_loop_persisted_requires_a_settled_published_snapshot() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();

  Loop loop;
  loop.loopId = 5;
  loop.loopLengthTicks = 2u * Config::TICKS_PER_BAR;
  loop.beginCapture(CapturePhase::Record);
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(100, 1, 60, 100)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(200, 1, 60, 0)));
  TEST_ASSERT_EQUAL(CommitResult::Committed,
                    loop.commitCapturePass(CommitReason::RecordStop, 200));
  drainPendingSnapshotPublish(loop);
  TEST_ASSERT_TRUE(loop.publishedSnapshotIsCurrent());
  TEST_ASSERT_TRUE(loopUsesLoopSnapshotBodyFormatV1ForSave(loop));

  // A republish of the same committed state leaves the root coverage-current but the publisher
  // unsettled.
  TEST_ASSERT_TRUE(loop.requestCommittedPassesRepublish());
  TEST_ASSERT_TRUE(loop.hasPendingSnapshotPublish());
  TEST_ASSERT_TRUE(loop.publishedSnapshotIsCurrent());
  // The pre-RC2 predicate (`snapshot && publishedSnapshotIsCurrent()`) is still true here, which is
  // exactly what this fixture pins as insufficient.
  TEST_ASSERT_TRUE(loop.publishedSnapshot() != nullptr && loop.publishedSnapshotIsCurrent());
  TEST_ASSERT_FALSE_MESSAGE(loopUsesLoopSnapshotBodyFormatV1ForSave(loop),
      "an LSV1 body may embed the selected root only when the publisher is settled");
  TEST_ASSERT_FALSE(loop.hasSettledPublishedSnapshotForSave());

  std::vector<uint8_t> buffer;
  MemoryStorageIo mem(&buffer);
  TEST_ASSERT_TRUE(writeLoopPersisted(mem.io(), loop));
  TEST_ASSERT_EQUAL_MESSAGE(LoopSnapshotBodyFormatProbeResult::Legacy,
                            probeLoopSnapshotBodyFormat(buffer.data(), buffer.size()),
                            "an unsettled root must persist the passes-authoritative body");

  drainPendingSnapshotPublish(loop);
  TEST_ASSERT_TRUE(loop.hasSettledPublishedSnapshotForSave());
  TEST_ASSERT_TRUE(loopUsesLoopSnapshotBodyFormatV1ForSave(loop));
}

/// DEC-073 RC2: the body-format decision waits for a transient pending publication only within its
/// bounded settle allowance, then commits to the passes-authoritative body. A stale (uncovered)
/// root is never transient, so it never waits.
void test_loop_persist_body_decision_awaits_settlement_within_its_budget() {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();

  Loop loop;
  loop.loopId = 6;
  loop.loopLengthTicks = 2u * Config::TICKS_PER_BAR;
  loop.beginCapture(CapturePhase::Record);
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOn(120, 1, 62, 100)));
  TEST_ASSERT_TRUE(loop.appendCaptureEvent(MidiEvent::NoteOff(220, 1, 62, 0)));
  TEST_ASSERT_EQUAL(CommitResult::Committed,
                    loop.commitCapturePass(CommitReason::RecordStop, 220));
  drainPendingSnapshotPublish(loop);
  TEST_ASSERT_EQUAL(LoopPersistBodyDecision::Lsv1SelectedRoot,
                    decideLoopPersistBodyFormat(loop, false));

  TEST_ASSERT_TRUE(loop.requestCommittedPassesRepublish());
  TEST_ASSERT_EQUAL(LoopPersistBodyDecision::AwaitSettled,
                    decideLoopPersistBodyFormat(loop, true));
  TEST_ASSERT_EQUAL(LoopPersistBodyDecision::PassesAuthoritative,
                    decideLoopPersistBodyFormat(loop, false));

  drainPendingSnapshotPublish(loop);
  // Once settled, even an exhausted budget resolves to the selected root.
  TEST_ASSERT_EQUAL(LoopPersistBodyDecision::Lsv1SelectedRoot,
                    decideLoopPersistBodyFormat(loop, false));

  // An uncovered root is not transient: no budget may make the save wait for it.
  ++loop.playbackRevision;
  TEST_ASSERT_FALSE(loop.publishedSnapshotIsCurrent());
  TEST_ASSERT_EQUAL(LoopPersistBodyDecision::PassesAuthoritative,
                    decideLoopPersistBodyFormat(loop, true));
}

void test_lsv1_rejects_duplicate_loop_snapshot_section() {
  const ImmutableLoopSnapshotRef snapshot =
      LoopSnapshot::createEmpty(1, Config::TICKS_PER_BAR, 0, 2, 2);
  PersistedLoopSnapshot metadata{};
  metadata.loopId = 1;
  metadata.loopLengthTicks = Config::TICKS_PER_BAR;
  metadata.nextPassId = 2;
  PersistedLoopSnapshot persistedOracle = metadata;

  std::vector<uint8_t> buffer;
  MemoryStorageIo mem(&buffer);
  TEST_ASSERT_TRUE(
      writeLoopSnapshotBodyFormatV1(mem.io(), metadata, persistedOracle, *snapshot));
  const std::vector<uint8_t> original = buffer;
  buffer.insert(buffer.end(), original.begin() + 8, original.end());

  LoopSnapshotBodyFormatV1ReadResult rejected{};
  TEST_ASSERT_FALSE(readLoopSnapshotBodyFormatV1(buffer.data(), buffer.size(), rejected));
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

void test_persisted_snapshot_release_returns_chunks_in_bounded_units() {
  for (const size_t quantum : {size_t(1), size_t(7), size_t(32)}) {
    LoopEventStore::resetPoolForTests();
    auto snapshot = std::make_shared<PersistedLoopSnapshot>();
    snapshot->passes.recordPass.id = 1;
    LoopEventStore record;
    for (size_t i = 0; i < 257; ++i) {
      TEST_ASSERT_TRUE(record.append(MidiEvent::ControlChange(i, 1, 7, 64)));
    }
    TEST_ASSERT_TRUE(transferCaptureStoreToCommittedChunkIds(
        record, snapshot->passes.recordPass.committedChunkIds));
    OverdubPass overdub;
    overdub.id = 2;
    LoopEventStore capture;
    TEST_ASSERT_TRUE(capture.append(MidiEvent::ControlChange(2, 1, 7, 64)));
    TEST_ASSERT_TRUE(transferCaptureStoreToCommittedChunkIds(capture, overdub.committedChunkIds));
    CommittedChunkIdList external;
    TEST_ASSERT_TRUE(LoopEventStore::deepCloneCommittedChunkIds(external, overdub.committedChunkIds));
    const uint16_t externalChunk = external.front();
    snapshot->passes.overdubPasses.push_back(std::move(overdub));
    EditPass edit;
    edit.addedEvents.resize(45);
    snapshot->passes.editPasses.push_back(std::move(edit));
    snapshot->passes.loopGeometries.resize(3);
    TEST_ASSERT_EQUAL(4u, LoopEventStore::usedChunkCount());
    std::weak_ptr<PersistedLoopSnapshot> lifetime = snapshot;
    PersistedLoopSnapshotReleaseCursor release;
    TEST_ASSERT_TRUE(release.begin(snapshot));
    TEST_ASSERT_NULL(snapshot.get());
    TEST_ASSERT_FALSE(release.step(0));
    TEST_ASSERT_EQUAL(0u, release.completedUnits());
    auto other = std::make_shared<PersistedLoopSnapshot>();
    TEST_ASSERT_FALSE(release.begin(other));
    TEST_ASSERT_NOT_NULL(other.get());
    size_t turns = 0;
    while (!release.complete() && turns++ < 1000) {
      const size_t before = release.completedUnits();
      const uint16_t chunksBefore = LoopEventStore::usedChunkCount();
      release.step(quantum);
      TEST_ASSERT_TRUE(release.completedUnits() - before <= quantum);
      TEST_ASSERT_TRUE(chunksBefore - LoopEventStore::usedChunkCount() <= quantum);
    }
    TEST_ASSERT_TRUE(release.complete());
    TEST_ASSERT_TRUE(lifetime.expired());
    TEST_ASSERT_EQUAL(1u, LoopEventStore::usedChunkCount());
    TEST_ASSERT_EQUAL(1u, LoopEventStore::chunkReferenceCount(externalChunk));
    TEST_ASSERT_EQUAL(1u, LoopEventStore::chunkUsedCount(externalChunk));
    LoopEventStore::releaseChunkRefs(external);
    external.clear();
    TEST_ASSERT_EQUAL(0u, LoopEventStore::usedChunkCount());
  }
}

void test_persisted_snapshot_release_leaves_shared_owner_unchanged() {
  auto snapshot = std::make_shared<PersistedLoopSnapshot>();
  snapshot->loopId = 7;
  EditPass edit;
  edit.addedEvents.resize(3);
  snapshot->passes.editPasses.push_back(std::move(edit));
  auto reader = snapshot;
  PersistedLoopSnapshotReleaseCursor release;
  TEST_ASSERT_TRUE(release.begin(snapshot));
  TEST_ASSERT_TRUE(release.step(1));
  TEST_ASSERT_EQUAL(1u, release.completedUnits());
  TEST_ASSERT_EQUAL(7u, reader->loopId);
  TEST_ASSERT_EQUAL(3u, reader->passes.editPasses.front().addedEvents.size());
  TEST_ASSERT_TRUE(release.begin(reader));
  while (!release.step(1)) {}
}

void test_bounded_undo_trim_rechecks_pressure_after_final_root_release() {
  for (const size_t quantum : {size_t(1), size_t(7), size_t(32)}) {
    LoopEventStore::resetPoolForTests();
    MemoryMonitor::setNativeTestFreeHeap(Config::HEAP_RESERVE_BYTES + 1024);
    GlobalUndoStack stack;
    bool released = false;
    for (size_t i = 0; i < Config::MIN_UNDO_DEPTH + 3; ++i) {
      UndoEntry entry;
      entry.id = i + 1;
      entry.passId = i + 1;
      if (i == 0) {
        entry.beforePublishedSnapshot = ImmutableLoopSnapshotRef(new LoopSnapshot(),
            [&released](const LoopSnapshot* root) {
              delete root;
              released = true;
              MemoryMonitor::setNativeTestFreeHeap(Config::HEAP_RESERVE_BYTES + 1024);
            });
        // Two references in the retired entry must both drain before feedback.
        entry.afterPublishedSnapshot = entry.beforePublishedSnapshot;
        entry.passIds.resize(7, 1);
        entry.editPassIds.resize(9, 2);
      }
      stack.entries.push_back(std::move(entry));
    }
    stack.cursor = stack.entries.size();
    const auto* storage = stack.entries.data();
    MemoryMonitor::setNativeTestFreeHeap(Config::HEAP_RESERVE_BYTES - 1);
    GlobalUndoStackTrimCursor trim;
    TEST_ASSERT_TRUE(trim.begin(stack));
    TEST_ASSERT_FALSE(trim.begin(stack));
    TEST_ASSERT_FALSE(trim.step(0));
    size_t turns = 0;
    while (!trim.complete() && turns++ < 10000) {
      const auto before = trim.completedUnits();
      trim.step(quantum);
      TEST_ASSERT_TRUE(trim.completedUnits() - before <= quantum);
      TEST_ASSERT_EQUAL_PTR(storage, stack.entries.data());
    }
    TEST_ASSERT_TRUE(trim.complete());
    TEST_ASSERT_TRUE(released);
    TEST_ASSERT_EQUAL(1u, trim.trimmedCount());
    TEST_ASSERT_EQUAL(Config::MIN_UNDO_DEPTH + 2, stack.entries.size());
    TEST_ASSERT_EQUAL(stack.entries.size(), stack.cursor);
    TEST_ASSERT_EQUAL(2u, stack.entries.front().id);
    MemoryMonitor::resetNativeTestFreeHeap();
  }
}

void test_bounded_undo_trim_matches_depth_and_redo_reference() {
  for (const bool pressure : {false, true}) {
    for (const size_t count : {size_t(0), size_t(3), size_t(11),
                              size_t(Config::ABSOLUTE_MAX_UNDO_ENTRIES + 2)}) {
      for (const size_t cursor : {size_t(0), count / 2, count}) {
        LoopEventStore::resetPoolForTests();
        MemoryMonitor::setNativeTestFreeHeap(Config::HEAP_RESERVE_BYTES + (pressure ? -1 : 1024));
        GlobalUndoStack reference, bounded;
        for (size_t i = 0; i < count; ++i) {
          UndoEntry entry;
          entry.id = i + 1;
          reference.entries.push_back(entry);
          bounded.entries.push_back(std::move(entry));
        }
        reference.cursor = bounded.cursor = cursor;
        const size_t expected = trimGlobalUndoStackForMemory(reference);
        GlobalUndoStackTrimCursor trim;
        TEST_ASSERT_TRUE(trim.begin(bounded));
        size_t turns = 0;
        while (!trim.step(32) && turns++ < 100000) {}
        TEST_ASSERT_TRUE(trim.complete());
        TEST_ASSERT_EQUAL(expected, trim.trimmedCount());
        TEST_ASSERT_EQUAL(reference.cursor, bounded.cursor);
        TEST_ASSERT_EQUAL(reference.entries.size(), bounded.entries.size());
        for (size_t i = 0; i < reference.entries.size(); ++i) {
          TEST_ASSERT_EQUAL(reference.entries[i].id, bounded.entries[i].id);
        }
        MemoryMonitor::resetNativeTestFreeHeap();
      }
    }
  }
}

void test_undo_publication_retains_redo_until_commit_and_defers_latest_command() {
  MemoryMonitor::setNativeTestFreeHeap(Config::HEAP_RESERVE_BYTES + 1024);
  for (size_t quantum : {size_t(1), size_t(7), size_t(32)}) {
    GlobalUndoStack stack;
    stack.entries.reserve(8);
    bool redoReleased = false;
    for (size_t i = 0; i < 4; ++i) {
      UndoEntry row; row.id = i + 1; row.passId = i + 1;
      if (i == 3) row.beforePublishedSnapshot = ImmutableLoopSnapshotRef(new LoopSnapshot(),
          [&redoReleased](const LoopSnapshot* root) { delete root; redoReleased = true; });
      stack.entries.push_back(std::move(row));
    }
    stack.cursor = 2; stack.nextEntryId = 5;
    UndoEntry entry; entry.kind = UndoEntryKind::OverdubPassAdded;
    entry.loopId = 7; entry.passId = 9; entry.slotIndex = 1;
    TrackUndoPublication work;
    TEST_ASSERT_FALSE(work.begin(stack, entry)); // No retired-row reservation.
    TEST_ASSERT_TRUE(work.prewarm(2));
    TEST_ASSERT_TRUE(work.begin(stack, entry));
    TEST_ASSERT_TRUE(work.defer(TrackUndoPublication::Command::Undo, 1, 7));
    TEST_ASSERT_TRUE(work.defer(TrackUndoPublication::Command::Redo, 1, 7));
    TEST_ASSERT_FALSE(work.defer(TrackUndoPublication::Command::None, 1, 7));
    TEST_ASSERT_FALSE(work.defer(static_cast<TrackUndoPublication::Command>(255), 1, 7));
    TEST_ASSERT_EQUAL(TrackUndoPublication::Command::None, work.takePendingCommand().kind);
    TEST_ASSERT_FALSE(work.stepPrepare(0));
    while (!work.stepPrepare(quantum)) {
      TEST_ASSERT_FALSE(redoReleased);
      TEST_ASSERT_EQUAL(2u, work.visibleUndoCount());
      TEST_ASSERT_EQUAL(2u, work.visibleRedoCount());
    }
    TEST_ASSERT_FALSE(redoReleased);
    TEST_ASSERT_FALSE(work.canPublish(stack, 8, 9));
    TEST_ASSERT_FALSE(work.canPublish(stack, 7, 10));
    TEST_ASSERT_TRUE(work.canPublish(stack, 7, 9));
    work.publishAtCommit();
    TEST_ASSERT_EQUAL(5u, stack.entries.back().id);
    TEST_ASSERT_EQUAL(3u, stack.cursor);
    TEST_ASSERT_EQUAL(6u, stack.nextEntryId);
    TEST_ASSERT_FALSE(work.beginRestore());
    TEST_ASSERT_FALSE(work.beginTrimAfterReclaim());
    size_t turns = 0;
    while (!work.stepReleaseRedo(quantum) && turns++ < 1000) {}
    TEST_ASSERT_TRUE(redoReleased);
    TEST_ASSERT_EQUAL(TrackUndoPublication::Phase::AwaitReclaim, work.phase());
    TEST_ASSERT_TRUE(work.protectedState());
    TEST_ASSERT_TRUE(work.beginTrimAfterReclaim());
    while (!work.stepTrim(quantum)) {}
    TEST_ASSERT_TRUE(work.finish());
    TEST_ASSERT_FALSE(work.protectedState());
    auto pending = work.takePendingCommand();
    TEST_ASSERT_EQUAL(TrackUndoPublication::Command::Redo, pending.kind);
    TEST_ASSERT_EQUAL(1u, pending.slot);
    TEST_ASSERT_EQUAL(7u, pending.loopId);
    TEST_ASSERT_EQUAL(TrackUndoPublication::Command::None, work.takePendingCommand().kind);
  }
  MemoryMonitor::resetNativeTestFreeHeap();
}

void test_undo_publication_precommit_restore_preserves_resources_and_entry_buffers() {
  for (size_t moved : {size_t(0), size_t(1), size_t(2), size_t(3)}) {
    GlobalUndoStack stack;
    stack.entries.reserve(5);
    bool released = false;
    for (size_t i = 0; i < 4; ++i) {
      UndoEntry row; row.id = i + 1;
      if (i == 3) row.beforePublishedSnapshot = ImmutableLoopSnapshotRef(new LoopSnapshot(),
          [&released](const LoopSnapshot* root) { delete root; released = true; });
      stack.entries.push_back(std::move(row));
    }
    stack.cursor = 2; stack.nextEntryId = 5;
    UndoEntry entry; entry.kind = UndoEntryKind::OverdubPassAdded;
    entry.loopId = 7; entry.passId = 9;
    entry.passIds = {8,9}; entry.editPassIds = {10,11};
    const auto* passStorage = entry.passIds.data();
    const auto* editStorage = entry.editPassIds.data();
    TrackUndoPublication work;
    TEST_ASSERT_TRUE(work.prewarm(2));
    TEST_ASSERT_TRUE(work.begin(stack, entry));
    work.stepPrepare(moved);
    TEST_ASSERT_FALSE(released);
    TEST_ASSERT_TRUE(work.beginRestore());
    while (!work.stepRestore(1)) {}
    TEST_ASSERT_FALSE(released);
    TEST_ASSERT_FALSE(work.finish()); // Recovery must retain the new private entry.
    UndoEntry recovered;
    TEST_ASSERT_TRUE(work.recoverPreparedEntry(recovered));
    TEST_ASSERT_EQUAL_PTR(passStorage, recovered.passIds.data());
    TEST_ASSERT_EQUAL_PTR(editStorage, recovered.editPassIds.data());
    TEST_ASSERT_TRUE(work.finish());
    TEST_ASSERT_EQUAL(4u, stack.entries.size());
    TEST_ASSERT_EQUAL(2u, stack.cursor);
    TEST_ASSERT_EQUAL(5u, stack.nextEntryId);
    for (size_t i = 0; i < 4; ++i) TEST_ASSERT_EQUAL(i + 1, stack.entries[i].id);
    stack.clear();
    TEST_ASSERT_TRUE(released);
  }
}

#include "../../scripts/native_slot_payload_decode.inc"

void test_diagnostic_decoder_uses_production_lsv1_root() {
  const auto root = buildLsv1RoundTripSnapshotFixture();
  PersistedLoopSnapshot metadata{};
  metadata.loopLengthTicks = root->loopLengthTicks();
  PersistedLoopSnapshot oracle{};
  std::vector<uint8_t> bytes;
  MemoryStorageIo io(&bytes);
  TEST_ASSERT_TRUE(writeLoopSnapshotBodyFormatV1(io.io(), metadata, oracle, *root));
  DecodedSlotBody decoded;
  TEST_ASSERT_TRUE(decodeSlotBody(bytes, decoded));
  TEST_ASSERT_TRUE(decoded.lsv1);
  DecodedSlotBody targetDecoded;
  TEST_ASSERT_FALSE(decodeSlotBody(bytes, targetDecoded, true));
  SessionMidiEventVec expected;
  LoopContentResolution::resolveWindow(*root, 0, root->loopLengthTicks(), expected);
  TEST_ASSERT_EQUAL(expected.size(), decoded.events.size());
  TEST_ASSERT_GREATER_THAN_UINT32(0, decoded.events.size());
  for (size_t i = 0; i < expected.size(); ++i) {
    TEST_ASSERT_EQUAL(expected[i].tick, decoded.events[i].tick);
    TEST_ASSERT_EQUAL(expected[i].noteId, decoded.events[i].noteId);
    TEST_ASSERT_EQUAL(expected[i].type, decoded.events[i].type);
    TEST_ASSERT_EQUAL(expected[i].data.noteData.note, decoded.events[i].data.noteData.note);
  }
  bytes.resize(8);
  TEST_ASSERT_FALSE(decodeSlotBody(bytes, decoded));
}

void test_diagnostic_decoder_uses_production_legacy_passes() {
  PersistedLoopSnapshot snapshot{};
  snapshot.loopLengthTicks = 768;
  snapshot.passes.recordPass = makeRecordPassWithEvents(7, 0, CapturePassState::Active, 0, 10);
  std::vector<uint8_t> bytes;
  MemoryStorageIo io(&bytes);
  TEST_ASSERT_TRUE(writePersistedLoopSnapshot(io.io(), snapshot));
  DecodedSlotBody decoded;
  TEST_ASSERT_TRUE(decodeSlotBody(bytes, decoded));
  TEST_ASSERT_FALSE(decoded.lsv1);
  TEST_ASSERT_EQUAL(768, decoded.length);
  TEST_ASSERT_EQUAL(1, decoded.events.size());
  bytes.resize(1);
  TEST_ASSERT_FALSE(decodeSlotBody(bytes, decoded));
  releasePersistedLoopSnapshotChunks(snapshot);
}

void test_diagnostic_decoder_reads_teensy_legacy_wire_events() {
  LoopEventStore::initPool();
  std::vector<uint8_t> bytes;
  for (uint32_t value : {2u, 0u, 768u, 0u, 2u, 8u, 1u, 1u, 1u, 1u, 0u})
    appendRaw(bytes, value);
  appendRaw(bytes, uint8_t{0}); // pass state
  appendRaw(bytes, uint8_t{0}); // record pass
  appendRaw(bytes, uint32_t{768});
  appendRaw(bytes, uint32_t{1});
  const uint8_t event[20] = {16,0,0,0, 0x90,6,0,0, 7,0,0,0, 60,100,0,0, 0,0,0,0};
  bytes.insert(bytes.end(), event, event + sizeof(event));
  MemoryStorageIo tail(&bytes);
  TEST_ASSERT_TRUE(writePersistedEditsTail(tail.io(), 2, EditPassVec{}, LoopGeometryVec{}, CommittedOverdubPassVec{}));
  DecodedSlotBody decoded;
  TEST_ASSERT_TRUE(decodeSlotBody(bytes, decoded, true));
  TEST_ASSERT_EQUAL(1, decoded.events.size());
  TEST_ASSERT_EQUAL(16, decoded.events[0].tick);
  TEST_ASSERT_EQUAL(7, decoded.events[0].noteId);
  TEST_ASSERT_EQUAL(6, decoded.events[0].channel);
  TEST_ASSERT_EQUAL(60, decoded.events[0].data.noteData.note);
  bytes.pop_back();
  TEST_ASSERT_FALSE(decodeSlotBody(bytes, decoded, true));
}

void test_diagnostic_decoder_reads_captured_teensy_lsv1() {
  const std::string source=__FILE__;
  std::ifstream file(source.substr(0,source.find_last_of("/\\")+1)+"fixtures/s13_teensy_lsv1.bin",std::ios::binary);
  TEST_ASSERT_TRUE(file.good());
  std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(file)),std::istreambuf_iterator<char>());
  LoopEventStore::initPool();
  DecodedSlotBody decoded;
  TEST_ASSERT_TRUE(decodeSlotBody(bytes,decoded,true));
  TEST_ASSERT_TRUE(decoded.lsv1);
  TEST_ASSERT_EQUAL(1536,decoded.length);
  TEST_ASSERT_EQUAL(62,decoded.events.size());
  TEST_ASSERT_EQUAL(8,decoded.events[0].tick);
  TEST_ASSERT_EQUAL(54,decoded.events[0].data.noteData.note);
  TEST_ASSERT_EQUAL(31,decoded.events[0].noteId);
  bytes.pop_back();
  TEST_ASSERT_FALSE(decodeSlotBody(bytes,decoded,true));
}

int main(int argc, char** argv) {
  if (argc == 3 && std::strcmp(argv[1], "--decode-slot-body") == 0)
    return decodeSlotBodyFile(argv[2]);

  UNITY_BEGIN();
  RUN_TEST(test_diagnostic_decoder_reads_captured_teensy_lsv1);
  RUN_TEST(test_diagnostic_decoder_reads_teensy_legacy_wire_events);
  RUN_TEST(test_diagnostic_decoder_uses_production_lsv1_root);
  RUN_TEST(test_diagnostic_decoder_uses_production_legacy_passes);
  RUN_TEST(test_undo_publication_retains_redo_until_commit_and_defers_latest_command);
  RUN_TEST(test_undo_publication_precommit_restore_preserves_resources_and_entry_buffers);
  RUN_TEST(test_bounded_undo_trim_rechecks_pressure_after_final_root_release);
  RUN_TEST(test_bounded_undo_trim_matches_depth_and_redo_reference);
  RUN_TEST(test_persisted_snapshot_release_returns_chunks_in_bounded_units);
  RUN_TEST(test_persisted_snapshot_release_leaves_shared_owner_unchanged);
  RUN_TEST(test_write_read_loop_snapshot_roundtrip);
  RUN_TEST(test_write_read_disabled_take_preserved);
  RUN_TEST(test_pending_take_not_persisted);
  RUN_TEST(test_write_read_edits_tail_roundtrip);
  RUN_TEST(test_write_read_loop_geometry_tail_roundtrip);
  RUN_TEST(test_legacy_snapshot_without_geometry_tail_loads);
  RUN_TEST(test_save_loop_geometry_assigns_id_and_marks_dirty);
  RUN_TEST(test_legacy_edit_tail_v4_rejected);
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
  RUN_TEST(test_write_read_overdub_session_index_tail_roundtrip);
  RUN_TEST(test_legacy_snapshot_without_overdub_session_index_tail_stays_ungrouped);
  RUN_TEST(test_write_loop_persisted_emits_lsv1_when_published_snapshot_current);
  RUN_TEST(test_legacy_slot_migrates_to_lsv1_after_loaded_hydration);
  RUN_TEST(test_write_loop_persisted_stays_legacy_without_published_snapshot);
  RUN_TEST(test_write_loop_persisted_requires_a_settled_published_snapshot);
  RUN_TEST(test_loop_persist_body_decision_awaits_settlement_within_its_budget);
  RUN_TEST(test_lsv1_body_format_roundtrip_dual_write);
  RUN_TEST(test_lsv1_stepped_parse_matches_full_read);
  RUN_TEST(test_lsv1_payload_byte_slice_quota_and_io_boundaries);
  RUN_TEST(test_lsv1_stepped_save_tail_matches_monolithic);
  RUN_TEST(test_lsv1_stepped_save_tail_resumes_write_payload_without_regather);
  RUN_TEST(test_lsv1_stepped_save_tail_large_snapshot_matches_monolithic);
  RUN_TEST(test_lsv1_load_commit_skips_rebuild);
  RUN_TEST(test_lsv1_probe_legacy_and_unsupported);
  RUN_TEST(test_lsv1_rejects_missing_loop_snapshot_section);
  RUN_TEST(test_lsv1_unknown_section_is_skipped);
  RUN_TEST(test_lsv1_rejects_duplicate_loop_snapshot_section);
  return UNITY_END();
}
