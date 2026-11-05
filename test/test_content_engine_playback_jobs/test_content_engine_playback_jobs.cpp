//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// fixed-memory-engine E6.2: playback compose work run by the worker scheduler. Eight tracks, each with its own
// playback windows and compose cursors, on a simulated clock: a Clock tick every 1,040 µs (300 BPM, 192 PPQN), every
// scheduler grain costing the measured worst g = 810 µs, a display job always pending. The steady window of each track
// is a hard job with its next boundary as deadline; transitions (cold starts) are transition-class jobs, ahead of a long
// compaction-sized background job that is already running. This proves steady-window deadlines under load, not
// source-transition liveness (test_content_engine_source_transitions). Every track starts
// on time, half the tracks switch source mid-loop, nothing underruns, no hard deadline is missed, display keeps its
// share, and track 0's emitted stream equals a reference built from uninterrupted compositions.

#include <unity.h>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <new>
#include <vector>

#include "ContentEngine/Composer.h"
#include "ContentEngine/EngineCore.h"
#include "ContentEngine/PlaybackJobs.h"
#include "ContentEngine/PlaybackTrack.h"

using namespace ContentEngine;

using Pool = PagePool<512, 64>;
using Runs = RunTable<32, Pool>;
using TestComposer = Composer<Pool, Runs>;
using Window = SpanWindow<3072>;
using Table = CheckpointTable<TestComposer::Checkpoint, 8, 8>;
using Manifests = ManifestTable<32, 8>;
constexpr uint16_t kT = Config::kMaxSoundingPerTrack;
using Track = PlaybackTrack<TestComposer, Table, Window, Manifests, Runs, kT>;
using Player = WindowPlayer<kT>;
using Emit = Player::Emit;
using Sched = WorkerScheduler<32>;
constexpr uint8_t kTracks = 8;
using Jobs = PlaybackJobs<Track, kTracks, Sched>;

constexpr uint32_t kLoop = 768 * 8;
constexpr uint32_t kWin = 1536;
constexpr uint32_t kUsPerTick = 1040;
constexpr uint32_t kGrainUs = 810;

static Pool gPool;
static Runs gRuns;
static Manifests gManifests;
static Table gTable;
static TestComposer gComp[kTracks][2], gRefA, gRefB;
static Window gBuf[kTracks][3], gRefWinA, gRefWinB;
static TerminalScheduler<kT> gSched[kTracks], gRefSched;
static Player gPlayer[kTracks], gRefPlayer;
static Track gTrack[kTracks];
static Sched gS;
static Jobs gJobs;
static ManifestId gSrcA[kTracks], gSrcB[kTracks];

static ManifestId source(uint8_t track, uint32_t seed, uint32_t revision) {
  const RunId r = gRuns.allocate(track, RunKind::kRecord);
  struct Ev {
    uint32_t tick;
    bool on;
    uint32_t id;
    uint8_t pitch;
  };
  std::vector<Ev> ev;
  uint32_t x = seed;
  for (uint32_t i = 0; i < 120; ++i) {  // 120 notes per loop, lengths up to 400 ticks
    x = x * 1103515245u + 12345u;
    const uint32_t on = (x >> 8) % (kLoop - 500), len = 20 + (x >> 4) % 380;
    const uint8_t pitch = static_cast<uint8_t>(36 + (x >> 20) % 48);
    ev.push_back({on, true, seed * 1000 + i, pitch});
    ev.push_back({on + len, false, seed * 1000 + i, pitch});
  }
  std::stable_sort(ev.begin(), ev.end(), [](const Ev& a, const Ev& b) { return a.tick < b.tick || (a.tick == b.tick && !a.on && b.on); });
  for (const Ev& e : ev) {
    PassEvent p{};
    p.tick = e.tick;
    p.noteId = e.id;
    p.status = e.on ? 0x90 : 0x80;
    p.data1 = e.pitch;
    p.data2 = e.on ? 100 : 0;
    TEST_ASSERT_EQUAL(AppendResult::Ok, gRuns.append(r, p, gPool));
  }
  gRuns.seal(r);
  const ManifestId m = gManifests.create(track, revision, kLoop, &r, 1, gRuns);
  gRuns.release(r);
  return m;
}

void setUp() {
  gPool.init();
  gRuns.init();
  gManifests.init();
  gTable.init();
  gS.init(kGrainUs, 0);
  Track* tracks[kTracks];
  for (uint8_t i = 0; i < kTracks; ++i) {
    gComp[i][0].init(gPool, gRuns, nullptr);
    gComp[i][1].init(gPool, gRuns, nullptr);
    gSched[i].init();
    gPlayer[i].init(gSched[i]);
    Window* bufs[3] = {&gBuf[i][0], &gBuf[i][1], &gBuf[i][2]};
    gTrack[i].init(gComp[i][0], gComp[i][1], bufs, gTable, gManifests, gRuns, gPlayer[i], kWin);
    tracks[i] = &gTrack[i];
    gSrcA[i] = source(i, 11 + i, 1);
    gSrcB[i] = source(i, 51 + i, 2);
  }
  gJobs.init(tracks, gS);
  gRefA.init(gPool, gRuns, nullptr);
  gRefB.init(gPool, gRuns, nullptr);
  gRefSched.init();
  gRefPlayer.init(gRefSched);
}
void tearDown() {}

/// Track 0's reference: A and B composed uninterrupted from cycle 0, one window per boundary, B from `switchAt`.
static std::vector<Emit> reference(uint32_t from, uint32_t to, uint32_t switchAt) {
  auto beginSrc = [](TestComposer& c, ManifestId m) {
    const auto& man = gManifests.manifest(m);
    const SourceRun src[1] = {{man.runs[0], gRuns.run(man.runs[0]).rowCount}};
    TEST_ASSERT_TRUE(c.begin(src, 1, kLoop, man.identity));
  };
  beginSrc(gRefA, gSrcA[0]);
  beginSrc(gRefB, gSrcB[0]);
  auto next = [](TestComposer& c, Window& w) {
    if (!c.composeNext(w, kWin)) {
      c.nextCycle();
      TEST_ASSERT_TRUE(c.composeNext(w, kWin));
    }
  };
  std::vector<Emit> out;
  Emit buf[64];
  for (uint32_t t = 0; t < to; ++t) {
    if ((t % kLoop) % kWin == 0) {
      next(gRefA, gRefWinA);
      next(gRefB, gRefWinB);
      if (t >= from) {
        const uint8_t n = gRefPlayer.beginWindow(t < switchAt ? gRefWinA : gRefWinB, buf, 64);
        out.insert(out.end(), buf, buf + n);
      }
    }
    if (t >= from) {
      const uint8_t n = gRefPlayer.tick(t, buf, 64);
      out.insert(out.end(), buf, buf + n);
    }
  }
  return out;
}

void test_eight_tracks_play_and_switch_under_the_scheduler() {
  // A display job is always pending: it must keep at least one grain in four while playback runs.
  struct Display {
    uint32_t runs;
  } display{0};
  uint8_t dispId;
  TEST_ASSERT_TRUE(gS.submit(JobSpec{JobClass::Display, 0, 0xFFFFFFFFu,
                                     [](void* c) {
                                       ++static_cast<Display*>(c)->runs;
                                       return JobStep::Continue;
                                     },
                                     &display},
                             dispId));
  // A long background job (compaction-sized: 3,000 grains, ~2.4 s) is already running when playback starts.
  struct Long {
    uint32_t left;
  } compaction{3000};
  uint8_t longId;
  TEST_ASSERT_TRUE(gS.submit(JobSpec{JobClass::Background, 0, 3000,
                                     [](void* c) {
                                       return --static_cast<Long*>(c)->left == 0 ? JobStep::Done : JobStep::Continue;
                                     },
                                     &compaction},
                             longId));
  const uint32_t startTick = kLoop - 1200, endTick = 4 * kLoop;
  for (uint8_t i = 0; i < kTracks; ++i) gTrack[i].select(i, gSrcA[i], startTick);
  uint32_t switchAt[kTracks] = {};
  std::vector<Emit> got;
  Emit buf[64];
  uint32_t nowUs = startTick * kUsPerTick, tick = startTick, nextTickUs = nowUs;
  while (tick < endTick) {
    if (nowUs >= nextTickUs) {  // the Clock tick (ISR in production): no composition, no page read
      if (tick == kLoop + 2 * kWin + 300)
        for (uint8_t i = 0; i < kTracks; i += 2) gTrack[i].select(i, gSrcB[i], tick);
      for (uint8_t i = 0; i < kTracks; ++i) {
        const uint32_t before = gTrack[i].stats().switches;
        const uint8_t n = gTrack[i].tick(tick, buf, 64);
        if (gTrack[i].stats().switches != before) switchAt[i] = tick;
        if (i == 0 && tick >= kLoop) got.insert(got.end(), buf, buf + n);
      }
      ++tick;
      nextTickUs += kUsPerTick;
      continue;
    }
    gJobs.service(tick, nowUs, kUsPerTick);
    if (gS.runGrain(nowUs) == Sched::kNoJob)
      nowUs = nextTickUs;  // idle until the next tick
    else
      nowUs += kGrainUs;
  }
  const uint32_t b = kLoop + 3 * kWin;  // the boundary after the mid-loop selection
  for (uint8_t i = 0; i < kTracks; ++i) {
    TEST_ASSERT_EQUAL_UINT32(0, gTrack[i].stats().underruns);
    TEST_ASSERT_EQUAL_UINT32(0, gTrack[i].stats().lateSourceSwitches);
    TEST_ASSERT_EQUAL_UINT32(i % 2 == 0 ? 2 : 1, gTrack[i].stats().switches);
    if (i % 2 == 0) TEST_ASSERT_EQUAL_UINT32(b, switchAt[i]);
  }
  TEST_ASSERT_EQUAL_UINT32(0, gS.report().missedDeadlines);
  TEST_ASSERT_EQUAL_UINT32(0, gS.report().overruns);
  TEST_ASSERT_TRUE(display.runs * 4 + 4 >= gS.report().grains);
  TEST_ASSERT_EQUAL_UINT32(0, gJobs.stats().tableFull);
  const auto want = reference(kLoop, endTick, b);
  TEST_ASSERT_TRUE(want.size() > 200);
  TEST_ASSERT_EQUAL_UINT32(want.size(), got.size());
  for (size_t k = 0; k < want.size(); ++k) {
    TEST_ASSERT_EQUAL_UINT32(want[k].tick, got[k].tick);
    TEST_ASSERT_EQUAL_UINT8(want[k].cls, got[k].cls);
    TEST_ASSERT_EQUAL_UINT32(want[k].noteId, got[k].noteId);
  }
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_eight_tracks_play_and_switch_under_the_scheduler);
  return UNITY_END();
}
