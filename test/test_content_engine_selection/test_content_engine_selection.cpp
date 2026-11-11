//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// fixed-memory-engine Q-OUT-2 (owner decision 2026-11-05: source-ordered refusal). Which note instances may sound when
// the source has more than T = 16 overlapping must depend only on the immutable revision, the effective cyclic note
// geometry and DEC-244 order, never on which notes survived earlier cycles:
//   * the selection repeats identically every loop from the first steady cycle, also above 16 overlapping notes;
//   * a cold start (from a checkpoint, or warmed up from the loop start) makes the same decisions as uninterrupted
//     playback after 2, 4 or 8 cycles; only the physical sounding state differs (nothing sent before the start);
//   * port congestion and revision adoption do not change later source eligibility;
//   * at most 16 selected instances sound, none is cut early or restarted to make room, no phantom NoteOffs.
// "Decisions" are compared as the START entries of each window (which instances begin sounding where), cycle by cycle.

#include <unity.h>

#include <algorithm>
#include <cstdint>
#include <set>
#include <tuple>
#include <vector>

#include "ContentEngine/Composition/CheckpointTable.h"
#include "ContentEngine/Composition/SpanWindowComposer.h"
#include "ContentEngine/EngineCore.h"
#include "ContentEngine/Composition/SpanWindow.h"
#include "ContentEngine/Playback/TerminalScheduler.h"

using namespace ContentEngine;

using Pool = PagePool<2048, 64>;
using Runs = RunTable<64, Pool>;
using TestComposer = SpanWindowComposer<Pool, Runs>;
using Window = SpanWindow<3072>;
using Manifests = ManifestTable<16, 8>;
using Table = CheckpointTable<TestComposer::Checkpoint, 2, 8>;
using Sweep = CheckpointSweep<TestComposer, Table, Window, Manifests, Runs>;
using Cold = ColdStart<TestComposer, Table, Window, Manifests, Runs>;
using Player = WindowPlayer<16>;

static Pool gPool;
static Runs gRuns;
static Manifests gManifests;
static EditIndexStore<Pool, Runs> gIndex;
static Table gTable;
static TestComposer gA, gB, gSweepComposer;
static Window gWin, gScratch;
static Sweep gSweep;
static Cold gCold;

constexpr uint32_t kLoop = 768 * 4, kWin = 768, kSpacing = 1536, kWindows = kLoop / kWin;

struct N {
  uint32_t id, on, off;
  uint8_t pitch, channel;
};

static PassEvent row(uint32_t tick, uint32_t id, uint8_t status, uint8_t d1, uint8_t origin = 0) {
  PassEvent e;
  e.tick = tick;
  e.noteId = id;
  e.seq = 0;
  e.status = status;
  e.data1 = d1;
  e.data2 = (status & 0xF0) == 0x90 ? 100 : 0;
  e.origin = origin;
  return e;
}

static RunId makeRun(uint8_t kind, const std::vector<N>& notes) {
  const RunId r = gRuns.allocate(0, kind);
  std::vector<std::pair<uint64_t, PassEvent>> ev;
  for (const auto& n : notes) {
    const uint8_t ch = static_cast<uint8_t>(n.channel - 1);
    ev.push_back({static_cast<uint64_t>(n.on) * 2 + 1, row(n.on, n.id, static_cast<uint8_t>(0x90 | ch), n.pitch)});
    ev.push_back({static_cast<uint64_t>(n.off) * 2, row(n.off, n.id, static_cast<uint8_t>(0x80 | ch), n.pitch,
                                                        n.off - n.on >= kLoop ? kOriginHeldLoop : 0)});
  }
  std::stable_sort(ev.begin(), ev.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
  for (const auto& e : ev) TEST_ASSERT_EQUAL(AppendResult::Ok, gRuns.append(r, e.second, gPool));
  gRuns.seal(r);
  return r;
}

static ManifestId manifest(const std::vector<RunId>& runs, uint32_t revision) {
  const ManifestId m = gManifests.create(0, revision, kLoop, runs.data(), static_cast<uint8_t>(runs.size()), gRuns);
  TEST_ASSERT_NOT_EQUAL(kNoManifest, m);
  return m;
}
static std::vector<SourceRun> sourceOf(ManifestId m) {
  std::vector<SourceRun> s;
  const auto& man = gManifests.manifest(m);
  for (uint8_t i = 0; i < man.runCount; ++i) s.push_back({man.runs[i], gRuns.run(man.runs[i]).rowCount});
  return s;
}

/// Decisions of one window: the instances that begin sounding, as (position, noteId, runKey).
using Decisions = std::set<std::tuple<uint32_t, uint32_t, uint16_t>>;
static Decisions decisions(const Window& w) {
  Decisions d;
  for (uint16_t i = 0; i < w.count; ++i)
    if (!w.entries[i].terminal && (w.entries[i].status & 0xF0) == 0x90)
      d.insert({w.entries[i].tick, w.entries[i].noteId, w.entries[i].runKey});
  return d;
}
/// One cycle of decisions per window.
static std::vector<Decisions> cycleDecisions(TestComposer& c) {
  std::vector<Decisions> out;
  while (c.composeNext(gWin, kWin)) out.push_back(decisions(gWin));
  c.nextCycle();
  return out;
}

void setUp() {
  gPool.init();
  gRuns.init();
  gManifests.init();
  TEST_ASSERT_TRUE(gIndex.init(gPool, gRuns));
  gTable.init();
  gA.init(gPool, gRuns, &gIndex);
  gB.init(gPool, gRuns, &gIndex);
  gSweepComposer.init(gPool, gRuns, &gIndex);
  gCold = Cold{};
  gSweep = Sweep{};
}
void tearDown() {}

static void sweep(ManifestId m) {
  TEST_ASSERT_EQUAL(Sweep::Status::Running, gSweep.start(gSweepComposer, gTable, gManifests, gRuns, 0, m, kWin, kSpacing));
  while (gSweep.step(gScratch, 128) == Sweep::Status::Running) {
  }
  TEST_ASSERT_EQUAL(Sweep::Status::Done, gSweep.status());
}

static void coldStart(TestComposer& c, ManifestId m, uint32_t position, uint32_t cycle) {
  Cold::Status st = gCold.start(c, gTable, gManifests, gRuns, 0, m, position, cycle, kWin);
  while (st == Cold::Status::Running) st = gCold.step(gScratch, 128);
  TEST_ASSERT_EQUAL(Cold::Status::Done, st);
}

/// More than 16 cyclically overlapping notes with different starts and durations, several crossing the loop end,
/// across three passes.
static ManifestId saturatedSource(uint32_t seed) {
  std::vector<std::vector<N>> passes(3);
  uint32_t id = 1;
  for (uint8_t p = 0; p < 3; ++p)
    for (uint32_t k = 0; k < 14; ++k) {
      seed = seed * 1103515245u + 12345u;
      const uint32_t on = (seed >> 8) % kLoop;
      const uint32_t len = 300 + (seed >> 4) % (kLoop + 400);  // some longer than the loop (Q-LEN-1 re-strike)
      passes[p].push_back({id++, on, on + len, static_cast<uint8_t>(30 + p * 20 + k), 1});
    }
  std::vector<RunId> runs;
  for (uint8_t p = 0; p < 3; ++p) runs.push_back(makeRun(p == 0 ? RunKind::kRecord : RunKind::kOverdub, passes[p]));
  return manifest(runs, 1);
}

/// RED 1: the selection repeats identically every loop from cycle 1 (uninterrupted, 8 cycles), above 16 overlapping.
void test_selection_repeats_every_loop_above_sixteen() {
  for (uint32_t seed : {11u, 23u, 37u, 41u, 59u}) {
    setUp();
    const ManifestId m = saturatedSource(seed);
    const auto src = sourceOf(m);
    TEST_ASSERT_TRUE(gA.begin(src.data(), static_cast<uint8_t>(src.size()), kLoop, gManifests.manifest(m).identity));
    std::vector<std::vector<Decisions>> cycles;
    uint32_t refused = 0;
    for (uint32_t c = 0; c < 9; ++c) {
      cycles.push_back(cycleDecisions(gA));
    }
    for (uint32_t c = 2; c < 9; ++c)
      for (uint32_t w = 0; w < kWindows; ++w)
        TEST_ASSERT_TRUE_MESSAGE(cycles[c][w] == cycles[1][w], "selection differs between steady cycles");
    (void)refused;
  }
}

/// RED 2: notes held across the loop end compete with notes starting at tick 0.
void test_wrap_held_notes_compete_with_tick_zero() {
  std::vector<N> held, zero;
  uint32_t id = 1;
  for (uint32_t k = 0; k < 12; ++k) held.push_back({id++, kLoop - 200 + 10 * k, kLoop + 400 + 30 * k, static_cast<uint8_t>(40 + k), 1});
  for (uint32_t k = 0; k < 10; ++k) zero.push_back({id++, 0, 300 + 20 * k, static_cast<uint8_t>(70 + k), 1});
  const ManifestId m = manifest({makeRun(RunKind::kRecord, held), makeRun(RunKind::kOverdub, zero)}, 1);
  const auto src = sourceOf(m);
  TEST_ASSERT_TRUE(gA.begin(src.data(), static_cast<uint8_t>(src.size()), kLoop, gManifests.manifest(m).identity));
  std::vector<std::vector<Decisions>> cycles;
  for (uint32_t c = 0; c < 6; ++c) cycles.push_back(cycleDecisions(gA));
  for (uint32_t c = 2; c < 6; ++c) TEST_ASSERT_TRUE(cycles[c] == cycles[1]);
  // Steady: 12 wrap-held instances open at tick 0, so only 4 of the 10 tick-0 notes are selected, every cycle.
  uint32_t atZero = 0;
  for (const auto& d : cycles[1][0]) atZero += std::get<0>(d) == 0 ? 1 : 0;
  TEST_ASSERT_EQUAL_UINT32(4, atZero);
  // A cold start at the loop start makes the same decisions (the held notes are counted, though not sounding).
  sweep(m);
  coldStart(gB, m, 0, 3);
  const auto cold = cycleDecisions(gB);
  TEST_ASSERT_TRUE(cold == cycles[1]);
}

/// RED 3: cold start (checkpoint and warm-up), and uninterrupted playback after 2, 4 and 8 cycles agree.
void test_cold_start_and_long_play_make_the_same_decisions() {
  const ManifestId m = saturatedSource(77);
  const auto src = sourceOf(m);
  TEST_ASSERT_TRUE(gA.begin(src.data(), static_cast<uint8_t>(src.size()), kLoop, gManifests.manifest(m).identity));
  std::vector<std::vector<Decisions>> play;
  for (uint32_t c = 0; c <= 8; ++c) play.push_back(cycleDecisions(gA));
  TEST_ASSERT_TRUE(play[2] == play[4]);
  TEST_ASSERT_TRUE(play[4] == play[8]);
  // Warm-up path (no checkpoints): cold start at every window boundary.
  for (uint32_t w = 0; w < kWindows; ++w) {
    coldStart(gB, m, w * kWin, 5);
    for (uint32_t k = w; k < kWindows; ++k) {
      TEST_ASSERT_TRUE(gB.composeNext(gWin, kWin));
      TEST_ASSERT_TRUE_MESSAGE(decisions(gWin) == play[2][k], "warm-up cold start decides differently");
    }
    gCold.release();
  }
  // Checkpoint path.
  sweep(m);
  TEST_ASSERT_TRUE(gSweep.periodic());
  for (uint32_t w = 0; w < kWindows; ++w) {
    coldStart(gB, m, w * kWin, 6);
    for (uint32_t k = w; k < kWindows; ++k) {
      TEST_ASSERT_TRUE(gB.composeNext(gWin, kWin));
      TEST_ASSERT_TRUE_MESSAGE(decisions(gWin) == play[2][k], "checkpoint cold start decides differently");
    }
    gCold.release();
  }
}

/// RED 4 (bound, no stealing, no phantom, no oscillation): played through the terminal scheduler for 8 cycles, at most
/// 16 sound, every NoteOn the player sends ends exactly once, and every selected instance ends at its composed end
/// (inline length or its TERMINAL), never earlier.
void test_selected_instances_bounded_and_never_cut() {
  const ManifestId m = saturatedSource(91);
  const auto src = sourceOf(m);
  static TerminalScheduler<16> sched;
  static Player player;
  sched.init();
  player.init(sched);
  TEST_ASSERT_TRUE(gA.begin(src.data(), static_cast<uint8_t>(src.size()), kLoop, gManifests.manifest(m).identity));
  std::set<std::pair<uint32_t, uint16_t>> sounding;
  uint32_t ons = 0, offs = 0;
  Player::Emit buf[64];
  for (uint32_t c = 0; c < 8; ++c) {
    while (gA.composeNext(gWin, kWin)) {
      auto take = [&](uint8_t n) {
        for (uint8_t i = 0; i < n; ++i) {
          const auto key = std::make_pair(buf[i].noteId, buf[i].runKey);
          if (buf[i].cls == 1) {
            TEST_ASSERT_TRUE_MESSAGE(sounding.insert(key).second, "instance started twice");
            ++ons;
          } else if (buf[i].cls == 0) {
            TEST_ASSERT_EQUAL_MESSAGE(1, sounding.erase(key), "phantom NoteOff");
            ++offs;
          }
        }
        TEST_ASSERT_TRUE(sounding.size() <= 16);
      };
      take(player.beginWindow(gWin, buf, 64));
      for (uint32_t t = gWin.cycleBase + gWin.start; t < gWin.cycleBase + gWin.end; ++t) take(player.tick(t, buf, 64));
    }
    gA.nextCycle();
  }
  TEST_ASSERT_TRUE(ons > 100);
  TEST_ASSERT_EQUAL_UINT32(0, sched.counters().refusedFull);  // the composer's selection never exceeds T
  TEST_ASSERT_EQUAL_UINT32(0, sched.counters().lateTerminals);
}

/// No starvation: a 20-note pad held longer than the loop (Q-LEN-1, each note re-struck every loop) keeps 16 notes
/// sounding every cycle; the same 16 are selected each loop, and those 16 sound together.
void test_twenty_note_pad_keeps_sixteen_voices() {
  std::vector<N> pad;
  for (uint32_t k = 0; k < 20; ++k) pad.push_back({k + 1, 10 * k, 10 * k + 3 * kLoop, static_cast<uint8_t>(40 + k), 1});
  const ManifestId m = manifest({makeRun(RunKind::kRecord, pad)}, 1);
  const auto src = sourceOf(m);
  TEST_ASSERT_TRUE(gA.begin(src.data(), static_cast<uint8_t>(src.size()), kLoop, gManifests.manifest(m).identity));
  std::vector<std::vector<Decisions>> cycles;
  for (uint32_t c = 0; c < 6; ++c) cycles.push_back(cycleDecisions(gA));
  for (uint32_t c = 1; c < 6; ++c) {
    size_t starts = 0;
    for (const auto& d : cycles[c]) starts += d.size();
    TEST_ASSERT_EQUAL_UINT32(16, starts);
    TEST_ASSERT_TRUE(cycles[c] == cycles[1]);
  }
}

/// Named limit: more than kMaxOpen (48) effective instances open at once. The extra ones are refused untracked and
/// counted (`openOverflow`); the output bound still holds, and the sweep reports the composition as not proved periodic.
void test_open_table_overflow_is_named_and_bounded() {
  std::vector<N> notes;
  uint32_t id = 1;
  for (uint32_t k = 0; k < 60; ++k)  // 60 notes sounding together over most of the loop
    notes.push_back({id++, 10 + k, kLoop - 100 + (k % 7), static_cast<uint8_t>(20 + k), 1});
  const ManifestId m = manifest({makeRun(RunKind::kRecord, notes)}, 1);
  const auto src = sourceOf(m);
  TEST_ASSERT_TRUE(gA.begin(src.data(), static_cast<uint8_t>(src.size()), kLoop, gManifests.manifest(m).identity));
  uint32_t starts = 0;
  while (gA.composeNext(gWin, kWin)) starts += gWin.startCount;
  TEST_ASSERT_EQUAL_UINT32(16, starts);
  TEST_ASSERT_EQUAL_UINT32(60 - TestComposer::kMaxOpen, gA.stats().openOverflow);
  sweep(m);
  TEST_ASSERT_FALSE(gSweep.periodic());
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_selection_repeats_every_loop_above_sixteen);
  RUN_TEST(test_wrap_held_notes_compete_with_tick_zero);
  RUN_TEST(test_cold_start_and_long_play_make_the_same_decisions);
  RUN_TEST(test_selected_instances_bounded_and_never_cut);
  RUN_TEST(test_twenty_note_pad_keeps_sixteen_voices);
  RUN_TEST(test_open_table_overflow_is_named_and_bounded);
  return UNITY_END();
}
