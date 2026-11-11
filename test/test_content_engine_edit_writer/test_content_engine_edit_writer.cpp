//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// fixed-memory-engine E4.5: the NOTE_EDIT edit writer (owner decision 2026-11-05: hear each edit; the session rewrites
// one edit run). Every apply / undo / redo reserves a fresh edit run at acceptance (refused by name only when content is
// full), the rebuild writes it in grains (hides, then replacements in tick order, noteIds kept) and publishes base runs
// + edit run as the session's one History state. E: steps live in the writer's log (99, oldest forgotten). Leaving keeps
// one U: step; abandon returns to the start. Effective content is checked by composing the slot's current manifest
// with the production composer and edit index, and a random sequence is checked against a reference model.

#include <unity.h>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <new>
#include <vector>

#include "ContentEngine/Composition/SpanWindowComposer.h"
#include "ContentEngine/NoteEdit/NoteEditWriter.h"
#include "ContentEngine/EngineCore.h"
#include "ContentEngine/History/SlotUndoHistory.h"
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

using Pool = PagePool<1024, 64>;  // 576 pages are the edit-index partition
using Runs = RunTable<64, Pool>;
using Manifests = ManifestTable<64, 63>;
using Hist = History<Manifests, Runs, 8, 20>;
using Res = RowReservations<Pool, Runs>;
using Writer = EditWriter<Pool, Runs, Manifests, Hist, /*selection*/ 16, /*steps*/ 99, /*log*/ 64>;
using TestComposer = Composer<Pool, Runs>;

static Pool gPool;
static Runs gRuns;
static Manifests gManifests;
static Hist gH;
static Res gRes;
static EditIndexStore<Pool, Runs> gIndex;
static TestComposer gC;
static SpanWindow<3072> gWin;
static Writer gW;
static EditChange gLog[64];

constexpr uint32_t kLoop = 768 * 8;
constexpr uint8_t kNotes = 12;  // <= 16 sounding at once whatever the edits do

static EditNote gBase[kNotes];
static ManifestId gBaseManifest;

void setUp() {
  gPool.init();
  gRuns.init();
  gManifests.init();
  TEST_ASSERT_TRUE(gIndex.init(gPool, gRuns));
  gRes.init(gPool, gRuns, 0);  // the index store took its partition at init
  gH.init(gManifests, gRuns);
  gC.init(gPool, gRuns, &gIndex);
  gW.init(gPool, gRuns, gManifests, gRes, gH, gLog);
  // The take: 12 notes, distinct pitches, spread over the loop.
  const RunId r = gRuns.allocate(0, RunKind::kRecord);
  struct Ev {
    uint32_t tick;
    bool on;
    uint8_t i;
  };
  std::vector<Ev> ev;
  for (uint8_t i = 0; i < kNotes; ++i) {
    gBase[i] = EditNote{100u + i, 400u * i + 10u, 120u, static_cast<uint8_t>(40 + i), 90, 0, 0};
    ev.push_back({gBase[i].tick, true, i});
    ev.push_back({gBase[i].tick + gBase[i].length, false, i});
  }
  std::stable_sort(ev.begin(), ev.end(), [](const Ev& a, const Ev& b) { return a.tick < b.tick || (a.tick == b.tick && !a.on && b.on); });
  for (const Ev& e : ev) {
    PassEvent p{};
    p.tick = e.tick;
    p.noteId = gBase[e.i].noteId;
    p.status = e.on ? 0x90 : 0x80;
    p.data1 = gBase[e.i].pitch;
    p.data2 = e.on ? 90 : 0;
    TEST_ASSERT_EQUAL(AppendResult::Ok, gRuns.append(r, p, gPool));
  }
  gRuns.seal(r);
  gBaseManifest = gManifests.create(0, 1, kLoop, &r, 1, gRuns);
  gRuns.release(r);
  gH.commit(0, gBaseManifest);
}
void tearDown() {}

struct Played {
  uint32_t tick, noteId, length;
  uint8_t pitch, velocity;
  bool operator==(const Played& o) const {
    return tick == o.tick && noteId == o.noteId && length == o.length && pitch == o.pitch && velocity == o.velocity;
  }
};
static bool byTick(const Played& a, const Played& b) { return a.tick < b.tick || (a.tick == b.tick && a.noteId < b.noteId); }

/// The slot's effective notes: its current manifest composed for one cycle.
static std::vector<Played> heard() {
  const ManifestId m = gH.current(0);
  const auto& man = gManifests.manifest(m);
  SourceRun src[63];
  for (uint8_t i = 0; i < man.runCount; ++i) src[i] = {man.runs[i], gRuns.run(man.runs[i]).rowCount};
  TEST_ASSERT_TRUE(gC.begin(src, man.runCount, kLoop, man.identity));
  std::vector<Played> out;
  while (gC.composeNext(gWin, kLoop / 4))  // window boundaries depend on every run being in tick order
    for (uint16_t k = 0; k < gWin.count; ++k) {
      const SpanEntry& e = gWin.entries[k];
      if (e.terminal) {  // the end of a note started in an earlier window
        for (auto& p : out)
          if (p.noteId == e.noteId && p.length == kOpenLength) p.length = e.tick - p.tick;
        continue;
      }
      if ((e.status & 0xF0) != 0x90) continue;
      out.push_back({e.tick, e.noteId, e.length, e.data1, e.data2});
    }
  gC.end();
  std::sort(out.begin(), out.end(), byTick);
  return out;
}
/// What the selection says the slot should play (no causing note: the overlap tests write the results into `cur`).
static std::vector<Played> expected(const EditNote* cur) {
  std::vector<Played> out;
  for (uint8_t i = 0; i < kNotes; ++i)
    if (!cur[i].deleted) out.push_back({cur[i].tick, cur[i].noteId, cur[i].length, cur[i].pitch, cur[i].velocity});
  std::sort(out.begin(), out.end(), byTick);
  return out;
}

/// The session's edit run, if any: HideNote rows first, then rows in tick order, NoteOff before NoteOn at a tick.
static void checkEditRunOrder() {
  const auto& man = gManifests.manifest(gH.current(0));
  const RunId r = man.runs[man.runCount - 1];
  if (gRuns.run(r).kind != RunKind::kEdit) return;
  const uint32_t rows = gRuns.run(r).rowCount;
  uint32_t i = 0;
  while (i < rows && gRuns.rowAt(r, i, gPool).status == kHideNoteStatus) ++i;
  for (uint32_t k = i; k < rows; ++k) {
    const PassEvent& e = gRuns.rowAt(r, k, gPool);
    TEST_ASSERT_NOT_EQUAL(kHideNoteStatus, e.status);
    if (k == i) continue;
    const PassEvent& p = gRuns.rowAt(r, k - 1, gPool);
    TEST_ASSERT_TRUE(p.tick < e.tick || (p.tick == e.tick && !((p.status & 0xF0) == 0x90 && (e.status & 0xF0) == 0x80)));
  }
}
static void expectHeard(const EditNote* cur) {
  checkEditRunOrder();
  const auto want = expected(cur);
  const auto got = heard();
  TEST_ASSERT_EQUAL_UINT32(want.size(), got.size());
  for (size_t i = 0; i < want.size(); ++i) TEST_ASSERT_TRUE(want[i] == got[i]);
}
static uint32_t finish(uint32_t maxRows = 8) {
  uint32_t grains = 0;
  while (gW.step(maxRows)) TEST_ASSERT_TRUE(++grains < 1000);
  return grains + 1;
}
static void drainReclaim() {
  while (gRuns.reclaimPendingRuns() != 0) gRuns.reclaim(gPool, 64);
}
static EditChange moved(uint16_t i, uint32_t tick, uint32_t length, uint8_t pitch, uint8_t velocity, bool deleted = false) {
  EditNote v = gBase[i];
  v.tick = tick;
  v.length = length;
  v.pitch = pitch;
  v.velocity = velocity;
  v.deleted = deleted ? 1 : 0;
  return EditChange{i, v};
}

// An edit is audible once its run is built: the original is hidden and the replacement plays, same noteId.
void test_edit_is_heard_after_its_rebuild() {
  TEST_ASSERT_EQUAL(EditResult::Ok, gW.begin(0, gBase, kNotes));
  const EditChange c = moved(3, 2000, 300, 110, 64);
  TEST_ASSERT_EQUAL(EditResult::Ok, gW.apply(&c, 1));
  TEST_ASSERT_EQUAL_UINT16(gBaseManifest, gH.current(0));  // not yet published
  const uint32_t grains = finish(2);
  TEST_ASSERT_TRUE(grains <= 16);  // 12 notes gathered, 1 + 1 sifts, 2 rows: 2 units per grain
  TEST_ASSERT_NOT_EQUAL(gBaseManifest, gH.current(0));
  EditNote cur[kNotes];
  for (uint8_t i = 0; i < kNotes; ++i) cur[i] = gBase[i];
  cur[3] = c.value;
  expectHeard(cur);
  TEST_ASSERT_EQUAL_UINT8(1, gW.depth());
  TEST_ASSERT_TRUE(gRes.check());
}

// Delete, E: undo back to the start (no run needed), E: redo.
void test_delete_and_session_undo_redo() {
  gW.begin(0, gBase, kNotes);
  const EditChange d = moved(5, gBase[5].tick, gBase[5].length, gBase[5].pitch, gBase[5].velocity, true);
  gW.apply(&d, 1);
  finish();
  EditNote cur[kNotes];
  for (uint8_t i = 0; i < kNotes; ++i) cur[i] = gBase[i];
  cur[5].deleted = 1;
  expectHeard(cur);
  TEST_ASSERT_EQUAL(EditResult::Ok, gW.undo());
  TEST_ASSERT_FALSE(gW.building());
  TEST_ASSERT_EQUAL_UINT16(gBaseManifest, gH.current(0));  // nothing edited: the start, at once
  expectHeard(gBase);
  TEST_ASSERT_EQUAL(EditResult::NothingToUndo, gW.undo());
  TEST_ASSERT_EQUAL(EditResult::Ok, gW.redo());
  finish();
  expectHeard(cur);
  TEST_ASSERT_EQUAL(EditResult::NothingToRedo, gW.redo());
}

// Leaving NOTE_EDIT keeps the edits as one U: step.
void test_leave_makes_one_undo_step() {
  gW.begin(0, gBase, kNotes);
  for (uint16_t i = 0; i < 3; ++i) {
    const EditChange c = moved(i, gBase[i].tick + 50, 80, gBase[i].pitch, 70);
    gW.apply(&c, 1);
    finish();
  }
  gW.leave();
  TEST_ASSERT_FALSE(gW.editing());
  const ManifestId edited = gH.current(0);
  TEST_ASSERT_EQUAL_UINT8(2, gH.undoDepth(0));  // the take, the edit session
  TEST_ASSERT_TRUE(gH.undo(0));
  TEST_ASSERT_EQUAL_UINT16(gBaseManifest, gH.current(0));
  TEST_ASSERT_TRUE(gH.redo(0));
  TEST_ASSERT_EQUAL_UINT16(edited, gH.current(0));
}

// Leave during a rebuild completes it first; abandon during a rebuild drops it and restores the start.
void test_leave_and_abandon_during_a_rebuild() {
  gW.begin(0, gBase, kNotes);
  EditChange c = moved(1, 3000, 90, 100, 50);
  gW.apply(&c, 1);
  gW.step(1);
  gW.leave();
  TEST_ASSERT_TRUE(gW.editing());  // finishing
  finish();
  TEST_ASSERT_FALSE(gW.editing());
  EditNote cur[kNotes];
  for (uint8_t i = 0; i < kNotes; ++i) cur[i] = gBase[i];
  cur[1] = c.value;
  expectHeard(cur);
  // A second session, abandoned mid-rebuild.
  const ManifestId before = gH.current(0);
  for (uint8_t i = 0; i < kNotes; ++i) cur[i].noteId = gBase[i].noteId;
  TEST_ASSERT_EQUAL(EditResult::Ok, gW.begin(0, cur, kNotes));
  c = moved(2, 100, 90, 101, 50);
  gW.apply(&c, 1);
  gW.step(1);
  gW.abandon();
  TEST_ASSERT_FALSE(gW.editing());
  TEST_ASSERT_EQUAL_UINT16(before, gH.current(0));
  expectHeard(cur);
  drainReclaim();
  TEST_ASSERT_TRUE(gRes.check());
  TEST_ASSERT_TRUE(checkConservation(gPool, gRuns, gManifests));
}

// A newer action supersedes a running rebuild.
void test_newer_edit_supersedes_the_rebuild() {
  gW.begin(0, gBase, kNotes);
  const EditChange a = moved(4, 1000, 100, 104, 60);
  gW.apply(&a, 1);
  gW.step(1);
  const EditChange b = moved(4, 1200, 100, 105, 61);
  gW.apply(&b, 1);
  finish();
  TEST_ASSERT_EQUAL_UINT32(1, gW.stats().superseded);
  EditNote cur[kNotes];
  for (uint8_t i = 0; i < kNotes; ++i) cur[i] = gBase[i];
  cur[4] = b.value;
  expectHeard(cur);
  TEST_ASSERT_EQUAL_UINT8(2, gW.depth());
  drainReclaim();
  TEST_ASSERT_TRUE(gRes.check());
}

// With content full the action is refused by name and nothing changes.
void test_full_content_refuses_the_action() {
  gW.begin(0, gBase, kNotes);
  const RunId filler = gRuns.allocate(1, RunKind::kRecord);
  PassEvent p{};
  p.status = 0xB0;
  while (gRes.accept(filler, p, 0) == Admission::Ok) {
  }
  const EditNote before = gW.note(6);
  const EditChange c = moved(6, 10, 10, 60, 60);
  TEST_ASSERT_EQUAL(EditResult::DeniedCapacity, gW.apply(&c, 1));
  TEST_ASSERT_TRUE(gW.note(6) == before);
  TEST_ASSERT_EQUAL_UINT8(0, gW.depth());
  TEST_ASSERT_FALSE(gW.building());
  TEST_ASSERT_EQUAL_UINT16(gBaseManifest, gH.current(0));
  TEST_ASSERT_TRUE(gRes.check());
}

// A step lists each note once; a duplicate is refused by name and nothing changes.
void test_duplicate_note_in_one_step_is_refused() {
  gW.begin(0, gBase, kNotes);
  const EditChange two[] = {moved(2, 100, 20, 70, 50), moved(2, 200, 20, 70, 50)};
  TEST_ASSERT_EQUAL(EditResult::DeniedSelection, gW.apply(two, 2));
  TEST_ASSERT_TRUE(gW.note(2) == gBase[2]);
  TEST_ASSERT_EQUAL_UINT8(0, gW.depth());
  TEST_ASSERT_FALSE(gW.building());
}

// The oldest E: steps are forgotten when the log is full (here by entries: 64 single-note steps).
void test_log_forgets_the_oldest_steps() {
  gW.begin(0, gBase, kNotes);
  for (uint32_t s = 0; s < 120; ++s) {
    const EditChange c = moved(static_cast<uint16_t>(s % kNotes), 10 + s * 7, 50, gBase[s % kNotes].pitch, 40);
    TEST_ASSERT_EQUAL(EditResult::Ok, gW.apply(&c, 1));
    finish(64);
    drainReclaim();
  }
  TEST_ASSERT_EQUAL_UINT8(64, gW.depth());  // the log holds 64 single-note entries: 64 steps
  TEST_ASSERT_EQUAL_UINT32(120 - 64, gW.stats().forgotSteps);
  uint32_t undos = 0;
  while (gW.undo() == EditResult::Ok) {
    drainReclaim();  // the background reclaim job frees each superseded rebuild
    TEST_ASSERT_TRUE(++undos < 200);
  }
  TEST_ASSERT_EQUAL_UINT32(64, undos);
  finish(64);
  // The state after step 56 (the oldest kept step undone): steps 0..55 applied.
  EditNote cur[kNotes];
  for (uint8_t i = 0; i < kNotes; ++i) cur[i] = gBase[i];
  for (uint32_t s = 0; s < 56; ++s) cur[s % kNotes] = moved(static_cast<uint16_t>(s % kNotes), 10 + s * 7, 50, gBase[s % kNotes].pitch, 40).value;
  expectHeard(cur);
}

// ---- Reference model: a stack of selection snapshots with the same step / entry limits -----------------------------
// Overlap (owner 2026-11-07, legacy `resolveConstrainedGeometry`): the selected, edited note A is never changed; a note
// B of its pitch that starts inside A is hidden, one that starts before A and sounds into it ends at A's start, edges
// that only touch change nothing — live while A stays selected, so moving A on or undoing brings B back. Base: note i at
// 400 i + 10, 120 long, pitch 40 + i; the loop is 6144.
void test_edited_notes_hide_and_shorten_same_pitch_notes() {
  TEST_ASSERT_EQUAL(EditResult::Ok, gW.begin(0, gBase, kNotes));
  gW.cause(3);  // A, the selected note
  EditNote cur[kNotes];
  for (uint8_t i = 0; i < kNotes; ++i) cur[i] = gBase[i];
  // A (note 3) onto pitch 45 from 1990, 100 long: B (note 5, 2010–2130) starts inside — hidden.
  EditChange c = moved(3, 1990, 100, 45, 90);
  TEST_ASSERT_EQUAL(EditResult::Ok, gW.apply(&c, 1));
  finish();
  cur[3] = c.value;
  cur[5].deleted = 1;
  expectHeard(cur);
  // A from 2050: B starts before and sounds into it — it ends at 2050.
  c = moved(3, 2050, 100, 45, 90);
  gW.apply(&c, 1);
  finish();
  cur[3] = c.value;
  cur[5] = gBase[5];
  cur[5].length = 2050 - 2010;
  expectHeard(cur);
  // A from 2130, where B ends: they only touch — B as it was.
  c = moved(3, 2130, 100, 45, 90);
  gW.apply(&c, 1);
  finish();
  cur[3] = c.value;
  cur[5] = gBase[5];
  expectHeard(cur);
  // Back to the hide; undo brings B back, redo hides it again.
  c = moved(3, 2000, 100, 45, 90);
  gW.apply(&c, 1);
  finish();
  cur[3] = c.value;
  cur[5].deleted = 1;
  expectHeard(cur);
  TEST_ASSERT_EQUAL(EditResult::Ok, gW.undo());
  finish();
  cur[3] = moved(3, 2130, 100, 45, 90).value;
  cur[5] = gBase[5];
  expectHeard(cur);
  TEST_ASSERT_EQUAL(EditResult::Ok, gW.redo());
  finish();
  cur[3] = c.value;
  cur[5].deleted = 1;
  expectHeard(cur);
  // A elsewhere, on its own pitch: B is back.
  c = moved(3, gBase[3].tick, gBase[3].length, gBase[3].pitch, 90);
  gW.apply(&c, 1);
  finish();
  cur[3] = c.value;
  cur[5] = gBase[5];
  expectHeard(cur);
  TEST_ASSERT_TRUE(gRes.check());
}

// Across the loop's wrap: A (note 1) on pitch 40 from 6100, 100 long, sounds to 56 — B (note 0, 10–130) starts inside
// its wrapped part: hidden. A from 50: B starts before, ends at 50.
void test_overlap_across_the_loop_wrap() {
  gW.begin(0, gBase, kNotes);
  gW.cause(1);
  EditNote cur[kNotes];
  for (uint8_t i = 0; i < kNotes; ++i) cur[i] = gBase[i];
  EditChange c = moved(1, 6100, 100, 40, 90);
  gW.apply(&c, 1);
  finish();
  cur[1] = c.value;
  cur[1].length = kOpenLength;  // one composed cycle: A's end is in the next
  cur[0].deleted = 1;
  expectHeard(cur);
  c = moved(1, 50, 100, 40, 90);
  gW.apply(&c, 1);
  finish();
  cur[1] = c.value;
  cur[0] = gBase[0];
  cur[0].length = 40;
  expectHeard(cur);
}

// The edges: B starting where A starts is hidden; B starting where A ends is untouched; a deleted A is no longer
// the causing note; a created note undone (deleted again, unedited) is not a target.
void test_overlap_edges_deleted_and_created_notes() {
  gW.begin(0, gBase, kNotes);
  gW.cause(3);
  EditNote cur[kNotes];
  for (uint8_t i = 0; i < kNotes; ++i) cur[i] = gBase[i];
  EditChange c = moved(3, 2010, 50, 45, 90);  // A starts with B (note 5, 2010–2130)
  gW.apply(&c, 1);
  finish();
  cur[3] = c.value;
  cur[5].deleted = 1;
  expectHeard(cur);
  c = moved(3, 1910, 100, 45, 90);  // A ends where B starts
  gW.apply(&c, 1);
  finish();
  cur[3] = c.value;
  cur[5] = gBase[5];
  expectHeard(cur);
  // A over B, deleted — with another edited note live on B's pitch far away (so B's pitch is looked at): B as it was.
  EditChange two[2] = {moved(3, 1990, 100, 45, 90, true), moved(9, 4000, 50, 45, 90)};
  gW.apply(two, 2);
  finish();
  cur[3] = two[0].value;
  cur[9] = two[1].value;
  expectHeard(cur);
  c = moved(9, gBase[9].tick, gBase[9].length, gBase[9].pitch, gBase[9].velocity);  // note 9 back as it was
  gW.apply(&c, 1);
  finish();
  cur[9] = gBase[9];
  // A over B again; a note created inside A on its pitch, then undone (deleted, unedited): nothing for it.
  c = moved(3, 1990, 300, 45, 90);
  gW.apply(&c, 1);
  finish();
  uint16_t at = 0;
  TEST_ASSERT_EQUAL(EditResult::Ok, gW.create(EditNote{999, 2100, 40, 45, 80, 0, 0}, at));
  finish();
  TEST_ASSERT_EQUAL(EditResult::Ok, gW.undo());
  finish();
  cur[3] = c.value;
  cur[5].deleted = 1;
  expectHeard(cur);
  TEST_ASSERT_TRUE(gRes.check());
}

// Notes of one pitch that overlap in the recording stay as recorded: only the selected, edited note causes.
void test_unedited_overlaps_are_left_alone() {
  const RunId r = gRuns.allocate(1, RunKind::kRecord);
  auto row = [](uint32_t tick, uint32_t id, uint8_t status, uint8_t pitch, uint8_t vel) {
    PassEvent e{};
    e.tick = tick;
    e.noteId = id;
    e.status = status;
    e.data1 = pitch;
    e.data2 = vel;
    return e;
  };
  const PassEvent rows[] = {row(100, 501, 0x90, 50, 90), row(200, 502, 0x90, 50, 90), row(300, 501, 0x80, 50, 0),
                            row(400, 502, 0x80, 50, 0),  row(1000, 503, 0x90, 60, 90), row(1100, 503, 0x80, 60, 0)};
  for (const PassEvent& e : rows) TEST_ASSERT_EQUAL(AppendResult::Ok, gRuns.append(r, e, gPool));
  gRuns.seal(r);
  const ManifestId m = gManifests.create(1, 1, kLoop, &r, 1, gRuns);
  gRuns.release(r);
  gH.commit(1, m);
  const EditNote sel[] = {{501, 100, 200, 50, 90, 0, 0}, {502, 200, 200, 50, 90, 0, 0}, {503, 1000, 100, 60, 90, 0, 0}};
  TEST_ASSERT_EQUAL(EditResult::Ok, gW.begin(1, sel, 3));
  gW.cause(2);
  EditNote v = sel[2];
  v.tick = 1200;
  v.pitch = 50;  // onto their pitch, overlapping neither
  const EditChange c{2, v};
  TEST_ASSERT_EQUAL(EditResult::Ok, gW.apply(&c, 1));
  finish();
  const auto& man = gManifests.manifest(gH.current(1));
  TEST_ASSERT_EQUAL_UINT32(3, gRuns.run(man.runs[man.runCount - 1]).rowCount);  // 503's hide and its two rows only
  gW.cause(0);  // 501 selected, unedited: it causes nothing on 502
  finish();
  const auto& man2 = gManifests.manifest(gH.current(1));
  TEST_ASSERT_EQUAL_UINT32(3, gRuns.run(man2.runs[man2.runCount - 1]).rowCount);
  TEST_ASSERT_TRUE(gRes.check());
}

// An edited note is a target too; deselecting A settles its results — B keeps them when A moves on later — and undoing
// A's last step (the settled one) brings B back (owner 2026-11-07, DEC-030 sticky).
void test_settle_keeps_the_results_and_edited_notes_are_targets() {
  gW.begin(0, gBase, kNotes);
  EditNote cur[kNotes];
  for (uint8_t i = 0; i < kNotes; ++i) cur[i] = gBase[i];
  // B (note 5) edited first: velocity 70.
  gW.cause(5);
  EditChange c = moved(5, gBase[5].tick, gBase[5].length, gBase[5].pitch, 70);
  gW.apply(&c, 1);
  finish();
  cur[5] = c.value;
  // A (note 3) selected and moved over B's start: B, though edited, ends at A's start.
  gW.settle();
  gW.cause(3);
  c = moved(3, 2050, 100, 45, 90);
  gW.apply(&c, 1);
  finish();
  cur[3] = c.value;
  cur[5].length = 40;
  expectHeard(cur);
  // Another selection without settling: A's results go (a rebuild, no edit), and come back with A.
  gW.cause(Writer::kNoCause);
  finish();
  cur[5].length = gBase[5].length;
  expectHeard(cur);
  gW.cause(3);
  finish();
  cur[5].length = 40;
  expectHeard(cur);
  const uint8_t depth = gW.depth();
  // Deselect A (another note selected): settled, no new step.
  gW.settle();
  gW.cause(7);
  finish();
  expectHeard(cur);
  TEST_ASSERT_EQUAL_UINT8(depth, gW.depth());
  TEST_ASSERT_EQUAL_UINT32(40, gW.note(5).length);  // B's own state now
  // A selected again and moved away: B stays shortened (sticky).
  gW.settle();
  gW.cause(3);
  c = moved(3, 3000, 100, 45, 90);
  gW.apply(&c, 1);
  finish();
  cur[3] = c.value;
  expectHeard(cur);
  // Undo the move away, then the settled step: A back over B's start, then A and B as before it.
  TEST_ASSERT_EQUAL(EditResult::Ok, gW.undo());
  finish();
  cur[3] = moved(3, 2050, 100, 45, 90).value;
  expectHeard(cur);
  TEST_ASSERT_EQUAL(EditResult::Ok, gW.undo());
  finish();
  cur[3] = gBase[3];
  cur[5].length = gBase[5].length;
  expectHeard(cur);
  TEST_ASSERT_TRUE(gRes.check());
}

struct RefStep {
  std::vector<EditNote> before;  // the selection before the step
  uint16_t entries;
};
void test_random_sequences_match_the_reference_model() {
  gW.begin(0, gBase, kNotes);
  std::vector<EditNote> cur(gBase, gBase + kNotes);
  std::vector<RefStep> undo, redo;
  uint32_t x = 2026;
  for (uint32_t op = 0; op < 3000; ++op) {
    x = x * 1103515245u + 12345u;
    const uint32_t kind = (x >> 20) % 10;
    if (kind < 6) {
      const uint16_t n = static_cast<uint16_t>(1 + (x >> 8) % 4);
      EditChange ch[4];
      std::vector<EditNote> next = cur;
      const uint16_t start = static_cast<uint16_t>((x >> 3) % kNotes);
      for (uint16_t k = 0; k < n; ++k) {  // distinct notes: a step changes each note once
        x = x * 1103515245u + 12345u;
        const uint16_t i = static_cast<uint16_t>((start + k * 5) % kNotes);
        const bool del = ((x >> 4) % 7) == 0;
        // A third of the edits land on another note's pitch near it (overlap); the others on a pitch of their own.
        const bool onto = ((x >> 14) % 3) == 0;
        const uint16_t other = static_cast<uint16_t>((i + 1 + (x >> 16) % (kNotes - 1)) % kNotes);
        const uint32_t tick = onto ? (gBase[other].tick + 60u + (x >> 12) % 160u) % (kLoop - 300) : (x >> 12) % (kLoop - 300);
        ch[k] = moved(i, tick, 1 + (x >> 3) % 200, onto ? gBase[other].pitch : static_cast<uint8_t>(60 + i),
                      static_cast<uint8_t>(1 + (x >> 5) % 126), del);
        next[i] = ch[k].value;
      }
      TEST_ASSERT_EQUAL(EditResult::Ok, gW.apply(ch, n));
      redo.clear();
      uint16_t total = n;
      for (const auto& s : undo) total = static_cast<uint16_t>(total + s.entries);
      while (undo.size() == 99 || total > 64) {
        total = static_cast<uint16_t>(total - undo.front().entries);
        undo.erase(undo.begin());
      }
      undo.push_back({cur, n});
      cur = next;
    } else if (kind < 8) {
      const EditResult r = gW.undo();
      if (undo.empty()) {
        TEST_ASSERT_EQUAL(EditResult::NothingToUndo, r);
      } else {
        TEST_ASSERT_EQUAL(EditResult::Ok, r);
        redo.push_back({cur, undo.back().entries});
        cur = undo.back().before;
        undo.pop_back();
      }
    } else {
      const EditResult r = gW.redo();
      if (redo.empty()) {
        TEST_ASSERT_EQUAL(EditResult::NothingToRedo, r);
      } else {
        TEST_ASSERT_EQUAL(EditResult::Ok, r);
        undo.push_back({cur, redo.back().entries});
        cur = redo.back().before;
        redo.pop_back();
      }
    }
    drainReclaim();  // the background reclaim job
    if ((x >> 9) % 3 != 0) {  // sometimes let the next action supersede a running rebuild
      finish(1 + (x >> 2) % 40);
      drainReclaim();
      if (op % 7 == 0) expectHeard(cur.data());
      TEST_ASSERT_TRUE(gRes.check());
    }
    TEST_ASSERT_EQUAL_UINT8(undo.size(), gW.depth());
    TEST_ASSERT_EQUAL_UINT8(redo.size(), gW.redoDepth());
    for (uint8_t i = 0; i < kNotes; ++i) TEST_ASSERT_TRUE(gW.note(i) == cur[i]);
  }
  finish();
  expectHeard(cur.data());
  gW.leave();
  drainReclaim();
  TEST_ASSERT_TRUE(checkConservation(gPool, gRuns, gManifests));
}

void test_no_heap_and_production_size() {
  gW.begin(0, gBase, kNotes);
  gTrap = true;
  const unsigned long before = gHeapAllocations;
  for (uint32_t s = 0; s < 50; ++s) {
    const EditChange c = moved(static_cast<uint16_t>(s % kNotes), 10 + s, 30, 70, 40);
    gW.apply(&c, 1);
    if (s % 3 == 0) gW.undo();
    while (gW.step(16)) {
    }
    drainReclaim();
  }
  gW.leave();
  gTrap = false;
  TEST_ASSERT_EQUAL_UINT32(0, gHeapAllocations - before);
  using Production = EditWriter<ProductionPool, ProductionRuns, ProductionManifests, History<ProductionManifests, ProductionRuns>>;
  char line[80];
  snprintf(line, sizeof line, "production EditWriter %u B", static_cast<unsigned>(sizeof(Production)));
  TEST_MESSAGE(line);
  TEST_ASSERT_TRUE(sizeof(Production) <= 65536u);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_edit_is_heard_after_its_rebuild);
  RUN_TEST(test_delete_and_session_undo_redo);
  RUN_TEST(test_leave_makes_one_undo_step);
  RUN_TEST(test_leave_and_abandon_during_a_rebuild);
  RUN_TEST(test_newer_edit_supersedes_the_rebuild);
  RUN_TEST(test_full_content_refuses_the_action);
  RUN_TEST(test_duplicate_note_in_one_step_is_refused);
  RUN_TEST(test_log_forgets_the_oldest_steps);
  RUN_TEST(test_edited_notes_hide_and_shorten_same_pitch_notes);
  RUN_TEST(test_overlap_across_the_loop_wrap);
  RUN_TEST(test_overlap_edges_deleted_and_created_notes);
  RUN_TEST(test_unedited_overlaps_are_left_alone);
  RUN_TEST(test_settle_keeps_the_results_and_edited_notes_are_targets);
  RUN_TEST(test_random_sequences_match_the_reference_model);
  RUN_TEST(test_no_heap_and_production_size);
  return UNITY_END();
}
