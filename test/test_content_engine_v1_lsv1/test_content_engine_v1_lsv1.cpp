//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// fixed-memory-engine E9.7b: V1 slot files imported at boot (owner 2026-11-05: exact current content; pass-stream files
// skipped and reported), from LSV1 bytes built here exactly as the old firmware writes them on the Teensy (the 10-byte
// epoch header, LoopMetadata, the PersistedLoopSnapshot oracle, LoopSnapshot, SAVE; `MidiEvent` 20 bytes, payload at 12).
// Checked: notes with their stored geometry (one held across the loop end stored wrapped, one stored unwrapped), CC,
// program change and pitch bend; a pass-stream file and a shifted loop start skipped and named; a truncated file
// failing by name while the other slots import; V1 files never written, the imported slot saved in the new format
// once approved; an engine file taking precedence over the slot's V1 file; no allocation.

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
static EnginePsram gPsram;
static EngineRam2 gRam2;
static EngineRoot gE;
constexpr uint32_t kUsPerTick = 1040;
static uint32_t gClock = 0;
static uint32_t us(uint32_t c) { return c * kUsPerTick; }
static void run(uint32_t to) {
  for (; gClock < to; ++gClock) {
    gE.submitJobs(gClock, us(gClock), kUsPerTick);
    while (gE.runGrain(us(gClock))) {
    }
    gE.clockTick(gClock, us(gClock), kUsPerTick);
  }
}

// ---- LSV1 bytes, as the old firmware writes them ------------------------------------------------------------------------
struct Bytes {
  std::vector<uint8_t> b;
  void u8(uint8_t v) { b.push_back(v); }
  void u16(uint16_t v) { u8(v & 0xFF); u8(v >> 8); }
  void u32(uint32_t v) { u16(v & 0xFFFF); u16(v >> 16); }
  void name(const char* s) { u8(static_cast<uint8_t>(std::strlen(s))); for (; *s; ++s) u8(static_cast<uint8_t>(*s)); }
};
struct N { uint32_t id; uint8_t pitch, velocity; uint32_t start, end; };
struct E { uint32_t tick; uint8_t type, channel; uint8_t d0, d1; int16_t bend; };
static std::vector<uint8_t> lsv1(uint32_t loop, uint32_t loopStart, const std::vector<N>& notes, const std::vector<E>& events,
                                 bool epoch = true) {
  Bytes f;
  if (epoch) {  // the 10-byte epoch header as the firmware writes it (not parsed): epoch, schema, CRC
    f.u32(0x1142);
    f.u16(1);
    f.u32(0x83BDE906);
  }
  f.u8('L'); f.u8('S'); f.u8('V'); f.u8('1'); f.u16(1); f.u16(1);
  f.name("LoopMetadata"); f.u32(32); for (int i = 0; i < 8; ++i) f.u32(i == 2 ? loop : 0);
  f.name("PersistedLoopSnapshot"); f.u32(40); for (int i = 0; i < 40; ++i) f.u8(0x33);  // the pass oracle: skipped
  Bytes s;
  s.u32(1); s.u32(loop); s.u32(loopStart); s.u32(1000); s.u32(1000);
  s.u32(static_cast<uint32_t>(notes.size()));
  for (const N& n : notes) { s.u32(n.id); s.u8(n.pitch); s.u8(n.velocity); s.u16(0); s.u32(n.start); s.u32(n.end); }
  s.u32(static_cast<uint32_t>(events.size()));
  for (const E& e : events) {
    s.u32(500); s.u32(0);                       // eventId, canonical order
    s.u32(e.tick); s.u8(e.type); s.u8(e.channel); s.u16(0); s.u32(0);  // tick, type, channel, pad, noteId
    if (e.type == 0xE0) s.u16(static_cast<uint16_t>(e.bend)); else { s.u8(e.d0); s.u8(e.d1); }
    s.u16(0); s.u32(0);  // the rest of the 8-byte union
  }
  for (int i = 0; i < 8; ++i) s.u8(0);  // membership tables after the events: not read
  f.name("LoopSnapshot"); f.u32(static_cast<uint32_t>(s.b.size())); f.b.insert(f.b.end(), s.b.begin(), s.b.end());
  f.u8('S'); f.u8('A'); f.u8('V'); f.u8('E');
  return f.b;
}
static void put(const char* path, const std::vector<uint8_t>& bytes) {
  gCard.openWrite(path);
  gCard.write(bytes.data(), static_cast<uint32_t>(bytes.size()));
  gCard.closeWrite();
}
static std::vector<uint8_t> get(const char* path) {
  const int f = gCard.find(path);
  return std::vector<uint8_t>(gCard.files[f].data, gCard.files[f].data + gCard.files[f].size);
}

void setUp() {
  gCard.reset();
  TEST_ASSERT_TRUE(gE.init(gPsram, gRam2, 2000));
  gClock = 0;
}
void tearDown() {}

constexpr uint32_t kLoop = 3072;
static const std::vector<N> kNotes = {{1, 60, 100, 0, 300}, {2, 64, 90, 1000, 1500}, {3, 67, 80, 2900, 200},
                                      {4, 69, 70, 3000, 3072 + 100}};  // wrapped, and stored unwrapped
// The program change's second union byte is stale (the firmware's union is not cleared): it must not reach the row.
static const std::vector<E> kEvents = {{50, 0xB0, 1, 7, 100, 0}, {60, 0xC0, 1, 5, 99, 0}, {2000, 0xE0, 2, 0, 0, 2000}};

// ---- Tests -------------------------------------------------------------------------------------------------------------
void test_an_lsv1_slot_imports_its_current_content() {
  put("/MidiLooper/current/slots/loop_01_02.bin", lsv1(kLoop, 0, kNotes, kEvents));  // slot 10
  const std::vector<uint8_t> before = get("/MidiLooper/current/slots/loop_01_02.bin");
  gE.attachCard(gCard);
  const uint32_t writes = gCard.writes;
  gE.startBootLoad();
  run(30);
  TEST_ASSERT_EQUAL_UINT32(1, gE.persistence().stats().v1Imported);
  TEST_ASSERT_EQUAL_UINT32(writes, gCard.writes);  // read only
  // Its notes, with their stored geometry (gathered as NOTE_EDIT would).
  TEST_ASSERT_EQUAL(ProductionNoteEdit::Gather::Running, gE.noteEdit().request({10, 0, kLoop, 0, 127}, 0));
  run(gClock + 30);
  TEST_ASSERT_EQUAL_UINT16(4, gE.noteEdit().count());
  for (uint16_t i = 0; i < 4; ++i) {
    const EditNote& n = gE.noteEdit().note(i);
    for (const N& w : kNotes)
      if (w.pitch == n.pitch) {
        TEST_ASSERT_EQUAL_UINT32(w.start, n.tick);
        const uint32_t end = w.end >= kLoop ? w.end - kLoop : w.end;
        TEST_ASSERT_EQUAL_UINT32(end >= w.start ? end - w.start : end + kLoop - w.start, n.length);
        TEST_ASSERT_EQUAL_UINT8(w.velocity, n.velocity);
      }
  }
  // Its events, in their own run, first.
  const auto& m = gE.manifests().manifest(gE.history().current(10));
  TEST_ASSERT_EQUAL_UINT8(2, m.runCount);
  const RunId ev = m.runs[0];
  TEST_ASSERT_EQUAL_UINT32(3, gE.runs().run(ev).rowCount);
  const PassEvent& cc = gE.runs().rowAt(ev, 0, gE.pool());
  TEST_ASSERT_EQUAL_HEX8(0xB0, cc.status);
  TEST_ASSERT_EQUAL_UINT8(7, cc.data1);
  TEST_ASSERT_EQUAL_UINT8(100, cc.data2);
  TEST_ASSERT_EQUAL_HEX8(0xC0, gE.runs().rowAt(ev, 1, gE.pool()).status);
  TEST_ASSERT_EQUAL_UINT8(5, gE.runs().rowAt(ev, 1, gE.pool()).data1);
  TEST_ASSERT_EQUAL_UINT8(0, gE.runs().rowAt(ev, 1, gE.pool()).data2);
  const PassEvent& pb = gE.runs().rowAt(ev, 2, gE.pool());
  TEST_ASSERT_EQUAL_HEX8(0xE1, pb.status);  // channel 2
  TEST_ASSERT_EQUAL_UINT32(2000 + 8192, pb.data1 | (pb.data2 << 7));
  // Approved: the slot is saved in the new format; the V1 file is untouched.
  TEST_ASSERT_TRUE(gE.approveSaving());
  run(gClock + ProductionPersistence::kSettleUs / kUsPerTick + 50);
  TEST_ASSERT_TRUE(gCard.exists("/MidiLooper/engine/slot_10.mle"));
  TEST_ASSERT_TRUE(before == get("/MidiLooper/current/slots/loop_01_02.bin"));
}

void test_notes_out_of_start_order_import_in_windows() {
  // The firmware sorts notes by id, not start: 2,000 notes stored in reverse start order — more than one collect window
  // holds — import in start order; an over-dense span is refused by name.
  std::vector<N> many;
  for (uint32_t k = 0; k < 2000; ++k) {
    const uint32_t start = (1999 - k) * 3;  // id rises, start falls
    many.push_back({k + 1, static_cast<uint8_t>(30 + k % 60), 90, start % kLoop, (start + 2) % kLoop});
  }
  put("/MidiLooper/current/slots/loop_00_04.bin", lsv1(6144, 0, many, kEvents));
  std::vector<N> dense;
  for (uint32_t k = 0; k < 900; ++k) dense.push_back({k + 1, static_cast<uint8_t>(30 + k % 60), 90, 10, 20});
  put("/MidiLooper/current/slots/loop_00_05.bin", lsv1(kLoop, 0, dense, {}));
  gE.attachCard(gCard);
  gE.startBootLoad();
  run(400);
  TEST_ASSERT_EQUAL_UINT32(1, gE.persistence().stats().v1Imported);
  TEST_ASSERT_EQUAL_UINT64(1ull << 5, gE.persistence().stats().v1FailedMask);  // 900 notes in one span: Dense
  const auto& m = gE.manifests().manifest(gE.history().current(4));
  const RunId r = m.runs[m.runCount - 1];
  TEST_ASSERT_EQUAL_UINT32(4000, gE.runs().run(r).rowCount);
  for (uint32_t i = 1; i < gE.runs().run(r).rowCount; ++i)
    TEST_ASSERT_TRUE(gE.runs().rowAt(r, i - 1, gE.pool()).tick <= gE.runs().rowAt(r, i, gE.pool()).tick);
  TEST_ASSERT_TRUE(gE.reservations().check());
}

void test_skipped_and_failed_v1_slots_are_named() {
  put("/MidiLooper/current/slots/loop_00_00.bin", {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20});
  put("/MidiLooper/current/slots/loop_00_01.bin", lsv1(kLoop, 768, kNotes, kEvents));  // loop start edited
  auto truncated = lsv1(kLoop, 0, kNotes, kEvents);
  truncated.resize(truncated.size() - 40);
  put("/MidiLooper/current/slots/loop_00_02.bin", truncated);
  put("/MidiLooper/current/slots/loop_00_03.bin", lsv1(kLoop, 0, kNotes, kEvents, /*epoch*/ false));
  // An LSV1 snapshot whose events are only note events and whose notes list is empty (found on the owner's card):
  // skipped as empty, not imported as an empty state.
  put("/MidiLooper/current/slots/loop_00_06.bin", lsv1(kLoop, 0, {}, {{100, 0x90, 1, 60, 100, 0}, {200, 0x80, 1, 60, 0, 0}}));
  gE.attachCard(gCard);
  gE.startBootLoad();
  run(40);
  TEST_ASSERT_EQUAL_UINT64((1ull << 0) | (1ull << 1) | (1ull << 6), gE.persistence().stats().v1SkippedMask);
  TEST_ASSERT_EQUAL_UINT64(1ull << 1, gE.persistence().stats().v1OffsetMask);
  TEST_ASSERT_EQUAL_UINT64(1ull << 6, gE.persistence().stats().v1EmptyMask);
  TEST_ASSERT_EQUAL(kNoManifest, gE.history().current(6));
  TEST_ASSERT_EQUAL_UINT64(1ull << 2, gE.persistence().stats().v1FailedMask);
  TEST_ASSERT_EQUAL_UINT32(1, gE.persistence().stats().v1Imported);  // slot 3, no epoch header
  for (uint8_t s : {0, 1, 2}) TEST_ASSERT_EQUAL(kNoManifest, gE.history().current(s));
  TEST_ASSERT_NOT_EQUAL(kNoManifest, gE.history().current(3));
  TEST_ASSERT_TRUE(gE.reservations().check());
}

void test_an_engine_file_takes_precedence() {
  // Slot 3 gets an engine file from a first engine; its V1 file differs.
  put("/MidiLooper/current/slots/loop_00_03.bin", lsv1(kLoop, 0, kNotes, kEvents));
  gE.attachCard(gCard);
  gE.startBootLoad();
  run(30);
  gE.approveSaving();
  run(gClock + ProductionPersistence::kSettleUs / kUsPerTick + 50);
  TEST_ASSERT_TRUE(gCard.exists("/MidiLooper/engine/slot_03.mle"));
  put("/MidiLooper/current/slots/loop_00_03.bin", lsv1(kLoop, 0, {{9, 40, 50, 10, 20}}, {}));
  TEST_ASSERT_TRUE(gE.init(gPsram, gRam2, 2000));
  gE.attachCard(gCard);
  gE.startBootLoad();
  run(gClock + 30);
  TEST_ASSERT_EQUAL_UINT32(1, gE.persistence().stats().loads);
  TEST_ASSERT_EQUAL_UINT32(0, gE.persistence().stats().v1Imported);
}

void test_no_heap() {
  put("/MidiLooper/current/slots/loop_00_05.bin", lsv1(kLoop, 0, kNotes, kEvents));
  gE.attachCard(gCard);
  gTrap = true;
  const unsigned long before = gHeapAllocations;
  gE.startBootLoad();
  run(30);
  gTrap = false;
  TEST_ASSERT_EQUAL_UINT32(0, gHeapAllocations - before);
  TEST_ASSERT_EQUAL_UINT32(1, gE.persistence().stats().v1Imported);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_an_lsv1_slot_imports_its_current_content);
  RUN_TEST(test_notes_out_of_start_order_import_in_windows);
  RUN_TEST(test_skipped_and_failed_v1_slots_are_named);
  RUN_TEST(test_an_engine_file_takes_precedence);
  RUN_TEST(test_no_heap);
  return UNITY_END();
}
