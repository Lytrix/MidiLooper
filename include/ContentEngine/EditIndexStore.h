//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// Shared, revision-keyed edit-hide index (fixed-memory-engine E3.3, placed and sliced in E5.3).
//
// A manifest's edit runs (NOTE_EDIT commits) hide noteIds in earlier passes. Composing that manifest needs, per NoteOn,
// "does a later edit run hide this noteId?". The answer depends only on the manifest identity, so one sorted index per
// identity is shared by every composer composing it:
//   * fixed store of `Slots` slots of `Capacity` entries (noteId -> latest hiding pass); residency is bounded by Slots,
//     never one index per historical manifest;
//   * a composer pins the slot of the identity it composes (`acquire` / `release`); a manifest without edit runs needs
//     no slot (`NotNeeded`);
//   * a new identity takes a free slot, else the least recently used unpinned slot; pinned slots are never evicted;
//   * named outcomes: `Busy` (every slot pinned by other identities), `CapacityExceeded` (more hides than a slot holds,
//     found while gathering), `Building` (another job is still building that identity); none of them keeps a slot;
//   * the identity is monotonic and never reused (DEC-246), so a slot can never answer for another revision; an evicted
//     identity rebuilds the same index from its runs (deterministic: heapsort + collapse to the latest hiding pass).
//
// Placement (E5.3): the entries live in the page pool's edit-index partition. `init` takes Slots x kPagesPerSlot pages
// from the pool once, tags them `kOwnerIndex` and never returns or adds to them; content, writers and Stop never see
// them (the ledger's fixed reserve then counts only the compaction partition). The store object holds headers and the
// page lists only.
//
// Grains (E5.3): a worker job builds with `reserve` (takes and pins the slot, state Building) and `buildStep` (at most
// `maxWork` units: a gathered row, a sift-down, an extraction or a collapsed entry). `acquire` still builds to
// completion when nothing prepared the identity (tests, tools); `syncBuilds()` counts those, so a production path can
// prove it never builds inside a composer's `begin`.
// Production sizing: 18 slots (one per compose cursor: 16 playback + 2 display; each pins at most one) x 4,096 hides
// = 576 pages, the plan's edit-index partition.

#pragma once

#include <cstdint>

#include "ContentEngine/EngineConfig.h"
#include "ContentEngine/PagePool.h"
#include "ContentEngine/PassEvent.h"
#include "ContentEngine/SourceRun.h"

namespace ContentEngine {

enum class IndexResult : uint8_t { Ready, NotNeeded, Busy, CapacityExceeded, Building };

struct IndexLease {
  static constexpr uint8_t kNoSlot = 0xFF;
  uint8_t slot = kNoSlot;
  uint32_t identity = 0;
};

template <typename Pool, typename Runs, uint8_t Slots = 18, uint16_t Capacity = 4096>
class EditIndexStore {
 public:
  struct Entry {
    uint32_t noteId;
    uint8_t order;  // latest hiding pass
    uint8_t pad[3];
  };
  static constexpr uint8_t kSlots = Slots;
  static constexpr uint16_t kCapacity = Capacity;
  static constexpr uint32_t kEntriesPerPage = Pool::kPageBytes / sizeof(Entry);
  static constexpr uint16_t kPagesPerSlot = static_cast<uint16_t>((Capacity + kEntriesPerPage - 1) / kEntriesPerPage);
  static constexpr uint32_t kPages = static_cast<uint32_t>(Slots) * kPagesPerSlot;

  /// Takes the index partition from `pool` (call right after `pool.init()`). False when the pool cannot supply it.
  bool init(Pool& pool, const Runs& runs) {
    pool_ = &pool;
    runs_ = &runs;
    clock_ = 0;
    syncBuilds_ = 0;
    placed_ = pool.freeCount() >= kPages;
    for (auto& s : slots_) {
      s.identity = 0;
      s.pins = 0;
      s.count = 0;
      s.lastUse = 0;
      s.state = SlotState::Empty;
      for (auto& p : s.pages) p = placed_ ? pool.acquire(kOwnerIndex) : kNoPage;
    }
    return placed_;
  }

  /// Pins the identity's index, building it to completion when no job prepared it (counted in `syncBuilds()`).
  IndexResult acquire(uint32_t identity, const SourceRun* src, uint8_t count, IndexLease& lease) {
    const IndexResult r = reserve(identity, src, count, lease);
    if (r != IndexResult::Building || lease.slot == IndexLease::kNoSlot) return r;
    ++syncBuilds_;
    IndexResult b;
    while ((b = buildStep(lease, src, count, 0xFFFFFFFFu)) == IndexResult::Building) {
    }
    if (b != IndexResult::Ready) release(lease);
    return b;
  }

  /// Pins the identity's index if it is built (Ready). Otherwise takes a slot for it and pins it in state Building
  /// (`lease` set; build it with `buildStep`). Building with no lease: another job is building that identity.
  /// O(runs + Slots): no row is read.
  IndexResult reserve(uint32_t identity, const SourceRun* src, uint8_t count, IndexLease& lease) {
    lease = IndexLease{};
    bool anyEdit = false;
    for (uint8_t r = 0; r < count && !anyEdit; ++r) anyEdit = runs_->run(src[r].run).kind == RunKind::kEdit;
    if (!anyEdit) return IndexResult::NotNeeded;
    if (!placed_) return IndexResult::Busy;  // `init` could not take the partition
    for (uint8_t i = 0; i < Slots; ++i)
      if (slots_[i].state != SlotState::Empty && slots_[i].identity == identity) {
        if (slots_[i].state == SlotState::Building) return IndexResult::Building;
        ++slots_[i].pins;
        slots_[i].lastUse = ++clock_;
        lease = IndexLease{i, identity};
        return IndexResult::Ready;
      }
    int victim = -1;
    for (uint8_t i = 0; i < Slots && victim < 0; ++i)
      if (slots_[i].state == SlotState::Empty) victim = i;
    if (victim < 0)
      for (uint8_t i = 0; i < Slots; ++i)
        if (slots_[i].pins == 0 && (victim < 0 || slots_[i].lastUse < slots_[victim].lastUse)) victim = i;
    if (victim < 0) return IndexResult::Busy;
    Slot& s = slots_[victim];
    s.identity = identity;
    s.state = SlotState::Building;
    s.count = 0;
    s.pins = 1;
    s.lastUse = ++clock_;
    s.build = Build{};
    lease = IndexLease{static_cast<uint8_t>(victim), identity};
    return IndexResult::Building;
  }

  /// One grain of the build of the leased slot (`src` must be the source it was reserved for): `Building`, `Ready`, or
  /// `CapacityExceeded` (more hides than a slot holds; the slot is emptied and the caller releases its lease).
  IndexResult buildStep(const IndexLease& lease, const SourceRun* src, uint8_t count, uint32_t maxWork) {
    if (lease.slot >= Slots) return IndexResult::NotNeeded;
    Slot& s = slots_[lease.slot];
    if (s.identity != lease.identity || s.state == SlotState::Empty) return IndexResult::CapacityExceeded;
    if (s.state == SlotState::Ready) return IndexResult::Ready;
    Build& b = s.build;
    for (uint32_t work = 0; work < maxWork; ++work) {
      switch (b.phase) {
        case Phase::Gather: {
          // Hide rows lead an edit run.
          while (b.run < count && (runs_->run(src[b.run].run).kind != RunKind::kEdit || b.row >= src[b.run].rowLimit ||
                                   row(src[b.run], b.row).status != kHideNoteStatus)) {
            ++b.run;
            b.row = 0;
          }
          if (b.run >= count) {
            b.phase = Phase::Heapify;
            b.i = static_cast<int32_t>(s.count / 2) - 1;
            break;
          }
          if (s.count >= Capacity) {
            s.state = SlotState::Empty;
            s.pins = 0;
            return IndexResult::CapacityExceeded;
          }
          entry(s, s.count++) = Entry{row(src[b.run], b.row).noteId, b.run, {0, 0, 0}};
          ++b.row;
          break;
        }
        case Phase::Heapify:
          if (b.i < 0) {
            b.phase = Phase::Sort;
            b.end = s.count;
            break;
          }
          sift(s, static_cast<uint16_t>(b.i--), s.count);
          break;
        case Phase::Sort:
          if (b.end <= 1) {
            b.phase = Phase::Collapse;
            b.i = 0;
            b.w = 0;
            break;
          }
          swap(s, 0, static_cast<uint16_t>(b.end - 1));
          sift(s, 0, static_cast<uint16_t>(--b.end));
          break;
        case Phase::Collapse: {
          if (b.i >= static_cast<int32_t>(s.count)) {
            s.count = b.w;
            s.state = SlotState::Ready;
            return IndexResult::Ready;
          }
          const Entry e = entry(s, static_cast<uint16_t>(b.i++));
          if (b.w > 0 && entry(s, static_cast<uint16_t>(b.w - 1)).noteId == e.noteId) {
            Entry& last = entry(s, static_cast<uint16_t>(b.w - 1));
            if (e.order > last.order) last.order = e.order;
          } else {
            entry(s, b.w++) = e;
          }
          break;
        }
      }
    }
    return IndexResult::Building;
  }

  /// Unpins. A slot whose build was abandoned (no pins left while Building) is emptied.
  void release(IndexLease& lease) {
    if (lease.slot < Slots) {
      Slot& s = slots_[lease.slot];
      if (s.state != SlotState::Empty && s.identity == lease.identity && s.pins > 0 && --s.pins == 0 &&
          s.state == SlotState::Building)
        s.state = SlotState::Empty;
    }
    lease = IndexLease{};
  }

  /// A later edit run (pass order > `order`) of the leased identity hides `noteId`. O(log H).
  bool hidden(const IndexLease& lease, uint32_t noteId, uint8_t order) const {
    if (lease.slot >= Slots) return false;
    const Slot& s = slots_[lease.slot];
    if (s.state != SlotState::Ready) return false;
    uint16_t lo = 0, hi = s.count;
    while (lo < hi) {
      const uint16_t mid = static_cast<uint16_t>(lo + (hi - lo) / 2);
      if (entry(s, mid).noteId < noteId)
        lo = static_cast<uint16_t>(mid + 1);
      else
        hi = mid;
    }
    return lo < s.count && entry(s, lo).noteId == noteId && entry(s, lo).order > order;
  }

  uint16_t pins(uint8_t slot) const { return slot < Slots ? slots_[slot].pins : 0; }
  bool holds(uint32_t identity) const {
    for (const auto& s : slots_)
      if (s.state == SlotState::Ready && s.identity == identity) return true;
    return false;
  }
  uint8_t slotsInUse() const {
    uint8_t n = 0;
    for (const auto& s : slots_) n += s.state != SlotState::Empty ? 1 : 0;
    return n;
  }
  uint32_t syncBuilds() const { return syncBuilds_; }
  PageId page(uint8_t slot, uint16_t k) const { return slots_[slot].pages[k]; }

 private:
  enum class SlotState : uint8_t { Empty, Building, Ready };
  enum class Phase : uint8_t { Gather, Heapify, Sort, Collapse };
  struct Build {
    Phase phase = Phase::Gather;
    uint8_t run = 0;
    uint32_t row = 0;
    int32_t i = 0;
    uint16_t end = 0, w = 0;
  };
  struct Slot {
    uint32_t identity;
    uint32_t lastUse;
    uint16_t pins;
    uint16_t count;
    SlotState state;
    Build build;
    PageId pages[kPagesPerSlot];
  };

  Entry& entry(Slot& s, uint16_t i) {
    return reinterpret_cast<Entry*>(pool_->rows(s.pages[i / kEntriesPerPage]))[i % kEntriesPerPage];
  }
  const Entry& entry(const Slot& s, uint16_t i) const {
    return reinterpret_cast<const Entry*>(static_cast<const Pool*>(pool_)->rows(s.pages[i / kEntriesPerPage]))[i % kEntriesPerPage];
  }
  void swap(Slot& s, uint16_t a, uint16_t b) {
    const Entry t = entry(s, a);
    entry(s, a) = entry(s, b);
    entry(s, b) = t;
  }
  void sift(Slot& s, uint16_t root, uint16_t end) {
    while (static_cast<uint32_t>(root) * 2 + 1 < end) {
      uint16_t child = static_cast<uint16_t>(root * 2 + 1);
      if (child + 1 < end && entry(s, child).noteId < entry(s, static_cast<uint16_t>(child + 1)).noteId) ++child;
      if (!(entry(s, root).noteId < entry(s, child).noteId)) return;
      swap(s, root, child);
      root = child;
    }
  }

  const PassEvent& row(const SourceRun& s, uint32_t i) const { return runs_->rowAt(s.run, i, *pool_); }

  Pool* pool_;
  const Runs* runs_;
  uint32_t clock_;
  uint32_t syncBuilds_;
  bool placed_;
  Slot slots_[Slots];
};

}  // namespace ContentEngine
