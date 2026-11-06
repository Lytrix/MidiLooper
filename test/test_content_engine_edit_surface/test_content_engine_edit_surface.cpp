//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// fixed-memory-engine E9.8b-2: NOTE_EDIT from the DROID on the production root (owner 2026-11-06: core NOTE_EDIT,
// matching the legacy mappings). Edit mode 38 enters on the shown slot (the bracket at the playhead selecting the note
// in its 16th) and leaves keeping the edits as one undo step; F1 selects the 16th under the fader and, within it, the
// stacked note by the position inside the step; the geometry faders wait 750 ms after a selection unless F1 moved;
// F2 moves the start by 16ths keeping its offset, F3 sets it from the reference 16th ± (value − 64), F4 sets the
// pitch — each heard; one fader gesture is one undo step (record double / triple undo / redo while editing); NOTELEN
// toggles start / end editing (F2 / F3 then set the end, at least a 16th) and its double deletes the selected note or
// creates one at the bracket (none within 24 ticks of a note); a window denser than the selection is halved around the
// playhead; no allocation.

#include <unity.h>

#include <cstdint>
#include <cstdlib>
#include <initializer_list>
#include <new>

#include "ContentEngine/EngineClock.h"
#include "ContentEngine/EngineRoot.h"
#include "ContentEngine/Surface.h"

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
using S = Surface<EngineRoot>;

static EnginePsram gPsram;
static EngineRam2 gRam2;
static EngineRoot gE;
static EngineClock gClk;
static S gS;
static uint32_t gUs = 0, gLast = 0;
static constexpr uint32_t kTick = 2604, kBarUs = 768 * kTick;

// Live notes played into a take: positions in µs.
struct Live {
  uint32_t on, off;
  uint8_t pitch;
};
static const Live* gLive = nullptr;
static uint32_t gLiveCount = 0;
// NoteOns heard on USB: (tick, pitch), the last 4,096.
struct Heard {
  uint32_t tick;
  uint8_t pitch;
};
static Heard gHeard[4096];
static uint32_t gHeardCount = 0;

static void run(uint32_t toUs) {
  ProductionHub::Sent buf[32];
  for (; gUs < toUs; gUs += 125) {
    uint8_t count = 0;
    uint32_t t;
    while (gClk.due(gUs, t, count)) {
      gE.clockTick(t, gUs, gClk.usPerTick());
      gLast = t;
    }
    gE.submitJobs(gLast, gUs, gClk.usPerTick());
    while (gE.runGrain(gUs)) {
    }
    for (uint8_t p = 0; p < 3; ++p) {
      const uint8_t n = gE.transmit(p, gUs, buf, 32);
      for (uint8_t i = 0; p == kPortUsbDevice && i < n; ++i)
        if ((buf[i].status & 0xF0) == 0x90) gHeard[gHeardCount++ % 4096] = {gLast, buf[i].data1};
    }
    gS.service(gUs / 1000, gLast, gUs);
    for (uint32_t i = 0; i < gLiveCount; ++i) {
      const Live& l = gLive[i];
      if (l.on >= gUs && l.on < gUs + 125) gE.inputNoteOn(gLast, 0x90, l.pitch, 100, gUs, gClk.usPerTick());
      if (l.off >= gUs && l.off < gUs + 125) gE.inputNoteOff(gLast, 0x80, l.pitch, gUs);
    }
  }
}
static void ms(uint32_t m) { run(gUs + m * 1000); }
static void tap(uint8_t note, uint32_t holdMs = 50) {
  gS.input(0x9F, note, 127, gUs / 1000);
  ms(holdMs);
  gS.input(0x8F, note, 0, gUs / 1000);
}
static void dbl(uint8_t note) {
  tap(note);
  ms(100);
  tap(note);
}
static void pitchBend(uint8_t channel0, int32_t logical) {
  int32_t raw = logical + 8192;
  if (raw > 16383) raw = 16383;
  gS.input(static_cast<uint8_t>(0xE0 | channel0), static_cast<uint8_t>(raw & 0x7F), static_cast<uint8_t>(raw >> 7),
           gUs / 1000);
}
static void f1(int32_t pb) { pitchBend(15, pb), ms(5); }
static void f2(int32_t pb) { pitchBend(13, pb), ms(5); }
static void f3(uint8_t cc) { gS.input(0xBE, 2, cc, gUs / 1000), ms(5); }
static void f4(uint8_t cc) { gS.input(0xBE, 3, cc, gUs / 1000), ms(5); }
/// The F1 value at the centre of 16th `step` of `steps` (the legacy inverse, one note in the step).
static int32_t pbFor(uint32_t step, uint32_t steps, uint32_t stack = 0, uint32_t count = 1) {
  const int64_t span = 16384;
  const int64_t pos = static_cast<int64_t>(step) * span + (static_cast<int64_t>(stack) * 2 + 1) * span / (count * 2);
  return static_cast<int32_t>(-8192 + pos / static_cast<int64_t>(steps - 1));
}

void setUp() {
  TEST_ASSERT_TRUE(gE.init(gPsram, gRam2, 2000));
  gClk.init(12000);
  gS.init(gE, gClk);
  gUs = 1000000;
  gLast = 0;
  gLive = nullptr;
  gLiveCount = 0;
  gHeardCount = 0;
}
void tearDown() {}

/// Slot 0 on track 0, one bar: `notes` (positions in ticks from the take start, each 40 ticks long).
static void recordBar(std::initializer_list<std::pair<uint32_t, uint8_t>> notes) {
  static Live live[700];
  tap(S::kSlotBase + 0);
  ms(400);  // the clock starts and the take with it
  TEST_ASSERT_EQUAL(ProductionTransport::Mode::Recording, gE.transport().mode());
  // The take started on the clock's first tick (0): positions from there.
  const uint32_t t0 = gUs - gLast * kTick;
  uint32_t n = 0;
  for (const auto& p : notes) live[n++] = {t0 + p.first * kTick + kTick / 2, t0 + (p.first + 40) * kTick, p.second};
  gLive = live;
  gLiveCount = n;
  run(t0 + kBarUs - 200 * kTick);
  tap(S::kSlotBase + 0);  // the take ends at the bar
  run(t0 + kBarUs + 50 * kTick);
  gLive = nullptr;
  gLiveCount = 0;
  TEST_ASSERT_EQUAL(ProductionTransport::Mode::Idle, gE.transport().mode());
}
static void enterEdit() {
  tap(S::kEditModeNote);
  ms(400);
  for (int i = 0; i < 50 && !gS.edit().editing(); ++i) ms(5);
  TEST_ASSERT_TRUE(gS.edit().editing());
}
static EditNote sel() {
  EditNote n{};
  TEST_ASSERT_TRUE(gS.edit().selected(n));
  return n;
}
/// Whether a NoteOn of `pitch` is heard at bar position `pos` over the next two bars.
static bool heard(uint8_t pitch, uint32_t pos) {
  const uint32_t from = gHeardCount;
  run(gUs + 2 * kBarUs);
  for (uint32_t i = from; i < gHeardCount; ++i)
    if (gHeard[i % 4096].pitch == pitch && (gHeard[i % 4096].tick - gE.transport().origin(0)) % 768 == pos) return true;
  return false;
}
/// NoteOns of `pitch` heard from now for one loop (768 ticks): their positions in the bar.
static uint32_t heardAt(uint8_t pitch) {
  const uint32_t from = gHeardCount;
  run(gUs + 2 * kBarUs);
  for (uint32_t i = from; i < gHeardCount; ++i)
    if (gHeard[i % 4096].pitch == pitch) return (gHeard[i % 4096].tick - gE.transport().origin(0)) % 768;
  return 0xFFFFFFFFu;
}

void test_enter_selects_at_the_bracket_and_f1_selects_stacked_notes() {
  recordBar({{0, 60}, {96, 64}, {100, 72}, {384, 67}});
  enterEdit();
  TEST_ASSERT_EQUAL_UINT32(0, gS.edit().windowFirst());
  TEST_ASSERT_EQUAL_UINT32(768, gS.edit().windowTicks());
  // F1: step 2 (tick 96) holds 64 @ 96 and 72 @ 100: the lower part of the step picks 64, the upper 72.
  f1(pbFor(2, 16, 0, 2));
  TEST_ASSERT_EQUAL_UINT8(64, sel().pitch);
  f1(pbFor(2, 16, 1, 2));
  TEST_ASSERT_EQUAL_UINT8(72, sel().pitch);
  f1(pbFor(8, 16));
  TEST_ASSERT_EQUAL_UINT8(67, sel().pitch);
  f1(pbFor(5, 16));  // an empty 16th: the bracket alone
  EditNote n;
  TEST_ASSERT_FALSE(gS.edit().selected(n));
  TEST_ASSERT_EQUAL_UINT32(5 * 48, gS.edit().bracket());
}

void test_geometry_faders_move_and_pitch_heard_one_step_a_gesture() {
  recordBar({{0, 60}, {100, 64}, {384, 67}});
  enterEdit();
  f1(pbFor(2, 16));  // 64 @ 100 (step 2, offset 4)
  const uint8_t depth0 = gE.noteEdit().depth();
  // F2: to step 10 with the offset in the 16th kept, moved twice within 300 ms: one step.
  f2(-8192 + 16384 * 6 / 15);
  f2(-8192 + 16384 * 10 / 15 + 20);
  TEST_ASSERT_EQUAL_UINT32(10 * 48 + 4, sel().tick);
  TEST_ASSERT_EQUAL_UINT8(depth0 + 1, gE.noteEdit().depth());
  TEST_ASSERT_EQUAL_UINT32(484, heardAt(64));
  // F3 (a new gesture, after 300 ms): the reference 16th's tick (480), not the start (484), + 5.
  ms(400);
  f3(69);
  TEST_ASSERT_EQUAL_UINT32(485, sel().tick);
  TEST_ASSERT_EQUAL_UINT8(depth0 + 2, gE.noteEdit().depth());
  // F4: pitch.
  ms(400);
  f4(70);
  TEST_ASSERT_EQUAL_UINT8(70, sel().pitch);
  TEST_ASSERT_EQUAL_UINT32(485, heardAt(70));
  // Undo (record double) twice: the pitch, then the fine move; redo once.
  dbl(S::kRecordNote);
  ms(500);
  TEST_ASSERT_EQUAL_UINT8(64, sel().pitch);
  dbl(S::kRecordNote);
  ms(500);
  TEST_ASSERT_EQUAL_UINT32(484, sel().tick);
  tap(S::kRecordNote), ms(100), tap(S::kRecordNote), ms(100), tap(S::kRecordNote);  // triple: redo
  ms(500);
  TEST_ASSERT_EQUAL_UINT32(485, sel().tick);
}

void test_geometry_waits_the_grace_period_after_entry() {
  recordBar({{96, 60}});
  enterEdit();  // the bracket at the playhead selects the closest note (60 @ 96)
  TEST_ASSERT_EQUAL_UINT8(60, sel().pitch);
  f4(62);  // within 750 ms of the selection: ignored
  TEST_ASSERT_EQUAL_UINT8(60, sel().pitch);
  ms(800);
  f4(62);
  TEST_ASSERT_EQUAL_UINT8(62, sel().pitch);
  dbl(S::kRecordNote);  // undo back to the session start: the selection stays on the note
  ms(500);
  TEST_ASSERT_EQUAL_UINT8(60, sel().pitch);
}

void test_length_mode_sets_the_end_at_least_a_sixteenth() {
  recordBar({{96, 64}});
  enterEdit();
  f1(pbFor(2, 16));
  tap(S::kLengthNote);
  ms(450);
  TEST_ASSERT_TRUE(gS.edit().lengthMode());
  TEST_ASSERT_EQUAL_UINT32(96 + 40, gS.edit().bracket());  // the bracket at the note's end
  f2(-8192 + 16384 * 400 / 767);  // the end to tick ~400
  const EditNote a = sel();
  TEST_ASSERT_UINT32_WITHIN(1, 400 - 96, a.length);
  ms(400);
  f2(-8192);  // the end to 0: before the start — kept a 16th after it
  TEST_ASSERT_EQUAL_UINT32(48, sel().length);
  ms(400);
  f3(64 + 20);  // fine: the end anchor — the end the last coarse move left (144, as legacy) — + 20
  TEST_ASSERT_EQUAL_UINT32(164 - 96, sel().length);
  tap(S::kLengthNote);  // back to start editing: the bracket at the start
  ms(450);
  TEST_ASSERT_FALSE(gS.edit().lengthMode());
  TEST_ASSERT_EQUAL_UINT32(96, gS.edit().bracket());
  tap(S::kLengthNote);  // length mode again; an F1 selection leaves it
  ms(450);
  TEST_ASSERT_TRUE(gS.edit().lengthMode());
  f1(pbFor(2, 16));
  TEST_ASSERT_FALSE(gS.edit().lengthMode());
}

void test_no_note_is_created_next_to_a_note() {
  recordBar({{380, 69}});
  enterEdit();
  f1(pbFor(8, 16));  // step 8 (384) is empty — 69 starts in step 7, 4 ticks before the bracket
  EditNote n;
  TEST_ASSERT_FALSE(gS.edit().selected(n));
  const uint16_t size = gE.noteEdit().size();
  dbl(S::kLengthNote);
  ms(50);
  TEST_ASSERT_EQUAL_UINT16(size, gE.noteEdit().size());
}

void test_notelen_double_deletes_and_creates_and_leave_keeps_one_step() {
  recordBar({{96, 64}, {384, 67}});
  const uint8_t undo0 = gE.history().undoDepth(0);
  enterEdit();
  f1(pbFor(2, 16));
  dbl(S::kLengthNote);  // delete 64
  ms(50);
  EditNote n;
  TEST_ASSERT_FALSE(gS.edit().selected(n));
  TEST_ASSERT_EQUAL_UINT32(0xFFFFFFFFu, heardAt(64));
  f1(pbFor(12, 16));  // an empty 16th (576)
  dbl(S::kLengthNote);  // create: C3, a 32nd, velocity 80
  ms(50);
  const EditNote c = sel();
  TEST_ASSERT_EQUAL_UINT8(60, c.pitch);
  TEST_ASSERT_EQUAL_UINT32(576, c.tick);
  TEST_ASSERT_EQUAL_UINT32(24, c.length);
  TEST_ASSERT_EQUAL_UINT32(576, heardAt(60));
  // Next to a note (67 @ 384, the bracket 8 ticks from it): nothing created.
  f1(pbFor(8, 16));
  dbl(S::kLengthNote);  // deletes 67 (selected) ...
  ms(50);
  const uint16_t size = gE.noteEdit().size();
  dbl(S::kLengthNote);  // ... and now creates at 384 (no note there any more)
  ms(50);
  TEST_ASSERT_EQUAL_UINT16(size + 1, gE.noteEdit().size());
  // Leave: the edits stay, one undo step.
  tap(S::kEditModeNote);
  ms(400);
  TEST_ASSERT_FALSE(gS.edit().active());
  ms(200);
  TEST_ASSERT_EQUAL_UINT8(undo0 + 1, gE.history().undoDepth(0));
  TEST_ASSERT_TRUE(heard(60, 576));
  TEST_ASSERT_TRUE(heard(60, 384));
  TEST_ASSERT_EQUAL_UINT32(0xFFFFFFFFu, heardAt(67));
}

void test_a_window_denser_than_the_selection_is_halved() {
  // 2 bars, 600 notes: more than the 512 a session holds; the first bar alone fits.
  static Live live[700];
  tap(S::kSlotBase + 0);
  ms(400);
  const uint32_t t0 = gUs - gLast * kTick;
  uint32_t n = 0;
  for (uint32_t k = 0; k < 600; ++k) {
    const uint32_t tick = (k % 300) * 2 + (k / 300) * 768;
    live[n++] = {t0 + tick * kTick + kTick / 2, t0 + (tick + 1) * kTick, static_cast<uint8_t>(20 + k % 80)};
  }
  gLive = live;
  gLiveCount = n;
  run(t0 + 2 * kBarUs - 200 * kTick);
  tap(S::kSlotBase + 0);
  run(t0 + 2 * kBarUs + 50 * kTick);
  gLive = nullptr;
  gLiveCount = 0;
  enterEdit();
  TEST_ASSERT_EQUAL_UINT32(768, gS.edit().windowTicks());
}

void test_no_heap() {
  recordBar({{96, 64}});
  gTrap = true;
  const unsigned long before = gHeapAllocations;
  enterEdit();
  f1(pbFor(2, 16));
  f2(0);
  f4(70);
  tap(S::kEditModeNote);
  ms(400);
  gTrap = false;
  TEST_ASSERT_EQUAL_UINT32(0, gHeapAllocations - before);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_enter_selects_at_the_bracket_and_f1_selects_stacked_notes);
  RUN_TEST(test_geometry_faders_move_and_pitch_heard_one_step_a_gesture);
  RUN_TEST(test_geometry_waits_the_grace_period_after_entry);
  RUN_TEST(test_length_mode_sets_the_end_at_least_a_sixteenth);
  RUN_TEST(test_notelen_double_deletes_and_creates_and_leave_keeps_one_step);
  RUN_TEST(test_no_note_is_created_next_to_a_note);
  RUN_TEST(test_a_window_denser_than_the_selection_is_halved);
  RUN_TEST(test_no_heap);
  return UNITY_END();
}
