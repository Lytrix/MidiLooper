//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// The run-once store's run area (DEC-254, E4.6b; owner 2026-11-11: "I choose DEC-254's run-once store").
//
// * A run record: an extent of whole clusters in the run area — a header sector ("MLR1": slot, kind, rows, the rows'
//   CRC-32, a check word), then the rows, 32 a sector (16 B each, `SetFile`'s row bytes). The header is written last, so
//   a record cut short by power loss fails its check and is never taken.
// * Space: a bitmap of `kCluster`-sector clusters, built at boot from every valid state record (`mark`), then only
//   taken during a session (`take`, next fit); what no state record lists is free again at the next boot.
// * A run on the card: `RunId` → (record sector, the run's serial at writing) — a `RunId` is reused after release, its
//   serial is not (`RunTable::Run::serial`).
// Pure and bounded: no allocation, no I/O (the persistence owner reads and writes the sectors).

#pragma once

#include <cstdint>

namespace ContentEngine {
namespace runstore {

constexpr uint32_t kSector = 512;
constexpr uint32_t kRowBytes = 16;
constexpr uint32_t kRowsPerSector = kSector / kRowBytes;  // 32
constexpr uint32_t kCluster = 8;                            // sectors (4 KiB)
constexpr uint32_t kAreaSectors = 1u << 21;                 // 1 GiB
constexpr uint32_t kClusters = kAreaSectors / kCluster;     // 262,144
constexpr uint32_t kNoSector = 0xFFFFFFFFu;
constexpr uint32_t kRunMagic = 0x31524C4Du;  // "MLR1"

/// The sectors a run of `rows` takes: its header and its rows.
inline uint32_t sectorsFor(uint32_t rows) { return 1 + (rows + kRowsPerSector - 1) / kRowsPerSector; }
inline uint32_t clustersFor(uint32_t rows) { return (sectorsFor(rows) + kCluster - 1) / kCluster; }

inline void put32(uint8_t* b, uint32_t v) {
  for (uint8_t i = 0; i < 4; ++i) b[i] = static_cast<uint8_t>(v >> (8 * i));
}
inline uint32_t get32(const uint8_t* b) {
  return b[0] | static_cast<uint32_t>(b[1]) << 8 | static_cast<uint32_t>(b[2]) << 16 | static_cast<uint32_t>(b[3]) << 24;
}
inline uint32_t headerCheck(uint32_t slot, uint32_t kind, uint32_t rows, uint32_t crc) {
  return ((slot * 0x9E3779B1u) ^ (kind * 0x85EBCA77u) ^ (rows * 2654435761u) ^ crc) + 0x3C6EF372u;
}
/// A run record's header sector.
inline void writeHeader(uint8_t* b, uint8_t slot, uint8_t kind, uint32_t rows, uint32_t crc) {
  for (uint32_t i = 0; i < kSector; ++i) b[i] = 0;
  put32(b, kRunMagic);
  b[4] = slot;
  b[5] = kind;
  put32(b + 8, rows);
  put32(b + 12, crc);
  put32(b + 16, headerCheck(slot, kind, rows, crc));
}
/// True when `b` is a valid header of `slot`'s run of `kind` and `rows`; its rows' CRC in `crc`.
inline bool readHeader(const uint8_t* b, uint8_t slot, uint8_t kind, uint32_t rows, uint32_t& crc) {
  crc = get32(b + 12);
  return get32(b) == kRunMagic && b[4] == slot && b[5] == kind && get32(b + 8) == rows &&
         get32(b + 16) == headerCheck(slot, kind, rows, crc);
}

/// The run area's clusters: set = held by a run record a valid state record lists (or written this session).
class ClusterMap {
 public:
  void clear() {
    for (uint32_t& w : bits_) w = 0;
    cursor_ = 0;
    used_ = 0;
  }
  /// The extent at area sector `at` of a run of `rows` is held. False: outside the area.
  bool mark(uint32_t at, uint32_t rows) {
    if (at % kCluster != 0) return false;
    const uint32_t c = at / kCluster, n = clustersFor(rows);
    if (c >= kClusters || n > kClusters - c) return false;
    for (uint32_t k = c; k < c + n; ++k)
      if (!test(k)) {
        set(k);
        ++used_;
      }
    return true;
  }
  /// An extent for a run of `rows` (next fit from the last one taken, wrapping once): its first area sector, or
  /// `kNoSector` when no contiguous room is left.
  uint32_t take(uint32_t rows) {
    const uint32_t n = clustersFor(rows);
    if (n == 0 || n > kClusters) return kNoSector;
    uint32_t c = cursor_, run = 0, start = c, scanned = 0;
    while (scanned < kClusters + n) {
      if (c >= kClusters) {  // wrap: a run never spans the end
        c = 0;
        run = 0;
      }
      if (test(c)) {
        run = 0;
      } else {
        if (run == 0) start = c;
        if (++run == n) {
          for (uint32_t k = start; k < start + n; ++k) set(k);
          used_ += n;
          cursor_ = start + n;
          return start * kCluster;
        }
      }
      ++c;
      ++scanned;
    }
    return kNoSector;
  }
  uint32_t usedClusters() const { return used_; }
  bool held(uint32_t cluster) const { return cluster < kClusters && test(cluster); }

 private:
  bool test(uint32_t c) const { return (bits_[c / 32] >> (c % 32)) & 1u; }
  void set(uint32_t c) { bits_[c / 32] |= 1u << (c % 32); }
  uint32_t bits_[kClusters / 32] = {};
  uint32_t cursor_ = 0, used_ = 0;
};

/// A run on the card: its record's area sector and the run's serial when written (0: not on the card).
struct CardRun {
  uint32_t sector = kNoSector;
  uint32_t serial = 0;
};

}  // namespace runstore
}  // namespace ContentEngine
