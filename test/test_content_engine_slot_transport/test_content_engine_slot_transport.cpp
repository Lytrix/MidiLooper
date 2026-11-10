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
static uint32_t gGrains = 0;    // compose grains per track per tick (0: as many as pending)
static uint32_t gGrainRows = 256;

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
  gX.setBarTicks(0);  // not reset by init: a test that sets it must not leak into the next
  gOut.clear();
  gClock = 0;
  gAccepted = 0;
  gGrains = 0;
  gGrainRows = 256;
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
      for (uint32_t g = 0; (gGrains == 0 || g < gGrains) && t.work(gGrainRows); ++g) {
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
// 59 on the downbeat; 67 starts in window 0 and is still held when window 0 is composed ahead (take tick 1536); 58 at
// the last tick of window 0; 69 is held at Stop.
static const std::vector<Live> kTake = {{kS, kS + 50, 59},          {kS + 1535, kS + 1540, 58},
                                        {kS + 100, kS + 400, 60},   {kS + 1000, kS + 2500, 67},
                                        {kS + 1600, kS + 1700, 62}, {kS + 2000, kS + 2900, 64},
                                        {kS + 3000, kS + 3060, 65}, {kS + 3050, 0, 69}};
static const std::map<uint8_t, Played> kA = {{59, {0, 50}},      {58, {1535, 1540}},
                                             {60, {100, 400}},   {67, {1000, 2500}}, {62, {1600, 1700}},
                                             {64, {2000, 2900}}, {65, {3000, 3060}}, {69, {3050, kLoop}}};

static void recordTake() {
  gClock = kS;
  startRecord(0);
  run(kS + kLoop, kTake);
  stop();
}

// ---- Tests -------------------------------------------------------------------------------------------------------------
void test_record_then_the_take_plays() {
  recordTake();
  run(kS + kLoop + 10);  // inside cycle 1's window 0: the provisional Record source is already released
  TEST_ASSERT_EQUAL_UINT16(Manifests::kManifestCount - gH.heldStates(), gManifests.freeCount());
  TEST_ASSERT_EQUAL_UINT8(0, gX.playingSlot(0));
  TEST_ASSERT_EQUAL_UINT32(kS, gX.origin(0));
  TEST_ASSERT_EQUAL_UINT8(1, gH.undoDepth(0));
  run(kS + 4 * kLoop + 1);  // cycle 3's held note ends on cycle 4's first tick
  // Cycle 1 is whole: window 0 was composed ahead during recording and plays from the Stop tick (E9.1b); the note
  // still held when it was composed keeps sounding into window 1 and ends where the take says.
  TEST_ASSERT_EQUAL_UINT32(1, gX.stats().recordPrepares);
  TEST_ASSERT_EQUAL_UINT32(1, gTrack[0].stats().preparedHandoffs);
  expectCycle(1, kA);
  expectCycle(2, kA);
  TEST_ASSERT_EQUAL_UINT32(0, gTrack[0].stats().underruns);  // no boundary without a window
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
  // 76 in window 0 after the overdub starts: heard in window 0 of cycle 3, right after the wrap (E9.1b).
  const std::vector<Live> live = {{c2 + 200, c2 + 300, 76},
                                  {c2 + 1700, c2 + 1800, 70},
                                  {c2 + 3000, c3 + 200, 72},
                                  {c3 + 2000, c3 + 2100, 74}};
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
  // Cycle 3, right after the wrap: window 0 from the provisional source (76), window 1 from the published state (70);
  // 72 sounded live.
  TEST_ASSERT_EQUAL_UINT32(1, gX.stats().provisionalSelections);
  auto c3n = cycleNotes(3);
  TEST_ASSERT_EQUAL_UINT32(1, c3n[76].size());
  TEST_ASSERT_EQUAL_UINT32(200, c3n[76][0].on);
  TEST_ASSERT_EQUAL_UINT32(300, c3n[76][0].off);
  TEST_ASSERT_EQUAL_UINT32(1, c3n[70].size());
  TEST_ASSERT_EQUAL_UINT32(1700, c3n[70][0].on);
  // Steady: A + B, the held note from 3000 to 200 of the next cycle.
  std::map<uint8_t, Played> ab = kA;
  ab[70] = {1700, 1800};
  ab[72] = {3000, kLoop + 200};
  ab[74] = {2000, 2100};
  ab[76] = {200, 300};
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
  // Across a wrap (a provisional source is taken and adopted), 90 still held at Stop: closed by it.
  run(c1 + kLoop + 2500, {{c1 + 1700, c1 + 1800, 70}, {c1 + kLoop + 2400, 0, 90}});
  TEST_ASSERT_EQUAL_UINT32(1, gX.stats().provisionalSelections);  // cycle 2 stops before its lead
  stop();
  run(kS + 4 * kLoop + 1);  // cycle 3's held note ends on cycle 4's first tick
  std::map<uint8_t, Played> ab = kA;
  ab[70] = {1700, 1800};
  ab[90] = {2400, 2500};  // plays: Stop released its live occupancy
  expectCycle(3, ab);
  Emit buf[kT];
  uint8_t n;
  TEST_ASSERT_EQUAL(R::Ok, gX.undo(0, gClock, buf, kT, n));
  run(kS + 6 * kLoop + 1);
  expectCycle(5, kA);
  TEST_ASSERT_EQUAL(R::Ok, gX.redo(0, gClock, buf, kT, n));
  run(kS + 8 * kLoop + 1);
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
  // Nothing outlives its holder: with the track stopped, every live manifest is a history state (provisional sources
  // and the prepared Record window released theirs).
  TEST_ASSERT_EQUAL_UINT16(Manifests::kManifestCount - gH.heldStates(), gManifests.freeCount());
  TEST_ASSERT_TRUE(gRes.check());
  TEST_ASSERT_TRUE(checkConservation(gPool, gRuns, gManifests));
}

/// DEC-270 — get a loop: a state made elsewhere for this slot (a saved stage's slot, read in) placed on top of a
/// playing slot is one undo step, heard on its track as an undo is; refused, the caller keeping it, while the slot is
/// written.
void test_a_placed_state_is_one_undo_step_heard_on_the_track() {
  recordTake();
  run(kS + 2 * kLoop + 1);
  const uint8_t depth = gH.undoDepth(0);
  auto state = [](uint8_t pitch) {
    const RunId r = gRuns.allocate(0, RunKind::kRecord);
    PassEvent on{}, off{};
    on.tick = 100;
    on.noteId = off.noteId = 900u + pitch;
    on.status = 0x90;
    off.tick = 200;
    off.status = 0x80;
    on.data1 = off.data1 = pitch;
    on.data2 = 100;
    on.seq = 1;
    off.seq = 2;
    TEST_ASSERT_EQUAL(AppendResult::Ok, gRuns.append(r, on, gPool));
    TEST_ASSERT_EQUAL(AppendResult::Ok, gRuns.append(r, off, gPool));
    gRuns.seal(r);
    const ManifestId m = gManifests.create(0, 99, kLoop, &r, 1, gRuns);
    gRuns.release(r);
    return m;
  };
  Emit buf[kT];
  uint8_t n;
  TEST_ASSERT_EQUAL(R::Ok, gX.place(0, state(80), gClock, buf, kT, n));
  gOut.insert(gOut.end(), buf, buf + n);
  TEST_ASSERT_EQUAL_UINT8(depth + 1, gH.undoDepth(0));
  run(kS + 4 * kLoop + 1);
  expectCycle(3, {{80, {100, 200}}});
  TEST_ASSERT_EQUAL(R::Ok, gX.undo(0, gClock, buf, kT, n));
  gOut.insert(gOut.end(), buf, buf + n);
  run(kS + 6 * kLoop + 1);
  expectCycle(5, kA);
  TEST_ASSERT_EQUAL(R::Ok, gX.startOverdub(0, gClock));
  const uint16_t free = gManifests.freeCount();
  const ManifestId held = state(81);
  TEST_ASSERT_EQUAL(R::Busy, gX.place(0, held, gClock, buf, kT, n));
  TEST_ASSERT_TRUE(gManifests.manifest(held).live);  // still the caller's
  gManifests.release(held, gRuns);
  TEST_ASSERT_EQUAL_UINT16(free, gManifests.freeCount());
  stop();
  TEST_ASSERT_TRUE(gRes.check());
}

/// A one-note state for `slot` (pitch `pitch`, 100–200), its creator's reference.
static ManifestId oneNote(uint8_t slot, uint8_t pitch) {
  const RunId r = gRuns.allocate(slot, RunKind::kRecord);
  PassEvent on{}, off{};
  on.tick = 100;
  on.noteId = off.noteId = 900u + pitch;
  on.status = 0x90;
  off.tick = 200;
  off.status = 0x80;
  on.data1 = off.data1 = pitch;
  on.data2 = 100;
  on.seq = 1;
  off.seq = 2;
  TEST_ASSERT_EQUAL(AppendResult::Ok, gRuns.append(r, on, gPool));
  TEST_ASSERT_EQUAL(AppendResult::Ok, gRuns.append(r, off, gPool));
  gRuns.seal(r);
  const ManifestId m = gManifests.create(slot, 99, kLoop, &r, 1, gRuns);
  gRuns.release(r);
  return m;
}

/// DEC-271 — get a loop's audition: heard on its slot's playing track from the next boundary (as an undo is), the
/// history untouched; while it lasts the slot and its track refuse what would change them; cancel brings the slot's own
/// state back and nothing outlives it.
void test_an_audition_is_heard_and_cancel_brings_the_slot_back() {
  recordTake();
  run(kS + 2 * kLoop + 1);
  const ManifestId was = gH.current(0);
  const uint8_t depth = gH.undoDepth(0);
  const uint16_t free = gManifests.freeCount();
  TEST_ASSERT_EQUAL(R::Ok, gX.auditionStart(0, 0, oneNote(0, 80), gClock));
  TEST_ASSERT_TRUE(gX.auditioning() && gX.auditionSounding());
  run(kS + 4 * kLoop + 1);
  expectCycle(3, {{80, {100, 200}}});
  TEST_ASSERT_EQUAL(was, gH.current(0));
  TEST_ASSERT_EQUAL_UINT8(depth, gH.undoDepth(0));
  Emit buf[kT];
  uint8_t n;
  TEST_ASSERT_EQUAL(R::Busy, gX.undo(0, gClock, buf, kT, n));
  TEST_ASSERT_EQUAL(R::Busy, gX.clear(0, gClock, buf, kT, n));
  TEST_ASSERT_EQUAL(R::Busy, gX.startOverdub(0, gClock));
  TEST_ASSERT_EQUAL(R::Busy, gX.launch(0, 1));
  const ManifestId other = oneNote(0, 81);
  TEST_ASSERT_EQUAL(R::Busy, gX.place(0, other, gClock, buf, kT, n));
  TEST_ASSERT_EQUAL(R::Busy, gX.auditionStart(0, 0, other, gClock));  // one at a time: still the caller's
  gManifests.release(other, gRuns);
  TEST_ASSERT_EQUAL(R::Ok, gX.auditionCancel(gClock));
  TEST_ASSERT_FALSE(gX.auditioning());
  run(kS + 6 * kLoop + 1);
  expectCycle(5, kA);
  TEST_ASSERT_EQUAL_UINT16(free, gManifests.freeCount());  // the preview gone once the track let it go
  TEST_ASSERT_EQUAL(R::NoChange, gX.auditionCancel(gClock));
  TEST_ASSERT_TRUE(gRes.check());
}

/// DEC-271: KEEP commits the kept copy as one undo step and the track follows it; undo brings the slot's loop back.
void test_keeping_an_audition_is_one_undo_step() {
  recordTake();
  run(kS + 2 * kLoop + 1);
  const uint8_t depth = gH.undoDepth(0);
  TEST_ASSERT_EQUAL(R::Ok, gX.auditionStart(0, 0, oneNote(0, 80), gClock));
  run(kS + 3 * kLoop + 1);
  const ManifestId kept = oneNote(0, 80);
  Emit buf[kT];
  uint8_t n;
  TEST_ASSERT_EQUAL(R::Ok, gX.auditionKeep(kept, gClock, buf, kT, n));
  TEST_ASSERT_FALSE(gX.auditioning());
  TEST_ASSERT_EQUAL(kept, gH.current(0));
  TEST_ASSERT_EQUAL_UINT8(depth + 1, gH.undoDepth(0));
  run(kS + 5 * kLoop + 1);
  expectCycle(4, {{80, {100, 200}}});
  TEST_ASSERT_EQUAL(R::Ok, gX.undo(0, gClock, buf, kT, n));
  run(kS + 7 * kLoop + 1);
  expectCycle(6, kA);
  const ManifestId stray = oneNote(0, 82);
  TEST_ASSERT_EQUAL(R::NoChange, gX.auditionKeep(stray, gClock, buf, kT, n));  // nothing auditioned: not taken
  gManifests.release(stray, gRuns);
  TEST_ASSERT_TRUE(gRes.check());
}

/// DEC-271: a slot its track does not play is previewed silently (the track plays on, nothing launched); a track that
/// stops ends the sound, and a cancel then restarts nothing.
void test_an_audition_off_its_slot_is_silent_and_a_stop_restarts_nothing() {
  recordTake();
  run(kS + 2 * kLoop + 1);
  TEST_ASSERT_EQUAL(R::Ok, gX.auditionStart(0, 1, oneNote(1, 80), gClock));
  TEST_ASSERT_FALSE(gX.auditionSounding());
  run(kS + 4 * kLoop + 1);
  expectCycle(3, kA);
  TEST_ASSERT_EQUAL_UINT8(0, gX.playingSlot(0));
  TEST_ASSERT_EQUAL(R::Ok, gX.auditionCancel(gClock));
  TEST_ASSERT_EQUAL(R::Ok, gX.auditionStart(0, 0, oneNote(0, 80), gClock));
  Emit buf[kT];
  gOut.insert(gOut.end(), buf, buf + gX.stopTrack(0, gClock, buf, kT));
  TEST_ASSERT_FALSE(gX.auditionSounding());
  TEST_ASSERT_EQUAL(R::Ok, gX.auditionCancel(gClock));
  TEST_ASSERT_FALSE(gX.playing(0));
  const size_t at = gOut.size();
  run(kS + 6 * kLoop);
  for (size_t i = at; i < gOut.size(); ++i) TEST_ASSERT_NOT_EQUAL(1, gOut[i].cls);  // nothing starts again
  TEST_ASSERT_TRUE(gRes.check());
}

/// Track 0's NoteOns of `pitch` and NoteOffs of it, as loop positions within local cycle `cycle` (ticks relative to the
/// cycle start; an off may lie past it).
static std::vector<uint32_t> onsAt(uint32_t cycle, uint8_t pitch, int cls) {
  std::vector<uint32_t> v;
  for (const Emit& e : gOut)
    if (e.cls == cls && e.pitch == pitch && e.tick >= cycle * kLoop && e.tick < (cycle + 1) * kLoop)
      v.push_back(e.tick - cycle * kLoop);
  return v;
}

/// DEC-271 amendment (owner: "Need beat line"): an audition is heard from the next beat line — the window playing is
/// composed from the preview and joined there: a preview note sounding across the beat starts at it, what the old
/// source sounded ends at it — and a cancel goes back on the next beat the same way.
void test_an_audition_and_its_cancel_switch_on_the_next_beat() {
  gX.setBarTicks(768);  // beats of 192 ticks
  recordTake();
  run(kS + 3 * kLoop + 50);  // cycle 3, position 50: the next beat is 192
  TEST_ASSERT_EQUAL(R::Ok, gX.auditionStart(0, 0, oneNote(0, 80), gClock));
  run(kS + 4 * kLoop + 50);
  TEST_ASSERT_TRUE(onsAt(3, 59, 1) == std::vector<uint32_t>{0});    // the old source before the beat
  TEST_ASSERT_TRUE(onsAt(3, 60, 1) == std::vector<uint32_t>{100});
  TEST_ASSERT_TRUE(onsAt(3, 60, 0) == std::vector<uint32_t>{192});  // ended at the join
  TEST_ASSERT_TRUE(onsAt(3, 80, 1) == std::vector<uint32_t>{192});  // 100–200, sounding at the beat: starts there
  TEST_ASSERT_TRUE(onsAt(3, 80, 0) == std::vector<uint32_t>{200});
  TEST_ASSERT_TRUE(onsAt(3, 67, 1).empty());                         // nothing of the old source after it
  // Cancel at cycle 4, position 50 (the preview's 80 plays at 100): back on the beat at 192.
  TEST_ASSERT_EQUAL(R::Ok, gX.auditionCancel(gClock));
  run(kS + 6 * kLoop + 1);
  TEST_ASSERT_TRUE(onsAt(4, 80, 1) == std::vector<uint32_t>{100});
  TEST_ASSERT_TRUE(onsAt(4, 80, 0) == std::vector<uint32_t>{192});  // ended at the join
  TEST_ASSERT_TRUE(onsAt(4, 60, 1) == std::vector<uint32_t>{192});  // the slot's 100–400 sounding there: starts
  TEST_ASSERT_TRUE(onsAt(4, 67, 1) == std::vector<uint32_t>{1000});  // and its loop plays on
  expectCycle(5, kA);
  TEST_ASSERT_TRUE(gRes.check());
}

/// A beat that is the window's end is the boundary: the switch is the ordinary one there.
void test_an_audition_on_the_last_beat_of_a_window_switches_at_the_boundary() {
  gX.setBarTicks(768);
  recordTake();
  run(kS + 3 * kLoop + kWin - 100);  // the next beat is the window's end (1536)
  TEST_ASSERT_EQUAL(R::Ok, gX.auditionStart(0, 0, oneNote(0, 80), gClock));
  run(kS + 5 * kLoop + 1);
  TEST_ASSERT_TRUE(onsAt(3, 58, 1) == std::vector<uint32_t>{1535});  // the old source to the window's end
  TEST_ASSERT_TRUE(onsAt(3, 80, 1).empty());                         // 80 lies before the switch in this cycle
  TEST_ASSERT_TRUE(onsAt(3, 64, 1).empty());                         // nothing of the old source past it
  TEST_ASSERT_TRUE(onsAt(4, 80, 1) == std::vector<uint32_t>{100});
  TEST_ASSERT_EQUAL(R::Ok, gX.auditionCancel(gClock));
  TEST_ASSERT_TRUE(gRes.check());
}

/// DEC-272: an empty loop of a given length on top of any slot — a filled one too — is one undo step; its playing
/// track goes quiet from the next boundary; undo brings the loop back; refused while the slot is written.
void test_an_empty_loop_over_a_filled_slot_is_one_undo_step() {
  recordTake();
  run(kS + 2 * kLoop + 1);
  const ManifestId was = gH.current(0);
  const uint8_t depth = gH.undoDepth(0);
  Emit buf[kT];
  uint8_t n;
  TEST_ASSERT_EQUAL(R::Ok, gX.emptyLoop(0, 2 * 768, gClock, buf, kT, n));
  gOut.insert(gOut.end(), buf, buf + n);
  TEST_ASSERT_EQUAL_UINT8(depth + 1, gH.undoDepth(0));
  TEST_ASSERT_EQUAL_UINT32(2 * 768, gManifests.manifest(gH.current(0)).loopLengthTicks);
  TEST_ASSERT_TRUE(gX.contentOf(0) != kNoManifest);  // an empty loop is content: it plays, is edited, saved
  const size_t at = gOut.size();
  run(kS + 4 * kLoop + 1);
  size_t ons = 0;
  for (size_t i = at; i < gOut.size(); ++i) ons += gOut[i].cls == 1 && gOut[i].tick >= 3 * kLoop ? 1 : 0;
  TEST_ASSERT_EQUAL(0u, ons);  // nothing plays from it
  TEST_ASSERT_EQUAL(R::Ok, gX.undo(0, gClock, buf, kT, n));
  TEST_ASSERT_EQUAL(was, gH.current(0));
  TEST_ASSERT_EQUAL(R::NoChange, gX.emptyLoop(0, 0, gClock, buf, kT, n));
  TEST_ASSERT_EQUAL(R::Ok, gX.startOverdub(0, gClock));
  TEST_ASSERT_EQUAL(R::Busy, gX.emptyLoop(0, 768, gClock, buf, kT, n));
  stop();
  TEST_ASSERT_TRUE(gRes.check());
}

void test_an_undo_on_a_boundary_is_heard_from_that_boundary() {
  recordTake();
  run(kS + kLoop + 10);
  TEST_ASSERT_EQUAL(R::Ok, gX.startOverdub(0, gClock));
  run(kS + kLoop + 2500, {{kS + kLoop + 200, kS + kLoop + 300, 76}});  // in window 0
  stop();
  run(kS + 4 * kLoop);  // tick 4L (cycle 4, position 0) not played yet
  Emit buf[kT];
  uint8_t n;
  TEST_ASSERT_EQUAL(R::Ok, gX.undo(0, gClock, buf, kT, n));
  run(kS + 5 * kLoop + 1);
  expectCycle(4, kA);  // the whole of cycle 4, window 0 included
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

void test_the_lead_covers_window_composition_under_a_grain_budget() {
  recordTake();
  run(kS + 2 * kLoop);
  gGrains = 1;  // one grain of 8 row units per track per tick: a window takes many ticks to compose
  gGrainRows = 8;
  TEST_ASSERT_EQUAL(R::Ok, gX.startOverdub(0, gClock));
  const uint32_t c2 = kS + 2 * kLoop, c3 = kS + 3 * kLoop;
  run(c3 + 600, {{c2 + 200, c2 + 300, 76}});
  TEST_ASSERT_EQUAL_UINT32(1, gX.stats().provisionalSelections);
  auto n = cycleNotes(3);  // right after the wrap: the provisional window 0 was ready in time
  TEST_ASSERT_EQUAL_UINT32(1, n[76].size());
  TEST_ASSERT_EQUAL_UINT32(200, n[76][0].on);
  TEST_ASSERT_EQUAL_UINT32(1, n[59].size());  // and the take's downbeat
  TEST_ASSERT_EQUAL_UINT32(0, gTrack[0].stats().underruns);
}

void test_a_short_take_composes_at_stop() {
  gClock = kS;
  startRecord(0);
  const uint32_t loop = 1200;  // Stop before window 0 can be composed ahead
  run(kS + loop, {{kS + 100, kS + 300, 60}, {kS + 900, kS + 1000, 62}});
  stop();
  TEST_ASSERT_EQUAL_UINT32(0, gX.stats().recordPrepares);
  run(kS + 4 * loop);
  // Composed at Stop, aimed at the Stop tick: with ample worker time it is ready there, so it plays every cycle.
  uint32_t ons60 = 0;
  for (const Emit& e : gOut)
    if (e.cls == 1 && e.pitch == 60) {
      TEST_ASSERT_EQUAL_UINT32(100, e.tick % loop);
      ++ons60;
    }
  TEST_ASSERT_EQUAL_UINT32(3, ons60);
  TEST_ASSERT_EQUAL_UINT32(0, gTrack[0].stats().underruns);
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
  RUN_TEST(test_a_placed_state_is_one_undo_step_heard_on_the_track);
  RUN_TEST(test_an_audition_is_heard_and_cancel_brings_the_slot_back);
  RUN_TEST(test_keeping_an_audition_is_one_undo_step);
  RUN_TEST(test_an_audition_off_its_slot_is_silent_and_a_stop_restarts_nothing);
  RUN_TEST(test_an_audition_and_its_cancel_switch_on_the_next_beat);
  RUN_TEST(test_an_audition_on_the_last_beat_of_a_window_switches_at_the_boundary);
  RUN_TEST(test_an_empty_loop_over_a_filled_slot_is_one_undo_step);
  RUN_TEST(test_an_undo_on_a_boundary_is_heard_from_that_boundary);
  RUN_TEST(test_one_writer_and_named_refusals);
  RUN_TEST(test_a_full_run_list_ends_the_take_at_a_wrap);
  RUN_TEST(test_the_lead_covers_window_composition_under_a_grain_budget);
  RUN_TEST(test_a_short_take_composes_at_stop);
  RUN_TEST(test_no_heap);
  return UNITY_END();
}
