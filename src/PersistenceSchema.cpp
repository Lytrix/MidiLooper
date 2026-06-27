//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "PersistenceSchema.h"

namespace PersistenceSchema {
namespace {

uint32_t crc32Update(uint32_t crc, const uint8_t* data, size_t size) {
  crc = ~crc;
  for (size_t i = 0; i < size; ++i) {
    crc ^= data[i];
    for (int bit = 0; bit < 8; ++bit) {
      const uint32_t mask = static_cast<uint32_t>(-(crc & 1U));
      crc = (crc >> 1U) ^ (0xEDB88320UL & mask);
    }
  }
  return ~crc;
}

}  // namespace

uint32_t crc32(const uint8_t* data, size_t size) {
  return crc32Update(0, data, size);
}

uint32_t crc32Continue(uint32_t crc, const uint8_t* data, size_t size) {
  return crc32Update(~crc, data, size);
}

}  // namespace PersistenceSchema
