//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// Stage B.2a of openspec/changes/revision-manifest-prototype: drive one operation sequence through the production
// `Loop` (capture → commit → snapshot publication → `tryCopyCurrentSnapshotEditViewEvents`) and through
// `RevisionStore::Store`, and compare the ordered effective stream of every revision.
//
// Parity key (B.2a): production has no single equal-tick order, so streams are compared as sequences of
// (tick, equal-tick class) groups, each group compared as a multiset of MIDI content. See design.md § Stage B findings.

#include <unity.h>

#include "../../src/Logger.cpp"
#include "../../src/Utils/NoteUtils.cpp"
#include "../../src/LoopEventStore.cpp"
#include "../../src/EditManager/EditApply.cpp"
#include "../../src/Loop/LoopPasses.cpp"
#include "../test_support/MemoryMonitorNativeDeps.cpp"
#include "../../src/Utils/LoopEventValidation.cpp"
#include "../../src/Loop.cpp"
#include "../test_support/LoopCaptureTestDeps.cpp"

#include <algorithm>
#include <cstdio>
#include <tuple>
#include <vector>

#include "Loop.h"
#include "MidiEvent.h"
#include "RevisionStore/RevisionStore.h"

namespace {

using namespace RevisionStore;
using DiffCap = Capacity<1024, 32, 32, 128, 32, 16, 16>;
static Store<DiffCap> gStore;
static PassEvent gOut[8192];

constexpr uint32_t kLength = 8 * Config::TICKS_PER_BAR;

// One comparable row: content only (no seq, no noteId -- production NoteOffs carry no noteId).
struct Row {
  uint32_t tick;
  uint8_t cls;
  uint8_t status;
  uint8_t data1;
  uint8_t data2;
  bool operator<(const Row& o) const {
    return std::tie(tick, cls, status, data1, data2) < std::tie(o.tick, o.cls, o.status, o.data1, o.data2);
  }
  bool operator==(const Row& o) const {
    return tick == o.tick && cls == o.cls && status == o.status && data1 == o.data1 && data2 == o.data2;
  }
};

uint8_t statusOf(const MidiEvent& e) {
  return static_cast<uint8_t>(static_cast<uint8_t>(e.type) | ((e.channel - 1) & 0x0F));
}

Row rowOf(const MidiEvent& e) {
  PassEvent p;
  p.tick = e.tick;
  p.status = statusOf(e);
  p.data1 = e.data.noteData.note;
  p.data2 = e.data.noteData.velocity;
  return {p.tick, equalTickClass(p), p.status, p.data1, p.data2};
}

Row rowOf(const PassEvent& p) { return {p.tick, equalTickClass(p), p.status, p.data1, p.data2}; }

// Normalise: keep the (tick, class) group sequence as produced, sort inside each group. Returns false if the
// producer's own group sequence is not ordered by (tick, class) -- an ordering violation, not a parity gap.
bool normalise(std::vector<Row>& rows) {
  size_t i = 0;
  while (i < rows.size()) {
    size_t j = i + 1;
    while (j < rows.size() && rows[j].tick == rows[i].tick && rows[j].cls == rows[i].cls) ++j;
    std::sort(rows.begin() + i, rows.begin() + j);
    if (j < rows.size()) {
      const bool ordered = rows[j].tick > rows[i].tick || (rows[j].tick == rows[i].tick && rows[j].cls > rows[i].cls);
      if (!ordered) return false;
    }
    i = j;
  }
  return true;
}

struct Op {
  uint32_t tick;
  uint8_t type;  // midi::MidiType value
  uint8_t channel;
  uint8_t data1;
  uint8_t data2;
};

MidiEvent toMidi(const Op& op) {
  switch (op.type) {
    case 0x90: return MidiEvent::NoteOn(op.tick, op.channel, op.data1, op.data2);
    case 0x80: return MidiEvent::NoteOff(op.tick, op.channel, op.data1, op.data2);
    default: return MidiEvent::ControlChange(op.tick, op.channel, op.data1, op.data2);
  }
}

void finishPublication(Loop& loop) {
  TEST_ASSERT_TRUE(loop.requestCommittedPassesRebuild());
  for (uint32_t grain = 0; grain < 4096 && loop.hasPendingSnapshotPublish(); ++grain) {
    loop.stepPendingSnapshotPublish(1);
  }
  TEST_ASSERT_FALSE_MESSAGE(loop.hasPendingSnapshotPublish(), "snapshot drain exhausted");
  TEST_ASSERT_TRUE(loop.publishedSnapshotIsCurrent());
}

// Production owns capture finalization (min length, pairing, wrap); the store receives what production committed.
// So the raw ops go to the `Loop`, and the store's pass is fed the committed pass's chunk rows in stored order.
void commitBoth(Loop& loop, bool record, const std::vector<Op>& ops) {
  loop.beginCapture(record ? CapturePhase::Record : CapturePhase::Overdub);
  uint32_t lastTick = 0;
  for (const Op& op : ops) {
    TEST_ASSERT_TRUE(loop.appendCaptureEvent(toMidi(op)));
    lastTick = std::max(lastTick, op.tick);
  }
  TEST_ASSERT_EQUAL(CommitResult::Committed,
                    loop.commitCapturePass(record ? CommitReason::RecordStop : CommitReason::OverdubStop, lastTick + 1));
  const CommittedChunkIdList& committed =
      record ? loop.passes.recordPass.committedChunkIds : loop.passes.overdubPasses.back().committedChunkIds;
  const CommittedChunkIdList* lists[] = {&committed};
  SessionMidiEventVec rows;
  CommittedEventRange::full(lists, 1, kLength).appendTo(rows);
  TEST_ASSERT_EQUAL(AdmitResult::Ok, gStore.beginPass(record ? PassKind::Record : PassKind::Overdub, kLength));
  for (const MidiEvent& e : rows) {
    TEST_ASSERT_EQUAL(AppendResult::Ok,
                      gStore.appendMidi(e.tick, statusOf(e), e.data.noteData.note, e.data.noteData.velocity, e.noteId));
  }
  gStore.commitPass();
  finishPublication(loop);
}

// Returns the number of groups compared; fails the test on the first divergence, naming it.
void assertParity(const Loop& loop, const char* label) {
  SessionMidiEventVec production;
  TEST_ASSERT_TRUE(loop.tryCopyCurrentSnapshotEditViewEvents(production));
  std::vector<Row> a;
  for (const MidiEvent& e : production) a.push_back(rowOf(e));
  const ComposeStamp stamp = gStore.composeWindow(gStore.current(), 0, kLength, gOut, 8192);
  TEST_ASSERT_EQUAL(ComposeStatus::Complete, stamp.status);
  std::vector<Row> b;
  for (uint32_t i = 0; i < stamp.count; ++i) b.push_back(rowOf(gOut[i]));
  TEST_ASSERT_TRUE_MESSAGE(normalise(a), "production stream not ordered by (tick, class)");
  TEST_ASSERT_TRUE_MESSAGE(normalise(b), "store stream not ordered by (tick, class)");
  const size_t n = std::min(a.size(), b.size());
  for (size_t i = 0; i < n; ++i) {
    if (!(a[i] == b[i])) {
      char msg[200];
      snprintf(msg, sizeof(msg), "%s: row %u production t=%u s=%02X d=%u/%u store t=%u s=%02X d=%u/%u", label,
               static_cast<unsigned>(i), a[i].tick, a[i].status, a[i].data1, a[i].data2, b[i].tick, b[i].status,
               b[i].data1, b[i].data2);
      TEST_FAIL_MESSAGE(msg);
    }
  }
  char sizes[120];
  snprintf(sizes, sizeof(sizes), "%s: production %u rows, store %u rows", label, static_cast<unsigned>(a.size()),
           static_cast<unsigned>(b.size()));
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(a.size(), b.size(), sizes);
}

void resetAll(Loop& loop) {
  LoopEventStore::resetPoolForTests();
  LoopEventStore::initPool();
  loop.loopLengthTicks = kLength;
  gStore.reset();
}

}  // namespace

void setUp() {}
void tearDown() {}

// Non-overlapping notes and same-tick class collisions across Record and two overdubs.
void test_record_and_overdubs_without_overlap_match_production() {
  Loop loop;
  resetAll(loop);
  commitBoth(loop, true,
             {{96, 0x90, 1, 60, 100}, {192, 0x80, 1, 60, 0}, {192, 0x90, 1, 62, 90}, {384, 0x80, 1, 62, 0},
              {384, 0xB0, 1, 1, 64}});
  assertParity(loop, "rev1");
  commitBoth(loop, false, {{192, 0x90, 2, 72, 80}, {288, 0x80, 2, 72, 0}, {384, 0x90, 2, 74, 80},
                           {480, 0x80, 2, 74, 0}});
  assertParity(loop, "rev2");
  commitBoth(loop, false, {{0, 0x90, 3, 36, 100}, {96, 0x80, 3, 36, 0}});
  assertParity(loop, "rev3");
}

// A Record take that crosses the wrap: NoteOff tick < NoteOn tick.
void test_wrap_crossing_record_note_matches_production() {
  Loop loop;
  resetAll(loop);
  commitBoth(loop, true, {{kLength - 48, 0x90, 1, 60, 100}, {48, 0x80, 1, 60, 0}, {384, 0x90, 1, 64, 100},
                          {480, 0x80, 1, 64, 0}});
  assertParity(loop, "wrap rev1");
}

// Diagnosis aid: print both streams (not called by default).
[[maybe_unused]] void dumpBoth(const Loop& loop, const char* label) {
  SessionMidiEventVec production;
  loop.tryCopyCurrentSnapshotEditViewEvents(production);
  printf("PROBE %s production:", label);
  for (const MidiEvent& e : production) {
    const Row r = rowOf(e);
    printf(" %u:%02X/%u/%u", r.tick, r.status, r.data1, r.data2);
  }
  const ComposeStamp stamp = gStore.composeWindow(gStore.current(), 0, kLength, gOut, 8192);
  printf("\nPROBE %s store:     ", label);
  for (uint32_t i = 0; i < stamp.count; ++i) {
    const Row r = rowOf(gOut[i]);
    printf(" %u:%02X/%u/%u", r.tick, r.status, r.data1, r.data2);
  }
  printf("\n");
}

// Same-tick duplicate CC (sustain 127 then 0) and a same-pitch overlap between Record and an overdub (not resolved at
// commit on this path: both notes stay).
void test_same_tick_cc_and_cross_pass_overlap_match_production() {
  Loop loop;
  resetAll(loop);
  commitBoth(loop, true, {{96, 0xB0, 1, 64, 127}, {96, 0xB0, 1, 64, 0}, {96, 0x90, 1, 60, 100}, {480, 0x80, 1, 60, 0}});
  assertParity(loop, "sustain-127-then-0");
  commitBoth(loop, false, {{288, 0x90, 1, 60, 90}, {384, 0x80, 1, 60, 0}});
  assertParity(loop, "same-pitch overlap across passes");
}

// NAMED DIVERGENCE (B.2a finding 3): a zero-length note (NoteOn + velocity-0 NoteOn on one tick) is committed to the
// chunk rows, but production's effective view drops note events whose noteId has no display span
// (`copyEditViewEventsFromSnapshot`). The store keeps them. Which layer owns that rule is a B.3 decision.
void test_zero_length_note_is_dropped_by_production_effective_view() {
  Loop loop;
  resetAll(loop);
  commitBoth(loop, true, {{96, 0x90, 1, 60, 100}, {480, 0x80, 1, 60, 0}, {192, 0x90, 1, 50, 100},
                          {192, 0x90, 1, 50, 0}});
  SessionMidiEventVec production;
  TEST_ASSERT_TRUE(loop.tryCopyCurrentSnapshotEditViewEvents(production));
  const ComposeStamp stamp = gStore.composeWindow(gStore.current(), 0, kLength, gOut, 8192);
  TEST_ASSERT_EQUAL_UINT32(2, production.size());
  TEST_ASSERT_EQUAL_UINT32(4, stamp.count);
  TEST_IGNORE_MESSAGE("known divergence: production effective view drops span-less (zero-length) notes");
}

int main(int /*argc*/, char** /*argv*/) {
  UNITY_BEGIN();
  RUN_TEST(test_record_and_overdubs_without_overlap_match_production);
  RUN_TEST(test_wrap_crossing_record_note_matches_production);
  RUN_TEST(test_same_tick_cc_and_cross_pass_overlap_match_production);
  RUN_TEST(test_zero_length_note_is_dropped_by_production_effective_view);
  return UNITY_END();
}
