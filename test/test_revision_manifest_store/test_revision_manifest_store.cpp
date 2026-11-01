//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// Stage A of openspec/changes/revision-manifest-prototype: fixed event pages, immutable committed passes and
// immutable revision manifests. The oracle is a plain vector model of "the committed passes of revision N".

#include <unity.h>

#include <algorithm>
#include <cstdlib>
#include <new>
#include <tuple>
#include <vector>

#include "RevisionStore/RevisionStore.h"

// ---- Heap witness: every operator new in this process is counted ----------------------------------------------------
static unsigned long gHeapAllocations = 0;
void* operator new(std::size_t size) {
  ++gHeapAllocations;
  if (void* p = std::malloc(size == 0 ? 1 : size)) return p;
  throw std::bad_alloc();
}
void* operator new[](std::size_t size) { return operator new(size); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }

using namespace RevisionStore;

using TestCap = Capacity</*pages*/ 512, /*rows per page*/ 32, /*runs*/ 24, /*pages per run*/ 64, /*manifests*/ 24,
                         /*runs per manifest*/ 8, /*history*/ 8>;
using TestStore = Store<TestCap>;
static TestStore gStore;

constexpr uint32_t kBarTicks = 768;
constexpr uint32_t kLongTakeBars = 25;
constexpr uint32_t kLongTakeTicks = kBarTicks * kLongTakeBars;
constexpr uint8_t kNoteOn = 0x90;
constexpr uint8_t kNoteOff = 0x80;

// ---- Oracle -----------------------------------------------------------------------------------------------------------
struct OraclePass {
  std::vector<PassEvent> rows;     // Midi rows in append order
  std::vector<uint32_t> hides;     // noteIds hidden in earlier passes
};
struct OracleRevision {
  std::vector<OraclePass> passes;  // in commit order
};

struct Keyed {
  uint32_t tick;
  uint32_t pass;
  uint32_t seq;
  PassEvent row;
};

static std::vector<PassEvent> oracleWindow(const OracleRevision& rev, uint32_t from, uint32_t to) {
  std::vector<Keyed> keyed;
  for (uint32_t i = 0; i < rev.passes.size(); ++i) {
    const OraclePass& p = rev.passes[i];
    for (uint32_t s = 0; s < p.rows.size(); ++s) {
      const PassEvent& row = p.rows[s];
      if (row.tick < from || row.tick >= to) continue;
      bool hidden = false;
      for (uint32_t j = i + 1; j < rev.passes.size() && !hidden && row.noteId != 0; ++j) {
        const auto& h = rev.passes[j].hides;
        hidden = std::find(h.begin(), h.end(), row.noteId) != h.end();
      }
      if (!hidden) keyed.push_back({row.tick, i, s, row});
    }
  }
  std::sort(keyed.begin(), keyed.end(), [](const Keyed& a, const Keyed& b) {
    if (a.tick != b.tick) return a.tick < b.tick;
    const uint8_t ca = equalTickClass(a.row), cb = equalTickClass(b.row);
    if (ca != cb) return ca < cb;
    if (a.pass != b.pass) return a.pass < b.pass;
    return a.seq < b.seq;
  });
  std::vector<PassEvent> out;
  for (const Keyed& k : keyed) out.push_back(k.row);
  return out;
}

static uint32_t gLcg = 1;
static uint32_t nextRandom() {
  gLcg = gLcg * 1664525u + 1013904223u;
  return gLcg >> 8;
}

static uint32_t gNextNoteId = 1;

// Appends `notes` paired notes inside [0, loopTicks) in a non-monotonic order (like a capture across the wrap) to both
// the store writer and the oracle pass. A note whose off would pass the loop end wraps to the loop start.
template <typename S>
static void appendNotesTo(S& store, OraclePass& pass, uint32_t notes, uint32_t loopTicks,
                          uint8_t channelStatusOffset = 0) {
  for (uint32_t n = 0; n < notes; ++n) {
    const uint32_t on = nextRandom() % loopTicks;
    const uint32_t off = (on + 24 + nextRandom() % 300) % loopTicks;
    const uint8_t pitch = static_cast<uint8_t>(36 + nextRandom() % 48);
    const uint32_t id = gNextNoteId++;
    TEST_ASSERT_EQUAL(AppendResult::Ok, store.appendMidi(on, kNoteOn + channelStatusOffset, pitch, 100, id));
    TEST_ASSERT_EQUAL(AppendResult::Ok, store.appendMidi(off, kNoteOff + channelStatusOffset, pitch, 0, id));
    PassEvent a;
    a.tick = on;
    a.status = kNoteOn + channelStatusOffset;
    a.data1 = pitch;
    a.data2 = 100;
    a.noteId = id;
    PassEvent b = a;
    b.tick = off;
    b.status = kNoteOff + channelStatusOffset;
    b.data2 = 0;
    pass.rows.push_back(a);
    pass.rows.push_back(b);
  }
}

static void appendNotes(OraclePass& pass, uint32_t notes, uint32_t loopTicks, uint8_t channelStatusOffset = 0) {
  appendNotesTo(gStore, pass, notes, loopTicks, channelStatusOffset);
}

static void assertSameRows(const std::vector<PassEvent>& expected, const PassEvent* got, uint32_t count) {
  TEST_ASSERT_EQUAL_UINT32(expected.size(), count);
  for (uint32_t i = 0; i < count; ++i) {
    TEST_ASSERT_EQUAL_UINT32(expected[i].tick, got[i].tick);
    TEST_ASSERT_EQUAL_UINT8(expected[i].status, got[i].status);
    TEST_ASSERT_EQUAL_UINT8(expected[i].data1, got[i].data1);
    TEST_ASSERT_EQUAL_UINT32(expected[i].noteId, got[i].noteId);
  }
}

static PassEvent gOut[4096];

static void assertManifestMatches(ManifestId id, const OracleRevision& rev, uint32_t expectedRevision, uint32_t from,
                                  uint32_t to) {
  const ComposeStamp stamp = gStore.composeWindow(id, from, to, gOut, 4096);
  TEST_ASSERT_EQUAL(ComposeStatus::Complete, stamp.status);
  TEST_ASSERT_EQUAL_UINT16(id, stamp.manifest);
  TEST_ASSERT_EQUAL_UINT32(expectedRevision, stamp.revision);
  assertSameRows(oracleWindow(rev, from, to), gOut, stamp.count);
}

static OracleRevision recordTake(uint32_t notes, uint32_t loopTicks) {
  OracleRevision rev;
  rev.passes.emplace_back();
  TEST_ASSERT_EQUAL(AdmitResult::Ok, gStore.beginPass(PassKind::Record, loopTicks));
  appendNotes(rev.passes.back(), notes, loopTicks);
  TEST_ASSERT_NOT_EQUAL(kNoManifest, gStore.commitPass());
  return rev;
}

static OracleRevision overdub(const OracleRevision& parent, uint32_t notes, uint32_t loopTicks,
                              const std::vector<uint32_t>& hides = {}) {
  OracleRevision rev = parent;
  rev.passes.emplace_back();
  TEST_ASSERT_EQUAL(AdmitResult::Ok, gStore.beginPass(PassKind::Overdub));
  for (uint32_t h : hides) {
    TEST_ASSERT_EQUAL(AppendResult::Ok, gStore.appendHide(h));
    rev.passes.back().hides.push_back(h);
  }
  appendNotes(rev.passes.back(), notes, loopTicks, 7);  // overdub chords on ch8
  TEST_ASSERT_NOT_EQUAL(kNoManifest, gStore.commitPass());
  return rev;
}

void setUp() {
  gStore.reset();
  gLcg = 1;
  gNextNoteId = 1;
}
void tearDown() {}

// ---- Tests ----------------------------------------------------------------------------------------------------------

void test_record_commit_is_revision_one_and_tick_ordered() {
  const OracleRevision rev = recordTake(200, 4 * kBarTicks);
  const ManifestId current = gStore.current();
  TEST_ASSERT_EQUAL_UINT32(1, gStore.manifest(current).revision);
  TEST_ASSERT_EQUAL_UINT8(1, gStore.manifest(current).runCount);
  assertManifestMatches(current, rev, 1, 0, 4 * kBarTicks);
}

// The owner's property: Record + four overdubs committed back to back; every revision is a complete immutable state
// at its commit, and every older manifest that a reader pinned still composes exactly its own committed passes.
void test_every_revision_is_exactly_its_committed_passes_and_old_pins_survive() {
  std::vector<OracleRevision> revisions;
  std::vector<ManifestId> pins;
  revisions.push_back(recordTake(300, kLongTakeTicks));
  pins.push_back(gStore.pinCurrent());
  for (int i = 0; i < 4; ++i) {
    revisions.push_back(overdub(revisions.back(), 40, kLongTakeTicks));
    pins.push_back(gStore.pinCurrent());
  }
  TEST_ASSERT_EQUAL_UINT32(5, gStore.manifest(gStore.current()).revision);
  for (uint32_t r = 0; r < revisions.size(); ++r) {
    assertManifestMatches(pins[r], revisions[r], r + 1, 0, kLongTakeTicks);
  }
  for (ManifestId pin : pins) gStore.unpin(pin);
}

// The device witness: revision 7 served an EMPTY window. Here every two-bar window of the long take at the newest
// revision contains Record plus every committed overdub inside it, and the stamp names the manifest it read.
void test_two_bar_windows_of_long_take_contain_record_and_all_overdubs() {
  OracleRevision rev = recordTake(300, kLongTakeTicks);
  for (int i = 0; i < 6; ++i) rev = overdub(rev, 30, kLongTakeTicks);
  const ManifestId current = gStore.current();
  TEST_ASSERT_EQUAL_UINT32(7, gStore.manifest(current).revision);
  uint32_t total = 0;
  for (uint32_t from = 0; from < kLongTakeTicks; from += 2 * kBarTicks) {
    const uint32_t to = std::min(from + 2 * kBarTicks, kLongTakeTicks);
    assertManifestMatches(current, rev, 7, from, to);
    total += static_cast<uint32_t>(oracleWindow(rev, from, to).size());
  }
  TEST_ASSERT_EQUAL_UINT32(600 + 6 * 60, total);
}

void test_uncommitted_overdub_never_changes_current() {
  OracleRevision rev = recordTake(100, 8 * kBarTicks);
  rev = overdub(rev, 20, 8 * kBarTicks);
  const ManifestId before = gStore.current();
  TEST_ASSERT_EQUAL(AdmitResult::Ok, gStore.beginPass(PassKind::Overdub));
  OracleRevision scratch;
  scratch.passes.emplace_back();
  appendNotes(scratch.passes.back(), 50, 8 * kBarTicks);
  TEST_ASSERT_EQUAL_UINT16(before, gStore.current());
  assertManifestMatches(before, rev, 2, 0, 8 * kBarTicks);
  const uint16_t freeWhileWriting = gStore.freePageCount();
  gStore.abandonPass();
  TEST_ASSERT_TRUE(gStore.freePageCount() > freeWhileWriting);
  TEST_ASSERT_EQUAL_UINT16(before, gStore.current());
  assertManifestMatches(before, rev, 2, 0, 8 * kBarTicks);
}

void test_hide_masks_earlier_runs_only_and_older_revision_keeps_the_note() {
  OracleRevision r1 = recordTake(50, 4 * kBarTicks);
  const ManifestId pin1 = gStore.pinCurrent();
  const uint32_t hidden = r1.passes[0].rows[10].noteId;
  OracleRevision r2 = overdub(r1, 10, 4 * kBarTicks, {hidden});
  assertManifestMatches(gStore.current(), r2, 2, 0, 4 * kBarTicks);
  const ComposeStamp s2 = gStore.composeWindow(gStore.current(), 0, 4 * kBarTicks, gOut, 4096);
  for (uint32_t i = 0; i < s2.count; ++i) TEST_ASSERT_NOT_EQUAL_UINT32(hidden, gOut[i].noteId);
  assertManifestMatches(pin1, r1, 1, 0, 4 * kBarTicks);
  gStore.unpin(pin1);
}

void test_undo_redo_select_existing_manifests_and_commit_after_undo_reclaims_redo_branch() {
  const uint16_t freeAtStart = gStore.freePageCount();
  OracleRevision r1 = recordTake(100, 8 * kBarTicks);
  OracleRevision r2 = overdub(r1, 40, 8 * kBarTicks);
  const ManifestId m2 = gStore.current();
  TEST_ASSERT_TRUE(gStore.undo());
  assertManifestMatches(gStore.current(), r1, 1, 0, 8 * kBarTicks);
  TEST_ASSERT_TRUE(gStore.redo());
  TEST_ASSERT_EQUAL_UINT16(m2, gStore.current());
  assertManifestMatches(gStore.current(), r2, 2, 0, 8 * kBarTicks);

  TEST_ASSERT_TRUE(gStore.undo());
  const uint16_t freeBeforeBranch = gStore.freePageCount();
  OracleRevision r2b = overdub(r1, 5, 8 * kBarTicks);
  // The abandoned redo branch (40 notes = 80 rows = 3 pages) went back to the pool; the new branch took 1 page.
  TEST_ASSERT_EQUAL_UINT16(freeBeforeBranch + 3 - 1, gStore.freePageCount());
  TEST_ASSERT_FALSE(gStore.canRedo());
  assertManifestMatches(gStore.current(), r2b, 2, 0, 8 * kBarTicks);
  TEST_ASSERT_TRUE(freeAtStart > gStore.freePageCount());
}

void test_compaction_keeps_revision_content_and_reclaims_after_last_reader() {
  OracleRevision rev = recordTake(200, kLongTakeTicks);
  for (int i = 0; i < 4; ++i) rev = overdub(rev, 25, kLongTakeTicks, {rev.passes[0].rows[4 * i].noteId});
  const ManifestId reader = gStore.pinCurrent();  // playback pinned the 5-run manifest
  const uint16_t freeBefore = gStore.freePageCount();
  TEST_ASSERT_EQUAL(CompactResult::Ok, gStore.compactCurrentPrefix(4));
  const ManifestId compacted = gStore.current();
  TEST_ASSERT_NOT_EQUAL(reader, compacted);
  TEST_ASSERT_EQUAL_UINT8(2, gStore.manifest(compacted).runCount);
  for (uint32_t from = 0; from < kLongTakeTicks; from += 3 * kBarTicks) {
    const uint32_t to = std::min(from + 3 * kBarTicks, kLongTakeTicks);
    assertManifestMatches(compacted, rev, 5, from, to);  // same revision, same content
    assertManifestMatches(reader, rev, 5, from, to);     // the pinned reader is undisturbed
  }
  TEST_ASSERT_TRUE(gStore.freePageCount() < freeBefore);  // both representations coexist while pinned
  gStore.unpin(reader);
  // Undo history still holds revisions 1..4, which share every old run, so nothing is reclaimable yet; what must hold
  // is conservation: no page is lost or double-freed.
  TEST_ASSERT_EQUAL_UINT32(TestCap::kPageCount, gStore.ownedPageCount() + gStore.freePageCount());
  assertManifestMatches(gStore.current(), rev, 5, 0, kLongTakeTicks);
}

// With no history behind it (a lone Record + overdubs, then the older revisions age out), compaction plus the last
// reader leaving returns the superseded runs' pages to the pool.
void test_compaction_reclaims_superseded_runs_when_no_revision_references_them() {
  OracleRevision rev = recordTake(200, 8 * kBarTicks);
  for (int i = 0; i < 3; ++i) rev = overdub(rev, 30, 8 * kBarTicks);
  const uint32_t ownedBefore = gStore.ownedPageCount();
  TEST_ASSERT_EQUAL(CompactResult::Ok, gStore.compactCurrentPrefix(4));
  // Old runs are still referenced by revisions 1..3 in history, so pages grew by the compacted run only.
  const uint32_t ownedAfter = gStore.ownedPageCount();
  TEST_ASSERT_TRUE(ownedAfter > ownedBefore);
  // A new Record replaces the content; history keeps eight entries, so commit eight Records to age out revs 1..5.
  for (int i = 0; i < TestCap::kHistoryDepth; ++i) {
    TEST_ASSERT_EQUAL(AdmitResult::Ok, gStore.beginPass(PassKind::Record, 4 * kBarTicks));
    gStore.appendMidi(0, kNoteOn, 60, 100, 0);
    gStore.commitPass();
  }
  // Only the eight one-page Record runs remain.
  TEST_ASSERT_EQUAL_UINT32(TestCap::kHistoryDepth, gStore.ownedPageCount());
  TEST_ASSERT_EQUAL_UINT32(TestCap::kPageCount, gStore.ownedPageCount() + gStore.freePageCount());
  TEST_ASSERT_EQUAL_UINT16(TestCap::kHistoryDepth, gStore.liveManifestCount());
  (void)rev;
}

void test_full_manifest_denies_overdub_admission_until_compaction() {
  OracleRevision rev = recordTake(20, 4 * kBarTicks);
  for (int i = 0; i < TestCap::kRunsPerManifest - 1; ++i) rev = overdub(rev, 2, 4 * kBarTicks);
  TEST_ASSERT_EQUAL(AdmitResult::DeniedManifestFull, gStore.beginPass(PassKind::Overdub));
  TEST_ASSERT_EQUAL(CompactResult::Ok, gStore.compactCurrentPrefix(TestCap::kRunsPerManifest));
  TEST_ASSERT_EQUAL(AdmitResult::Ok, gStore.beginPass(PassKind::Overdub));
  gStore.abandonPass();
  assertManifestMatches(gStore.current(), rev, TestCap::kRunsPerManifest, 0, 4 * kBarTicks);
}

void test_page_exhaustion_is_a_named_denial_not_a_hang() {
  TEST_ASSERT_EQUAL(AdmitResult::Ok, gStore.beginPass(PassKind::Record, kLongTakeTicks));
  AppendResult last = AppendResult::Ok;
  uint32_t appended = 0;
  while (last == AppendResult::Ok && appended < 100000) {
    last = gStore.appendMidi(appended % kLongTakeTicks, kNoteOn, 60, 100, appended + 1);
    if (last == AppendResult::Ok) ++appended;
  }
  TEST_ASSERT_EQUAL(AppendResult::DeniedRunFull, last);  // per-run page directory bound reached first
  TEST_ASSERT_EQUAL_UINT32(TestCap::kPagesPerRun * TestCap::kEventsPerPage, appended);
  gStore.abandonPass();
  TEST_ASSERT_EQUAL_UINT16(TestCap::kPageCount, gStore.freePageCount());
}

// Stage A acceptance: after construction, record + overdubs + commit + compose + undo + compaction allocate nothing.
void test_zero_heap_after_construction() {
  const unsigned long before = gHeapAllocations;
  TEST_ASSERT_EQUAL(AdmitResult::Ok, gStore.beginPass(PassKind::Record, kLongTakeTicks));
  for (uint32_t i = 0; i < 400; ++i) gStore.appendMidi((i * 97) % kLongTakeTicks, kNoteOn, 60, 100, i + 1);
  gStore.commitPass();
  for (int od = 0; od < 5; ++od) {
    gStore.beginPass(PassKind::Overdub);
    gStore.appendHide(static_cast<uint32_t>(od + 1));
    for (uint32_t i = 0; i < 60; ++i) gStore.appendMidi((i * 131 + od) % kLongTakeTicks, kNoteOn + 7, 64, 90, 1000 + i);
    gStore.commitPass();
  }
  const ManifestId pin = gStore.pinCurrent();
  for (uint32_t from = 0; from < kLongTakeTicks; from += 2 * kBarTicks) {
    gStore.composeWindow(pin, from, from + 2 * kBarTicks, gOut, 4096);
  }
  gStore.undo();
  gStore.redo();
  gStore.beginCompaction(0, 4, CompactionScope::AllowCopies);
  while (gStore.stepCompaction(64) == CompactStep::Pending) {
  }
  gStore.unpin(pin);
  TEST_ASSERT_EQUAL_UINT32(0, gHeapAllocations - before);
  // Witness self-check: the counter does see an allocation, so the zero above is not vacuous.
  std::vector<int> probe(1);
  TEST_ASSERT_TRUE(gHeapAllocations - before > 0);
}

// ---- Stage B qualification gates (owner review 2026-11-01) ------------------------------------------------------------

// A reader holding a redo-branch manifest keeps it through undo + a new commit that discards the branch from history.
// The branch's pages return only when that reader releases it.
void test_reader_of_discarded_redo_branch_survives_until_released() {
  OracleRevision r1 = recordTake(100, 8 * kBarTicks);
  const uint32_t ownedAtR1 = gStore.ownedPageCount();
  OracleRevision r2 = overdub(r1, 40, 8 * kBarTicks);
  OracleRevision r3 = overdub(r2, 40, 8 * kBarTicks);
  const ManifestId reader = gStore.pinCurrent();
  const uint32_t readerIdentity = gStore.identityOf(reader);
  const uint32_t branchPages = gStore.ownedPageCount() - ownedAtR1;
  TEST_ASSERT_TRUE(gStore.undo());
  TEST_ASSERT_TRUE(gStore.undo());
  OracleRevision r2b = overdub(r1, 10, 8 * kBarTicks);  // discards r2, r3 from history
  TEST_ASSERT_FALSE(gStore.canRedo());
  const uint32_t r2bPages = gStore.ownedPageCount() - ownedAtR1 - branchPages;
  // Branch pages are still owned: the reader holds r3, which holds r2's run too.
  TEST_ASSERT_EQUAL_UINT32(ownedAtR1 + branchPages + r2bPages, gStore.ownedPageCount());
  const ComposeStamp stamp = gStore.composeWindow(reader, 0, 8 * kBarTicks, gOut, 4096);
  TEST_ASSERT_EQUAL_UINT32(readerIdentity, stamp.identity);
  TEST_ASSERT_EQUAL_UINT32(3, stamp.revision);
  assertSameRows(oracleWindow(r3, 0, 8 * kBarTicks), gOut, stamp.count);
  assertManifestMatches(gStore.current(), r2b, 2, 0, 8 * kBarTicks);
  gStore.unpin(reader);
  TEST_ASSERT_EQUAL_UINT32(ownedAtR1 + r2bPages, gStore.ownedPageCount());
  TEST_ASSERT_EQUAL_UINT32(TestCap::kPageCount, gStore.ownedPageCount() + gStore.freePageCount());
  assertManifestMatches(gStore.current(), r2b, 2, 0, 8 * kBarTicks);
}

// Logical revision and physical identity are separate: compaction keeps the revision and changes the identity, and a
// manifest slot reused later never repeats an identity, so a window built from the old manifest cannot be mislabeled.
void test_compaction_and_slot_reuse_change_identity_not_revision() {
  OracleRevision rev = recordTake(50, 4 * kBarTicks);
  rev = overdub(rev, 10, 4 * kBarTicks);
  const ManifestId before = gStore.pinCurrent();
  const ComposeStamp oldStamp = gStore.composeWindow(before, 0, 4 * kBarTicks, gOut, 4096);
  TEST_ASSERT_EQUAL(CompactResult::Ok, gStore.compactCurrentPrefix(2));
  const ComposeStamp newStamp = gStore.composeWindow(gStore.current(), 0, 4 * kBarTicks, gOut, 4096);
  TEST_ASSERT_EQUAL_UINT32(oldStamp.revision, newStamp.revision);
  TEST_ASSERT_NOT_EQUAL_UINT32(oldStamp.identity, newStamp.identity);
  gStore.unpin(before);  // the pre-compaction manifest is now unreferenced; its slot is free
  TEST_ASSERT_EQUAL(AdmitResult::Ok, gStore.beginPass(PassKind::Overdub));
  gStore.appendMidi(0, kNoteOn, 60, 100, 9999);
  const ManifestId next = gStore.commitPass();
  TEST_ASSERT_NOT_EQUAL_UINT32(oldStamp.identity, gStore.identityOf(next));
  TEST_ASSERT_NOT_EQUAL_UINT32(newStamp.identity, gStore.identityOf(next));
}

// Equal-tick rows follow production's canonical class (NoteOff < NoteOn < other), and inside one class keep their
// append ordinal through seal: the CC appended before the program change stays before it.
void test_equal_tick_rows_follow_production_class_then_append_order() {
  TEST_ASSERT_EQUAL(AdmitResult::Ok, gStore.beginPass(PassKind::Record, 4 * kBarTicks));
  gStore.appendMidi(500, kNoteOn, 40, 100, 1);
  gStore.appendMidi(96, 0xB0, 64, 127, 0);   // CC then NoteOn then NoteOff, all at tick 96
  gStore.appendMidi(96, kNoteOn, 60, 100, 2);
  gStore.appendMidi(96, kNoteOff, 59, 0, 3);
  gStore.appendMidi(10, kNoteOff, 40, 0, 1);
  gStore.appendMidi(96, 0xC0, 5, 0, 0);      // program change last on tick 96
  gStore.commitPass();
  const ComposeStamp stamp = gStore.composeWindow(gStore.current(), 0, 4 * kBarTicks, gOut, 4096);
  TEST_ASSERT_EQUAL_UINT32(6, stamp.count);
  const uint8_t expectedStatus[] = {kNoteOff, kNoteOff, kNoteOn, 0xB0, 0xC0, kNoteOn};
  const uint32_t expectedTick[] = {10, 96, 96, 96, 96, 500};
  for (uint32_t i = 0; i < 6; ++i) {
    TEST_ASSERT_EQUAL_UINT32(expectedTick[i], gOut[i].tick);
    TEST_ASSERT_EQUAL_UINT8(expectedStatus[i], gOut[i].status);
  }
}

// Across runs the class still decides first: an overdub NoteOff on the Record's NoteOn tick plays before it.
void test_equal_tick_class_outranks_run_order() {
  gStore.beginPass(PassKind::Record, 4 * kBarTicks);
  gStore.appendMidi(192, kNoteOn, 60, 100, 1);
  gStore.appendMidi(192, 0xB0, 1, 10, 0);
  gStore.commitPass();
  gStore.beginPass(PassKind::Overdub);
  gStore.appendMidi(192, kNoteOff + 7, 72, 0, 2);
  gStore.appendMidi(192, 0xB0 + 7, 1, 20, 0);
  gStore.commitPass();
  const ComposeStamp stamp = gStore.composeWindow(gStore.current(), 0, 4 * kBarTicks, gOut, 4096);
  TEST_ASSERT_EQUAL_UINT32(4, stamp.count);
  const uint8_t expectedStatus[] = {kNoteOff + 7, kNoteOn, 0xB0, 0xB0 + 7};  // other-class ties keep run order
  for (uint32_t i = 0; i < 4; ++i) TEST_ASSERT_EQUAL_UINT8(expectedStatus[i], gOut[i].status);
}

// A tiny store makes the pool, not the per-run directory, run out first.
using TinyCap = Capacity</*pages*/ 8, /*rows per page*/ 4, /*runs*/ 6, /*pages per run*/ 8, /*manifests*/ 6,
                         /*runs per manifest*/ 4, /*history*/ 4>;
static Store<TinyCap> gTiny;

// Page exhaustion mid-pass latches a named stop: later appends return the same reason, the committed revision is
// untouched, and the writer can either abandon (every provisional page returns) or commit what it captured.
void test_page_exhaustion_mid_pass_latches_and_leaves_committed_revision_untouched() {
  gTiny.reset();
  OracleRevision r1;
  r1.passes.emplace_back();
  TEST_ASSERT_EQUAL(AdmitResult::Ok, gTiny.beginPass(PassKind::Record, 4 * kBarTicks));
  appendNotesTo(gTiny, r1.passes.back(), 4, 4 * kBarTicks);  // 8 rows = 2 pages
  const ManifestId m1 = gTiny.commitPass();
  for (int attempt = 0; attempt < 2; ++attempt) {
    TEST_ASSERT_EQUAL(AdmitResult::Ok, gTiny.beginPass(PassKind::Overdub));
    uint32_t accepted = 0;
    AppendResult last = AppendResult::Ok;
    while (last == AppendResult::Ok) {
      last = gTiny.appendMidi(accepted * 7, kNoteOn, 70, 90, 500 + accepted);
      if (last == AppendResult::Ok) ++accepted;
    }
    TEST_ASSERT_EQUAL(AppendResult::DeniedNoPage, last);
    TEST_ASSERT_EQUAL(AppendResult::DeniedNoPage, gTiny.writerStop());
    // The Record left six pages: four inline data pages, then a fifth data page that also needs a directory page.
    TEST_ASSERT_EQUAL_UINT32(5 * 4, accepted);
    TEST_ASSERT_EQUAL(AppendResult::DeniedNoPage, gTiny.appendMidi(1, kNoteOn, 71, 90, 9000));  // latched
    TEST_ASSERT_EQUAL_UINT16(m1, gTiny.current());
    const ComposeStamp s1 = gTiny.composeWindow(m1, 0, 4 * kBarTicks, gOut, 4096);
    assertSameRows(oracleWindow(r1, 0, 4 * kBarTicks), gOut, s1.count);
    if (attempt == 0) {
      gTiny.abandonPass();
      TEST_ASSERT_EQUAL(AppendResult::Ok, gTiny.writerStop());
      TEST_ASSERT_EQUAL_UINT32(2, gTiny.ownedPageCount());
      TEST_ASSERT_EQUAL_UINT16(TinyCap::kPageCount - 2, gTiny.freePageCount());
    } else {
      const ManifestId m2 = gTiny.commitPass();  // commit the captured rows: a complete, smaller revision 2
      TEST_ASSERT_EQUAL_UINT32(2, gTiny.manifest(m2).revision);
      const ComposeStamp s2 = gTiny.composeWindow(m2, 0, 4 * kBarTicks, gOut, 4096);
      TEST_ASSERT_EQUAL_UINT32(8 + 20, s2.count);
    }
  }
}

// A pass takes any free pages by index; after a branch is discarded the next pass spans non-adjacent page ids.
void test_pass_spans_non_contiguous_free_pages() {
  gTiny.reset();
  OracleRevision r1;
  r1.passes.emplace_back();
  gTiny.beginPass(PassKind::Record, 4 * kBarTicks);
  appendNotesTo(gTiny, r1.passes.back(), 2, 4 * kBarTicks);  // 1 page
  gTiny.commitPass();
  OracleRevision scratch;
  scratch.passes.emplace_back();
  gTiny.beginPass(PassKind::Overdub);
  appendNotesTo(gTiny, scratch.passes.back(), 4, 4 * kBarTicks);  // 2 pages
  gTiny.commitPass();
  TEST_ASSERT_TRUE(gTiny.undo());
  OracleRevision r2 = r1;
  r2.passes.emplace_back();
  gTiny.beginPass(PassKind::Overdub);
  appendNotesTo(gTiny, r2.passes.back(), 1, 4 * kBarTicks);  // 1 page; commit releases the discarded 2-page branch
  gTiny.commitPass();
  OracleRevision r3 = r2;
  r3.passes.emplace_back();
  gTiny.beginPass(PassKind::Overdub);
  appendNotesTo(gTiny, r3.passes.back(), 6, 4 * kBarTicks);  // 3 pages from the fragmented free stack
  const ManifestId m3 = gTiny.commitPass();
  const RunId runId = gTiny.manifest(m3).runs[2];
  TEST_ASSERT_EQUAL_UINT16(3, gTiny.run(runId).pageCount);
  bool adjacent = true;
  for (uint16_t i = 1; i < 3; ++i) adjacent = adjacent && gTiny.dataPageOf(runId, i) == gTiny.dataPageOf(runId, i - 1) + 1;
  TEST_ASSERT_FALSE(adjacent);
  const ComposeStamp s = gTiny.composeWindow(m3, 0, 4 * kBarTicks, gOut, 4096);
  assertSameRows(oracleWindow(r3, 0, 4 * kBarTicks), gOut, s.count);
}

// ---- Sort-free commit and step-bounded compaction (B.4 follow-up) -----------------------------------------------------

// A capture-shaped take: tick-ordered from mid-loop, one wrap, and same-tick NoteOn-then-NoteOff inversions (legato).
// Commit needs no sort: two segments, slowSeals stays 0, and the order is DEC-244.
static OracleRevision appendCaptureShapedTake(uint32_t loopTicks, uint32_t notes) {
  OracleRevision rev;
  rev.passes.emplace_back();
  OraclePass& pass = rev.passes.back();
  const uint32_t step = loopTicks / notes;
  uint32_t tick = loopTicks / 2;
  uint32_t previousId = 0;
  uint8_t previousPitch = 0;
  for (uint32_t n = 0; n < notes; ++n, tick = (tick + step) % loopTicks) {
    const uint8_t pitch = static_cast<uint8_t>(40 + n % 30);
    const uint32_t id = gNextNoteId++;
    PassEvent on;
    on.tick = tick;
    on.status = kNoteOn;
    on.data1 = pitch;
    on.data2 = 100;
    on.noteId = id;
    TEST_ASSERT_EQUAL(AppendResult::Ok, gStore.appendMidi(on.tick, on.status, on.data1, on.data2, on.noteId));
    pass.rows.push_back(on);
    if (previousId != 0) {  // legato: the previous note's NoteOff arrives after this NoteOn, on the same tick
      PassEvent off;
      off.tick = tick;
      off.status = kNoteOff;
      off.data1 = previousPitch;
      off.noteId = previousId;
      TEST_ASSERT_EQUAL(AppendResult::Ok, gStore.appendMidi(off.tick, off.status, off.data1, 0, off.noteId));
      pass.rows.push_back(off);
    }
    previousId = id;
    previousPitch = pitch;
  }
  return rev;
}

void test_capture_shaped_take_commits_without_sort() {
  TEST_ASSERT_EQUAL(AdmitResult::Ok, gStore.beginPass(PassKind::Record, kLongTakeTicks));
  const OracleRevision rev = appendCaptureShapedTake(kLongTakeTicks, 400);
  const ManifestId m = gStore.commitPass();
  TEST_ASSERT_EQUAL_UINT32(0, gStore.slowSeals());
  TEST_ASSERT_EQUAL_UINT8(2, gStore.run(gStore.manifest(m).runs[0]).segCount);  // one wrap
  assertManifestMatches(m, rev, 1, 0, kLongTakeTicks);
  for (uint32_t from = 0; from < kLongTakeTicks; from += 2 * kBarTicks) {
    assertManifestMatches(m, rev, 1, from, std::min(from + 2 * kBarTicks, kLongTakeTicks));
  }
}

// Scattered appends exceed the segment budget: the commit falls back to a sort, counted, and the order is still exact.
void test_segment_budget_overflow_falls_back_to_counted_sort() {
  const OracleRevision rev = recordTake(200, 4 * kBarTicks);  // random ticks: far more than 8 descents
  TEST_ASSERT_EQUAL_UINT32(1, gStore.slowSeals());
  assertManifestMatches(gStore.current(), rev, 1, 0, 4 * kBarTicks);
}

static OracleRevision buildFiveRunTake() {
  OracleRevision rev = recordTake(200, kLongTakeTicks);
  for (int i = 0; i < 4; ++i) rev = overdub(rev, 25, kLongTakeTicks, {rev.passes[0].rows[4 * i].noteId});
  return rev;
}

void test_stepped_compaction_matches_and_is_bounded_per_step() {
  const OracleRevision rev = buildFiveRunTake();
  TEST_ASSERT_EQUAL(CompactResult::Ok, gStore.beginCompaction(0, 5, CompactionScope::AllowCopies));
  uint32_t steps = 0;
  CompactStep step = CompactStep::Pending;
  while (step == CompactStep::Pending) {
    const uint16_t freeBefore = gStore.freePageCount();
    step = gStore.stepCompaction(7);
    TEST_ASSERT_TRUE(freeBefore - gStore.freePageCount() <= 2);  // 7 rows: at most one data page (+ its directory page)
    ++steps;
    if (step == CompactStep::Pending) assertManifestMatches(gStore.current(), rev, 5, 0, kLongTakeTicks);  // undisturbed
  }
  TEST_ASSERT_EQUAL(CompactStep::Published, step);
  const uint32_t sourceRows = 400 + 4 * 50;
  TEST_ASSERT_TRUE(steps >= sourceRows / 7);
  TEST_ASSERT_EQUAL_UINT8(1, gStore.manifest(gStore.current()).runCount);
  assertManifestMatches(gStore.current(), rev, 5, 0, kLongTakeTicks);
  TEST_ASSERT_EQUAL_UINT32(TestCap::kPageCount, gStore.ownedPageCount() + gStore.freePageCount());
}

void test_undo_during_compaction_supersedes_it_and_conserves_pages() {
  const OracleRevision rev = buildFiveRunTake();
  const uint32_t ownedBefore = gStore.ownedPageCount();
  TEST_ASSERT_EQUAL(CompactResult::Ok, gStore.beginCompaction(0, 5, CompactionScope::AllowCopies));
  TEST_ASSERT_EQUAL(CompactStep::Pending, gStore.stepCompaction(50));
  TEST_ASSERT_TRUE(gStore.undo());  // current is now revision 4 (four runs): it no longer starts with the five runs
  CompactStep step = CompactStep::Pending;
  while (step == CompactStep::Pending) step = gStore.stepCompaction(50);
  TEST_ASSERT_EQUAL(CompactStep::Superseded, step);
  TEST_ASSERT_EQUAL_UINT32(ownedBefore, gStore.ownedPageCount());
  TEST_ASSERT_TRUE(gStore.redo());
  assertManifestMatches(gStore.current(), rev, 5, 0, kLongTakeTicks);
}

void test_overdub_committed_during_compaction_is_kept_on_publish() {
  OracleRevision rev = buildFiveRunTake();
  TEST_ASSERT_EQUAL(CompactResult::Ok, gStore.beginCompaction(0, 5, CompactionScope::AllowCopies));
  TEST_ASSERT_EQUAL(CompactStep::Pending, gStore.stepCompaction(100));
  rev = overdub(rev, 10, kLongTakeTicks, {rev.passes[1].rows[0].noteId});  // revision 6 commits mid-job
  CompactStep step = CompactStep::Pending;
  while (step == CompactStep::Pending) step = gStore.stepCompaction(100);
  TEST_ASSERT_EQUAL(CompactStep::Published, step);
  TEST_ASSERT_EQUAL_UINT8(2, gStore.manifest(gStore.current()).runCount);  // compacted + the new overdub
  assertManifestMatches(gStore.current(), rev, 6, 0, kLongTakeTicks);
}

// ---- B.5a: history-closed compaction and a shared page pool -------------------------------------------------------

void test_compaction_refuses_a_range_history_still_names_and_admits_a_closed_one() {
  OracleRevision rev = recordTake(50, 4 * kBarTicks);
  for (int i = 0; i < 3; ++i) rev = overdub(rev, 5, 4 * kBarTicks);  // history: rev1..rev4, each a prefix
  // Overdubs 1..3: revisions 2 and 3 hold only part of that range.
  TEST_ASSERT_FALSE(gStore.rangeIsHistoryClosed(1, 3));
  TEST_ASSERT_EQUAL(CompactResult::NotHistoryClosed, gStore.beginCompaction(1, 3));
  TEST_ASSERT_EQUAL_UINT8(0, gStore.largestClosedRangeFrom(1));
  // Age the history out with a fresh store whose depth is 2: only the last two revisions remain.
  using ShortCap = Capacity<512, 32, 24, 64, 24, 8, /*history*/ 2>;
  static Store<ShortCap> shortStore;
  shortStore.reset();
  shortStore.beginPass(PassKind::Record, 4 * kBarTicks);
  shortStore.appendMidi(0, kNoteOn, 60, 100, 1);
  shortStore.appendMidi(96, kNoteOff, 60, 0, 1);
  shortStore.commitPass();
  for (uint32_t i = 0; i < 4; ++i) {
    shortStore.beginPass(PassKind::Overdub);
    shortStore.appendMidi(100 + i * 10, kNoteOn, 70 + i, 100, 10 + i);
    shortStore.appendMidi(150 + i * 10, kNoteOff, 70 + i, 0, 10 + i);
    shortStore.commitPass();
  }
  // History holds revisions 4 (runs R,1,2,3) and 5 (R,1,2,3,4): runs 1..3 are closed, run 4 is not.
  TEST_ASSERT_EQUAL_UINT8(3, shortStore.largestClosedRangeFrom(1));
  const uint32_t ownedBefore = shortStore.ownedPageCount();
  TEST_ASSERT_EQUAL(CompactResult::Ok, shortStore.beginCompaction(1, 3));
  while (shortStore.stepCompaction(4) == CompactStep::Pending) {
  }
  // Both history entries were rewritten, so the three originals are gone: one merged run replaces three.
  TEST_ASSERT_EQUAL_UINT8(3, shortStore.manifest(shortStore.current()).runCount);
  TEST_ASSERT_TRUE(shortStore.ownedPageCount() < ownedBefore);
  TEST_ASSERT_TRUE(shortStore.undo());
  TEST_ASSERT_EQUAL_UINT8(2, shortStore.manifest(shortStore.current()).runCount);  // revision 4: R + merged
}

void test_two_slots_share_one_page_pool() {
  using SlotCap = Capacity<512, 32, 24, 64, 24, 8, 8>;
  static StoreCore<SlotCap>::Pool pool;
  pool.reset();
  static StoreCore<SlotCap> slotA(pool);
  static StoreCore<SlotCap> slotB(pool);
  slotA.releaseAll();
  slotB.releaseAll();
  TEST_ASSERT_EQUAL(AdmitResult::Ok, slotA.beginPass(PassKind::Record, 4 * kBarTicks));
  for (uint32_t i = 0; i < 100; ++i) slotA.appendMidi(i * 30, kNoteOn, 60, 100, i + 1);
  slotA.commitPass();
  TEST_ASSERT_EQUAL(AdmitResult::Ok, slotB.beginPass(PassKind::Record, 4 * kBarTicks));
  for (uint32_t i = 0; i < 40; ++i) slotB.appendMidi(i * 50, kNoteOn, 72, 100, i + 1);
  slotB.commitPass();
  TEST_ASSERT_EQUAL_UINT32(512, slotA.ownedPageCount() + slotB.ownedPageCount() + pool.freeCount());
  const ComposeStamp a = slotA.composeWindow(slotA.current(), 0, 4 * kBarTicks, gOut, 4096);
  const ComposeStamp b = slotB.composeWindow(slotB.current(), 0, 4 * kBarTicks, gOut, 4096);
  TEST_ASSERT_EQUAL_UINT32(100, a.count);
  TEST_ASSERT_EQUAL_UINT32(40, b.count);
  slotA.releaseAll();
  TEST_ASSERT_EQUAL_UINT32(512, slotB.ownedPageCount() + pool.freeCount());
}

// A run larger than the inline page ids spills into directory pages and still composes exactly.
void test_large_run_uses_directory_pages() {
  const OracleRevision rev = recordTake(1000, kLongTakeTicks);  // 2,000 rows = 63 data pages at 32 rows
  const RunId run = gStore.manifest(gStore.current()).runs[0];
  TEST_ASSERT_EQUAL_UINT16(63, gStore.run(run).pageCount);
  TEST_ASSERT_EQUAL_UINT16(1, gStore.run(run).dirPageCount);
  assertManifestMatches(gStore.current(), rev, 1, 0, kLongTakeTicks);
}

// ---- B.5c / DEC-245: lossless compaction, explicit projections, the planner ---------------------------------------

struct SourceKey {
  uint32_t tick, noteId, seq, serial;
  uint8_t status, data1, data2, hide;
  bool operator<(const SourceKey& o) const {
    return std::tie(serial, hide, seq, tick, noteId, status, data1, data2) <
           std::tie(o.serial, o.hide, o.seq, o.tick, o.noteId, o.status, o.data1, o.data2);
  }
  bool operator==(const SourceKey& o) const { return !(*this < o) && !(o < *this); }
};

template <typename S>
static std::vector<SourceKey> sourceMultiset(const S& store, ManifestId id) {
  std::vector<SourceKey> keys;
  store.forEachSourceRow(id, [&](const PassEvent& e, uint32_t serial, bool isHide) {
    keys.push_back({e.tick, e.noteId, e.seq, serial, e.status, e.data1, e.data2, static_cast<uint8_t>(isHide)});
  });
  std::sort(keys.begin(), keys.end());
  return keys;
}

static std::vector<PassEvent> composeAll(ManifestId id, EffectivePolicy policy) {
  const ComposeStamp stamp = gStore.composeWindow(id, 0, kLongTakeTicks, gOut, 4096, policy);
  TEST_ASSERT_EQUAL(ComposeStatus::Complete, stamp.status);
  return std::vector<PassEvent>(gOut, gOut + stamp.count);
}

static bool sameRows(const std::vector<PassEvent>& a, const std::vector<PassEvent>& b) {
  if (a.size() != b.size()) return false;
  for (size_t i = 0; i < a.size(); ++i) {
    if (a[i].tick != b[i].tick || a[i].status != b[i].status || a[i].data1 != b[i].data1 || a[i].data2 != b[i].data2 ||
        a[i].noteId != b[i].noteId) {
      return false;
    }
  }
  return true;
}

// Record with a zero-length note; OD1 repeats a Record CC row byte-for-byte and hides a Record note; OD2 hides a note
// OD1 added (a hide between source passes *inside* the compacted range) and repeats OD1's CC; OD3 is left outside.
static void buildLosslessFixture() {
  gStore.beginPass(PassKind::Record, kLongTakeTicks);
  gStore.appendMidi(96, kNoteOn, 60, 100, 1);
  gStore.appendMidi(480, kNoteOff, 60, 0, 1);
  gStore.appendMidi(500, 0xB0, 74, 100, 0);
  gStore.appendMidi(768, kNoteOn, 62, 90, 2);  // zero-length pair
  gStore.appendMidi(768, kNoteOff, 62, 0, 2);
  gStore.appendMidi(900, kNoteOn, 64, 80, 3);
  gStore.appendMidi(1200, kNoteOff, 64, 0, 3);
  gStore.commitPass();
  gStore.beginPass(PassKind::Overdub);
  gStore.appendHide(3);
  gStore.appendMidi(500, 0xB0, 74, 100, 0);  // identical to the Record row
  gStore.appendMidi(1500, kNoteOn + 1, 70, 100, 10);
  gStore.appendMidi(1700, kNoteOff + 1, 70, 0, 10);
  gStore.commitPass();
  gStore.beginPass(PassKind::Overdub);
  gStore.appendHide(10);
  gStore.appendMidi(500, 0xB0, 74, 100, 0);
  gStore.appendMidi(2000, kNoteOn + 2, 72, 100, 20);
  gStore.appendMidi(2100, kNoteOff + 2, 72, 0, 20);
  gStore.commitPass();
  gStore.beginPass(PassKind::Overdub);
  gStore.appendMidi(3000, kNoteOn + 3, 50, 100, 30);
  gStore.appendMidi(3100, kNoteOff + 3, 50, 0, 30);
  gStore.commitPass();
}

void test_compaction_is_lossless_for_source_rows_hides_duplicates_and_zero_length_pairs() {
  buildLosslessFixture();
  const ManifestId before = gStore.pinCurrent();
  const auto sourceBefore = sourceMultiset(gStore, before);
  const auto effectiveBefore = composeAll(before, EffectivePolicy::revision());
  const auto compatBefore = composeAll(before, EffectivePolicy::productionCompat());
  const auto rawBefore = composeAll(before, EffectivePolicy::source());
  TEST_ASSERT_EQUAL(CompactResult::Ok, gStore.beginCompaction(1, 2, CompactionScope::AllowCopies));  // OD1 + OD2
  while (gStore.stepCompaction(3) == CompactStep::Pending) {
  }
  const ManifestId after = gStore.current();
  TEST_ASSERT_EQUAL_UINT8(3, gStore.manifest(after).runCount);
  // Layer 1: the committed source multiset, with each row's source pass serial, is unchanged.
  TEST_ASSERT_TRUE(sourceBefore == sourceMultiset(gStore, after));
  TEST_ASSERT_TRUE(sameRows(rawBefore, composeAll(after, EffectivePolicy::source())));
  // Layer 2: the effective outputs are unchanged under both policies.
  TEST_ASSERT_TRUE(sameRows(effectiveBefore, composeAll(after, EffectivePolicy::revision())));
  TEST_ASSERT_TRUE(sameRows(compatBefore, composeAll(after, EffectivePolicy::productionCompat())));
  // The hide between OD1 and OD2 still works inside the merged run: OD1's note 70 is gone, OD2's note 72 is not.
  bool has70 = false, has72 = false;
  for (const PassEvent& e : effectiveBefore) {
    has70 = has70 || e.data1 == 70;
    has72 = has72 || e.data1 == 72;
  }
  TEST_ASSERT_FALSE(has70);
  TEST_ASSERT_TRUE(has72);
  gStore.unpin(before);
}

void test_effective_policies_are_explicit_projections_over_lossless_source() {
  buildLosslessFixture();
  const ManifestId m = gStore.current();
  auto count = [](const std::vector<PassEvent>& rows, uint8_t status, uint8_t data1) {
    uint32_t n = 0;
    for (const PassEvent& e : rows) n += (e.status == status && e.data1 == data1) ? 1 : 0;
    return n;
  };
  const auto source = composeAll(m, EffectivePolicy::source());
  const auto revision = composeAll(m, EffectivePolicy::revision());
  const auto compat = composeAll(m, EffectivePolicy::productionCompat());
  // B.3a: three identical CC74=100 rows stay three in source and in the revision view; production collapses to one.
  TEST_ASSERT_EQUAL_UINT32(3, count(source, 0xB0, 74));
  TEST_ASSERT_EQUAL_UINT32(3, count(revision, 0xB0, 74));
  TEST_ASSERT_EQUAL_UINT32(1, count(compat, 0xB0, 74));
  // B.3b: the zero-length pair stays in source and revision; production compat suppresses it.
  TEST_ASSERT_EQUAL_UINT32(1, count(revision, kNoteOn, 62));
  TEST_ASSERT_EQUAL_UINT32(0, count(compat, kNoteOn, 62));
  TEST_ASSERT_EQUAL_UINT32(0, count(compat, kNoteOff, 62));
  // Hidden rows are absent from the revision view but present in source.
  TEST_ASSERT_EQUAL_UINT32(1, count(source, kNoteOn, 64));
  TEST_ASSERT_EQUAL_UINT32(0, count(revision, kNoteOn, 64));
}

void test_planner_takes_only_closed_ranges_within_the_row_cap() {
  using PlanCap = Capacity<512, 32, 24, 64, 24, 12, /*history*/ 2>;
  static Store<PlanCap> plan;
  plan.reset();
  plan.beginPass(PassKind::Record, 4 * kBarTicks);
  plan.appendMidi(0, kNoteOn, 60, 100, 1);
  plan.appendMidi(96, kNoteOff, 60, 0, 1);
  plan.commitPass();
  TEST_ASSERT_EQUAL(CompactResult::NothingToCompact, plan.planCompaction(1000));
  for (uint32_t i = 0; i < 6; ++i) {
    plan.beginPass(PassKind::Overdub);
    for (uint32_t k = 0; k < 10; ++k) plan.appendMidi(100 + i * 50 + k, kNoteOn, 70, 100, 100 + i * 10 + k);
    plan.commitPass();
  }
  // History holds revisions 6 and 7: overdubs 1..5 are closed; a 25-row cap admits only two of them at a time.
  TEST_ASSERT_EQUAL(CompactResult::Ok, plan.planCompaction(25));
  while (plan.stepCompaction(8) == CompactStep::Pending) {
  }
  TEST_ASSERT_EQUAL_UINT8(6, plan.manifest(plan.current()).runCount);  // R, [OD1+OD2], OD3, OD4, OD5, OD6
  // The next plan does not re-merge the 20-row merged run with another overdub past the cap; it starts after it.
  TEST_ASSERT_EQUAL(CompactResult::Ok, plan.planCompaction(25));
  while (plan.stepCompaction(8) == CompactStep::Pending) {
  }
  TEST_ASSERT_EQUAL_UINT8(5, plan.manifest(plan.current()).runCount);  // R, [OD1+OD2], [OD3+OD4], OD5, OD6
}

int main(int /*argc*/, char** /*argv*/) {
  UNITY_BEGIN();
  RUN_TEST(test_record_commit_is_revision_one_and_tick_ordered);
  RUN_TEST(test_every_revision_is_exactly_its_committed_passes_and_old_pins_survive);
  RUN_TEST(test_two_bar_windows_of_long_take_contain_record_and_all_overdubs);
  RUN_TEST(test_uncommitted_overdub_never_changes_current);
  RUN_TEST(test_hide_masks_earlier_runs_only_and_older_revision_keeps_the_note);
  RUN_TEST(test_undo_redo_select_existing_manifests_and_commit_after_undo_reclaims_redo_branch);
  RUN_TEST(test_compaction_keeps_revision_content_and_reclaims_after_last_reader);
  RUN_TEST(test_compaction_reclaims_superseded_runs_when_no_revision_references_them);
  RUN_TEST(test_full_manifest_denies_overdub_admission_until_compaction);
  RUN_TEST(test_page_exhaustion_is_a_named_denial_not_a_hang);
  RUN_TEST(test_zero_heap_after_construction);
  RUN_TEST(test_reader_of_discarded_redo_branch_survives_until_released);
  RUN_TEST(test_compaction_and_slot_reuse_change_identity_not_revision);
  RUN_TEST(test_equal_tick_rows_follow_production_class_then_append_order);
  RUN_TEST(test_equal_tick_class_outranks_run_order);
  RUN_TEST(test_page_exhaustion_mid_pass_latches_and_leaves_committed_revision_untouched);
  RUN_TEST(test_pass_spans_non_contiguous_free_pages);
  RUN_TEST(test_capture_shaped_take_commits_without_sort);
  RUN_TEST(test_segment_budget_overflow_falls_back_to_counted_sort);
  RUN_TEST(test_stepped_compaction_matches_and_is_bounded_per_step);
  RUN_TEST(test_undo_during_compaction_supersedes_it_and_conserves_pages);
  RUN_TEST(test_overdub_committed_during_compaction_is_kept_on_publish);
  RUN_TEST(test_compaction_refuses_a_range_history_still_names_and_admits_a_closed_one);
  RUN_TEST(test_two_slots_share_one_page_pool);
  RUN_TEST(test_large_run_uses_directory_pages);
  RUN_TEST(test_compaction_is_lossless_for_source_rows_hides_duplicates_and_zero_length_pairs);
  RUN_TEST(test_effective_policies_are_explicit_projections_over_lossless_source);
  RUN_TEST(test_planner_takes_only_closed_ranges_within_the_row_cap);
  return UNITY_END();
}
