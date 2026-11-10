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
// Retention (owner decision 2026-11-05, E6.2d): each slot keeps `Sets` checkpoint sets (2: the current source's and
// one historical), each keyed by (slot, manifest identity). A cold start looks up the set of its own manifest, so Undo /
// Redo to a recently played revision resume from its checkpoints without rebuilding them. A new sweep reuses its
// identity's set or takes the least recently used set that no sweep is writing; Undo / Redo themselves never replace a
// set. Completed checkpoints are value snapshots with an identity stamp and pin nothing (identities are never reused,
// so a stale set never matches another manifest); a running sweep holds its source lease. A sweep whose set was taken
// by a newer sweep ends `Superseded` and writes nothing.

#pragma once

#include <cstdint>
#include <type_traits>
#include <utility>

#include "ContentEngine/EditIndexStore.h"
#include "ContentEngine/EngineConfig.h"
#include "ContentEngine/SourceRun.h"

namespace ContentEngine {

template <typename Checkpoint, uint8_t Slots = 64, uint8_t PerSlot = 8, uint8_t Sets = 2>
class CheckpointTable {
 public:
  static constexpr uint8_t kSlots = Slots;
  static constexpr uint8_t kPerSlot = PerSlot;
  static constexpr uint8_t kSets = Sets;

  enum class EntryState : uint8_t { Empty, Provisional, Usable, Unusable };

  void init() {
    for (auto& slot : slots_)
      for (auto& set : slot.sets) clear(set);
    clock_ = 0;
  }

  /// Starts a sweep for `identity` on `slot`: its own set if the slot has one, else the least recently used set no
  /// sweep is writing (else the least recently used, whose sweep is then superseded). The set is emptied and marked
  /// as being swept until `endSweep`.
  void beginSweep(uint8_t slot, uint32_t identity, uint32_t loopLength, uint32_t spacing) {
    Set* set = find(slot, identity);
    if (set == nullptr) set = victim(slot);
    clear(*set);
    set->identity = identity;
    set->loopLength = loopLength;
    set->spacing = spacing;
    set->sweeping = true;
    set->lastUse = ++clock_;
  }
  void endSweep(uint8_t slot, uint32_t identity) {
    if (Set* set = find(slot, identity)) set->sweeping = false;
  }
  /// Drops `identity`'s set on `slot` (all sets with identity 0).
  void retire(uint8_t slot, uint32_t identity = 0) {
    for (auto& set : slots_[slot].sets)
      if (identity == 0 || set.identity == identity) clear(set);
  }

  /// The slot holds a set for `identity` (a sweep that lost its set is superseded).
  bool holds(uint8_t slot, uint32_t identity) const { return identity != 0 && find(slot, identity) != nullptr; }
  uint32_t spacing(uint8_t slot, uint32_t identity) const {
    const Set* set = find(slot, identity);
    return set == nullptr ? 0 : set->spacing;
  }

  /// Index of the checkpoint at `position`, or PerSlot when it is not a checkpoint position of that set.
  uint8_t indexOf(uint8_t slot, uint32_t identity, uint32_t position) const {
    const Set* set = find(slot, identity);
    if (set == nullptr || set->spacing == 0 || position % set->spacing != 0 || position >= set->loopLength) return PerSlot;
    const uint32_t k = position / set->spacing;
    return k < PerSlot ? static_cast<uint8_t>(k) : PerSlot;
  }

  /// Stores `cp` (state `st`) at its position in `identity`'s set. False when the set is gone (superseded) or the
  /// position is not a checkpoint.
  bool store(uint8_t slot, uint32_t identity, const Checkpoint& cp, EntryState st) {
    const uint8_t k = indexOf(slot, identity, cp.position);
    if (k == PerSlot) return false;
    Set& set = *find(slot, identity);
    set.entries[k] = cp;
    set.state[k] = cp.valid ? st : EntryState::Unusable;
    return true;
  }
  void setState(uint8_t slot, uint32_t identity, uint8_t k, EntryState st) {
    if (Set* set = find(slot, identity)) set->state[k] = st;
  }
  EntryState state(uint8_t slot, uint32_t identity, uint8_t k) const {
    const Set* set = find(slot, identity);
    return set == nullptr ? EntryState::Empty : set->state[k];
  }
  const Checkpoint& entry(uint8_t slot, uint32_t identity, uint8_t k) const { return find(slot, identity)->entries[k]; }

  /// The usable checkpoint of `identity` nearest at or before `position`; nullptr when none (replay from loop start).
  /// A hit marks the set recently used.
  const Checkpoint* nearest(uint8_t slot, uint32_t identity, uint32_t position) const {
    const Set* set = find(slot, identity);
    if (set == nullptr || set->spacing == 0) return nullptr;
    uint32_t k = position / set->spacing;
    if (k >= PerSlot) k = PerSlot - 1;
    for (uint32_t i = k + 1; i-- > 0;)
      if (set->state[i] == EntryState::Usable) {
        set->lastUse = ++clock_;
        return &set->entries[i];
      }
    return nullptr;
  }

 private:
  struct Set {
    uint32_t identity, loopLength, spacing;
    mutable uint32_t lastUse;
    bool sweeping;
    EntryState state[PerSlot];
    Checkpoint entries[PerSlot];
  };
  struct Slot {
    Set sets[Sets];
  };

  static void clear(Set& set) {
    set.identity = set.loopLength = set.spacing = 0;
    set.lastUse = 0;
    set.sweeping = false;
    for (auto& st : set.state) st = EntryState::Empty;
  }
  Set* find(uint8_t slot, uint32_t identity) {
    for (auto& set : slots_[slot].sets)
      if (identity != 0 && set.identity == identity) return &set;
    return nullptr;
  }
  const Set* find(uint8_t slot, uint32_t identity) const {
    for (const auto& set : slots_[slot].sets)
      if (identity != 0 && set.identity == identity) return &set;
    return nullptr;
  }
  /// An empty set, else the least recently used one no sweep is writing, else the least recently used.
  Set* victim(uint8_t slot) {
    Set* best = nullptr;
    for (auto& set : slots_[slot].sets) {
      if (set.identity == 0) return &set;
      if (!set.sweeping && (best == nullptr || set.lastUse < best->lastUse)) best = &set;
    }
    if (best != nullptr) return best;
    for (auto& set : slots_[slot].sets)
      if (best == nullptr || set.lastUse < best->lastUse) best = &set;
    return best;
  }

  Slot slots_[Slots];
  mutable uint32_t clock_ = 0;
};

/// Same composer state, ignoring the cycle stamp (steady state is periodic from cycle 1).
template <typename Checkpoint>
bool sameState(const Checkpoint& a, const Checkpoint& b) {
  if (!a.valid || !b.valid || a.identity != b.identity || a.loopLength != b.loopLength || a.position != b.position ||
      a.runCount != b.runCount || a.activeCount != b.activeCount || a.openCount != b.openCount)
    return false;
  for (uint8_t i = 0; i < a.activeCount; ++i)
    if (a.active[i].pitch != b.active[i].pitch || a.active[i].run != b.active[i].run ||
        a.active[i].count != b.active[i].count)
      return false;
  for (uint8_t i = 0; i < a.openCount; ++i)
    if (a.open[i].noteId != b.open[i].noteId || a.open[i].runKey != b.open[i].runKey ||
        a.open[i].pitch != b.open[i].pitch || a.open[i].channel != b.open[i].channel)
      return false;
  return true;
}

/// A worker job's hold on its source manifest (E5): the manifest pin, the run list with sealed row limits, and the
/// edit-index build in grains before any composition (a composer's `begin` then finds the index Ready and never builds
/// inside a grain). `close` releases everything the job holds; the composer releases its own index lease in `end`.
template <typename Composer, typename Manifests, typename Runs>
class SourceLease {
 public:
  using Store = typename std::remove_pointer<decltype(std::declval<Composer&>().indexStore())>::type;

  void open(Manifests& manifests, Runs& runs, Composer& composer, ManifestId manifest) {
    close();
    manifests_ = &manifests;
    runs_ = &runs;
    store_ = composer.indexStore();
    manifest_ = manifest;
    manifests.pin(manifest);
    pinned_ = true;
    const auto& m = manifests.manifest(manifest);
    identity_ = m.identity;
    loop_ = m.loopLengthTicks;
    count_ = m.runCount;
    // DEC-287: the runs before a groove pass are its sources, kept and not played — composed (and edit-indexed) empty,
    // in place, so a fit lookup's position still names the same run.
    uint8_t played = 0;
    for (uint8_t i = 0; i < count_; ++i)
      if (runs.run(m.runs[i]).kind == RunKind::kGroove) played = i;
    for (uint8_t i = 0; i < count_; ++i) src_[i] = {m.runs[i], i < played ? 0u : runs.run(m.runs[i]).rowCount};
    index_ = store_ == nullptr ? IndexResult::NotNeeded : IndexResult::Busy;  // not reserved yet
  }

  /// One index grain (<= `maxWork` units). Ready / NotNeeded: compose; Building / Busy: call again next grain;
  /// CapacityExceeded: the source cannot be composed.
  IndexResult prepare(uint32_t maxWork) {
    if (index_ == IndexResult::Busy || (index_ == IndexResult::Building && lease_.slot == IndexLease::kNoSlot)) {
      index_ = store_->reserve(identity_, src_, count_, lease_);
      if (index_ != IndexResult::Building || lease_.slot == IndexLease::kNoSlot) return index_;
    }
    if (index_ == IndexResult::Building) {
      index_ = store_->buildStep(lease_, src_, count_, maxWork);
      if (index_ == IndexResult::CapacityExceeded) store_->release(lease_);
    }
    return index_;
  }
  bool prepared() const { return index_ == IndexResult::Ready || index_ == IndexResult::NotNeeded; }
  /// Drops the job's own index pin once the composer has taken its own.
  void dropIndexLease() {
    if (store_ != nullptr) store_->release(lease_);
  }

  void close() {
    dropIndexLease();
    if (pinned_) manifests_->unpin(manifest_, *runs_);
    pinned_ = false;
  }

  bool pinned() const { return pinned_; }
  const SourceRun* source() const { return src_; }
  uint8_t count() const { return count_; }
  uint32_t identity() const { return identity_; }
  uint32_t loopLength() const { return loop_; }

 private:
  Manifests* manifests_ = nullptr;
  Runs* runs_ = nullptr;
  Store* store_ = nullptr;
  ManifestId manifest_ = kNoManifest;
  bool pinned_ = false;
  uint32_t identity_ = 0, loop_ = 0;
  uint8_t count_ = 0;
  IndexResult index_ = IndexResult::NotNeeded;
  IndexLease lease_;
  SourceRun src_[Manifests::kMaxRuns];
};

/// The sweep worker's checkpoint job for one manifest (E5). It pins the manifest while it reads it and releases the pin
/// when it ends (done, superseded, refused or cancelled), so replacement and reclamation never free pages under it.
/// `step` is one grain: at most `maxRows` source rows of the current window. A checkpoint is published only at a window
/// boundary and only while the slot's table still belongs to this sweep; a sweep replaced at any point (also between
/// the grains of one window) ends `Superseded` at its next step and publishes nothing more.
///
/// Work bound: the sweep composes at most two loops (cycle 0, then cycle 1 up to convergence), i.e. at most
/// `2 x ceil(rows per loop / maxRows) + 2 x windows per loop` grains, however long the loop plays; it never waits for
/// playback. `periodic()` (when Done) reports that the composition is periodic from cycle 1, so every published
/// checkpoint equals uninterrupted playback in any later cycle. It holds whenever no effective instance overflowed the composer's open table (Q-OUT-2 proof,
/// design.md); convergence (equal state at one position implies equal state from there on) ends the sweep early.
template <typename Composer, typename Table, typename Window, typename Manifests, typename Runs>
class CheckpointSweep {
 public:
  enum class Status : uint8_t { Idle, Running, Done, Superseded, Refused };
  static constexpr uint32_t kIndexGrain = 256;  // index units per grain (plan § 7.1)
  using Checkpoint = typename Composer::Checkpoint;
  using State = typename Table::EntryState;

  /// Pins `manifest` and takes a checkpoint set for it (CheckpointTable::beginSweep). The first grains build the manifest's edit index (if it
  /// has edit runs and it is not resident); composition begins once it is Ready. Refused (source released) when the
  /// index does not fit.
  Status start(Composer& composer, Table& table, Manifests& manifests, Runs& runs, uint8_t slot, ManifestId manifest,
               uint32_t windowTicks, uint32_t spacing) {
    if (status_ == Status::Running) cancel();
    composer_ = &composer;
    table_ = &table;
    slot_ = slot;
    window_ = windowTicks;
    position_ = 0;
    cycle_ = 0;
    windows_ = 0;
    grains_ = 0;
    unusable_ = 0;
    periodic_ = false;
    composing_ = false;
    source_.open(manifests, runs, composer, manifest);
    identity_ = source_.identity();
    loop_ = source_.loopLength();
    table.beginSweep(slot, identity_, loop_, spacing);
    return status_ = Status::Running;
  }

  /// One grain: `maxRows` source rows of composition, or `kIndexGrain` index units while the index builds.
  Status step(Window& w, uint32_t maxRows) {
    if (status_ != Status::Running) return status_;
    if (!table_->holds(slot_, identity_)) {
      releaseSource();
      return status_ = Status::Superseded;
    }
    ++grains_;
    if (!composing_) {
      const IndexResult r = source_.prepare(kIndexGrain);
      if (r == IndexResult::CapacityExceeded) {
        releaseSource();
        return status_ = Status::Refused;
      }
      if (!source_.prepared()) return status_;
      if (!composer_->begin(source_.source(), source_.count(), loop_, identity_)) {
        releaseSource();
        return status_ = Status::Refused;
      }
      source_.dropIndexLease();
      composing_ = true;
      return status_;
    }
    if (!composer_->windowOpen()) composer_->openWindow(w, window_);
    if (!composer_->composeRows(w, maxRows)) return status_;
    ++windows_;
    position_ += window_;
    if (position_ >= loop_) {
      position_ = 0;
      Checkpoint cp;
      composer_->capture(cp);
      if (cycle_ == 0) {  // end of cycle 0: its selections were made without carried-in instances, so provisional
        cycle_ = 1;
        table_->store(slot_, identity_, cp, State::Provisional);
        composer_->nextCycle();
        return status_;
      }
      // A whole steady cycle 1 without convergence: its end is the steady position-0 state (Q-OUT-2 proof).
      if (!cp.valid) ++unusable_;
      table_->store(slot_, identity_, cp, State::Usable);
      return finish(composer_->stats().openOverflow == 0);
    }
    const uint8_t k = table_->indexOf(slot_, identity_, position_);
    if (k == Table::kPerSlot) return status_;
    Checkpoint cp;
    if (!composer_->capture(cp) && cycle_ != 0) ++unusable_;  // stored as Unusable: a named miss at this position
    if (cycle_ == 0) {
      table_->store(slot_, identity_, cp, State::Provisional);
      return status_;
    }
    if (table_->state(slot_, identity_, k) == State::Provisional && sameState(cp, table_->entry(slot_, identity_, k)))
      return finish(composer_->stats().openOverflow == 0);
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
  bool pinned() const { return source_.pinned(); }
  uint32_t windows() const { return windows_; }
  uint32_t grains() const { return grains_; }
  /// Checkpoint positions whose capture was refused (more open instances or occluder pairs than a checkpoint holds):
  /// named cache misses there; a cold start resumes from an earlier checkpoint or warms up, never from partial state.
  uint32_t unusable() const { return unusable_; }

 private:
  /// Converged (equal state at one position, so equal from there on) or cycle 1 complete: what is still provisional is
  /// promoted only on convergence. `periodic`: no instance overflowed the composer's open table (Q-OUT-2 proof holds).
  Status finish(bool periodic) {
    periodic_ = periodic;
    for (uint8_t k = 0; k < Table::kPerSlot; ++k)
      if (table_->state(slot_, identity_, k) == State::Provisional && periodic)
        table_->setState(slot_, identity_, k, State::Usable);
    releaseSource();
    return status_ = Status::Done;
  }
  void releaseSource() {
    composer_->end();
    source_.close();
    table_->endSweep(slot_, identity_);  // the set (if still ours) may be replaced by least-recently-used from now on
  }

  Composer* composer_ = nullptr;
  Table* table_ = nullptr;
  SourceLease<Composer, Manifests, Runs> source_;
  uint8_t slot_ = 0;
  bool composing_ = false, periodic_ = false;
  uint32_t identity_ = 0, window_ = 0, loop_ = 0, position_ = 0, cycle_ = 0, windows_ = 0, grains_ = 0, unusable_ = 0;
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
  static constexpr uint32_t kIndexGrain = 256;

  /// `warmUp` false (E9.1b, a Record prefix: nothing crosses its loop end yet): with no checkpoint, compose from the
  /// loop start in cycle `cycle` without replaying cycle 0 first.
  Status start(Composer& composer, const Table& table, Manifests& manifests, Runs& runs, uint8_t slot,
               ManifestId manifest, uint32_t position, uint32_t cycle, uint32_t windowTicks, bool warmUp = true) {
    cancel();
    warm_ = warmUp;
    composer_ = &composer;
    table_ = &table;
    slot_ = slot;
    target_ = position;
    cycle_ = cycle;
    window_ = windowTicks;
    replayed_ = 0;
    grains_ = 0;
    warmup_ = false;
    composing_ = false;
    seeking_ = false;
    source_.open(manifests, runs, composer, manifest);
    return status_ = Status::Running;
  }

  /// One grain: `maxRows` source rows of replay, or `kIndexGrain` index units while the index builds.
  Status step(Window& scratch, uint32_t maxRows) {
    if (status_ != Status::Running) return status_;
    ++grains_;
    if (seeking_) {  // the second resume grain
      composer_->resumeSeek();
      seeking_ = false;
      composing_ = true;
      settle();
      return status_;
    }
    if (!composing_) {
      if (source_.prepare(kIndexGrain) == IndexResult::CapacityExceeded) return refuse();
      if (source_.prepared()) begin();
      return status_;
    }
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
    source_.close();
  }

  Status status() const { return status_; }
  bool pinned() const { return source_.pinned(); }
  uint32_t replayed() const { return replayed_; }
  /// The cold start resumed from a checkpoint of its manifest (false: it warmed up from the loop start).
  bool fromCheckpoint() const { return fromCheckpoint_; }
  uint32_t grains() const { return grains_; }

 private:
  /// Index Ready: resume from the nearest usable checkpoint, else warm up from the loop start.
  void begin() {
    fromCheckpoint_ = false;
    if (const auto* cp = table_->nearest(slot_, source_.identity(), target_)) {
      fromCheckpoint_ = true;
      if (!composer_->resumeBind(source_.source(), source_.count(), source_.loopLength(), source_.identity(), *cp)) {
        refuse();
        return;
      }
      at_ = cp->position;
      source_.dropIndexLease();
      seeking_ = true;  // cursors are positioned on the next grain
      return;
    } else {
      if (!composer_->begin(source_.source(), source_.count(), source_.loopLength(), source_.identity())) {
        refuse();
        return;
      }
      warmup_ = warm_;  // cycle 0 from the loop start, then replay to the position
      at_ = 0;
    }
    source_.dropIndexLease();
    composing_ = true;
    settle();
  }
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
  const Table* table_ = nullptr;
  SourceLease<Composer, Manifests, Runs> source_;
  uint8_t slot_ = 0;
  bool warmup_ = false, composing_ = false, seeking_ = false, fromCheckpoint_ = false, warm_ = true;
  uint32_t target_ = 0, cycle_ = 0, window_ = 0, at_ = 0, replayed_ = 0, grains_ = 0;
  Status status_ = Status::Idle;
};

}  // namespace ContentEngine
