//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include <unity.h>

#include "../../src/Logger.cpp"
#include "../../src/Utils/LoopEventValidation.cpp"
#include "../../src/Utils/IntervalProjection.cpp"
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

/// Two sequential notes on one pitch — one in the head window, one in the tail window — are not a
/// stored wrap pair. Judging every same channel/pitch off against every on matched (on 2016,
/// off 192) and reported check=4 on a canonical store (session_20260805_030517).
void test_validate_no_wrapped_pair_storage_sequential_same_pitch_passes() {
  constexpr uint32_t loopLength = 2304;
  MidiEventVec events;
  events.push_back(MidiEvent::NoteOn(96, 5, 26, 100));
  events.push_back(MidiEvent::NoteOff(192, 5, 26, 0));
  events.push_back(MidiEvent::NoteOn(2016, 5, 26, 100));
  events.push_back(MidiEvent::NoteOff(2112, 5, 26, 0));

  const auto result = LoopEventValidation::validateLoopEvents(
      events, loopLength,
      static_cast<uint32_t>(LoopEventValidation::LoopEventCheck::NoWrappedPairStorage));
  TEST_ASSERT_TRUE(result.passed);
}

/// Same store with explicit noteIds takes the strongest pairing path.
void test_validate_no_wrapped_pair_storage_sequential_same_pitch_with_note_ids_passes() {
  constexpr uint32_t loopLength = 2304;
  MidiEventVec events;
  events.push_back(MidiEvent::NoteOn(96, 5, 26, 100));
  events[0].noteId = 11;
  events.push_back(MidiEvent::NoteOff(192, 5, 26, 0));
  events[1].noteId = 11;
  events.push_back(MidiEvent::NoteOn(2016, 5, 26, 100));
  events[2].noteId = 12;
  events.push_back(MidiEvent::NoteOff(2112, 5, 26, 0));
  events[3].noteId = 12;

  const auto result = LoopEventValidation::validateLoopEvents(
      events, loopLength,
      static_cast<uint32_t>(LoopEventValidation::LoopEventCheck::NoWrappedPairStorage));
  TEST_ASSERT_TRUE(result.passed);
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

/// Two sequential notes on one pitch are canonical. The unpaired all-pairs scan reported
/// check=2 here because note-on B matched note-off A (session_20260805_020716 lines 5168/7102).
void test_validate_sequential_same_pitch_notes_pass() {
  constexpr uint32_t loopLength = 2304;
  MidiEventVec events;
  events.push_back(MidiEvent::NoteOn(100, 1, 60, 100));
  events.push_back(MidiEvent::NoteOff(200, 1, 60, 0));
  events.push_back(MidiEvent::NoteOn(500, 1, 60, 100));
  events.push_back(MidiEvent::NoteOff(600, 1, 60, 0));

  const auto result = LoopEventValidation::validateLoopEvents(
      events, loopLength, LoopEventValidation::kCanonicalInvariantMask);
  TEST_ASSERT_TRUE(result.passed);
}

/// Same as above but with noteId tags on both sides, which is what the session store carries.
void test_validate_sequential_same_pitch_notes_with_note_ids_pass() {
  constexpr uint32_t loopLength = 2304;
  MidiEventVec events;
  events.push_back(MidiEvent::NoteOn(100, 1, 60, 100));
  events[0].noteId = 1;
  events.push_back(MidiEvent::NoteOff(200, 1, 60, 0));
  events[1].noteId = 1;
  events.push_back(MidiEvent::NoteOn(500, 1, 60, 100));
  events[2].noteId = 2;
  events.push_back(MidiEvent::NoteOff(600, 1, 60, 0));
  events[3].noteId = 2;

  const auto result = LoopEventValidation::validateLoopEvents(
      events, loopLength, LoopEventValidation::kCanonicalInvariantMask);
  TEST_ASSERT_TRUE(result.passed);
}

/// Pairing must not hide a real inversion: this note's own off precedes its on.
void test_validate_linear_note_off_fails_inverted_own_pair() {
  constexpr uint32_t loopLength = 2304;
  MidiEventVec events;
  events.push_back(MidiEvent::NoteOn(100, 1, 60, 100));
  events[0].noteId = 1;
  events.push_back(MidiEvent::NoteOff(80, 1, 60, 0));
  events[1].noteId = 1;

  const auto result = LoopEventValidation::validateLoopEvents(
      events, loopLength,
      static_cast<uint32_t>(LoopEventValidation::LoopEventCheck::LinearNoteOff));
  TEST_ASSERT_FALSE(result.passed);
}

/// A note stored beyond loopLength is linear, not wrapped, and must still pass.
void test_validate_linear_note_off_beyond_loop_length_passes() {
  constexpr uint32_t loopLength = 2304;
  MidiEventVec events;
  events.push_back(MidiEvent::NoteOn(2200, 1, 60, 100));
  events[0].noteId = 1;
  events.push_back(MidiEvent::NoteOff(2400, 1, 60, 0));
  events[1].noteId = 1;

  const auto result = LoopEventValidation::validateLoopEvents(
      events, loopLength,
      static_cast<uint32_t>(LoopEventValidation::LoopEventCheck::LinearNoteOff));
  TEST_ASSERT_TRUE(result.passed);
}

int main(int /*argc*/, char** /*argv*/) {
  UNITY_BEGIN();
  RUN_TEST(test_validate_canonical_linear_passes);
  RUN_TEST(test_validate_sequential_same_pitch_notes_pass);
  RUN_TEST(test_validate_sequential_same_pitch_notes_with_note_ids_pass);
  RUN_TEST(test_validate_linear_note_off_fails_inverted_own_pair);
  RUN_TEST(test_validate_linear_note_off_beyond_loop_length_passes);
  RUN_TEST(test_validate_note_on_in_loop_range_fails);
  RUN_TEST(test_validate_linear_note_off_fails_wrap_pair);
  RUN_TEST(test_validate_no_wrapped_pair_storage_fails);
  RUN_TEST(test_validate_no_wrapped_pair_storage_sequential_same_pitch_passes);
  RUN_TEST(test_validate_no_wrapped_pair_storage_sequential_same_pitch_with_note_ids_passes);
  RUN_TEST(test_validate_note_id_pairing_fails_duplicate);
  RUN_TEST(test_validate_persisted_tick_cap_fails);
  RUN_TEST(test_validate_orphan_note_off_fails);
  return UNITY_END();
}
