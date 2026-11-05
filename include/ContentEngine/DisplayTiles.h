//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// Piano-roll tiles (fixed-memory-engine E7, plan revision 3 § 9.3, invariant I6).
//
// * Span tile per (detail slot, bar): up to 256 note segments (8 B) plus a 32 x 128 one-bit raster = 2,560 B. A bar with
//   more than 256 segments keeps only its raster: the named `dense` state.
// * Density tile per (slot, bar) for every slot: 128 pitches x 4-bit note count (saturating at 15) = 64 B.
// * Tiles are replaced only by complete ones. A sweep writes a span tile into a spare and swaps it in whole; a density
//   tile is written into a staging copy and copied in one step between draws. The draw rule (`drawSource`): a bar's span
//   tile, else its density tile — whatever identity they hold — so a starved sweep, a focus switch, an Undo or an
//   exhausted pool show the previous content, never a blank bar. Nothing on the draw path allocates or reads pages.
// * `TileSweep` (a display job, soft) composes the slot's manifest — cycle 0 to warm up, then the steady cycle 1, so
//   notes held across the loop end appear from bar 0 — with every effective note selected (the piano roll shows stored
//   content; OUT-POLY limits only output), and publishes each bar as it completes. A sweep whose slot was given another
//   manifest stops (`Superseded`), returns its spare and releases its source lease.
// No allocation; `init()` writes every field.

#pragma once

#include <cstdint>

#include "ContentEngine/CheckpointTable.h"
#include "ContentEngine/SpanWindow.h"

namespace ContentEngine {

struct NoteSegment {
  uint16_t start;   // ticks from the bar start
  uint16_t length;  // ticks inside this bar
  uint8_t pitch, velocity;
  uint8_t flags;    // bits 0-3 channel, bit 4: continues from the previous bar, bit 5: continues into the next bar
  uint8_t pad;
};
static_assert(sizeof(NoteSegment) == 8, "a note segment is 8 bytes");

struct SpanTile {
  static constexpr uint16_t kSegments = 256;
  static constexpr uint8_t kColumns = 32;
  NoteSegment segments[kSegments];
  uint8_t raster[kColumns][16];  // 128 pitches, one bit each
};
static_assert(sizeof(SpanTile) == 2560, "a span tile is 2,560 bytes (plan § 9.3)");

struct DensityTile {
  uint8_t counts[64];  // pitch p: nibble (p & 1) of counts[p / 2], notes sounding in the bar, saturating at 15
};

template <uint8_t Slots = 64, uint8_t Bars = 64, uint8_t DetailSlots = 2, uint8_t Spares = 4>
class TileStore {
  static_assert(Bars <= 64, "one swept bit per bar");
 public:
  static constexpr uint8_t kNone = 0xFF;
  static constexpr uint16_t kSpanTiles = DetailSlots * Bars + Spares;
  enum class Kind : uint8_t { Empty, Span, Dense, Density };

  struct Header {
    uint32_t identity;  // the manifest swept
    uint16_t count;     // segments
    uint8_t slot;       // the slot it shows (detail tiles move between slots)
    bool dense;
  };
  struct Draw {
    Kind kind;
    uint32_t identity;
    const SpanTile* span;        // Span / Dense
    const Header* header;        // Span / Dense
    const DensityTile* density;  // Density
  };

  void init() {
    for (uint16_t i = 0; i < kSpanTiles; ++i) header_[i] = Header{0, 0, kNone, false};
    freeSpares_ = 0;
    for (uint16_t i = 0; i < Spares; ++i) spare_[freeSpares_++] = static_cast<uint16_t>(DetailSlots * Bars + i);
    for (uint8_t d = 0; d < DetailSlots; ++d) {
      detailSlot_[d] = kNone;
      for (uint8_t b = 0; b < Bars; ++b) map_[d][b] = static_cast<uint16_t>(d * Bars + b);
    }
    for (uint8_t s = 0; s < Slots; ++s) {
      wanted_[s] = 0;
      densityIdentity_[s] = 0;
      densitySwept_[s] = 0;
    }
  }

  /// The slot's manifest the display wants (a newer one supersedes a running sweep for the slot).
  void setWanted(uint8_t slot, uint32_t identity) { wanted_[slot] = identity; }
  uint32_t wanted(uint8_t slot) const { return wanted_[slot]; }
  /// Gives the slot detail tiles (focus or its successor). Its bars keep showing their previous tiles until swept.
  void setDetail(uint8_t detail, uint8_t slot) { detailSlot_[detail] = slot; }
  uint8_t detailOf(uint8_t slot) const {
    for (uint8_t d = 0; d < DetailSlots; ++d)
      if (detailSlot_[d] == slot) return d;
    return kNone;
  }

  /// A spare span tile to write into, or kNone when all are in use (the sweep then writes density only).
  uint16_t takeSpare() { return freeSpares_ == 0 ? kNoTile : spare_[--freeSpares_]; }
  void giveSpare(uint16_t tile) { spare_[freeSpares_++] = tile; }
  SpanTile& tile(uint16_t i) { return tiles_[i]; }
  Header& header(uint16_t i) { return header_[i]; }
  /// Swaps a complete span tile in for (detail, bar); the tile it replaces becomes a spare. One step.
  void publishSpan(uint8_t detail, uint8_t bar, uint16_t tile) {
    const uint16_t old = map_[detail][bar];
    map_[detail][bar] = tile;
    giveSpare(old);
  }
  /// Copies a complete density tile in for (slot, bar). One step. Density tiles keep one identity per slot (the sweep
  /// publishing them) and a swept bit per bar: the draw rule needs only whether a bar was ever swept.
  void publishDensity(uint8_t slot, uint8_t bar, const DensityTile& t, uint32_t identity) {
    density_[slot][bar] = t;
    densityIdentity_[slot] = identity;
    densitySwept_[slot] |= 1ull << bar;
  }

  /// The draw rule: the bar's span tile if the slot has detail tiles and the bar was swept for this slot, else its
  /// density tile, else Empty (a bar never swept: an empty slot, or one not yet loaded).
  Draw drawSource(uint8_t slot, uint8_t bar) const {
    const uint8_t d = detailOf(slot);
    if (d != kNone) {
      const uint16_t i = map_[d][bar];
      if (header_[i].identity != 0 && header_[i].slot == slot)
        return Draw{header_[i].dense ? Kind::Dense : Kind::Span, header_[i].identity, &tiles_[i], &header_[i], nullptr};
    }
    if ((densitySwept_[slot] >> bar) & 1u)
      return Draw{Kind::Density, densityIdentity_[slot], nullptr, nullptr, &density_[slot][bar]};
    return Draw{Kind::Empty, 0, nullptr, nullptr, nullptr};
  }
  uint16_t freeSpares() const { return freeSpares_; }

  static constexpr uint16_t kNoTile = 0xFFFF;

 private:
  SpanTile tiles_[kSpanTiles];
  Header header_[kSpanTiles];
  uint16_t map_[DetailSlots][Bars];
  uint8_t detailSlot_[DetailSlots];
  uint16_t spare_[Spares];
  uint16_t freeSpares_ = 0;
  DensityTile density_[Slots][Bars];
  uint32_t densityIdentity_[Slots];
  uint64_t densitySwept_[Slots];
  uint32_t wanted_[Slots];
};

template <typename Composer, typename Window, typename Manifests, typename Runs, typename Store>
class TileSweep {
 public:
  enum class Status : uint8_t { Idle, Running, Done, Superseded, Refused };
  static constexpr uint32_t kIndexGrain = 256;
  static constexpr uint8_t kMaxOpen = 64;  // notes open while a bar is built (composer tracks 48 effective instances)

  /// Pins `manifest` and sweeps it into `slot`'s tiles (span tiles too when the slot has detail tiles).
  Status start(Composer& composer, Store& store, Manifests& manifests, Runs& runs, uint8_t slot, ManifestId manifest,
               uint32_t windowTicks, uint32_t barTicks) {
    cancel();
    composer_ = &composer;
    store_ = &store;
    slot_ = slot;
    window_ = windowTicks;
    bar_ = barTicks;
    composing_ = false;
    cycle_ = 0;
    openCount_ = 0;
    currentBar_ = 0;
    spare_ = Store::kNoTile;
    bars_ = 0;
    overflow_ = 0;
    source_.open(manifests, runs, composer, manifest);
    identity_ = source_.identity();
    loop_ = source_.loopLength();
    store.setWanted(slot, identity_);
    beginBar();
    return status_ = Status::Running;
  }

  /// One grain: `maxRows` row units of composition (or index units while the index builds), then the tiles of every
  /// bar the composed windows completed.
  Status step(Window& w, uint32_t maxRows) {
    if (status_ != Status::Running) return status_;
    if (store_->wanted(slot_) != identity_) return stop(Status::Superseded);
    if (!composing_) {
      const IndexResult r = source_.prepare(kIndexGrain);
      if (r == IndexResult::CapacityExceeded) return stop(Status::Refused);
      if (!source_.prepared()) return status_;
      if (!composer_->begin(source_.source(), source_.count(), loop_, identity_)) return stop(Status::Refused);
      source_.dropIndexLease();
      composing_ = true;
      return status_;
    }
    if (!composer_->windowOpen() && !composer_->openWindow(w, window_)) {
      if (cycle_ == 1) {  // the steady cycle is tiled
        while (currentBar_ * bar_ < loop_) finishBar();
        return stop(Status::Done);
      }
      // Cycle 0 only follows its notes: those still sounding at the loop end start before bar 0 of cycle 1.
      for (uint8_t i = 0; i < openCount_;) {
        Open& n = open_[i];
        if (n.end != kOpenEnd && n.end <= loop_) {
          open_[i] = open_[--openCount_];
          continue;
        }
        n.start = 0;
        n.before = true;
        if (n.end != kOpenEnd) n.end -= loop_;
        ++i;
      }
      composer_->nextCycle();
      cycle_ = 1;
      return status_;
    }
    if (!composer_->composeRows(w, maxRows)) return status_;
    consume(w, cycle_ == 1);
    return status_;
  }

  void cancel() {
    if (status_ == Status::Running) stop(Status::Superseded);
  }
  Status status() const { return status_; }
  bool pinned() const { return source_.pinned(); }
  uint32_t barsPublished() const { return bars_; }
  /// Bar-crossing notes beyond the sweep's open table (cannot happen inside W: the composer holds at most 48).
  uint32_t overflow() const { return overflow_; }

 private:
  struct Open {
    uint32_t noteId;
    uint16_t runKey;
    uint32_t start, end;  // loop positions; end kOpenEnd until known
    uint8_t pitch, velocity, channel;
    bool before;  // started before this cycle (held across the loop end)
  };

  /// The window's entries, in tick order: NoteOn starts, TERMINAL ends. With `tile` (cycle 1) every bar boundary they
  /// pass is finished; in cycle 0 notes are only followed (ended ones dropped after the window).
  void consume(const Window& w, bool tile) {
    for (uint16_t k = 0; k < w.count; ++k) {
      const SpanEntry& e = w.entries[k];
      while (tile && e.tick >= (currentBar_ + 1) * bar_) finishBar();
      if (e.terminal) {
        for (uint8_t i = 0; i < openCount_; ++i)
          if (open_[i].noteId == e.noteId && open_[i].runKey == e.runKey && open_[i].end == kOpenEnd) {
            open_[i].end = e.tick;
            break;
          }
        continue;
      }
      if ((e.status & 0xF0) != 0x90 || e.data2 == 0) continue;
      const Open n{e.noteId, e.runKey, e.tick, e.length == kOpenLength ? kOpenEnd : e.tick + e.length,
                   e.data1, e.data2, static_cast<uint8_t>(e.status & 0x0F), false};
      if (n.end != kOpenEnd && (tile ? n.end <= (currentBar_ + 1) * bar_ : n.end <= loop_)) {
        if (tile) draw(n, n.start - currentBar_ * bar_, n.end - n.start, false, false);  // inside this bar: drawn now
        continue;
      }
      if (openCount_ == kMaxOpen) {  // more bar-crossing notes than the composer can hold open: named, not silent
        ++overflow_;
        continue;
      }
      open_[openCount_++] = n;
    }
    if (!tile) {
      for (uint8_t i = 0; i < openCount_;)
        if (open_[i].end != kOpenEnd && open_[i].end <= w.end)
          open_[i] = open_[--openCount_];
        else
          ++i;
      return;
    }
    while ((currentBar_ + 1) * bar_ <= w.end && currentBar_ * bar_ < loop_) finishBar();
  }

  void beginBar() {
    if (store_->detailOf(slot_) != Store::kNone && spare_ == Store::kNoTile) spare_ = store_->takeSpare();
    segCount_ = 0;
    dense_ = false;
    for (auto& c : density_.counts) c = 0;
    if (spare_ != Store::kNoTile)
      for (auto& col : store_->tile(spare_).raster)
        for (auto& b : col) b = 0;
  }

  /// Draws every open note's part inside the current bar, publishes the bar's tiles, and drops notes that ended.
  void finishBar() {
    const uint32_t a = currentBar_ * bar_, b = a + bar_;
    for (uint8_t i = 0; i < openCount_;) {
      Open& n = open_[i];
      const uint32_t s = n.start > a ? n.start : a;
      const uint32_t e = n.end == kOpenEnd || n.end > b ? b : n.end;
      if (e > s || (e == s && n.start >= a && !n.before))
        draw(n, s - a, e - s, n.start < a || n.before, n.end == kOpenEnd || n.end > b);
      if (n.end != kOpenEnd && n.end <= b) {
        open_[i] = open_[--openCount_];
      } else {
        ++i;
      }
    }
    publish();
    ++currentBar_;
    if (currentBar_ * bar_ < loop_) beginBar();
  }

  void draw(const Open& n, uint32_t start, uint32_t length, bool fromLeft, bool toRight) {
    uint8_t& c = density_.counts[n.pitch / 2];
    const uint8_t shift = (n.pitch & 1) ? 4 : 0;
    if (((c >> shift) & 0xF) < 15) c = static_cast<uint8_t>(c + (1 << shift));
    if (spare_ == Store::kNoTile) return;
    SpanTile& t = store_->tile(spare_);
    const uint32_t c0 = start * SpanTile::kColumns / bar_;
    uint32_t c1 = (start + (length ? length - 1 : 0)) * SpanTile::kColumns / bar_;
    if (c1 >= SpanTile::kColumns) c1 = SpanTile::kColumns - 1;
    for (uint32_t col = c0; col <= c1; ++col) t.raster[col][n.pitch / 8] |= static_cast<uint8_t>(1u << (n.pitch % 8));
    if (segCount_ == SpanTile::kSegments) {
      dense_ = true;  // more notes than segments: the raster alone (named dense state)
      return;
    }
    t.segments[segCount_++] = NoteSegment{static_cast<uint16_t>(start), static_cast<uint16_t>(length), n.pitch,
                                          n.velocity,
                                          static_cast<uint8_t>(n.channel | (fromLeft ? 0x10 : 0) | (toRight ? 0x20 : 0)),
                                          0};
  }

  void publish() {
    const uint8_t bar = static_cast<uint8_t>(currentBar_);
    store_->publishDensity(slot_, bar, density_, identity_);
    const uint8_t d = store_->detailOf(slot_);
    if (spare_ != Store::kNoTile && d != Store::kNone) {
      store_->header(spare_) = typename Store::Header{identity_, segCount_, slot_, dense_};
      store_->publishSpan(d, bar, spare_);
      spare_ = Store::kNoTile;
    }
    ++bars_;
  }

  Status stop(Status s) {
    if (spare_ != Store::kNoTile) {
      store_->giveSpare(spare_);
      spare_ = Store::kNoTile;
    }
    composer_->end();
    source_.close();
    return status_ = s;
  }

  Composer* composer_ = nullptr;
  Store* store_ = nullptr;
  SourceLease<Composer, Manifests, Runs> source_;
  uint8_t slot_ = 0;
  bool composing_ = false, dense_ = false;
  uint32_t identity_ = 0, window_ = 0, bar_ = 0, loop_ = 0, cycle_ = 0, currentBar_ = 0, bars_ = 0, overflow_ = 0;
  uint16_t segCount_ = 0, spare_ = 0xFFFF;
  uint8_t openCount_ = 0;
  Open open_[kMaxOpen];
  DensityTile density_;
  Status status_ = Status::Idle;
};

}  // namespace ContentEngine
