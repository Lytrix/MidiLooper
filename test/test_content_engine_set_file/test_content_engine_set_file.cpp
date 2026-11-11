//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// fixed-memory-engine E8.1: the engine's slot file (owner decision 2026-11-05: new format, new folder; V1 imported
// read-only). A slot's history (record, overdub and edit revisions sharing runs; current one step back after an Undo)
// is saved in persistence grains, the engine is re-initialised, and the slot is loaded: every state composes to the
// same effective content, the chain, current state and floor are the same, Undo / Redo work, shared runs are stored
// once and note ids are never reused. A corrupted, truncated or oversized file, or an occupied slot, ends with a named
// refusal and everything released. A take of more than 65,535 rows round-trips. No heap.

#include <unity.h>

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <new>
#include <vector>

#include "ContentEngine/Composition/SpanWindowComposer.h"
#include "ContentEngine/EngineCore.h"
#include "ContentEngine/History/SlotUndoHistory.h"
#include "ContentEngine/Storage/SlotFile.h"
#include "ContentEngine/Composition/FitRun.h"

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

using Pool = PagePool<2048 + 576, 64>;
using Runs = RunTable<32, Pool>;
using Manifests = ManifestTable<16, 8>;
using Hist = SlotUndoHistory<Manifests, Runs, 4, 20>;
using Res = RowReservations<Pool, Runs>;
using TestComposer = SpanWindowComposer<Pool, Runs>;
using Window = SpanWindow<3072>;

struct MemoryFile {  // the SD file stand-in
  std::vector<uint8_t> bytes;
  size_t pos = 0;
  size_t failAfter = SIZE_MAX;  // an I/O error once this many bytes were transferred
  bool write(const uint8_t* d, uint32_t n) {
    if (bytes.size() + n > failAfter) return false;
    bytes.insert(bytes.end(), d, d + n);
    return true;
  }
  bool read(uint8_t* d, uint32_t n) {
    if (pos + n > bytes.size() || pos + n > failAfter) return false;
    std::memcpy(d, bytes.data() + pos, n);
    pos += n;
    return true;
  }
};
using Save = SaveJob<Pool, Runs, Manifests, Hist, MemoryFile, 4096>;
using Load = LoadJob<Pool, Runs, Manifests, Hist, MemoryFile, 4096>;

static Pool gPool;
static Runs gRuns;
static Manifests gManifests;
static Hist gH;
static Res gRes;
static NoteIdSource gIds;
static EditIndexStore<Pool, Runs> gIndex;
static TestComposer gC;
static Window gWin;
static uint8_t gScratch[4096];
static Save gSave;
static Load gLoad;

constexpr uint32_t kLoop = 768 * 4;

static void initEngine() {
  gPool.init();
  gRuns.init();
  gManifests.init();
  TEST_ASSERT_TRUE(gIndex.init(gPool, gRuns));
  gRes.init(gPool, gRuns, 0);
  gH.init(gManifests, gRuns);
  gIds.init();
  gC.init(gPool, gRuns, &gIndex);
}
void setUp() { initEngine(); }
void tearDown() {}

static PassEvent row(uint32_t tick, uint32_t id, uint8_t status, uint8_t d1, uint8_t d2) {
  PassEvent e{};
  e.tick = tick;
  e.noteId = id;
  e.status = status;
  e.data1 = d1;
  e.data2 = d2;
  e.seq = id * 3;
  e.origin = static_cast<uint8_t>(id & 1);
  return e;
}
static RunId run(uint8_t kind, const std::vector<PassEvent>& rows) {
  const RunId r = gRuns.allocate(0, kind);
  for (const PassEvent& e : rows) TEST_ASSERT_EQUAL(AppendResult::Ok, gRuns.append(r, e, gPool));
  gRuns.seal(r);
  return r;
}
static ManifestId manifest(std::vector<RunId> runs, uint32_t revision) {
  return gManifests.create(0, revision, kLoop, runs.data(), static_cast<uint8_t>(runs.size()), gRuns);
}
struct Ev {
  uint32_t tick, noteId;
  uint8_t status, d1;
  uint16_t length;
  bool operator==(const Ev& o) const {
    return tick == o.tick && noteId == o.noteId && status == o.status && d1 == o.d1 && length == o.length;
  }
};
static std::vector<Ev> composed(ManifestId m) {
  const auto& man = gManifests.manifest(m);
  SourceRun src[8];
  for (uint8_t i = 0; i < man.runCount; ++i) src[i] = {man.runs[i], gRuns.run(man.runs[i]).rowCount};
  TEST_ASSERT_TRUE(gC.begin(src, man.runCount, kLoop, man.identity));
  std::vector<Ev> out;
  while (gC.composeNext(gWin, kLoop))
    for (uint16_t k = 0; k < gWin.count; ++k)
      out.push_back({gWin.entries[k].tick, gWin.entries[k].noteId, gWin.entries[k].status, gWin.entries[k].data1,
                     gWin.entries[k].length});
  gC.end();
  return out;
}

/// Slot 0: A (record) -> A+B (overdub) -> A+B+E (edit: hides 2, replaces it) -> A+B+E+C (overdub); Undo once.
static void buildHistory() {
  const RunId a = run(RunKind::kRecord, {row(100, 1, 0x90, 60, 100), row(300, 1, 0x80, 60, 0), row(400, 2, 0x91, 62, 90),
                                         row(900, 2, 0x81, 62, 0), row(2900, 3, 0x90, 64, 80), row(3500, 3, 0x80, 64, 0)});
  const RunId b = run(RunKind::kOverdub, {row(200, 10, 0x90, 60, 70), row(250, 10, 0x80, 60, 0), row(1500, 11, 0xB0, 7, 99)});
  const RunId e = run(RunKind::kEdit, {row(0, 2, kHideNoteStatus, 0, 0), row(450, 2, 0x91, 63, kVelocityMuted | 90), row(800, 2, 0x81, 63, 0)});
  const RunId c = run(RunKind::kOverdub, {row(1000, 20, 0x90, 70, 60), row(1100, 20, 0x80, 70, 0)});
  gH.commit(0, manifest({a}, 1));
  gH.commit(0, manifest({a, b}, 2));
  gH.commit(0, manifest({a, b, e}, 3));
  gH.commit(0, manifest({a, b, e, c}, 4));
  for (RunId r : {a, b, e, c}) gRuns.release(r);  // writers' references: the manifests hold the runs
  gH.undo(0);
  while (gIds.peek() <= 20) gIds.advance();
}

static void saveSlot(MemoryFile& f, uint8_t slot = 0) {
  TEST_ASSERT_EQUAL(Save::Status::Running, gSave.start(gPool, gRuns, gManifests, gH, slot, f, gIds.peek() - 1, gScratch));
  uint32_t grains = 0;
  while (gSave.step() == Save::Status::Running) TEST_ASSERT_TRUE(++grains < 10000);
  TEST_ASSERT_EQUAL(Save::Status::Done, gSave.status());
}
static Load::Status loadSlot(MemoryFile& f, uint8_t slot = 0) {
  f.pos = 0;
  if (gLoad.start(gPool, gRuns, gManifests, gH, gRes, gIds, slot, f, gScratch) != Load::Status::Running)
    return gLoad.status();
  uint32_t grains = 0;
  while (gLoad.step() == Load::Status::Running) TEST_ASSERT_TRUE(++grains < 10000);
  return gLoad.status();
}

void test_round_trip_keeps_content_history_and_ids() {
  buildHistory();
  ManifestId states[20];
  int16_t cur;
  bool below;
  const uint8_t n = gH.chain(0, states, 20, cur, below);
  std::vector<std::vector<Ev>> want;
  for (uint8_t i = 0; i < n; ++i) want.push_back(composed(states[i]));
  std::vector<std::vector<PassEvent>> rowsOfLast;  // every field of every row of the newest state
  for (uint8_t k = 0; k < gManifests.manifest(states[n - 1]).runCount; ++k) {
    const RunId r = gManifests.manifest(states[n - 1]).runs[k];
    rowsOfLast.emplace_back();
    for (uint32_t i = 0; i < gRuns.run(r).rowCount; ++i) rowsOfLast.back().push_back(gRuns.rowAt(r, i, gPool));
  }
  MemoryFile f;
  saveSlot(f);
  TEST_ASSERT_EQUAL_UINT32(6 + 3 + 3 + 2, gSave.totalRows());  // each run once, though states share them
  TEST_ASSERT_EQUAL_UINT16(0, gManifests.manifest(states[0]).readerPins);  // the save's pins released
  initEngine();
  TEST_ASSERT_EQUAL(Load::Status::Done, loadSlot(f));
  ManifestId loaded[20];
  int16_t cur2;
  bool below2;
  TEST_ASSERT_EQUAL_UINT8(n, gH.chain(0, loaded, 20, cur2, below2));
  TEST_ASSERT_EQUAL_INT16(cur, cur2);
  TEST_ASSERT_EQUAL(below, below2);
  for (uint8_t i = 0; i < n; ++i) {
    const auto got = composed(loaded[i]);
    TEST_ASSERT_EQUAL_UINT32(want[i].size(), got.size());
    for (size_t k = 0; k < got.size(); ++k) TEST_ASSERT_TRUE(want[i][k] == got[k]);
  }
  for (uint8_t k = 0; k < gManifests.manifest(loaded[n - 1]).runCount; ++k) {
    const RunId r = gManifests.manifest(loaded[n - 1]).runs[k];
    TEST_ASSERT_EQUAL_UINT32(rowsOfLast[k].size(), gRuns.run(r).rowCount);
    for (uint32_t i = 0; i < gRuns.run(r).rowCount; ++i) {
      const PassEvent &a = rowsOfLast[k][i], &b = gRuns.rowAt(r, i, gPool);
      TEST_ASSERT_TRUE(a.tick == b.tick && a.noteId == b.noteId && a.seq == b.seq && a.status == b.status &&
                       a.data1 == b.data1 && a.data2 == b.data2 && a.origin == b.origin);
    }
    TEST_ASSERT_EQUAL_UINT8(gRuns.run(gManifests.manifest(states[n - 1]).runs[k]).kind, gRuns.run(r).kind);
  }
  TEST_ASSERT_TRUE(gIds.peek() > 20);  // never reused
  TEST_ASSERT_TRUE(gH.redo(0));        // the Undo is still undone: Redo returns to A+B+E+C
  TEST_ASSERT_EQUAL_UINT16(loaded[3], gH.current(0));
  for (int i = 0; i < 4; ++i) TEST_ASSERT_TRUE(gH.undo(0));
  TEST_ASSERT_EQUAL_UINT16(kNoManifest, gH.current(0));  // down to the empty slot, as saved
  TEST_ASSERT_TRUE(gRes.check());
  TEST_ASSERT_TRUE(checkConservation(gPool, gRuns, gManifests));
}

/// `f` with header version `v` and its CRC trailer recomputed.
static MemoryFile withVersion(const MemoryFile& f, uint16_t v) {
  MemoryFile g = f;
  g.bytes[4] = static_cast<uint8_t>(v & 0xFF);
  g.bytes[5] = static_cast<uint8_t>(v >> 8);
  const size_t n = g.bytes.size() - 4;
  const uint32_t crc = crc32Update(0, g.bytes.data(), static_cast<uint32_t>(n));
  for (int i = 0; i < 4; ++i) g.bytes[n + i] = static_cast<uint8_t>(crc >> (8 * i));
  return g;
}

// Version 3 (DEC-257: a state may list a fit run; version 2: a muted note's NoteOn carries `kVelocityMuted`) is
// written; a version-1 file still loads; a later version is refused by name.
void test_version_one_loads_and_a_later_one_is_refused() {
  buildHistory();
  MemoryFile f;
  saveSlot(f);
  TEST_ASSERT_EQUAL_UINT16(kSetFileVersion, static_cast<uint16_t>(f.bytes[4] | (f.bytes[5] << 8)));
  TEST_ASSERT_EQUAL_UINT16(5, kSetFileVersion);
  TEST_ASSERT_TRUE(withVersion(f, kSetFileVersion).bytes == f.bytes);  // the trailer recomputed as the save wrote it
  initEngine();
  MemoryFile later = withVersion(f, 6), one = withVersion(f, 1);
  TEST_ASSERT_EQUAL(Load::Status::BadFormat, loadSlot(later));
  TEST_ASSERT_EQUAL(Load::Status::Done, loadSlot(one));
}

// An empty loop (owner 2026-11-08: one run without rows) round-trips: its loop length and its run.
void test_an_empty_loop_round_trips() {
  const RunId r = run(RunKind::kRecord, {});
  gH.commit(0, manifest({r}, 1));
  gRuns.release(r);
  MemoryFile f;
  saveSlot(f);
  initEngine();
  TEST_ASSERT_EQUAL(Load::Status::Done, loadSlot(f));
  const ManifestId m = gH.current(0);
  TEST_ASSERT_TRUE(m != kNoManifest);
  TEST_ASSERT_EQUAL_UINT32(kLoop, gManifests.manifest(m).loopLengthTicks);
  TEST_ASSERT_EQUAL_UINT8(1, gManifests.manifest(m).runCount);
  TEST_ASSERT_EQUAL_UINT32(0, gRuns.run(gManifests.manifest(m).runs[0]).rowCount);
  TEST_ASSERT_TRUE(gRes.check());
  TEST_ASSERT_TRUE(checkConservation(gPool, gRuns, gManifests));
}

static void expectNothingHeld(uint16_t freePages, uint16_t freeRuns, uint16_t freeManifests) {
  while (gRuns.reclaimPendingRuns() != 0) gRuns.reclaim(gPool, 64);
  TEST_ASSERT_EQUAL_UINT16(freePages, gPool.freeCount());
  TEST_ASSERT_EQUAL_UINT16(freeRuns, gRuns.freeCount());
  TEST_ASSERT_EQUAL_UINT16(freeManifests, gManifests.freeCount());
  TEST_ASSERT_TRUE(gRes.check());
  ManifestId probe[1];
  int16_t cur;
  bool below;
  TEST_ASSERT_EQUAL_UINT8(0, gH.chain(0, probe, 1, cur, below));
}

// A corrupted byte, a truncated file, a bad magic, an I/O error, too little content quota, an occupied slot: named,
// and nothing is left behind.
void test_failures_are_named_and_release_everything() {
  buildHistory();
  MemoryFile good;
  saveSlot(good);
  initEngine();
  const uint16_t pages = gPool.freeCount(), runs = gRuns.freeCount(), manifests = gManifests.freeCount();

  MemoryFile bad = good;
  bad.bytes[24 + 4 * 8 + 5] ^= 0x40;  // a row byte
  TEST_ASSERT_EQUAL(Load::Status::BadCrc, loadSlot(bad));
  expectNothingHeld(pages, runs, manifests);

  MemoryFile cut = good;
  cut.bytes.resize(good.bytes.size() - 7);
  TEST_ASSERT_EQUAL(Load::Status::IoError, loadSlot(cut));
  expectNothingHeld(pages, runs, manifests);

  MemoryFile magic = good;
  magic.bytes[0] ^= 1;
  TEST_ASSERT_EQUAL(Load::Status::BadFormat, loadSlot(magic));
  expectNothingHeld(pages, runs, manifests);

  MemoryFile io = good;
  io.failAfter = 200;
  TEST_ASSERT_EQUAL(Load::Status::IoError, loadSlot(io));
  expectNothingHeld(pages, runs, manifests);

  // Content quota nearly full: the rows do not fit, refused before any is read.
  const RunId filler = gRuns.allocate(1, RunKind::kRecord);
  PassEvent cc{};
  cc.status = 0xB0;
  while (gRes.accept(filler, cc, 0) == Admission::Ok && gPool.freeCount() > 1) {
  }
  const uint16_t pagesFull = gPool.freeCount(), runsFull = gRuns.freeCount();
  TEST_ASSERT_EQUAL(Load::Status::DeniedCapacity, loadSlot(good));
  expectNothingHeld(pagesFull, runsFull, manifests);
  gRuns.seal(filler);
  gRuns.release(filler);
  while (gRuns.reclaimPendingRuns() != 0) gRuns.reclaim(gPool, 64);

  TEST_ASSERT_EQUAL(Load::Status::Done, loadSlot(good));
  TEST_ASSERT_EQUAL(Load::Status::DeniedSlot, loadSlot(good));  // the slot has history now
}

// A take of more than 65,535 rows round-trips (the row reservation counts in 32 bits); each grain moves at most one
// chunk.
void test_large_take_round_trips_in_bounded_grains() {
  std::vector<PassEvent> rows;
  for (uint32_t i = 0; i < 35000; ++i) {
    rows.push_back(row(i % kLoop, 1 + i, 0x90, static_cast<uint8_t>(i % 128), 100));
    rows.push_back(row(i % kLoop, 1 + i, 0x80, static_cast<uint8_t>(i % 128), 0));
  }
  const RunId r = run(RunKind::kRecord, rows);
  gH.commit(0, manifest({r}, 1));
  gRuns.release(r);
  MemoryFile f;
  TEST_ASSERT_EQUAL(Save::Status::Running, gSave.start(gPool, gRuns, gManifests, gH, 0, f, 35000, gScratch));
  size_t before = 0;
  while (gSave.step() == Save::Status::Running) {
    TEST_ASSERT_TRUE(f.bytes.size() - before <= 4096);
    before = f.bytes.size();
  }
  initEngine();
  f.pos = 0;
  TEST_ASSERT_EQUAL(Load::Status::Running, gLoad.start(gPool, gRuns, gManifests, gH, gRes, gIds, 0, f, gScratch));
  size_t pos = 0;
  while (gLoad.step() == Load::Status::Running) {
    TEST_ASSERT_TRUE(f.pos - pos <= 4096 + 64);
    pos = f.pos;
  }
  TEST_ASSERT_EQUAL(Load::Status::Done, gLoad.status());
  TEST_ASSERT_EQUAL_UINT32(70000, gRuns.run(gManifests.manifest(gH.current(0)).runs[0]).rowCount);
  TEST_ASSERT_TRUE(gRes.check());
}

// DEC-285 (slice 0 of the groove design; found by its review): a shortened loop's fit run names the content runs it
// filters. Saved and loaded, the runs get other RunIds — the lookups must still name the same runs, or a note past
// the shortened end is heard again.
static FitBuilder<Pool, Runs, Res> gFit;
static std::vector<Ev> composedAt(ManifestId m, uint32_t loop) {
  const auto& man = gManifests.manifest(m);
  SourceRun src[8];
  for (uint8_t i = 0; i < man.runCount; ++i) src[i] = {man.runs[i], gRuns.run(man.runs[i]).rowCount};
  TEST_ASSERT_TRUE(gC.begin(src, man.runCount, loop, man.identity));
  std::vector<Ev> out;
  while (gC.composeNext(gWin, loop))
    for (uint16_t k = 0; k < gWin.count; ++k)
      out.push_back({gWin.entries[k].tick, gWin.entries[k].noteId, gWin.entries[k].status, gWin.entries[k].data1,
                     gWin.entries[k].length});
  gC.end();
  return out;
}
void test_a_shortened_loop_round_trips() {
  // A 2-bar take shortened to 1 bar: the note at 1,000 lies past the end and must stay silent.
  const RunId pad = run(RunKind::kRecord, {});  // takes RunId 0, so the take's RunId differs after the load
  const RunId a = run(RunKind::kRecord, {row(100, 1, 0x90, 60, 100), row(300, 1, 0x80, 60, 0), row(1000, 2, 0x90, 62, 90),
                                         row(1200, 2, 0x80, 62, 0)});
  RunId fit = kNoRun;
  TEST_ASSERT_EQUAL(FitResult::Ok, gFit.build(gPool, gRuns, gRes, 0, &a, 1, 768, fit));
  const RunId list[2] = {a, fit};
  gH.commit(0, gManifests.create(0, 1, 768, list, 2, gRuns));
  for (RunId r : {a, fit, pad}) gRuns.release(r);
  while (gIds.peek() <= 2) gIds.advance();
  const auto want = composedAt(gH.current(0), 768);
  TEST_ASSERT_EQUAL_UINT32(1, want.size());  // the note at 100 only
  MemoryFile f;
  saveSlot(f);
  initEngine();
  TEST_ASSERT_EQUAL(Load::Status::Done, loadSlot(f));
  TEST_ASSERT_TRUE(gManifests.manifest(gH.current(0)).runs[0] != a);  // other RunIds now
  const auto got = composedAt(gH.current(0), 768);
  TEST_ASSERT_EQUAL_UINT32(want.size(), got.size());
  for (size_t k = 0; k < got.size(); ++k) TEST_ASSERT_TRUE(want[k] == got[k]);
  // The same file as version 3 (lookups keyed by RunId then): they are not loaded — none can name a wrong run.
  initEngine();
  MemoryFile three = withVersion(f, 3);
  TEST_ASSERT_EQUAL(Load::Status::Done, loadSlot(three));
  const auto& m = gManifests.manifest(gH.current(0));
  const RunId loadedFit = m.runs[1];
  TEST_ASSERT_EQUAL_UINT8(RunKind::kFit, gRuns.run(loadedFit).kind);
  for (uint32_t i = 0; i < gRuns.run(loadedFit).rowCount; ++i) {
    const uint8_t st = gRuns.rowAt(loadedFit, i, gPool).status;
    TEST_ASSERT_FALSE(st >= kFitFilterStatus && st <= kFitHeldStatus);
  }
  TEST_ASSERT_TRUE(gRes.check());
  TEST_ASSERT_TRUE(checkConservation(gPool, gRuns, gManifests));
}

void test_no_heap() {
  buildHistory();
  MemoryFile f;
  f.bytes.reserve(1 << 16);
  gTrap = true;
  const unsigned long before = gHeapAllocations;
  saveSlot(f);
  initEngine();
  TEST_ASSERT_EQUAL(Load::Status::Done, loadSlot(f));
  gTrap = false;
  TEST_ASSERT_EQUAL_UINT32(0, gHeapAllocations - before);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_round_trip_keeps_content_history_and_ids);
  RUN_TEST(test_version_one_loads_and_a_later_one_is_refused);
  RUN_TEST(test_an_empty_loop_round_trips);
  RUN_TEST(test_failures_are_named_and_release_everything);
  RUN_TEST(test_large_take_round_trips_in_bounded_grains);
  RUN_TEST(test_a_shortened_loop_round_trips);
  RUN_TEST(test_no_heap);
  return UNITY_END();
}
