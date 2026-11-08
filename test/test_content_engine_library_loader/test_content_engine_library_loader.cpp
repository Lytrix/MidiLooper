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
#include "ContentEngine/History.h"
#include "ContentEngine/LibraryLoader.h"
#include "ContentEngine/LibraryWriter.h"
#include "ContentEngine/SetFile.h"

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
using Runs = RunTable<32, Pool>;
using Manifests = ManifestTable<16, 8>;
using Hist = History<Manifests, Runs, 4, 20>;

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
  m.surface[0] = 1;
  return m;
}
static std::vector<uint8_t> blobSlot(const Record& r, uint8_t slot) {
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
  c.surface[0] = 3;
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
  gRes.init(gPool, gRuns, 0);
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
  TEST_ASSERT_EQUAL_UINT8(1, gL.blob().surface[0]);
  TEST_ASSERT_TRUE(gRes.check());
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
  TEST_ASSERT_EQUAL_UINT8(3, gL.blob().surface[0]);
  TEST_ASSERT_TRUE(gW.checkpoint(h));
  TEST_ASSERT_EQUAL_UINT32(1, h.sequence);  // return writes no checkpoint
  TEST_ASSERT_TRUE(gRes.check());
}

void test_a_load_that_fails_is_undone_from_the_checkpoint() {
  buildSet();
  saveMilestone();
  Record r;
  TEST_ASSERT_TRUE(gW.readRecord(0, r));
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
  Record r;
  TEST_ASSERT_TRUE(gW.readRecord(0, r));
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

void test_refusals() {
  buildSet();
  TEST_ASSERT_EQUAL(Loader::Result::NoCheckpoint, gL.beginReturn());
  TEST_ASSERT_EQUAL(Loader::Result::NoMilestone, gL.beginLoad(0, before()));
  saveMilestone();
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
  RUN_TEST(test_refusals);
  RUN_TEST(test_clearing_releases_the_set);
  RUN_TEST(test_no_heap);
  return UNITY_END();
}
