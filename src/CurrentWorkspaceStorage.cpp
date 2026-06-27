//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "CurrentWorkspaceStorage.h"
#include "CurrentSetStorage.h"

#include <cstdio>
#include <cstring>

#if defined(ARDUINO)
#include <SD.h>
#endif

namespace CurrentWorkspaceStorage {
namespace {

#pragma pack(push, 1)

struct WorkspaceMetaWire {
  uint16_t schemaVersion;
  uint32_t currentEpoch;
  uint32_t lastCommittedEpoch;
  uint16_t derivedFromSetId;
  uint16_t derivedFromRevisionId;
  uint16_t lastCommittedRevisionId;
  uint8_t slotCount;
  SlotSummary slotSummary[kSlotSummaryCount];
  uint64_t updatedUnix;
  uint32_t crc32;
};

struct EpochFileHeaderWire {
  uint32_t epoch;
  uint16_t schemaVersion;
  uint32_t crc32;
};

#pragma pack(pop)

static_assert(sizeof(WorkspaceMetaWire) == kWorkspaceMetaByteSize,
              "WorkspaceMetaWire size mismatch");
static_assert(sizeof(EpochFileHeaderWire) == kEpochFileHeaderByteSize,
              "EpochFileHeaderWire size mismatch");

bool ioWrite(const StorageIo& io, const void* data, size_t size) {
  return io.write && io.write(data, size);
}

bool ioRead(const StorageIo& io, void* data, size_t size) {
  return io.read && io.read(data, size);
}

WorkspaceMetaWire toWire(const WorkspaceMetaRecord& record) {
  WorkspaceMetaWire wire{};
  wire.schemaVersion = record.schemaVersion;
  wire.currentEpoch = record.currentEpoch;
  wire.lastCommittedEpoch = record.lastCommittedEpoch;
  wire.derivedFromSetId = record.derivedFromSetId;
  wire.derivedFromRevisionId = record.derivedFromRevisionId;
  wire.lastCommittedRevisionId = record.lastCommittedRevisionId;
  wire.slotCount = record.slotCount;
  std::memcpy(wire.slotSummary, record.slotSummary, sizeof(wire.slotSummary));
  wire.updatedUnix = record.updatedUnix;
  wire.crc32 = record.crc32;
  return wire;
}

void fromWire(const WorkspaceMetaWire& wire, WorkspaceMetaRecord& record) {
  record.schemaVersion = wire.schemaVersion;
  record.currentEpoch = wire.currentEpoch;
  record.lastCommittedEpoch = wire.lastCommittedEpoch;
  record.derivedFromSetId = wire.derivedFromSetId;
  record.derivedFromRevisionId = wire.derivedFromRevisionId;
  record.lastCommittedRevisionId = wire.lastCommittedRevisionId;
  record.slotCount = wire.slotCount;
  std::memcpy(record.slotSummary, wire.slotSummary, sizeof(record.slotSummary));
  record.updatedUnix = wire.updatedUnix;
  record.crc32 = wire.crc32;
}

EpochFileHeaderWire toWire(const EpochFileHeader& header) {
  EpochFileHeaderWire wire{};
  wire.epoch = header.epoch;
  wire.schemaVersion = header.schemaVersion;
  wire.crc32 = header.crc32;
  return wire;
}

void fromWire(const EpochFileHeaderWire& wire, EpochFileHeader& header) {
  header.epoch = wire.epoch;
  header.schemaVersion = wire.schemaVersion;
  header.crc32 = wire.crc32;
}

}  // namespace

bool formatSlotPath(char* out, size_t outSize, uint8_t slotIndex, bool undoSlot) {
  if (out == nullptr || outSize == 0) {
    return false;
  }
  const char* dir = undoSlot ? kUndoDir : kSlotsDir;
  const int written = std::snprintf(out, outSize, "%s/slot_%02u.bin", dir, slotIndex);
  return written > 0 && static_cast<size_t>(written) < outSize;
}

bool formatSlotTempPath(char* out, size_t outSize, uint8_t slotIndex, bool undoSlot) {
  if (out == nullptr || outSize == 0) {
    return false;
  }
  const char* dir = undoSlot ? kUndoDir : kSlotsDir;
  const int written = std::snprintf(out, outSize, "%s/slot_%02u.bin.tmp", dir, slotIndex);
  return written > 0 && static_cast<size_t>(written) < outSize;
}

uint32_t computeWorkspaceMetaChecksum(const WorkspaceMetaRecord& record) {
  WorkspaceMetaWire wire = toWire(record);
  wire.crc32 = 0;
  return PersistenceSchema::crc32(reinterpret_cast<const uint8_t*>(&wire), sizeof(wire));
}

uint32_t computeEpochFileHeaderChecksum(const EpochFileHeader& header) {
  EpochFileHeaderWire wire = toWire(header);
  wire.crc32 = 0;
  return PersistenceSchema::crc32(reinterpret_cast<const uint8_t*>(&wire), sizeof(wire));
}

bool writeWorkspaceMeta(const StorageIo& io, const WorkspaceMetaRecord& record) {
  WorkspaceMetaRecord stamped = record;
  stamped.crc32 = computeWorkspaceMetaChecksum(record);
  const WorkspaceMetaWire wire = toWire(stamped);
  return ioWrite(io, &wire, sizeof(wire));
}

bool readWorkspaceMeta(const StorageIo& io, WorkspaceMetaRecord& record) {
  WorkspaceMetaWire wire{};
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
  const uint32_t expected = computeWorkspaceMetaChecksum(record);
  record.crc32 = storedCrc;
  return storedCrc == expected;
}

bool writeEpochFileHeader(const StorageIo& io, const EpochFileHeader& header) {
  EpochFileHeader stamped = header;
  stamped.crc32 = computeEpochFileHeaderChecksum(header);
  const EpochFileHeaderWire wire = toWire(stamped);
  return ioWrite(io, &wire, sizeof(wire));
}

bool readEpochFileHeader(const StorageIo& io, EpochFileHeader& header) {
  EpochFileHeaderWire wire{};
  if (!ioRead(io, &wire, sizeof(wire))) {
    return false;
  }
  fromWire(wire, header);
  return PersistenceSchema::isSchemaMajorCompatible(header.schemaVersion,
                                                  PersistenceSchema::kSetRevisionSchemaVersion);
}

bool isWorkspaceDirty(uint32_t currentEpoch, uint32_t lastCommittedEpoch) {
  return currentEpoch != lastCommittedEpoch;
}

bool isEpochHeaderChecksumValid(const EpochFileHeader& header) {
  const uint32_t storedCrc = header.crc32;
  EpochFileHeader probe = header;
  probe.crc32 = 0;
  return storedCrc == computeEpochFileHeaderChecksum(probe);
}

bool epochHeaderMatchesEpoch(const EpochFileHeader& header, uint32_t expectedEpoch) {
  return header.epoch == expectedEpoch;
}

uint32_t computeEpochFileBodyChecksum(const uint8_t* body, size_t bodySize) {
  if (body == nullptr || bodySize == 0) {
    return 0;
  }
  return PersistenceSchema::crc32(body, bodySize);
}

bool validateEpochFileBytes(const uint8_t* fileBytes, size_t fileSize, uint32_t expectedEpoch,
                            bool expectEpochHeader) {
  if (fileBytes == nullptr || fileSize == 0) {
    return false;
  }
  if (!expectEpochHeader) {
    return fileSize >= sizeof(CurrentSetStorage::COMPLETE_MAGIC);
  }
  if (fileSize < kEpochFileHeaderByteSize) {
    return false;
  }
  EpochFileHeaderWire wire{};
  std::memcpy(&wire, fileBytes, sizeof(wire));
  EpochFileHeader header{};
  fromWire(wire, header);
  if (!PersistenceSchema::isSchemaMajorCompatible(header.schemaVersion,
                                                  PersistenceSchema::kSetRevisionSchemaVersion)) {
    return false;
  }
  if (!epochHeaderMatchesEpoch(header, expectedEpoch)) {
    return false;
  }
  const uint32_t expectedBodyCrc =
      computeEpochFileBodyChecksum(fileBytes + kEpochFileHeaderByteSize,
                                   fileSize - kEpochFileHeaderByteSize);
  return header.crc32 == expectedBodyCrc;
}

#if defined(ARDUINO)

bool writeEpochHeaderPlaceholder(File& file, uint32_t epoch) {
  EpochFileHeader header{};
  header.epoch = epoch;
  header.schemaVersion = PersistenceSchema::kSetRevisionSchemaVersion;
  header.crc32 = 0;
  const EpochFileHeaderWire wire = toWire(header);
  return file.write(reinterpret_cast<const uint8_t*>(&wire), sizeof(wire)) == sizeof(wire);
}

bool finalizeEpochFileHeaderCrc(const char* path) {
  if (path == nullptr) {
    return false;
  }
  File file = SD.open(path, FILE_READ);
  if (!file) {
    return false;
  }
  const size_t fileSize = file.size();
  if (fileSize < kEpochFileHeaderByteSize) {
    file.close();
    return false;
  }
  if (!file.seek(kEpochFileHeaderByteSize)) {
    file.close();
    return false;
  }
  uint32_t bodyCrc = 0;
  uint8_t buffer[256];
  while (file.available()) {
    const int bytesRead = file.read(buffer, sizeof(buffer));
    if (bytesRead <= 0) {
      break;
    }
    bodyCrc = PersistenceSchema::crc32Continue(bodyCrc, buffer,
                                               static_cast<size_t>(bytesRead));
  }
  file.close();

  file = SD.open(path, FILE_WRITE);
  if (!file) {
    return false;
  }
  constexpr size_t kCrcFieldOffset = sizeof(uint32_t) + sizeof(uint16_t);
  if (!file.seek(kCrcFieldOffset)) {
    file.close();
    return false;
  }
  const bool ok = file.write(reinterpret_cast<const uint8_t*>(&bodyCrc), sizeof(bodyCrc)) ==
                  sizeof(bodyCrc);
  file.close();
  return ok;
}

bool fileStartsWithEpochHeader(File& file) {
  if (!file.seek(0)) {
    return false;
  }
  uint32_t firstWord = 0;
  if (file.read(reinterpret_cast<uint8_t*>(&firstWord), sizeof(firstWord)) != sizeof(firstWord)) {
    return false;
  }
  if (firstWord == CurrentSetStorage::CONTAINER_VERSION) {
    return false;
  }
  uint16_t schemaVersion = 0;
  if (file.read(reinterpret_cast<uint8_t*>(&schemaVersion), sizeof(schemaVersion)) !=
      sizeof(schemaVersion)) {
    return false;
  }
  if (!file.seek(0)) {
    return false;
  }
  return PersistenceSchema::isSchemaMajorCompatible(schemaVersion,
                                                     PersistenceSchema::kSetRevisionSchemaVersion);
}

bool writeWorkspaceMetaFile(const WorkspaceMetaRecord& record) {
  if (!CurrentSetStorage::ensureDirectory(kCurrentRoot)) {
    return false;
  }
  File file = SD.open(kWorkspaceMetaTempPath, FILE_WRITE);
  if (!file) {
    return false;
  }
  const StorageIo io = StorageIo{
      [&file](const void* data, size_t size) {
        return file.write(static_cast<const uint8_t*>(data), size) == size;
      },
      [](void*, size_t) { return false; },
  };
  const bool wrote = writeWorkspaceMeta(io, record);
  file.close();
  if (!wrote) {
    return false;
  }
  return CurrentSetStorage::atomicRenameTempFile(kWorkspaceMetaTempPath, kWorkspaceMetaPath);
}

bool readWorkspaceMetaFile(WorkspaceMetaRecord& record) {
  File file = SD.open(kWorkspaceMetaPath, FILE_READ);
  if (!file) {
    return false;
  }
  const StorageIo io = StorageIo{
      [](const void*, size_t) { return false; },
      [&file](void* data, size_t size) {
        return file.read(static_cast<uint8_t*>(data), size) == size;
      },
  };
  const bool ok = readWorkspaceMeta(io, record);
  file.close();
  return ok;
}

#endif

}  // namespace CurrentWorkspaceStorage
