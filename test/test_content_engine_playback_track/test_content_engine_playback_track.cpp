//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// fixed-memory-engine E6.1: one track's playback windows (plan revision 3 § 9.1). Three buffers (playing, next,
// transition), a steady and a transition composer, deterministic succession at boundaries. The track's emitted stream
// is compared with a reference that plays the same schedule from two uninterrupted compositions (one per source,
// composed in lockstep from cycle 0) through its own player: a switch on time, a late switch (transition not ready at
// its boundary: the old source continues, the switch lands one boundary later), an underrun (nothing composed), a
// selection undone before its boundary, and no heap.

#include <unity.h>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <new>
#include <vector>

#include "ContentEngine/Composer.h"
#include "ContentEngine/EngineCore.h"
#include "ContentEngine/PlaybackTrack.h"

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

using Pool = PagePool<1024, 64>;
using Runs = RunTable<32, Pool>;
using TestComposer = Composer<Pool, Runs>;
using Window = SpanWindow<3072>;
using Table = CheckpointTable<TestComposer::Checkpoint, 2, 8>;
using Manifests = ManifestTable<16, 8>;
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
static TestComposer gA, gB, gRefA, gRefB;
static Window gBuf[3], gRefWinA, gRefWinB;
static TerminalScheduler<kT> gSched, gRefSched;
static Player gPlayer, gRefPlayer;
static Track gTrack;
static ManifestId gSrcA, gSrcB;

struct N {
  uint32_t id, on, off;
  uint8_t pitch;
};

static ManifestId source(const std::vector<N>& notes, uint32_t revision) {
  const RunId r = gRuns.allocate(0, RunKind::kRecord);
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
    if (e.tick >= kLoop) p.origin = kOriginHeldLoop;
    TEST_ASSERT_EQUAL(AppendResult::Ok, gRuns.append(r, p, gPool));
  }
  gRuns.seal(r);
  const ManifestId m = gManifests.create(0, revision, kLoop, &r, 1, gRuns);
  gRuns.release(r);
  return m;
}

void setUp() {
  gPool.init();
  gRuns.init();
  gManifests.init();
  gTable.init();
  for (auto* c : {&gA, &gB, &gRefA, &gRefB}) c->init(gPool, gRuns, nullptr);
  gSched.init();
  gRefSched.init();
  gPlayer.init(gSched);
  gRefPlayer.init(gRefSched);
  Window* bufs[3] = {&gBuf[0], &gBuf[1], &gBuf[2]};
  gTrack.init(gA, gB, bufs, gTable, gManifests, gRuns, gPlayer, kWin);
  std::vector<N> a, b;
  for (uint32_t i = 0; i < 20; ++i) a.push_back({1 + i, i * 300 + 7, i * 300 + 7 + 700, static_cast<uint8_t>(40 + i % 12)});
  a.push_back({60, kLoop - 200, kLoop + 400, 70});  // held across the loop end
  for (uint32_t i = 0; i < 16; ++i) b.push_back({100 + i, i * 377 + 50, i * 377 + 50 + 250, static_cast<uint8_t>(52 + i % 9)});
  gSrcA = source(a, 1);
  gSrcB = source(b, 2);
}
void tearDown() {}

/// Reference: both sources composed uninterrupted from cycle 0 in lockstep, one window per boundary; `pick(t)` names
/// the source (0 A, 1 B, -1 none) that plays the window starting at t.
template <typename Pick>
static std::vector<Emit> reference(uint32_t from, uint32_t to, Pick pick) {
  auto beginSrc = [](TestComposer& c, ManifestId m) {
    const auto& man = gManifests.manifest(m);
    SourceRun src[8];
    for (uint8_t i = 0; i < man.runCount; ++i) src[i] = {man.runs[i], gRuns.run(man.runs[i]).rowCount};
    TEST_ASSERT_TRUE(c.begin(src, man.runCount, kLoop, man.identity));
  };
  beginSrc(gRefA, gSrcA);
  beginSrc(gRefB, gSrcB);
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
      const int p = t >= from ? pick(t) : -1;
      if (p >= 0) {
        const uint8_t n = gRefPlayer.beginWindow(p == 0 ? gRefWinA : gRefWinB, buf, 64);
        out.insert(out.end(), buf, buf + n);
      }
    }
    if (t >= from) {
      const uint8_t n = gRefPlayer.tick(t, buf, 64);
      out.insert(out.end(), buf, buf + n);
    }
  }
  gRefA.end();
  gRefB.end();
  return out;
}

static void expectEqual(const std::vector<Emit>& want, const std::vector<Emit>& got) {
  TEST_ASSERT_TRUE(want.size() > 20);  // the comparison covers real music
  TEST_ASSERT_EQUAL_UINT32(want.size(), got.size());
  for (size_t i = 0; i < want.size(); ++i) {
    TEST_ASSERT_EQUAL_UINT32(want[i].tick, got[i].tick);
    TEST_ASSERT_EQUAL_UINT8(want[i].cls, got[i].cls);
    TEST_ASSERT_EQUAL_UINT32(want[i].noteId, got[i].noteId);
  }
}
static void workAll() {
  for (uint32_t g = 0; gTrack.work(128); ++g) TEST_ASSERT_TRUE(g < 10000);
}

// A at the start of cycle 1, B selected mid-window: B from the next boundary, both on time.
void test_switch_on_time() {
  const uint32_t t0 = kLoop, sel = kLoop + 3 * kWin + 100, b = kLoop + 4 * kWin;
  std::vector<Emit> got;
  Emit buf[64];
  gTrack.select(0, gSrcA, t0 - 1);
  workAll();
  for (uint32_t t = t0; t < 3 * kLoop; ++t) {
    if (t == sel) gTrack.select(0, gSrcB, t);
    workAll();  // ample time before every boundary
    const uint8_t n = gTrack.tick(t, buf, 64);
    got.insert(got.end(), buf, buf + n);
  }
  expectEqual(reference(t0, 3 * kLoop, [b](uint32_t t) { return t < b ? 0 : 1; }), got);
  TEST_ASSERT_EQUAL_UINT32(2, gTrack.stats().switches);
  TEST_ASSERT_EQUAL_UINT32(0, gTrack.stats().lateSwitches);
  TEST_ASSERT_EQUAL_UINT32(0, gTrack.stats().underruns);
}

// The transition gets no grains until after its boundary: the old source plays that window, the switch lands later.
void test_late_switch_keeps_the_old_source() {
  const uint32_t t0 = kLoop, sel = kLoop + 3 * kWin + 100, b = kLoop + 4 * kWin;
  std::vector<Emit> got;
  Emit buf[64];
  gTrack.select(0, gSrcA, t0 - 1);
  workAll();
  uint32_t switchedAt = 0;
  for (uint32_t t = t0; t < 3 * kLoop; ++t) {
    if (t == sel) gTrack.select(0, gSrcB, t);
    if (t < sel || t > b + 10) workAll();  // starved from the selection until just after its boundary
    const uint32_t before = gTrack.stats().switches;
    const uint8_t n = gTrack.tick(t, buf, 64);
    if (gTrack.stats().switches != before) switchedAt = t;
    got.insert(got.end(), buf, buf + n);
  }
  TEST_ASSERT_EQUAL_UINT32(b + kWin, switchedAt);
  TEST_ASSERT_EQUAL_UINT32(1, gTrack.stats().lateSwitches);
  expectEqual(reference(t0, 3 * kLoop, [switchedAt](uint32_t t) { return t < switchedAt ? 0 : 1; }), got);
}

// Nothing composed for the first boundaries: underruns (silence), then A from the boundary its window is ready for.
void test_underrun_then_start() {
  const uint32_t t0 = kLoop;
  std::vector<Emit> got;
  Emit buf[64];
  gTrack.select(0, gSrcA, t0 - 1);
  uint32_t startedAt = 0;
  for (uint32_t t = t0; t < 3 * kLoop; ++t) {
    if (t > t0 + 2 * kWin + 5) workAll();
    const uint32_t before = gTrack.stats().switches;
    const uint8_t n = gTrack.tick(t, buf, 64);
    if (gTrack.stats().switches != before) startedAt = t;
    got.insert(got.end(), buf, buf + n);
  }
  TEST_ASSERT_EQUAL_UINT32(t0 + 3 * kWin, startedAt);
  TEST_ASSERT_EQUAL_UINT32(3, gTrack.stats().underruns);
  expectEqual(reference(t0, 3 * kLoop, [startedAt](uint32_t t) { return t < startedAt ? -1 : 0; }), got);
}

// The steady composer is starved across a boundary: that window is silent (an underrun), the late window is
// discarded, and the source resumes at the next boundary.
void test_steady_underrun_recovers() {
  const uint32_t t0 = kLoop, b = kLoop + 3 * kWin;
  std::vector<Emit> got;
  Emit buf[64];
  gTrack.select(0, gSrcA, t0 - 1);
  workAll();
  uint32_t resumedAt = 0;
  for (uint32_t t = t0; t < 3 * kLoop; ++t) {
    if (t < b - kWin || t > b + 10) workAll();  // nothing composed while window b would be
    const uint32_t played = gTrack.playingIdentity();
    const uint8_t n = gTrack.tick(t, buf, 64);
    if (played == 0 && gTrack.playingIdentity() != 0 && t > b) resumedAt = t;
    got.insert(got.end(), buf, buf + n);
  }
  TEST_ASSERT_EQUAL_UINT32(b + kWin, resumedAt);
  TEST_ASSERT_EQUAL_UINT32(1, gTrack.stats().underruns);
  expectEqual(reference(t0, 3 * kLoop, [b](uint32_t t) { return t == b ? -1 : 0; }), got);
}

// B selected then A again before B's boundary: no transition, the stream is A's.
void test_selection_undone_before_its_boundary() {
  const uint32_t t0 = kLoop;
  std::vector<Emit> got;
  Emit buf[64];
  gTrack.select(0, gSrcA, t0 - 1);
  workAll();
  for (uint32_t t = t0; t < 2 * kLoop; ++t) {
    if (t == t0 + 500) gTrack.select(0, gSrcB, t);
    if (t == t0 + 900) gTrack.select(0, gSrcA, t);
    workAll();
    const uint8_t n = gTrack.tick(t, buf, 64);
    got.insert(got.end(), buf, buf + n);
  }
  TEST_ASSERT_FALSE(gTrack.transitionPending());
  TEST_ASSERT_EQUAL_UINT32(1, gTrack.stats().switches);
  expectEqual(reference(t0, 2 * kLoop, [](uint32_t) { return 0; }), got);
}

void test_no_heap_while_playing() {
  Emit buf[64];
  gTrack.select(0, gSrcA, kLoop - 1);
  workAll();
  gTrap = true;
  const unsigned long before = gHeapAllocations;
  for (uint32_t t = kLoop; t < 3 * kLoop; ++t) {
    if (t == kLoop + 2000) gTrack.select(0, gSrcB, t);
    gTrack.work(128);
    gTrack.tick(t, buf, 64);
  }
  gTrack.stop();
  gTrap = false;
  TEST_ASSERT_EQUAL_UINT32(0, gHeapAllocations - before);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_switch_on_time);
  RUN_TEST(test_late_switch_keeps_the_old_source);
  RUN_TEST(test_underrun_then_start);
  RUN_TEST(test_steady_underrun_recovers);
  RUN_TEST(test_selection_undone_before_its_boundary);
  RUN_TEST(test_no_heap_while_playing);
  return UNITY_END();
}
