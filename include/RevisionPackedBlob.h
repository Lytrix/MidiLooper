//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#pragma once

#include <cstddef>
#include <cstdint>

#include "PersistenceSchema.h"
#include "StorageLoopIo.h"

namespace RevisionPackedBlob {

constexpr char kRevisionMagic[] = "REVPK01";
constexpr uint32_t kRevisionCompleteMagic = 0x53564F4BU;  // "SVOK"
constexpr uint16_t kRevisionHeaderSize = 128;
constexpr size_t kRevisionHeaderByteSize = 128;
constexpr size_t kLoopIndexEntryByteSize = 32;
constexpr size_t kRevisionFooterByteSize = 12;

struct RevisionHeader {
  char magic[8] = {};
  uint16_t schemaVersion = PersistenceSchema::kSetRevisionSchemaVersion;
  uint16_t headerSize = kRevisionHeaderSize;
  uint16_t revisionId = 0;
  uint16_t setId = 0;
  uint32_t sourceEpoch = 0;
  uint64_t createdUnix = 0;
  uint32_t transportOffset = 0;
  uint32_t globalOffset = 0;
  uint32_t undoOffset = 0;
  uint32_t loopIndexOffset = 0;
  uint16_t loopIndexCount = 0;
  uint32_t payloadSize = 0;
  uint32_t headerCrc32 = 0;
  uint32_t workspaceFlags = 0;
};

struct LoopIndexEntry {
  uint8_t trackIndex = 0;
  uint8_t slotIndex = 0;
  uint8_t occupied = 0;
  uint8_t reserved0 = 0;
  uint32_t blobOffset = 0;
  uint32_t blobSize = 0;
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

bool writeLoopIndexEntry(const StorageIo& io, const LoopIndexEntry& entry);
bool readLoopIndexEntry(const StorageIo& io, LoopIndexEntry& entry);

bool writeRevisionFooter(const StorageIo& io, const RevisionFooter& footer);
bool readRevisionFooterAtOffset(const uint8_t* fileBytes, size_t fileSize,
                                RevisionFooter& footer);

/// Stack-only parser: reads header and loop index from a byte buffer (no heap).
bool parseRevisionHeaderFromBytes(const uint8_t* fileBytes, size_t fileSize,
                                  RevisionHeader& headerOut);

bool readLoopIndexEntryFromBytes(const uint8_t* fileBytes, size_t fileSize,
                                 const RevisionHeader& header, uint16_t index,
                                 LoopIndexEntry& entryOut);

bool validateRevisionFooterFromBytes(const uint8_t* fileBytes, size_t fileSize,
                                     RevisionFooter& footerOut);

}  // namespace RevisionPackedBlob
