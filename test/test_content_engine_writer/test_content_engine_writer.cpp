//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// fixed-memory-engine E2: the fixed-page writer (Record / overdub interval). NoteOn reserves its NoteOff (CE-A1);
// NoteOff pairs LIFO per (channel, pitch) lane (PAIR-1) and is written into the run that owes it, including closing runs
// after a wrap; Stop writes generated closures from the reservation and seals; nothing allocates after init.

#include <unity.h>

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <new>

#include "ContentEngine/EngineCore.h"
#include "ContentEngine/Writer.h"

static unsigned long gHeapAllocations = 0;
void* operator new(std::size_t size) {
  ++gHeapAllocations;
  if (void* p = std::malloc(size == 0 ? 1 : size)) return p;
  throw std::bad_alloc();
}
void* operator new[](std::size_t size) { return operator new(size); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }

using namespace ContentEngine;

using Pool = PagePool<96, 8>;
using Runs = RunTable<24, Pool, 4, 4>;
using Manifests = ManifestTable<16, 8>;
using Res = RowReservations<Pool, Runs>;
using TestWriter = Writer<Pool, Runs>;

static Pool gPool;
static Runs gRuns;
static Manifests gManifests;
static Res gRes;
static TestWriter gW;
static NoteIdSource gIds;

static void initAll(uint32_t fixedReserve = 0) {
  std::memset(static_cast<void*>(&gPool), 0xA5, sizeof gPool);
  std::memset(static_cast<void*>(&gRuns), 0xA5, sizeof gRuns);
  std::memset(static_cast<void*>(&gRes), 0xA5, sizeof gRes);
  std::memset(static_cast<void*>(&gW), 0xA5, sizeof gW);
  gPool.init();
  gRuns.init();
  gManifests.init();
  gRes.init(gPool, gRuns, fixedReserve);
  gIds.init();
  gW.init(gPool, gRuns, gRes, gIds);
}

static bool conserved() { return checkConservation(gPool, gRuns, gManifests); }

static const PassEvent& rowAt(RunId run, uint32_t i) { return gRuns.rowAt(run, i, gPool); }

void setUp() { initAll(); }
void tearDown() {}

void test_noteoff_pairs_lifo_on_its_lane_and_keeps_channel() {
  TEST_ASSERT_EQUAL(WriterResult::Ok, gW.begin(/*slot*/ 3, /*kind*/ 0));
  const RunId run = gW.activeRun();
  TEST_ASSERT_EQUAL(WriterResult::Ok, gW.noteOn(100, 1, 60, 90));  // id A
  TEST_ASSERT_EQUAL(WriterResult::Ok, gW.noteOn(110, 1, 60, 91));  // id B, same lane
  TEST_ASSERT_EQUAL(WriterResult::Ok, gW.noteOn(120, 2, 60, 92));  // id C, other channel
  TEST_ASSERT_EQUAL(WriterResult::Ok, gW.noteOff(130, 1, 60, 0));  // pairs with B (LIFO)
  TEST_ASSERT_EQUAL(WriterResult::Ok, gW.noteOff(140, 2, 60, 0));  // pairs with C
  TEST_ASSERT_EQUAL(WriterResult::Ok, gW.noteOff(150, 1, 60, 0));  // pairs with A
  const uint32_t a = rowAt(run, 0).noteId, b = rowAt(run, 1).noteId, c = rowAt(run, 2).noteId;
  TEST_ASSERT_TRUE(a != b && b != c && a != c);
  TEST_ASSERT_EQUAL_UINT32(b, rowAt(run, 3).noteId);
  TEST_ASSERT_EQUAL_UINT8(0x80 | 0, rowAt(run, 3).status & 0xF0);
  TEST_ASSERT_EQUAL_UINT32(c, rowAt(run, 4).noteId);
  TEST_ASSERT_EQUAL_UINT8(2 - 1, rowAt(run, 4).status & 0x0F);  // channel kept
  TEST_ASSERT_EQUAL_UINT32(a, rowAt(run, 5).noteId);
  TEST_ASSERT_EQUAL_UINT16(0, gW.openCount());
  TEST_ASSERT_EQUAL_UINT16(0, gRes.outstanding(run));
}

void test_unmatched_noteoff_is_named_and_writes_nothing() {
  gW.begin(0, 0);
  TEST_ASSERT_EQUAL(WriterResult::Unmatched, gW.noteOff(10, 1, 60, 0));
  TEST_ASSERT_EQUAL_UINT32(0, gRuns.run(gW.activeRun()).rowCount);
}

void test_open_note_cap_refuses_only_new_notes() {
  gW.begin(0, 0);
  for (uint32_t i = 0; i < Config::kMaxOpenNotesPerWriter; ++i)
    TEST_ASSERT_EQUAL(WriterResult::Ok, gW.noteOn(i, 1 + (i % 16), i % 128, 100));
  TEST_ASSERT_EQUAL(WriterResult::DeniedOpenNotes, gW.noteOn(500, 1, 1, 100));
  TEST_ASSERT_EQUAL(WriterResult::Ok, gW.noteOff(501, 1, 0, 0));
  TEST_ASSERT_EQUAL(WriterResult::Ok, gW.noteOn(502, 1, 1, 100));
}

// Pool exhausted by a long take with notes held: new input is refused; every held note still closes; Stop seals.
void test_exhaustion_refuses_new_input_but_closes_every_held_note() {
  gW.begin(0, 0);
  for (uint8_t p = 0; p < 20; ++p) TEST_ASSERT_EQUAL(WriterResult::Ok, gW.noteOn(p, 1, p, 100));
  uint32_t t = 100;
  while (gW.channelEvent(t, 0xB0, 1, 64) == WriterResult::Ok) ++t;
  TEST_ASSERT_EQUAL(WriterResult::DeniedCapacity, gW.noteOn(t, 1, 100, 100));
  for (uint8_t p = 0; p < 10; ++p) TEST_ASSERT_EQUAL(WriterResult::Ok, gW.noteOff(t + p, 1, p, 0));
  TEST_ASSERT_EQUAL(WriterResult::Ok, gW.stop(t + 50));
  TEST_ASSERT_EQUAL_UINT16(0, gW.openCount());
  TEST_ASSERT_EQUAL(RunState::Sealed, gRuns.run(gW.lastRun()).state);
  TEST_ASSERT_TRUE(gRes.check());
  TEST_ASSERT_TRUE(conserved());
}

// Wrap with a held note: the old run becomes closing and receives that note's NoteOff; it seals when its last note
// closes, and the writer reports it.
void test_wrap_hands_held_note_to_closing_run() {
  gW.begin(0, 1);
  gW.noteOn(10, 1, 60, 100);
  gW.noteOn(20, 1, 62, 100);
  gW.noteOff(30, 1, 62, 0);
  const RunId first = gW.activeRun();
  TEST_ASSERT_EQUAL(WriterResult::Ok, gW.wrap());
  const RunId second = gW.activeRun();
  TEST_ASSERT_TRUE(second != first);
  TEST_ASSERT_EQUAL_UINT8(1, gW.closingCount());
  gW.noteOn(5, 1, 64, 100);  // new note in the new interval
  TEST_ASSERT_EQUAL(WriterResult::Ok, gW.noteOff(40, 1, 60, 0));  // closes the held note in `first`
  TEST_ASSERT_EQUAL_UINT32(4, gRuns.run(first).rowCount);
  TEST_ASSERT_EQUAL(RunState::Sealed, gRuns.run(first).state);
  RunId sealed = kNoRun;
  TEST_ASSERT_TRUE(gW.takeSealed(sealed));
  TEST_ASSERT_EQUAL_UINT16(first, sealed);
  TEST_ASSERT_EQUAL_UINT8(0, gW.closingCount());
}

// A wrap with no held note seals the run at once.
void test_wrap_without_held_note_seals_immediately() {
  gW.begin(0, 1);
  gW.noteOn(10, 1, 60, 100);
  gW.noteOff(20, 1, 60, 0);
  const RunId first = gW.activeRun();
  gW.wrap();
  TEST_ASSERT_EQUAL(RunState::Sealed, gRuns.run(first).state);
  RunId sealed = kNoRun;
  TEST_ASSERT_TRUE(gW.takeSealed(sealed));
  TEST_ASSERT_EQUAL_UINT16(first, sealed);
}

// Two closing runs already: the next wrap's commit is owed; the active run continues (DEC-069 owed commit).
void test_third_held_wrap_owes_the_commit() {
  gW.begin(0, 1);
  gW.noteOn(1, 1, 60, 100);
  gW.wrap();
  gW.noteOn(2, 1, 61, 100);
  gW.wrap();
  gW.noteOn(3, 1, 62, 100);
  const RunId active = gW.activeRun();
  TEST_ASSERT_EQUAL(WriterResult::CommitOwed, gW.wrap());
  TEST_ASSERT_EQUAL_UINT16(active, gW.activeRun());
  TEST_ASSERT_EQUAL_UINT8(2, gW.closingCount());
  gW.noteOff(10, 1, 60, 0);  // the oldest closing run seals
  TEST_ASSERT_EQUAL_UINT8(1, gW.closingCount());
  TEST_ASSERT_EQUAL(WriterResult::Ok, gW.wrap());  // now the wrap can hand over
  TEST_ASSERT_EQUAL_UINT8(2, gW.closingCount());
}

// Stop writes a generated closure (flagged) for every open note into the run that owes it, then seals everything.
void test_stop_closes_open_notes_in_their_own_runs_and_seals() {
  const unsigned long heapBefore = gHeapAllocations;
  gW.begin(0, 1);
  gW.noteOn(10, 3, 60, 100);
  const RunId first = gW.activeRun();
  gW.wrap();
  gW.noteOn(20, 4, 61, 100);
  const RunId second = gW.activeRun();
  TEST_ASSERT_EQUAL(WriterResult::Ok, gW.stop(700));
  const PassEvent& c1 = rowAt(first, gRuns.run(first).rowCount - 1);
  const PassEvent& c2 = rowAt(second, gRuns.run(second).rowCount - 1);
  TEST_ASSERT_EQUAL_UINT32(700, c1.tick);
  TEST_ASSERT_EQUAL_UINT8(0x80 | 2, c1.status);
  TEST_ASSERT_TRUE((c1.origin & kOriginGeneratedClosure) != 0);
  TEST_ASSERT_EQUAL_UINT32(rowAt(first, 0).noteId, c1.noteId);
  TEST_ASSERT_EQUAL_UINT8(0x80 | 3, c2.status);
  TEST_ASSERT_EQUAL(RunState::Sealed, gRuns.run(first).state);
  TEST_ASSERT_EQUAL(RunState::Sealed, gRuns.run(second).state);
  TEST_ASSERT_EQUAL_UINT16(0, gRes.outstanding(first));
  TEST_ASSERT_EQUAL(WriterResult::NotWriting, gW.noteOn(800, 1, 1, 1));
  TEST_ASSERT_TRUE(gRes.check());
  TEST_ASSERT_TRUE(conserved());
  TEST_ASSERT_EQUAL_UINT32(0, gHeapAllocations - heapBefore);
}

// No descriptor for the next wrap: the writer enters FinalInterval; the next wrap ends the take like a Stop.
void test_final_interval_when_descriptors_run_out() {
  gW.begin(0, 1);
  RunId hog[Runs::kRunCount];
  uint16_t n = 0;
  while ((hog[n] = gRuns.allocate(9, 0)) != kNoRun) ++n;
  gW.noteOn(10, 1, 60, 100);
  TEST_ASSERT_EQUAL(WriterResult::FinalInterval, gW.wrap());
  TEST_ASSERT_TRUE(gW.isFinalInterval());
  TEST_ASSERT_EQUAL(WriterResult::Ok, gW.noteOff(20, 1, 60, 0));
  TEST_ASSERT_EQUAL(WriterResult::Stopped, gW.wrap());
  TEST_ASSERT_EQUAL(RunState::Sealed, gRuns.run(gW.lastRun()).state);
}

// Random input over many takes: NoteOn / NoteOff / CC / wrap / Stop / release of finished takes. Invariant R and page
// conservation hold at every step; a NoteOff for an open lane is never refused; refusals happen (not vacuous); no heap.
void test_writer_random_input_fuzz() {
  initAll(/*fixedReserve*/ 4);
  const unsigned long heapBefore = gHeapAllocations;
  uint32_t seed = 0x5EEDu;
  auto rnd = [&seed]() {
    seed ^= seed << 13;
    seed ^= seed >> 17;
    seed ^= seed << 5;
    return seed;
  };
  uint16_t openOnLane[17][8] = {};  // test-side mirror: open notes per (channel 1..16, pitch 60..67)
  RunId finished[Runs::kRunCount];
  uint16_t finishedCount = 0;
  uint32_t refusals = 0, noteOffs = 0, takes = 0;
  bool writing = false;
  for (uint32_t step = 0; step < 100000; ++step) {
    if (!writing) {
      if (gW.begin(0, 1) == WriterResult::Ok) {
        writing = true;
        ++takes;
      }
    } else {
      const uint32_t op = rnd() % 100;
      const uint8_t ch = static_cast<uint8_t>(1 + rnd() % 16), p = static_cast<uint8_t>(rnd() % 8);
      if (op < 40) {
        const WriterResult r = gW.noteOn(step, ch, static_cast<uint8_t>(60 + p), 100);
        if (r == WriterResult::Ok)
          ++openOnLane[ch][p];
        else
          ++refusals;
      } else if (op < 75) {
        const WriterResult r = gW.noteOff(step, ch, static_cast<uint8_t>(60 + p), 0);
        if (openOnLane[ch][p] > 0) {
          TEST_ASSERT_EQUAL(WriterResult::Ok, r);
          --openOnLane[ch][p];
          ++noteOffs;
        } else {
          TEST_ASSERT_EQUAL(WriterResult::Unmatched, r);
        }
      } else if (op < 90) {
        if (gW.channelEvent(step, 0xB0, 1, 2) != WriterResult::Ok) ++refusals;
      } else if (op < 99) {
        const WriterResult r = gW.wrap(step);
        if (r == WriterResult::Stopped) writing = false;
      } else {
        TEST_ASSERT_EQUAL(WriterResult::Ok, gW.stop(step));
        writing = false;
      }
      if (!writing) {
        for (auto& c : openOnLane)
          for (auto& x : c) x = 0;
      }
    }
    // Sealed runs become content; every few takes the oldest content is dropped and reclaimed (undo / overwrite).
    RunId sealed;
    while (gW.takeSealed(sealed)) finished[finishedCount++] = sealed;
    if (finishedCount > 12) {
      gRuns.release(finished[0]);
      while (!gRuns.reclaimStep(finished[0], gPool, 8)) {}
      for (uint16_t i = 1; i < finishedCount; ++i) finished[i - 1] = finished[i];
      --finishedCount;
    }
    if (step % 499 == 0) {
      TEST_ASSERT_TRUE(gRes.check());
      TEST_ASSERT_TRUE(conserved());
    }
  }
  TEST_ASSERT_TRUE(gRes.check());
  TEST_ASSERT_TRUE(conserved());
  TEST_ASSERT_TRUE(refusals > 0);
  TEST_ASSERT_TRUE(noteOffs > 1000);
  TEST_ASSERT_TRUE(takes > 10);
  TEST_ASSERT_EQUAL_UINT32(0, gW.sealedOverflow());
  TEST_ASSERT_EQUAL_UINT32(0, gHeapAllocations - heapBefore);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_noteoff_pairs_lifo_on_its_lane_and_keeps_channel);
  RUN_TEST(test_unmatched_noteoff_is_named_and_writes_nothing);
  RUN_TEST(test_open_note_cap_refuses_only_new_notes);
  RUN_TEST(test_exhaustion_refuses_new_input_but_closes_every_held_note);
  RUN_TEST(test_wrap_hands_held_note_to_closing_run);
  RUN_TEST(test_wrap_without_held_note_seals_immediately);
  RUN_TEST(test_third_held_wrap_owes_the_commit);
  RUN_TEST(test_stop_closes_open_notes_in_their_own_runs_and_seals);
  RUN_TEST(test_final_interval_when_descriptors_run_out);
  RUN_TEST(test_writer_random_input_fuzz);
  return UNITY_END();
}
