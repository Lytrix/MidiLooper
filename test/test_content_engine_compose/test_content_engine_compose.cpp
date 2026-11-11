//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// fixed-memory-engine E3: the span-window composer over real engine runs (written by the E2 writer), integrated with the
// terminal scheduler (OUT-POLY T = 16). The emitted stream is compared with the independent reference model.
//   * 384 source notes sounding at once, 16 sent, across several windows, with a cross-window shortening;
//   * a live writer note still held when its windows were composed (live close, no duplicate NoteOff);
//   * a revision switch at a window boundary (undo): uncarried instances closed, carried ones continue, a note that
//     was never sent is never closed.

#include <unity.h>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <new>
#include <vector>

#include "ContentEngine/Composition/SpanWindowComposer.h"
#include "ContentEngine/EngineCore.h"
#include "ContentEngine/Composition/SpanWindow.h"
#include "ContentEngine/Content/CaptureWriter.h"
#include "../test_engine_window_bound/span_window_model.h"

static unsigned long gHeapAllocations = 0;
static bool gTrapHeap = false;
void* operator new(std::size_t size) {
  if (gTrapHeap) ++gHeapAllocations;
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
using Manifests = ManifestTable<16, 8>;
using Res = RowReservations<Pool, Runs>;
using W = CaptureWriter<Pool, Runs>;
using TestWindow = SpanWindow<3072>;
using TestComposer = SpanWindowComposer<Pool, Runs>;
using Player = WindowPlayer<Config::kMaxSoundingPerTrack>;

static Pool gPool;
static Runs gRuns;
static Manifests gManifests;
static Res gRes;
static W gW;
static NoteIdSource gIds;
static TestComposer gC;
static TestWindow gWin;
static TerminalScheduler<Config::kMaxSoundingPerTrack> gSched;
static Player gPlayer;

constexpr uint32_t kLoop = 768 * 8;
constexpr uint32_t kWin = 1536;

struct NoteSpec {
  uint8_t channel, pitch;
  uint32_t on, off;
};

static void initAll() {
  gPool.init();
  gRuns.init();
  gManifests.init();
  gRes.init(gPool, gRuns, 0);
  gIds.init();
  gW.init(gPool, gRuns, gRes, gIds);
  gC.init(gPool, gRuns);
  gSched.init();
  gPlayer.init(gSched);
}

/// Records one pass from note specs through the writer (events in tick order, NoteOff before NoteOn at a tick).
/// `stopAt` = 0 leaves the take open (still writing); returns the run.
static RunId recordPass(const std::vector<NoteSpec>& notes, uint32_t stopAt) {
  struct Ev {
    uint32_t tick;
    bool on;
    NoteSpec n;
  };
  std::vector<Ev> ev;
  for (const auto& n : notes) {
    ev.push_back({n.on, true, n});
    if (n.off != 0) ev.push_back({n.off, false, n});
  }
  std::stable_sort(ev.begin(), ev.end(), [](const Ev& a, const Ev& b) {
    if (a.tick != b.tick) return a.tick < b.tick;
    return !a.on && b.on;
  });
  TEST_ASSERT_EQUAL(WriterResult::Ok, gW.begin(0, 1));
  const RunId run = gW.activeRun();
  for (const auto& e : ev) {
    const WriterResult r = e.on ? gW.noteOn(e.tick, e.n.channel, e.n.pitch, 100)
                                : gW.noteOff(e.tick, e.n.channel, e.n.pitch, 0);
    TEST_ASSERT_EQUAL(WriterResult::Ok, r);
  }
  if (stopAt != 0) gW.stop(stopAt);
  return run;
}

/// Reference notes read back from the runs (the writer's own LIFO pairing decides each note's NoteOff).
static std::vector<Oracle::Note> oracleNotes(const RunId* runs, uint8_t count) {
  std::vector<Oracle::Note> out;
  for (uint8_t order = 0; order < count; ++order) {
    const auto& r = gRuns.run(runs[order]);
    for (uint32_t i = 0; i < r.rowCount; ++i) {
      const PassEvent& e = gRuns.rowAt(runs[order], i, gPool);
      if ((e.status & 0xF0) != 0x90) continue;
      uint32_t off = 0;
      for (uint32_t k = i + 1; k < r.rowCount; ++k) {
        const PassEvent& x = gRuns.rowAt(runs[order], k, gPool);
        if ((x.status & 0xF0) == 0x80 && x.noteId == e.noteId) {
          off = x.tick;
          break;
        }
      }
      out.push_back({e.noteId, order, static_cast<uint8_t>((e.status & 0x0F) + 1), e.data1, e.tick, off});
    }
  }
  return out;
}

/// Plays a whole loop cycle of `source` through the composer and the player; returns the emitted stream.
static std::vector<Player::Emit> playCycle(const SourceRun* source, uint8_t count, uint32_t identity,
                                           uint32_t maxTerminals[1], uint32_t maxStarts[1], bool trapHeap = false) {
  std::vector<Player::Emit> out;
  out.reserve(8192);  // harness storage, allocated before the trap so only engine calls are counted
  Player::Emit buf[64];
  gTrapHeap = trapHeap;
  gC.begin(source, count, kLoop, identity);
  while (gC.composeNext(gWin, kWin)) {
    maxTerminals[0] = std::max<uint32_t>(maxTerminals[0], gWin.terminalCount);
    maxStarts[0] = std::max<uint32_t>(maxStarts[0], gWin.startCount);
    TEST_ASSERT_TRUE(gWin.carryCount <= Config::kMaxSoundingPerTrack);
    TEST_ASSERT_TRUE(gWin.count <= Config::kWindowSourceAdmission + Config::kMaxSoundingPerTrack);
    uint8_t n = gPlayer.beginWindow(gWin, buf, 64);
    out.insert(out.end(), buf, buf + n);
    for (uint32_t t = gWin.start; t < gWin.end; ++t) {
      n = gPlayer.tick(t, buf, 64);
      out.insert(out.end(), buf, buf + n);
      TEST_ASSERT_TRUE(gSched.sounding() <= Config::kMaxSoundingPerTrack);
    }
  }
  gTrapHeap = false;
  return out;
}

static bool sameStream(const std::vector<Player::Emit>& got, const std::vector<SpanModel::OutRow>& want) {
  if (got.size() != want.size()) return false;
  for (size_t i = 0; i < got.size(); ++i) {
    if (got[i].tick != want[i].tick || got[i].cls != want[i].cls || got[i].noteId != want[i].noteId / 64 ||
        got[i].channel != want[i].channel || got[i].pitch != want[i].pitch)
      return false;
  }
  return true;
}

void setUp() { initAll(); }
void tearDown() {}

// 384 source notes sounding at once (3 passes x 16 channels x 8 stacked, three pitches), spanning windows 0..2, plus a
// fourth pass whose note on pass 0's pitch shortens pass 0's sounding notes inside window 1. 16 are sent; the stream equals the
// reference with the same admission; every window stays inside the bound.
// Guards DEC-297 (deep-overdub timing): a one-grain bind costs about 13 us a run, past g at 63 runs. A source binds
// in steps of `kRunsPerStep` runs, and the stepped bind composes exactly what the one-shot bind does.
void test_a_source_binds_in_steps_of_runs() {
  SourceRun many[30];
  for (uint32_t k = 0; k < 30; ++k) {
    const RunId r = recordPass({{1, static_cast<uint8_t>(40 + k), 10 + 100 * k, 50 + 100 * k}}, kLoop);
    many[k] = {r, gRuns.run(r).rowCount};
  }
  TEST_ASSERT_TRUE(gC.begin(many, 30, kLoop, 7));  // one shot
  TEST_ASSERT_TRUE(gC.composeNext(gWin, kLoop));
  const uint16_t want = gWin.count;
  TEST_ASSERT_EQUAL_UINT16(30, want);  // one entry a note
  gC.bindStart(many, 30, kLoop, 8, true);
  uint8_t steps = 0;
  TestComposer::BindStep b;
  do {
    b = gC.bindMore(TestComposer::kRunsPerStep);
    ++steps;
  } while (b == TestComposer::BindStep::Running);
  TEST_ASSERT_EQUAL(TestComposer::BindStep::Done, b);
  TEST_ASSERT_EQUAL_UINT8(3, steps);  // 30 runs, 12 a step
  TEST_ASSERT_TRUE(gC.composeNext(gWin, kLoop));
  TEST_ASSERT_EQUAL_UINT16(want, gWin.count);
}
void test_384_source_16_output_across_windows() {
  std::vector<NoteSpec> passes[4];
  for (uint8_t r = 0; r < 3; ++r)
    for (uint8_t ch = 1; ch <= 16; ++ch)
      for (uint8_t k = 0; k < 8; ++k) {
        const uint32_t on = 100 + r * 300 + k * 20 + ch;
        passes[r].push_back({ch, static_cast<uint8_t>(40 + r), on, on + 3000});
      }
  passes[3].push_back({5, 40, 2000, 2100});  // a fourth pass shortens pass 0's pitch-40 notes sounding at 2000
  RunId runs[4];
  for (uint8_t r = 0; r < 4; ++r) runs[r] = recordPass(passes[r], 6000);
  const SourceRun source[] = {{runs[0], gRuns.run(runs[0]).rowCount},
                              {runs[1], gRuns.run(runs[1]).rowCount},
                              {runs[2], gRuns.run(runs[2]).rowCount},
                              {runs[3], gRuns.run(runs[3]).rowCount}};
  uint32_t maxTerm[1] = {0}, maxStarts[1] = {0};
  const unsigned long heapBefore = gHeapAllocations;
  const auto got = playCycle(source, 4, 1, maxTerm, maxStarts, /*trapHeap*/ true);
  TEST_ASSERT_EQUAL_UINT32(0, gHeapAllocations - heapBefore);
  const auto eff = Oracle::effective(oracleNotes(runs, 4), kLoop);
  uint32_t sourceSounding = 0;
  for (const auto& e : eff)
    if (e.fate != Oracle::Fate::Hidden && e.note.on <= 1800 && 1800 < e.note.off) ++sourceSounding;
  TEST_ASSERT_EQUAL_UINT32(384, sourceSounding);
  const auto want = SpanModel::expandedStream(SpanModel::timeline(eff, kLoop, 1), 0, kLoop, Config::kMaxSoundingPerTrack);
  TEST_ASSERT_TRUE(sameStream(got, want));
  TEST_ASSERT_TRUE(maxTerm[0] <= Config::kMaxSoundingPerTrack);
  TEST_ASSERT_TRUE(gSched.counters().refusedFull == 0);  // the composer already applied OUT-POLY
}

// A writer note still held when its windows are composed from the open prefix: START with an open end; the live NoteOff
// closes it now (once); the next cycle, composed from the sealed run, plays it with its recorded end.
void test_live_held_note_closed_live_then_recorded_end_next_cycle() {
  const RunId base = recordPass({{1, 60, 100, 400}}, 6000);
  TEST_ASSERT_EQUAL(WriterResult::Ok, gW.begin(0, 1));
  const RunId live = gW.activeRun();
  TEST_ASSERT_EQUAL(WriterResult::Ok, gW.noteOn(200, 2, 70, 100));  // held
  const SourceRun prefix[] = {{base, gRuns.run(base).rowCount}, {live, gRuns.run(live).rowCount}};
  Player::Emit buf[64];
  std::vector<Player::Emit> got;
  gC.begin(prefix, 2, kLoop, 7);
  TEST_ASSERT_TRUE(gC.composeNext(gWin, kWin));  // window 0 from the prefix
  uint8_t n = gPlayer.beginWindow(gWin, buf, 64);
  for (uint32_t t = 0; t < 1600; ++t) {
    if (t == 1536) {
      TEST_ASSERT_TRUE(gC.composeNext(gWin, kWin));  // window 1, still from the prefix
      n = gPlayer.beginWindow(gWin, buf, 64);
      got.insert(got.end(), buf, buf + n);
    }
    if (t == 1550) {  // the live NoteOff
      TEST_ASSERT_EQUAL(WriterResult::Ok, gW.noteOff(1550, 2, 70, 0));
      gPlayer.liveNoteOff(gIds.peek() - 1, live, t);  // sent by this tick
    }
    n = gPlayer.tick(t, buf, 64);
    got.insert(got.end(), buf, buf + n);
  }
  uint32_t offs = 0;
  for (const auto& e : got)
    if (e.cls == 0 && e.pitch == 70) {
      ++offs;
      TEST_ASSERT_EQUAL_UINT32(1550, e.tick);
    }
  TEST_ASSERT_EQUAL_UINT32(1, offs);
  gW.stop(6000);
  // Next cycle from the sealed runs: the held note ends at its recorded 1550.
  gSched.init();
  gPlayer.init(gSched);
  const SourceRun sealed[] = {{base, gRuns.run(base).rowCount}, {live, gRuns.run(live).rowCount}};
  uint32_t a[1] = {0}, b[1] = {0};
  const auto next = playCycle(sealed, 2, 8, a, b);
  bool sawOff = false;
  for (const auto& e : next)
    if (e.cls == 0 && e.pitch == 70) sawOff = e.tick == 1550;
  TEST_ASSERT_TRUE(sawOff);
}

// Revision switch (undo) at the window-2 boundary: M2 = {pass0, pass1}, then M1 = {pass0}.
void test_revision_switch_closes_uncarried_and_never_closes_unsent() {
  const RunId r0 = recordPass({{1, 60, 100, 5000}, {1, 62, 200, 4000}}, 6000);
  const RunId r1 = recordPass({{2, 60, 1000, 4500}, {2, 64, 2000, 5000}}, 6000);
  const SourceRun m2[] = {{r0, gRuns.run(r0).rowCount}, {r1, gRuns.run(r1).rowCount}};
  const SourceRun m1[] = {{r0, gRuns.run(r0).rowCount}};
  Player::Emit buf[64];
  std::vector<Player::Emit> got;
  gC.begin(m2, 2, kLoop, 2);
  for (uint32_t w = 0; w < 2; ++w) {  // windows 0, 1 from M2
    TEST_ASSERT_TRUE(gC.composeNext(gWin, kWin));
    uint8_t n = gPlayer.beginWindow(gWin, buf, 64);
    got.insert(got.end(), buf, buf + n);
    for (uint32_t t = gWin.start; t < gWin.end; ++t) {
      n = gPlayer.tick(t, buf, 64);
      got.insert(got.end(), buf, buf + n);
    }
  }
  static TestComposer c1;  // M1, composed from the loop start up to window 2
  c1.init(gPool, gRuns);
  c1.begin(m1, 1, kLoop, 1);
  for (uint32_t w = 0; w < 4; ++w) {
    TEST_ASSERT_TRUE(c1.composeNext(gWin, kWin));
    if (w < 2) continue;
    uint8_t n = gPlayer.beginWindow(gWin, buf, 64);  // identity changes at w = 2: transition
    got.insert(got.end(), buf, buf + n);
    for (uint32_t t = gWin.start; t < gWin.end; ++t) {
      n = gPlayer.tick(t, buf, 64);
      got.insert(got.end(), buf, buf + n);
    }
  }
  // Expected (ids from the writer: r0 = 1 (60), 2 (62); r1 = 3 (60), 4 (64)).
  struct X {
    uint32_t tick;
    uint8_t cls;
    uint32_t id;
  } want[] = {{100, 1, 1}, {200, 1, 2}, {999, 0, 1}, {1000, 1, 3}, {2000, 1, 4},
              {3072, 0, 3}, {3072, 0, 4}, {4000, 0, 2}};
  TEST_ASSERT_EQUAL_UINT32(8, got.size());
  for (size_t i = 0; i < 8; ++i) {
    TEST_ASSERT_EQUAL_UINT32(want[i].tick, got[i].tick);
    TEST_ASSERT_EQUAL_UINT8(want[i].cls, got[i].cls);
    TEST_ASSERT_EQUAL_UINT32(want[i].id, got[i].noteId);
  }
}

// Unique attribution: an edit run's replacement row keeps the hidden note's id. A sounding instance from the original
// run must not be taken for the replacement after a revision switch: instances are keyed by (noteId, run).
void test_instance_identity_includes_run() {
  TerminalScheduler<4> s;
  s.init();
  TerminalScheduler<4>::Out out[4];
  TEST_ASSERT_EQUAL(StartResult::Started, s.start(10, 42, /*run*/ 3, /*order*/ 0, 1, 60, kOpenEnd));
  const TerminalScheduler<4>::Carry replacement[] = {{42, /*run*/ 9, 500}};
  TEST_ASSERT_EQUAL(1, s.transition(100, replacement, 1, out, 4));  // not the same instance: closed
  TEST_ASSERT_EQUAL_UINT32(42, out[0].noteId);
  TEST_ASSERT_EQUAL(StartResult::Started, s.start(110, 42, 9, 1, 1, 60, 500));  // the replacement may now sound
  const TerminalScheduler<4>::Carry same[] = {{42, 9, 500}};
  TEST_ASSERT_EQUAL(0, s.transition(200, same, 1, out, 4));
  TEST_ASSERT_EQUAL(1, s.sounding());
}

// The same note id in two passes (an edit run's replacement keeps the hidden note's id): the composer must end each
// instance from its own run's NoteOff. Matching by id alone would close the earlier pass's note at the later NoteOff.
void test_same_note_id_in_two_runs_ends_each_from_its_own_run() {
  const RunId a = gRuns.allocate(0, 0);
  const RunId b = gRuns.allocate(0, 0);
  auto put = [](RunId r, uint32_t tick, uint8_t status, uint8_t pitch, uint8_t vel) {
    PassEvent e;
    e.tick = tick;
    e.noteId = 5;
    e.seq = tick;
    e.status = status;
    e.data1 = pitch;
    e.data2 = vel;
    TEST_ASSERT_EQUAL(AppendResult::Ok, gRuns.append(r, e, gPool));
  };
  put(a, 100, 0x90, 60, 100);
  put(a, 500, 0x80, 60, 0);
  put(b, 300, 0x90, 72, 100);
  put(b, 400, 0x80, 72, 0);
  gRuns.seal(a);
  gRuns.seal(b);
  const SourceRun src[] = {{a, 2}, {b, 2}};
  uint32_t x[1] = {0}, y[1] = {0};
  const auto got = playCycle(src, 2, 3, x, y);
  TEST_ASSERT_EQUAL_UINT32(4, got.size());
  TEST_ASSERT_EQUAL_UINT32(400, got[2].tick);
  TEST_ASSERT_EQUAL_UINT8(72, got[2].pitch);
  TEST_ASSERT_EQUAL_UINT32(500, got[3].tick);
  TEST_ASSERT_EQUAL_UINT8(60, got[3].pitch);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_384_source_16_output_across_windows);
  RUN_TEST(test_a_source_binds_in_steps_of_runs);
  RUN_TEST(test_live_held_note_closed_live_then_recorded_end_next_cycle);
  RUN_TEST(test_revision_switch_closes_uncarried_and_never_closes_unsent);
  RUN_TEST(test_instance_identity_includes_run);
  RUN_TEST(test_same_note_id_in_two_runs_ends_each_from_its_own_run);
  return UNITY_END();
}
