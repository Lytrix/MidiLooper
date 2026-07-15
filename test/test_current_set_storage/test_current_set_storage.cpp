//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include <cstring>
#include <map>
#include <string>
#include <unity.h>
#include <vector>

#include "../../src/CurrentSetStorage.cpp"
#include "../../src/Logger.cpp"
#include "../../src/Utils/NoteUtils.cpp"
#include "../../src/LoopEventStore.cpp"
#include "../../src/EditApply.cpp"
#include "../../src/LoopPasses.cpp"
#include "../../src/StorageLoopIo.cpp"
#include "../test_support/MemoryMonitorNativeDeps.cpp"
#include "../../src/Utils/LoopEventValidation.cpp"
#include "../../src/Loop.cpp"
#include "../test_support/LoopCaptureTestDeps.cpp"
#include "../test_support/PublishedChunkIdTestHelpers.h"
#include "CurrentSetStorage.h"
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

PersistedLoopSnapshot makeSampleLoopSnapshot() {
  PersistedLoopSnapshot snapshot{};
  snapshot.loopId = 0;
  snapshot.loopLengthTicks = 768;
  snapshot.nextPassId = 2;
  RecordPass record{};
  record.id = 1;
  record.state = CapturePassState::Active;
  LoopEventStore capture;
  TEST_ASSERT_TRUE(capture.append(MidiEvent::NoteOn(0, 1, 60, 100)));
  PublishedChunkIdList publishedIds;
  TEST_ASSERT_TRUE(transferCaptureStoreToPublished(capture, publishedIds));
  record.publishedChunkIds = std::move(publishedIds);
  snapshot.passes.recordPass = std::move(record);
  return snapshot;
}

}  // namespace

void test_runtime_bundle_header_round_trip() {
  CurrentSetStorage::MetaHeader written{};
  written.containerVersion = CurrentSetStorage::CONTAINER_VERSION;
  written.lastActiveUnix = 1780000000UL;
  written.anchor.loadedFromSequence = 3;
  written.anchor.lastAnchoredSequence = 3;
  written.anchor.lastMaterialChangeUnix = 1779990000UL;
  written.anchor.hasMaterialChangesSinceAnchor = 0;

  std::vector<uint8_t> buffer;
  MemoryStorageIo ioWriter(&buffer);
  TEST_ASSERT_TRUE(CurrentSetStorage::writeMetaHeader(ioWriter.io(), written));

  MemoryStorageIo ioReader(&buffer);
  CurrentSetStorage::MetaHeader readBack{};
  TEST_ASSERT_TRUE(CurrentSetStorage::readMetaHeader(ioReader.io(), readBack));
  TEST_ASSERT_EQUAL_UINT32(written.containerVersion, readBack.containerVersion);
  TEST_ASSERT_EQUAL_UINT32(written.lastActiveUnix, readBack.lastActiveUnix);
  TEST_ASSERT_EQUAL_UINT32(written.anchor.loadedFromSequence, readBack.anchor.loadedFromSequence);
  TEST_ASSERT_EQUAL_UINT32(written.anchor.lastAnchoredSequence,
                             readBack.anchor.lastAnchoredSequence);
  TEST_ASSERT_EQUAL_UINT8(written.anchor.hasMaterialChangesSinceAnchor,
                          readBack.anchor.hasMaterialChangesSinceAnchor);
}

void test_loop_slot_path_two_digit_padding() {
  char path[48];
  TEST_ASSERT_TRUE(CurrentSetStorage::formatLoopSlotPath(path, sizeof(path), 0, 7));
  TEST_ASSERT_EQUAL_STRING("/MidiLooper/current/slots/loop_00_07.bin", path);
  TEST_ASSERT_TRUE(CurrentSetStorage::formatLoopSlotPath(path, sizeof(path), 15, 15));
  TEST_ASSERT_EQUAL_STRING("/MidiLooper/current/slots/loop_15_15.bin", path);
}

void test_loop_slot_temp_path_two_digit_padding() {
  char path[48];
  TEST_ASSERT_TRUE(CurrentSetStorage::formatLoopSlotTempPath(path, sizeof(path), 0, 7));
  TEST_ASSERT_EQUAL_STRING("/MidiLooper/current/slots/loop_00_07.bin.tmp", path);
  TEST_ASSERT_TRUE(CurrentSetStorage::formatLoopSlotTempPath(path, sizeof(path), 15, 15));
  TEST_ASSERT_EQUAL_STRING("/MidiLooper/current/slots/loop_15_15.bin.tmp", path);
}

void test_loop_file_round_trip_with_complete_magic() {
  const PersistedLoopSnapshot snapshot = makeSampleLoopSnapshot();
  std::vector<uint8_t> buffer;
  MemoryStorageIo ioWriter(&buffer);
  TEST_ASSERT_TRUE(writePersistedLoopSnapshot(ioWriter.io(), snapshot));
  TEST_ASSERT_TRUE(CurrentSetStorage::writeSaveFileToken(ioWriter.io()));

  TEST_ASSERT_TRUE(CurrentSetStorage::verifySaveFileTokenAtEnd(buffer.data(), buffer.size()));

  MemoryStorageIo ioReader(&buffer);
  const size_t payloadSize = buffer.size() - sizeof(CurrentSetStorage::kSaveFileToken);
  (void)payloadSize;

  PersistedLoopSnapshot readBack{};
  TEST_ASSERT_TRUE(readPersistedLoopSnapshot(ioReader.io(), readBack));
  TEST_ASSERT_EQUAL_UINT32(snapshot.loopLengthTicks, readBack.loopLengthTicks);
  TEST_ASSERT_TRUE(readBack.passes.hasRecordPass());
}

void test_complete_magic_fail_hard_without_footer() {
  std::vector<uint8_t> buffer = {0x01, 0x02, 0x03};
  TEST_ASSERT_FALSE(CurrentSetStorage::verifySaveFileTokenAtEnd(buffer.data(), buffer.size()));
}

void test_runtime_bundle_header_anchor_fields_round_trip() {
  CurrentSetStorage::MetaHeader written{};
  written.containerVersion = CurrentSetStorage::CONTAINER_VERSION;
  written.lastActiveUnix = 1800000123UL;
  written.anchor.loadedFromSequence = 9;
  written.anchor.lastAnchoredSequence = 7;
  written.anchor.lastMaterialChangeUnix = 1799999999UL;
  written.anchor.hasMaterialChangesSinceAnchor = 1;

  std::vector<uint8_t> buffer;
  MemoryStorageIo ioWriter(&buffer);
  TEST_ASSERT_TRUE(CurrentSetStorage::writeMetaHeader(ioWriter.io(), written));

  MemoryStorageIo ioReader(&buffer);
  CurrentSetStorage::MetaHeader readBack{};
  TEST_ASSERT_TRUE(CurrentSetStorage::readMetaHeader(ioReader.io(), readBack));
  TEST_ASSERT_EQUAL_UINT32(written.anchor.loadedFromSequence,
                           readBack.anchor.loadedFromSequence);
  TEST_ASSERT_EQUAL_UINT32(written.anchor.lastAnchoredSequence,
                           readBack.anchor.lastAnchoredSequence);
  TEST_ASSERT_EQUAL_UINT32(written.anchor.lastMaterialChangeUnix,
                           readBack.anchor.lastMaterialChangeUnix);
  TEST_ASSERT_EQUAL_UINT8(written.anchor.hasMaterialChangesSinceAnchor,
                          readBack.anchor.hasMaterialChangesSinceAnchor);
}

void test_slot_payload_policy_skips_clean_slots_without_full_rewrite() {
  uint16_t writes = 0;
  for (uint8_t track = 0; track < Config::NUM_TRACKS; ++track) {
    for (uint8_t slot = 0; slot < Config::MAX_LOOPS_PER_TRACK; ++slot) {
      if (CurrentSetStorage::shouldWriteLoopPayloadForSlot(false, false)) {
        ++writes;
      }
    }
  }
  TEST_ASSERT_EQUAL_UINT16(0, writes);
}

void test_slot_payload_policy_writes_all_slots_when_full_rewrite_forced() {
  uint16_t writes = 0;
  for (uint8_t track = 0; track < Config::NUM_TRACKS; ++track) {
    for (uint8_t slot = 0; slot < Config::MAX_LOOPS_PER_TRACK; ++slot) {
      if (CurrentSetStorage::shouldWriteLoopPayloadForSlot(true, false)) {
        ++writes;
      }
    }
  }
  TEST_ASSERT_EQUAL_UINT16(Config::NUM_TRACKS * Config::MAX_LOOPS_PER_TRACK, writes);
}

void test_count_loop_slot_payload_writes_all_clean_without_full_rewrite() {
  bool slotDirty[Config::NUM_TRACKS][Config::MAX_LOOPS_PER_TRACK] = {};
  const CurrentSetStorage::LoopSlotPayloadWriteCounts counts =
      CurrentSetStorage::countLoopSlotPayloadWrites(false, slotDirty);
  TEST_ASSERT_EQUAL_UINT16(0, counts.writes);
  TEST_ASSERT_EQUAL_UINT16(Config::NUM_TRACKS * Config::MAX_LOOPS_PER_TRACK, counts.skips);
}

void test_count_loop_slot_payload_writes_single_dirty_slot() {
  bool slotDirty[Config::NUM_TRACKS][Config::MAX_LOOPS_PER_TRACK] = {};
  constexpr uint8_t dirtyTrack = 4;
  constexpr uint8_t dirtySlot = 2;
  slotDirty[dirtyTrack][dirtySlot] = true;
  const CurrentSetStorage::LoopSlotPayloadWriteCounts counts =
      CurrentSetStorage::countLoopSlotPayloadWrites(false, slotDirty);
  TEST_ASSERT_EQUAL_UINT16(1, counts.writes);
  TEST_ASSERT_EQUAL_UINT16(Config::NUM_TRACKS * Config::MAX_LOOPS_PER_TRACK - 1, counts.skips);
}

void test_count_loop_slot_payload_writes_all_when_full_rewrite_forced() {
  bool slotDirty[Config::NUM_TRACKS][Config::MAX_LOOPS_PER_TRACK] = {};
  const CurrentSetStorage::LoopSlotPayloadWriteCounts counts =
      CurrentSetStorage::countLoopSlotPayloadWrites(true, slotDirty);
  TEST_ASSERT_EQUAL_UINT16(Config::NUM_TRACKS * Config::MAX_LOOPS_PER_TRACK, counts.writes);
  TEST_ASSERT_EQUAL_UINT16(0, counts.skips);
}

void test_load_set_policy_autosaves_only_when_current_set_dirty() {
  CurrentSetStorage::AnchorFields cleanAnchor{};
  cleanAnchor.hasMaterialChangesSinceAnchor = 0;
  TEST_ASSERT_FALSE(CurrentSetStorage::shouldAutoSaveBeforeLoadIntoCurrent(cleanAnchor));

  CurrentSetStorage::AnchorFields dirtyAnchor{};
  dirtyAnchor.hasMaterialChangesSinceAnchor = 1;
  dirtyAnchor.lastMaterialChangeUnix = 1782345600UL;
  TEST_ASSERT_TRUE(CurrentSetStorage::shouldAutoSaveBeforeLoadIntoCurrent(dirtyAnchor));
}

void test_load_set_policy_applies_loaded_anchor_fields() {
  CurrentSetStorage::AnchorFields anchor{};
  anchor.loadedFromSequence = 2;
  anchor.lastAnchoredSequence = 2;
  anchor.hasMaterialChangesSinceAnchor = 1;
  anchor.lastMaterialChangeUnix = 1782345600UL;

  CurrentSetStorage::applyLoadedSetAnchorFields(7, anchor);

  TEST_ASSERT_EQUAL_UINT32(7, anchor.loadedFromSequence);
  TEST_ASSERT_EQUAL_UINT32(7, anchor.lastAnchoredSequence);
  TEST_ASSERT_EQUAL_UINT8(0, anchor.hasMaterialChangesSinceAnchor);
  TEST_ASSERT_EQUAL_UINT32(0, anchor.lastMaterialChangeUnix);
}

int main(int argc, char** argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_runtime_bundle_header_round_trip);
  RUN_TEST(test_loop_slot_path_two_digit_padding);
  RUN_TEST(test_loop_slot_temp_path_two_digit_padding);
  RUN_TEST(test_loop_file_round_trip_with_complete_magic);
  RUN_TEST(test_complete_magic_fail_hard_without_footer);
  RUN_TEST(test_runtime_bundle_header_anchor_fields_round_trip);
  RUN_TEST(test_slot_payload_policy_skips_clean_slots_without_full_rewrite);
  RUN_TEST(test_slot_payload_policy_writes_all_slots_when_full_rewrite_forced);
  RUN_TEST(test_count_loop_slot_payload_writes_all_clean_without_full_rewrite);
  RUN_TEST(test_count_loop_slot_payload_writes_single_dirty_slot);
  RUN_TEST(test_count_loop_slot_payload_writes_all_when_full_rewrite_forced);
  RUN_TEST(test_load_set_policy_autosaves_only_when_current_set_dirty);
  RUN_TEST(test_load_set_policy_applies_loaded_anchor_fields);
  return UNITY_END();
}
