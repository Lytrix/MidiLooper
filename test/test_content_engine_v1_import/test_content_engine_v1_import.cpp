//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// fixed-memory-engine E8.2: V1 import, exact current content only (owner decision 2026-11-05). A slot's resolved notes
// and events (as the old firmware plays them) become one take and the slot's only history state. The composed take
// equals the input exactly: every note's start, end and pitch (same-pitch overlaps inside the take kept, Q-OVL-1; a
// note held across the loop end sounding from bar 0 of the next cycle; a zero-length note), every controller event,
// fresh note ids, NoteOff before NoteOn at a tick. Wrong order, bad input, too many held notes, too little content
// quota and an occupied slot are refused by name with nothing held. No heap.

#include <unity.h>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <new>
#include <tuple>
#include <vector>

#include "ContentEngine/Composer.h"
#include "ContentEngine/EngineCore.h"
#include "ContentEngine/SetFile.h"

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
using Manifests = ManifestTable<8, 8>;
using Hist = History<Manifests, Runs, 4, 20>;
using Res = RowReservations<Pool, Runs>;
using Import = ResolvedImport<Pool, Runs, Manifests, Hist, /*held*/ 8>;
using DisplayComposer = Composer<Pool, Runs, 64, 48>;  // every stored note (the check is of storage, not output)
using Window = SpanWindow<3072>;

constexpr uint32_t kLoop = 768 * 4;

static Pool gPool;
static Runs gRuns;
static Manifests gManifests;
static Hist gH;
static Res gRes;
static NoteIdSource gIds;
static DisplayComposer gC;
static Window gWin;
static Import gImport;

void setUp() {
  gPool.init();
  gRuns.init();
  gManifests.init();
  gRes.init(gPool, gRuns, 0);
  gH.init(gManifests, gRuns);
  gIds.init(500);  // ids already used by other slots
  gC.init(gPool, gRuns, nullptr);
}
void tearDown() {}

struct Note {
  uint32_t start, end;
  uint8_t pitch, velocity, channel;
};
struct Event {
  uint32_t tick;
  uint8_t status, d1, d2;
};
static Import::Status importSlot(uint8_t slot, const std::vector<Note>& notes, const std::vector<Event>& events) {
  if (gImport.start(gPool, gRuns, gManifests, gH, gRes, gIds, slot, kLoop, static_cast<uint32_t>(notes.size()),
                    static_cast<uint32_t>(events.size())) != Import::Status::Running)
    return gImport.status();
  size_t n = 0, e = 0;
  while (n < notes.size() || e < events.size()) {  // the caller's merge by tick (events first on a tie)
    if (e < events.size() && (n == notes.size() || events[e].tick <= notes[n].start)) {
      gImport.addEvent(events[e].tick, events[e].status, events[e].d1, events[e].d2);
      ++e;
    } else {
      gImport.addNote(notes[n].start, notes[n].end, notes[n].pitch, notes[n].velocity, notes[n].channel);
      ++n;
    }
    if (gImport.status() != Import::Status::Running) return gImport.status();
  }
  return gImport.finish();
}

static const std::vector<Note> kNotes = {
    {100, 600, 60, 100, 0},           // overlapping the next one on the same pitch: both kept (one take)
    {300, 400, 60, 90, 0},
    {300, 300, 61, 80, 1},            // zero length
    {600, 650, 60, 85, 0},            // starts on the tick the first one ends
    {700, 1700, 64, 70, 2},           // crossing bars
    {kLoop - 200, 150, 67, 60, 0},    // held across the loop end
};
static const std::vector<Event> kEvents = {{50, 0xB0, 7, 100}, {700, 0xE1, 0, 64}, {2000, 0xC0, 5, 0}};

void test_the_take_composes_back_to_the_resolved_content() {
  const uint32_t idsBefore = gIds.peek();
  TEST_ASSERT_EQUAL(Import::Status::Done, importSlot(0, kNotes, kEvents));
  const ManifestId m = gH.current(0);
  TEST_ASSERT_NOT_EQUAL(kNoManifest, m);
  TEST_ASSERT_FALSE(gH.canUndo(0) && gH.undoDepth(0) > 1);  // the only state: no history from before
  TEST_ASSERT_EQUAL_UINT8(1, gH.undoDepth(0));            // (Undo empties the slot)
  const RunId r = gManifests.manifest(m).runs[0];
  TEST_ASSERT_EQUAL_UINT8(RunKind::kRecord, gRuns.run(r).kind);
  TEST_ASSERT_EQUAL_UINT32(2 * kNotes.size() + kEvents.size(), gRuns.run(r).rowCount);
  // Rows: tick order, NoteOff before NoteOn at a tick, fresh ids, the held note's NoteOff in the wrap band.
  for (uint32_t i = 1; i < gRuns.run(r).rowCount; ++i) TEST_ASSERT_TRUE(gRuns.rowAt(r, i - 1, gPool).tick <= gRuns.rowAt(r, i, gPool).tick);
  std::vector<uint32_t> ids;
  for (uint32_t i = 0; i < gRuns.run(r).rowCount; ++i) {
    const PassEvent& e = gRuns.rowAt(r, i, gPool);
    if ((e.status & 0xF0) == 0x90) {
      TEST_ASSERT_TRUE(e.noteId >= idsBefore);
      ids.push_back(e.noteId);
    }
    if (i > 0 && (e.status & 0xF0) == 0x80 && e.tick == gRuns.rowAt(r, i - 1, gPool).tick && e.noteId != gRuns.rowAt(r, i - 1, gPool).noteId)
      TEST_ASSERT_NOT_EQUAL(0x90, gRuns.rowAt(r, i - 1, gPool).status & 0xF0);  // another note's NoteOn never precedes
  }
  std::sort(ids.begin(), ids.end());
  TEST_ASSERT_TRUE(std::adjacent_find(ids.begin(), ids.end()) == ids.end());  // fresh and unique
  TEST_ASSERT_EQUAL_UINT32(kLoop + 150, gRuns.rowAt(r, gRuns.run(r).rowCount - 1, gPool).tick);
  // Composition, steady cycle 1: (start, end, pitch) of every note and every event.
  const SourceRun src[1] = {{r, gRuns.run(r).rowCount}};
  TEST_ASSERT_TRUE(gC.begin(src, 1, kLoop, gManifests.manifest(m).identity));
  while (gC.composeNext(gWin, kLoop)) {
  }
  gC.nextCycle();
  TEST_ASSERT_TRUE(gC.composeNext(gWin, kLoop));
  std::vector<std::tuple<uint32_t, uint32_t, uint8_t>> notes;  // start, end, pitch
  std::vector<std::tuple<uint32_t, uint8_t, uint8_t>> events;
  uint32_t heldEnd = 0;
  for (uint16_t k = 0; k < gWin.count; ++k) {
    const SpanEntry& e = gWin.entries[k];
    if (e.terminal) {
      if (e.noteId == gRuns.rowAt(r, gRuns.run(r).rowCount - 1, gPool).noteId) heldEnd = e.tick;  // carried in
      continue;
    }
    if ((e.status & 0xF0) == 0x90)
      notes.emplace_back(e.tick, e.length == kOpenLength ? kLoop : e.tick + e.length, e.data1);
    else
      events.emplace_back(e.tick, e.status, e.data1);
  }
  gC.end();
  std::vector<std::tuple<uint32_t, uint32_t, uint8_t>> want;
  for (const Note& n : kNotes) want.emplace_back(n.start, n.end >= n.start ? n.end : kLoop, n.pitch);
  std::sort(want.begin(), want.end());
  std::sort(notes.begin(), notes.end());
  TEST_ASSERT_EQUAL_UINT32(want.size(), notes.size());
  for (size_t i = 0; i < want.size(); ++i) TEST_ASSERT_TRUE(want[i] == notes[i]);
  TEST_ASSERT_EQUAL_UINT32(150, heldEnd);  // the held note sounds from bar 0 of the next cycle until its end
  TEST_ASSERT_EQUAL_UINT32(kEvents.size(), events.size());
  for (size_t i = 0; i < kEvents.size(); ++i)
    TEST_ASSERT_TRUE(std::get<0>(events[i]) == kEvents[i].tick && std::get<1>(events[i]) == kEvents[i].status);
  TEST_ASSERT_TRUE(gRes.check());
  TEST_ASSERT_TRUE(checkConservation(gPool, gRuns, gManifests));
}

static void expectNothingHeld(uint16_t pages, uint16_t runs, uint16_t manifests) {
  while (gRuns.reclaimPendingRuns() != 0) gRuns.reclaim(gPool, 64);
  TEST_ASSERT_EQUAL_UINT16(pages, gPool.freeCount());
  TEST_ASSERT_EQUAL_UINT16(runs, gRuns.freeCount());
  TEST_ASSERT_EQUAL_UINT16(manifests, gManifests.freeCount());
  TEST_ASSERT_TRUE(gRes.check());
}

void test_refusals_are_named_and_release_everything() {
  const uint16_t pages = gPool.freeCount(), runs = gRuns.freeCount(), manifests = gManifests.freeCount();
  std::vector<Note> unordered = kNotes;
  std::swap(unordered[0], unordered[3]);
  TEST_ASSERT_EQUAL(Import::Status::BadOrder, importSlot(0, unordered, {}));
  expectNothingHeld(pages, runs, manifests);
  TEST_ASSERT_EQUAL(Import::Status::BadInput, importSlot(0, {{100, 200, 200, 90, 0}}, {}));  // pitch 200
  expectNothingHeld(pages, runs, manifests);
  TEST_ASSERT_EQUAL(Import::Status::BadInput, importSlot(0, {}, {{100, 0x90, 60, 1}}));  // a note as an event
  expectNothingHeld(pages, runs, manifests);
  std::vector<Note> chord;
  for (uint8_t i = 0; i < 9; ++i) chord.push_back({100, 900, static_cast<uint8_t>(40 + i), 90, 0});
  TEST_ASSERT_EQUAL(Import::Status::TooManyHeld, importSlot(0, chord, {}));  // 9 held, room for 8
  expectNothingHeld(pages, runs, manifests);
  // Content quota nearly full: refused at admission.
  const RunId filler = gRuns.allocate(1, RunKind::kRecord);
  PassEvent cc{};
  cc.status = 0xB0;
  while (gRes.accept(filler, cc, 0) == Admission::Ok && gPool.freeCount() > 0) {
  }
  TEST_ASSERT_EQUAL(Import::Status::DeniedCapacity, importSlot(0, kNotes, kEvents));
  expectNothingHeld(gPool.freeCount(), static_cast<uint16_t>(runs - 1), manifests);
  gRuns.seal(filler);
  gRuns.release(filler);
  while (gRuns.reclaimPendingRuns() != 0) gRuns.reclaim(gPool, 64);
  TEST_ASSERT_EQUAL(Import::Status::Done, importSlot(0, kNotes, kEvents));
  TEST_ASSERT_EQUAL(Import::Status::DeniedSlot, importSlot(0, kNotes, kEvents));  // the slot has content now
}

void test_no_heap() {
  gTrap = true;
  const unsigned long before = gHeapAllocations;
  TEST_ASSERT_EQUAL(Import::Status::Running, gImport.start(gPool, gRuns, gManifests, gH, gRes, gIds, 2, kLoop, 5, 1));  // over-counted
  gImport.addEvent(10, 0xB0, 1, 2);
  gImport.addNote(20, 80, 60, 90, 0);
  gImport.addNote(30, 40, 61, 90, 0);
  gImport.addNote(kLoop - 5, 5, 62, 90, 0);
  TEST_ASSERT_EQUAL(Import::Status::Done, gImport.finish());
  gTrap = false;
  TEST_ASSERT_EQUAL_UINT32(0, gHeapAllocations - before);
  TEST_ASSERT_TRUE(gRes.check());  // the rows counted but not used are given back
  TEST_ASSERT_EQUAL_UINT32(0, gRes.reservedPages());
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_the_take_composes_back_to_the_resolved_content);
  RUN_TEST(test_refusals_are_named_and_release_everything);
  RUN_TEST(test_no_heap);
  return UNITY_END();
}
