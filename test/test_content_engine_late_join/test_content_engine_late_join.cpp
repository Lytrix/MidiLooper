//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// fixed-memory-engine E9.4: bounded late join (owner ruling 2026-11-05), the required REDs. A transition whose window
// completes after its boundary becomes audible inside that window when it completes within the limit — the earlier of
// 24 ticks and 25 ms (9 ticks at 120 BPM) — joining at the current tick: notes sounding there start late with their
// original end, notes wholly in the missed span are not emitted, later events keep their schedule, old-source notes the
// new source carries keep sounding and the others end, the join state is prepared in worker grains (the Clock tick
// only adopts it), and later cycles equal uninterrupted composition. Beyond the limit the boundary rule holds and the
// miss is counted. Per-port admission differs without orphan NoteOffs.

#include <unity.h>

#include <algorithm>
#include <cstdint>
#include <map>
#include <tuple>
#include <vector>

#include "ContentEngine/Playback/ClockOutput.h"
#include "ContentEngine/Composition/SpanWindowComposer.h"
#include "ContentEngine/EngineCore.h"
#include "ContentEngine/Playback/MidiOutputHub.h"
#include "ContentEngine/Playback/PlaybackTrack.h"

using namespace ContentEngine;

using Pool = PagePool<1024, 64>;
using Runs = RunTable<32, Pool>;
using TestComposer = Composer<Pool, Runs>;
using Window = SpanWindow<3072>;
using Table = CheckpointTable<TestComposer::Checkpoint, 2, 8>;
using Manifests = ManifestTable<16, 8>;
constexpr uint16_t kT = Config::kMaxSoundingPerTrack;
using Track = PlaybackTrack<TestComposer, Table, Window, Manifests, Runs, kT>;
using Player = WindowPlayer<kT>;
using Emit = Player::Emit;

constexpr uint32_t kWin = 1536;
constexpr uint32_t kLimit300 = 24;             // 300 BPM: 1,040 µs per tick, 24 ticks < 25 ms
constexpr uint32_t kLimit120 = 25000 / 2604;   // 120 BPM: 25 ms is 9 ticks

static Pool gPool;
static Runs gRuns;
static Manifests gManifests;
static Table gTable;
static TestComposer gA, gB, gRef;
static Window gBuf[3], gRefWin;
static TerminalScheduler<kT> gSched, gRefSched;
static Player gPlayer, gRefPlayer;
static Track gTrack;
static std::vector<Emit> gOut;
static uint32_t gTick = 0;

void setUp() {
  gPool.init();
  gRuns.init();
  gManifests.init();
  gTable.init();
  for (auto* c : {&gA, &gB, &gRef}) c->init(gPool, gRuns, nullptr);
  gSched.init();
  gRefSched.init();
  gPlayer.init(gSched);
  gRefPlayer.init(gRefSched);
  Window* bufs[3] = {&gBuf[0], &gBuf[1], &gBuf[2]};
  gTrack.init(gA, gB, bufs, gTable, gManifests, gRuns, gPlayer, kWin);
  gOut.clear();
  gTick = 0;
}
void tearDown() {}

struct N {
  uint32_t id;
  uint8_t pitch;
  uint32_t on, off;
};
struct Cc {
  uint32_t tick;
  uint8_t status, d1, d2;
};
static RunId run(const std::vector<N>& notes, const std::vector<Cc>& events = {}) {
  const RunId r = gRuns.allocate(0, RunKind::kRecord);
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
  std::vector<PassEvent> rows;
  std::stable_sort(ev.begin(), ev.end(), [](const Ev& a, const Ev& b) { return a.tick < b.tick || (a.tick == b.tick && !a.on && b.on); });
  for (const Ev& e : ev) {
    PassEvent p{};
    p.tick = e.tick;
    p.noteId = e.n.id;
    p.status = e.on ? 0x90 : 0x80;
    p.data1 = e.n.pitch;
    p.data2 = e.on ? 100 : 0;
    rows.push_back(p);
  }
  for (const Cc& c : events) {
    PassEvent p{};
    p.tick = c.tick;
    p.status = c.status;
    p.data1 = c.d1;
    p.data2 = c.d2;
    rows.push_back(p);
  }
  std::stable_sort(rows.begin(), rows.end(), [](const PassEvent& a, const PassEvent& b) { return a.tick < b.tick; });
  for (const PassEvent& p : rows) TEST_ASSERT_EQUAL(AppendResult::Ok, gRuns.append(r, p, gPool));
  gRuns.seal(r);
  return r;
}
/// Events (not notes) with `d1` sent in [from, to).
static std::vector<uint32_t> eventsOf(uint8_t d1, uint32_t from, uint32_t to) {
  std::vector<uint32_t> v;
  for (const Emit& e : gOut)
    if (e.cls == 2 && e.pitch == d1 && e.tick >= from && e.tick < to) v.push_back(e.tick);
  return v;
}
static ManifestId manifest(std::vector<RunId> runs, uint32_t loop) {
  const ManifestId m = gManifests.create(0, 1, loop, runs.data(), static_cast<uint8_t>(runs.size()), gRuns);
  return m;
}

/// Ticks up to `to`. Steady work runs freely; transition work only from tick `transFrom` on, `rows` units a grain and
/// at most `grains` grains a tick (0: until done) — so the window completes when the test says.
static void play(uint32_t to, uint32_t transFrom, uint32_t rows = 256, uint32_t grains = 0) {
  Emit buf[64];
  for (; gTick < to; ++gTick) {
    while (gTrack.workSteady(256)) {
    }
    if (gTick >= transFrom)
      for (uint32_t g = 0; (grains == 0 || g < grains) && gTrack.workTransition(rows); ++g) {
      }
    const uint8_t n = gTrack.tick(gTick, buf, 64);
    gOut.insert(gOut.end(), buf, buf + n);
  }
}
/// NoteOn ticks of `pitch` in [from, to), and the first NoteOff after each.
static std::vector<std::pair<uint32_t, uint32_t>> notesOf(uint8_t pitch, uint32_t from, uint32_t to) {
  std::vector<std::pair<uint32_t, uint32_t>> v;
  for (size_t i = 0; i < gOut.size(); ++i) {
    const Emit& e = gOut[i];
    if (e.cls != 1 || e.pitch != pitch || e.tick < from || e.tick >= to) continue;
    uint32_t off = 0xFFFFFFFFu;
    for (size_t k = i + 1; k < gOut.size(); ++k)
      if (gOut[k].cls == 0 && gOut[k].noteId == e.noteId && gOut[k].runKey == e.runKey) {
        off = gOut[k].tick;
        break;
      }
    v.push_back({e.tick, off});
  }
  return v;
}
/// Every NoteOff closes a sounding instance; nothing is left sounding after `settle`.
static void expectNoOrphans() {
  std::map<std::pair<uint32_t, uint16_t>, int> open;
  for (const Emit& e : gOut) {
    const auto k = std::make_pair(e.noteId, e.runKey);
    if (e.cls == 1) ++open[k];
    if (e.cls == 0) {
      TEST_ASSERT_TRUE(open[k] > 0);
      --open[k];
    }
  }
}

// A one- or two-bar take: a long note across any join, two short notes, a note after the join span.
static std::vector<N> take() {
  return {{1, 60, 0, 500}, {2, 62, 2, 6}, {3, 63, 20, 30}, {4, 64, 100, 140}};
}

/// Selects `m` on a track playing nothing, aimed at the boundary `b` (cycle 2's start); its window may complete from
/// tick `b + d` on; the limit is `limit`. Returns b.
static uint32_t lateTake(uint32_t loop, uint32_t d, uint32_t limit) {
  // Sustain on at 3 (essential: kept across a join), a volume change at 4 (continuous: the next value follows).
  const ManifestId m = manifest({run(take(), {{3, 0xB0, 64, 127}, {4, 0xB0, 7, 90}})}, loop);
  gTrack.setLateJoinLimit(limit);
  const uint32_t b = 2 * loop;
  gTick = b - 1;
  gTrack.select(0, m, b - 1);
  play(b + d, b + d);  // nothing composes before b + d
  play(b + 3 * loop + 1, b + d);
  return b;
}

// ---- REDs --------------------------------------------------------------------------------------------------------------
void test_takes_join_late_within_24_ticks_at_300_bpm() {
  for (uint32_t loop : {768u, 1536u})
    for (uint32_t d : {1u, 10u, 24u}) {
      setUp();
      const uint32_t b = lateTake(loop, d, kLimit300);
      const uint32_t j = b + d;
      TEST_ASSERT_EQUAL_UINT32(1, gTrack.stats().lateJoins);
      TEST_ASSERT_EQUAL_UINT32(d, gTrack.stats().lastLateTicks);
      TEST_ASSERT_EQUAL_UINT32(0, gTrack.stats().lateJoinsMissed);
      // The long note starts late, once, and keeps its end.
      const auto n60 = notesOf(60, b, b + loop);
      TEST_ASSERT_EQUAL_UINT32(1, n60.size());
      TEST_ASSERT_EQUAL_UINT32(j, n60[0].first);
      TEST_ASSERT_EQUAL_UINT32(b + 500, n60[0].second);
      // Short notes wholly in the missed span are not emitted; one crossing the join starts late.
      TEST_ASSERT_EQUAL_UINT32(d >= 6 ? 0 : 1, notesOf(62, b, b + loop).size());
      const auto n63 = notesOf(63, b, b + loop);
      TEST_ASSERT_EQUAL_UINT32(1, n63.size());
      TEST_ASSERT_EQUAL_UINT32(d >= 20 ? j : b + 20, n63[0].first);
      TEST_ASSERT_EQUAL_UINT32(b + 30, n63[0].second);
      // Missed-span state: sustain at the join (or on time), the continuous controller only when not missed.
      const auto sus = eventsOf(64, b, b + loop), vol = eventsOf(7, b, b + loop);
      TEST_ASSERT_EQUAL_UINT32(1, sus.size());
      TEST_ASSERT_EQUAL_UINT32(d > 3 ? j : b + 3, sus[0]);
      TEST_ASSERT_EQUAL_UINT32(d > 4 ? 0 : 1, vol.size());
      // After the join, the schedule; the next cycle whole.
      TEST_ASSERT_EQUAL_UINT32(b + 100, notesOf(64, b, b + loop)[0].first);
      for (uint8_t p : {60, 62, 63, 64}) TEST_ASSERT_EQUAL_UINT32(1, notesOf(p, b + loop, b + 2 * loop).size());
      TEST_ASSERT_EQUAL_UINT32(b + loop, notesOf(60, b + loop, b + 2 * loop)[0].first);
      expectNoOrphans();
    }
}

void test_the_25_ms_cap_and_just_beyond_either_limit() {
  struct Case {
    uint32_t limit, d;
    bool joins;
  };
  for (const Case& c : {Case{kLimit120, kLimit120, true}, Case{kLimit120, kLimit120 + 1, false},
                        Case{kLimit300, kLimit300 + 1, false}}) {
    setUp();
    const uint32_t loop = 768;
    const uint32_t b = lateTake(loop, c.d, c.limit);
    TEST_ASSERT_EQUAL_UINT32(c.joins ? 1 : 0, gTrack.stats().lateJoins);
    TEST_ASSERT_EQUAL_UINT32(c.joins ? 0 : 1, gTrack.stats().lateJoinsMissed);
    // Beyond the limit: nothing of the missed cycle sounds; the take plays from the next boundary, whole.
    TEST_ASSERT_EQUAL_UINT32(c.joins ? 1 : 0, notesOf(60, b, b + loop).size());
    TEST_ASSERT_EQUAL_UINT32(b + loop, notesOf(60, b + loop, b + 2 * loop)[0].first);
    expectNoOrphans();
  }
}

void test_notes_already_sounding_from_the_previous_source() {
  const uint32_t loop = 1536;
  const RunId ra = run({{1, 50, 0, 1500}, {2, 51, 100, 900}});
  const RunId rb = run({{10, 52, 0, 300}});
  const ManifestId a = manifest({ra}, loop), ab = manifest({ra, rb}, loop);
  gTrack.setLateJoinLimit(kLimit300);
  gTick = 0;
  gTrack.select(0, a, 0);  // A plays from cycle 1
  play(2 * loop - 1, 0);
  // Redo-like: A -> A + B at cycle 2, ready 10 ticks late. X (pitch 50) sounds from A and is carried: no restart.
  gTrack.select(0, ab, 2 * loop - 1);
  play(2 * loop + 10, 2 * loop + 10);
  play(3 * loop - 1, 0);
  const uint32_t b = 2 * loop;
  TEST_ASSERT_EQUAL_UINT32(1, gTrack.stats().lateJoins);
  const auto x = notesOf(50, b, b + loop);
  TEST_ASSERT_EQUAL_UINT32(1, x.size());
  TEST_ASSERT_EQUAL_UINT32(b, x[0].first);         // from A at the boundary
  TEST_ASSERT_EQUAL_UINT32(b + 1500, x[0].second);  // not cut at the join
  const auto z = notesOf(52, b, b + loop);
  TEST_ASSERT_EQUAL_UINT32(1, z.size());
  TEST_ASSERT_EQUAL_UINT32(b + 10, z[0].first);  // B's note joins late
  TEST_ASSERT_EQUAL_UINT32(b + 300, z[0].second);
  // Undo-like: back to A at cycle 3, ready 10 ticks late: Z (only in A + B) ends at the join, X goes on.
  gTrack.select(0, a, 3 * loop - 1);
  play(3 * loop + 10, 3 * loop + 10);
  play(4 * loop + 1, 0);
  const uint32_t b2 = 3 * loop;
  TEST_ASSERT_EQUAL_UINT32(2, gTrack.stats().lateJoins);
  const auto z2 = notesOf(52, b2, b2 + loop);
  TEST_ASSERT_EQUAL_UINT32(1, z2.size());
  TEST_ASSERT_EQUAL_UINT32(b2 + 10, z2[0].second);  // closed at the join: A does not have it
  TEST_ASSERT_EQUAL_UINT32(b2 + 1500, notesOf(50, b2, b2 + loop)[0].second);
  expectNoOrphans();
}

void test_dense_midi_at_the_join_is_prepared_by_workers() {
  const uint32_t loop = 1536;
  std::vector<N> dense;
  uint32_t id = 100;
  for (uint32_t k = 0; k < 300; ++k) dense.push_back({id++, static_cast<uint8_t>(20 + k % 60), k % 20, k % 20 + 2});
  for (uint8_t k = 0; k < 16; ++k) dense.push_back({id++, static_cast<uint8_t>(90 + k), 5, 1000});
  const ManifestId m = manifest({run(dense)}, loop);
  gTrack.setLateJoinLimit(kLimit300);
  const uint32_t b = 2 * loop, d = 24;
  gTick = b - 1;
  gTrack.select(0, m, b - 1);
  play(b + d, b + d + 1);  // nothing composes before the join tick
  uint32_t grains = 0;     // then the window and the join state, in 8-unit worker grains
  while (gTrack.workTransition(8)) ++grains;
  const size_t before = gOut.size();
  Emit buf[64];
  const uint8_t atJoin = gTrack.tick(gTick, buf, 64);  // the Clock tick only adopts the prepared state
  gOut.insert(gOut.end(), buf, buf + atJoin);
  ++gTick;
  TEST_ASSERT_EQUAL_UINT32(before + atJoin, gOut.size());
  TEST_ASSERT_EQUAL_UINT32(1, gTrack.stats().lateJoins);
  TEST_ASSERT_TRUE(grains > 40);  // window and join scan took many bounded grains, none in the Clock tick
  TEST_ASSERT_TRUE(atJoin <= kT);  // the dense missed span emitted nothing: only the long notes start late
  for (uint8_t i = 0; i < atJoin; ++i) {
    TEST_ASSERT_EQUAL_UINT8(1, buf[i].cls);
    TEST_ASSERT_TRUE(buf[i].pitch >= 90);
  }
  play(b + 2 * loop, 0);
  expectNoOrphans();
}

void test_a_join_point_the_clock_passes_moves_on() {
  // One small grain a tick: the window and its join state take several ticks, so the prepared join point is passed and
  // moves on; notes that ended meanwhile never start.
  const uint32_t loop = 1536;
  std::vector<N> notes;
  uint32_t id = 1;
  for (uint32_t k = 0; k < 20; ++k) notes.push_back({id++, static_cast<uint8_t>(30 + k), k, k + 2});  // short
  for (uint8_t k = 0; k < 12; ++k)  // long, one starting on every tick: some start exactly at a passed join point
    notes.push_back({id++, static_cast<uint8_t>(90 + k), static_cast<uint32_t>(2 + k), 900});
  const ManifestId m = manifest({run(notes)}, loop);
  gTrack.setLateJoinLimit(kLimit300);
  const uint32_t b = 2 * loop;
  gTick = b - 1;
  gTrack.select(0, m, b - 1);
  play(b + 2 * loop, b + 1, 16, 1);
  TEST_ASSERT_EQUAL_UINT32(1, gTrack.stats().lateJoins);
  const uint32_t j = b + gTrack.stats().lastLateTicks;
  TEST_ASSERT_TRUE(j > b + 3);  // passed more than once
  for (const Emit& e : gOut)
    if (e.cls == 1 && e.tick >= b && e.tick < b + loop) {
      TEST_ASSERT_TRUE(e.tick >= j);
      TEST_ASSERT_TRUE(e.endTick == kOpenEnd || e.endTick > e.tick);  // nothing started that had already ended
    }
  for (uint8_t k = 0; k < 12; ++k) {  // every long note sounds once: late at the join, or on time after it
    const auto n = notesOf(static_cast<uint8_t>(90 + k), b, b + loop);
    TEST_ASSERT_EQUAL_UINT32(1, n.size());
    TEST_ASSERT_EQUAL_UINT32(std::max<uint32_t>(j, b + 2 + k), n[0].first);
  }
  expectNoOrphans();
}

void test_a_launch_ends_what_sounded_at_its_commit() {
  // The old slot's long note sounds at the commit; the launched slot is not ready even after the late limit, so its
  // window is adopted at the next boundary — the old note must still end at the commit.
  const uint32_t loop = 1536;
  const ManifestId a = manifest({run({{1, 50, 0, 1500}})}, loop);
  const ManifestId b = manifest({run({{2, 60, 0, 100}})}, loop);
  gTrack.setLateJoinLimit(kLimit300);
  gTick = 0;
  gTrack.select(0, a, 0);
  play(loop + 200, 0);  // A's note sounds from 1536
  gTrack.launch(0, b);
  Emit buf[kT];
  const uint8_t n = gTrack.commitLaunch(buf, kT);  // the timeline restarts: tick 0 is next
  TEST_ASSERT_EQUAL_UINT8(1, n);
  TEST_ASSERT_EQUAL_UINT8(0, buf[0].cls);
  TEST_ASSERT_EQUAL_UINT8(50, buf[0].pitch);
  gOut.clear();
  gTick = 0;
  play(kLimit300 + 2, 1000000);  // no transition work: the launch misses its late window
  play(loop + 10, 0);
  TEST_ASSERT_EQUAL_UINT32(1, gTrack.stats().lateJoinsMissed);
  TEST_ASSERT_EQUAL_UINT32(0, notesOf(50, 0, loop + 10).size());
  TEST_ASSERT_EQUAL_UINT32(loop, notesOf(60, 0, loop + 10)[0].first);
  expectNoOrphans();
}

void test_din_and_usb_diverge_when_din_cannot_admit_the_late_note() {
  using Hub = OutputHub<>;
  using Out = ClockOutput<Hub, Emit, 1, kT, 3>;
  static Hub hub;
  static Out out;
  hub.init();
  hub.configure(0, kDinMessageMicros);
  hub.configure(1, kUsbMessageMicros);
  out.init(hub);
  out.setPorts(0, 0x3);  // DIN + USB device
  const ManifestId m = manifest({run({{1, 60, 0, 500}})}, 768);
  gTrack.setLateJoinLimit(kLimit300);
  gTick = 2 * 768 - 1;
  gTrack.select(0, m, gTick);
  play(2 * 768 + 10, 2 * 768 + 10);
  play(2 * 768 + 600, 0);
  // Route each event at its own time, the ports draining in between (as the firmware does each tick).
  Hub::Sent s[64], usbSent[8];
  uint8_t usb = 0;
  bool dinSentIt = false;
  uint32_t din = 0, now = 0;
  auto drain = [&](uint32_t until) {
    for (; now <= until; now += 125) {
      usb = static_cast<uint8_t>(usb + hub.transmit(now, 1, usbSent + usb, static_cast<uint8_t>(8 - usb)));
      const uint8_t k = hub.transmit(now, 0, s, 64);
      for (uint8_t i = 0; i < k; ++i) dinSentIt |= s[i].data1 == 60;
      din += k;
    }
  };
  for (Emit e : gOut) {
    e.track = 0;
    drain(e.tick * 1040);
    uint32_t suppressed = 0;
    if (e.cls == 1) {  // DIN's queue is full of other NoteOns when the late note comes
      for (uint32_t k = 0; hub.canAdmitNoteOn(0); ++k) hub.noteOn(now, 0, 0x90, 30, 90, 0x40000000u + k);
      suppressed = hub.port(0).counters().suppressedNoteOn;
    }
    out.route(&e, 1, e.tick * 1040, 1040);
    if (e.cls == 1) TEST_ASSERT_EQUAL_UINT32(suppressed + 1, hub.port(0).counters().suppressedNoteOn);  // not on DIN
  }
  drain(now + 200000);
  TEST_ASSERT_EQUAL_UINT8(2, usb);  // USB: the late NoteOn and its NoteOff
  TEST_ASSERT_EQUAL_HEX8(0x90, usbSent[0].status);
  TEST_ASSERT_EQUAL_HEX8(0x80, usbSent[1].status);
  TEST_ASSERT_FALSE(dinSentIt);  // DIN: neither the note nor an orphan NoteOff for it
  TEST_ASSERT_TRUE(din > 0);
}

void test_later_cycles_equal_uninterrupted_composition() {
  const uint32_t loop = 1536;
  const RunId r = run(take());
  const ManifestId m = manifest({r}, loop);
  gTrack.setLateJoinLimit(kLimit300);
  const uint32_t b = 2 * loop;
  gTick = b - 1;
  gTrack.select(0, m, b - 1);
  play(b + 10, b + 10);
  play(b + 4 * loop, 0);
  // Reference: the same source composed uninterrupted from cycle 0.
  const SourceRun src[1] = {{r, gRuns.run(r).rowCount}};
  TEST_ASSERT_TRUE(gRef.begin(src, 1, loop, gManifests.manifest(m).identity));
  std::vector<Emit> ref;
  Emit buf[64];
  for (uint32_t t = 0; t < 6 * loop; ++t) {
    if ((t % loop) % kWin == 0) {
      if (!gRef.composeNext(gRefWin, kWin)) {
        gRef.nextCycle();
        TEST_ASSERT_TRUE(gRef.composeNext(gRefWin, kWin));
      }
      const uint8_t n = gRefPlayer.beginWindow(gRefWin, buf, 64);
      ref.insert(ref.end(), buf, buf + n);
    }
    const uint8_t n = gRefPlayer.tick(t, buf, 64);
    ref.insert(ref.end(), buf, buf + n);
  }
  auto cycle = [](const std::vector<Emit>& v, uint32_t from, uint32_t to) {
    std::vector<std::tuple<uint32_t, uint8_t, uint8_t>> c;
    for (const Emit& e : v)
      if (e.tick >= from && e.tick < to) c.emplace_back(e.tick - from, e.cls, e.pitch);
    return c;
  };
  for (uint32_t k = 1; k < 4; ++k) {
    const auto got = cycle(gOut, b + k * loop, b + (k + 1) * loop), want = cycle(ref, (2 + k) * loop, (3 + k) * loop);
    TEST_ASSERT_TRUE(got == want);
  }
}

// ---- NOTE_EDIT joins (owner 2026-11-07: "hear the changes instantly") -------------------------------------------------
// The edited source joins the window playing at the next tick: a note moved just ahead of the playhead plays, one moved
// just behind it is not heard this cycle, a sounding note the edit keeps on its pitch goes on with its new end, one
// it moved to another pitch ends at the join, a new note sounding there does not start late. A 1-bar loop (shorter than
// the window) and the second window of a two-window loop.
void test_an_edit_joins_the_window_playing() {
  for (uint32_t loop : {768u, 3072u}) {
    setUp();
    const uint32_t o = loop > kWin ? kWin : 0;  // the window the edit lands in
    const RunId ra = run({{1, 60, o + 100, o + 200}, {2, 62, o + 600, o + 700}, {3, 64, o + 300, o + 700},
                          {5, 66, o + 300, o + 700}});
    const RunId rb = run({{1, 60, o + 500, o + 550},    // ahead of the playhead: plays
                          {2, 62, o + 350, o + 450},    // just behind it: from the next cycle
                          {3, 64, o + 300, o + 650},    // sounding, kept: the new end
                          {5, 67, o + 300, o + 700},    // sounding, another pitch: ends at the join
                          {4, 65, o + 380, o + 600}},   // new, sounding at the join: not started late
                         {{o + 50, 0xB0, 64, 127}});    // sustain in the passed span: not sent again
    const ManifestId a = manifest({ra}, loop), e = manifest({rb}, loop);
    gTick = 0;
    gTrack.select(0, a, 0);
    const uint32_t b = 2 * loop, j = b + o + 401;
    play(j, 0);
    gTrack.edit(0, e, j - 1);
    play(b + 2 * loop, 0);
    TEST_ASSERT_EQUAL_UINT32(1, gTrack.stats().editJoins);
    TEST_ASSERT_EQUAL_UINT32(0, gTrack.stats().lateJoins);
    const uint32_t c = b + o, n = b + loop + o;  // this cycle's and the next's window start
    auto at = [](const std::vector<std::pair<uint32_t, uint32_t>>& v) { return v.size() == 1 ? v[0] : std::make_pair(0u, 0u); };
    TEST_ASSERT_EQUAL_UINT32(c + 500, at(notesOf(60, j, b + loop)).first);
    TEST_ASSERT_EQUAL_UINT32(0, notesOf(62, j, b + loop).size());
    TEST_ASSERT_EQUAL_UINT32(n + 350, at(notesOf(62, b + loop, b + 2 * loop)).first);
    TEST_ASSERT_EQUAL_UINT32(c + 650, at(notesOf(64, c, b + loop)).second);
    TEST_ASSERT_EQUAL_UINT32(j, at(notesOf(66, c, b + loop)).second);
    TEST_ASSERT_EQUAL_UINT32(0, notesOf(67, c, b + loop).size());
    TEST_ASSERT_EQUAL_UINT32(0, notesOf(65, c, b + loop).size());
    TEST_ASSERT_EQUAL_UINT32(n + 380, at(notesOf(65, b + loop, b + 2 * loop)).first);
    TEST_ASSERT_EQUAL_UINT32(0, eventsOf(64, j, b + loop).size());
    TEST_ASSERT_EQUAL_UINT32(1, eventsOf(64, b + loop, b + 2 * loop).size());
    expectNoOrphans();
  }
}

// An edit join the window's end overtakes (no worker time until after it) joins inside the next window when ready.
void test_an_overtaken_edit_joins_the_next_window() {
  const uint32_t loop = 1536;
  const RunId ra = run({{1, 60, 100, 200}});
  const RunId rb = run({{1, 60, 500, 550}});
  const ManifestId a = manifest({ra}, loop), e = manifest({rb}, loop);
  gTick = 0;
  gTrack.select(0, a, 0);
  const uint32_t b = 3 * loop;
  play(b - 10, 0);
  gTrack.edit(0, e, b - 11);
  play(b + 20, b + 20);  // starved until 20 ticks into the next window
  play(b + loop, 0);
  TEST_ASSERT_EQUAL_UINT32(1, gTrack.stats().editJoins);
  TEST_ASSERT_EQUAL_UINT32(0, notesOf(60, b + 20, b + 450).size());  // A's note at 100: the edited source plays
  const auto x = notesOf(60, b, b + loop);
  TEST_ASSERT_EQUAL_UINT32(1, x.size());
  TEST_ASSERT_EQUAL_UINT32(b + 500, x[0].first);
  expectNoOrphans();
}

// A sounding note that sounds past the window's end ends at the join (its end would come as the edited copy's, which
// its instance never matches) — never left sounding.
void test_an_edit_keeps_a_note_sounding_past_the_window() {
  const uint32_t loop = 3072;
  const ManifestId a = manifest({run({{6, 69, 200, 1600}})}, loop), e = manifest({run({{6, 69, 200, 1600}, {7, 70, 900, 950}})}, loop);
  gTick = 0;
  gTrack.select(0, a, 0);
  const uint32_t b = 2 * loop;
  play(b + 401, 0);
  gTrack.edit(0, e, b + 400);
  play(b + loop, 0);
  TEST_ASSERT_EQUAL_UINT32(1, gTrack.stats().editJoins);
  const auto x = notesOf(69, b, b + loop);
  TEST_ASSERT_EQUAL_UINT32(1, x.size());
  TEST_ASSERT_EQUAL_UINT32(b + 401, x[0].second);
  TEST_ASSERT_EQUAL_UINT32(b + 900, notesOf(70, b, b + loop)[0].first);
  expectNoOrphans();
}

// The window completes just before its end and the join is not ready yet: it joins inside the next window.
void test_an_edit_ready_at_the_window_end_joins_the_next() {
  const uint32_t loop = 1536;
  const ManifestId a = manifest({run({{1, 60, 100, 200}})}, loop), e = manifest({run({{1, 60, 500, 550}})}, loop);
  gTick = 0;
  gTrack.select(0, a, 0);
  const uint32_t b = 3 * loop;
  for (uint32_t d = 2; d <= 6; ++d) {  // whichever grain lands on the boundary
    setUp();
    const ManifestId a2 = manifest({run({{1, 60, 100, 200}})}, loop), e2 = manifest({run({{1, 60, 500, 550}})}, loop);
    gTick = 0;
    gTrack.select(0, a2, 0);
    play(b - d, 0);
    gTrack.edit(0, e2, b - d - 1);
    play(b + loop, 0, 256, 1);  // one grain a tick
    TEST_ASSERT_EQUAL_UINT32(1, gTrack.stats().editJoins);
    TEST_ASSERT_EQUAL_UINT32(0, notesOf(60, b, b + 300).size());
    TEST_ASSERT_EQUAL_UINT32(b + 500, notesOf(60, b, b + loop)[0].first);
    expectNoOrphans();
  }
  (void)a;
  (void)e;
}

// An edit superseded by an ordinary selection: that one's late join is the bounded one (notes start late).
void test_a_selection_after_an_edit_joins_as_usual() {
  const uint32_t loop = 1536;
  const ManifestId a = manifest({run({{1, 60, 100, 200}})}, loop), e = manifest({run({{1, 60, 500, 550}})}, loop),
                   s = manifest({run({{2, 61, 0, 300}})}, loop);
  gTrack.setLateJoinLimit(kLimit300);
  gTick = 0;
  gTrack.select(0, a, 0);
  const uint32_t b = 3 * loop;
  play(b - 100, 0);
  gTrack.edit(0, e, b - 101);
  gTrack.select(0, s, b - 101);
  play(b + 10, b + 10);  // ready 10 ticks into the window
  play(b + loop, 0);
  TEST_ASSERT_EQUAL_UINT32(0, gTrack.stats().editJoins);
  TEST_ASSERT_EQUAL_UINT32(1, gTrack.stats().lateJoins);
  TEST_ASSERT_EQUAL_UINT32(b + 10, notesOf(61, b, b + loop)[0].first);  // started late
  expectNoOrphans();
}

// An edit published on the tick before a boundary is the ordinary switch there.
void test_an_edit_before_a_boundary_switches_there() {
  const uint32_t loop = 1536;
  const ManifestId a = manifest({run({{1, 60, 100, 200}})}, loop), e = manifest({run({{1, 61, 0, 50}})}, loop);
  gTick = 0;
  gTrack.select(0, a, 0);
  play(2 * loop, 0);
  gTrack.edit(0, e, 2 * loop - 1);
  play(3 * loop, 0);
  TEST_ASSERT_EQUAL_UINT32(0, gTrack.stats().editJoins);
  TEST_ASSERT_EQUAL_UINT32(2 * loop, notesOf(61, 2 * loop, 3 * loop)[0].first);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_takes_join_late_within_24_ticks_at_300_bpm);
  RUN_TEST(test_the_25_ms_cap_and_just_beyond_either_limit);
  RUN_TEST(test_notes_already_sounding_from_the_previous_source);
  RUN_TEST(test_dense_midi_at_the_join_is_prepared_by_workers);
  RUN_TEST(test_a_join_point_the_clock_passes_moves_on);
  RUN_TEST(test_a_launch_ends_what_sounded_at_its_commit);
  RUN_TEST(test_din_and_usb_diverge_when_din_cannot_admit_the_late_note);
  RUN_TEST(test_later_cycles_equal_uninterrupted_composition);
  RUN_TEST(test_an_edit_joins_the_window_playing);
  RUN_TEST(test_an_overtaken_edit_joins_the_next_window);
  RUN_TEST(test_an_edit_keeps_a_note_sounding_past_the_window);
  RUN_TEST(test_an_edit_ready_at_the_window_end_joins_the_next);
  RUN_TEST(test_a_selection_after_an_edit_joins_as_usual);
  RUN_TEST(test_an_edit_before_a_boundary_switches_there);
  return UNITY_END();
}
