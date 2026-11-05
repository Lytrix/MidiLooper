//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// fixed-memory-engine milestone E1: storage core (page pool, global run/manifest tables, row reservations).
// Proves page conservation by ownership, bounded reclamation, refcount release-once, explicit init over poisoned
// memory, zero heap allocation after init, and design clause CE-A1 (reservations track outstanding owed rows; an owed
// row is never refused; new input is refused only at its first row).

#include <unity.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>

#include "ContentEngine/EngineCore.h"

// ---- Heap witness: every operator new in this process is counted ----------------------------------------------------
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

// Small capacities so exhaustion, directory pages and reclamation are all reached quickly.
using SmallPool = PagePool</*pages*/ 96, /*rows per page*/ 8>;  // 128 B pages: 64 ids per directory page
using SmallRuns = RunTable</*runs*/ 24, SmallPool, /*inline pages*/ 4, /*dir pages*/ 4>;
using SmallManifests = ManifestTable</*manifests*/ 16, /*runs per manifest*/ 8>;
using SmallReservations = RowReservations<SmallPool, SmallRuns>;

static SmallPool gPool;
static SmallRuns gRuns;
static SmallManifests gManifests;
static SmallReservations gRes;

static ProductionPool gProdPool;
static ProductionRuns gProdRuns;
static ProductionManifests gProdManifests;
static ProductionReservations gProdRes;

static void poison(void* p, size_t n) { std::memset(p, 0xA5, n); }

static void initSmall(uint32_t fixedReserve = 0) {
  poison(static_cast<void*>(&gPool), sizeof gPool);
  poison(static_cast<void*>(&gRuns), sizeof gRuns);
  poison(static_cast<void*>(&gManifests), sizeof gManifests);
  poison(static_cast<void*>(&gRes), sizeof gRes);
  gPool.init();
  gRuns.init();
  gManifests.init();
  gRes.init(gPool, gRuns, fixedReserve);
}

static PassEvent row(uint32_t tick, uint8_t status, uint8_t note, uint32_t noteId) {
  PassEvent e;
  e.tick = tick;
  e.noteId = noteId;
  e.seq = tick;
  e.status = status;
  e.data1 = note;
  e.data2 = 100;
  e.origin = 0;
  return e;
}

static bool conserved() { return checkConservation(gPool, gRuns, gManifests); }

// Simple deterministic PRNG (no <random>: keeps the heap witness honest).
static uint32_t gSeed = 0x12345678u;
static uint32_t rnd() {
  gSeed ^= gSeed << 13;
  gSeed ^= gSeed >> 17;
  gSeed ^= gSeed << 5;
  return gSeed;
}

void setUp() {}
void tearDown() {}

// ---- Page pool --------------------------------------------------------------------------------------------------------

void test_pool_conserves_pages_by_owner() {
  initSmall();
  const unsigned long heapBefore = gHeapAllocations;
  PageId held[SmallPool::kPageCount];
  for (uint16_t i = 0; i < SmallPool::kPageCount; ++i) {
    held[i] = gPool.acquire(static_cast<PageOwner>(i % 3));
    TEST_ASSERT_NOT_EQUAL(kNoPage, held[i]);
  }
  TEST_ASSERT_EQUAL(kNoPage, gPool.acquire(0));
  TEST_ASSERT_EQUAL_UINT16(0, gPool.freeCount());
  TEST_ASSERT_TRUE(gPool.checkFreeStack());
  // Wrong owner and double release are refused and change nothing.
  TEST_ASSERT_FALSE(gPool.release(held[0], 1));
  for (uint16_t i = 0; i < SmallPool::kPageCount; i += 2) TEST_ASSERT_TRUE(gPool.release(held[i], i % 3));
  TEST_ASSERT_FALSE(gPool.release(held[0], 0));
  TEST_ASSERT_TRUE(gPool.checkFreeStack());
  TEST_ASSERT_EQUAL_UINT16(SmallPool::kPageCount / 2, gPool.freeCount());
  TEST_ASSERT_EQUAL_UINT32(SmallPool::kPageCount / 2, gPool.countOwnedBy(0) + gPool.countOwnedBy(1) +
                                                          gPool.countOwnedBy(2));
  TEST_ASSERT_EQUAL_UINT32(0, gHeapAllocations - heapBefore);
}

// ---- pagesToAppend is exactly what appends consume ---------------------------------------------------------------------

void test_pages_to_append_matches_actual_consumption() {
  for (uint32_t start = 0; start < 200; start += 7) {
    for (uint32_t extra = 1; extra < 400; extra += 13) {
      initSmall();
      const RunId run = gRuns.allocate(0, 0);
      for (uint32_t i = 0; i < start; ++i) TEST_ASSERT_EQUAL(AppendResult::Ok, gRuns.append(run, row(i, 0x90, 60, i), gPool));
      const auto& r = gRuns.run(run);
      const uint32_t predicted = pagesToAppend<SmallRuns::kRowsPerPage, SmallRuns::kIdsPerDirPage,
                                               SmallRuns::kInlinePages>(r.pageCount, r.rowCount, extra);
      const uint16_t freeBefore = gPool.freeCount();
      uint32_t appended = 0;
      for (; appended < extra; ++appended)
        if (gRuns.append(run, row(start + appended, 0x90, 60, appended), gPool) != AppendResult::Ok) break;
      if (appended == extra) TEST_ASSERT_EQUAL_UINT32(predicted, static_cast<uint32_t>(freeBefore - gPool.freeCount()));
      TEST_ASSERT_TRUE(conserved());
    }
  }
}

// ---- Runs: directory pages, read-back, bounded reclamation -------------------------------------------------------------

void test_run_directory_readback_and_bounded_reclaim() {
  initSmall();
  const unsigned long heapBefore = gHeapAllocations;
  const RunId run = gRuns.allocate(3, 1);
  // 4 inline pages + one directory page listing 64 more: 68 data pages = 544 rows; stop below the pool size.
  const uint32_t rows = 8 * 70;
  for (uint32_t i = 0; i < rows; ++i) TEST_ASSERT_EQUAL(AppendResult::Ok, gRuns.append(run, row(i, 0x90, i & 127, i), gPool));
  TEST_ASSERT_EQUAL_UINT8(2, gRuns.run(run).dirPageCount);  // data page 68 opened a second directory page
  for (uint32_t i = 0; i < rows; ++i) TEST_ASSERT_EQUAL_UINT32(i, gRuns.rowAt(run, i, gPool).noteId);
  TEST_ASSERT_TRUE(conserved());
  TEST_ASSERT_TRUE(gRuns.seal(run));
  TEST_ASSERT_TRUE(gRuns.release(run));
  // Reclaim in grains of 5 units; conservation holds after every grain and the descriptor is freed at the end.
  TEST_ASSERT_EQUAL_UINT16(1, gRuns.reclaimPendingRuns());
  TEST_ASSERT_EQUAL_UINT32(gRuns.heldPages(run) + 1, gRuns.reclaimPendingUnits());
  uint32_t steps = 0;
  while (gRuns.reclaimPendingRuns() != 0) {
    TEST_ASSERT_TRUE(gRuns.reclaim(gPool, 5) <= 5);
    TEST_ASSERT_TRUE(conserved());
    ++steps;
    TEST_ASSERT_TRUE(steps < 100);
  }
  TEST_ASSERT_EQUAL_UINT32(0, gRuns.reclaimPendingUnits());
  TEST_ASSERT_EQUAL_UINT16(SmallPool::kPageCount, gPool.freeCount());
  TEST_ASSERT_EQUAL_UINT16(SmallRuns::kRunCount, gRuns.freeCount());
  TEST_ASSERT_TRUE(conserved());
  // The reused descriptor gets a new serial.
  const uint32_t oldSerial = 1;
  const RunId again = gRuns.allocate(0, 0);
  TEST_ASSERT_TRUE(gRuns.run(again).serial > oldSerial);
  TEST_ASSERT_EQUAL_UINT32(0, gHeapAllocations - heapBefore);
}

// ---- Manifests: refcounts release each run exactly once ----------------------------------------------------------------

void test_manifest_release_releases_runs_once() {
  initSmall();
  const RunId a = gRuns.allocate(0, 0);
  const RunId b = gRuns.allocate(0, 1);
  for (uint32_t i = 0; i < 20; ++i) gRuns.append(a, row(i, 0x90, 60, i), gPool);
  for (uint32_t i = 0; i < 20; ++i) gRuns.append(b, row(i, 0x90, 61, i), gPool);
  gRuns.seal(a);
  gRuns.seal(b);
  const RunId m1Runs[] = {a};
  const RunId m2Runs[] = {a, b};
  const ManifestId m1 = gManifests.create(0, 1, 768, m1Runs, 1, gRuns);
  const ManifestId m2 = gManifests.create(0, 2, 768, m2Runs, 2, gRuns);
  TEST_ASSERT_TRUE(gManifests.manifest(m2).identity > gManifests.manifest(m1).identity);
  // Writers drop their own references once the manifests hold the runs.
  gRuns.release(a);
  gRuns.release(b);
  TEST_ASSERT_TRUE(conserved());
  gManifests.pin(m2);
  TEST_ASSERT_FALSE(gManifests.release(m2, gRuns));  // pinned: still live
  TEST_ASSERT_TRUE(gManifests.unpin(m2, gRuns));     // last reference
  TEST_ASSERT_EQUAL(RunState::Reclaiming, gRuns.run(b).state);
  TEST_ASSERT_EQUAL(RunState::Sealed, gRuns.run(a).state);  // still listed by m1
  TEST_ASSERT_TRUE(gManifests.release(m1, gRuns));
  TEST_ASSERT_EQUAL(RunState::Reclaiming, gRuns.run(a).state);
  TEST_ASSERT_EQUAL_UINT16(2, gRuns.reclaimPendingRuns());
  while (gRuns.reclaimPendingRuns() != 0) gRuns.reclaim(gPool, 64);
  TEST_ASSERT_EQUAL_UINT16(SmallPool::kPageCount, gPool.freeCount());
  TEST_ASSERT_EQUAL_UINT16(SmallManifests::kManifestCount, gManifests.freeCount());
  TEST_ASSERT_TRUE(conserved());
}

// ---- Explicit init over poisoned memory at production capacity --------------------------------------------------------

void test_production_core_initializes_over_poison_without_heap() {
  poison(static_cast<void*>(&gProdPool), sizeof gProdPool);
  poison(static_cast<void*>(&gProdRuns), sizeof gProdRuns);
  poison(static_cast<void*>(&gProdManifests), sizeof gProdManifests);
  poison(static_cast<void*>(&gProdRes), sizeof gProdRes);
  const unsigned long heapBefore = gHeapAllocations;
  gProdPool.init();
  gProdRuns.init();
  gProdManifests.init();
  gProdRes.init(gProdPool, gProdRuns, Config::kCompactionReservePages + Config::kIndexReservePages);
  TEST_ASSERT_EQUAL_UINT16(Config::kPageCount, gProdPool.freeCount());
  TEST_ASSERT_EQUAL_UINT16(Config::kRunCount, gProdRuns.freeCount());
  TEST_ASSERT_EQUAL_UINT16(Config::kManifestCount, gProdManifests.freeCount());
  TEST_ASSERT_EQUAL_UINT32(Config::kContentQuotaPages, gProdRes.contentFreePages());
  TEST_ASSERT_TRUE(checkConservation(gProdPool, gProdRuns, gProdManifests));
  // A full-quota take at production capacity: 3,222 pages of content rows (incl. directory pages) then refusal.
  const RunId run = gProdRuns.allocate(0, 0);
  uint32_t accepted = 0;
  while (gProdRes.accept(run, row(accepted, 0xB0, 1, accepted), 0) == Admission::Ok) ++accepted;
  TEST_ASSERT_TRUE(gProdRes.check());
  TEST_ASSERT_EQUAL_UINT32(Config::kCompactionReservePages + Config::kIndexReservePages, gProdPool.freeCount());
  TEST_ASSERT_TRUE(checkConservation(gProdPool, gProdRuns, gProdManifests));
  TEST_ASSERT_EQUAL_UINT32(0, gHeapAllocations - heapBefore);
  const RegionBytes regions = productionRegions();
  TEST_ASSERT_EQUAL_UINT32(4151296u, static_cast<uint32_t>(regions.poolPages));
  TEST_MESSAGE("production regions measured (bytes) — see the test log line below");
  char line[200];
  std::snprintf(line, sizeof line, "regions: pages=%u pool_meta=%u runs=%u (desc %u B) manifests=%u (desc %u B) res=%u rows=%u",
                static_cast<unsigned>(regions.poolPages), static_cast<unsigned>(regions.poolMetadata),
                static_cast<unsigned>(regions.runTable), static_cast<unsigned>(sizeof(ProductionRuns::Run)),
                static_cast<unsigned>(regions.manifestTable), static_cast<unsigned>(sizeof(ProductionManifests::Manifest)),
                static_cast<unsigned>(regions.reservations), static_cast<unsigned>(accepted));
  TEST_MESSAGE(line);
}

// ---- CE-A1: reservations ---------------------------------------------------------------------------------------------

// Long sequential take: NoteOn (owes its NoteOff) then NoteOff, repeated until capacity. Completed notes consume rows;
// each next NoteOn must find room for itself and its NoteOff. At refusal no NoteOff is owed and every note is complete.
void test_reservations_long_sequential_take() {
  initSmall();
  const unsigned long heapBefore = gHeapAllocations;
  const RunId run = gRuns.allocate(0, 0);
  uint32_t notes = 0;
  for (;;) {
    if (gRes.accept(run, row(notes * 10, 0x90, 60, notes), 1) != Admission::Ok) break;
    TEST_ASSERT_EQUAL_UINT16(1, gRes.outstanding(run));
    TEST_ASSERT_TRUE(gRes.writeOwed(run, row(notes * 10 + 5, 0x80, 60, notes)));
    TEST_ASSERT_TRUE(gRes.check());
    ++notes;
  }
  TEST_ASSERT_EQUAL_UINT16(0, gRes.outstanding(run));
  TEST_ASSERT_EQUAL_UINT32(notes * 2, gRuns.run(run).rowCount);
  // The whole pool is content now, minus at most one partial page worth of rows.
  TEST_ASSERT_TRUE(gRuns.run(run).rowCount + 2 * SmallRuns::kRowsPerPage >=
                   (SmallPool::kPageCount - gRuns.run(run).dirPageCount) * SmallRuns::kRowsPerPage);
  TEST_ASSERT_TRUE(conserved());
  TEST_ASSERT_EQUAL_UINT32(0, gHeapAllocations - heapBefore);
}

// 128 simultaneous open notes, then other messages until refusal: every one of the 128 NoteOffs still writes.
void test_reservations_max_open_notes_survive_exhaustion() {
  initSmall();
  const RunId run = gRuns.allocate(0, 0);
  for (uint32_t n = 0; n < Config::kMaxOpenNotesPerWriter; ++n)
    TEST_ASSERT_EQUAL(Admission::Ok, gRes.accept(run, row(n, 0x90, n & 127, n), 1));
  uint32_t fillers = 0;
  while (gRes.accept(run, row(200 + fillers, 0xB0, 7, 0), 0) == Admission::Ok) ++fillers;
  TEST_ASSERT_TRUE(fillers > 0);
  TEST_ASSERT_TRUE(gRes.check());
  // A new NoteOn is refused at its first row; nothing is written for it.
  const uint32_t rowsBefore = gRuns.run(run).rowCount;
  TEST_ASSERT_EQUAL(Admission::DeniedCapacity, gRes.accept(run, row(999, 0x90, 1, 999), 1));
  TEST_ASSERT_EQUAL_UINT32(rowsBefore, gRuns.run(run).rowCount);
  for (uint32_t n = 0; n < Config::kMaxOpenNotesPerWriter; ++n) {
    TEST_ASSERT_TRUE(gRes.writeOwed(run, row(5000 + n, 0x80, n & 127, n)));
    TEST_ASSERT_TRUE(gRes.check());
  }
  TEST_ASSERT_EQUAL_UINT16(0, gRes.outstanding(run));
  TEST_ASSERT_TRUE(conserved());
}

// Closing runs keep their own owed NoteOffs while the next interval's run fills the pool; Stop then writes every owed
// row of every run and seals them.
void test_reservations_closing_runs_and_stop_at_exhaustion() {
  initSmall();
  const RunId first = gRuns.allocate(0, 0);
  for (uint32_t n = 0; n < 50; ++n) TEST_ASSERT_EQUAL(Admission::Ok, gRes.accept(first, row(n, 0x90, n, n), 1));
  const RunId second = gRuns.allocate(0, 0);  // wrap: `first` is now closing
  for (uint32_t n = 0; n < 40; ++n) TEST_ASSERT_EQUAL(Admission::Ok, gRes.accept(second, row(n, 0x90, 60, 100 + n), 1));
  const RunId third = gRuns.allocate(0, 0);   // second wrap: two closing runs
  uint32_t filled = 0;
  while (gRes.accept(third, row(filled, 0x90, 70, 1000 + filled), 1) == Admission::Ok) ++filled;
  TEST_ASSERT_TRUE(filled > 0);
  TEST_ASSERT_TRUE(gRes.check());
  // Some NoteOffs of the closing runs arrive late, interleaved with refusals of new input.
  for (uint32_t n = 0; n < 25; ++n) {
    TEST_ASSERT_TRUE(gRes.writeOwed(first, row(800 + n, 0x80, n, n)));
    TEST_ASSERT_EQUAL(Admission::DeniedCapacity, gRes.accept(third, row(900, 0xB0, 1, 0), 0));
  }
  // Stop: every owed row of every run, then seal.
  const RunId order[] = {first, second, third};
  for (RunId r : order) {
    while (gRes.outstanding(r) > 0) TEST_ASSERT_TRUE(gRes.writeOwed(r, row(9000, 0x80, 0, 0)));
    TEST_ASSERT_TRUE(gRuns.seal(r));
  }
  TEST_ASSERT_TRUE(gRes.check());
  TEST_ASSERT_TRUE(conserved());
}

// Random interleaving: 3 writers, each with an active run and up to 2 closing runs; new notes, NoteOffs, other
// messages, wraps, Stops, and release + reclamation of finished takes. Invariant R and conservation hold throughout,
// an owed row is never refused, and refusals do happen (the test is not vacuous).
void test_reservations_random_interleaving() {
  initSmall(/*fixedReserve*/ 6);
  const unsigned long heapBefore = gHeapAllocations;
  struct Writer {
    RunId runs[3];   // [0] active, [1..2] closing
    uint8_t runCount;
    RunId committed[SmallRuns::kRunCount];  // sealed runs of the current take (content until the take is dropped)
    uint8_t committedCount;
  } writers[3] = {};
  uint32_t refusals = 0, owedWrites = 0, accepted = 0;
  gSeed = 0xC0FFEEu;
  for (uint32_t step = 0; step < 200000; ++step) {
    Writer& w = writers[rnd() % 3];
    const uint32_t op = rnd() % 100;
    if (w.runCount == 0) {
      const RunId r = gRuns.allocate(0, 0);
      if (r != kNoRun) {
        w.runs[0] = r;
        w.runCount = 1;
      }
      continue;
    }
    if (op < 40) {  // new NoteOn on the active run (writer cap: 128 open across its runs)
      uint32_t open = 0;
      for (uint8_t i = 0; i < w.runCount; ++i) open += gRes.outstanding(w.runs[i]);
      if (open < Config::kMaxOpenNotesPerWriter) {
        if (gRes.accept(w.runs[0], row(step, 0x90, step & 127, step), 1) == Admission::Ok)
          ++accepted;
        else
          ++refusals;
      }
    } else if (op < 75) {  // an owed NoteOff on any of the writer's runs
      const RunId r = w.runs[rnd() % w.runCount];
      if (gRes.outstanding(r) > 0) {
        TEST_ASSERT_TRUE(gRes.writeOwed(r, row(step, 0x80, 0, 0)));
        ++owedWrites;
      }
    } else if (op < 85) {  // other channel message
      if (gRes.accept(w.runs[0], row(step, 0xE0, 0, 0), 0) != Admission::Ok) ++refusals;
    } else if (op < 99) {  // wrap: the active run becomes closing (if it owes rows) or seals
      if (w.runCount < 3) {
        const RunId next = gRuns.allocate(0, 0);
        if (next != kNoRun) {
          const RunId old = w.runs[0];
          for (uint8_t i = w.runCount; i > 0; --i) w.runs[i] = w.runs[i - 1];
          w.runs[0] = next;
          ++w.runCount;
          (void)old;
        }
      }
    } else {  // Stop: write everything owed, seal all runs, release them to reclamation (the take is undone)
      for (uint8_t i = 0; i < w.runCount; ++i) {
        const RunId r = w.runs[i];
        while (gRes.outstanding(r) > 0) {
          TEST_ASSERT_TRUE(gRes.writeOwed(r, row(step, 0x80, 0, 0)));
          ++owedWrites;
        }
        TEST_ASSERT_TRUE(gRuns.seal(r));
        gRuns.release(r);
      }
      w.runCount = 0;
      for (uint8_t i = 0; i < w.committedCount; ++i)
        gRuns.release(w.committed[i]);
      w.committedCount = 0;
    }
    // Closing runs that owe nothing seal (their manifests would publish here).
    for (uint8_t i = 1; i < w.runCount;) {
      if (gRes.outstanding(w.runs[i]) == 0) {
        gRuns.seal(w.runs[i]);
        w.committed[w.committedCount++] = w.runs[i];
        for (uint8_t k = i; k + 1 < w.runCount; ++k) w.runs[k] = w.runs[k + 1];
        --w.runCount;
      } else {
        ++i;
      }
    }
    // The reclaim worker: one bounded grain per step.
    gRuns.reclaim(gPool, 4);
    if (step % 997 == 0) {
      TEST_ASSERT_TRUE(gRes.check());
      TEST_ASSERT_TRUE(conserved());
    }
  }
  TEST_ASSERT_TRUE(gRes.check());
  TEST_ASSERT_TRUE(conserved());
  TEST_ASSERT_TRUE(refusals > 0);
  TEST_ASSERT_TRUE(owedWrites > 1000);
  TEST_ASSERT_TRUE(accepted > 1000);
  TEST_ASSERT_EQUAL_UINT32(0, gHeapAllocations - heapBefore);
}

// The witness does see an allocation, so the zero results above are not vacuous.
void test_heap_witness_self_check() {
  const unsigned long before = gHeapAllocations;
  int* p = new int(7);
  delete p;
  TEST_ASSERT_EQUAL_UINT32(1, gHeapAllocations - before);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_pool_conserves_pages_by_owner);
  RUN_TEST(test_pages_to_append_matches_actual_consumption);
  RUN_TEST(test_run_directory_readback_and_bounded_reclaim);
  RUN_TEST(test_manifest_release_releases_runs_once);
  RUN_TEST(test_production_core_initializes_over_poison_without_heap);
  RUN_TEST(test_reservations_long_sequential_take);
  RUN_TEST(test_reservations_max_open_notes_survive_exhaustion);
  RUN_TEST(test_reservations_closing_runs_and_stop_at_exhaustion);
  RUN_TEST(test_reservations_random_interleaving);
  RUN_TEST(test_heap_witness_self_check);
  return UNITY_END();
}
