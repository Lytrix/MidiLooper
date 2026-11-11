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
// * `TileSweep` (a display job, soft) composes the slot's manifest with every effective note selected (the piano roll
//   shows stored content; OUT-POLY limits only output) and publishes each bar as it completes. Bars after the source's
//   wrap reach compose the same in cycle 0 as in steady state, so they are published in cycle 0; the early bars that
//   notes held across the loop end reach are published from cycle 1, after which the sweep stops (one loop plus those
//   bars, instead of two loops). A sweep whose slot was given another
//   manifest stops (`Superseded`), returns its spare and releases its source lease.
// No allocation; `init()` writes every field.

#pragma once

#include <cstdint>

#include "ContentEngine/CheckpointTable.h"
#include "ContentEngine/SpanWindow.h"
#include "ContentEngine/WorkerScheduler.h"

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
  uint8_t detailSlot(uint8_t detail) const { return detailSlot_[detail]; }
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

  /// E9.6: an empty (or cleared) slot shows nothing: its density bars and its detail tiles drop out. One step per bar.
  void clearSlot(uint8_t slot) {
    densitySwept_[slot] = 0;
    densityIdentity_[slot] = 0;
    wanted_[slot] = 0;
    const uint8_t d = detailOf(slot);
    if (d != kNone)
      for (uint8_t b = 0; b < Bars; ++b) header_[map_[d][b]].identity = 0;
  }
  /// The bar's density tile, or nullptr when the bar was never swept (overview, pitch extent).
  const DensityTile* densityTile(uint8_t slot, uint8_t bar) const {
    return ((densitySwept_[slot] >> bar) & 1u) ? &density_[slot][bar] : nullptr;
  }
  /// The identity the slot's density tiles were last published from (0: none).
  uint32_t densityIdentity(uint8_t slot) const { return densityIdentity_[slot]; }

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
    bindStarted_ = false;
    cycle_ = 0;
    openCount_ = 0;
    currentBar_ = 0;
    spare_ = Store::kNoTile;
    bars_ = 0;
    overflow_ = 0;
    windows_ = 0;
    deferred_ = 0;
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
      if (!bindStarted_) composer_->bindStart(source_.source(), source_.count(), loop_, identity_, true);
      bindStarted_ = true;
      const auto b = composer_->bindMore(Composer::kRunsPerStep);  // in steps (owner 2026-11-11)
      if (b == Composer::BindStep::Running) return status_;
      bindStarted_ = false;
      if (b == Composer::BindStep::Failed) return stop(Status::Refused);
      source_.dropIndexLease();
      composing_ = true;
      deferred_ = (composer_->wrapReach() + bar_ - 1) / bar_;  // bars the wrap reach touches: published from cycle 1
      return status_;
    }
    if (!composer_->windowOpen() && !composer_->openWindow(w, window_)) {
      while (currentBar_ * bar_ < loop_) finishBar();
      if (cycle_ == 1 || deferred_ == 0) return stop(Status::Done);
      // Notes still sounding at the loop end start before bar 0 of cycle 1, which re-tiles the deferred bars.
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
      currentBar_ = 0;
      beginBar();
      return status_;
    }
    if (!composer_->composeRows(w, maxRows)) return status_;
    ++windows_;
    consume(w);
    if (cycle_ == 1 && currentBar_ >= deferred_) return stop(Status::Done);
    return status_;
  }

  void cancel() {
    if (status_ == Status::Running) stop(Status::Superseded);
  }
  Status status() const { return status_; }
  bool pinned() const { return source_.pinned(); }
  uint32_t barsPublished() const { return bars_; }
  uint32_t windows() const { return windows_; }  // windows composed: one loop, plus the bars the wrap reach touches
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

  /// The window's entries, in tick order: NoteOn starts, TERMINAL ends; every bar boundary they pass is finished.
  void consume(const Window& w) {
    for (uint16_t k = 0; k < w.count; ++k) {
      const SpanEntry& e = w.entries[k];
      while (e.tick >= (currentBar_ + 1) * bar_) finishBar();
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
      if (n.end != kOpenEnd && n.end <= (currentBar_ + 1) * bar_) {
        draw(n, n.start - currentBar_ * bar_, n.end - n.start, false, false);  // inside this bar: drawn now
        continue;
      }
      if (openCount_ == kMaxOpen) {  // more bar-crossing notes than the composer can hold open: named, not silent
        ++overflow_;
        continue;
      }
      open_[openCount_++] = n;
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
    if (cycle_ == 0 && bar < deferred_) return;  // reached by notes held across the loop end: published from cycle 1
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
  bool bindStarted_ = false;  // a stepped bind under way (this job's)
  Store* store_ = nullptr;
  SourceLease<Composer, Manifests, Runs> source_;
  uint8_t slot_ = 0;
  bool composing_ = false, dense_ = false;
  uint32_t identity_ = 0, window_ = 0, bar_ = 0, loop_ = 0, cycle_ = 0, currentBar_ = 0, bars_ = 0, overflow_ = 0,
           deferred_ = 0, windows_ = 0;
  uint16_t segCount_ = 0, spare_ = 0xFFFF;
  uint8_t openCount_ = 0;
  Open open_[kMaxOpen];
  DensityTile density_;
  Status status_ = Status::Idle;
};

/// The live overlay (plan § 9.3): what the writer is recording, drawn over the piano roll until the committed tiles
/// show it. A list of the take's notes in loop positions (start, end, pitch, brightness from velocity), so the roll
/// draws them at whatever scale its view has — owner 2026-11-06: as legacy, a first take's view grows with the current
/// tick, so a fixed-scale raster cannot follow it. A held note's end is the current position (`advance`) until its
/// NoteOff. A note crossing the loop end (`setLoop`; 0: no loop yet, a first take) wraps. More than `MaxNotes` notes:
/// the rest are not shown until the tiles show them (`dropped`). `extent` names the pitches shown, for the auto-fit.
/// Pickups (E9.8j, a Record take's notes begun before its tick 0; `MaxPickups`): drawn `before` ticks before the
/// view's end (the loop's, or the take's length so far), ending where they were released — wrapped to the take's
/// start when released after tick 0; a held one fills the view (at Stop it becomes almost a loop long). Notes
/// `count()` and `note(i)` list both. No allocation; O(1) a NoteOn / advance, O(`MaxPickups`) a NoteOff.
template <uint16_t MaxNotes = 768, uint8_t MaxPickups = 16>
class LiveOverlay {
 public:
  static constexpr uint32_t kOpen = 0xFFFFFFFFu;
  struct Note {
    uint32_t start, end;  // loop positions; end kOpen while held
    uint8_t pitch, value;
  };

  void init() {
    loop_ = 0;
    clear();
  }
  /// The loop length positions wrap at (0: none yet). Does not clear.
  void setLoop(uint32_t loopLength) { loop_ = loopLength; }
  uint32_t loopLength() const { return loop_; }
  void clear() {
    count_ = 0;
    pickupCount_ = 0;
    now_ = 0;
    dropped_ = 0;
    for (auto& o : open_) o = kNone;
    lo_ = 0xFF;
    hi_ = 0;
  }

  void noteOn(uint32_t pos, uint8_t pitch, uint8_t velocity) {
    pitch &= 0x7F;
    noteOff(pos, pitch);  // a retrigger ends the held one
    if (count_ == MaxNotes) {
      ++dropped_;
      return;
    }
    notes_[count_] = Note{wrap(pos), kOpen, pitch, static_cast<uint8_t>(velocity >> 3 == 0 ? 1 : velocity >> 3)};
    open_[pitch] = count_++;
    if (pitch < lo_) lo_ = pitch;
    if (pitch > hi_) hi_ = pitch;
  }
  /// A pickup: begun `before` ticks before tick 0; `off` where it ended relative to tick 0 (negative: before it), or
  /// held (`open`) — its NoteOff then ends it (`noteOff`).
  void pickup(uint32_t before, int32_t off, bool open, uint8_t pitch, uint8_t velocity) {
    if (pickupCount_ == MaxPickups || before == 0) return;
    pitch &= 0x7F;
    pickups_[pickupCount_++] = Pickup{before, off, pitch, static_cast<uint8_t>(velocity >> 3 == 0 ? 1 : velocity >> 3), open};
    if (pitch < lo_) lo_ = pitch;
    if (pitch > hi_) hi_ = pitch;
  }
  /// The current position: held notes reach it.
  void advance(uint32_t pos) { now_ = wrap(pos); }
  /// Ends the note held on `pitch`, else a held pickup on it.
  void noteOff(uint32_t pos, uint8_t pitch) {
    pitch &= 0x7F;
    if (open_[pitch] == kNone) {
      for (uint8_t i = 0; i < pickupCount_; ++i)
        if (pickups_[i].open && pickups_[i].pitch == pitch) {
          pickups_[i].open = false;
          pickups_[i].off = static_cast<int32_t>(wrap(pos));
          return;
        }
      return;
    }
    notes_[open_[pitch]].end = wrap(pos);
    open_[pitch] = kNone;
  }

  uint16_t count() const { return static_cast<uint16_t>(count_ + pickupCount_); }
  /// Note `i` with a held note's end at the current position. A pickup's start is `before` ticks before
  /// `wrapLength()`; its end before its start means it wraps.
  Note note(uint16_t i) const {
    if (i >= count_) {
      const Pickup& p = pickups_[i - count_];
      const uint32_t w = wrapLength();
      const uint32_t start = w > p.before ? w - p.before : 0;
      uint32_t end;
      if (p.open)
        end = start == 0 ? w : start - 1;  // the whole view
      else if (p.off < 0)
        end = start + static_cast<uint32_t>(p.off + static_cast<int32_t>(p.before));  // ended before tick 0
      else
        end = static_cast<uint32_t>(p.off);  // wrapped to the take's start
      return Note{start, end, p.pitch, p.value};
    }
    Note n = notes_[i];
    if (n.end == kOpen) n.end = now_;
    return n;
  }
  /// Where wrapped notes wrap: the loop, or a first take's length so far.
  uint32_t wrapLength() const { return loop_ != 0 ? loop_ : now_; }
  uint32_t dropped() const { return dropped_; }
  /// The lowest and highest pitch shown. False: none.
  bool extent(uint8_t& lo, uint8_t& hi) const {
    if (lo_ > hi_) return false;
    lo = lo_;
    hi = hi_;
    return true;
  }

 private:
  static constexpr uint16_t kNone = 0xFFFF;
  uint32_t wrap(uint32_t pos) const { return loop_ == 0 ? pos : pos % loop_; }

  struct Pickup {
    uint32_t before;
    int32_t off;
    uint8_t pitch, value;
    bool open;
  };
  Note notes_[MaxNotes];
  Pickup pickups_[MaxPickups];
  uint16_t open_[128];
  uint16_t count_ = 0;
  uint8_t pickupCount_ = 0;
  uint32_t loop_ = 0, now_ = 0, dropped_ = 0;
  uint8_t lo_ = 0xFF, hi_ = 0;
};

/// Tile sweeps as scheduler jobs (E7.2). `Workers` sweep workers (the display compose cursors), each with its own
/// composer and scratch window. A slot's newest request replaces its older one (and supersedes a running sweep for the
/// slot). The focused slot's request is started first, then the others in request order. Each running sweep is a
/// display-class job, so it gets at least one grain in four while display work is pending.
template <typename Composer, typename Window, typename Manifests, typename Runs, typename Store, typename Scheduler,
          uint8_t Workers = 2, uint8_t Slots = 64>
class DisplayJobs {
 public:
  using Sweep = TileSweep<Composer, Window, Manifests, Runs, Store>;
  static constexpr uint32_t kRowsPerGrain = 128;
  static constexpr uint32_t kDeclaredGrains = 4000;  // one loop and the wrap-reach bars of a W-sized slot

  void init(Composer* const* composers, Window* const* scratch, Store& store, Manifests& manifests, Runs& runs,
            Scheduler& scheduler, uint32_t windowTicks, uint32_t barTicks) {
    for (uint8_t i = 0; i < Workers; ++i) worker_[i] = Worker{composers[i], scratch[i], this, {}, false, 0xFF, kNoManifest};
    store_ = &store;
    manifests_ = &manifests;
    runs_ = &runs;
    scheduler_ = &scheduler;
    window_ = windowTicks;
    bar_ = barTicks;
    for (auto& r : pending_) r = kNoManifest;
    for (auto& p : pinned_) p = false;
    order_ = 0;
    for (auto& o : requestOrder_) o = 0;
    focus_ = 0xFF;
  }

  /// The slot's tiles should show `manifest` (a commit, Undo / Redo, an edit, a load). Replaces the slot's older request.
  void request(uint8_t slot, ManifestId manifest) {
    dropPin(slot);
    pending_[slot] = manifest;
    requestOrder_[slot] = ++order_;
    store_->setWanted(slot, manifests_->manifest(manifest).identity);
  }
  /// As `request`, holding `manifest` with a pin of its own until its sweep takes one (or the request is replaced or
  /// cancelled): for a manifest its owner may release meanwhile (plan §6.2 fix 2, a get-a-loop preview).
  void requestPinned(uint8_t slot, ManifestId manifest) {
    manifests_->pin(manifest);
    request(slot, manifest);
    pinned_[slot] = true;
  }
  /// The slot's tiles are wanted no more: its request (and pin) dropped, its tiles cleared — a running sweep of it
  /// stops at its next grain (`Superseded`), releasing its own pin.
  void cancel(uint8_t slot) {
    dropPin(slot);
    pending_[slot] = kNoManifest;
    store_->clearSlot(slot);
  }
  void setFocus(uint8_t slot) { focus_ = slot; }

  /// Starts sweeps on idle workers (focus first) and submits their jobs.
  void service() {
    for (auto& w : worker_) {
      if (w.live) continue;
      const uint8_t slot = next();
      if (slot == 0xFF) return;
      const ManifestId m = pending_[slot];
      pending_[slot] = kNoManifest;
      if (w.sweep.start(*w.composer, *store_, *manifests_, *runs_, slot, m, window_, bar_) != Sweep::Status::Running) {
        unpinTaken(slot, m);
        continue;
      }
      uint8_t id;
      w.slot = slot;
      w.manifest = m;
      w.live = scheduler_->submit(JobSpec{JobClass::Display, 0, kDeclaredGrains, &step, &w}, id);
      if (!w.live) w.sweep.cancel();  // table full: the slot's request is retried (its pin kept)
      if (!w.live) pending_[slot] = m;
      if (w.live) unpinTaken(slot, m);  // the sweep holds its own pin now
    }
  }

  /// E9.6: the display cursor is wanted elsewhere (a NOTE_EDIT gather): running sweeps stop and their slots are asked
  /// again; `service` starts nothing until the caller resumes calling it.
  void yield() {
    for (auto& w : worker_) {
      if (!w.live || w.sweep.status() != Sweep::Status::Running) continue;
      const ManifestId m = w.manifest;
      if (pending_[w.slot] == kNoManifest) {  // asked again: held by a pin of the request's (the sweep lets go)
        manifests_->pin(m);
        pending_[w.slot] = m;
        pinned_[w.slot] = true;
        requestOrder_[w.slot] = ++order_;
      }
      w.sweep.cancel();
    }
  }
  bool sweeping() const {
    for (const auto& w : worker_)
      if (w.live) return true;
    return false;
  }

  bool idle() const {
    for (const auto& w : worker_)
      if (w.live) return false;
    for (const auto r : pending_)
      if (r != kNoManifest) return false;
    return true;
  }

 private:
  struct Worker {
    Composer* composer;
    Window* scratch;
    DisplayJobs* owner;
    Sweep sweep;
    bool live;
    uint8_t slot;
    ManifestId manifest;  // the source of the running sweep (asked again when it yields)
  };

  /// The focused slot's request, else the oldest.
  uint8_t next() const {
    if (focus_ != 0xFF && pending_[focus_] != kNoManifest) return focus_;
    uint8_t best = 0xFF;
    for (uint8_t s = 0; s < Slots; ++s)
      if (pending_[s] != kNoManifest && (best == 0xFF || requestOrder_[s] < requestOrder_[best])) best = s;
    return best;
  }

  static JobStep step(void* ctx) {
    Worker& w = *static_cast<Worker*>(ctx);
    if (w.sweep.step(*w.scratch, kRowsPerGrain) == Sweep::Status::Running) return JobStep::Continue;
    w.live = false;
    return JobStep::Done;
  }

  Worker worker_[Workers];
  Store* store_ = nullptr;
  Manifests* manifests_ = nullptr;
  Runs* runs_ = nullptr;
  Scheduler* scheduler_ = nullptr;
  uint32_t window_ = 0, bar_ = 0, order_ = 0;
  /// The request for `slot`, `m`, was taken off `pending_`: its pin (if it had one) goes.
  void unpinTaken(uint8_t slot, ManifestId m) {
    if (pinned_[slot]) manifests_->unpin(m, *runs_);
    pinned_[slot] = false;
  }
  void dropPin(uint8_t slot) {
    if (pinned_[slot] && pending_[slot] != kNoManifest) manifests_->unpin(pending_[slot], *runs_);
    pinned_[slot] = false;
  }

  ManifestId pending_[Slots];
  bool pinned_[Slots];  // the pending request holds a pin of its own (`requestPinned`)
  uint32_t requestOrder_[Slots];
  uint8_t focus_ = 0xFF;
};

}  // namespace ContentEngine
