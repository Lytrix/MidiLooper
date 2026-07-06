//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#pragma once

#include <cstdint>

namespace Diagnostics {

constexpr uint8_t kTraceFormatVersion = 1;
constexpr uint32_t kLastRecordMagic = 0x44494147u;  // 'DIAG'

enum class Category : uint8_t {
  Memory = 1,
  Playback = 2,
  Edit = 3,
  Storage = 4,
  Display = 5,
  Validation = 6,
};

constexpr uint16_t makeEventId(Category category, uint8_t localId) {
  return static_cast<uint16_t>((static_cast<uint16_t>(category) << 8) | localId);
}

constexpr uint8_t eventCategory(uint16_t eventId) {
  return static_cast<uint8_t>(eventId >> 8);
}

struct DiagContextSnapshot {
  uint8_t trackState = 0;
  uint8_t editSession = 255;
  uint8_t looperState = 0;
  uint8_t flags = 0;
};

enum DiagContextFlag : uint8_t {
  kFlagLoadSaveOverlay = 1u << 0,
  kFlagSettingsOverlay = 1u << 1,
  kFlagPanic = 1u << 2,
};

enum DiagRecordFlag : uint8_t {
  kRecordHasHeapSnapshot = 1u << 0,
};

struct DiagTraceRecord {
  uint32_t micros = 0;
  uint16_t eventId = 0;
  DiagContextSnapshot context{};
  uint8_t recordFlags = 0;
  uint8_t reserved = 0;
  uint32_t heapFree = 0;
  uint32_t heapUsed = 0;
  uint32_t extmemFree = 0;
  uint32_t payload = 0;
};

struct DiagTraceHeader {
  uint8_t magic[4] = {'D', 'I', 'A', 'G'};
  uint8_t formatVersion = kTraceFormatVersion;
  uint8_t recordSize = static_cast<uint8_t>(sizeof(DiagTraceRecord));
  uint16_t firmwareBuild = 0;
  uint32_t reserved = 0;
};

struct DiagLastRecordSlot {
  uint32_t magic = 0;
  DiagTraceRecord record{};
};

}  // namespace Diagnostics
