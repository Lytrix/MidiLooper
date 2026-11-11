//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// fixed-memory-engine E4: undo history and slot currents (owner decisions 2026-11-05). Undo / redo are per slot; at
// most 99 states are held across all slots; a commit drops its slot's redo branch and, when full, evicts the oldest
// step in the set (oldest-committed non-current state; a redo state takes its whole branch with it). Recording is never
// refused for history. Dropped states release their manifests, so their runs reach the reclaim queue. Overdub session
// (DEC-038, E4.2): wraps are commits, undo on the session slot stops at the session start, and Stop folds the session
// into one step. A random sequence (with sessions) is checked against an independent reference model written from
// those rules. NOTE_EDIT session (E4.3; owner: limit 99, shared): the same bound and fold, plus abandon; an overdub and a
// NOTE_EDIT session can be open at once on different slots.

#include <unity.h>

#include <cstdint>
#include <cstdlib>
#include <new>
#include <vector>

#include "ContentEngine/EngineCore.h"
#include "ContentEngine/History/SlotUndoHistory.h"

static unsigned long gHeapAllocations = 0;
static bool gTrap = false;
void* operator new(std::size_t size) {
  if (gTrap) ++gHeapAllocations;
  if (void* p = std::malloc(size == 0 ? 1 : size)) return p;
  throw std::bad_alloc();
}
void* operator new[](std::size_t size) { return operator new(size); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }

using namespace ContentEngine;

using Pool = PagePool<512, 8>;
using Runs = RunTable<160, Pool, 4, 4>;
using Manifests = ManifestTable<160, 4>;
using ProdHistory = History<Manifests, Runs>;               // 64 slots, depth 99
using SmallHistory = History<Manifests, Runs, 8, 20>;       // eviction reached quickly

static Pool gPool;
static Runs gRuns;
static Manifests gManifests;
static ProdHistory gH;
static SmallHistory gSmall;

static void setup() {
  gPool.init();
  gRuns.init();
  gManifests.init();
  gH.init(gManifests, gRuns);
  gSmall.init(gManifests, gRuns);
}

/// A committed take: one sealed run of one row, listed by a new manifest that holds the only references.
static ManifestId take(uint8_t slot) {
  const RunId r = gRuns.allocate(slot, 0);
  TEST_ASSERT_NOT_EQUAL(kNoRun, r);
  PassEvent e{};
  e.status = 0x90;
  e.data1 = 60;
  e.data2 = 100;
  TEST_ASSERT_EQUAL(AppendResult::Ok, gRuns.append(r, e, gPool));
  gRuns.seal(r);
  const ManifestId m = gManifests.create(slot, 1, 768, &r, 1, gRuns);
  TEST_ASSERT_NOT_EQUAL(kNoManifest, m);
  gRuns.release(r);  // the writer's reference; the manifest keeps the run
  return m;
}

static void drainReclaim() {
  while (gRuns.reclaimPendingRuns() != 0) gRuns.reclaim(gPool, 64);
}

// Record, two overdubs; undo back to the empty slot and redo forward; a commit after undo drops the redo branch.
void test_undo_redo_and_redo_drop() {
  setup();
  const ManifestId a = take(0), b = take(0), c = take(0);
  gH.commit(0, a);
  gH.commit(0, b);
  gH.commit(0, c);
  TEST_ASSERT_EQUAL_UINT16(c, gH.current(0));
  TEST_ASSERT_EQUAL_UINT8(3, gH.undoDepth(0));
  TEST_ASSERT_TRUE(gH.undo(0));
  TEST_ASSERT_EQUAL_UINT16(b, gH.current(0));
  TEST_ASSERT_TRUE(gH.undo(0));
  TEST_ASSERT_TRUE(gH.undo(0));
  TEST_ASSERT_EQUAL_UINT16(kNoManifest, gH.current(0));  // the empty slot
  TEST_ASSERT_FALSE(gH.undo(0));
  TEST_ASSERT_EQUAL_UINT32(0, gH.stats().undoPastRetained);  // the empty slot is the bottom, nothing forgotten
  TEST_ASSERT_EQUAL_UINT8(3, gH.stats().highWater);
  TEST_ASSERT_EQUAL_UINT8(ProdHistory::kNone, gH.stats().lastLostSlot);
  TEST_ASSERT_EQUAL_UINT8(3, gH.redoDepth(0));
  TEST_ASSERT_TRUE(gH.redo(0));
  TEST_ASSERT_EQUAL_UINT16(a, gH.current(0));
  TEST_ASSERT_TRUE(gH.redo(0));
  TEST_ASSERT_EQUAL_UINT16(b, gH.current(0));
  const uint16_t freeBefore = gManifests.freeCount();
  const ManifestId d = take(0);
  TEST_ASSERT_EQUAL(HistoryCommit::Committed, gH.commit(0, d));  // drops c
  TEST_ASSERT_EQUAL_UINT16(freeBefore, gManifests.freeCount());  // d taken, c freed
  TEST_ASSERT_FALSE(gH.canRedo(0));
  TEST_ASSERT_EQUAL_UINT8(3, gH.undoDepth(0));  // a, b, d
  TEST_ASSERT_EQUAL_UINT16(1, gRuns.reclaimPendingRuns());  // c's run
  TEST_ASSERT_EQUAL_UINT32(1, gH.stats().droppedRedo);
  TEST_ASSERT_EQUAL_UINT16(0, gH.lostSteps(0));  // a redo dropped by a commit is not lost to pressure
  TEST_ASSERT_TRUE(checkConservation(gPool, gRuns, gManifests));
  drainReclaim();
  TEST_ASSERT_TRUE(checkConservation(gPool, gRuns, gManifests));
}

// Undo on one slot never moves another.
void test_slots_are_independent() {
  setup();
  const ManifestId a0 = take(0), a1 = take(1), b0 = take(0), b1 = take(1);
  gH.commit(0, a0);
  gH.commit(1, a1);
  gH.commit(0, b0);
  gH.commit(1, b1);
  TEST_ASSERT_TRUE(gH.undo(1));
  TEST_ASSERT_TRUE(gH.undo(1));
  TEST_ASSERT_EQUAL_UINT16(kNoManifest, gH.current(1));
  TEST_ASSERT_EQUAL_UINT16(b0, gH.current(0));
  TEST_ASSERT_EQUAL_UINT8(2, gH.undoDepth(0));
  TEST_ASSERT_FALSE(gH.canUndo(2));
  TEST_ASSERT_FALSE(gH.canRedo(2));
  const ManifestId c0 = take(0);
  gH.commit(0, c0);  // slot 1's redo survives a commit on slot 0
  TEST_ASSERT_EQUAL_UINT8(2, gH.redoDepth(1));
}

// 99 states on one slot, then one more: its oldest undo state is forgotten and the empty slot is no longer reachable.
void test_full_history_forgets_the_oldest_step() {
  setup();
  ManifestId first = kNoManifest;
  for (uint8_t i = 0; i < 99; ++i) {
    const ManifestId m = take(0);
    if (i == 0) first = m;
    TEST_ASSERT_EQUAL(HistoryCommit::Committed, gH.commit(0, m));
  }
  TEST_ASSERT_EQUAL_UINT8(99, gH.heldStates());
  TEST_ASSERT_EQUAL_UINT8(99, gH.undoDepth(0));
  const ManifestId m = take(0);
  TEST_ASSERT_EQUAL(HistoryCommit::CommittedEvicted, gH.commit(0, m));
  TEST_ASSERT_EQUAL_UINT8(99, gH.heldStates());
  TEST_ASSERT_EQUAL_UINT8(98, gH.undoDepth(0));  // 99 states, floor = the oldest kept
  TEST_ASSERT_FALSE(gManifests.manifest(first).live);
  TEST_ASSERT_EQUAL_UINT32(1, gH.stats().evictedUndo);
  // Pressure (E4.7): the table peaked at 99, slot 0 lost one step.
  TEST_ASSERT_EQUAL_UINT8(99, gH.stats().highWater);
  TEST_ASSERT_EQUAL_UINT16(1, gH.lostSteps(0));
  TEST_ASSERT_EQUAL_UINT8(0, gH.stats().lastLostSlot);
  TEST_ASSERT_EQUAL_UINT32(0, gH.stats().evictedRedo);
  for (uint8_t i = 0; i < 98; ++i) TEST_ASSERT_TRUE(gH.undo(0));
  TEST_ASSERT_EQUAL_UINT32(0, gH.stats().undoPastRetained);
  TEST_ASSERT_FALSE(gH.undo(0));
  TEST_ASSERT_NOT_EQUAL(kNoManifest, gH.current(0));  // the floor: never the empty slot
  TEST_ASSERT_EQUAL_UINT32(1, gH.stats().undoPastRetained);  // refused because a step was forgotten
  TEST_ASSERT_EQUAL_UINT8(0, gH.stats().lastPastSlot);
  TEST_ASSERT_TRUE(checkConservation(gPool, gRuns, gManifests));
}

// The oldest step in the set may be another slot's redo branch: it goes whole, and that slot keeps its current.
void test_eviction_takes_the_oldest_step_across_slots() {
  setup();
  const ManifestId r1 = take(1), r2 = take(1);
  gH.commit(1, r1);
  gH.commit(1, r2);
  TEST_ASSERT_TRUE(gH.undo(1));
  TEST_ASSERT_TRUE(gH.undo(1));  // slot 1 empty, r1 and r2 are redo (oldest commits in the set)
  for (uint8_t i = 0; i < 97; ++i) gH.commit(0, take(0));
  TEST_ASSERT_EQUAL_UINT8(99, gH.heldStates());
  TEST_ASSERT_EQUAL(HistoryCommit::CommittedEvicted, gH.commit(0, take(0)));
  TEST_ASSERT_FALSE(gH.canRedo(1));  // slot 1's whole redo branch went
  TEST_ASSERT_TRUE(gH.canUndo(0) && gH.undoDepth(0) == 98);  // slot 0 kept every step (98 states + empty below)
  TEST_ASSERT_EQUAL_UINT8(98, gH.heldStates());
  TEST_ASSERT_EQUAL_UINT32(2, gH.stats().droppedRedo);
  TEST_ASSERT_EQUAL_UINT32(1, gH.stats().evictedRedo);  // one branch, two steps, lost by slot 1
  TEST_ASSERT_EQUAL_UINT16(2, gH.lostSteps(1));
  TEST_ASSERT_EQUAL_UINT16(0, gH.lostSteps(0));
  TEST_ASSERT_EQUAL_UINT8(1, gH.stats().lastLostSlot);
  TEST_ASSERT_FALSE(gManifests.manifest(r1).live);
  TEST_ASSERT_FALSE(gManifests.manifest(r2).live);
}

// ---- E4.2 overdub session (DEC-038) -------------------------------------------------------------------------------------

// Wraps are steps while overdubbing (O:), undo stops at the session start, and after Stop one U: undoes the session.
void test_session_undo_is_bounded_and_stop_makes_one_step() {
  setup();
  const ManifestId rec = take(0);
  gH.commit(0, rec);
  TEST_ASSERT_TRUE(gH.beginSession(SessionKind::Overdub, 0));
  TEST_ASSERT_FALSE(gH.beginSession(SessionKind::Overdub, 1));  // one writer
  const ManifestId w1 = take(0), w2 = take(0), w3 = take(0);
  gH.commit(0, w1);
  gH.commit(0, w2);
  gH.commit(0, w3);
  TEST_ASSERT_EQUAL_UINT8(3, gH.sessionDepth(SessionKind::Overdub));
  TEST_ASSERT_TRUE(gH.undo(0));
  TEST_ASSERT_TRUE(gH.undo(0));
  TEST_ASSERT_TRUE(gH.undo(0));
  TEST_ASSERT_EQUAL_UINT16(rec, gH.current(0));
  TEST_ASSERT_FALSE(gH.undo(0));  // not below the session start
  TEST_ASSERT_EQUAL_UINT32(0, gH.stats().undoPastRetained);  // a session bound, not forgotten history
  TEST_ASSERT_EQUAL_UINT8(0, gH.sessionDepth(SessionKind::Overdub));
  TEST_ASSERT_TRUE(gH.redo(0));
  TEST_ASSERT_TRUE(gH.redo(0));  // w2 current, w3 redo
  gH.endSession(SessionKind::Overdub);
  TEST_ASSERT_FALSE(gH.inSession(SessionKind::Overdub));
  TEST_ASSERT_EQUAL_UINT16(w2, gH.current(0));
  TEST_ASSERT_FALSE(gManifests.manifest(w1).live);  // folded
  TEST_ASSERT_FALSE(gManifests.manifest(w3).live);  // session redo dropped
  TEST_ASSERT_EQUAL_UINT8(2, gH.undoDepth(0));      // record, session
  TEST_ASSERT_TRUE(gH.undo(0));
  TEST_ASSERT_EQUAL_UINT16(rec, gH.current(0));      // one step undoes the whole session
  TEST_ASSERT_TRUE(gH.redo(0));
  TEST_ASSERT_EQUAL_UINT16(w2, gH.current(0));
  TEST_ASSERT_EQUAL_UINT32(1, gH.stats().collapsedWraps);
  TEST_ASSERT_TRUE(checkConservation(gPool, gRuns, gManifests));
}

// Every wrap undone at Stop: the session leaves no step, and the slot keeps its pre-session redo-free state.
void test_session_fully_undone_leaves_no_step() {
  setup();
  const ManifestId rec = take(0);
  gH.commit(0, rec);
  gH.beginSession(SessionKind::Overdub, 0);
  const ManifestId w1 = take(0), w2 = take(0);
  gH.commit(0, w1);
  gH.commit(0, w2);
  gH.undo(0);
  gH.undo(0);
  gH.endSession(SessionKind::Overdub);
  TEST_ASSERT_EQUAL_UINT16(rec, gH.current(0));
  TEST_ASSERT_FALSE(gH.canRedo(0));
  TEST_ASSERT_EQUAL_UINT8(1, gH.heldStates());
  TEST_ASSERT_FALSE(gManifests.manifest(w1).live);
  TEST_ASSERT_FALSE(gManifests.manifest(w2).live);
}

// Partial undo then a new wrap drops the session's redo; other slots keep their own undo during the session.
void test_session_new_wrap_after_undo_and_other_slots() {
  setup();
  gH.commit(0, take(0));
  const ManifestId o1 = take(1), o2 = take(1);
  gH.commit(1, o1);
  gH.commit(1, o2);
  gH.beginSession(SessionKind::Overdub, 0);
  const ManifestId w1 = take(0), w2 = take(0), w3 = take(0);
  gH.commit(0, w1);
  gH.commit(0, w2);
  gH.undo(0);
  gH.commit(0, w3);  // drops w2
  TEST_ASSERT_FALSE(gManifests.manifest(w2).live);
  TEST_ASSERT_EQUAL_UINT8(2, gH.sessionDepth(SessionKind::Overdub));
  TEST_ASSERT_TRUE(gH.undo(1));  // slot 1 is not in the session
  TEST_ASSERT_TRUE(gH.undo(1));
  TEST_ASSERT_EQUAL_UINT16(kNoManifest, gH.current(1));
  gH.endSession(SessionKind::Overdub);
  TEST_ASSERT_EQUAL_UINT16(w3, gH.current(0));
  TEST_ASSERT_EQUAL_UINT8(2, gH.undoDepth(0));
  TEST_ASSERT_EQUAL_UINT8(2, gH.redoDepth(1));
}

// A session longer than the history forgets its own oldest wraps, never its start: after Stop one U: still returns to
// the pre-overdub state (owner 2026-11-05: the session start is the restore point).
void test_long_session_shares_the_99() {
  setup();
  const ManifestId rec = take(0);
  gH.commit(0, rec);
  gH.beginSession(SessionKind::Overdub, 0);
  ManifestId last = kNoManifest;
  for (uint32_t i = 0; i < 150; ++i) {
    drainReclaim();
    last = take(0);
    gH.commit(0, last);
    TEST_ASSERT_TRUE(gH.heldStates() <= 99);
  }
  TEST_ASSERT_EQUAL_UINT8(98, gH.sessionDepth(SessionKind::Overdub));  // 99 held: the start + 98 wraps
  uint32_t undos = 0;
  while (gH.undo(0)) TEST_ASSERT_TRUE(++undos < 200);
  TEST_ASSERT_EQUAL_UINT32(98, undos);
  TEST_ASSERT_EQUAL_UINT16(rec, gH.current(0));  // the oldest kept wrap steps straight back to the start
  for (uint32_t k = 0; gH.redo(0); ++k) TEST_ASSERT_TRUE(k < 200);
  gH.endSession(SessionKind::Overdub);
  TEST_ASSERT_EQUAL_UINT16(last, gH.current(0));
  TEST_ASSERT_EQUAL_UINT8(2, gH.heldStates());
  TEST_ASSERT_TRUE(gH.undo(0));
  TEST_ASSERT_EQUAL_UINT16(rec, gH.current(0));
  drainReclaim();
  TEST_ASSERT_TRUE(checkConservation(gPool, gRuns, gManifests));
}

// ---- E4.3 NOTE_EDIT session ---------------------------------------------------------------------------------------------

// Edit actions are E: steps; leaving NOTE_EDIT folds them into one step; abandoning returns to the session start.
void test_edit_session_fold_and_abandon() {
  setup();
  const ManifestId rec = take(0);
  gH.commit(0, rec);
  TEST_ASSERT_TRUE(gH.beginSession(SessionKind::Edit, 0));
  const ManifestId e1 = take(0), e2 = take(0), e3 = take(0);
  gH.commit(0, e1);
  gH.commit(0, e2);
  gH.commit(0, e3);
  TEST_ASSERT_EQUAL_UINT8(3, gH.sessionDepth(SessionKind::Edit));
  TEST_ASSERT_EQUAL_UINT8(0, gH.sessionDepth(SessionKind::Overdub));
  TEST_ASSERT_TRUE(gH.undo(0));
  gH.endSession(SessionKind::Edit);
  TEST_ASSERT_EQUAL_UINT16(e2, gH.current(0));
  TEST_ASSERT_EQUAL_UINT8(2, gH.undoDepth(0));  // record, the edit session
  TEST_ASSERT_FALSE(gManifests.manifest(e1).live);
  TEST_ASSERT_FALSE(gManifests.manifest(e3).live);
  // A second session, abandoned: back to e2, both edits released.
  TEST_ASSERT_TRUE(gH.beginSession(SessionKind::Edit, 0));
  const ManifestId f1 = take(0), f2 = take(0);
  gH.commit(0, f1);
  gH.commit(0, f2);
  gH.abandonSession(SessionKind::Edit);
  TEST_ASSERT_FALSE(gH.inSession(SessionKind::Edit));
  TEST_ASSERT_EQUAL_UINT16(e2, gH.current(0));
  TEST_ASSERT_FALSE(gH.canRedo(0));
  TEST_ASSERT_FALSE(gManifests.manifest(f1).live);
  TEST_ASSERT_FALSE(gManifests.manifest(f2).live);
  TEST_ASSERT_TRUE(checkConservation(gPool, gRuns, gManifests));
}

// One overdub and one NOTE_EDIT session at once, on different slots; never two sessions on one slot.
void test_overdub_and_edit_sessions_together() {
  setup();
  gH.commit(0, take(0));
  gH.commit(1, take(1));
  TEST_ASSERT_TRUE(gH.beginSession(SessionKind::Overdub, 0));
  TEST_ASSERT_FALSE(gH.beginSession(SessionKind::Edit, 0));  // same slot
  TEST_ASSERT_TRUE(gH.beginSession(SessionKind::Edit, 1));
  TEST_ASSERT_FALSE(gH.beginSession(SessionKind::Edit, 2));  // one NOTE_EDIT session
  gH.commit(0, take(0));
  gH.commit(1, take(1));
  gH.commit(1, take(1));
  TEST_ASSERT_EQUAL_UINT8(1, gH.sessionDepth(SessionKind::Overdub));
  TEST_ASSERT_EQUAL_UINT8(2, gH.sessionDepth(SessionKind::Edit));
  TEST_ASSERT_TRUE(gH.undo(0));
  TEST_ASSERT_FALSE(gH.undo(0));  // overdub start
  TEST_ASSERT_TRUE(gH.undo(1));
  TEST_ASSERT_TRUE(gH.undo(1));
  TEST_ASSERT_FALSE(gH.undo(1));  // edit start
  gH.endSession(SessionKind::Overdub);
  gH.endSession(SessionKind::Edit);
  TEST_ASSERT_EQUAL_UINT8(1, gH.undoDepth(0));  // both sessions fully undone: no step left
  TEST_ASSERT_EQUAL_UINT8(1, gH.undoDepth(1));
  TEST_ASSERT_EQUAL_UINT8(2, gH.heldStates());
}

// Redo from before a session is not a session step: unreachable while it is open, kept by an empty abandon.
void test_session_leaves_earlier_redo_alone() {
  setup();
  const ManifestId a = take(0), b = take(0);
  gH.commit(0, a);
  gH.commit(0, b);
  gH.undo(0);
  TEST_ASSERT_TRUE(gH.beginSession(SessionKind::Edit, 0));
  TEST_ASSERT_FALSE(gH.redo(0));
  TEST_ASSERT_FALSE(gH.undo(0));
  TEST_ASSERT_EQUAL_UINT8(0, gH.sessionDepth(SessionKind::Edit));
  gH.abandonSession(SessionKind::Edit);
  TEST_ASSERT_EQUAL_UINT16(a, gH.current(0));
  TEST_ASSERT_TRUE(gH.redo(0));
  TEST_ASSERT_EQUAL_UINT16(b, gH.current(0));
}

// A NOTE_EDIT session runs past 99 edits: it forgets its own oldest edits, never the pre-edit state, so abandon
// always restores the loop as it was before NOTE_EDIT (owner 2026-11-05).
void test_long_edit_session_abandon_restores_the_start() {
  setup();
  const ManifestId before = take(0);
  gH.commit(0, take(0));
  gH.commit(0, before);
  TEST_ASSERT_TRUE(gH.beginSession(SessionKind::Edit, 0));
  for (uint32_t i = 0; i < 120; ++i) {
    drainReclaim();
    gH.commit(0, take(0));
  }
  TEST_ASSERT_TRUE(gManifests.manifest(before).live);
  TEST_ASSERT_EQUAL_UINT8(98, gH.sessionDepth(SessionKind::Edit));  // 99 held: the protected start + 98 edits
  gH.abandonSession(SessionKind::Edit);
  TEST_ASSERT_EQUAL_UINT16(before, gH.current(0));
  TEST_ASSERT_FALSE(gH.canRedo(0));
  TEST_ASSERT_EQUAL_UINT8(1, gH.heldStates());  // the first take was the oldest unprotected step: forgotten first
  TEST_ASSERT_FALSE(gH.canUndo(0));
  drainReclaim();
  TEST_ASSERT_TRUE(checkConservation(gPool, gRuns, gManifests));
}

// A session started on the empty slot keeps the empty slot reachable however long it runs.
void test_session_on_empty_slot_keeps_empty_reachable() {
  setup();
  gH.beginSession(SessionKind::Overdub, 3);
  for (uint32_t i = 0; i < 130; ++i) {
    drainReclaim();
    gH.commit(3, take(3));
  }
  uint32_t undos = 0;
  while (gH.undo(3)) TEST_ASSERT_TRUE(++undos < 200);
  TEST_ASSERT_EQUAL_UINT32(99, undos);
  TEST_ASSERT_EQUAL_UINT16(kNoManifest, gH.current(3));
  for (uint32_t k = 0; gH.redo(3); ++k) TEST_ASSERT_TRUE(k < 200);
  gH.abandonSession(SessionKind::Overdub);
  TEST_ASSERT_EQUAL_UINT16(kNoManifest, gH.current(3));
  TEST_ASSERT_EQUAL_UINT8(0, gH.heldStates());
}

// ---- Independent reference model: per slot a list of (manifest, commit seq) and a cursor ------------------------------
struct RefState {
  ManifestId m;
  uint32_t seq;
};
struct RefSlot {
  std::vector<RefState> states;
  int cur = -1;  // -1: below states[0]
  bool emptyBelow = true;
};
struct Ref {
  std::vector<RefSlot> slots;
  uint32_t seq = 0;
  size_t depth;
  std::vector<ManifestId> released;
  int sessSlot[2] = {-1, -1};
  uint32_t sessStart[2] = {0, 0};
  ManifestId sessBase[2] = {kNoManifest, kNoManifest};  // the protected start state (kNoManifest: the empty slot)
  bool isProtected(size_t slot, const RefState& st) const {
    for (int k = 0; k < 2; ++k)
      if (sessSlot[k] == static_cast<int>(slot) && sessBase[k] == st.m) return true;
    return false;
  }
  bool emptyProtected(size_t slot) const {
    for (int k = 0; k < 2; ++k)
      if (sessSlot[k] == static_cast<int>(slot) && sessBase[k] == kNoManifest) return true;
    return false;
  }
  Ref(size_t s, size_t d) : slots(s), depth(d) {}
  size_t held() const {
    size_t n = 0;
    for (auto& s : slots) n += s.states.size();
    return n;
  }
  void dropAbove(RefSlot& s) {
    for (size_t i = static_cast<size_t>(s.cur + 1); i < s.states.size(); ++i) released.push_back(s.states[i].m);
    s.states.resize(static_cast<size_t>(s.cur + 1));
    if (s.cur < 0) s.emptyBelow = true;
  }
  void commit(size_t slot, ManifestId m) {
    dropAbove(slots[slot]);
    if (held() == depth) {
      // The oldest-committed state in the set that is no slot's current.
      size_t bs = 0, bi = 0;
      bool found = false;
      for (size_t k = 0; k < slots.size(); ++k)
        for (size_t i = 0; i < slots[k].states.size(); ++i)
          if (static_cast<int>(i) != slots[k].cur && !isProtected(k, slots[k].states[i]) &&
              (!found || slots[k].states[i].seq < slots[bs].states[bi].seq)) {
            bs = k;
            bi = i;
            found = true;
          }
      TEST_ASSERT_TRUE(found);
      RefSlot& s = slots[bs];
      if (static_cast<int>(bi) < s.cur) {  // an undo state: the floor moves up
        released.push_back(s.states[bi].m);
        s.states.erase(s.states.begin() + static_cast<long>(bi));
        --s.cur;
        if (bi == 0 && !emptyProtected(bs)) s.emptyBelow = false;
      } else {  // a redo state: the whole branch above current goes
        dropAbove(s);
      }
    }
    slots[slot].states.push_back({m, ++seq});
    slots[slot].cur = static_cast<int>(slots[slot].states.size()) - 1;
  }
  bool begin(int kind, size_t slot) {
    if (sessSlot[kind] >= 0 || sessSlot[1 - kind] == static_cast<int>(slot)) return false;
    sessSlot[kind] = static_cast<int>(slot);
    sessStart[kind] = seq;
    sessBase[kind] = current(slot);
    return true;
  }
  void end(int kind) {
    if (sessSlot[kind] < 0) return;
    RefSlot& s = slots[static_cast<size_t>(sessSlot[kind])];
    sessSlot[kind] = -1;
    dropAbove(s);
    if (s.cur < 0 || s.states[static_cast<size_t>(s.cur)].seq <= sessStart[kind]) return;
    while (s.cur > 0 && s.states[static_cast<size_t>(s.cur - 1)].seq > sessStart[kind]) {  // fold into its current
      released.push_back(s.states[static_cast<size_t>(s.cur - 1)].m);
      s.states.erase(s.states.begin() + (s.cur - 1));
      --s.cur;
    }
  }
  void abandon(int kind) {
    if (sessSlot[kind] < 0) return;
    RefSlot& s = slots[static_cast<size_t>(sessSlot[kind])];
    sessSlot[kind] = -1;
    bool removed = false;
    while (!s.states.empty() && s.states.back().seq > sessStart[kind]) {
      released.push_back(s.states.back().m);
      s.states.pop_back();
      removed = true;
    }
    if (!removed) return;
    s.cur = static_cast<int>(s.states.size()) - 1;
    TEST_ASSERT_EQUAL_UINT16(sessBase[kind], current(static_cast<size_t>(&s - &slots[0])));  // back at the start
  }
  bool bounded(size_t slot, int idx) const {  // state idx of slot is from before an open session on it
    for (int k = 0; k < 2; ++k)
      if (sessSlot[k] == static_cast<int>(slot) && idx >= 0 &&
          slots[slot].states[static_cast<size_t>(idx)].seq <= sessStart[k])
        return true;
    return false;
  }
  bool undo(size_t slot) {
    RefSlot& s = slots[slot];
    if (bounded(slot, s.cur)) return false;
    if (s.cur > 0 || (s.cur == 0 && s.emptyBelow)) {
      --s.cur;
      return true;
    }
    return false;
  }
  bool redo(size_t slot) {
    RefSlot& s = slots[slot];
    if (s.cur + 1 < static_cast<int>(s.states.size()) && !bounded(slot, s.cur + 1)) {
      ++s.cur;
      return true;
    }
    return false;
  }
  ManifestId current(size_t slot) const {
    const RefSlot& s = slots[slot];
    return s.cur < 0 ? kNoManifest : s.states[static_cast<size_t>(s.cur)].m;
  }
};

template <typename H>
static void randomAgainstReference(H& h, uint8_t slots, uint8_t depth, uint32_t steps, uint32_t seed) {
  Ref ref(slots, depth);
  uint32_t x = seed;
  for (uint32_t step = 0; step < steps; ++step) {
    x = x * 1103515245u + 12345u;
    const uint8_t slot = static_cast<uint8_t>((x >> 8) % slots);
    const uint32_t op = (x >> 20) % 13;
    const int kind = static_cast<int>((x >> 4) & 1);
    const SessionKind k = kind == 0 ? SessionKind::Overdub : SessionKind::Edit;
    if (op == 10) {
      TEST_ASSERT_EQUAL(ref.begin(kind, slot), h.beginSession(k, slot));
    } else if (op == 11) {
      ref.end(kind);
      h.endSession(k);
    } else if (op == 12) {
      ref.abandon(kind);
      h.abandonSession(k);
    } else if (op < 4) {
      drainReclaim();
      const ManifestId m = take(slot);
      h.commit(slot, m);
      ref.commit(slot, m);
    } else if (op < 7) {
      TEST_ASSERT_EQUAL(ref.undo(slot), h.undo(slot));
    } else {
      TEST_ASSERT_EQUAL(ref.redo(slot), h.redo(slot));
    }
    for (uint8_t k = 0; k < slots; ++k) TEST_ASSERT_EQUAL_UINT16(ref.current(k), h.current(k));
    for (int kk = 0; kk < 2; ++kk) {
      size_t n = 0;
      if (ref.sessSlot[kk] >= 0) {
        const RefSlot& ss = ref.slots[static_cast<size_t>(ref.sessSlot[kk])];
        for (int i = ss.cur; i >= 0 && ss.states[static_cast<size_t>(i)].seq > ref.sessStart[kk]; --i) ++n;
      }
      TEST_ASSERT_EQUAL_UINT8(n, h.sessionDepth(kk == 0 ? SessionKind::Overdub : SessionKind::Edit));
    }
    TEST_ASSERT_EQUAL_UINT32(ref.held(), h.heldStates());
    for (const ManifestId m : ref.released) TEST_ASSERT_FALSE(gManifests.manifest(m).live);
    ref.released.clear();
    if (step % 257 == 0) {
      for (uint8_t k = 0; k < slots; ++k) {
        const RefSlot& s = ref.slots[k];
        TEST_ASSERT_EQUAL_UINT8(s.cur < 0 ? 0 : s.cur + (s.emptyBelow ? 1 : 0), h.undoDepth(k));
        TEST_ASSERT_EQUAL_UINT8(static_cast<int>(s.states.size()) - 1 - s.cur, h.redoDepth(k));
      }
      TEST_ASSERT_EQUAL_UINT16(Manifests::kManifestCount - ref.held(), gManifests.freeCount());
      TEST_ASSERT_TRUE(checkConservation(gPool, gRuns, gManifests));
    }
  }
}

void test_random_sequences_match_the_reference_model_small() {
  setup();
  randomAgainstReference(gSmall, 8, 20, 60000, 7);
}

void test_random_sequences_match_the_reference_model_production() {
  setup();
  randomAgainstReference(gH, 64, 99, 60000, 99);
}


// ---- DEC-254 (E4.6c): the cold tier ----------------------------------------------------------------------------------
struct FakeCold : ColdSink {
  uint8_t n[64] = {};
  bool accept = true;
  uint32_t demotes = 0;
  ManifestId last = kNoManifest;
  bool demote(uint8_t s, ManifestId m) override {
    if (!accept) return false;
    ++n[s];
    ++demotes;
    last = m;
    return true;
  }
  uint8_t coldCount(uint8_t s) const override { return n[s]; }
  void dropOldestCold(uint8_t s) override {
    if (n[s] != 0) --n[s];
  }
  void clearCold(uint8_t s) override { n[s] = 0; }
  uint8_t fetchSlot = 0xFF;
  uint32_t fetches = 0;
  void requestFetch(uint8_t s) override {
    fetchSlot = s;
    ++fetches;
  }
};

// A full table demotes the oldest undo step instead of forgetting it: the slot's UNDO level is unchanged; undo stops at
// the oldest hot state (the fetch is E4.6d). Refused (not on the card), it is forgotten as before.
void test_a_full_table_demotes_the_oldest_step_cold() {
  setup();
  FakeCold cold;
  gH.attachCold(&cold);
  ManifestId first = kNoManifest;
  for (uint8_t i = 0; i < 60; ++i) {
    const ManifestId m = take(0);
    if (i == 0) first = m;
    gH.commit(0, m);
  }
  for (uint8_t i = 0; i < 39; ++i) gH.commit(1, take(1));
  TEST_ASSERT_EQUAL_UINT8(99, gH.heldStates());
  const uint8_t depth0 = gH.undoDepth(0);
  TEST_ASSERT_EQUAL_UINT8(60, depth0);  // 59 steps down to the first take, and the empty slot below it
  gH.commit(1, take(1));
  TEST_ASSERT_EQUAL_UINT32(1, gH.stats().demoted);
  TEST_ASSERT_EQUAL_UINT32(0, gH.stats().evictedUndo);
  TEST_ASSERT_EQUAL_UINT16(first, cold.last);  // slot 0's oldest
  TEST_ASSERT_EQUAL_UINT8(1, cold.n[0]);
  TEST_ASSERT_EQUAL_UINT8(depth0, gH.undoDepth(0));  // kept: one hot step fewer, one cold more
  // Not on the card: forgotten, as before the cold tier (and the slot's floor rises).
  cold.accept = false;
  gH.commit(1, take(1));
  TEST_ASSERT_EQUAL_UINT32(1, gH.stats().demoted);
  TEST_ASSERT_EQUAL_UINT32(1, gH.stats().evictedUndo);
  TEST_ASSERT_EQUAL_UINT8(depth0 - 2, gH.undoDepth(0));  // that step, and the empty slot below (the floor rule)
  cold.accept = true;
  for (uint8_t i = 0; i < 57; ++i) TEST_ASSERT_TRUE(gH.undo(0));
  TEST_ASSERT_FALSE(gH.undo(0));  // the oldest hot state: the cold one below waits for E4.6d
  TEST_ASSERT_EQUAL_UINT32(1, gH.stats().undoAtCold);
  TEST_ASSERT_TRUE(gH.undoDepth(0) >= 1);  // the cold step still counted
  gH.clearSlot(0);
  TEST_ASSERT_EQUAL_UINT8(0, cold.n[0]);  // clearing a slot clears its cold states too
}

// Per-slot depth: a slot keeps 99 states, hot and cold; its 100th forgets its own oldest — no other slot loses a step.
void test_a_slot_keeps_99_and_forgets_only_its_own() {
  setup();
  FakeCold cold;
  gH.attachCold(&cold);
  gH.commit(5, take(5));
  gH.commit(5, take(5));
  const uint8_t other = gH.undoDepth(5);
  for (uint16_t i = 0; i < 140; ++i) {
    gH.commit(0, take(0));
    drainReclaim();
  }
  TEST_ASSERT_EQUAL_UINT8(other, gH.undoDepth(5));  // the quiet slot kept both its steps
  TEST_ASSERT_EQUAL_UINT8(2, gH.undoDepth(5));
  TEST_ASSERT_TRUE(gH.stats().slotDepth > 0);
  TEST_ASSERT_EQUAL_UINT8(98, gH.undoDepth(0));  // 99 states: 98 steps below the current (no empty slot left)
  TEST_ASSERT_TRUE(cold.n[0] > 0);
  TEST_ASSERT_EQUAL_UINT32(0, gH.stats().evictedUndo);  // nothing forgotten for room in the table
}

// Undo stops at the oldest hot state when a cold one lies below — never jumps over it to the empty slot.
void test_undo_stops_above_a_cold_state() {
  setup();
  FakeCold cold;
  gH.attachCold(&cold);
  for (uint8_t i = 0; i < 3; ++i) gH.commit(0, take(0));
  for (uint8_t i = 0; i < 96; ++i) gH.commit(1, take(1));
  gH.commit(1, take(1));  // full: slot 0's oldest goes cold
  TEST_ASSERT_EQUAL_UINT8(1, cold.n[0]);
  TEST_ASSERT_EQUAL_UINT8(3, gH.undoDepth(0));  // two hot steps down, the cold one, the empty slot
  TEST_ASSERT_TRUE(gH.undo(0));
  TEST_ASSERT_FALSE(gH.undo(0));
  TEST_ASSERT_NOT_EQUAL(kNoManifest, gH.current(0));  // still the oldest hot state, not the empty slot
}

void test_no_heap_after_init() {
  setup();
  gTrap = true;
  const unsigned long before = gHeapAllocations;
  for (uint32_t i = 0; i < 300; ++i) {
    drainReclaim();
    gH.commit(static_cast<uint8_t>(i % 5), take(static_cast<uint8_t>(i % 5)));
    if (i % 3 == 0) gH.undo(static_cast<uint8_t>(i % 5));
    if (i % 7 == 0) gH.redo(static_cast<uint8_t>(i % 5));
  }
  gTrap = false;
  TEST_ASSERT_EQUAL_UINT32(0, gHeapAllocations - before);
  TEST_ASSERT_TRUE(sizeof(History<ProductionManifests, ProductionRuns>) <= 2048u);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_undo_redo_and_redo_drop);
  RUN_TEST(test_slots_are_independent);
  RUN_TEST(test_full_history_forgets_the_oldest_step);
  RUN_TEST(test_eviction_takes_the_oldest_step_across_slots);
  RUN_TEST(test_session_undo_is_bounded_and_stop_makes_one_step);
  RUN_TEST(test_session_fully_undone_leaves_no_step);
  RUN_TEST(test_session_new_wrap_after_undo_and_other_slots);
  RUN_TEST(test_long_session_shares_the_99);
  RUN_TEST(test_edit_session_fold_and_abandon);
  RUN_TEST(test_overdub_and_edit_sessions_together);
  RUN_TEST(test_session_leaves_earlier_redo_alone);
  RUN_TEST(test_long_edit_session_abandon_restores_the_start);
  RUN_TEST(test_session_on_empty_slot_keeps_empty_reachable);
  RUN_TEST(test_random_sequences_match_the_reference_model_small);
  RUN_TEST(test_random_sequences_match_the_reference_model_production);
  RUN_TEST(test_a_full_table_demotes_the_oldest_step_cold);
  RUN_TEST(test_a_slot_keeps_99_and_forgets_only_its_own);
  RUN_TEST(test_undo_stops_above_a_cold_state);
  RUN_TEST(test_no_heap_after_init);
  return UNITY_END();
}
