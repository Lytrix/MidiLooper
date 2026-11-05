//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// fixed-memory-engine E5: worst-case grain durations on the device (plan revision 3 § 7.1: "E4 measures it per job
// type on the device, and the worst measured value replaces 500 µs in every bound"). Production placement: the page
// pool (pages + edit-index partition) in PSRAM, tables and windows in RAM2. Each grain is timed with the DWT cycle
// counter; the Clock ISR and USB interrupts stay enabled, as in the product. Run on the board:
//   pio test -e teensy41 -f test_content_engine_grain_bench
// On the host the suite only checks that the workload builds (timings are meaningless there).

#include <unity.h>

#include <cstdint>
#include <cstdio>

#include "ContentEngine/CheckpointTable.h"
#include "ContentEngine/Composer.h"
#include "ContentEngine/EngineCore.h"
#include "ContentEngine/SpanWindow.h"
#include "ContentEngine/WorkerScheduler.h"

#if defined(__IMXRT1062__)
#include <Arduino.h>
#define BENCH_PSRAM EXTMEM
#define BENCH_RAM2 DMAMEM
static inline uint32_t cycles() { return ARM_DWT_CYCCNT; }
static inline double toMicros(uint32_t c) { return static_cast<double>(c) / (F_CPU_ACTUAL / 1000000.0); }
#else
#define BENCH_PSRAM
#define BENCH_RAM2
static inline uint32_t cycles() { return 0; }
static inline double toMicros(uint32_t) { return 0; }
#endif

using namespace ContentEngine;

using Pool = ProductionPool;
using Runs = ProductionRuns;
using TestComposer = ProductionComposer;
using Window = SpanWindow<Config::kWindowEntries>;
using Manifests = ProductionManifests;
using Table = CheckpointTable<TestComposer::Checkpoint, 1, 8>;
using Sweep = CheckpointSweep<TestComposer, Table, Window, Manifests, Runs>;
using Cold = ColdStart<TestComposer, Table, Window, Manifests, Runs>;

BENCH_PSRAM static Pool gPool;
BENCH_RAM2 static Runs gRuns;
BENCH_RAM2 static Manifests gManifests;
BENCH_RAM2 static ProductionEditIndex gIndex;
BENCH_RAM2 static Window gWin;
BENCH_RAM2 static TestComposer gComp;
BENCH_RAM2 static Table gTable;
static Sweep gSweep;
static Cold gCold;

constexpr uint32_t kLoop = 768 * 8, kWinTicks = 1536;
constexpr uint8_t kPasses = 59;            // + 1 edit run = 60 committed runs
constexpr uint32_t kNotesPerPass = 47;     // 59 x 47 = 2,773 NoteOns in window 0 (CE-B1 admission 2,816)
constexpr uint32_t kHides = 4096;

struct Stat {
  uint32_t n = 0, max = 0;
  uint64_t sum = 0;
  void add(uint32_t c) {
    ++n;
    sum += c;
    if (c > max) max = c;
  }
};

static void report(const char* name, const Stat& s) {
  char line[160];
  snprintf(line, sizeof(line), "GRAIN,%s,n=%lu,max_us=%.1f,avg_us=%.1f", name, static_cast<unsigned long>(s.n),
           toMicros(s.max), s.n ? toMicros(static_cast<uint32_t>(s.sum / s.n)) : 0.0);
#if defined(__IMXRT1062__)
  Serial.println(line);
#endif
  TEST_MESSAGE(line);
}

static PassEvent row(uint32_t tick, uint32_t id, uint8_t status, uint8_t d1) {
  PassEvent e;
  e.tick = tick;
  e.noteId = id;
  e.seq = 0;
  e.status = status;
  e.data1 = d1;
  e.data2 = (status & 0xF0) == 0x90 ? 100 : 0;
  e.origin = 0;
  return e;
}

static ManifestId gManifest;

/// 59 dense passes (all NoteOns of each pass in window 0, sorted, short notes, every pitch reused across passes so
/// the occlusion look-ahead works) plus one edit run with 4,096 hides (one index probe per NoteOn).
static void buildWorkload() {
  gPool.init();
  TEST_ASSERT_TRUE(gIndex.init(gPool, gRuns));
  gRuns.init();
  gManifests.init();
  RunId ids[kPasses + 1];
  uint32_t id = 1;
  uint32_t x = 7;
  for (uint8_t p = 0; p < kPasses; ++p) {
    const RunId r = gRuns.allocate(0, p == 0 ? RunKind::kRecord : RunKind::kOverdub);
    TEST_ASSERT_NOT_EQUAL(kNoRun, r);
    for (uint32_t k = 0; k < kNotesPerPass; ++k) {
      const uint32_t on = k * (kWinTicks / kNotesPerPass) + p % 8;
      const uint8_t pitch = static_cast<uint8_t>(24 + (k * 7 + p) % 80);
      TEST_ASSERT_EQUAL(AppendResult::Ok, gRuns.append(r, row(on, id, 0x90, pitch), gPool));
      TEST_ASSERT_EQUAL(AppendResult::Ok, gRuns.append(r, row(on + 20, id, 0x80, pitch), gPool));
      ++id;
    }
    gRuns.seal(r);
    ids[p] = r;
  }
  const RunId e = gRuns.allocate(0, RunKind::kEdit);
  for (uint32_t k = 0; k < kHides; ++k) {
    x = x * 1103515245u + 12345u;
    TEST_ASSERT_EQUAL(AppendResult::Ok, gRuns.append(e, row(0, 1 + (x >> 8) % (id * 4), kHideNoteStatus, 0), gPool));
  }
  gRuns.seal(e);
  ids[kPasses] = e;
  gManifest = gManifests.create(0, 1, kLoop, ids, kPasses + 1, gRuns);
  TEST_ASSERT_NOT_EQUAL(kNoManifest, gManifest);
}

/// Clustered window for the OVL-2/6 look-ahead: every pass has 6 NoteOns inside each 9-tick cluster (clusters every
/// 64 ticks), each pass on 6 pitches of its own per cluster. `rotating`: the pitches move every cluster, so over the
/// loop every pass starts notes on most pitches and the onset-pitch mask cannot skip it (adversarial worst case);
/// otherwise each pass keeps its 6 pitches (the mask skips every pass without the NoteOn's pitch).
static void clusteredWindow(bool rotating, const char* name) {
  gPool.init();
  TEST_ASSERT_TRUE(gIndex.init(gPool, gRuns));
  gRuns.init();
  gManifests.init();
  RunId ids[kPasses];
  uint32_t id = 1;
  constexpr uint32_t kClusters = 8;  // 59 x 8 x 6 = 2,832 NoteOns in window 0
  for (uint8_t p = 0; p < kPasses; ++p) {
    const RunId r = gRuns.allocate(0, p == 0 ? RunKind::kRecord : RunKind::kOverdub);
    for (uint32_t c = 0; c < kClusters; ++c) {  // rows in tick order: the six NoteOns, then their NoteOffs
      for (uint32_t k = 0; k < 6; ++k) {
        const uint8_t pitch = static_cast<uint8_t>(30 + (p * 6 + k + (rotating ? c * 7 : 0)) % 90);
        TEST_ASSERT_EQUAL(AppendResult::Ok, gRuns.append(r, row(c * 64 + k + (p % 3), id + k, 0x90, pitch), gPool));
      }
      for (uint32_t k = 0; k < 6; ++k) {
        const uint8_t pitch = static_cast<uint8_t>(30 + (p * 6 + k + (rotating ? c * 7 : 0)) % 90);
        TEST_ASSERT_EQUAL(AppendResult::Ok, gRuns.append(r, row(c * 64 + k + (p % 3) + 40, id + k, 0x80, pitch), gPool));
      }
      id += 6;
    }
    gRuns.seal(r);
    ids[p] = r;
  }
  const ManifestId mc = gManifests.create(0, 1, kLoop, ids, kPasses, gRuns);
  SourceRun src[kPasses];
  for (uint8_t i = 0; i < kPasses; ++i) src[i] = {ids[i], gRuns.run(ids[i]).rowCount};
  gComp.init(gPool, gRuns, &gIndex);
  TEST_ASSERT_TRUE(gComp.begin(src, kPasses, kLoop, gManifests.manifest(mc).identity));
  Stat cluster;
  TEST_ASSERT_TRUE(gComp.openWindow(gWin, kWinTicks));
  bool done;
  do {
    const uint32_t c0 = cycles();
    done = gComp.composeRows(gWin, 128);
    cluster.add(cycles() - c0);
  } while (!done);
  report(name, cluster);
  char la[140];
  snprintf(la, sizeof(la), "GRAIN,%s_lookahead_rows=%lu,max_per_noteon=%lu", name,
           static_cast<unsigned long>(gComp.stats().lookaheadRowsLastWindow),
           static_cast<unsigned long>(gComp.stats().maxLookaheadRowsPerNoteOn));
  TEST_MESSAGE(la);
}

void setUp() {}
void tearDown() {}

void test_grain_durations() {
  buildWorkload();
  gComp.init(gPool, gRuns, &gIndex);
  gTable.init();

  // Index build: reserve + buildStep(256 units), 4,096 hides.
  Stat index;
  {
    const auto& m = gManifests.manifest(gManifest);
    SourceRun src[Manifests::kMaxRuns];
    for (uint8_t i = 0; i < m.runCount; ++i) src[i] = {m.runs[i], gRuns.run(m.runs[i]).rowCount};
    IndexLease lease;
    uint32_t c0 = cycles();
    TEST_ASSERT_EQUAL(IndexResult::Building, gIndex.reserve(m.identity, src, m.runCount, lease));
    index.add(cycles() - c0);
    IndexResult r;
    do {
      c0 = cycles();
      r = gIndex.buildStep(lease, src, m.runCount, 256);
      index.add(cycles() - c0);
    } while (r == IndexResult::Building);
    TEST_ASSERT_EQUAL(IndexResult::Ready, r);

    // Seek grain: begin (binary searches over 60 runs, wrap pre-pass, index pin).
    Stat seek;
    for (int k = 0; k < 8; ++k) {
      c0 = cycles();
      TEST_ASSERT_TRUE(gComp.begin(src, m.runCount, kLoop, m.identity));
      seek.add(cycles() - c0);
    }
    gIndex.release(lease);
    report("index_build_256", index);
    report("compose_seek", seek);
  }

  // Window compose: grains of 128 source rows over the densest window, then the rest of the loop.
  Stat compose, open;
  uint32_t grainsWindow0 = 0;
  for (uint32_t w = 0; w < kLoop / kWinTicks; ++w) {
    uint32_t c0 = cycles();
    TEST_ASSERT_TRUE(gComp.openWindow(gWin, kWinTicks));
    open.add(cycles() - c0);
    bool done;
    do {
      c0 = cycles();
      done = gComp.composeRows(gWin, 128);
      compose.add(cycles() - c0);
      if (w == 0) ++grainsWindow0;
    } while (!done);
    if (w == 0) {
      char la[120];
      snprintf(la, sizeof(la), "GRAIN,window0_lookahead_rows=%lu,max_per_noteon=%lu",
               static_cast<unsigned long>(gComp.stats().lookaheadRowsLastWindow),
               static_cast<unsigned long>(gComp.stats().maxLookaheadRowsPerNoteOn));
      TEST_MESSAGE(la);
    }
  }
  report("compose_128_rows", compose);
  report("compose_open_window", open);
  char line[96];
  snprintf(line, sizeof(line), "GRAIN,window0_grains=%lu,entries=%u", static_cast<unsigned long>(grainsWindow0), gWin.count);
  TEST_MESSAGE(line);

  // Checkpoint capture and resume.
  Stat cap, res;
  TestComposer::Checkpoint cp;
  {
    const auto& m = gManifests.manifest(gManifest);
    SourceRun src[Manifests::kMaxRuns];
    for (uint8_t i = 0; i < m.runCount; ++i) src[i] = {m.runs[i], gRuns.run(m.runs[i]).rowCount};
    TEST_ASSERT_TRUE(gComp.begin(src, m.runCount, kLoop, m.identity));
    TEST_ASSERT_TRUE(gComp.composeNext(gWin, kWinTicks));
    for (int k = 0; k < 8; ++k) {
      uint32_t c0 = cycles();
      gComp.capture(cp);
      cap.add(cycles() - c0);
    }
    for (int k = 0; k < 8; ++k) {
      uint32_t c0 = cycles();
      gComp.resume(src, m.runCount, kLoop, m.identity, cp);
      res.add(cycles() - c0);
    }
    gComp.end();
  }
  report("checkpoint_capture", cap);
  report("checkpoint_resume", res);

  // Sweep job grains (index resident) and a cold start replay.
  Stat sweep, cold;
  TEST_ASSERT_EQUAL(Sweep::Status::Running, gSweep.start(gComp, gTable, gManifests, gRuns, 0, gManifest, kWinTicks, 3072));
  for (;;) {
    const uint32_t c0 = cycles();
    const Sweep::Status s = gSweep.step(gWin, 128);
    sweep.add(cycles() - c0);
    if (s != Sweep::Status::Running) break;
  }
  TEST_ASSERT_EQUAL(Sweep::Status::Done, gSweep.status());
  static TestComposer coldComposer;
  coldComposer.init(gPool, gRuns, &gIndex);
  Cold::Status st = gCold.start(coldComposer, gTable, gManifests, gRuns, 0, gManifest, 4608, 3, kWinTicks);
  while (st == Cold::Status::Running) {
    const uint32_t c0 = cycles();
    st = gCold.step(gWin, 128);
    cold.add(cycles() - c0);
  }
  TEST_ASSERT_EQUAL(Cold::Status::Done, st);
  gCold.release();
  report("sweep_step", sweep);
  report("cold_start_step", cold);

  // Scheduler decision overhead with a full 32-job table (job bodies empty).
  static WorkerScheduler<32> sched;
  sched.init(500, 50000);
  uint8_t jid;
  for (uint8_t k = 0; k < 32; ++k)
    sched.submit({k < 16 ? JobClass::Hard : JobClass::Background, 1000000u + k * 1000u, 0xFFFFFFFF,
                  [](void*) { return JobStep::Continue; }, nullptr},
                 jid);
  Stat pick;
  for (uint32_t k = 0; k < 256; ++k) {
    const uint32_t c0 = cycles();
    sched.runGrain(k * 500);
    pick.add(cycles() - c0);
  }
  report("scheduler_pick_32_jobs", pick);

  clusteredWindow(false, "compose_128_rows_clustered");
  clusteredWindow(true, "compose_128_rows_clustered_rotating");
}

#if defined(__IMXRT1062__)
void setup() {
  ARM_DEMCR |= ARM_DEMCR_TRCENA;
  ARM_DWT_CTRL |= ARM_DWT_CTRL_CYCCNTENA;
  Serial.begin(115200);
  while (!Serial && millis() < 15000) {
  }
  delay(500);  // the runner opens the port after enumeration
  UNITY_BEGIN();
  RUN_TEST(test_grain_durations);
  UNITY_END();
}
void loop() {}
#else
int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_grain_durations);
  return UNITY_END();
}
#endif
