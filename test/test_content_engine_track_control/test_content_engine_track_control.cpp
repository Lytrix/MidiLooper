//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// fixed-memory-engine E9.4: gestures and live input through `TrackControl` on the production root (owner 2026-11-05:
// one sounding slot per track; Clear undoable), checked on the USB device port the way the musician hears it. A queued
// Record starts on the bar, or at the playing slot's loop start; a queued Stop ends on the bar; a second writer waits
// for the first; slot switches play the new slot from its position 0 on the next grid step or at the loop end, with
// window 0 ready (its downbeat on the commit tick) and the old slot's notes ended; an empty target stops the track and
// takes the slot; Clear is undoable; mute / solo end what sounds and silence a track; transport stop silences all and
// transport start plays every track from position 0 at once; live input is played through on the right track's
// ports and recorded while a take runs; no allocation.

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
using Q = ProductionControl::Quantize;
using Sent = ProductionHub::Sent;

static EnginePsram gPsram;
static EngineRam2 gRam2;
static EngineRoot gE;

constexpr uint32_t kUsPerTick = 1040;
constexpr uint32_t kBar = kTicksPerBar;

struct Wire {
  uint32_t clock;
  Sent s;
};
static std::vector<Wire> gWire[3];
static uint32_t gClock = 0;

void setUp() {
  TEST_ASSERT_TRUE(gE.init(gPsram, gRam2, 2000));
  for (auto& w : gWire) {
    w.clear();
    w.reserve(30000);
  }
  gClock = 0;
}
void tearDown() {}

static uint32_t us(uint32_t clock) { return clock * kUsPerTick; }
static ProductionControl& C() { return gE.control(); }
static ProductionTransport& X() { return gE.transport(); }

struct Live {
  uint32_t on, off;
  uint8_t pitch;
  uint8_t channel = 0;  // 0-based, in the status byte
};
static void run(uint32_t to, const std::vector<Live>& live = {}) {
  Sent buf[32];
  for (; gClock < to; ++gClock) {
    gE.submitJobs(gClock, us(gClock), kUsPerTick);
    while (gE.runGrain(us(gClock))) {
    }
    gE.clockTick(gClock, us(gClock), kUsPerTick);
    for (uint32_t step = 0; step < kUsPerTick; step += 125)
      for (uint8_t p = 0; p < 3; ++p) {
        const uint8_t n = gE.transmit(p, us(gClock) + step, buf, 32);
        for (uint8_t i = 0; i < n; ++i) gWire[p].push_back({gClock, buf[i]});
      }
    for (const Live& l : live) {
      if (l.off == gClock) C().inputNoteOff(gClock, static_cast<uint8_t>(0x80 | l.channel), l.pitch, us(gClock));
      if (l.on == gClock) C().inputNoteOn(gClock, static_cast<uint8_t>(0x90 | l.channel), l.pitch, 100, us(gClock), kUsPerTick);
    }
  }
}
/// NoteOns of `pitch` on port `p` at Clock ticks in [from, to).
static std::vector<uint32_t> ons(uint8_t p, uint8_t pitch, uint32_t from, uint32_t to) {
  std::vector<uint32_t> v;
  for (const Wire& w : gWire[p])
    if ((w.s.status & 0xF0) == 0x90 && w.s.data1 == pitch && w.clock >= from && w.clock < to) v.push_back(w.clock);
  return v;
}
static uint32_t noteOnsIn(uint8_t p, uint32_t from, uint32_t to) {
  uint32_t n = 0;
  for (const Wire& w : gWire[p]) n += (w.s.status & 0xF0) == 0x90 && w.clock >= from && w.clock < to ? 1 : 0;
  return n;
}
static void expectSilentEnd() {
  for (uint8_t p = 0; p < 3; ++p) TEST_ASSERT_EQUAL_UINT16(0, gE.hub().soundingCount(p));
}

/// The tick a Record queued now on track `t` starts: its playing slot's next loop start, else the next bar.
static uint32_t recordStart(uint8_t t) {
  const ManifestId m = X().playing(t) ? X().contentOf(X().playingSlot(t)) : kNoManifest;
  if (m == kNoManifest) return (gClock / kBar + 1) * kBar;
  const uint32_t loop = X().loopLength(m), o = X().origin(t);
  return o + ((gClock - o) / loop + 1) * loop;
}
/// Track `t` records `notes` (positions from the take start) into `slot` for `bars` bars, ended by a queued Stop.
static uint32_t recordBars(uint8_t t, uint8_t slot, uint32_t bars, const std::vector<std::pair<uint32_t, uint8_t>>& notes) {
  C().selectTrack(t);
  const uint32_t start = recordStart(t);
  TEST_ASSERT_EQUAL(R::Ok, C().queueRecord(t, slot, gClock));
  std::vector<Live> live;
  for (const auto& n : notes) live.push_back({start + n.first, start + n.first + 40, n.second});
  run(start + 1, live);
  TEST_ASSERT_EQUAL(ProductionTransport::Mode::Recording, X().mode());
  run(start + bars * kBar - 100, live);
  TEST_ASSERT_EQUAL(R::Ok, C().queueStop(t, gClock));
  run(start + bars * kBar + 1, live);
  TEST_ASSERT_EQUAL_UINT32(bars * kBar, X().loopLength(X().contentOf(slot)));
  TEST_ASSERT_EQUAL(ProductionTransport::Mode::Idle, X().mode());
  return start;
}

// ---- Tests -------------------------------------------------------------------------------------------------------------
void test_queued_record_and_stop_follow_the_bar() {
  gClock = 1000;
  const uint32_t start = recordBars(0, 0, 4, {{0, 60}, {800, 62}, {2000, 64}});
  TEST_ASSERT_EQUAL_UINT32(1536, start);  // the bar after tick 1000
  TEST_ASSERT_EQUAL_UINT32(start, X().origin(0));
  TEST_ASSERT_EQUAL_UINT32(4 * kBar, X().loopLength(X().contentOf(0)));
  TEST_ASSERT_EQUAL_UINT32(1, C().stats().recordsStarted);
  run(start + 3 * 4 * kBar);
  // Heard while played (thru: input lands after a tick's drain, so it leaves with the next one) and from cycle 1 on
  // (the take), downbeat included.
  const auto on60 = ons(kPortUsbDevice, 60, 0, gClock);
  TEST_ASSERT_EQUAL_UINT32(3, on60.size());
  TEST_ASSERT_EQUAL_UINT32(start + 1, on60[0]);
  TEST_ASSERT_EQUAL_UINT32(start + 4 * kBar, on60[1]);
  TEST_ASSERT_EQUAL_UINT32(start + 8 * kBar, on60[2]);
}

void test_record_follows_the_playing_slots_loop_start_and_one_writer_waits() {
  gClock = 0;
  C().selectTrack(0);
  TEST_ASSERT_EQUAL(R::Ok, C().queueRecord(0, 0, gClock));
  const std::vector<Live> take = {{kBar + 10, kBar + 50, 60}};
  run(kBar + 1, take);
  run(kBar + 2000, take);
  TEST_ASSERT_EQUAL(R::Ok, C().stopNow(gClock, us(gClock), kUsPerTick));  // not on a bar: a 2,000-tick loop
  const uint32_t origin = X().origin(0), loop = 2000;
  TEST_ASSERT_EQUAL_UINT32(loop, X().loopLength(X().contentOf(0)));
  // Track 1 records first and overdub-blocks track 0's queued Record (one writer).
  run(gClock + 10);
  TEST_ASSERT_EQUAL(R::Ok, C().queueRecord(1, 8, gClock));
  const uint32_t r1 = (gClock / kBar + 1) * kBar;
  run(r1 + 1);
  TEST_ASSERT_EQUAL(ProductionTransport::Mode::Recording, X().mode());
  TEST_ASSERT_EQUAL(R::Ok, C().queueRecord(0, 1, gClock));  // slot 1 on track 0, phase-referenced to slot 0
  run(gClock + 2 * loop + 50, {{r1 + 100, r1 + 140, 70}});   // track 1's take
  TEST_ASSERT_TRUE(C().recordQueued(0));
  TEST_ASSERT_TRUE(C().stats().recordsWaited >= 2);  // slot 0's loop starts came and went
  TEST_ASSERT_EQUAL(R::Ok, C().stopNow(gClock, us(gClock), kUsPerTick));
  // It begins at slot 0's next loop start after the writer is free: no bar alignment needed; slot 0 stops there.
  const uint32_t expected = origin + ((gClock - origin) / loop + 1) * loop;
  TEST_ASSERT_TRUE(expected % kBar != 0);
  run(expected + 300, {{expected + 100, expected + 140, 61}});
  TEST_ASSERT_FALSE(C().recordQueued(0));
  TEST_ASSERT_EQUAL_UINT8(0, X().writingTrack());
  TEST_ASSERT_EQUAL_UINT32(0, ons(kPortUsbDevice, 60, expected, expected + 300).size());
  TEST_ASSERT_EQUAL(R::Ok, C().stopNow(gClock, us(gClock), kUsPerTick));
  TEST_ASSERT_EQUAL_UINT32(gClock - expected, X().loopLength(X().contentOf(1)));
}

void test_slot_switches_play_the_new_slot_from_position_0_without_a_gap() {
  gClock = 0;
  recordBars(0, 0, 4, {{0, 60}, {1600, 61}});       // slot 0: A
  recordBars(0, 1, 2, {{0, 70}, {200, 71}});        // slot 1: B (track 0 now plays B)
  TEST_ASSERT_EQUAL_UINT8(1, X().playingSlot(0));
  run(gClock + 500);
  TEST_ASSERT_EQUAL(R::Ok, C().queueSwitch(0, 0, Q::NextGrid, gClock));
  const uint32_t commit = (gClock / kLaunchGridTicks + 1) * kLaunchGridTicks;
  run(commit + 2000);
  TEST_ASSERT_EQUAL_UINT8(0, X().playingSlot(0));
  TEST_ASSERT_EQUAL_UINT32(commit, X().origin(0));
  const auto a = ons(kPortUsbDevice, 60, commit, commit + 1);
  TEST_ASSERT_EQUAL_UINT32(1, a.size());  // A's downbeat on the commit tick: window 0 was ready
  TEST_ASSERT_EQUAL_UINT32(1, ons(kPortUsbDevice, 61, commit, commit + 2000).size());
  TEST_ASSERT_EQUAL_UINT32(0, ons(kPortUsbDevice, 71, commit, commit + 2000).size());  // B ended
  // Back to B at A's loop end.
  TEST_ASSERT_EQUAL(R::Ok, C().queueSwitch(0, 1, Q::LoopEnd, gClock));
  run(commit + 4 * kBar + 10);
  TEST_ASSERT_EQUAL_UINT8(1, X().playingSlot(0));
  TEST_ASSERT_EQUAL_UINT32(commit + 4 * kBar, X().origin(0));
  TEST_ASSERT_EQUAL_UINT32(1, ons(kPortUsbDevice, 70, commit + 4 * kBar, commit + 4 * kBar + 1).size());
  // B, a one-window take, could not compose its window 0 before its first tick (the queued Stop runs in that tick):
  // it joined late inside the window (bounded late join); every missed boundary was recovered that way.
  TEST_ASSERT_TRUE(gE.track(0).stats().lateJoins >= 1);
  TEST_ASSERT_EQUAL_UINT32(gE.track(0).stats().lateJoins, gE.track(0).stats().underruns);
  TEST_ASSERT_EQUAL_UINT32(0, gE.track(0).stats().lateJoinsMissed);
  TEST_ASSERT_EQUAL_UINT32(2, C().stats().switches);
}

void test_an_empty_target_stops_the_track_and_clear_is_undoable() {
  gClock = 0;
  recordBars(0, 0, 2, {{0, 60}, {700, 62}});
  run(gClock + 300);
  TEST_ASSERT_EQUAL(R::DeniedSlot, C().queueRecord(0, 0, gClock));  // has content
  // Clear: silent at once, undoable.
  TEST_ASSERT_EQUAL(R::Ok, C().clear(0, gClock, us(gClock), kUsPerTick));
  const uint32_t cleared = gClock;
  run(cleared + 4 * kBar);
  TEST_ASSERT_EQUAL_UINT32(0, noteOnsIn(kPortUsbDevice, cleared, gClock));
  expectSilentEnd();
  TEST_ASSERT_EQUAL(R::Ok, C().undo(0, gClock, us(gClock), kUsPerTick));
  const uint32_t undone = gClock;
  run(undone + 4 * kBar);
  TEST_ASSERT_TRUE(ons(kPortUsbDevice, 60, undone, gClock).size() >= 1);  // back
  // A switch to an empty slot stops the track and takes the slot, ready to record.
  TEST_ASSERT_EQUAL(R::Ok, C().queueSwitch(0, 3, Q::NextGrid, gClock));
  run(gClock + kLaunchGridTicks + 1);
  TEST_ASSERT_FALSE(X().playing(0));
  TEST_ASSERT_EQUAL_UINT8(3, X().playingSlot(0));
  const uint32_t stopped = gClock;
  run(stopped + 4 * kBar);
  TEST_ASSERT_EQUAL_UINT32(0, noteOnsIn(kPortUsbDevice, stopped, gClock));
  expectSilentEnd();
  // A cleared slot can be recorded again; Undo then steps back to the empty state, then the old take.
  TEST_ASSERT_EQUAL(R::Ok, C().redo(0, gClock, us(gClock), kUsPerTick));  // slot 0 cleared again
  TEST_ASSERT_EQUAL(kNoManifest, X().contentOf(0));
  TEST_ASSERT_EQUAL(R::Ok, C().queueRecord(0, 0, gClock));
}

void test_mute_and_solo_end_what_sounds_and_silence_a_track() {
  gClock = 0;
  recordBars(0, 0, 2, {{0, 60}});  // a 40-tick note at the downbeat
  recordBars(1, 8, 2, {{0, 72}});
  const uint32_t o0 = X().origin(0), o1 = X().origin(1);
  TEST_ASSERT_TRUE((o0 - o1) % (2 * kBar) != 0);  // the two downbeats fall apart
  run(o0 + 4 * 2 * kBar + 10);  // inside track 0's note
  TEST_ASSERT_EQUAL_UINT16(1, gE.hub().soundingCount(kPortUsbDevice));
  C().setMute(0, true, us(gClock));
  run(gClock + 1);
  TEST_ASSERT_EQUAL_UINT16(0, gE.hub().soundingCount(kPortUsbDevice));  // track 0's note ended at once
  const uint32_t muted = gClock;
  run(muted + 2 * 2 * kBar);
  TEST_ASSERT_EQUAL_UINT32(0, ons(kPortUsbDevice, 60, muted, gClock).size());
  TEST_ASSERT_TRUE(ons(kPortUsbDevice, 72, muted, gClock).size() >= 1);
  C().setMute(0, false, us(gClock));
  C().setSolo(0, true, us(gClock));  // solo track 0: track 1 silent
  const uint32_t soloed = gClock;
  run(soloed + 2 * 2 * kBar);
  TEST_ASSERT_TRUE(ons(kPortUsbDevice, 60, soloed, gClock).size() >= 1);
  TEST_ASSERT_EQUAL_UINT32(0, ons(kPortUsbDevice, 72, soloed, gClock).size());
  C().setSolo(0, false, us(gClock));
  const uint32_t all = gClock;
  run(all + 2 * 2 * kBar);
  TEST_ASSERT_TRUE(ons(kPortUsbDevice, 72, all, gClock).size() >= 1);
}

void test_transport_stop_silences_and_start_plays_from_position_0() {
  gClock = 0;
  recordBars(0, 0, 2, {{0, 60}, {400, 62}});
  recordBars(1, 8, 1, {{0, 72}});
  run(gClock + 333);
  C().selectTrack(0);
  TEST_ASSERT_EQUAL(R::Ok, C().queueRecord(0, 2, gClock));  // queued gestures are dropped by transport stop
  C().transportStop(gClock, us(gClock), kUsPerTick);
  const uint32_t stopped = gClock;
  run(stopped + 3000);
  TEST_ASSERT_FALSE(C().recordQueued(0));
  // History moves on a stopped track's slot do not start it.
  TEST_ASSERT_EQUAL(R::Ok, C().clear(0, gClock, us(gClock), kUsPerTick));
  TEST_ASSERT_EQUAL(R::Ok, C().undo(0, gClock, us(gClock), kUsPerTick));
  run(gClock + 2000);
  TEST_ASSERT_EQUAL_UINT32(0, noteOnsIn(kPortUsbDevice, stopped, gClock));
  expectSilentEnd();
  const uint32_t start = gClock;
  C().transportStart(start, us(start), kUsPerTick);
  run(start + 3 * kBar);
  TEST_ASSERT_EQUAL_UINT32(start, X().origin(0));
  TEST_ASSERT_EQUAL_UINT32(start, X().origin(1));
  TEST_ASSERT_EQUAL_UINT32(1, ons(kPortUsbDevice, 60, start, start + 1).size());  // both downbeats at once
  TEST_ASSERT_EQUAL_UINT32(1, ons(kPortUsbDevice, 72, start, start + 1).size());
  TEST_ASSERT_EQUAL_UINT32(start + 400, ons(kPortUsbDevice, 62, start, gClock)[0]);
}

void test_live_input_is_heard_on_the_right_tracks_ports_and_recorded() {
  gE.output().setPorts(0, 1u << kPortUsbDevice);
  gE.output().setPorts(1, 1u << kPortUsbHost);
  gClock = 100;
  C().selectTrack(1);
  run(200, {{150, 160, 50}, {170, 180, 50}, {175, 190, 50}});  // track 1 selected: thru on USB host only
  TEST_ASSERT_EQUAL_UINT32(0, noteOnsIn(kPortUsbDevice, 0, gClock));
  TEST_ASSERT_EQUAL_UINT32(3, ons(kPortUsbHost, 50, 0, gClock).size());  // a fast repeat is a new note
  {  // the restrike at 175 (50 still held from 170) ends the held note first: NoteOn, NoteOff, NoteOn
    std::vector<uint8_t> seq;
    for (const Wire& w : gWire[kPortUsbHost])
      if (w.s.data1 == 50 && w.clock >= 170 && w.clock < 180) seq.push_back(w.s.status & 0xF0);
    TEST_ASSERT_EQUAL_UINT32(3, seq.size());
    TEST_ASSERT_EQUAL_HEX8(0x90, seq[0]);
    TEST_ASSERT_EQUAL_HEX8(0x80, seq[1]);
    TEST_ASSERT_EQUAL_HEX8(0x90, seq[2]);
  }
  C().selectTrack(0);
  run(300, {{250, 260, 51}});
  TEST_ASSERT_EQUAL_UINT32(1, ons(kPortUsbDevice, 51, 0, gClock).size());
  expectSilentEnd();
  // While track 1 records, input goes to its take and its ports, whichever track is selected.
  C().selectTrack(1);
  TEST_ASSERT_EQUAL(R::Ok, C().queueRecord(1, 8, gClock));
  run(kBar + 1);
  C().selectTrack(0);
  run(kBar + 500, {{kBar + 100, kBar + 140, 52}});
  TEST_ASSERT_EQUAL_UINT32(1, ons(kPortUsbHost, 52, kBar, gClock).size());
  TEST_ASSERT_EQUAL(R::Ok, C().stopNow(gClock, us(gClock), kUsPerTick));
  run(gClock + 2 * 500 + 10);
  TEST_ASSERT_EQUAL_UINT32(3, ons(kPortUsbHost, 52, kBar, gClock).size());  // played, then twice from the take
}

void test_no_heap() {
  const unsigned long before = gHeapAllocations;
  const std::vector<Live> take = {{kBar + 10, kBar + 50, 60}};  // before the trap: the test's own allocation
  gTrap = true;
  gClock = 0;
  C().queueRecord(0, 0, 0);
  run(kBar + 900, take);
  C().stopNow(gClock, us(gClock), kUsPerTick);
  C().queueRecord(0, 1, gClock);
  run(gClock + 1000);
  C().stopNow(gClock, us(gClock), kUsPerTick);
  C().queueSwitch(0, 0, Q::NextGrid, gClock);
  run(gClock + 200);
  C().setMute(0, true, us(gClock));
  C().clear(1, gClock, us(gClock), kUsPerTick);
  C().transportStop(gClock, us(gClock), kUsPerTick);
  C().transportStart(gClock, us(gClock), kUsPerTick);
  run(gClock + 200);
  gTrap = false;
  TEST_ASSERT_EQUAL_UINT32(0, gHeapAllocations - before);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_queued_record_and_stop_follow_the_bar);
  RUN_TEST(test_record_follows_the_playing_slots_loop_start_and_one_writer_waits);
  RUN_TEST(test_slot_switches_play_the_new_slot_from_position_0_without_a_gap);
  RUN_TEST(test_an_empty_target_stops_the_track_and_clear_is_undoable);
  RUN_TEST(test_mute_and_solo_end_what_sounds_and_silence_a_track);
  RUN_TEST(test_transport_stop_silences_and_start_plays_from_position_0);
  RUN_TEST(test_live_input_is_heard_on_the_right_tracks_ports_and_recorded);
  RUN_TEST(test_no_heap);
  return UNITY_END();
}
