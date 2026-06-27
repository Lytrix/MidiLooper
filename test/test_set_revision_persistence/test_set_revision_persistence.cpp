//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include <cstring>
#include <string>
#include <unity.h>
#include <vector>

#include "../../src/CurrentWorkspaceStorage.cpp"
#include "../../src/PersistenceSchema.cpp"
#include "../../src/RevisionPackedBlob.cpp"
#include "../../src/SetRevisionCatalog.cpp"
#include "CurrentWorkspaceStorage.h"
#include "RevisionPackedBlob.h"
#include "SetRevisionCatalog.h"

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

CurrentWorkspaceStorage::WorkspaceMetaRecord makeSampleWorkspaceMeta() {
  CurrentWorkspaceStorage::WorkspaceMetaRecord record{};
  record.currentEpoch = 120;
  record.lastCommittedEpoch = 119;
  record.derivedFromSetId = 1;
  record.derivedFromRevisionId = 2;
  record.lastCommittedRevisionId = 2;
  record.updatedUnix = 1782345600ULL;
  record.slotSummary[0].occupied = 1;
  record.slotSummary[0].noteCount = 42;
  record.slotSummary[0].bars = 4;
  return record;
}

SetRevisionCatalog::SetMetaRecord makeSampleSetMeta() {
  SetRevisionCatalog::SetMetaRecord record{};
  record.setId = 3;
  record.latestRevisionId = 4;
  record.revisionCount = 4;
  record.createdUnix = 1782000000ULL;
  record.updatedUnix = 1782345600ULL;
  std::snprintf(record.subtitle, sizeof(record.subtitle), "Night Jam");
  record.favorite = 1;
  return record;
}

std::vector<uint8_t> buildSampleRevisionBlob() {
  RevisionPackedBlob::RevisionHeader header{};
  std::memcpy(header.magic, RevisionPackedBlob::kRevisionMagic, sizeof(header.magic));
  header.revisionId = 5;
  header.setId = 1;
  header.sourceEpoch = 120;
  header.createdUnix = 1782345600ULL;
  header.loopIndexOffset = RevisionPackedBlob::kRevisionHeaderByteSize;
  header.loopIndexCount = 2;

  RevisionPackedBlob::LoopIndexEntry entry0{};
  entry0.trackIndex = 0;
  entry0.slotIndex = 1;
  entry0.occupied = 1;
  const uint32_t payloadBase =
      static_cast<uint32_t>(RevisionPackedBlob::kRevisionHeaderByteSize +
                            2U * RevisionPackedBlob::kLoopIndexEntryByteSize);
  header.transportOffset = payloadBase;
  entry0.blobOffset = payloadBase + 16U + 8U + 32U;
  entry0.blobSize = 64;
  entry0.loopLengthTicks = 768;
  entry0.noteCount = 12;
  entry0.bars = 2;

  header.globalOffset = header.transportOffset + 16U;
  header.undoOffset = header.globalOffset + 8U;

  RevisionPackedBlob::LoopIndexEntry entry1{};
  entry1.trackIndex = 2;
  entry1.slotIndex = 3;
  entry1.occupied = 1;
  entry1.blobOffset = entry0.blobOffset + entry0.blobSize;
  entry1.blobSize = 48;
  entry1.loopLengthTicks = 1536;
  entry1.noteCount = 24;
  entry1.bars = 4;

  const std::vector<uint8_t> transportPayload(16, 0xAA);
  const std::vector<uint8_t> globalPayload(8, 0xBB);
  const std::vector<uint8_t> undoPayload(32, 0xCC);
  const std::vector<uint8_t> loopPayload0(64, 0xDD);
  const std::vector<uint8_t> loopPayload1(48, 0xEE);

  std::vector<uint8_t> payload;
  payload.insert(payload.end(), transportPayload.begin(), transportPayload.end());
  payload.insert(payload.end(), globalPayload.begin(), globalPayload.end());
  payload.insert(payload.end(), undoPayload.begin(), undoPayload.end());
  payload.insert(payload.end(), loopPayload0.begin(), loopPayload0.end());
  payload.insert(payload.end(), loopPayload1.begin(), loopPayload1.end());
  header.payloadSize = static_cast<uint32_t>(payload.size());

  std::vector<uint8_t> file;
  MemoryStorageIo headerWriter(&file);
  TEST_ASSERT_TRUE(RevisionPackedBlob::writeRevisionHeader(headerWriter.io(), header));
  MemoryStorageIo indexWriter(&file);
  TEST_ASSERT_TRUE(RevisionPackedBlob::writeLoopIndexEntry(indexWriter.io(), entry0));
  TEST_ASSERT_TRUE(RevisionPackedBlob::writeLoopIndexEntry(indexWriter.io(), entry1));
  file.insert(file.end(), payload.begin(), payload.end());

  RevisionPackedBlob::RevisionFooter footer{};
  footer.fileSize = static_cast<uint32_t>(file.size() + RevisionPackedBlob::kRevisionFooterByteSize);
  footer.payloadCrc32 =
      RevisionPackedBlob::computeRevisionPayloadChecksum(payload.data(), payload.size());
  MemoryStorageIo footerWriter(&file);
  TEST_ASSERT_TRUE(RevisionPackedBlob::writeRevisionFooter(footerWriter.io(), footer));
  return file;
}

}  // namespace

void test_schema_version_major_minor_helpers() {
  TEST_ASSERT_EQUAL_UINT8(0, PersistenceSchema::schemaMajor(PersistenceSchema::kSetRevisionSchemaVersion));
  TEST_ASSERT_EQUAL_UINT8(1, PersistenceSchema::schemaMinor(PersistenceSchema::kSetRevisionSchemaVersion));
  TEST_ASSERT_TRUE(PersistenceSchema::isSchemaMajorCompatible(
      PersistenceSchema::kSetRevisionSchemaVersion, PersistenceSchema::kSetRevisionSchemaVersion));
  TEST_ASSERT_FALSE(PersistenceSchema::isSchemaMajorCompatible(0x0100U, 0x0001U));
}

void test_workspace_meta_round_trip() {
  const CurrentWorkspaceStorage::WorkspaceMetaRecord written = makeSampleWorkspaceMeta();
  std::vector<uint8_t> buffer;
  MemoryStorageIo writer(&buffer);
  TEST_ASSERT_TRUE(CurrentWorkspaceStorage::writeWorkspaceMeta(writer.io(), written));

  MemoryStorageIo reader(&buffer);
  CurrentWorkspaceStorage::WorkspaceMetaRecord readBack{};
  TEST_ASSERT_TRUE(CurrentWorkspaceStorage::readWorkspaceMeta(reader.io(), readBack));
  TEST_ASSERT_EQUAL_UINT32(written.currentEpoch, readBack.currentEpoch);
  TEST_ASSERT_EQUAL_UINT32(written.lastCommittedEpoch, readBack.lastCommittedEpoch);
  TEST_ASSERT_EQUAL_UINT16(written.derivedFromSetId, readBack.derivedFromSetId);
  TEST_ASSERT_EQUAL_UINT16(1, readBack.slotSummary[0].occupied);
  TEST_ASSERT_EQUAL_UINT16(42, readBack.slotSummary[0].noteCount);
  TEST_ASSERT_EQUAL(CurrentWorkspaceStorage::kWorkspaceMetaByteSize, buffer.size());
}

void test_epoch_file_header_round_trip() {
  CurrentWorkspaceStorage::EpochFileHeader written{};
  written.epoch = 42;
  std::vector<uint8_t> buffer;
  MemoryStorageIo writer(&buffer);
  TEST_ASSERT_TRUE(CurrentWorkspaceStorage::writeEpochFileHeader(writer.io(), written));

  MemoryStorageIo reader(&buffer);
  CurrentWorkspaceStorage::EpochFileHeader readBack{};
  TEST_ASSERT_TRUE(CurrentWorkspaceStorage::readEpochFileHeader(reader.io(), readBack));
  TEST_ASSERT_EQUAL_UINT32(42, readBack.epoch);
  TEST_ASSERT_EQUAL(CurrentWorkspaceStorage::kEpochFileHeaderByteSize, buffer.size());
}

void test_workspace_dirty_derivation() {
  TEST_ASSERT_TRUE(CurrentWorkspaceStorage::isWorkspaceDirty(120, 119));
  TEST_ASSERT_FALSE(CurrentWorkspaceStorage::isWorkspaceDirty(120, 120));
}

void test_set_catalog_index_round_trip() {
  SetRevisionCatalog::SetCatalogIndex written{};
  written.nextSetId = 5;
  written.setCount = 4;
  std::vector<uint8_t> buffer;
  MemoryStorageIo writer(&buffer);
  TEST_ASSERT_TRUE(SetRevisionCatalog::writeSetCatalogIndex(writer.io(), written));

  MemoryStorageIo reader(&buffer);
  SetRevisionCatalog::SetCatalogIndex readBack{};
  TEST_ASSERT_TRUE(SetRevisionCatalog::readSetCatalogIndex(reader.io(), readBack));
  TEST_ASSERT_EQUAL_UINT32(5, readBack.nextSetId);
  TEST_ASSERT_EQUAL_UINT32(4, readBack.setCount);
  TEST_ASSERT_EQUAL(SetRevisionCatalog::kSetCatalogIndexByteSize, buffer.size());
}

void test_set_meta_record_round_trip() {
  const SetRevisionCatalog::SetMetaRecord written = makeSampleSetMeta();
  std::vector<uint8_t> buffer;
  MemoryStorageIo writer(&buffer);
  TEST_ASSERT_TRUE(SetRevisionCatalog::writeSetMetaRecord(writer.io(), written));

  MemoryStorageIo reader(&buffer);
  SetRevisionCatalog::SetMetaRecord readBack{};
  TEST_ASSERT_TRUE(SetRevisionCatalog::readSetMetaRecord(reader.io(), readBack));
  TEST_ASSERT_EQUAL_UINT16(3, readBack.setId);
  TEST_ASSERT_EQUAL_UINT16(4, readBack.latestRevisionId);
  TEST_ASSERT_EQUAL_STRING("Night Jam", readBack.subtitle);
  TEST_ASSERT_EQUAL(SetRevisionCatalog::kSetMetaRecordByteSize, buffer.size());
}

void test_set_id_monotonic_allocation() {
  SetRevisionCatalog::SetCatalogIndex index{};
  index.nextSetId = 3;
  index.setCount = 2;
  TEST_ASSERT_EQUAL_UINT16(3, SetRevisionCatalog::allocateNextSetId(index));
  TEST_ASSERT_EQUAL_UINT32(4, index.nextSetId);
  TEST_ASSERT_EQUAL_UINT32(3, index.setCount);
  TEST_ASSERT_EQUAL_UINT16(4, SetRevisionCatalog::allocateNextSetId(index));
  TEST_ASSERT_EQUAL_UINT32(5, index.nextSetId);
}

void test_failed_revision_reuses_id() {
  SetRevisionCatalog::SetMetaRecord meta = makeSampleSetMeta();
  const uint16_t pendingId = SetRevisionCatalog::peekNextRevisionId(meta);
  TEST_ASSERT_EQUAL_UINT16(5, pendingId);
  TEST_ASSERT_EQUAL_UINT16(5, SetRevisionCatalog::peekNextRevisionId(meta));
  SetRevisionCatalog::applyValidatedRevisionToSetMeta(meta, pendingId, 1782400000ULL);
  TEST_ASSERT_EQUAL_UINT16(5, meta.latestRevisionId);
  TEST_ASSERT_EQUAL_UINT16(6, SetRevisionCatalog::peekNextRevisionId(meta));
}

void test_revision_id_visible_only_after_complete() {
  SetRevisionCatalog::SetMetaRecord meta{};
  meta.setId = 1;
  const uint16_t pendingId = SetRevisionCatalog::peekNextRevisionId(meta);
  TEST_ASSERT_EQUAL_UINT16(1, pendingId);
  TEST_ASSERT_EQUAL_UINT16(0, meta.latestRevisionId);
  SetRevisionCatalog::applyValidatedRevisionToSetMeta(meta, pendingId, 1782345600ULL);
  TEST_ASSERT_EQUAL_UINT16(1, meta.latestRevisionId);
  TEST_ASSERT_EQUAL_UINT16(1, meta.revisionCount);
}

void test_catalog_path_formatting() {
  char path[64];
  TEST_ASSERT_TRUE(SetRevisionCatalog::formatSetFolderPath(path, sizeof(path), 12));
  TEST_ASSERT_EQUAL_STRING("/MidiLooper/sets/S0012", path);
  TEST_ASSERT_TRUE(SetRevisionCatalog::formatSetMetaPath(path, sizeof(path), 12));
  TEST_ASSERT_EQUAL_STRING("/MidiLooper/sets/S0012/set.bin", path);
  TEST_ASSERT_TRUE(SetRevisionCatalog::formatRevisionPath(path, sizeof(path), 12, 7, false));
  TEST_ASSERT_EQUAL_STRING("/MidiLooper/sets/S0012/revisions/v0007.bin", path);
  TEST_ASSERT_TRUE(SetRevisionCatalog::formatRevisionPath(path, sizeof(path), 12, 7, true));
  TEST_ASSERT_EQUAL_STRING("/MidiLooper/sets/S0012/revisions/v0007.bin.tmp", path);
}

void test_current_slot_path_formatting() {
  char path[64];
  TEST_ASSERT_TRUE(CurrentWorkspaceStorage::formatSlotPath(path, sizeof(path), 3, false));
  TEST_ASSERT_EQUAL_STRING("/MidiLooper/current/slots/slot_03.bin", path);
  TEST_ASSERT_TRUE(CurrentWorkspaceStorage::formatSlotTempPath(path, sizeof(path), 3, true));
  TEST_ASSERT_EQUAL_STRING("/MidiLooper/current/undo/slot_03.bin.tmp", path);
}

void test_revision_header_wire_size() {
  TEST_ASSERT_EQUAL(128, RevisionPackedBlob::kRevisionHeaderByteSize);
  TEST_ASSERT_EQUAL(32, RevisionPackedBlob::kLoopIndexEntryByteSize);
  TEST_ASSERT_EQUAL(12, RevisionPackedBlob::kRevisionFooterByteSize);
}

void test_revision_blob_parser_without_heap() {
  const std::vector<uint8_t> file = buildSampleRevisionBlob();
  RevisionPackedBlob::RevisionHeader header{};
  TEST_ASSERT_TRUE(
      RevisionPackedBlob::parseRevisionHeaderFromBytes(file.data(), file.size(), header));
  TEST_ASSERT_EQUAL_UINT16(5, header.revisionId);
  TEST_ASSERT_EQUAL_UINT32(120, header.sourceEpoch);
  TEST_ASSERT_EQUAL_UINT16(2, header.loopIndexCount);

  RevisionPackedBlob::LoopIndexEntry entry0{};
  TEST_ASSERT_TRUE(RevisionPackedBlob::readLoopIndexEntryFromBytes(
      file.data(), file.size(), header, 0, entry0));
  TEST_ASSERT_EQUAL_UINT8(0, entry0.trackIndex);
  TEST_ASSERT_EQUAL_UINT8(1, entry0.slotIndex);
  TEST_ASSERT_EQUAL_UINT32(64, entry0.blobSize);

  RevisionPackedBlob::LoopIndexEntry entry1{};
  TEST_ASSERT_TRUE(RevisionPackedBlob::readLoopIndexEntryFromBytes(
      file.data(), file.size(), header, 1, entry1));
  TEST_ASSERT_EQUAL_UINT8(2, entry1.trackIndex);
  TEST_ASSERT_EQUAL_UINT32(48, entry1.blobSize);

  RevisionPackedBlob::RevisionFooter footer{};
  TEST_ASSERT_TRUE(
      RevisionPackedBlob::validateRevisionFooterFromBytes(file.data(), file.size(), footer));
  TEST_ASSERT_EQUAL_UINT32(RevisionPackedBlob::kRevisionCompleteMagic, footer.completeMagic);
  TEST_ASSERT_EQUAL_UINT32(static_cast<uint32_t>(file.size()), footer.fileSize);
}

void test_revision_header_rejects_bad_magic() {
  std::vector<uint8_t> file = buildSampleRevisionBlob();
  file[0] = 'X';
  RevisionPackedBlob::RevisionHeader header{};
  TEST_ASSERT_FALSE(
      RevisionPackedBlob::parseRevisionHeaderFromBytes(file.data(), file.size(), header));
}

void test_epoch_file_bytes_validate_with_header() {
  const uint8_t payload[] = {0x01, 0x02, 0x03, 0x04};
  const uint32_t bodyCrc =
      CurrentWorkspaceStorage::computeEpochFileBodyChecksum(payload, sizeof(payload));

  std::vector<uint8_t> file(CurrentWorkspaceStorage::kEpochFileHeaderByteSize + sizeof(payload));
  const uint32_t epoch = 7;
  const uint16_t schema = PersistenceSchema::kSetRevisionSchemaVersion;
  std::memcpy(file.data(), &epoch, sizeof(epoch));
  std::memcpy(file.data() + sizeof(epoch), &schema, sizeof(schema));
  std::memcpy(file.data() + sizeof(epoch) + sizeof(schema), &bodyCrc, sizeof(bodyCrc));
  std::memcpy(file.data() + CurrentWorkspaceStorage::kEpochFileHeaderByteSize, payload,
              sizeof(payload));

  TEST_ASSERT_TRUE(CurrentWorkspaceStorage::validateEpochFileBytes(
      file.data(), file.size(), 7, true));
  TEST_ASSERT_FALSE(CurrentWorkspaceStorage::validateEpochFileBytes(
      file.data(), file.size(), 8, true));
}

void test_epoch_file_bytes_legacy_without_header() {
  const uint8_t legacy[] = {0x06, 0x00, 0x00, 0x00, 0x01, 0x02};
  TEST_ASSERT_TRUE(
      CurrentWorkspaceStorage::validateEpochFileBytes(legacy, sizeof(legacy), 0, false));
}

void test_workspace_dirty_matches_epoch_divergence() {
  TEST_ASSERT_TRUE(CurrentWorkspaceStorage::isWorkspaceDirty(3, 2));
  TEST_ASSERT_FALSE(CurrentWorkspaceStorage::isWorkspaceDirty(5, 5));
}

int main(int argc, char** argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_schema_version_major_minor_helpers);
  RUN_TEST(test_workspace_meta_round_trip);
  RUN_TEST(test_epoch_file_header_round_trip);
  RUN_TEST(test_workspace_dirty_derivation);
  RUN_TEST(test_set_catalog_index_round_trip);
  RUN_TEST(test_set_meta_record_round_trip);
  RUN_TEST(test_set_id_monotonic_allocation);
  RUN_TEST(test_failed_revision_reuses_id);
  RUN_TEST(test_revision_id_visible_only_after_complete);
  RUN_TEST(test_catalog_path_formatting);
  RUN_TEST(test_current_slot_path_formatting);
  RUN_TEST(test_revision_header_wire_size);
  RUN_TEST(test_revision_blob_parser_without_heap);
  RUN_TEST(test_revision_header_rejects_bad_magic);
  RUN_TEST(test_epoch_file_bytes_validate_with_header);
  RUN_TEST(test_epoch_file_bytes_legacy_without_header);
  RUN_TEST(test_workspace_dirty_matches_epoch_divergence);
  return UNITY_END();
}
