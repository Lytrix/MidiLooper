//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// The cold undo states (DEC-254, E4.6c): states of a slot below its oldest hot state, whose rows are on the card only.
// A cold state is a descriptor — revision, loop length, and per run its record in the run-once store (sector, rows,
// kind) — held in PSRAM so the UNDO level counts it, a save lists it in the slot's state record, the boot marks its
// extents, and a fetch (E4.6d) knows what to read.
//
// * Per slot a list, oldest first; states are pushed at the newest end (a demoted hot state is the newest cold one) and
//   dropped at the oldest (the slot's depth is full) or all at once (the slot is cleared).
// * Run records are shared by many states (an overdub's chain lists the take again and again): a directory, keyed by
//   record sector, holds each once with a reference count; a state lists directory indices in chunks of eight.
// * Bounded: `kStates` descriptors (64 slots × 99), `kChunks` chunks, `kDir` records. `push` refuses (false) when one is
//   full — the caller then forgets the state, as before the cold tier.
// No allocation, no I/O.

#pragma once

#include <cstdint>

namespace ContentEngine {
namespace cold {

constexpr uint16_t kNone = 0xFFFFu;
constexpr uint16_t kStates = 64u * 99u;
constexpr uint16_t kChunks = 8192;
constexpr uint8_t kChunkRefs = 8;
constexpr uint16_t kDir = 8192;  // a power of two (open addressing)
constexpr uint32_t kEmptySector = 0xFFFFFFFFu;

/// A run record in the run-once store.
struct CardRunRef {
  uint32_t sector = kEmptySector;
  uint32_t rows = 0;
  uint8_t kind = 0;
};

template <uint8_t Slots = 64>
class ColdTier {
 public:
  void clear() {
    for (auto& d : dir_) d = Dir{};
    occupied_ = 0;
    for (uint16_t i = 0; i < kStates; ++i) states_[i].next = static_cast<uint16_t>(i + 1 < kStates ? i + 1 : kNone);
    freeState_ = 0;
    for (uint16_t i = 0; i < kChunks; ++i) chunks_[i].next = static_cast<uint16_t>(i + 1 < kChunks ? i + 1 : kNone);
    freeChunk_ = 0;
    for (uint8_t s = 0; s < Slots; ++s) {
      head_[s] = tail_[s] = kNone;
      count_[s] = 0;
    }
    total_ = 0;
  }

  uint8_t count(uint8_t slot) const { return slot < Slots ? count_[slot] : 0; }
  uint16_t total() const { return total_; }

  /// Slot `slot`'s newest cold state. False (nothing changes) when a table is full.
  bool push(uint8_t slot, uint32_t revision, uint32_t loop, const CardRunRef* runs, uint8_t n) {
    return insertAfter(slot, tail_[slot < Slots ? slot : 0], revision, loop, runs, n) != kNone;
  }
  /// A cold state of slot `slot` placed after its state `after` (kNone: as its oldest) — a boot puts the saved cold
  /// states below those its own load demoted. Its index, or kNone when a table is full.
  uint16_t insertAfter(uint8_t slot, uint16_t after, uint32_t revision, uint32_t loop, const CardRunRef* runs,
                       uint8_t n) {
    if (slot >= Slots || freeState_ == kNone) return kNone;
    const uint16_t chunks = static_cast<uint16_t>((n + kChunkRefs - 1) / kChunkRefs);
    if (!chunksFree(chunks)) return kNone;
    uint16_t idx[64];
    for (uint8_t i = 0; i < n; ++i) {
      idx[i] = addRef(runs[i]);
      if (idx[i] == kNone) {  // the directory is full: undo what was taken
        for (uint8_t k = 0; k < i; ++k) release(idx[k]);
        return kNone;
      }
    }
    const uint16_t st = freeState_;
    freeState_ = states_[st].next;
    State& x = states_[st];
    x.revision = revision;
    x.loop = loop;
    x.n = n;
    x.next = kNone;
    x.chunk = kNone;
    uint16_t* link = &x.chunk;
    for (uint8_t i = 0; i < n; ++i) {
      if (i % kChunkRefs == 0) {
        const uint16_t c = freeChunk_;
        freeChunk_ = chunks_[c].next;
        chunks_[c].next = kNone;
        *link = c;
        link = &chunks_[c].next;
      }
      chunkOf(x, i).ref[i % kChunkRefs] = idx[i];
    }
    if (after == kNone) {
      x.next = head_[slot];
      head_[slot] = st;
    } else {
      x.next = states_[after].next;
      states_[after].next = st;
    }
    if (x.next == kNone) tail_[slot] = st;
    ++count_[slot];
    ++total_;
    return st;
  }
  /// Forgets slot `slot`'s oldest cold state. False: none.
  bool dropOldest(uint8_t slot) {
    if (slot >= Slots || head_[slot] == kNone) return false;
    const uint16_t st = head_[slot];
    head_[slot] = states_[st].next;
    if (head_[slot] == kNone) tail_[slot] = kNone;
    freeStateAt(st);
    --count_[slot];
    --total_;
    ++epoch_[slot];
    return true;
  }
  /// Changes when a slot's cold states are dropped (a save listing them restarts on a change).
  uint16_t epoch(uint8_t slot) const { return slot < Slots ? epoch_[slot] : 0; }
  /// Forgets every cold state of the slot (newest first is not needed: they all go).
  void clearSlot(uint8_t slot) {
    while (dropOldest(slot)) {
    }
  }

  // ---- Reading: a slot's states oldest first ----------------------------------------------------------------------
  uint16_t first(uint8_t slot) const { return slot < Slots ? head_[slot] : kNone; }
  uint16_t next(uint16_t st) const { return states_[st].next; }
  /// Slot `slot`'s `k`-th cold state, oldest first (kNone: none).
  uint16_t at(uint8_t slot, uint8_t k) const {
    uint16_t st = first(slot);
    while (st != kNone && k-- > 0) st = states_[st].next;
    return st;
  }
  uint32_t revision(uint16_t st) const { return states_[st].revision; }
  uint32_t loop(uint16_t st) const { return states_[st].loop; }
  uint8_t runCount(uint16_t st) const { return states_[st].n; }
  CardRunRef run(uint16_t st, uint8_t i) const {
    const Dir& d = dir_[chunkOf(states_[st], i).ref[i % kChunkRefs]];
    CardRunRef r;
    r.sector = d.sector;
    r.rows = d.rows;
    r.kind = d.kind;
    return r;
  }

 private:
  struct Dir {
    uint32_t sector = kEmptySector;
    uint32_t rows = 0;
    uint8_t kind = 0;
    uint16_t refs = 0;
  };
  struct State {
    uint32_t revision = 0, loop = 0;
    uint16_t chunk = kNone, next = kNone;
    uint8_t n = 0;
  };
  struct Chunk {
    uint16_t ref[kChunkRefs] = {};
    uint16_t next = kNone;
  };

  static uint16_t hash(uint32_t sector) { return static_cast<uint16_t>((sector * 2654435761u) >> 19) & (kDir - 1); }
  static constexpr uint32_t kTomb = 0xFFFFFFFEu;  // a removed entry: probes go on past it, an insert may reuse it
  uint16_t addRef(const CardRunRef& r) {
    uint16_t h = hash(r.sector), reuse = kNone;
    for (uint16_t probe = 0; probe < kDir; ++probe, h = static_cast<uint16_t>((h + 1) & (kDir - 1))) {
      Dir& d = dir_[h];
      if (d.sector == r.sector) {
        ++d.refs;
        return h;
      }
      if (d.sector == kTomb) {
        if (reuse == kNone) reuse = h;
        continue;
      }
      if (d.sector == kEmptySector) {
        if (reuse == kNone) {
          if (occupied_ + 1 >= kDir) return kNone;  // keep an empty entry: probes end
          reuse = h;
          ++occupied_;
        }
        break;
      }
    }
    if (reuse == kNone) return kNone;
    Dir& d = dir_[reuse];
    d.sector = r.sector;
    d.rows = r.rows;
    d.kind = r.kind;
    d.refs = 1;
    return reuse;
  }
  /// One reference to directory entry `h` goes; the last one leaves a tombstone.
  void release(uint16_t h) {
    if (--dir_[h].refs != 0) return;
    dir_[h].sector = kTomb;
  }
  bool chunksFree(uint16_t n) const {
    uint16_t c = freeChunk_;
    for (uint16_t k = 0; k < n; ++k) {
      if (c == kNone) return false;
      c = chunks_[c].next;
    }
    return true;
  }
  Chunk& chunkOf(State& x, uint8_t i) {
    uint16_t c = x.chunk;
    for (uint8_t k = 0; k < i / kChunkRefs; ++k) c = chunks_[c].next;
    return chunks_[c];
  }
  const Chunk& chunkOf(const State& x, uint8_t i) const {
    uint16_t c = x.chunk;
    for (uint8_t k = 0; k < i / kChunkRefs; ++k) c = chunks_[c].next;
    return chunks_[c];
  }
  void freeStateAt(uint16_t st) {
    State& x = states_[st];
    uint16_t c = x.chunk;
    for (uint8_t i = 0; i < x.n; ++i) {
      if (i > 0 && i % kChunkRefs == 0) c = chunks_[c].next;
      release(chunks_[c].ref[i % kChunkRefs]);
    }
    c = x.chunk;
    while (c != kNone) {
      const uint16_t nx = chunks_[c].next;
      chunks_[c].next = freeChunk_;
      freeChunk_ = c;
      c = nx;
    }
    x.chunk = kNone;
    x.n = 0;
    x.next = freeState_;
    freeState_ = st;
  }

  Dir dir_[kDir];
  State states_[kStates];
  Chunk chunks_[kChunks];
  uint16_t head_[Slots] = {}, tail_[Slots] = {};
  uint8_t count_[Slots] = {};
  uint16_t epoch_[Slots] = {};
  uint16_t freeState_ = kNone, freeChunk_ = kNone, occupied_ = 0, total_ = 0;
};

}  // namespace cold
}  // namespace ContentEngine
