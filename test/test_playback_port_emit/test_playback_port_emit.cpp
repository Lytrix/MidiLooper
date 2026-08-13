//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// Gate 2 — enabled slots run the playback engine; mute/solo/slot-mute gate the port.

#include <unity.h>

#include "ActiveNoteLedger.h"
#include "Utils/PlaybackPortEmit.h"

void test_engine_runs_when_slot_enabled_even_if_muted() {
  TEST_ASSERT_TRUE(PlaybackPortEmit::engineShouldRun(true));
  TEST_ASSERT_FALSE(PlaybackPortEmit::engineShouldRun(false));
}

void test_port_emit_requires_audible_and_unmuted_slot() {
  TEST_ASSERT_TRUE(PlaybackPortEmit::portShouldEmit(true, false));
  TEST_ASSERT_FALSE(PlaybackPortEmit::portShouldEmit(false, false));
  TEST_ASSERT_FALSE(PlaybackPortEmit::portShouldEmit(true, true));
  TEST_ASSERT_FALSE(PlaybackPortEmit::portShouldEmit(false, true));
}

void test_ledger_stays_active_after_note_on() {
  ActiveNoteLedger ledger;
  ledger.noteOn(1, 60, 100, 90);
  TEST_ASSERT_TRUE(ledger.isActive(1, 60));
  uint8_t seen = 0;
  ledger.forEachActive([&](uint8_t channel, uint8_t note, const ActiveNoteLedger::Entry& entry) {
    TEST_ASSERT_EQUAL_UINT8(1, channel);
    TEST_ASSERT_EQUAL_UINT8(60, note);
    TEST_ASSERT_EQUAL_UINT32(100, entry.startTick);
    ++seen;
  });
  TEST_ASSERT_EQUAL_UINT8(1, seen);
}

int main(int /*argc*/, char** /*argv*/) {
  UNITY_BEGIN();
  RUN_TEST(test_engine_runs_when_slot_enabled_even_if_muted);
  RUN_TEST(test_port_emit_requires_audible_and_unmuted_slot);
  RUN_TEST(test_ledger_stays_active_after_note_on);
  return UNITY_END();
}
