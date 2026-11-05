//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// fixed-memory-engine E9.7b, on the Teensy only: the card's V1 slot files read into the engine at boot — READ ONLY.
// The card adapter refuses every write (makeDir, openWrite, write, rename return false) and saving is never approved,
// so nothing on the card can change. Reports per slot: imported (with its notes and events), skipped (pass stream or
// a shifted loop start) or failed, and the time the whole boot import took. This confirms the Teensy `MidiEvent`
// layout against the owner's real files.

#include <unity.h>

#ifdef ARDUINO
#include <Arduino.h>
#include <SD.h>

#include <cstdio>

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
  bool openWrite(const char*) override { return false; }
  bool write(const uint8_t*, uint32_t) override { return false; }
  bool closeWrite() override { return false; }
  bool rename(const char*, const char*) override { return false; }
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
  char line[200];
  for (uint8_t s = 0; s < Config::kSlotCount; ++s) {
    const ManifestId m = gE.history().current(s);
    const bool skipped = (st.v1SkippedMask >> s) & 1, failed = (st.v1FailedMask >> s) & 1;
    if (m == kNoManifest && !skipped && !failed) continue;
    uint32_t noteRows = 0, eventRows = 0, loop = 0;
    if (m != kNoManifest) {
      const auto& x = gE.manifests().manifest(m);
      loop = x.loopLengthTicks;
      noteRows = gE.runs().run(x.runs[x.runCount - 1]).rowCount;
      eventRows = x.runCount > 1 ? gE.runs().run(x.runs[0]).rowCount : 0;
    }
    snprintf(line, sizeof line, "#CAP,V1_SLOT,track=%u,slot=%u,%s,outcome=%02x,loop=%u,notes=%u,events=%u", s / 8, s % 8,
             m != kNoManifest ? "imported" : skipped ? "skipped" : "failed", st.v1Outcome[s], static_cast<unsigned>(loop),
             static_cast<unsigned>(noteRows / 2), static_cast<unsigned>(eventRows));
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
  snprintf(line, sizeof line, "#CAP,V1_IMPORT,imported=%u,skipped_mask=%llx,failed_mask=%llx,us=%u",
           static_cast<unsigned>(st.v1Imported), static_cast<unsigned long long>(st.v1SkippedMask),
           static_cast<unsigned long long>(st.v1FailedMask), static_cast<unsigned>(us));
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
