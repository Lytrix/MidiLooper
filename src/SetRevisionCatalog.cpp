//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "SetRevisionCatalog.h"

#include <cstdio>
#include <cstring>

namespace SetRevisionCatalog {
namespace {

#pragma pack(push, 1)

struct SetCatalogIndexFileLayout {
  uint16_t schemaVersion;
  uint16_t reserved;
  uint32_t nextSetId;
  uint32_t setCount;
  uint32_t catalogChecksum;
};

struct SetMetaRecordFileLayout {
  uint16_t schemaVersion;
  uint16_t setId;
  uint16_t latestRevisionId;
  uint16_t revisionCount;
  uint64_t createdUnix;
  uint64_t updatedUnix;
  char subtitle[kSetSubtitleCapacity];
  uint8_t favorite;
  uint8_t reserved[15];
  uint32_t crc32;
};

#pragma pack(pop)

static_assert(sizeof(SetCatalogIndexFileLayout) == kSetCatalogIndexByteSize,
              "SetCatalogIndexFileLayout size mismatch");
static_assert(sizeof(SetMetaRecordFileLayout) == kSetMetaRecordByteSize,
              "SetMetaRecordFileLayout size mismatch");

bool ioWrite(const StorageIo& io, const void* data, size_t size) {
  return io.write && io.write(data, size);
}

bool ioRead(const StorageIo& io, void* data, size_t size) {
  return io.read && io.read(data, size);
}

SetCatalogIndexFileLayout toFileLayout(const SetCatalogIndex& index) {
  SetCatalogIndexFileLayout fileLayout{};
  fileLayout.schemaVersion = index.schemaVersion;
  fileLayout.reserved = index.reserved;
  fileLayout.nextSetId = index.nextSetId;
  fileLayout.setCount = index.setCount;
  fileLayout.catalogChecksum = index.catalogChecksum;
  return fileLayout;
}

void fromFileLayout(const SetCatalogIndexFileLayout& fileLayout, SetCatalogIndex& index) {
  index.schemaVersion = fileLayout.schemaVersion;
  index.reserved = fileLayout.reserved;
  index.nextSetId = fileLayout.nextSetId;
  index.setCount = fileLayout.setCount;
  index.catalogChecksum = fileLayout.catalogChecksum;
}

SetMetaRecordFileLayout toFileLayout(const SetMetaRecord& record) {
  SetMetaRecordFileLayout fileLayout{};
  fileLayout.schemaVersion = record.schemaVersion;
  fileLayout.setId = record.setId;
  fileLayout.latestRevisionId = record.latestRevisionId;
  fileLayout.revisionCount = record.revisionCount;
  fileLayout.createdUnix = record.createdUnix;
  fileLayout.updatedUnix = record.updatedUnix;
  std::memcpy(fileLayout.subtitle, record.subtitle, sizeof(fileLayout.subtitle));
  fileLayout.favorite = record.favorite;
  std::memcpy(fileLayout.reserved, record.reserved, sizeof(fileLayout.reserved));
  fileLayout.crc32 = record.crc32;
  return fileLayout;
}

void fromFileLayout(const SetMetaRecordFileLayout& fileLayout, SetMetaRecord& record) {
  record.schemaVersion = fileLayout.schemaVersion;
  record.setId = fileLayout.setId;
  record.latestRevisionId = fileLayout.latestRevisionId;
  record.revisionCount = fileLayout.revisionCount;
  record.createdUnix = fileLayout.createdUnix;
  record.updatedUnix = fileLayout.updatedUnix;
  std::memcpy(record.subtitle, fileLayout.subtitle, sizeof(record.subtitle));
  record.favorite = fileLayout.favorite;
  std::memcpy(record.reserved, fileLayout.reserved, sizeof(record.reserved));
  record.crc32 = fileLayout.crc32;
}

}  // namespace

uint32_t computeSetCatalogIndexChecksum(const SetCatalogIndex& index) {
  SetCatalogIndexFileLayout fileLayout = toFileLayout(index);
  fileLayout.catalogChecksum = 0;
  return PersistenceSchema::crc32(reinterpret_cast<const uint8_t*>(&fileLayout), sizeof(fileLayout));
}

uint32_t computeSetMetaChecksum(const SetMetaRecord& record) {
  SetMetaRecordFileLayout fileLayout = toFileLayout(record);
  fileLayout.crc32 = 0;
  return PersistenceSchema::crc32(reinterpret_cast<const uint8_t*>(&fileLayout), sizeof(fileLayout));
}

bool writeSetCatalogIndex(const StorageIo& io, const SetCatalogIndex& index) {
  SetCatalogIndex stamped = index;
  stamped.catalogChecksum = computeSetCatalogIndexChecksum(index);
  const SetCatalogIndexFileLayout fileLayout = toFileLayout(stamped);
  return ioWrite(io, &fileLayout, sizeof(fileLayout));
}

bool readSetCatalogIndex(const StorageIo& io, SetCatalogIndex& index) {
  SetCatalogIndexFileLayout fileLayout{};
  if (!ioRead(io, &fileLayout, sizeof(fileLayout))) {
    return false;
  }
  fromFileLayout(fileLayout, index);
  if (!PersistenceSchema::isSchemaMajorCompatible(index.schemaVersion,
                                                  PersistenceSchema::kSetRevisionSchemaVersion)) {
    return false;
  }
  if (index.nextSetId == 0) {
    index.nextSetId = 1;
  }
  const uint32_t storedChecksum = index.catalogChecksum;
  index.catalogChecksum = 0;
  const uint32_t expected = computeSetCatalogIndexChecksum(index);
  index.catalogChecksum = storedChecksum;
  return storedChecksum == expected;
}

bool writeSetMetaRecord(const StorageIo& io, const SetMetaRecord& record) {
  SetMetaRecord stamped = record;
  stamped.crc32 = computeSetMetaChecksum(record);
  const SetMetaRecordFileLayout fileLayout = toFileLayout(stamped);
  return ioWrite(io, &fileLayout, sizeof(fileLayout));
}

bool readSetMetaRecord(const StorageIo& io, SetMetaRecord& record) {
  SetMetaRecordFileLayout fileLayout{};
  if (!ioRead(io, &fileLayout, sizeof(fileLayout))) {
    return false;
  }
  fromFileLayout(fileLayout, record);
  if (!PersistenceSchema::isSchemaMajorCompatible(record.schemaVersion,
                                                  PersistenceSchema::kSetRevisionSchemaVersion)) {
    return false;
  }
  const uint32_t storedCrc = record.crc32;
  record.crc32 = 0;
  const uint32_t expected = computeSetMetaChecksum(record);
  record.crc32 = storedCrc;
  return storedCrc == expected;
}

uint16_t allocateNextSetId(SetCatalogIndex& index) {
  if (index.nextSetId == 0) {
    index.nextSetId = 1;
  }
  const uint16_t allocated = static_cast<uint16_t>(index.nextSetId);
  ++index.nextSetId;
  ++index.setCount;
  return allocated;
}

uint16_t peekNextRevisionId(const SetMetaRecord& meta) {
  if (meta.revisionCount == 0) {
    return 1;
  }
  return static_cast<uint16_t>(meta.latestRevisionId + 1U);
}

void applyValidatedRevisionToSetMeta(SetMetaRecord& meta, uint16_t revisionId,
                                     uint64_t updatedUnix) {
  meta.latestRevisionId = revisionId;
  if (meta.revisionCount == 0) {
    meta.createdUnix = updatedUnix;
  }
  ++meta.revisionCount;
  meta.updatedUnix = updatedUnix;
}

bool formatSetFolderPath(char* out, size_t outSize, uint16_t setId) {
  if (out == nullptr || outSize == 0) {
    return false;
  }
  const int written = std::snprintf(out, outSize, "%s/S%04u", kSetsRoot, setId);
  return written > 0 && static_cast<size_t>(written) < outSize;
}

bool formatSetMetaPath(char* out, size_t outSize, uint16_t setId) {
  if (out == nullptr || outSize == 0) {
    return false;
  }
  const int written = std::snprintf(out, outSize, "%s/S%04u/set.bin", kSetsRoot, setId);
  return written > 0 && static_cast<size_t>(written) < outSize;
}

bool formatRevisionPath(char* out, size_t outSize, uint16_t setId, uint16_t revisionId,
                        bool tempFile) {
  if (out == nullptr || outSize == 0) {
    return false;
  }
  const char* suffix = tempFile ? kRevisionTempSuffix : "";
  const int written = std::snprintf(out, outSize, "%s/S%04u/revisions/v%04u%s%s", kSetsRoot, setId,
                                  revisionId, kRevisionFileExtension, suffix);
  return written > 0 && static_cast<size_t>(written) < outSize;
}

}  // namespace SetRevisionCatalog
