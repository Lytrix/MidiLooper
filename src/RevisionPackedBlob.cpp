//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "RevisionPackedBlob.h"

#include <cstring>

namespace RevisionPackedBlob {
namespace {

#pragma pack(push, 1)

struct RevisionHeaderWire {
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

struct ChunkHeaderWire {
  uint8_t type;
  uint8_t trackIndex;
  uint8_t slotIndex;
  uint8_t reserved;
  uint32_t bodyLength;
};

struct SlotIndexEntryWire {
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

struct RevisionFooterWire {
  uint32_t completeMagic;
  uint32_t payloadCrc32;
  uint32_t fileSize;
};

#pragma pack(pop)

static_assert(sizeof(RevisionHeaderWire) == kRevisionHeaderByteSize,
              "RevisionHeaderWire size mismatch");
static_assert(sizeof(ChunkHeaderWire) == kChunkHeaderByteSize, "ChunkHeaderWire size mismatch");
static_assert(sizeof(SlotIndexEntryWire) == kSlotIndexEntryByteSize,
              "SlotIndexEntryWire size mismatch");
static_assert(sizeof(RevisionFooterWire) == kRevisionFooterByteSize,
              "RevisionFooterWire size mismatch");

bool ioWrite(const StorageIo& io, const void* data, size_t size) {
  return io.write && io.write(data, size);
}

bool ioRead(const StorageIo& io, void* data, size_t size) {
  return io.read && io.read(data, size);
}

RevisionHeaderWire toWire(const RevisionHeader& header) {
  RevisionHeaderWire wire{};
  std::memcpy(wire.magic, header.magic, sizeof(wire.magic));
  wire.schemaVersion = header.schemaVersion;
  wire.headerSize = header.headerSize;
  wire.revisionId = header.revisionId;
  wire.setId = header.setId;
  wire.sourceEpoch = header.sourceEpoch;
  wire.createdUnix = header.createdUnix;
  wire.chunkCount = header.chunkCount;
  wire.reserved0 = header.reserved0;
  wire.payloadSize = header.payloadSize;
  wire.headerCrc32 = header.headerCrc32;
  wire.workspaceFlags = header.workspaceFlags;
  return wire;
}

void fromWire(const RevisionHeaderWire& wire, RevisionHeader& header) {
  std::memcpy(header.magic, wire.magic, sizeof(header.magic));
  header.schemaVersion = wire.schemaVersion;
  header.headerSize = wire.headerSize;
  header.revisionId = wire.revisionId;
  header.setId = wire.setId;
  header.sourceEpoch = wire.sourceEpoch;
  header.createdUnix = wire.createdUnix;
  header.chunkCount = wire.chunkCount;
  header.reserved0 = wire.reserved0;
  header.payloadSize = wire.payloadSize;
  header.headerCrc32 = wire.headerCrc32;
  header.workspaceFlags = wire.workspaceFlags;
}

ChunkHeaderWire toWire(const ChunkHeader& chunkHeader) {
  ChunkHeaderWire wire{};
  wire.type = chunkHeader.type;
  wire.trackIndex = chunkHeader.trackIndex;
  wire.slotIndex = chunkHeader.slotIndex;
  wire.reserved = chunkHeader.reserved;
  wire.bodyLength = chunkHeader.bodyLength;
  return wire;
}

void fromWire(const ChunkHeaderWire& wire, ChunkHeader& chunkHeader) {
  chunkHeader.type = wire.type;
  chunkHeader.trackIndex = wire.trackIndex;
  chunkHeader.slotIndex = wire.slotIndex;
  chunkHeader.reserved = wire.reserved;
  chunkHeader.bodyLength = wire.bodyLength;
}

SlotIndexEntryWire toWire(const SlotIndexEntry& entry) {
  SlotIndexEntryWire wire{};
  wire.trackIndex = entry.trackIndex;
  wire.slotIndex = entry.slotIndex;
  wire.occupied = entry.occupied;
  wire.reserved0 = entry.reserved0;
  wire.chunkOffset = entry.chunkOffset;
  wire.bodyLength = entry.bodyLength;
  wire.loopLengthTicks = entry.loopLengthTicks;
  wire.noteCount = entry.noteCount;
  wire.bars = entry.bars;
  std::memcpy(wire.reserved1, entry.reserved1, sizeof(wire.reserved1));
  return wire;
}

void fromWire(const SlotIndexEntryWire& wire, SlotIndexEntry& entry) {
  entry.trackIndex = wire.trackIndex;
  entry.slotIndex = wire.slotIndex;
  entry.occupied = wire.occupied;
  entry.reserved0 = wire.reserved0;
  entry.chunkOffset = wire.chunkOffset;
  entry.bodyLength = wire.bodyLength;
  entry.loopLengthTicks = wire.loopLengthTicks;
  entry.noteCount = wire.noteCount;
  entry.bars = wire.bars;
  std::memcpy(entry.reserved1, wire.reserved1, sizeof(entry.reserved1));
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
    ChunkHeaderWire chunkWire{};
    std::memcpy(&chunkWire, payload + cursor, sizeof(chunkWire));
    const size_t bodyStart = cursor + kChunkHeaderByteSize;
    const size_t bodyEnd = bodyStart + static_cast<size_t>(chunkWire.bodyLength);
    if (bodyEnd > header.payloadSize) {
      return false;
    }
    if (chunkWire.type == static_cast<uint8_t>(ChunkType::SlotIndex)) {
      bodyOut = payload + bodyStart;
      bodySizeOut = chunkWire.bodyLength;
      return true;
    }
    cursor = bodyEnd;
  }
  return false;
}

}  // namespace

uint32_t computeRevisionHeaderChecksum(const RevisionHeader& header) {
  RevisionHeaderWire wire = toWire(header);
  wire.headerCrc32 = 0;
  return PersistenceSchema::crc32(reinterpret_cast<const uint8_t*>(&wire), sizeof(wire));
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
  const RevisionHeaderWire wire = toWire(stamped);
  return ioWrite(io, &wire, sizeof(wire));
}

bool readRevisionHeader(const StorageIo& io, RevisionHeader& header) {
  RevisionHeaderWire wire{};
  if (!ioRead(io, &wire, sizeof(wire))) {
    return false;
  }
  fromWire(wire, header);
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
  const ChunkHeaderWire wire = toWire(chunkHeader);
  return ioWrite(io, &wire, sizeof(wire));
}

bool readChunkHeader(const StorageIo& io, ChunkHeader& chunkHeader) {
  ChunkHeaderWire wire{};
  if (!ioRead(io, &wire, sizeof(wire))) {
    return false;
  }
  fromWire(wire, chunkHeader);
  return true;
}

bool writeSlotIndexEntry(const StorageIo& io, const SlotIndexEntry& entry) {
  const SlotIndexEntryWire wire = toWire(entry);
  return ioWrite(io, &wire, sizeof(wire));
}

bool slotIndexEntryWireBytes(const SlotIndexEntry& entry, uint8_t* out, size_t outSize) {
  if (out == nullptr || outSize < kSlotIndexEntryByteSize) {
    return false;
  }
  const SlotIndexEntryWire wire = toWire(entry);
  std::memcpy(out, &wire, sizeof(wire));
  return true;
}

bool chunkHeaderWireBytes(const ChunkHeader& chunkHeader, uint8_t* out, size_t outSize) {
  if (out == nullptr || outSize < kChunkHeaderByteSize) {
    return false;
  }
  const ChunkHeaderWire wire = toWire(chunkHeader);
  std::memcpy(out, &wire, sizeof(wire));
  return true;
}

bool readSlotIndexEntry(const StorageIo& io, SlotIndexEntry& entry) {
  SlotIndexEntryWire wire{};
  if (!ioRead(io, &wire, sizeof(wire))) {
    return false;
  }
  fromWire(wire, entry);
  return true;
}

bool writeRevisionFooter(const StorageIo& io, const RevisionFooter& footer) {
  const RevisionFooterWire wire{footer.completeMagic, footer.payloadCrc32, footer.fileSize};
  return ioWrite(io, &wire, sizeof(wire));
}

bool readRevisionFooterAtOffset(const uint8_t* fileBytes, size_t fileSize,
                                RevisionFooter& footer) {
  if (fileBytes == nullptr || fileSize < kRevisionFooterByteSize) {
    return false;
  }
  const size_t footerOffset = fileSize - kRevisionFooterByteSize;
  RevisionFooterWire wire{};
  std::memcpy(&wire, fileBytes + footerOffset, sizeof(wire));
  footer.completeMagic = wire.completeMagic;
  footer.payloadCrc32 = wire.payloadCrc32;
  footer.fileSize = wire.fileSize;
  return footer.completeMagic == kRevisionCompleteMagic && footer.fileSize == fileSize;
}

bool parseRevisionHeaderFromBytes(const uint8_t* fileBytes, size_t fileSize,
                                  RevisionHeader& headerOut) {
  if (fileBytes == nullptr || fileSize < kRevisionHeaderByteSize) {
    return false;
  }
  RevisionHeaderWire wire{};
  std::memcpy(&wire, fileBytes, sizeof(wire));
  fromWire(wire, headerOut);
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

bool readSlotIndexEntryFromBytes(const uint8_t* slotIndexBody, size_t slotIndexBodySize,
                                 uint16_t index, SlotIndexEntry& entryOut) {
  if (slotIndexBody == nullptr || slotIndexBodySize < kSlotIndexBodyPrefixByteSize) {
    return false;
  }
  uint16_t entryCount = 0;
  std::memcpy(&entryCount, slotIndexBody, sizeof(entryCount));
  if (index >= entryCount) {
    return false;
  }
  const size_t entryOffset =
      kSlotIndexBodyPrefixByteSize + static_cast<size_t>(index) * kSlotIndexEntryByteSize;
  if (entryOffset + kSlotIndexEntryByteSize > slotIndexBodySize) {
    return false;
  }
  SlotIndexEntryWire wire{};
  std::memcpy(&wire, slotIndexBody + entryOffset, sizeof(wire));
  fromWire(wire, entryOut);
  return true;
}

bool readSlotIndexEntryFromRevisionBytes(const uint8_t* fileBytes, size_t fileSize,
                                         const RevisionHeader& header, uint16_t index,
                                         SlotIndexEntry& entryOut) {
  const uint8_t* slotIndexBody = nullptr;
  size_t slotIndexBodySize = 0;
  if (!findSlotIndexChunkBody(fileBytes, fileSize, header, slotIndexBody, slotIndexBodySize)) {
    return false;
  }
  return readSlotIndexEntryFromBytes(slotIndexBody, slotIndexBodySize, index, entryOut);
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
    ChunkHeaderWire chunkWire{};
    std::memcpy(&chunkWire, payload + cursor, sizeof(chunkWire));
    const size_t bodyStart = cursor + kChunkHeaderByteSize;
    const size_t bodyEnd = bodyStart + static_cast<size_t>(chunkWire.bodyLength);
    if (bodyEnd > header.payloadSize) {
      return false;
    }
    if (chunkWire.type == static_cast<uint8_t>(type) &&
        chunkWire.trackIndex == trackIndex && chunkWire.slotIndex == slotIndex) {
      bodyOffsetInFileOut = static_cast<uint32_t>(payloadOffset + bodyStart);
      bodyLengthOut = chunkWire.bodyLength;
      return true;
    }
    cursor = bodyEnd;
  }
  return false;
}

bool readSlotIndexEntryCountFromRevisionBytes(const uint8_t* fileBytes, size_t fileSize,
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
      kSlotIndexBodyPrefixByteSize + static_cast<size_t>(entryCountOut) * kSlotIndexEntryByteSize;
  return slotIndexBodySize >= expectedSize;
}

bool readSlotIndexEntriesFromRevisionBytes(const uint8_t* fileBytes, size_t fileSize,
                                           const RevisionHeader& header,
                                           SlotIndexEntry* entriesOut, uint16_t maxEntries,
                                           uint16_t& entryCountOut) {
  entryCountOut = 0;
  if (entriesOut == nullptr || maxEntries == 0) {
    return false;
  }
  uint16_t totalEntries = 0;
  if (!readSlotIndexEntryCountFromRevisionBytes(fileBytes, fileSize, header, totalEntries)) {
    return false;
  }
  for (uint16_t i = 0; i < totalEntries; ++i) {
    if (i >= maxEntries) {
      return false;
    }
    if (!readSlotIndexEntryFromRevisionBytes(fileBytes, fileSize, header, i, entriesOut[i])) {
      return false;
    }
    ++entryCountOut;
  }
  return true;
}

}  // namespace RevisionPackedBlob
