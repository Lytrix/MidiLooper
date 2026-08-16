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

struct WorkspaceMetaFileLayout {
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

struct EpochFileHeaderFileLayout {
  uint32_t epoch;
  uint16_t schemaVersion;
  uint32_t crc32;
};

#pragma pack(pop)

static_assert(sizeof(WorkspaceMetaFileLayout) == kWorkspaceMetaByteSize,
              "WorkspaceMetaFileLayout size mismatch");
static_assert(sizeof(EpochFileHeaderFileLayout) == kEpochFileHeaderByteSize,
              "EpochFileHeaderFileLayout size mismatch");

bool ioWrite(const StorageIo& io, const void* data, size_t size) {
  return io.write && io.write(data, size);
}

bool ioRead(const StorageIo& io, void* data, size_t size) {
  return io.read && io.read(data, size);
}

WorkspaceMetaFileLayout toFileLayout(const WorkspaceMetaRecord& record) {
  WorkspaceMetaFileLayout fileLayout{};
  fileLayout.schemaVersion = record.schemaVersion;
  fileLayout.currentEpoch = record.currentEpoch;
  fileLayout.lastCommittedEpoch = record.lastCommittedEpoch;
  fileLayout.derivedFromSetId = record.derivedFromSetId;
  fileLayout.derivedFromRevisionId = record.derivedFromRevisionId;
  fileLayout.lastCommittedRevisionId = record.lastCommittedRevisionId;
  fileLayout.slotCount = record.slotCount;
  std::memcpy(fileLayout.slotSummary, record.slotSummary, sizeof(fileLayout.slotSummary));
  fileLayout.updatedUnix = record.updatedUnix;
  fileLayout.crc32 = record.crc32;
  return fileLayout;
}

void fromFileLayout(const WorkspaceMetaFileLayout& fileLayout, WorkspaceMetaRecord& record) {
  record.schemaVersion = fileLayout.schemaVersion;
  record.currentEpoch = fileLayout.currentEpoch;
  record.lastCommittedEpoch = fileLayout.lastCommittedEpoch;
  record.derivedFromSetId = fileLayout.derivedFromSetId;
  record.derivedFromRevisionId = fileLayout.derivedFromRevisionId;
  record.lastCommittedRevisionId = fileLayout.lastCommittedRevisionId;
  record.slotCount = fileLayout.slotCount;
  std::memcpy(record.slotSummary, fileLayout.slotSummary, sizeof(record.slotSummary));
  record.updatedUnix = fileLayout.updatedUnix;
  record.crc32 = fileLayout.crc32;
}

EpochFileHeaderFileLayout toFileLayout(const EpochFileHeader& header) {
  EpochFileHeaderFileLayout fileLayout{};
  fileLayout.epoch = header.epoch;
  fileLayout.schemaVersion = header.schemaVersion;
  fileLayout.crc32 = header.crc32;
  return fileLayout;
}

void fromFileLayout(const EpochFileHeaderFileLayout& fileLayout, EpochFileHeader& header) {
  header.epoch = fileLayout.epoch;
  header.schemaVersion = fileLayout.schemaVersion;
  header.crc32 = fileLayout.crc32;
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
  WorkspaceMetaFileLayout fileLayout = toFileLayout(record);
  fileLayout.crc32 = 0;
  return PersistenceSchema::crc32(reinterpret_cast<const uint8_t*>(&fileLayout), sizeof(fileLayout));
}

uint32_t computeEpochFileHeaderChecksum(const EpochFileHeader& header) {
  EpochFileHeaderFileLayout fileLayout = toFileLayout(header);
  fileLayout.crc32 = 0;
  return PersistenceSchema::crc32(reinterpret_cast<const uint8_t*>(&fileLayout), sizeof(fileLayout));
}

bool writeWorkspaceMeta(const StorageIo& io, const WorkspaceMetaRecord& record) {
  WorkspaceMetaRecord stamped = record;
  stamped.crc32 = computeWorkspaceMetaChecksum(record);
  const WorkspaceMetaFileLayout fileLayout = toFileLayout(stamped);
  return ioWrite(io, &fileLayout, sizeof(fileLayout));
}

bool readWorkspaceMeta(const StorageIo& io, WorkspaceMetaRecord& record) {
  WorkspaceMetaFileLayout fileLayout{};
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
  const uint32_t expected = computeWorkspaceMetaChecksum(record);
  record.crc32 = storedCrc;
  return storedCrc == expected;
}

bool writeEpochFileHeader(const StorageIo& io, const EpochFileHeader& header) {
  EpochFileHeader stamped = header;
  stamped.crc32 = computeEpochFileHeaderChecksum(header);
  const EpochFileHeaderFileLayout fileLayout = toFileLayout(stamped);
  return ioWrite(io, &fileLayout, sizeof(fileLayout));
}

bool readEpochFileHeader(const StorageIo& io, EpochFileHeader& header) {
  EpochFileHeaderFileLayout fileLayout{};
  if (!ioRead(io, &fileLayout, sizeof(fileLayout))) {
    return false;
  }
  fromFileLayout(fileLayout, header);
  return PersistenceSchema::isSchemaMajorCompatible(header.schemaVersion,
                                                  PersistenceSchema::kSetRevisionSchemaVersion);
}

bool isWorkspaceDirty(uint32_t currentEpoch, uint32_t lastCommittedEpoch) {
  return currentEpoch != lastCommittedEpoch;
}

uint32_t resolveCompletedWorkspaceEpochForRevisionSnapshot(uint32_t currentWorkspaceEpoch,
                                                           uint32_t deferredSaveWorkspaceEpoch,
                                                           bool deferredSaveInProgress) {
  if (deferredSaveInProgress && deferredSaveWorkspaceEpoch > 0) {
    return deferredSaveWorkspaceEpoch - 1U;
  }
  return currentWorkspaceEpoch;
}

uint32_t workspaceEpochAfterRevisionSnapshot(uint32_t snapshotSourceEpoch) {
  return snapshotSourceEpoch + 1U;
}

uint32_t syncLastCommittedEpochAfterRevisionCommitComplete(uint32_t currentWorkspaceEpoch) {
  return currentWorkspaceEpoch;
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

bool continueEpochFileBodyCrc(const uint8_t* body, size_t bodySize, uint32_t& crc, size_t& offset,
                              size_t maxBytes, bool& done) {
  done = false;
  if (bodySize == 0) {
    done = true;
    return true;
  }
  if (body == nullptr || offset > bodySize) {
    return false;
  }
  if (offset == bodySize) {
    done = true;
    return true;
  }
  const size_t remaining = bodySize - offset;
  const size_t n = remaining < maxBytes ? remaining : maxBytes;
  crc = PersistenceSchema::crc32Continue(crc, body + offset, n);
  offset += n;
  done = offset >= bodySize;
  return true;
}

bool validateEpochFileBytes(const uint8_t* fileBytes, size_t fileSize, uint32_t expectedEpoch,
                            bool expectEpochHeader) {
  if (fileBytes == nullptr || fileSize == 0) {
    return false;
  }
  if (!expectEpochHeader) {
    return fileSize >= sizeof(CurrentSetStorage::kSaveFileToken);
  }
  if (fileSize < kEpochFileHeaderByteSize) {
    return false;
  }
  EpochFileHeaderFileLayout fileLayout{};
  std::memcpy(&fileLayout, fileBytes, sizeof(fileLayout));
  EpochFileHeader header{};
  fromFileLayout(fileLayout, header);
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
  const EpochFileHeaderFileLayout fileLayout = toFileLayout(header);
  return file.write(reinterpret_cast<const uint8_t*>(&fileLayout), sizeof(fileLayout)) == sizeof(fileLayout);
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
  return writeEpochFileHeaderCrc(path, bodyCrc);
}

bool writeEpochFileHeaderCrc(const char* path, uint32_t bodyCrc) {
  if (path == nullptr) {
    return false;
  }
  File file = SD.open(path, FILE_WRITE);
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

bool validateEpochFileOnSd(const char* path, uint32_t expectedEpoch) {
  if (path == nullptr) {
    return false;
  }
  File file = SD.open(path, FILE_READ);
  if (!file) {
    return false;
  }
  const size_t fileSize = file.size();
  if (fileSize < sizeof(CurrentSetStorage::kSaveFileToken)) {
    file.close();
    return false;
  }

  size_t bodyOffset = 0;
  if (fileStartsWithEpochHeader(file)) {
    EpochFileHeader header{};
    const StorageIo epochIo = StorageIo{
        [](const void*, size_t) { return false; },
        [&file](void* data, size_t size) {
          return file.read(static_cast<uint8_t*>(data), size) == size;
        },
    };
    if (!readEpochFileHeader(epochIo, header)) {
      file.close();
      return false;
    }
    if (!epochHeaderMatchesEpoch(header, expectedEpoch)) {
      file.close();
      return false;
    }
    bodyOffset = kEpochFileHeaderByteSize;
    if (!file.seek(bodyOffset)) {
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
    if (header.crc32 != bodyCrc) {
      file.close();
      return false;
    }
  } else if (expectedEpoch != 0) {
    file.close();
    return false;
  }

  file.close();
  return CurrentSetStorage::verifySaveFileTokenAtPath(path);
}

bool validateEpochFileOnSdQuick(const char* path, uint32_t expectedEpoch) {
  if (path == nullptr) {
    return false;
  }
  File file = SD.open(path, FILE_READ);
  if (!file) {
    return false;
  }
  const size_t fileSize = file.size();
  if (fileSize < sizeof(CurrentSetStorage::kSaveFileToken)) {
    file.close();
    return false;
  }

  if (fileStartsWithEpochHeader(file)) {
    EpochFileHeader header{};
    const StorageIo epochIo = StorageIo{
        [](const void*, size_t) { return false; },
        [&file](void* data, size_t size) {
          return file.read(static_cast<uint8_t*>(data), size) == size;
        },
    };
    if (!readEpochFileHeader(epochIo, header)) {
      file.close();
      return false;
    }
    if (!epochHeaderMatchesEpoch(header, expectedEpoch) || header.crc32 == 0) {
      file.close();
      return false;
    }
  } else if (expectedEpoch != 0) {
    file.close();
    return false;
  }

  file.close();
  return CurrentSetStorage::verifySaveFileTokenAtPath(path);
}

#endif

}  // namespace CurrentWorkspaceStorage
