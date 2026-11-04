//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// Shared, revision-keyed edit-hide index (fixed-memory-engine E3.3).
//
// A manifest's edit runs (NOTE_EDIT commits) hide noteIds in earlier passes. Composing that manifest needs, per NoteOn,
// "does a later edit run hide this noteId?". The answer depends only on the manifest identity, so one sorted index per
// identity is shared by every composer composing it:
//   * fixed store of `Slots` slots of `Capacity` entries (noteId -> latest hiding pass); residency is bounded by Slots,
//     never one index per historical manifest;
//   * a composer pins the slot of the identity it composes (`acquire` / `release`); a manifest without edit runs needs
//     no slot (`NotNeeded`);
//   * a new identity takes a free slot, else the least recently used unpinned slot; pinned slots are never evicted;
//   * named outcomes: `Busy` (every slot pinned by other identities), `CapacityExceeded` (more hides than a slot holds);
//     neither takes a slot;
//   * the identity is monotonic and never reused (DEC-246), so a slot can never answer for another revision; an evicted
//     identity rebuilds the same index from its runs (deterministic: heapsort + collapse to the latest hiding pass).
// Production sizing: 18 slots (one per compose cursor: 16 playback + 2 display; each pins at most one) x 4,096 hides,
// the edit-index reserve of the plan. No allocation; build is O(H log H) on `acquire`.

#pragma once

#include <cstdint>

#include "ContentEngine/EngineConfig.h"
#include "ContentEngine/PassEvent.h"
#include "ContentEngine/SourceRun.h"

namespace ContentEngine {

enum class IndexResult : uint8_t { Ready, NotNeeded, Busy, CapacityExceeded };

struct IndexLease {
  static constexpr uint8_t kNoSlot = 0xFF;
  uint8_t slot = kNoSlot;
  uint32_t identity = 0;
};

template <typename Pool, typename Runs, uint8_t Slots = 18, uint16_t Capacity = 4096>
class EditIndexStore {
 public:
  static constexpr uint8_t kSlots = Slots;
  static constexpr uint16_t kCapacity = Capacity;

  void init(const Pool& pool, const Runs& runs) {
    pool_ = &pool;
    runs_ = &runs;
    clock_ = 0;
    for (auto& s : slots_) {
      s.identity = 0;
      s.pins = 0;
      s.count = 0;
      s.lastUse = 0;
      s.valid = false;
    }
  }

  IndexResult acquire(uint32_t identity, const SourceRun* src, uint8_t count, IndexLease& lease) {
    lease = IndexLease{};
    bool anyEdit = false;
    uint32_t hides = 0;
    for (uint8_t r = 0; r < count; ++r) {
      if (runs_->run(src[r].run).kind != RunKind::kEdit) continue;
      anyEdit = true;
      for (uint32_t i = 0; i < src[r].rowLimit && row(src[r], i).status == kHideNoteStatus; ++i) ++hides;
    }
    if (!anyEdit) return IndexResult::NotNeeded;
    for (uint8_t i = 0; i < Slots; ++i)
      if (slots_[i].valid && slots_[i].identity == identity) {
        ++slots_[i].pins;
        slots_[i].lastUse = ++clock_;
        lease = IndexLease{i, identity};
        return IndexResult::Ready;
      }
    if (hides > Capacity) return IndexResult::CapacityExceeded;
    int victim = -1;
    for (uint8_t i = 0; i < Slots && victim < 0; ++i)
      if (!slots_[i].valid) victim = i;
    if (victim < 0)
      for (uint8_t i = 0; i < Slots; ++i)
        if (slots_[i].pins == 0 && (victim < 0 || slots_[i].lastUse < slots_[victim].lastUse)) victim = i;
    if (victim < 0) return IndexResult::Busy;
    build(slots_[victim], identity, src, count);
    slots_[victim].pins = 1;
    slots_[victim].lastUse = ++clock_;
    lease = IndexLease{static_cast<uint8_t>(victim), identity};
    return IndexResult::Ready;
  }

  void release(IndexLease& lease) {
    if (lease.slot < Slots && slots_[lease.slot].valid && slots_[lease.slot].identity == lease.identity &&
        slots_[lease.slot].pins > 0)
      --slots_[lease.slot].pins;
    lease = IndexLease{};
  }

  /// A later edit run (pass order > `order`) of the leased identity hides `noteId`. O(log H).
  bool hidden(const IndexLease& lease, uint32_t noteId, uint8_t order) const {
    if (lease.slot >= Slots) return false;
    const Slot& s = slots_[lease.slot];
    uint16_t lo = 0, hi = s.count;
    while (lo < hi) {
      const uint16_t mid = static_cast<uint16_t>(lo + (hi - lo) / 2);
      if (s.entries[mid].noteId < noteId)
        lo = static_cast<uint16_t>(mid + 1);
      else
        hi = mid;
    }
    return lo < s.count && s.entries[lo].noteId == noteId && s.entries[lo].order > order;
  }

  uint16_t pins(uint8_t slot) const { return slot < Slots ? slots_[slot].pins : 0; }
  bool holds(uint32_t identity) const {
    for (const auto& s : slots_)
      if (s.valid && s.identity == identity) return true;
    return false;
  }
  uint8_t slotsInUse() const {
    uint8_t n = 0;
    for (const auto& s : slots_) n += s.valid ? 1 : 0;
    return n;
  }

 private:
  struct Entry {
    uint32_t noteId;
    uint8_t order;  // latest hiding pass
  };
  struct Slot {
    uint32_t identity;
    uint32_t lastUse;
    uint16_t pins;
    uint16_t count;
    bool valid;
    Entry entries[Capacity];
  };

  const PassEvent& row(const SourceRun& s, uint32_t i) const { return runs_->rowAt(s.run, i, *pool_); }

  void build(Slot& s, uint32_t identity, const SourceRun* src, uint8_t count) {
    s.identity = identity;
    s.valid = true;
    s.count = 0;
    for (uint8_t r = 0; r < count; ++r) {
      if (runs_->run(src[r].run).kind != RunKind::kEdit) continue;
      for (uint32_t i = 0; i < src[r].rowLimit && row(src[r], i).status == kHideNoteStatus; ++i)
        s.entries[s.count++] = Entry{row(src[r], i).noteId, r};
    }
    // Heapsort by noteId, then one entry per noteId with its latest hiding pass.
    auto less = [&s](uint16_t a, uint16_t b) { return s.entries[a].noteId < s.entries[b].noteId; };
    auto swap = [&s](uint16_t a, uint16_t b) {
      const Entry t = s.entries[a];
      s.entries[a] = s.entries[b];
      s.entries[b] = t;
    };
    auto sift = [&](uint16_t root, uint16_t end) {
      while (static_cast<uint32_t>(root) * 2 + 1 < end) {
        uint16_t child = static_cast<uint16_t>(root * 2 + 1);
        if (child + 1 < end && less(child, static_cast<uint16_t>(child + 1))) ++child;
        if (!less(root, child)) return;
        swap(root, child);
        root = child;
      }
    };
    for (int32_t i = s.count / 2 - 1; i >= 0; --i) sift(static_cast<uint16_t>(i), s.count);
    for (uint16_t end = s.count; end > 1; --end) {
      swap(0, static_cast<uint16_t>(end - 1));
      sift(0, static_cast<uint16_t>(end - 1));
    }
    uint16_t w = 0;
    for (uint16_t i = 0; i < s.count; ++i) {
      if (w > 0 && s.entries[w - 1].noteId == s.entries[i].noteId) {
        if (s.entries[i].order > s.entries[w - 1].order) s.entries[w - 1].order = s.entries[i].order;
      } else {
        s.entries[w++] = s.entries[i];
      }
    }
    s.count = w;
  }

  const Pool* pool_;
  const Runs* runs_;
  uint32_t clock_;
  Slot slots_[Slots];
};

}  // namespace ContentEngine
