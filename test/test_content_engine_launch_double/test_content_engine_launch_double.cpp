//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// A launched slot plays each note once — relaunched while playing, with a late launch window (few grains a tick), from a
// checkpoint set, and across loop wraps — on a line of 16ths whose NoteOffs fall on the next NoteOn's tick (owner
// 2026-11-09: "the first note played twice"). On the device the doubles were the slot's own content: a take stopped
// after the bar line keeps the notes played after it, which come back at the loop start (E9.8i) on top of its first
// notes. The launch path itself plays each note once; these cases keep it so.

#include <unity.h>

#include <cstdint>
#include <cstdio>
#include <vector>

#include "ContentEngine/EngineRoot.h"

using namespace ContentEngine;
using R = ProductionTransport::Result;

static EnginePsram gPsram;
static EngineRam2 gRam2;
static EngineRoot gE;
constexpr uint32_t kUsPerTick = 1040;
constexpr uint32_t kBar = kTicksPerBar;
static uint32_t gClock = 0;
static uint32_t us(uint32_t c) { return c * kUsPerTick; }

struct Live {
  uint32_t on, off;
  uint8_t pitch;
};
struct Out {
  uint32_t clock;
  uint8_t status, d1, d2;
};
static std::vector<Out> gOut;
static uint32_t gGrainsPerTick = 0xFFFFFFFFu;  // the device runs a few grains a pass: a launch window may be late
static void run(uint32_t to, const std::vector<Live>& live = {}) {
  ProductionHub::Sent buf[32];
  for (; gClock < to; ++gClock) {
    gE.submitJobs(gClock, us(gClock), kUsPerTick);
    for (uint32_t g = 0; g < gGrainsPerTick && gE.runGrain(us(gClock)); ++g) {
    }
    gE.clockTick(gClock, us(gClock), kUsPerTick);
    const uint8_t n = gE.transmit(kPortUsbDevice, us(gClock) + 1000, buf, 32);
    for (uint8_t i = 0; i < n; ++i) gOut.push_back({gClock, buf[i].status, buf[i].data1, buf[i].data2});
    for (const Live& l : live) {
      if (l.off == gClock) gE.inputNoteOff(gClock, 0x80, l.pitch, us(gClock));
      if (l.on == gClock) gE.inputNoteOn(gClock, 0x90, l.pitch, 100, us(gClock), kUsPerTick);
    }
  }
}
/// The device's slot 56: 84 at 0, then 83, 82 … every 16th (the first 47 ticks), each NoteOff on the next NoteOn's tick.
static std::vector<Live> line(uint32_t s) {
  std::vector<Live> v;
  uint32_t at = 0;
  for (uint8_t k = 0; k < 16; ++k) {
    const uint32_t next = k == 0 ? 47 : at + 48;
    v.push_back({s + at, s + next, static_cast<uint8_t>(84 - k)});
    at = next;
  }
  return v;
}
static void recordLine(uint8_t t, uint8_t slot, uint32_t bars = 4) {
  gE.control().selectTrack(t);
  TEST_ASSERT_EQUAL(R::Ok, gE.control().queueRecord(t, slot, gClock));
  const uint32_t s = (gClock / kBar + 1) * kBar;
  const auto take = line(s);
  run(s + bars * kBar - 10, take);
  TEST_ASSERT_EQUAL(R::Ok, gE.control().queueStop(t, gClock));
  run(s + bars * kBar + 1, take);
}
/// NoteOns of one pitch sent twice within two ticks (a retrigger: whatever was sent between them).
static std::vector<Out> doubled(uint32_t from) {
  std::vector<Out> d;
  uint32_t last[128];
  for (auto& x : last) x = 0xFFFFFFFFu;
  for (const Out& o : gOut) {
    if (o.clock < from || (o.status & 0xF0) != 0x90 || o.d2 == 0) continue;
    if (last[o.d1] != 0xFFFFFFFFu && o.clock - last[o.d1] <= 2) d.push_back(o);
    last[o.d1] = o.clock;
  }
  return d;
}
static void print(const std::vector<Out>& d, uint32_t base) {
  for (const Out& o : d) printf("doubled: +%lu n%u\n", static_cast<unsigned long>(o.clock - base), o.d1);
}

void setUp() {
  TEST_ASSERT_TRUE(gE.init(gPsram, gRam2, 2000));
  gClock = 0;
  gOut.clear();
}
void tearDown() {}

void test_a_relaunch_while_playing_plays_each_note_once() {
  recordLine(7, 56);
  run(gClock + 4 * kBar);  // playing on
  const uint32_t at = gClock;
  TEST_ASSERT_EQUAL(R::Ok, gE.control().queueSwitch(7, 56, ProductionControl::Quantize::NextGrid, gClock));
  run(gClock + 2 * kBar);
  const auto d = doubled(at);
  print(d, at);
  TEST_ASSERT_EQUAL_UINT32(0, d.size());
}

void test_a_late_launch_window_plays_each_note_once() {
  recordLine(7, 56);
  run(gClock + 4 * kBar);
  for (uint32_t grains : {1u, 2u, 3u}) {
    gGrainsPerTick = grains;
    const uint32_t at = gClock;
    TEST_ASSERT_EQUAL(R::Ok, gE.control().queueSwitch(7, 56, ProductionControl::Quantize::NextGrid, gClock));
    run(gClock + 2 * kBar);
    gGrainsPerTick = 0xFFFFFFFFu;
    run(gClock + 4 * kBar);
    const auto d = doubled(at);
    printf("grains a tick %u:\n", grains);
    print(d, at);
    TEST_ASSERT_EQUAL_UINT32(0, d.size());
  }
}

/// As on the device: a 16-bar loop played a whole cycle (the checkpoint sweep has run), then relaunched — the cold
/// start resumes from a checkpoint set.
void test_a_relaunch_from_a_checkpoint_plays_each_note_once() {
  recordLine(7, 56, 16);
  run(gClock + 17 * kBar);
  const uint32_t starts = gE.track(7).stats().checkpointStarts;
  const uint32_t at = gClock;
  TEST_ASSERT_EQUAL(R::Ok, gE.control().queueSwitch(7, 56, ProductionControl::Quantize::NextGrid, gClock));
  run(gClock + 2 * kBar);
  printf("checkpoint starts %lu -> %lu\n", (unsigned long)starts, (unsigned long)gE.track(7).stats().checkpointStarts);
  const auto d = doubled(at);
  print(d, at);
  TEST_ASSERT_EQUAL_UINT32(starts + 1, gE.track(7).stats().checkpointStarts);
  TEST_ASSERT_EQUAL_UINT32(0, d.size());
}

void test_a_loop_wrap_plays_each_note_once() {
  recordLine(7, 56);
  const uint32_t at = gClock;
  run(gClock + 9 * kBar);  // two wraps
  const auto d = doubled(at);
  print(d, at);
  TEST_ASSERT_EQUAL_UINT32(0, d.size());
}

/// The device's case (owner 2026-11-09, E9.8i amendment): a take stopped just after the bar line while the musician
/// started the line again — n84 a tick early, n83 and n82 after the line — plus one new note after it. The repeats are
/// hidden (the loop start plays each note once); the new note still comes back at the loop start.
void test_a_late_stop_hides_the_repeated_start_and_keeps_new_notes() {
  gE.control().selectTrack(7);
  TEST_ASSERT_EQUAL(R::Ok, gE.control().queueRecord(7, 56, gClock));
  const uint32_t s = (gClock / kBar + 1) * kBar, loop = 4 * kBar, barLine = s + loop;
  auto take = line(s);
  take.push_back({barLine - 1, barLine + 47, 84});
  take.push_back({barLine + 47, barLine + 95, 83});
  take.push_back({barLine + 95, barLine + 143, 82});
  take.push_back({barLine + 120, barLine + 128, 60});  // new material after the line
  take.push_back({barLine + 130, barLine + 138, 84});  // the start's pitch, but 130 ticks from it: new too
  run(barLine + 150, take);
  TEST_ASSERT_EQUAL(R::Ok, gE.control().stopNow(gClock, us(gClock), kUsPerTick, barLine));  // a late Stop: trimmed
  TEST_ASSERT_EQUAL_UINT32(loop, gE.manifests().manifest(gE.history().current(56)).loopLengthTicks);
  TEST_ASSERT_EQUAL_UINT32(3, gE.transport().stats().repeatsHidden);
  const uint32_t at = gClock;
  run(gClock + 2 * loop);  // two loop starts
  const auto d = doubled(at);
  print(d, at);
  TEST_ASSERT_EQUAL_UINT32(0, d.size());
  uint32_t starts60 = 0, starts84 = 0, late84 = 0;
  for (const Out& o : gOut)
    if (o.clock >= at && (o.status & 0xF0) == 0x90 && o.d2 > 0) {
      const uint32_t pos = (o.clock - s) % loop;
      if (o.d1 == 60 && pos == 120) ++starts60;
      if (o.d1 == 84 && pos == 0) ++starts84;
      if (o.d1 == 84 && pos == 130) ++late84;
    }
  TEST_ASSERT_EQUAL_UINT32(2, starts60);  // kept: plays at its wrapped place each cycle
  TEST_ASSERT_EQUAL_UINT32(2, starts84);
  TEST_ASSERT_EQUAL_UINT32(2, late84);
}

/// Without a trim nothing is hidden: a note in the loop's last 1/32 on the start's pitch is the loop's own.
void test_a_stop_on_the_bar_hides_nothing() {
  gE.control().selectTrack(7);
  TEST_ASSERT_EQUAL(R::Ok, gE.control().queueRecord(7, 56, gClock));
  const uint32_t s = (gClock / kBar + 1) * kBar, loop = 4 * kBar;
  auto take = line(s);
  take.push_back({s + loop - 10, s + loop - 5, 84});
  run(s + loop - 100, take);
  TEST_ASSERT_EQUAL(R::Ok, gE.control().queueStop(7, gClock));
  run(s + loop + 1, take);
  TEST_ASSERT_EQUAL_UINT32(loop, gE.manifests().manifest(gE.history().current(56)).loopLengthTicks);
  TEST_ASSERT_EQUAL_UINT32(0, gE.transport().stats().repeatsHidden);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_a_relaunch_while_playing_plays_each_note_once);
  RUN_TEST(test_a_late_launch_window_plays_each_note_once);
  RUN_TEST(test_a_relaunch_from_a_checkpoint_plays_each_note_once);
  RUN_TEST(test_a_loop_wrap_plays_each_note_once);
  RUN_TEST(test_a_late_stop_hides_the_repeated_start_and_keeps_new_notes);
  RUN_TEST(test_a_stop_on_the_bar_hides_nothing);
  return UNITY_END();
}
