//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "SetRevisionCatalog.h"

#include <cstdio>
#include <cstring>

namespace SetRevisionCatalog {
namespace {

#pragma pack(push, 1)

struct SetCatalogIndexWire {
  uint16_t schemaVersion;
  uint16_t reserved;
  uint32_t nextSetId;
  uint32_t setCount;
  uint32_t catalogChecksum;
};

struct SetMetaRecordWire {
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

static_assert(sizeof(SetCatalogIndexWire) == kSetCatalogIndexByteSize,
              "SetCatalogIndexWire size mismatch");
static_assert(sizeof(SetMetaRecordWire) == kSetMetaRecordByteSize,
              "SetMetaRecordWire size mismatch");

bool ioWrite(const StorageIo& io, const void* data, size_t size) {
  return io.write && io.write(data, size);
}

bool ioRead(const StorageIo& io, void* data, size_t size) {
  return io.read && io.read(data, size);
}

SetCatalogIndexWire toWire(const SetCatalogIndex& index) {
  SetCatalogIndexWire wire{};
  wire.schemaVersion = index.schemaVersion;
  wire.reserved = index.reserved;
  wire.nextSetId = index.nextSetId;
  wire.setCount = index.setCount;
  wire.catalogChecksum = index.catalogChecksum;
  return wire;
}

void fromWire(const SetCatalogIndexWire& wire, SetCatalogIndex& index) {
  index.schemaVersion = wire.schemaVersion;
  index.reserved = wire.reserved;
  index.nextSetId = wire.nextSetId;
  index.setCount = wire.setCount;
  index.catalogChecksum = wire.catalogChecksum;
}

SetMetaRecordWire toWire(const SetMetaRecord& record) {
  SetMetaRecordWire wire{};
  wire.schemaVersion = record.schemaVersion;
  wire.setId = record.setId;
  wire.latestRevisionId = record.latestRevisionId;
  wire.revisionCount = record.revisionCount;
  wire.createdUnix = record.createdUnix;
  wire.updatedUnix = record.updatedUnix;
  std::memcpy(wire.subtitle, record.subtitle, sizeof(wire.subtitle));
  wire.favorite = record.favorite;
  std::memcpy(wire.reserved, record.reserved, sizeof(wire.reserved));
  wire.crc32 = record.crc32;
  return wire;
}

void fromWire(const SetMetaRecordWire& wire, SetMetaRecord& record) {
  record.schemaVersion = wire.schemaVersion;
  record.setId = wire.setId;
  record.latestRevisionId = wire.latestRevisionId;
  record.revisionCount = wire.revisionCount;
  record.createdUnix = wire.createdUnix;
  record.updatedUnix = wire.updatedUnix;
  std::memcpy(record.subtitle, wire.subtitle, sizeof(record.subtitle));
  record.favorite = wire.favorite;
  std::memcpy(record.reserved, wire.reserved, sizeof(record.reserved));
  record.crc32 = wire.crc32;
}

}  // namespace

uint32_t computeSetCatalogIndexChecksum(const SetCatalogIndex& index) {
  SetCatalogIndexWire wire = toWire(index);
  wire.catalogChecksum = 0;
  return PersistenceSchema::crc32(reinterpret_cast<const uint8_t*>(&wire), sizeof(wire));
}

uint32_t computeSetMetaChecksum(const SetMetaRecord& record) {
  SetMetaRecordWire wire = toWire(record);
  wire.crc32 = 0;
  return PersistenceSchema::crc32(reinterpret_cast<const uint8_t*>(&wire), sizeof(wire));
}

bool writeSetCatalogIndex(const StorageIo& io, const SetCatalogIndex& index) {
  SetCatalogIndex stamped = index;
  stamped.catalogChecksum = computeSetCatalogIndexChecksum(index);
  const SetCatalogIndexWire wire = toWire(stamped);
  return ioWrite(io, &wire, sizeof(wire));
}

bool readSetCatalogIndex(const StorageIo& io, SetCatalogIndex& index) {
  SetCatalogIndexWire wire{};
  if (!ioRead(io, &wire, sizeof(wire))) {
    return false;
  }
  fromWire(wire, index);
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
  const SetMetaRecordWire wire = toWire(stamped);
  return ioWrite(io, &wire, sizeof(wire));
}

bool readSetMetaRecord(const StorageIo& io, SetMetaRecord& record) {
  SetMetaRecordWire wire{};
  if (!ioRead(io, &wire, sizeof(wire))) {
    return false;
  }
  fromWire(wire, record);
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
