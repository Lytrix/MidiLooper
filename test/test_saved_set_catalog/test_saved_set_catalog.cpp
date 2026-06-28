//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include <cstring>
#include <string>
#include <unity.h>
#include <vector>

#include "../../src/RtcTime.cpp"
#include "../../src/SavedSetCatalog.cpp"

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

}  // namespace

void test_set_index_round_trip() {
  SavedSetCatalog::SetIndex written{};
  written.nextSequence = 42;

  std::vector<uint8_t> buffer;
  MemoryStorageIo writer(&buffer);
  TEST_ASSERT_TRUE(SavedSetCatalog::writeSetIndex(writer.io(), written));

  MemoryStorageIo reader(&buffer);
  SavedSetCatalog::SetIndex readBack{};
  TEST_ASSERT_TRUE(SavedSetCatalog::readSetIndex(reader.io(), readBack));
  TEST_ASSERT_EQUAL_UINT32(42, readBack.nextSequence);
}

void test_parse_saved_set_folder_names() {
  uint32_t sequence = 0;
  SavedSetCatalog::FolderNamingMode mode = SavedSetCatalog::FolderNamingMode::Unknown;
  TEST_ASSERT_TRUE(SavedSetCatalog::parseSavedSetFolderName("260625_003", sequence, &mode));
  TEST_ASSERT_EQUAL_UINT32(3, sequence);
  TEST_ASSERT_EQUAL(SavedSetCatalog::FolderNamingMode::DateSequence, mode);

  TEST_ASSERT_TRUE(SavedSetCatalog::parseSavedSetFolderName("00003", sequence, &mode));
  TEST_ASSERT_EQUAL_UINT32(3, sequence);
  TEST_ASSERT_EQUAL(SavedSetCatalog::FolderNamingMode::SequenceUid, mode);
}

void test_catalog_filters_reserved_entries() {
  TEST_ASSERT_FALSE(SavedSetCatalog::isSavedSetFolderName("_current"));
  TEST_ASSERT_FALSE(SavedSetCatalog::isSavedSetFolderName("index.bin"));
  TEST_ASSERT_FALSE(SavedSetCatalog::isSavedSetFolderName("checkpoints"));
  TEST_ASSERT_FALSE(SavedSetCatalog::isSavedSetFolderName("bad_name"));
  TEST_ASSERT_TRUE(SavedSetCatalog::isSavedSetFolderName("260625_001"));
  TEST_ASSERT_TRUE(SavedSetCatalog::isSavedSetFolderName("00001"));
}

void test_format_saved_set_folder_name_hybrid_modes() {
  char folderName[16];
  SavedSetCatalog::FolderNamingMode mode = SavedSetCatalog::FolderNamingMode::Unknown;
  TEST_ASSERT_TRUE(SavedSetCatalog::formatSavedSetFolderName(3, 1782345600UL, true, folderName,
                                                             sizeof(folderName), &mode));
  TEST_ASSERT_EQUAL_STRING("260625_003", folderName);
  TEST_ASSERT_EQUAL(SavedSetCatalog::FolderNamingMode::DateSequence, mode);

  TEST_ASSERT_TRUE(SavedSetCatalog::formatSavedSetFolderName(3, 0, false, folderName,
                                                             sizeof(folderName), &mode));
  TEST_ASSERT_EQUAL_STRING("00003", folderName);
  TEST_ASSERT_EQUAL(SavedSetCatalog::FolderNamingMode::SequenceUid, mode);
}

void test_rtc_threshold_for_folder_naming() {
  RtcTime::resetForTest();
  RtcTime::setUnixTimeForTest(1767225599UL);
  TEST_ASSERT_FALSE(RtcTime::hasValidDateForFolderNaming());
  RtcTime::setUnixTimeForTest(1767225600UL);
  TEST_ASSERT_TRUE(RtcTime::hasValidDateForFolderNaming());
  RtcTime::resetForTest();
}

void test_reconcile_next_sequence_from_mixed_folders() {
  TEST_ASSERT_EQUAL_UINT32(8, SavedSetCatalog::reconcileNextSequence(7, 7));
  TEST_ASSERT_EQUAL_UINT32(9, SavedSetCatalog::reconcileNextSequence(4, 8));
  TEST_ASSERT_EQUAL_UINT32(1, SavedSetCatalog::reconcileNextSequence(0, 0));
}

void test_allocate_next_sequence_advances_index() {
  SavedSetCatalog::SetIndex index{};
  index.nextSequence = 3;
  TEST_ASSERT_EQUAL_UINT32(3, SavedSetCatalog::allocateNextSequence(index));
  TEST_ASSERT_EQUAL_UINT32(4, index.nextSequence);
}

void test_saved_set_metadata_trailer_round_trip() {
  SavedSetCatalog::SavedSetMetadata written{};
  written.sequence = 9;
  written.folderNamingMode = SavedSetCatalog::FolderNamingMode::DateSequence;
  written.createdAtUnix = 1782345600UL;
  written.masterLoopBars = 8;
  written.trackCount = 3;
  written.filledSlotCount = 12;
  written.perTrackFilledSlots[0] = 4;
  written.perTrackFilledSlots[1] = 5;
  written.perTrackFilledSlots[2] = 3;
  std::snprintf(written.userLabel, sizeof(written.userLabel), "Night Jam");

  std::vector<uint8_t> buffer;
  MemoryStorageIo writer(&buffer);
  TEST_ASSERT_TRUE(SavedSetCatalog::writeSavedSetMetadataTrailer(writer.io(), written));

  MemoryStorageIo reader(&buffer);
  SavedSetCatalog::SavedSetMetadata readBack{};
  TEST_ASSERT_TRUE(SavedSetCatalog::readSavedSetMetadataTrailer(reader.io(), readBack));
  TEST_ASSERT_EQUAL_UINT32(9, readBack.sequence);
  TEST_ASSERT_EQUAL(SavedSetCatalog::FolderNamingMode::DateSequence,
                    readBack.folderNamingMode);
  TEST_ASSERT_EQUAL_UINT32(1782345600UL, readBack.createdAtUnix);
  TEST_ASSERT_EQUAL_UINT16(8, readBack.masterLoopBars);
  TEST_ASSERT_EQUAL_UINT8(3, readBack.trackCount);
  TEST_ASSERT_EQUAL_UINT8(12, readBack.filledSlotCount);
  TEST_ASSERT_EQUAL_STRING("Night Jam", readBack.userLabel);
}

void test_default_label_prefers_date_then_fallback() {
  char label[48];
  SavedSetCatalog::formatDefaultSavedSetLabel(1782345600UL, "00003", label, sizeof(label));
  TEST_ASSERT_GREATER_THAN(0, static_cast<int>(std::strlen(label)));
  TEST_ASSERT_NOT_EQUAL(0, std::strcmp(label, "00003"));

  SavedSetCatalog::formatDefaultSavedSetLabel(0, "00003", label, sizeof(label));
  TEST_ASSERT_EQUAL_STRING("00003", label);
}


int main(int argc, char** argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_set_index_round_trip);
  RUN_TEST(test_parse_saved_set_folder_names);
  RUN_TEST(test_catalog_filters_reserved_entries);
  RUN_TEST(test_format_saved_set_folder_name_hybrid_modes);
  RUN_TEST(test_rtc_threshold_for_folder_naming);
  RUN_TEST(test_reconcile_next_sequence_from_mixed_folders);
  RUN_TEST(test_allocate_next_sequence_advances_index);
  RUN_TEST(test_saved_set_metadata_trailer_round_trip);
  RUN_TEST(test_default_label_prefers_date_then_fallback);
  return UNITY_END();
}
