//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
//  REVPK02 — LMDB-inspired revision snapshot: fixed header, typed chunk stream, CRC footer.
//  LoopSlot chunk bodies use StorageLoopIo v5 wire (recordPass, overdubPasses, editPasses).

#pragma once

#include <cstddef>
#include <cstdint>

#include "PersistenceSchema.h"
#include "StorageLoopIo.h"

namespace RevisionPackedBlob {

constexpr char kRevisionMagic[] = "REVPK02";
constexpr uint32_t kRevisionCompleteMagic = 0x53564F4BU;  // "SVOK"
constexpr uint16_t kRevisionHeaderSize = 128;
constexpr size_t kRevisionHeaderByteSize = 128;
constexpr size_t kChunkHeaderByteSize = 8;
constexpr size_t kSlotIndexEntryByteSize = 32;
constexpr size_t kRevisionFooterByteSize = 12;

enum class ChunkType : uint8_t {
  Transport = 1,
  Global = 2,
  Undo = 3,
  SlotIndex = 4,
  LoopSlot = 5,
};

struct RevisionHeader {
  char magic[8] = {};
  uint16_t schemaVersion = PersistenceSchema::kSetRevisionSchemaVersion;
  uint16_t headerSize = kRevisionHeaderSize;
  uint16_t revisionId = 0;
  uint16_t setId = 0;
  uint32_t sourceEpoch = 0;
  uint64_t createdUnix = 0;
  uint16_t chunkCount = 0;
  uint16_t reserved0 = 0;
  uint32_t payloadSize = 0;
  uint32_t headerCrc32 = 0;
  uint32_t workspaceFlags = 0;
};

struct ChunkHeader {
  uint8_t type = 0;
  uint8_t trackIndex = 0;
  uint8_t slotIndex = 0;
  uint8_t reserved = 0;
  uint32_t bodyLength = 0;
};

/// Directory entry inside a SlotIndex chunk (offset relative to payload start).
struct SlotIndexEntry {
  uint8_t trackIndex = 0;
  uint8_t slotIndex = 0;
  uint8_t occupied = 0;
  uint8_t reserved0 = 0;
  uint32_t chunkOffset = 0;
  uint32_t bodyLength = 0;
  uint32_t loopLengthTicks = 0;
  uint16_t noteCount = 0;
  uint16_t bars = 0;
  uint8_t reserved1[12] = {};
};

struct RevisionFooter {
  uint32_t completeMagic = kRevisionCompleteMagic;
  uint32_t payloadCrc32 = 0;
  uint32_t fileSize = 0;
};

uint32_t computeRevisionHeaderChecksum(const RevisionHeader& header);
uint32_t computeRevisionPayloadChecksum(const uint8_t* payload, size_t payloadSize);

bool writeRevisionHeader(const StorageIo& io, const RevisionHeader& header);
bool readRevisionHeader(const StorageIo& io, RevisionHeader& header);

bool writeChunkHeader(const StorageIo& io, const ChunkHeader& chunkHeader);
bool readChunkHeader(const StorageIo& io, ChunkHeader& chunkHeader);

bool writeSlotIndexEntry(const StorageIo& io, const SlotIndexEntry& entry);
bool readSlotIndexEntry(const StorageIo& io, SlotIndexEntry& entry);

/// Writes packed wire bytes for incremental payload CRC (matches on-disk layout).
bool slotIndexEntryWireBytes(const SlotIndexEntry& entry, uint8_t* out, size_t outSize);
bool chunkHeaderWireBytes(const ChunkHeader& chunkHeader, uint8_t* out, size_t outSize);

bool writeRevisionFooter(const StorageIo& io, const RevisionFooter& footer);
bool readRevisionFooterAtOffset(const uint8_t* fileBytes, size_t fileSize,
                                RevisionFooter& footer);

/// Stack-only parser: reads header from a byte buffer (no heap).
bool parseRevisionHeaderFromBytes(const uint8_t* fileBytes, size_t fileSize,
                                  RevisionHeader& headerOut);

/// Reads one SlotIndex entry from a SlotIndex chunk body (after entryCount prefix).
bool readSlotIndexEntryFromBytes(const uint8_t* slotIndexBody, size_t slotIndexBodySize,
                                 uint16_t index, SlotIndexEntry& entryOut);

/// Locates the SlotIndex chunk in the payload and reads entry at index.
bool readSlotIndexEntryFromRevisionBytes(const uint8_t* fileBytes, size_t fileSize,
                                         const RevisionHeader& header, uint16_t index,
                                         SlotIndexEntry& entryOut);

bool validateRevisionFooterFromBytes(const uint8_t* fileBytes, size_t fileSize,
                                     RevisionFooter& footerOut);

constexpr size_t kSlotIndexBodyPrefixByteSize = 4;

inline size_t slotIndexChunkBodySize(uint16_t entryCount) {
  return kSlotIndexBodyPrefixByteSize +
         static_cast<size_t>(entryCount) * kSlotIndexEntryByteSize;
}

}  // namespace RevisionPackedBlob
