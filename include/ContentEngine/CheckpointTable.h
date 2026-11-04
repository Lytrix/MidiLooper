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

#include "ContentEngine/EngineConfig.h"
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

/// The sweep worker's checkpoint job for one manifest (E5). It pins the manifest while it reads it and releases the pin
/// when it ends (done, superseded, refused or cancelled), so replacement and reclamation never free pages under it.
/// `step` is one grain: at most `maxRows` source rows of the current window. A checkpoint is published only at a window
/// boundary and only while the slot's table still belongs to this sweep; a sweep replaced at any point (also between
/// the grains of one window) ends `Superseded` at its next step and publishes nothing more.
///
/// Work bound: the sweep composes at most two loops (cycle 0, then cycle 1 up to convergence), i.e. at most
/// `2 x ceil(rows per loop / maxRows) + 2 x windows per loop` grains, however long the loop plays; it never waits for
/// playback. `periodic()` (when Done) reports that state at the end of cycle 1 equals state at the end of cycle 0,
/// i.e. that the composition is periodic from cycle 1 and every published checkpoint equals uninterrupted playback in
/// any later cycle. It is proved by convergence (equal state at one position implies equal state from there on) or by
/// comparing the position-0 states; see design.md E5 for when it holds.
template <typename Composer, typename Table, typename Window, typename Manifests, typename Runs>
class CheckpointSweep {
 public:
  enum class Status : uint8_t { Idle, Running, Done, Superseded, Refused };
  using Checkpoint = typename Composer::Checkpoint;
  using State = typename Table::EntryState;

  /// Pins `manifest`, retires the slot's previous table and begins composing. Refused (pin released) when the composer
  /// cannot begin the source.
  Status start(Composer& composer, Table& table, Manifests& manifests, Runs& runs, uint8_t slot, ManifestId manifest,
               uint32_t windowTicks, uint32_t spacing) {
    if (status_ == Status::Running) cancel();
    composer_ = &composer;
    table_ = &table;
    manifests_ = &manifests;
    runs_ = &runs;
    slot_ = slot;
    manifest_ = manifest;
    window_ = windowTicks;
    position_ = 0;
    cycle_ = 0;
    windows_ = 0;
    grains_ = 0;
    periodic_ = false;
    manifests.pin(manifest);
    pinned_ = true;
    const auto& m = manifests.manifest(manifest);
    identity_ = m.identity;
    loop_ = m.loopLengthTicks;
    SourceRun src[Manifests::kMaxRuns];
    for (uint8_t i = 0; i < m.runCount; ++i) src[i] = {m.runs[i], runs.run(m.runs[i]).rowCount};
    table.beginSweep(slot, identity_, loop_, spacing);
    status_ = Status::Running;
    if (!composer.begin(src, m.runCount, loop_, identity_)) {
      releaseSource();
      status_ = Status::Refused;
    }
    return status_;
  }

  /// One grain.
  Status step(Window& w, uint32_t maxRows) {
    if (status_ != Status::Running) return status_;
    if (!table_->current(slot_, identity_)) {
      releaseSource();
      return status_ = Status::Superseded;
    }
    ++grains_;
    if (!composer_->windowOpen()) composer_->openWindow(w, window_);
    if (!composer_->composeRows(w, maxRows)) return status_;
    ++windows_;
    position_ += window_;
    if (position_ >= loop_) {
      position_ = 0;
      Checkpoint cp;
      composer_->capture(cp);
      if (cycle_ == 0) {  // end of cycle 0: the position-0 checkpoint carries the steady state
        cycle_ = 1;
        table_->store(slot_, identity_, cp, State::Usable);
        composer_->nextCycle();
        return status_;
      }
      // A whole cycle 1 without convergence: periodic iff the cycle ends in the state it began with.
      return finish(table_->state(slot_, 0) == State::Usable && sameState(cp, table_->entry(slot_, 0)));
    }
    const uint8_t k = table_->indexOf(slot_, position_);
    if (k == Table::kPerSlot) return status_;
    Checkpoint cp;
    composer_->capture(cp);
    if (cycle_ == 0) {
      table_->store(slot_, identity_, cp, State::Provisional);
      return status_;
    }
    if (table_->state(slot_, k) == State::Provisional && sameState(cp, table_->entry(slot_, k))) return finish(true);
    table_->store(slot_, identity_, cp, State::Usable);
    return status_;
  }

  /// Stops the sweep and releases its source; the table keeps what was published (provisional entries stay unusable).
  void cancel() {
    if (status_ != Status::Running) return;
    releaseSource();
    status_ = Status::Superseded;
  }

  Status status() const { return status_; }
  bool periodic() const { return periodic_; }
  bool pinned() const { return pinned_; }
  uint32_t windows() const { return windows_; }
  uint32_t grains() const { return grains_; }

 private:
  /// Promotes the remaining provisional captures only when they are proved equal to their steady counterparts.
  Status finish(bool periodic) {
    periodic_ = periodic;
    if (periodic)
      for (uint8_t k = 0; k < Table::kPerSlot; ++k)
        if (table_->state(slot_, k) == State::Provisional) table_->setState(slot_, k, State::Usable);
    releaseSource();
    return status_ = Status::Done;
  }
  void releaseSource() {
    composer_->end();
    if (pinned_) manifests_->unpin(manifest_, *runs_);
    pinned_ = false;
  }

  Composer* composer_ = nullptr;
  Table* table_ = nullptr;
  Manifests* manifests_ = nullptr;
  Runs* runs_ = nullptr;
  uint8_t slot_ = 0;
  ManifestId manifest_ = kNoManifest;
  bool pinned_ = false, periodic_ = false;
  uint32_t identity_ = 0, window_ = 0, loop_ = 0, position_ = 0, cycle_ = 0, windows_ = 0, grains_ = 0;
  Status status_ = Status::Idle;
};

/// Mid-loop cold start (E5) in steady state at `position` (a window boundary) of playback cycle `cycle` (>= 1). It pins
/// the manifest, resumes from the nearest usable checkpoint (else warms up a whole cycle from the loop start: the
/// bounded reconstruction path) and replays, one grain per `step`, until the next `composeNext` yields the window at
/// `position`. The track keeps playing its previous source meanwhile. On `Done` the composer and the pin pass to the
/// caller (a compose cursor pins its manifest), who calls `release` when the cursor ends; `cancel` releases at once.
template <typename Composer, typename Table, typename Window, typename Manifests, typename Runs>
class ColdStart {
 public:
  enum class Status : uint8_t { Idle, Running, Done, Refused, Cancelled };

  Status start(Composer& composer, const Table& table, Manifests& manifests, Runs& runs, uint8_t slot,
               ManifestId manifest, uint32_t position, uint32_t cycle, uint32_t windowTicks) {
    cancel();
    composer_ = &composer;
    manifests_ = &manifests;
    runs_ = &runs;
    manifest_ = manifest;
    target_ = position;
    cycle_ = cycle;
    window_ = windowTicks;
    replayed_ = 0;
    grains_ = 0;
    manifests.pin(manifest);
    pinned_ = true;
    const auto& m = manifests.manifest(manifest);
    SourceRun src[Manifests::kMaxRuns];
    for (uint8_t i = 0; i < m.runCount; ++i) src[i] = {m.runs[i], runs.run(m.runs[i]).rowCount};
    warmup_ = false;
    if (const auto* cp = table.nearest(slot, m.identity, position)) {
      if (!composer.resume(src, m.runCount, m.loopLengthTicks, m.identity, *cp)) return refuse();
      at_ = cp->position;
    } else {
      if (!composer.begin(src, m.runCount, m.loopLengthTicks, m.identity)) return refuse();
      warmup_ = true;  // cycle 0 from the loop start, then replay to the position
      at_ = 0;
    }
    status_ = Status::Running;
    if (!warmup_) settle();
    return status_;
  }

  /// One grain.
  Status step(Window& scratch, uint32_t maxRows) {
    if (status_ != Status::Running) return status_;
    ++grains_;
    if (!composer_->windowOpen()) composer_->openWindow(scratch, window_);
    if (!composer_->composeRows(scratch, maxRows)) return status_;
    ++replayed_;
    if (warmup_) {
      if (composer_->windowOpen() || !composer_->nextCycle()) return status_;  // still in cycle 0
      warmup_ = false;
      at_ = 0;
    } else {
      at_ += window_;
    }
    settle();
    return status_;
  }

  void cancel() {
    if (status_ == Status::Running || status_ == Status::Done) release();
    if (status_ == Status::Running) status_ = Status::Cancelled;
  }
  /// Ends the composer's use of the source (the cursor that took it over ends).
  void release() {
    if (composer_ != nullptr) composer_->end();
    if (pinned_) manifests_->unpin(manifest_, *runs_);
    pinned_ = false;
  }

  Status status() const { return status_; }
  bool pinned() const { return pinned_; }
  uint32_t replayed() const { return replayed_; }
  uint32_t grains() const { return grains_; }

 private:
  void settle() {
    if (warmup_ || at_ < target_) return;
    composer_->rebaseCycle(cycle_);
    status_ = Status::Done;
  }
  Status refuse() {
    release();
    return status_ = Status::Refused;
  }

  Composer* composer_ = nullptr;
  Manifests* manifests_ = nullptr;
  Runs* runs_ = nullptr;
  ManifestId manifest_ = kNoManifest;
  bool pinned_ = false, warmup_ = false;
  uint32_t target_ = 0, cycle_ = 0, window_ = 0, at_ = 0, replayed_ = 0, grains_ = 0;
  Status status_ = Status::Idle;
};

}  // namespace ContentEngine
