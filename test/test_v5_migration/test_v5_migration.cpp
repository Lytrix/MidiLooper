//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include <cstring>
#include <set>
#include <string>
#include <unity.h>
#include <vector>

#include "../../src/CurrentSetStorage.cpp"
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
#include "CurrentSetStorage.h"
#include "GlobalUndoStack.h"
#include "StorageLoopIo.h"
#include "MidiEvent.h"

namespace {

constexpr uint32_t kV5StorageVersion = 5;

template <typename T>
void appendRaw(std::vector<uint8_t>& buffer, const T& value) {
  const uint8_t* ptr = reinterpret_cast<const uint8_t*>(&value);
  buffer.insert(buffer.end(), ptr, ptr + sizeof(T));
}

void appendEmptyLoopPool(std::vector<uint8_t>& buffer) {
  for (uint8_t pool = 0; pool < 8; ++pool) {
    const LoopId loopId = static_cast<LoopId>(pool);
    appendRaw(buffer, loopId);
    const uint32_t zero = 0;
    appendRaw(buffer, zero);  // startLoopTick
    appendRaw(buffer, zero);  // loopLengthTicks
    appendRaw(buffer, zero);  // loopStartTick
    const PassId nextPassId = 1;
    appendRaw(buffer, nextPassId);
    const NoteId nextNoteId = 1;
    appendRaw(buffer, nextNoteId);
    appendRaw(buffer, zero);  // nextMergeSequence
    const PassId invalidPass = kInvalidPassId;
    appendRaw(buffer, invalidPass);
    const uint32_t persistedCount = 0;
    appendRaw(buffer, persistedCount);
    const uint32_t editPassCount = 0;
    appendRaw(buffer, editPassCount);
  }
}

void appendV5MonolithFixture(std::vector<uint8_t>& buffer) {
  appendRaw(buffer, kV5StorageVersion);
  const float bpm = 120.0f;
  appendRaw(buffer, bpm);
  const uint32_t looperState = 0;
  appendRaw(buffer, looperState);
  const uint32_t masterLength = 0;
  appendRaw(buffer, masterLength);
  const uint8_t numTracks = 8;
  appendRaw(buffer, numTracks);

  for (uint8_t t = 0; t < numTracks; ++t) {
    const uint32_t trackState = 0;
    appendRaw(buffer, trackState);
    const bool muted = false;
    appendRaw(buffer, muted);
    for (uint8_t s = 0; s < 8; ++s) {
      const bool slotEnabled = false;
      const bool slotMuted = false;
      const LoopId slotLoopId = static_cast<LoopId>(s);
      appendRaw(buffer, slotEnabled);
      appendRaw(buffer, slotMuted);
      appendRaw(buffer, slotLoopId);
    }
    appendEmptyLoopPool(buffer);
  }

  const uint8_t selectedTrack = 0;
  appendRaw(buffer, selectedTrack);
  for (uint8_t t = 0; t < numTracks; ++t) {
    const uint8_t activeIdx = 0;
    appendRaw(buffer, activeIdx);
  }
  appendRaw(buffer, kGlobalUndoStackToken);
  for (uint8_t t = 0; t < numTracks; ++t) {
    const uint32_t entryCount = 0;
    const uint32_t cursor = 0;
    const uint32_t nextEntryId = 1;
    appendRaw(buffer, entryCount);
    appendRaw(buffer, cursor);
    appendRaw(buffer, nextEntryId);
  }
  appendRaw(buffer, CurrentSetStorage::kSaveFileToken);
}

size_t expectedV5FixtureBytes() {
  const size_t headerBytes =
      sizeof(kV5StorageVersion) + sizeof(float) + sizeof(uint32_t) + sizeof(uint32_t) +
      sizeof(uint8_t);
  const size_t slotMetaBytesPerTrack =
      8 * (sizeof(bool) + sizeof(bool) + sizeof(LoopId));
  const size_t loopHeaderBytesPerLoop =
      sizeof(LoopId) + sizeof(uint32_t) + sizeof(uint32_t) + sizeof(uint32_t) +
      sizeof(PassId) + sizeof(NoteId) + sizeof(uint32_t) + sizeof(PassId) + sizeof(uint32_t) +
      sizeof(uint32_t);
  const size_t loopBytesPerTrack = 8 * loopHeaderBytesPerLoop;
  const size_t trackBytes = 8 * (sizeof(uint32_t) + sizeof(bool) + slotMetaBytesPerTrack +
                                 loopBytesPerTrack);
  const size_t footerBytes = sizeof(uint8_t) + 8 * sizeof(uint8_t) + sizeof(kGlobalUndoStackToken) +
                             8 * (sizeof(uint32_t) + sizeof(uint32_t) + sizeof(uint32_t));
  return headerBytes + trackBytes + footerBytes + sizeof(CurrentSetStorage::kSaveFileToken);
}

}  // namespace

void test_v5_monolith_fixture_has_completion_marker() {
  std::vector<uint8_t> monolith;
  appendV5MonolithFixture(monolith);
  TEST_ASSERT_TRUE(
      CurrentSetStorage::verifySaveFileTokenAtEnd(monolith.data(), monolith.size()));
}

void test_migration_target_loop_names_are_two_digit() {
  char path[48];
  for (uint8_t track = 0; track < 8; ++track) {
    for (uint8_t slot = 0; slot < 8; ++slot) {
      TEST_ASSERT_TRUE(
          CurrentSetStorage::formatLoopSlotPath(path, sizeof(path), track, slot));
      char expected[48];
      std::snprintf(expected, sizeof(expected), "/MidiLooper/current/slots/loop_%02u_%02u.bin",
                    static_cast<unsigned>(track), static_cast<unsigned>(slot));
      TEST_ASSERT_EQUAL_STRING(expected, path);
    }
  }
}

void test_migration_target_contains_all_64_unique_loop_paths() {
  char path[48];
  std::set<std::string> uniquePaths;
  for (uint8_t track = 0; track < 8; ++track) {
    for (uint8_t slot = 0; slot < 8; ++slot) {
      TEST_ASSERT_TRUE(
          CurrentSetStorage::formatLoopSlotPath(path, sizeof(path), track, slot));
      uniquePaths.insert(path);
    }
  }
  TEST_ASSERT_EQUAL_UINT32(64, static_cast<uint32_t>(uniquePaths.size()));
}

void test_v6_meta_header_written_for_migrated_tree() {
  CurrentSetStorage::MetaHeader header{};
  header.containerVersion = CurrentSetStorage::CONTAINER_VERSION;
  header.lastActiveUnix = 0;
  std::vector<uint8_t> meta;
  class MemoryIo {
   public:
    explicit MemoryIo(std::vector<uint8_t>* b) : b_(b) {}
    StorageIo io() {
      return StorageIo{
          [this](const void* data, size_t size) {
            const auto* bytes = static_cast<const uint8_t*>(data);
            b_->insert(b_->end(), bytes, bytes + size);
            return true;
          },
          nullptr,
      };
    }

   private:
    std::vector<uint8_t>* b_;
  };
  MemoryIo io(&meta);
  TEST_ASSERT_TRUE(CurrentSetStorage::writeMetaHeader(io.io(), header));
  TEST_ASSERT_GREATER_THAN(0, static_cast<int>(meta.size()));
  uint32_t version = 0;
  std::memcpy(&version, meta.data(), sizeof(version));
  TEST_ASSERT_EQUAL_UINT32(CurrentSetStorage::CONTAINER_VERSION, version);
}

void test_v5_fixture_models_full_first_tree_baseline() {
  std::vector<uint8_t> monolith;
  appendV5MonolithFixture(monolith);
  TEST_ASSERT_EQUAL_UINT32(static_cast<uint32_t>(expectedV5FixtureBytes()),
                           static_cast<uint32_t>(monolith.size()));
}

int main(int argc, char** argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_v5_monolith_fixture_has_completion_marker);
  RUN_TEST(test_migration_target_loop_names_are_two_digit);
  RUN_TEST(test_migration_target_contains_all_64_unique_loop_paths);
  RUN_TEST(test_v6_meta_header_written_for_migrated_tree);
  RUN_TEST(test_v5_fixture_models_full_first_tree_baseline);
  return UNITY_END();
}
