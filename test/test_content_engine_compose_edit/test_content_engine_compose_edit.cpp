//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// fixed-memory-engine E3: edit hides. An Edit run (NOTE_EDIT commit) holds HideNote rows (status kHideNoteStatus, the
// target's noteId) and replacement notes that keep the target's noteId (DEC-245 lineage). A row of an earlier run is
// hidden when a later edit run hides its noteId. Edit runs express NOTE_EDIT's own overlap results explicitly, so they do
// not act as derived occluders (EDIT-1, draft); later overdub passes occlude edited notes like any earlier content; an
// edit-hidden note still counts as a source occluder (OVL-8). Expectations are hand-written.

#include <unity.h>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <new>
#include <vector>

#include "ContentEngine/Composer.h"
#include "ContentEngine/EngineCore.h"
#include "ContentEngine/SpanWindow.h"

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

using Pool = PagePool<256, 64>;
using Runs = RunTable<32, Pool>;
using TestComposer = Composer<Pool, Runs>;
using Player = WindowPlayer<Config::kMaxSoundingPerTrack>;

static Pool gPool;
static Runs gRuns;
static TestComposer gC;
static SpanWindow<3072> gWin;
static TerminalScheduler<Config::kMaxSoundingPerTrack> gSched;
static Player gPlayer;

constexpr uint32_t kLoop = 768 * 8;
constexpr uint32_t kWin = 1536;

struct N {
  uint32_t id;
  uint8_t channel, pitch;
  uint32_t on, off;
};
struct X {
  uint32_t tick;
  uint8_t cls;
  uint32_t id;
};

void setUp() {
  gPool.init();
  gRuns.init();
  gC.init(gPool, gRuns);
  gSched.init();
  gPlayer.init(gSched);
}
void tearDown() {}

static PassEvent row(uint32_t tick, uint32_t id, uint8_t status, uint8_t d1, uint8_t d2) {
  PassEvent e;
  e.tick = tick;
  e.noteId = id;
  e.seq = 0;
  e.status = status;
  e.data1 = d1;
  e.data2 = d2;
  e.origin = 0;
  return e;
}

/// A sealed run of `kind`: HideNote rows first (tick 0), then the notes in tick order (NoteOff before NoteOn at a tick).
static RunId makeRun(uint8_t kind, const std::vector<uint32_t>& hides, const std::vector<N>& notes) {
  const RunId r = gRuns.allocate(0, kind);
  for (uint32_t id : hides) TEST_ASSERT_EQUAL(AppendResult::Ok, gRuns.append(r, row(0, id, kHideNoteStatus, 0, 0), gPool));
  struct Ev {
    uint32_t tick;
    bool on;
    N n;
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
  for (const auto& e : ev)
    TEST_ASSERT_EQUAL(AppendResult::Ok,
                      gRuns.append(r, row(e.tick, e.n.id, static_cast<uint8_t>((e.on ? 0x90 : 0x80) | (e.n.channel - 1)),
                                          e.n.pitch, e.on ? 100 : 0),
                                   gPool));
  gRuns.seal(r);
  return r;
}

static void expectStream(const RunId* runs, uint8_t count, uint32_t cycles, const std::vector<X>& want) {
  SourceRun src[16];
  for (uint8_t i = 0; i < count; ++i) src[i] = {runs[i], gRuns.run(runs[i]).rowCount};
  std::vector<Player::Emit> got;
  got.reserve(1024);
  Player::Emit buf[64];
  gTrap = true;
  const unsigned long heapBefore = gHeapAllocations;
  TEST_ASSERT_TRUE(gC.begin(src, count, kLoop, 1));
  for (uint32_t c = 0; c < cycles; ++c) {
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
  gTrap = false;
  TEST_ASSERT_EQUAL_UINT32(0, gHeapAllocations - heapBefore);
  TEST_ASSERT_EQUAL_UINT32(want.size(), got.size());
  for (size_t i = 0; i < want.size(); ++i) {
    TEST_ASSERT_EQUAL_UINT32(want[i].tick, got[i].tick);
    TEST_ASSERT_EQUAL_UINT8(want[i].cls, got[i].cls);
    TEST_ASSERT_EQUAL_UINT32(want[i].id, got[i].noteId);
  }
}

// A later edit run hiding a note removes it (NoteOn and NoteOff), in every cycle.
void test_edit_hide_removes_note() {
  RunId r[2];
  r[0] = makeRun(RunKind::kOverdub, {}, {{1, 1, 60, 100, 200}, {2, 1, 62, 300, 400}});
  r[1] = makeRun(RunKind::kEdit, {1}, {});
  expectStream(r, 2, 2, {{300, 1, 2}, {400, 0, 2}, {kLoop + 300, 1, 2}, {kLoop + 400, 0, 2}});
}

// A replacement keeps the target's noteId and plays from the edit run.
void test_edit_replacement_plays_with_same_note_id() {
  RunId r[2];
  r[0] = makeRun(RunKind::kRecord, {}, {{1, 1, 60, 100, 200}});
  r[1] = makeRun(RunKind::kEdit, {1}, {{1, 1, 60, 300, 400}});
  expectStream(r, 2, 1, {{300, 1, 1}, {400, 0, 1}});
}

// Lineage: a second edit hides the first replacement and adds its own.
void test_edit_lineage_second_edit_replaces_first() {
  RunId r[3];
  r[0] = makeRun(RunKind::kRecord, {}, {{1, 1, 60, 100, 200}});
  r[1] = makeRun(RunKind::kEdit, {1}, {{1, 1, 60, 300, 400}});
  r[2] = makeRun(RunKind::kEdit, {1}, {{1, 1, 60, 500, 600}});
  expectStream(r, 3, 1, {{500, 1, 1}, {600, 0, 1}});
}

// EDIT-1: an edit replacement overlapping an earlier note does not shorten or hide it by derived occlusion (NOTE_EDIT
// writes its own overlap results as explicit hides/replacements).
void test_edit_run_is_not_a_derived_occluder() {
  RunId r[2];
  r[0] = makeRun(RunKind::kRecord, {}, {{1, 1, 62, 100, 200}, {2, 1, 60, 250, 450}});
  r[1] = makeRun(RunKind::kEdit, {1}, {{1, 1, 60, 300, 400}});
  expectStream(r, 2, 1, {{250, 1, 2}, {300, 1, 1}, {400, 0, 1}, {450, 0, 2}});
}

// A later overdub pass occludes an edited replacement like any earlier content (OVL-1).
void test_later_overdub_shortens_edit_replacement() {
  RunId r[3];
  r[0] = makeRun(RunKind::kRecord, {}, {{1, 1, 60, 100, 200}});
  r[1] = makeRun(RunKind::kEdit, {1}, {{1, 1, 60, 300, 400}});
  r[2] = makeRun(RunKind::kOverdub, {}, {{3, 2, 60, 350, 380}});
  expectStream(r, 3, 1, {{300, 1, 1}, {349, 0, 1}, {350, 1, 3}, {380, 0, 3}});
}

// OVL-8: a later-pass note deleted by NOTE_EDIT still leaves its sealed effect on earlier passes (X stays shortened).
void test_edit_hidden_later_note_still_occludes_earlier_pass() {
  RunId r[3];
  r[0] = makeRun(RunKind::kRecord, {}, {{1, 1, 62, 100, 500}});
  r[1] = makeRun(RunKind::kOverdub, {}, {{2, 2, 62, 300, 400}});
  r[2] = makeRun(RunKind::kEdit, {2}, {});
  expectStream(r, 3, 1, {{100, 1, 1}, {299, 0, 1}});
}

// Undo of the edit: the manifest without the edit run plays the original.
void test_manifest_without_edit_run_plays_original() {
  RunId r[2];
  r[0] = makeRun(RunKind::kRecord, {}, {{1, 1, 60, 100, 200}});
  r[1] = makeRun(RunKind::kEdit, {1}, {{1, 1, 60, 300, 400}});
  expectStream(r, 1, 1, {{100, 1, 1}, {200, 0, 1}});
}

// The hide table is fixed: a source with more hides than it holds is refused by name at begin.
void test_hide_capacity_refused_at_begin() {
  static Composer<Pool, Runs, 64, Config::kMaxSoundingPerTrack, /*MaxHides*/ 4> small;
  small.init(gPool, gRuns);
  const RunId r = makeRun(RunKind::kEdit, {1, 2, 3, 4, 5}, {});
  const SourceRun src[] = {{r, gRuns.run(r).rowCount}};
  TEST_ASSERT_FALSE(small.begin(src, 1, kLoop, 1));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_edit_hide_removes_note);
  RUN_TEST(test_edit_replacement_plays_with_same_note_id);
  RUN_TEST(test_edit_lineage_second_edit_replaces_first);
  RUN_TEST(test_edit_run_is_not_a_derived_occluder);
  RUN_TEST(test_later_overdub_shortens_edit_replacement);
  RUN_TEST(test_edit_hidden_later_note_still_occludes_earlier_pass);
  RUN_TEST(test_manifest_without_edit_run_plays_original);
  RUN_TEST(test_hide_capacity_refused_at_begin);
  return UNITY_END();
}
