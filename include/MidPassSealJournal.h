//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#pragma once

#include <cstddef>
#include <cstdint>

#include "MidiEvent.h"

/// Incremental sealed-chunk append file during open capture (DEC-020 Phase 4).
/// Merged into authoritative slot bytes on deferred save; recovery reads prefix in Phase 5.
namespace MidPassSealJournal {

constexpr uint32_t kMagic = 0x4A534D4CUL;  // "LMSJ" little-endian
constexpr uint16_t kVersion = 1;

struct FileHeader {
  uint32_t magic = kMagic;
  uint16_t version = kVersion;
  uint8_t trackIndex = 0;
  uint8_t slotIndex = 0;
};

struct RecordHeader {
  uint16_t chunkId = 0;
  uint32_t sealSequence = 0;
  uint16_t eventCount = 0;
};

constexpr size_t fileHeaderByteSize() { return sizeof(FileHeader); }

constexpr size_t recordHeaderByteSize() { return sizeof(RecordHeader); }

constexpr size_t recordByteSize(uint16_t eventCount) {
  return recordHeaderByteSize() + static_cast<size_t>(eventCount) * sizeof(MidiEvent);
}

}  // namespace MidPassSealJournal
