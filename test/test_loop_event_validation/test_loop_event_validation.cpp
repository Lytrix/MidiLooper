//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include <unity.h>

#include "../../src/Logger.cpp"
#include "../../src/Utils/LoopEventValidation.cpp"
#include "../../src/Utils/NoteUtils.cpp"
#include "Utils/LoopEventValidation.h"
#include "MidiEvent.h"

void test_validate_canonical_linear_passes() {
  constexpr uint32_t loopLength = 1536;
  MidiEventVec events;
  events.push_back(MidiEvent::NoteOn(1344, 1, 60, 100));
  events[0].noteId = 1;
  events.push_back(MidiEvent::NoteOff(1536, 1, 60, 0));

  const auto result = LoopEventValidation::validateLoopEvents(
      events, loopLength, LoopEventValidation::kCanonicalInvariantMask);
  TEST_ASSERT_TRUE(result.passed);
}

void test_validate_note_on_in_loop_range_fails() {
  constexpr uint32_t loopLength = 1536;
  MidiEventVec events;
  events.push_back(MidiEvent::NoteOn(1536, 1, 60, 100));
  events.push_back(MidiEvent::NoteOff(1600, 1, 60, 0));

  const auto result = LoopEventValidation::validateLoopEvents(
      events, loopLength,
      static_cast<uint32_t>(LoopEventValidation::LoopEventCheck::NoteOnInLoopRange));
  TEST_ASSERT_FALSE(result.passed);
  TEST_ASSERT_EQUAL(static_cast<int>(LoopEventValidation::LoopEventCheck::NoteOnInLoopRange),
                    static_cast<int>(result.firstFailure));
}

void test_validate_linear_note_off_fails_wrap_pair() {
  constexpr uint32_t loopLength = 1536;
  MidiEventVec events;
  events.push_back(MidiEvent::NoteOff(50, 1, 60, 0));
  events.push_back(MidiEvent::NoteOn(1400, 1, 60, 100));

  const auto result = LoopEventValidation::validateLoopEvents(
      events, loopLength,
      static_cast<uint32_t>(LoopEventValidation::LoopEventCheck::LinearNoteOff));
  TEST_ASSERT_FALSE(result.passed);
}

void test_validate_no_wrapped_pair_storage_fails() {
  constexpr uint32_t loopLength = 1536;
  MidiEventVec events;
  events.push_back(MidiEvent::NoteOff(50, 1, 60, 0));
  events.push_back(MidiEvent::NoteOn(1400, 1, 60, 100));

  const auto result = LoopEventValidation::validateLoopEvents(
      events, loopLength,
      static_cast<uint32_t>(LoopEventValidation::LoopEventCheck::NoWrappedPairStorage));
  TEST_ASSERT_FALSE(result.passed);
}

void test_validate_note_id_pairing_fails_duplicate() {
  MidiEventVec events;
  events.push_back(MidiEvent::NoteOn(100, 1, 60, 100));
  events[0].noteId = 7;
  events.push_back(MidiEvent::NoteOn(200, 1, 62, 100));
  events[1].noteId = 7;
  events.push_back(MidiEvent::NoteOff(300, 1, 60, 0));
  events.push_back(MidiEvent::NoteOff(400, 1, 62, 0));

  const auto result = LoopEventValidation::validateLoopEvents(
      events, 1536, static_cast<uint32_t>(LoopEventValidation::LoopEventCheck::NoteIdPairing));
  TEST_ASSERT_FALSE(result.passed);
}

void test_validate_persisted_tick_cap_fails() {
  constexpr uint32_t loopLength = 1536;
  MidiEventVec events;
  events.push_back(MidiEvent::NoteOn(100, 1, 60, 100));
  events.push_back(MidiEvent::NoteOff(loopLength + 1000, 1, 60, 0));

  const auto result = LoopEventValidation::validateLoopEvents(
      events, loopLength,
      static_cast<uint32_t>(LoopEventValidation::LoopEventCheck::PersistedTickCap));
  TEST_ASSERT_FALSE(result.passed);
}

void test_validate_orphan_note_off_fails() {
  MidiEventVec events;
  events.push_back(MidiEvent::NoteOff(100, 1, 60, 0));

  const auto result = LoopEventValidation::validateLoopEvents(
      events, 1536, static_cast<uint32_t>(LoopEventValidation::LoopEventCheck::OrphanNoteOff));
  TEST_ASSERT_FALSE(result.passed);
}

int main(int /*argc*/, char** /*argv*/) {
  UNITY_BEGIN();
  RUN_TEST(test_validate_canonical_linear_passes);
  RUN_TEST(test_validate_note_on_in_loop_range_fails);
  RUN_TEST(test_validate_linear_note_off_fails_wrap_pair);
  RUN_TEST(test_validate_no_wrapped_pair_storage_fails);
  RUN_TEST(test_validate_note_id_pairing_fails_duplicate);
  RUN_TEST(test_validate_persisted_tick_cap_fails);
  RUN_TEST(test_validate_orphan_note_off_fails);
  return UNITY_END();
}
