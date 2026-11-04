//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// fixed-memory-engine E5: restart-checkpoint ownership. One fixed table per slot, filled by the sweep job, stamped with
// the manifest identity swept and retired by the next sweep. A mid-loop cold start at any window boundary of any steady
// playback cycle resumes from the nearest usable checkpoint and must compose exactly the windows uninterrupted playback
// composes there, also while the sweep is still running; with a complete table it replays less than one spacing.
// Both jobs run in grains of <= 128 source rows, pin the manifest while they read it and release it when they end;
// a replaced sweep publishes nothing more, also when replaced between the grains of one window.

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
using Manifests = ManifestTable<16, 8>;
using Sweep = CheckpointSweep<TestComposer, Table, Window, Manifests, Runs>;
using Cold = ColdStart<TestComposer, Table, Window, Manifests, Runs>;
using State = Table::EntryState;

static Pool gPool;
static Runs gRuns;
static EditIndexStore<Pool, Runs> gIndex;
static TestComposer gRef, gSweepComposer, gCold;
static Window gWin, gScratch;
static Table gTable;
static Sweep gSweep;
static Cold gColdJob;
static Manifests gManifests;
constexpr uint32_t kGrainRows = 128;

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
  gManifests.init();
  gIndex.init(gPool, gRuns);
  gRef.init(gPool, gRuns, &gIndex);
  gSweepComposer.init(gPool, gRuns, &gIndex);
  gCold.init(gPool, gRuns, &gIndex);
  gTable.init();
  gSweep = Sweep{};
  gColdJob = Cold{};
}
void tearDown() {}

static ManifestId makeManifest(const SourceRun* src, uint8_t count, uint32_t revision = 1) {
  RunId ids[8];
  for (uint8_t i = 0; i < count; ++i) ids[i] = src[i].run;
  const ManifestId m = gManifests.create(0, revision, kLoop, ids, count, gRuns);
  TEST_ASSERT_NOT_EQUAL(kNoManifest, m);
  return m;
}
static uint32_t identityOf(ManifestId m) { return gManifests.manifest(m).identity; }

/// Uninterrupted playback from the loop start, composed in grains of random size: ref[c][w] is window w of cycle c.
static std::vector<std::vector<Snapshot>> reference(const SourceRun* src, uint8_t count, uint32_t identity) {
  std::vector<std::vector<Snapshot>> ref(kCycles);
  TEST_ASSERT_TRUE(gRef.begin(src, count, kLoop, identity));
  uint32_t g = identity;
  for (uint32_t c = 0; c < kCycles; ++c) {
    while (gRef.openWindow(gWin, kWin)) {
      while (!gRef.composeRows(gWin, 1 + (g = g * 1103515245u + 12345u) % 7)) {
      }
      ref[c].push_back(snap(gWin));
    }
    gRef.nextCycle();
  }
  gRef.end();
  return ref;
}

static void runSweep(ManifestId m) {
  TEST_ASSERT_EQUAL(Sweep::Status::Running, gSweep.start(gSweepComposer, gTable, gManifests, gRuns, 0, m, kWin, kSpacing));
  while (gSweep.step(gWin, kGrainRows) == Sweep::Status::Running) {
  }
  TEST_ASSERT_EQUAL(Sweep::Status::Done, gSweep.status());
  TEST_ASSERT_FALSE(gSweep.pinned());
}

/// Cold start at (cycle, window) through the grain job and compare every following window with uninterrupted
/// playback; returns the windows replayed.
static uint32_t checkColdStart(ManifestId m, const std::vector<std::vector<Snapshot>>& ref, uint32_t cycle,
                               uint32_t window) {
  gTrap = true;
  const unsigned long heapBefore = gHeapAllocations;
  Cold::Status st = gColdJob.start(gCold, gTable, gManifests, gRuns, 0, m, window * kWin, cycle, kWin);
  while (st == Cold::Status::Running) st = gColdJob.step(gScratch, kGrainRows);
  TEST_ASSERT_EQUAL(Cold::Status::Done, st);
  TEST_ASSERT_TRUE(gColdJob.pinned());
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
  gColdJob.release();
  TEST_ASSERT_FALSE(gColdJob.pinned());
  return gColdJob.replayed();
}

static uint8_t randomSource(uint32_t& seed, SourceRun* src, uint32_t maxNotes = 14) {
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
    for (uint32_t k = 0, n = rnd() % maxNotes; k < n; ++k) {
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

// Every steady cold start equals uninterrupted playback: from an empty table (warm-up from the loop start), at every
// grain of a running sweep (only usable checkpoints are resumed) and at every window of three steady cycles from a
// complete table (replay < one spacing). The sweep proves periodicity and composes at most two loops.
void test_cold_start_equals_uninterrupted_playback_random() {
  uint32_t seed = 0x5EED;
  uint32_t starts = 0, maxSweepWindows = 0, periodic = 0;
  for (uint32_t trial = 0; trial < 25; ++trial) {
    setUp();
    SourceRun src[8];
    const uint8_t count = randomSource(seed, src);
    const ManifestId m = makeManifest(src, count);
    const auto ref = reference(src, count, identityOf(m));

    gTable.beginSweep(0, identityOf(m), kLoop, kSpacing);  // empty table: warm-up path
    TEST_ASSERT_EQUAL_UINT32(kWindowsPerLoop + 5, checkColdStart(m, ref, 2, 5));

    TEST_ASSERT_EQUAL(Sweep::Status::Running, gSweep.start(gSweepComposer, gTable, gManifests, gRuns, 0, m, kWin, kSpacing));
    uint32_t step = 0;
    while (gSweep.step(gWin, kGrainRows) == Sweep::Status::Running) {
      if (gSweepComposer.windowOpen()) continue;  // cold starts between windows (the sweep's window is its own)
      const uint32_t w = (seed = seed * 1103515245u + 12345u) % kWindowsPerLoop;
      checkColdStart(m, ref, 1 + step % 3, w);
      ++step;
      ++starts;
    }
    TEST_ASSERT_EQUAL(Sweep::Status::Done, gSweep.status());
    TEST_ASSERT_FALSE(gSweep.pinned());
    TEST_ASSERT_TRUE(gSweep.windows() <= 2 * kWindowsPerLoop);
    if (gSweep.windows() > maxSweepWindows) maxSweepWindows = gSweep.windows();
    periodic += gSweep.periodic() ? 1 : 0;
    TEST_ASSERT_TRUE_MESSAGE(gSweep.periodic(), "below OUT-POLY saturation the composition is periodic from cycle 1");

    for (uint32_t c = 1; c < kCycles; ++c)
      for (uint32_t w = 0; w < kWindowsPerLoop; ++w) {
        const uint32_t replayed = checkColdStart(m, ref, c, w);
        const uint8_t k = static_cast<uint8_t>(w * kWin / kSpacing);
        if (gTable.state(0, k) == State::Usable) TEST_ASSERT_TRUE(replayed < kSpacing / kWin);
        ++starts;
      }
    TEST_ASSERT_EQUAL_UINT16(0, gManifests.manifest(m).readerPins);
  }
  TEST_ASSERT_TRUE(starts > 600);
  TEST_ASSERT_TRUE(maxSweepWindows > kWindowsPerLoop);  // some source needed cycle-1 convergence windows
}

// A note held across the loop end: cycle 0 of a cold composition does not carry it, steady playback does. The sweep
// must not promote the cold cycle-0 checkpoint while the carried note still sounds.
void test_sweep_keeps_cold_checkpoints_out_until_steady() {
  const RunId r = makeRun(RunKind::kRecord, {}, {{1, 1, 60, kLoop - 100, kLoop + 4000}});
  const SourceRun src[] = {{r, gRuns.run(r).rowCount}};
  const ManifestId m = makeManifest(src, 1);
  const auto ref = reference(src, 1, identityOf(m));
  TEST_ASSERT_EQUAL(Sweep::Status::Running, gSweep.start(gSweepComposer, gTable, gManifests, gRuns, 0, m, kWin, kSpacing));
  while (gSweep.windows() < kWindowsPerLoop) gSweep.step(gWin, kGrainRows);
  TEST_ASSERT_EQUAL(State::Usable, gTable.state(0, 0));       // position 0 of cycle 1
  TEST_ASSERT_EQUAL(State::Provisional, gTable.state(0, 1));  // 3072: cold, the held note is missing
  TEST_ASSERT_EQUAL_UINT8(0, gTable.entry(0, 1).admittedCount);
  while (gSweep.step(gWin, kGrainRows) == Sweep::Status::Running) {
  }
  TEST_ASSERT_EQUAL(Sweep::Status::Done, gSweep.status());
  TEST_ASSERT_TRUE(gSweep.periodic());
  TEST_ASSERT_EQUAL_UINT8(1, gTable.entry(0, 1).admittedCount);     // steady: the held note sounds at 3072
  TEST_ASSERT_EQUAL_UINT32(kWindowsPerLoop + 4, gSweep.windows());  // converged at 6144 (note ended at 4000)
  for (uint8_t k = 0; k < 4; ++k) TEST_ASSERT_EQUAL(State::Usable, gTable.state(0, k));
  checkColdStart(m, ref, 2, 3);
}

// Convergence within two cycles (design.md E5): hand-specified sources for each mechanism that carries state across
// the loop end. Each sweep proves periodicity, stays within two loops, and cold starts equal uninterrupted playback.
void test_convergence_within_two_cycles_per_mechanism() {
  struct Case {
    const char* name;
    std::vector<N> base, later;
    bool edit;
  };
  const std::vector<Case> cases = {
      {"Q-LEN-1 held-loop note (> 2L), re-struck every loop", {{1, 1, 60, kLoop - 10, kLoop - 10 + 2 * kLoop + 500}}, {}, false},
      {"Q-LEN-1 note of exactly L", {{1, 1, 60, 3000, 3000 + kLoop}}, {}, false},
      {"wrap hold shortened by a later pass in the next cycle", {{1, 1, 60, kLoop - 200, kLoop + 5000}}, {{2, 1, 60, 2000, 2400}}, false},
      {"wrap hold hidden at tick 0 (Q-OVL-3)", {{1, 1, 62, kLoop - 50, kLoop + 900}}, {{2, 1, 62, 0, 300}}, false},
      {"OVL-9 across cycles", {{1, 1, 64, 700, 900}}, {{2, 1, 64, kLoop - 100, kLoop + 1000}}, false},
      {"edit hide of a wrap-held note plus its replacement", {{1, 1, 65, kLoop - 300, kLoop + 2000}}, {{1, 1, 65, kLoop - 300, kLoop + 600}}, true},
  };
  for (const auto& c : cases) {
    setUp();
    SourceRun src[2];
    const RunId a = makeRun(RunKind::kRecord, {}, c.base);
    src[0] = {a, gRuns.run(a).rowCount};
    uint8_t count = 1;
    if (!c.later.empty()) {
      const RunId b = makeRun(c.edit ? RunKind::kEdit : RunKind::kOverdub, c.edit ? std::vector<uint32_t>{1} : std::vector<uint32_t>{}, c.later);
      src[count++] = {b, gRuns.run(b).rowCount};
    }
    const ManifestId m = makeManifest(src, count);
    const auto ref = reference(src, count, identityOf(m));
    runSweep(m);
    TEST_ASSERT_TRUE_MESSAGE(gSweep.periodic(), c.name);
    TEST_ASSERT_TRUE_MESSAGE(gSweep.windows() <= 2 * kWindowsPerLoop, c.name);
    for (uint32_t w = 0; w < kWindowsPerLoop; ++w) checkColdStart(m, ref, 1 + w % 3, w);
  }
}

// Revision transition: the slot commits B = A + an overdub while A's sweep runs. A's sweep is superseded and publishes
// nothing; B's sweep converges within two loops and B's cold starts equal B's uninterrupted playback.
void test_revision_transition_sweeps_the_new_manifest() {
  const RunId a = makeRun(RunKind::kRecord, {}, {{1, 1, 60, kLoop - 400, kLoop + 3000}, {2, 1, 62, 100, 900}});
  const RunId b = makeRun(RunKind::kOverdub, {}, {{3, 1, 60, 1500, 1800}, {4, 1, 67, kLoop - 20, kLoop + 7000}});
  const SourceRun srcA[] = {{a, gRuns.run(a).rowCount}};
  const SourceRun srcB[] = {{a, gRuns.run(a).rowCount}, {b, gRuns.run(b).rowCount}};
  const ManifestId mA = makeManifest(srcA, 1, 1);
  const ManifestId mB = makeManifest(srcB, 2, 2);
  const auto refB = reference(srcB, 2, identityOf(mB));
  TEST_ASSERT_EQUAL(Sweep::Status::Running, gSweep.start(gSweepComposer, gTable, gManifests, gRuns, 0, mA, kWin, kSpacing));
  for (int i = 0; i < 5; ++i) gSweep.step(gWin, kGrainRows);
  Sweep sweepB;
  TEST_ASSERT_EQUAL(Sweep::Status::Running, sweepB.start(gCold, gTable, gManifests, gRuns, 0, mB, kWin, kSpacing));
  TEST_ASSERT_EQUAL(Sweep::Status::Superseded, gSweep.step(gWin, kGrainRows));
  TEST_ASSERT_FALSE(gSweep.pinned());
  while (sweepB.step(gScratch, kGrainRows) == Sweep::Status::Running) {
  }
  TEST_ASSERT_TRUE(sweepB.periodic());
  TEST_ASSERT_TRUE(sweepB.windows() <= 2 * kWindowsPerLoop);
  TEST_ASSERT_NULL(gTable.nearest(0, identityOf(mA), 0));
  for (uint32_t w = 0; w < kWindowsPerLoop; ++w) checkColdStart(mB, refB, 2, w);
}

// A new sweep retires the slot's table at once; the old sweep ends Superseded, publishes nothing more and releases
// its pin.
void test_new_sweep_supersedes_the_old_one() {
  const RunId r = makeRun(RunKind::kRecord, {}, {{1, 1, 60, 100, 2000}});
  const SourceRun src[] = {{r, gRuns.run(r).rowCount}};
  const ManifestId m = makeManifest(src, 1);
  TEST_ASSERT_EQUAL(Sweep::Status::Running, gSweep.start(gSweepComposer, gTable, gManifests, gRuns, 0, m, kWin, kSpacing));
  while (gSweep.windows() < kWindowsPerLoop) gSweep.step(gWin, kGrainRows);
  TEST_ASSERT_NOT_NULL(gTable.nearest(0, identityOf(m), 0));
  TEST_ASSERT_EQUAL_UINT16(1, gManifests.manifest(m).readerPins);
  gTable.beginSweep(0, 999, kLoop, kSpacing);  // the slot committed another manifest
  TEST_ASSERT_NULL(gTable.nearest(0, identityOf(m), 0));
  TEST_ASSERT_EQUAL(Sweep::Status::Superseded, gSweep.step(gWin, kGrainRows));
  for (uint8_t k = 0; k < 8; ++k) TEST_ASSERT_EQUAL(State::Empty, gTable.state(0, k));
  TEST_ASSERT_TRUE(gTable.current(0, 999));
  TEST_ASSERT_EQUAL_UINT16(0, gManifests.manifest(m).readerPins);
}

// Replaced between the grains of one window (after composing, before the window's checkpoint is published): nothing
// is published into the replaced slot.
void test_replacement_between_grains_publishes_nothing() {
  std::vector<N> notes;
  for (uint32_t k = 0; k < 40; ++k) notes.push_back({k + 1, 1, static_cast<uint8_t>(40 + k), 10 + 70 * k, 60 + 70 * k});
  const RunId r = makeRun(RunKind::kRecord, {}, notes);
  const SourceRun src[] = {{r, gRuns.run(r).rowCount}};
  const ManifestId m = makeManifest(src, 1);
  TEST_ASSERT_EQUAL(Sweep::Status::Running, gSweep.start(gSweepComposer, gTable, gManifests, gRuns, 0, m, kWin, kSpacing));
  gSweep.step(gWin, kGrainRows);  // window [0, 1536): 44 rows, one grain
  for (int i = 0; i < 3; ++i) TEST_ASSERT_EQUAL(Sweep::Status::Running, gSweep.step(gWin, 4));  // window 2 in progress
  TEST_ASSERT_TRUE(gSweepComposer.windowOpen());
  gTable.beginSweep(0, 4242, kLoop, kSpacing);
  TEST_ASSERT_EQUAL(Sweep::Status::Superseded, gSweep.step(gWin, kGrainRows));  // would have completed 3072
  TEST_ASSERT_EQUAL(State::Empty, gTable.state(0, 1));
  TEST_ASSERT_FALSE(gSweep.pinned());
}

// Reclamation safety: the slot drops the manifest while its sweep runs. The pin keeps the manifest and its runs
// alive until the sweep ends; then the manifest is freed and its runs go to reclaim. Cancel releases the same way,
// including the composer's edit-index lease.
void test_pin_keeps_source_alive_until_the_sweep_ends() {
  const RunId a = makeRun(RunKind::kRecord, {}, {{1, 1, 60, 100, 2000}, {2, 1, 61, 300, 900}});
  const RunId e = makeRun(RunKind::kEdit, {2}, {{2, 1, 61, 300, 600}});
  const SourceRun src[] = {{a, gRuns.run(a).rowCount}, {e, gRuns.run(e).rowCount}};
  const ManifestId m = makeManifest(src, 2);
  gRuns.release(a);  // drop the writers' references: the manifest is the only owner
  gRuns.release(e);
  const uint16_t freeBefore = gManifests.freeCount();
  TEST_ASSERT_EQUAL(Sweep::Status::Running, gSweep.start(gSweepComposer, gTable, gManifests, gRuns, 0, m, kWin, kSpacing));
  gSweep.step(gWin, kGrainRows);
  TEST_ASSERT_FALSE(gManifests.release(m, gRuns));  // the slot replaced it: only the sweep's pin remains
  TEST_ASSERT_TRUE(gManifests.manifest(m).live);
  TEST_ASSERT_EQUAL(RunState::Sealed, gRuns.run(a).state);
  while (gSweep.step(gWin, kGrainRows) == Sweep::Status::Running) {
  }
  TEST_ASSERT_EQUAL(Sweep::Status::Done, gSweep.status());
  TEST_ASSERT_EQUAL_UINT16(freeBefore + 1, gManifests.freeCount());
  TEST_ASSERT_EQUAL(RunState::Reclaiming, gRuns.run(a).state);
  TEST_ASSERT_EQUAL(RunState::Reclaiming, gRuns.run(e).state);
  TEST_ASSERT_EQUAL_UINT8(0, gIndex.slotsInUse() > 0 ? gIndex.pins(0) : 0);

  setUp();
  const RunId b = makeRun(RunKind::kRecord, {}, {{1, 1, 60, 100, 2000}});
  const RunId f = makeRun(RunKind::kEdit, {1}, {{1, 1, 60, 100, 500}});
  const SourceRun src2[] = {{b, gRuns.run(b).rowCount}, {f, gRuns.run(f).rowCount}};
  const ManifestId m2 = makeManifest(src2, 2);
  TEST_ASSERT_EQUAL(Sweep::Status::Running, gSweep.start(gSweepComposer, gTable, gManifests, gRuns, 0, m2, kWin, kSpacing));
  gSweep.step(gWin, 1);
  TEST_ASSERT_EQUAL_UINT16(1, gManifests.manifest(m2).readerPins);
  gSweep.cancel();
  TEST_ASSERT_EQUAL(Sweep::Status::Superseded, gSweep.status());
  TEST_ASSERT_EQUAL_UINT16(0, gManifests.manifest(m2).readerPins);
  TEST_ASSERT_FALSE(gIndex.holds(identityOf(m2)) && gIndex.pins(0) != 0);
}

// OUT-POLY saturation (more than T notes sounding): admission is first come, so which notes a cycle drops depends on
// the notes carried in, and uninterrupted playback itself settles only after a few cycles. The sweep detects it
// (`periodic()` false) and never promotes cold captures; a cold start then reconstructs the cycle-1 state
// deterministically, within T. Open owner question Q-OUT-2 (design.md E5).
void test_saturated_composition_is_flagged_aperiodic() {
  uint32_t seed = 77;
  auto rnd = [&seed]() {
    seed ^= seed << 13;
    seed ^= seed >> 17;
    seed ^= seed << 5;
    return seed;
  };
  std::vector<N> notes;
  uint32_t id = 1;
  for (int k = 0; k < 30; ++k) {
    const uint32_t on = rnd() % kLoop;
    notes.push_back({id++, 1, static_cast<uint8_t>(30 + k), on, on + 2000 + rnd() % kLoop});
  }
  std::vector<N> later;
  for (int k = 0; k < 8; ++k) {
    const uint32_t on = rnd() % kLoop;
    later.push_back({id++, 1, static_cast<uint8_t>(30 + rnd() % 30), on, on + 100 + rnd() % 3000});
  }
  const RunId a = makeRun(RunKind::kRecord, {}, notes);
  const RunId b = makeRun(RunKind::kOverdub, {}, later);
  const SourceRun src[] = {{a, gRuns.run(a).rowCount}, {b, gRuns.run(b).rowCount}};
  const ManifestId m = makeManifest(src, 2);
  const auto ref = reference(src, 2, identityOf(m));
  bool cycleDiffers = false;
  for (uint32_t w = 0; w < kWindowsPerLoop; ++w) {
    Snapshot one = ref[1][w];
    one.cycleBase = ref[3][w].cycleBase;
    cycleDiffers |= !same(one, ref[3][w]);
  }
  TEST_ASSERT_TRUE(cycleDiffers);  // the fixture saturates: uninterrupted playback is not periodic from cycle 1
  runSweep(m);
  TEST_ASSERT_FALSE(gSweep.periodic());
  TEST_ASSERT_TRUE(gSweep.windows() <= 2 * kWindowsPerLoop);
  for (uint8_t k = 0; k < 4; ++k)  // a provisional (cold) capture is never resumed
    if (gTable.state(0, k) == State::Provisional)
      TEST_ASSERT_TRUE(gTable.nearest(0, identityOf(m), k * kSpacing) != &gTable.entry(0, k));
  for (uint32_t w = 0; w < kWindowsPerLoop; ++w) {  // cold starts equal cycle 1 of uninterrupted playback
    Cold::Status st = gColdJob.start(gCold, gTable, gManifests, gRuns, 0, m, w * kWin, 1, kWin);
    while (st == Cold::Status::Running) st = gColdJob.step(gScratch, kGrainRows);
    TEST_ASSERT_EQUAL(Cold::Status::Done, st);
    TEST_ASSERT_TRUE(gCold.composeNext(gWin, kWin));
    TEST_ASSERT_TRUE(same(snap(gWin), ref[1][w]));
    TEST_ASSERT_TRUE(gWin.carryCount <= 16);
    gColdJob.release();
  }
}

// The jobs build a non-resident edit index in grains before composing: no composer `begin` builds synchronously, and
// the result equals a table swept after a synchronous build.
void test_jobs_build_the_edit_index_in_grains() {
  std::vector<N> base;
  std::vector<uint32_t> hides;
  for (uint32_t k = 0; k < 600; ++k) base.push_back({k + 1, 1, static_cast<uint8_t>(30 + k % 60), 10 + 20 * k, 15 + 20 * k});
  for (uint32_t k = 0; k < 600; k += 2) hides.push_back(k + 1);
  const RunId a = makeRun(RunKind::kRecord, {}, base);
  const RunId e = makeRun(RunKind::kEdit, hides, {{3, 1, 31, 50, 400}});
  const SourceRun src[] = {{a, gRuns.run(a).rowCount}, {e, gRuns.run(e).rowCount}};
  const ManifestId m = makeManifest(src, 2);
  TEST_ASSERT_FALSE(gIndex.holds(identityOf(m)));
  TEST_ASSERT_EQUAL(Sweep::Status::Running, gSweep.start(gSweepComposer, gTable, gManifests, gRuns, 0, m, kWin, kSpacing));
  uint32_t indexGrains = 0;
  while (!gSweepComposer.windowOpen() && gSweep.windows() == 0) {
    gSweep.step(gWin, kGrainRows);
    ++indexGrains;
  }
  TEST_ASSERT_TRUE(indexGrains > 2);  // 300 hides: gather, heapify, sort and collapse take several grains
  while (gSweep.step(gWin, kGrainRows) == Sweep::Status::Running) {
  }
  TEST_ASSERT_EQUAL(Sweep::Status::Done, gSweep.status());
  TEST_ASSERT_EQUAL_UINT32(0, gIndex.syncBuilds());
  TestComposer::Checkpoint swept[4];
  for (uint8_t k = 0; k < 4; ++k) swept[k] = gTable.entry(0, k);

  Cold::Status st = gColdJob.start(gCold, gTable, gManifests, gRuns, 0, m, 5 * kWin, 2, kWin);
  while (st == Cold::Status::Running) st = gColdJob.step(gScratch, kGrainRows);
  TEST_ASSERT_EQUAL(Cold::Status::Done, st);
  TEST_ASSERT_EQUAL_UINT32(0, gIndex.syncBuilds());
  gColdJob.release();

  reference(src, 2, identityOf(m));  // a composer `begin` on a resident index: no build
  TEST_ASSERT_EQUAL_UINT32(0, gIndex.syncBuilds());

  setUp();  // the same source with a synchronously built index sweeps to the same checkpoints
  const RunId a2 = makeRun(RunKind::kRecord, {}, base);
  const RunId e2 = makeRun(RunKind::kEdit, hides, {{3, 1, 31, 50, 400}});
  const SourceRun src2[] = {{a2, gRuns.run(a2).rowCount}, {e2, gRuns.run(e2).rowCount}};
  const ManifestId m2 = makeManifest(src2, 2);
  TestComposer::Checkpoint warm;
  TEST_ASSERT_TRUE(gRef.begin(src2, 2, kLoop, identityOf(m2)));  // synchronous build
  TEST_ASSERT_EQUAL_UINT32(1, gIndex.syncBuilds());
  gRef.end();
  runSweep(m2);
  for (uint8_t k = 0; k < 4; ++k) {
    warm = gTable.entry(0, k);
    warm.identity = swept[k].identity;
    TEST_ASSERT_TRUE(sameState(warm, swept[k]));
  }
  checkColdStart(m2, reference(src2, 2, identityOf(m2)), 1, 3);
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
  RUN_TEST(test_convergence_within_two_cycles_per_mechanism);
  RUN_TEST(test_revision_transition_sweeps_the_new_manifest);
  RUN_TEST(test_new_sweep_supersedes_the_old_one);
  RUN_TEST(test_replacement_between_grains_publishes_nothing);
  RUN_TEST(test_pin_keeps_source_alive_until_the_sweep_ends);
  RUN_TEST(test_saturated_composition_is_flagged_aperiodic);
  RUN_TEST(test_jobs_build_the_edit_index_in_grains);
  RUN_TEST(test_table_lookup_and_unusable_entries);
  RUN_TEST(test_production_table_within_budget);
  return UNITY_END();
}
