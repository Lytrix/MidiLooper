//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// fixed-memory-engine E6.2c: source-transition liveness (owner ruling 2026-11-05, E5 scheduling REDs). Simulated
// clock: a Clock tick every 1,040 µs (300 BPM, 192 PPQN); every scheduler grain costs the measured worst g = 810 µs.
// Load: seven dense playing tracks, each a hard window job of 100 grains (the measured worst) per boundary with that
// boundary as deadline, plus a display job always pending. Track 0 plays real, admission-dense music (~2,700 notes per
// window) through PlaybackTrack and PlaybackJobs; its source changes are Transition-class jobs and every cold start
// warms up a whole cycle (no checkpoints). Checked: steady deadlines, acceptance-to-audible latency (reported),
// late source switches named, previous-source pins kept until adoption and released after, track 0's stream against
// a reference from uninterrupted compositions, and — under an infeasible load — named misses with no phantom or
// duplicated note and no leaked pin.

#include <unity.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <vector>

#include "ContentEngine/Composer.h"
#include "ContentEngine/EngineCore.h"
#include "ContentEngine/PlaybackJobs.h"
#include "ContentEngine/PlaybackTrack.h"

using namespace ContentEngine;

using Pool = PagePool<4096, 64>;  // 576 pages are the edit-index partition
using Runs = RunTable<64, Pool>;
using TestComposer = Composer<Pool, Runs>;
using Window = SpanWindow<3072>;
using Table = CheckpointTable<TestComposer::Checkpoint, 8, 8>;
using Manifests = ManifestTable<32, 8>;
constexpr uint16_t kT = Config::kMaxSoundingPerTrack;
using Track = PlaybackTrack<TestComposer, Table, Window, Manifests, Runs, kT>;
using Player = WindowPlayer<kT>;
using Emit = Player::Emit;
using Sched = WorkerScheduler<32>;
using Jobs = PlaybackJobs<Track, 1, Sched>;

constexpr uint32_t kLoop = 768 * 8;
constexpr uint32_t kWin = 1536;
constexpr uint32_t kUsPerTick = 1040;
constexpr uint32_t kGrainUs = 810;
constexpr uint8_t kDense = 7;
constexpr uint32_t kStart = kLoop - 1300;  // the first source is selected here: its cold start precedes the measured part

static Pool gPool;
static Runs gRuns;
static Manifests gManifests;
static Table gTable;
static EditIndexStore<Pool, Runs> gIndex;
static TestComposer gComp[2];
static Window gBuf[3];
static TerminalScheduler<kT> gSched;
static Player gPlayer;
static Track gTrack;
static Sched gS;
static Jobs gJobs;

// ---- Sources ----------------------------------------------------------------------------------------------------------
static RunId noteRun(uint32_t seed, uint32_t notesPerWindow) {
  const RunId r = gRuns.allocate(0, RunKind::kRecord);
  struct Ev {
    uint32_t tick;
    bool on;
    uint32_t id;
    uint8_t pitch;
  };
  std::vector<Ev> ev;
  const uint32_t n = notesPerWindow * (kLoop / kWin);
  for (uint32_t i = 0; i < n; ++i) {
    const uint32_t on = static_cast<uint32_t>((static_cast<uint64_t>(i) * (kLoop - 40)) / n);
    const uint8_t pitch = static_cast<uint8_t>(30 + (i * 7 + seed) % 80);
    ev.push_back({on, true, seed * 100000 + i, pitch});
    ev.push_back({on + 4 + (i + seed) % 7, false, seed * 100000 + i, pitch});  // <= 20 open (owner ceiling)
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
  return r;
}
static ManifestId manifestOf(const RunId* runs, uint8_t count, uint32_t revision) {
  const ManifestId m = gManifests.create(0, revision, kLoop, runs, count, gRuns);
  for (uint8_t i = 0; i < count; ++i) gRuns.release(runs[i]);  // drop the writer references; manifests hold the runs
  return m;
}
static ManifestId denseSource(uint32_t seed, uint32_t revision) {
  const RunId r = noteRun(seed, 2700);
  return manifestOf(&r, 1, revision);
}
/// An edit commit on top of `base`: the base's run plus an edit run hiding `hides` of its notes.
static ManifestId editedSource(ManifestId base, uint32_t seed, uint32_t hides, uint32_t revision) {
  const RunId baseRun = gManifests.manifest(base).runs[0];
  gRuns.addRef(baseRun);
  const RunId e = gRuns.allocate(0, RunKind::kEdit);
  for (uint32_t i = 0; i < hides; ++i) {
    PassEvent p{};
    p.noteId = seed * 100000 + i * 2;
    p.status = kHideNoteStatus;
    TEST_ASSERT_EQUAL(AppendResult::Ok, gRuns.append(e, p, gPool));
  }
  gRuns.seal(e);
  const RunId runs[2] = {baseRun, e};
  return manifestOf(runs, 2, revision);
}

void setUp() {
  gPool.init();
  gRuns.init();
  gManifests.init();
  gTable.init();
  TEST_ASSERT_TRUE(gIndex.init(gPool, gRuns));
  gComp[0].init(gPool, gRuns, &gIndex);
  gComp[1].init(gPool, gRuns, &gIndex);
  gSched.init();
  gPlayer.init(gSched);
  Window* bufs[3] = {&gBuf[0], &gBuf[1], &gBuf[2]};
  gTrack.init(gComp[0], gComp[1], bufs, gTable, gManifests, gRuns, gPlayer, kWin);
  gS.init(kGrainUs, 0);
  Track* tracks[1] = {&gTrack};
  gJobs.init(tracks, gS);
}
void tearDown() {}

// ---- Load and simulation ------------------------------------------------------------------------------------------
struct DenseLoad {  // one dense playing track's steady windows: a hard job of `grains` per boundary
  uint32_t grains, left;
  bool live;
  uint32_t misses;
};
static JobStep denseStep(void* c) {
  auto* d = static_cast<DenseLoad*>(c);
  if (--d->left > 0) return JobStep::Continue;
  d->live = false;
  return JobStep::Done;
}
struct Display {
  uint32_t runs;
};
static JobStep displayStep(void* c) {
  ++static_cast<Display*>(c)->runs;
  return JobStep::Continue;
}

struct Sim {
  uint32_t denseGrains = 100;
  uint32_t longHardGrains = 0;  // one more hard obligation, always pending: a long sealing / publication job
  uint32_t longHardDeadlineTicks = 0;
  DenseLoad longHard{0, 0, false, 0};
  std::vector<Emit> emits;  // track 0, from `recordFrom`
  uint32_t recordFrom = kLoop;
  DenseLoad dense[kDense];
  Display display{0};
  std::vector<uint32_t> adoptions;  // ticks at which track 0 adopted a selection
};

/// Runs ticks [from, to): `onTick(t)` may select or stop before track 0's tick.
template <typename OnTick>
static void simulate(Sim& sim, uint32_t from, uint32_t to, OnTick onTick) {
  uint8_t dispId;
  TEST_ASSERT_TRUE(gS.submit(JobSpec{JobClass::Display, 0, 0xFFFFFFFFu, displayStep, &sim.display}, dispId));
  for (auto& d : sim.dense) d = DenseLoad{sim.denseGrains, 0, false, 0};
  if (sim.longHardGrains != 0) {
    sim.longHard = DenseLoad{sim.longHardGrains, sim.longHardGrains, true, 0};
    uint8_t id;
    TEST_ASSERT_TRUE(gS.submit(
        JobSpec{JobClass::Hard, (from + sim.longHardDeadlineTicks) * kUsPerTick, sim.longHardGrains, denseStep, &sim.longHard},
        id));
  }
  Emit buf[64];
  uint32_t nowUs = from * kUsPerTick, tick = from, nextTickUs = nowUs;
  while (tick < to) {
    if (nowUs >= nextTickUs) {
      if ((tick % kLoop) % kWin == 0)  // every boundary: the dense tracks' next windows are due at the next one
        for (auto& d : sim.dense) {
          if (d.live) {
            ++d.misses;
            continue;
          }
          d.left = d.grains;
          d.live = true;
          uint8_t id;
          TEST_ASSERT_TRUE(gS.submit(JobSpec{JobClass::Hard, (tick + kWin) * kUsPerTick, d.grains, denseStep, &d}, id));
        }
      onTick(tick);
      const uint32_t before = gTrack.stats().switches;
      const uint8_t n = gTrack.tick(tick, buf, 64);
      if (gTrack.stats().switches != before) sim.adoptions.push_back(tick);
      if (tick >= sim.recordFrom) sim.emits.insert(sim.emits.end(), buf, buf + n);
      ++tick;
      nextTickUs += kUsPerTick;
      continue;
    }
    gJobs.service(tick, nowUs, kUsPerTick);
    nowUs = gS.runGrain(nowUs) == Sched::kNoJob ? nextTickUs : nowUs + kGrainUs;
  }
  gS.cancel(dispId);
}

/// Track 0's reference: each source composed uninterrupted from cycle 0 in lockstep; `pick(t)` names the index of the
/// source whose window starts at boundary t (-1: none).
template <typename Pick>
static std::vector<Emit> reference(const std::vector<ManifestId>& sources, uint32_t from, uint32_t to, Pick pick) {
  static TestComposer comps[4];
  static Window wins[4];
  static TerminalScheduler<kT> sched;
  static Player player;
  sched.init();
  player.init(sched);
  for (size_t k = 0; k < sources.size(); ++k) {
    comps[k].init(gPool, gRuns, &gIndex);
    const auto& m = gManifests.manifest(sources[k]);
    SourceRun src[8];
    for (uint8_t i = 0; i < m.runCount; ++i) src[i] = {m.runs[i], gRuns.run(m.runs[i]).rowCount};
    TEST_ASSERT_TRUE(comps[k].begin(src, m.runCount, kLoop, m.identity));
  }
  std::vector<Emit> out;
  Emit buf[64];
  for (uint32_t t = 0; t < to; ++t) {
    if ((t % kLoop) % kWin == 0) {
      for (size_t k = 0; k < sources.size(); ++k)
        if (!comps[k].composeNext(wins[k], kWin)) {
          comps[k].nextCycle();
          TEST_ASSERT_TRUE(comps[k].composeNext(wins[k], kWin));
        }
      const int p = t >= from ? pick(t) : -1;
      if (p >= 0) {
        const uint8_t n = player.beginWindow(wins[p], buf, 64);
        out.insert(out.end(), buf, buf + n);
      }
    }
    if (t >= from) {
      const uint8_t n = player.tick(t, buf, 64);
      out.insert(out.end(), buf, buf + n);
    }
  }
  for (size_t k = 0; k < sources.size(); ++k) comps[k].end();
  return out;
}
static void expectStream(const std::vector<Emit>& want, const std::vector<Emit>& got) {
  TEST_ASSERT_TRUE(want.size() > 1000);
  TEST_ASSERT_EQUAL_UINT32(want.size(), got.size());
  for (size_t k = 0; k < want.size(); ++k) {
    TEST_ASSERT_EQUAL_UINT32(want[k].tick, got[k].tick);
    TEST_ASSERT_EQUAL_UINT8(want[k].cls, got[k].cls);
    TEST_ASSERT_EQUAL_UINT32(want[k].noteId, got[k].noteId);
  }
}
static void report(const char* name, const Sim& sim) {
  char line[220];
  snprintf(line, sizeof line,
           "%s: latency last %lu ticks (%.0f ms), worst %lu ticks; last transition %lu grains; late source switches %lu; "
           "transition grains %lu of %lu; hard misses %lu",
           name, static_cast<unsigned long>(gTrack.stats().lastLatencyTicks),
           gTrack.stats().lastLatencyTicks * kUsPerTick / 1000.0, static_cast<unsigned long>(gTrack.stats().maxLatencyTicks),
           static_cast<unsigned long>(gTrack.stats().lastTransitionGrains),
           static_cast<unsigned long>(gTrack.stats().lateSourceSwitches),
           static_cast<unsigned long>(gS.report().transitionGrains), static_cast<unsigned long>(gS.report().grains),
           static_cast<unsigned long>(gS.report().missedDeadlines));
  TEST_MESSAGE(line);
  snprintf(line, sizeof line, "%s: grains hard %lu, transition %lu, display %lu, background %lu; slack-forced %lu", name,
           static_cast<unsigned long>(gS.report().hardGrains), static_cast<unsigned long>(gS.report().transitionGrains),
           static_cast<unsigned long>(gS.report().displayGrains), static_cast<unsigned long>(gS.report().backgroundGrains),
           static_cast<unsigned long>(gS.report().slackForcedHard));
  TEST_MESSAGE(line);
}
static uint16_t pins(ManifestId m) { return gManifests.manifest(m).readerPins; }

// ---- REDs ---------------------------------------------------------------------------------------------------------

// (a) Eight dense playing tracks plus one Undo needing a cold-start composition.
void test_eight_dense_tracks_and_an_undo() {
  const ManifestId before = denseSource(1, 1), current = denseSource(2, 2);
  Sim sim;
  const uint32_t undoAt = kLoop + 2 * kWin + 100;
  gTrack.select(0, current, kStart);
  simulate(sim, kStart, 4 * kLoop, [&](uint32_t t) {
    if (t == undoAt) gTrack.select(0, before, t);
  });
  report("(a) 8 dense + Undo", sim);
  TEST_ASSERT_EQUAL_UINT32(0, gTrack.stats().underruns);
  TEST_ASSERT_EQUAL_UINT32(0, gS.report().missedDeadlines);
  for (const auto& d : sim.dense) TEST_ASSERT_EQUAL_UINT32(0, d.misses);
  TEST_ASSERT_EQUAL_UINT32(2, gTrack.stats().switches);
  const uint32_t adopted = sim.adoptions.back();
  TEST_ASSERT_TRUE(adopted > undoAt);
  TEST_ASSERT_EQUAL_UINT32(adopted - undoAt, gTrack.stats().lastLatencyTicks);
  TEST_ASSERT_TRUE(sim.display.runs * 4 + 4 >= gS.report().grains);
  expectStream(reference({current, before}, kLoop, 4 * kLoop, [adopted](uint32_t t) { return t < adopted ? 0 : 1; }),
               sim.emits);
}

// (a2) The same Undo with the Undo target's restart checkpoints in the slot's table (every 2 windows): the cold start
// resumes near its target instead of warming up a cycle — the plan's next-window case. (The table keeps one manifest
// per slot, so in the engine as built an Undo target's checkpoints are normally gone: see design E6.2c.)
void test_eight_dense_tracks_and_an_undo_with_checkpoints() {
  using Sweep = CheckpointSweep<TestComposer, Table, Window, Manifests, Runs>;
  static Sweep sweep;
  static TestComposer sweepComposer;
  static Window scratch;
  sweepComposer.init(gPool, gRuns, &gIndex);
  const ManifestId before = denseSource(15, 1), current = denseSource(16, 2);
  for (const ManifestId m : {current, before}) {  // the slot's table keeps one manifest: the Undo target's, swept last
    TEST_ASSERT_EQUAL(Sweep::Status::Running, sweep.start(sweepComposer, gTable, gManifests, gRuns, 0, m, kWin, 2 * kWin));
    while (sweep.step(scratch, 128) == Sweep::Status::Running) {
    }
    TEST_ASSERT_EQUAL(Sweep::Status::Done, sweep.status());
  }
  Sim sim;
  const uint32_t undoAt = kLoop + 2 * kWin + 100;
  gTrack.select(0, current, kStart);
  simulate(sim, kStart, 4 * kLoop, [&](uint32_t t) {
    if (t == undoAt) gTrack.select(0, before, t);
  });
  report("(a2) 8 dense + Undo, checkpoints swept", sim);
  TEST_ASSERT_EQUAL_UINT32(0, gTrack.stats().underruns);
  TEST_ASSERT_EQUAL_UINT32(0, gS.report().missedDeadlines);
  TEST_ASSERT_EQUAL_UINT32(2, gTrack.stats().switches);
  const uint32_t adopted = sim.adoptions.back();
  TEST_ASSERT_EQUAL_UINT32(kLoop + 3 * kWin, adopted);  // the first boundary after acceptance
  expectStream(reference({current, before}, kLoop, 4 * kLoop, [adopted](uint32_t t) { return t < adopted ? 0 : 1; }),
               sim.emits);
}

// (b) Continuous playback demand plus an edit-index build (an edit commit with 4,096 hides).
void test_playback_demand_and_an_edit_index_build() {
  const ManifestId base = denseSource(3, 1);
  const ManifestId edited = editedSource(base, 3, 4096, 2);
  Sim sim;
  const uint32_t commitAt = kLoop + kWin + 700;
  gTrack.select(0, base, kStart);
  simulate(sim, kStart, 4 * kLoop, [&](uint32_t t) {
    if (t == commitAt) gTrack.select(0, edited, t);
  });
  report("(b) demand + edit index", sim);
  TEST_ASSERT_EQUAL_UINT32(0, gTrack.stats().underruns);
  TEST_ASSERT_EQUAL_UINT32(0, gS.report().missedDeadlines);
  TEST_ASSERT_EQUAL_UINT32(2, gTrack.stats().switches);
  const uint32_t adopted = sim.adoptions.back();
  expectStream(reference({base, edited}, kLoop, 4 * kLoop, [adopted](uint32_t t) { return t < adopted ? 0 : 1; }),
               sim.emits);
}

// (c) Repeated commits: B is superseded by C before it is ready, C by D; D is adopted, B and C leave no pin.
void test_repeated_commits_supersede_unfinished_transitions() {
  const ManifestId a = denseSource(4, 1), b = denseSource(5, 2), c = denseSource(6, 3), d = denseSource(7, 4);
  Sim sim;
  const uint32_t at = kLoop + kWin + 200;
  gTrack.select(0, a, kStart);
  simulate(sim, kStart, 5 * kLoop, [&](uint32_t t) {
    if (t == at) gTrack.select(0, b, t);
    if (t == at + 20) {
      TEST_ASSERT_TRUE(gTrack.transitionPending());  // B unfinished
      gTrack.select(0, c, t);
    }
    if (t == at + 40) gTrack.select(0, d, t);
  });
  report("(c) repeated commits", sim);
  TEST_ASSERT_EQUAL_UINT32(4, gTrack.stats().accepted);
  TEST_ASSERT_EQUAL_UINT32(2, gTrack.stats().switches);  // A, then D
  TEST_ASSERT_EQUAL_UINT16(0, pins(b));
  TEST_ASSERT_EQUAL_UINT16(0, pins(c));
  TEST_ASSERT_EQUAL_UINT16(1, pins(d));  // the steady cursor plays D
  TEST_ASSERT_EQUAL_UINT16(0, pins(a));  // released at adoption
  const uint32_t adopted = sim.adoptions.back();
  expectStream(reference({a, d}, kLoop, 5 * kLoop, [adopted](uint32_t t) { return t < adopted ? 0 : 1; }), sim.emits);
}

// (d) Stop while a source transition is pending: every sounding note ends, nothing plays after, no pin is left.
void test_stop_while_a_transition_is_pending() {
  const ManifestId a = denseSource(8, 1), b = denseSource(9, 2);
  Sim sim;
  const uint32_t selectAt = kLoop + kWin + 300, stopAt = selectAt + 30;
  std::vector<Emit> stopOffs;
  gTrack.select(0, a, kStart);
  simulate(sim, kStart, 3 * kLoop, [&](uint32_t t) {
    if (t == selectAt) gTrack.select(0, b, t);
    if (t == stopAt) {
      TEST_ASSERT_TRUE(gTrack.transitionPending());
      Emit buf[kT];
      const uint8_t n = gTrack.stop(t, buf, kT);
      stopOffs.assign(buf, buf + n);
    }
  });
  TEST_ASSERT_EQUAL_UINT16(0, pins(a));
  TEST_ASSERT_EQUAL_UINT16(0, pins(b));
  for (const Emit& e : stopOffs) TEST_ASSERT_EQUAL_UINT8(0, e.cls);
  for (const Emit& e : sim.emits) TEST_ASSERT_TRUE(e.tick <= stopAt);  // silence after the stop
  // Every NoteOn emitted got exactly one NoteOff, the stop's included.
  std::vector<uint32_t> open;
  for (const Emit& e : sim.emits) {
    if (e.cls == 1) open.push_back(e.noteId);
    if (e.cls == 0) {
      auto it = std::find(open.begin(), open.end(), e.noteId);
      TEST_ASSERT_TRUE(it != open.end());
      open.erase(it);
    }
  }
  for (const Emit& e : stopOffs) {
    auto it = std::find(open.begin(), open.end(), e.noteId);
    TEST_ASSERT_TRUE(it != open.end());
    open.erase(it);
  }
  TEST_ASSERT_TRUE(open.empty());
}

// (e) Sustained feasible load: the dense tracks plus a long hard obligation (10,000 grains due in 40 s) keep hard work
// pending all the time; a new source every 3 windows for 6 loops. Transitions still progress: every selection becomes
// audible within two boundaries, and the latest accepted one plays at the end.
void test_sustained_load_bounded_progress_for_the_latest_source() {
  const ManifestId s[3] = {denseSource(10, 1), denseSource(11, 2), denseSource(12, 3)};
  Sim sim;
  sim.longHardGrains = 10000;
  sim.longHardDeadlineTicks = 40000000 / kUsPerTick;
  uint32_t selections = 0, latest = 0;
  gTrack.select(0, s[0], kLoop - 1);
  simulate(sim, kStart, 8 * kLoop, [&](uint32_t t) {
    if (t > kLoop && t < 7 * kLoop && (t - kLoop) % (3 * kWin) == 500) {
      latest = (latest + 1) % 3;
      gTrack.select(0, s[latest], t);
      ++selections;
    }
  });
  report("(e) sustained", sim);
  TEST_ASSERT_EQUAL_UINT32(selections + 1, gTrack.stats().switches);
  TEST_ASSERT_TRUE(gTrack.stats().maxLatencyTicks <= 2 * kWin);  // adopted by the second boundary after acceptance
  TEST_ASSERT_EQUAL_UINT32(gManifests.manifest(s[latest]).identity, gTrack.playingIdentity());
  TEST_ASSERT_EQUAL_UINT32(0, gTrack.stats().underruns);
  TEST_ASSERT_EQUAL_UINT32(0, gS.report().missedDeadlines);
}

// (f) Infeasible load (seven tracks of 400 grains per window): misses are named; track 0's audio stays well formed
// (no NoteOff without its NoteOn, no NoteOn twice) and every pin is released at stop.
void test_infeasible_load_names_misses_without_corruption() {
  const ManifestId a = denseSource(13, 1), b = denseSource(14, 2);
  Sim sim;
  sim.denseGrains = 400;
  gTrack.select(0, a, kStart);
  std::vector<Emit> stopOffs;
  simulate(sim, kStart, 4 * kLoop, [&](uint32_t t) {
    if (t == kLoop + kWin + 50) gTrack.select(0, b, t);
    if (t == 4 * kLoop - 1) {
      Emit buf[kT];
      const uint8_t n = gTrack.stop(t, buf, kT);
      stopOffs.assign(buf, buf + n);
    }
  });
  report("(f) infeasible", sim);
  uint32_t denseMisses = 0;
  for (const auto& d : sim.dense) denseMisses += d.misses;
  TEST_ASSERT_TRUE(gS.report().missedDeadlines > 0);
  TEST_ASSERT_TRUE(denseMisses > 0);
  std::vector<uint32_t> open;
  for (const Emit& e : sim.emits) {
    if (e.cls == 1) {
      TEST_ASSERT_TRUE(std::find(open.begin(), open.end(), e.noteId) == open.end());
      open.push_back(e.noteId);
    } else if (e.cls == 0) {
      auto it = std::find(open.begin(), open.end(), e.noteId);
      TEST_ASSERT_TRUE(it != open.end());
      open.erase(it);
    }
  }
  for (const Emit& e : stopOffs) {
    auto it = std::find(open.begin(), open.end(), e.noteId);
    TEST_ASSERT_TRUE(it != open.end());
    open.erase(it);
  }
  TEST_ASSERT_TRUE(open.empty());
  TEST_ASSERT_EQUAL_UINT16(0, pins(a));
  TEST_ASSERT_EQUAL_UINT16(0, pins(b));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_eight_dense_tracks_and_an_undo);
  RUN_TEST(test_eight_dense_tracks_and_an_undo_with_checkpoints);
  RUN_TEST(test_playback_demand_and_an_edit_index_build);
  RUN_TEST(test_repeated_commits_supersede_unfinished_transitions);
  RUN_TEST(test_stop_while_a_transition_is_pending);
  RUN_TEST(test_sustained_load_bounded_progress_for_the_latest_source);
  RUN_TEST(test_infeasible_load_names_misses_without_corruption);
  return UNITY_END();
}
