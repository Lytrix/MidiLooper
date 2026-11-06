//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// fixed-memory-engine E9.7a: saving and loading on the card (owner 2026-11-05: approval once, remembered by the engine
// folder; autosave when settled), on the production root and a fixed-memory fake card. Nothing is written before
// approval; a slot is saved once it has settled, never while a take or NOTE_EDIT is open on it, and again after Undo;
// a second engine booting from the card restores the slots exactly and saves nothing back; a corrupt file fails by name
// and the other slots load; an interrupted save leaves the previous file whole; no allocation.

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
  struct File {
    char name[48];
    bool used;
    uint32_t size;
    uint8_t data[kBytes];
  };
  File files[kFiles];
  char dirs[4][48];
  int dirCount = 0;
  int reading = -1, writing = -1;
  uint32_t readAt = 0;
  uint32_t failWriteAfter = 0xFFFFFFFFu;  // bytes, then every write fails
  uint32_t writes = 0;                   // any mutation of the card

  void reset() {
    for (auto& f : files) f.used = false;
    dirCount = 0;
    reading = writing = -1;
    failWriteAfter = 0xFFFFFFFFu;
    writes = 0;
  }
  int find(const char* p) const {
    for (int i = 0; i < kFiles; ++i)
      if (files[i].used && std::strcmp(files[i].name, p) == 0) return i;
    return -1;
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
  bool openWrite(const char* p) override {
    int i = find(p);
    if (i < 0)
      for (i = 0; i < kFiles && files[i].used; ++i) {
      }
    if (i == kFiles) return false;
    files[i].used = true;
    std::strcpy(files[i].name, p);
    files[i].size = 0;
    writing = i;
    ++writes;
    return true;
  }
  bool write(const uint8_t* d, uint32_t n) override {
    if (writing < 0 || files[writing].size + n > kBytes || files[writing].size + n > failWriteAfter) return false;
    std::memcpy(files[writing].data + files[writing].size, d, n);
    files[writing].size += n;
    return true;
  }
  bool closeWrite() override {
    writing = -1;
    return true;
  }
  bool rename(const char* from, const char* to) override {
    const int f = find(from);
    if (f < 0) return false;
    const int t = find(to);
    if (t >= 0) files[t].used = false;
    std::strcpy(files[f].name, to);
    ++writes;
    return true;
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
    while (e.runGrain(us(gClock))) {
    }
    e.clockTick(gClock, us(gClock), kUsPerTick);
    for (uint8_t p = 0; p < 3; ++p) e.transmit(p, us(gClock) + 1000, buf, 32);
    for (const Live& l : live) {
      if (l.off == gClock) e.inputNoteOff(gClock, 0x80, l.pitch, us(gClock));
      if (l.on == gClock) e.inputNoteOn(gClock, 0x90, l.pitch, 100, us(gClock), kUsPerTick);
    }
  }
}
constexpr uint32_t kSettleTicks = ProductionPersistence::kSettleUs / kUsPerTick + 2;

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
  TEST_ASSERT_TRUE(gA.approveSaving());
  TEST_ASSERT_TRUE(gCard.exists(ProductionPersistence::kFolder));
  run(gA, gClock + kSettleTicks + 50);
  TEST_ASSERT_TRUE(gCard.exists("/MidiLooper/engine/slot_00.mle"));
  TEST_ASSERT_EQUAL_UINT32(1, gA.persistence().stats().saves);
  TEST_ASSERT_FALSE(gCard.exists("/MidiLooper/engine/slot_00.tmp"));
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
  // Undo is a change too.
  TEST_ASSERT_EQUAL(R::Ok, gA.control().undo(0, gClock, us(gClock), kUsPerTick));
  run(gA, gClock + kSettleTicks + 50);
  TEST_ASSERT_EQUAL_UINT32(3, gA.persistence().stats().saves);
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
  run(gA, o + 4 * kBar - 100, {{o + 600, o + 700, 64}});
  TEST_ASSERT_EQUAL(R::Ok, gA.control().stopNow(gClock, us(gClock), kUsPerTick));
  run(gA, gClock + kSettleTicks + 100);
  TEST_ASSERT_TRUE(gA.persistence().savedCurrent(0) && gA.persistence().savedCurrent(9));  // each saved when settled
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

void test_a_corrupt_file_fails_by_name_and_an_interrupted_save_keeps_the_old_file() {
  gA.approveSaving();
  record(gA, 0, 0, 60);
  record(gA, 1, 1, 62);  // track 1: plays nothing, so its Record starts on the bar
  run(gA, gClock + kSettleTicks + 100);
  TEST_ASSERT_EQUAL_UINT32(2, gA.persistence().stats().saves);
  const int f0 = gCard.find("/MidiLooper/engine/slot_00.mle");
  const uint32_t size0 = gCard.files[f0].size;
  // An interrupted save of slot 0 (the card fails part way): the previous file stays whole.
  TEST_ASSERT_EQUAL(R::Ok, gA.control().undo(0, gClock, us(gClock), kUsPerTick));
  gCard.failWriteAfter = 100;
  run(gA, gClock + kSettleTicks + 100);
  TEST_ASSERT_EQUAL_UINT32(1, gA.persistence().stats().saveFailed);
  TEST_ASSERT_EQUAL_UINT32(size0, gCard.files[gCard.find("/MidiLooper/engine/slot_00.mle")].size);
  gCard.failWriteAfter = 0xFFFFFFFFu;
  // Slot 1's file corrupted: it fails by name, slot 0 loads.
  const int f1 = gCard.find("/MidiLooper/engine/slot_01.mle");
  gCard.files[f1].data[gCard.files[f1].size / 2] ^= 0x5A;
  TEST_ASSERT_TRUE(gB.init(gPsramB, gRam2B, 2000));
  gB.attachCard(gCard);
  gB.startBootLoad();
  run(gB, gClock + 20);
  TEST_ASSERT_EQUAL_UINT32(1, gB.persistence().stats().loads);
  TEST_ASSERT_EQUAL_UINT32(1, gB.persistence().stats().loadFailed);
  TEST_ASSERT_EQUAL_UINT64(1ull << 1, gB.persistence().stats().loadFailedMask);
  TEST_ASSERT_EQUAL(kNoManifest, gB.history().current(1));
  TEST_ASSERT_NOT_EQUAL(kNoManifest, gB.history().current(0));
}

// E9.8a: a card whose rename cannot replace removes the old file first. A save interrupted between the two leaves only
// the complete `.tmp`: boot loads it (and the next save writes the `.mle` again). A first save interrupted part way
// (an incomplete `.tmp`, no `.mle`) falls back to the slot's V1 file.
static void put(const char* path, const std::vector<uint8_t>& bytes) {
  gCard.openWrite(path);
  gCard.write(bytes.data(), static_cast<uint32_t>(bytes.size()));
  gCard.closeWrite();
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

void test_an_interrupted_replace_boots_from_the_tmp_file() {
  gA.approveSaving();
  record(gA, 0, 0, 60);
  run(gA, gClock + kSettleTicks + 100);
  TEST_ASSERT_EQUAL_UINT32(1, gA.persistence().stats().saves);
  // Interrupted after the old `.mle` was removed, before the rename: only the complete `.tmp` is left.
  std::strcpy(gCard.files[gCard.find("/MidiLooper/engine/slot_00.mle")].name, "/MidiLooper/engine/slot_00.tmp");
  // Slot 3: a first save interrupted part way (garbage `.tmp`, no `.mle`) beside its V1 file.
  put("/MidiLooper/engine/slot_03.tmp", {1, 2, 3, 4, 5, 6, 7, 8});
  put("/MidiLooper/current/slots/loop_00_03.bin", v1PassStream());
  TEST_ASSERT_TRUE(gB.init(gPsramB, gRam2B, 2000));
  gB.attachCard(gCard);
  gB.startBootLoad();
  run(gB, gClock + 30);
  TEST_ASSERT_EQUAL_UINT32(1, gB.persistence().stats().loads);
  TEST_ASSERT_EQUAL_UINT32(0, gB.persistence().stats().loadFailed);
  TEST_ASSERT_EQUAL_UINT8(gA.history().undoDepth(0), gB.history().undoDepth(0));
  TEST_ASSERT_EQUAL_UINT32(1, gB.persistence().stats().v1Imported);  // slot 3
  TEST_ASSERT_NOT_EQUAL(kNoManifest, gB.history().current(3));
  // Loaded from the `.tmp`: saved again as the `.mle`.
  run(gB, gClock + kSettleTicks + 100);
  TEST_ASSERT_TRUE(gCard.exists("/MidiLooper/engine/slot_00.mle"));
  TEST_ASSERT_TRUE(gB.persistence().savedCurrent(0));
}

// Owner 2026-11-06: the selected track and slots survive a reboot. Saved like a slot (only after approval, once
// settled); a second engine on the same card restores them; a damaged file restores nothing.
void test_the_surface_state_is_saved_and_restored() {
  const uint8_t state[ProductionPersistence::kStateBytes] = {7, 0, 9, 18, 24, 32, 41, 50, 63};
  gA.setSurfaceState(state, us(gClock));
  run(gA, gClock + kSettleTicks + 20);
  TEST_ASSERT_FALSE(gCard.exists("/MidiLooper/engine/surface.mls"));  // not approved: nothing written
  TEST_ASSERT_EQUAL_UINT32(0, gCard.writes);
  gA.approveSaving();
  run(gA, gClock + kSettleTicks / 2);
  gA.setSurfaceState(state, us(gClock));  // unchanged: no new settle time
  run(gA, gClock + kSettleTicks);
  TEST_ASSERT_TRUE(gCard.exists("/MidiLooper/engine/surface.mls"));
  TEST_ASSERT_FALSE(gCard.exists("/MidiLooper/engine/surface.tmp"));
  const uint32_t writes = gCard.writes;
  run(gA, gClock + kSettleTicks + 20);
  TEST_ASSERT_EQUAL_UINT32(writes, gCard.writes);  // saved once
  // A new selection is saved only once it has settled (2 s).
  const uint8_t next[ProductionPersistence::kStateBytes] = {3, 1, 9, 18, 24, 32, 41, 50, 63};
  gA.setSurfaceState(next, us(gClock));
  run(gA, gClock + kSettleTicks / 2);
  TEST_ASSERT_EQUAL_UINT32(writes, gCard.writes);
  run(gA, gClock + kSettleTicks);
  TEST_ASSERT_TRUE(gCard.writes > writes);
  TEST_ASSERT_TRUE(gB.init(gPsramB, gRam2B, 2000));
  gB.attachCard(gCard);
  gB.startBootLoad();
  run(gB, gClock + 20);
  uint8_t back[ProductionPersistence::kStateBytes] = {};
  TEST_ASSERT_TRUE(gB.persistence().restoredSurfaceState(back));
  TEST_ASSERT_EQUAL_UINT8_ARRAY(next, back, ProductionPersistence::kStateBytes);
  // Damaged: nothing restored.
  const int f = gCard.find("/MidiLooper/engine/surface.mls");
  gCard.files[f].data[5] ^= 0x01;  // a bit of the state word
  TEST_ASSERT_TRUE(gB.init(gPsramB, gRam2B, 2000));
  gB.attachCard(gCard);
  gB.startBootLoad();
  run(gB, gClock + 20);
  TEST_ASSERT_FALSE(gB.persistence().restoredSurfaceState(back));
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
  RUN_TEST(test_a_corrupt_file_fails_by_name_and_an_interrupted_save_keeps_the_old_file);
  RUN_TEST(test_an_interrupted_replace_boots_from_the_tmp_file);
  RUN_TEST(test_the_surface_state_is_saved_and_restored);
  RUN_TEST(test_no_heap);
  return UNITY_END();
}
