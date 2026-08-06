//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include <cstring>
#include <unity.h>
#include <vector>

#include "../../src/Logger.cpp"
#include "../../src/Utils/NoteUtils.cpp"
#include "../../src/LoopEventStore.cpp"
#include "../../src/EditManager/EditApply.cpp"
#include "../../src/LoopPasses.cpp"
#include "../../src/StorageLoopIo.cpp"
#include "../test_support/MemoryMonitorNativeDeps.cpp"
#include "../../src/Utils/LoopEventValidation.cpp"
#include "../../src/Loop.cpp"
#include "../test_support/LoopCaptureTestDeps.cpp"

#include "GlobalUndoStack.h"
#include "Loop.h"
#include "../test_support/CommittedChunkIdTestHelpers.h"

namespace {

template <typename T>
void appendRaw(std::vector<uint8_t>& buffer, const T& value) {
  const uint8_t* ptr = reinterpret_cast<const uint8_t*>(&value);
  buffer.insert(buffer.end(), ptr, ptr + sizeof(T));
}

RecordPass makeRecordPassWithNotes(unsigned noteCount) {
  LoopEventStore capture;
  for (unsigned i = 0; i < noteCount; ++i) {
    TEST_ASSERT_TRUE(capture.append(MidiEvent::NoteOn(i * 48u, 1, 60, 100)));
    TEST_ASSERT_TRUE(capture.append(MidiEvent::NoteOff(i * 48u + 24u, 1, 60, 0)));
  }
  CommittedChunkIdList publishedIds;
  TEST_ASSERT_TRUE(transferCaptureStoreToCommittedChunkIds(capture, publishedIds));
  RecordPass pass{};
  pass.id = 1;
  pass.state = CapturePassState::Active;
  pass.committedChunkIds = std::move(publishedIds);
  return pass;
}

PersistedLoopSnapshot makeSnapshotWithRecordPass() {
  PersistedLoopSnapshot snapshot{};
  snapshot.loopId = 2;
  snapshot.loopLengthTicks = 49152;
  snapshot.nextPassId = 2;
  snapshot.nextNoteId = 3;
  snapshot.passes.recordPass = makeRecordPassWithNotes(4);
  return snapshot;
}

void appendNullSnapshotFlag(std::vector<uint8_t>& buffer) {
  const bool hasSnapshot = false;
  appendRaw(buffer, hasSnapshot);
}

void appendSnapshotBody(std::vector<uint8_t>& buffer, const PersistedLoopSnapshot& snapshot) {
  const bool hasSnapshot = true;
  appendRaw(buffer, hasSnapshot);
  struct MemoryStorageIo {
    std::vector<uint8_t>* buf;
    StorageIo io() {
      return StorageIo{
          [this](const void* data, size_t size) -> bool {
            const auto* bytes = static_cast<const uint8_t*>(data);
            buf->insert(buf->end(), bytes, bytes + size);
            return true;
          },
          nullptr};
    }
  } mem{&buffer};
  TEST_ASSERT_TRUE(writePersistedLoopSnapshot(mem.io(), snapshot));
}

void appendUndoEntryTail(std::vector<uint8_t>& buffer, const UndoEntry& entry) {
  appendRaw(buffer, entry.beforeGeometry);
  appendRaw(buffer, entry.afterGeometry);
  appendRaw(buffer, entry.beforeLoopStartTick);
  appendRaw(buffer, entry.beforeLoopLengthTicks);
  appendRaw(buffer, entry.afterLoopStartTick);
  appendRaw(buffer, entry.afterLoopLengthTicks);
  const uint32_t beforeTrackState = static_cast<uint32_t>(entry.beforeTrackState);
  const uint32_t afterTrackState = static_cast<uint32_t>(entry.afterTrackState);
  appendRaw(buffer, beforeTrackState);
  appendRaw(buffer, afterTrackState);
  appendRaw(buffer, entry.hasTrackState);
  appendRaw(buffer, entry.hasRedoPayload);
}

void appendUndoEntryHeader(std::vector<uint8_t>& buffer, const UndoEntry& entry) {
  appendRaw(buffer, entry.id);
  const uint8_t kind = static_cast<uint8_t>(entry.kind);
  appendRaw(buffer, kind);
  appendRaw(buffer, entry.slotIndex);
  appendRaw(buffer, entry.loopId);
  appendRaw(buffer, entry.passId);
}

bool skipUndoStackMetadataFromBuffer(const std::vector<uint8_t>& buffer, size_t& cursor) {
  if (cursor + sizeof(uint32_t) * 3 > buffer.size()) {
    return false;
  }
  uint32_t entryCount = 0;
  std::memcpy(&entryCount, buffer.data() + cursor, sizeof(entryCount));
  cursor += sizeof(uint32_t) * 3;

  struct MemoryStorageIo {
    const std::vector<uint8_t>* buf;
    size_t* pos;
    StorageIo io() {
      return StorageIo{
          nullptr,
          [this](void* data, size_t size) -> bool {
            if (*pos + size > buf->size()) {
              return false;
            }
            std::memcpy(data, buf->data() + *pos, size);
            *pos += size;
            return true;
          }};
    }
  } mem{&buffer, &cursor};

  for (uint32_t i = 0; i < entryCount; ++i) {
    UndoEntry entry{};
    if (cursor + 14 > buffer.size()) {
      return false;
    }
    std::memcpy(&entry.id, buffer.data() + cursor, sizeof(entry.id));
    cursor += sizeof(entry.id);
    uint8_t kindRaw = 0;
    std::memcpy(&kindRaw, buffer.data() + cursor, sizeof(kindRaw));
    cursor += sizeof(kindRaw);
    entry.kind = static_cast<UndoEntryKind>(kindRaw);
    std::memcpy(&entry.slotIndex, buffer.data() + cursor, sizeof(entry.slotIndex));
    cursor += sizeof(entry.slotIndex);
    std::memcpy(&entry.loopId, buffer.data() + cursor, sizeof(entry.loopId));
    cursor += sizeof(entry.loopId);
    std::memcpy(&entry.passId, buffer.data() + cursor, sizeof(entry.passId));
    cursor += sizeof(entry.passId);

    bool hasSnapshot = false;
    if (!mem.io().read(&hasSnapshot, sizeof(hasSnapshot))) {
      return false;
    }
    if (hasSnapshot && !skipPersistedLoopSnapshotPayload(mem.io(), false)) {
      return false;
    }
    if (!mem.io().read(&hasSnapshot, sizeof(hasSnapshot))) {
      return false;
    }
    if (hasSnapshot && !skipPersistedLoopSnapshotPayload(mem.io(), false)) {
      return false;
    }

    if (cursor + sizeof(entry.beforeGeometry) + sizeof(entry.afterGeometry) + sizeof(uint32_t) * 4 +
            sizeof(uint32_t) * 2 + sizeof(bool) * 2 >
        buffer.size()) {
      return false;
    }
    std::memcpy(&entry.beforeGeometry, buffer.data() + cursor, sizeof(entry.beforeGeometry));
    cursor += sizeof(entry.beforeGeometry);
    std::memcpy(&entry.afterGeometry, buffer.data() + cursor, sizeof(entry.afterGeometry));
    cursor += sizeof(entry.afterGeometry);
    cursor += sizeof(uint32_t) * 4;
    cursor += sizeof(uint32_t) * 2;
    cursor += sizeof(bool) * 2;
  }
  return true;
}

}  // namespace

void test_skip_snapshot_payload_matches_write() {
  const PersistedLoopSnapshot snapshot = makeSnapshotWithRecordPass();

  std::vector<uint8_t> written;
  struct MemoryStorageIo {
    std::vector<uint8_t>* buf;
    size_t readPos = 0;
    void resetRead() { readPos = 0; }
    StorageIo io() {
      return StorageIo{
          [this](const void* data, size_t size) -> bool {
            const auto* bytes = static_cast<const uint8_t*>(data);
            buf->insert(buf->end(), bytes, bytes + size);
            return true;
          },
          [this](void* data, size_t size) -> bool {
            if (readPos + size > buf->size()) {
              return false;
            }
            std::memcpy(data, buf->data() + readPos, size);
            readPos += size;
            return true;
          }};
    }
  } mem{&written};

  TEST_ASSERT_TRUE(writePersistedLoopSnapshot(mem.io(), snapshot));
  mem.resetRead();
  TEST_ASSERT_TRUE(skipPersistedLoopSnapshotPayload(mem.io(), false));
  TEST_ASSERT_EQUAL_UINT32(static_cast<uint32_t>(written.size()),
                           static_cast<uint32_t>(mem.readPos));
}

void test_undo_metadata_skip_record_pass_added() {
  std::vector<uint8_t> buffer;
  const uint32_t entryCount = 1;
  const uint32_t cursor = 1;
  const uint32_t nextEntryId = 2;
  appendRaw(buffer, entryCount);
  appendRaw(buffer, cursor);
  appendRaw(buffer, nextEntryId);

  UndoEntry entry;
  entry.kind = UndoEntryKind::RecordPassAdded;
  entry.slotIndex = 0;
  entry.loopId = 2;
  entry.passId = 1;
  appendUndoEntryHeader(buffer, entry);
  appendNullSnapshotFlag(buffer);
  appendNullSnapshotFlag(buffer);
  appendUndoEntryTail(buffer, entry);

  size_t readCursor = 0;
  TEST_ASSERT_TRUE(skipUndoStackMetadataFromBuffer(buffer, readCursor));
  TEST_ASSERT_EQUAL_UINT32(static_cast<uint32_t>(buffer.size()),
                           static_cast<uint32_t>(readCursor));
}

void test_undo_metadata_skip_with_snapshot_bodies() {
  const PersistedLoopSnapshot snapshot = makeSnapshotWithRecordPass();
  std::vector<uint8_t> buffer;
  const uint32_t entryCount = 1;
  const uint32_t cursor = 1;
  const uint32_t nextEntryId = 2;
  appendRaw(buffer, entryCount);
  appendRaw(buffer, cursor);
  appendRaw(buffer, nextEntryId);

  UndoEntry entry;
  entry.kind = UndoEntryKind::OverdubPassAdded;
  entry.slotIndex = 0;
  entry.loopId = 2;
  entry.passId = 2;
  appendUndoEntryHeader(buffer, entry);
  appendNullSnapshotFlag(buffer);
  appendSnapshotBody(buffer, snapshot);
  appendUndoEntryTail(buffer, entry);

  size_t readCursor = 0;
  TEST_ASSERT_TRUE(skipUndoStackMetadataFromBuffer(buffer, readCursor));
  TEST_ASSERT_EQUAL_UINT32(static_cast<uint32_t>(buffer.size()),
                           static_cast<uint32_t>(readCursor));
}

void test_stale_bundle_gap_changes_footer_token() {
  std::vector<uint8_t> buffer;
  const uint8_t selectedTrack = 1;
  appendRaw(buffer, selectedTrack);
  for (uint8_t t = 0; t < Config::NUM_TRACKS; ++t) {
    const uint8_t activeIdx = 0;
    appendRaw(buffer, activeIdx);
  }
  appendRaw(buffer, kFooterSelectedSlotExtensionToken);
  for (uint8_t t = 0; t < Config::NUM_TRACKS; ++t) {
    const uint8_t selectedIdx = 0;
    appendRaw(buffer, selectedIdx);
  }
  appendRaw(buffer, kGlobalUndoStackToken);

  const size_t validFooterEnd = buffer.size();
  buffer.insert(buffer.end(), 4096, 0xA5);
  appendRaw(buffer, kGlobalUndoStackToken);

  uint32_t tokenAtGap = 0;
  std::memcpy(&tokenAtGap, buffer.data() + validFooterEnd, sizeof(tokenAtGap));
  TEST_ASSERT_NOT_EQUAL(kGlobalUndoStackToken, tokenAtGap);

  uint32_t tokenAtTail = 0;
  std::memcpy(&tokenAtTail, buffer.data() + buffer.size() - sizeof(tokenAtTail),
              sizeof(tokenAtTail));
  TEST_ASSERT_EQUAL_UINT32(kGlobalUndoStackToken, tokenAtTail);
}

int main(int /*argc*/, char** /*argv*/) {
  UNITY_BEGIN();
  RUN_TEST(test_skip_snapshot_payload_matches_write);
  RUN_TEST(test_undo_metadata_skip_record_pass_added);
  RUN_TEST(test_undo_metadata_skip_with_snapshot_bodies);
  RUN_TEST(test_stale_bundle_gap_changes_footer_token);
  return UNITY_END();
}
