//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// fixed-memory-engine E3, slice 2: notes held across the loop end, composed over several cycles. Runs are written by the
// E2 writer (a held note's NoteOff lands in the closing run at `loop + position`); the emitted stream over 3 cycles from
// a cold start is compared with the independent reference model (OVL rules incl. Q-OVL-3, wrap-aware OVL-6, DEC-148).

#include <unity.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "ContentEngine/Composition/SpanWindowComposer.h"
#include "ContentEngine/EngineCore.h"
#include "ContentEngine/Composition/SpanWindow.h"
#include "ContentEngine/Content/CaptureWriter.h"
#include "../test_engine_window_bound/span_window_model.h"

using namespace ContentEngine;

using Pool = PagePool<1024, 64>;
using Runs = RunTable<64, Pool>;
using Res = RowReservations<Pool, Runs>;
using W = CaptureWriter<Pool, Runs>;
using TestComposer = SpanWindowComposer<Pool, Runs>;
using Player = WindowPlayer<Config::kMaxSoundingPerTrack>;

static Pool gPool;
static Runs gRuns;
static Res gRes;
static W gW;
static NoteIdSource gIds;
static TestComposer gC;
static SpanWindow<3072> gWin;
static TerminalScheduler<Config::kMaxSoundingPerTrack> gSched;
static Player gPlayer;

static uint32_t gLoop = 768 * 8;  // loop length of the current test (reset in setUp)
constexpr uint32_t kWin = 1536;
constexpr uint32_t kCycles = 3;

struct NoteSpec {
  uint8_t channel, pitch;
  uint32_t on, off;  // off >= gLoop: held across the loop end, ends at off - gLoop in the next cycle
};

static void setUpPlayerOnly();

void setUp() {
  gLoop = 768 * 8;
  gPool.init();
  gRuns.init();
  gRes.init(gPool, gRuns, 0);
  gIds.init();
  gW.init(gPool, gRuns, gRes, gIds);
  gC.init(gPool, gRuns);
  gSched.init();
  gPlayer.init(gSched);
}
void tearDown() {}

static void setUpPlayerOnly() {
  gC.init(gPool, gRuns);
  gSched.init();
  gPlayer.init(gSched);
}

/// One pass through the writer: events before the loop end, then the wrap (held notes go to the closing run), then the
/// NoteOffs past the loop end. Returns the pass's run.
static RunId recordPass(const std::vector<NoteSpec>& notes) {
  struct Ev {
    uint32_t tick;
    bool on;
    NoteSpec n;
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
  TEST_ASSERT_EQUAL(WriterResult::Ok, gW.begin(0, 1, gLoop));
  const RunId run = gW.activeRun();
  bool wrapped = false;
  for (const auto& e : ev) {
    if (e.tick >= gLoop && !wrapped) {
      gW.wrap(gLoop);
      wrapped = true;
    }
    const WriterResult r = e.on ? gW.noteOn(e.tick, e.n.channel, e.n.pitch, 100)
                                : gW.noteOff(e.tick, e.n.channel, e.n.pitch, 0);
    TEST_ASSERT_EQUAL(WriterResult::Ok, r);
  }
  gW.stop(3 * gLoop);
  return run;
}

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
          if (x.tick - e.tick >= gLoop)  // Q-LEN-1: held a full loop -> effective end one tick before the restart
            off = e.tick == 0 ? gLoop - 1 : e.tick - 1;
          else
            off = x.tick >= gLoop ? x.tick - gLoop : x.tick;
          break;
        }
      }
      out.push_back({e.noteId, order, static_cast<uint8_t>((e.status & 0x0F) + 1), e.data1, e.tick, off});
    }
  }
  return out;
}

static std::vector<Player::Emit> play(const RunId* runs, uint8_t count) {
  SourceRun src[16];
  for (uint8_t i = 0; i < count; ++i) src[i] = {runs[i], gRuns.run(runs[i]).rowCount};
  std::vector<Player::Emit> out;
  Player::Emit buf[64];
  gC.begin(src, count, gLoop, 1);
  for (uint32_t c = 0; c < kCycles; ++c) {
    while (gC.composeNext(gWin, kWin)) {
      TEST_ASSERT_TRUE(gWin.carryCount <= Config::kMaxSoundingPerTrack);
      TEST_ASSERT_TRUE(gWin.terminalCount <= Config::kMaxSoundingPerTrack);
      uint8_t n = gPlayer.beginWindow(gWin, buf, 64);
      out.insert(out.end(), buf, buf + n);
      for (uint32_t t = gWin.cycleBase + gWin.start; t < gWin.cycleBase + gWin.end; ++t) {
        n = gPlayer.tick(t, buf, 64);
        out.insert(out.end(), buf, buf + n);
      }
    }
    if (c + 1 < kCycles) TEST_ASSERT_TRUE(gC.nextCycle());
  }
  return out;
}

static void assertNoPhantomNoteOffs(const std::vector<Player::Emit>& s);

static void assertMatchesReference(const RunId* runs, uint8_t count) {
  const auto got = play(runs, count);
  assertNoPhantomNoteOffs(got);
  const auto eff = Oracle::effective(oracleNotes(runs, count), gLoop);
  const auto want = SpanModel::expandedStream(SpanModel::timeline(eff, gLoop, kCycles), 0, gLoop * kCycles,
                                              Config::kMaxSoundingPerTrack);
  for (size_t i = 0; i < got.size() && i < want.size(); ++i) {
    if (got[i].tick != want[i].tick || got[i].cls != want[i].cls || got[i].noteId != want[i].noteId / 64 ||
        got[i].channel != want[i].channel || got[i].pitch != want[i].pitch) {
      char line[160];
      std::snprintf(line, sizeof line, "row %u: engine (%u,%u,id %u,p%u) reference (%u,%u,id %u,p%u)",
                    static_cast<unsigned>(i), got[i].tick, got[i].cls, got[i].noteId, got[i].pitch, want[i].tick,
                    want[i].cls, want[i].noteId / 64, want[i].pitch);
      TEST_FAIL_MESSAGE(line);
    }
  }
  if (got.size() != want.size()) {
    char line[96];
    std::snprintf(line, sizeof line, "stream size: engine %u, reference %u", static_cast<unsigned>(got.size()),
                  static_cast<unsigned>(want.size()));
    TEST_FAIL_MESSAGE(line);
  }
}

// A note held across the loop end: not sounded in the first cycle's head (DEC-148), then ends at its wrap position.
void test_wrap_held_note_over_three_cycles() {
  const RunId r = recordPass({{1, 60, 5800, gLoop + 300}, {1, 62, 100, 200}});
  assertMatchesReference(&r, 1);
}

// A later pass starting inside the wrap-held note's head shortens it there (OVL-1 head shorten, hold > 0).
void test_later_pass_shortens_wrap_held_note_in_head() {
  RunId r[2];
  r[0] = recordPass({{1, 60, 5800, gLoop + 300}});
  r[1] = recordPass({{2, 60, 200, 260}});
  assertMatchesReference(r, 2);
}

// A later pass starting at tick 0 hides the wrap-held note entirely (Q-OVL-3, ratified).
void test_later_pass_at_tick_zero_hides_wrap_held_note() {
  RunId r[2];
  r[0] = recordPass({{1, 60, 5800, gLoop + 300}});
  r[1] = recordPass({{2, 60, 0, 50}});
  assertMatchesReference(r, 2);
}

// A later wrap-held note hides earlier notes that start inside its head, from the first cycle on (OVL-3).
void test_later_wrap_held_note_hides_earlier_notes_in_its_head() {
  RunId r[2];
  r[0] = recordPass({{1, 64, 100, 300}});
  r[1] = recordPass({{2, 64, 5900, gLoop + 500}});
  assertMatchesReference(r, 2);
}

// Minimum-length hide only while the earlier note still sounds: a later note 3 ticks after an earlier note that already
// ended leaves it alone (in-cycle and across the loop end); a wrap-held remnant shorter than 8 ticks is hidden.
void test_min_length_hide_requires_the_note_to_still_sound() {
  RunId r[2];
  r[0] = recordPass({{1, 66, 100, 102}, {1, 67, 6140, 6143}, {1, 68, 6140, gLoop + 100}});
  r[1] = recordPass({{2, 66, 103, 150}, {2, 67, 2, 40}, {2, 68, 2, 40}});
  assertMatchesReference(r, 2);
}

// Random stacked passes with wrap-held notes over three cycles, against the reference.
void test_random_wrap_held_passes_match_reference() {
  uint32_t seed = 0xA11CE;
  auto rnd = [&seed]() {
    seed ^= seed << 13;
    seed ^= seed >> 17;
    seed ^= seed << 5;
    return seed;
  };
  for (uint32_t trial = 0; trial < 40; ++trial) {
    setUp();
    const uint32_t passes = 1 + rnd() % 6;
    RunId runs[8];
    for (uint32_t p = 0; p < passes; ++p) {
      std::vector<NoteSpec> notes;
      bool used[16][12] = {};
      for (uint32_t k = 0, n = rnd() % 10; k < n; ++k) {
        const uint8_t ch = static_cast<uint8_t>(1 + rnd() % 3), pitch = static_cast<uint8_t>(rnd() % 12);
        if (used[ch][pitch]) continue;  // one note per lane per pass keeps the writer's pairing unambiguous
        used[ch][pitch] = true;
        const uint32_t on = rnd() % gLoop;
        const uint32_t len = 1 + rnd() % (rnd() % 3 == 0 ? (rnd() % 2 ? 3 * gLoop : 3000) : 300);  // incl. held-loop notes
        notes.push_back({ch, static_cast<uint8_t>(48 + pitch), on, on + len});
      }
      runs[p] = recordPass(notes);
    }
    assertMatchesReference(runs, static_cast<uint8_t>(passes));
  }
}

// ---- Independently specified expectations (hand-written from the clauses, not from the reference model) -----------------

struct X {
  uint32_t tick;
  uint8_t cls;  // 0 NoteOff, 1 NoteOn
  uint32_t id;
};

/// Every NoteOff closes an instance that was started and is open; one open instance per (noteId, run) at a time.
static void assertNoPhantomNoteOffs(const std::vector<Player::Emit>& s) {
  struct Open {
    uint32_t id;
    uint16_t runKey;
  };
  std::vector<Open> open;
  for (const auto& e : s) {
    if (e.cls == 1) {
      for (const auto& o : open) TEST_ASSERT_FALSE(o.id == e.noteId && o.runKey == e.runKey);  // no second iteration
      open.push_back({e.noteId, e.runKey});
    } else if (e.cls == 0) {
      bool found = false;
      for (size_t i = 0; i < open.size(); ++i)
        if (open[i].id == e.noteId && open[i].runKey == e.runKey) {
          open.erase(open.begin() + static_cast<long>(i));
          found = true;
          break;
        }
      TEST_ASSERT_TRUE_MESSAGE(found, "NoteOff for an instance that was not started");
    }
  }
}

static void assertStream(const RunId* runs, uint8_t count, uint32_t cycles, const std::vector<X>& want) {
  SourceRun src[16];
  for (uint8_t i = 0; i < count; ++i) src[i] = {runs[i], gRuns.run(runs[i]).rowCount};
  std::vector<Player::Emit> got;
  Player::Emit buf[64];
  gC.begin(src, count, gLoop, 1);
  for (uint32_t c = 0; c < cycles; ++c) {
    while (gC.composeNext(gWin, kWin)) {
      uint8_t n = gPlayer.beginWindow(gWin, buf, 64);
      got.insert(got.end(), buf, buf + n);
      for (uint32_t t = gWin.cycleBase + gWin.start; t < gWin.cycleBase + gWin.end; ++t) {
        n = gPlayer.tick(t, buf, 64);
        got.insert(got.end(), buf, buf + n);
      }
    }
    if (c + 1 < cycles) gC.nextCycle();
  }
  assertNoPhantomNoteOffs(got);
  TEST_ASSERT_EQUAL_UINT32(0, gSched.counters().duplicates);
  if (got.size() != want.size()) {
    char line[64];
    std::snprintf(line, sizeof line, "stream size %u, expected %u", static_cast<unsigned>(got.size()),
                  static_cast<unsigned>(want.size()));
    TEST_FAIL_MESSAGE(line);
  }
  for (size_t i = 0; i < want.size(); ++i) {
    if (got[i].tick != want[i].tick || got[i].cls != want[i].cls || got[i].noteId != want[i].id) {
      char line[120];
      std::snprintf(line, sizeof line, "row %u: got (%u,%u,id %u) expected (%u,%u,id %u)", static_cast<unsigned>(i),
                    got[i].tick, got[i].cls, got[i].noteId, want[i].tick, want[i].cls, want[i].id);
      TEST_FAIL_MESSAGE(line);
    }
  }
}

// OVL-6 boundaries in one cycle. Earlier notes A60/A61/A62 [1000, 2000) (ids 1-3); later notes at 1008/1009/1010
// (ids 4-6). Remnants (h - 1) - on = 7 / 8 / 9: A60 hidden, A61 ends at 1008, A62 ends at 1009.
void test_fixture_min_length_boundaries_7_8_9() {
  RunId r[2];
  r[0] = recordPass({{1, 60, 1000, 2000}, {1, 61, 1000, 2000}, {1, 62, 1000, 2000}});
  r[1] = recordPass({{2, 60, 1008, 1100}, {2, 61, 1009, 1100}, {2, 62, 1010, 1100}});
  assertStream(r, 2, 1,
               {{1000, 1, 2}, {1000, 1, 3}, {1008, 0, 2}, {1008, 1, 4}, {1009, 0, 3}, {1009, 1, 5}, {1010, 1, 6},
                {1100, 0, 4}, {1100, 0, 5}, {1100, 0, 6}});
}

// OVL-6 across the loop end (owner decision: the whole surviving duration, unwrapped). Wrap-held A70/A71/A72 start at
// L-5 (ids 1-3); later notes at 3/4/5 in the next cycle (ids 4-6). Remnants (L + h - 1) - (L - 5) = 7 / 8 / 9.
// Cycle 0: no A has started yet at 3..5 (cold start). Cycle 1: A71 ends at L+3, A72 at L+4; A70 never sounds.
void test_fixture_min_length_across_loop_end() {
  RunId r[2];
  r[0] = recordPass({{1, 70, gLoop - 5, gLoop + 200}, {1, 71, gLoop - 5, gLoop + 200}, {1, 72, gLoop - 5, gLoop + 200}});
  r[1] = recordPass({{2, 70, 3, 50}, {2, 71, 4, 50}, {2, 72, 5, 50}});
  const uint32_t L = gLoop;
  assertStream(r, 2, 2,
               {{3, 1, 4}, {4, 1, 5}, {5, 1, 6}, {50, 0, 4}, {50, 0, 5}, {50, 0, 6}, {L - 5, 1, 2}, {L - 5, 1, 3},
                {L + 3, 0, 2}, {L + 3, 1, 4}, {L + 4, 0, 3}, {L + 4, 1, 5}, {L + 5, 1, 6}, {L + 50, 0, 4},
                {L + 50, 0, 5}, {L + 50, 0, 6}, {2 * L - 5, 1, 2}, {2 * L - 5, 1, 3}});
}

// End-abutment leaves the earlier note unchanged, in a cycle and across the loop end: NoteOff before NoteOn at the tick.
// Pass 0: A60 [100, 200) id 1, C62 [L-100, L+50) id 2. Pass 1: D62 [50, 120) id 3, B60 [200, 300) id 4.
void test_fixture_end_abutment_in_cycle_and_across_loop_end() {
  RunId r[2];
  r[0] = recordPass({{1, 60, 100, 200}, {1, 62, gLoop - 100, gLoop + 50}});
  r[1] = recordPass({{2, 62, 50, 120}, {2, 60, 200, 300}});
  const uint32_t L = gLoop;
  assertStream(r, 2, 2,
               {{50, 1, 3}, {100, 1, 1}, {120, 0, 3}, {200, 0, 1}, {200, 1, 4}, {300, 0, 4}, {L - 100, 1, 2},
                {L + 50, 0, 2}, {L + 50, 1, 3}, {L + 100, 1, 1}, {L + 120, 0, 3}, {L + 200, 0, 1}, {L + 200, 1, 4},
                {L + 300, 0, 4}, {2 * L - 100, 1, 2}});
}

// Q-OVL-3: a later note at tick 0 hides the wrap-held earlier note in every cycle (no remnant before the loop end).
void test_fixture_tick_zero_hides_wrap_held_note() {
  RunId r[2];
  r[0] = recordPass({{1, 60, gLoop - 100, gLoop + 50}});
  r[1] = recordPass({{2, 60, 0, 30}});
  const uint32_t L = gLoop;
  assertStream(r, 2, 3, {{0, 1, 2}, {30, 0, 2}, {L, 1, 2}, {L + 30, 0, 2}, {2 * L, 1, 2}, {2 * L + 30, 0, 2}});
}

// Two iterations of the same (noteId, run): a note one tick shorter than the loop ends at 99 and restarts at 100 each
// cycle; absolute ticks keep them apart (no duplicate instance, NoteOff before the next NoteOn).
void test_fixture_successive_iterations_of_one_note() {
  const RunId r = recordPass({{1, 64, 100, gLoop + 99}});
  const uint32_t L = gLoop;
  assertStream(&r, 1, 3, {{100, 1, 1}, {L + 99, 0, 1}, {L + 100, 1, 1}, {2 * L + 99, 0, 1}, {2 * L + 100, 1, 1}});
}

// Look-ahead work on dense stacked chords: 8 passes each start a 128-note chord (16 channels x 8 pitches) one tick apart.
// The worst NoteOn reads at most the rows of all passes within its next 9 ticks plus nothing from wrap sections (no
// tick-0 starts): measured against that bound, not assumed.
void test_lookahead_work_is_bounded_on_dense_chords() {
  RunId r[8];
  for (uint8_t p = 0; p < 8; ++p) {
    std::vector<NoteSpec> chord;
    for (uint8_t ch = 1; ch <= 16; ++ch)
      for (uint8_t k = 0; k < 8; ++k) chord.push_back({ch, static_cast<uint8_t>(40 + k), 1000u + p, 1004u + p});
    r[p] = recordPass(chord);
  }
  SourceRun src[8];
  for (uint8_t i = 0; i < 8; ++i) src[i] = {r[i], gRuns.run(r[i]).rowCount};
  gC.begin(src, 8, gLoop, 1);
  uint32_t rowsInNineTicks = 0;  // every row of every pass lies within 1000..1011: the whole dense block
  for (uint8_t i = 0; i < 8; ++i) rowsInNineTicks += gRuns.run(r[i]).rowCount;
  while (gC.composeNext(gWin, kWin)) {}
  char line[120];
  std::snprintf(line, sizeof line, "dense chords: max look-ahead rows per NoteOn %u (bound: rows within 9 ticks = %u)",
                gC.stats().maxLookaheadRowsPerNoteOn, rowsInNineTicks);
  TEST_MESSAGE(line);
  TEST_ASSERT_TRUE(gC.stats().maxLookaheadRowsPerNoteOn <= rowsInNineTicks);
}

// ---- Q-LEN-1 (owner decision 2026-11-04: re-strike once per loop), hand-specified ------------------------------------

static RunId recordLong(uint32_t on, uint32_t off, uint8_t pitch = 64) {
  TEST_ASSERT_EQUAL(WriterResult::Ok, gW.begin(0, 1, gLoop));
  const RunId run = gW.activeRun();
  TEST_ASSERT_EQUAL(WriterResult::Ok, gW.noteOn(on, 1, pitch, 100));
  for (uint32_t w = gLoop; w <= off; w += gLoop) gW.wrap(w);
  TEST_ASSERT_EQUAL(WriterResult::Ok, gW.noteOff(off, 1, pitch, 0));
  gW.stop(off + gLoop);
  return run;
}

// Durations L-1 (ordinary wrap-held), L, L+1 and > 2L from S = 100 all replay as: NoteOn every loop at S, NoteOff one tick
// before the next NoteOn. The original late NoteOff is never sent against a newer iteration.
void test_qlen1_durations_L_minus_1_L_L_plus_1_and_beyond_2L() {
  const uint32_t L = gLoop;
  const std::vector<X> want = {{100, 1, 1}, {L + 99, 0, 1}, {L + 100, 1, 1}, {2 * L + 99, 0, 1}, {2 * L + 100, 1, 1}};
  for (const uint32_t off : {L + 99, L + 100, L + 101, 2 * L + 500}) {
    setUp();
    const RunId r = recordLong(100, off);
    TEST_ASSERT_EQUAL(off - 100 >= L ? kOriginHeldLoop : 0, gRuns.rowAt(r, 1, gPool).origin & kOriginHeldLoop);
    assertStream(&r, 1, 3, want);
  }
}

// A held-loop note beginning near the loop end still sounds into the next cycle (no clipping at tick 0).
void test_qlen1_note_beginning_near_loop_end() {
  const uint32_t L = gLoop;
  const RunId r = recordLong(L - 10, 2 * L + 20);
  assertStream(&r, 1, 3, {{L - 10, 1, 1}, {2 * L - 11, 0, 1}, {2 * L - 10, 1, 1}, {3 * L - 11, 0, 1}, {3 * L - 10, 1, 1}});
}

// A held-loop note starting at tick 0 ends at the loop's last tick each cycle and restarts at the next cycle's tick 0.
void test_qlen1_note_starting_at_tick_zero() {
  const uint32_t L = gLoop;
  const RunId r = recordLong(0, L + 5);
  assertStream(&r, 1, 3, {{0, 1, 1}, {L - 1, 0, 1}, {L, 1, 1}, {2 * L - 1, 0, 1}, {2 * L, 1, 1}, {3 * L - 1, 0, 1}});
}

// Overlap against the effective repeating spans. Pass 0: A60 [100, L+500) held a loop (id 1), D62 [500, 600) (id 2).
// Pass 1: B60 [300, 400) (id 3) shortens A to 299 every cycle; C62 [1000, L+2000) held a loop (id 4) sounds continuously
// (each iteration until 999 of the next cycle), so D starts inside it and is hidden in every cycle (cold start too).
void test_qlen1_later_pass_overlap_with_repeating_spans() {
  RunId r[2];
  r[0] = recordPass({{1, 60, 100, gLoop + 500}, {1, 62, 500, 600}});
  r[1] = recordPass({{2, 60, 300, 400}, {2, 62, 1000, gLoop + 2000}});
  std::vector<X> want;
  for (uint32_t c = 0; c < 3; ++c) {
    const uint32_t b = c * gLoop;
    want.push_back({b + 100, 1, 1});
    want.push_back({b + 299, 0, 1});
    want.push_back({b + 300, 1, 3});
    want.push_back({b + 400, 0, 3});
    if (c > 0) want.push_back({b + 999, 0, 4});
    want.push_back({b + 1000, 1, 4});
  }
  assertStream(r, 2, 3, want);
}

// A live hold across several wraps sounds through the live path until the real NoteOff: while the pitch is occupied,
// the replayed iterations of the recorded note are suppressed (no playback NoteOn, no orphan NoteOff). After release the
// sealed take replays with the re-strike rule.
void test_qlen1_live_hold_across_wraps_then_replay() {
  TEST_ASSERT_EQUAL(WriterResult::Ok, gW.begin(0, 1, gLoop));
  const RunId run = gW.activeRun();
  TEST_ASSERT_EQUAL(WriterResult::Ok, gW.noteOn(100, 1, 70, 100));
  TerminalScheduler<Config::kMaxSoundingPerTrack>::Out offs[16];
  gSched.occupy(70, 100, offs, 16);  // the live key is down
  std::vector<Player::Emit> got;
  Player::Emit buf[64];
  const SourceRun prefix[] = {{run, 1}};  // only the NoteOn is recorded so far
  gC.begin(prefix, 1, gLoop, 1);
  for (uint32_t c = 0; c < 3; ++c) {
    gW.wrap((c + 1) * gLoop);
    while (gC.composeNext(gWin, kWin)) {
      uint8_t n = gPlayer.beginWindow(gWin, buf, 64);
      got.insert(got.end(), buf, buf + n);
      for (uint32_t t = gWin.cycleBase + gWin.start; t < gWin.cycleBase + gWin.end; ++t) {
        n = gPlayer.tick(t, buf, 64);
        got.insert(got.end(), buf, buf + n);
      }
    }
    gC.nextCycle();
  }
  TEST_ASSERT_EQUAL_UINT32(0, got.size());  // nothing replayed under the live hold
  TEST_ASSERT_TRUE(gSched.counters().suppressedOccupied >= 3);
  TEST_ASSERT_EQUAL(WriterResult::Ok, gW.noteOff(3 * gLoop + 200, 1, 70, 0));
  gSched.release(70);
  gW.stop(4 * gLoop);
  TEST_ASSERT_TRUE((gRuns.rowAt(run, 1, gPool).origin & kOriginHeldLoop) != 0);
  setUpPlayerOnly();
  const uint32_t L = gLoop;
  assertStream(&run, 1, 2, {{100, 1, 1}, {L + 99, 0, 1}, {L + 100, 1, 1}});
}

// Ordering regression: a TERMINAL for pos - 1 is written after an entry a lower pass already wrote at pos. Pass 0: A60
// [100, 3000) id 1 (carried into window 1), C61 [2000, 2100) id 2. Pass 1: B60 [2000, 2200) id 3 shortens A to 1999.
// A's NoteOff must go out at 1999, not a tick late.
void test_fixture_shorten_terminal_after_lower_pass_entry_at_same_tick() {
  RunId r[2];
  r[0] = recordPass({{1, 60, 100, 3000}, {1, 61, 2000, 2100}});
  r[1] = recordPass({{2, 60, 2000, 2200}});
  assertStream(r, 2, 1, {{100, 1, 1}, {1999, 0, 1}, {2000, 1, 2}, {2000, 1, 3}, {2100, 0, 2}, {2200, 0, 3}});
}

// ---- Q-OVL-4 (owner decision 2026-11-04, ratified), hand-specified, L = 768 -------------------------------------------
// Earlier A (pass 0) at 700, recorded end 900 (wraps to 132). Overlap is resolved against effective instances in every
// cycle: an A NoteOn inside B's span is hidden; an A that starts outside it is shortened by B's next start.

static SpanWindow<3072> gScratch;

/// Plays `cyclesA` cycles of source A from a cold start, then switches to source B at the next cycle boundary. B's
/// composer is warmed by one silent cycle first (revision adoption: its carry reflects its steady state).
static std::vector<Player::Emit> playThenAdopt(const SourceRun* a, uint8_t aCount, uint32_t cyclesA,
                                               const SourceRun* b, uint8_t bCount, uint32_t cyclesB) {
  std::vector<Player::Emit> got;
  Player::Emit buf[64];
  gC.begin(a, aCount, gLoop, 1);
  for (uint32_t c = 0; c < cyclesA; ++c) {
    while (gC.composeNext(gWin, kWin)) {
      uint8_t n = gPlayer.beginWindow(gWin, buf, 64);
      got.insert(got.end(), buf, buf + n);
      for (uint32_t t = gWin.cycleBase + gWin.start; t < gWin.cycleBase + gWin.end; ++t) {
        n = gPlayer.tick(t, buf, 64);
        got.insert(got.end(), buf, buf + n);
      }
    }
    gC.nextCycle();
  }
  static TestComposer cb;
  cb.init(gPool, gRuns);
  cb.begin(b, bCount, gLoop, 2);
  for (uint32_t c = 0; c < cyclesA; ++c) {  // warm-up to the adoption cycle, silently
    while (cb.composeNext(gScratch, kWin)) {}
    cb.nextCycle();
  }
  for (uint32_t c = 0; c < cyclesB; ++c) {
    while (cb.composeNext(gWin, kWin)) {
      uint8_t n = gPlayer.beginWindow(gWin, buf, 64);
      got.insert(got.end(), buf, buf + n);
      for (uint32_t t = gWin.cycleBase + gWin.start; t < gWin.cycleBase + gWin.end; ++t) {
        n = gPlayer.tick(t, buf, 64);
        got.insert(got.end(), buf, buf + n);
      }
    }
    cb.nextCycle();
  }
  return got;
}

static void expectEmits(const std::vector<Player::Emit>& got, const std::vector<X>& want) {
  assertNoPhantomNoteOffs(got);
  TEST_ASSERT_EQUAL_UINT32(want.size(), got.size());
  for (size_t i = 0; i < want.size(); ++i) {
    TEST_ASSERT_EQUAL_UINT32(want[i].tick, got[i].tick);
    TEST_ASSERT_EQUAL_UINT8(want[i].cls, got[i].cls);
    TEST_ASSERT_EQUAL_UINT32(want[i].id, got[i].noteId);
  }
}

// Case 1: B 50-750 covers A's start at 700: A is hidden in every repetition, cold start included; nothing is carried
// for A, no NoteOff for A.
void test_qovl4_case1_later_note_covers_earlier_start() {
  gLoop = 768;
  RunId r[2];
  r[0] = recordPass({{1, 60, 700, 900}});
  r[1] = recordPass({{2, 60, 50, 750}});
  assertStream(r, 2, 3, {{50, 1, 2}, {750, 0, 2}, {818, 1, 2}, {1518, 0, 2}, {1586, 1, 2}, {2286, 0, 2}});
}

// Case 2: B 50-600 does not cover 700: A starts at 700 and is shortened to 817 by the next B at 818 (head shorten of the
// wrapped instance), every cycle; in the cold first cycle no prior A instance exists to shorten.
void test_qovl4_case2_earlier_note_shortened_by_next_later_start() {
  gLoop = 768;
  RunId r[2];
  r[0] = recordPass({{1, 60, 700, 900}});
  r[1] = recordPass({{2, 60, 50, 600}});
  assertStream(r, 2, 3,
               {{50, 1, 2}, {600, 0, 2}, {700, 1, 1}, {817, 0, 1}, {818, 1, 2}, {1368, 0, 2}, {1468, 1, 1},
                {1585, 0, 1}, {1586, 1, 2}, {2136, 0, 2}, {2236, 1, 1}});
}

// Adoption, case 2: A was transmitted at 700 under revision {A}; the adopted revision {A, B} carries the same instance
// and shortens it to 817 (not closed at the switch, not hidden retroactively).
void test_qovl4_case2_adoption_keeps_and_shortens_transmitted_note() {
  gLoop = 768;
  RunId r[2];
  r[0] = recordPass({{1, 60, 700, 900}});
  r[1] = recordPass({{2, 60, 50, 600}});
  const SourceRun m1[] = {{r[0], gRuns.run(r[0]).rowCount}};
  const SourceRun m2[] = {{r[0], gRuns.run(r[0]).rowCount}, {r[1], gRuns.run(r[1]).rowCount}};
  expectEmits(playThenAdopt(m1, 1, 1, m2, 2, 1), {{700, 1, 1}, {817, 0, 1}, {818, 1, 2}, {1368, 0, 2}, {1468, 1, 1}});
}

// Adoption, case 1: in the adopted revision A is hidden, so the already-transmitted A is closed at the switch (a real
// NoteOff for a sounding note) and never restarts.
void test_qovl4_case1_adoption_closes_transmitted_note_at_switch() {
  gLoop = 768;
  RunId r[2];
  r[0] = recordPass({{1, 60, 700, 900}});
  r[1] = recordPass({{2, 60, 50, 750}});
  const SourceRun m1[] = {{r[0], gRuns.run(r[0]).rowCount}};
  const SourceRun m2[] = {{r[0], gRuns.run(r[0]).rowCount}, {r[1], gRuns.run(r[1]).rowCount}};
  expectEmits(playThenAdopt(m1, 1, 1, m2, 2, 1), {{700, 1, 1}, {768, 0, 1}, {818, 1, 2}, {1518, 0, 2}});
}

// Unwrapped endpoints: A 600 -> 900 (wraps to 132) is shortened by B1 at 700 (end 699, same cycle) and would be by B2 at
// 100 of the next cycle (end 99, numerically smaller but later in absolute time). The earlier absolute end, 699, wins.
void test_fixture_unwrapped_endpoint_smaller_phase_is_later() {
  gLoop = 768;
  RunId r[2];
  r[0] = recordPass({{1, 60, 600, 900}});
  r[1] = recordPass({{2, 60, 100, 150}, {2, 60, 700, 750}});
  assertStream(r, 2, 2,
               {{100, 1, 2}, {150, 0, 2}, {600, 1, 1}, {699, 0, 1}, {700, 1, 3}, {750, 0, 3}, {868, 1, 2}, {918, 0, 2},
                {1368, 1, 1}, {1467, 0, 1}, {1468, 1, 3}, {1518, 0, 3}});
}

// Look-ahead exactness: a carried note's natural NoteOff at 2000 is not sent at 1999 although the player looks one tick
// ahead; at 2000 it goes out before the NoteOn at 2000 (DEC-244).
void test_fixture_lookahead_never_sends_a_terminal_early() {
  RunId r[2];
  r[0] = recordPass({{1, 60, 100, 2000}, {1, 62, 1999, 2050}});
  r[1] = recordPass({{2, 61, 2000, 2100}});
  assertStream(r, 2, 1, {{100, 1, 1}, {1999, 1, 2}, {2000, 0, 1}, {2000, 1, 3}, {2050, 0, 2}, {2100, 0, 3}});
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_wrap_held_note_over_three_cycles);
  RUN_TEST(test_later_pass_shortens_wrap_held_note_in_head);
  RUN_TEST(test_later_pass_at_tick_zero_hides_wrap_held_note);
  RUN_TEST(test_later_wrap_held_note_hides_earlier_notes_in_its_head);
  RUN_TEST(test_min_length_hide_requires_the_note_to_still_sound);
  RUN_TEST(test_random_wrap_held_passes_match_reference);
  RUN_TEST(test_fixture_min_length_boundaries_7_8_9);
  RUN_TEST(test_fixture_min_length_across_loop_end);
  RUN_TEST(test_fixture_end_abutment_in_cycle_and_across_loop_end);
  RUN_TEST(test_fixture_tick_zero_hides_wrap_held_note);
  RUN_TEST(test_fixture_successive_iterations_of_one_note);
  RUN_TEST(test_lookahead_work_is_bounded_on_dense_chords);
  RUN_TEST(test_qlen1_durations_L_minus_1_L_L_plus_1_and_beyond_2L);
  RUN_TEST(test_qlen1_note_beginning_near_loop_end);
  RUN_TEST(test_qlen1_note_starting_at_tick_zero);
  RUN_TEST(test_qlen1_later_pass_overlap_with_repeating_spans);
  RUN_TEST(test_qlen1_live_hold_across_wraps_then_replay);
  RUN_TEST(test_fixture_shorten_terminal_after_lower_pass_entry_at_same_tick);
  RUN_TEST(test_qovl4_case1_later_note_covers_earlier_start);
  RUN_TEST(test_qovl4_case2_earlier_note_shortened_by_next_later_start);
  RUN_TEST(test_qovl4_case2_adoption_keeps_and_shortens_transmitted_note);
  RUN_TEST(test_qovl4_case1_adoption_closes_transmitted_note_at_switch);
  RUN_TEST(test_fixture_unwrapped_endpoint_smaller_phase_is_later);
  RUN_TEST(test_fixture_lookahead_never_sends_a_terminal_early);
  return UNITY_END();
}
