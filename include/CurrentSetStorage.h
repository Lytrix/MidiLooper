//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#pragma once

#include <cstddef>
#include <cstdint>

#include "PersistenceLayout.h"
#include "CurrentWorkspaceStorage.h"
#include "StorageLoopIo.h"
#include "Globals.h"

namespace CurrentSetStorage {

constexpr uint32_t CONTAINER_VERSION = 6;
constexpr uint32_t kSaveFileToken = 0x45564153UL;  // "SAVE"

/// Legacy flat SavedSet folders (brownfield) under sets/archive until 3.6.
inline constexpr const char* kSetsRoot = PersistenceLayout::kSetsRoot;
inline constexpr const char* kSetsArchiveDir = PersistenceLayout::kSetsArchiveDir;
constexpr char kSetIndexPath[] = "/MidiLooper/sets/index.bin";
constexpr char kSetIndexTempPath[] = "/MidiLooper/sets/index.bin.tmp";

/// Mutable workspace root — NOT inside sets/.
inline constexpr const char* kCurrentSetDir = PersistenceLayout::kCurrentRoot;
constexpr char kCurrentSlotsDir[] = "/MidiLooper/current/slots";

/// Interim deferred-FSM runtime bundle (transport+tracks+undo) until transport.bin/global.bin split.
constexpr char kCurrentRuntimeBundlePath[] = "/MidiLooper/current/temp/runtime.bundle.bin";
constexpr char kCurrentRuntimeBundleTempPath[] =
    "/MidiLooper/current/temp/runtime.bundle.bin.tmp";

/// Legacy alias — SD path is `runtime.bundle.bin` under `current/temp/` (not `set.bin`).
inline constexpr const char* kCurrentMetaPath = kCurrentRuntimeBundlePath;
inline constexpr const char* kCurrentMetaTempPath = kCurrentRuntimeBundleTempPath;

inline constexpr const char* kCheckpointsDir = PersistenceLayout::kCheckpointsDir;
constexpr char kLegacyMonolithPath[] = "/midilooper_state.raw";

/// Per-Set metadata filename (flat SavedSet folders and `sets/S####/`).
constexpr char kSetBinFileName[] = "set.bin";
constexpr char kSetBinTempFileName[] = "set.bin.tmp";

constexpr size_t kMetaPayloadOffset = CurrentWorkspaceStorage::kEpochFileHeaderByteSize;
constexpr size_t kLastActiveUnixOffset = kMetaPayloadOffset + sizeof(uint32_t);
constexpr size_t kAnchorFieldsOffset = kMetaPayloadOffset + sizeof(uint32_t) + sizeof(uint32_t);

struct AnchorFields {
  uint32_t loadedFromSequence = 0;
  uint32_t lastAnchoredSequence = 0;
  uint32_t lastMaterialChangeUnix = 0;
  uint8_t hasMaterialChangesSinceAnchor = 0;
  uint8_t reserved[3] = {0, 0, 0};
};

struct MetaHeader {
  uint32_t containerVersion = CONTAINER_VERSION;
  uint32_t lastActiveUnix = 0;
  AnchorFields anchor{};
};

bool formatLoopSlotPath(char* out, size_t outSize, uint8_t trackIndex, uint8_t slotIndex);
bool formatLoopSlotTempPath(char* out, size_t outSize, uint8_t trackIndex, uint8_t slotIndex);

bool writeMetaHeader(const StorageIo& io, const MetaHeader& header);
bool readMetaHeader(const StorageIo& io, MetaHeader& header);

bool writeSaveFileToken(const StorageIo& io);
bool verifySaveFileTokenAtEnd(const uint8_t* fileBytes, size_t fileSize);

bool shouldWriteLoopPayloadForSlot(bool forceCurrentSetFullLoopWrite, bool slotDirty);

struct LoopSlotPayloadWriteCounts {
  uint16_t writes = 0;
  uint16_t skips = 0;
};

LoopSlotPayloadWriteCounts countLoopSlotPayloadWrites(
    bool forceFullRewrite,
    const bool slotDirty[Config::NUM_TRACKS][Config::MAX_LOOPS_PER_TRACK]);

bool shouldAutoSaveBeforeLoadIntoCurrent(const AnchorFields& anchor);

void applyLoadedSetAnchorFields(uint32_t sourceSequence, AnchorFields& anchor);

#if defined(ARDUINO)
bool ensureDirectory(const char* path);
bool atomicRenameTempFile(const char* tempPath, const char* finalPath);
bool verifySaveFileTokenAtPath(const char* path);
bool patchLastActiveUnix(const char* metaPath, uint32_t lastActiveUnix);
bool patchAnchorFields(const char* metaPath, const AnchorFields& anchor);
#endif

}  // namespace CurrentSetStorage
