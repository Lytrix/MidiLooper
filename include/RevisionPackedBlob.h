//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
//  REVPK02 — LMDB-inspired revision snapshot: fixed header, typed chunk stream, CRC footer.
//  LoopSlot chunk bodies use StorageLoopIo v5 slot file layout (recordPass, overdubPasses, editPasses).

#pragma once

#include <cstddef>
#include <cstdint>

#include "PersistenceSchema.h"
#include "StorageLoopIo.h"

namespace RevisionPackedBlob {

constexpr char kRevisionMagic[] = "REVPK02";
constexpr uint32_t kRevisionSvokFileToken = 0x53564F4BU;  // "SVOK"
constexpr uint16_t kRevisionHeaderSize = 128;
constexpr size_t kRevisionHeaderByteSize = 128;
constexpr size_t kChunkHeaderByteSize = 8;
constexpr size_t kRevisionLoopSlotDirectoryEntryByteSize = 32;
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

/// Directory entry inside a revision SlotIndex chunk (offset relative to payload start).
struct RevisionLoopSlotDirectoryEntry {
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
  uint32_t svokToken = kRevisionSvokFileToken;
  uint32_t payloadCrc32 = 0;
  uint32_t fileSize = 0;
};

uint32_t computeRevisionHeaderChecksum(const RevisionHeader& header);
uint32_t computeRevisionPayloadChecksum(const uint8_t* payload, size_t payloadSize);

bool writeRevisionHeader(const StorageIo& io, const RevisionHeader& header);
bool readRevisionHeader(const StorageIo& io, RevisionHeader& header);

bool writeChunkHeader(const StorageIo& io, const ChunkHeader& chunkHeader);
bool readChunkHeader(const StorageIo& io, ChunkHeader& chunkHeader);

bool writeRevisionLoopSlotDirectoryEntry(const StorageIo& io, const RevisionLoopSlotDirectoryEntry& entry);
bool readRevisionLoopSlotDirectoryEntry(const StorageIo& io, RevisionLoopSlotDirectoryEntry& entry);

/// SD file bytes for incremental payload CRC (matches on-disk layout).
bool revisionLoopSlotDirectoryEntryFileBytes(const RevisionLoopSlotDirectoryEntry& entry, uint8_t* out, size_t outSize);
bool revisionChunkHeaderFileBytes(const ChunkHeader& chunkHeader, uint8_t* out, size_t outSize);

bool writeRevisionFooter(const StorageIo& io, const RevisionFooter& footer);
bool readRevisionFooterAtOffset(const uint8_t* fileBytes, size_t fileSize,
                                RevisionFooter& footer);

/// Stack-only parser: reads header from a byte buffer (no heap).
bool parseRevisionHeaderFromBytes(const uint8_t* fileBytes, size_t fileSize,
                                  RevisionHeader& headerOut);

/// Reads one SlotIndex entry from a SlotIndex chunk body (after entryCount prefix).
bool readRevisionLoopSlotDirectoryEntryFromBytes(const uint8_t* slotIndexBody, size_t slotIndexBodySize,
                                 uint16_t index, RevisionLoopSlotDirectoryEntry& entryOut);

/// Locates the SlotIndex chunk in the payload and reads entry at index.
bool readRevisionLoopSlotDirectoryEntryFromRevisionBytes(const uint8_t* fileBytes, size_t fileSize,
                                         const RevisionHeader& header, uint16_t index,
                                         RevisionLoopSlotDirectoryEntry& entryOut);

bool validateRevisionFooterFromBytes(const uint8_t* fileBytes, size_t fileSize,
                                     RevisionFooter& footerOut);

/// Locates a chunk body in the revision payload (offset relative to file start).
bool findChunkBodyInRevisionBytes(const uint8_t* fileBytes, size_t fileSize,
                                  const RevisionHeader& header, ChunkType type,
                                  uint8_t trackIndex, uint8_t slotIndex,
                                  uint32_t& bodyOffsetInFileOut, uint32_t& bodyLengthOut);

/// Reads SlotIndex entry count from a validated revision byte buffer.
bool readRevisionLoopSlotDirectoryEntryCountFromRevisionBytes(const uint8_t* fileBytes, size_t fileSize,
                                              const RevisionHeader& header,
                                              uint16_t& entryCountOut);

/// Fills slot index entries from SlotIndex chunk (stack buffer, no heap).
bool readRevisionLoopSlotDirectoryEntriesFromRevisionBytes(const uint8_t* fileBytes, size_t fileSize,
                                           const RevisionHeader& header,
                                           RevisionLoopSlotDirectoryEntry* entriesOut, uint16_t maxEntries,
                                           uint16_t& entryCountOut);

constexpr size_t kSlotIndexBodyPrefixByteSize = 4;

inline size_t slotIndexChunkBodySize(uint16_t entryCount) {
  return kSlotIndexBodyPrefixByteSize +
         static_cast<size_t>(entryCount) * kRevisionLoopSlotDirectoryEntryByteSize;
}

}  // namespace RevisionPackedBlob
