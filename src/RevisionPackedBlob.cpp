//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "RevisionPackedBlob.h"

#include <cstring>

namespace RevisionPackedBlob {
namespace {

#pragma pack(push, 1)

struct RevisionHeaderFileLayout {
  char magic[8];
  uint16_t schemaVersion;
  uint16_t headerSize;
  uint16_t revisionId;
  uint16_t setId;
  uint32_t sourceEpoch;
  uint64_t createdUnix;
  uint16_t chunkCount;
  uint16_t reserved0;
  uint32_t payloadSize;
  uint32_t headerCrc32;
  uint32_t workspaceFlags;
  uint8_t reserved[84];
};

struct RevisionChunkHeaderFileLayout {
  uint8_t type;
  uint8_t trackIndex;
  uint8_t slotIndex;
  uint8_t reserved;
  uint32_t bodyLength;
};

struct RevisionLoopSlotDirectoryEntryFileLayout {
  uint8_t trackIndex;
  uint8_t slotIndex;
  uint8_t occupied;
  uint8_t reserved0;
  uint32_t chunkOffset;
  uint32_t bodyLength;
  uint32_t loopLengthTicks;
  uint16_t noteCount;
  uint16_t bars;
  uint8_t reserved1[12];
};

struct RevisionFooterFileLayout {
  uint32_t svokToken;
  uint32_t payloadCrc32;
  uint32_t fileSize;
};

#pragma pack(pop)

static_assert(sizeof(RevisionHeaderFileLayout) == kRevisionHeaderByteSize,
              "RevisionHeaderFileLayout size mismatch");
static_assert(sizeof(RevisionChunkHeaderFileLayout) == kChunkHeaderByteSize, "RevisionChunkHeaderFileLayout size mismatch");
static_assert(sizeof(RevisionLoopSlotDirectoryEntryFileLayout) == kRevisionLoopSlotDirectoryEntryByteSize,
              "RevisionLoopSlotDirectoryEntryFileLayout size mismatch");
static_assert(sizeof(RevisionFooterFileLayout) == kRevisionFooterByteSize,
              "RevisionFooterFileLayout size mismatch");

bool ioWrite(const StorageIo& io, const void* data, size_t size) {
  return io.write && io.write(data, size);
}

bool ioRead(const StorageIo& io, void* data, size_t size) {
  return io.read && io.read(data, size);
}

RevisionHeaderFileLayout toFileLayout(const RevisionHeader& header) {
  RevisionHeaderFileLayout fileLayout{};
  std::memcpy(fileLayout.magic, header.magic, sizeof(fileLayout.magic));
  fileLayout.schemaVersion = header.schemaVersion;
  fileLayout.headerSize = header.headerSize;
  fileLayout.revisionId = header.revisionId;
  fileLayout.setId = header.setId;
  fileLayout.sourceEpoch = header.sourceEpoch;
  fileLayout.createdUnix = header.createdUnix;
  fileLayout.chunkCount = header.chunkCount;
  fileLayout.reserved0 = header.reserved0;
  fileLayout.payloadSize = header.payloadSize;
  fileLayout.headerCrc32 = header.headerCrc32;
  fileLayout.workspaceFlags = header.workspaceFlags;
  return fileLayout;
}

void fromFileLayout(const RevisionHeaderFileLayout& fileLayout, RevisionHeader& header) {
  std::memcpy(header.magic, fileLayout.magic, sizeof(header.magic));
  header.schemaVersion = fileLayout.schemaVersion;
  header.headerSize = fileLayout.headerSize;
  header.revisionId = fileLayout.revisionId;
  header.setId = fileLayout.setId;
  header.sourceEpoch = fileLayout.sourceEpoch;
  header.createdUnix = fileLayout.createdUnix;
  header.chunkCount = fileLayout.chunkCount;
  header.reserved0 = fileLayout.reserved0;
  header.payloadSize = fileLayout.payloadSize;
  header.headerCrc32 = fileLayout.headerCrc32;
  header.workspaceFlags = fileLayout.workspaceFlags;
}

RevisionChunkHeaderFileLayout toFileLayout(const ChunkHeader& chunkHeader) {
  RevisionChunkHeaderFileLayout fileLayout{};
  fileLayout.type = chunkHeader.type;
  fileLayout.trackIndex = chunkHeader.trackIndex;
  fileLayout.slotIndex = chunkHeader.slotIndex;
  fileLayout.reserved = chunkHeader.reserved;
  fileLayout.bodyLength = chunkHeader.bodyLength;
  return fileLayout;
}

void fromFileLayout(const RevisionChunkHeaderFileLayout& fileLayout, ChunkHeader& chunkHeader) {
  chunkHeader.type = fileLayout.type;
  chunkHeader.trackIndex = fileLayout.trackIndex;
  chunkHeader.slotIndex = fileLayout.slotIndex;
  chunkHeader.reserved = fileLayout.reserved;
  chunkHeader.bodyLength = fileLayout.bodyLength;
}

RevisionLoopSlotDirectoryEntryFileLayout toFileLayout(const RevisionLoopSlotDirectoryEntry& entry) {
  RevisionLoopSlotDirectoryEntryFileLayout fileLayout{};
  fileLayout.trackIndex = entry.trackIndex;
  fileLayout.slotIndex = entry.slotIndex;
  fileLayout.occupied = entry.occupied;
  fileLayout.reserved0 = entry.reserved0;
  fileLayout.chunkOffset = entry.chunkOffset;
  fileLayout.bodyLength = entry.bodyLength;
  fileLayout.loopLengthTicks = entry.loopLengthTicks;
  fileLayout.noteCount = entry.noteCount;
  fileLayout.bars = entry.bars;
  std::memcpy(fileLayout.reserved1, entry.reserved1, sizeof(fileLayout.reserved1));
  return fileLayout;
}

void fromFileLayout(const RevisionLoopSlotDirectoryEntryFileLayout& fileLayout, RevisionLoopSlotDirectoryEntry& entry) {
  entry.trackIndex = fileLayout.trackIndex;
  entry.slotIndex = fileLayout.slotIndex;
  entry.occupied = fileLayout.occupied;
  entry.reserved0 = fileLayout.reserved0;
  entry.chunkOffset = fileLayout.chunkOffset;
  entry.bodyLength = fileLayout.bodyLength;
  entry.loopLengthTicks = fileLayout.loopLengthTicks;
  entry.noteCount = fileLayout.noteCount;
  entry.bars = fileLayout.bars;
  std::memcpy(entry.reserved1, fileLayout.reserved1, sizeof(entry.reserved1));
}

bool magicMatches(const char* magic) {
  return std::memcmp(magic, kRevisionMagic, sizeof(kRevisionMagic)) == 0;
}

bool findSlotIndexChunkBody(const uint8_t* fileBytes, size_t fileSize,
                            const RevisionHeader& header, const uint8_t*& bodyOut,
                            size_t& bodySizeOut) {
  if (fileBytes == nullptr || header.payloadSize == 0) {
    return false;
  }
  const size_t payloadOffset = kRevisionHeaderByteSize;
  if (payloadOffset + header.payloadSize > fileSize) {
    return false;
  }
  const uint8_t* payload = fileBytes + payloadOffset;
  size_t cursor = 0;
  while (cursor + kChunkHeaderByteSize <= header.payloadSize) {
    RevisionChunkHeaderFileLayout chunkFileLayout{};
    std::memcpy(&chunkFileLayout, payload + cursor, sizeof(chunkFileLayout));
    const size_t bodyStart = cursor + kChunkHeaderByteSize;
    const size_t bodyEnd = bodyStart + static_cast<size_t>(chunkFileLayout.bodyLength);
    if (bodyEnd > header.payloadSize) {
      return false;
    }
    if (chunkFileLayout.type == static_cast<uint8_t>(ChunkType::SlotIndex)) {
      bodyOut = payload + bodyStart;
      bodySizeOut = chunkFileLayout.bodyLength;
      return true;
    }
    cursor = bodyEnd;
  }
  return false;
}

}  // namespace

uint32_t computeRevisionHeaderChecksum(const RevisionHeader& header) {
  RevisionHeaderFileLayout fileLayout = toFileLayout(header);
  fileLayout.headerCrc32 = 0;
  return PersistenceSchema::crc32(reinterpret_cast<const uint8_t*>(&fileLayout), sizeof(fileLayout));
}

uint32_t computeRevisionPayloadChecksum(const uint8_t* payload, size_t payloadSize) {
  if (payload == nullptr || payloadSize == 0) {
    return 0;
  }
  return PersistenceSchema::crc32(payload, payloadSize);
}

bool writeRevisionHeader(const StorageIo& io, const RevisionHeader& header) {
  RevisionHeader stamped = header;
  if (stamped.headerSize == 0) {
    stamped.headerSize = kRevisionHeaderSize;
  }
  if (stamped.magic[0] == '\0') {
    std::memcpy(stamped.magic, kRevisionMagic, sizeof(stamped.magic));
  }
  stamped.headerCrc32 = computeRevisionHeaderChecksum(stamped);
  const RevisionHeaderFileLayout fileLayout = toFileLayout(stamped);
  return ioWrite(io, &fileLayout, sizeof(fileLayout));
}

bool readRevisionHeader(const StorageIo& io, RevisionHeader& header) {
  RevisionHeaderFileLayout fileLayout{};
  if (!ioRead(io, &fileLayout, sizeof(fileLayout))) {
    return false;
  }
  fromFileLayout(fileLayout, header);
  if (!magicMatches(header.magic)) {
    return false;
  }
  if (header.headerSize != kRevisionHeaderSize) {
    return false;
  }
  if (!PersistenceSchema::isSchemaMajorCompatible(header.schemaVersion,
                                                  PersistenceSchema::kSetRevisionSchemaVersion)) {
    return false;
  }
  const uint32_t storedCrc = header.headerCrc32;
  header.headerCrc32 = 0;
  const uint32_t expected = computeRevisionHeaderChecksum(header);
  header.headerCrc32 = storedCrc;
  return storedCrc == expected;
}

bool writeChunkHeader(const StorageIo& io, const ChunkHeader& chunkHeader) {
  const RevisionChunkHeaderFileLayout fileLayout = toFileLayout(chunkHeader);
  return ioWrite(io, &fileLayout, sizeof(fileLayout));
}

bool readChunkHeader(const StorageIo& io, ChunkHeader& chunkHeader) {
  RevisionChunkHeaderFileLayout fileLayout{};
  if (!ioRead(io, &fileLayout, sizeof(fileLayout))) {
    return false;
  }
  fromFileLayout(fileLayout, chunkHeader);
  return true;
}

bool writeRevisionLoopSlotDirectoryEntry(const StorageIo& io, const RevisionLoopSlotDirectoryEntry& entry) {
  const RevisionLoopSlotDirectoryEntryFileLayout fileLayout = toFileLayout(entry);
  return ioWrite(io, &fileLayout, sizeof(fileLayout));
}

bool revisionLoopSlotDirectoryEntryFileBytes(const RevisionLoopSlotDirectoryEntry& entry, uint8_t* out, size_t outSize) {
  if (out == nullptr || outSize < kRevisionLoopSlotDirectoryEntryByteSize) {
    return false;
  }
  const RevisionLoopSlotDirectoryEntryFileLayout fileLayout = toFileLayout(entry);
  std::memcpy(out, &fileLayout, sizeof(fileLayout));
  return true;
}

bool revisionChunkHeaderFileBytes(const ChunkHeader& chunkHeader, uint8_t* out, size_t outSize) {
  if (out == nullptr || outSize < kChunkHeaderByteSize) {
    return false;
  }
  const RevisionChunkHeaderFileLayout fileLayout = toFileLayout(chunkHeader);
  std::memcpy(out, &fileLayout, sizeof(fileLayout));
  return true;
}

bool readRevisionLoopSlotDirectoryEntry(const StorageIo& io, RevisionLoopSlotDirectoryEntry& entry) {
  RevisionLoopSlotDirectoryEntryFileLayout fileLayout{};
  if (!ioRead(io, &fileLayout, sizeof(fileLayout))) {
    return false;
  }
  fromFileLayout(fileLayout, entry);
  return true;
}

bool writeRevisionFooter(const StorageIo& io, const RevisionFooter& footer) {
  const RevisionFooterFileLayout fileLayout{footer.svokToken, footer.payloadCrc32, footer.fileSize};
  return ioWrite(io, &fileLayout, sizeof(fileLayout));
}

bool readRevisionFooterAtOffset(const uint8_t* fileBytes, size_t fileSize,
                                RevisionFooter& footer) {
  if (fileBytes == nullptr || fileSize < kRevisionFooterByteSize) {
    return false;
  }
  const size_t footerOffset = fileSize - kRevisionFooterByteSize;
  RevisionFooterFileLayout fileLayout{};
  std::memcpy(&fileLayout, fileBytes + footerOffset, sizeof(fileLayout));
  footer.svokToken = fileLayout.svokToken;
  footer.payloadCrc32 = fileLayout.payloadCrc32;
  footer.fileSize = fileLayout.fileSize;
  return footer.svokToken == kRevisionSvokFileToken && footer.fileSize == fileSize;
}

bool parseRevisionHeaderFromBytes(const uint8_t* fileBytes, size_t fileSize,
                                  RevisionHeader& headerOut) {
  if (fileBytes == nullptr || fileSize < kRevisionHeaderByteSize) {
    return false;
  }
  RevisionHeaderFileLayout fileLayout{};
  std::memcpy(&fileLayout, fileBytes, sizeof(fileLayout));
  fromFileLayout(fileLayout, headerOut);
  if (!magicMatches(headerOut.magic)) {
    return false;
  }
  if (headerOut.headerSize != kRevisionHeaderSize) {
    return false;
  }
  if (!PersistenceSchema::isSchemaMajorCompatible(
          headerOut.schemaVersion, PersistenceSchema::kSetRevisionSchemaVersion)) {
    return false;
  }
  const uint32_t storedCrc = headerOut.headerCrc32;
  headerOut.headerCrc32 = 0;
  const uint32_t expected = computeRevisionHeaderChecksum(headerOut);
  headerOut.headerCrc32 = storedCrc;
  return storedCrc == expected;
}

bool readRevisionLoopSlotDirectoryEntryFromBytes(const uint8_t* slotIndexBody, size_t slotIndexBodySize,
                                 uint16_t index, RevisionLoopSlotDirectoryEntry& entryOut) {
  if (slotIndexBody == nullptr || slotIndexBodySize < kSlotIndexBodyPrefixByteSize) {
    return false;
  }
  uint16_t entryCount = 0;
  std::memcpy(&entryCount, slotIndexBody, sizeof(entryCount));
  if (index >= entryCount) {
    return false;
  }
  const size_t entryOffset =
      kSlotIndexBodyPrefixByteSize + static_cast<size_t>(index) * kRevisionLoopSlotDirectoryEntryByteSize;
  if (entryOffset + kRevisionLoopSlotDirectoryEntryByteSize > slotIndexBodySize) {
    return false;
  }
  RevisionLoopSlotDirectoryEntryFileLayout fileLayout{};
  std::memcpy(&fileLayout, slotIndexBody + entryOffset, sizeof(fileLayout));
  fromFileLayout(fileLayout, entryOut);
  return true;
}

bool readRevisionLoopSlotDirectoryEntryFromRevisionBytes(const uint8_t* fileBytes, size_t fileSize,
                                         const RevisionHeader& header, uint16_t index,
                                         RevisionLoopSlotDirectoryEntry& entryOut) {
  const uint8_t* slotIndexBody = nullptr;
  size_t slotIndexBodySize = 0;
  if (!findSlotIndexChunkBody(fileBytes, fileSize, header, slotIndexBody, slotIndexBodySize)) {
    return false;
  }
  return readRevisionLoopSlotDirectoryEntryFromBytes(slotIndexBody, slotIndexBodySize, index, entryOut);
}

bool validateRevisionFooterFromBytes(const uint8_t* fileBytes, size_t fileSize,
                                     RevisionFooter& footerOut) {
  if (!readRevisionFooterAtOffset(fileBytes, fileSize, footerOut)) {
    return false;
  }
  RevisionHeader header{};
  if (!parseRevisionHeaderFromBytes(fileBytes, fileSize, header)) {
    return false;
  }
  const size_t payloadOffset = kRevisionHeaderByteSize;
  if (payloadOffset + header.payloadSize + kRevisionFooterByteSize > fileSize) {
    return false;
  }
  const uint32_t expectedPayloadCrc =
      header.payloadSize == 0
          ? 0U
          : computeRevisionPayloadChecksum(fileBytes + payloadOffset, header.payloadSize);
  return footerOut.payloadCrc32 == expectedPayloadCrc;
}

bool findChunkBodyInRevisionBytes(const uint8_t* fileBytes, size_t fileSize,
                                  const RevisionHeader& header, ChunkType type,
                                  uint8_t trackIndex, uint8_t slotIndex,
                                  uint32_t& bodyOffsetInFileOut, uint32_t& bodyLengthOut) {
  bodyOffsetInFileOut = 0;
  bodyLengthOut = 0;
  if (fileBytes == nullptr || header.payloadSize == 0) {
    return false;
  }
  const size_t payloadOffset = kRevisionHeaderByteSize;
  if (payloadOffset + header.payloadSize > fileSize) {
    return false;
  }
  const uint8_t* payload = fileBytes + payloadOffset;
  size_t cursor = 0;
  while (cursor + kChunkHeaderByteSize <= header.payloadSize) {
    RevisionChunkHeaderFileLayout chunkFileLayout{};
    std::memcpy(&chunkFileLayout, payload + cursor, sizeof(chunkFileLayout));
    const size_t bodyStart = cursor + kChunkHeaderByteSize;
    const size_t bodyEnd = bodyStart + static_cast<size_t>(chunkFileLayout.bodyLength);
    if (bodyEnd > header.payloadSize) {
      return false;
    }
    if (chunkFileLayout.type == static_cast<uint8_t>(type) &&
        chunkFileLayout.trackIndex == trackIndex && chunkFileLayout.slotIndex == slotIndex) {
      bodyOffsetInFileOut = static_cast<uint32_t>(payloadOffset + bodyStart);
      bodyLengthOut = chunkFileLayout.bodyLength;
      return true;
    }
    cursor = bodyEnd;
  }
  return false;
}

bool readRevisionLoopSlotDirectoryEntryCountFromRevisionBytes(const uint8_t* fileBytes, size_t fileSize,
                                              const RevisionHeader& header,
                                              uint16_t& entryCountOut) {
  entryCountOut = 0;
  const uint8_t* slotIndexBody = nullptr;
  size_t slotIndexBodySize = 0;
  if (!findSlotIndexChunkBody(fileBytes, fileSize, header, slotIndexBody, slotIndexBodySize) ||
      slotIndexBodySize < kSlotIndexBodyPrefixByteSize) {
    return false;
  }
  std::memcpy(&entryCountOut, slotIndexBody, sizeof(entryCountOut));
  const size_t expectedSize =
      kSlotIndexBodyPrefixByteSize + static_cast<size_t>(entryCountOut) * kRevisionLoopSlotDirectoryEntryByteSize;
  return slotIndexBodySize >= expectedSize;
}

bool readRevisionLoopSlotDirectoryEntriesFromRevisionBytes(const uint8_t* fileBytes, size_t fileSize,
                                           const RevisionHeader& header,
                                           RevisionLoopSlotDirectoryEntry* entriesOut, uint16_t maxEntries,
                                           uint16_t& entryCountOut) {
  entryCountOut = 0;
  if (entriesOut == nullptr || maxEntries == 0) {
    return false;
  }
  uint16_t totalEntries = 0;
  if (!readRevisionLoopSlotDirectoryEntryCountFromRevisionBytes(fileBytes, fileSize, header, totalEntries)) {
    return false;
  }
  for (uint16_t i = 0; i < totalEntries; ++i) {
    if (i >= maxEntries) {
      return false;
    }
    if (!readRevisionLoopSlotDirectoryEntryFromRevisionBytes(fileBytes, fileSize, header, i, entriesOut[i])) {
      return false;
    }
    ++entryCountOut;
  }
  return true;
}

}  // namespace RevisionPackedBlob
