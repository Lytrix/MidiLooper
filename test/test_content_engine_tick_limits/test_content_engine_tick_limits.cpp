//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// fixed-memory-engine E6.2: per-tick limits (owner decision 2026-11-05). Continuous controllers (every CC but the
// essential and parameter ones, pitch bend, pressure) are limited to 32 values per track per tick, when recorded (the
// writer) and when played (the composer leaves the rest out of the window); essential messages (Bank Select, Program Change, CC64-69, CC120-127) and RPN/NRPN are never limited.
// The player consumes a row only if it can emit it: the rest waits for the next tick (spill), and due NoteOffs that
// do not fit stay due. Nothing is consumed without being emitted.

#include <unity.h>

#include <cstdint>
#include <cstdlib>
#include <new>
#include <vector>

#include "ContentEngine/Composer.h"
#include "ContentEngine/EngineCore.h"
#include "ContentEngine/MidiClass.h"
#include "ContentEngine/SpanWindow.h"
#include "ContentEngine/Writer.h"

using namespace ContentEngine;

using Pool = PagePool<96, 8>;
using Runs = RunTable<24, Pool, 4, 4>;
using Res = RowReservations<Pool, Runs>;
using TestWriter = Writer<Pool, Runs>;
constexpr uint16_t kT = Config::kMaxSoundingPerTrack;
using Player = WindowPlayer<kT>;

static Pool gPool;
static Runs gRuns;
static Res gRes;
static TestWriter gW;
static NoteIdSource gIds;
static SpanWindow<512> gWin;
static TerminalScheduler<kT> gSched;
static Player gPlayer;

void setUp() {
  gPool.init();
  gRuns.init();
  gRes.init(gPool, gRuns, 0);
  gIds.init();
  gW.init(gPool, gRuns, gRes, gIds);
  gSched.init();
  gPlayer.init(gSched);
  gWin.reset(1, 0, 1536);
}
void tearDown() {}

void test_classification() {
  TEST_ASSERT_EQUAL(ControlClass::Essential, controlClass(0xB0, 64));  // sustain
  TEST_ASSERT_EQUAL(ControlClass::Essential, controlClass(0xB3, 69));
  TEST_ASSERT_EQUAL(ControlClass::Essential, controlClass(0xC2, 5));   // program change
  TEST_ASSERT_EQUAL(ControlClass::Essential, controlClass(0xB0, 0));   // bank select
  TEST_ASSERT_EQUAL(ControlClass::Essential, controlClass(0xB0, 123));  // all notes off
  TEST_ASSERT_EQUAL(ControlClass::Parameter, controlClass(0xB0, 101));
  TEST_ASSERT_EQUAL(ControlClass::Parameter, controlClass(0xB0, 6));
  TEST_ASSERT_EQUAL(ControlClass::Continuous, controlClass(0xB0, 1));   // mod wheel
  TEST_ASSERT_EQUAL(ControlClass::Continuous, controlClass(0xB0, 74));
  TEST_ASSERT_EQUAL(ControlClass::Continuous, controlClass(0xE0, 0));   // pitch bend
  TEST_ASSERT_EQUAL(ControlClass::Continuous, controlClass(0xD0, 40));  // channel pressure
}

// Recording: 40 mod-wheel values on one tick keep 32; sustain, program change and RPN on that tick all record; the
// next tick has its own 32.
void test_writer_limits_continuous_per_tick() {
  TEST_ASSERT_EQUAL(WriterResult::Ok, gW.begin(0, RunKind::kRecord));
  uint32_t ok = 0, limited = 0;
  for (uint8_t v = 0; v < 40; ++v) {
    const WriterResult r = gW.channelEvent(100, 0xB0, 1, v);
    ok += r == WriterResult::Ok;
    limited += r == WriterResult::Limited;
  }
  TEST_ASSERT_EQUAL_UINT32(32, ok);
  TEST_ASSERT_EQUAL_UINT32(8, limited);
  TEST_ASSERT_EQUAL(WriterResult::Ok, gW.channelEvent(100, 0xB0, 64, 127));
  TEST_ASSERT_EQUAL(WriterResult::Ok, gW.channelEvent(100, 0xC0, 3, 0));
  TEST_ASSERT_EQUAL(WriterResult::Ok, gW.channelEvent(100, 0xB0, 101, 0));
  TEST_ASSERT_EQUAL(WriterResult::Limited, gW.channelEvent(100, 0xE0, 0, 64));  // pitch bend shares the limit
  TEST_ASSERT_EQUAL(WriterResult::Ok, gW.channelEvent(101, 0xE0, 0, 64));
  TEST_ASSERT_EQUAL_UINT32(9, gW.continuousLimited());
  TEST_ASSERT_EQUAL_UINT32(32 + 3 + 1, gRuns.run(gW.activeRun()).rowCount);
}

static void add(uint32_t tick, uint32_t id, uint8_t status, uint8_t d1, uint8_t d2, uint16_t length = 0) {
  gWin.entries[gWin.count++] = SpanEntry{tick, id, length, 0, status, d1, d2, 0};
}

// Playback: two passes put 50 continuous rows, 3 essential rows and 2 NoteOns on one tick. The composed window keeps
// the notes, the essentials and the first 32 controllers in DEC-244 order — the same every cycle.
void test_composer_limits_continuous_per_tick() {
  using CPool = PagePool<64, 64>;
  using CRuns = RunTable<8, CPool>;
  static CPool pool;
  static CRuns runs;
  static Composer<CPool, CRuns> comp;
  static SpanWindow<512> win;
  pool.init();
  runs.init();
  comp.init(pool, runs, nullptr);
  auto row = [](uint32_t tick, uint32_t id, uint8_t status, uint8_t d1, uint8_t d2) {
    PassEvent e{};
    e.tick = tick;
    e.noteId = id;
    e.status = status;
    e.data1 = d1;
    e.data2 = d2;
    return e;
  };
  RunId r[2];
  for (uint8_t p = 0; p < 2; ++p) {
    r[p] = runs.allocate(0, p == 0 ? RunKind::kRecord : RunKind::kOverdub);
    runs.append(r[p], row(10, 1 + p, 0x90, static_cast<uint8_t>(60 + p), 100), pool);
    if (p == 0) {
      runs.append(r[p], row(10, 0, 0xB0, 64, 127), pool);
      runs.append(r[p], row(10, 0, 0xC0, 7, 0), pool);
      runs.append(r[p], row(10, 0, 0xB0, 0, 1), pool);
    }
    for (uint8_t i = 0; i < 25; ++i) runs.append(r[p], row(10, 0, 0xB0, 1, static_cast<uint8_t>(p * 25 + i)), pool);
    runs.append(r[p], row(30, 1 + p, 0x80, static_cast<uint8_t>(60 + p), 0), pool);
    runs.seal(r[p]);
  }
  const SourceRun src[2] = {{r[0], runs.run(r[0]).rowCount}, {r[1], runs.run(r[1]).rowCount}};
  TEST_ASSERT_TRUE(comp.begin(src, 2, 768, 1));
  for (uint32_t cycle = 0; cycle < 2; ++cycle) {
    uint32_t notes = 0, essential = 0, continuous = 0, lastValue = 0;
    while (comp.composeNext(win, 768))
      for (uint16_t k = 0; k < win.count; ++k) {
        const SpanEntry& e = win.entries[k];
        if (e.terminal || e.tick != 10) continue;
        if ((e.status & 0xF0) == 0x90) {
          ++notes;
        } else if (controlClass(e.status, e.data1) == ControlClass::Essential) {
          ++essential;
        } else {
          ++continuous;
          lastValue = e.data2;
        }
      }
    TEST_ASSERT_EQUAL_UINT32(2, notes);
    TEST_ASSERT_EQUAL_UINT32(3, essential);
    TEST_ASSERT_EQUAL_UINT32(Config::kMaxContinuousPerTick, continuous);
    TEST_ASSERT_EQUAL_UINT32(31, lastValue);  // pass 0's 25 values, then pass 1's first 7
    comp.nextCycle();
  }
  TEST_ASSERT_EQUAL_UINT32(2 * 18, comp.stats().continuousLimited);
  comp.end();
}

// A buffer of 8: the tick emits 8 and the rest wait; over the next ticks every row plays once, in order.
void test_player_spills_instead_of_losing() {
  for (uint8_t i = 0; i < 20; ++i) add(10, 0, 0xB0, 64, i);  // essential: never limited
  add(10, 1, 0x90, 60, 100, 40);
  Player::Emit out[8];
  gPlayer.beginWindow(gWin, out, 8);
  std::vector<Player::Emit> got;
  for (uint32_t t = 10; t < 20; ++t) {
    const uint8_t n = gPlayer.tick(t, out, 8);
    got.insert(got.end(), out, out + n);
  }
  TEST_ASSERT_EQUAL_UINT32(21, got.size());
  for (uint8_t i = 0; i < 20; ++i) TEST_ASSERT_EQUAL_UINT8(2, got[i].cls);
  TEST_ASSERT_EQUAL_UINT8(1, got[20].cls);  // the NoteOn after them, late but played
  TEST_ASSERT_EQUAL_UINT32(2, gPlayer.stats().spills);
}

// 16 notes end on one tick and the buffer holds 4: every NoteOff still goes out, 4 per tick.
void test_due_noteoffs_wait_instead_of_lost() {
  for (uint8_t i = 0; i < kT; ++i) add(0, 100 + i, 0x90, static_cast<uint8_t>(40 + i), 100, 5);
  Player::Emit out[64];
  gPlayer.beginWindow(gWin, out, 64);
  TEST_ASSERT_EQUAL_UINT8(kT, gPlayer.tick(0, out, 64));
  uint32_t offs = 0;
  for (uint32_t t = 1; t < 12; ++t) {
    const uint8_t n = gPlayer.tick(t, out, 4);
    for (uint8_t i = 0; i < n; ++i) offs += out[i].cls == 0;
  }
  TEST_ASSERT_EQUAL_UINT32(kT, offs);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_classification);
  RUN_TEST(test_writer_limits_continuous_per_tick);
  RUN_TEST(test_composer_limits_continuous_per_tick);
  RUN_TEST(test_player_spills_instead_of_losing);
  RUN_TEST(test_due_noteoffs_wait_instead_of_lost);
  return UNITY_END();
}
