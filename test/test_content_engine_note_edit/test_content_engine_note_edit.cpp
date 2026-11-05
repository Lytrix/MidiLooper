//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// fixed-memory-engine E9.5: NOTE_EDIT through `NoteEditSession` on the production root, heard on the USB device port.
// The selection is what the piano roll shows in a range — a note wholly occluded by a later pass is not selectable —
// with stored geometry (a note shortened by a later overdub keeps its recorded length); an edit is heard from the next
// boundary after its rebuild; undo / redo return each step's focus token; delete; leave keeps one U: step, abandon
// returns to the start; a range of more than 512 notes is refused by name; no allocation.

#include <unity.h>

#include <cstdint>
#include <cstdlib>
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
using G = ProductionNoteEdit::Gather;
using Sent = ProductionHub::Sent;

static EnginePsram gPsram;
static EngineRam2 gRam2;
static EngineRoot gE;

constexpr uint32_t kUsPerTick = 1040;
constexpr uint32_t kBar = kTicksPerBar;
constexpr uint32_t kLoop = 4 * kBar;

struct Wire {
  uint32_t clock;
  Sent s;
};
static std::vector<Wire> gWire;
static uint32_t gClock = 0;

void setUp() {
  TEST_ASSERT_TRUE(gE.init(gPsram, gRam2, 2000));
  gWire.clear();
  gWire.reserve(30000);
  gClock = 0;
}
void tearDown() {}

static uint32_t us(uint32_t c) { return c * kUsPerTick; }
struct Live {
  uint32_t on, off;
  uint8_t pitch;
};
static void run(uint32_t to, const std::vector<Live>& live = {}) {
  Sent buf[32];
  for (; gClock < to; ++gClock) {
    gE.submitJobs(gClock, us(gClock), kUsPerTick);
    while (gE.runGrain(us(gClock))) {
    }
    gE.clockTick(gClock, us(gClock), kUsPerTick);
    for (uint32_t step = 0; step < kUsPerTick; step += 125) {
      const uint8_t n = gE.transmit(kPortUsbDevice, us(gClock) + step, buf, 32);
      for (uint8_t i = 0; i < n; ++i) gWire.push_back({gClock, buf[i]});
    }
    for (const Live& l : live) {
      if (l.off == gClock) gE.control().inputNoteOff(gClock, 0x80, l.pitch, us(gClock));
      if (l.on == gClock) gE.control().inputNoteOn(gClock, 0x90, l.pitch, 100, us(gClock), kUsPerTick);
    }
  }
}
/// Positions (in the track's loop) of NoteOns of `pitch` sent in [from, to).
static std::vector<uint32_t> positions(uint8_t pitch, uint32_t from, uint32_t to) {
  std::vector<uint32_t> v;
  for (const Wire& w : gWire)
    if ((w.s.status & 0xF0) == 0x90 && w.s.data1 == pitch && w.clock >= from && w.clock < to)
      v.push_back((w.clock - gE.transport().origin(0)) % kLoop);
  return v;
}
static void gather(const ProductionNoteEdit::Query& q, uint32_t focus = 0) {
  TEST_ASSERT_EQUAL(G::Running, gE.noteEdit().request(q, focus));
  run(gClock + 20);
  TEST_ASSERT_FALSE(gE.noteEdit().gatherPending());
}
static const EditNote* find(uint8_t pitch) {
  for (uint16_t i = 0; i < gE.noteEdit().count(); ++i)
    if (gE.noteEdit().note(i).pitch == pitch) return &gE.noteEdit().note(i);
  return nullptr;
}
static int indexOf(uint8_t pitch) {
  for (uint16_t i = 0; i < gE.noteEdit().count(); ++i)
    if (gE.noteEdit().note(i).pitch == pitch) return i;
  return -1;
}

/// Slot 0: a take (U 66 [0, 50), P 60 [100, 600), R 62 [1000, 1100), T 64 [2000, 2100)) overdubbed once (Q 60
/// [300, 400): P sounds until 299; S 62 [1000, 1050): R is hidden, same start; V 66 held from 3000 across the loop end
/// to 100: from the steady cycle on it sounds at position 0, so U, starting under it, is hidden there — not in cycle 0).
static void takeAndOverdub() {
  gE.control().selectTrack(0);
  TEST_ASSERT_EQUAL(R::Ok, gE.control().queueRecord(0, 0, 0));
  const uint32_t s = kBar;
  run(s + kLoop - 50, {{s, s + 50, 66}, {s + 100, s + 600, 60}, {s + 1000, s + 1100, 62}, {s + 2000, s + 2100, 64}});
  TEST_ASSERT_EQUAL(R::Ok, gE.control().queueStop(0, gClock));
  run(s + kLoop + 1);
  run(s + kLoop + 50);
  TEST_ASSERT_EQUAL(R::Ok, gE.control().startOverdub(0, gClock));
  const uint32_t c1 = s + kLoop;
  run(c1 + kLoop + 500, {{c1 + 300, c1 + 400, 60}, {c1 + 1000, c1 + 1050, 62}, {c1 + 3000, c1 + kLoop + 100, 66}});
  TEST_ASSERT_EQUAL(R::Ok, gE.control().stopNow(gClock, us(gClock), kUsPerTick));
  run(gClock + 2 * kLoop);
}

// ---- Tests -------------------------------------------------------------------------------------------------------------
void test_the_selection_is_what_shows_with_stored_geometry() {
  takeAndOverdub();
  gather({0, 0, kLoop, 0, 127});
  TEST_ASSERT_EQUAL(G::Done, gE.noteEdit().status());
  const EditNote* p = find(60);
  TEST_ASSERT_NOT_NULL(p);
  TEST_ASSERT_EQUAL_UINT32(100, p->tick);
  TEST_ASSERT_EQUAL_UINT32(500, p->length);  // recorded, not the 199 heard (Q shortens it at composition)
  uint8_t sixtyTwo = 0;
  for (uint16_t i = 0; i < gE.noteEdit().count(); ++i) sixtyTwo += gE.noteEdit().note(i).pitch == 62 ? 1 : 0;
  TEST_ASSERT_EQUAL_UINT8(1, sixtyTwo);  // S; R is wholly occluded and not selectable
  TEST_ASSERT_EQUAL_UINT32(50, find(62)->length);
  TEST_ASSERT_EQUAL_UINT16(5, gE.noteEdit().count());  // P, Q, S, T, V: R and U are not shown in the steady cycle
  const EditNote* v = find(66);
  TEST_ASSERT_NOT_NULL(v);
  TEST_ASSERT_EQUAL_UINT32(3000, v->tick);
  TEST_ASSERT_EQUAL_UINT32(kLoop + 100 - 3000, v->length);  // stored across the loop end
  // Range and pitch filters, each alone.
  gather({0, 1500, kLoop, 0, 127});
  TEST_ASSERT_EQUAL_UINT16(2, gE.noteEdit().count());  // T, V
  gather({0, 0, kLoop, 63, 65});
  TEST_ASSERT_EQUAL_UINT16(1, gE.noteEdit().count());
  TEST_ASSERT_EQUAL_UINT8(64, gE.noteEdit().note(0).pitch);
}

void test_an_edit_is_heard_and_undo_redo_return_its_focus() {
  takeAndOverdub();
  gather({0, 0, kLoop, 0, 127}, /*focus at entry*/ 7);
  const uint8_t before = gE.history().undoDepth(0);
  TEST_ASSERT_EQUAL(EditResult::Ok, gE.noteEdit().enter());
  const int t = indexOf(64);
  EditNote moved = gE.noteEdit().note(static_cast<uint16_t>(t));
  moved.tick = 2400;
  const EditChange c{static_cast<uint16_t>(t), moved};
  TEST_ASSERT_EQUAL(EditResult::Ok, gE.noteEdit().apply(&c, 1, 11));
  const uint32_t at = gClock;
  run(at + 2 * kLoop + 10);
  auto pos = positions(64, at + kLoop, at + 2 * kLoop);
  TEST_ASSERT_EQUAL_UINT32(1, pos.size());
  TEST_ASSERT_EQUAL_UINT32(2400, pos[0]);  // heard where it was moved
  uint32_t focus = 0;
  TEST_ASSERT_EQUAL(EditResult::Ok, gE.noteEdit().undo(focus));
  TEST_ASSERT_EQUAL_UINT32(7, focus);  // the entry focus
  const uint32_t u = gClock;
  run(u + 2 * kLoop + 10);
  pos = positions(64, u + kLoop, u + 2 * kLoop);
  TEST_ASSERT_EQUAL_UINT32(2000, pos[0]);
  TEST_ASSERT_EQUAL(EditResult::Ok, gE.noteEdit().redo(focus));
  TEST_ASSERT_EQUAL_UINT32(11, focus);
  const uint32_t r = gClock;
  run(r + 2 * kLoop + 10);
  TEST_ASSERT_EQUAL_UINT32(2400, positions(64, r + kLoop, r + 2 * kLoop)[0]);
  // Leave: one U: step on the slot; Undo of the slot brings the note back to 2000.
  gE.noteEdit().leave();
  run(gClock + 20);
  TEST_ASSERT_FALSE(gE.noteEdit().editing());
  TEST_ASSERT_EQUAL_UINT8(before + 1, gE.history().undoDepth(0));
  TEST_ASSERT_EQUAL(R::Ok, gE.control().undo(0, gClock, us(gClock), kUsPerTick));
  const uint32_t z = gClock;
  run(z + 2 * kLoop + 10);
  TEST_ASSERT_EQUAL_UINT32(2000, positions(64, z + kLoop, z + 2 * kLoop)[0]);
}

void test_delete_and_abandon() {
  takeAndOverdub();
  gather({0, 0, kLoop, 0, 127});
  TEST_ASSERT_EQUAL(EditResult::Ok, gE.noteEdit().enter());
  const int t = indexOf(64);
  EditNote gone = gE.noteEdit().note(static_cast<uint16_t>(t));
  gone.deleted = 1;
  // One selection owner (owner 2026-11-05): no new selection while the session is open.
  TEST_ASSERT_EQUAL(G::Refused, gE.noteEdit().request({0, 0, kLoop, 0, 127}, 0));
  TEST_ASSERT_EQUAL_UINT16(5, gE.noteEdit().count());  // the session's selection is untouched
  const EditChange c{static_cast<uint16_t>(t), gone};
  TEST_ASSERT_EQUAL(EditResult::Ok, gE.noteEdit().apply(&c, 1, 0));
  const uint32_t at = gClock;
  run(at + 2 * kLoop + 10);
  TEST_ASSERT_EQUAL_UINT32(0, positions(64, at + kLoop, at + 2 * kLoop).size());
  gE.noteEdit().abandon();  // back to the start: T plays again
  const uint32_t ab = gClock;
  run(ab + 2 * kLoop + 10);
  TEST_ASSERT_EQUAL_UINT32(2000, positions(64, ab + kLoop, ab + 2 * kLoop)[0]);
  TEST_ASSERT_FALSE(gE.noteEdit().editing());
}

void test_more_than_512_notes_is_refused_by_name() {
  gE.control().selectTrack(0);
  TEST_ASSERT_EQUAL(R::Ok, gE.control().queueRecord(0, 0, 0));
  std::vector<Live> many;
  for (uint32_t k = 0; k < 520; ++k) many.push_back({kBar + 5 * k + 1, kBar + 5 * k + 3, static_cast<uint8_t>(30 + k % 60)});
  run(kBar + kLoop - 10, many);
  TEST_ASSERT_EQUAL(R::Ok, gE.control().queueStop(0, gClock));
  run(kBar + kLoop + kLoop);
  TEST_ASSERT_EQUAL(G::Running, gE.noteEdit().request({0, 0, kLoop, 0, 127}, 0));
  run(gClock + 30);
  TEST_ASSERT_EQUAL(G::TooMany, gE.noteEdit().status());
  TEST_ASSERT_EQUAL_UINT16(0, gE.noteEdit().count());
  TEST_ASSERT_EQUAL(EditResult::DeniedSelection, gE.noteEdit().enter());
}

void test_no_heap() {
  takeAndOverdub();
  gTrap = true;
  const unsigned long before = gHeapAllocations;
  gE.noteEdit().request({0, 0, kLoop, 0, 127}, 1);
  run(gClock + 20);
  gE.noteEdit().enter();
  EditNote n = gE.noteEdit().note(0);
  n.velocity = 50;
  const EditChange c{0, n};
  gE.noteEdit().apply(&c, 1, 2);
  run(gClock + 200);
  uint32_t f;
  gE.noteEdit().undo(f);
  gE.noteEdit().leave();
  run(gClock + 200);
  gTrap = false;
  TEST_ASSERT_EQUAL_UINT32(0, gHeapAllocations - before);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_the_selection_is_what_shows_with_stored_geometry);
  RUN_TEST(test_an_edit_is_heard_and_undo_redo_return_its_focus);
  RUN_TEST(test_delete_and_abandon);
  RUN_TEST(test_more_than_512_notes_is_refused_by_name);
  RUN_TEST(test_no_heap);
  return UNITY_END();
}
