//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#pragma once

#include <cstddef>
#include <cstdint>

#include "Globals.h"
#include "PersistenceLayout.h"
#include "PersistenceSchema.h"
#include "StorageLoopIo.h"

#if defined(ARDUINO)
#include <SD.h>
#endif

namespace CurrentWorkspaceStorage {

inline constexpr const char* kCurrentRoot = PersistenceLayout::kCurrentRoot;
constexpr char kWorkspaceMetaPath[] = "/MidiLooper/current/workspace.bin";
constexpr char kWorkspaceMetaTempPath[] = "/MidiLooper/current/workspace.bin.tmp";
constexpr char kTransportPath[] = "/MidiLooper/current/transport.bin";
constexpr char kGlobalPath[] = "/MidiLooper/current/global.bin";
constexpr char kSlotsDir[] = "/MidiLooper/current/slots";
constexpr char kUndoDir[] = "/MidiLooper/current/undo";
inline constexpr const char* kCurrentTempDir = PersistenceLayout::kCurrentTempDir;

constexpr size_t kSlotSummaryCount = Config::MAX_LOOPS_PER_TRACK;
constexpr size_t kWorkspaceMetaByteSize = 77;
constexpr size_t kEpochFileHeaderByteSize = 10;

constexpr size_t kSlotSummaryByteSize = 6;

#pragma pack(push, 1)
struct SlotSummary {
  uint8_t occupied = 0;
  uint16_t noteCount = 0;
  uint16_t bars = 0;
  uint8_t muted = 0;
};
#pragma pack(pop)

static_assert(sizeof(SlotSummary) == kSlotSummaryByteSize, "SlotSummary SD file size mismatch");

struct WorkspaceMetaRecord {
  uint16_t schemaVersion = PersistenceSchema::kSetRevisionSchemaVersion;
  uint32_t currentEpoch = 0;
  uint32_t lastCommittedEpoch = 0;
  uint16_t derivedFromSetId = 0;
  uint16_t derivedFromRevisionId = 0;
  uint16_t lastCommittedRevisionId = 0;
  uint8_t slotCount = Config::MAX_LOOPS_PER_TRACK;
  SlotSummary slotSummary[kSlotSummaryCount]{};
  uint64_t updatedUnix = 0;
  uint32_t crc32 = 0;
};

struct EpochFileHeader {
  uint32_t epoch = 0;
  uint16_t schemaVersion = PersistenceSchema::kSetRevisionSchemaVersion;
  uint32_t crc32 = 0;
};

bool formatSlotPath(char* out, size_t outSize, uint8_t slotIndex, bool undoSlot);
bool formatSlotTempPath(char* out, size_t outSize, uint8_t slotIndex, bool undoSlot);

uint32_t computeWorkspaceMetaChecksum(const WorkspaceMetaRecord& record);
uint32_t computeEpochFileHeaderChecksum(const EpochFileHeader& header);

bool writeWorkspaceMeta(const StorageIo& io, const WorkspaceMetaRecord& record);
bool readWorkspaceMeta(const StorageIo& io, WorkspaceMetaRecord& record);

bool writeEpochFileHeader(const StorageIo& io, const EpochFileHeader& header);
bool readEpochFileHeader(const StorageIo& io, EpochFileHeader& header);

bool isWorkspaceDirty(uint32_t currentEpoch, uint32_t lastCommittedEpoch);

/// Last fully written Current epoch to freeze at revision commit SNAPSHOT.
uint32_t resolveCompletedWorkspaceEpochForRevisionSnapshot(uint32_t currentWorkspaceEpoch,
                                                           uint32_t deferredSaveWorkspaceEpoch,
                                                           bool deferredSaveInProgress);

/// Live epoch after SNAPSHOT so post-snapshot capture belongs to the next boundary.
uint32_t workspaceEpochAfterRevisionSnapshot(uint32_t snapshotSourceEpoch);

/// Revision commit COMPLETE sync — matches live epoch so dirty clears (see current-workspace spec).
uint32_t syncLastCommittedEpochAfterRevisionCommitComplete(uint32_t currentWorkspaceEpoch);

bool isEpochHeaderChecksumValid(const EpochFileHeader& header);
bool epochHeaderMatchesEpoch(const EpochFileHeader& header, uint32_t expectedEpoch);
uint32_t computeEpochFileBodyChecksum(const uint8_t* body, size_t bodySize);
bool validateEpochFileBytes(const uint8_t* fileBytes, size_t fileSize, uint32_t expectedEpoch,
                            bool expectEpochHeader);

#if defined(ARDUINO)
bool writeEpochHeaderPlaceholder(File& file, uint32_t epoch);
bool finalizeEpochFileHeaderCrc(const char* path);
bool fileStartsWithEpochHeader(File& file);
bool writeWorkspaceMetaFile(const WorkspaceMetaRecord& record);
bool readWorkspaceMetaFile(WorkspaceMetaRecord& record);

/// Epoch header, body CRC, and completion marker for a persisted Current file.
bool validateEpochFileOnSd(const char* path, uint32_t expectedEpoch);

/// Boot probe only — epoch header finalized + SAVE token; no full-file body CRC walk.
bool validateEpochFileOnSdQuick(const char* path, uint32_t expectedEpoch);
#endif

}  // namespace CurrentWorkspaceStorage
