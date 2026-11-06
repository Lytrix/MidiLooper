//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// fixed-memory-engine E9.6: the piano roll from tiles. Renderer (a small tile store): span segments at the legacy
// geometry and brightness, the highlighted note, band clipping, a dense bar's raster, the density fallback, the pitch
// extent and the live overlay on top. Root (production): the overlay paints a take on the
// shown slot and gives way to the committed tiles; Clear blanks the slot; a NOTE_EDIT gather takes the display cursor
// from a running sweep, which is asked again and completes; no allocation.

#include <unity.h>

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <new>
#include <vector>

#include "ContentEngine/EngineRoot.h"

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
using R = ProductionTransport::Result;
using Store = TileStore<8, 64, 2, 4>;
using Overlay = LiveOverlay<>;

constexpr uint16_t kW = 256, kH = 64;
static uint8_t gFb[kW * kH / 2];
static uint8_t px(int x, int y) {
  const uint32_t i = static_cast<uint32_t>(y) * kW + static_cast<uint32_t>(x);
  return i % 2 ? (gFb[i / 2] & 0x0F) : (gFb[i / 2] >> 4);
}
static uint32_t lit(int y0, int y1) {
  uint32_t n = 0;
  for (int y = y0; y <= y1; ++y)
    for (int x = 0; x < kW; ++x) n += px(x, y) != 0;
  return n;
}

static Store gStore;
static Overlay gOverlay;
static EnginePsram gPsram;
static EngineRam2 gRam2;
static EngineRoot gE;

void setUp() {
  std::memset(gFb, 0, sizeof gFb);
  gStore.init();
  gOverlay.init();
}
void tearDown() {}

constexpr uint32_t kBar = 768, kLoop = 4 * kBar;
// The legacy layout: notes between x 22 and 225, rows 0..31, pitches 48..63 here.
static RollView view(uint8_t slot) { return RollView{slot, kLoop, kBar, 0, kLoop, 48, 63, 22, 225, 0, 31}; }

static void span(uint8_t slot, uint8_t bar, std::vector<NoteSegment> segs, uint32_t identity) {
  const uint16_t t = gStore.takeSpare();
  for (size_t i = 0; i < segs.size(); ++i) gStore.tile(t).segments[i] = segs[i];
  gStore.header(t) = Store::Header{identity, static_cast<uint16_t>(segs.size()), slot, false};
  gStore.publishSpan(gStore.detailOf(slot), bar, t);
}

// ---- Renderer ----------------------------------------------------------------------------------------------------------
void test_span_segments_at_the_legacy_geometry() {
  gStore.setDetail(0, 1);
  span(1, 0, {{0, 192, 48, 100, 0, 0}, {384, 96, 63, 90, 0, 0}}, 7);
  span(1, 2, {{0, 768, 55, 80, 0, 0}}, 7);
  RollView v = view(1);
  v.highlightTick = 384;
  v.highlightPitch = 63;
  drawRollBand(gStore, static_cast<const Overlay*>(nullptr), v, gFb, kW, 0, 31);
  // Pitch 48 -> row 31 (low at the bottom), x from 22 for 192 / 3072 of 203 columns.
  TEST_ASSERT_EQUAL_UINT8(kRollNoteBrightness, px(22, 31));
  TEST_ASSERT_EQUAL_UINT8(kRollNoteBrightness, px(22 + 12, 31));
  TEST_ASSERT_EQUAL_UINT8(0, px(22 + 14, 31));
  // Pitch 63 -> row 0, highlighted.
  TEST_ASSERT_EQUAL_UINT8(kRollHighlightBrightness, px(22 + 25, 0));
  // Bar 2's whole-bar note: x 22 + map(1536) .. 22 + map(2304).
  const int y55 = roll::map(55, 48, 63, 31, 0);
  TEST_ASSERT_EQUAL_UINT8(kRollNoteBrightness, px(22 + 101, y55));
  TEST_ASSERT_EQUAL_UINT8(kRollNoteBrightness, px(22 + 152, y55));
  TEST_ASSERT_EQUAL_UINT8(0, px(22 + 154, y55));
  // Bars 1 and 3 were never swept: nothing there.
  TEST_ASSERT_EQUAL_UINT8(0, px(22 + 60, y55));
}

void test_bands_touch_only_their_rows() {
  gStore.setDetail(0, 1);
  span(1, 0, {{0, 768, 48, 100, 0, 0}, {0, 768, 63, 100, 0, 0}}, 7);
  drawRollBand(gStore, static_cast<const Overlay*>(nullptr), view(1), gFb, kW, 0, 7);
  TEST_ASSERT_TRUE(lit(0, 7) > 0);
  TEST_ASSERT_EQUAL_UINT32(0, lit(8, 63));  // pitch 48's row 31 lies in another band
  drawRollBand(gStore, static_cast<const Overlay*>(nullptr), view(1), gFb, kW, 24, 31);
  TEST_ASSERT_TRUE(lit(31, 31) > 0);
}

void test_dense_raster_density_fallback_and_extent() {
  gStore.setDetail(0, 1);
  const uint16_t t = gStore.takeSpare();
  std::memset(gStore.tile(t).raster, 0, sizeof gStore.tile(t).raster);
  gStore.tile(t).raster[4][50 / 8] |= 1u << (50 % 8);  // column 4 of bar 1, pitch 50
  gStore.header(t) = Store::Header{7, 300, 1, true};
  gStore.publishSpan(0, 1, t);
  // Slot 2 has density only.
  DensityTile d{};
  d.counts[60 / 2] |= 3u << ((60 & 1) * 4);
  gStore.publishDensity(2, 0, d, 9);
  drawRollBand(gStore, static_cast<const Overlay*>(nullptr), view(1), gFb, kW, 0, 31);
  const int x = 22 + roll::map(kBar + 4 * 24, 0, kLoop, 0, 203);
  TEST_ASSERT_EQUAL_UINT8(kRollNoteBrightness, px(x, roll::map(50, 48, 63, 31, 0)));
  std::memset(gFb, 0, sizeof gFb);
  drawRollBand(gStore, static_cast<const Overlay*>(nullptr), view(2), gFb, kW, 0, 31);
  const int y60 = roll::map(60, 48, 63, 31, 0);
  TEST_ASSERT_EQUAL_UINT8(kRollDensityBrightness, px(22, y60));  // never blank: the density tile, dim
  TEST_ASSERT_EQUAL_UINT8(kRollDensityBrightness, px(22 + 50, y60));
  uint8_t lo, hi;
  TEST_ASSERT_TRUE(pitchExtent(gStore, 2, 0, 3, lo, hi));
  TEST_ASSERT_EQUAL_UINT8(60, lo);
  TEST_ASSERT_EQUAL_UINT8(60, hi);
  TEST_ASSERT_FALSE(pitchExtent(gStore, 3, 0, 3, lo, hi));
}

void test_the_live_overlay_draws_on_top() {
  gStore.setDetail(0, 1);
  const RollView v = view(1);
  gOverlay.clear();
  gOverlay.setLoop(kLoop);
  gOverlay.noteOn(300, 52, 100);
  gOverlay.advance(600);  // held: drawn to the current position
  drawRollBand(gStore, &gOverlay, v, gFb, kW, 0, 31);
  const int y = roll::map(52, 48, 63, 31, 0);
  TEST_ASSERT_TRUE(px(22 + roll::map(300, 0, kLoop, 0, 203), y) != 0);
  TEST_ASSERT_TRUE(px(22 + roll::map(590, 0, kLoop, 0, 203), y) != 0);
  TEST_ASSERT_EQUAL_UINT8(0, px(22 + roll::map(700, 0, kLoop, 0, 203), y));
  // The same notes at another scale (a first take's view grows): half the loop spans the width.
  std::memset(gFb, 0, sizeof gFb);
  RollView half = v;
  half.loop = kLoop / 2;
  half.ticks = kLoop / 2;
  drawRollBand(gStore, &gOverlay, half, gFb, kW, 0, 31);
  TEST_ASSERT_TRUE(px(22 + roll::map(590, 0, kLoop / 2, 0, 203), y) != 0);
  // Wrapped: a held note crossing the loop end draws its tail and its head.
  std::memset(gFb, 0, sizeof gFb);
  gOverlay.clear();
  gOverlay.noteOn(kLoop - 100, 55, 100);
  gOverlay.advance(kLoop + 100);
  drawRollBand(gStore, &gOverlay, v, gFb, kW, 0, 31);
  const int y55 = roll::map(55, 48, 63, 31, 0);
  TEST_ASSERT_TRUE(px(22 + roll::map(kLoop - 50, 0, kLoop, 0, 203), y55) != 0);
  TEST_ASSERT_TRUE(px(22 + roll::map(50, 0, kLoop, 0, 203), y55) != 0);
  gOverlay.clear();
}

// ---- Root --------------------------------------------------------------------------------------------------------------
constexpr uint32_t kUsPerTick = 1040;
static uint32_t gClock = 0;
static uint32_t us(uint32_t c) { return c * kUsPerTick; }
struct Live {
  uint32_t on, off;
  uint8_t pitch;
};
static uint32_t gGrains = 0;  // grains per tick (0: as many as pending)
static void run(uint32_t to, const std::vector<Live>& live = {}) {
  ProductionHub::Sent buf[32];
  for (; gClock < to; ++gClock) {
    gE.submitJobs(gClock, us(gClock), kUsPerTick);
    for (uint32_t g = 0; (gGrains == 0 || g < gGrains) && gE.runGrain(us(gClock)); ++g) {
    }
    gE.clockTick(gClock, us(gClock), kUsPerTick);
    for (uint8_t p = 0; p < 3; ++p) gE.transmit(p, us(gClock) + 1000, buf, 32);
    for (const Live& l : live) {
      if (l.off == gClock) gE.inputNoteOff(gClock, 0x80, l.pitch, us(gClock));
      if (l.on == gClock) gE.inputNoteOn(gClock, 0x90, l.pitch, 100, us(gClock), kUsPerTick);
    }
  }
}
static RollView rootView() { return RollView{0, kLoop, kBar, 0, kLoop, 48, 63, 22, 225, 0, 31}; }

void test_root_overlay_then_tiles_then_clear() {
  TEST_ASSERT_TRUE(gE.init(gPsram, gRam2, 2000));
  gClock = 0;
  gE.setDisplayFocus(0, 1);
  gE.control().selectTrack(0);
  TEST_ASSERT_EQUAL(R::Ok, gE.control().queueRecord(0, 0, 0));
  const std::vector<Live> take = {{kBar + 768, kBar + 1200, 55}};
  run(kBar + 1300, take);
  gE.drawRollBand(rootView(), gFb, kW, 0, 31);  // the take so far: the overlay
  const int y = roll::map(55, 48, 63, 31, 0);
  TEST_ASSERT_TRUE(px(22 + roll::map(900, 0, kLoop, 0, 203), y) != 0);
  run(kBar + kLoop - 10, take);
  TEST_ASSERT_EQUAL(R::Ok, gE.control().queueStop(0, gClock));
  run(kBar + kLoop + 2000, take);
  // Committed: the span tiles show the note (brightness 7), the overlay is gone.
  TEST_ASSERT_EQUAL(ProductionTiles::Kind::Span, gE.tiles().drawSource(0, 1).kind);
  std::memset(gFb, 0, sizeof gFb);
  gE.drawRollBand(rootView(), gFb, kW, 0, 31);
  TEST_ASSERT_EQUAL_UINT8(kRollNoteBrightness, px(22 + roll::map(900, 0, kLoop, 0, 203), y));
  TEST_ASSERT_EQUAL_UINT32(roll::map(1200, 0, kLoop, 0, 203) - roll::map(768, 0, kLoop, 0, 203) + 1, lit(y, y));
  // Clear: the slot draws nothing.
  TEST_ASSERT_EQUAL(R::Ok, gE.control().clear(0, gClock, us(gClock), kUsPerTick));
  run(gClock + 10);
  std::memset(gFb, 0, sizeof gFb);
  gE.drawRollBand(rootView(), gFb, kW, 0, 31);
  TEST_ASSERT_EQUAL_UINT32(0, lit(0, 31));
}

// E9.8j: while the take records, its pre-roll is drawn — a note held into tick 0 from the start, a pickup begun 20 ticks
// early before the take's current end, wrapping to where it was released (100 ticks into the take).
void test_the_pre_roll_is_drawn_while_the_take_records() {
  TEST_ASSERT_TRUE(gE.init(gPsram, gRam2, 2000));
  gClock = 0;
  gE.setDisplayFocus(0, 1);
  gE.control().selectTrack(0);
  TEST_ASSERT_EQUAL(R::Ok, gE.control().queueRecord(0, 0, 0));
  const std::vector<Live> take = {{kBar - 400, kBar + 50, 50}, {kBar - 20, kBar + 100, 57}};
  run(kBar + 300, take);
  const auto& o = gE.overlay();
  TEST_ASSERT_EQUAL_UINT16(2, o.count());
  TEST_ASSERT_EQUAL_UINT32(299, o.wrapLength());  // a first take: its length so far
  bool sawPickup = false, sawStart = false;
  for (uint16_t i = 0; i < o.count(); ++i) {
    const auto n = o.note(i);
    if (n.pitch == 57) {
      sawPickup = true;
      TEST_ASSERT_EQUAL_UINT32(299 - 20, n.start);
      TEST_ASSERT_EQUAL_UINT32(100, n.end);  // wrapped: released 100 ticks into the take
    }
    if (n.pitch == 50) {
      sawStart = true;
      TEST_ASSERT_EQUAL_UINT32(0, n.start);
      TEST_ASSERT_EQUAL_UINT32(50, n.end);
    }
  }
  TEST_ASSERT_TRUE(sawPickup && sawStart);
  RollView v = rootView();  // as the screen: the view is the take so far
  v.loop = v.ticks = 299;
  gE.drawRollBand(v, gFb, kW, 0, 31);
  const int y57 = roll::map(57, 48, 63, 31, 0), y50 = roll::map(50, 48, 63, 31, 0);
  TEST_ASSERT_TRUE(px(22 + roll::map(290, 0, 299, 0, 203), y57) != 0);  // before the view's end
  TEST_ASSERT_TRUE(px(22 + roll::map(50, 0, 299, 0, 203), y57) != 0);   // wrapped to the start
  TEST_ASSERT_EQUAL_UINT8(0, px(22 + roll::map(200, 0, 299, 0, 203), y57));
  TEST_ASSERT_TRUE(px(22 + roll::map(20, 0, 299, 0, 203), y50) != 0);
}

void test_a_gather_takes_the_display_cursor_from_a_sweep() {
  TEST_ASSERT_TRUE(gE.init(gPsram, gRam2, 2000));
  gClock = 0;
  gE.setDisplayFocus(0, 1);
  gE.control().selectTrack(0);
  TEST_ASSERT_EQUAL(R::Ok, gE.control().queueRecord(0, 0, 0));
  std::vector<Live> take;
  for (uint32_t k = 0; k < 40; ++k) take.push_back({kBar + 70 * k + 5, kBar + 70 * k + 40, static_cast<uint8_t>(48 + k % 16)});
  run(kBar + kLoop - 10, take);
  TEST_ASSERT_EQUAL(R::Ok, gE.control().queueStop(0, gClock));
  run(kBar + kLoop + 1, take);
  // One grain a tick: the commit's sweep is running when a selection is asked for.
  gGrains = 1;
  run(gClock + 3);
  TEST_ASSERT_TRUE(gE.displayJobs().sweeping());
  TEST_ASSERT_EQUAL(ProductionNoteEdit::Gather::Running, gE.noteEdit().request({0, 0, kLoop, 0, 127}, 0));
  run(gClock + 400);
  TEST_ASSERT_EQUAL(ProductionNoteEdit::Gather::Done, gE.noteEdit().status());
  TEST_ASSERT_EQUAL_UINT16(40, gE.noteEdit().count());
  run(gClock + 1500);  // the sweep was asked again and completes
  gGrains = 0;
  TEST_ASSERT_EQUAL_UINT32(gE.manifests().manifest(gE.history().current(0)).identity, gE.tiles().densityIdentity(0));
  TEST_ASSERT_EQUAL(ProductionTiles::Kind::Span, gE.tiles().drawSource(0, 0).kind);
}

void test_a_new_focus_gets_span_tiles() {
  TEST_ASSERT_TRUE(gE.init(gPsram, gRam2, 2000));
  gClock = 0;
  gE.setDisplayFocus(5, 6);  // slot 0 is not shown in detail
  gE.control().selectTrack(0);
  TEST_ASSERT_EQUAL(R::Ok, gE.control().queueRecord(0, 0, 0));
  const std::vector<Live> take = {{kBar + 10, kBar + 300, 60}};
  run(kBar + 1000, take);
  TEST_ASSERT_EQUAL(R::Ok, gE.control().stopNow(gClock, us(gClock), kUsPerTick));
  run(gClock + 2000);
  TEST_ASSERT_EQUAL(ProductionTiles::Kind::Density, gE.tiles().drawSource(0, 0).kind);
  gE.setDisplayFocus(0, 1);  // now it is: swept again for its span tiles
  run(gClock + 2000);
  TEST_ASSERT_EQUAL(ProductionTiles::Kind::Span, gE.tiles().drawSource(0, 0).kind);
}

void test_no_heap() {
  TEST_ASSERT_TRUE(gE.init(gPsram, gRam2, 2000));
  const std::vector<Live> take = {{kBar + 10, kBar + 300, 60}};
  gTrap = true;
  const unsigned long before = gHeapAllocations;
  gClock = 0;
  gE.setDisplayFocus(0, 1);
  gE.control().queueRecord(0, 0, 0);
  run(kBar + 1000, take);
  gE.control().stopNow(gClock, us(gClock), kUsPerTick);
  run(gClock + 2000);
  gE.drawRollBand(rootView(), gFb, kW, 0, 31);
  gTrap = false;
  TEST_ASSERT_EQUAL_UINT32(0, gHeapAllocations - before);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_span_segments_at_the_legacy_geometry);
  RUN_TEST(test_bands_touch_only_their_rows);
  RUN_TEST(test_dense_raster_density_fallback_and_extent);
  RUN_TEST(test_the_live_overlay_draws_on_top);
  RUN_TEST(test_root_overlay_then_tiles_then_clear);
  RUN_TEST(test_the_pre_roll_is_drawn_while_the_take_records);
  RUN_TEST(test_a_gather_takes_the_display_cursor_from_a_sweep);
  RUN_TEST(test_a_new_focus_gets_span_tiles);
  RUN_TEST(test_no_heap);
  return UNITY_END();
}
