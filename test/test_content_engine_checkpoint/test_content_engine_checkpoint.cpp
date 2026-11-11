//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// fixed-memory-engine E3: restart checkpoints. A checkpoint is the composer's carry state at a window boundary (sparse
// source counters <= 64 entries, admitted notes <= 16, stamps). Resuming from it must compose exactly the windows the
// uninterrupted composition produces from that point on: same entries, same carry lists, across cycles, with notes held
// across the loop end, held-loop notes (Q-LEN-1) and edit hides. A checkpoint that does not fit is refused by name.

#include <unity.h>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <new>
#include <vector>

#include "ContentEngine/Composition/SpanWindowComposer.h"
#include "ContentEngine/EngineCore.h"
#include "ContentEngine/Composition/SpanWindow.h"

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

using Pool = PagePool<2048, 64>;
using Runs = RunTable<32, Pool>;
using TestComposer = Composer<Pool, Runs>;
using Window = SpanWindow<3072>;

static Pool gPool;
static Runs gRuns;
static EditIndexStore<Pool, Runs> gIndex;
static TestComposer gRef, gRes;
static Window gWin;

constexpr uint32_t kLoop = 768 * 8;
constexpr uint32_t kWin = 1536;
constexpr uint32_t kCycles = 3;

struct N {
  uint32_t id;
  uint8_t channel, pitch;
  uint32_t on, off;  // off may exceed the loop (held across the end); off - on >= loop is a held-loop note
};

static PassEvent row(uint32_t tick, uint32_t id, uint8_t status, uint8_t d1, uint8_t d2, uint8_t origin) {
  PassEvent e;
  e.tick = tick;
  e.noteId = id;
  e.seq = 0;
  e.status = status;
  e.data1 = d1;
  e.data2 = d2;
  e.origin = origin;
  return e;
}

static RunId makeRun(uint8_t kind, const std::vector<uint32_t>& hides, const std::vector<N>& notes) {
  const RunId r = gRuns.allocate(0, kind);
  for (uint32_t id : hides) gRuns.append(r, row(0, id, kHideNoteStatus, 0, 0, 0), gPool);
  struct Ev {
    uint32_t tick;
    bool on;
    N n;
  };
  std::vector<Ev> ev;
  for (const auto& n : notes) {
    ev.push_back({n.on, true, n});
    ev.push_back({n.off, false, n});
  }
  std::stable_sort(ev.begin(), ev.end(), [](const Ev& a, const Ev& b) {
    if (a.tick != b.tick) return a.tick < b.tick;
    return !a.on && b.on;
  });
  for (const auto& e : ev) {
    const uint8_t flag = !e.on && e.n.off - e.n.on >= kLoop ? kOriginHeldLoop : 0;
    TEST_ASSERT_EQUAL(AppendResult::Ok,
                      gRuns.append(r, row(e.tick, e.n.id, static_cast<uint8_t>((e.on ? 0x90 : 0x80) | (e.n.channel - 1)),
                                          e.n.pitch, e.on ? 100 : 0, flag),
                                   gPool));
  }
  gRuns.seal(r);
  return r;
}

struct Snapshot {  // one composed window, for comparison
  uint32_t cycle, start, count, carryCount;
  std::vector<SpanEntry> entries;
  std::vector<CarryRef> carry;
};

static Snapshot snap(const Window& w, uint32_t cycle) {
  Snapshot s{cycle, w.start, w.count, w.carryCount, {}, {}};
  s.entries.assign(w.entries, w.entries + w.count);
  s.carry.assign(w.carry, w.carry + w.carryCount);
  return s;
}

static bool same(const Snapshot& a, const Snapshot& b) {
  if (a.cycle != b.cycle || a.start != b.start || a.count != b.count || a.carryCount != b.carryCount) return false;
  for (uint32_t i = 0; i < a.count; ++i)
    if (std::memcmp(&a.entries[i], &b.entries[i], sizeof(SpanEntry)) != 0) return false;
  for (uint32_t i = 0; i < a.carryCount; ++i)
    if (a.carry[i].noteId != b.carry[i].noteId || a.carry[i].runKey != b.carry[i].runKey) return false;
  return true;
}

void setUp() {
  gPool.init();
  gRuns.init();
  gIndex.init(gPool, gRuns);
  gRef.init(gPool, gRuns, &gIndex);
  gRes.init(gPool, gRuns, &gIndex);
}
void tearDown() {}

/// Composes kCycles from a cold start, capturing a checkpoint at every window boundary; then resumes from each one and
/// requires the resumed windows to equal the reference windows from that point on.
static uint32_t checkResumeEquivalence(const SourceRun* src, uint8_t count) {
  std::vector<Snapshot> ref;
  std::vector<TestComposer::Checkpoint> cps;
  std::vector<size_t> cpWindow;  // index into `ref` of the first window after the checkpoint
  TEST_ASSERT_TRUE(gRef.begin(src, count, kLoop, 7));
  for (uint32_t c = 0; c < kCycles; ++c) {
    while (gRef.composeNext(gWin, kWin)) {
      ref.push_back(snap(gWin, c));
      TestComposer::Checkpoint cp;
      if (gRef.capture(cp)) {
        cps.push_back(cp);
        cpWindow.push_back(ref.size());
      }
    }
    gRef.nextCycle();
  }
  uint32_t resumed = 0;
  for (size_t k = 0; k < cps.size(); ++k) {
    TEST_ASSERT_TRUE(gRes.resume(src, count, kLoop, 7, cps[k]));
    size_t i = cpWindow[k];
    gTrap = true;
    const unsigned long heapBefore = gHeapAllocations;
    for (uint32_t c = cps[k].cycle; c < kCycles && i < ref.size(); ++c) {
      while (i < ref.size() && gRes.composeNext(gWin, kWin)) {
        gTrap = false;
        TEST_ASSERT_TRUE_MESSAGE(same(snap(gWin, c), ref[i]), "resumed window differs from the reference");
        gTrap = true;
        ++i;
      }
      gRes.nextCycle();
    }
    gTrap = false;
    TEST_ASSERT_EQUAL_UINT32(0, gHeapAllocations - heapBefore);
    TEST_ASSERT_EQUAL_UINT32(ref.size(), i);
    ++resumed;
  }
  return resumed;
}

void test_resume_equals_uninterrupted_composition_random() {
  uint32_t seed = 0xC0DE;
  auto rnd = [&seed]() {
    seed ^= seed << 13;
    seed ^= seed >> 17;
    seed ^= seed << 5;
    return seed;
  };
  uint32_t total = 0;
  for (uint32_t trial = 0; trial < 30; ++trial) {
    setUp();
    const uint8_t passes = static_cast<uint8_t>(1 + rnd() % 5);
    RunId runs[8];
    SourceRun src[8];
    uint32_t id = 1;
    std::vector<uint32_t> ids;
    for (uint8_t p = 0; p < passes; ++p) {
      std::vector<N> notes;
      bool used[17][12] = {};
      for (uint32_t k = 0, n = rnd() % 12; k < n; ++k) {
        const uint8_t ch = static_cast<uint8_t>(1 + rnd() % 3), pitch = static_cast<uint8_t>(rnd() % 12);
        if (used[ch][pitch]) continue;
        used[ch][pitch] = true;
        const uint32_t on = rnd() % kLoop;
        const uint32_t len = 1 + rnd() % (rnd() % 4 == 0 ? (rnd() % 2 ? 3 * kLoop : 3000) : 400);
        ids.push_back(id);
        notes.push_back({id++, ch, static_cast<uint8_t>(48 + pitch), on, on + len});
      }
      runs[p] = makeRun(p == 0 ? RunKind::kRecord : RunKind::kOverdub, {}, notes);
      src[p] = {runs[p], gRuns.run(runs[p]).rowCount};
    }
    uint8_t count = passes;
    if (!ids.empty() && rnd() % 2) {  // an edit run hiding one note and adding a replacement with its id
      const uint32_t target = ids[rnd() % ids.size()];
      const uint32_t on = rnd() % kLoop;
      runs[count] = makeRun(RunKind::kEdit, {target}, {{target, 1, 60, on, on + 200}});
      src[count] = {runs[count], gRuns.run(runs[count]).rowCount};
      ++count;
    }
    total += checkResumeEquivalence(src, count);
  }
  TEST_ASSERT_TRUE(total > 100);  // checkpoints were actually exercised
}

// A checkpoint whose sounding source set exceeds 64 (pitch, pass) entries is refused by name; the composition itself
// is unaffected.
void test_checkpoint_that_does_not_fit_is_refused() {
  RunId runs[3];
  SourceRun src[3];
  uint32_t id = 1;
  for (uint8_t p = 0; p < 3; ++p) {
    std::vector<N> notes;
    for (uint8_t k = 0; k < 30; ++k) notes.push_back({id++, 1, static_cast<uint8_t>(20 + k + 30 * p), 100, 3000});
    runs[p] = makeRun(RunKind::kOverdub, {}, notes);
    src[p] = {runs[p], gRuns.run(runs[p]).rowCount};
  }
  TEST_ASSERT_TRUE(gRef.begin(src, 3, kLoop, 1));
  TEST_ASSERT_TRUE(gRef.composeNext(gWin, kWin));  // at 1536 all 90 notes sound
  TestComposer::Checkpoint cp;
  TEST_ASSERT_FALSE(gRef.capture(cp));
  TEST_ASSERT_FALSE(cp.valid);
  TEST_ASSERT_FALSE(gRes.resume(src, 3, kLoop, 1, cp));
}

// A checkpoint only resumes the source it was taken from (identity, loop length, run count).
void test_checkpoint_rejects_another_source() {
  const RunId r = makeRun(RunKind::kRecord, {}, {{1, 1, 60, 100, 2000}});
  const SourceRun src[] = {{r, gRuns.run(r).rowCount}};
  TEST_ASSERT_TRUE(gRef.begin(src, 1, kLoop, 5));
  TEST_ASSERT_TRUE(gRef.composeNext(gWin, kWin));
  TestComposer::Checkpoint cp;
  TEST_ASSERT_TRUE(gRef.capture(cp));
  TEST_ASSERT_FALSE(gRes.resume(src, 1, kLoop, 6, cp));
  TEST_ASSERT_FALSE(gRes.resume(src, 1, kLoop * 2, 5, cp));
  TEST_ASSERT_TRUE(gRes.resume(src, 1, kLoop, 5, cp));
}

// Replay bound: a start at any position resumes from the nearest earlier checkpoint, so it composes at most one window
// before reaching its own (checkpoints at every window boundary here).
void test_resume_reaches_target_window_without_replaying_the_loop() {
  const RunId r = makeRun(RunKind::kRecord, {}, {{1, 1, 60, 100, 5000}, {2, 1, 62, 4700, 4800}});
  const SourceRun src[] = {{r, gRuns.run(r).rowCount}};
  TEST_ASSERT_TRUE(gRef.begin(src, 1, kLoop, 9));
  TestComposer::Checkpoint at4608;
  for (int w = 0; w < 3; ++w) TEST_ASSERT_TRUE(gRef.composeNext(gWin, kWin));
  TEST_ASSERT_TRUE(gRef.capture(at4608));
  TEST_ASSERT_EQUAL_UINT32(4608, at4608.position);
  TEST_ASSERT_TRUE(gRes.resume(src, 1, kLoop, 9, at4608));
  TEST_ASSERT_TRUE(gRes.composeNext(gWin, kWin));
  TEST_ASSERT_EQUAL_UINT32(4608, gWin.start);
  TEST_ASSERT_EQUAL_UINT8(1, gWin.carryCount);  // note 1 carried in from the checkpoint
  TEST_ASSERT_EQUAL_UINT32(1, gWin.carry[0].noteId);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_resume_equals_uninterrupted_composition_random);
  RUN_TEST(test_checkpoint_that_does_not_fit_is_refused);
  RUN_TEST(test_checkpoint_rejects_another_source);
  RUN_TEST(test_resume_reaches_target_window_without_replaying_the_loop);
  return UNITY_END();
}
