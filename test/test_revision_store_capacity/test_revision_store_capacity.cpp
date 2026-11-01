//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// B.5 of openspec/changes/revision-manifest-prototype: the worst-case page budget of one slot, measured by driving the
// real store through a long session and recording `ownedPageCount()` after every operation.
//
// Session: a dense 64-bar Record (16,640 rows, the densest observed device bar for 64 bars), then 30 overdubs of
// 1,040 rows that each hide up to 4 earlier notes; every fifth overdub is undone and replaced by a different one
// (a discarded branch); a reader pins the previous generation while each overdub is written; stepped compaction
// (2,048 rows per step) is interleaved with the next overdub's appends whenever the manifest reaches 8 runs.

#include <unity.h>

#include <cstdio>
#include <memory>

#include "RevisionStore/RevisionStore.h"

using namespace RevisionStore;

namespace {

constexpr uint32_t kBarTicks = 768;
constexpr uint32_t kLoopTicks = 64 * kBarTicks;
constexpr uint32_t kRecordNotes = 8320;   // 16,640 rows
constexpr uint32_t kOverdubNotes = 520;   // 1,040 rows
constexpr uint32_t kOverdubs = 30;
constexpr uint8_t kCompactAt = 8;
constexpr uint32_t kStepRows = 2048;
constexpr uint32_t kRowsPerPage = 64;

template <uint8_t History>
using CapFor = Capacity</*pages*/ 4096, kRowsPerPage, /*runs*/ 255, /*pages per run*/ 1024, /*manifests*/ 255,
                        /*runs per manifest*/ 32, History, /*segments*/ 8, /*hide pages*/ 4>;

enum class Policy { WholePrefix, OverdubsOnly, None };

struct Result {
  uint32_t peakPages = 0;
  uint32_t endPages = 0;
  uint32_t contentPages = 0;  // rows of the final revision / 64, rounded up: the lower bound
  uint32_t compactions = 0;
  uint32_t superseded = 0;
  uint32_t slowSeals = 0;
  uint32_t revision = 0;
  uint64_t streamHash = 0;  // FNV-1a over the final revision's composed rows (tick, status, data1, data2)
};

uint32_t gLcg = 1;
uint32_t nextRandom() {
  gLcg = gLcg * 1664525u + 1013904223u;
  return gLcg >> 8;
}

template <typename S>
struct Session {
  S& store;
  Policy policy;
  Result result;
  std::unique_ptr<PassEvent[]> window{new PassEvent[65536]};
  uint32_t nextNoteId = 1;
  uint32_t recordFirstId = 0;

  void track() {
    const uint32_t owned = store.ownedPageCount();
    if (owned > result.peakPages) result.peakPages = owned;
  }

  void stepCompactionIfActive() {
    if (!store.compacting()) return;
    const CompactStep step = store.stepCompaction(kStepRows);
    if (step == CompactStep::Published) ++result.compactions;
    if (step == CompactStep::Superseded) ++result.superseded;
    TEST_ASSERT_NOT_EQUAL(CompactStep::DeniedNoPage, step);
    track();
  }

  void maybeBeginCompaction() {
    if (policy == Policy::None || store.compacting()) return;
    const uint8_t runs = store.manifest(store.current()).runCount;
    if (runs < kCompactAt) return;
    const CompactResult begun = policy == Policy::WholePrefix ? store.beginCompaction(0, runs)
                                                              : store.beginCompaction(1, static_cast<uint8_t>(runs - 1));
    TEST_ASSERT_EQUAL(CompactResult::Ok, begun);
  }

  // Capture-shaped: time order from mid-loop, NoteOffs when due. One compaction step per 256 appended rows.
  void appendTake(uint32_t notes, uint8_t channel) {
    struct Pending {
      uint32_t off;
      uint8_t pitch;
      uint32_t id;
    } pending[64];
    uint32_t count = 0;
    uint32_t appended = 0;
    const uint32_t step = kLoopTicks / notes;
    auto add = [&](uint32_t tick, uint8_t status, uint8_t pitch, uint8_t velocity, uint32_t id) {
      TEST_ASSERT_EQUAL(AppendResult::Ok, store.appendMidi(tick % kLoopTicks, status, pitch, velocity, id));
      if (++appended % 256 == 0) {
        track();
        stepCompactionIfActive();
      }
    };
    auto flush = [&](uint32_t now) {
      for (;;) {
        uint32_t best = count;
        for (uint32_t k = 0; k < count; ++k) {
          if (pending[k].off <= now && (best == count || pending[k].off < pending[best].off)) best = k;
        }
        if (best == count) return;
        add(pending[best].off, static_cast<uint8_t>(0x80 | channel), pending[best].pitch, 0, pending[best].id);
        pending[best] = pending[--count];
      }
    };
    const uint32_t start = nextRandom() % kLoopTicks;
    for (uint32_t i = 0; i < notes; ++i) {
      const uint32_t on = start + i * step;
      flush(on);
      const uint8_t pitch = static_cast<uint8_t>(36 + nextRandom() % 48);
      const uint32_t id = nextNoteId++;
      add(on, static_cast<uint8_t>(0x90 | channel), pitch, 100, id);
      pending[count++] = {on + 24 + nextRandom() % 96, pitch, id};
    }
    flush(UINT32_MAX);
  }

  void overdub(uint8_t channel) {
    const ManifestId reader = store.pinCurrent();  // playback holds the previous generation while we write
    TEST_ASSERT_EQUAL(AdmitResult::Ok, store.beginPass(PassKind::Overdub));
    for (uint32_t h = 0, n = nextRandom() % 5; h < n; ++h) {
      store.appendHide(recordFirstId + nextRandom() % kRecordNotes);
    }
    appendTake(kOverdubNotes, channel);
    store.commitPass();
    track();
    store.unpin(reader);
    track();
    maybeBeginCompaction();
  }

  void run() {
    TEST_ASSERT_EQUAL(AdmitResult::Ok, store.beginPass(PassKind::Record, kLoopTicks));
    recordFirstId = nextNoteId;
    appendTake(kRecordNotes, 0);
    store.commitPass();
    track();
    for (uint32_t od = 1; od <= kOverdubs; ++od) {
      overdub(static_cast<uint8_t>(1 + od % 3));
      if (od % 5 == 0 && store.canUndo()) {
        TEST_ASSERT_TRUE(store.undo());  // the next overdub discards this one (a branch)
        track();
      }
    }
    while (store.compacting()) stepCompactionIfActive();
    result.endPages = store.ownedPageCount();
    const ComposeStamp stamp = store.composeWindow(store.current(), 0, kLoopTicks, window.get(), 65536);
    TEST_ASSERT_EQUAL(ComposeStatus::Complete, stamp.status);
    result.contentPages = (stamp.count + kRowsPerPage - 1) / kRowsPerPage;
    result.revision = stamp.revision;
    uint64_t h = 1469598103934665603ull;
    for (uint32_t i = 0; i < stamp.count; ++i) {
      const PassEvent& e = window[i];
      const uint32_t fields[] = {e.tick, e.status, e.data1, e.data2};
      for (uint32_t f : fields) {
        h ^= f;
        h *= 1099511628211ull;
      }
    }
    result.streamHash = h;
    result.slowSeals = store.slowSeals();
    TEST_ASSERT_EQUAL_UINT32(S::kPageCountForTest, store.ownedPageCount() + store.freePageCount());
  }
};

template <uint8_t History>
struct StoreWithPageCount : Store<CapFor<History>> {
  static constexpr uint32_t kPageCountForTest = CapFor<History>::kPageCount;
};

template <uint8_t History>
Result runPolicy(Policy policy) {
  gLcg = 7;
  auto store = std::make_unique<StoreWithPageCount<History>>();
  Session<StoreWithPageCount<History>> session{*store, policy};
  session.run();
  return session.result;
}

void report(const char* label, const Result& r) {
  printf("B5 %-28s peak_pages=%4u end_pages=%4u content_pages=%4u peak/content=%.2f compactions=%u superseded=%u "
         "slow_seals=%u revision=%u\n",
         label, r.peakPages, r.endPages, r.contentPages, r.contentPages ? double(r.peakPages) / r.contentPages : 0.0,
         r.compactions, r.superseded, r.slowSeals, r.revision);
}

}  // namespace

void setUp() {}
void tearDown() {}

void test_slot_page_budget_by_compaction_policy_and_history_depth() {
  const Result none16 = runPolicy<16>(Policy::None);
  const Result whole16 = runPolicy<16>(Policy::WholePrefix);
  const Result ods16 = runPolicy<16>(Policy::OverdubsOnly);
  const Result whole99 = runPolicy<99>(Policy::WholePrefix);
  const Result ods99 = runPolicy<99>(Policy::OverdubsOnly);
  report("no compaction, history 16", none16);
  report("whole prefix, history 16", whole16);
  report("overdubs only, history 16", ods16);
  report("whole prefix, history 99", whole99);
  report("overdubs only, history 99", ods99);
  printf("B5 descriptor bytes per store (history 99, excluding pages): %u\n",
         static_cast<unsigned>(sizeof(Store<CapFor<99>>) - sizeof(typename Store<CapFor<99>>::EventPage) * 4096));
  // Every policy must end with exactly the same ordered stream (compaction never changes content).
  TEST_ASSERT_TRUE(none16.streamHash == whole16.streamHash);
  TEST_ASSERT_TRUE(none16.streamHash == ods16.streamHash);
  TEST_ASSERT_TRUE(none16.streamHash == whole99.streamHash);
  TEST_ASSERT_TRUE(none16.streamHash == ods99.streamHash);
  TEST_ASSERT_EQUAL_UINT32(0, ods99.slowSeals);
}

int main(int /*argc*/, char** /*argv*/) {
  UNITY_BEGIN();
  RUN_TEST(test_slot_page_budget_by_compaction_policy_and_history_depth);
  return UNITY_END();
}
