//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// fixed-memory-engine E5: restart-checkpoint ownership. One fixed table per slot, filled by the sweep job, stamped with
// the manifest identity swept and retired by the next sweep. A mid-loop cold start at any window boundary of any steady
// playback cycle resumes from the nearest usable checkpoint and must compose exactly the windows uninterrupted playback
// composes there, also while the sweep is still running; with a complete table it replays less than one spacing.

#include <unity.h>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <new>
#include <vector>

#include "ContentEngine/CheckpointTable.h"
#include "ContentEngine/Composer.h"
#include "ContentEngine/EngineCore.h"
#include "ContentEngine/SpanWindow.h"

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

using Pool = PagePool<2048, 64>;
using Runs = RunTable<32, Pool>;
using TestComposer = Composer<Pool, Runs>;
using Window = SpanWindow<3072>;
using Table = CheckpointTable<TestComposer::Checkpoint, /*slots*/ 2, /*per slot*/ 8>;
using Sweep = CheckpointSweep<TestComposer, Table, Window>;
using State = Table::EntryState;

static Pool gPool;
static Runs gRuns;
static EditIndexStore<Pool, Runs> gIndex;
static TestComposer gRef, gSweepComposer, gCold;
static Window gWin, gScratch;
static Table gTable;
static Sweep gSweep;

constexpr uint32_t kLoop = 768 * 16;
constexpr uint32_t kWin = 1536;
constexpr uint32_t kSpacing = 3072;  // two windows per spacing: four checkpoints per loop
constexpr uint32_t kCycles = 4;
constexpr uint32_t kWindowsPerLoop = kLoop / kWin;

struct N {
  uint32_t id;
  uint8_t channel, pitch;
  uint32_t on, off;
};

static PassEvent row(uint32_t tick, uint32_t id, uint8_t status, uint8_t d1, uint8_t d2, uint8_t origin) {
  PassEvent e;
  e.tick = tick;
  e.noteId = id;
  e.seq = 0;
  e.status = status;
  e.data1 = d1;
  e.data2 = d2;
  e.origin = origin;
  return e;
}

static RunId makeRun(uint8_t kind, const std::vector<uint32_t>& hides, const std::vector<N>& notes) {
  const RunId r = gRuns.allocate(0, kind);
  for (uint32_t id : hides) gRuns.append(r, row(0, id, kHideNoteStatus, 0, 0, 0), gPool);
  struct Ev {
    uint32_t tick;
    bool on;
    N n;
  };
  std::vector<Ev> ev;
  for (const auto& n : notes) {
    ev.push_back({n.on, true, n});
    ev.push_back({n.off, false, n});
  }
  std::stable_sort(ev.begin(), ev.end(), [](const Ev& a, const Ev& b) {
    if (a.tick != b.tick) return a.tick < b.tick;
    return !a.on && b.on;
  });
  for (const auto& e : ev) {
    const uint8_t flag = !e.on && e.n.off - e.n.on >= kLoop ? kOriginHeldLoop : 0;
    TEST_ASSERT_EQUAL(AppendResult::Ok,
                      gRuns.append(r, row(e.tick, e.n.id, static_cast<uint8_t>((e.on ? 0x90 : 0x80) | (e.n.channel - 1)),
                                          e.n.pitch, e.on ? 100 : 0, flag),
                                   gPool));
  }
  gRuns.seal(r);
  return r;
}

struct Snapshot {
  uint32_t start, cycleBase, count, carryCount;
  std::vector<SpanEntry> entries;
  std::vector<CarryRef> carry;
};

static Snapshot snap(const Window& w) {
  Snapshot s{w.start, w.cycleBase, w.count, w.carryCount, {}, {}};
  s.entries.assign(w.entries, w.entries + w.count);
  s.carry.assign(w.carry, w.carry + w.carryCount);
  return s;
}

static bool same(const Snapshot& a, const Snapshot& b) {
  if (a.start != b.start || a.cycleBase != b.cycleBase || a.count != b.count || a.carryCount != b.carryCount)
    return false;
  for (uint32_t i = 0; i < a.count; ++i)
    if (std::memcmp(&a.entries[i], &b.entries[i], sizeof(SpanEntry)) != 0) return false;
  for (uint32_t i = 0; i < a.carryCount; ++i)
    if (a.carry[i].noteId != b.carry[i].noteId || a.carry[i].runKey != b.carry[i].runKey) return false;
  return true;
}

void setUp() {
  gPool.init();
  gRuns.init();
  gIndex.init(gPool, gRuns);
  gRef.init(gPool, gRuns, &gIndex);
  gSweepComposer.init(gPool, gRuns, &gIndex);
  gCold.init(gPool, gRuns, &gIndex);
  gTable.init();
}
void tearDown() {}

/// Uninterrupted playback from the loop start: ref[c][w] is window w of cycle c.
static std::vector<std::vector<Snapshot>> reference(const SourceRun* src, uint8_t count, uint32_t identity) {
  std::vector<std::vector<Snapshot>> ref(kCycles);
  TEST_ASSERT_TRUE(gRef.begin(src, count, kLoop, identity));
  for (uint32_t c = 0; c < kCycles; ++c) {
    while (gRef.composeNext(gWin, kWin)) ref[c].push_back(snap(gWin));
    gRef.nextCycle();
  }
  gRef.end();
  return ref;
}

/// Cold start at (cycle, window) and compare every following window with uninterrupted playback; returns the replay.
static uint32_t checkColdStart(const SourceRun* src, uint8_t count, uint32_t identity,
                               const std::vector<std::vector<Snapshot>>& ref, uint32_t cycle, uint32_t window) {
  uint32_t replayed = 0;
  gTrap = true;
  const unsigned long heapBefore = gHeapAllocations;
  TEST_ASSERT_TRUE(coldStart(gCold, gTable, 0, src, count, kLoop, identity, window * kWin, cycle, kWin, gScratch,
                             replayed));
  for (uint32_t c = cycle, w = window; c < kCycles; ++c, w = 0) {
    for (; w < kWindowsPerLoop; ++w) {
      TEST_ASSERT_TRUE(gCold.composeNext(gWin, kWin));
      gTrap = false;
      TEST_ASSERT_TRUE_MESSAGE(same(snap(gWin), ref[c][w]), "cold start differs from uninterrupted playback");
      gTrap = true;
    }
    gCold.nextCycle();
  }
  gTrap = false;
  TEST_ASSERT_EQUAL_UINT32(0, gHeapAllocations - heapBefore);
  gCold.end();
  return replayed;
}

static uint8_t randomSource(uint32_t& seed, SourceRun* src) {
  auto rnd = [&seed]() {
    seed ^= seed << 13;
    seed ^= seed >> 17;
    seed ^= seed << 5;
    return seed;
  };
  const uint8_t passes = static_cast<uint8_t>(1 + rnd() % 5);
  uint32_t id = 1;
  std::vector<uint32_t> ids;
  for (uint8_t p = 0; p < passes; ++p) {
    std::vector<N> notes;
    bool used[17][12] = {};
    for (uint32_t k = 0, n = rnd() % 14; k < n; ++k) {
      const uint8_t ch = static_cast<uint8_t>(1 + rnd() % 3), pitch = static_cast<uint8_t>(rnd() % 12);
      if (used[ch][pitch]) continue;
      used[ch][pitch] = true;
      const uint32_t on = rnd() % kLoop;
      const uint32_t len = 1 + rnd() % (rnd() % 3 == 0 ? (rnd() % 2 ? 3 * kLoop : kLoop) : 2000);
      ids.push_back(id);
      notes.push_back({id++, ch, static_cast<uint8_t>(48 + pitch), on, on + len});
    }
    const RunId r = makeRun(p == 0 ? RunKind::kRecord : RunKind::kOverdub, {}, notes);
    src[p] = {r, gRuns.run(r).rowCount};
  }
  uint8_t count = passes;
  if (!ids.empty() && rnd() % 2) {
    const uint32_t target = ids[rnd() % ids.size()];
    const uint32_t on = rnd() % kLoop;
    const RunId r = makeRun(RunKind::kEdit, {target}, {{target, 1, 60, on, on + 900}});
    src[count++] = {r, gRuns.run(r).rowCount};
  }
  return count;
}

// Every steady cold start equals uninterrupted playback: from a complete table (replay < one spacing), from an empty
// one (warm-up from the loop start) and at every step of a running sweep (only usable checkpoints are resumed).
void test_cold_start_equals_uninterrupted_playback_random() {
  uint32_t seed = 0x5EED;
  uint32_t starts = 0, maxSweepWindows = 0;
  for (uint32_t trial = 0; trial < 25; ++trial) {
    setUp();
    SourceRun src[8];
    const uint8_t count = randomSource(seed, src);
    const uint32_t identity = 100 + trial;
    const auto ref = reference(src, count, identity);

    // Empty table: warm-up path.
    gTable.beginSweep(0, identity, kLoop, kSpacing);
    const uint32_t cold = checkColdStart(src, count, identity, ref, 2, 5);
    TEST_ASSERT_EQUAL_UINT32(kWindowsPerLoop + 5, cold);

    // During the sweep.
    TEST_ASSERT_EQUAL(Sweep::Status::Running,
                      gSweep.start(gSweepComposer, gTable, 0, src, count, kLoop, identity, kWin, kSpacing));
    uint32_t step = 0;
    while (gSweep.step(gWin) == Sweep::Status::Running) {
      const uint32_t w = (seed = seed * 1103515245u + 12345u) % kWindowsPerLoop;
      checkColdStart(src, count, identity, ref, 1 + step % 3, w);
      ++step;
      ++starts;
    }
    TEST_ASSERT_EQUAL(Sweep::Status::Done, gSweep.status());
    TEST_ASSERT_TRUE(gSweep.windows() <= 2 * kWindowsPerLoop);
    if (gSweep.windows() > maxSweepWindows) maxSweepWindows = gSweep.windows();

    // Complete table: every window of every steady cycle.
    for (uint32_t c = 1; c < kCycles; ++c)
      for (uint32_t w = 0; w < kWindowsPerLoop; ++w) {
        const uint32_t replayed = checkColdStart(src, count, identity, ref, c, w);
        const uint8_t k = static_cast<uint8_t>(w * kWin / kSpacing);
        if (gTable.state(0, k) == State::Usable) TEST_ASSERT_TRUE(replayed < kSpacing / kWin);
        ++starts;
      }
  }
  TEST_ASSERT_TRUE(starts > 600);
  TEST_ASSERT_TRUE(maxSweepWindows > kWindowsPerLoop);  // some source needed cycle-1 convergence windows
}

// A note held across the loop end: cycle 0 of a cold composition does not carry it, steady playback does. The sweep
// must not promote the cold cycle-0 checkpoint while the carried note still sounds.
void test_sweep_keeps_cold_checkpoints_out_until_steady() {
  const RunId r = makeRun(RunKind::kRecord, {}, {{1, 1, 60, kLoop - 100, kLoop + 4000}});
  const SourceRun src[] = {{r, gRuns.run(r).rowCount}};
  const auto ref = reference(src, 1, 9);
  TEST_ASSERT_EQUAL(Sweep::Status::Running, gSweep.start(gSweepComposer, gTable, 0, src, 1, kLoop, 9, kWin, kSpacing));
  for (uint32_t i = 0; i < kWindowsPerLoop; ++i) gSweep.step(gWin);
  TEST_ASSERT_EQUAL(State::Usable, gTable.state(0, 0));       // position 0 of cycle 1
  TEST_ASSERT_EQUAL(State::Provisional, gTable.state(0, 1));  // 3072: cold, the held note is missing
  TEST_ASSERT_EQUAL_UINT8(0, gTable.entry(0, 1).admittedCount);
  while (gSweep.step(gWin) == Sweep::Status::Running) {
  }
  TEST_ASSERT_EQUAL(Sweep::Status::Done, gSweep.status());
  TEST_ASSERT_EQUAL_UINT8(1, gTable.entry(0, 1).admittedCount);  // steady: the held note sounds at 3072
  TEST_ASSERT_EQUAL_UINT32(kWindowsPerLoop + 4, gSweep.windows());  // converged at 6144 (note ended at 4000)
  for (uint8_t k = 0; k < 4; ++k) TEST_ASSERT_EQUAL(State::Usable, gTable.state(0, k));
  checkColdStart(src, 1, 9, ref, 2, 3);
}

// A new sweep retires the slot's table at once; the old sweep ends Superseded and writes nothing more.
void test_new_sweep_supersedes_the_old_one() {
  const RunId r = makeRun(RunKind::kRecord, {}, {{1, 1, 60, 100, 2000}});
  const SourceRun src[] = {{r, gRuns.run(r).rowCount}};
  TEST_ASSERT_EQUAL(Sweep::Status::Running, gSweep.start(gSweepComposer, gTable, 0, src, 1, kLoop, 11, kWin, kSpacing));
  for (uint32_t i = 0; i < kWindowsPerLoop; ++i) gSweep.step(gWin);
  TEST_ASSERT_NOT_NULL(gTable.nearest(0, 11, 0));
  gTable.beginSweep(0, 12, kLoop, kSpacing);  // the slot committed manifest 12
  TEST_ASSERT_NULL(gTable.nearest(0, 11, 0));
  TEST_ASSERT_EQUAL(Sweep::Status::Superseded, gSweep.step(gWin));
  for (uint8_t k = 0; k < 8; ++k) TEST_ASSERT_EQUAL(State::Empty, gTable.state(0, k));
  TEST_ASSERT_TRUE(gTable.current(0, 12));
}

// Lookup: nearest usable at or before the position, stamped identity only, other slots independent; an overflowed
// checkpoint is stored unusable and skipped.
void test_table_lookup_and_unusable_entries() {
  TestComposer::Checkpoint cp{};
  cp.valid = true;
  gTable.beginSweep(0, 5, kLoop, kSpacing);
  gTable.beginSweep(1, 6, kLoop, kSpacing);
  for (uint32_t k = 0; k < 4; ++k) {
    cp.position = k * kSpacing;
    cp.valid = k != 2;
    TEST_ASSERT_TRUE(gTable.store(0, 5, cp, State::Usable));
  }
  TEST_ASSERT_EQUAL(State::Unusable, gTable.state(0, 2));
  TEST_ASSERT_EQUAL_UINT32(kSpacing, gTable.nearest(0, 5, 2 * kSpacing + 10)->position);  // 2 skipped
  TEST_ASSERT_EQUAL_UINT32(3 * kSpacing, gTable.nearest(0, 5, kLoop - 1)->position);
  TEST_ASSERT_NULL(gTable.nearest(0, 6, 0));   // wrong identity for slot 0
  TEST_ASSERT_NULL(gTable.nearest(1, 6, 0));   // slot 1 is empty
  cp.position = 100;                            // not a checkpoint position
  TEST_ASSERT_FALSE(gTable.store(0, 5, cp, State::Usable));
  TEST_ASSERT_FALSE(gTable.store(0, 4, cp, State::Usable));  // superseded identity
  gTable.retire(0);
  TEST_ASSERT_NULL(gTable.nearest(0, 5, 0));
}

// Production table: 64 slots x 8 checkpoints within the 262,144 B budget (plan § 4.1); zero heap throughout.
void test_production_table_within_budget() {
  TEST_ASSERT_TRUE(sizeof(ProductionCheckpoints) <= 262144u);
  TEST_ASSERT_EQUAL_UINT32(0, gHeapAllocations);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_cold_start_equals_uninterrupted_playback_random);
  RUN_TEST(test_sweep_keeps_cold_checkpoints_out_until_steady);
  RUN_TEST(test_new_sweep_supersedes_the_old_one);
  RUN_TEST(test_table_lookup_and_unusable_entries);
  RUN_TEST(test_production_table_within_budget);
  return UNITY_END();
}
