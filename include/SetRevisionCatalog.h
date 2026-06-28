//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#pragma once

#include <cstddef>
#include <cstdint>

#include "PersistenceLayout.h"
#include "PersistenceSchema.h"
#include "StorageLoopIo.h"

namespace SetRevisionCatalog {

inline constexpr const char* kSetsRoot = PersistenceLayout::kSetsRoot;
constexpr char kSetIndexPath[] = "/MidiLooper/sets/index.bin";
constexpr char kSetIndexTempPath[] = "/MidiLooper/sets/index.bin.tmp";
inline constexpr const char* kRevisionFileExtension = PersistenceLayout::kRevisionFileExtension;
inline constexpr const char* kRevisionTempSuffix = PersistenceLayout::kRevisionTempSuffix;

constexpr size_t kSetSubtitleCapacity = 48;
constexpr size_t kSetCatalogIndexByteSize = 16;
constexpr size_t kSetMetaRecordByteSize = 92;

struct SetCatalogIndex {
  uint16_t schemaVersion = PersistenceSchema::kSetRevisionSchemaVersion;
  uint16_t reserved = 0;
  uint32_t nextSetId = 1;
  uint32_t setCount = 0;
  uint32_t catalogChecksum = 0;
};

struct SetMetaRecord {
  uint16_t schemaVersion = PersistenceSchema::kSetRevisionSchemaVersion;
  uint16_t setId = 0;
  uint16_t latestRevisionId = 0;
  uint16_t revisionCount = 0;
  uint64_t createdUnix = 0;
  uint64_t updatedUnix = 0;
  char subtitle[kSetSubtitleCapacity] = {};
  uint8_t favorite = 0;
  uint8_t reserved[15] = {};
  uint32_t crc32 = 0;
};

/// One Set row in the workspace browser (catalog `sets/S####/` only).
struct SetBrowserListEntry {
  char folderName[16] = {};
  uint16_t setId = 0;
  uint16_t latestRevisionId = 0;
  uint64_t updatedUnix = 0;
  uint8_t favorite = 0;
};

/// One revision row in the revision-history drill-down (newest first).
struct RevisionBrowserListEntry {
  uint16_t revisionId = 0;
  uint64_t createdUnix = 0;
};

uint32_t computeSetCatalogIndexChecksum(const SetCatalogIndex& index);
uint32_t computeSetMetaChecksum(const SetMetaRecord& record);

bool writeSetCatalogIndex(const StorageIo& io, const SetCatalogIndex& index);
bool readSetCatalogIndex(const StorageIo& io, SetCatalogIndex& index);

bool writeSetMetaRecord(const StorageIo& io, const SetMetaRecord& record);
bool readSetMetaRecord(const StorageIo& io, SetMetaRecord& record);

/// Monotonic Set id from index.bin; ids are never recycled.
uint16_t allocateNextSetId(SetCatalogIndex& index);

/// Next revision id for a pending commit (does not update catalog — COMPLETE only).
uint16_t peekNextRevisionId(const SetMetaRecord& meta);

/// Apply validated revision to set.bin at COMPLETE only.
void applyValidatedRevisionToSetMeta(SetMetaRecord& meta, uint16_t revisionId,
                                     uint64_t updatedUnix);

/// Parse `S####` catalog folder basename; false for legacy SavedSet folder names.
bool parseSetIdFromFolderName(const char* folderName, uint16_t& setIdOut);

bool formatSetFolderPath(char* out, size_t outSize, uint16_t setId);
bool formatSetMetaPath(char* out, size_t outSize, uint16_t setId);
bool formatRevisionPath(char* out, size_t outSize, uint16_t setId, uint16_t revisionId,
                        bool tempFile);

/// Sort workspace browser rows newest `updatedUnix` first (stable enough for small N).
void sortSetBrowserListEntriesByUpdatedUnixDesc(SetBrowserListEntry* entries, size_t count);

/// Sort revision history rows newest `createdUnix` first (stable enough for small N).
void sortRevisionBrowserListEntriesByCreatedUnixDesc(RevisionBrowserListEntry* entries,
                                                     size_t count);

}  // namespace SetRevisionCatalog
