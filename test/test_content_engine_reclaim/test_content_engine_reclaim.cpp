//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// fixed-memory-engine E5: the reclaim worker (plan revision 3 § 5.3, § 7.1). A run whose last reference drops joins
// the run table's reclaim queue in release order; `reclaim` returns at most its unit budget (a page, or an emptied
// descriptor) per call, and `ReclaimJob` runs it as a background scheduler job of 64 units per grain, declaring the
// grains the queue needs so it never overruns. Pinned manifests keep their runs; a run shared by two manifests is
// reclaimed only after both drop it. Conservation holds after every grain, and nothing allocates.

#include <unity.h>

#include <cstdint>
#include <cstdlib>
#include <new>

#include "ContentEngine/EngineCore.h"
#include "ContentEngine/ReclaimJob.h"
#include "ContentEngine/WorkerScheduler.h"

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

using Pool = PagePool</*pages*/ 512, /*rows per page*/ 8>;  // 128 B pages: 64 ids per directory page
using Runs = RunTable</*runs*/ 32, Pool, /*inline pages*/ 4, /*dir pages*/ 4>;
using Manifests = ManifestTable</*manifests*/ 16, /*runs per manifest*/ 8>;
using Job = ReclaimJob<Runs, Pool>;
using Sched = WorkerScheduler<8>;

static Pool gPool;
static Runs gRuns;
static Manifests gManifests;
static Job gJob;
static Sched gS;

static PassEvent row(uint32_t tick, uint32_t id) {
  PassEvent e{};
  e.tick = tick;
  e.noteId = id;
  e.status = 0x90;
  e.data1 = static_cast<uint8_t>(id & 0x7F);
  e.data2 = 100;
  return e;
}

static void setup() {
  gPool.init();
  gRuns.init();
  gManifests.init();
  gJob.bind(gRuns, gPool);
  gS.init(800, 800);
}

static bool conserved() { return checkConservation(gPool, gRuns, gManifests); }

/// A sealed run of `rows` rows, still holding its writer reference.
static RunId sealedRun(uint32_t rows) {
  const RunId r = gRuns.allocate(0, 0);
  TEST_ASSERT_NOT_EQUAL(kNoRun, r);
  for (uint32_t i = 0; i < rows; ++i) TEST_ASSERT_EQUAL(AppendResult::Ok, gRuns.append(r, row(i, i), gPool));
  TEST_ASSERT_TRUE(gRuns.seal(r));
  return r;
}

// Release order is reclaim order; every call stays within its unit budget; the pending units account exactly.
void test_queue_order_and_unit_budget() {
  setup();
  const RunId a = sealedRun(8 * 3);    // 3 pages
  const RunId b = sealedRun(8 * 70);   // 70 data pages + 2 directory pages
  const RunId c = sealedRun(0);        // no pages: only its descriptor
  const uint32_t units = gRuns.heldPages(a) + gRuns.heldPages(b) + gRuns.heldPages(c) + 3;
  TEST_ASSERT_TRUE(gRuns.release(b));
  TEST_ASSERT_TRUE(gRuns.release(a));
  TEST_ASSERT_TRUE(gRuns.release(c));
  TEST_ASSERT_EQUAL_UINT16(3, gRuns.reclaimPendingRuns());
  TEST_ASSERT_EQUAL_UINT32(units, gRuns.reclaimPendingUnits());
  RunId freedOrder[3];
  uint8_t freed = 0;
  bool wasFree[3] = {false, false, false};
  const RunId ids[3] = {a, b, c};
  uint32_t calls = 0;
  while (gRuns.reclaimPendingRuns() != 0) {
    const uint16_t before = gPool.freeCount();
    TEST_ASSERT_EQUAL_UINT32(1, gRuns.reclaim(gPool, 1));
    TEST_ASSERT_TRUE(gPool.freeCount() - before <= 1);
    TEST_ASSERT_TRUE(conserved());
    for (uint8_t k = 0; k < 3; ++k)
      if (!wasFree[k] && gRuns.run(ids[k]).state == RunState::Free) {
        wasFree[k] = true;
        freedOrder[freed++] = ids[k];
      }
    ++calls;
  }
  TEST_ASSERT_EQUAL_UINT32(units, calls);
  TEST_ASSERT_EQUAL_UINT32(0, gRuns.reclaimPendingUnits());
  TEST_ASSERT_EQUAL_UINT8(3, freed);
  TEST_ASSERT_EQUAL_UINT16(b, freedOrder[0]);
  TEST_ASSERT_EQUAL_UINT16(a, freedOrder[1]);
  TEST_ASSERT_EQUAL_UINT16(c, freedOrder[2]);
  TEST_ASSERT_EQUAL_UINT16(Pool::kPageCount, gPool.freeCount());
  TEST_ASSERT_EQUAL_UINT16(Runs::kRunCount, gRuns.freeCount());
  TEST_ASSERT_EQUAL_UINT32(0, gRuns.reclaim(gPool, 64));  // empty queue: nothing to do
}

// A pinned manifest keeps its runs out of the queue; a run shared by two manifests waits for the second release.
void test_pins_and_shared_runs_hold_reclamation() {
  setup();
  const RunId a = sealedRun(40);
  const RunId b = sealedRun(40);
  const RunId one[] = {a};
  const RunId both[] = {a, b};
  const ManifestId m1 = gManifests.create(0, 1, 768, one, 1, gRuns);
  const ManifestId m2 = gManifests.create(0, 2, 768, both, 2, gRuns);
  gRuns.release(a);  // writers drop their references
  gRuns.release(b);
  TEST_ASSERT_EQUAL_UINT16(0, gRuns.reclaimPendingRuns());
  gManifests.pin(m2);
  TEST_ASSERT_FALSE(gManifests.release(m2, gRuns));
  TEST_ASSERT_TRUE(gJob.submitIfPending(gS));
  TEST_ASSERT_FALSE(gJob.running());  // nothing queued
  TEST_ASSERT_TRUE(gManifests.unpin(m2, gRuns));
  TEST_ASSERT_EQUAL_UINT16(1, gRuns.reclaimPendingRuns());  // b only: m1 still lists a
  TEST_ASSERT_EQUAL(RunState::Sealed, gRuns.run(a).state);
  TEST_ASSERT_TRUE(gJob.submitIfPending(gS));
  while (gS.runGrain(0) != Sched::kNoJob) TEST_ASSERT_TRUE(conserved());
  TEST_ASSERT_EQUAL(RunState::Free, gRuns.run(b).state);
  TEST_ASSERT_EQUAL(RunState::Sealed, gRuns.run(a).state);
  TEST_ASSERT_TRUE(gManifests.release(m1, gRuns));
  TEST_ASSERT_TRUE(gJob.submitIfPending(gS));
  while (gS.runGrain(0) != Sched::kNoJob) TEST_ASSERT_TRUE(conserved());
  TEST_ASSERT_EQUAL(RunState::Free, gRuns.run(a).state);
  TEST_ASSERT_EQUAL_UINT16(Pool::kPageCount, gPool.freeCount());
}

// Through the scheduler: a background job of 64 units per grain, finished within its declared grains (no overrun),
// display keeps its share; runs released while it runs go to the next submission.
void test_job_runs_in_bounded_grains_beside_display() {
  setup();
  RunId r[12];
  uint32_t units = 0;
  for (uint8_t i = 0; i < 10; ++i) {
    r[i] = sealedRun(8 * 20);  // 20 pages each
    units += gRuns.heldPages(r[i]) + 1;
    gRuns.release(r[i]);
  }
  struct Display {
    uint32_t runs;
  } display{0};
  const JobSpec disp{JobClass::Display, 0, 1000, [](void* c) {
                       ++static_cast<Display*>(c)->runs;
                       return JobStep::Continue;
                     },
                     &display};
  uint8_t dispId;
  TEST_ASSERT_TRUE(gS.submit(disp, dispId));
  TEST_ASSERT_TRUE(gJob.submitIfPending(gS));
  const uint32_t declared = gJob.grainsLeft();
  TEST_ASSERT_EQUAL_UINT32((units + 63) / 64, declared);
  uint32_t grains = 0;
  bool releasedMore = false;
  while (gJob.running()) {
    const uint16_t before = gPool.freeCount();
    gS.runGrain(grains * 800);
    TEST_ASSERT_TRUE(gPool.freeCount() - before <= Job::kUnitsPerGrain);
    TEST_ASSERT_TRUE(conserved());
    if (!releasedMore) {  // released mid-job: taken by the next submission, never extending this one
      r[10] = sealedRun(8 * 30);
      r[11] = sealedRun(8 * 30);
      gRuns.release(r[10]);
      gRuns.release(r[11]);
      releasedMore = true;
    }
    TEST_ASSERT_TRUE(++grains < 100);
  }
  TEST_ASSERT_EQUAL_UINT32(0, gS.report().overruns);
  TEST_ASSERT_TRUE(gS.report().backgroundGrains <= declared);
  TEST_ASSERT_TRUE(display.runs * 4 + 4 >= gS.report().grains);
  TEST_ASSERT_TRUE(gRuns.reclaimPendingRuns() > 0);  // the two later releases
  TEST_ASSERT_TRUE(gJob.submitIfPending(gS));
  while (gJob.running()) {
    gS.runGrain(grains * 800);
    TEST_ASSERT_TRUE(++grains < 200);
  }
  TEST_ASSERT_EQUAL_UINT32(0, gS.report().overruns);
  TEST_ASSERT_EQUAL_UINT16(0, gRuns.reclaimPendingRuns());
  TEST_ASSERT_EQUAL_UINT16(Pool::kPageCount, gPool.freeCount());
  TEST_ASSERT_TRUE(conserved());
}

// Writers keep allocating and appending while reclamation runs; a freed descriptor is reused only after its reclaim.
void test_writing_interleaved_with_reclaim() {
  setup();
  uint32_t x = 12345;
  RunId live[16];
  uint8_t liveCount = 0;
  for (uint32_t step = 0; step < 20000; ++step) {
    x = x * 1103515245u + 12345u;
    const uint32_t op = (x >> 16) % 8;
    if (op < 3 && liveCount < 16 && gRuns.freeCount() > 0 && gPool.freeCount() > 40) {
      live[liveCount++] = sealedRun((x >> 8) % 160);
    } else if (op < 5 && liveCount > 0) {
      const uint8_t k = static_cast<uint8_t>((x >> 4) % liveCount);
      TEST_ASSERT_TRUE(gRuns.release(live[k]));
      TEST_ASSERT_EQUAL(RunState::Reclaiming, gRuns.run(live[k]).state);
      live[k] = live[--liveCount];
    } else {
      gRuns.reclaim(gPool, 1 + (x >> 12) % 64);
    }
    if (step % 97 == 0) TEST_ASSERT_TRUE(conserved());
  }
  for (uint8_t k = 0; k < liveCount; ++k) gRuns.release(live[k]);
  while (gRuns.reclaimPendingRuns() != 0) gRuns.reclaim(gPool, 64);
  TEST_ASSERT_EQUAL_UINT16(Pool::kPageCount, gPool.freeCount());
  TEST_ASSERT_EQUAL_UINT16(Runs::kRunCount, gRuns.freeCount());
  TEST_ASSERT_TRUE(conserved());
}

void test_no_heap() {
  const unsigned long before = gHeapAllocations;
  setup();
  for (uint8_t i = 0; i < 8; ++i) gRuns.release(sealedRun(8 * 9));
  TEST_ASSERT_TRUE(gJob.submitIfPending(gS));
  while (gJob.running()) gS.runGrain(0);
  TEST_ASSERT_EQUAL_UINT32(0, gHeapAllocations - before);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_queue_order_and_unit_budget);
  RUN_TEST(test_pins_and_shared_runs_hold_reclamation);
  RUN_TEST(test_job_runs_in_bounded_grains_beside_display);
  RUN_TEST(test_writing_interleaved_with_reclaim);
  RUN_TEST(test_no_heap);
  return UNITY_END();
}
