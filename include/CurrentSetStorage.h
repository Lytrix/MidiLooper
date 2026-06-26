//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#pragma once

#include <cstddef>
#include <cstdint>

#include "StorageLoopIo.h"

namespace CurrentSetStorage {

constexpr uint32_t CONTAINER_VERSION = 6;
constexpr uint32_t COMPLETE_MAGIC = 0x45564153UL;  // "SAVE"

constexpr char kSetsRoot[] = "/Sets";
constexpr char kSetIndexPath[] = "/Sets/index.bin";
constexpr char kSetIndexTempPath[] = "/Sets/index.bin.tmp";
constexpr char kCurrentSetDir[] = "/Sets/_current";
constexpr char kCurrentMetaPath[] = "/Sets/_current/meta.bin";
constexpr char kCurrentMetaTempPath[] = "/Sets/_current/meta.bin.tmp";
constexpr char kCheckpointsDir[] = "/Sets/_current/checkpoints";
constexpr char kLegacyMonolithPath[] = "/midilooper_state.raw";

/// Byte offset of lastActiveUnix inside v6 meta.bin (after containerVersion).
constexpr size_t kLastActiveUnixOffset = sizeof(uint32_t);
/// Byte offset of AnchorFields inside v6 meta.bin.
constexpr size_t kAnchorFieldsOffset = sizeof(uint32_t) + sizeof(uint32_t);

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

bool writeCompleteMagic(const StorageIo& io);
bool verifyCompleteMagicAtEnd(const uint8_t* fileBytes, size_t fileSize);

#if defined(ARDUINO)
bool ensureDirectory(const char* path);
bool atomicRenameTempFile(const char* tempPath, const char* finalPath);
bool verifyFileCompleteMagic(const char* path);
bool patchLastActiveUnix(const char* metaPath, uint32_t lastActiveUnix);
bool patchAnchorFields(const char* metaPath, const AnchorFields& anchor);
#endif

}  // namespace CurrentSetStorage
