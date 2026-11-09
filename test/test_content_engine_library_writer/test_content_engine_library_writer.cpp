//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// Sets browser slice 1c-ii (DEC-258): writing milestones and the checkpoint. A milestone is the whole set at the
// instant it was asked for (a change made while it writes is not in it), published blob → record → header; an
// interrupted write publishes nothing and its space is reused; a save that does not fit is refused before any write;
// the checkpoint is replaced transactionally (a failed replacement leaves the previous one current); the snapshot's
// pins are released. No heap.

#include <unity.h>

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <map>
#include <new>
#include <vector>

#include "ContentEngine/EngineCore.h"
#include "ContentEngine/History.h"
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

static Pool gPool;
static Runs gRuns;
static Manifests gManifests;
static Hist gH;
static FakeCard gCard;
static LibWriter gW;
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

void setUp() {
  gPool.init();
  gRuns.init();
  gManifests.init();
  gH.init(gManifests, gRuns);
  gCard = FakeCard{};
  gW = LibWriter{};
  TEST_ASSERT_TRUE(gW.mount(gCard));
}
void tearDown() {}

void test_a_milestone_is_published_and_reads_back() {
  buildSet();
  const auto t0slot0 = saveNow(0), t0slot2 = saveNow(2);
  gManifests.pin(gH.current(0));  // another reader's pin, which the writer must leave
  TEST_ASSERT_EQUAL(LibWriter::Result::Ok,
                    gW.beginMilestone(gPool, gRuns, gManifests, gH, 100, milestone(Stage::Take, 4), gScratch));
  TEST_ASSERT_EQUAL(LibWriter::Status::Done, runToEnd());
  LibWriter fresh;  // as at the next boot
  TEST_ASSERT_TRUE(fresh.mount(gCard));
  TEST_ASSERT_EQUAL_UINT16(1, fresh.header().count);
  Record r;
  TEST_ASSERT_TRUE(fresh.readRecord(0, r));
  TEST_ASSERT_EQUAL(Stage::Take, r.stage);
  TEST_ASSERT_EQUAL_UINT8(4, r.stageNumber);
  TEST_ASSERT_EQUAL_UINT16(2, r.set);
  TEST_ASSERT_EQUAL_UINT8(2, r.loops);
  TEST_ASSERT_EQUAL_UINT32(kLoop, r.shortestLoop);
  TEST_ASSERT_EQUAL_UINT32(4 * 768, r.longestLoop);
  TEST_ASSERT_EQUAL_UINT32(1, r.sequence);
  TEST_ASSERT_EQUAL_UINT32(r.logSectors, fresh.header().appendSector);
  TEST_ASSERT_TRUE((r.mosaic[0] & 0x3) != 0 && ((r.mosaic[0] >> 4) & 0x3) != 0 && ((r.mosaic[0] >> 2) & 0x3) == 0);
  TEST_ASSERT_TRUE(blobSlot(r, 0) == t0slot0);  // the slot files as a save at that instant writes them
  TEST_ASSERT_TRUE(blobSlot(r, 2) == t0slot2);
  TEST_ASSERT_EQUAL_UINT16(1, gManifests.manifest(gH.current(0)).readerPins);  // the snapshot's pins released, once
  TEST_ASSERT_EQUAL_UINT32(0, gCard.outOfOrder);  // blob, then record, then header, each programmed first
}

void test_a_change_while_it_writes_is_not_in_it() {
  buildSet();
  const auto t0slot0 = saveNow(0), t0slot2 = saveNow(2);
  TEST_ASSERT_EQUAL(LibWriter::Result::Ok,
                    gW.beginMilestone(gPool, gRuns, gManifests, gH, 100, milestone(Stage::Building, 1), gScratch));
  gH.undo(0);  // slot 0 one step back and a new take on it (the T0 state leaves its chain), slot 2 replaced
  const RunId e = run(0, RunKind::kRecord, {row(1, 7, 0x90, 30, 30), row(2, 7, 0x80, 30, 0), row(3, 8, 0x90, 31, 31),
                                           row(4, 8, 0x80, 31, 0), row(5, 9, 0x90, 32, 32), row(6, 9, 0x80, 32, 0)});
  commit(0, {e}, 5);
  gRuns.release(e);
  const RunId d = run(2, RunKind::kRecord, {row(5, 9, 0x90, 70, 70), row(50, 9, 0x80, 70, 0)});
  commit(2, {d}, 4);
  gRuns.release(d);
  TEST_ASSERT_EQUAL(LibWriter::Status::Done, runToEnd());
  Record r;
  TEST_ASSERT_TRUE(gW.readRecord(0, r));
  TEST_ASSERT_TRUE(blobSlot(r, 0) == t0slot0);
  TEST_ASSERT_TRUE(blobSlot(r, 2) == t0slot2);
  TEST_ASSERT_FALSE(saveNow(2) == t0slot2);  // the live set did change
  TEST_ASSERT_FALSE(saveNow(0) == t0slot0);
}

void test_an_interrupted_write_publishes_nothing_and_its_space_is_reused() {
  buildSet();
  gCard.failAfter = 2;  // the blob's header and one body sector, then the card fails
  TEST_ASSERT_EQUAL(LibWriter::Result::Ok,
                    gW.beginMilestone(gPool, gRuns, gManifests, gH, 100, milestone(Stage::Sketch, 1), gScratch));
  TEST_ASSERT_EQUAL(LibWriter::Status::Failed, runToEnd());
  LibWriter fresh;
  TEST_ASSERT_TRUE(fresh.mount(gCard));
  TEST_ASSERT_EQUAL_UINT16(0, fresh.header().count);
  TEST_ASSERT_EQUAL_UINT32(0, fresh.header().appendSector);
  TEST_ASSERT_EQUAL_UINT16(0, gManifests.manifest(gH.current(0)).readerPins);
  gCard.failAfter = 0xFFFFFFFFu;
  TEST_ASSERT_EQUAL(LibWriter::Result::Ok,
                    gW.beginMilestone(gPool, gRuns, gManifests, gH, 100, milestone(Stage::Sketch, 1), gScratch));
  TEST_ASSERT_EQUAL(LibWriter::Status::Done, runToEnd());
  Record r;
  TEST_ASSERT_TRUE(gW.readRecord(0, r));
  TEST_ASSERT_EQUAL_UINT32(0, r.logSector);  // the failed write's space, reused
}

void test_a_record_written_before_its_header_is_not_published() {
  buildSet();
  TEST_ASSERT_EQUAL(LibWriter::Result::Ok,
                    gW.beginMilestone(gPool, gRuns, gManifests, gH, 100, milestone(Stage::Sketch, 1), gScratch));
  TEST_ASSERT_EQUAL(LibWriter::Status::Done, runToEnd());
  const uint32_t before = gCard.writes;
  Record first;
  TEST_ASSERT_TRUE(gW.readRecord(0, first));
  uint32_t blobWrites = 1;  // header + slots
  {
    uint8_t h[kSector];
    gCard.readLibrarySector(kLogBase + first.logSector, h);
    BlobHeader bh;
    decode(h, bh);
    for (uint8_t s = 0; s < 4; ++s) blobWrites += (bh.slotBytes[s] + kSector - 1) / kSector;
  }
  gCard.failAfter = before + blobWrites + 1;  // the blob and the record, then the header fails (and tears)
  LibWriter booted;  // after a reboot: it knows the current header only from the card
  TEST_ASSERT_TRUE(booted.mount(gCard));
  TEST_ASSERT_EQUAL(LibWriter::Result::Ok,
                    booted.beginMilestone(gPool, gRuns, gManifests, gH, 100, milestone(Stage::Take, 1), gScratch));
  LibWriter::Status st;
  while ((st = booted.step()) == LibWriter::Status::Running) {
  }
  TEST_ASSERT_EQUAL(LibWriter::Status::Failed, st);
  LibWriter fresh;
  TEST_ASSERT_TRUE(fresh.mount(gCard));
  TEST_ASSERT_EQUAL_UINT16(1, fresh.header().count);  // the second record exists on the card but is not published
  Record r;
  TEST_ASSERT_FALSE(fresh.readRecord(1, r));
}

/// DEC-268: a deletion is a tombstone record and a header — no blob, the log not advanced, nothing older rewritten; a
/// writer that never captured a set (just booted) publishes it too.
void test_a_deletion_publishes_a_tombstone_and_rewrites_nothing() {
  buildSet();
  TEST_ASSERT_EQUAL(LibWriter::Result::Ok,
                    gW.beginMilestone(gPool, gRuns, gManifests, gH, 100, milestone(Stage::Sketch, 1), gScratch));
  TEST_ASSERT_EQUAL(LibWriter::Status::Done, runToEnd());
  const uint32_t append = gW.header().appendSector;
  std::vector<uint8_t> blob(kSector);
  gCard.readLibrarySector(kLogBase, blob.data());
  LibWriter booted;
  TEST_ASSERT_TRUE(booted.mount(gCard));
  const uint32_t before = gCard.writes;
  TEST_ASSERT_EQUAL(LibWriter::Result::Ok, booted.beginDeletion(2, 1, 1794176000));
  TEST_ASSERT_EQUAL(LibWriter::Result::Busy, booted.beginDeletion(2, 0, 0));
  LibWriter::Status st;
  while ((st = booted.step()) == LibWriter::Status::Running) {
  }
  TEST_ASSERT_EQUAL(LibWriter::Status::Done, st);
  TEST_ASSERT_EQUAL_UINT32(2, gCard.writes - before);  // the record's sector, then the header
  TEST_ASSERT_EQUAL_UINT32(0, gCard.outOfOrder);
  LibWriter fresh;
  TEST_ASSERT_TRUE(fresh.mount(gCard));
  TEST_ASSERT_EQUAL_UINT16(2, fresh.header().count);
  TEST_ASSERT_EQUAL_UINT32(append, fresh.header().appendSector);
  Record m, t;
  TEST_ASSERT_TRUE(fresh.readRecord(0, m));  // the milestone as it was
  TEST_ASSERT_EQUAL(kMilestone, m.kind);
  TEST_ASSERT_EQUAL_UINT32(1, m.sequence);
  TEST_ASSERT_TRUE(fresh.readRecord(1, t));
  TEST_ASSERT_EQUAL(kDeletion, t.kind);
  TEST_ASSERT_EQUAL_UINT16(2, t.set);
  TEST_ASSERT_EQUAL_UINT32(1, t.parent);
  TEST_ASSERT_EQUAL_UINT32(2, t.sequence);
  TEST_ASSERT_EQUAL_UINT32(0, t.logSectors);
  std::vector<uint8_t> after(kSector);
  gCard.readLibrarySector(kLogBase, after.data());
  TEST_ASSERT_TRUE(after == blob);  // its blob untouched
}

/// A deletion whose header write fails publishes nothing: the milestone stays; a full index refuses it unwritten.
void test_a_failed_deletion_publishes_nothing() {
  buildSet();
  TEST_ASSERT_EQUAL(LibWriter::Result::Ok,
                    gW.beginMilestone(gPool, gRuns, gManifests, gH, 100, milestone(Stage::Sketch, 1), gScratch));
  TEST_ASSERT_EQUAL(LibWriter::Status::Done, runToEnd());
  gCard.failAfter = gCard.writes + 1;  // the record, then the header fails
  TEST_ASSERT_EQUAL(LibWriter::Result::Ok, gW.beginDeletion(2, 0, 0));
  TEST_ASSERT_EQUAL(LibWriter::Status::Failed, runToEnd());
  LibWriter fresh;
  TEST_ASSERT_TRUE(fresh.mount(gCard));
  TEST_ASSERT_EQUAL_UINT16(1, fresh.header().count);
  Record r;
  TEST_ASSERT_FALSE(fresh.readRecord(1, r));
  gCard.failAfter = 0xFFFFFFFFu;
  IndexHeader full;
  full.sequence = 9;
  full.count = static_cast<uint16_t>(kIndexRecords);
  uint8_t sector[kSector];
  encode(full, sector);
  gCard.writeLibrarySector(0, sector);
  gCard.libraryReady(FakeCard::kIdle);
  LibWriter booted;
  TEST_ASSERT_TRUE(booted.mount(gCard));
  const uint32_t before = gCard.writes;
  TEST_ASSERT_EQUAL(LibWriter::Result::IndexFull, booted.beginDeletion(2, 0, 0));
  TEST_ASSERT_EQUAL_UINT32(before, gCard.writes);
}

void test_a_save_that_does_not_fit_writes_nothing() {
  buildSet();
  IndexHeader full;
  full.sequence = 7;
  full.count = 3;
  full.appendSector = kLogSectors - 2;  // two sectors left: the set needs more
  full.nextSequence = 4;
  uint8_t s[kSector];
  encode(full, s);
  gCard.writeLibrarySector(0, s);
  gCard.writes = 0;
  TEST_ASSERT_TRUE(gW.mount(gCard));
  TEST_ASSERT_EQUAL(LibWriter::Result::LogFull,
                    gW.beginMilestone(gPool, gRuns, gManifests, gH, 100, milestone(Stage::Take, 1), gScratch));
  TEST_ASSERT_EQUAL_UINT32(0, gCard.writes);
  TEST_ASSERT_EQUAL_UINT16(0, gManifests.manifest(gH.current(0)).readerPins);
  full.appendSector = 0;
  full.count = kIndexRecords;
  encode(full, s);
  gCard.writeLibrarySector(0, s);
  gCard.writes = 0;
  TEST_ASSERT_TRUE(gW.mount(gCard));
  TEST_ASSERT_EQUAL(LibWriter::Result::IndexFull,
                    gW.beginMilestone(gPool, gRuns, gManifests, gH, 100, milestone(Stage::Take, 1), gScratch));
  TEST_ASSERT_EQUAL_UINT32(0, gCard.writes);
}

void test_the_checkpoint_is_replaced_transactionally() {
  buildSet();
  LibWriter::Checkpoint c;
  c.wallTime = 1794175000;
  c.set = 2;
  c.loaded = 9;
  TEST_ASSERT_EQUAL(LibWriter::Result::Ok, gW.beginCheckpoint(gPool, gRuns, gManifests, gH, 100, c, gScratch));
  TEST_ASSERT_EQUAL(LibWriter::Status::Done, runToEnd());
  CheckpointHeader h;
  TEST_ASSERT_TRUE(gW.checkpoint(h));
  TEST_ASSERT_EQUAL_UINT32(1, h.sequence);
  const uint32_t ok = gCard.writes;
  gCard.failAfter = ok + 3;  // the next one fails inside its blob
  c.loaded = 10;
  TEST_ASSERT_EQUAL(LibWriter::Result::Ok, gW.beginCheckpoint(gPool, gRuns, gManifests, gH, 100, c, gScratch));
  TEST_ASSERT_EQUAL(LibWriter::Status::Failed, runToEnd());
  LibWriter fresh;
  TEST_ASSERT_TRUE(fresh.mount(gCard));
  TEST_ASSERT_TRUE(fresh.checkpoint(h));
  TEST_ASSERT_EQUAL_UINT32(1, h.sequence);  // the previous checkpoint is still the current one
  TEST_ASSERT_EQUAL_UINT32(9, h.loaded);
  gCard.failAfter = 0xFFFFFFFFu;
  TEST_ASSERT_EQUAL(LibWriter::Result::Ok, fresh.beginCheckpoint(gPool, gRuns, gManifests, gH, 100, c, gScratch));
  while (fresh.step() == LibWriter::Status::Running) {
  }
  LibWriter again;
  TEST_ASSERT_TRUE(again.mount(gCard));
  TEST_ASSERT_TRUE(again.checkpoint(h));
  TEST_ASSERT_EQUAL_UINT32(2, h.sequence);
  TEST_ASSERT_EQUAL_UINT32(10, h.loaded);
  TEST_ASSERT_EQUAL_UINT32(0, gCard.outOfOrder);
}

void test_no_heap() {
  buildSet();
  gTrap = true;
  const unsigned long before = gHeapAllocations;
  gW.beginMilestone(gPool, gRuns, gManifests, gH, 100, milestone(Stage::Sketch, 1), gScratch);
  while (gW.step() == LibWriter::Status::Running) {
  }
  gTrap = false;
  TEST_ASSERT_EQUAL_UINT32(before, gHeapAllocations);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_a_milestone_is_published_and_reads_back);
  RUN_TEST(test_a_change_while_it_writes_is_not_in_it);
  RUN_TEST(test_an_interrupted_write_publishes_nothing_and_its_space_is_reused);
  RUN_TEST(test_a_record_written_before_its_header_is_not_published);
  RUN_TEST(test_a_deletion_publishes_a_tombstone_and_rewrites_nothing);
  RUN_TEST(test_a_failed_deletion_publishes_nothing);
  RUN_TEST(test_a_save_that_does_not_fit_writes_nothing);
  RUN_TEST(test_the_checkpoint_is_replaced_transactionally);
  RUN_TEST(test_no_heap);
  return UNITY_END();
}
