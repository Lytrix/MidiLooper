//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "SavedSetCatalog.h"

#include <cstdlib>
#include <cstdio>
#include <cstring>

namespace SavedSetCatalog {
namespace {

bool ioWrite(const StorageIo& io, const void* data, size_t size) {
  return io.write && io.write(data, size);
}

bool ioRead(const StorageIo& io, void* data, size_t size) {
  return io.read && io.read(data, size);
}

bool isDigits(const char* text, size_t length) {
  if (text == nullptr || length == 0) {
    return false;
  }
  for (size_t i = 0; i < length; ++i) {
    if (text[i] < '0' || text[i] > '9') {
      return false;
    }
  }
  return true;
}

const char* basenameFromPath(const char* name) {
  if (name == nullptr) {
    return nullptr;
  }
  const char* lastSlash = std::strrchr(name, '/');
  return (lastSlash != nullptr) ? lastSlash + 1 : name;
}

struct SavedSetMetadataTrailerWire {
  uint32_t magic = kSavedSetMetaTrailerMagic;
  uint32_t sequence = 0;
  uint8_t folderNamingMode = 0;
  uint8_t reserved[3] = {0, 0, 0};
  uint32_t createdAtUnix = 0;
  uint16_t masterLoopBars = 0;
  uint8_t trackCount = 0;
  uint8_t filledSlotCount = 0;
  uint8_t perTrackFilledSlots[Config::NUM_TRACKS] = {};
  char userLabel[kSavedSetLabelCapacity] = {};
};

static_assert(sizeof(SavedSetMetadataTrailerWire) == kSavedSetMetadataTrailerByteSize,
              "SavedSet metadata trailer wire size mismatch");

bool unixToUtcDate(uint32_t unixTime, int& year, unsigned& month, unsigned& day) {
  if (unixTime == 0) {
    return false;
  }
  const int64_t daysSinceEpoch = static_cast<int64_t>(unixTime / 86400UL);
  int64_t z = daysSinceEpoch + 719468;
  const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
  const uint32_t dayOfEra = static_cast<uint32_t>(z - era * 146097);  // [0, 146096]
  const uint32_t yearOfEra =
      (dayOfEra - dayOfEra / 1460 + dayOfEra / 36524 - dayOfEra / 146096) /
      365;  // [0, 399]
  year = static_cast<int>(yearOfEra) + static_cast<int>(era) * 400;
  const uint32_t dayOfYear =
      dayOfEra - (365 * yearOfEra + yearOfEra / 4 - yearOfEra / 100);  // [0, 365]
  const uint32_t monthPrime = (5 * dayOfYear + 2) / 153;                // [0, 11]
  day = dayOfYear - (153 * monthPrime + 2) / 5 + 1;                     // [1, 31]
  month = monthPrime + (monthPrime < 10 ? 3 : -9);                      // [1, 12]
  year += (month <= 2);
  return true;
}

}  // namespace

bool writeSetIndex(const StorageIo& io, const SetIndex& index) {
  return ioWrite(io, &index.nextSequence, sizeof(index.nextSequence));
}

bool readSetIndex(const StorageIo& io, SetIndex& index) {
  if (!ioRead(io, &index.nextSequence, sizeof(index.nextSequence))) {
    return false;
  }
  if (index.nextSequence == 0) {
    index.nextSequence = 1;
  }
  return true;
}

bool parseSavedSetFolderName(const char* folderName, uint32_t& sequence,
                             FolderNamingMode* namingModeOut) {
  sequence = 0;
  const char* name = basenameFromPath(folderName);
  if (name == nullptr || name[0] == '\0') {
    return false;
  }
  if (name[0] == '_' || std::strcmp(name, "index.bin") == 0) {
    return false;
  }

  const size_t length = std::strlen(name);
  if (length == 5 && isDigits(name, 5)) {
    const uint32_t parsed = static_cast<uint32_t>(std::strtoul(name, nullptr, 10));
    if (parsed == 0) {
      return false;
    }
    sequence = parsed;
    if (namingModeOut != nullptr) {
      *namingModeOut = FolderNamingMode::SequenceUid;
    }
    return true;
  }

  if (length == 10 && name[6] == '_' && isDigits(name, 6) && isDigits(name + 7, 3)) {
    const uint32_t parsed = static_cast<uint32_t>(std::strtoul(name + 7, nullptr, 10));
    if (parsed == 0) {
      return false;
    }
    sequence = parsed;
    if (namingModeOut != nullptr) {
      *namingModeOut = FolderNamingMode::DateSequence;
    }
    return true;
  }

  return false;
}

bool isSavedSetFolderName(const char* folderName) {
  uint32_t sequence = 0;
  return parseSavedSetFolderName(folderName, sequence, nullptr);
}

bool formatSavedSetFolderName(uint32_t sequence, uint32_t unixTime,
                              bool hasValidDateForFolderNaming, char* out,
                              size_t outSize, FolderNamingMode* namingModeOut) {
  if (out == nullptr || outSize == 0 || sequence == 0) {
    return false;
  }
  out[0] = '\0';

  if (hasValidDateForFolderNaming && unixTime != 0) {
    int year = 0;
    unsigned month = 0;
    unsigned day = 0;
    if (unixToUtcDate(unixTime, year, month, day)) {
      const int written = std::snprintf(
          out, outSize, "%02u%02u%02u_%03lu", static_cast<unsigned>(year % 100), month, day,
          static_cast<unsigned long>(sequence));
      if (written > 0 && static_cast<size_t>(written) < outSize) {
        if (namingModeOut != nullptr) {
          *namingModeOut = FolderNamingMode::DateSequence;
        }
        return true;
      }
      return false;
    }
  }

  const int written =
      std::snprintf(out, outSize, "%05lu", static_cast<unsigned long>(sequence));
  if (written <= 0 || static_cast<size_t>(written) >= outSize) {
    return false;
  }
  if (namingModeOut != nullptr) {
    *namingModeOut = FolderNamingMode::SequenceUid;
  }
  return true;
}

uint32_t reconcileNextSequence(uint32_t indexedNextSequence,
                               uint32_t highestSeenSequence) {
  uint32_t reconciled = indexedNextSequence == 0 ? 1 : indexedNextSequence;
  const uint32_t minimumFromFolders =
      highestSeenSequence == UINT32_MAX ? UINT32_MAX : highestSeenSequence + 1;
  if (minimumFromFolders > reconciled) {
    reconciled = minimumFromFolders;
  }
  if (reconciled == 0) {
    reconciled = 1;
  }
  return reconciled;
}

uint32_t allocateNextSequence(SetIndex& index) {
  if (index.nextSequence == 0) {
    index.nextSequence = 1;
  }
  const uint32_t allocated = index.nextSequence;
  index.nextSequence++;
  if (index.nextSequence == 0) {
    index.nextSequence = UINT32_MAX;
  }
  return allocated;
}

void formatDefaultSavedSetLabel(uint32_t createdAtUnix, const char* fallbackFolderName,
                                char* out, size_t outSize) {
  if (out == nullptr || outSize == 0) {
    return;
  }
  out[0] = '\0';

  if (createdAtUnix != 0) {
    static const char* kMonthNames[] = {"January",   "February", "March",    "April",
                                        "May",       "June",     "July",     "August",
                                        "September", "October",  "November", "December"};
    int year = 0;
    unsigned month = 0;
    unsigned day = 0;
    if (unixToUtcDate(createdAtUnix, year, month, day) && month >= 1 && month <= 12) {
      const int written = std::snprintf(out, outSize, "%u %s %d", day,
                                        kMonthNames[month - 1], year);
      if (written > 0 && static_cast<size_t>(written) < outSize) {
        return;
      }
    }
  }

  if (fallbackFolderName != nullptr && fallbackFolderName[0] != '\0') {
    std::snprintf(out, outSize, "%s", fallbackFolderName);
  }
}

bool writeSavedSetMetadataTrailer(const StorageIo& io,
                                  const SavedSetMetadata& metadata) {
  SavedSetMetadataTrailerWire wire{};
  wire.sequence = metadata.sequence;
  wire.folderNamingMode = static_cast<uint8_t>(metadata.folderNamingMode);
  wire.createdAtUnix = metadata.createdAtUnix;
  wire.masterLoopBars = metadata.masterLoopBars;
  wire.trackCount = metadata.trackCount;
  wire.filledSlotCount = metadata.filledSlotCount;
  std::memcpy(wire.perTrackFilledSlots, metadata.perTrackFilledSlots,
              sizeof(wire.perTrackFilledSlots));
  std::memcpy(wire.userLabel, metadata.userLabel, sizeof(wire.userLabel));
  return ioWrite(io, &wire, sizeof(wire));
}

bool readSavedSetMetadataTrailer(const StorageIo& io, SavedSetMetadata& metadata) {
  SavedSetMetadataTrailerWire wire{};
  if (!ioRead(io, &wire, sizeof(wire))) {
    return false;
  }
  if (wire.magic != kSavedSetMetaTrailerMagic || wire.sequence == 0) {
    return false;
  }
  metadata.sequence = wire.sequence;
  metadata.folderNamingMode = static_cast<FolderNamingMode>(wire.folderNamingMode);
  metadata.createdAtUnix = wire.createdAtUnix;
  metadata.masterLoopBars = wire.masterLoopBars;
  metadata.trackCount = wire.trackCount;
  metadata.filledSlotCount = wire.filledSlotCount;
  std::memcpy(metadata.perTrackFilledSlots, wire.perTrackFilledSlots,
              sizeof(metadata.perTrackFilledSlots));
  std::memcpy(metadata.userLabel, wire.userLabel, sizeof(metadata.userLabel));
  metadata.userLabel[sizeof(metadata.userLabel) - 1] = '\0';
  return true;
}

bool shouldRunEightHourFailsafe(bool hasMaterialChangesSinceAnchor,
                                uint32_t lastMaterialChangeUnix, uint32_t nowUnix,
                                bool captureActive) {
  if (!hasMaterialChangesSinceAnchor || captureActive) {
    return false;
  }
  if (lastMaterialChangeUnix == 0 || nowUnix == 0 || nowUnix < lastMaterialChangeUnix) {
    return false;
  }
  return nowUnix - lastMaterialChangeUnix >= kEightHourFailsafeSeconds;
}

}  // namespace SavedSetCatalog
