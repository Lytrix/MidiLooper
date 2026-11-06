//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// fixed-memory-engine E9.7b / E9.7c, on the Teensy only: the card's V1 slot files read into the engine at boot — READ
// ONLY. The card adapter refuses every write (makeDir, openWrite, write, rename return false) and saving is never
// approved, so nothing on the card can change. Reports per slot: imported (with its runs, notes and kept events),
// skipped (a shifted loop start, or empty) or failed; the slots whose legacy note edits were discarded; and the time
// the whole boot import took. This confirms the Teensy `MidiEvent` layout against the owner's real files.

#include <unity.h>

#ifdef ARDUINO
#include <Arduino.h>
#include <SD.h>

#include <cstdio>
#include <cstring>

#include "ContentEngine/EngineRoot.h"

using namespace ContentEngine;

struct ReadOnlySdCard : CardIo {
  File f;
  bool exists(const char* p) override { return SD.exists(p); }
  bool makeDir(const char*) override { return false; }
  bool openRead(const char* p) override {
    f = SD.open(p, FILE_READ);
    return static_cast<bool>(f);
  }
  bool read(uint8_t* d, uint32_t n) override { return f && f.read(d, n) == static_cast<int>(n); }
  void closeRead() override {
    if (f) f.close();
  }
  bool openStore(const char*, uint32_t, bool, bool&) override { return false; }  // read only
  bool readSector(uint32_t, uint8_t*) override { return false; }
  bool writeSector(uint32_t, const uint8_t*) override { return false; }
};

EXTMEM static EnginePsram gPsram;
DMAMEM static EngineRam2 gRam2;
static EngineRoot gE;
static ReadOnlySdCard gCard;

void setUp() {}
void tearDown() {}

void test_the_cards_v1_slots_import_read_only() {
  TEST_ASSERT_TRUE(SD.begin(BUILTIN_SDCARD));
  TEST_ASSERT_TRUE(gE.init(gPsram, gRam2, 2000));
  gE.attachCard(gCard);
  TEST_ASSERT_FALSE(gE.persistence().approved());  // no engine folder: the engine never writes
  gE.startBootLoad();
  const uint32_t t0 = micros();
  uint32_t clock = 0;
  while (gE.persistence().pending() && micros() - t0 < 60000000u) {
    gE.submitJobs(clock, micros(), 1040);
    while (gE.runGrain(micros())) {
    }
    ++clock;
  }
  const uint32_t us = micros() - t0;
  const auto& st = gE.persistence().stats();
  char line[320];
  for (uint8_t s = 0; s < Config::kSlotCount; ++s) {
    const ManifestId m = gE.history().current(s);
    const bool skipped = (st.v1SkippedMask >> s) & 1, failed = (st.v1FailedMask >> s) & 1;
    if (m == kNoManifest && !skipped && !failed) continue;
    uint32_t notes = 0, events = 0, loop = 0, runs = 0;
    if (m != kNoManifest) {
      const auto& x = gE.manifests().manifest(m);
      loop = x.loopLengthTicks;
      runs = x.runCount;
      for (uint8_t k = 0; k < x.runCount; ++k)
        for (uint32_t i = 0; i < gE.runs().run(x.runs[k]).rowCount; ++i) {
          const uint8_t type = gE.runs().rowAt(x.runs[k], i, gE.pool()).status & 0xF0;
          notes += type == 0x90;
          events += type != 0x90 && type != 0x80;
        }
    }
    snprintf(line, sizeof line, "#CAP,V1_SLOT,track=%u,slot=%u,%s,outcome=%02x,loop=%u,runs=%u,notes=%u,events=%u", s / 8,
             s % 8, m != kNoManifest ? "imported" : skipped ? "skipped" : "failed", st.v1Outcome[s],
             static_cast<unsigned>(loop), static_cast<unsigned>(runs), static_cast<unsigned>(notes),
             static_cast<unsigned>(events));
    TEST_MESSAGE(line);
  }
  // Diagnostics (read only): each slot file's size and first 24 bytes.
  for (uint8_t s = 0; s < Config::kSlotCount; ++s) {
    char path[48];
    snprintf(path, sizeof path, "/MidiLooper/current/slots/loop_%02u_%02u.bin", s / 8, s % 8);
    File f = SD.open(path, FILE_READ);
    if (!f) continue;
    uint8_t b[24] = {};
    const int n = f.read(b, sizeof b);
    char hex[60] = {};
    for (int i = 0; i < n && i < 24; ++i) snprintf(hex + 2 * i, 3, "%02x", b[i]);
    snprintf(line, sizeof line, "#CAP,V1_FILE,slot=%u,size=%u,offset_skip=%u,head=%s", s,
             static_cast<unsigned>(f.size()), static_cast<unsigned>((st.v1OffsetMask >> s) & 1), hex);
    TEST_MESSAGE(line);
    f.close();
  }
  // E9.7c survey (read only): every pass-stream slot's passes and edit passes.
  for (uint8_t s = 0; s < Config::kSlotCount; ++s) {
    char path[48];
    snprintf(path, sizeof path, "/MidiLooper/current/slots/loop_%02u_%02u.bin", s / 8, s % 8);
    File f = SD.open(path, FILE_READ);
    if (!f || f.size() <= 78) {
      if (f) f.close();
      continue;
    }
    uint8_t b[64];
    auto rd = [&](uint32_t n) { return f.read(b, n) == static_cast<int>(n); };
    auto u32 = [&](int o) { return static_cast<uint32_t>(b[o] | (b[o + 1] << 8) | (b[o + 2] << 16) | (static_cast<uint32_t>(b[o + 3]) << 24)); };
    rd(10);  // epoch header
    if (!rd(4) || (b[0] == 'L' && b[1] == 'S')) {
      f.close();
      continue;
    }
    rd(28);
    const uint32_t loop = u32(4), loopStart = u32(8);
    rd(4);
    const uint32_t passes = u32(0);
    char desc[120] = {};
    uint32_t total = 0;
    for (uint32_t k = 0; k < passes && k < 16; ++k) {
      if (!rd(18)) break;
      const uint8_t state = b[8], type = b[9];
      const uint32_t n = u32(14);
      total += n;
      snprintf(desc + strlen(desc), sizeof desc - strlen(desc), "%c%c%u ", type ? 'O' : 'R', state ? 'x' : 'a',
               static_cast<unsigned>(n));
      f.seek(f.position() + n * 20);
    }
    rd(12);
    const bool ept3 = u32(4) == 0x45505433u;
    snprintf(line, sizeof line, "#CAP,V1_PASSES,slot=%u,loop=%u,start=%u,passes=%u,events=%u,edit_tail=%u,edits=%u,%s", s,
             static_cast<unsigned>(loop), static_cast<unsigned>(loopStart), static_cast<unsigned>(passes),
             static_cast<unsigned>(total), ept3, static_cast<unsigned>(ept3 ? u32(8) : 0), desc);
    TEST_MESSAGE(line);
    f.close();
  }
  // E9.7c diagnostics (read only): per failed pass-stream slot, its events' shape.
  for (uint8_t s = 0; s < Config::kSlotCount; ++s) {
    if (!((st.v1FailedMask >> s) & 1)) continue;
    char path[48];
    snprintf(path, sizeof path, "/MidiLooper/current/slots/loop_%02u_%02u.bin", s / 8, s % 8);
    File f = SD.open(path, FILE_READ);
    if (!f) continue;
    uint8_t b[64];
    auto rd = [&](uint32_t n) { return f.read(b, n) == static_cast<int>(n); };
    auto u32 = [&](int o) { return static_cast<uint32_t>(b[o] | (b[o + 1] << 8) | (b[o + 2] << 16) | (static_cast<uint32_t>(b[o + 3]) << 24)); };
    rd(10);
    rd(36);
    const uint32_t loop = u32(8), passes = u32(32);
    uint32_t maxTick = 0, ons = 0, offs = 0, vel0 = 0, other = 0, unpaired = 0, stray = 0, types = 0;
    char first[64] = {};
    for (uint32_t k = 0; k < passes && k < 32; ++k) {
      if (!rd(18)) break;
      const uint32_t n = u32(14);
      const bool active = b[8] == 0;
      uint8_t open[128][2];
      uint32_t opens = 0;
      for (uint32_t i = 0; i < n; ++i) {
        if (!rd(20)) break;
        if (!active) continue;
        const uint32_t t = u32(0);
        if (t > maxTick) maxTick = t;
        const uint8_t type = b[4], ch = b[5], p = b[12], v = b[13];
        if (k == 0 && i < 3) snprintf(first + strlen(first), sizeof first - strlen(first), "%u:%02x/%u/%u/%u ", static_cast<unsigned>(t), type, ch, p, v);
        types |= 1u << (type >> 4);
        if (type == 0x90 && v != 0) {
          ++ons;
          if (opens < 128) { open[opens][0] = ch; open[opens][1] = p; ++opens; }
        } else if (type == 0x80 || type == 0x90) {
          (type == 0x80 ? offs : vel0)++;
          bool hit = false;
          for (uint32_t j = opens; j-- > 0;)
            if (open[j][0] == ch && open[j][1] == p) {
              for (uint32_t m = j + 1; m < opens; ++m) { open[m - 1][0] = open[m][0]; open[m - 1][1] = open[m][1]; }
              --opens;
              hit = true;
              break;
            }
          stray += !hit;
        } else {
          ++other;
        }
      }
      unpaired += opens;
    }
    rd(12);
    snprintf(line, sizeof line, "#CAP,V1_DIAG,slot=%u,loop=%u,passes=%u,max_tick=%u,ons=%u,offs=%u,vel0=%u,other=%u,unpaired=%u,stray=%u,types=%04x,tail=%08x/%u,first=%s",
             s, static_cast<unsigned>(loop), static_cast<unsigned>(passes), static_cast<unsigned>(maxTick), static_cast<unsigned>(ons),
             static_cast<unsigned>(offs), static_cast<unsigned>(vel0), static_cast<unsigned>(other), static_cast<unsigned>(unpaired),
             static_cast<unsigned>(stray), static_cast<unsigned>(types), static_cast<unsigned>(u32(4)), static_cast<unsigned>(u32(8)), first);
    TEST_MESSAGE(line);
    f.close();
  }
  snprintf(line, sizeof line, "#CAP,V1_IMPORT,imported=%u,held_whole=%u,skipped_mask=%llx,edited_mask=%llx,failed_mask=%llx,us=%u",
           static_cast<unsigned>(st.v1Imported), static_cast<unsigned>(st.v1HeldWhole), static_cast<unsigned long long>(st.v1SkippedMask),
           static_cast<unsigned long long>(st.v1EditedMask), static_cast<unsigned long long>(st.v1FailedMask),
           static_cast<unsigned>(us));
  TEST_MESSAGE(line);
  TEST_ASSERT_FALSE(gE.persistence().pending());
  TEST_ASSERT_TRUE(gE.reservations().check());
}

void setup() {
  delay(2000);
  UNITY_BEGIN();
  RUN_TEST(test_the_cards_v1_slots_import_read_only);
  UNITY_END();
}
void loop() {}
#else
int main(int, char**) {
  UNITY_BEGIN();
  return UNITY_END();  // the card is on the Teensy
}
#endif
