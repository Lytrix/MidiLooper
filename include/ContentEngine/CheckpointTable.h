//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// Restart-checkpoint ownership (fixed-memory-engine E5, plan revision 3 § 7.2, § 5.4): one fixed table per slot of
// `PerSlot` checkpoints at a fixed spacing (8 bars), stamped with the manifest identity swept. The sweep worker fills it
// as a by-product of composing the loop; a mid-loop cold start (undo, redo, edit commit, slot switch) resumes from the
// nearest usable checkpoint at or before its position and replays at most the windows of one spacing.
//
// Steady state: playback from cycle 1 on carries notes held across the loop end, cycle 0 of a cold composition does
// not. A cold start wants the steady state, so the sweep composes cycle 0 and captures provisional checkpoints, takes
// the position-0 checkpoint at its end, then continues into cycle 1 until a capture equals the provisional one at the
// same position. A checkpoint is the composer's whole state (resume equivalence, `test_content_engine_checkpoint`), so
// from that position on the cycle-0 captures are steady too and are promoted without composing further. Cost: one loop
// plus the windows until the carried notes have ended (<= one more loop). Only usable checkpoints are ever resumed.
//
// Retirement: a new sweep for a slot replaces its table. The table holds no page references, only an identity stamp;
// manifest identities are never reused, so a stale checkpoint can never match another manifest and needs no pin. A
// sweep whose identity is no longer the slot's ends `Superseded` and writes nothing.

#pragma once

#include <cstdint>

#include "ContentEngine/SourceRun.h"

namespace ContentEngine {

template <typename Checkpoint, uint8_t Slots = 64, uint8_t PerSlot = 8>
class CheckpointTable {
 public:
  static constexpr uint8_t kSlots = Slots;
  static constexpr uint8_t kPerSlot = PerSlot;

  enum class EntryState : uint8_t { Empty, Provisional, Usable, Unusable };

  void init() {
    for (auto& s : slots_) {
      s.identity = 0;
      s.loopLength = 0;
      s.spacing = 0;
      for (auto& st : s.state) st = EntryState::Empty;
    }
  }

  /// Starts a sweep for `identity` on `slot`: the previous table is retired at once.
  void beginSweep(uint8_t slot, uint32_t identity, uint32_t loopLength, uint32_t spacing) {
    Slot& s = slots_[slot];
    s.identity = identity;
    s.loopLength = loopLength;
    s.spacing = spacing;
    for (auto& st : s.state) st = EntryState::Empty;
  }
  void retire(uint8_t slot) { beginSweep(slot, 0, 0, 0); }

  bool current(uint8_t slot, uint32_t identity) const { return identity != 0 && slots_[slot].identity == identity; }
  uint32_t spacing(uint8_t slot) const { return slots_[slot].spacing; }

  /// Index of the checkpoint at `position`, or PerSlot when it is not a checkpoint position of this slot.
  uint8_t indexOf(uint8_t slot, uint32_t position) const {
    const Slot& s = slots_[slot];
    if (s.spacing == 0 || position % s.spacing != 0 || position >= s.loopLength) return PerSlot;
    const uint32_t k = position / s.spacing;
    return k < PerSlot ? static_cast<uint8_t>(k) : PerSlot;
  }

  /// Stores `cp` (state `st`) at its position. False when the sweep is superseded or the position is not a checkpoint.
  bool store(uint8_t slot, uint32_t identity, const Checkpoint& cp, EntryState st) {
    if (!current(slot, identity)) return false;
    const uint8_t k = indexOf(slot, cp.position);
    if (k == PerSlot) return false;
    slots_[slot].entries[k] = cp;
    slots_[slot].state[k] = cp.valid ? st : EntryState::Unusable;
    return true;
  }
  void setState(uint8_t slot, uint8_t k, EntryState st) { slots_[slot].state[k] = st; }
  EntryState state(uint8_t slot, uint8_t k) const { return slots_[slot].state[k]; }
  const Checkpoint& entry(uint8_t slot, uint8_t k) const { return slots_[slot].entries[k]; }

  /// The usable checkpoint of `identity` nearest at or before `position`; nullptr when none (replay from loop start).
  const Checkpoint* nearest(uint8_t slot, uint32_t identity, uint32_t position) const {
    const Slot& s = slots_[slot];
    if (!current(slot, identity) || s.spacing == 0) return nullptr;
    uint32_t k = position / s.spacing;
    if (k >= PerSlot) k = PerSlot - 1;
    for (uint32_t i = k + 1; i-- > 0;)
      if (s.state[i] == EntryState::Usable) return &s.entries[i];
    return nullptr;
  }

 private:
  struct Slot {
    uint32_t identity, loopLength, spacing;
    EntryState state[PerSlot];
    Checkpoint entries[PerSlot];
  };
  Slot slots_[Slots];
};

/// Same composer state, ignoring the cycle stamp (steady state is periodic from cycle 1).
template <typename Checkpoint>
bool sameState(const Checkpoint& a, const Checkpoint& b) {
  if (!a.valid || !b.valid || a.identity != b.identity || a.loopLength != b.loopLength || a.position != b.position ||
      a.runCount != b.runCount || a.activeCount != b.activeCount || a.admittedCount != b.admittedCount)
    return false;
  for (uint8_t i = 0; i < a.activeCount; ++i)
    if (a.active[i].pitch != b.active[i].pitch || a.active[i].run != b.active[i].run ||
        a.active[i].count != b.active[i].count)
      return false;
  for (uint8_t i = 0; i < a.admittedCount; ++i)
    if (a.admitted[i].noteId != b.admitted[i].noteId || a.admitted[i].runKey != b.admitted[i].runKey ||
        a.admitted[i].pitch != b.admitted[i].pitch || a.admitted[i].channel != b.admitted[i].channel)
      return false;
  return true;
}

/// The sweep worker's checkpoint job for one manifest. `step` composes one window; the job is stamped with the
/// identity it sweeps and ends `Superseded` once the slot's table belongs to another sweep.
template <typename Composer, typename Table, typename Window>
class CheckpointSweep {
 public:
  enum class Status : uint8_t { Running, Done, Superseded, Refused };
  using Checkpoint = typename Composer::Checkpoint;
  using State = typename Table::EntryState;

  /// Starts the sweep (retires the slot's previous table). Refused when the composer cannot begin the source.
  Status start(Composer& composer, Table& table, uint8_t slot, const SourceRun* source, uint8_t count,
               uint32_t loopLength, uint32_t identity, uint32_t windowTicks, uint32_t spacing) {
    composer_ = &composer;
    table_ = &table;
    slot_ = slot;
    identity_ = identity;
    window_ = windowTicks;
    loop_ = loopLength;
    position_ = 0;
    cycle_ = 0;
    windows_ = 0;
    table.beginSweep(slot, identity, loopLength, spacing);
    status_ = composer.begin(source, count, loopLength, identity) ? Status::Running : Status::Refused;
    return status_;
  }

  Status step(Window& w) {
    if (status_ != Status::Running) return status_;
    if (!table_->current(slot_, identity_)) {
      composer_->end();
      return status_ = Status::Superseded;
    }
    composer_->composeNext(w, window_);
    ++windows_;
    position_ += window_;
    if (position_ >= loop_) {
      position_ = 0;
      if (cycle_ == 0) {  // end of cycle 0: the position-0 checkpoint carries the steady state
        cycle_ = 1;
        Checkpoint cp;
        composer_->capture(cp);
        table_->store(slot_, identity_, cp, State::Usable);
        composer_->nextCycle();
        return status_;
      }
      return finish();  // a whole steady cycle composed: every checkpoint is a cycle-1 capture
    }
    const uint8_t k = table_->indexOf(slot_, position_);
    if (k == Table::kPerSlot) return status_;
    Checkpoint cp;
    composer_->capture(cp);
    if (cycle_ == 0) {
      table_->store(slot_, identity_, cp, State::Provisional);
      return status_;
    }
    if (table_->state(slot_, k) == State::Provisional && sameState(cp, table_->entry(slot_, k))) return finish();
    table_->store(slot_, identity_, cp, State::Usable);
    return status_;
  }

  Status status() const { return status_; }
  uint32_t windows() const { return windows_; }

 private:
  /// Converged: the remaining provisional captures equal their steady counterparts.
  Status finish() {
    for (uint8_t k = 0; k < Table::kPerSlot; ++k)
      if (table_->state(slot_, k) == State::Provisional) table_->setState(slot_, k, State::Usable);
    composer_->end();
    return status_ = Status::Done;
  }

  Composer* composer_ = nullptr;
  Table* table_ = nullptr;
  uint8_t slot_ = 0;
  uint32_t identity_ = 0, window_ = 0, loop_ = 0, position_ = 0, cycle_ = 0, windows_ = 0;
  Status status_ = Status::Refused;
};

/// Mid-loop cold start in steady state at `position` (a window boundary) of playback cycle `cycle` (>= 1): resumes
/// from the nearest usable checkpoint, else warms up from the loop start, and replays into `scratch` until the next
/// `composeNext` yields the window at `position`. `replayed` counts the windows composed. False when the composer
/// cannot begin the source.
template <typename Composer, typename Table, typename Window>
bool coldStart(Composer& composer, const Table& table, uint8_t slot, const SourceRun* source, uint8_t count,
               uint32_t loopLength, uint32_t identity, uint32_t position, uint32_t cycle, uint32_t windowTicks,
               Window& scratch, uint32_t& replayed) {
  replayed = 0;
  uint32_t at = 0;
  if (const auto* cp = table.nearest(slot, identity, position)) {
    if (!composer.resume(source, count, loopLength, identity, *cp)) return false;
    at = cp->position;
  } else {
    if (!composer.begin(source, count, loopLength, identity)) return false;
    while (composer.composeNext(scratch, windowTicks)) ++replayed;  // cycle 0 warm-up
    composer.nextCycle();
  }
  composer.rebaseCycle(cycle);
  for (; at < position; at += windowTicks) {
    composer.composeNext(scratch, windowTicks);
    ++replayed;
  }
  return true;
}

}  // namespace ContentEngine
