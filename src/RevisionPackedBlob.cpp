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
  uint32_t transportOffset;
  uint32_t globalOffset;
  uint32_t undoOffset;
  uint32_t loopIndexOffset;
  uint16_t loopIndexCount;
  uint16_t loopIndexPadding;
  uint32_t payloadSize;
  uint32_t headerCrc32;
  uint32_t workspaceFlags;
  uint8_t reserved[56];
  uint8_t trailingPad[12];
};

struct LoopIndexEntryWire {
  uint8_t trackIndex;
  uint8_t slotIndex;
  uint8_t occupied;
  uint8_t reserved0;
  uint32_t blobOffset;
  uint32_t blobSize;
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
static_assert(sizeof(LoopIndexEntryWire) == kLoopIndexEntryByteSize,
              "LoopIndexEntryWire size mismatch");
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
  wire.transportOffset = header.transportOffset;
  wire.globalOffset = header.globalOffset;
  wire.undoOffset = header.undoOffset;
  wire.loopIndexOffset = header.loopIndexOffset;
  wire.loopIndexCount = header.loopIndexCount;
  wire.loopIndexPadding = 0;
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
  header.transportOffset = wire.transportOffset;
  header.globalOffset = wire.globalOffset;
  header.undoOffset = wire.undoOffset;
  header.loopIndexOffset = wire.loopIndexOffset;
  header.loopIndexCount = wire.loopIndexCount;
  header.payloadSize = wire.payloadSize;
  header.headerCrc32 = wire.headerCrc32;
  header.workspaceFlags = wire.workspaceFlags;
}

LoopIndexEntryWire toWire(const LoopIndexEntry& entry) {
  LoopIndexEntryWire wire{};
  wire.trackIndex = entry.trackIndex;
  wire.slotIndex = entry.slotIndex;
  wire.occupied = entry.occupied;
  wire.reserved0 = entry.reserved0;
  wire.blobOffset = entry.blobOffset;
  wire.blobSize = entry.blobSize;
  wire.loopLengthTicks = entry.loopLengthTicks;
  wire.noteCount = entry.noteCount;
  wire.bars = entry.bars;
  std::memcpy(wire.reserved1, entry.reserved1, sizeof(wire.reserved1));
  return wire;
}

void fromWire(const LoopIndexEntryWire& wire, LoopIndexEntry& entry) {
  entry.trackIndex = wire.trackIndex;
  entry.slotIndex = wire.slotIndex;
  entry.occupied = wire.occupied;
  entry.reserved0 = wire.reserved0;
  entry.blobOffset = wire.blobOffset;
  entry.blobSize = wire.blobSize;
  entry.loopLengthTicks = wire.loopLengthTicks;
  entry.noteCount = wire.noteCount;
  entry.bars = wire.bars;
  std::memcpy(entry.reserved1, wire.reserved1, sizeof(entry.reserved1));
}

bool magicMatches(const char* magic) {
  return std::memcmp(magic, kRevisionMagic, sizeof(kRevisionMagic)) == 0;
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

bool writeLoopIndexEntry(const StorageIo& io, const LoopIndexEntry& entry) {
  const LoopIndexEntryWire wire = toWire(entry);
  return ioWrite(io, &wire, sizeof(wire));
}

bool readLoopIndexEntry(const StorageIo& io, LoopIndexEntry& entry) {
  LoopIndexEntryWire wire{};
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

bool readLoopIndexEntryFromBytes(const uint8_t* fileBytes, size_t fileSize,
                                 const RevisionHeader& header, uint16_t index,
                                 LoopIndexEntry& entryOut) {
  if (fileBytes == nullptr || index >= header.loopIndexCount) {
    return false;
  }
  const size_t entryOffset =
      static_cast<size_t>(header.loopIndexOffset) +
      static_cast<size_t>(index) * kLoopIndexEntryByteSize;
  if (entryOffset + kLoopIndexEntryByteSize > fileSize) {
    return false;
  }
  LoopIndexEntryWire wire{};
  std::memcpy(&wire, fileBytes + entryOffset, sizeof(wire));
  fromWire(wire, entryOut);
  return true;
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
  const size_t payloadOffset =
      static_cast<size_t>(header.loopIndexOffset) +
      static_cast<size_t>(header.loopIndexCount) * kLoopIndexEntryByteSize;
  if (payloadOffset + header.payloadSize + kRevisionFooterByteSize > fileSize) {
    return false;
  }
  const uint32_t expectedPayloadCrc =
      header.payloadSize == 0
          ? 0U
          : computeRevisionPayloadChecksum(fileBytes + payloadOffset, header.payloadSize);
  return footerOut.payloadCrc32 == expectedPayloadCrc;
}

}  // namespace RevisionPackedBlob
