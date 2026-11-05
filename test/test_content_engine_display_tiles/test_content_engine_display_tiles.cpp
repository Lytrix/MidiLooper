//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// fixed-memory-engine E7.1: piano-roll tiles (plan revision 3 § 9.3, invariant I6, gate H6). The tile sweep composes a
// slot's manifest (steady cycle, every effective note — OUT-POLY limits output, never the piano roll) into span tiles
// (detail slots) and density tiles (every slot). Checked: exact segments per bar (bar crossings, a note held across
// the loop end, an OVL-1 shortened note, 20 simultaneous notes), the dense state beyond 256 segments, and the draw
// rule — a bar is drawn from its span tile, else its density tile, and never goes blank on a starved sweep, an Undo, a
// focus switch, a superseded sweep or exhausted spares.

#include <unity.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <vector>

#include "ContentEngine/Composer.h"
#include "ContentEngine/DisplayTiles.h"
#include "ContentEngine/EngineCore.h"

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
using Runs = RunTable<16, Pool>;
using DisplayComposer = Composer<Pool, Runs, 64, 48>;  // every effective note selected (no OUT-POLY on the display)
using Window = SpanWindow<3072>;
using Manifests = ManifestTable<8, 8>;
using Store = TileStore</*slots*/ 4, /*bars*/ 8, /*detail*/ 2, /*spares*/ 4>;
using Sweep = TileSweep<DisplayComposer, Window, Manifests, Runs, Store>;

constexpr uint32_t kBar = 768;
constexpr uint32_t kLoop = 8 * kBar;
constexpr uint32_t kWin = 2 * kBar;

static Pool gPool;
static Runs gRuns;
static Manifests gManifests;
static DisplayComposer gComp;
static Window gWin;
static Store gStore;
static Sweep gSweep;

struct N {
  uint32_t id;
  uint8_t pitch;
  uint32_t on, off;
};
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
static ManifestId manifest(const std::vector<RunId>& runs, uint32_t revision, uint8_t slot = 0) {
  return gManifests.create(slot, revision, kLoop, runs.data(), static_cast<uint8_t>(runs.size()), gRuns);
}
static uint32_t identityOf(ManifestId m) { return gManifests.manifest(m).identity; }
static void sweep(uint8_t slot, ManifestId m) {
  TEST_ASSERT_EQUAL(Sweep::Status::Running, gSweep.start(gComp, gStore, gManifests, gRuns, slot, m, kWin, kBar));
  while (gSweep.step(gWin, 128) == Sweep::Status::Running) {
  }
  TEST_ASSERT_EQUAL(Sweep::Status::Done, gSweep.status());
  TEST_ASSERT_FALSE(gSweep.pinned());
}
struct Seg {
  uint16_t start, length;
  uint8_t pitch;
  bool left, right;
};
static std::vector<Seg> segments(uint8_t slot, uint8_t bar) {
  const Store::Draw d = gStore.drawSource(slot, bar);
  TEST_ASSERT_EQUAL(Store::Kind::Span, d.kind);
  std::vector<Seg> out;
  for (uint16_t i = 0; i < d.header->count; ++i) {
    const NoteSegment& s = d.span->segments[i];
    out.push_back({s.start, s.length, s.pitch, (s.flags & 0x10) != 0, (s.flags & 0x20) != 0});
  }
  std::sort(out.begin(), out.end(), [](const Seg& a, const Seg& b) { return a.pitch < b.pitch || (a.pitch == b.pitch && a.start < b.start); });
  return out;
}
static bool rasterBit(uint8_t slot, uint8_t bar, uint8_t column, uint8_t pitch) {
  const Store::Draw d = gStore.drawSource(slot, bar);
  return (d.span->raster[column][pitch / 8] >> (pitch % 8)) & 1u;
}
static uint8_t density(uint8_t slot, uint8_t bar, uint8_t pitch) {
  const Store::Draw d = gStore.drawSource(slot, bar);
  TEST_ASSERT_EQUAL(Store::Kind::Density, d.kind);
  return (d.density->counts[pitch / 2] >> ((pitch & 1) ? 4 : 0)) & 0xF;
}

void setUp() {
  gPool.init();
  gRuns.init();
  gManifests.init();
  gComp.init(gPool, gRuns, nullptr);
  gStore.init();
}
void tearDown() {}

// Exact segments: a note inside a bar, one crossing a bar line, one held across the loop end (drawn from bar 0), an
// OVL-1 shortened note, and 20 notes sounding at once (all drawn; OUT-POLY limits only output).
void test_segments_per_bar() {
  std::vector<N> a = {{1, 60, 100, 400}, {2, 62, 700, 900}, {3, 64, kLoop - 200, kLoop + 300}, {4, 66, 1600, 2200}};
  for (uint32_t i = 0; i < 20; ++i) a.push_back({100 + i, static_cast<uint8_t>(80 + i), 3 * kBar + 10, 3 * kBar + 100});
  const RunId ra = run(a, RunKind::kRecord);
  const RunId rb = run({{50, 66, 1800, 1900}}, RunKind::kOverdub);  // shortens note 4 to 1799 (OVL-1)
  gStore.setDetail(0, 0);
  sweep(0, manifest({ra, rb}, 1));
  // Bar 0: note 1 inside; note 2's first part, continuing into bar 1; the end of the note held across the loop end,
  // from the bar start, continuing from the left.
  auto s0 = segments(0, 0);
  TEST_ASSERT_EQUAL_UINT32(3, s0.size());
  TEST_ASSERT_TRUE(s0[0].pitch == 60 && s0[0].start == 100 && s0[0].length == 300 && !s0[0].left && !s0[0].right);
  TEST_ASSERT_TRUE(s0[1].pitch == 62 && s0[1].start == 700 && s0[1].length == kBar - 700 && !s0[1].left && s0[1].right);
  TEST_ASSERT_TRUE(s0[2].pitch == 64 && s0[2].start == 0 && s0[2].length == 300 && s0[2].left && !s0[2].right);
  // Bars 0 / 1: note 2 crosses the bar line at 768.
  TEST_ASSERT_TRUE(rasterBit(0, 0, 0, 60) == false && rasterBit(0, 0, 100 * 32 / kBar, 60));
  auto s1 = segments(0, 1);
  TEST_ASSERT_EQUAL_UINT32(1, s1.size());
  TEST_ASSERT_TRUE(s1[0].pitch == 62 && s1[0].start == 0 && s1[0].length == 900 - kBar && s1[0].left && !s1[0].right);
  // Bars 2: note 4 shortened to 1799 by the overdub at 1800, and the overdub note itself.
  auto s2 = segments(0, 2);
  TEST_ASSERT_EQUAL_UINT32(2, s2.size());
  TEST_ASSERT_TRUE(s2[0].pitch == 66 && s2[0].start == 1600 - 2 * kBar && s2[0].length == 1799 - 1600);
  TEST_ASSERT_TRUE(s2[1].pitch == 66 && s2[1].start == 1800 - 2 * kBar && s2[1].length == 100);
  // Bar 3: all 20 simultaneous notes.
  TEST_ASSERT_EQUAL_UINT32(20, segments(0, 3).size());
  // Bar 7: the note held across the loop end, continuing to the right.
  auto s7 = segments(0, 7);
  TEST_ASSERT_EQUAL_UINT32(1, s7.size());
  TEST_ASSERT_TRUE(s7[0].pitch == 64 && s7[0].start == kBar - 200 && s7[0].length == 200 && s7[0].right);
  TEST_ASSERT_EQUAL_UINT32(0, segments(0, 5).size());  // an empty bar of a swept slot: a span tile with nothing
}

// Density tiles for a slot without detail tiles: notes sounding in each bar, per pitch.
void test_density_for_other_slots() {
  const RunId r = run({{1, 60, 100, 400}, {2, 60, 500, 600}, {3, 61, 700, 900}}, RunKind::kRecord);
  sweep(2, manifest({r}, 1, 2));
  TEST_ASSERT_EQUAL_UINT8(2, density(2, 0, 60));
  TEST_ASSERT_EQUAL_UINT8(1, density(2, 0, 61));
  TEST_ASSERT_EQUAL_UINT8(1, density(2, 1, 61));  // note 3 sounds into bar 1
  TEST_ASSERT_EQUAL_UINT8(0, density(2, 1, 60));
  TEST_ASSERT_EQUAL(Store::Kind::Empty, gStore.drawSource(3, 0).kind);  // never swept: legitimately empty
}

// More than 256 notes in a bar: the named dense state keeps the raster.
void test_dense_bar() {
  std::vector<N> a;
  for (uint32_t i = 0; i < 300; ++i) a.push_back({1 + i, static_cast<uint8_t>(20 + i % 100), 5 * kBar + i, 5 * kBar + i + 2});
  gStore.setDetail(0, 0);
  sweep(0, manifest({run(a, RunKind::kRecord)}, 1));
  const Store::Draw d = gStore.drawSource(0, 5);
  TEST_ASSERT_EQUAL(Store::Kind::Dense, d.kind);
  TEST_ASSERT_EQUAL_UINT16(SpanTile::kSegments, d.header->count);
  TEST_ASSERT_TRUE(rasterBit(0, 5, 299 * 32 / kBar, 20 + 299 % 100));  // the 300th note is in the raster
  TEST_ASSERT_EQUAL_UINT32(0, gSweep.overflow());
}

// The draw rule never blanks: Undo with a starved sweep shows the previous tiles; a superseded sweep returns its spare
// and its pin; a focus switch shows density until the new detail tiles are swept; with no spare left the density
// tile is still published.
void test_draw_rule_never_blanks() {
  const RunId ra = run({{1, 60, 100, 400}}, RunKind::kRecord);
  const RunId rb = run({{2, 62, 100, 400}}, RunKind::kOverdub);
  const ManifestId mA = manifest({ra}, 1), mAB = manifest({ra, rb}, 2);
  gStore.setDetail(0, 0);
  sweep(0, mAB);
  // Undo to A: its sweep is starved (no grains yet): every bar still draws AB's complete tiles.
  TEST_ASSERT_EQUAL(Sweep::Status::Running, gSweep.start(gComp, gStore, gManifests, gRuns, 0, mA, kWin, kBar));
  for (uint8_t bar = 0; bar < 8; ++bar) {
    TEST_ASSERT_EQUAL(Store::Kind::Span, gStore.drawSource(0, bar).kind);
    TEST_ASSERT_EQUAL_UINT32(identityOf(mAB), gStore.drawSource(0, bar).identity);
  }
  // While it runs, every bar always draws a complete tile of one revision or the other.
  while (gSweep.step(gWin, 16) == Sweep::Status::Running)
    for (uint8_t bar = 0; bar < 8; ++bar) {
      const Store::Draw d = gStore.drawSource(0, bar);
      TEST_ASSERT_EQUAL(Store::Kind::Span, d.kind);
      TEST_ASSERT_TRUE(d.identity == identityOf(mA) || d.identity == identityOf(mAB));
    }
  for (uint8_t bar = 0; bar < 8; ++bar) TEST_ASSERT_EQUAL_UINT32(identityOf(mA), gStore.drawSource(0, bar).identity);
  TEST_ASSERT_EQUAL_UINT16(4, gStore.freeSpares());
  // A superseded sweep (Redo selected before it finished): spare and pin returned, no tile of it half-shown.
  TEST_ASSERT_EQUAL(Sweep::Status::Running, gSweep.start(gComp, gStore, gManifests, gRuns, 0, mAB, kWin, kBar));
  for (int g = 0; g < 3; ++g) gSweep.step(gWin, 16);
  gStore.setWanted(0, identityOf(mA));
  TEST_ASSERT_EQUAL(Sweep::Status::Superseded, gSweep.step(gWin, 16));
  TEST_ASSERT_FALSE(gSweep.pinned());
  TEST_ASSERT_EQUAL_UINT16(4, gStore.freeSpares());
  // Focus switch: slot 1 gets the detail tiles; its bars draw density until swept; slot 0 falls back to density.
  const RunId rc = run({{3, 64, 100, 400}}, RunKind::kRecord);
  const ManifestId mC = manifest({rc}, 1, 1);
  TEST_ASSERT_EQUAL(Sweep::Status::Running, gSweep.start(gComp, gStore, gManifests, gRuns, 1, mC, kWin, kBar));
  gStore.setDetail(1, 0xFF);
  while (gSweep.step(gWin, 128) == Sweep::Status::Running) {
  }
  gStore.setDetail(0, 1);
  TEST_ASSERT_EQUAL(Store::Kind::Density, gStore.drawSource(1, 0).kind);  // slot 1: density until its span sweep
  TEST_ASSERT_EQUAL(Store::Kind::Density, gStore.drawSource(0, 0).kind);  // slot 0 lost detail: its density
  // No spare left: the sweep still publishes density.
  while (gStore.freeSpares() > 0) gStore.takeSpare();
  sweep(1, mC);
  TEST_ASSERT_EQUAL(Store::Kind::Density, gStore.drawSource(1, 0).kind);
}

void test_no_heap() {
  const RunId r = run({{1, 60, 100, 400}, {2, 61, kLoop - 50, kLoop + 50}}, RunKind::kRecord);
  const ManifestId m = manifest({r}, 1);
  gStore.setDetail(0, 0);
  gTrap = true;
  const unsigned long before = gHeapAllocations;
  sweep(0, m);
  for (uint8_t bar = 0; bar < 8; ++bar) gStore.drawSource(0, bar);
  gTrap = false;
  TEST_ASSERT_EQUAL_UINT32(0, gHeapAllocations - before);
  TEST_ASSERT_TRUE(sizeof(TileStore<>) <= 337920u + 262400u + 4352u);  // plan § 4.1 tiles + § 4.2 tile headers
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_segments_per_bar);
  RUN_TEST(test_density_for_other_slots);
  RUN_TEST(test_dense_bar);
  RUN_TEST(test_draw_rule_never_blanks);
  RUN_TEST(test_no_heap);
  return UNITY_END();
}
