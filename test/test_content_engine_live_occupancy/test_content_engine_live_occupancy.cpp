//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// fixed-memory-engine E6.2f: live occupancy while an interval is recorded (plan § 6.2 "live effect", § 9.2 step 2).
// Track 0 plays A. During cycle 2 the musician plays B on top: each accepted live NoteOn ends the sounding notes on its
// pitch now and suppresses A's NoteOns there until its NoteOff (any channel, DEC-033). From cycle 3 the stored runs
// (A, then B as the later pass) are composed. The plan requires the live and composed results to agree; this test
// compares, note by note, what A sounded in cycle 2 (live) with what it sounds in cycle 3 (composed). Exact agreement is
// asserted where it holds; the differences live input cannot avoid (it cannot see a note before it is played) are
// asserted as named, measured differences for the owner's review.

#include <unity.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <map>
#include <vector>

#include "ContentEngine/Composition/SpanWindowComposer.h"
#include "ContentEngine/EngineCore.h"
#include "ContentEngine/Playback/PlaybackTrack.h"

using namespace ContentEngine;

using Pool = PagePool<256, 64>;
using Runs = RunTable<16, Pool>;
using TestComposer = SpanWindowComposer<Pool, Runs>;
using Window = SpanWindow<3072>;
using Table = CheckpointTable<TestComposer::Checkpoint, 2, 8>;
using Manifests = ManifestTable<8, 8>;
constexpr uint16_t kT = Config::kMaxSoundingPerTrack;
using Track = PlaybackTrack<TestComposer, Table, Window, Manifests, Runs, kT>;
using Player = WindowPlayer<kT>;
using Emit = Player::Emit;

constexpr uint32_t kLoop = 768 * 8;
constexpr uint32_t kWin = 1536;

static Pool gPool;
static Runs gRuns;
static Manifests gManifests;
static Table gTable;
static TestComposer gA, gB, gRef;
static Window gBuf[3], gRefWin;
static TerminalScheduler<kT> gSched, gRefSched;
static Player gPlayer, gRefPlayer;
static Track gTrack;

struct N {
  uint32_t id;
  uint8_t pitch;
  uint32_t on, off;
};
// A (the take playing) and B (played live over it in cycle 2, stored as the next pass).
static const std::vector<N> kA = {
    {1, 60, 100, 600},    // sounding when B1 starts: shortened
    {2, 60, 400, 450},    // starts while B1 is held: hidden
    {3, 62, 1000, 1200},  // starts on the same tick as B2
    {4, 64, 2000, 2300},  // B3 starts 4 ticks after it
    {5, 66, 3000, 3100},  // B4 ended before it: unaffected
    {6, 70, 100, 900},    // other pitch: unaffected
    {7, 60, 700, 800},    // after B1's release: unaffected
};
static const std::vector<N> kB = {{101, 60, 300, 500}, {102, 62, 1000, 1100}, {103, 64, 2004, 2100}, {104, 66, 2800, 2900}};

static RunId run(const std::vector<N>& notes, uint8_t kind) {
  const RunId r = gRuns.allocate(0, kind);
  struct Ev {
    uint32_t tick;
    bool on;
    N n;
  };
  std::vector<Ev> ev;
  for (const N& n : notes) {
    ev.push_back({n.on, true, n});
    ev.push_back({n.off, false, n});
  }
  std::stable_sort(ev.begin(), ev.end(), [](const Ev& a, const Ev& b) { return a.tick < b.tick || (a.tick == b.tick && !a.on && b.on); });
  for (const Ev& e : ev) {
    PassEvent p{};
    p.tick = e.tick;
    p.noteId = e.n.id;
    p.status = e.on ? 0x90 : 0x80;
    p.data1 = e.n.pitch;
    p.data2 = e.on ? 100 : 0;
    TEST_ASSERT_EQUAL(AppendResult::Ok, gRuns.append(r, p, gPool));
  }
  gRuns.seal(r);
  return r;
}

struct Span {
  uint32_t on = 0, off = 0;
  bool played = false;
};
/// Per A note, its first NoteOn and the following NoteOff within [from, from + kLoop), as loop positions.
static std::map<uint32_t, Span> spans(const std::vector<Emit>& emits, uint32_t from) {
  std::map<uint32_t, Span> out;
  for (const Emit& e : emits) {
    if (e.tick < from || e.tick >= from + kLoop || e.noteId > 100) continue;
    Span& s = out[e.noteId];
    if (e.cls == 1 && !s.played) {
      s.played = true;
      s.on = e.tick - from;
    } else if (e.cls == 0 && s.played && s.off == 0) {
      s.off = e.tick - from;
    }
  }
  return out;
}

void setUp() {
  gPool.init();
  gRuns.init();
  gManifests.init();
  gTable.init();
  for (auto* c : {&gA, &gB, &gRef}) c->init(gPool, gRuns, nullptr);
  gSched.init();
  gRefSched.init();
  gPlayer.init(gSched);
  gRefPlayer.init(gRefSched);
  Window* bufs[3] = {&gBuf[0], &gBuf[1], &gBuf[2]};
  gTrack.init(gA, gB, bufs, gTable, gManifests, gRuns, gPlayer, kWin);
}
void tearDown() {}

void test_live_occupancy_against_the_composed_next_cycle() {
  const RunId ra = run(kA, RunKind::kRecord);
  const RunId rb = run(kB, RunKind::kOverdub);
  const ManifestId mA = gManifests.create(0, 1, kLoop, &ra, 1, gRuns);
  const RunId both[2] = {ra, rb};
  const ManifestId mAB = gManifests.create(0, 2, kLoop, both, 2, gRuns);

  // Live: A plays; in cycle 2 the musician plays B. Each live event lands after that tick's playback (input arrives
  // between ticks); its NoteOffs for A go out at the same tick.
  std::vector<Emit> live;
  Emit buf[64];
  gTrack.select(0, mA, kLoop - 1300);
  for (uint32_t t = kLoop - 1300; t < 3 * kLoop; ++t) {
    while (gTrack.work(128)) {
    }
    uint8_t n = gTrack.tick(t, buf, 64);
    live.insert(live.end(), buf, buf + n);
    if (t >= 2 * kLoop)
      for (const N& b : kB) {
        if (t - 2 * kLoop == b.on) {
          n = gTrack.liveNoteOn(b.pitch, t, buf, 64);
          live.insert(live.end(), buf, buf + n);
        }
        if (t - 2 * kLoop == b.off) gTrack.liveNoteOff(b.pitch);
      }
  }
  // Composed: A + B as stored runs, played uninterrupted; cycle 3 is steady.
  const SourceRun src[2] = {{ra, gRuns.run(ra).rowCount}, {rb, gRuns.run(rb).rowCount}};
  TEST_ASSERT_TRUE(gRef.begin(src, 2, kLoop, gManifests.manifest(mAB).identity));
  std::vector<Emit> composed;
  for (uint32_t t = 0; t < 4 * kLoop; ++t) {
    if ((t % kLoop) % kWin == 0) {
      if (!gRef.composeNext(gRefWin, kWin)) {
        gRef.nextCycle();
        TEST_ASSERT_TRUE(gRef.composeNext(gRefWin, kWin));
      }
      const uint8_t n = gRefPlayer.beginWindow(gRefWin, buf, 64);
      composed.insert(composed.end(), buf, buf + n);
    }
    const uint8_t n = gRefPlayer.tick(t, buf, 64);
    composed.insert(composed.end(), buf, buf + n);
  }
  auto L = spans(live, 2 * kLoop), C = spans(composed, 3 * kLoop);
  for (const N& a : kA) {
    char line[160];
    snprintf(line, sizeof line, "A%u pitch %u [%u,%u]: live %s [%u,%u], composed %s [%u,%u]", a.id, a.pitch, a.on, a.off,
             L[a.id].played ? "plays" : "silent", L[a.id].on, L[a.id].off, C[a.id].played ? "plays" : "silent",
             C[a.id].on, C[a.id].off);
    TEST_MESSAGE(line);
  }
  // Agreement: hidden while B is held, and every unaffected note.
  for (uint32_t id : {2u, 5u, 6u, 7u}) {
    TEST_ASSERT_EQUAL(C[id].played, L[id].played);
    TEST_ASSERT_EQUAL_UINT32(C[id].on, L[id].on);
    TEST_ASSERT_EQUAL_UINT32(C[id].off, L[id].off);
  }
  TEST_ASSERT_FALSE(L[2].played);
  // Named differences (live cannot see B before it is played):
  // D1 shorten: composed ends A at B.on - 1 (OVL-1), live at B.on.
  TEST_ASSERT_TRUE(C[1].played && L[1].played);
  TEST_ASSERT_EQUAL_UINT32(299, C[1].off);
  TEST_ASSERT_EQUAL_UINT32(300, L[1].off);
  // D2 equal start: composed hides A (OVL-2); live has already sent it, so it sounds for 0 ticks.
  TEST_ASSERT_FALSE(C[3].played);
  TEST_ASSERT_TRUE(L[3].played && L[3].on == 1000 && L[3].off == 1000);
  // D3 B within 8 ticks after A's start: composed hides A (OVL-6 minimum length); live A sounds until B.
  TEST_ASSERT_FALSE(C[4].played);
  TEST_ASSERT_TRUE(L[4].played && L[4].on == 2000 && L[4].off == 2004);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_live_occupancy_against_the_composed_next_cycle);
  return UNITY_END();
}
