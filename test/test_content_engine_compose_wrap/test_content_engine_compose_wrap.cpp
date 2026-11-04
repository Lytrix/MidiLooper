//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// fixed-memory-engine E3, slice 2: notes held across the loop end, composed over several cycles. Runs are written by the
// E2 writer (a held note's NoteOff lands in the closing run at `loop + position`); the emitted stream over 3 cycles from
// a cold start is compared with the independent reference model (OVL rules incl. Q-OVL-3, wrap-aware OVL-6, DEC-148).

#include <unity.h>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <vector>

#include "ContentEngine/Composer.h"
#include "ContentEngine/EngineCore.h"
#include "ContentEngine/SpanWindow.h"
#include "ContentEngine/Writer.h"
#include "../test_engine_window_bound/span_window_model.h"

using namespace ContentEngine;

using Pool = PagePool<1024, 64>;
using Runs = RunTable<64, Pool>;
using Res = RowReservations<Pool, Runs>;
using W = Writer<Pool, Runs>;
using TestComposer = Composer<Pool, Runs>;
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

constexpr uint32_t kLoop = 768 * 8;
constexpr uint32_t kWin = 1536;
constexpr uint32_t kCycles = 3;

struct NoteSpec {
  uint8_t channel, pitch;
  uint32_t on, off;  // off >= kLoop: held across the loop end, ends at off - kLoop in the next cycle
};

void setUp() {
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
  TEST_ASSERT_EQUAL(WriterResult::Ok, gW.begin(0, 1));
  const RunId run = gW.activeRun();
  bool wrapped = false;
  for (const auto& e : ev) {
    if (e.tick >= kLoop && !wrapped) {
      gW.wrap(kLoop);
      wrapped = true;
    }
    const WriterResult r = e.on ? gW.noteOn(e.tick, e.n.channel, e.n.pitch, 100)
                                : gW.noteOff(e.tick, e.n.channel, e.n.pitch, 0);
    TEST_ASSERT_EQUAL(WriterResult::Ok, r);
  }
  gW.stop(3 * kLoop);
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
          off = x.tick >= kLoop ? x.tick - kLoop : x.tick;
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
  gC.begin(src, count, kLoop, 1);
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

static void assertMatchesReference(const RunId* runs, uint8_t count) {
  const auto got = play(runs, count);
  const auto eff = Oracle::effective(oracleNotes(runs, count), kLoop);
  const auto want = SpanModel::expandedStream(SpanModel::timeline(eff, kLoop, kCycles), 0, kLoop * kCycles,
                                              Config::kMaxSoundingPerTrack);
  if (got.size() != want.size()) {
    char line[96];
    std::snprintf(line, sizeof line, "stream size: engine %u, reference %u", static_cast<unsigned>(got.size()),
                  static_cast<unsigned>(want.size()));
    TEST_FAIL_MESSAGE(line);
  }
  for (size_t i = 0; i < got.size(); ++i) {
    if (got[i].tick != want[i].tick || got[i].cls != want[i].cls || got[i].noteId != want[i].noteId / 64 ||
        got[i].channel != want[i].channel || got[i].pitch != want[i].pitch) {
      char line[160];
      std::snprintf(line, sizeof line, "row %u: engine (%u,%u,id %u,p%u) reference (%u,%u,id %u,p%u)",
                    static_cast<unsigned>(i), got[i].tick, got[i].cls, got[i].noteId, got[i].pitch, want[i].tick,
                    want[i].cls, want[i].noteId / 64, want[i].pitch);
      TEST_FAIL_MESSAGE(line);
    }
  }
}

// A note held across the loop end: not sounded in the first cycle's head (DEC-148), then ends at its wrap position.
void test_wrap_held_note_over_three_cycles() {
  const RunId r = recordPass({{1, 60, 5800, kLoop + 300}, {1, 62, 100, 200}});
  assertMatchesReference(&r, 1);
}

// A later pass starting inside the wrap-held note's head shortens it there (OVL-1 head shorten, hold > 0).
void test_later_pass_shortens_wrap_held_note_in_head() {
  RunId r[2];
  r[0] = recordPass({{1, 60, 5800, kLoop + 300}});
  r[1] = recordPass({{2, 60, 200, 260}});
  assertMatchesReference(r, 2);
}

// A later pass starting at tick 0 hides the wrap-held note entirely (Q-OVL-3, ratified).
void test_later_pass_at_tick_zero_hides_wrap_held_note() {
  RunId r[2];
  r[0] = recordPass({{1, 60, 5800, kLoop + 300}});
  r[1] = recordPass({{2, 60, 0, 50}});
  assertMatchesReference(r, 2);
}

// A later wrap-held note hides earlier notes that start inside its head, from the first cycle on (OVL-3).
void test_later_wrap_held_note_hides_earlier_notes_in_its_head() {
  RunId r[2];
  r[0] = recordPass({{1, 64, 100, 300}});
  r[1] = recordPass({{2, 64, 5900, kLoop + 500}});
  assertMatchesReference(r, 2);
}

// Minimum-length hide only while the earlier note still sounds: a later note 3 ticks after an earlier note that already
// ended leaves it alone (in-cycle and across the loop end); a wrap-held remnant shorter than 8 ticks is hidden.
void test_min_length_hide_requires_the_note_to_still_sound() {
  RunId r[2];
  r[0] = recordPass({{1, 66, 100, 102}, {1, 67, 6140, 6143}, {1, 68, 6140, kLoop + 100}});
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
        const uint32_t on = rnd() % kLoop;
        const uint32_t len = 1 + rnd() % (rnd() % 3 == 0 ? 3000 : 300);
        notes.push_back({ch, static_cast<uint8_t>(48 + pitch), on, on + len});
      }
      runs[p] = recordPass(notes);
    }
    assertMatchesReference(runs, static_cast<uint8_t>(passes));
  }
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_wrap_held_note_over_three_cycles);
  RUN_TEST(test_later_pass_shortens_wrap_held_note_in_head);
  RUN_TEST(test_later_pass_at_tick_zero_hides_wrap_held_note);
  RUN_TEST(test_later_wrap_held_note_hides_earlier_notes_in_its_head);
  RUN_TEST(test_min_length_hide_requires_the_note_to_still_sound);
  RUN_TEST(test_random_wrap_held_passes_match_reference);
  return UNITY_END();
}
