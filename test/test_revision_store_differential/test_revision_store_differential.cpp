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
#define LOOP_PENDING_NOTE_CHANGE_PROVIDES_DISPLAY_APPLY
#include "../test_support/LoopCaptureTestDeps.cpp"
#include "../../src/EditManager/EditSessionLiveStoreSpan.cpp"
#include "../../src/EditManager/NoteEditCurrentState.cpp"
#include "../../src/EditManager/EditSessionInteraction.cpp"
#include "../../src/EditManager/ResolveConstrainedGeometry.cpp"
#include "../../src/EditManager/ParticipatingNoteSession.cpp"
#include "../../src/Loop/LoopPendingNoteChange.cpp"
#include "../../src/Utils/RuntimeTimingTelemetry.cpp"
#include "../../src/Track/RuntimePlaybackView.cpp"
#include "../../src/Track/RuntimeCaptureCompanions.cpp"
#include "../../src/Utils/PlaybackCursorAdvance.cpp"
#include "../../src/Utils/CommittedPlaybackLedgerCatchUp.cpp"

#include <algorithm>
#include <cstdio>
#include <iterator>
#include <tuple>
#include <vector>

#include "Loop.h"
#include "MidiEvent.h"
#include "EditPass.h"
#include "PendingNoteChange.h"
#include "RevisionStore/RevisionStore.h"
#include "RevisionStore/RevisionImport.h"

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

// ---- C1: import parity ----------------------------------------------------------------------------------------------
// The Stage-C import (`RevisionStore::importLoop`) of the production Loop, read the real-time way (edit index built off
// the edge, then `IndexOnly`) under the production projection, must equal production's effective view.
using ImportCap = Capacity</*pages*/ 512, /*rows per page*/ 32, /*runs*/ 48, /*pages per run*/ 256, /*manifests*/ 48,
                           /*runs per manifest*/ 48, /*history*/ 2>;  // 512 x 512 B = 256 KiB; shallow history as on device
static Store<ImportCap> gImport;
static uint32_t gImportChecks = 0;
static uint32_t gImportUnpairedOffs = 0;

void assertImportParity(const Loop& loop, const char* label) {
  ImportStats stats;
  const ImportResult result = importLoop(loop, gImport, &stats);
  if (result != ImportResult::Ok) {
    printf("C3BDBG %s result=%u free=%u owned=%u runs=%u manifests=%u steps=%u rows=%u capture_runs=%u\n", label,
           static_cast<unsigned>(result), gImport.freePageCount(), static_cast<unsigned>(gImport.ownedPageCount()),
           gImport.liveRunCount(), gImport.liveManifestCount(), stats.steps, stats.rows, stats.captureRuns);
  }
  TEST_ASSERT_EQUAL_MESSAGE(ImportResult::Ok, result, label);
  gImportUnpairedOffs += stats.unpairedNoteOffs;
  IndexStep step = IndexStep::Pending;
  while (step == IndexStep::Pending) step = gImport.stepEditIndex(256);
  const ComposeStamp stamp = gImport.composeWindow(gImport.current(), 0, loop.loopLengthTicks, gOut, 8192,
                                                   EffectivePolicy::fullRebuildSnapshotCompat().with(HideLookup::IndexOnly));
  TEST_ASSERT_EQUAL_MESSAGE(ComposeStatus::Complete, stamp.status, label);
  SessionMidiEventVec production;
  TEST_ASSERT_TRUE_MESSAGE(loop.tryCopyCurrentSnapshotEditViewEvents(production), label);
  std::vector<Row> a, b;
  for (const MidiEvent& e : production) a.push_back(rowOf(e));
  for (uint32_t i = 0; i < stamp.count; ++i) b.push_back(rowOf(gOut[i]));
  TEST_ASSERT_TRUE_MESSAGE(normalise(a), label);
  TEST_ASSERT_TRUE_MESSAGE(normalise(b), label);
  char msg[160];
  snprintf(msg, sizeof(msg), "%s: import rows %u vs production %u", label, static_cast<unsigned>(b.size()),
           static_cast<unsigned>(a.size()));
  if (a.size() != b.size()) {
    std::vector<Row> sa = a, sb = b, onlyProd, onlyImport;
    std::sort(sa.begin(), sa.end());
    std::sort(sb.begin(), sb.end());
    std::set_difference(sa.begin(), sa.end(), sb.begin(), sb.end(), std::back_inserter(onlyProd));
    std::set_difference(sb.begin(), sb.end(), sa.begin(), sa.end(), std::back_inserter(onlyImport));
    for (const Row& r : onlyProd) printf("C1DIFF %s production-only t=%u s=%02X d=%u/%u\n", label, r.tick, r.status, r.data1, r.data2);
    for (const Row& r : onlyImport) printf("C1DIFF %s import-only t=%u s=%02X d=%u/%u\n", label, r.tick, r.status, r.data1, r.data2);
    for (const EditPass& e : loop.passes.editPasses) {
      printf("C1DIFF %s edit id=%u state=%d action=%d prop=%d target=%u start=%u end=%u pitch=%u\n", label,
             static_cast<unsigned>(e.id), static_cast<int>(e.state), static_cast<int>(e.actionType),
             static_cast<int>(e.propertyType), static_cast<unsigned>(e.targetNoteId), e.startTick, e.endTick, e.pitch);
    }
  }
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(a.size(), b.size(), msg);
  for (size_t i = 0; i < a.size(); ++i) {
    if (!(a[i] == b[i])) {
      snprintf(msg, sizeof(msg), "%s: import row %u t=%u s=%02X d=%u/%u vs production t=%u s=%02X d=%u/%u", label,
               static_cast<unsigned>(i), b[i].tick, b[i].status, b[i].data1, b[i].data2, a[i].tick, a[i].status,
               a[i].data1, a[i].data2);
      TEST_FAIL_MESSAGE(msg);
    }
  }
  ++gImportChecks;
}

// Returns the number of groups compared; fails the test on the first divergence, naming it.
void assertParity(const Loop& loop, const char* label, EffectivePolicy policy = EffectivePolicy::revision()) {
  SessionMidiEventVec production;
  TEST_ASSERT_TRUE(loop.tryCopyCurrentSnapshotEditViewEvents(production));
  std::vector<Row> a;
  for (const MidiEvent& e : production) a.push_back(rowOf(e));
  const ComposeStamp stamp = gStore.composeWindow(gStore.current(), 0, kLength, gOut, 8192, policy);
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
  assertImportParity(loop, label);
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

// ---- B.2b: seeded random sequences under DEC-244 ---------------------------------------------------------------------
//
// Expected order (DEC-244): tick, equal-tick class, pass commit order, committed row order. The store must equal it
// exactly; production must equal it as a multiset per (tick, class) group, and its within-group reorderings are counted
// as production conformance gaps (reported, not failed).

struct ModelPass {
  PassId id = kInvalidPassId;
  std::vector<PassEvent> rows;  // committed row order
  bool active = true;
};

struct Sequence {
  Loop loop;
  std::vector<ModelPass> passes;   // commit order; passes[0] is Record
  std::vector<size_t> redoStack;   // indices of passes disabled by undo, most recent last
  uint32_t lcg = 1;
  uint32_t gapsProductionOrder = 0;
  uint32_t groupsCompared = 0;
  uint32_t collapsedRows = 0;
  uint32_t revisionsWithCollapse = 0;
  uint32_t gapsByClass[3] = {0, 0, 0};
  uint32_t audibleCcGaps = 0;
  uint32_t next() {
    lcg = lcg * 1664525u + 1013904223u;
    return lcg >> 8;
  }
};

// Production's cross-pass collapse (`midiEventMatchesSnapshot`): a row from a later pass is not added when an earlier
// active pass already holds a match -- same tick, type and channel; CCs also match number and value; NoteOn/NoteOff also
// match pitch, and a NoteOn with a noteId on both sides never matches (the store's rows carry ids on NoteOn only).
bool productionCollapses(const PassEvent& existing, const PassEvent& incoming) {
  if (existing.tick != incoming.tick || (existing.status & 0xF0) != (incoming.status & 0xF0) ||
      (existing.status & 0x0F) != (incoming.status & 0x0F)) {
    return false;
  }
  const uint8_t type = incoming.status & 0xF0;
  if (type == 0xB0) return existing.data1 == incoming.data1 && existing.data2 == incoming.data2;
  if (type == 0x80 || type == 0x90) {
    if (existing.data1 != incoming.data1) return false;
    if (existing.noteId != 0 && incoming.noteId != 0 && existing.noteId != incoming.noteId) return false;
  }
  return true;
}

uint32_t gFirstAudiblePrinted = 0;

std::vector<Row> expectedRows(const Sequence& q, bool productionCollapse = false, uint32_t* collapsed = nullptr) {
  struct K {
    uint32_t tick;
    uint8_t cls;
    size_t pass;
    size_t row;
    Row r;
  };
  std::vector<K> ks;
  std::vector<PassEvent> earlier;
  for (size_t p = 0; p < q.passes.size(); ++p) {
    if (!q.passes[p].active) continue;
    std::vector<PassEvent> kept;
    for (size_t i = 0; i < q.passes[p].rows.size(); ++i) {
      const PassEvent& row = q.passes[p].rows[i];
      if (productionCollapse) {
        bool hit = false;
        for (const PassEvent& e : earlier) hit = hit || productionCollapses(e, row);
        if (hit) {
          if (collapsed != nullptr) ++*collapsed;
          continue;
        }
      }
      kept.push_back(row);
      const Row r = rowOf(row);
      ks.push_back({r.tick, r.cls, p, i, r});
    }
    earlier.insert(earlier.end(), kept.begin(), kept.end());
  }
  std::sort(ks.begin(), ks.end(), [](const K& a, const K& b) {
    return std::tie(a.tick, a.cls, a.pass, a.row) < std::tie(b.tick, b.cls, b.pass, b.row);
  });
  std::vector<Row> out;
  for (const K& k : ks) out.push_back(k.r);
  return out;
}

// One pass of random content: distinct pitches inside the pass (no same-pitch overlap, so no span-less notes), notes
// of at least 24 ticks that may cross the wrap, and CCs on a 24-tick grid so equal-tick collisions are frequent.
std::vector<Op> randomPassOps(Sequence& q, uint8_t channel) {
  std::vector<Op> ops;
  const uint32_t grid = 24;
  const uint32_t slots = kLength / grid;
  const uint32_t notes = 2 + q.next() % 6;
  uint8_t pitch = static_cast<uint8_t>(36 + q.next() % 24);
  for (uint32_t n = 0; n < notes; ++n, pitch = static_cast<uint8_t>(pitch + 1 + q.next() % 3)) {
    const uint32_t on = (q.next() % slots) * grid;
    const uint32_t off = (on + grid * (1 + q.next() % 16)) % kLength;
    ops.push_back({on, 0x90, channel, pitch, static_cast<uint8_t>(40 + q.next() % 80)});
    ops.push_back({off, 0x80, channel, pitch, 0});
  }
  const uint32_t ccs = q.next() % 4;
  for (uint32_t c = 0; c < ccs; ++c) {
    const uint32_t t = (q.next() % slots) * grid;
    ops.push_back({t, 0xB0, channel, 64, 127});
    if (q.next() % 2) ops.push_back({t, 0xB0, channel, 64, 0});  // same-tick sustain 127 then 0
  }
  return ops;
}

void commitRandomPass(Sequence& q, bool record) {
  Loop& loop = q.loop;
  const std::vector<Op> ops = randomPassOps(q, static_cast<uint8_t>(1 + q.passes.size() % 3));
  loop.beginCapture(record ? CapturePhase::Record : CapturePhase::Overdub);
  uint32_t lastTick = 0;
  for (const Op& op : ops) {
    TEST_ASSERT_TRUE(loop.appendCaptureEvent(toMidi(op)));
    lastTick = std::max(lastTick, op.tick);
  }
  TEST_ASSERT_EQUAL(CommitResult::Committed,
                    loop.commitCapturePass(record ? CommitReason::RecordStop : CommitReason::OverdubStop, lastTick + 1));
  const PassId passId = record ? loop.passes.recordPass.id : loop.passes.overdubPasses.back().id;
  const CommittedChunkIdList& committed =
      record ? loop.passes.recordPass.committedChunkIds : loop.passes.overdubPasses.back().committedChunkIds;
  const CommittedChunkIdList* lists[] = {&committed};
  SessionMidiEventVec rows;
  CommittedEventRange::full(lists, 1, kLength).appendTo(rows);
  ModelPass model;
  model.id = passId;
  TEST_ASSERT_EQUAL(AdmitResult::Ok, gStore.beginPass(record ? PassKind::Record : PassKind::Overdub, kLength));
  for (const MidiEvent& e : rows) {
    PassEvent p;
    p.tick = e.tick;
    p.status = statusOf(e);
    p.data1 = e.data.noteData.note;
    p.data2 = e.data.noteData.velocity;
    p.noteId = e.isNoteOn() ? e.noteId : 0;
    model.rows.push_back(p);
    TEST_ASSERT_EQUAL(AppendResult::Ok, gStore.appendMidi(p.tick, p.status, p.data1, p.data2, e.noteId));
  }
  gStore.commitPass();
  // Commit after undo discards the redo branch in both models (production's passes stay Disabled).
  q.redoStack.clear();
  q.passes.push_back(model);
}

void republish(Loop& loop) { finishPublication(loop); }

void compareRevision(Sequence& q, uint32_t seed, uint32_t step) {
  char where[96];
  snprintf(where, sizeof(where), "seed %u step %u", seed, step);
  const std::vector<Row> expected = expectedRows(q);
  // Store: exact.
  const ComposeStamp stamp = gStore.composeWindow(gStore.current(), 0, kLength, gOut, 8192);
  TEST_ASSERT_EQUAL_MESSAGE(ComposeStatus::Complete, stamp.status, where);
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(expected.size(), stamp.count, where);
  for (uint32_t i = 0; i < stamp.count; ++i) {
    if (!(rowOf(gOut[i]) == expected[i])) {
      char msg[200];
      snprintf(msg, sizeof(msg), "%s: store row %u differs from DEC-244 order", where, i);
      TEST_FAIL_MESSAGE(msg);
    }
  }
  // Production: multiset per (tick, class) group against DEC-244 minus production's cross-pass collapse (a named
  // divergence, counted); count within-group order gaps.
  uint32_t collapsedRows = 0;
  const std::vector<Row> expectedProd = expectedRows(q, true, &collapsedRows);
  q.collapsedRows += collapsedRows;
  if (collapsedRows != 0) ++q.revisionsWithCollapse;
  SessionMidiEventVec production;
  TEST_ASSERT_TRUE_MESSAGE(q.loop.tryCopyCurrentSnapshotEditViewEvents(production), where);
  std::vector<Row> prod;
  for (const MidiEvent& e : production) prod.push_back(rowOf(e));
  if (expectedProd.size() != prod.size()) {
    std::vector<Row> e = expectedProd, pr = prod;
    std::sort(e.begin(), e.end());
    std::sort(pr.begin(), pr.end());
    std::vector<Row> missing, extra;
    std::set_difference(e.begin(), e.end(), pr.begin(), pr.end(), std::back_inserter(missing));
    std::set_difference(pr.begin(), pr.end(), e.begin(), e.end(), std::back_inserter(extra));
    for (const Row& r : missing) printf("DIFF %s missing-in-production t=%u s=%02X d=%u/%u\n", where, r.tick, r.status, r.data1, r.data2);
    for (const Row& r : extra) printf("DIFF %s extra-in-production t=%u s=%02X d=%u/%u\n", where, r.tick, r.status, r.data1, r.data2);
    for (const Row& r : expectedProd) printf("DIFF %s expectedProd t=%u s=%02X d=%u/%u\n", where, r.tick, r.status, r.data1, r.data2);
  }
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(expectedProd.size(), prod.size(), where);
  std::vector<Row> prodSorted = prod;
  std::vector<Row> expSorted = expectedProd;
  TEST_ASSERT_TRUE_MESSAGE(normalise(prodSorted), where);
  TEST_ASSERT_TRUE_MESSAGE(normalise(expSorted), where);
  for (size_t i = 0; i < expSorted.size(); ++i) {
    if (!(prodSorted[i] == expSorted[i])) {
      char msg[200];
      snprintf(msg, sizeof(msg), "%s: production content differs at row %u (t=%u s=%02X vs t=%u s=%02X)", where,
               static_cast<unsigned>(i), prodSorted[i].tick, prodSorted[i].status, expSorted[i].tick,
               expSorted[i].status);
      TEST_FAIL_MESSAGE(msg);
    }
  }
  {
    // DEC-245: the store's explicit production projection reproduces production's effective content directly.
    const ComposeStamp compat =
        gStore.composeWindow(gStore.current(), 0, kLength, gOut, 8192, EffectivePolicy::fullRebuildSnapshotCompat());
    std::vector<Row> compatRows;
    for (uint32_t i = 0; i < compat.count; ++i) compatRows.push_back(rowOf(gOut[i]));
    TEST_ASSERT_TRUE_MESSAGE(normalise(compatRows), where);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(prodSorted.size(), compatRows.size(), where);
    for (size_t i = 0; i < compatRows.size(); ++i) {
      TEST_ASSERT_TRUE_MESSAGE(compatRows[i] == prodSorted[i], where);
    }
  }
  assertImportParity(q.loop, where);
  for (size_t i = 0; i < expectedProd.size();) {
    size_t j = i + 1;
    while (j < expectedProd.size() && expectedProd[j].tick == expectedProd[i].tick && expectedProd[j].cls == expectedProd[i].cls) ++j;
    bool same = true;
    for (size_t k = i; k < j; ++k) same = same && prod[k] == expectedProd[k];
    ++q.groupsCompared;
    if (!same) {
      ++q.gapsProductionOrder;
      ++q.gapsByClass[expectedProd[i].cls];
      // Audible: the same controller on the same channel ends on a different value (last write wins).
      if (expectedProd[i].cls == 2) {
        bool flipped = false;
        for (size_t k = i; k < j && !flipped; ++k) {
          uint8_t lastExpected = 0, lastProduction = 0;
          for (size_t m = i; m < j; ++m) {
            if (expectedProd[m].status == expectedProd[k].status && expectedProd[m].data1 == expectedProd[k].data1) {
              lastExpected = expectedProd[m].data2;
            }
            if (prod[m].status == expectedProd[k].status && prod[m].data1 == expectedProd[k].data1) {
              lastProduction = prod[m].data2;
            }
          }
          flipped = lastExpected != lastProduction;
        }
        if (flipped) {
          ++q.audibleCcGaps;
          if (q.audibleCcGaps == 1 && gFirstAudiblePrinted++ == 0) {
            printf("B2B first_audible seed=%u step=%u tick=%u\n", seed, step, expectedProd[i].tick);
            for (size_t m = i; m < j; ++m) {
              printf("B2B   expected %02X/%u/%u   production %02X/%u/%u\n", expectedProd[m].status, expectedProd[m].data1,
                     expectedProd[m].data2, prod[m].status, prod[m].data1, prod[m].data2);
            }
          }
        }
      }
    }
    i = j;
  }
}

uint32_t gTotalGroups = 0;
uint32_t gTotalGaps = 0;
uint32_t gTotalRevisions = 0;
uint32_t gTotalUndos = 0;
uint32_t gTotalRedos = 0;
uint32_t gTotalCollapsedRows = 0;
uint32_t gTotalGapsByClass[3] = {0, 0, 0};
uint32_t gTotalAudibleCcGaps = 0;
uint32_t gTotalRevisionsWithCollapse = 0;

void runSeed(uint32_t seed, uint32_t steps) {
  Sequence q;
  q.lcg = seed;
  resetAll(q.loop);
  commitRandomPass(q, true);
  republish(q.loop);
  compareRevision(q, seed, 0);
  ++gTotalRevisions;
  for (uint32_t step = 1; step <= steps; ++step) {
    const uint32_t roll = q.next() % 100;
    const bool canUndo = q.passes.size() > 1 && q.passes.back().active && gStore.canUndo();
    if (roll < 20 && canUndo) {
      // Undo the newest active overdub: disable it in production, move the store's cursor back.
      size_t idx = q.passes.size();
      while (idx > 1 && !q.passes[idx - 1].active) --idx;
      if (idx <= 1) continue;
      ModelPass& pass = q.passes[idx - 1];
      TEST_ASSERT_TRUE(q.loop.setCapturePassState(pass.id, CapturePassState::Disabled));
      TEST_ASSERT_TRUE(gStore.undo());
      pass.active = false;
      q.redoStack.push_back(idx - 1);
      ++gTotalUndos;
    } else if (roll < 32 && !q.redoStack.empty()) {
      ModelPass& pass = q.passes[q.redoStack.back()];
      q.redoStack.pop_back();
      TEST_ASSERT_TRUE(q.loop.setCapturePassState(pass.id, CapturePassState::Active));
      TEST_ASSERT_TRUE(gStore.redo());
      pass.active = true;
      ++gTotalRedos;
    } else {
      if (!q.redoStack.empty()) {
        // Production keeps the discarded branch Disabled; the model drops it from the redo stack.
        for (size_t idx : q.redoStack) q.passes[idx].active = false;
      }
      commitRandomPass(q, false);
    }
    republish(q.loop);
    compareRevision(q, seed, step);
    ++gTotalRevisions;
  }
  gTotalGroups += q.groupsCompared;
  gTotalGaps += q.gapsProductionOrder;
  gTotalCollapsedRows += q.collapsedRows;
  for (int c = 0; c < 3; ++c) gTotalGapsByClass[c] += q.gapsByClass[c];
  gTotalAudibleCcGaps += q.audibleCcGaps;
  gTotalRevisionsWithCollapse += q.revisionsWithCollapse;
}

void test_seeded_sequences_store_exact_and_production_multiset_under_dec244() {
  for (uint32_t seed = 1; seed <= 40; ++seed) runSeed(seed, 12);
  printf("B2B revisions=%u undos=%u redos=%u groups=%u production_order_gaps=%u collapsed_rows=%u revisions_with_collapse=%u\n",
         gTotalRevisions, gTotalUndos, gTotalRedos, gTotalGroups, gTotalGaps, gTotalCollapsedRows,
         gTotalRevisionsWithCollapse);
  printf("B2B production_order_gaps_by_class noteoff=%u noteon=%u other=%u audible_cc_final_value=%u\n",
         gTotalGapsByClass[0], gTotalGapsByClass[1], gTotalGapsByClass[2], gTotalAudibleCcGaps);
  // Pinned so a change in either producer is noticed; these are production conformance gaps under DEC-244.
  TEST_ASSERT_EQUAL_UINT32(520, gTotalRevisions);
}

// ---- B.3: overlap-resolve results (Hide / Shorten companions) ---------------------------------------------------------
//
// Production decides what an overdub hides or shortens (overlap-resolve, unchanged owner) and, at the overdub stop,
// commits the OverdubPass and seals the session's pending changes into companion edit passes
// (`sealPendingNoteChangesToEditPasses`), which publication applies. The harness injects the pending changes with the
// existing test hook and drives that same sequence. The store represents a Hide as a HideNote row in the overdub's run,
// and a Shorten as a HideNote row plus replacement NoteOn/NoteOff rows on the note's own channel.

struct Change {
  PendingNoteChangeKind kind;
  NoteId noteId;
  uint8_t channel;
  uint8_t pitch;
  uint8_t velocity;
  uint32_t startTick;
  uint32_t endTick;
};

// Committed rows of the newest pass, with each NoteOff paired to its NoteOn's noteId (same pass, channel and pitch).
std::vector<PassEvent> committedPassRows(const Loop& loop, bool record) {
  const CommittedChunkIdList& committed =
      record ? loop.passes.recordPass.committedChunkIds : loop.passes.overdubPasses.back().committedChunkIds;
  const CommittedChunkIdList* lists[] = {&committed};
  SessionMidiEventVec rows;
  CommittedEventRange::full(lists, 1, kLength).appendTo(rows);
  std::vector<PassEvent> out;
  for (const MidiEvent& e : rows) {
    PassEvent p;
    p.tick = e.tick;
    p.status = statusOf(e);
    p.data1 = e.data.noteData.note;
    p.data2 = e.data.noteData.velocity;
    p.noteId = e.isNoteOn() ? e.noteId : 0;
    out.push_back(p);
  }
  // Production's attribution (the importer's LanePairer): tick order, NoteOff before NoteOn at an equal tick, LIFO
  // per lane, then the unique earlier unpaired NoteOff for a still-open NoteOn.
  std::vector<uint32_t> order(out.size());
  for (uint32_t i = 0; i < order.size(); ++i) order[i] = i;
  std::stable_sort(order.begin(), order.end(), [&](uint32_t a, uint32_t b) { return tickClassLess(out[a], out[b]); });
  static LanePairer<256, 512> pairer;
  pairer.reset();
  for (uint32_t i : order) {
    PassEvent& e = out[i];
    const uint8_t channel = static_cast<uint8_t>((e.status & 0x0F) + 1);
    if (equalTickClass(e) == 1) {
      pairer.onNoteOn(channel, e.data1, e.tick, e.noteId);
    } else if (equalTickClass(e) == 0) {
      e.noteId = pairer.onNoteOff(channel, e.data1, e.tick, i);
    }
  }
  while (pairer.wrapStep([&](uint32_t index, uint32_t id) { out[index].noteId = id; })) {
  }
  return out;
}

EditPassIdList gLastCompanionIds;

void commitWithChanges(Loop& loop, bool record, const std::vector<Op>& ops, const std::vector<Change>& changes,
                       std::vector<PassEvent>* committedOut = nullptr) {
  loop.beginCapture(record ? CapturePhase::Record : CapturePhase::Overdub);
  for (const Change& c : changes) {
    PendingNoteChange change{};
    change.kind = c.kind;
    change.noteId = c.noteId;
    change.channel = c.channel;
    change.pitch = c.pitch;
    change.velocity = c.velocity;
    change.startTick = c.startTick;
    change.endTick = c.endTick;
    loop.pushPendingNoteChangeForTest(change);
  }
  uint32_t lastTick = 0;
  for (const Op& op : ops) {
    TEST_ASSERT_TRUE(loop.appendCaptureEvent(toMidi(op)));
    lastTick = std::max(lastTick, op.tick);
  }
  TEST_ASSERT_EQUAL(CommitResult::Committed,
                    loop.commitCapturePass(record ? CommitReason::RecordStop : CommitReason::OverdubStop, lastTick + 1));
  gLastCompanionIds.clear();
  if (!record) gLastCompanionIds = loop.sealPendingNoteChangesToEditPasses(false);
  const std::vector<PassEvent> rows = committedPassRows(loop, record);
  if (committedOut != nullptr) *committedOut = rows;
  TEST_ASSERT_EQUAL(AdmitResult::Ok, gStore.beginPass(record ? PassKind::Record : PassKind::Overdub, kLength));
  for (const Change& c : changes) {
    TEST_ASSERT_EQUAL(AppendResult::Ok, gStore.appendHide(c.noteId));
    if (c.kind == PendingNoteChangeKind::Shorten) {
      gStore.appendMidi(c.startTick, static_cast<uint8_t>(0x90 | (c.channel - 1)), c.pitch, c.velocity, c.noteId);
      gStore.appendMidi(c.endTick, static_cast<uint8_t>(0x80 | (c.channel - 1)), c.pitch, 0, c.noteId);
    }
  }
  for (const PassEvent& p : rows) {
    TEST_ASSERT_EQUAL(AppendResult::Ok, gStore.appendMidi(p.tick, p.status, p.data1, p.data2, p.noteId));
  }
  gStore.commitPass();
  finishPublication(loop);
}

// Count production effective-view rows matching (tick, status, data1).
uint32_t productionCount(const Loop& loop, uint32_t tick, uint8_t status, uint8_t data1) {
  SessionMidiEventVec production;
  TEST_ASSERT_TRUE(loop.tryCopyCurrentSnapshotEditViewEvents(production));
  uint32_t n = 0;
  for (const MidiEvent& e : production) {
    const Row r = rowOf(e);
    if (r.tick == tick && r.status == status && r.data1 == data1) ++n;
  }
  return n;
}

NoteId noteIdAt(const std::vector<PassEvent>& rows, uint8_t pitch) {
  for (const PassEvent& p : rows) {
    if (equalTickClass(p) == 1 && p.data1 == pitch) return p.noteId;
  }
  TEST_FAIL_MESSAGE("note not found in committed rows");
  return kInvalidNoteId;
}

void test_overdub_hide_of_record_note_matches_production() {
  Loop loop;
  resetAll(loop);
  std::vector<PassEvent> rec;
  commitWithChanges(loop, true, {{96, 0x90, 1, 60, 100}, {480, 0x80, 1, 60, 0}, {768, 0x90, 1, 64, 90}, {960, 0x80, 1, 64, 0}},
                    {}, &rec);
  const NoteId hidden = noteIdAt(rec, 60);
  commitWithChanges(loop, false, {{192, 0x90, 1, 60, 110}, {384, 0x80, 1, 60, 0}},
                    {{PendingNoteChangeKind::Hide, hidden, 1, 60, 100, 96, 480}});
  assertParity(loop, "hide record note");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, productionCount(loop, 96, 0x90, 60), "hidden Record NoteOn is gone");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, productionCount(loop, 480, 0x80, 60), "hidden Record NoteOff is gone");
}

void test_overdub_shorten_of_record_note_matches_production() {
  Loop loop;
  resetAll(loop);
  std::vector<PassEvent> rec;
  commitWithChanges(loop, true, {{96, 0x90, 1, 60, 100}, {960, 0x80, 1, 60, 0}, {1536, 0x90, 1, 67, 80}, {1700, 0x80, 1, 67, 0}},
                    {}, &rec);
  const NoteId shortened = noteIdAt(rec, 60);
  commitWithChanges(loop, false, {{480, 0x90, 1, 60, 110}, {700, 0x80, 1, 60, 0}},
                    {{PendingNoteChangeKind::Shorten, shortened, 1, 60, 100, 96, 480}});
  assertParity(loop, "shorten record note");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, productionCount(loop, 480, 0x80, 60), "shortened note ends at 480");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, productionCount(loop, 960, 0x80, 60), "original end 960 is gone");
}

void test_overdub_shorten_of_wrap_crossing_note_matches_production() {
  Loop loop;
  resetAll(loop);
  std::vector<PassEvent> rec;
  commitWithChanges(loop, true, {{kLength - 192, 0x90, 1, 62, 100}, {384, 0x80, 1, 62, 0}}, {}, &rec);
  const NoteId shortened = noteIdAt(rec, 62);
  commitWithChanges(loop, false, {{96, 0x90, 1, 62, 110}, {200, 0x80, 1, 62, 0}},
                    {{PendingNoteChangeKind::Shorten, shortened, 1, 62, 100, kLength - 192, 96}});
  assertParity(loop, "shorten wrap-crossing note");
}

void test_second_overdub_hides_first_overdub_note_matches_production() {
  Loop loop;
  resetAll(loop);
  commitWithChanges(loop, true, {{96, 0x90, 1, 48, 100}, {480, 0x80, 1, 48, 0}}, {});
  std::vector<PassEvent> od1;
  commitWithChanges(loop, false, {{768, 0x90, 1, 72, 90}, {1152, 0x80, 1, 72, 0}}, {}, &od1);
  const NoteId hidden = noteIdAt(od1, 72);
  commitWithChanges(loop, false, {{900, 0x90, 1, 72, 110}, {1000, 0x80, 1, 72, 0}},
                    {{PendingNoteChangeKind::Hide, hidden, 1, 72, 90, 768, 1152}});
  assertParity(loop, "hide first-overdub note");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, productionCount(loop, 768, 0x90, 72), "hidden overdub NoteOn is gone");
}

// The companion event path stamps replacement events with channel 1 (`appendCompanionEventChanges`); a Shorten of a
// channel-3 note shows whether that reaches the effective view.
void test_overdub_shorten_of_channel_three_note_matches_production() {
  Loop loop;
  resetAll(loop);
  std::vector<PassEvent> rec;
  commitWithChanges(loop, true, {{96, 0x90, 3, 60, 100}, {960, 0x80, 3, 60, 0}}, {}, &rec);
  const NoteId shortened = noteIdAt(rec, 60);
  commitWithChanges(loop, false, {{480, 0x90, 3, 60, 110}, {700, 0x80, 3, 60, 0}},
                    {{PendingNoteChangeKind::Shorten, shortened, 3, 60, 100, 96, 480}});
  assertParity(loop, "shorten channel-3 note");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, productionCount(loop, 480, 0x82, 60), "replacement NoteOff stays on channel 3");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, productionCount(loop, 480, 0x80, 60), "no channel-1 replacement");
}

// Production undo of an overdub session (`TrackUndo` applyUndoEntry): disable the capture pass and its companion
// edit passes, bump the playback revision, republish.
void productionUndoOverdub(Loop& loop, PassId passId, const EditPassIdList& companions) {
  TEST_ASSERT_TRUE(loop.setCapturePassState(passId, CapturePassState::Disabled));
  for (EditPass& row : loop.passes.editPasses) {
    for (const EditPassId id : companions) {
      if (row.id == id) row.state = EditPassState::Disabled;
    }
  }
  ++loop.playbackRevision;
  finishPublication(loop);
}

void test_undo_of_overdub_with_hide_restores_the_note_in_both() {
  Loop loop;
  resetAll(loop);
  std::vector<PassEvent> rec;
  commitWithChanges(loop, true, {{96, 0x90, 1, 60, 100}, {480, 0x80, 1, 60, 0}, {768, 0x90, 1, 64, 90}, {960, 0x80, 1, 64, 0}},
                    {}, &rec);
  const NoteId hidden = noteIdAt(rec, 60);
  commitWithChanges(loop, false, {{192, 0x90, 1, 60, 110}, {384, 0x80, 1, 60, 0}},
                    {{PendingNoteChangeKind::Hide, hidden, 1, 60, 100, 96, 480}});
  TEST_ASSERT_EQUAL_UINT32(1, gLastCompanionIds.size());
  productionUndoOverdub(loop, loop.passes.overdubPasses.back().id, gLastCompanionIds);
  TEST_ASSERT_TRUE(gStore.undo());
  assertParity(loop, "undo overdub with hide");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, productionCount(loop, 96, 0x90, 60), "hidden note is back after undo");
}

// ---- B.3: NOTE_EDIT passes ------------------------------------------------------------------------------------------

struct NoteEdit {
  EditActionType action;
  EditPropertyType property;
  NoteId target;
  uint8_t channel;
  uint8_t pitch;      // resulting pitch
  uint8_t velocity;   // resulting velocity
  uint32_t startTick; // resulting span
  uint32_t endTick;
};

EditPassId saveEditBoth(Loop& loop, const NoteEdit& edit) {
  EditPass row{};
  row.passType = EditPassType::Note;
  row.actionType = edit.action;
  row.propertyType = edit.property;
  row.targetNoteId = edit.target;
  row.startTick = edit.startTick;
  row.endTick = edit.endTick;
  row.pitch = edit.pitch;
  row.velocity = edit.velocity;
  const EditPassId id = loop.saveNoteEditPass(0, row);
  TEST_ASSERT_NOT_EQUAL(kInvalidEditPassId, id);
  finishPublication(loop);
  TEST_ASSERT_EQUAL(AdmitResult::Ok, gStore.beginPass(PassKind::Edit));
  gStore.appendHide(edit.target);
  if (edit.action != EditActionType::Delete) {
    gStore.appendMidi(edit.startTick, static_cast<uint8_t>(0x90 | (edit.channel - 1)), edit.pitch, edit.velocity, edit.target);
    gStore.appendMidi(edit.endTick, static_cast<uint8_t>(0x80 | (edit.channel - 1)), edit.pitch, 0, edit.target);
  }
  gStore.commitPass();
  return id;
}

std::vector<PassEvent> recordForEdits(Loop& loop) {
  std::vector<PassEvent> rec;
  commitWithChanges(loop, true, {{96, 0x90, 1, 60, 100}, {480, 0x80, 1, 60, 0}, {768, 0x90, 1, 64, 90}, {960, 0x80, 1, 64, 0}},
                    {}, &rec);
  return rec;
}

void test_note_edit_pitch_matches_production() {
  Loop loop;
  resetAll(loop);
  const auto rec = recordForEdits(loop);
  saveEditBoth(loop, {EditActionType::Update, EditPropertyType::Pitch, noteIdAt(rec, 60), 1, 67, 100, 96, 480});
  assertParity(loop, "edit pitch");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, productionCount(loop, 96, 0x90, 67), "pitch edit applied");
}

void test_note_edit_length_matches_production() {
  Loop loop;
  resetAll(loop);
  const auto rec = recordForEdits(loop);
  saveEditBoth(loop, {EditActionType::Update, EditPropertyType::Length, noteIdAt(rec, 60), 1, 60, 100, 96, 288});
  assertParity(loop, "edit length");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, productionCount(loop, 288, 0x80, 60), "length edit applied");
}

void test_note_edit_velocity_matches_production() {
  Loop loop;
  resetAll(loop);
  const auto rec = recordForEdits(loop);
  saveEditBoth(loop, {EditActionType::Update, EditPropertyType::Velocity, noteIdAt(rec, 64), 1, 64, 40, 768, 960});
  assertParity(loop, "edit velocity");
}

void test_note_edit_delete_matches_production() {
  Loop loop;
  resetAll(loop);
  const auto rec = recordForEdits(loop);
  saveEditBoth(loop, {EditActionType::Delete, EditPropertyType::None, noteIdAt(rec, 64), 1, 64, 90, 768, 960});
  assertParity(loop, "edit delete");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, productionCount(loop, 768, 0x90, 64), "delete applied");
}

void test_undo_of_note_edit_restores_the_note_in_both() {
  Loop loop;
  resetAll(loop);
  const auto rec = recordForEdits(loop);
  const EditPassId id =
      saveEditBoth(loop, {EditActionType::Update, EditPropertyType::Pitch, noteIdAt(rec, 60), 1, 67, 100, 96, 480});
  for (EditPass& row : loop.passes.editPasses) {
    if (row.id == id) row.state = EditPassState::Disabled;
  }
  ++loop.playbackRevision;
  finishPublication(loop);
  TEST_ASSERT_TRUE(gStore.undo());
  assertParity(loop, "undo edit pitch");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, productionCount(loop, 96, 0x90, 60), "original pitch back after undo");
}

// Two edits on one note: the second edit's run hides the first edit's replacement rows (same noteId).
void test_two_edits_on_one_note_match_production() {
  Loop loop;
  resetAll(loop);
  const auto rec = recordForEdits(loop);
  const NoteId target = noteIdAt(rec, 60);
  saveEditBoth(loop, {EditActionType::Update, EditPropertyType::Pitch, target, 1, 67, 100, 96, 480});
  saveEditBoth(loop, {EditActionType::Update, EditPropertyType::Length, target, 1, 67, 100, 96, 240});
  assertParity(loop, "pitch then length");
}

// DEC-245 B.3b: a zero-length note (NoteOn + velocity-0 NoteOn on one tick) stays in the committed source (4 rows);
// production's effective view drops it (`copyEditViewEventsFromSnapshot` keeps only notes with a span). That is
// the store's explicit `fullRebuildSnapshotCompat()` projection, not a storage rule: parity under it, 4 rows in source.
void test_zero_length_note_kept_in_source_and_suppressed_by_production_projection() {
  Loop loop;
  resetAll(loop);
  commitWithChanges(loop, true, {{96, 0x90, 1, 60, 100}, {480, 0x80, 1, 60, 0}, {192, 0x90, 1, 50, 100},
                                 {192, 0x90, 1, 50, 0}},
                    {});
  assertParity(loop, "zero-length, production projection", EffectivePolicy::fullRebuildSnapshotCompat());
  const ComposeStamp source = gStore.composeWindow(gStore.current(), 0, kLength, gOut, 8192, EffectivePolicy::source());
  TEST_ASSERT_EQUAL_UINT32(4, source.count);
}

// ---- B.3 seeded: overdubs with Hide/Shorten, NOTE_EDITs and undo, mixed ---------------------------------------------

struct VisibleNote {
  NoteId id;
  uint8_t channel, pitch, velocity;
  uint32_t start, end;
};

// Visible, non-wrapping notes of the store's current revision (paired by noteId in composed order).
std::vector<VisibleNote> visibleNotes() {
  const ComposeStamp stamp = gStore.composeWindow(gStore.current(), 0, kLength, gOut, 8192);
  std::vector<VisibleNote> notes;
  for (uint32_t i = 0; i < stamp.count; ++i) {
    const PassEvent& on = gOut[i];
    if (equalTickClass(on) != 1 || on.noteId == 0) continue;
    for (uint32_t j = i + 1; j < stamp.count; ++j) {
      const PassEvent& off = gOut[j];
      if (equalTickClass(off) == 0 && off.noteId == on.noteId) {
        if (off.tick >= on.tick + 48) {
          notes.push_back({on.noteId, static_cast<uint8_t>((on.status & 0x0F) + 1), on.data1, on.data2, on.tick, off.tick});
        }
        break;
      }
    }
  }
  return notes;
}

struct UndoStep {
  bool isEdit;
  PassId passId;
  EditPassIdList companions;
  EditPassId editId;
};

void runB3Seed(uint32_t seed, uint32_t steps, uint32_t& revisions, uint32_t& changesApplied, uint32_t& editsApplied,
               uint32_t& undos) {
  Sequence q;
  q.lcg = seed;
  resetAll(q.loop);
  Loop& loop = q.loop;
  std::vector<UndoStep> undoStack;
  {
    std::vector<Op> ops;
    for (uint8_t n = 0; n < 8; ++n) {
      const uint32_t on = (q.next() % (kLength / 96 - 4)) * 96;
      ops.push_back({on, 0x90, 1, static_cast<uint8_t>(48 + n * 2), static_cast<uint8_t>(60 + n)});
      ops.push_back({on + 96 + (q.next() % 4) * 48, 0x80, 1, static_cast<uint8_t>(48 + n * 2), 0});
    }
    commitWithChanges(loop, true, ops, {});
  }
  ++revisions;
  char where[64];
  for (uint32_t step = 1; step <= steps; ++step) {
    snprintf(where, sizeof(where), "b3 seed %u step %u", seed, step);
    const uint32_t roll = q.next() % 100;
    const std::vector<VisibleNote> visible = visibleNotes();
    if (roll < 15 && !undoStack.empty()) {
      const UndoStep undo = undoStack.back();
      undoStack.pop_back();
      if (undo.isEdit) {
        for (EditPass& row : loop.passes.editPasses) {
          if (row.id == undo.editId) row.state = EditPassState::Disabled;
        }
        ++loop.playbackRevision;
        finishPublication(loop);
      } else {
        productionUndoOverdub(loop, undo.passId, undo.companions);
      }
      TEST_ASSERT_TRUE_MESSAGE(gStore.undo(), where);
      ++undos;
    } else if (roll < 50 && !visible.empty()) {
      const VisibleNote& n = visible[q.next() % visible.size()];
      const uint32_t kind = q.next() % 4;
      NoteEdit edit{EditActionType::Update, EditPropertyType::Pitch, n.id, n.channel, n.pitch, n.velocity, n.start, n.end};
      if (kind == 0) edit.pitch = static_cast<uint8_t>(n.pitch + 1 + q.next() % 5);
      if (kind == 1) {
        edit.property = EditPropertyType::Length;
        edit.endTick = n.start + 24 + (q.next() % ((n.end - n.start) / 24)) * 24;
      }
      if (kind == 2) {
        edit.property = EditPropertyType::Velocity;
        edit.velocity = static_cast<uint8_t>(20 + q.next() % 100);
      }
      if (kind == 3) {
        edit.action = EditActionType::Delete;
        edit.property = EditPropertyType::None;
      }
      UndoStep u{true, kInvalidPassId, {}, saveEditBoth(loop, edit)};
      undoStack.push_back(u);
      ++editsApplied;
    } else {
      // Overdub on channel 2 with up to two overlap-resolve results on visible notes.
      std::vector<Change> changes;
      std::vector<NoteId> used;
      for (uint32_t c = 0, count = visible.empty() ? 0 : q.next() % 3; c < count; ++c) {
        const VisibleNote& n = visible[q.next() % visible.size()];
        if (std::find(used.begin(), used.end(), n.id) != used.end()) continue;
        used.push_back(n.id);
        if (q.next() % 2) {
          changes.push_back({PendingNoteChangeKind::Hide, n.id, n.channel, n.pitch, n.velocity, n.start, n.end});
        } else {
          const uint32_t newEnd = n.start + 24 + (q.next() % ((n.end - n.start) / 24)) * 24;
          changes.push_back({PendingNoteChangeKind::Shorten, n.id, n.channel, n.pitch, n.velocity, n.start, newEnd});
        }
      }
      std::vector<Op> ops;
      for (uint8_t k = 0; k < 2; ++k) {
        const uint32_t on = (q.next() % (kLength / 96 - 4)) * 96;
        const uint8_t pitch = static_cast<uint8_t>(80 + step * 2 + k);
        ops.push_back({on, 0x90, 2, pitch, 90});
        ops.push_back({on + 96, 0x80, 2, pitch, 0});
      }
      commitWithChanges(loop, false, ops, changes);
      undoStack.push_back({false, loop.passes.overdubPasses.back().id, gLastCompanionIds, kInvalidEditPassId});
      changesApplied += static_cast<uint32_t>(changes.size());
    }
    assertParity(loop, where);
    ++revisions;
  }
}

void test_seeded_overlap_edit_and_undo_match_production() {
  uint32_t revisions = 0, changes = 0, edits = 0, undos = 0;
  for (uint32_t seed = 1; seed <= 12; ++seed) runB3Seed(seed, 10, revisions, changes, edits, undos);
  printf("B3 revisions=%u overlap_changes=%u note_edits=%u undos=%u\n", revisions, changes, edits, undos);
  TEST_ASSERT_TRUE(changes > 10 && edits > 10 && undos > 5);
}

// C1 refusals: a slot that does not fit the Stage-C pool, or an edit kind the import does not express, is a named
// refusal and leaves the store empty (no partial mirror; the production reader stays authoritative).
void test_import_refusals_are_named_and_leave_no_partial_mirror() {
  Loop loop;
  resetAll(loop);
  std::vector<Op> ops;
  for (uint8_t n = 0; n < 60; ++n) {
    ops.push_back({static_cast<uint32_t>(n) * 96u, 0x90, 1, static_cast<uint8_t>(30 + n), 100});
    ops.push_back({static_cast<uint32_t>(n) * 96u + 48u, 0x80, 1, static_cast<uint8_t>(30 + n), 0});
  }
  commitWithChanges(loop, true, ops, {});
  using TinyImportCap = Capacity</*pages*/ 3, 32, 8, 8, 8, 8, 4>;  // 96 rows: the 120-row take does not fit
  static Store<TinyImportCap> tiny;
  tiny.reset();
  TEST_ASSERT_EQUAL(ImportResult::DeniedCapacity, importLoop(loop, tiny));
  TEST_ASSERT_EQUAL_UINT32(0, tiny.ownedPageCount());
  TEST_ASSERT_EQUAL_UINT16(kNoManifest, tiny.current());
  // A ControlChange edit pass is refused before anything is written.
  EditPass cc{};
  cc.passType = EditPassType::ControlChange;
  cc.actionType = EditActionType::Update;
  cc.propertyType = EditPropertyType::Value;
  TEST_ASSERT_NOT_EQUAL(kInvalidEditPassId, loop.saveNoteEditPass(0, cc, EditPassType::ControlChange));
  const ManifestId publishedBefore = gImport.current();
  const uint32_t pagesBefore = gImport.ownedPageCount();
  TEST_ASSERT_EQUAL(ImportResult::UnsupportedEditPass, importLoop(loop, gImport));
  TEST_ASSERT_EQUAL_UINT16(publishedBefore, gImport.current());  // the published manifest is untouched
  TEST_ASSERT_EQUAL_UINT32(pagesBefore, gImport.ownedPageCount());
}

// ---- C1 follow-up: the production collapse depends on row provenance -------------------------------------------------
// Matrix measured on the production path (the projection must reproduce it, and compaction must not erase the kind):
//   capture + capture identical  -> collapse
//   capture + edit replacement   -> both kept
//   edit + edit replacements     -> measured here, not inferred
uint32_t productionCountOn(const Loop& loop, uint32_t tick, uint8_t status, uint8_t pitch) {
  SessionMidiEventVec production;
  TEST_ASSERT_TRUE(loop.tryCopyCurrentSnapshotEditViewEvents(production));
  uint32_t n = 0;
  for (const MidiEvent& e : production) {
    const Row r = rowOf(e);
    n += (r.tick == tick && r.status == status && r.data1 == pitch) ? 1 : 0;
  }
  return n;
}

void test_collapse_matrix_capture_capture_collapses() {
  Loop loop;
  resetAll(loop);
  commitWithChanges(loop, true, {{96, 0xB0, 1, 74, 100}, {480, 0x90, 1, 50, 100}, {600, 0x80, 1, 50, 0}}, {});
  commitWithChanges(loop, false, {{96, 0xB0, 1, 74, 100}, {700, 0x90, 1, 52, 100}, {800, 0x80, 1, 52, 0}}, {});
  TEST_ASSERT_EQUAL_UINT32(1, productionCountOn(loop, 96, 0xB0, 74));
  assertParity(loop, "matrix capture+capture", EffectivePolicy::fullRebuildSnapshotCompat());
}

void test_collapse_matrix_capture_and_edit_replacement_both_kept() {
  Loop loop;
  resetAll(loop);
  std::vector<PassEvent> rec;
  commitWithChanges(loop, true, {{96, 0x90, 1, 60, 100}, {480, 0x80, 1, 60, 0}, {96, 0x90, 1, 62, 100},
                                 {480, 0x80, 1, 62, 0}},
                    {}, &rec);
  // Edit note 62 to pitch 60: its replacement is identical to the captured note 60 (on and off rows).
  saveEditBoth(loop, {EditActionType::Update, EditPropertyType::Pitch, noteIdAt(rec, 62), 1, 60, 100, 96, 480});
  TEST_ASSERT_EQUAL_UINT32(2, productionCountOn(loop, 96, 0x90, 60));
  assertParity(loop, "matrix capture+edit", EffectivePolicy::fullRebuildSnapshotCompat());
}

void test_collapse_matrix_edit_and_edit_replacements_measured() {
  Loop loop;
  resetAll(loop);
  std::vector<PassEvent> rec;
  commitWithChanges(loop, true, {{96, 0x90, 1, 60, 100}, {480, 0x80, 1, 60, 0}, {96, 0x90, 1, 62, 100},
                                 {480, 0x80, 1, 62, 0}},
                    {}, &rec);
  // Two edits, two different notes, identical replacements (pitch 64, same span, channel and velocity).
  saveEditBoth(loop, {EditActionType::Update, EditPropertyType::Pitch, noteIdAt(rec, 60), 1, 64, 100, 96, 480});
  saveEditBoth(loop, {EditActionType::Update, EditPropertyType::Pitch, noteIdAt(rec, 62), 1, 64, 100, 96, 480});
  const uint32_t ons = productionCountOn(loop, 96, 0x90, 64);
  const uint32_t offs = productionCountOn(loop, 480, 0x80, 64);
  printf("C1MATRIX edit+edit identical replacements: production NoteOn=%u NoteOff=%u\n", ons, offs);
  TEST_ASSERT_EQUAL_UINT32(2, ons);  // named from the production measurement (see the C1 evidence)
  TEST_ASSERT_EQUAL_UINT32(2, offs);
  assertParity(loop, "matrix edit+edit", EffectivePolicy::fullRebuildSnapshotCompat());
}

// ---- C3b: the bounded, transactional import ------------------------------------------------------------------------

static ImportJob<ImportCap> gJob;

void buildImportSource(Loop& loop) {
  resetAll(loop);
  std::vector<Op> rec;
  for (uint8_t n = 0; n < 40; ++n) {
    rec.push_back({static_cast<uint32_t>(n) * 96u, 0x90, 1, static_cast<uint8_t>(40 + n), 100});
    rec.push_back({static_cast<uint32_t>(n) * 96u + 48u, 0x80, 1, static_cast<uint8_t>(40 + n), 0});
  }
  commitWithChanges(loop, true, rec, {});
  commitWithChanges(loop, false, {{100, 0x90, 2, 70, 90}, {300, 0x80, 2, 70, 0}}, {});
}

// The candidate is private: readers keep the previously published manifest until the atomic publish.
void test_import_candidate_is_invisible_until_published() {
  Loop loop;
  buildImportSource(loop);
  gImport.reset();
  TEST_ASSERT_EQUAL(ImportResult::Ok, importLoop(loop, gImport));  // a published import, M_old
  const ManifestId before = gImport.current();
  const uint32_t identityBefore = gImport.identityOf(before);
  commitWithChanges(loop, false, {{500, 0x90, 3, 72, 90}, {700, 0x80, 3, 72, 0}}, {});  // production moves on
  TEST_ASSERT_EQUAL(ImportStep::Pending, gJob.begin(loop, gImport));
  uint32_t steps = 0;
  ImportStep step = ImportStep::Pending;
  while (step == ImportStep::Pending) {
    TEST_ASSERT_EQUAL_UINT16(before, gImport.current());  // nothing visible yet
    TEST_ASSERT_EQUAL_UINT32(identityBefore, gImport.identityOf(gImport.current()));
    step = gJob.step(16);
    ++steps;
  }
  TEST_ASSERT_EQUAL(ImportStep::Published, step);
  TEST_ASSERT_NOT_EQUAL(before, gImport.current());
  TEST_ASSERT_TRUE(steps >= gJob.stats().rows / 16);  // bounded: at most 16 rows per step
  assertImportParity(loop, "C3b candidate published");
}

// A production change mid-build cancels the candidate: SourceMoved, pages returned, published manifest untouched.
void test_import_source_moving_mid_build_cancels_the_candidate() {
  Loop loop;
  buildImportSource(loop);
  gImport.reset();
  TEST_ASSERT_EQUAL(ImportResult::Ok, importLoop(loop, gImport));
  const ManifestId published = gImport.current();
  const uint32_t pagesPublished = gImport.ownedPageCount();
  TEST_ASSERT_EQUAL(ImportStep::Pending, gJob.begin(loop, gImport));
  TEST_ASSERT_EQUAL(ImportStep::Pending, gJob.step(16));
  TEST_ASSERT_TRUE(gImport.ownedPageCount() > pagesPublished);  // the candidate holds pages
  commitWithChanges(loop, false, {{900, 0x90, 4, 74, 90}, {1000, 0x80, 4, 74, 0}}, {});  // key moves
  TEST_ASSERT_EQUAL(ImportStep::Refused, gJob.step(16));
  TEST_ASSERT_EQUAL(ImportResult::SourceMoved, gJob.result());
  TEST_ASSERT_EQUAL_UINT16(published, gImport.current());
  TEST_ASSERT_EQUAL_UINT32(pagesPublished, gImport.ownedPageCount());
  TEST_ASSERT_EQUAL_UINT8(0, gImport.stagedRunCount());
}

// Device crash 2026-11-01 (null read in stepCaptures during overdubs): production may rebuild its pass vectors with the
// same content, so the source key does not move but the storage does. The job keeps ids, never pointers, across steps:
// a rebuilt vector mid-import gives exactly the content of a fresh import.
void test_import_survives_pass_storage_rebuilt_with_the_same_content() {
  Loop loop;
  buildImportSource(loop);
  commitWithChanges(loop, false, {{700, 0x90, 3, 60, 90}, {800, 0x80, 3, 60, 0}}, {});
  gImport.reset();
  TEST_ASSERT_EQUAL(ImportStep::Pending, gJob.begin(loop, gImport));
  TEST_ASSERT_EQUAL(ImportStep::Pending, gJob.step(16));
  {
    // Same content, new storage; the old storage is poisoned before it is released.
    auto overdubs = loop.passes.overdubPasses;
    loop.passes.overdubPasses.swap(overdubs);
    for (OverdubPass& old : overdubs) {
      old.committedChunkIds.clear();
      old.id = 0;
    }
  }
  ImportStep step = ImportStep::Pending;
  for (uint32_t i = 0; i < 10000 && step == ImportStep::Pending; ++i) step = gJob.step(16);
  TEST_ASSERT_EQUAL(ImportStep::Published, step);
  const ComposeStamp a = gImport.composeWindow(gImport.current(), 0, loop.loopLengthTicks, gOut, 8192,
                                               EffectivePolicy::source());
  std::vector<PassEvent> rebuilt(gOut, gOut + a.count);
  gImport.reset();
  TEST_ASSERT_EQUAL(ImportResult::Ok, importLoop(loop, gImport));
  const ComposeStamp b = gImport.composeWindow(gImport.current(), 0, loop.loopLengthTicks, gOut, 8192,
                                               EffectivePolicy::source());
  TEST_ASSERT_EQUAL_UINT32(b.count, a.count);
  for (uint32_t i = 0; i < a.count; ++i) {
    TEST_ASSERT_EQUAL_UINT32(gOut[i].tick, rebuilt[i].tick);
    TEST_ASSERT_EQUAL_UINT8(gOut[i].status, rebuilt[i].status);
    TEST_ASSERT_EQUAL_UINT8(gOut[i].data1, rebuilt[i].data1);
    TEST_ASSERT_EQUAL_UINT32(gOut[i].noteId, rebuilt[i].noteId);
  }
}

// Refusals happen before anything is written: capacity (atomic, conservative) and no committed source.
void test_import_refusals_happen_before_anything_is_written() {
  Loop loop;
  buildImportSource(loop);
  using SmallCap = Capacity</*pages*/ 4, 32, 8, 8, 8, 8, 2>;
  static Store<SmallCap> small;
  static ImportJob<SmallCap> smallJob;
  small.reset();
  TEST_ASSERT_EQUAL(ImportStep::Refused, smallJob.begin(loop, small));
  TEST_ASSERT_EQUAL(ImportResult::DeniedCapacity, smallJob.result());
  TEST_ASSERT_EQUAL_UINT32(0, small.ownedPageCount());
  Loop empty;
  resetAll(empty);
  TEST_ASSERT_EQUAL(ImportStep::Refused, gJob.begin(empty, gImport));
  TEST_ASSERT_EQUAL(ImportResult::NoCommittedSource, gJob.result());
}

// ---- C4a: NoteOff attribution must follow production's tick-ordered lane walk (EditApply findNoteOffForOnIndex) ----
// Device (2026-11-01, same-pitch 16ths overdubbed onto themselves): two NoteOffs of hidden notes were kept by the shadow.
// Append-order LIFO and production's tick walk diverge when same-pitch notes overlap across the loop wrap.

void test_wrap_overlapping_same_pitch_notes_are_attributed_like_production() {
  Loop loop;
  resetAll(loop);
  std::vector<PassEvent> rec;
  // Capture order: A on @1400, B on @1450 (overlap), the loop wraps, A's and B's offs at 20 and 60; then C, D.
  commitWithChanges(loop, true,
                    {{100, 0x90, 1, 50, 100}, {300, 0x80, 1, 50, 0}, {1400, 0x90, 1, 79, 100}, {1450, 0x90, 1, 79, 90},
                     {20, 0x80, 1, 79, 0}, {60, 0x80, 1, 79, 0}, {500, 0x90, 1, 72, 100}, {600, 0x80, 1, 72, 0}},
                    {}, &rec);
  NoteId a = kInvalidNoteId;
  for (const PassEvent& p : rec) {
    if (equalTickClass(p) == 1 && p.data1 == 79 && p.tick == 1400) a = p.noteId;
  }
  TEST_ASSERT_NOT_EQUAL(kInvalidNoteId, a);
  // An overdub hides A (as an overlap companion would).
  commitWithChanges(loop, false, {{900, 0x90, 2, 40, 100}, {950, 0x80, 2, 40, 0}},
                    {{PendingNoteChangeKind::Hide, a, 1, 79, 100, 1400, 20}});
  assertParity(loop, "wrap-overlap hide");  // includes the import-parity check against the passes view
}

int main(int /*argc*/, char** /*argv*/) {
  UNITY_BEGIN();
  RUN_TEST(test_record_and_overdubs_without_overlap_match_production);
  RUN_TEST(test_wrap_crossing_record_note_matches_production);
  RUN_TEST(test_same_tick_cc_and_cross_pass_overlap_match_production);
  RUN_TEST(test_seeded_sequences_store_exact_and_production_multiset_under_dec244);
  RUN_TEST(test_overdub_hide_of_record_note_matches_production);
  RUN_TEST(test_overdub_shorten_of_record_note_matches_production);
  RUN_TEST(test_overdub_shorten_of_wrap_crossing_note_matches_production);
  RUN_TEST(test_second_overdub_hides_first_overdub_note_matches_production);
  RUN_TEST(test_overdub_shorten_of_channel_three_note_matches_production);
  RUN_TEST(test_undo_of_overdub_with_hide_restores_the_note_in_both);
  RUN_TEST(test_note_edit_pitch_matches_production);
  RUN_TEST(test_note_edit_length_matches_production);
  RUN_TEST(test_note_edit_velocity_matches_production);
  RUN_TEST(test_note_edit_delete_matches_production);
  RUN_TEST(test_undo_of_note_edit_restores_the_note_in_both);
  RUN_TEST(test_two_edits_on_one_note_match_production);
  RUN_TEST(test_zero_length_note_kept_in_source_and_suppressed_by_production_projection);
  RUN_TEST(test_seeded_overlap_edit_and_undo_match_production);
  RUN_TEST(test_import_refusals_are_named_and_leave_no_partial_mirror);
  RUN_TEST(test_collapse_matrix_capture_capture_collapses);
  RUN_TEST(test_collapse_matrix_capture_and_edit_replacement_both_kept);
  RUN_TEST(test_collapse_matrix_edit_and_edit_replacements_measured);
  RUN_TEST(test_import_candidate_is_invisible_until_published);
  RUN_TEST(test_import_source_moving_mid_build_cancels_the_candidate);
  RUN_TEST(test_import_survives_pass_storage_rebuilt_with_the_same_content);
  RUN_TEST(test_import_refusals_happen_before_anything_is_written);
  RUN_TEST(test_wrap_overlapping_same_pitch_notes_are_attributed_like_production);
  printf("C1 import_parity_checks=%u unpaired_noteoffs=%u\n", gImportChecks, gImportUnpairedOffs);
  return UNITY_END();
}
