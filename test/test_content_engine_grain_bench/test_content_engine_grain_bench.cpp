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

#include "ContentEngine/Composition/CheckpointTable.h"
#include "ContentEngine/Composition/SpanWindowComposer.h"
#include "ContentEngine/EngineCore.h"
#include "ContentEngine/Content/ReclaimJob.h"
#include "ContentEngine/NoteEdit/NoteEditWriter.h"
#include "ContentEngine/History/SlotUndoHistory.h"
#include "ContentEngine/Playback/MidiOutputHub.h"
#include "ContentEngine/Composition/SpanWindow.h"
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
using Hist = History<Manifests, Runs>;
using Edits = EditWriter<Pool, Runs, Manifests, Hist>;
BENCH_RAM2 static Hist gHist;
BENCH_RAM2 static ProductionReservations gRes;
BENCH_RAM2 static Edits gEdits;
BENCH_PSRAM static EditChange gEditLog[Config::kEditLogEntries];
BENCH_PSRAM static EditNote gSel[Config::kEditSelection];
BENCH_RAM2 static EditChange gChanges[Config::kEditSelection];  // an action is built in RAM by the UI
using TickPlayer = WindowPlayer<Config::kMaxSoundingPerTrack>;
BENCH_RAM2 static TerminalScheduler<Config::kMaxSoundingPerTrack> gTickSched;
BENCH_RAM2 static TickPlayer gTickPlayer;
BENCH_RAM2 static OutputHub<> gHub;

constexpr uint32_t kLoop = 768 * 8, kWinTicks = 1536;
constexpr uint8_t kPasses = 59;            // + 1 edit run = 60 committed runs
constexpr uint32_t kNotesPerPass = 47;     // 59 x 47 = 2,773 NoteOns in window 0 (CE-B1 admission 2,816)
constexpr uint32_t kHides = 4096;
constexpr uint32_t kNoteTicks = 10;

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
/// the occlusion look-ahead works) plus one edit run with 4,096 hides (one index probe per NoteOn). Pass onsets are
/// staggered across each 32-tick slot and notes last 10 ticks, so at most about 19 instances are open at once: the
/// owner's realistic ceiling is 20 open notes per track (2026-11-05), enough to exercise the T = 16 voice limit.
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
      const uint32_t on = k * (kWinTicks / kNotesPerPass) + p * (kWinTicks / kNotesPerPass) / kPasses;
      const uint8_t pitch = static_cast<uint8_t>(24 + (k * 7 + p) % 80);
      TEST_ASSERT_EQUAL(AppendResult::Ok, gRuns.append(r, row(on, id, 0x90, pitch), gPool));
      TEST_ASSERT_EQUAL(AppendResult::Ok, gRuns.append(r, row(on + kNoteTicks, id, 0x80, pitch), gPool));
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
enum class TickLoad : uint8_t { Spread, Clustered, CcBurst };

/// The Clock tick path (E6.2, measure first): one track's windows for a whole loop, played tick by tick through the
/// player, the terminal scheduler and the output hub (every emit queued on 3 ports; ports drained outside the timed
/// region). Per tick: window rows processed, emits, time. Workloads: the spread window; 59 passes starting chords in
/// the same 9 ticks (OUT-2 selects 16); and the admitted maximum of continuous-controller rows on one tick (59 x 47),
/// which the composer limits to 32 per tick (owner 2026-11-05).
static void tickPath(TickLoad load, const char* name) {
  if (load == TickLoad::Spread) {
    buildWorkload();
  } else {
    gPool.init();
    TEST_ASSERT_TRUE(gIndex.init(gPool, gRuns));
    gRuns.init();
    gManifests.init();
    RunId ids[kPasses];
    uint32_t id = 1;
    for (uint8_t p = 0; p < kPasses; ++p) {
      const RunId r = gRuns.allocate(0, p == 0 ? RunKind::kRecord : RunKind::kOverdub);
      if (load == TickLoad::Clustered) {
        for (uint32_t c = 0; c < 8; ++c) {
          for (uint32_t k = 0; k < 6; ++k)
            gRuns.append(r, row(c * 64 + k + (p % 3), id + k, 0x90, static_cast<uint8_t>(30 + (p * 6 + k + c * 7) % 90)), gPool);
          for (uint32_t k = 0; k < 6; ++k)
            gRuns.append(r, row(c * 64 + k + (p % 3) + 40, id + k, 0x80, static_cast<uint8_t>(30 + (p * 6 + k + c * 7) % 90)), gPool);
          id += 6;
        }
      } else {
        static constexpr uint8_t kContinuous[] = {1, 2, 7, 10, 11, 74};  // continuous controllers only (MidiClass.h)
        for (uint32_t k = 0; k < kNotesPerPass; ++k) gRuns.append(r, row(100, id++, 0xB0, kContinuous[k % 6]), gPool);
      }
      gRuns.seal(r);
      ids[p] = r;
    }
    gManifest = gManifests.create(0, 1, kLoop, ids, kPasses, gRuns);
  }
  const auto& m = gManifests.manifest(gManifest);
  SourceRun src[Manifests::kMaxRuns];
  for (uint8_t i = 0; i < m.runCount; ++i) src[i] = {m.runs[i], gRuns.run(m.runs[i]).rowCount};
  gComp.init(gPool, gRuns, &gIndex);
  TEST_ASSERT_TRUE(gComp.begin(src, m.runCount, kLoop, m.identity));
  gTickSched.init();
  gTickPlayer.init(gTickSched);
  gHub.init();
  gHub.configure(0, kDinMessageMicros);
  gHub.configure(1, kUsbMessageMicros);
  gHub.configure(2, kUsbMessageMicros);
  TickPlayer::Emit out[255];
  OutputHub<>::Sent sent[32];
  Stat tick;
  uint32_t maxRows = 0, maxEmits = 0, rowsAtWorst = 0, totalRows = 0;
  uint64_t totalCycles = 0;
  while (gComp.composeNext(gWin, kWinTicks)) {
    uint16_t k = 0;
    uint8_t n = gTickPlayer.beginWindow(gWin, out, 255);
    for (uint32_t pos = gWin.start; pos < gWin.end; ++pos) {
      const uint32_t t = gWin.cycleBase + pos;
      uint32_t rows = 0;
      while (k < gWin.count && gWin.entries[k].tick <= pos) {
        ++rows;
        ++k;
      }
      const uint32_t now = t * 1040;  // ~1.04 ms per tick at 300 BPM, 192 PPQN
      const uint32_t c0 = cycles();
      n = static_cast<uint8_t>(n + gTickPlayer.tick(t, out + n, static_cast<uint8_t>(255 - n)));
      for (uint8_t i = 0; i < n; ++i)
        for (uint8_t p = 0; p < 3; ++p) {
          auto port = gHub.port(p);
          if (out[i].cls == 1)
            port.noteOn(now, static_cast<uint8_t>(0x90 | (out[i].channel - 1)), out[i].pitch, 100, out[i].noteId);
          else if (out[i].cls == 0)
            port.noteOff(now, out[i].noteId);
          else
            port.control(now, static_cast<uint8_t>(0xB0 | (out[i].channel - 1)), out[i].pitch, 0);
        }
      const uint32_t dt = cycles() - c0;
      tick.add(dt);
      totalCycles += dt;
      totalRows += rows;
      if (rows > maxRows) maxRows = rows;
      if (n > maxEmits) maxEmits = n;
      if (dt == tick.max) rowsAtWorst = rows;
      for (uint8_t p = 0; p < 3; ++p) gHub.port(p).transmit(now, sent, 32);  // the port drivers (not timed)
      n = 0;
    }
  }
  gComp.end();
  report(name, tick);
  char line[200];
  snprintf(line, sizeof(line),
           "GRAIN,%s_rows_max=%lu,emits_max=%lu,rows_at_worst=%lu,us_per_row=%.2f,spills=%lu,continuous_limited=%lu", name,
           static_cast<unsigned long>(maxRows), static_cast<unsigned long>(maxEmits),
           static_cast<unsigned long>(rowsAtWorst), totalRows ? toMicros(static_cast<uint32_t>(totalCycles / totalRows)) : 0.0,
           static_cast<unsigned long>(gTickPlayer.stats().spills),
           static_cast<unsigned long>(gComp.stats().continuousLimited));
  TEST_MESSAGE(line);
}

/// Edit writer rebuild grains (E4.5), worst case: a 512-note selection, every note moved, then the session rebuilt
/// (512 notes gathered with their hides, a 1,024-key heapsort, 1,024 replacement rows) at 128 units per grain; then an
/// E: undo of that step.
static void editGrains() {
  gPool.init();
  gRuns.init();
  gManifests.init();
  TEST_ASSERT_TRUE(gIndex.init(gPool, gRuns));
  gRes.init(gPool, gRuns, kLedgerFixedReservePages);
  gHist.init(gManifests, gRuns);
  gEdits.init(gPool, gRuns, gManifests, gRes, gHist, gEditLog);
  const RunId r = gRuns.allocate(0, RunKind::kRecord);
  for (uint32_t i = 0; i < Config::kEditSelection; ++i) {  // tick order: NoteOn at 11i, its NoteOff at 11i + 8
    gSel[i] = EditNote{1000 + i, i * 11, 8, static_cast<uint8_t>(i & 0x7F), 90, 0, 0};
    gRuns.append(r, row(gSel[i].tick, gSel[i].noteId, 0x90, gSel[i].pitch), gPool);
    gRuns.append(r, row(gSel[i].tick + 8, gSel[i].noteId, 0x80, gSel[i].pitch), gPool);
  }
  gRuns.seal(r);
  const ManifestId base = gManifests.create(0, 1, kLoop, &r, 1, gRuns);
  gRuns.release(r);
  gHist.commit(0, base);
  TEST_ASSERT_EQUAL(EditResult::Ok, gEdits.begin(0, gSel, Config::kEditSelection));
  for (uint32_t i = 0; i < Config::kEditSelection; ++i) {
    gChanges[i] = EditChange{static_cast<uint16_t>(i), gSel[i]};
    gChanges[i].value.tick = (kLoop - 1 - i * 11) % kLoop;  // reversed order: the sort has real work
  }
  Stat apply, rebuild, undo;
  uint32_t c0 = cycles();
  TEST_ASSERT_EQUAL(EditResult::Ok, gEdits.apply(gChanges, Config::kEditSelection));
  apply.add(cycles() - c0);
  bool more = true;
  while (more) {
    c0 = cycles();
    more = gEdits.step(128);
    rebuild.add(cycles() - c0);
  }
  c0 = cycles();
  TEST_ASSERT_EQUAL(EditResult::Ok, gEdits.undo());
  undo.add(cycles() - c0);
  gEdits.leave();
  report("edit_apply_512", apply);
  report("edit_rebuild_128_units", rebuild);
  report("edit_undo_512", undo);
}

/// Reclaim worker grains: 60 released runs of 40 pages each (24 directory-listed), returned 64 units per grain.
static void reclaimGrains() {
  gPool.init();
  gRuns.init();
  gManifests.init();
  uint32_t id = 1;
  for (uint8_t p = 0; p < 60; ++p) {
    const RunId r = gRuns.allocate(0, RunKind::kOverdub);
    for (uint32_t k = 0; k < 40u * Config::kRowsPerPage; ++k)
      TEST_ASSERT_EQUAL(AppendResult::Ok, gRuns.append(r, row(k, id++, 0x90, static_cast<uint8_t>(k & 0x7F)), gPool));
    gRuns.seal(r);
    gRuns.release(r);
  }
  Stat st;
  while (gRuns.reclaimPendingRuns() != 0) {
    const uint32_t c0 = cycles();
    gRuns.reclaim(gPool, ReclaimJob<Runs, Pool>::kUnitsPerGrain);
    st.add(cycles() - c0);
  }
  TEST_ASSERT_EQUAL_UINT16(Pool::kPageCount, gPool.freeCount());
  report("reclaim_64_units", st);
}

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
  Stat cap, res, seek;
  TestComposer::Checkpoint cp;
  {
    const auto& m = gManifests.manifest(gManifest);
    SourceRun src[Manifests::kMaxRuns];
    for (uint8_t i = 0; i < m.runCount; ++i) src[i] = {m.runs[i], gRuns.run(m.runs[i]).rowCount};
    Stat beg;
    for (int k = 0; k < 8; ++k) {
      uint32_t c0 = cycles();
      const bool ok = gComp.begin(src, m.runCount, kLoop, m.identity);
      beg.add(cycles() - c0);
      TEST_ASSERT_TRUE(ok);
    }
    report("composer_begin", beg);
    snprintf(line, sizeof(line), "GRAIN,index_sync_builds=%lu", static_cast<unsigned long>(gIndex.syncBuilds()));
    TEST_MESSAGE(line);
    // A real mid-loop checkpoint: the end of window 0, within the open-instance table.
    TEST_ASSERT_TRUE(gComp.composeNext(gWin, kWinTicks));
    TEST_ASSERT_EQUAL_UINT32(0, gComp.stats().openOverflow);
    for (int k = 0; k < 8; ++k) {
      uint32_t c0 = cycles();
      const bool ok = gComp.capture(cp);
      cap.add(cycles() - c0);
      TEST_ASSERT_TRUE_MESSAGE(ok, "capture refused: resume would not be measured");
    }
    for (int k = 0; k < 8; ++k) {
      uint32_t c0 = cycles();
      const bool ok = gComp.resumeBind(src, m.runCount, kLoop, m.identity, cp);
      res.add(cycles() - c0);
      TEST_ASSERT_TRUE(ok);
      c0 = cycles();
      gComp.resumeSeek();
      seek.add(cycles() - c0);
    }
    gComp.end();
  }
  report("checkpoint_capture", cap);
  report("checkpoint_resume_bind", res);
  report("checkpoint_resume_seek", seek);

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
  reclaimGrains();
  editGrains();
  tickPath(TickLoad::Spread, "tick_spread");
  tickPath(TickLoad::Clustered, "tick_clustered");
  tickPath(TickLoad::CcBurst, "tick_cc_burst");
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
