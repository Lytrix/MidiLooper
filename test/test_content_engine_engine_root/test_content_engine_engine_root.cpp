//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// fixed-memory-engine E9.2: the engine at production capacity in its two regions (EXTMEM / DMAMEM on the Teensy,
// statics on the host), poisoned before `init()` (gate H2b), then driven like the firmware: a Clock tick every
// 1,040 µs (300 BPM, 192 PPQN), `submitJobs` each pass and scheduler grains while the tick period lasts. Track 0
// records 8 bars, overdubs a cycle; track 1 records 4 bars meanwhile playing. Checked: the census, every steady cycle's
// notes against what was played, a checkpoint set swept for each playing current, no missed hard deadline, the ledger
// and conservation, and no allocation after `init()`. On the Teensy the same run reports init time and the longest
// grain (env `teensy41-engine-bench`).

#include <unity.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <set>
#include <tuple>
#include <vector>

#include "ContentEngine/EngineRoot.h"

#ifdef ARDUINO
#include <Arduino.h>
#define ENGINE_PSRAM EXTMEM
#define ENGINE_RAM2 DMAMEM
static uint32_t realMicros() { return micros(); }
#else
#define ENGINE_PSRAM
#define ENGINE_RAM2
static uint32_t realMicros() { return 0; }
#endif

static unsigned long gHeapAllocations = 0;
static bool gTrap = false;
#ifndef ARDUINO  // the host run is the allocation gate (H2); the Teensy build has no exceptions
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
#endif

using namespace ContentEngine;
using Transport = ProductionTransport;
using R = Transport::Result;
using Emit = ProductionPlayer::Emit;

ENGINE_PSRAM static EnginePsram gPsram;
ENGINE_RAM2 static EngineRam2 gRam2;
static EngineRoot gE;

constexpr uint32_t kUsPerTick = 1040;
constexpr uint32_t kBar = 768;
constexpr size_t kEmitRoom = 4000;  // emits per track over the run (checked: never exceeded)

void setUp() {}
void tearDown() {}

// ---- Driving ---------------------------------------------------------------------------------------------------------
struct Live {
  uint8_t track;
  uint32_t on, off;
  uint8_t pitch;
};
struct Driver {
  uint32_t clock = 0;
  uint32_t simUs = 0;        // host: simulated time, each grain costs g
  uint32_t maxGrainUs = 0;   // device: measured
  std::vector<Emit> out[2];  // per track, ticks track-local
  uint32_t accepted = 0;
  uint32_t incompleteSeen = 0;  // ticks at which a playing current still waited for its completion
};
static Driver gD;

static bool sealed(ManifestId m) {
  const auto& x = gE.manifests().manifest(m);
  for (uint8_t i = 0; i < x.runCount; ++i)
    if (gE.runs().run(x.runs[i]).state == RunState::Writing) return false;
  return true;
}

static uint32_t now() {
#ifdef ARDUINO
  return realMicros();
#else
  return gD.simUs;
#endif
}

/// Clock ticks up to `to`: the engine's pass (jobs, then grains for the rest of the tick period), each track's tick,
/// then the live input landing on that tick.
static void run(uint32_t to, const std::vector<Live>& live = {}) {
  Emit buf[64];
  for (; gD.clock < to; ++gD.clock) {
    const uint32_t tickStart = now();
    gE.submitJobs(gD.clock, tickStart, kUsPerTick);
    for (uint8_t t = 0; t < 2; ++t) {  // a steady job aims at the next boundary of its own track's grid
      const uint32_t target = gE.jobs().steadyTarget(t), local = gD.clock - gE.transport().origin(t);
      if (target != 0) TEST_ASSERT_TRUE(target >= local && target - local <= kWindowTicks);
    }
    for (;;) {
#ifdef ARDUINO
      if (realMicros() - tickStart + kGrainUs > kUsPerTick) break;
      const uint32_t g0 = realMicros();
      if (!gE.runGrain(g0)) break;
      const uint32_t g = realMicros() - g0;
      if (g > gD.maxGrainUs) gD.maxGrainUs = g;
#else
      if (gD.simUs - tickStart + kGrainUs > kUsPerTick) break;
      if (!gE.runGrain(gD.simUs)) break;
      gD.simUs += kGrainUs;
#endif
    }
#ifndef ARDUINO
    gD.simUs = tickStart + kUsPerTick;
#else
    while (realMicros() - tickStart < kUsPerTick) {
    }
#endif
    for (uint8_t t = 0; t < 2; ++t) {
      const uint8_t n = gE.transport().tick(t, gD.clock, buf, 64);
      gD.out[t].insert(gD.out[t].end(), buf, buf + n);
      // No checkpoint set is ever built for a state whose runs may still grow under its identity.
      const uint8_t slot = gE.transport().playingSlot(t);
      const ManifestId m = slot == Transport::kNoSlot ? kNoManifest : gE.history().current(slot);
      if (m != kNoManifest && !sealed(m)) {
        ++gD.incompleteSeen;
        TEST_ASSERT_FALSE(gE.checkpoints().holds(slot, gE.manifests().manifest(m).identity));
      }
    }
    for (const Live& l : live) {
      if (l.off == gD.clock) gE.transport().noteOff(gD.clock, 1, l.pitch, 0);
      if (l.on == gD.clock) {
        uint8_t k;
        if (gE.transport().noteOn(gD.clock, 1, l.pitch, 100, buf, 64, k) == R::Ok) ++gD.accepted;
        gD.out[l.track].insert(gD.out[l.track].end(), buf, buf + k);
      }
    }
  }
}
static void stop() {
  Emit buf[Config::kMaxSoundingPerTrack];
  uint8_t n;
  TEST_ASSERT_EQUAL(R::Ok, gE.transport().stop(gD.clock, buf, Config::kMaxSoundingPerTrack, n));
}

/// (position, pitch) of every NoteOn track `t` sent in its local cycle `cycle` of `loop`.
static std::set<std::pair<uint32_t, uint8_t>> cycleOns(uint8_t t, uint32_t loop, uint32_t cycle) {
  std::set<std::pair<uint32_t, uint8_t>> s;
  for (const Emit& e : gD.out[t])
    if (e.cls == 1 && e.tick >= cycle * loop && e.tick < (cycle + 1) * loop) s.insert({e.tick - cycle * loop, e.pitch});
  return s;
}
static std::vector<Live> notes(uint8_t track, uint32_t from, uint32_t span, uint32_t every, uint8_t pitch0, uint32_t len) {
  std::vector<Live> v;
  for (uint32_t p = 0; p + len < span; p += every)
    v.push_back({track, from + p, from + p + len, static_cast<uint8_t>(pitch0 + (p / every) % 24)});
  return v;
}
static std::set<std::pair<uint32_t, uint8_t>> positions(const std::vector<Live>& v, uint32_t from) {
  std::set<std::pair<uint32_t, uint8_t>> s;
  for (const Live& l : v) s.insert({l.on - from, l.pitch});
  return s;
}

// ---- Tests -------------------------------------------------------------------------------------------------------------
void test_census_and_init_over_poison() {
  std::memset(static_cast<void*>(&gPsram), 0xA5, sizeof gPsram);  // gate H2b: nothing relies on zero
  std::memset(static_cast<void*>(&gRam2), 0xA5, sizeof gRam2);
  const uint32_t t0 = realMicros();
  TEST_ASSERT_TRUE(gE.init(gPsram, gRam2, 2000));
  const uint32_t initUs = realMicros() - t0;
  char line[120];
  EngineRoot::census([&](EngineRoot::Region r, const char* name, size_t bytes) {
    snprintf(line, sizeof line, "#CAP,MEM_REGION,%s,%s,%u", r == EngineRoot::Region::Psram ? "psram" : "ram2", name,
             static_cast<unsigned>(bytes));
    TEST_MESSAGE(line);
  });
  snprintf(line, sizeof line, "#CAP,ENGINE_INIT,us=%u", static_cast<unsigned>(initUs));
  TEST_MESSAGE(line);
  TEST_ASSERT_TRUE(gE.reservations().check());
  TEST_ASSERT_TRUE(checkConservation(gE.pool(), gE.runs(), gE.manifests()));
  // Placed for later slices, initialised now: idle over the poison.
  TEST_ASSERT_EQUAL_UINT32(0, gE.editWriter().stats().rebuilds);
  TEST_ASSERT_EQUAL_UINT32(0, gE.editWriter().stats().forgotSteps);
  for (uint8_t p = 0; p < 3; ++p) TEST_ASSERT_EQUAL_UINT16(0, gE.hub().soundingCount(p));
  TEST_ASSERT_EQUAL(ProductionTiles::Kind::Empty, gE.tiles().drawSource(0, 0).kind);
#ifdef ARDUINO
  // Plan § 4.1 / § 4.2 budgets on the target's layout (E1.7; also static_asserts in EngineRoot.h).
  TEST_ASSERT_TRUE(sizeof(EnginePsram) <= kPsramBudget);
  TEST_ASSERT_TRUE(sizeof(EngineRam2) <= kRam2Budget);
#endif
}

void test_two_tracks_record_overdub_and_play_under_the_scheduler() {
  Transport& x = gE.transport();
  // Inputs and output buffers first: the allocation trap counts the engine only.
  const uint32_t s0 = 1000, loop0 = 8 * kBar;  // track 0: an 8-bar take (4 windows), a note every 96 ticks
  const uint32_t s1 = s0 + loop0 + 500, loop1 = 4 * kBar;  // track 1: 4 bars, recorded while track 0 plays
  const uint32_t c3 = s0 + 3 * loop0;                      // track 0 overdubs its cycle 3
  const auto take0 = notes(0, s0, loop0, 96, 40, 48);
  const auto take1 = notes(1, s1, loop1, 64, 70, 30);
  auto over = notes(0, c3 + 48, loop0 - 96, 192, 90, 40);
  over.push_back({0, c3 + loop0 - 300, c3 + loop0 + 2000, 120});  // held across the wrap: completed in place
  for (auto& o : gD.out) o.reserve(kEmitRoom);  // RAM2 heap on the Teensy: sized to this run
  gTrap = true;
  const unsigned long heapBefore = gHeapAllocations;
  gD.clock = s0;
  TEST_ASSERT_EQUAL(R::Ok, x.startRecord(0, 0, gD.clock));
  run(s0 + loop0, take0);
  stop();
  run(s1);
  TEST_ASSERT_EQUAL(R::Ok, x.startRecord(1, 8, gD.clock));
  TEST_ASSERT_EQUAL(R::Busy, x.startOverdub(0, gD.clock));  // one writer
  run(s1 + loop1, take1);
  stop();
  run(c3 + 10);
  TEST_ASSERT_EQUAL(R::Ok, x.startOverdub(0, gD.clock));
  run(c3 + loop0 + 200, over);
  stop();
  run(s0 + 7 * loop0 + 1);
  gTrap = false;
  TEST_ASSERT_EQUAL_UINT32(0, gHeapAllocations - heapBefore);
  for (auto& o : gD.out) TEST_ASSERT_TRUE(o.size() <= kEmitRoom);

  // Track 0: whole from cycle 1, the overdub from cycle 4 (heard from right after its wrap).
  const auto want0 = positions(take0, s0);
  TEST_ASSERT_TRUE(want0 == cycleOns(0, loop0, 1));
  TEST_ASSERT_TRUE(want0 == cycleOns(0, loop0, 2));
  auto want0b = want0;
  for (const auto& o : positions(over, c3)) want0b.insert(o);
  TEST_ASSERT_TRUE(gD.incompleteSeen > 0);  // the held note's state was current, unswept, until its completion
  TEST_ASSERT_TRUE(want0b == cycleOns(0, loop0, 4));
  TEST_ASSERT_TRUE(want0b == cycleOns(0, loop0, 6));
  // Track 1: whole from its cycle 1.
  const auto want1 = positions(take1, s1);
  for (uint32_t c = 1; c < 6; ++c) TEST_ASSERT_TRUE(want1 == cycleOns(1, loop1, c));

  // Workers: no hard deadline missed; a checkpoint set for each track's current.
  const auto& rep = gE.scheduler().report();
  char line[200];
  snprintf(line, sizeof line,
           "#CAP,ENGINE_RUN,grains=%u,hard=%u,transition=%u,background=%u,missed=%u,overruns=%u,max_grain_us=%u,"
           "sweeps=%u/%u",
           static_cast<unsigned>(rep.grains), static_cast<unsigned>(rep.hardGrains),
           static_cast<unsigned>(rep.transitionGrains), static_cast<unsigned>(rep.backgroundGrains),
           static_cast<unsigned>(rep.missedDeadlines), static_cast<unsigned>(rep.overruns),
           static_cast<unsigned>(gD.maxGrainUs), static_cast<unsigned>(gE.stats().sweepsDone),
           static_cast<unsigned>(gE.stats().sweepsStarted));
  TEST_MESSAGE(line);
  TEST_ASSERT_EQUAL_UINT32(0, rep.missedDeadlines);
  TEST_ASSERT_TRUE(gE.stats().sweepsStarted <= 8);  // each current once (superseded ones included), never in a loop
  TEST_ASSERT_TRUE(gE.stats().sweepsDone >= 3);
  TEST_ASSERT_EQUAL_UINT32(0, gE.runs().reclaimPendingRuns());  // released runs went back to the pool
  for (uint8_t t = 0; t < 2; ++t) {
    TEST_ASSERT_EQUAL_UINT32(0, gE.track(t).stats().underruns);
    const uint8_t slot = x.playingSlot(t);
    TEST_ASSERT_TRUE(gE.checkpoints().holds(slot, gE.manifests().manifest(gE.history().current(slot)).identity));
  }
  TEST_ASSERT_TRUE(gE.reservations().check());
  TEST_ASSERT_TRUE(checkConservation(gE.pool(), gE.runs(), gE.manifests()));
#ifdef ARDUINO
  TEST_ASSERT_TRUE(gD.maxGrainUs <= kGrainUs);
#endif
}

#ifdef ARDUINO
void setup() {
  delay(2000);
  UNITY_BEGIN();
  RUN_TEST(test_census_and_init_over_poison);
  RUN_TEST(test_two_tracks_record_overdub_and_play_under_the_scheduler);
  UNITY_END();
}
void loop() {}
#else
int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_census_and_init_over_poison);
  RUN_TEST(test_two_tracks_record_overdub_and_play_under_the_scheduler);
  return UNITY_END();
}
#endif
