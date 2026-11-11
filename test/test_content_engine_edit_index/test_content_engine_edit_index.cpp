//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// fixed-memory-engine E3.3: shared, revision-keyed edit-hide index. One fixed store of S slots; a slot holds the sorted
// hide index of one manifest identity and is pinned by the composers composing that identity. Residency is bounded by
// S (never one index per historical manifest); a manifest without edit runs needs no slot; only unpinned slots are
// evicted (least recently used); exhaustion and capacity are named outcomes; an evicted identity rebuilds identically.
// E5.3: the entries live in the pool's edit-index partition (taken once at init, owner-tagged, never borrowed), and a
// worker builds an index in grains (`reserve` + `buildStep`) with the same result as the synchronous build.

#include <unity.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include <new>
#include <vector>

#include "ContentEngine/Composer.h"
#include "ContentEngine/EditIndexStore.h"
#include "ContentEngine/EngineCore.h"

static unsigned long gHeapAllocations = 0;
void* operator new(std::size_t size) {
  ++gHeapAllocations;
  if (void* p = std::malloc(size == 0 ? 1 : size)) return p;
  throw std::bad_alloc();
}
void* operator new[](std::size_t size) { return operator new(size); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }

using namespace ContentEngine;

using Pool = PagePool<256, 64>;
using Runs = RunTable<32, Pool>;
using Store = EditIndexStore<Pool, Runs, /*slots*/ 3, /*capacity*/ 8>;

static Pool gPool;
static Runs gRuns;
static Store gStore;

static PassEvent row(uint32_t tick, uint32_t id, uint8_t status) {
  PassEvent e;
  e.tick = tick;
  e.noteId = id;
  e.seq = 0;
  e.status = status;
  e.data1 = 60;
  e.data2 = 100;
  e.origin = 0;
  return e;
}

static RunId editRun(std::initializer_list<uint32_t> hides) {
  const RunId r = gRuns.allocate(0, RunKind::kEdit);
  for (uint32_t id : hides) gRuns.append(r, row(0, id, kHideNoteStatus), gPool);
  gRuns.seal(r);
  return r;
}
static RunId noteRun() {
  const RunId r = gRuns.allocate(0, RunKind::kRecord);
  gRuns.append(r, row(10, 1, 0x90), gPool);
  gRuns.append(r, row(20, 1, 0x80), gPool);
  gRuns.seal(r);
  return r;
}

void setUp() {
  gPool.init();
  gRuns.init();
  TEST_ASSERT_TRUE(gStore.init(gPool, gRuns));
}
void tearDown() {}

void test_same_identity_shares_one_pinned_slot() {
  const RunId a = noteRun();
  const RunId e = editRun({1, 5, 3});
  const SourceRun src[] = {{a, 2}, {e, 3}};
  IndexLease l1, l2;
  TEST_ASSERT_EQUAL(IndexResult::Ready, gStore.acquire(42, src, 2, l1));
  TEST_ASSERT_EQUAL(IndexResult::Ready, gStore.acquire(42, src, 2, l2));
  TEST_ASSERT_EQUAL_UINT8(l1.slot, l2.slot);
  TEST_ASSERT_EQUAL_UINT16(2, gStore.pins(l1.slot));
  TEST_ASSERT_TRUE(gStore.hidden(l1, 5, 0));
  TEST_ASSERT_FALSE(gStore.hidden(l1, 5, 1));  // the hide applies to earlier passes only
  TEST_ASSERT_FALSE(gStore.hidden(l1, 2, 0));
  gStore.release(l1);
  gStore.release(l2);
  TEST_ASSERT_EQUAL_UINT16(0, gStore.pins(l2.slot));
}

void test_manifest_without_edit_runs_takes_no_slot() {
  const RunId a = noteRun();
  const SourceRun src[] = {{a, 2}};
  IndexLease l;
  TEST_ASSERT_EQUAL(IndexResult::NotNeeded, gStore.acquire(7, src, 1, l));
  TEST_ASSERT_EQUAL_UINT8(0, gStore.slotsInUse());
  TEST_ASSERT_FALSE(gStore.hidden(l, 1, 0));
  gStore.release(l);  // harmless
}

void test_exhaustion_is_named_and_pinned_slots_are_never_evicted() {
  const RunId e = editRun({1});
  const SourceRun src[] = {{e, 1}};
  IndexLease l[4];
  for (uint32_t i = 0; i < 3; ++i) TEST_ASSERT_EQUAL(IndexResult::Ready, gStore.acquire(100 + i, src, 1, l[i]));
  TEST_ASSERT_EQUAL(IndexResult::Busy, gStore.acquire(200, src, 1, l[3]));  // all three pinned
  const uint8_t freed = l[1].slot;
  gStore.release(l[1]);
  TEST_ASSERT_EQUAL(IndexResult::Ready, gStore.acquire(200, src, 1, l[3]));  // evicts the unpinned one (identity 101)
  TEST_ASSERT_EQUAL_UINT8(freed, l[3].slot);
  TEST_ASSERT_TRUE(gStore.holds(100));
  TEST_ASSERT_TRUE(gStore.holds(102));
  TEST_ASSERT_FALSE(gStore.holds(101));
}

void test_least_recently_used_unpinned_slot_is_evicted() {
  const RunId e = editRun({1});
  const SourceRun src[] = {{e, 1}};
  IndexLease l;
  for (uint32_t id : {10u, 11u, 12u}) {
    TEST_ASSERT_EQUAL(IndexResult::Ready, gStore.acquire(id, src, 1, l));
    gStore.release(l);
  }
  TEST_ASSERT_EQUAL(IndexResult::Ready, gStore.acquire(10, src, 1, l));  // touch 10
  gStore.release(l);
  TEST_ASSERT_EQUAL(IndexResult::Ready, gStore.acquire(13, src, 1, l));  // evicts 11, the oldest
  gStore.release(l);
  TEST_ASSERT_FALSE(gStore.holds(11));
  TEST_ASSERT_TRUE(gStore.holds(10));
}

void test_capacity_is_named_and_takes_no_slot() {
  const RunId e = editRun({1, 2, 3, 4, 5, 6, 7, 8, 9});
  const SourceRun src[] = {{e, gRuns.run(e).rowCount}};
  IndexLease l;
  TEST_ASSERT_EQUAL(IndexResult::CapacityExceeded, gStore.acquire(5, src, 1, l));
  TEST_ASSERT_EQUAL_UINT8(0, gStore.slotsInUse());
}

void test_evicted_identity_rebuilds_identically() {
  const RunId a = noteRun();
  const RunId e1 = editRun({4, 2});
  const RunId e2 = editRun({2, 9});
  const SourceRun src[] = {{a, 2}, {e1, 2}, {e2, 2}};
  IndexLease l;
  TEST_ASSERT_EQUAL(IndexResult::Ready, gStore.acquire(77, src, 3, l));
  bool before[12][3];
  for (uint32_t id = 0; id < 12; ++id)
    for (uint8_t r = 0; r < 3; ++r) before[id][r] = gStore.hidden(l, id, r);
  gStore.release(l);
  const SourceRun other[] = {{e1, 2}};
  IndexLease x;
  for (uint32_t id : {1u, 2u, 3u}) {  // push 77 out
    gStore.acquire(id, other, 1, x);
    gStore.release(x);
  }
  TEST_ASSERT_FALSE(gStore.holds(77));
  TEST_ASSERT_EQUAL(IndexResult::Ready, gStore.acquire(77, src, 3, l));
  for (uint32_t id = 0; id < 12; ++id)
    for (uint8_t r = 0; r < 3; ++r) TEST_ASSERT_EQUAL(before[id][r], gStore.hidden(l, id, r));
  TEST_ASSERT_TRUE(gStore.hidden(l, 2, 1));   // edit 2 hides 2 in edit 1 (latest hiding pass = 2)
  TEST_ASSERT_FALSE(gStore.hidden(l, 4, 1));  // edit 1 hides 4 only in pass 0
  gStore.release(l);
}

// Placement: init takes exactly the partition from the pool, tagged kOwnerIndex; content can exhaust the pool without
// ever receiving an index page, and the index still answers afterwards. A pool too small for the partition is refused
// by name (every identity with edit runs is then Busy).
void test_partition_taken_from_pool_and_never_borrowed() {
  TEST_ASSERT_EQUAL_UINT32(Store::kPages, gPool.countOwnedBy(kOwnerIndex));
  TEST_ASSERT_EQUAL_UINT32(256 - Store::kPages, gPool.freeCount());
  const RunId n = noteRun();
  const RunId e = editRun({4, 2, 9});
  const SourceRun src[] = {{n, 2}, {e, 3}};
  IndexLease l;
  TEST_ASSERT_EQUAL(IndexResult::Ready, gStore.acquire(3, src, 2, l));
  while (gPool.acquire(0) != kNoPage) {
  }
  TEST_ASSERT_EQUAL_UINT32(Store::kPages, gPool.countOwnedBy(kOwnerIndex));
  for (uint8_t s = 0; s < Store::kSlots; ++s)
    for (uint16_t k = 0; k < Store::kPagesPerSlot; ++k) TEST_ASSERT_EQUAL_UINT16(kOwnerIndex, gPool.ownerOf(gStore.page(s, k)));
  TEST_ASSERT_TRUE(gStore.hidden(l, 9, 0));
  TEST_ASSERT_FALSE(gStore.hidden(l, 5, 0));

  static Pool tiny;  // 256 pages, all taken: no room for a partition
  tiny.init();
  while (tiny.acquire(0) != kNoPage) {
  }
  static Store unplaced;
  TEST_ASSERT_FALSE(unplaced.init(tiny, gRuns));
  TEST_ASSERT_EQUAL(IndexResult::Busy, unplaced.acquire(3, src, 2, l));
}

// Incremental build equals the synchronous one for random multi-edit sources and random grain sizes.
void test_incremental_build_equals_synchronous_random() {
  uint32_t seed = 0xB111D;
  auto rnd = [&seed]() {
    seed ^= seed << 13;
    seed ^= seed >> 17;
    seed ^= seed << 5;
    return seed;
  };
  using Big = EditIndexStore<Pool, Runs, 2, 64>;
  static Big big;
  for (uint32_t trial = 0; trial < 200; ++trial) {
    gPool.init();
    gRuns.init();
    TEST_ASSERT_TRUE(big.init(gPool, gRuns));
    SourceRun src[4];
    const uint8_t count = static_cast<uint8_t>(1 + rnd() % 4);
    for (uint8_t r = 0; r < count; ++r) {
      if (rnd() % 3 == 0) {
        const RunId n = noteRun();
        src[r] = {n, 2};
        continue;
      }
      const RunId e = gRuns.allocate(0, RunKind::kEdit);
      for (uint32_t k = 0, h = rnd() % 16; k < h; ++k) gRuns.append(e, row(0, 1 + rnd() % 24, kHideNoteStatus), gPool);
      gRuns.append(e, row(5, 7, 0x90), gPool);
      gRuns.seal(e);
      src[r] = {e, gRuns.run(e).rowCount};
    }
    IndexLease a, b;
    const IndexResult ra = big.acquire(1, src, count, a);
    const IndexResult rb = big.reserve(2, src, count, b);
    if (ra == IndexResult::NotNeeded) {
      TEST_ASSERT_EQUAL(IndexResult::NotNeeded, rb);
      continue;
    }
    TEST_ASSERT_EQUAL(IndexResult::Ready, ra);
    TEST_ASSERT_EQUAL(IndexResult::Building, rb);
    IndexResult st;
    while ((st = big.buildStep(b, src, count, 1 + rnd() % 9)) == IndexResult::Building) {
    }
    TEST_ASSERT_EQUAL(IndexResult::Ready, st);
    for (uint32_t id = 0; id < 26; ++id)
      for (uint8_t o = 0; o < 4; ++o) TEST_ASSERT_EQUAL(big.hidden(a, id, o), big.hidden(b, id, o));
  }
}

// While a job builds an identity, another reserve or acquire of it is named Building (no lease, nothing pinned); a
// build abandoned before Ready frees its slot; capacity found while gathering empties the slot.
void test_building_identity_is_named_and_abandoned_build_frees_slot() {
  const RunId e = editRun({1, 2, 3, 4, 5, 6});
  const SourceRun src[] = {{e, 6}};
  IndexLease job, other;
  TEST_ASSERT_EQUAL(IndexResult::Building, gStore.reserve(8, src, 1, job));
  TEST_ASSERT_NOT_EQUAL(IndexLease::kNoSlot, job.slot);
  TEST_ASSERT_EQUAL(IndexResult::Building, gStore.buildStep(job, src, 1, 2));
  TEST_ASSERT_EQUAL(IndexResult::Building, gStore.acquire(8, src, 1, other));
  TEST_ASSERT_EQUAL_UINT8(IndexLease::kNoSlot, other.slot);
  TEST_ASSERT_EQUAL_UINT16(1, gStore.pins(job.slot));
  TEST_ASSERT_FALSE(gStore.holds(8));
  gStore.release(job);  // abandoned
  TEST_ASSERT_EQUAL_UINT8(0, gStore.slotsInUse());
  TEST_ASSERT_EQUAL(IndexResult::Ready, gStore.acquire(8, src, 1, other));  // builds afresh

  const RunId big = editRun({1, 2, 3, 4, 5, 6, 7, 8, 9});
  const SourceRun srcBig[] = {{big, 9}};
  IndexLease l;
  TEST_ASSERT_EQUAL(IndexResult::Building, gStore.reserve(9, srcBig, 1, l));
  IndexResult st;
  while ((st = gStore.buildStep(l, srcBig, 1, 3)) == IndexResult::Building) {
  }
  TEST_ASSERT_EQUAL(IndexResult::CapacityExceeded, st);
  gStore.release(l);
  TEST_ASSERT_EQUAL_UINT8(1, gStore.slotsInUse());  // only identity 8
}

// Grain bound at production capacity: 4,096 hides build in <= 60 grains of 256 units (gather 16, heapify 8, sort 16,
// collapse 16, phase changes); each grain is bounded by 256 units of O(log H) work.
void test_full_capacity_build_grain_count() {
  using BigPool = PagePool<640, 64>;
  using BigRuns = RunTable<4, BigPool>;
  static BigPool pool;
  static BigRuns runs;
  static EditIndexStore<BigPool, BigRuns, 1, 4096> store;
  pool.init();
  runs.init();
  TEST_ASSERT_TRUE(store.init(pool, runs));
  const RunId e = runs.allocate(0, RunKind::kEdit);
  uint32_t x = 12345;
  for (uint32_t k = 0; k < 4096; ++k) {
    x = x * 1103515245u + 12345u;
    TEST_ASSERT_EQUAL(AppendResult::Ok, runs.append(e, row(0, (x >> 8) % 100000, kHideNoteStatus), pool));
  }
  runs.seal(e);
  const SourceRun src[] = {{e, 4096}};
  IndexLease l;
  TEST_ASSERT_EQUAL(IndexResult::Building, store.reserve(1, src, 1, l));
  uint32_t grains = 1;
  while (store.buildStep(l, src, 1, 256) == IndexResult::Building) ++grains;
  printf("E5.3 index build, 4096 hides: %u grains of 256 units\n", static_cast<unsigned>(grains));
  TEST_ASSERT_TRUE(grains <= 60);
  TEST_ASSERT_EQUAL_UINT32(0, store.syncBuilds());
}

// The composer no longer carries a hide table: the production composer stays inside the 12 KiB-per-cursor budget
// (18 cursors <= 221,184 B).
// DEC-286 (groove design § 2): the edit-hide index is cached by identity, so two views of one state — played from its
// groove pass, rebuilt from the passes before it — must not share one: a shared identity serves the first view's hides
// to the second. The rebuild view's identity (`prefixViewIdentity`) has its own index.
void test_a_rebuild_view_has_its_own_index() {
  const RunId a = noteRun();
  const RunId playEdit = editRun({1});     // the playback view deletes note 1
  const RunId prefixEdit = editRun({2});   // the prefix's own edit deletes note 2
  const SourceRun play[] = {{a, 2}, {playEdit, 1}};
  const SourceRun prefix[] = {{a, 2}, {prefixEdit, 1}};
  IndexLease lp, lr;
  TEST_ASSERT_EQUAL(IndexResult::Ready, gStore.acquire(42, play, 2, lp));
  TEST_ASSERT_TRUE(gStore.hidden(lp, 1, 0));
  // The hazard: the prefix composed under the same identity is served the playback view's hides.
  IndexLease shared;
  TEST_ASSERT_EQUAL(IndexResult::Ready, gStore.acquire(42, prefix, 2, shared));
  TEST_ASSERT_TRUE(gStore.hidden(shared, 1, 0));   // wrong: note 1 hidden in the prefix
  TEST_ASSERT_FALSE(gStore.hidden(shared, 2, 0));  // wrong: note 2 back
  gStore.release(shared);
  // Its own identity: its own hides.
  TEST_ASSERT_TRUE(prefixViewIdentity(42) != 42);
  TEST_ASSERT_EQUAL(IndexResult::Ready, gStore.acquire(prefixViewIdentity(42), prefix, 2, lr));
  TEST_ASSERT_TRUE(lr.slot != lp.slot);
  TEST_ASSERT_TRUE(gStore.hidden(lr, 2, 0));
  TEST_ASSERT_FALSE(gStore.hidden(lr, 1, 0));
  TEST_ASSERT_TRUE(gStore.hidden(lp, 1, 0));  // the playback view's index untouched
  TEST_ASSERT_FALSE(gStore.hidden(lp, 2, 0));
  gStore.release(lp);
  gStore.release(lr);
}

void test_production_composer_within_budget() {
  TEST_ASSERT_TRUE(sizeof(Composer<ProductionPool, ProductionRuns>) <= 12304u);  // + 16 B the dirty-pitch mask (2026-11-11)
  TEST_ASSERT_EQUAL_UINT32(0, gHeapAllocations);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_same_identity_shares_one_pinned_slot);
  RUN_TEST(test_manifest_without_edit_runs_takes_no_slot);
  RUN_TEST(test_exhaustion_is_named_and_pinned_slots_are_never_evicted);
  RUN_TEST(test_least_recently_used_unpinned_slot_is_evicted);
  RUN_TEST(test_capacity_is_named_and_takes_no_slot);
  RUN_TEST(test_evicted_identity_rebuilds_identically);
  RUN_TEST(test_partition_taken_from_pool_and_never_borrowed);
  RUN_TEST(test_incremental_build_equals_synchronous_random);
  RUN_TEST(test_building_identity_is_named_and_abandoned_build_frees_slot);
  RUN_TEST(test_full_capacity_build_grain_count);
  RUN_TEST(test_a_rebuild_view_has_its_own_index);
  RUN_TEST(test_production_composer_within_budget);
  return UNITY_END();
}
