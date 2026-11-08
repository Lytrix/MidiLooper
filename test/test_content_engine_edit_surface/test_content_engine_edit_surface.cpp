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

#include <vector>

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
static Heard gOff[4096];  // NoteOffs heard on USB
static uint32_t gOffCount = 0;

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
        if ((buf[i].status & 0xF0) == 0x90 && buf[i].data2 != 0)
          gHeard[gHeardCount++ % 4096] = {gLast, buf[i].data1};
        else if ((buf[i].status & 0xF0) == 0x80 || (buf[i].status & 0xF0) == 0x90)
          gOff[gOffCount++ % 4096] = {gLast, buf[i].data1};
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
  gOffCount = 0;
}
void tearDown() {}

/// Slot 0 on track 0, one bar: `notes` (positions in ticks from the take start, each 40 ticks long).
static void recordBar(std::initializer_list<std::pair<uint32_t, uint8_t>> notes) {
  static Live live[700];
  tap(S::kSlotBase + 0);  // armed
  ms(400);
  tap(S::kSlotBase + 0);  // the clock starts and the take with it
  ms(400);
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
/// Past the motor faders' echo window after a write (NOTE_EDIT feedback): a value equal to a just-written one is the
/// fader's echo, not a move.
static void settle() { ms(EditSurface<EngineRoot>::kMotorSettleMs + 10); }
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
  settle();
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

void test_geometry_faders_move_and_pitch_heard_one_step_a_note() {
  recordBar({{0, 60}, {100, 64}, {384, 67}});
  enterEdit();
  settle();
  f1(pbFor(2, 16));  // 64 @ 100 (step 2, offset 4)
  settle();          // F2–F4's motors follow the selection first
  const uint8_t depth0 = gE.noteEdit().depth();
  // F2: to step 10 with the offset in the 16th kept: the note's step.
  f2(-8192 + 16384 * 6 / 15);
  f2(-8192 + 16384 * 10 / 15 + 20);
  TEST_ASSERT_EQUAL_UINT32(10 * 48 + 4, sel().tick);
  TEST_ASSERT_EQUAL_UINT8(depth0 + 1, gE.noteEdit().depth());
  TEST_ASSERT_EQUAL_UINT32(484, heardAt(64));
  // F3: the reference 16th's tick (480), not the start (484), + 5 — the same note: its step replaced, not another.
  ms(400);
  f3(69);
  TEST_ASSERT_EQUAL_UINT32(485, sel().tick);
  TEST_ASSERT_EQUAL_UINT8(depth0 + 1, gE.noteEdit().depth());
  // F4: pitch.
  ms(400);
  f4(70);
  TEST_ASSERT_EQUAL_UINT8(70, sel().pitch);
  TEST_ASSERT_EQUAL_UINT32(485, heardAt(70));
  TEST_ASSERT_EQUAL_UINT8(depth0 + 1, gE.noteEdit().depth());
  TEST_ASSERT_EQUAL_UINT8(depth0, gS.edit().steps());  // the screen's EDIT count: the note's step once deselected
  // Another note: a step of its own; a change back to its value takes the step away again.
  f1(pbFor(8, 16));  // 67 @ 384
  TEST_ASSERT_EQUAL_UINT8(67, sel().pitch);
  TEST_ASSERT_EQUAL_UINT8(depth0 + 1, gS.edit().steps());
  settle();
  f4(72);
  TEST_ASSERT_EQUAL_UINT8(depth0 + 2, gE.noteEdit().depth());
  TEST_ASSERT_EQUAL_UINT8(depth0 + 1, gS.edit().steps());
  ms(400);
  f4(67);
  TEST_ASSERT_EQUAL_UINT8(depth0 + 1, gE.noteEdit().depth());
  TEST_ASSERT_EQUAL_UINT8(depth0 + 1, gS.edit().steps());
  ms(400);
  f4(72);
  TEST_ASSERT_EQUAL_UINT8(depth0 + 2, gE.noteEdit().depth());
  // Undo (record double) twice: 67's step, then all of 64's; redo once.
  dbl(S::kRecordNote);
  ms(500);
  dbl(S::kRecordNote);
  ms(500);
  TEST_ASSERT_EQUAL_UINT8(depth0, gS.edit().steps());
  TEST_ASSERT_EQUAL_UINT32(100, heardAt(64));  // every F2 / F3 / F4 change undone at once
  tap(S::kRecordNote), ms(100), tap(S::kRecordNote), ms(100), tap(S::kRecordNote);  // triple: redo
  ms(500);
  TEST_ASSERT_EQUAL_UINT32(485, heardAt(70));
}

// The DROID keeps sending a fader at rest (a repeat, or jitter of a few values): F1 acts only when its position moves,
// as legacy (`shouldApplySelectionOnNoteIdChange`). Owner 2026-11-06: F2–F4 did not move the selected note — each F1
// message re-selected at F1's 16th, which the moved note had left.
void test_f1_at_rest_keeps_the_moved_note_selected() {
  recordBar({{0, 60}, {100, 64}, {384, 67}});
  enterEdit();
  settle();
  f1(pbFor(2, 16));  // 64 @ 100
  settle();          // F2–F4's motors follow the selection first
  f2(-8192 + 16384 * 10 / 15 + 20);  // to step 10
  TEST_ASSERT_EQUAL_UINT32(10 * 48 + 4, sel().tick);
  f1(pbFor(2, 16));      // F1 repeats its value
  f1(pbFor(2, 16) + 3);  // and jitters
  TEST_ASSERT_EQUAL_UINT8(64, sel().pitch);
  TEST_ASSERT_EQUAL_UINT32(10 * 48 + 4, sel().tick);
  TEST_ASSERT_EQUAL_UINT32(10 * 48 + 4, gS.edit().bracket());
  ms(400);
  f2(-8192 + 16384 * 12 / 15 + 20);  // the note keeps moving
  TEST_ASSERT_EQUAL_UINT8(64, sel().pitch);
  TEST_ASSERT_EQUAL_UINT32(12 * 48 + 4, sel().tick);
  ms(400);
  f4(70);
  TEST_ASSERT_EQUAL_UINT8(70, sel().pitch);
  f1(pbFor(8, 16));  // F1 moved to another 16th: it selects again
  TEST_ASSERT_EQUAL_UINT8(67, sel().pitch);
  // A new session: F1's first message selects, even at the value the last session left it (past the motor's echo
  // window: the opening writes F1 to the selection).
  tap(S::kEditModeNote);
  ms(400);
  TEST_ASSERT_FALSE(gS.edit().editing());
  enterEdit();
  settle();
  f1(pbFor(8, 16));
  TEST_ASSERT_EQUAL_UINT32(384, gS.edit().bracket());
  TEST_ASSERT_EQUAL_UINT8(67, sel().pitch);
}

// The DROID's fader bank (Program Change on channel 16, `_EDIT_NOTE_STATE`) follows NOTE_EDIT, as legacy
// `sendEditSessionMidi`: owner 2026-11-06 — in NOTE_EDIT F2–F4 sent the loop-edit bank's messages (CC 101 on channel 16).
void test_the_droid_fader_bank_follows_note_edit() {
  uint8_t p = 0xEE;
  TEST_ASSERT_TRUE(gS.bank(p));  // boot: the current bank, so the DROID follows a restart
  TEST_ASSERT_EQUAL_UINT8(S::kLoopEditBank, p);
  TEST_ASSERT_FALSE(gS.bank(p));  // once
  recordBar({{96, 60}});
  TEST_ASSERT_FALSE(gS.bank(p));
  enterEdit();
  TEST_ASSERT_TRUE(gS.bank(p));
  TEST_ASSERT_EQUAL_UINT8(S::kNoteEditBank, p);
  TEST_ASSERT_FALSE(gS.bank(p));
  tap(S::kEditModeNote);  // leave
  ms(400);
  TEST_ASSERT_FALSE(gS.edit().active());
  TEST_ASSERT_TRUE(gS.bank(p));
  TEST_ASSERT_EQUAL_UINT8(S::kLoopEditBank, p);
  TEST_ASSERT_FALSE(gS.bank(p));
}

// Motor feedback (owner 2026-11-06): the DROID's motor faders follow the selection — a value, then its trigger note,
// released after the gate; F1 / F2 / F3 / F4 = pitch bend 16, pitch bend 14, CC 2 / CC 3 on 15; trigger notes 0 on 16 /
// 14, 1 / 2 on 15. A fader's echo of a written value changes nothing.
using Motor = EditSurface<EngineRoot>::MotorMsg;
struct Sent {
  uint32_t ms;
  Motor m;
};
static std::vector<Sent> drainMotors(uint32_t forMs) {
  std::vector<Sent> v;
  Motor m;
  for (uint32_t t = 0; t < forMs; ++t) {
    while (gS.motor(gUs / 1000, m)) v.push_back({gUs / 1000, m});
    ms(1);
  }
  return v;
}
static const Sent* findMsg(const std::vector<Sent>& v, uint8_t status, uint8_t d1) {
  for (const Sent& s : v)
    if (s.m.status == status && s.m.d1 == d1) return &s;
  return nullptr;
}
static const Sent* findStatus(const std::vector<Sent>& v, uint8_t status) {
  for (const Sent& s : v)
    if (s.m.status == status) return &s;
  return nullptr;
}
/// Times fader `i` was written (its trigger note on): F1 / F2 note 0 on 16 / 14, F3 / F4 note 1 / 2 on 15.
static uint32_t written(const std::vector<Sent>& v, uint8_t i) {
  static const uint8_t kStatus[4] = {0x9F, 0x9D, 0x9E, 0x9E}, kNote[4] = {0, 0, 1, 2};
  uint32_t n = 0;
  for (const Sent& s : v) n += (s.m.status == kStatus[i] && s.m.d1 == kNote[i]) ? 1 : 0;
  return n;
}
static int32_t pbOf(const Sent* s) { return static_cast<int32_t>(s->m.d1 | (s->m.d2 << 7)) - 8192; }
static uint32_t count(const std::vector<Sent>& v, uint8_t status) {
  uint32_t n = 0;
  for (const Sent& s : v) n += (s.m.status == status) ? 1 : 0;
  return n;
}

void test_motor_faders_follow_the_selection_and_their_echo_changes_nothing() {
  recordBar({{0, 60}, {100, 64}, {384, 67}});
  tap(S::kEditModeNote);
  for (int i = 0; i < 1000 && !gS.edit().editing(); ++i) ms(1);
  TEST_ASSERT_TRUE(gS.edit().editing());
  const EditNote n0 = sel();
  // The opening wrote the motors just now: their travel from positions not yet known is not a move.
  const uint8_t depth = gE.noteEdit().depth();
  f1(pbFor(12, 16));
  f2(4000);
  f3(10);
  f4(30);
  TEST_ASSERT_EQUAL_UINT8(depth, gE.noteEdit().depth());
  TEST_ASSERT_EQUAL_UINT32(n0.tick, sel().tick);
  TEST_ASSERT_EQUAL_UINT8(n0.pitch, sel().pitch);
  std::vector<Sent> v = drainMotors(300);
  // The opening: all four, each value before its trigger, the trigger released after the gate.
  TEST_ASSERT_EQUAL_UINT32(1, count(v, 0xEF));
  TEST_ASSERT_EQUAL_UINT32(1, count(v, 0xED));
  const Sent* fine = findMsg(v, 0xBE, 2);
  const Sent* pitch = findMsg(v, 0xBE, 3);
  TEST_ASSERT_NOT_NULL(fine);
  TEST_ASSERT_NOT_NULL(pitch);
  TEST_ASSERT_EQUAL_UINT8(64 + n0.tick % 48, fine->m.d2);
  TEST_ASSERT_EQUAL_UINT8(n0.pitch, pitch->m.d2);
  const Sent* on = findMsg(v, 0x9E, 1);
  const Sent* off = findMsg(v, 0x8E, 1);
  TEST_ASSERT_NOT_NULL(on);
  TEST_ASSERT_NOT_NULL(off);
  TEST_ASSERT_TRUE(fine->ms <= on->ms);
  TEST_ASSERT_TRUE(off->ms - on->ms >= EditSurface<EngineRoot>::kMotorGateMs);
  TEST_ASSERT_NOT_NULL(findMsg(v, 0x9F, 0));  // F1's trigger (channel 16 note 0)
  TEST_ASSERT_NOT_NULL(findMsg(v, 0x9D, 0));  // F2's (channel 14 note 0)
  TEST_ASSERT_NOT_NULL(findMsg(v, 0x9E, 2));  // F4's (channel 15 note 2)
  // The echo of every written value: nothing changes, no undo step.
  f1(pbOf(findStatus(v, 0xEF)));
  f2(pbOf(findStatus(v, 0xED)));
  f3(fine->m.d2);
  f4(pitch->m.d2);
  TEST_ASSERT_EQUAL_UINT8(depth, gE.noteEdit().depth());
  TEST_ASSERT_EQUAL_UINT32(n0.tick, sel().tick);
  TEST_ASSERT_EQUAL_UINT8(n0.pitch, sel().pitch);
  // F1 selects 67 @ 384, its motors arrive; then 64 @ 100: F2–F4 follow, F1 is not written.
  settle();
  drainMotors(200);
  f1(pbFor(8, 16));
  TEST_ASSERT_EQUAL_UINT8(67, sel().pitch);
  v = drainMotors(5);
  const int32_t f2at67 = pbOf(findStatus(v, 0xED));
  const uint8_t f3at67 = findMsg(v, 0xBE, 2)->m.d2;
  const uint8_t depth67 = gE.noteEdit().depth();
  // The motors come to rest past their values (the DROID's land a few percent off): F2 travelled up from 64's 16th and
  // stops 450 beyond, F3 travelled down from 68 and stops 4 below — not moves.
  f2(f2at67 + 450);
  f3(static_cast<uint8_t>(f3at67 - 4));
  f4(67);
  TEST_ASSERT_EQUAL_UINT32(384, sel().tick);
  TEST_ASSERT_EQUAL_UINT8(depth67, gE.noteEdit().depth());
  settle();
  drainMotors(200);
  f1(pbFor(2, 16));
  TEST_ASSERT_EQUAL_UINT8(64, sel().pitch);
  v = drainMotors(5);  // the writes, before the motors move
  TEST_ASSERT_EQUAL_UINT32(0, written(v, 0));
  TEST_ASSERT_EQUAL_UINT32(1, written(v, 1));
  TEST_ASSERT_EQUAL_UINT8(64 + 4, findMsg(v, 0xBE, 2)->m.d2);
  TEST_ASSERT_EQUAL_UINT8(64, findMsg(v, 0xBE, 3)->m.d2);
  // The motors travel from where the faders were (the opening's values) to the new ones: every message on the way is
  // theirs, and a motor still moving past the settle time keeps its window open.
  const uint8_t depth2 = gE.noteEdit().depth();
  const int32_t f2to = pbOf(findStatus(v, 0xED));
  f2((f2at67 + f2to) / 2);
  f3(static_cast<uint8_t>((f3at67 + 68) / 2));
  for (int k = 0; k <= 8; ++k)  // ~850 ms of F4 travel, from 67 to 64
    f4(static_cast<uint8_t>(67 - 3 * k / 8)), ms(95);
  TEST_ASSERT_EQUAL_UINT32(100, sel().tick);
  TEST_ASSERT_EQUAL_UINT8(64, sel().pitch);
  TEST_ASSERT_EQUAL_UINT8(depth2, gE.noteEdit().depth());
  // Arrived: a move away from the written value is the musician's, at once.
  f4(70);
  TEST_ASSERT_EQUAL_UINT8(70, sel().pitch);
  ms(400);
  f4(64);
  TEST_ASSERT_EQUAL_UINT8(64, sel().pitch);
  settle();
  f3(68);  // F3 rests at the note's offset (its value already)
  // F2's value maps back to the note's 16th: its echo after the settle time does not move the note either.
  const int32_t coarse = pbOf(findStatus(v, 0xED));
  settle();
  ms(1100);  // past F2's 1 s rest: an at-rest value acts again
  const uint8_t depth1 = gE.noteEdit().depth();
  f2(coarse);
  TEST_ASSERT_EQUAL_UINT32(100, sel().tick);
  TEST_ASSERT_EQUAL_UINT8(depth1, gE.noteEdit().depth());  // a value that changes nothing takes no step
  // A note on a 16th's first tick (67 @ 384): F2's value maps back into that 16th, not the one before.
  f1(pbFor(8, 16));
  TEST_ASSERT_EQUAL_UINT8(67, sel().pitch);
  v = drainMotors(300);
  const int32_t onBoundary = pbOf(findStatus(v, 0xED));
  settle();
  ms(1100);
  f2(onBoundary);
  TEST_ASSERT_EQUAL_UINT32(384, sel().tick);
  f1(pbFor(2, 16));  // back to 64
  TEST_ASSERT_EQUAL_UINT8(64, sel().pitch);
  settle();
  // F2 moves the note to step 10: F3 would follow (the offset in its new 16th), but it is there already — not
  // rewritten, so the musician's F3 move right after acts at once (owner 2026-11-06: a rewrite on every F2 step
  // swallowed it).
  f3(68);  // F3 at the offset
  drainMotors(200);
  f2(-8192 + 16384 * 10 / 15 + 20);
  TEST_ASSERT_EQUAL_UINT32(10 * 48 + 4, sel().tick);
  v = drainMotors(30);
  TEST_ASSERT_EQUAL_UINT32(0, written(v, 0) + written(v, 1) + written(v, 2) + written(v, 3));
  // F3 moves it within the 16th: nothing is written — F2 shows the reference 16th F3 offsets from (owner 2026-11-06:
  // F2 followed F3, its landing a little off was read as a move and took the note a 16th on).
  f3(64 + 20);
  TEST_ASSERT_EQUAL_UINT32(10 * 48 + 20, sel().tick);
  v = drainMotors(300);
  TEST_ASSERT_EQUAL_UINT32(0, written(v, 0) + written(v, 1) + written(v, 2) + written(v, 3));
  // A hand on a fader the motor was just sent to: a move off the motor's path is the musician's at once, and so is
  // everything after it, even back near the written value.
  settle();
  f4(64);  // F4 rests at 64's pitch
  drainMotors(200);
  f1(pbFor(8, 16));  // 67: F4 written 64 -> 67
  TEST_ASSERT_EQUAL_UINT8(67, sel().pitch);
  f4(90);  // off the path 64..67
  TEST_ASSERT_EQUAL_UINT8(90, sel().pitch);
  ms(400);
  f4(66);
  TEST_ASSERT_EQUAL_UINT8(66, sel().pitch);
  // A hand move sends the fader's position back as a value alone — no trigger — so the DROID's stored start value
  // (where a `clear`, also sent by the 16th-step LEDs 2 / 3, moves the motor) is the fader's (owner 2026-11-06: F4 went
  // back to an old pitch).
  v = drainMotors(300);
  f4(72);
  TEST_ASSERT_EQUAL_UINT8(72, sel().pitch);
  v = drainMotors(300);
  TEST_ASSERT_EQUAL_UINT32(0, written(v, 3));
  const Sent* synced = findMsg(v, 0xBE, 3);
  TEST_ASSERT_NOT_NULL(synced);
  TEST_ASSERT_EQUAL_UINT8(72, synced->m.d2);
  // A hand position still to be sent when the fader is written: the write wins, the old position is not sent after it.
  f4(75);
  f1(pbFor(10, 16));  // 64, moved to 500 above: F4 written 64
  TEST_ASSERT_EQUAL_UINT8(64, sel().pitch);
  v = drainMotors(300);
  uint32_t cc3 = 0;
  for (const Sent& s : v) cc3 += (s.m.status == 0xBE && s.m.d1 == 3) ? 1 : 0;
  TEST_ASSERT_EQUAL_UINT32(1, cc3);
  TEST_ASSERT_EQUAL_UINT8(64, findMsg(v, 0xBE, 3)->m.d2);
  // Outside NOTE_EDIT nothing is sent back.
  tap(S::kEditModeNote);
  ms(400);
  TEST_ASSERT_FALSE(gS.edit().active());
  drainMotors(300);
  f4(50);
  f2(1000);
  TEST_ASSERT_EQUAL_UINT32(0, drainMotors(50).size());
}

// F2 back over the note's start within one gesture: the gesture's step is undone and the note is where it began — the
// bracket goes with it (owner 2026-11-06: it stayed a 16th on, at the note's end) and no step remains.
void test_f2_back_at_the_start_keeps_the_bracket_on_the_note() {
  recordBar({{0, 60}, {100, 64}, {384, 67}});
  enterEdit();
  settle();
  f1(pbFor(2, 16));  // 64 @ 100
  settle();
  const uint8_t depth0 = gE.noteEdit().depth();
  f2(-8192 + 16384 * 3 / 15 + 20);  // step 3: 148
  TEST_ASSERT_EQUAL_UINT32(148, sel().tick);
  TEST_ASSERT_EQUAL_UINT32(148, gS.edit().bracket());
  f2(-8192 + 16384 * 2 / 15 + 20);  // back to step 2 within the gesture: 100
  TEST_ASSERT_EQUAL_UINT32(100, sel().tick);
  TEST_ASSERT_EQUAL_UINT32(100, gS.edit().bracket());
  TEST_ASSERT_EQUAL_UINT8(depth0, gE.noteEdit().depth());
}

// F3 puts the start past its reference 16th (+60: into the next); F2 then moves it in whole 16ths keeping that offset
// (owner 2026-11-07: it fell back 48 ticks, and F3 was rewritten). Below the 16th (−20) too.
void test_f2_keeps_the_offset_f3_set_past_the_16th() {
  recordBar({{0, 60}, {100, 64}, {384, 67}});
  enterEdit();
  settle();
  f1(pbFor(2, 16));  // 64 @ 100: reference 16th 2 (96)
  settle();
  f3(64 + 60);
  TEST_ASSERT_EQUAL_UINT32(96 + 60, sel().tick);  // 156: in 16th 3
  drainMotors(200);
  ms(400);
  f2(-8192 + 16384 * 10 / 15 + 20);  // to 16th 10
  TEST_ASSERT_EQUAL_UINT32(10 * 48 + 60, sel().tick);
  TEST_ASSERT_EQUAL_UINT32(0, written(drainMotors(50), 2));  // F3's value is unchanged: not rewritten
  ms(400);
  f3(64 - 20);
  TEST_ASSERT_EQUAL_UINT32(10 * 48 - 20, sel().tick);
  ms(400);
  f2(-8192 + 16384 * 0 / 15 + 20);  // to 16th 0: −20 wraps to the loop's end
  TEST_ASSERT_EQUAL_UINT32(768 - 20, sel().tick);
  ms(400);
  f2(-8192 + 16384 * 5 / 15 + 20);  // and back: 16th 5 − 20
  TEST_ASSERT_EQUAL_UINT32(5 * 48 - 20, sel().tick);
}

// A write still to be sent when a later one finds the fader at its value already: the earlier is dropped (the motor
// would go to a value the selection no longer has).
void test_a_pending_write_gives_way_to_a_fader_already_there() {
  recordBar({{0, 60}, {100, 64}, {384, 67}});
  enterEdit();
  settle();
  drainMotors(200);
  f4(64);            // F4 rests at 64
  f1(pbFor(8, 16));  // 67: F4 to be written 67 (not sent yet)
  f1(pbFor(2, 16));  // 64: F4 is at 64 already
  TEST_ASSERT_EQUAL_UINT8(64, sel().pitch);
  TEST_ASSERT_EQUAL_UINT32(0, written(drainMotors(300), 3));
}

/// NoteOns of `pitch` heard at bar position `pos` over the next two bars.
static uint32_t onsAt(uint8_t pitch, uint32_t pos) {
  const uint32_t from = gHeardCount;
  run(gUs + 2 * kBarUs);
  uint32_t n = 0;
  for (uint32_t i = from; i < gHeardCount; ++i)
    if (gHeard[i % 4096].pitch == pitch && (gHeard[i % 4096].tick - gE.transport().origin(0)) % 768 == pos) ++n;
  return n;
}

// Overlap through the surface (owner 2026-11-07): F4 moves the selected note onto a stacked note's pitch — it hides it
// while on it, and brings it back when moved on; deselecting (F1 to another note) settles the hide, so moving the note on
// afterwards keeps it; leaving NOTE_EDIT keeps it too.
void test_pitch_onto_a_note_hides_it_and_deselect_settles() {
  recordBar({{96, 60}, {96, 64}, {384, 67}});
  enterEdit();
  settle();
  f1(pbFor(2, 16, 0, 2));  // 60 @ 96
  TEST_ASSERT_EQUAL_UINT8(60, sel().pitch);
  settle();
  f4(64);
  TEST_ASSERT_EQUAL_UINT32(2, onsAt(64, 96));  // A alone: B hidden
  ms(400);
  f4(66);
  TEST_ASSERT_EQUAL_UINT32(4, onsAt(64, 96) + onsAt(66, 96));  // B back while A is selected
  ms(400);
  f4(64);
  TEST_ASSERT_EQUAL_UINT32(2, onsAt(64, 96));
  f1(pbFor(8, 16));  // deselect: 67
  TEST_ASSERT_EQUAL_UINT8(67, sel().pitch);
  settle();
  f1(pbFor(2, 16, 0, 1));  // A again (the only note left at 96)
  TEST_ASSERT_EQUAL_UINT8(64, sel().pitch);
  settle();
  f4(70);
  TEST_ASSERT_EQUAL_UINT32(0, onsAt(64, 96));  // B stays hidden
  TEST_ASSERT_EQUAL_UINT32(2, onsAt(70, 96));
  tap(S::kEditModeNote);  // leave
  ms(400);
  TEST_ASSERT_EQUAL_UINT32(0, onsAt(64, 96));
}

// Undo restores the step's selection, and that note causes again.
void test_undo_restores_the_causing_note() {
  recordBar({{96, 60}, {96, 64}, {384, 67}});
  enterEdit();
  settle();
  f1(pbFor(2, 16, 0, 2));  // 60 @ 96
  TEST_ASSERT_EQUAL_UINT8(60, sel().pitch);
  settle();
  f4(64);  // A's step: B hidden
  TEST_ASSERT_EQUAL_UINT32(2, onsAt(64, 96));
  dbl(S::kRecordNote);  // undo A's step: A at 60 and B back, A still selected
  ms(800);
  TEST_ASSERT_EQUAL_UINT8(60, sel().pitch);
  TEST_ASSERT_EQUAL_UINT32(4, onsAt(60, 96) + onsAt(64, 96));
  settle();
  f4(64);
  TEST_ASSERT_EQUAL_UINT32(2, onsAt(64, 96));  // A causes again: B hidden
  f1(pbFor(8, 16));  // 67: A's results settle — they stay (sticky) when 67 is edited and that step undone
  settle();
  f4(68);
  dbl(S::kRecordNote);
  ms(800);
  TEST_ASSERT_EQUAL_UINT8(64, sel().pitch);  // the undone step's predecessor: A's, A selected
  TEST_ASSERT_EQUAL_UINT32(2, onsAt(64, 96));
}

// While playing, a move is heard in the bar it is made in (owner 2026-11-07), not from the next window: the first time
// the note's position comes round after the gesture it sounds at its new pitch — twice, a bar apart, so one of the two
// lies in a window's first bar (where the next boundary is a bar after the note).
void test_a_move_is_heard_in_the_bar_it_is_made() {
  recordBar({{96, 60}, {384, 67}});
  enterEdit();
  settle();
  f1(pbFor(8, 16));  // 67 @ 384
  TEST_ASSERT_EQUAL_UINT8(67, sel().pitch);
  settle();
  for (uint8_t k = 0; k < 2; ++k) {
    run(gUs + (768 - (gLast - gE.transport().origin(0)) % 768) * kTick);  // to the bar's start
    const uint32_t from = gHeardCount, at = gLast;
    f4(static_cast<uint8_t>(70 + 2 * k));
    run(gUs + 500 * kTick);  // past 384 in this bar
    uint8_t first = 0;
    for (uint32_t i = from; i < gHeardCount && first == 0; ++i)
      if (gHeard[i % 4096].tick > at && (gHeard[i % 4096].tick - gE.transport().origin(0)) % 768 == 384)
        first = gHeard[i % 4096].pitch;
    TEST_ASSERT_EQUAL_UINT8(70 + 2 * k, first);
    ms(400);
  }
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
  settle();
  f1(pbFor(2, 16));
  tap(S::kLengthNote);
  ms(450);
  TEST_ASSERT_TRUE(gS.edit().lengthMode());
  TEST_ASSERT_EQUAL_UINT32(96 + 40, gS.edit().bracket());  // the bracket at the note's end
  settle();  // F2 / F3 follow the switch to the end first
  f2(-8192 + 16384 * 400 / 767);  // the end to tick ~400
  const EditNote a = sel();
  TEST_ASSERT_UINT32_WITHIN(1, 400 - 96, a.length);
  TEST_ASSERT_EQUAL_UINT32(a.tick + a.length, gS.edit().bracket());  // the bracket follows the end
  ms(400);
  f2(-8192);  // the end to 0: before the start — kept a 16th after it
  TEST_ASSERT_EQUAL_UINT32(48, sel().length);
  settle();  // F3's motor follows the coarse move first
  f3(64 + 20);  // fine: the end anchor — the end the last coarse move left (144, as legacy) — + 20
  TEST_ASSERT_EQUAL_UINT32(164 - 96, sel().length);
  TEST_ASSERT_EQUAL_UINT32(164, gS.edit().bracket());
  tap(S::kLengthNote);  // back to start editing: the bracket at the start
  ms(450);
  TEST_ASSERT_FALSE(gS.edit().lengthMode());
  TEST_ASSERT_EQUAL_UINT32(96, gS.edit().bracket());
  tap(S::kLengthNote);  // length mode again; F1 at rest keeps it, an F1 move (a selection) leaves it
  ms(450);
  TEST_ASSERT_TRUE(gS.edit().lengthMode());
  f1(pbFor(2, 16));
  TEST_ASSERT_TRUE(gS.edit().lengthMode());
  f1(pbFor(3, 16));
  TEST_ASSERT_FALSE(gS.edit().lengthMode());
}

// Owner 2026-11-08 (not in legacy): F4 with NOTELEN held sets the velocity; that press is no short (length mode) or
// double (delete); on its release F4's motor goes back to the pitch.
void test_f4_with_notelen_held_sets_the_velocity() {
  recordBar({{96, 64}});
  enterEdit();
  settle();
  f1(pbFor(2, 16));
  settle();
  drainMotors(800);
  const uint8_t depth0 = gE.noteEdit().depth();
  gS.input(0x9F, S::kLengthNote, 127, gUs / 1000);  // hold
  ms(100);
  f4(30);
  ms(100);
  f4(1);
  TEST_ASSERT_EQUAL_UINT8(1, sel().velocity);
  TEST_ASSERT_EQUAL_UINT8(64, sel().pitch);
  ms(100);
  f4(90);
  gS.input(0x8F, S::kLengthNote, 0, gUs / 1000);  // release: no gesture
  const auto sent = drainMotors(800);
  TEST_ASSERT_FALSE(gS.edit().lengthMode());
  TEST_ASSERT_EQUAL_UINT8(90, sel().velocity);
  TEST_ASSERT_EQUAL_UINT8(64, sel().pitch);
  TEST_ASSERT_EQUAL_UINT8(depth0 + 1, gE.noteEdit().depth());  // the note's one step
  const Sent* back = findMsg(sent, 0xBE, 3);
  TEST_ASSERT_NOT_NULL(back);
  TEST_ASSERT_EQUAL_UINT8(64, back->m.d2);
  f4(64);  // the DROID: F4's motor arrives at the pitch
  TEST_ASSERT_FALSE(gS.edit().velocityMode());
  tap(S::kLengthNote);  // a press without F4: length mode as before
  ms(450);
  TEST_ASSERT_TRUE(gS.edit().lengthMode());
  TEST_ASSERT_FALSE(gS.edit().velocityMode());
  tap(S::kLengthNote);
  ms(450);
  drainMotors(800);
  // A long hold, the hand not on F4: velocity mode, F4's motor to the velocity; on the release back to the pitch.
  gS.input(0x9F, S::kLengthNote, 127, gUs / 1000);
  ms(S::kVelocityHoldMs - 50);
  TEST_ASSERT_FALSE(gS.edit().velocityMode());
  const auto held = drainMotors(100);
  TEST_ASSERT_TRUE(gS.edit().velocityMode());
  const Sent* toVel = findMsg(held, 0xBE, 3);
  TEST_ASSERT_NOT_NULL(toVel);
  TEST_ASSERT_EQUAL_UINT8(90, toVel->m.d2);
  drainMotors(800);
  f4(50);  // ...then moved by hand
  TEST_ASSERT_EQUAL_UINT8(50, sel().velocity);
  gS.input(0x8F, S::kLengthNote, 0, gUs / 1000);
  const auto released = drainMotors(800);
  TEST_ASSERT_FALSE(gS.edit().velocityMode());
  TEST_ASSERT_FALSE(gS.edit().lengthMode());  // the long hold toggled nothing
  const Sent* toPitch = findMsg(released, 0xBE, 3);
  TEST_ASSERT_NOT_NULL(toPitch);
  TEST_ASSERT_EQUAL_UINT8(64, toPitch->m.d2);
}

// F4 at 0 with NOTELEN held mutes the note (owner 2026-11-08): it keeps its velocity, is not heard — neither while
// editing nor after leaving — and is gathered and selectable again; a long hold writes F4's motor to 0; any other value
// unmutes it there.
void test_f4_at_zero_mutes_the_note() {
  recordBar({{96, 64}, {384, 67}});
  enterEdit();
  settle();
  f1(pbFor(2, 16));
  settle();
  drainMotors(800);
  gS.input(0x9F, S::kLengthNote, 127, gUs / 1000);  // hold
  ms(100);
  f4(30);
  ms(100);
  f4(0);
  TEST_ASSERT_EQUAL_UINT8(kVelocityMuted | 30, sel().velocity);
  gS.input(0x8F, S::kLengthNote, 0, gUs / 1000);
  drainMotors(800);
  f4(64);  // F4's motor arrives at the pitch
  TEST_ASSERT_EQUAL_UINT32(0, onsAt(64, 96));
  TEST_ASSERT_EQUAL_UINT32(2, onsAt(67, 384));
  tap(S::kEditModeNote);  // leave
  ms(400);
  TEST_ASSERT_EQUAL_UINT32(0, onsAt(64, 96));
  TEST_ASSERT_EQUAL_UINT32(2, onsAt(67, 384));
  enterEdit();
  settle();
  f1(pbFor(2, 16));
  TEST_ASSERT_EQUAL_UINT8(64, sel().pitch);  // still there
  TEST_ASSERT_EQUAL_UINT8(kVelocityMuted | 30, sel().velocity);
  settle();
  drainMotors(800);
  gS.input(0x9F, S::kLengthNote, 127, gUs / 1000);  // long hold: F4's motor to 0
  ms(S::kVelocityHoldMs - 50);
  const auto held = drainMotors(100);
  const Sent* toVel = findMsg(held, 0xBE, 3);
  TEST_ASSERT_NOT_NULL(toVel);
  TEST_ASSERT_EQUAL_UINT8(0, toVel->m.d2);
  drainMotors(800);
  f4(100);  // unmuted, at 100
  TEST_ASSERT_EQUAL_UINT8(100, sel().velocity);
  gS.input(0x8F, S::kLengthNote, 0, gUs / 1000);
  drainMotors(800);
  f4(64);
  TEST_ASSERT_EQUAL_UINT32(2, onsAt(64, 96));
}

/// Track 1's slot 8, one bar recorded while track 0 plays: `notes` (positions from the take start, 40 ticks each).
static void recordTrack1(std::initializer_list<std::pair<uint32_t, uint8_t>> notes) {
  static Live live[64];
  tap(S::kTrackBase + 1);
  ms(400);
  tap(S::kSlotBase + 0);  // armed: the take starts at the next bar
  for (int i = 0; i < 4000 && gE.transport().mode() != ProductionTransport::Mode::Recording; ++i) ms(1);
  TEST_ASSERT_EQUAL(ProductionTransport::Mode::Recording, gE.transport().mode());
  const uint32_t t0 = gUs - ((gLast - gE.transport().origin(0)) % 768) * kTick;  // at track 0's bar line
  uint32_t n = 0;
  for (const auto& p : notes) live[n++] = {t0 + p.first * kTick + kTick / 2, t0 + (p.first + 40) * kTick, p.second};
  gLive = live;
  gLiveCount = n;
  run(t0 + kBarUs - 200 * kTick);
  tap(S::kSlotBase + 0);  // the take ends at the bar
  run(t0 + kBarUs + 50 * kTick);
  gLive = nullptr;
  gLiveCount = 0;
  TEST_ASSERT_TRUE(gE.transport().contentOf(8) != kNoManifest);
}

// NOTE_EDIT follows the track (owner 2026-11-08): a track select leaves the session — its edits one undo step on its
// slot — and opens NOTE_EDIT on the new track's slot, or ends it when that slot is empty; outside NOTE_EDIT a select
// opens nothing.
void test_note_edit_follows_the_track() {
  recordBar({{96, 64}});
  recordTrack1({{384, 72}});
  tap(S::kTrackBase + 0);
  ms(400);
  TEST_ASSERT_FALSE(gS.edit().active());
  const uint8_t undo0 = gE.history().undoDepth(0);
  enterEdit();
  settle();
  f1(pbFor(2, 16));
  TEST_ASSERT_EQUAL_UINT8(64, sel().pitch);
  settle();
  f4(70);
  tap(S::kTrackBase + 1);  // to track 1: NOTE_EDIT on slot 8
  ms(400);
  for (int i = 0; i < 50 && !gS.edit().editing(); ++i) ms(5);
  TEST_ASSERT_TRUE(gS.edit().editing());
  TEST_ASSERT_EQUAL_UINT8(8, gS.edit().slot());
  TEST_ASSERT_EQUAL_UINT8(undo0 + 1, gE.history().undoDepth(0));  // track 0's edits: one step
  settle();
  f1(pbFor(8, 16));
  TEST_ASSERT_EQUAL_UINT8(72, sel().pitch);
  TEST_ASSERT_EQUAL_UINT32(96, heardAt(70));  // track 0's edit kept
  tap(S::kTrackBase + 2);  // an empty slot: NOTE_EDIT ends
  ms(400);
  TEST_ASSERT_FALSE(gS.edit().active());
  tap(S::kTrackBase + 0);  // outside NOTE_EDIT: nothing opens
  ms(400);
  TEST_ASSERT_FALSE(gS.edit().active());
  enterEdit();
  tap(S::kTrackSwitchNote);  // the track switch button follows too: track 1
  ms(500);
  for (int i = 0; i < 50 && !gS.edit().editing(); ++i) ms(5);
  TEST_ASSERT_TRUE(gS.edit().editing());
  TEST_ASSERT_EQUAL_UINT8(8, gS.edit().slot());
}

// NOTELEN short on an empty 16th creates a note there (owner 2026-11-08: there is no note to lengthen); on a note it
// still toggles length mode.
void test_notelen_short_on_an_empty_step_creates() {
  recordBar({{96, 64}});
  enterEdit();
  settle();
  f1(pbFor(12, 16));  // an empty 16th (576)
  EditNote n;
  TEST_ASSERT_FALSE(gS.edit().selected(n));
  const uint16_t size = gE.noteEdit().size();
  tap(S::kLengthNote);
  ms(450);
  TEST_ASSERT_EQUAL_UINT16(size + 1, gE.noteEdit().size());
  TEST_ASSERT_EQUAL_UINT8(60, sel().pitch);
  TEST_ASSERT_EQUAL_UINT32(576, sel().tick);
  TEST_ASSERT_FALSE(gS.edit().lengthMode());
  tap(S::kLengthNote);  // the new note is selected: length mode
  ms(450);
  TEST_ASSERT_TRUE(gS.edit().lengthMode());
  TEST_ASSERT_EQUAL_UINT16(size + 1, gE.noteEdit().size());
}

// A NOTELEN long press released without F4 moved by hand creates a note at the bracket (owner 2026-11-08); F4's motor
// arriving at the velocity is not a move; moved by hand, the hold edits the velocity and creates nothing.
void test_notelen_long_press_creates() {
  recordBar({{96, 64}});
  enterEdit();
  settle();
  f1(pbFor(12, 16));  // an empty 16th (576)
  settle();
  const uint16_t size = gE.noteEdit().size();
  gS.input(0x9F, S::kLengthNote, 127, gUs / 1000);
  ms(S::kVelocityHoldMs + 100);
  gS.input(0x8F, S::kLengthNote, 0, gUs / 1000);
  ms(450);
  TEST_ASSERT_EQUAL_UINT16(size + 1, gE.noteEdit().size());
  TEST_ASSERT_EQUAL_UINT32(576, sel().tick);
  TEST_ASSERT_FALSE(gS.edit().lengthMode());
  // On the new note: the motor goes to its velocity and lands there — still a long press; a note is there already.
  settle();
  drainMotors(800);
  gS.input(0x9F, S::kLengthNote, 127, gUs / 1000);
  ms(S::kVelocityHoldMs + 50);
  drainMotors(100);
  f4(80);  // the DROID: F4's motor arrives at the velocity
  gS.input(0x8F, S::kLengthNote, 0, gUs / 1000);
  ms(450);
  TEST_ASSERT_EQUAL_UINT16(size + 1, gE.noteEdit().size());
  TEST_ASSERT_EQUAL_UINT8(80, sel().velocity);
  // Moved by hand: velocity, no note.
  f1(pbFor(14, 16));  // another empty 16th (672)
  settle();
  drainMotors(800);
  gS.input(0x9F, S::kLengthNote, 127, gUs / 1000);
  ms(S::kVelocityHoldMs + 50);
  f4(30);
  gS.input(0x8F, S::kLengthNote, 0, gUs / 1000);
  ms(450);
  TEST_ASSERT_EQUAL_UINT16(size + 1, gE.noteEdit().size());
}

// A long press on an empty slot creates an empty 1-bar loop (owner 2026-11-08), selected and not played; NOTE_EDIT
// opens on it, a step is entered with NOTELEN, and it plays; Undo takes the loop back.
void test_slot_long_press_creates_an_empty_loop() {
  TEST_ASSERT_TRUE(gE.transport().contentOf(0) == kNoManifest);
  gS.input(0x9F, S::kSlotBase + 0, 127, gUs / 1000);
  ms(700);
  gS.input(0x8F, S::kSlotBase + 0, 0, gUs / 1000);
  ms(100);
  const ManifestId m = gE.transport().contentOf(0);
  TEST_ASSERT_TRUE(m != kNoManifest);
  TEST_ASSERT_EQUAL_UINT32(768, gE.manifests().manifest(m).loopLengthTicks);
  TEST_ASSERT_EQUAL_UINT16(1, gE.runs().run(gE.manifests().manifest(m).runs[0]).refs);  // the manifest's alone
  TEST_ASSERT_FALSE(gE.transport().playing(0));
  TEST_ASSERT_EQUAL_UINT8(1, gE.history().undoDepth(0));
  enterEdit();
  TEST_ASSERT_EQUAL_UINT16(0, gE.noteEdit().size());
  settle();
  f1(pbFor(4, 16));  // 192
  tap(S::kLengthNote);
  ms(450);
  TEST_ASSERT_EQUAL_UINT16(1, gE.noteEdit().size());
  TEST_ASSERT_EQUAL_UINT32(192, sel().tick);
  tap(S::kEditModeNote);  // leave
  ms(400);
  tap(S::kPlayStopNote);  // the selected slot plays
  ms(500);
  TEST_ASSERT_TRUE(gE.transport().playing(0));
  TEST_ASSERT_EQUAL_UINT32(192, heardAt(60));
}

// The edit button cycles NOTE_EDIT ↔ LOOP_EDIT and its long press leaves (owner 2026-11-08); from performance an empty
// slot opens LOOP_EDIT, where NOTELEN creates an empty loop; NOTE_EDIT then opens on it.
void test_edit_button_cycles_note_and_loop_edit() {
  tap(S::kEditModeNote);  // slot 0 empty: LOOP_EDIT
  ms(400);
  TEST_ASSERT_TRUE(gS.loopEdit());
  TEST_ASSERT_FALSE(gS.edit().active());
  tap(S::kEditModeNote);  // still empty: stays in LOOP_EDIT
  ms(400);
  TEST_ASSERT_TRUE(gS.loopEdit());
  tap(S::kLengthNote);  // an empty loop
  ms(450);
  TEST_ASSERT_TRUE(gE.transport().contentOf(0) != kNoManifest);
  tap(S::kEditModeNote);  // NOTE_EDIT on it
  ms(400);
  for (int i = 0; i < 50 && !gS.edit().editing(); ++i) ms(5);
  TEST_ASSERT_TRUE(gS.edit().editing());
  TEST_ASSERT_FALSE(gS.loopEdit());
  tap(S::kEditModeNote);  // LOOP_EDIT
  ms(400);
  TEST_ASSERT_FALSE(gS.edit().active());
  TEST_ASSERT_TRUE(gS.loopEdit());
  tap(S::kLengthNote);  // a filled slot: no second loop
  ms(450);
  TEST_ASSERT_EQUAL_UINT8(1, gE.history().undoDepth(0));
  tap(S::kEditModeNote, 700);  // long: performance
  ms(400);
  TEST_ASSERT_FALSE(gS.loopEdit());
  TEST_ASSERT_FALSE(gS.edit().active());
}

// LOOP_EDIT's F4 sets the tempo (legacy: pitch bend on channel 15, 1–300 BPM in tenths over the whole travel); on
// entry its motor is parked at the tempo, and that landing changes nothing.
void test_loop_edit_f4_sets_the_tempo() {
  tap(S::kEditModeNote);  // slot 0 empty: LOOP_EDIT
  ms(400);
  TEST_ASSERT_TRUE(gS.loopEdit());
  const auto sent = drainMotors(100);
  const Sent* park = findStatus(sent, 0xEE);
  TEST_ASSERT_NOT_NULL(park);
  const uint16_t wire = static_cast<uint16_t>(park->m.d1 | (park->m.d2 << 7));
  TEST_ASSERT_EQUAL_UINT16(TempoMapping::pitchBendWireFromBpm(120.0f), wire);
  const uint32_t us120 = gClk.usPerTick();
  const uint16_t near = static_cast<uint16_t>(wire + 300);  // the motor lands a little off
  gS.input(0xEE, near & 0x7F, near >> 7, gUs / 1000);
  TEST_ASSERT_EQUAL_UINT32(us120, gClk.usPerTick());
  ms(600);
  const uint16_t w90 = TempoMapping::pitchBendWireFromBpm(90.0f);
  gS.input(0xEE, w90 & 0x7F, w90 >> 7, gUs / 1000);
  TEST_ASSERT_EQUAL_UINT32(6000000000ull / (9000ull * 192), gClk.usPerTick());
  tap(S::kEditModeNote, 700);  // performance: F4's pitch bend is not the tempo
  ms(400);
  const uint16_t w60 = TempoMapping::pitchBendWireFromBpm(60.0f);
  gS.input(0xEE, w60 & 0x7F, w60 >> 7, gUs / 1000);
  TEST_ASSERT_EQUAL_UINT32(6000000000ull / (9000ull * 192), gClk.usPerTick());
}

struct NoteSpec {
  uint32_t pos, length;
  uint8_t pitch;
};
/// An overdub on slot 0 (playing, one bar): `notes` at loop positions from the next loop start, held for their lengths —
/// across the loop end too (their NoteOffs in the closing run's wrap section).
static void overdubHeld(std::initializer_list<NoteSpec> notes) {
  static Live live[64];
  tap(S::kRecordNote);  // a playing track overdubs at once
  ms(450);
  TEST_ASSERT_EQUAL(ProductionTransport::Mode::Overdubbing, gE.transport().mode());
  const uint32_t pos = (gLast - gE.transport().origin(0)) % 768;
  const uint32_t t0 = gUs + (768 - pos) * kTick;
  uint32_t n = 0, last = 0;
  for (const auto& x : notes) {
    live[n++] = {t0 + x.pos * kTick + kTick / 2, t0 + (x.pos + x.length) * kTick + kTick / 2, x.pitch};
    if (x.pos + x.length > last) last = x.pos + x.length;
  }
  gLive = live;
  gLiveCount = n;
  run(t0 + (last + 20) * kTick);
  gLive = nullptr;
  gLiveCount = 0;
  tap(S::kRecordNote);  // the overdub ends
  ms(450);
  run(gUs + kBarUs);
  TEST_ASSERT_EQUAL(ProductionTransport::Mode::Idle, gE.transport().mode());
}
/// NoteOns (`ons`) or NoteOffs of `pitch` over the next `loops` loops of `loop` ticks: their positions in the loop.
static std::vector<uint32_t> positions(bool ons, uint8_t pitch, uint32_t loop, uint32_t loops = 2) {
  const uint32_t from = ons ? gHeardCount : gOffCount;
  run(gUs + loops * loop * kTick);
  std::vector<uint32_t> v;
  const Heard* h = ons ? gHeard : gOff;
  const uint32_t to = ons ? gHeardCount : gOffCount;
  for (uint32_t i = from; i < to; ++i)
    if (h[i % 4096].pitch == pitch) v.push_back((h[i % 4096].tick - gE.transport().origin(0)) % loop);
  return v;
}
static char gPosText[256];
static bool all(const std::vector<uint32_t>& v, uint32_t pos) {
  int k = snprintf(gPosText, sizeof gPosText, "want %u got", static_cast<unsigned>(pos));
  for (uint32_t x : v)
    if (k < 240) k += snprintf(gPosText + k, sizeof gPosText - k, " %u", static_cast<unsigned>(x));
  if (v.empty()) return false;
  for (uint32_t x : v)
    if (x != pos) return false;
  return true;
}
#define EXPECT_ALL(v, pos) TEST_ASSERT_TRUE_MESSAGE(all(v, pos), gPosText)
static ProductionTransport::Result resizeTo(uint32_t loop) {
  const auto r = gE.control().resize(0, loop, gLast, gUs, gClk.usPerTick());
  run(gUs + 2 * 768 * kTick);  // the playing track follows from the next boundary
  return r;
}

// Loop length is a play window (owner 2026-11-08, DEC-257): lengthened, a note that wrapped ends at its recorded length
// inside the longer loop (not stretched); shortened, a note crossing the new end wraps, keeping its length, and notes
// past the end are kept but not played — lengthened again they play as recorded. Undo restores the length.
void test_resize_rewraps_notes_and_keeps_those_past_the_end() {
  recordBar({{96, 60}});
  overdubHeld({{300, 200, 64}, {600, 300, 62}});
  TEST_ASSERT_TRUE(gE.transport().playing(0));
  EXPECT_ALL(positions(false, 62, 768), 132);  // recorded: 600 + 300 wraps to 132
  TEST_ASSERT_EQUAL(ProductionTransport::Result::Ok, resizeTo(1536));
  EXPECT_ALL(positions(true, 62, 1536), 600);
  EXPECT_ALL(positions(false, 62, 1536), 900);  // resolved inside the longer loop
  EXPECT_ALL(positions(true, 60, 1536), 96);
  TEST_ASSERT_EQUAL(ProductionTransport::Result::Ok, resizeTo(384));
  TEST_ASSERT_TRUE(positions(true, 62, 384).empty());  // past the end: not played
  TEST_ASSERT_TRUE(positions(false, 62, 384).empty());
  EXPECT_ALL(positions(true, 64, 384), 300);
  EXPECT_ALL(positions(false, 64, 384), 116);  // 300 + 200 wraps at 384
  EXPECT_ALL(positions(true, 60, 384), 96);
  TEST_ASSERT_EQUAL(ProductionTransport::Result::Ok, resizeTo(768));  // back: kept notes play again
  EXPECT_ALL(positions(true, 62, 768), 600);
  EXPECT_ALL(positions(false, 62, 768), 132);
  EXPECT_ALL(positions(false, 64, 768), 500);
  const ManifestId m = gE.transport().contentOf(0);
  TEST_ASSERT_EQUAL_UINT32(768, gE.manifests().manifest(m).loopLengthTicks);
  gE.control().undo(0, gLast, gUs, gClk.usPerTick());
  TEST_ASSERT_EQUAL_UINT32(384, gE.manifests().manifest(gE.transport().contentOf(0)).loopLengthTicks);
}

// A note held for a whole loop (Q-LEN-1: re-struck every loop) keeps its recorded length once the loop is longer than
// it: lengthened, it ends at its NoteOff inside the loop instead of at its next re-strike.
void test_resize_resolves_a_held_loop_note() {
  recordBar({{96, 60}});
  overdubHeld({{100, 800, 65}});
  EXPECT_ALL(positions(true, 65, 768), 100);  // re-struck every loop
  TEST_ASSERT_EQUAL(ProductionTransport::Result::Ok, resizeTo(1536));
  EXPECT_ALL(positions(true, 65, 1536), 100);
  EXPECT_ALL(positions(false, 65, 1536), 900);
}

// LOOP_EDIT's F2 sets the loop length (legacy: pitch bend channel 14, 1–128 bars; its motor CC 101 on channel 16): moves
// within the settle time are one resize, one undo step; notes past the shorter end come back when lengthened.
void test_loop_edit_f2_sets_the_length() {
  recordBar({{96, 60}, {600, 62}});
  const uint8_t undo0 = gE.history().undoDepth(0);
  tap(S::kEditModeNote);  // NOTE_EDIT
  ms(400);
  tap(S::kEditModeNote);  // LOOP_EDIT
  ms(400);
  TEST_ASSERT_TRUE(gS.loopEdit());
  const auto sent = drainMotors(100);
  const Sent* park = findMsg(sent, 0xBF, 101);
  TEST_ASSERT_NOT_NULL(park);
  TEST_ASSERT_EQUAL_UINT8(0, park->m.d2);  // 1 bar
  const auto f2 = [](uint32_t bars) {
    const int32_t pb = -8192 + static_cast<int32_t>((bars - 1) * 16383 / 127) + 64;
    pitchBend(13, pb);
    ms(5);
  };
  f2(2);  // the motor landing: no change
  TEST_ASSERT_EQUAL_UINT16(0, gS.pendingLoopBars());
  ms(600);
  f2(2);
  TEST_ASSERT_EQUAL_UINT16(2, gS.pendingLoopBars());
  f2(4);
  ms(400);
  TEST_ASSERT_EQUAL_UINT16(0, gS.pendingLoopBars());
  TEST_ASSERT_EQUAL_UINT32(4 * 768, gE.manifests().manifest(gE.transport().contentOf(0)).loopLengthTicks);
  TEST_ASSERT_EQUAL_UINT8(undo0 + 1, gE.history().undoDepth(0));
  f2(1);  // back to a bar: a second step
  ms(400);
  TEST_ASSERT_EQUAL_UINT32(768, gE.manifests().manifest(gE.transport().contentOf(0)).loopLengthTicks);
  TEST_ASSERT_EQUAL_UINT8(undo0 + 2, gE.history().undoDepth(0));
}

void test_no_note_is_created_next_to_a_note() {
  recordBar({{380, 69}});
  enterEdit();
  settle();
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
  tap(S::kSlotBase + 0);  // armed
  ms(400);
  tap(S::kSlotBase + 0);  // the clock starts and the take with it
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
  RUN_TEST(test_geometry_faders_move_and_pitch_heard_one_step_a_note);
  RUN_TEST(test_f1_at_rest_keeps_the_moved_note_selected);
  RUN_TEST(test_the_droid_fader_bank_follows_note_edit);
  RUN_TEST(test_motor_faders_follow_the_selection_and_their_echo_changes_nothing);
  RUN_TEST(test_f2_back_at_the_start_keeps_the_bracket_on_the_note);
  RUN_TEST(test_f2_keeps_the_offset_f3_set_past_the_16th);
  RUN_TEST(test_a_pending_write_gives_way_to_a_fader_already_there);
  RUN_TEST(test_pitch_onto_a_note_hides_it_and_deselect_settles);
  RUN_TEST(test_undo_restores_the_causing_note);
  RUN_TEST(test_a_move_is_heard_in_the_bar_it_is_made);
  RUN_TEST(test_geometry_waits_the_grace_period_after_entry);
  RUN_TEST(test_length_mode_sets_the_end_at_least_a_sixteenth);
  RUN_TEST(test_notelen_double_deletes_and_creates_and_leave_keeps_one_step);
  RUN_TEST(test_f4_with_notelen_held_sets_the_velocity);
  RUN_TEST(test_f4_at_zero_mutes_the_note);
  RUN_TEST(test_note_edit_follows_the_track);
  RUN_TEST(test_notelen_short_on_an_empty_step_creates);
  RUN_TEST(test_notelen_long_press_creates);
  RUN_TEST(test_slot_long_press_creates_an_empty_loop);
  RUN_TEST(test_edit_button_cycles_note_and_loop_edit);
  RUN_TEST(test_loop_edit_f4_sets_the_tempo);
  RUN_TEST(test_resize_rewraps_notes_and_keeps_those_past_the_end);
  RUN_TEST(test_resize_resolves_a_held_loop_note);
  RUN_TEST(test_loop_edit_f2_sets_the_length);
  RUN_TEST(test_no_note_is_created_next_to_a_note);
  RUN_TEST(test_a_window_denser_than_the_selection_is_halved);
  RUN_TEST(test_no_heap);
  return UNITY_END();
}
