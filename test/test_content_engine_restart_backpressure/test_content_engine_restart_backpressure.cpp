//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// fixed-memory-engine E5 invariant (owner, 2026-11-05): composer checkpoints reconstruct the musical *source* state;
// they are not proof that DIN, USB device or USB host transmitted a note. Physical sounding-note ownership stays with
// the terminal scheduler (per track) and the output hub (per destination).
//
// Rig: composer -> WindowPlayer / TerminalScheduler<16> -> MidiOutputHub with a backpressured DIN port (40 ms per message)
// and a USB port, both receiving every note under one key. Scenario: play manifest M1; at a mid-loop window boundary
// cold-start M2 (undo / slot switch) from its swept checkpoint; at a later boundary adopt revision M3 the same way;
// then stop. On every port's wire: each transmitted NoteOn gets exactly one later NoteOff, no NoteOff is sent for a
// note that was never transmitted, and nothing sounds after the stop drains. The restart carries notes the DIN port
// never transmitted (suppressed or expired under backpressure) and those get no DIN NoteOff.

#include <unity.h>

#include <algorithm>
#include <cstdio>
#include <cstdint>
#include <map>
#include <tuple>
#include <set>
#include <utility>
#include <vector>

#include "ContentEngine/Composition/CheckpointTable.h"
#include "ContentEngine/Composition/SpanWindowComposer.h"
#include "ContentEngine/EngineCore.h"
#include "ContentEngine/Playback/MidiOutputHub.h"
#include "ContentEngine/Composition/SpanWindow.h"
#include "ContentEngine/Playback/TerminalScheduler.h"

using namespace ContentEngine;

using Pool = PagePool<2048, 64>;
using Runs = RunTable<64, Pool>;
using TestComposer = SpanWindowComposer<Pool, Runs>;
using Window = SpanWindow<3072>;
using Manifests = ManifestTable<16, 8>;
using Table = CheckpointTable<TestComposer::Checkpoint, 2, 8>;
using Sweep = CheckpointSweep<TestComposer, Table, Window, Manifests, Runs>;
using Cold = ColdStart<TestComposer, Table, Window, Manifests, Runs>;
using Player = WindowPlayer<16>;
using Hub = MidiOutputHub<128, 2>;

static Pool gPool;
static Runs gRuns;
static Manifests gManifests;
static EditIndexStore<Pool, Runs> gIndex;
static Table gTable;
static TestComposer gPlay, gCold, gSweepComposer;
static Window gWin, gScratch;
static Sweep gSweep;
static Cold gColdJob;
static TerminalScheduler<16> gSched;
static Player gPlayer;
static Hub gHub;

constexpr uint32_t kLoop = 768 * 8, kWin = 1536, kSpacing = 3072;
constexpr uint32_t kTickUs = 1042;  // 300 BPM, 192 ticks per quarter
constexpr uint8_t kDin = 0, kUsb = 1;

struct Wire {
  std::set<uint32_t> sounding[2];  // keys with a transmitted NoteOn and no NoteOff yet, per port
  uint32_t noteOns[2] = {0, 0}, noteOffs[2] = {0, 0};
};

struct Rig {
  std::map<std::pair<uint32_t, uint16_t>, uint32_t> keyOf;  // (noteId, runId) -> hub key of the sounding instance
  uint32_t nextKey = 0;
  Wire wire;
};
static Rig gRig;

static void transmit(uint32_t nowUs) {
  Hub::Sent sent[64];
  for (uint8_t p = 0; p < 2; ++p) {
    const uint8_t n = gHub.transmit(nowUs, p, sent, 64);
    for (uint8_t i = 0; i < n; ++i) {
      if (sent[i].cls == OutClass::NoteOn) {
        TEST_ASSERT_TRUE_MESSAGE(gRig.wire.sounding[p].insert(sent[i].key).second, "NoteOn twice on the wire");
        ++gRig.wire.noteOns[p];
      } else if (sent[i].cls == OutClass::NoteOff) {
        TEST_ASSERT_EQUAL_MESSAGE(1, gRig.wire.sounding[p].erase(sent[i].key), "NoteOff for a note never transmitted");
        ++gRig.wire.noteOffs[p];
      }
    }
  }
}

static void emit(const Player::Emit* e, uint8_t n, uint32_t nowUs) {
  for (uint8_t i = 0; i < n; ++i) {
    const auto id = std::make_pair(e[i].noteId, runIdOf(e[i].runKey));
    if (e[i].cls == 1) {
      TEST_ASSERT_TRUE(gRig.keyOf.find(id) == gRig.keyOf.end());
      const uint32_t key = ++gRig.nextKey;
      gRig.keyOf[id] = key;
      const uint8_t status = static_cast<uint8_t>(0x90 | (e[i].channel - 1));
      gHub.noteOn(nowUs, kDin, status, e[i].pitch, 100, key);
      gHub.noteOn(nowUs, kUsb, status, e[i].pitch, 100, key);
    } else if (e[i].cls == 0) {
      const auto it = gRig.keyOf.find(id);
      TEST_ASSERT_TRUE_MESSAGE(it != gRig.keyOf.end(), "scheduler NoteOff for an instance it never started");
      gHub.noteOff(nowUs, kDin, it->second);
      gHub.noteOff(nowUs, kUsb, it->second);
      gRig.keyOf.erase(it);
    }
  }
}

static void playWindow(const Window& w) {
  Player::Emit buf[64];
  const uint32_t t0 = w.cycleBase + w.start;
  emit(buf, gPlayer.beginWindow(w, buf, 64), t0 * kTickUs);
  for (uint32_t t = t0; t < w.cycleBase + w.end; ++t) {
    emit(buf, gPlayer.tick(t, buf, 64), t * kTickUs);
    transmit(t * kTickUs);
    TEST_ASSERT_TRUE(gSched.sounding() <= 16);
    TEST_ASSERT_TRUE(gHub.check());
  }
}

/// Source decisions of a window (Q-OUT-2): the instances that begin sounding, with refusals.
using Decisions = std::set<std::tuple<uint32_t, uint32_t, uint16_t>>;
static Decisions decisions(const Window& w) {
  Decisions d;
  for (uint16_t i = 0; i < w.count; ++i)
    if (!w.entries[i].terminal && (w.entries[i].status & 0xF0) == 0x90)
      d.insert({w.cycleBase + w.entries[i].tick, w.entries[i].noteId, w.entries[i].runKey});
  return d;
}
static std::vector<Decisions> gLog;  // decisions of the adopted composer, window by window
static uint32_t gRefused = 0;
static bool gLogging = false;

/// Plays `composer` from its current position until (cycle, position) is reached.
static void playUntil(TestComposer& c, uint32_t& cycle, uint32_t& pos, uint32_t untilCycle, uint32_t untilPos) {
  while (cycle < untilCycle || (cycle == untilCycle && pos < untilPos)) {
    if (!c.composeNext(gWin, kWin)) {
      c.nextCycle();
      ++cycle;
      pos = 0;
      continue;
    }
    if (gLogging) {
      gLog.push_back(decisions(gWin));
      gRefused += gWin.refusedCount;
    }
    playWindow(gWin);
    pos = gWin.end;
    if (pos >= kLoop) {
      c.nextCycle();
      ++cycle;
      pos = 0;
    }
  }
}

static PassEvent row(uint32_t tick, uint32_t id, uint8_t status, uint8_t d1) {
  PassEvent e;
  e.tick = tick;
  e.noteId = id;
  e.seq = 0;
  e.status = status;
  e.data1 = d1;
  e.data2 = (status & 0xF0) == 0x90 ? 100 : 0;
  e.origin = 0;
  return e;
}

struct N {
  uint32_t id, on, off;
  uint8_t pitch;
};
static RunId makeRun(uint8_t kind, const std::vector<N>& notes) {
  const RunId r = gRuns.allocate(0, kind);
  std::vector<std::pair<uint64_t, PassEvent>> ev;
  for (const auto& n : notes) {
    const uint8_t flag = n.off - n.on >= kLoop ? kOriginHeldLoop : 0;
    PassEvent off = row(n.off, n.id, 0x80, n.pitch);
    off.origin = flag;
    ev.push_back({static_cast<uint64_t>(n.on) * 2 + 1, row(n.on, n.id, 0x90, n.pitch)});
    ev.push_back({static_cast<uint64_t>(n.off) * 2, off});
  }
  std::stable_sort(ev.begin(), ev.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
  for (const auto& e : ev) TEST_ASSERT_EQUAL(AppendResult::Ok, gRuns.append(r, e.second, gPool));
  gRuns.seal(r);
  return r;
}

static ManifestId manifest(std::initializer_list<RunId> runs, uint32_t revision) {
  std::vector<RunId> ids(runs);
  const ManifestId m = gManifests.create(0, revision, kLoop, ids.data(), static_cast<uint8_t>(ids.size()), gRuns);
  TEST_ASSERT_NOT_EQUAL(kNoManifest, m);
  return m;
}

static void sweep(ManifestId m) {
  TEST_ASSERT_EQUAL(Sweep::Status::Running, gSweep.start(gSweepComposer, gTable, gManifests, gRuns, 0, m, kWin, kSpacing));
  while (gSweep.step(gScratch, 128) == Sweep::Status::Running) {
  }
  TEST_ASSERT_EQUAL(Sweep::Status::Done, gSweep.status());
}

/// Cold start of `m` at (cycle, position) through the grain job; the composer is then ready for that window.
static void coldStart(ManifestId m, uint32_t cycle, uint32_t position) {
  Cold::Status st = gColdJob.start(gCold, gTable, gManifests, gRuns, 0, m, position, cycle, kWin);
  while (st == Cold::Status::Running) st = gColdJob.step(gScratch, 128);
  TEST_ASSERT_EQUAL(Cold::Status::Done, st);
}

/// Carried instances of the window about to play that the DIN port never transmitted (still sounding in the
/// terminal scheduler, i.e. started, but not on the DIN wire).
static uint32_t carriedButNeverOnDin(const Window& w) {
  uint32_t n = 0;
  for (uint8_t i = 0; i < w.carryCount; ++i) {
    const auto it = gRig.keyOf.find({w.carry[i].noteId, runIdOf(w.carry[i].runKey)});
    if (it != gRig.keyOf.end() && gRig.wire.sounding[kDin].count(it->second) == 0) ++n;
  }
  return n;
}

void setUp() {
  gPool.init();
  gRuns.init();
  gManifests.init();
  TEST_ASSERT_TRUE(gIndex.init(gPool, gRuns));
  gTable.init();
  gPlay.init(gPool, gRuns, &gIndex);
  gCold.init(gPool, gRuns, &gIndex);
  gSweepComposer.init(gPool, gRuns, &gIndex);
  gSched.init();
  gPlayer.init(gSched);
  gHub.init();
  gHub.configure(kDin, 40000);  // severely backpressured DIN: 40 ms per message
  gHub.configure(kUsb, kUsbMessageMicros);
  gRig = Rig{};
}
void tearDown() {}

void test_restart_and_adoption_under_port_backpressure() {
  // M1: 8-note chords every beat (short and long notes), notes held across the loop end.
  std::vector<N> chords, held, later;
  uint32_t id = 1;
  for (uint32_t beat = 0; beat < kLoop / 192; ++beat)
    for (uint32_t k = 0; k < 8; ++k)
      chords.push_back({id++, beat * 192, beat * 192 + (k < 4 ? 6u : 150u + 40u * k), static_cast<uint8_t>(40 + k * 3)});
  for (uint32_t k = 0; k < 6; ++k) held.push_back({id++, kLoop - 900 + 100 * k, kLoop + 1500 + 100 * k, static_cast<uint8_t>(80 + k)});
  for (uint32_t k = 0; k < 10; ++k) later.push_back({id++, 300 + 500 * k, 300 + 500 * k + 2500, static_cast<uint8_t>(90 + k)});
  const RunId a = makeRun(RunKind::kRecord, chords);
  const RunId b = makeRun(RunKind::kOverdub, held);
  const RunId c = makeRun(RunKind::kOverdub, later);
  const ManifestId m1 = manifest({a}, 1);
  const ManifestId m2 = manifest({a, b}, 2);
  const ManifestId m3 = manifest({a, b, c}, 3);

  // Play M1 from the loop start into cycle 1.
  TEST_ASSERT_TRUE(gPlay.begin([&] {
    static SourceRun s[1];
    s[0] = {a, gRuns.run(a).rowCount};
    return s;
  }(), 1, kLoop, gManifests.manifest(m1).identity));
  uint32_t cycle = 0, pos = 0;
  playUntil(gPlay, cycle, pos, 1, kSpacing);

  // Mid-loop restart into M2 (swept beforehand) at cycle 1, position 3072.
  sweep(m2);
  coldStart(m2, 1, kSpacing);
  TEST_ASSERT_TRUE(gCold.composeNext(gWin, kWin));
  TEST_ASSERT_EQUAL_UINT32(kSpacing, gWin.start);
  const uint32_t neverSentAtRestart = carriedButNeverOnDin(gWin);
  playWindow(gWin);
  uint32_t coldCycle = 1, coldPos = gWin.end;
  playUntil(gCold, coldCycle, coldPos, 2, 4608);

  // Revision adoption M2 -> M3 at cycle 2, position 4608.
  gColdJob.release();
  gPlay.end();
  sweep(m3);
  static TestComposer adopt;
  adopt.init(gPool, gRuns, &gIndex);
  Cold job;
  Cold::Status st = job.start(adopt, gTable, gManifests, gRuns, 0, m3, 4608, 2, kWin);
  while (st == Cold::Status::Running) st = job.step(gScratch, 128);
  TEST_ASSERT_EQUAL(Cold::Status::Done, st);
  TEST_ASSERT_TRUE(adopt.composeNext(gWin, kWin));
  const uint32_t neverSentAtAdoption = carriedButNeverOnDin(gWin);
  gLog.push_back(decisions(gWin));
  gRefused += gWin.refusedCount;
  playWindow(gWin);
  uint32_t aCycle = 2, aPos = gWin.end;
  gLogging = true;
  playUntil(adopt, aCycle, aPos, 4, 0);
  gLogging = false;

  // Stop: every sounding instance closes; drain both ports.
  const uint32_t stopTick = 4 * kLoop;
  TerminalScheduler<16>::Out offs[16];
  const uint8_t closed = gSched.transition(stopTick, nullptr, 0, offs, 16);
  Player::Emit e[16];
  for (uint8_t i = 0; i < closed; ++i) e[i] = Player::Emit{offs[i].tick, 0, offs[i].noteId, offs[i].runKey, offs[i].channel, offs[i].pitch};
  emit(e, closed, stopTick * kTickUs);
  for (uint32_t t = 0; t < 20000; ++t) transmit((stopTick + t) * kTickUs);
  job.release();

  // Physical ownership: balanced wire on every port, nothing left sounding anywhere.
  for (uint8_t p = 0; p < 2; ++p) {
    TEST_ASSERT_EQUAL_UINT32(0, gRig.wire.sounding[p].size());
    TEST_ASSERT_EQUAL_UINT32(gRig.wire.noteOns[p], gRig.wire.noteOffs[p]);
    TEST_ASSERT_EQUAL_UINT16(0, gHub.soundingCount(p));
  }
  TEST_ASSERT_EQUAL_UINT32(0, gSched.sounding());
  TEST_ASSERT_TRUE(gRig.keyOf.empty());
  // Backpressure was real: DIN dropped or expired notes USB transmitted.
  const auto& din = gHub.port(kDin).counters();
  TEST_ASSERT_TRUE(din.suppressedNoteOn + din.expiredBeforeTransmit > 0);
  TEST_ASSERT_TRUE(gRig.wire.noteOns[kDin] < gRig.wire.noteOns[kUsb]);
  // The composer's carried state named notes the DIN port never transmitted; none of them got a DIN NoteOff.
  printf("restart: %u carried never on DIN; adoption: %u; DIN on %u / USB on %u; suppressed %u expired %u\n",
         static_cast<unsigned>(neverSentAtRestart), static_cast<unsigned>(neverSentAtAdoption),
         static_cast<unsigned>(gRig.wire.noteOns[kDin]), static_cast<unsigned>(gRig.wire.noteOns[kUsb]),
         static_cast<unsigned>(din.suppressedNoteOn), static_cast<unsigned>(din.expiredBeforeTransmit));
  TEST_ASSERT_TRUE(neverSentAtRestart > 0);
  TEST_ASSERT_TRUE(neverSentAtAdoption > 0);
  TEST_ASSERT_EQUAL_UINT16(0, gManifests.manifest(m3).readerPins);

  // Q-OUT-2: the adoption and the saturated DIN port changed nothing in the source's later eligibility. An isolated
  // cold start of M3 at the same place, with no player or port, decides every following window identically.
  TEST_ASSERT_TRUE(gRefused > 0);  // the source exceeded T: selection was exercised
  static TestComposer isolated;
  isolated.init(gPool, gRuns, &gIndex);
  Cold iso;
  Cold::Status s2 = iso.start(isolated, gTable, gManifests, gRuns, 0, m3, 4608, 2, kWin);
  while (s2 == Cold::Status::Running) s2 = iso.step(gScratch, 128);
  TEST_ASSERT_EQUAL(Cold::Status::Done, s2);
  for (size_t i = 0; i < gLog.size(); ++i) {
    if (!isolated.composeNext(gWin, kWin)) {
      isolated.nextCycle();
      TEST_ASSERT_TRUE(isolated.composeNext(gWin, kWin));
    }
    TEST_ASSERT_TRUE_MESSAGE(decisions(gWin) == gLog[i], "port congestion or adoption changed source eligibility");
  }
  iso.release();
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_restart_and_adoption_under_port_backpressure);
  return UNITY_END();
}
