//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// fixed-memory-engine E4: undo history and slot currents (owner decisions 2026-11-05). Undo / redo are per slot; at
// most 99 states are held across all slots; a commit drops its slot's redo branch and, when full, evicts the oldest
// step in the set (oldest-committed non-current state; a redo state takes its whole branch with it). Recording is never
// refused for history. Dropped states release their manifests, so their runs reach the reclaim queue. Overdub session
// (DEC-038, E4.2): wraps are commits, undo on the session slot stops at the session start, and Stop folds the session
// into one step. A random sequence (with sessions) is checked against an independent reference model written from
// those rules.

#include <unity.h>

#include <cstdint>
#include <cstdlib>
#include <new>
#include <vector>

#include "ContentEngine/EngineCore.h"
#include "ContentEngine/History.h"

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
  for (uint8_t i = 0; i < 98; ++i) TEST_ASSERT_TRUE(gH.undo(0));
  TEST_ASSERT_FALSE(gH.undo(0));
  TEST_ASSERT_NOT_EQUAL(kNoManifest, gH.current(0));  // the floor: never the empty slot
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
  TEST_ASSERT_FALSE(gManifests.manifest(r1).live);
  TEST_ASSERT_FALSE(gManifests.manifest(r2).live);
}

// ---- E4.2 overdub session (DEC-038) -------------------------------------------------------------------------------------

// Wraps are steps while overdubbing (O:), undo stops at the session start, and after Stop one U: undoes the session.
void test_session_undo_is_bounded_and_stop_makes_one_step() {
  setup();
  const ManifestId rec = take(0);
  gH.commit(0, rec);
  TEST_ASSERT_TRUE(gH.beginSession(0));
  TEST_ASSERT_FALSE(gH.beginSession(1));  // one writer
  const ManifestId w1 = take(0), w2 = take(0), w3 = take(0);
  gH.commit(0, w1);
  gH.commit(0, w2);
  gH.commit(0, w3);
  TEST_ASSERT_EQUAL_UINT8(3, gH.sessionDepth());
  TEST_ASSERT_TRUE(gH.undo(0));
  TEST_ASSERT_TRUE(gH.undo(0));
  TEST_ASSERT_TRUE(gH.undo(0));
  TEST_ASSERT_EQUAL_UINT16(rec, gH.current(0));
  TEST_ASSERT_FALSE(gH.undo(0));  // not below the session start
  TEST_ASSERT_EQUAL_UINT8(0, gH.sessionDepth());
  TEST_ASSERT_TRUE(gH.redo(0));
  TEST_ASSERT_TRUE(gH.redo(0));  // w2 current, w3 redo
  gH.endSession();
  TEST_ASSERT_FALSE(gH.inSession(0));
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
  gH.beginSession(0);
  const ManifestId w1 = take(0), w2 = take(0);
  gH.commit(0, w1);
  gH.commit(0, w2);
  gH.undo(0);
  gH.undo(0);
  gH.endSession();
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
  gH.beginSession(0);
  const ManifestId w1 = take(0), w2 = take(0), w3 = take(0);
  gH.commit(0, w1);
  gH.commit(0, w2);
  gH.undo(0);
  gH.commit(0, w3);  // drops w2
  TEST_ASSERT_FALSE(gManifests.manifest(w2).live);
  TEST_ASSERT_EQUAL_UINT8(2, gH.sessionDepth());
  TEST_ASSERT_TRUE(gH.undo(1));  // slot 1 is not in the session
  TEST_ASSERT_TRUE(gH.undo(1));
  TEST_ASSERT_EQUAL_UINT16(kNoManifest, gH.current(1));
  gH.endSession();
  TEST_ASSERT_EQUAL_UINT16(w3, gH.current(0));
  TEST_ASSERT_EQUAL_UINT8(2, gH.undoDepth(0));
  TEST_ASSERT_EQUAL_UINT8(2, gH.redoDepth(1));
}

// A session longer than the history: its oldest wraps (and the start) are forgotten like any oldest step; undo stops
// at the floor; Stop still folds what is left into one step.
void test_long_session_shares_the_99() {
  setup();
  gH.commit(0, take(0));
  gH.beginSession(0);
  ManifestId last = kNoManifest;
  for (uint32_t i = 0; i < 150; ++i) {
    drainReclaim();
    last = take(0);
    gH.commit(0, last);
    TEST_ASSERT_TRUE(gH.heldStates() <= 99);
  }
  TEST_ASSERT_EQUAL_UINT8(99, gH.sessionDepth());
  uint32_t undos = 0;
  while (gH.undo(0)) ++undos;
  TEST_ASSERT_EQUAL_UINT32(98, undos);  // the floor: the oldest kept wrap
  while (gH.redo(0)) {
  }
  gH.endSession();
  TEST_ASSERT_EQUAL_UINT16(last, gH.current(0));
  TEST_ASSERT_EQUAL_UINT8(1, gH.heldStates());
  TEST_ASSERT_FALSE(gH.canUndo(0));  // the pre-session state was forgotten during the session
  drainReclaim();
  TEST_ASSERT_TRUE(checkConservation(gPool, gRuns, gManifests));
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
  int sessionSlot = -1;
  uint32_t baseSeq = 0;
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
          if (static_cast<int>(i) != slots[k].cur && (!found || slots[k].states[i].seq < slots[bs].states[bi].seq)) {
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
        s.emptyBelow = false;
      } else {  // a redo state: the whole branch above current goes
        dropAbove(s);
      }
    }
    slots[slot].states.push_back({m, ++seq});
    slots[slot].cur = static_cast<int>(slots[slot].states.size()) - 1;
  }
  bool begin(size_t slot) {
    if (sessionSlot >= 0) return false;
    sessionSlot = static_cast<int>(slot);
    const RefSlot& s = slots[slot];
    baseSeq = s.cur < 0 ? 0 : s.states[static_cast<size_t>(s.cur)].seq;
    return true;
  }
  void end() {
    if (sessionSlot < 0) return;
    RefSlot& s = slots[static_cast<size_t>(sessionSlot)];
    sessionSlot = -1;
    dropAbove(s);
    if (s.cur < 0 || s.states[static_cast<size_t>(s.cur)].seq <= baseSeq) return;
    while (s.cur > 0 && s.states[static_cast<size_t>(s.cur - 1)].seq > baseSeq) {  // fold the session into its current
      released.push_back(s.states[static_cast<size_t>(s.cur - 1)].m);
      s.states.erase(s.states.begin() + (s.cur - 1));
      --s.cur;
    }
  }
  bool undo(size_t slot) {
    RefSlot& s = slots[slot];
    if (sessionSlot == static_cast<int>(slot) && s.cur >= 0 && s.states[static_cast<size_t>(s.cur)].seq <= baseSeq)
      return false;
    if (s.cur > 0 || (s.cur == 0 && s.emptyBelow)) {
      --s.cur;
      return true;
    }
    return false;
  }
  bool redo(size_t slot) {
    RefSlot& s = slots[slot];
    if (s.cur + 1 < static_cast<int>(s.states.size())) {
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
    const uint32_t op = (x >> 20) % 12;
    if (op == 10) {
      TEST_ASSERT_EQUAL(ref.begin(slot), h.beginSession(slot));
    } else if (op == 11) {
      ref.end();
      h.endSession();
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
    if (ref.sessionSlot >= 0) {
      size_t n = 0;
      const RefSlot& ss = ref.slots[static_cast<size_t>(ref.sessionSlot)];
      for (int i = ss.cur; i >= 0 && ss.states[static_cast<size_t>(i)].seq > ref.baseSeq; --i) ++n;
      TEST_ASSERT_EQUAL_UINT8(n, h.sessionDepth());
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
  RUN_TEST(test_random_sequences_match_the_reference_model_small);
  RUN_TEST(test_random_sequences_match_the_reference_model_production);
  RUN_TEST(test_no_heap_after_init);
  return UNITY_END();
}
