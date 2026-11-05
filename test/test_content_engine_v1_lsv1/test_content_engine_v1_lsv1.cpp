//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// fixed-memory-engine E9.7b / E9.7c: V1 slot files imported at boot (owner 2026-11-05: exact current content;
// pass-stream files imported, their legacy note edits discarded and reported), from LSV1 and pass-stream
// bytes built here exactly as the old firmware writes them on the Teensy (the 10-byte epoch header; LSV1: LoopMetadata,
// the PersistedLoopSnapshot oracle, LoopSnapshot; a pass stream: the header, the passes' raw events in capture order,
// the EPT3 tail; then SAVE; `MidiEvent` 20 bytes, payload at 12).
// Checked: notes with their stored geometry (one held across the loop end stored wrapped, one stored unwrapped), CC,
// program change and pitch bend; a pass stream's Active passes as runs in materialisation order (record, then
// overdubs by mergeSequence; an undone pass left out), its notes paired last-open-first and cyclically (a pass stored
// in tick order: a held note's NoteOff before its NoteOn), a NoteOff sealed past the loop end wrapped, a NoteOn with no
// NoteOff held for the whole loop and counted, a long pass imported in windows; a shifted loop start, an empty file, a
// loop-0 placeholder skipped and named; a slot with edit passes imported as recorded, edits discarded and named; junk, a truncated file, an over-dense pass and too many passes
// failing by name (every run released) while the other slots import; V1 files never written, the imported slot saved in the new format once approved; an engine file
// taking precedence over the slot's V1 file; no allocation.

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
// ---- Pass-stream bytes, as the old firmware writes them (`writeLegacyLoopPersisted`) -------------------------------------
struct Ev { uint32_t tick; uint8_t type, channel, d0, d1; };
struct Pass { uint32_t id, merge; uint8_t state, type; std::vector<Ev> ev; };
static Ev on(uint32_t t, uint8_t p, uint8_t v = 100) { return {t, 0x90, 1, p, v}; }
static Ev off(uint32_t t, uint8_t p) { return {t, 0x80, 1, p, 0}; }
static std::vector<uint8_t> passStream(uint32_t loop, uint32_t loopStart, const std::vector<Pass>& passes,
                                       uint32_t edits = 0) {
  Bytes f;
  f.u32(0x1142); f.u16(1); f.u32(0x83BDE906);  // the epoch header
  for (uint32_t w : {5u, 0u, loop, loopStart, 9u, 100u, 7u, 4u}) f.u32(w);
  f.u32(static_cast<uint32_t>(passes.size()));
  for (const Pass& p : passes) {
    f.u32(p.id); f.u32(p.merge); f.u8(p.state); f.u8(p.type); f.u32(loop); f.u32(static_cast<uint32_t>(p.ev.size()));
    for (const Ev& e : p.ev) {
      f.u32(e.tick); f.u8(e.type); f.u8(e.channel); f.u16(0); f.u32(e.type == 0x90 ? 77 : 0);
      if (e.type == 0xE0) f.u16(static_cast<uint16_t>(e.d0 | (e.d1 << 8))); else { f.u8(e.d0); f.u8(e.d1); }
      f.u16(0); f.u32(0);
    }
  }
  f.u32(9); f.u32(0x45505433u); f.u32(edits);
  for (uint32_t k = 0; k < edits * 40; ++k) f.u8(0x5A);  // edit passes: not read
  f.u32(0x314F4547u); f.u32(0); f.u32(0x3149534Fu); f.u32(0);  // GEO1, OSI1
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
  TEST_ASSERT_EQUAL_UINT64((1ull << 1) | (1ull << 6), gE.persistence().stats().v1SkippedMask);
  TEST_ASSERT_EQUAL_UINT64(1ull << 1, gE.persistence().stats().v1OffsetMask);
  TEST_ASSERT_EQUAL_UINT64(1ull << 6, gE.persistence().stats().v1EmptyMask);
  TEST_ASSERT_EQUAL(kNoManifest, gE.history().current(6));
  TEST_ASSERT_EQUAL_UINT64((1ull << 0) | (1ull << 2), gE.persistence().stats().v1FailedMask);  // junk, truncated
  TEST_ASSERT_EQUAL_UINT32(1, gE.persistence().stats().v1Imported);  // slot 3, no epoch header
  for (uint8_t s : {0, 1, 2}) TEST_ASSERT_EQUAL(kNoManifest, gE.history().current(s));
  TEST_ASSERT_NOT_EQUAL(kNoManifest, gE.history().current(3));
  TEST_ASSERT_TRUE(gE.reservations().check());
}

// The NoteOn rows of run `r`: (tick, pitch) in row order.
static std::vector<std::pair<uint32_t, uint8_t>> onsOf(RunId r) {
  std::vector<std::pair<uint32_t, uint8_t>> v;
  for (uint32_t i = 0; i < gE.runs().run(r).rowCount; ++i) {
    const PassEvent& e = gE.runs().rowAt(r, i, gE.pool());
    if ((e.status & 0xF0) == 0x90) v.push_back({e.tick, e.data1});
  }
  return v;
}

void test_a_pass_stream_slot_imports_its_active_passes_as_runs() {
  // The record pass in capture order: two overlapping 48s (last open first), a NoteOn of velocity 0 closing 50, a
  // stray NoteOff, a 67 held across the loop end whose NoteOff was sealed past it (3272 = 200 wrapped), CC and bend.
  const Pass record{1, 0, 0, 0, {on(0, 60), {50, 0xB0, 1, 7, 100}, on(100, 48), on(150, 48), off(200, 48), off(300, 60),
                                 off(400, 48), on(600, 50), {700, 0x90, 1, 50, 0}, off(800, 51), on(1000, 64),
                                 off(1500, 64), {2000, 0xE0, 2, 0xD0, 0x07}, on(2900, 67), off(3272, 67)}};
  // Stored in tick order, as on the card: 75 held across the loop end, its NoteOff (50) before its NoteOn (3000).
  const Pass overdubA{3, 2, 0, 1, {off(50, 75), on(500, 72), off(700, 72), on(3000, 75)}};
  const Pass undone{4, 3, 1, 1, {on(0, 80), off(100, 80), {10, 0xB0, 1, 1, 1}}};  // state 1: not Active
  const Pass overdubC{2, 1, 0, 1, {on(10, 74), off(20, 74)}};                     // merged before A
  put("/MidiLooper/current/slots/loop_01_04.bin", passStream(kLoop, 0, {record, overdubA, undone, overdubC}));
  const std::vector<uint8_t> before = get("/MidiLooper/current/slots/loop_01_04.bin");
  gE.attachCard(gCard);
  const uint32_t writes = gCard.writes;
  gE.startBootLoad();
  run(40);
  TEST_ASSERT_EQUAL_UINT32(1, gE.persistence().stats().v1Imported);
  TEST_ASSERT_EQUAL_UINT32(writes, gCard.writes);
  // The event run first, then the record pass, overdub C, overdub A.
  const auto& m = gE.manifests().manifest(gE.history().current(12));
  TEST_ASSERT_EQUAL_UINT8(4, m.runCount);
  TEST_ASSERT_EQUAL_UINT32(2, gE.runs().run(m.runs[0]).rowCount);
  TEST_ASSERT_EQUAL_HEX8(0xB0, gE.runs().rowAt(m.runs[0], 0, gE.pool()).status);
  const PassEvent& pb = gE.runs().rowAt(m.runs[0], 1, gE.pool());
  TEST_ASSERT_EQUAL_HEX8(0xE1, pb.status);
  TEST_ASSERT_EQUAL_UINT32(2000 + 8192, pb.data1 | (pb.data2 << 7));
  const std::vector<std::pair<uint32_t, uint8_t>> rec = {{0, 60}, {100, 48}, {150, 48}, {600, 50}, {1000, 64}, {2900, 67}};
  TEST_ASSERT_TRUE(rec == onsOf(m.runs[1]));
  TEST_ASSERT_TRUE((std::vector<std::pair<uint32_t, uint8_t>>{{10, 74}}) == onsOf(m.runs[2]));
  TEST_ASSERT_TRUE((std::vector<std::pair<uint32_t, uint8_t>>{{500, 72}, {3000, 75}}) == onsOf(m.runs[3]));
  // The geometry, as NOTE_EDIT gathers it: 48@100 closed by the later NoteOff (last open first), 67 wrapped.
  TEST_ASSERT_EQUAL(ProductionNoteEdit::Gather::Running, gE.noteEdit().request({12, 0, kLoop, 0, 127}, 0));
  run(gClock + 30);
  struct G { uint32_t tick; uint8_t pitch; uint32_t length; };
  const G want[] = {{0, 60, 300}, {100, 48, 300}, {150, 48, 50}, {600, 50, 100}, {1000, 64, 500},
                    {2900, 67, 372}, {10, 74, 10}, {500, 72, 200}, {3000, 75, 122}};
  TEST_ASSERT_EQUAL_UINT16(9, gE.noteEdit().count());
  TEST_ASSERT_EQUAL_UINT32(0, gE.persistence().stats().v1HeldWhole);
  for (const G& w : want) {
    bool found = false;
    for (uint16_t i = 0; i < gE.noteEdit().count(); ++i) {
      const EditNote& n = gE.noteEdit().note(i);
      found |= n.tick == w.tick && n.pitch == w.pitch && n.length == w.length;
    }
    TEST_ASSERT_TRUE_MESSAGE(found, "a pass-stream note with its paired geometry");
  }
  TEST_ASSERT_TRUE(gE.approveSaving());
  run(gClock + ProductionPersistence::kSettleUs / kUsPerTick + 50);
  TEST_ASSERT_TRUE(gCard.exists("/MidiLooper/engine/slot_12.mle"));
  TEST_ASSERT_TRUE(before == get("/MidiLooper/current/slots/loop_01_04.bin"));
  TEST_ASSERT_TRUE(gE.reservations().check());
}

void test_a_long_pass_imports_in_windows() {
  // 1,200 notes out of start order (several cycles of an overdub) in one pass, 700 in the next: more than one collect
  // window each; each run in start order.
  Pass a{1, 0, 0, 0, {}}, b{2, 1, 0, 1, {}};
  for (uint32_t k = 0; k < 1200; ++k) {
    const uint32_t t = ((k * 7919u) % 1200u) * 5u;
    a.ev.push_back(on(t, static_cast<uint8_t>(30 + k % 60)));
    a.ev.push_back(off(t + 2, static_cast<uint8_t>(30 + k % 60)));
  }
  for (uint32_t k = 0; k < 700; ++k) {
    const uint32_t t = ((k * 31u) % 700u) * 8u + 1u;
    b.ev.push_back(on(t, 90));
    b.ev.push_back(off(t + 3, 90));
  }
  put("/MidiLooper/current/slots/loop_00_07.bin", passStream(6144, 0, {a, b}));
  gE.attachCard(gCard);
  gE.startBootLoad();
  run(400);
  TEST_ASSERT_EQUAL_UINT32(1, gE.persistence().stats().v1Imported);
  const auto& m = gE.manifests().manifest(gE.history().current(7));
  TEST_ASSERT_EQUAL_UINT8(2, m.runCount);  // no kept events: no event run
  const uint32_t rows[] = {2400, 1400};
  for (uint8_t k = 0; k < 2; ++k) {
    const RunId r = m.runs[k];
    TEST_ASSERT_EQUAL_UINT32(rows[k], gE.runs().run(r).rowCount);
    for (uint32_t i = 1; i < gE.runs().run(r).rowCount; ++i)
      TEST_ASSERT_TRUE(gE.runs().rowAt(r, i - 1, gE.pool()).tick <= gE.runs().rowAt(r, i, gE.pool()).tick);
  }
  TEST_ASSERT_TRUE(gE.reservations().check());
}

void test_pass_stream_skips_and_failures_are_named() {
  const Pass record{1, 0, 0, 0, {on(0, 60), off(300, 60)}};
  put("/MidiLooper/current/slots/loop_00_00.bin", passStream(kLoop, 0, {record}, /*edits*/ 2));
  put("/MidiLooper/current/slots/loop_00_01.bin", passStream(kLoop, 768, {record}));
  put("/MidiLooper/current/slots/loop_00_02.bin", passStream(kLoop, 0, {{1, 0, 0, 0, {on(0, 60), on(10, 61), off(20, 60)}}}));
  put("/MidiLooper/current/slots/loop_00_03.bin", passStream(kLoop, 0, {}));  // no passes
  put("/MidiLooper/current/slots/loop_00_07.bin", passStream(0, 0, {}));      // the card's placeholder: loop 0
  std::vector<Pass> many;
  for (uint32_t k = 0; k < 17; ++k)  // 17 Active passes, two of them CC only: 15 with notes, still too many
    many.push_back({k + 1, k, 0, static_cast<uint8_t>(k == 0 ? 0 : 1),
                    k == 3 || k == 4 ? std::vector<Ev>{{k, 0xB0, 1, 7, 1}} : std::vector<Ev>{on(k, 60), off(k + 1, 60)}});
  put("/MidiLooper/current/slots/loop_00_05.bin", passStream(kLoop, 0, many));
  put("/MidiLooper/current/slots/loop_00_06.bin", passStream(kLoop, 0, {record}));
  // Refused after the first pass's run was taken (the second pass has 900 notes in one span: Dense): every run of
  // the failed import is released.
  Pass dense{2, 1, 0, 1, {}};
  for (uint32_t k = 0; k < 900; ++k) {
    dense.ev.push_back(on(10, static_cast<uint8_t>(30 + k % 60)));
    dense.ev.push_back(off(20, static_cast<uint8_t>(30 + k % 60)));
  }
  put("/MidiLooper/current/slots/loop_00_04.bin", passStream(kLoop, 0, {record, dense}));
  const uint32_t runsFree = gE.runs().freeCount() + gE.runs().reclaimPendingRuns();
  gE.attachCard(gCard);
  gE.startBootLoad();
  run(60);
  TEST_ASSERT_EQUAL_UINT32(runsFree - 3, gE.runs().freeCount() + gE.runs().reclaimPendingRuns());  // slots 0, 2, 6
  const auto& st = gE.persistence().stats();
  TEST_ASSERT_EQUAL_UINT64((1ull << 1) | (1ull << 3) | (1ull << 7), st.v1SkippedMask);
  TEST_ASSERT_EQUAL_UINT64(1ull << 0, st.v1EditedMask);  // imported as recorded, its two edit passes discarded
  TEST_ASSERT_EQUAL_UINT64(1ull << 1, st.v1OffsetMask);
  TEST_ASSERT_EQUAL_UINT64((1ull << 3) | (1ull << 7), st.v1EmptyMask);
  TEST_ASSERT_EQUAL_UINT64((1ull << 4) | (1ull << 5), st.v1FailedMask);  // Dense; 17 Active passes
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(ProductionPersistence::Reader::Status::Dense), st.v1Outcome[4]);
  using R = ProductionPersistence::Reader::Status;
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(R::ManyPasses), st.v1Outcome[5]);
  TEST_ASSERT_EQUAL_UINT32(3, st.v1Imported);  // slots 0, 2 and 6
  for (uint8_t s : {1, 3, 4, 5, 7}) TEST_ASSERT_EQUAL(kNoManifest, gE.history().current(s));
  {
    const auto& m0 = gE.manifests().manifest(gE.history().current(0));
    TEST_ASSERT_EQUAL_UINT8(1, m0.runCount);
    TEST_ASSERT_TRUE((std::vector<std::pair<uint32_t, uint8_t>>{{0, 60}}) == onsOf(m0.runs[0]));
  }
  TEST_ASSERT_NOT_EQUAL(kNoManifest, gE.history().current(6));
  // Slot 2: 61 has no NoteOff in its pass — held for the whole loop (its NoteOff one tick before its start), counted.
  TEST_ASSERT_EQUAL_UINT32(1, st.v1HeldWhole);
  TEST_ASSERT_EQUAL(ProductionNoteEdit::Gather::Running, gE.noteEdit().request({2, 0, kLoop, 0, 127}, 0));
  run(gClock + 30);
  TEST_ASSERT_EQUAL_UINT16(2, gE.noteEdit().count());
  bool whole = false;
  for (uint16_t i = 0; i < 2; ++i)
    whole |= gE.noteEdit().note(i).pitch == 61 && gE.noteEdit().note(i).tick == 10 &&
             gE.noteEdit().note(i).length == kLoop - 1;
  TEST_ASSERT_TRUE(whole);
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
  put("/MidiLooper/current/slots/loop_00_06.bin", passStream(kLoop, 0, {{1, 0, 0, 0, {on(0, 60), off(300, 60)}},
                                                                       {2, 1, 0, 1, {on(10, 61), {20, 0xB0, 1, 7, 1}, off(40, 61)}}}));
  gE.attachCard(gCard);
  gTrap = true;
  const unsigned long before = gHeapAllocations;
  gE.startBootLoad();
  run(30);
  gTrap = false;
  TEST_ASSERT_EQUAL_UINT32(0, gHeapAllocations - before);
  TEST_ASSERT_EQUAL_UINT32(2, gE.persistence().stats().v1Imported);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_an_lsv1_slot_imports_its_current_content);
  RUN_TEST(test_notes_out_of_start_order_import_in_windows);
  RUN_TEST(test_skipped_and_failed_v1_slots_are_named);
  RUN_TEST(test_a_pass_stream_slot_imports_its_active_passes_as_runs);
  RUN_TEST(test_a_long_pass_imports_in_windows);
  RUN_TEST(test_pass_stream_skips_and_failures_are_named);
  RUN_TEST(test_an_engine_file_takes_precedence);
  RUN_TEST(test_no_heap);
  return UNITY_END();
}
