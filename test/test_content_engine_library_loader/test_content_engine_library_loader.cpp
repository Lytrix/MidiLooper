//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// Sets browser slice 2a (DEC-259): loading a milestone and returning. A load writes the working set as the checkpoint
// first (its failure changes nothing), then replaces every slot with the milestone's — histories included; a load that
// fails after that is undone from the checkpoint; return brings the checkpoint back. No heap.

#include <unity.h>

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <map>
#include <new>
#include <vector>

#include "ContentEngine/EngineCore.h"
#include "ContentEngine/History/SlotUndoHistory.h"
#include "ContentEngine/Storage/LibraryLoader.h"
#include "ContentEngine/Storage/LibraryWriter.h"
#include "ContentEngine/Storage/SlotFile.h"

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
using namespace ContentEngine::library;

using Pool = PagePool<2048 + 576, 64>;
using Runs = RunTable<36, Pool, Config::kInlinePagesPerRun, 8, /*preview runs*/ 4>;
using Manifests = ManifestTable<16, 8>;
using Hist = SlotUndoHistory<Manifests, Runs, 4, 20>;

/// The card: the library's sectors kept sparsely; writes fail (tearing the sector) after `failAfter`; `writes` counts them.
struct FakeCard {
  static constexpr uint32_t kIdle = 0xFFFFFFFFu;
  std::map<uint32_t, std::vector<uint8_t>> sectors;
  uint32_t writes = 0, failAfter = 0xFFFFFFFFu;
  bool programming = false;  // a write is in flight until a `kIdle` poll sees it done
  uint32_t outOfOrder = 0;   // an index or checkpoint header written while an earlier write was still programming
  static bool publishing(uint32_t i) { return i < kCheckpointBase || i == checkpointBase(0) || i == checkpointBase(1); }
  bool readLibrarySector(uint32_t i, uint8_t* d) {
    auto it = sectors.find(i);
    if (it == sectors.end()) std::memset(d, 0, kSector);
    else std::memcpy(d, it->second.data(), kSector);
    return true;
  }
  bool writeLibrarySector(uint32_t i, const uint8_t* d) {
    if (writes >= failAfter) {  // the write that fails tears its sector, as power loss mid-program does
      const bool trap = gTrap;
      gTrap = false;
      sectors[i].assign(kSector, 0xA5);
      gTrap = trap;
      return false;
    }
    ++writes;
    if (programming && publishing(i)) ++outOfOrder;
    programming = true;
    const bool trap = gTrap;  // the fake card's own storage is not the writer's
    gTrap = false;
    sectors[i].assign(d, d + kSector);
    gTrap = trap;
    return true;
  }
  bool libraryReady(uint32_t i) {
    if (i != kIdle) return true;
    const bool was = programming;  // one poll to see it finish
    programming = false;
    return !was;
  }
};
struct MemoryFile {
  std::vector<uint8_t> bytes;
  bool write(const uint8_t* d, uint32_t n) {
    bytes.insert(bytes.end(), d, d + n);
    return true;
  }
};
using LibWriter = LibraryWriter<FakeCard, Pool, Runs, Manifests, Hist, 4>;
using Save = SaveJob<Pool, Runs, Manifests, Hist, MemoryFile, 512>;
using Loader = SetLoad<FakeCard, Pool, Runs, Manifests, Hist, 4>;
using Res = RowReservations<Pool, Runs>;

static Pool gPool;
static Runs gRuns;
static Manifests gManifests;
static Hist gH;
static FakeCard gCard;
static LibWriter gW;
static Loader gL;
static Res gRes;
static NoteIdSource gIds;
static uint8_t gLoadScratch[512];
static int gClears = 0;
static bool gRefuseClear = false;
static uint8_t gScratch[512];
constexpr uint32_t kLoop = 768 * 2;

static PassEvent row(uint32_t tick, uint32_t id, uint8_t status, uint8_t d1, uint8_t d2) {
  PassEvent e{};
  e.tick = tick;
  e.noteId = id;
  e.status = status;
  e.data1 = d1;
  e.data2 = d2;
  e.seq = id;
  return e;
}
static RunId run(uint8_t slot, uint8_t kind, std::initializer_list<PassEvent> rows) {
  const RunId r = gRuns.allocate(slot, kind);
  for (const PassEvent& e : rows) TEST_ASSERT_EQUAL(AppendResult::Ok, gRuns.append(r, e, gPool));
  gRuns.seal(r);
  return r;
}
static void commit(uint8_t slot, std::initializer_list<RunId> runs, uint32_t revision, uint32_t loop = kLoop) {
  std::vector<RunId> list(runs);
  gH.commit(slot, gManifests.create(slot, revision, loop, list.data(), static_cast<uint8_t>(list.size()), gRuns));
}
/// Slot 0: a record, then an overdub on it; slot 2: a record, 4 bars. Writers' references dropped.
static void buildSet() {
  const RunId a = run(0, RunKind::kRecord, {row(10, 1, 0x90, 60, 100), row(200, 1, 0x80, 60, 0)});
  const RunId b = run(0, RunKind::kOverdub, {row(300, 2, 0x90, 64, 90), row(400, 2, 0x80, 64, 0)});
  const RunId c = run(2, RunKind::kRecord, {row(0, 3, 0x90, 48, 80), row(700, 3, 0x80, 48, 0), row(800, 4, 0x90, 50, 80),
                                           row(900, 4, 0x80, 50, 0)});
  commit(0, {a}, 1);
  commit(0, {a, b}, 2);
  commit(2, {c}, 3, 4 * 768);
  for (RunId r : {a, b, c}) gRuns.release(r);
}
static std::vector<uint8_t> saveNow(uint8_t slot) {
  MemoryFile f;
  Save s;
  TEST_ASSERT_EQUAL(Save::Status::Running, s.start(gPool, gRuns, gManifests, gH, slot, f, 100, gScratch));
  while (s.step() == Save::Status::Running) {
  }
  return f.bytes;
}
static LibWriter::Status runToEnd() {
  LibWriter::Status st;
  int grains = 0;
  while ((st = gW.step()) == LibWriter::Status::Running) TEST_ASSERT_TRUE(++grains < 100000);
  return st;
}
static LibWriter::Milestone milestone(Stage stage, uint8_t number) {
  LibWriter::Milestone m;
  m.set = 2;
  m.stage = stage;
  m.stageNumber = number;
  m.parent = 0;
  m.wallTime = 1794175000;
  m.tempoTenths = 1200;
  m.selection[0] = 1;
  return m;
}
static std::vector<uint8_t> blobSlot(const Entry& r, uint8_t slot) {
  uint8_t h[kSector];
  gCard.readLibrarySector(kLogBase + r.logSector, h);
  BlobHeader bh;
  TEST_ASSERT_TRUE(decode(h, bh));
  uint32_t at = kLogBase + r.logSector + 1;
  for (uint8_t s = 0; s < slot; ++s) at += (bh.slotBytes[s] + kSector - 1) / kSector;
  std::vector<uint8_t> out;
  uint8_t sec[kSector];
  for (uint32_t k = 0; k < (bh.slotBytes[slot] + kSector - 1) / kSector; ++k) {
    gCard.readLibrarySector(at + k, sec);
    out.insert(out.end(), sec, sec + kSector);
  }
  out.resize(bh.slotBytes[slot]);
  return out;
}


static bool clearAll(void*) {
  ++gClears;
  if (gRefuseClear) return false;
  for (uint8_t s = 0; s < 4; ++s) TEST_ASSERT_TRUE(gH.clearSlot(s));
  return true;
}
static std::vector<std::vector<uint8_t>> setNow() {
  std::vector<std::vector<uint8_t>> v;
  for (uint8_t s = 0; s < 4; ++s) v.push_back(saveNow(s));
  return v;
}
static Loader::Status loadToEnd() {
  Loader::Status st;
  int grains = 0;
  while ((st = gL.step()) == Loader::Status::Running) TEST_ASSERT_TRUE(++grains < 100000);
  return st;
}
static LibWriter::Checkpoint before() {
  LibWriter::Checkpoint c;
  c.wallTime = 1794175000;
  c.set = 2;
  c.tempoTenths = 1000;
  c.selection[0] = 3;
  return c;
}
/// The set changed after the milestone: slot 0 one step back, slot 2 replaced, slot 3 recorded.
static void changeSet() {
  gH.undo(0);
  const RunId d = run(2, RunKind::kRecord, {row(5, 9, 0x90, 70, 70), row(50, 9, 0x80, 70, 0)});
  commit(2, {d}, 4);
  gRuns.release(d);
  const RunId e = run(3, RunKind::kRecord, {row(7, 10, 0x90, 40, 70), row(90, 10, 0x80, 40, 0)});
  commit(3, {e}, 5);
  gRuns.release(e);
}
static void saveMilestone() {
  TEST_ASSERT_EQUAL(LibWriter::Result::Ok,
                    gW.beginMilestone(gPool, gRuns, gManifests, gH, gIds.peek(), milestone(Stage::Take, 1), gScratch));
  TEST_ASSERT_EQUAL(LibWriter::Status::Done, runToEnd());
}

void setUp() {
  gPool.init();
  gRuns.init();
  gManifests.init();
  gRes.init(gPool, gRuns, 0, 16);
  gH.init(gManifests, gRuns);
  gIds.init();
  while (gIds.peek() <= 20) gIds.advance();
  gCard = FakeCard{};
  gW = LibWriter{};
  gL = Loader{};
  TEST_ASSERT_TRUE(gW.mount(gCard));
  gL.bind(gCard, gW, gPool, gRuns, gManifests, gH, gRes, gIds, gLoadScratch, &clearAll, nullptr);
  gClears = 0;
  gRefuseClear = false;
}
void tearDown() {}

void test_a_load_brings_the_milestone_back_histories_included() {
  buildSet();
  const auto atMilestone = setNow();
  saveMilestone();
  changeSet();
  TEST_ASSERT_FALSE(setNow() == atMilestone);
  TEST_ASSERT_EQUAL(Loader::Result::Ok, gL.beginLoad(0, before()));
  TEST_ASSERT_EQUAL(Loader::Status::Done, loadToEnd());
  TEST_ASSERT_TRUE(setNow() == atMilestone);  // every slot's file as at the milestone: states, current, runs
  TEST_ASSERT_EQUAL_UINT8(2, gH.undoDepth(0));  // its undo history too (the overdub, the record)
  TEST_ASSERT_EQUAL_UINT16(1200, gL.blob().tempoTenths);
  TEST_ASSERT_EQUAL_UINT8(1, gL.blob().selection[0]);
  TEST_ASSERT_TRUE(gRes.check());
  TEST_ASSERT_EQUAL_UINT16(0, gRuns.reclaimPendingRuns());  // the cleared set reclaimed before the read
}

void test_the_checkpoint_holds_the_set_before_the_load_and_return_brings_it_back() {
  buildSet();
  saveMilestone();
  changeSet();
  const auto beforeLoad = setNow();
  TEST_ASSERT_EQUAL(Loader::Result::Ok, gL.beginLoad(0, before()));
  TEST_ASSERT_EQUAL(Loader::Status::Done, loadToEnd());
  CheckpointHeader h;
  TEST_ASSERT_TRUE(gW.checkpoint(h));
  TEST_ASSERT_EQUAL_UINT32(1, h.loaded);  // the milestone it was taken before
  TEST_ASSERT_EQUAL_UINT16(2, h.set);
  TEST_ASSERT_EQUAL(Loader::Result::Ok, gL.beginReturn());
  TEST_ASSERT_EQUAL(Loader::Status::Done, loadToEnd());
  TEST_ASSERT_TRUE(setNow() == beforeLoad);
  TEST_ASSERT_EQUAL_UINT16(1000, gL.blob().tempoTenths);  // the checkpoint's tempo and surface
  TEST_ASSERT_EQUAL_UINT8(3, gL.blob().selection[0]);
  TEST_ASSERT_TRUE(gW.checkpoint(h));
  TEST_ASSERT_EQUAL_UINT32(1, h.sequence);  // return writes no checkpoint
  TEST_ASSERT_TRUE(gRes.check());
}

void test_a_load_that_fails_is_undone_from_the_checkpoint() {
  buildSet();
  saveMilestone();
  Entry r;
  TEST_ASSERT_TRUE(gW.readEntry(0, r));
  uint8_t h[kSector];
  gCard.readLibrarySector(kLogBase + r.logSector, h);
  BlobHeader bh;
  TEST_ASSERT_TRUE(decode(h, bh));
  const uint32_t slot2 = kLogBase + r.logSector + 1 + (bh.slotBytes[0] + kSector - 1) / kSector;
  gCard.sectors[slot2].assign(kSector, 0x5A);  // slot 2's file damaged on the card
  changeSet();
  const auto beforeLoad = setNow();
  TEST_ASSERT_EQUAL(Loader::Result::Ok, gL.beginLoad(0, before()));
  TEST_ASSERT_EQUAL(Loader::Status::Restored, loadToEnd());
  TEST_ASSERT_EQUAL_UINT8(2, gL.failedSlot());
  TEST_ASSERT_TRUE(setNow() == beforeLoad);
  TEST_ASSERT_TRUE(gRes.check());
}

void test_a_slot_file_shorter_than_its_header_says_fails_the_load() {
  buildSet();
  saveMilestone();
  Entry r;
  TEST_ASSERT_TRUE(gW.readEntry(0, r));
  uint8_t h[kSector];
  gCard.readLibrarySector(kLogBase + r.logSector, h);
  BlobHeader bh;
  TEST_ASSERT_TRUE(decode(h, bh));
  bh.slotBytes[2] += 4;  // still within its last sector
  encode(bh, h);
  gCard.sectors[kLogBase + r.logSector].assign(h, h + kSector);
  changeSet();
  const auto beforeLoad = setNow();
  TEST_ASSERT_EQUAL(Loader::Result::Ok, gL.beginLoad(0, before()));
  TEST_ASSERT_EQUAL(Loader::Status::Restored, loadToEnd());
  TEST_ASSERT_EQUAL_UINT8(2, gL.failedSlot());
  TEST_ASSERT_TRUE(setNow() == beforeLoad);
}

void test_a_checkpoint_that_cannot_be_written_changes_nothing() {
  buildSet();
  saveMilestone();
  changeSet();
  const auto beforeLoad = setNow();
  gCard.failAfter = gCard.writes + 2;
  TEST_ASSERT_EQUAL(Loader::Result::Ok, gL.beginLoad(0, before()));
  TEST_ASSERT_EQUAL(Loader::Status::Unchanged, loadToEnd());
  TEST_ASSERT_EQUAL(0, gClears);
  TEST_ASSERT_TRUE(setNow() == beforeLoad);
}

void test_a_refused_clear_changes_nothing() {
  buildSet();
  saveMilestone();
  changeSet();
  const auto beforeLoad = setNow();
  gRefuseClear = true;
  TEST_ASSERT_EQUAL(Loader::Result::Ok, gL.beginLoad(0, before()));
  TEST_ASSERT_EQUAL(Loader::Status::Unchanged, loadToEnd());
  TEST_ASSERT_TRUE(setNow() == beforeLoad);
}

void test_a_live_load_waits_for_the_switch_point_before_it_clears() {
  buildSet();
  saveMilestone();
  changeSet();
  const auto beforeLoad = setNow();
  TEST_ASSERT_EQUAL(Loader::Result::Ok, gL.beginLoad(0, before(), true));
  for (int i = 0; i < 100000 && !gL.waiting(); ++i) TEST_ASSERT_EQUAL(Loader::Status::Running, gL.step());
  TEST_ASSERT_TRUE(gL.waiting());
  CheckpointHeader h;
  TEST_ASSERT_TRUE(gW.checkpoint(h));  // the checkpoint is written ...
  for (int i = 0; i < 50; ++i) TEST_ASSERT_EQUAL(Loader::Status::Running, gL.step());
  TEST_ASSERT_EQUAL(0, gClears);  // ... but nothing cleared until the switch point
  TEST_ASSERT_TRUE(setNow() == beforeLoad);
  gL.release();
  TEST_ASSERT_EQUAL(Loader::Status::Done, loadToEnd());
  TEST_ASSERT_EQUAL(1, gClears);
}

/// DEC-261: staging loads the selected slot's current state beside the set (slot 0 skipped on the card), detached: the
/// histories stay as they are until the switch.
void test_staging_loads_the_selected_slot_beside_the_set() {
  buildSet();
  LibWriter::Milestone m = milestone(Stage::Take, 1);
  m.selection[1] = 2;  // track 0 selects slot 2
  TEST_ASSERT_EQUAL(LibWriter::Result::Ok, gW.beginMilestone(gPool, gRuns, gManifests, gH, gIds.peek(), m, gScratch));
  TEST_ASSERT_EQUAL(LibWriter::Status::Done, runToEnd());
  const auto atMilestone = setNow();
  changeSet();
  const auto beforeLoad = setNow();
  TEST_ASSERT_EQUAL(Loader::Result::Ok, gL.beginLoad(0, before(), true, 0x1));
  for (int i = 0; i < 100000 && !gL.waiting(); ++i) gL.step();
  TEST_ASSERT_TRUE(gL.waiting() && gL.stagingDone());
  const ManifestId staged = gL.staged(0);
  TEST_ASSERT_TRUE(staged != kNoManifest);
  TEST_ASSERT_EQUAL_UINT8(2, gL.stagedSlot(0));
  TEST_ASSERT_EQUAL_UINT32(4 * 768, gManifests.manifest(staged).loopLengthTicks);  // slot 2's, as at the milestone
  TEST_ASSERT_EQUAL_UINT8(1, gManifests.manifest(staged).runCount);
  TEST_ASSERT_EQUAL_UINT32(4, gRuns.run(gManifests.manifest(staged).runs[0]).rowCount);
  TEST_ASSERT_TRUE(setNow() == beforeLoad);  // no history touched
  gL.release();
  TEST_ASSERT_EQUAL(Loader::Status::Done, loadToEnd());
  TEST_ASSERT_EQUAL(staged, gH.current(2));  // adopted: the slot's current is the staged state (nothing to follow)
  TEST_ASSERT_TRUE(setNow()[2] == atMilestone[2]);
  const uint16_t free = gManifests.freeCount();
  gL.releaseStaged();
  TEST_ASSERT_EQUAL_UINT16(free, gManifests.freeCount());  // SlotUndoHistory holds it now
  TEST_ASSERT_TRUE(gManifests.manifest(staged).live);
}

/// DEC-270 — get a loop: one slot of a saved stage read into another slot's tables, its current state only, the
/// caller's; no checkpoint, no clear, no history touched.
void test_getting_a_loop_reads_one_slot_for_another_and_touches_nothing() {
  buildSet();
  saveMilestone();
  changeSet();
  const auto beforeGet = setNow();
  const uint16_t manifests = gManifests.freeCount();
  TEST_ASSERT_EQUAL(Loader::Result::Ok, gL.beginGet(0, 2, 1));
  TEST_ASSERT_EQUAL(Loader::Result::Busy, gL.beginGet(0, 2, 1));
  TEST_ASSERT_EQUAL(Loader::Status::Done, loadToEnd());
  TEST_ASSERT_TRUE(gL.kind() == Loader::Kind::Get);
  const ManifestId m = gL.takeGot();
  TEST_ASSERT_TRUE(m != kNoManifest);
  TEST_ASSERT_EQUAL(kNoManifest, gL.takeGot());  // taken once
  const auto& x = gManifests.manifest(m);
  TEST_ASSERT_EQUAL_UINT8(1, x.slot);  // bound to the slot it goes into
  TEST_ASSERT_EQUAL_UINT32(4 * 768, x.loopLengthTicks);  // slot 2's state at the stage, not its changed one
  TEST_ASSERT_EQUAL_UINT8(1, x.runCount);
  TEST_ASSERT_EQUAL_UINT8(1, gRuns.run(x.runs[0]).slot);
  TEST_ASSERT_EQUAL_UINT32(4, gRuns.run(x.runs[0]).rowCount);
  TEST_ASSERT_EQUAL_UINT8(48, gRuns.rowAt(x.runs[0], 0, gPool).data1);
  TEST_ASSERT_TRUE(setNow() == beforeGet);  // nothing else changed
  TEST_ASSERT_EQUAL_INT(0, gClears);
  CheckpointHeader cp;
  TEST_ASSERT_FALSE(gW.checkpoint(cp));  // no checkpoint written
  TEST_ASSERT_EQUAL_UINT16(manifests - 1, gManifests.freeCount());  // only the state taken
  gManifests.release(m, gRuns);
  TEST_ASSERT_TRUE(gRes.check());
}

/// DEC-285: a got loop keeps its state's run order, so a shortened loop's fit lookups (content runs by position) still
/// name the same runs — the take before the fit run, its lookup at position 0.
void test_a_got_shortened_loop_keeps_its_run_order() {
  buildSet();
  const RunId take = run(3, RunKind::kRecord, {row(10, 5, 0x90, 60, 100), row(200, 5, 0x80, 60, 0),
                                               row(900, 6, 0x90, 62, 100), row(1000, 6, 0x80, 62, 0)});
  const RunId fit = run(3, RunKind::kFit, {row(0, 0, kFitFilterStatus, 0, 0)});
  commit(3, {take, fit}, 4, 768);
  for (RunId r : {take, fit}) gRuns.release(r);
  saveMilestone();
  TEST_ASSERT_EQUAL(Loader::Result::Ok, gL.beginGet(0, 3, 1));
  TEST_ASSERT_EQUAL(Loader::Status::Done, loadToEnd());
  const ManifestId m = gL.takeGot();
  TEST_ASSERT_TRUE(m != kNoManifest);
  const auto& x = gManifests.manifest(m);
  TEST_ASSERT_EQUAL_UINT8(2, x.runCount);
  TEST_ASSERT_EQUAL_UINT8(RunKind::kRecord, gRuns.run(x.runs[0]).kind);
  TEST_ASSERT_EQUAL_UINT8(RunKind::kFit, gRuns.run(x.runs[1]).kind);
  TEST_ASSERT_EQUAL_UINT32(1, gRuns.run(x.runs[1]).rowCount);
  TEST_ASSERT_EQUAL_UINT32(0, gRuns.rowAt(x.runs[1], 0, gPool).tick);  // position 0: the take
  TEST_ASSERT_EQUAL_UINT8(kFitFilterStatus, gRuns.rowAt(x.runs[1], 0, gPool).status);
  gManifests.release(m, gRuns);
  TEST_ASSERT_TRUE(gRes.check());
}

/// DEC-270: a stage's empty slot, or a read that fails, gets nothing and changes nothing; refused by name otherwise.
void test_a_get_that_finds_nothing_or_fails_changes_nothing() {
  buildSet();
  saveMilestone();
  const auto beforeGet = setNow();
  const uint16_t manifests = gManifests.freeCount();
  TEST_ASSERT_EQUAL(Loader::Result::Ok, gL.beginGet(0, 1, 3));  // slot 1 was empty at the stage
  TEST_ASSERT_EQUAL(Loader::Status::Unchanged, loadToEnd());
  TEST_ASSERT_EQUAL(kNoManifest, gL.takeGot());
  Entry r;
  TEST_ASSERT_TRUE(gW.readEntry(0, r));
  uint8_t h[kSector];
  gCard.readLibrarySector(kLogBase + r.logSector, h);
  BlobHeader bh;
  TEST_ASSERT_TRUE(decode(h, bh));
  uint32_t at = kLogBase + r.logSector + 1 + (bh.slotBytes[0] + kSector - 1) / kSector;  // slot 2's first sector
  gCard.sectors[at][30] ^= 0xFF;  // a damaged row: the slot's CRC fails
  TEST_ASSERT_EQUAL(Loader::Result::Ok, gL.beginGet(0, 2, 3));
  TEST_ASSERT_EQUAL(Loader::Status::Unchanged, loadToEnd());
  TEST_ASSERT_EQUAL(kNoManifest, gL.takeGot());
  TEST_ASSERT_TRUE(setNow() == beforeGet);
  TEST_ASSERT_EQUAL_UINT16(manifests, gManifests.freeCount());
  TEST_ASSERT_TRUE(gRes.check());
  TEST_ASSERT_EQUAL(Loader::Result::NoSlot, gL.beginGet(0, 4, 1));  // a slot past the set
  TEST_ASSERT_EQUAL(Loader::Result::NoSlot, gL.beginGet(0, 1, 4));
  TEST_ASSERT_EQUAL(Loader::Result::NoMilestone, gL.beginGet(1, 1, 2));
}

/// Slot 1: x replaced by y (current y); slot 3: p replaced by q, then undone (current p). Saved as milestone 0.
static void saveHistories() {
  const RunId x = run(1, RunKind::kRecord, {row(10, 30, 0x90, 61, 100), row(20, 30, 0x80, 61, 0)});
  const RunId y = run(1, RunKind::kRecord, {row(30, 31, 0x90, 62, 100), row(40, 31, 0x80, 62, 0)});
  const RunId p = run(3, RunKind::kRecord, {row(50, 32, 0x90, 63, 100), row(60, 32, 0x80, 63, 0)});
  const RunId q = run(3, RunKind::kRecord, {row(70, 33, 0x90, 64, 100), row(80, 33, 0x80, 64, 0)});
  commit(1, {x}, 10);
  commit(1, {y}, 11);
  commit(3, {p}, 12);
  commit(3, {q}, 13);
  gH.undo(3);
  for (RunId k : {x, y, p, q}) gRuns.release(k);
  saveMilestone();
}

/// Red team fix 1 (plan §6.2): a get reads only the slot's current state — the runs it lists, nothing of the rest of
/// the history is allocated, reserved or left to reclaim.
void test_a_get_reads_only_the_current_states_runs() {
  saveHistories();
  for (uint8_t from : {1, 3}) {
    const uint16_t runs = gRuns.freeCount(), pending = gRuns.reclaimPendingRuns();
    TEST_ASSERT_EQUAL(Loader::Result::Ok, gL.beginGet(0, from, 0));
    TEST_ASSERT_EQUAL(Loader::Status::Done, loadToEnd());
    const ManifestId m = gL.takeGot();
    TEST_ASSERT_TRUE(m != kNoManifest);
    const auto& st = gManifests.manifest(m);
    TEST_ASSERT_EQUAL_UINT8(1, st.runCount);
    TEST_ASSERT_EQUAL_UINT32(from == 1 ? 11 : 12, st.revision);  // y; p (the undone-to current)
    TEST_ASSERT_EQUAL_UINT8(from == 1 ? 62 : 63, gRuns.rowAt(st.runs[0], 0, gPool).data1);
    TEST_ASSERT_EQUAL_UINT16(runs - 1, gRuns.freeCount());  // one run taken, none other
    TEST_ASSERT_EQUAL_UINT16(pending, gRuns.reclaimPendingRuns());  // nothing read and dropped
    gManifests.release(m, gRuns);
  }
  TEST_ASSERT_TRUE(gRes.check());
}

/// The whole slot file is still checked: a damaged row of a run the current state does not list fails the get.
void test_a_get_checks_the_whole_slot_file() {
  saveHistories();
  Entry r;
  TEST_ASSERT_TRUE(gW.readEntry(0, r));
  uint8_t h[kSector];
  gCard.readLibrarySector(kLogBase + r.logSector, h);
  BlobHeader bh;
  TEST_ASSERT_TRUE(decode(h, bh));
  const uint32_t at = kLogBase + r.logSector + 1;  // slot 0 empty: slot 1's file first
  TEST_ASSERT_EQUAL_UINT32(0, bh.slotBytes[0]);
  gCard.sectors[at][24 + 2 * 8 + 13] ^= 0x01;  // x's first row (the directory lists x, then y), its pitch
  TEST_ASSERT_EQUAL(Loader::Result::Ok, gL.beginGet(0, 1, 0));
  TEST_ASSERT_EQUAL(Loader::Status::Unchanged, loadToEnd());
  TEST_ASSERT_EQUAL(kNoManifest, gL.takeGot());
  TEST_ASSERT_TRUE(gRes.check());
}

/// Plan §6.2 fix 1 (a2): a preview get reads into preview runs within the preview partition — what content may take is
/// the same before, during and after — and a loop larger than the partition is refused (`Capacity`).
void test_a_preview_get_stays_in_the_preview_partition() {
  saveHistories();
  const uint32_t content = gRes.contentFreePages();
  TEST_ASSERT_EQUAL(Loader::Result::Ok, gL.beginGet(0, 1, 0, true));
  Loader::Status st;
  while ((st = gL.step()) == Loader::Status::Running) TEST_ASSERT_EQUAL_UINT32(content, gRes.contentFreePages());
  TEST_ASSERT_EQUAL(Loader::Status::Done, st);
  const ManifestId m = gL.takeGot();
  TEST_ASSERT_TRUE(m != kNoManifest);
  TEST_ASSERT_TRUE(gRuns.isPreview(gManifests.manifest(m).runs[0]));
  TEST_ASSERT_EQUAL_UINT32(content, gRes.contentFreePages());
  gManifests.release(m, gRuns);
  while (gRuns.reclaimPendingRuns() != 0) gRuns.reclaim(gPool, 64);
  TEST_ASSERT_EQUAL_UINT32(0, gRuns.previewPages());
  // A loop larger than the partition: refused, nothing taken.
  gRes.init(gPool, gRuns, 0, 0);
  TEST_ASSERT_EQUAL(Loader::Result::Ok, gL.beginGet(0, 1, 0, true));
  TEST_ASSERT_EQUAL(Loader::Status::Unchanged, loadToEnd());
  TEST_ASSERT_TRUE(gL.getWhy() == decltype(gL.getWhy())::Capacity);
  while (gRuns.reclaimPendingRuns() != 0) gRuns.reclaim(gPool, 64);
  TEST_ASSERT_EQUAL_UINT16(4, gRuns.previewFreeCount());  // the refused read's descriptor back
  TEST_ASSERT_TRUE(gRes.check());
}

void test_refusals() {
  buildSet();
  TEST_ASSERT_EQUAL(Loader::Result::NoCheckpoint, gL.beginReturn());
  TEST_ASSERT_EQUAL(Loader::Result::NoMilestone, gL.beginLoad(0, before()));
  saveMilestone();
  TEST_ASSERT_EQUAL(Loader::Result::NoMilestone, gL.beginLoad(1, before()));
  TEST_ASSERT_EQUAL(LibWriter::Result::Ok, gW.beginMark(kTrash, 2, 1, 0));  // DEC-268: a trash record has no set
  TEST_ASSERT_EQUAL(LibWriter::Status::Done, runToEnd());
  TEST_ASSERT_EQUAL(Loader::Result::NoMilestone, gL.beginLoad(1, before()));
  TEST_ASSERT_EQUAL(Loader::Result::Ok, gL.beginLoad(0, before()));
  TEST_ASSERT_EQUAL(Loader::Result::Busy, gL.beginLoad(0, before()));
  loadToEnd();
}

void test_clearing_releases_the_set() {
  const uint16_t freeManifests = gManifests.freeCount(), freeRuns = gRuns.freeCount();
  buildSet();
  TEST_ASSERT_TRUE(gManifests.freeCount() < freeManifests);
  for (uint8_t s = 0; s < 4; ++s) TEST_ASSERT_TRUE(gH.clearSlot(s));
  TEST_ASSERT_EQUAL_UINT16(freeManifests, gManifests.freeCount());
  TEST_ASSERT_EQUAL_UINT16(3, gRuns.reclaimPendingRuns());  // every run released, its pages for the reclaim job
  gRuns.reclaim(gPool, 1000);
  TEST_ASSERT_EQUAL_UINT16(freeRuns, gRuns.freeCount());
  TEST_ASSERT_EQUAL_UINT8(0, gH.undoDepth(0));
}

void test_no_heap() {
  buildSet();
  saveMilestone();
  changeSet();
  gTrap = true;
  const unsigned long n = gHeapAllocations;
  gL.beginLoad(0, before());
  loadToEnd();
  gL.beginReturn();
  loadToEnd();
  gTrap = false;
  TEST_ASSERT_EQUAL_UINT32(n, gHeapAllocations);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_a_load_brings_the_milestone_back_histories_included);
  RUN_TEST(test_the_checkpoint_holds_the_set_before_the_load_and_return_brings_it_back);
  RUN_TEST(test_a_load_that_fails_is_undone_from_the_checkpoint);
  RUN_TEST(test_a_slot_file_shorter_than_its_header_says_fails_the_load);
  RUN_TEST(test_a_checkpoint_that_cannot_be_written_changes_nothing);
  RUN_TEST(test_a_refused_clear_changes_nothing);
  RUN_TEST(test_a_live_load_waits_for_the_switch_point_before_it_clears);
  RUN_TEST(test_staging_loads_the_selected_slot_beside_the_set);
  RUN_TEST(test_getting_a_loop_reads_one_slot_for_another_and_touches_nothing);
  RUN_TEST(test_a_got_shortened_loop_keeps_its_run_order);
  RUN_TEST(test_a_get_that_finds_nothing_or_fails_changes_nothing);
  RUN_TEST(test_a_get_reads_only_the_current_states_runs);
  RUN_TEST(test_a_get_checks_the_whole_slot_file);
  RUN_TEST(test_a_preview_get_stays_in_the_preview_partition);
  RUN_TEST(test_refusals);
  RUN_TEST(test_clearing_releases_the_set);
  RUN_TEST(test_no_heap);
  return UNITY_END();
}
