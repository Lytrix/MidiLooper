//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// fixed-memory-engine E3.3: shared, revision-keyed edit-hide index. One fixed store of S slots; a slot holds the sorted
// hide index of one manifest identity and is pinned by the composers composing that identity. Residency is bounded by
// S (never one index per historical manifest); a manifest without edit runs needs no slot; only unpinned slots are
// evicted (least recently used); exhaustion and capacity are named outcomes; an evicted identity rebuilds identically.

#include <unity.h>

#include <cstdint>
#include <cstdlib>
#include <initializer_list>
#include <new>

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
  gStore.init(gPool, gRuns);
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

// The composer no longer carries a hide table: the production composer stays inside the 12 KiB-per-cursor budget
// (18 cursors <= 221,184 B).
void test_production_composer_within_budget() {
  TEST_ASSERT_TRUE(sizeof(Composer<ProductionPool, ProductionRuns>) <= 12288u);
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
  RUN_TEST(test_production_composer_within_budget);
  return UNITY_END();
}
