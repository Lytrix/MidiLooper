//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// The milestone library's on-card format (sets browser slice 1c-i; DEC-258).
//
// One preallocated file of `kSectors` × 512 B, read and written by sector only:
//   sectors 0–1        the index header, A / B (`IndexHeader`): the valid one with the higher sequence is current
//   sectors 2–513      `kIndexRecords` index records of 64 B, 8 a sector (`Record`), in publication order
//   two regions        the checkpoint, A / B (`kCheckpointSectors` each: a `CheckpointHeader` sector, then the set)
//   the rest           the milestone log, append-only: each milestone a `BlobHeader` sector, then its slots' set files,
//                      each starting on a sector
// A deletion (DEC-268) is a record too — a tombstone (`kind` 1, no blob) naming the milestone it hides (`parent`), or,
// with `parent` 0, every milestone of its set published before it — published as a milestone's record and header are.
// Nothing already written is rewritten; a deleted milestone's blob keeps its log space.
// A milestone is published in one order — blob, record, header (each programmed before the next) — so a header names
// only complete milestones; records and log space past it are ignored and reused. Every structure carries a CRC-32.
// `kMaxSetSectors` is the largest whole set (every content row, every run, every state, every slot), checked at
// compile time against what the checkpoint regions and the log hold. No allocation.

#pragma once

#include <cstdint>

#include "ContentEngine/EngineConfig.h"
#include "ContentEngine/SetFile.h"

namespace ContentEngine {
namespace library {

constexpr uint32_t kSector = 512;
constexpr uint32_t kSectors = 2097152;  // 1 GiB (owner 2026-11-08)
constexpr uint32_t kIndexRecords = 4096;
constexpr uint32_t kRecordBytes = 64;
constexpr uint32_t kRecordsPerSector = kSector / kRecordBytes;
constexpr uint32_t kHeaderSectors = 2;
constexpr uint32_t kRecordSectors = kIndexRecords / kRecordsPerSector;
constexpr uint8_t kSlots = Config::kSlotCount;

// The largest slot files of a whole set: every content row, each run once, every history state, a header and trailer
// a slot (`SaveJob`'s layout: 24 B header, 8 B a run, 16 B a row, 9 B + 2 B a run index a state, 4 B CRC).
constexpr uint32_t kMaxSetBytes = kSlots * (24u + 4u) + Config::kRunCount * 8u + Config::kContentRows * kSetFileRowBytes +
                                  Config::kHistoryDepth * (9u + 2u * Config::kRunsPerManifest);
constexpr uint32_t kMaxSetSectors = 1 + (kMaxSetBytes + kSector - 1) / kSector + kSlots;  // header, slots aligned
constexpr uint32_t kCheckpointSectors = 1 + kMaxSetSectors;
constexpr uint32_t kCheckpointBase = kHeaderSectors + kRecordSectors;
constexpr uint32_t kLogBase = kCheckpointBase + 2 * kCheckpointSectors;
constexpr uint32_t kLogSectors = kSectors - kLogBase;
static_assert(kMaxSetSectors == 4300, "DEC-258: the whole-set bound (2.2 MB) — revisit the decision if it moves");
static_assert(kLogSectors / kMaxSetSectors >= 485, "DEC-258: the log holds at least 485 sets of the largest size");

enum class Stage : uint8_t { Sketch, Building, Exploration, Take, Arrangement, Final, kCount };
inline char stageLetter(Stage s) {
  static const char kLetters[] = "SBETAF";
  return static_cast<uint8_t>(s) < 6 ? kLetters[static_cast<uint8_t>(s)] : '?';
}
inline const char* stageName(Stage s) {
  static const char* const kNames[] = {"SKETCH", "BUILDING", "EXPLORATION", "TAKE", "ARRANGEMENT", "FINAL"};
  return static_cast<uint8_t>(s) < 6 ? kNames[static_cast<uint8_t>(s)] : "?";
}

// ---- Byte helpers (little-endian) -------------------------------------------------------------------------------------
inline void put16(uint8_t* b, uint16_t v) {
  b[0] = static_cast<uint8_t>(v);
  b[1] = static_cast<uint8_t>(v >> 8);
}
inline void put32(uint8_t* b, uint32_t v) {
  put16(b, static_cast<uint16_t>(v));
  put16(b + 2, static_cast<uint16_t>(v >> 16));
}
inline uint16_t get16(const uint8_t* b) { return static_cast<uint16_t>(b[0] | (b[1] << 8)); }
inline uint32_t get32(const uint8_t* b) { return get16(b) | (static_cast<uint32_t>(get16(b + 2)) << 16); }

// ---- The index header ---------------------------------------------------------------------------------------------
constexpr uint32_t kHeaderMagic = 0x3142494Cu;  // "LIB1"

struct IndexHeader {
  uint32_t sequence = 0;  // higher is newer; 0 with count 0: an empty library
  uint16_t count = 0;     // published records
  uint32_t appendSector = 0;  // the log's next free sector, relative to `kLogBase`
  uint32_t nextSequence = 1;  // the next milestone's internal sequence (never reused)
};

inline void encode(const IndexHeader& h, uint8_t* sector) {
  for (uint32_t i = 0; i < kSector; ++i) sector[i] = 0;
  put32(sector, kHeaderMagic);
  put32(sector + 4, h.sequence);
  put16(sector + 8, h.count);
  put32(sector + 12, h.appendSector);
  put32(sector + 16, h.nextSequence);
  put32(sector + 20, crc32Update(0, sector, 20));
}
inline bool decode(const uint8_t* sector, IndexHeader& h) {
  if (get32(sector) != kHeaderMagic || get32(sector + 20) != crc32Update(0, sector, 20)) return false;
  h.sequence = get32(sector + 4);
  h.count = get16(sector + 8);
  h.appendSector = get32(sector + 12);
  h.nextSequence = get32(sector + 16);
  return h.count <= kIndexRecords && h.appendSector <= kLogSectors;
}
/// The current header from the two sector images: the valid one with the higher sequence; `which` its sector (0 / 1),
/// so the next header goes to the other. Neither valid: an empty library, written first to sector 0.
inline IndexHeader currentHeader(const uint8_t* a, const uint8_t* b, uint8_t& which) {
  IndexHeader ha, hb;
  const bool va = decode(a, ha), vb = decode(b, hb);
  if (va && (!vb || ha.sequence >= hb.sequence)) {
    which = 0;
    return ha;
  }
  if (vb) {
    which = 1;
    return hb;
  }
  which = 1;  // the first header then goes to sector 0
  return IndexHeader{};
}

// ---- Index records ------------------------------------------------------------------------------------------------
constexpr uint8_t kMilestone = 0, kDeletion = 1;

struct Record {
  uint32_t sequence = 0;        // internal, unique, never shown
  uint16_t set = 0;             // SET nn (1-based)
  Stage stage = Stage::Sketch;
  uint8_t stageNumber = 0;      // T02: 2 (per stage within its set)
  uint32_t parent = 0;          // the parent milestone's sequence (0: none — new work with no saved origin)
  uint32_t wallTime = 0;        // RTC seconds (0: the clock was not valid)
  uint16_t tempoTenths = 0;
  uint8_t loops = 0;            // slots with content
  uint8_t kind = 0;             // kMilestone, or kDeletion: a tombstone (DEC-268; byte 19, zero in every older record)
  uint32_t shortestLoop = 0, longestLoop = 0;  // ticks (0: no loops)
  uint32_t logSector = 0;       // the blob, relative to `kLogBase`
  uint32_t logSectors = 0;
  uint8_t mosaic[16] = {};      // 64 slots, 2 bits each: 0 empty, 1–3 note-count terciles
};

inline void encode(const Record& r, uint8_t* out) {
  for (uint32_t i = 0; i < kRecordBytes; ++i) out[i] = 0;
  put32(out, r.sequence);
  put16(out + 4, r.set);
  out[6] = static_cast<uint8_t>(r.stage);
  out[7] = r.stageNumber;
  put32(out + 8, r.parent);
  put32(out + 12, r.wallTime);
  put16(out + 16, r.tempoTenths);
  out[18] = r.loops;
  out[19] = r.kind;
  put32(out + 20, r.shortestLoop);
  put32(out + 24, r.longestLoop);
  put32(out + 28, r.logSector);
  put32(out + 32, r.logSectors);
  for (int i = 0; i < 16; ++i) out[36 + i] = r.mosaic[i];
  put32(out + 60, crc32Update(0, out, 60));
}
inline bool decode(const uint8_t* in, Record& r) {
  if (get32(in + 60) != crc32Update(0, in, 60) || get32(in) == 0) return false;
  r.sequence = get32(in);
  r.set = get16(in + 4);
  if (in[6] >= static_cast<uint8_t>(Stage::kCount)) return false;
  r.stage = static_cast<Stage>(in[6]);
  r.stageNumber = in[7];
  r.parent = get32(in + 8);
  r.wallTime = get32(in + 12);
  r.tempoTenths = get16(in + 16);
  r.loops = in[18];
  r.kind = in[19];
  if (r.kind > kDeletion) return false;
  r.shortestLoop = get32(in + 20);
  r.longestLoop = get32(in + 24);
  r.logSector = get32(in + 28);
  r.logSectors = get32(in + 32);
  for (int i = 0; i < 16; ++i) r.mosaic[i] = in[36 + i];
  if (r.kind == kDeletion) return r.logSectors == 0;
  return r.logSectors != 0 && r.logSector + r.logSectors <= kLogSectors;
}
/// Record `i`'s sector and its byte offset in that sector.
inline uint32_t recordSector(uint16_t i) { return kHeaderSectors + i / kRecordsPerSector; }
inline uint32_t recordOffset(uint16_t i) { return (i % kRecordsPerSector) * kRecordBytes; }

// ---- The blob header (a milestone's or a checkpoint's first sector) -----------------------------------------------
constexpr uint32_t kBlobMagic = 0x31424C4Du;  // "MLB1"
constexpr uint8_t kSurfaceBytes = 9;

struct BlobHeader {
  uint32_t sequence = 0;
  uint32_t noteIdHigh = 0;
  uint16_t tempoTenths = 0;
  uint8_t surface[kSurfaceBytes] = {};
  uint32_t slotBytes[kSlots] = {};  // each slot's set file (0: empty); slot k starts at the sector after k − 1's end
};

inline void encode(const BlobHeader& h, uint8_t* sector) {
  for (uint32_t i = 0; i < kSector; ++i) sector[i] = 0;
  put32(sector, kBlobMagic);
  put32(sector + 4, h.sequence);
  put32(sector + 8, h.noteIdHigh);
  put16(sector + 12, h.tempoTenths);
  for (uint8_t i = 0; i < kSurfaceBytes; ++i) sector[14 + i] = h.surface[i];
  for (uint8_t s = 0; s < kSlots; ++s) put32(sector + 24 + 4u * s, h.slotBytes[s]);
  put32(sector + 24 + 4u * kSlots, crc32Update(0, sector, 24 + 4u * kSlots));
}
inline bool decode(const uint8_t* sector, BlobHeader& h) {
  const uint32_t end = 24 + 4u * kSlots;
  if (get32(sector) != kBlobMagic || get32(sector + end) != crc32Update(0, sector, end)) return false;
  h.sequence = get32(sector + 4);
  h.noteIdHigh = get32(sector + 8);
  h.tempoTenths = get16(sector + 12);
  for (uint8_t i = 0; i < kSurfaceBytes; ++i) h.surface[i] = sector[14 + i];
  for (uint8_t s = 0; s < kSlots; ++s) h.slotBytes[s] = get32(sector + 24 + 4u * s);
  return true;
}
/// A blob's sectors: its header, then each non-empty slot's set file rounded up to whole sectors.
inline uint32_t blobSectors(const uint32_t* slotBytes) {
  uint32_t n = 1;
  for (uint8_t s = 0; s < kSlots; ++s) n += (slotBytes[s] + kSector - 1) / kSector;
  return n;
}

// ---- The checkpoint header ---------------------------------------------------------------------------------------
constexpr uint32_t kCheckpointMagic = 0x3150434Du;  // "MCP1"

struct CheckpointHeader {
  uint32_t sequence = 0;     // higher is newer
  uint32_t wallTime = 0;     // when it was taken (0: the clock was not valid)
  uint32_t base = 0;         // the workspace's base milestone then (sequence; 0 none)
  uint16_t set = 0;          // the set it was working in
  uint32_t loaded = 0;       // the milestone whose load it preceded (sequence)
  uint32_t sectors = 0;      // its blob (header included), from the region's second sector
};

inline void encode(const CheckpointHeader& h, uint8_t* sector) {
  for (uint32_t i = 0; i < kSector; ++i) sector[i] = 0;
  put32(sector, kCheckpointMagic);
  put32(sector + 4, h.sequence);
  put32(sector + 8, h.wallTime);
  put32(sector + 12, h.base);
  put16(sector + 16, h.set);
  put32(sector + 20, h.loaded);
  put32(sector + 24, h.sectors);
  put32(sector + 28, crc32Update(0, sector, 28));
}
inline bool decode(const uint8_t* sector, CheckpointHeader& h) {
  if (get32(sector) != kCheckpointMagic || get32(sector + 28) != crc32Update(0, sector, 28)) return false;
  h.sequence = get32(sector + 4);
  h.wallTime = get32(sector + 8);
  h.base = get32(sector + 12);
  h.set = get16(sector + 16);
  h.loaded = get32(sector + 20);
  h.sectors = get32(sector + 24);
  return h.sectors != 0 && h.sectors <= kMaxSetSectors;
}
inline uint32_t checkpointBase(uint8_t region) { return kCheckpointBase + region * kCheckpointSectors; }
/// The current checkpoint from both regions' header sectors: the valid one with the higher sequence (`region` 0 / 1),
/// or none (false). The next checkpoint goes to the other region.
inline bool currentCheckpoint(const uint8_t* a, const uint8_t* b, CheckpointHeader& out, uint8_t& region) {
  CheckpointHeader ha, hb;
  const bool va = decode(a, ha), vb = decode(b, hb);
  if (va && (!vb || ha.sequence >= hb.sequence)) {
    out = ha;
    region = 0;
    return true;
  }
  if (vb) {
    out = hb;
    region = 1;
    return true;
  }
  region = 1;  // the first checkpoint goes to region 0
  return false;
}

// ---- Capacity --------------------------------------------------------------------------------------------------------
enum class Fit : uint8_t { Ok, IndexFull, LogFull, TooLarge };
/// Whether a blob of `sectors` can be published after `h` (DEC-258: refused before anything is written).
inline Fit fits(const IndexHeader& h, uint32_t sectors) {
  if (sectors > kMaxSetSectors) return Fit::TooLarge;
  if (h.count >= kIndexRecords) return Fit::IndexFull;
  if (sectors > kLogSectors - h.appendSector) return Fit::LogFull;
  return Fit::Ok;
}

/// The next stage number in `set` for `stage`: one past the highest published (records `r[0..n)`).
inline uint8_t nextStageNumber(const Record* r, uint16_t n, uint16_t set, Stage stage) {
  uint8_t high = 0;
  for (uint16_t i = 0; i < n; ++i)
    if (r[i].set == set && r[i].stage == stage && r[i].stageNumber > high) high = r[i].stageNumber;
  return static_cast<uint8_t>(high + 1);
}

}  // namespace library
}  // namespace ContentEngine
