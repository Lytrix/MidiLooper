//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include <cstring>
#include <string>
#include <unity.h>
#include <vector>

#include "../../src/PersistenceBudget.cpp"
#include "../../src/CurrentWorkspaceStorage.cpp"
#include "../../src/PersistenceSchema.cpp"
#include "../../src/RevisionPackedBlob.cpp"
#include "../../src/RevisionLoadPolicy.cpp"
#include "../../src/BootRecoveryPolicy.cpp"
#include "../../src/SetRevisionCatalog.cpp"
#include "BootRecoveryPolicy.h"
#include "CurrentWorkspaceStorage.h"
#include "PersistenceBudget.h"
#include "RevisionCommitPolicy.h"
#include "RevisionLoadPolicy.h"
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

  const std::vector<uint8_t> transportPayload(16, 0xAA);
  const std::vector<uint8_t> loopPayload0(64, 0xDD);
  const std::vector<uint8_t> loopPayload1(48, 0xEE);

  RevisionPackedBlob::RevisionLoopSlotDirectoryEntry entry0{};
  entry0.trackIndex = 0;
  entry0.slotIndex = 1;
  entry0.occupied = 1;
  entry0.bodyLength = static_cast<uint32_t>(loopPayload0.size());
  entry0.loopLengthTicks = 768;
  entry0.noteCount = 12;
  entry0.bars = 2;

  RevisionPackedBlob::RevisionLoopSlotDirectoryEntry entry1{};
  entry1.trackIndex = 2;
  entry1.slotIndex = 3;
  entry1.occupied = 1;
  entry1.bodyLength = static_cast<uint32_t>(loopPayload1.size());
  entry1.loopLengthTicks = 1536;
  entry1.noteCount = 24;
  entry1.bars = 4;

  std::vector<uint8_t> payload;

  auto appendChunk = [&](RevisionPackedBlob::ChunkType type, uint8_t track, uint8_t slot,
                         const std::vector<uint8_t>& body) {
    RevisionPackedBlob::ChunkHeader chunkHeader{};
    chunkHeader.type = static_cast<uint8_t>(type);
    chunkHeader.trackIndex = track;
    chunkHeader.slotIndex = slot;
    chunkHeader.bodyLength = static_cast<uint32_t>(body.size());
    MemoryStorageIo chunkWriter(&payload);
    TEST_ASSERT_TRUE(RevisionPackedBlob::writeChunkHeader(chunkWriter.io(), chunkHeader));
    payload.insert(payload.end(), body.begin(), body.end());
  };

  appendChunk(RevisionPackedBlob::ChunkType::Transport, 0, 0, transportPayload);

  entry0.chunkOffset = static_cast<uint32_t>(payload.size());
  appendChunk(RevisionPackedBlob::ChunkType::LoopSlot, entry0.trackIndex, entry0.slotIndex,
              loopPayload0);

  entry1.chunkOffset = static_cast<uint32_t>(payload.size());
  appendChunk(RevisionPackedBlob::ChunkType::LoopSlot, entry1.trackIndex, entry1.slotIndex,
              loopPayload1);

  const uint16_t entryCount = 2;
  const uint16_t reservedPrefix = 0;
  std::vector<uint8_t> slotIndexBody;
  slotIndexBody.insert(slotIndexBody.end(), reinterpret_cast<const uint8_t*>(&entryCount),
                       reinterpret_cast<const uint8_t*>(&entryCount) + sizeof(entryCount));
  slotIndexBody.insert(slotIndexBody.end(), reinterpret_cast<const uint8_t*>(&reservedPrefix),
                       reinterpret_cast<const uint8_t*>(&reservedPrefix) + sizeof(reservedPrefix));
  MemoryStorageIo entry0Writer(&slotIndexBody);
  TEST_ASSERT_TRUE(RevisionPackedBlob::writeRevisionLoopSlotDirectoryEntry(entry0Writer.io(), entry0));
  MemoryStorageIo entry1Writer(&slotIndexBody);
  TEST_ASSERT_TRUE(RevisionPackedBlob::writeRevisionLoopSlotDirectoryEntry(entry1Writer.io(), entry1));
  appendChunk(RevisionPackedBlob::ChunkType::SlotIndex, 0, 0, slotIndexBody);

  header.chunkCount = 4;
  header.payloadSize = static_cast<uint32_t>(payload.size());

  std::vector<uint8_t> file;
  MemoryStorageIo headerWriter(&file);
  TEST_ASSERT_TRUE(RevisionPackedBlob::writeRevisionHeader(headerWriter.io(), header));
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

void test_revision_header_sd_file_byte_size() {
  TEST_ASSERT_EQUAL(128, RevisionPackedBlob::kRevisionHeaderByteSize);
  TEST_ASSERT_EQUAL(8, RevisionPackedBlob::kChunkHeaderByteSize);
  TEST_ASSERT_EQUAL(32, RevisionPackedBlob::kRevisionLoopSlotDirectoryEntryByteSize);
  TEST_ASSERT_EQUAL(12, RevisionPackedBlob::kRevisionFooterByteSize);
}

void test_revision_blob_parser_without_heap() {
  const std::vector<uint8_t> file = buildSampleRevisionBlob();
  RevisionPackedBlob::RevisionHeader header{};
  TEST_ASSERT_TRUE(
      RevisionPackedBlob::parseRevisionHeaderFromBytes(file.data(), file.size(), header));
  TEST_ASSERT_EQUAL_UINT16(5, header.revisionId);
  TEST_ASSERT_EQUAL_UINT32(120, header.sourceEpoch);
  TEST_ASSERT_EQUAL_UINT16(4, header.chunkCount);

  uint32_t transportOffset = 0;
  uint32_t transportLength = 0;
  TEST_ASSERT_TRUE(RevisionPackedBlob::findChunkBodyInRevisionBytes(
      file.data(), file.size(), header, RevisionPackedBlob::ChunkType::Transport, 0, 0,
      transportOffset, transportLength));
  TEST_ASSERT_EQUAL_UINT32(16, transportLength);

  uint16_t slotIndexCount = 0;
  TEST_ASSERT_TRUE(RevisionPackedBlob::readRevisionLoopSlotDirectoryEntryCountFromRevisionBytes(
      file.data(), file.size(), header, slotIndexCount));
  TEST_ASSERT_EQUAL_UINT16(2, slotIndexCount);

  RevisionPackedBlob::RevisionLoopSlotDirectoryEntry indexedEntries[4]{};
  uint16_t indexedCount = 0;
  TEST_ASSERT_TRUE(RevisionPackedBlob::readRevisionLoopSlotDirectoryEntriesFromRevisionBytes(
      file.data(), file.size(), header, indexedEntries, 4, indexedCount));
  TEST_ASSERT_EQUAL_UINT16(2, indexedCount);
  TEST_ASSERT_EQUAL_UINT8(0, indexedEntries[0].trackIndex);
  TEST_ASSERT_EQUAL_UINT8(1, indexedEntries[0].slotIndex);

  RevisionPackedBlob::RevisionLoopSlotDirectoryEntry entry0{};
  TEST_ASSERT_TRUE(RevisionPackedBlob::readRevisionLoopSlotDirectoryEntryFromRevisionBytes(
      file.data(), file.size(), header, 0, entry0));
  TEST_ASSERT_EQUAL_UINT8(0, entry0.trackIndex);
  TEST_ASSERT_EQUAL_UINT8(1, entry0.slotIndex);
  TEST_ASSERT_EQUAL_UINT32(64, entry0.bodyLength);

  RevisionPackedBlob::RevisionLoopSlotDirectoryEntry entry1{};
  TEST_ASSERT_TRUE(RevisionPackedBlob::readRevisionLoopSlotDirectoryEntryFromRevisionBytes(
      file.data(), file.size(), header, 1, entry1));
  TEST_ASSERT_EQUAL_UINT8(2, entry1.trackIndex);
  TEST_ASSERT_EQUAL_UINT32(48, entry1.bodyLength);

  RevisionPackedBlob::RevisionFooter footer{};
  TEST_ASSERT_TRUE(
      RevisionPackedBlob::validateRevisionFooterFromBytes(file.data(), file.size(), footer));
  TEST_ASSERT_EQUAL_UINT32(RevisionPackedBlob::kRevisionSvokFileToken, footer.svokToken);
  TEST_ASSERT_EQUAL_UINT32(static_cast<uint32_t>(file.size()), footer.fileSize);
}

void test_revision_slot_index_parse_ignores_zero_chunk_count() {
  std::vector<uint8_t> file = buildSampleRevisionBlob();
  RevisionPackedBlob::RevisionHeader header{};
  TEST_ASSERT_TRUE(
      RevisionPackedBlob::parseRevisionHeaderFromBytes(file.data(), file.size(), header));
  header.chunkCount = 0;

  uint16_t slotIndexCount = 0;
  TEST_ASSERT_TRUE(RevisionPackedBlob::readRevisionLoopSlotDirectoryEntryCountFromRevisionBytes(
      file.data(), file.size(), header, slotIndexCount));
  TEST_ASSERT_EQUAL_UINT16(2, slotIndexCount);
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

void test_revision_snapshot_source_epoch_idle() {
  TEST_ASSERT_EQUAL_UINT32(
      120,
      CurrentWorkspaceStorage::resolveCompletedWorkspaceEpochForRevisionSnapshot(120, 0, false));
}

void test_revision_snapshot_source_epoch_during_deferred_save() {
  TEST_ASSERT_EQUAL_UINT32(
      119,
      CurrentWorkspaceStorage::resolveCompletedWorkspaceEpochForRevisionSnapshot(120, 120, true));
}

void test_revision_snapshot_bumps_live_epoch() {
  TEST_ASSERT_EQUAL_UINT32(121,
                           CurrentWorkspaceStorage::workspaceEpochAfterRevisionSnapshot(120));
}

void test_last_committed_sync_after_revision_commit_complete_clears_dirty() {
  const uint32_t sourceEpoch = 120;
  const uint32_t currentAfterSnapshot =
      CurrentWorkspaceStorage::workspaceEpochAfterRevisionSnapshot(sourceEpoch);
  const uint32_t lastCommitted =
      CurrentWorkspaceStorage::syncLastCommittedEpochAfterRevisionCommitComplete(
          currentAfterSnapshot);
  TEST_ASSERT_EQUAL_UINT32(121, lastCommitted);
  TEST_ASSERT_FALSE(
      CurrentWorkspaceStorage::isWorkspaceDirty(currentAfterSnapshot, lastCommitted));
}

void test_record_deferred_save_leaves_workspace_dirty_until_commit_complete() {
  TEST_ASSERT_TRUE(CurrentWorkspaceStorage::isWorkspaceDirty(120, 119));
}

void test_revision_commit_playing_uses_active_persistence_budget() {
  TEST_ASSERT_EQUAL_UINT32(Config::maxPersistenceMicrosActive,
                           PersistenceBudget::resolveMaxPersistenceMicros(false, true));
}

void test_revision_commit_idle_uses_unbounded_persistence_budget() {
  TEST_ASSERT_EQUAL_UINT32(Config::maxPersistenceMicrosIdle,
                           PersistenceBudget::resolveMaxPersistenceMicros(false, false));
}

void test_persistence_slice_budget_exhausted_during_playing_commit() {
  TEST_ASSERT_TRUE(PersistenceBudget::persistenceSliceBudgetExhausted(
      Config::maxPersistenceMicrosActive, Config::maxPersistenceMicrosActive));
  TEST_ASSERT_FALSE(PersistenceBudget::persistenceSliceBudgetExhausted(
      Config::maxPersistenceMicrosActive, Config::maxPersistenceMicrosActive - 1U));
}

void test_persistence_slice_budget_never_exhausted_when_idle() {
  TEST_ASSERT_FALSE(
      PersistenceBudget::persistenceSliceBudgetExhausted(Config::maxPersistenceMicrosIdle, 1000000U));
}

void test_persistence_slice_budget_idle_capped_per_loop() {
  TEST_ASSERT_EQUAL_UINT32(
      Config::maxPersistenceMicrosPerLoop,
      PersistenceBudget::resolvePersistenceSliceBudgetUs(false, false));
  TEST_ASSERT_TRUE(PersistenceBudget::persistenceSliceBudgetExhausted(
      Config::maxPersistenceMicrosPerLoop, Config::maxPersistenceMicrosPerLoop));
}

void test_revision_commit_write_path_does_not_materialize() {
  TEST_ASSERT_FALSE(RevisionCommitPolicy::kWritePathUsesLoopPassesMaterialize);
}

void test_revision_commit_loop_slot_streams_via_storage_loop_io() {
  TEST_ASSERT_TRUE(RevisionCommitPolicy::kLoopSlotBodyUsesStorageLoopIoStream);
}

void test_dirty_load_request_shows_prompt_when_workspace_dirty() {
  TEST_ASSERT_EQUAL(RevisionLoadPolicy::LoadRequestGate::ShowDirtyPrompt,
                    RevisionLoadPolicy::resolveLoadRequestGate(true));
}

void test_dirty_load_request_dispatches_when_workspace_clean() {
  TEST_ASSERT_EQUAL(RevisionLoadPolicy::LoadRequestGate::DispatchImmediately,
                    RevisionLoadPolicy::resolveLoadRequestGate(false));
}

void test_save_then_load_dispatches_after_commit_complete() {
  TEST_ASSERT_TRUE(RevisionLoadPolicy::shouldDispatchStagedLoadAfterCommitComplete(true, true));
  TEST_ASSERT_FALSE(RevisionLoadPolicy::shouldDispatchStagedLoadAfterCommitComplete(true, false));
  TEST_ASSERT_FALSE(RevisionLoadPolicy::shouldDispatchStagedLoadAfterCommitComplete(false, true));
}

void test_minimal_loading_overlay_during_pipeline() {
  TEST_ASSERT_TRUE(RevisionLoadPolicy::isMinimalLoadingOverlayActive(true, true, false, false));
  TEST_ASSERT_TRUE(RevisionLoadPolicy::isMinimalLoadingOverlayActive(true, false, true, false));
  TEST_ASSERT_TRUE(RevisionLoadPolicy::isMinimalLoadingOverlayActive(true, false, false, true));
  TEST_ASSERT_FALSE(RevisionLoadPolicy::isMinimalLoadingOverlayActive(false, true, true, true));
  TEST_ASSERT_FALSE(RevisionLoadPolicy::isMinimalLoadingOverlayActive(true, false, false, false));
}

void test_boot_recovery_plan_includes_derived_and_latest() {
  const BootRecoveryPolicy::RevisionRecoveryPlan plan =
      BootRecoveryPolicy::buildRevisionRecoveryPlan(3, 9, 10);
  TEST_ASSERT_EQUAL_UINT16(3, plan.setId);
  TEST_ASSERT_EQUAL_UINT16(9, plan.derivedRevisionId);
  TEST_ASSERT_EQUAL_UINT16(10, plan.latestRevisionId);
}

void test_boot_recovery_plan_skips_without_set_id() {
  const BootRecoveryPolicy::RevisionRecoveryPlan plan =
      BootRecoveryPolicy::buildRevisionRecoveryPlan(0, 9, 10);
  TEST_ASSERT_EQUAL_UINT16(0, plan.setId);
  TEST_ASSERT_EQUAL_UINT16(0, plan.derivedRevisionId);
  TEST_ASSERT_EQUAL_UINT16(0, plan.latestRevisionId);
}

void test_boot_recovery_latest_fallback_when_derived_fails() {
  TEST_ASSERT_EQUAL_UINT16(10, BootRecoveryPolicy::resolveLatestRevisionFallback(9, 10));
  TEST_ASSERT_EQUAL_UINT16(0, BootRecoveryPolicy::resolveLatestRevisionFallback(10, 10));
  TEST_ASSERT_EQUAL_UINT16(10, BootRecoveryPolicy::resolveLatestRevisionFallback(0, 10));
}

bool probeEpochComplete(uint32_t candidateEpoch, void* context) {
  const auto* completeEpochs = static_cast<const uint8_t*>(context);
  if (completeEpochs == nullptr || candidateEpoch == 0 || candidateEpoch > 8) {
    return false;
  }
  return completeEpochs[candidateEpoch] != 0;
}

void test_boot_recovery_resolves_highest_valid_epoch() {
  const uint8_t completeEpochs[9] = {0, 1, 0, 0, 1, 0, 0, 0, 0};
  TEST_ASSERT_EQUAL_UINT32(
      4, BootRecoveryPolicy::resolveHighestValidWorkspaceEpoch(8, probeEpochComplete,
                                                               const_cast<uint8_t*>(completeEpochs)));
  TEST_ASSERT_EQUAL_UINT32(
      0, BootRecoveryPolicy::resolveHighestValidWorkspaceEpoch(0, probeEpochComplete,
                                                               const_cast<uint8_t*>(completeEpochs)));
}

void test_boot_epoch_candidate_rejects_partial_successor() {
  TEST_ASSERT_FALSE(BootRecoveryPolicy::isWorkspaceEpochBootCandidate(
      41, true, true, true));
  TEST_ASSERT_FALSE(BootRecoveryPolicy::isWorkspaceEpochBootCandidate(
      41, false, false, true));
  TEST_ASSERT_TRUE(BootRecoveryPolicy::isWorkspaceEpochBootCandidate(
      41, true, false, true));
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
  RUN_TEST(test_revision_header_sd_file_byte_size);
  RUN_TEST(test_revision_blob_parser_without_heap);
  RUN_TEST(test_revision_slot_index_parse_ignores_zero_chunk_count);
  RUN_TEST(test_revision_header_rejects_bad_magic);
  RUN_TEST(test_epoch_file_bytes_validate_with_header);
  RUN_TEST(test_epoch_file_bytes_legacy_without_header);
  RUN_TEST(test_workspace_dirty_matches_epoch_divergence);
  RUN_TEST(test_revision_snapshot_source_epoch_idle);
  RUN_TEST(test_revision_snapshot_source_epoch_during_deferred_save);
  RUN_TEST(test_revision_snapshot_bumps_live_epoch);
  RUN_TEST(test_last_committed_sync_after_revision_commit_complete_clears_dirty);
  RUN_TEST(test_record_deferred_save_leaves_workspace_dirty_until_commit_complete);
  RUN_TEST(test_revision_commit_playing_uses_active_persistence_budget);
  RUN_TEST(test_revision_commit_idle_uses_unbounded_persistence_budget);
  RUN_TEST(test_persistence_slice_budget_exhausted_during_playing_commit);
  RUN_TEST(test_persistence_slice_budget_never_exhausted_when_idle);
  RUN_TEST(test_persistence_slice_budget_idle_capped_per_loop);
  RUN_TEST(test_revision_commit_write_path_does_not_materialize);
  RUN_TEST(test_revision_commit_loop_slot_streams_via_storage_loop_io);
  RUN_TEST(test_dirty_load_request_shows_prompt_when_workspace_dirty);
  RUN_TEST(test_dirty_load_request_dispatches_when_workspace_clean);
  RUN_TEST(test_save_then_load_dispatches_after_commit_complete);
  RUN_TEST(test_minimal_loading_overlay_during_pipeline);
  RUN_TEST(test_boot_recovery_plan_includes_derived_and_latest);
  RUN_TEST(test_boot_recovery_plan_skips_without_set_id);
  RUN_TEST(test_boot_recovery_latest_fallback_when_derived_fails);
  RUN_TEST(test_boot_recovery_resolves_highest_valid_epoch);
  RUN_TEST(test_boot_epoch_candidate_rejects_partial_successor);
  return UNITY_END();
}
