//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#pragma once

#include <cstddef>
#include <cstdint>

namespace PersistenceSchema {

/// Shared schemaVersion for workspace.bin, index.bin, set.bin, and REVPK02 (major/minor u16).
constexpr uint16_t kSetRevisionSchemaVersion = 0x0001U;

inline uint8_t schemaMajor(uint16_t schemaVersion) {
  return static_cast<uint8_t>(schemaVersion >> 8);
}

inline uint8_t schemaMinor(uint16_t schemaVersion) {
  return static_cast<uint8_t>(schemaVersion & 0xFFU);
}

inline bool isSchemaMajorCompatible(uint16_t onDisk, uint16_t firmware) {
  return schemaMajor(onDisk) == schemaMajor(firmware);
}

uint32_t crc32(const uint8_t* data, size_t size);
uint32_t crc32Continue(uint32_t crc, const uint8_t* data, size_t size);

}  // namespace PersistenceSchema
