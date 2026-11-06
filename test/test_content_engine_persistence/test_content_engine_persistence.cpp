//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// fixed-memory-engine E9.7a / E9.8g: saving and loading on the card (owner 2026-11-05: approval once, remembered by the
// engine folder; autosave when settled; owner 2026-11-06: a save must not hold the main loop — the A / B store), on the
// production root and a fixed-memory fake card. Nothing is written before approval; a slot is saved once it has
// settled, never while a take or NOTE_EDIT is open on it, and again after Undo, into the store's sectors at most two a
// step; a second engine booting from the card restores the slots exactly and saves nothing back; a corrupt copy falls
// back to the other, a slot whose copies fail fails by name; an interrupted save leaves the current copy whole; engine
// files of before the store are loaded and written into it; a new store's stale sectors never load; no allocation.

#include <unity.h>

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <new>
#include <vector>

#include "ContentEngine/EngineRoot.h"

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
using R = ProductionTransport::Result;

// ---- A fixed-memory card ---------------------------------------------------------------------------------------------
struct FakeCard : CardIo {
  static constexpr int kFiles = 12;
  static constexpr uint32_t kBytes = 1u << 19;
  static constexpr int kSectors = 1024;  // the store's sectors written so far (sparse: the store is ~277 MB)
  struct File {
    char name[48];
    bool used;
    uint32_t size;
    uint8_t data[kBytes];
  };
  struct Sector {
    uint32_t index;
    uint8_t data[512];
  };
  File files[kFiles];
  char dirs[4][48];
  int dirCount = 0;
  int reading = -1;
  uint32_t readAt = 0;
  Sector sectors[kSectors];
  int sectorCount = 0;
  bool storeExists = false, storeOpen = false;
  uint32_t storeSectors = 0;
  uint32_t writes = 0;                    // any mutation of the card by the engine
  uint32_t sectorWrites = 0, sectorReads = 0;
  uint32_t failSectorWritesAfter = 0xFFFFFFFFu;  // sector writes, then every one fails
  uint32_t failOneSectorWrite = 0xFFFFFFFFu;     // sector writes, then the next one fails (once)
  uint32_t stepWrites = 0, mostStepWrites = 0;   // sector writes in one engine step, and the most of them

  void reset() {
    for (auto& f : files) f.used = false;
    dirCount = 0;
    reading = -1;
    sectorCount = 0;
    storeExists = storeOpen = false;
    storeSectors = 0;
    failSectorWritesAfter = failOneSectorWrite = 0xFFFFFFFFu;
    writes = sectorWrites = sectorReads = stepWrites = mostStepWrites = 0;
  }
  int find(const char* p) const {
    for (int i = 0; i < kFiles; ++i)
      if (files[i].used && std::strcmp(files[i].name, p) == 0) return i;
    return -1;
  }
  uint8_t* sector(uint32_t i) {  // nullptr: never written (reads as stale card data)
    for (int k = 0; k < sectorCount; ++k)
      if (sectors[k].index == i) return sectors[k].data;
    return nullptr;
  }
  void endStep() {
    if (stepWrites > mostStepWrites) mostStepWrites = stepWrites;
    stepWrites = 0;
  }
  bool exists(const char* p) override {
    for (int i = 0; i < dirCount; ++i)
      if (std::strcmp(dirs[i], p) == 0) return true;
    return find(p) >= 0;
  }
  bool makeDir(const char* p) override {
    std::strcpy(dirs[dirCount++], p);
    ++writes;
    return true;
  }
  bool openRead(const char* p) override {
    reading = find(p);
    readAt = 0;
    return reading >= 0;
  }
  bool read(uint8_t* d, uint32_t n) override {
    if (reading < 0 || readAt + n > files[reading].size) return false;
    std::memcpy(d, files[reading].data + readAt, n);
    readAt += n;
    return true;
  }
  void closeRead() override { reading = -1; }
  bool openStore(const char* p, uint32_t n, bool create, bool& created) override {
    TEST_ASSERT_EQUAL_STRING("/MidiLooper/engine/store.bin", p);
    created = false;
    if (!storeExists) {
      if (!create) return false;
      storeExists = created = true;  // a new file over old card data: whatever the sectors held stays
      ++writes;
    }
    storeOpen = true;
    storeSectors = n;
    return true;
  }
  bool readSector(uint32_t i, uint8_t* d) override {
    if (!storeOpen || i >= storeSectors) return false;
    ++sectorReads;
    const uint8_t* s = sector(i);
    for (int k = 0; k < 512; ++k) d[k] = s ? s[k] : 0xA5;
    return true;
  }
  bool writeSector(uint32_t i, const uint8_t* d) override {
    if (!storeOpen || i >= storeSectors || sectorWrites >= failSectorWritesAfter) return false;
    if (sectorWrites == failOneSectorWrite) {
      failOneSectorWrite = 0xFFFFFFFFu;
      return false;
    }
    uint8_t* s = sector(i);
    if (s == nullptr) {
      TEST_ASSERT_TRUE(sectorCount < kSectors);
      sectors[sectorCount].index = i;
      s = sectors[sectorCount++].data;
    }
    std::memcpy(s, d, 512);
    ++writes;
    ++sectorWrites;
    ++stepWrites;
    return true;
  }
  // Fixtures: files as an earlier firmware left them.
  void put(const char* path, const uint8_t* d, uint32_t n) {
    int i = find(path);
    if (i < 0)
      for (i = 0; i < kFiles && files[i].used; ++i) {
      }
    TEST_ASSERT_TRUE(i < kFiles && n <= kBytes);
    files[i].used = true;
    std::strcpy(files[i].name, path);
    files[i].size = n;
    std::memcpy(files[i].data, d, n);
  }
};

static FakeCard gCard;
static EnginePsram gPsramA, gPsramB;
static EngineRam2 gRam2A, gRam2B;
static EngineRoot gA, gB;

constexpr uint32_t kUsPerTick = 1040;
constexpr uint32_t kBar = kTicksPerBar;
static uint32_t gClock = 0;
static uint32_t us(uint32_t c) { return c * kUsPerTick; }

struct Live {
  uint32_t on, off;
  uint8_t pitch;
};
static void run(EngineRoot& e, uint32_t to, const std::vector<Live>& live = {}) {
  ProductionHub::Sent buf[32];
  for (; gClock < to; ++gClock) {
    e.submitJobs(gClock, us(gClock), kUsPerTick);
    gCard.endStep();
    while (e.runGrain(us(gClock))) gCard.endStep();
    gCard.endStep();
    e.clockTick(gClock, us(gClock), kUsPerTick);
    for (uint8_t p = 0; p < 3; ++p) e.transmit(p, us(gClock) + 1000, buf, 32);
    for (const Live& l : live) {
      if (l.off == gClock) e.inputNoteOff(gClock, 0x80, l.pitch, us(gClock));
      if (l.on == gClock) e.inputNoteOn(gClock, 0x90, l.pitch, 100, us(gClock), kUsPerTick);
    }
  }
}
constexpr uint32_t kSettleTicks = ProductionPersistence::kSettleUs / kUsPerTick + 2;
constexpr uint32_t kCopy = ProductionPersistence::kCopySectors;
static uint32_t copyBase(uint8_t s, uint8_t c) { return 2 + (2u * s + c) * kCopy; }
static uint32_t word(const uint8_t* b, int k) {
  return b[4 * k] | uint32_t(b[4 * k + 1]) << 8 | uint32_t(b[4 * k + 2]) << 16 | uint32_t(b[4 * k + 3]) << 24;
}
/// Slot `s`'s copy with the later sequence (-1: none written).
static int laterCopy(uint8_t s) {
  int best = -1;
  uint32_t seq = 0;
  for (uint8_t c = 0; c < 2; ++c) {
    const uint8_t* h = gCard.sector(copyBase(s, c));
    if (h == nullptr || word(h, 0) != 0x31434C4Du) continue;
    if (best < 0 || static_cast<int32_t>(word(h, 2) - seq) > 0) {
      best = c;
      seq = word(h, 2);
    }
  }
  return best;
}
static void bootB() {
  TEST_ASSERT_TRUE(gB.init(gPsramB, gRam2B, 2000));
  gB.attachCard(gCard);
  gB.startBootLoad();
  run(gB, gClock + 30);
}

void setUp() {
  gCard.reset();
  TEST_ASSERT_TRUE(gA.init(gPsramA, gRam2A, 2000));
  gA.attachCard(gCard);
  gClock = 0;
}
void tearDown() {}

/// Track `t` records a take into `slot` (4 bars, two notes) through queued Record / Stop.
static void record(EngineRoot& e, uint8_t t, uint8_t slot, uint8_t pitch) {
  e.control().selectTrack(t);
  TEST_ASSERT_EQUAL(R::Ok, e.control().queueRecord(t, slot, gClock));
  const uint32_t s = (gClock / kBar + 1) * kBar;
  const std::vector<Live> take = {{s + 10, s + 200, pitch}, {s + 1700, s + 1900, static_cast<uint8_t>(pitch + 2)}};
  run(e, s + 4 * kBar - 10, take);
  TEST_ASSERT_EQUAL(R::Ok, e.control().queueStop(t, gClock));
  run(e, s + 4 * kBar + 1, take);
}

// ---- Tests -------------------------------------------------------------------------------------------------------------
void test_nothing_is_written_before_approval() {
  TEST_ASSERT_FALSE(gA.persistence().approved());
  record(gA, 0, 0, 60);
  run(gA, gClock + 3 * kSettleTicks);
  TEST_ASSERT_EQUAL_UINT32(0, gCard.writes);  // not even the folder
  TEST_ASSERT_FALSE(gCard.storeExists);
  TEST_ASSERT_TRUE(gA.approveSaving());
  TEST_ASSERT_TRUE(gCard.exists(ProductionPersistence::kFolder));
  TEST_ASSERT_TRUE(gCard.storeExists && gA.persistence().storeReady());  // created with the approval
  run(gA, gClock + kSettleTicks + 50);
  TEST_ASSERT_EQUAL_UINT32(1, gA.persistence().stats().saves);
  TEST_ASSERT_EQUAL_INT(0, laterCopy(0));        // the first save: copy A
  TEST_ASSERT_EQUAL_INT(-1, gCard.find("/MidiLooper/engine/slot_00.mle"));  // no file: the store only
}

void test_autosave_waits_for_settle_and_free_slots() {
  gA.approveSaving();
  record(gA, 0, 0, 60);
  run(gA, gClock + kSettleTicks / 2);
  TEST_ASSERT_EQUAL_UINT32(0, gA.persistence().stats().saves);  // not settled yet
  run(gA, gClock + kSettleTicks);
  TEST_ASSERT_EQUAL_UINT32(1, gA.persistence().stats().saves);
  // An overdub open on the slot: no save until it stops and settles.
  TEST_ASSERT_EQUAL(R::Ok, gA.control().startOverdub(0, gClock));
  const uint32_t o = gClock;
  run(gA, o + 3 * 4 * kBar, {{o + 100, o + 300, 70}, {o + 4 * kBar + 100, o + 4 * kBar + 300, 71}});
  TEST_ASSERT_EQUAL_UINT32(1, gA.persistence().stats().saves);
  TEST_ASSERT_EQUAL(R::Ok, gA.control().stopNow(gClock, us(gClock), kUsPerTick));
  run(gA, gClock + kSettleTicks + 50);
  TEST_ASSERT_EQUAL_UINT32(2, gA.persistence().stats().saves);
  TEST_ASSERT_EQUAL_INT(1, laterCopy(0));  // the second: copy B
  // Undo is a change too.
  TEST_ASSERT_EQUAL(R::Ok, gA.control().undo(0, gClock, us(gClock), kUsPerTick));
  run(gA, gClock + kSettleTicks + 50);
  TEST_ASSERT_EQUAL_UINT32(3, gA.persistence().stats().saves);
  TEST_ASSERT_EQUAL_INT(0, laterCopy(0));  // and A again
  // NOTE_EDIT open on the slot: held back until it is left.
  gA.noteEdit().request({0, 0, 4 * kBar, 0, 127}, 0);
  run(gA, gClock + 20);
  TEST_ASSERT_EQUAL(EditResult::Ok, gA.noteEdit().enter());
  EditNote n = gA.noteEdit().note(0);
  n.velocity = 40;
  const EditChange c{0, n};
  gA.noteEdit().apply(&c, 1, 0);
  run(gA, gClock + 2 * kSettleTicks);
  TEST_ASSERT_EQUAL_UINT32(3, gA.persistence().stats().saves);
  gA.noteEdit().leave();
  run(gA, gClock + kSettleTicks + 50);
  TEST_ASSERT_EQUAL_UINT32(4, gA.persistence().stats().saves);
  TEST_ASSERT_TRUE(gA.persistence().savedCurrent(0));
}

void test_a_second_engine_boots_from_the_card() {
  gA.approveSaving();
  record(gA, 0, 0, 60);
  record(gA, 1, 9, 72);
  TEST_ASSERT_EQUAL(R::Ok, gA.control().startOverdub(0, gClock));
  const uint32_t o = gClock;
  std::vector<Live> notes = {{o + 600, o + 700, 64}};
  for (uint8_t i = 0; i < 48; ++i)  // enough content that slot 0's body spans several sectors
    notes.push_back({o + 100 + i * 60u, o + 130 + i * 60u, static_cast<uint8_t>(40 + i)});
  run(gA, o + 4 * kBar - 100, notes);
  TEST_ASSERT_EQUAL(R::Ok, gA.control().stopNow(gClock, us(gClock), kUsPerTick));
  gCard.mostStepWrites = 0;
  const uint32_t before = gCard.sectorWrites;
  run(gA, gClock + kSettleTicks + 100);
  TEST_ASSERT_TRUE(gA.persistence().savedCurrent(0) && gA.persistence().savedCurrent(9));  // each saved when settled
  // A save is sector writes, at most one in a step (owner 2026-11-06: a save must not hold the main loop; one took up
  // to 8 ms on the device); slot 0 took several.
  TEST_ASSERT_TRUE(gCard.sectorWrites - before > 4);
  TEST_ASSERT_EQUAL_UINT32(1, gCard.mostStepWrites);
  const uint32_t writes = gCard.writes;
  // Boot: a fresh engine on the same card.
  TEST_ASSERT_TRUE(gB.init(gPsramB, gRam2B, 2000));
  gB.attachCard(gCard);
  TEST_ASSERT_TRUE(gB.persistence().approved());  // remembered: the folder
  gB.startBootLoad();
  const uint32_t boot = gClock;
  run(gB, boot + 20);
  TEST_ASSERT_EQUAL_UINT32(2, gB.persistence().stats().loads);
  TEST_ASSERT_EQUAL_UINT32(0, gB.persistence().stats().loadFailed);
  TEST_ASSERT_EQUAL_UINT32(0, gB.persistence().stats().migrated);
  for (uint8_t s : {0, 9}) {
    TEST_ASSERT_EQUAL_UINT8(gA.history().undoDepth(s), gB.history().undoDepth(s));
    const auto& ma = gA.manifests().manifest(gA.history().current(s));
    const auto& mb = gB.manifests().manifest(gB.history().current(s));
    TEST_ASSERT_EQUAL_UINT32(ma.loopLengthTicks, mb.loopLengthTicks);
    TEST_ASSERT_EQUAL_UINT8(ma.runCount, mb.runCount);
    for (uint8_t r = 0; r < ma.runCount; ++r)
      TEST_ASSERT_EQUAL_UINT32(gA.runs().run(ma.runs[r]).rowCount, gB.runs().run(mb.runs[r]).rowCount);
  }
  run(gB, gClock + 2 * kSettleTicks);
  TEST_ASSERT_EQUAL_UINT32(writes, gCard.writes);  // loading saves nothing back
  TEST_ASSERT_EQUAL_UINT32(0, gB.persistence().stats().saves);
}

void test_a_save_after_a_boot_wins_without_reading_the_card() {
  gA.approveSaving();
  record(gA, 0, 0, 60);
  run(gA, gClock + kSettleTicks + 50);  // slot 0: copy A, sequence 1
  TEST_ASSERT_EQUAL(R::Ok, gA.control().undo(0, gClock, us(gClock), kUsPerTick));
  run(gA, gClock + kSettleTicks + 50);  // copy B, 2
  record(gA, 1, 9, 72);
  run(gA, gClock + kSettleTicks + 50);  // slot 9: 3
  TEST_ASSERT_EQUAL_UINT32(3, gA.persistence().stats().saves);
  TEST_ASSERT_EQUAL_INT(1, laterCopy(0));
  bootB();
  TEST_ASSERT_EQUAL_UINT32(2, gB.persistence().stats().loads);
  // A change after the boot: its save goes to slot 0's copy A, past every sequence the boot found — and reads no
  // sector (a read took 10 ms on the device).
  TEST_ASSERT_EQUAL(R::Ok, gB.control().redo(0, gClock, us(gClock), kUsPerTick));
  const uint32_t reads = gCard.sectorReads;
  run(gB, gClock + kSettleTicks + 50);
  TEST_ASSERT_EQUAL_UINT32(1, gB.persistence().stats().saves);
  TEST_ASSERT_EQUAL_UINT32(reads, gCard.sectorReads);
  TEST_ASSERT_EQUAL_INT(0, laterCopy(0));
  TEST_ASSERT_TRUE(word(gCard.sector(copyBase(0, 0)), 2) > 3);
  // And the next boot loads it: the redone slot.
  const uint8_t depth = gB.history().undoDepth(0);
  TEST_ASSERT_TRUE(gA.init(gPsramA, gRam2A, 2000));
  gA.attachCard(gCard);
  gA.startBootLoad();
  run(gA, gClock + 30);
  TEST_ASSERT_EQUAL_UINT32(0, gA.persistence().stats().loadFailed);
  TEST_ASSERT_EQUAL_UINT8(depth, gA.history().undoDepth(0));
}

void test_a_corrupt_copy_falls_back_and_an_interrupted_save_keeps_the_current_copy() {
  gA.approveSaving();
  record(gA, 0, 0, 60);
  record(gA, 1, 1, 62);  // track 1: plays nothing, so its Record starts on the bar
  run(gA, gClock + kSettleTicks + 100);
  TEST_ASSERT_EQUAL_UINT32(2, gA.persistence().stats().saves);
  // An interrupted save of slot 0 (the card fails part way): copy A, the current one, stays whole.
  TEST_ASSERT_EQUAL(R::Ok, gA.control().undo(0, gClock, us(gClock), kUsPerTick));
  gCard.failSectorWritesAfter = gCard.sectorWrites + 1;  // one body sector of B
  run(gA, gClock + kSettleTicks + 100);
  TEST_ASSERT_EQUAL_UINT32(1, gA.persistence().stats().saveFailed);
  TEST_ASSERT_EQUAL_INT(0, laterCopy(0));
  gCard.failSectorWritesAfter = 0xFFFFFFFFu;
  // Slot 1's only copy corrupted: it fails by name; slot 0 loads its recorded take (not the undone state).
  gCard.sector(copyBase(1, 0) + 1)[40] ^= 0x5A;
  bootB();
  TEST_ASSERT_EQUAL_UINT32(1, gB.persistence().stats().loads);
  TEST_ASSERT_EQUAL_UINT32(1, gB.persistence().stats().loadFailed);
  TEST_ASSERT_EQUAL_UINT64(1ull << 1, gB.persistence().stats().loadFailedMask);
  TEST_ASSERT_EQUAL(kNoManifest, gB.history().current(1));
  TEST_ASSERT_NOT_EQUAL(kNoManifest, gB.history().current(0));
  // Slot 0 saved twice more (redo: B, the take; undo: A, the undone state), then A damaged: boot falls back to B.
  run(gA, gClock + 1);
  TEST_ASSERT_EQUAL(R::Ok, gA.control().redo(0, gClock, us(gClock), kUsPerTick));
  run(gA, gClock + kSettleTicks + 100);
  TEST_ASSERT_EQUAL(R::Ok, gA.control().undo(0, gClock, us(gClock), kUsPerTick));
  run(gA, gClock + kSettleTicks + 100);
  TEST_ASSERT_EQUAL_INT(0, laterCopy(0));  // redo saved B, undo saved A
  bootB();  // the later copy: the undone state, its take to redo
  TEST_ASSERT_EQUAL_UINT8(gA.history().redoDepth(0), gB.history().redoDepth(0));
  TEST_ASSERT_EQUAL_UINT8(1, gB.history().redoDepth(0));
  gCard.sector(copyBase(0, 0) + 1)[40] ^= 0x5A;  // the later copy (the undone state) damaged
  bootB();
  TEST_ASSERT_EQUAL_UINT64(1ull << 1, gB.persistence().stats().loadFailedMask);  // slot 1 still; slot 0 loaded
  TEST_ASSERT_NOT_EQUAL(kNoManifest, gB.history().current(0));  // copy B: the take (A held the undone state)
}

void test_a_failed_last_sector_leaves_the_copy_unsealed() {
  gA.approveSaving();
  record(gA, 0, 0, 60);
  run(gA, gClock + kSettleTicks + 100);  // A: the take
  TEST_ASSERT_EQUAL(R::Ok, gA.control().undo(0, gClock, us(gClock), kUsPerTick));
  uint32_t before = gCard.sectorWrites;
  run(gA, gClock + kSettleTicks + 100);  // B: the undone state
  const uint32_t save = gCard.sectorWrites - before;  // its body, the last partial sector, the header
  TEST_ASSERT_TRUE(save >= 2);
  TEST_ASSERT_EQUAL(R::Ok, gA.control().redo(0, gClock, us(gClock), kUsPerTick));
  run(gA, gClock + kSettleTicks + 100);  // A: the take again
  // The same undone state saved into B again, the card failing at its last body sector only: the header, which the
  // card would take, is not written.
  const uint8_t header[512] = {};
  gCard.writeSector(copyBase(0, 1), header);
  TEST_ASSERT_EQUAL(R::Ok, gA.control().undo(0, gClock, us(gClock), kUsPerTick));
  gCard.failOneSectorWrite = gCard.sectorWrites + save - 2;
  run(gA, gClock + kSettleTicks + 100);
  TEST_ASSERT_EQUAL_UINT32(1, gA.persistence().stats().saveFailed);
  TEST_ASSERT_EQUAL_UINT32(0, word(gCard.sector(copyBase(0, 1)), 0));  // not sealed
  TEST_ASSERT_EQUAL_INT(0, laterCopy(0));
}

// Engine files of before the store (E9.8g): `slot_NN.mle`, or the complete `.tmp` an interrupted rename left, load and
// are written into the store; an incomplete `.tmp` with no `.mle` falls back to the slot's V1 file. The files stay.
struct VecSink {
  std::vector<uint8_t>* v;
  bool write(const uint8_t* d, uint32_t n) {
    v->insert(v->end(), d, d + n);
    return true;
  }
};
static std::vector<uint8_t> engineFile(EngineRoot& e, uint8_t slot) {
  std::vector<uint8_t> bytes, scratch(ProductionPersistence::kChunk);
  VecSink sink{&bytes};
  using Job = SaveJob<ProductionPool, ProductionRuns, ProductionManifests, ProductionHistory, VecSink>;
  Job job;
  TEST_ASSERT_EQUAL(Job::Status::Running,
                    job.start(const_cast<ProductionPool&>(e.pool()), const_cast<ProductionRuns&>(e.runs()),
                              const_cast<ProductionManifests&>(e.manifests()), e.history(), slot, sink, 100000,
                              scratch.data()));
  while (job.step() == decltype(job)::Status::Running) {
  }
  TEST_ASSERT_EQUAL(decltype(job)::Status::Done, job.status());
  return bytes;
}
static std::vector<uint8_t> v1PassStream() {  // one record pass: 60 at 0-300, loop 3072 (the legacy writer's layout)
  std::vector<uint8_t> b;
  auto u32 = [&](uint32_t v) { for (int i = 0; i < 4; ++i) b.push_back(static_cast<uint8_t>(v >> (8 * i))); };
  u32(0x1142); b.push_back(1); b.push_back(0); u32(0);  // epoch header
  for (uint32_t w : {5u, 0u, 3072u, 0u, 9u, 100u, 7u, 4u}) u32(w);
  u32(1);
  u32(1); u32(0); b.push_back(0); b.push_back(0); u32(3072); u32(2);
  for (uint32_t t : {0u, 300u}) {
    u32(t); b.push_back(t == 0 ? 0x90 : 0x80); b.push_back(1); b.push_back(0); b.push_back(0); u32(7);
    b.push_back(60); b.push_back(t == 0 ? 100 : 0); for (int i = 0; i < 6; ++i) b.push_back(0);
  }
  u32(9); u32(0x45505433u); u32(0);
  return b;
}

void test_engine_files_of_before_the_store_are_written_into_it() {
  record(gA, 0, 0, 60);  // not approved: the slots' files are made here, as the earlier firmware wrote them
  record(gA, 2, 18, 64);
  const std::vector<uint8_t> f0 = engineFile(gA, 0), f18 = engineFile(gA, 18);
  gCard.makeDir(ProductionPersistence::kFolder);  // approved by the earlier firmware
  gCard.put("/MidiLooper/engine/slot_00.mle", f0.data(), static_cast<uint32_t>(f0.size()));
  gCard.put("/MidiLooper/engine/slot_18.tmp", f18.data(), static_cast<uint32_t>(f18.size()));  // a replace interrupted
  const uint8_t junk[8] = {1, 2, 3, 4, 5, 6, 7, 8};  // slot 3: a first save interrupted, beside its V1 file
  gCard.put("/MidiLooper/engine/slot_03.tmp", junk, sizeof junk);
  const std::vector<uint8_t> v1 = v1PassStream();
  gCard.put("/MidiLooper/current/slots/loop_00_03.bin", v1.data(), static_cast<uint32_t>(v1.size()));
  bootB();
  TEST_ASSERT_TRUE(gB.persistence().storeReady());  // created at the first boot with approval
  TEST_ASSERT_EQUAL_UINT32(2, gB.persistence().stats().loads);
  TEST_ASSERT_EQUAL_UINT32(2, gB.persistence().stats().migrated);
  TEST_ASSERT_EQUAL_UINT32(1, gB.persistence().stats().v1Imported);
  TEST_ASSERT_EQUAL_UINT8(gA.history().undoDepth(0), gB.history().undoDepth(0));
  run(gB, gClock + 2 * kSettleTicks);
  TEST_ASSERT_EQUAL_UINT32(3, gB.persistence().stats().saves);  // all three into the store
  TEST_ASSERT_TRUE(laterCopy(0) >= 0 && laterCopy(18) >= 0 && laterCopy(3) >= 0);
  // From the store from now on: the files may go bad, the slots still load.
  gCard.files[gCard.find("/MidiLooper/engine/slot_00.mle")].data[30] ^= 0x5A;
  bootB();
  TEST_ASSERT_EQUAL_UINT32(3, gB.persistence().stats().loads);
  TEST_ASSERT_EQUAL_UINT32(0, gB.persistence().stats().migrated);
  TEST_ASSERT_EQUAL_UINT32(0, gB.persistence().stats().v1Imported);
  TEST_ASSERT_EQUAL_UINT32(0, gB.persistence().stats().loadFailed);
  // A slot in the store whose copy fails stays empty, by name: the older file beside it is not taken.
  gCard.sector(copyBase(18, laterCopy(18)) + 1)[0] ^= 0x5A;  // its body damaged
  bootB();
  TEST_ASSERT_EQUAL_UINT64(1ull << 18, gB.persistence().stats().loadFailedMask);
  TEST_ASSERT_EQUAL(kNoManifest, gB.history().current(18));
  TEST_ASSERT_EQUAL_UINT32(0, gB.persistence().stats().migrated);
}

// A store created over old card data (a store deleted and made again lands on the same sectors): its headers are
// cleared at creation, so nothing stale loads.
void test_a_new_store_never_loads_stale_sectors() {
  gA.approveSaving();
  record(gA, 0, 0, 60);
  run(gA, gClock + kSettleTicks + 100);
  TEST_ASSERT_EQUAL_INT(0, laterCopy(0));
  gCard.storeExists = gCard.storeOpen = false;  // the file deleted: its sectors keep their bytes
  bootB();  // approved (the folder): a new store
  TEST_ASSERT_TRUE(gB.persistence().storeReady());
  TEST_ASSERT_EQUAL_UINT32(0, gB.persistence().stats().loads);
  TEST_ASSERT_EQUAL(kNoManifest, gB.history().current(0));
}

// A copy is the slot's only where the header names the slot: slot 0's copy placed in slot 5's region does not load.
void test_a_copy_loads_only_into_its_own_slot() {
  gA.approveSaving();
  record(gA, 0, 0, 60);
  run(gA, gClock + kSettleTicks + 100);
  for (uint32_t i = 0; i < 4; ++i) {
    const uint8_t* from = gCard.sector(copyBase(0, 0) + i);
    if (from == nullptr) break;
    uint8_t copy[512];
    std::memcpy(copy, from, 512);
    gCard.writeSector(copyBase(5, 0) + i, copy);
  }
  bootB();
  TEST_ASSERT_EQUAL_UINT32(1, gB.persistence().stats().loads);  // slot 0 only
  TEST_ASSERT_EQUAL(kNoManifest, gB.history().current(5));
}

// Owner 2026-11-06: the selected track and slots survive a reboot. Saved like a slot (only after approval, once
// settled) into the store's two state sectors in turn; a second engine restores the later one, the earlier one when
// the later is damaged, nothing when both are.
void test_the_surface_state_is_saved_and_restored() {
  const uint8_t state[ProductionPersistence::kStateBytes] = {7, 0, 9, 18, 24, 32, 41, 50, 63};
  gA.setSurfaceState(state, us(gClock));
  run(gA, gClock + kSettleTicks + 20);
  TEST_ASSERT_EQUAL_UINT32(0, gCard.writes);  // not approved: nothing written
  gA.approveSaving();
  run(gA, gClock + kSettleTicks / 2);
  gA.setSurfaceState(state, us(gClock));  // unchanged: no new settle time
  run(gA, gClock + kSettleTicks);
  TEST_ASSERT_NOT_NULL(gCard.sector(0));
  const uint32_t writes = gCard.writes;
  run(gA, gClock + kSettleTicks + 20);
  TEST_ASSERT_EQUAL_UINT32(writes, gCard.writes);  // saved once
  // A new selection is saved only once it has settled (2 s), into the other sector.
  const uint8_t next[ProductionPersistence::kStateBytes] = {3, 1, 9, 18, 24, 32, 41, 50, 63};
  gA.setSurfaceState(next, us(gClock));
  run(gA, gClock + kSettleTicks / 2);
  TEST_ASSERT_EQUAL_UINT32(writes, gCard.writes);
  run(gA, gClock + kSettleTicks);
  TEST_ASSERT_EQUAL_UINT32(writes + 1, gCard.writes);
  TEST_ASSERT_NOT_NULL(gCard.sector(1));
  bootB();
  uint8_t back[ProductionPersistence::kStateBytes] = {};
  TEST_ASSERT_TRUE(gB.persistence().restoredSurfaceState(back));
  TEST_ASSERT_EQUAL_UINT8_ARRAY(next, back, ProductionPersistence::kStateBytes);
  // The later one damaged: the earlier selection.
  gCard.sector(1)[9] ^= 0x01;  // a bit of the state word
  bootB();
  TEST_ASSERT_TRUE(gB.persistence().restoredSurfaceState(back));
  TEST_ASSERT_EQUAL_UINT8_ARRAY(state, back, ProductionPersistence::kStateBytes);
  // A third selection goes back to sector 0 and is the one restored (the later sequence, whichever sector).
  gCard.sector(1)[9] ^= 0x01;  // repaired
  const uint8_t third[ProductionPersistence::kStateBytes] = {5, 2, 9, 18, 24, 32, 41, 50, 63};
  gA.setSurfaceState(third, us(gClock));
  run(gA, gClock + kSettleTicks + 20);
  bootB();
  TEST_ASSERT_TRUE(gB.persistence().restoredSurfaceState(back));
  TEST_ASSERT_EQUAL_UINT8_ARRAY(third, back, ProductionPersistence::kStateBytes);
  // Both damaged: nothing restored.
  gCard.sector(0)[9] ^= 0x01;
  gCard.sector(1)[9] ^= 0x01;
  bootB();
  TEST_ASSERT_FALSE(gB.persistence().restoredSurfaceState(back));
}

// The state file of before the store (`surface.mls`, "MLS1") is restored and written into the store.
void test_the_state_file_of_before_the_store_is_restored() {
  gCard.makeDir(ProductionPersistence::kFolder);
  const uint32_t st = 3u | 1u << 3 | 2u << 6;  // track 3; track 0's slot 1, track 1's slot 2 (+8)
  const uint32_t w[3] = {0x31534C4Du, st, (st * 2654435761u) ^ 0xA5A5A5A5u};
  uint8_t b[12];
  for (int i = 0; i < 12; ++i) b[i] = static_cast<uint8_t>(w[i / 4] >> (8 * (i % 4)));
  gCard.put("/MidiLooper/engine/surface.mls", b, sizeof b);
  bootB();
  uint8_t back[ProductionPersistence::kStateBytes] = {};
  TEST_ASSERT_TRUE(gB.persistence().restoredSurfaceState(back));
  TEST_ASSERT_EQUAL_UINT8(3, back[0]);
  TEST_ASSERT_EQUAL_UINT8(1, back[1]);
  TEST_ASSERT_EQUAL_UINT8(10, back[2]);
  gB.setSurfaceState(back, us(gClock));
  run(gB, gClock + kSettleTicks + 20);
  TEST_ASSERT_NOT_NULL(gCard.sector(0));  // in the store
}

void test_no_heap() {
  gA.approveSaving();
  const std::vector<Live> take = {{kBar + 10, kBar + 200, 60}};
  gTrap = true;
  const unsigned long before = gHeapAllocations;
  gA.control().queueRecord(0, 0, 0);
  run(gA, kBar + 1000, take);
  gA.control().stopNow(gClock, us(gClock), kUsPerTick);
  run(gA, gClock + kSettleTicks + 100);
  gB.init(gPsramB, gRam2B, 2000);
  gB.attachCard(gCard);
  gB.startBootLoad();
  run(gB, gClock + 20);
  gTrap = false;
  TEST_ASSERT_EQUAL_UINT32(0, gHeapAllocations - before);
  TEST_ASSERT_EQUAL_UINT32(1, gB.persistence().stats().loads);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_nothing_is_written_before_approval);
  RUN_TEST(test_autosave_waits_for_settle_and_free_slots);
  RUN_TEST(test_a_second_engine_boots_from_the_card);
  RUN_TEST(test_a_save_after_a_boot_wins_without_reading_the_card);
  RUN_TEST(test_a_corrupt_copy_falls_back_and_an_interrupted_save_keeps_the_current_copy);
  RUN_TEST(test_a_failed_last_sector_leaves_the_copy_unsealed);
  RUN_TEST(test_engine_files_of_before_the_store_are_written_into_it);
  RUN_TEST(test_a_new_store_never_loads_stale_sectors);
  RUN_TEST(test_a_copy_loads_only_into_its_own_slot);
  RUN_TEST(test_the_surface_state_is_saved_and_restored);
  RUN_TEST(test_the_state_file_of_before_the_store_is_restored);
  RUN_TEST(test_no_heap);
  return UNITY_END();
}
