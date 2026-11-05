//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// Undo history and slot currents of the content engine (fixed-memory-engine E4; owner decisions 2026-11-05).
//
// * Per slot: undo and redo step only the slot they are asked for. A slot's history is a chain of committed states
//   (one manifest each, oldest to newest) and a current pointer into it; below the first state lies the empty slot,
//   unless older states were evicted, in which case the oldest kept state is the floor.
// * Shared depth: at most `Depth` states are held across all slots (99 in W). Each holds one manifest reference.
// * Commit: drops the slot's redo branch (newer than current), then, if the table is full, evicts the oldest step in the
//   set — the oldest-committed state that is not any slot's current — and appends the new state as current. Recording
//   is never refused for history. Evicting a slot's oldest undo state moves its floor up; evicting a redo state drops
//   that slot's whole redo branch (a redo chain cannot have a hole).
// * Undo / redo: O(1) pointer moves; they return the slot's new current manifest (`kNoManifest`: the empty slot).
// * Overdub session (DEC-038, E4.2): every non-empty wrap is an ordinary commit. While the session is open, undo on its
//   slot stops at the session start (the O: level counts the session's states). `endSession` (Stop) drops the
//   session's redo and collapses its states into one step — the session's current — so U: undoes the whole session
//   at once; with every wrap undone it leaves no step. The start is the commit counter at `beginSession`, so eviction
//   (the session's states share the 99) can never leave it dangling, and redo from before the session is neither a
//   session step nor reachable while the session is open.
// * NOTE_EDIT session (E4.3; owner 2026-11-05: limit 99, shared): each edit action is a commit (the E: level); the
//   same bound and fold apply when NOTE_EDIT is left, and `abandonSession` returns the slot to its session-start state.
//   One overdub session and one NOTE_EDIT session may be open at once (W: one writer + one NOTE_EDIT), on different
//   slots.
// Every dropped state releases its manifest reference, so its runs reach the reclaim queue. No allocation.

#pragma once

#include <cstdint>

#include "ContentEngine/EngineConfig.h"

namespace ContentEngine {

enum class HistoryCommit : uint8_t { Committed, CommittedEvicted, Refused };
enum class SessionKind : uint8_t { Overdub = 0, Edit = 1 };

template <typename Manifests, typename Runs, uint8_t Slots = Config::kSlotCount, uint8_t Depth = Config::kHistoryDepth>
class History {
 public:
  static constexpr uint8_t kNone = 0xFF;
  static_assert(Depth < kNone, "entry indices fit uint8_t");
  static_assert(Depth > Slots || Slots == 0, "a full table always holds an evictable state (currents <= slots)");

  struct Stats {
    uint32_t evictedUndo;     // oldest undo states forgotten to make room
    uint32_t droppedRedo;     // redo states dropped (by a commit on their slot, or by eviction)
    uint32_t collapsedWraps;  // session states folded into the session's one step at its end (overdub or edit)
  };

  void init(Manifests& manifests, Runs& runs) {
    manifests_ = &manifests;
    runs_ = &runs;
    for (uint8_t i = 0; i < Depth; ++i) {
      entries_[i] = Entry{kNoManifest, 0, kNone, kNone, 0};
      nextFree_[i] = static_cast<uint8_t>(i + 1 < Depth ? i + 1 : kNone);
    }
    freeHead_ = Depth ? 0 : kNone;
    count_ = 0;
    seq_ = 0;
    for (auto& s : slots_) s = Slot{kNone, kNone, kNone, true};
    stats_ = Stats{};
    for (auto& x : sessions_) x = Session{kNone, 0};
  }

  /// Opens a session of `kind` on `slot` at its current state. False (nothing changes) while a session of that kind is
  /// open, or while the other kind is open on the same slot.
  bool beginSession(SessionKind kind, uint8_t slot) {
    Session& x = sessions_[static_cast<uint8_t>(kind)];
    if (slot >= Slots || x.slot != kNone || sessions_[1 - static_cast<uint8_t>(kind)].slot == slot) return false;
    x = Session{slot, seq_};
    return true;
  }
  bool inSession(SessionKind kind) const { return sessions_[static_cast<uint8_t>(kind)].slot != kNone; }
  /// The O: / E: level: the session's states at or below its slot's current.
  uint8_t sessionDepth(SessionKind kind) const {
    const Session& x = sessions_[static_cast<uint8_t>(kind)];
    if (x.slot == kNone) return 0;
    uint8_t n = 0;
    for (uint8_t e = slots_[x.slot].current; e != kNone && entries_[e].seq > x.baseSeq; e = entries_[e].older) ++n;
    return n;
  }
  /// Stop / leave NOTE_EDIT: drops the session's redo and folds its states into one step (the current). No step when
  /// every step was undone. O(session states).
  void endSession(SessionKind kind) {
    Session& x = sessions_[static_cast<uint8_t>(kind)];
    if (x.slot == kNone) return;
    const uint8_t slot = x.slot;
    const uint32_t base = x.baseSeq;
    x = Session{kNone, 0};
    dropRedo(slot);
    Slot& s = slots_[slot];
    const uint8_t c = s.current;
    if (c == kNone || entries_[c].seq <= base) return;
    uint8_t e = entries_[c].older;
    while (e != kNone && entries_[e].seq > base) {
      const uint8_t older = entries_[e].older;
      freeEntry(e);
      ++stats_.collapsedWraps;
      e = older;
    }
    entries_[c].older = e;
    if (e == kNone)
      s.oldest = c;
    else
      entries_[e].newer = c;
  }
  /// Abandon (leave NOTE_EDIT without keeping it): every session state goes and the slot returns to its session-start
  /// state. False when that state was forgotten during the session (the session outgrew the 99): the slot then keeps
  /// its oldest remaining state, the floor.
  bool abandonSession(SessionKind kind) {
    Session& x = sessions_[static_cast<uint8_t>(kind)];
    if (x.slot == kNone) return true;
    const uint8_t slot = x.slot;
    const uint32_t base = x.baseSeq;
    x = Session{kNone, 0};
    Slot& s = slots_[slot];
    uint8_t e = s.newest;
    bool removed = false;
    while (e != kNone && entries_[e].seq > base) {
      const uint8_t older = entries_[e].older;
      if (older == kNone && !s.emptyBelow) break;  // the start was forgotten: keep the floor
      freeEntry(e);
      ++stats_.droppedRedo;
      removed = true;
      e = older;
    }
    if (!removed) return s.current == kNone || entries_[s.current].seq <= base;  // no step taken: current is the start
    s.current = e;
    if (e == kNone) {
      s.oldest = s.newest = kNone;
      return true;
    }
    entries_[e].newer = kNone;
    s.newest = e;
    return entries_[e].seq <= base;
  }

  /// Makes `manifest` the slot's new current state. History takes over the caller's reference.
  HistoryCommit commit(uint8_t slot, ManifestId manifest) {
    if (slot >= Slots || manifest == kNoManifest) return HistoryCommit::Refused;
    dropRedo(slot);
    bool evicted = false;
    if (count_ == Depth) {
      evictOldest();
      evicted = true;
    }
    const uint8_t e = freeHead_;
    freeHead_ = nextFree_[e];
    ++count_;
    Slot& s = slots_[slot];
    entries_[e] = Entry{manifest, ++seq_, s.newest, kNone, slot};
    if (s.newest != kNone) entries_[s.newest].newer = e;
    if (s.oldest == kNone) s.oldest = e;
    s.newest = e;
    s.current = e;
    return evicted ? HistoryCommit::CommittedEvicted : HistoryCommit::Committed;
  }

  bool canUndo(uint8_t slot) const {
    const Slot& s = slots_[slot];
    if (s.current == kNone) return false;
    for (const Session& x : sessions_)
      if (x.slot == slot && entries_[s.current].seq <= x.baseSeq) return false;  // not below the session start
    return entries_[s.current].older != kNone || s.emptyBelow;
  }
  bool canRedo(uint8_t slot) const {
    const Slot& s = slots_[slot];
    const uint8_t next = s.current == kNone ? (s.emptyBelow ? s.oldest : kNone) : entries_[s.current].newer;
    if (next == kNone) return false;
    for (const Session& x : sessions_)
      if (x.slot == slot && entries_[next].seq <= x.baseSeq) return false;  // not into redo from before the session
    return true;
  }

  /// O(1). False (nothing changes) when the slot cannot undo.
  bool undo(uint8_t slot) {
    if (!canUndo(slot)) return false;
    Slot& s = slots_[slot];
    s.current = entries_[s.current].older;  // kNone: the empty slot
    return true;
  }
  bool redo(uint8_t slot) {
    if (!canRedo(slot)) return false;
    Slot& s = slots_[slot];
    s.current = s.current == kNone ? s.oldest : entries_[s.current].newer;
    return true;
  }

  /// The slot's current manifest, or `kNoManifest` for an empty slot.
  ManifestId current(uint8_t slot) const {
    const uint8_t c = slots_[slot].current;
    return c == kNone ? kNoManifest : entries_[c].manifest;
  }
  /// Undo steps available on the slot (the `U:` level).
  uint8_t undoDepth(uint8_t slot) const {
    const Slot& s = slots_[slot];
    uint8_t n = 0;
    for (uint8_t e = s.current; e != kNone; e = entries_[e].older) ++n;
    return s.emptyBelow ? n : static_cast<uint8_t>(n - (n > 0 ? 1 : 0));
  }
  uint8_t redoDepth(uint8_t slot) const {
    const Slot& s = slots_[slot];
    uint8_t n = 0;
    for (uint8_t e = s.current == kNone ? (s.emptyBelow ? s.oldest : kNone) : entries_[s.current].newer; e != kNone;
         e = entries_[e].newer)
      ++n;
    return n;
  }
  uint8_t heldStates() const { return count_; }
  const Stats& stats() const { return stats_; }

 private:
  struct Entry {
    ManifestId manifest;
    uint32_t seq;  // commit order across all slots
    uint8_t older, newer;
    uint8_t slot;
  };
  struct Session {
    uint8_t slot;      // kNone: not open
    uint32_t baseSeq;  // the commit counter at the session start: the session's states are the later commits
  };
  struct Slot {
    uint8_t oldest, newest, current;  // current kNone: the empty slot (below `oldest`)
    bool emptyBelow;                  // the empty slot is reachable by undo (no state of this slot was evicted)
  };

  void freeEntry(uint8_t e) {
    manifests_->release(entries_[e].manifest, *runs_);
    entries_[e].manifest = kNoManifest;
    nextFree_[e] = freeHead_;
    freeHead_ = e;
    --count_;
  }

  /// Drops every state newer than the slot's current (all of them when the slot is at its empty base).
  void dropRedo(uint8_t slot) {
    Slot& s = slots_[slot];
    uint8_t e = s.current == kNone ? s.oldest : entries_[s.current].newer;
    if (e == kNone) return;
    if (s.current == kNone) {
      s.oldest = s.newest = kNone;
      s.emptyBelow = true;  // the base is the empty slot again: everything above it is gone
    } else {
      entries_[s.current].newer = kNone;
      s.newest = s.current;
    }
    while (e != kNone) {
      const uint8_t next = entries_[e].newer;
      freeEntry(e);
      ++stats_.droppedRedo;
      e = next;
    }
  }

  /// The oldest step in the set: per slot, its oldest state if that is not current (an undo state, or — when the slot
  /// is at its empty base — the start of its redo chain), else the start of its redo chain. Lowest commit seq wins.
  void evictOldest() {
    uint8_t bestSlot = kNone;
    uint32_t bestSeq = 0;
    for (uint8_t i = 0; i < Slots; ++i) {
      const Slot& s = slots_[i];
      uint8_t cand = kNone;
      if (s.oldest != kNone && s.oldest != s.current)
        cand = s.oldest;
      else if (s.current != kNone)
        cand = entries_[s.current].newer;
      if (cand != kNone && (bestSlot == kNone || entries_[cand].seq < bestSeq)) {
        bestSlot = i;
        bestSeq = entries_[cand].seq;
      }
    }
    Slot& s = slots_[bestSlot];
    const bool undoSide = s.current != kNone && s.oldest != s.current;
    if (!undoSide) {
      dropRedo(bestSlot);  // a redo chain cannot have a hole
      return;
    }
    const uint8_t e = s.oldest;
    s.oldest = entries_[e].newer;
    entries_[s.oldest].older = kNone;
    s.emptyBelow = false;
    freeEntry(e);
    ++stats_.evictedUndo;
  }

  Manifests* manifests_ = nullptr;
  Runs* runs_ = nullptr;
  Entry entries_[Depth];
  uint8_t nextFree_[Depth];
  uint8_t freeHead_ = kNone;
  uint8_t count_ = 0;
  Session sessions_[2];  // indexed by SessionKind
  uint32_t seq_ = 0;
  Slot slots_[Slots];
  Stats stats_{};
};

}  // namespace ContentEngine
