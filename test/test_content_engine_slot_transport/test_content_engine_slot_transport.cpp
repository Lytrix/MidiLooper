//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// fixed-memory-engine E9.1: the slot transport end to end — writer, manifests, history and playback tracks driven by
// gestures on a Clock timeline, with ample worker time (every pending compose grain runs before each tick).
// Record → the take plays; overdub with a note held across the loop end → the wrap publishes (owner 2026-11-05) so the
// cycle's other notes play in the next cycle, and the held note's release completes the state in place; Stop folds
// the session into one step; Undo / Redo / Undo to empty follow on the track. One writer: refusals by name. No heap.

#include <unity.h>

#include <cstdint>
#include <cstdlib>
#include <map>
#include <new>
#include <vector>

#include "ContentEngine/Composer.h"
#include "ContentEngine/EngineCore.h"
#include "ContentEngine/History.h"
#include "ContentEngine/PlaybackTrack.h"
#include "ContentEngine/SlotTransport.h"
#include "ContentEngine/Writer.h"

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

using Pool = PagePool<512, 64>;
using Runs = RunTable<32, Pool>;
using Manifests = ManifestTable<24, 8>;
using Hist = History<Manifests, Runs, 16, 20>;
using Res = RowReservations<Pool, Runs>;
using TestComposer = Composer<Pool, Runs>;
using Window = SpanWindow<3072>;
using Table = CheckpointTable<TestComposer::Checkpoint, 16, 8>;
constexpr uint16_t kT = Config::kMaxSoundingPerTrack;
using Track = PlaybackTrack<TestComposer, Table, Window, Manifests, Runs, kT>;
using Player = WindowPlayer<kT>;
using Emit = Player::Emit;
using W = Writer<Pool, Runs>;
using Transport = SlotTransport<Pool, Runs, Manifests, Hist, Track, 2>;
using R = Transport::Result;

constexpr uint32_t kLoop = 768 * 4;  // 4 bars, two windows
constexpr uint32_t kWin = 1536;
constexpr uint32_t kS = 1000;  // Record starts at this Clock tick: track 0's origin

static Pool gPool;
static Runs gRuns;
static Manifests gManifests;
static Hist gH;
static Res gRes;
static NoteIdSource gIds;
static W gWriter;
static Table gTable;
static TestComposer gComp[2][2];
static Window gBuf[2][3];
static TerminalScheduler<kT> gSched[2];
static Player gPlayer[2];
static Track gTrack[2];
static Transport gX;
static std::vector<Emit> gOut;  // track 0's emits, ticks track-local
static uint32_t gClock = 0;
static uint32_t gAccepted = 0;  // live NoteOns the transport accepted

void setUp() {
  gPool.init();
  gRuns.init();
  gManifests.init();
  gRes.init(gPool, gRuns, 0);
  gH.init(gManifests, gRuns);
  gIds.init(1);
  gWriter.init(gPool, gRuns, gRes, gIds);
  gTable.init();
  Track* tracks[2];
  for (uint8_t t = 0; t < 2; ++t) {
    gComp[t][0].init(gPool, gRuns, nullptr);
    gComp[t][1].init(gPool, gRuns, nullptr);
    gSched[t].init();
    gPlayer[t].init(gSched[t]);
    Window* bufs[3] = {&gBuf[t][0], &gBuf[t][1], &gBuf[t][2]};
    gTrack[t].init(gComp[t][0], gComp[t][1], bufs, gTable, gManifests, gRuns, gPlayer[t], kWin);
    tracks[t] = &gTrack[t];
  }
  gX.init(gPool, gRuns, gManifests, gH, gWriter, tracks);
  gOut.clear();
  gClock = 0;
  gAccepted = 0;
}
void tearDown() {}

// ---- Driving ---------------------------------------------------------------------------------------------------------
struct Live {  // a live note on track 0, in Clock ticks
  uint32_t on, off;
  uint8_t pitch;
};

/// Runs Clock ticks up to `to`: compose work, each track's tick, then the live input landing on that tick. `strict`:
/// every live NoteOn must be accepted.
static void run(uint32_t to, const std::vector<Live>& live = {}, bool strict = true) {
  Emit buf[64];
  for (; gClock < to; ++gClock) {
    for (auto& t : gTrack)
      while (t.work(256)) {
      }
    uint8_t n = gX.tick(0, gClock, buf, 64);
    gOut.insert(gOut.end(), buf, buf + n);
    gX.tick(1, gClock, buf, 64);
    for (const Live& l : live) {
      if (l.off == gClock) gX.noteOff(gClock, 1, l.pitch, 0);
      if (l.on == gClock) {
        uint8_t k;
        const R r = gX.noteOn(gClock, 1, l.pitch, 100, buf, 64, k);
        if (strict) TEST_ASSERT_EQUAL(R::Ok, r);
        if (r == R::Ok) ++gAccepted;
        gOut.insert(gOut.end(), buf, buf + k);
      }
    }
  }
}
static void startRecord(uint8_t slot) {
  TEST_ASSERT_EQUAL(R::Ok, gX.startRecord(0, slot, gClock));
}
static void stop() {
  Emit buf[kT];
  uint8_t n;
  TEST_ASSERT_EQUAL(R::Ok, gX.stop(gClock, buf, kT, n));
  gOut.insert(gOut.end(), buf, buf + n);
}

struct Played {
  uint32_t on, off;  // loop positions; off may be >= kLoop for a note ending in the next cycle
};
/// Track 0's notes starting in local cycle `cycle`, by pitch, with their NoteOff (possibly in the next cycle).
static std::map<uint8_t, std::vector<Played>> cycleNotes(uint32_t cycle) {
  std::map<uint8_t, std::vector<Played>> out;
  const uint32_t from = cycle * kLoop;
  for (size_t i = 0; i < gOut.size(); ++i) {
    const Emit& e = gOut[i];
    if (e.cls != 1 || e.tick < from || e.tick >= from + kLoop) continue;
    uint32_t off = 0xFFFFFFFFu;
    for (size_t k = i + 1; k < gOut.size(); ++k)
      if (gOut[k].cls == 0 && gOut[k].noteId == e.noteId && gOut[k].pitch == e.pitch) {
        off = gOut[k].tick - from;
        break;
      }
    out[e.pitch].push_back({e.tick - from, off});
  }
  return out;
}
static void expectCycle(uint32_t cycle, const std::map<uint8_t, Played>& want) {
  auto got = cycleNotes(cycle);
  char msg[96];
  snprintf(msg, sizeof msg, "cycle %u: %u pitches, want %u", cycle, static_cast<unsigned>(got.size()),
           static_cast<unsigned>(want.size()));
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(want.size(), got.size(), msg);
  for (const auto& w : want) {
    snprintf(msg, sizeof msg, "cycle %u pitch %u", cycle, w.first);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, got[w.first].size(), msg);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(w.second.on, got[w.first][0].on, msg);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(w.second.off, got[w.first][0].off, msg);
  }
}
static bool allSealed(ManifestId m) {
  const auto& x = gManifests.manifest(m);
  for (uint8_t i = 0; i < x.runCount; ++i)
    if (gRuns.run(x.runs[i]).state == RunState::Writing) return false;
  return true;
}

// The take: A notes in both windows (positions relative to the Record start).
static const std::vector<Live> kTake = {
    {kS + 100, kS + 400, 60}, {kS + 1600, kS + 1700, 62}, {kS + 2000, kS + 2900, 64}, {kS + 3000, kS + 3060, 65}};
static const std::map<uint8_t, Played> kA = {{60, {100, 400}}, {62, {1600, 1700}}, {64, {2000, 2900}}, {65, {3000, 3060}}};

static void recordTake() {
  gClock = kS;
  startRecord(0);
  run(kS + kLoop, kTake);
  stop();
}

// ---- Tests -------------------------------------------------------------------------------------------------------------
void test_record_then_the_take_plays() {
  recordTake();
  TEST_ASSERT_EQUAL_UINT8(0, gX.playingSlot(0));
  TEST_ASSERT_EQUAL_UINT32(kS, gX.origin(0));
  TEST_ASSERT_EQUAL_UINT8(1, gH.undoDepth(0));
  run(kS + 4 * kLoop);
  // Cycle 1: the take's first window is not ready at the Stop tick (E9.1b moves its composition before Stop), so it
  // plays from the next window boundary (window 1 starts at 1536).
  expectCycle(1, {{62, {1600, 1700}}, {64, {2000, 2900}}, {65, {3000, 3060}}});
  expectCycle(2, kA);
  expectCycle(3, kA);
  TEST_ASSERT_TRUE(gRes.check());
  TEST_ASSERT_TRUE(checkConservation(gPool, gRuns, gManifests));
}

void test_overdub_publishes_at_the_wrap_and_completes_the_held_note() {
  recordTake();
  run(kS + 2 * kLoop + 50);
  TEST_ASSERT_EQUAL(R::Ok, gX.startOverdub(0, gClock));
  const uint32_t c2 = kS + 2 * kLoop, c3 = kS + 3 * kLoop;
  // Cycle 2: 70 in window 1; 72 held across the loop end, released in cycle 3; 74 in cycle 3.
  const std::vector<Live> live = {{c2 + 1700, c2 + 1800, 70}, {c2 + 3000, c3 + 200, 72}, {c3 + 2000, c3 + 2100, 74}};
  run(c3 + 1, live);  // the wrap at c3
  TEST_ASSERT_EQUAL_UINT32(1, gX.stats().publishedWraps);
  TEST_ASSERT_EQUAL_UINT32(1, gX.stats().closingPublished);
  const ManifestId atWrap = gH.current(0);
  const uint32_t atWrapIdentity = gManifests.manifest(atWrap).identity;
  TEST_ASSERT_FALSE(allSealed(atWrap));
  run(c3 + 201, live);  // 72 released: its closing run seals
  TEST_ASSERT_EQUAL_UINT32(1, gX.stats().completions);
  TEST_ASSERT_NOT_EQUAL(atWrapIdentity, gManifests.manifest(gH.current(0)).identity);
  TEST_ASSERT_TRUE(allSealed(gH.current(0)));
  TEST_ASSERT_EQUAL_UINT8(1, gH.sessionDepth(SessionKind::Overdub));  // in place: still one step
  run(c3 + 2500, live);
  TEST_ASSERT_EQUAL_UINT32(gManifests.manifest(gH.current(0)).identity, gTrack[0].playingIdentity());  // adopted
  stop();
  TEST_ASSERT_EQUAL_UINT8(2, gH.undoDepth(0));  // the take, the folded session
  run(kS + 7 * kLoop + 300);  // cycle 6's held note ends in cycle 7
  // Cycle 3: the wrap's state plays from window 1 (70 heard the cycle after it was played); 72 sounded live.
  auto c3n = cycleNotes(3);
  TEST_ASSERT_EQUAL_UINT32(1, c3n[70].size());
  TEST_ASSERT_EQUAL_UINT32(1700, c3n[70][0].on);
  // Steady: A + B, the held note from 3000 to 200 of the next cycle.
  std::map<uint8_t, Played> ab = kA;
  ab[70] = {1700, 1800};
  ab[72] = {3000, kLoop + 200};
  ab[74] = {2000, 2100};
  expectCycle(5, ab);
  expectCycle(6, ab);
  TEST_ASSERT_TRUE(gRes.check());
  TEST_ASSERT_TRUE(checkConservation(gPool, gRuns, gManifests));
}

void test_undo_redo_and_undo_to_empty_follow_on_the_track() {
  recordTake();
  run(kS + kLoop + 10);
  TEST_ASSERT_EQUAL(R::Ok, gX.startOverdub(0, gClock));
  const uint32_t c1 = kS + kLoop;
  run(c1 + 2500, {{c1 + 1700, c1 + 1800, 70}, {c1 + 2400, 0, 90}});  // 90 still held at Stop: closed by it
  stop();
  run(kS + 4 * kLoop);
  std::map<uint8_t, Played> ab = kA;
  ab[70] = {1700, 1800};
  ab[90] = {2400, 2500};  // plays: Stop released its live occupancy
  expectCycle(3, ab);
  Emit buf[kT];
  uint8_t n;
  TEST_ASSERT_EQUAL(R::Ok, gX.undo(0, gClock, buf, kT, n));
  run(kS + 6 * kLoop);
  expectCycle(5, kA);
  TEST_ASSERT_EQUAL(R::Ok, gX.redo(0, gClock, buf, kT, n));
  run(kS + 8 * kLoop);
  expectCycle(7, ab);
  TEST_ASSERT_EQUAL(R::Ok, gX.undo(0, gClock, buf, kT, n));
  TEST_ASSERT_EQUAL(R::Ok, gX.undo(0, gClock + 1, buf, kT, n));  // to the empty slot: the track stops
  gOut.insert(gOut.end(), buf, buf + n);
  const size_t at = gOut.size();
  run(kS + 10 * kLoop);
  for (size_t i = at; i < gOut.size(); ++i) TEST_ASSERT_NOT_EQUAL(1, gOut[i].cls);  // nothing starts any more
  std::map<uint32_t, int> open;  // every NoteOn has its NoteOff
  for (const Emit& e : gOut) open[e.noteId] += e.cls == 1 ? 1 : e.cls == 0 ? -1 : 0;
  for (const auto& o : open) TEST_ASSERT_EQUAL_INT(0, o.second);
  TEST_ASSERT_EQUAL(R::NoChange, gX.undo(0, gClock, buf, kT, n));
  TEST_ASSERT_TRUE(gRes.check());
  TEST_ASSERT_TRUE(checkConservation(gPool, gRuns, gManifests));
}

void test_one_writer_and_named_refusals() {
  Emit buf[kT];
  uint8_t n;
  TEST_ASSERT_EQUAL(R::NotWriting, gX.stop(5, buf, kT, n));
  TEST_ASSERT_EQUAL(R::Empty, gX.startOverdub(1, 5));  // track 1 plays nothing
  // A Stop with nothing recorded leaves the slot empty and holds nothing.
  const uint16_t pages = gPool.freeCount(), runs = gRuns.freeCount();
  TEST_ASSERT_EQUAL(R::Ok, gX.startRecord(1, 8, 10));
  TEST_ASSERT_EQUAL(R::Empty, gX.stop(500, buf, kT, n));
  while (gRuns.reclaimPendingRuns() != 0) gRuns.reclaim(gPool, 64);
  TEST_ASSERT_EQUAL_UINT16(pages, gPool.freeCount());
  TEST_ASSERT_EQUAL_UINT16(runs, gRuns.freeCount());
  TEST_ASSERT_EQUAL(kNoManifest, gH.current(8));
  recordTake();
  TEST_ASSERT_EQUAL(R::DeniedSlot, gX.startRecord(1, 0, gClock));  // the slot has content
  TEST_ASSERT_EQUAL(R::Ok, gX.startOverdub(0, gClock));
  TEST_ASSERT_EQUAL(R::Busy, gX.startRecord(1, 8, gClock));  // one writer at a time
  TEST_ASSERT_EQUAL(R::Busy, gX.startOverdub(0, gClock));
  // A whole cycle with nothing played: the wrap publishes no step.
  run(gClock + kLoop + 10);
  TEST_ASSERT_EQUAL_UINT32(1, gX.stats().emptyWraps);
  stop();
  TEST_ASSERT_EQUAL_UINT8(1, gH.undoDepth(0));
  TEST_ASSERT_EQUAL(Transport::Mode::Idle, gX.mode());
  TEST_ASSERT_TRUE(gRes.check());
  TEST_ASSERT_TRUE(checkConservation(gPool, gRuns, gManifests));
}

void test_a_full_run_list_ends_the_take_at_a_wrap() {
  recordTake();
  gAccepted = 0;  // overdub notes only
  run(kS + kLoop + 10);
  TEST_ASSERT_EQUAL(R::Ok, gX.startOverdub(0, gClock));
  std::vector<Live> live;
  for (uint32_t c = 1; c < 10; ++c) live.push_back({kS + c * kLoop + 1700, kS + c * kLoop + 1750, static_cast<uint8_t>(80 + c)});
  run(kS + 11 * kLoop, live, false);  // one note per cycle: the take ends when the 8-run list cannot take the next
  TEST_ASSERT_EQUAL_UINT32(1, gX.stats().takesEndedFull);
  TEST_ASSERT_EQUAL(Transport::Mode::Idle, gX.mode());
  TEST_ASSERT_EQUAL_UINT8(Manifests::kMaxRuns, gManifests.manifest(gH.current(0)).runCount);
  TEST_ASSERT_EQUAL_UINT8(2, gH.undoDepth(0));  // the session folded at the take's end
  TEST_ASSERT_TRUE(allSealed(gH.current(0)));
  uint32_t overdubRows = 0;  // every accepted note is in the slot's state
  const auto& m = gManifests.manifest(gH.current(0));
  for (uint8_t i = 1; i < m.runCount; ++i) overdubRows += gRuns.run(m.runs[i]).rowCount;
  TEST_ASSERT_EQUAL_UINT32(2 * gAccepted, overdubRows);
  TEST_ASSERT_TRUE(gRes.check());
  TEST_ASSERT_TRUE(checkConservation(gPool, gRuns, gManifests));
}

void test_no_heap() {
  gTrap = true;
  const unsigned long before = gHeapAllocations;
  gClock = kS;
  TEST_ASSERT_EQUAL(R::Ok, gX.startRecord(0, 0, gClock));
  Emit buf[64];
  uint8_t k;
  for (; gClock < kS + 3 * kLoop; ++gClock) {
    for (auto& t : gTrack)
      while (t.work(256)) {
      }
    gX.tick(0, gClock, buf, 64);
    const uint32_t pos = (gClock - kS) % kLoop;
    if (gClock == kS + kLoop) gX.stop(gClock, buf, kT, k);
    if (gClock == kS + kLoop + 20) gX.startOverdub(0, gClock);
    if (pos == 1700) gX.noteOn(gClock, 1, static_cast<uint8_t>(60 + gClock % 7), 90, buf, 64, k);
    if (pos == 1800) gX.noteOff(gClock, 1, static_cast<uint8_t>(60 + (gClock - 100) % 7), 0);
  }
  gX.stop(gClock, buf, kT, k);
  gX.undo(0, gClock, buf, kT, k);
  gTrap = false;
  TEST_ASSERT_EQUAL_UINT32(0, gHeapAllocations - before);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_record_then_the_take_plays);
  RUN_TEST(test_overdub_publishes_at_the_wrap_and_completes_the_held_note);
  RUN_TEST(test_undo_redo_and_undo_to_empty_follow_on_the_track);
  RUN_TEST(test_one_writer_and_named_refusals);
  RUN_TEST(test_a_full_run_list_ends_the_take_at_a_wrap);
  RUN_TEST(test_no_heap);
  return UNITY_END();
}
