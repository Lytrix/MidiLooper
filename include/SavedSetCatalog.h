//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#pragma once

#include <cstddef>
#include <cstdint>

#include "Globals.h"
#include "StorageLoopIo.h"

namespace SavedSetCatalog {

constexpr uint32_t kSavedSetMetaTrailerMagic = 0x44535453UL;  // "SSTD"
constexpr size_t kSavedSetLabelCapacity = 32;
/// Byte size of on-disk SavedSet metadata trailer (before kSaveFileToken).
constexpr size_t kSavedSetMetadataTrailerByteSize = 60;

enum class FolderNamingMode : uint8_t {
  Unknown = 0,
  DateSequence = 1,
  SequenceUid = 2,
};

struct SetIndex {
  uint32_t nextSequence = 1;
};

struct SavedSetMetadata {
  uint32_t sequence = 0;
  FolderNamingMode folderNamingMode = FolderNamingMode::Unknown;
  uint32_t createdAtUnix = 0;
  char userLabel[kSavedSetLabelCapacity] = {};
  uint16_t masterLoopBars = 0;
  uint8_t trackCount = 0;
  uint8_t filledSlotCount = 0;
  uint8_t perTrackFilledSlots[Config::NUM_TRACKS] = {};
};

struct SavedSetFolderListEntry {
  char folderName[16];
  uint32_t sequence;
};

bool writeSetIndex(const StorageIo& io, const SetIndex& index);
bool readSetIndex(const StorageIo& io, SetIndex& index);

bool parseSavedSetFolderName(const char* folderName, uint32_t& sequence,
                             FolderNamingMode* namingModeOut = nullptr);
bool isSavedSetFolderName(const char* folderName);

bool formatSavedSetFolderName(uint32_t sequence, uint32_t unixTime,
                              bool hasValidDateForFolderNaming, char* out,
                              size_t outSize, FolderNamingMode* namingModeOut = nullptr);

uint32_t reconcileNextSequence(uint32_t indexedNextSequence,
                               uint32_t highestSeenSequence);
uint32_t allocateNextSequence(SetIndex& index);

void formatDefaultSavedSetLabel(uint32_t createdAtUnix, const char* fallbackFolderName,
                                char* out, size_t outSize);

bool writeSavedSetMetadataTrailer(const StorageIo& io,
                                  const SavedSetMetadata& metadata);
bool readSavedSetMetadataTrailer(const StorageIo& io, SavedSetMetadata& metadata);

}  // namespace SavedSetCatalog
