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
// * Restore point (owner 2026-11-05): an open session protects its start state (or, for a session started on the empty
//   slot, the empty slot) from eviction, so abandon always returns to the pre-session state and U: after the fold
//   always reaches it. A session longer than the 99 forgets its own oldest steps instead; undo then steps from the
//   oldest kept session state straight back to the start.
// * Pressure diagnostics (E4.7, owner 2026-11-06; no effect on any rule above): the most states ever held, steps
//   forgotten by eviction (undo states, and redo branches), how many each slot lost and which lost last, and undo
//   presses refused because the slot's older steps were forgotten.
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
  static constexpr uint8_t kDepth = Depth;
  static_assert(Depth < kNone, "entry indices fit uint8_t");
  static_assert(Depth > Slots + 2 || Slots == 0,
                "a full table always holds an evictable state (currents <= slots, protected session starts <= 2)");

  struct Stats {
    uint32_t evictedUndo;     // oldest undo states forgotten to make room
    uint32_t droppedRedo;     // redo states dropped (by a commit on their slot, or by eviction)
    uint32_t collapsedWraps;  // session states folded into the session's one step at its end (overdub or edit)
    // Pressure (E4.7).
    uint32_t evictedRedo;       // redo branches dropped by eviction (their states also count in `droppedRedo`)
    uint32_t undoPastRetained;  // undo refused at a floor that eviction raised (older steps forgotten)
    uint8_t highWater;          // the most states held at once
    uint8_t lastLostSlot;       // the slot that last lost a step to eviction (kNone: none)
    uint8_t lastPastSlot;       // the slot of the last undo refused past retained history (kNone: none)
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
    stats_.lastLostSlot = stats_.lastPastSlot = kNone;
    for (auto& n : lost_) n = 0;
    for (auto& x : sessions_) x = Session{kNone, 0, kNone};
  }

  /// Opens a session of `kind` on `slot` at its current state. False (nothing changes) while a session of that kind is
  /// open, or while the other kind is open on the same slot.
  bool beginSession(SessionKind kind, uint8_t slot) {
    Session& x = sessions_[static_cast<uint8_t>(kind)];
    if (slot >= Slots || x.slot != kNone || sessions_[1 - static_cast<uint8_t>(kind)].slot == slot) return false;
    x = Session{slot, seq_, slots_[slot].current};  // the start state is protected until the session ends
    return true;
  }
  bool inSession(SessionKind kind) const { return sessions_[static_cast<uint8_t>(kind)].slot != kNone; }
  /// The slot a session of `kind` is open on (kNone: none).
  uint8_t sessionSlot(SessionKind kind) const { return sessions_[static_cast<uint8_t>(kind)].slot; }
  /// The O: / E: level: the session's states at or below its slot's current.
  uint8_t sessionDepth(SessionKind kind) const {
    const Session& x = sessions_[static_cast<uint8_t>(kind)];
    if (x.slot == kNone) return 0;
    uint8_t n = 0;
    for (uint8_t e = slots_[x.slot].current; e != kNone && entries_[e].seq > x.baseSeq && n < Depth; e = entries_[e].older)
      ++n;
    return n;
  }
  /// Stop / leave NOTE_EDIT: drops the session's redo and folds its states into one step (the current). No step when
  /// every step was undone. O(session states).
  void endSession(SessionKind kind) {
    Session& x = sessions_[static_cast<uint8_t>(kind)];
    if (x.slot == kNone) return;
    const uint8_t slot = x.slot;
    const uint32_t base = x.baseSeq;
    x = Session{kNone, 0, kNone};
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
  /// state, which the session protected from eviction. O(session states).
  void abandonSession(SessionKind kind) {
    Session& x = sessions_[static_cast<uint8_t>(kind)];
    if (x.slot == kNone) return;
    const uint8_t slot = x.slot;
    const uint32_t base = x.baseSeq;
    const uint8_t start = x.base;
    x = Session{kNone, 0, kNone};
    Slot& s = slots_[slot];
    uint8_t e = s.newest;
    bool removed = false;
    while (e != kNone && entries_[e].seq > base) {
      const uint8_t older = entries_[e].older;
      freeEntry(e);
      ++stats_.droppedRedo;
      removed = true;
      e = older;
    }
    if (!removed) return;  // no session step: the current is still the start
    s.current = start;
    if (start == kNone) {
      s.oldest = s.newest = kNone;
      return;
    }
    entries_[start].newer = kNone;
    s.newest = start;
  }

  /// NOTE_EDIT (E4.5): the open session of `kind` has one state, replaced in place as edits are rebuilt. `manifest`
  /// becomes it (History takes the reference; a replaced manifest is released); `kNoManifest` removes it, so the slot is
  /// back at the session start. False when no session of `kind` is open.
  bool setSessionState(SessionKind kind, ManifestId manifest) {
    const Session& x = sessions_[static_cast<uint8_t>(kind)];
    if (x.slot == kNone) return false;
    Slot& s = slots_[x.slot];
    const bool sessionCurrent = s.current != kNone && entries_[s.current].seq > x.baseSeq;
    if (manifest == kNoManifest) {
      if (sessionCurrent) {
        s.current = entries_[s.current].older;  // the start (protected, so still held)
        dropRedo(x.slot);
      }
      return true;
    }
    if (!sessionCurrent) {
      commit(x.slot, manifest);
      return true;
    }
    manifests_->release(entries_[s.current].manifest, *runs_);
    entries_[s.current].manifest = manifest;
    return true;
  }

  /// A take's completion (E9.1): the slot's state holding `old` holds `fresh` instead, in place (no step, no redo
  /// dropped). History takes the caller's reference to `fresh` and releases `old`. False (nothing changes) when no state
  /// of the slot holds `old`. O(slot states).
  bool replaceState(uint8_t slot, ManifestId old, ManifestId fresh) {
    if (slot >= Slots || fresh == kNoManifest) return false;
    for (uint8_t e = slots_[slot].oldest; e != kNone; e = entries_[e].newer) {
      if (entries_[e].manifest != old) continue;
      manifests_->release(old, *runs_);
      entries_[e].manifest = fresh;
      return true;
    }
    return false;
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
    if (count_ > stats_.highWater) stats_.highWater = count_;
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
    if (!canUndo(slot)) {
      if (slot < Slots && atForgottenFloor(slot)) {
        ++stats_.undoPastRetained;
        stats_.lastPastSlot = slot;
      }
      return false;
    }
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
    for (uint8_t e = s.current; e != kNone && n < Depth; e = entries_[e].older) ++n;
    return s.emptyBelow ? n : static_cast<uint8_t>(n - (n > 0 ? 1 : 0));
  }
  uint8_t redoDepth(uint8_t slot) const {
    const Slot& s = slots_[slot];
    uint8_t n = 0;
    for (uint8_t e = s.current == kNone ? (s.emptyBelow ? s.oldest : kNone) : entries_[s.current].newer; e != kNone;
         e = entries_[e].newer) {
      if (n == Depth) break;  // never more states than the table holds
      ++n;
    }
    return n;
  }
  uint8_t heldStates() const { return count_; }
  /// Diagnostics (undo-depth sizing, E9.8b-3b): the newest commit's slot, manifest and the state before it on that slot
  /// (`kNoManifest`: none). False: none held.
  bool newest(uint8_t& slot, ManifestId& manifest, ManifestId* before = nullptr) const {
    uint32_t best = 0;
    uint8_t at = kNone;
    for (uint8_t i = 0; i < Depth; ++i)
      if (entries_[i].manifest != kNoManifest && entries_[i].seq > best) {
        best = entries_[i].seq;
        at = i;
      }
    if (at == kNone) return false;
    slot = entries_[at].slot;
    manifest = entries_[at].manifest;
    if (before != nullptr)
      *before = entries_[at].older == kNone ? kNoManifest : entries_[entries_[at].older].manifest;
    return true;
  }

  /// Persistence (E8): the slot's states oldest to newest (at most `max`), the current one's index in that list (-1:
  /// the empty slot) and whether the empty slot lies below the oldest. O(states).
  uint8_t chain(uint8_t slot, ManifestId* out, uint8_t max, int16_t& currentIndex, bool& emptyBelow) const {
    const Slot& s = slots_[slot];
    uint8_t n = 0;
    currentIndex = -1;
    for (uint8_t e = s.oldest; e != kNone && n < max; e = entries_[e].newer) {
      if (e == s.current) currentIndex = n;
      out[n++] = entries_[e].manifest;
    }
    emptyBelow = s.emptyBelow;
    return n;
  }
  /// Load (E8): rebuilds an empty slot's chain from saved states (oldest to newest; History takes their references),
  /// then steps back to the saved current state and floor. Commits follow the ordinary rules (the shared 99 may
  /// forget older steps elsewhere). False when the slot is not empty or the input is inconsistent.
  bool restoreSlot(uint8_t slot, const ManifestId* states, uint8_t n, int16_t currentIndex, bool emptyBelow) {
    if (slot >= Slots || slots_[slot].oldest != kNone || currentIndex >= static_cast<int16_t>(n) || currentIndex < -1 ||
        (currentIndex == -1 && n > 0 && !emptyBelow))
      return false;
    for (uint8_t i = 0; i < n; ++i) commit(slot, states[i]);
    Slot& s = slots_[slot];
    s.emptyBelow = emptyBelow;
    uint8_t e = s.newest;
    for (int16_t i = static_cast<int16_t>(n) - 1; i > currentIndex && e != kNone; --i) e = entries_[e].older;
    s.current = currentIndex < 0 ? kNone : e;
    return true;
  }
  /// Sets browser 2a (DEC-259): forgets the slot's whole history — a load replaces the set, histories included. The
  /// states' references are released (a pinned one lives until its reader unpins it). False while a session is open on
  /// the slot.
  bool clearSlot(uint8_t slot) {
    if (slot >= Slots) return false;
    for (const Session& x : sessions_)
      if (x.slot == slot) return false;
    Slot& s = slots_[slot];
    for (uint8_t e = s.oldest; e != kNone;) {
      const uint8_t next = entries_[e].newer;
      freeEntry(e);
      e = next;
    }
    s.oldest = s.newest = s.current = kNone;
    s.emptyBelow = true;
    lost_[slot] = 0;
    return true;
  }
  const Stats& stats() const { return stats_; }
  /// States committed so far (any slot): the sets browser's evidence of changed content (slice 4).
  uint32_t commits() const { return seq_; }
  /// Steps the slot has lost to eviction (undo states and dropped redo states), saturating.
  uint16_t lostSteps(uint8_t slot) const { return slot < Slots ? lost_[slot] : 0; }

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
    uint8_t base;      // the start state, protected from eviction (kNone: the session started on the empty slot)
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

  /// Drops every state newer than the slot's current (all of them when the slot is at its empty base); returns how many.
  uint8_t dropRedo(uint8_t slot) {
    Slot& s = slots_[slot];
    uint8_t e = s.current == kNone ? s.oldest : entries_[s.current].newer;
    if (e == kNone) return 0;
    if (s.current == kNone) {
      s.oldest = s.newest = kNone;
      s.emptyBelow = true;  // the base is the empty slot again: everything above it is gone
    } else {
      entries_[s.current].newer = kNone;
      s.newest = s.current;
    }
    uint8_t n = 0;
    while (e != kNone) {
      const uint8_t next = entries_[e].newer;
      freeEntry(e);
      ++stats_.droppedRedo;
      ++n;
      e = next;
    }
    return n;
  }
  /// The slot is at the floor eviction raised: its oldest kept state, with no empty slot below.
  bool atForgottenFloor(uint8_t slot) const {
    const Slot& s = slots_[slot];
    return s.current != kNone && entries_[s.current].older == kNone && !s.emptyBelow;
  }
  void lose(uint8_t slot, uint8_t n) {
    const uint32_t sum = static_cast<uint32_t>(lost_[slot]) + n;
    lost_[slot] = static_cast<uint16_t>(sum > 0xFFFFu ? 0xFFFFu : sum);
    stats_.lastLostSlot = slot;
  }

  bool protectedStart(uint8_t e) const {
    for (const Session& x : sessions_)
      if (x.slot != kNone && x.base == e) return true;
    return false;
  }
  bool emptyStartProtected(uint8_t slot) const {
    for (const Session& x : sessions_)
      if (x.slot == slot && x.base == kNone) return true;
    return false;
  }

  /// The oldest step in the set: the oldest-committed state that is no slot's current and no open session's protected
  /// start. Per slot that is its oldest state (skipping a protected start) if it lies below current, else the start of
  /// its redo chain. Lowest commit seq wins.
  void evictOldest() {
    uint8_t bestSlot = kNone, best = kNone;
    for (uint8_t i = 0; i < Slots; ++i) {
      const Slot& s = slots_[i];
      uint8_t cand = s.oldest;
      if (cand != kNone && protectedStart(cand)) cand = entries_[cand].newer;
      if (cand == s.current) cand = s.current == kNone ? kNone : entries_[s.current].newer;
      if (cand != kNone && (best == kNone || entries_[cand].seq < entries_[best].seq)) {
        bestSlot = i;
        best = cand;
      }
    }
    Slot& s = slots_[bestSlot];
    const bool undoSide = s.current != kNone && entries_[best].seq < entries_[s.current].seq;
    if (!undoSide) {
      lose(bestSlot, dropRedo(bestSlot));  // a redo chain cannot have a hole
      ++stats_.evictedRedo;
      return;
    }
    const uint8_t older = entries_[best].older, newer = entries_[best].newer;
    entries_[newer].older = older;
    if (older == kNone) {
      s.oldest = newer;
      if (!emptyStartProtected(bestSlot)) s.emptyBelow = false;  // the floor moves up
    } else {
      entries_[older].newer = newer;  // a session's oldest step above its protected start
    }
    freeEntry(best);
    ++stats_.evictedUndo;
    lose(bestSlot, 1);
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
  uint16_t lost_[Slots] = {};  // per slot: steps lost to eviction (E4.7)
};

}  // namespace ContentEngine
