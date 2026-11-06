//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// fixed-memory-engine E9.8k: the rules behind the screen's status band — the save dots (legacy timing), positions and
// the launch countdown, the tempo in tenths, port labels and note names. The overdub / NOTE_EDIT session's slot for the
// undo label.

#include <unity.h>

#include <cstring>

#include "ContentEngine/EngineClock.h"
#include "ContentEngine/EngineRoot.h"
#include "ContentEngine/StatusView.h"

using namespace ContentEngine;
using namespace ContentEngine::status;

void setUp() {}
void tearDown() {}

void test_the_save_dots_follow_the_legacy_rules() {
  SaveIndicator d;
  d.start(3, 1);  // what happened before the screen looked does not flash
  TEST_ASSERT_EQUAL(SaveDots::Off, d.update(0, false, false, 3, 1));
  TEST_ASSERT_EQUAL(SaveDots::Pending, d.update(10, false, true, 3, 1));
  TEST_ASSERT_EQUAL(SaveDots::Busy, d.update(20, true, true, 3, 1));  // busy wins over pending
  TEST_ASSERT_EQUAL(SaveDots::Done, d.update(1000, false, false, 4, 1));
  TEST_ASSERT_EQUAL(SaveDots::Done, d.update(1000 + SaveIndicator::kFlashMs - 1, false, false, 4, 1));
  TEST_ASSERT_EQUAL(SaveDots::Off, d.update(1000 + SaveIndicator::kFlashMs, false, false, 4, 1));
  TEST_ASSERT_EQUAL(SaveDots::Failed, d.update(3000, false, false, 4, 2));
  TEST_ASSERT_EQUAL(SaveDots::Pending, d.update(3001, false, true, 4, 2));  // a pending change hides the flash
  TEST_ASSERT_EQUAL(SaveDots::Off, d.update(3000 + SaveIndicator::kFlashMs, false, false, 4, 2));
  TEST_ASSERT_EQUAL_UINT8(0, SaveIndicator::step(199));
  TEST_ASSERT_EQUAL_UINT8(1, SaveIndicator::step(200));
  TEST_ASSERT_EQUAL_UINT8(0, SaveIndicator::step(800));
}

void test_positions_and_the_countdown() {
  const uint32_t bar = kTicksPerBar, q = EngineClock::kTicksPerQuarter;
  Position p = position(0, bar, q);
  TEST_ASSERT_EQUAL_UINT32(1, p.bar);
  TEST_ASSERT_EQUAL_UINT8(1, p.beat);
  TEST_ASSERT_EQUAL_UINT8(1, p.sixteenth);
  TEST_ASSERT_EQUAL_UINT8(0, p.tick);
  p = position(117 * bar + 1 * q + 2 * (q / 4) + 12, bar, q);  // 118.2.3.12
  TEST_ASSERT_EQUAL_UINT32(118, p.bar);
  TEST_ASSERT_EQUAL_UINT8(2, p.beat);
  TEST_ASSERT_EQUAL_UINT8(3, p.sixteenth);
  TEST_ASSERT_EQUAL_UINT8(12, p.tick);
  p = countdown(bar + 2 * q + q / 4 + 24, bar, q);  // -1.2.1.24: every part 0-based
  TEST_ASSERT_EQUAL_UINT32(1, p.bar);
  TEST_ASSERT_EQUAL_UINT8(2, p.beat);
  TEST_ASSERT_EQUAL_UINT8(1, p.sixteenth);
  TEST_ASSERT_EQUAL_UINT8(24, p.tick);
  p = countdown(1, bar, q);
  TEST_ASSERT_EQUAL_UINT32(0, p.bar);
  TEST_ASSERT_EQUAL_UINT8(1, p.tick);
}

void test_the_tempo_port_and_note_labels() {
  const uint32_t q = EngineClock::kTicksPerQuarter;
  TEST_ASSERT_EQUAL_UINT16(1200, tempoTenths(60000000u / (120u * q), q));
  TEST_ASSERT_EQUAL_UINT16(1213, tempoTenths(2576, q));  // 121.3
  TEST_ASSERT_EQUAL_UINT16(1200, tempoTenths(2605, q));  // 119.96: rounded
  TEST_ASSERT_EQUAL_UINT16(0, tempoTenths(0, q));
  TEST_ASSERT_EQUAL_STRING("DIN1", portLabel(1));
  TEST_ASSERT_EQUAL_STRING("USB1", portLabel(2));
  TEST_ASSERT_EQUAL_STRING("HOST", portLabel(4));
  TEST_ASSERT_EQUAL_STRING("ALL", portLabel(7));
  TEST_ASSERT_EQUAL_STRING("MIX", portLabel(3));
  TEST_ASSERT_EQUAL_STRING("OFF", portLabel(0));
  char n[5];
  noteName(60, n);
  TEST_ASSERT_EQUAL_STRING("C3", n);
  noteName(61, n);
  TEST_ASSERT_EQUAL_STRING("C#3", n);
  noteName(55, n);
  TEST_ASSERT_EQUAL_STRING("G2", n);
  noteName(0, n);
  TEST_ASSERT_EQUAL_STRING("C-2", n);
  noteName(127, n);
  TEST_ASSERT_EQUAL_STRING("G8", n);
}

static EnginePsram gPsram;
static EngineRam2 gRam2;
static EngineRoot gE;

void test_the_undo_label_finds_the_open_sessions_slot() {
  TEST_ASSERT_TRUE(gE.init(gPsram, gRam2, 2000));
  auto& h = gE.history();
  TEST_ASSERT_EQUAL_UINT8(0xFF, h.sessionSlot(SessionKind::Overdub));
  TEST_ASSERT_TRUE(h.beginSession(SessionKind::Overdub, 5));
  TEST_ASSERT_TRUE(h.beginSession(SessionKind::Edit, 9));
  TEST_ASSERT_EQUAL_UINT8(5, h.sessionSlot(SessionKind::Overdub));
  TEST_ASSERT_EQUAL_UINT8(9, h.sessionSlot(SessionKind::Edit));
  h.endSession(SessionKind::Overdub);
  TEST_ASSERT_EQUAL_UINT8(0xFF, h.sessionSlot(SessionKind::Overdub));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_the_save_dots_follow_the_legacy_rules);
  RUN_TEST(test_positions_and_the_countdown);
  RUN_TEST(test_the_tempo_port_and_note_labels);
  RUN_TEST(test_the_undo_label_finds_the_open_sessions_slot);
  return UNITY_END();
}
