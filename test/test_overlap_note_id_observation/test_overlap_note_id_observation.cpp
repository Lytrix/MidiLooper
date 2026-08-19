//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// Gate 0 / Gate 1 — OverlapNoteIdSet + diagnostic observed vs production geometry.
// OverlapNoteIdObservation is test-only. Selection is normalized geometry + [S, E).

#include <unity.h>

#include "Globals.h"
#include "OverlapNoteIdObservation.h"
#include "OverlapNoteIdSet.h"
#include "Utils/NoteUtils.h"

#include "../../src/Logger.cpp"
#include "../../src/Utils/IntervalProjection.cpp"
#include "../../src/Utils/NoteUtils.cpp"

namespace {

constexpr uint32_t kLoopLen = 8u * Config::TICKS_PER_BAR;
constexpr uint8_t kPitch = 60;

NoteUtils::DisplayNote makeNote(NoteId id, uint32_t startTick, uint32_t endTick,
                                uint8_t pitch = kPitch) {
  NoteUtils::DisplayNote note{};
  note.noteId = id;
  note.note = pitch;
  note.velocity = 100;
  note.startTick = startTick;
  note.endTick = endTick;
  return note;
}

void collectBoth(const NoteUtils::DisplayNoteVec& notes, uint32_t startTick, uint32_t endTick,
                 OverlapNoteIdSet& observed, OverlapNoteIdSet& geometry) {
  OverlapNoteIdObservation::collectObservedOverlapNoteIds(notes, kPitch, startTick, endTick,
                                                          kLoopLen, observed);
  OverlapNoteIdObservation::collectGeometryOverlapNoteIds(notes, kPitch, startTick, endTick,
                                                          kLoopLen, geometry);
}

void assertObservedEqualsGeometry(const NoteUtils::DisplayNoteVec& notes, uint32_t startTick,
                                  uint32_t endTick) {
  OverlapNoteIdSet observed;
  OverlapNoteIdSet geometry;
  collectBoth(notes, startTick, endTick, observed, geometry);
  TEST_ASSERT_FALSE(observed.overflowed());
  TEST_ASSERT_FALSE(geometry.overflowed());
  TEST_ASSERT_TRUE(observed == geometry);
}

}  // namespace

void test_gate0_insert_contains_and_duplicate() {
  OverlapNoteIdSet ids;
  TEST_ASSERT_TRUE(ids.insert(1));
  TEST_ASSERT_TRUE(ids.insert(2));
  TEST_ASSERT_TRUE(ids.contains(1));
  TEST_ASSERT_TRUE(ids.contains(2));
  TEST_ASSERT_FALSE(ids.contains(3));
  TEST_ASSERT_EQUAL_UINT32(2u, static_cast<uint32_t>(ids.size()));
  TEST_ASSERT_TRUE(ids.insert(1));
  TEST_ASSERT_EQUAL_UINT32(2u, static_cast<uint32_t>(ids.size()));
  TEST_ASSERT_FALSE(ids.overflowed());
}

void test_gate0_rejects_invalid_and_overflow_does_not_grow() {
  OverlapNoteIdSet ids;
  TEST_ASSERT_FALSE(ids.insert(kInvalidNoteId));
  TEST_ASSERT_EQUAL_UINT32(0u, static_cast<uint32_t>(ids.size()));

  for (NoteId id = 1; id <= kOverlapNoteIdSetCapacity; ++id) {
    TEST_ASSERT_TRUE(ids.insert(id));
  }
  TEST_ASSERT_EQUAL_UINT32(static_cast<uint32_t>(kOverlapNoteIdSetCapacity),
                           static_cast<uint32_t>(ids.size()));
  TEST_ASSERT_FALSE(ids.overflowed());
  TEST_ASSERT_FALSE(ids.insert(kOverlapNoteIdSetCapacity + 1));
  TEST_ASSERT_TRUE(ids.overflowed());
  TEST_ASSERT_EQUAL_UINT32(static_cast<uint32_t>(kOverlapNoteIdSetCapacity),
                           static_cast<uint32_t>(ids.size()));
  TEST_ASSERT_FALSE(ids.contains(kOverlapNoteIdSetCapacity + 1));
}

void test_gate0_full_loop_hold_unique_same_pitch_count_fits_provisional_capacity() {
  NoteUtils::DisplayNoteVec notes;
  const uint32_t step = Config::TICKS_PER_16TH_STEP;
  NoteId nextId = 1;
  for (uint32_t start = 0; start + step < kLoopLen; start += step) {
    notes.push_back(makeNote(nextId++, start, start + step));
  }
  OverlapNoteIdSet observed;
  OverlapNoteIdObservation::collectObservedOverlapNoteIds(notes, kPitch, 0, kLoopLen, kLoopLen,
                                                          observed);
  TEST_ASSERT_FALSE(observed.overflowed());
  TEST_ASSERT_EQUAL_UINT32(static_cast<uint32_t>(notes.size()),
                           static_cast<uint32_t>(observed.size()));
  TEST_ASSERT_TRUE(observed.size() <= kOverlapNoteIdSetCapacity);
}

void test_gate1_starts_after_on_before_off() {
  NoteUtils::DisplayNoteVec notes;
  notes.push_back(makeNote(1, 1500, 2000));
  assertObservedEqualsGeometry(notes, 1000, 1800);
  OverlapNoteIdSet observed;
  OverlapNoteIdSet geometry;
  collectBoth(notes, 1000, 1800, observed, geometry);
  TEST_ASSERT_TRUE(observed.contains(1));
}

void test_gate1_starts_exactly_at_incoming_end_excluded() {
  NoteUtils::DisplayNoteVec notes;
  notes.push_back(makeNote(1, 1800, 2000));
  assertObservedEqualsGeometry(notes, 1000, 1800);
  OverlapNoteIdSet observed;
  OverlapNoteIdObservation::collectObservedOverlapNoteIds(notes, kPitch, 1000, 1800, kLoopLen,
                                                          observed);
  TEST_ASSERT_FALSE(observed.contains(1));
}

void test_gate1_starts_one_tick_before_incoming_end() {
  NoteUtils::DisplayNoteVec notes;
  notes.push_back(makeNote(1, 1799, 2000));
  assertObservedEqualsGeometry(notes, 1000, 1800);
  OverlapNoteIdSet observed;
  OverlapNoteIdObservation::collectObservedOverlapNoteIds(notes, kPitch, 1000, 1800, kLoopLen,
                                                          observed);
  TEST_ASSERT_TRUE(observed.contains(1));
}

void test_gate1_already_sounding_at_incoming_start() {
  NoteUtils::DisplayNoteVec notes;
  notes.push_back(makeNote(1, 500, 1600));
  assertObservedEqualsGeometry(notes, 1000, 1800);
  OverlapNoteIdSet observed;
  OverlapNoteIdObservation::collectObservedOverlapNoteIds(notes, kPitch, 1000, 1800, kLoopLen,
                                                          observed);
  TEST_ASSERT_TRUE(observed.contains(1));
}

void test_gate1_ended_during_span_kept() {
  NoteUtils::DisplayNoteVec notes;
  notes.push_back(makeNote(1, 1200, 1400));
  assertObservedEqualsGeometry(notes, 1000, 1800);
  OverlapNoteIdSet observed;
  OverlapNoteIdObservation::collectObservedOverlapNoteIds(notes, kPitch, 1000, 1800, kLoopLen,
                                                          observed);
  TEST_ASSERT_TRUE(observed.contains(1));
}

void test_gate1_nested_same_pitch() {
  NoteUtils::DisplayNoteVec notes;
  notes.push_back(makeNote(1, 800, 2000));
  notes.push_back(makeNote(2, 1200, 1400));
  notes.push_back(makeNote(3, 1500, 1900));
  assertObservedEqualsGeometry(notes, 1000, 1800);
  OverlapNoteIdSet observed;
  OverlapNoteIdObservation::collectObservedOverlapNoteIds(notes, kPitch, 1000, 1800, kLoopLen,
                                                          observed);
  TEST_ASSERT_TRUE(observed.contains(1));
  TEST_ASSERT_TRUE(observed.contains(2));
  TEST_ASSERT_TRUE(observed.contains(3));
}

void test_gate1_other_pitch_excluded() {
  NoteUtils::DisplayNoteVec notes;
  notes.push_back(makeNote(1, 1200, 1400, 60));
  notes.push_back(makeNote(2, 1200, 1400, 72));
  assertObservedEqualsGeometry(notes, 1000, 1800);
  OverlapNoteIdSet observed;
  OverlapNoteIdObservation::collectObservedOverlapNoteIds(notes, kPitch, 1000, 1800, kLoopLen,
                                                          observed);
  TEST_ASSERT_TRUE(observed.contains(1));
  TEST_ASSERT_FALSE(observed.contains(2));
}

void test_gate1_wrap_tail_to_head() {
  NoteUtils::DisplayNoteVec notes;
  notes.push_back(makeNote(1, kLoopLen - 40, 20));
  assertObservedEqualsGeometry(notes, 0, 30);
  OverlapNoteIdSet observed;
  OverlapNoteIdObservation::collectObservedOverlapNoteIds(notes, kPitch, 0, 30, kLoopLen,
                                                          observed);
  TEST_ASSERT_TRUE(observed.contains(1));
}

void test_gate1_incoming_in_tail_against_wrap() {
  NoteUtils::DisplayNoteVec notes;
  notes.push_back(makeNote(1, kLoopLen - 40, 20));
  assertObservedEqualsGeometry(notes, kLoopLen - 30, kLoopLen - 5);
  OverlapNoteIdSet observed;
  OverlapNoteIdObservation::collectObservedOverlapNoteIds(notes, kPitch, kLoopLen - 30,
                                                          kLoopLen - 5, kLoopLen, observed);
  TEST_ASSERT_TRUE(observed.contains(1));
}

void test_gate1_incoming_ending_after_wrap() {
  NoteUtils::DisplayNoteVec notes;
  notes.push_back(makeNote(1, kLoopLen - 40, kLoopLen - 10));
  notes.push_back(makeNote(2, 8, 48));
  notes.push_back(makeNote(3, kLoopLen - 80, 16));
  assertObservedEqualsGeometry(notes, kLoopLen - 50, kLoopLen + 40);
}

void test_gate1_ends_exactly_at_incoming_start() {
  NoteUtils::DisplayNoteVec notes;
  notes.push_back(makeNote(1, 500, 1000));
  assertObservedEqualsGeometry(notes, 1000, 1800);
  OverlapNoteIdSet observed;
  OverlapNoteIdObservation::collectObservedOverlapNoteIds(notes, kPitch, 1000, 1800, kLoopLen,
                                                          observed);
  TEST_ASSERT_FALSE(observed.contains(1));
}

void test_gate1_zero_length_note_excluded() {
  NoteUtils::DisplayNoteVec notes;
  notes.push_back(makeNote(1, 1200, 1200));
  notes.push_back(makeNote(2, 1100, 1400));
  assertObservedEqualsGeometry(notes, 1000, 1800);
  OverlapNoteIdSet observed;
  OverlapNoteIdObservation::collectObservedOverlapNoteIds(notes, kPitch, 1000, 1800, kLoopLen,
                                                          observed);
  TEST_ASSERT_FALSE(observed.contains(1));
  TEST_ASSERT_TRUE(observed.contains(2));
}

// Reconstructed split-chunk span from test_windowed_overlap_matches_full_note_split_across_chunks:
// NoteOn@50 in chunk 0, NoteOff@400 in chunk 1, incoming [300, 350) in the sounding gap.
void test_gate1_split_chunk_on_off_outside_incoming_window() {
  NoteUtils::DisplayNoteVec notes;
  notes.push_back(makeNote(1, 50, 400));
  notes.push_back(makeNote(10, 0, 1, 72));
  notes.push_back(makeNote(11, 254, 255, 72));
  assertObservedEqualsGeometry(notes, 300, 350);
  OverlapNoteIdSet observed;
  OverlapNoteIdObservation::collectObservedOverlapNoteIds(notes, kPitch, 300, 350, kLoopLen,
                                                          observed);
  TEST_ASSERT_TRUE(observed.contains(1));
  TEST_ASSERT_EQUAL_UINT32(1u, static_cast<uint32_t>(observed.size()));
}

// After seal, source [50, 200) is shortened to endTick 119
// (test_seal_pending_shorten_to_edit_pass_after_overdub_publish).
void test_gate1_prior_shorten_companion_uses_shortened_span() {
  NoteUtils::DisplayNoteVec notes;
  notes.push_back(makeNote(1, 50, 119));
  assertObservedEqualsGeometry(notes, 80, 110);
  OverlapNoteIdSet stillOverlaps;
  OverlapNoteIdObservation::collectGeometryOverlapNoteIds(notes, kPitch, 80, 110, kLoopLen,
                                                          stillOverlaps);
  TEST_ASSERT_TRUE(stillOverlaps.contains(1));

  assertObservedEqualsGeometry(notes, 130, 170);
  OverlapNoteIdSet afterShortenedEnd;
  OverlapNoteIdObservation::collectGeometryOverlapNoteIds(notes, kPitch, 130, 170, kLoopLen,
                                                          afterShortenedEnd);
  TEST_ASSERT_FALSE(afterShortenedEnd.contains(1));
}

void test_gate1_prior_hide_companion_absent_from_geometry() {
  NoteUtils::DisplayNoteVec notes;
  notes.push_back(makeNote(2, 2000, 2200));
  assertObservedEqualsGeometry(notes, 1100, 1300);
  OverlapNoteIdSet observed;
  OverlapNoteIdObservation::collectObservedOverlapNoteIds(notes, kPitch, 1100, 1300, kLoopLen,
                                                          observed);
  TEST_ASSERT_FALSE(observed.contains(1));
  TEST_ASSERT_FALSE(observed.contains(2));
}

void test_display_note_present_at_hold_linearizes_short_wrap_pair() {
  constexpr uint32_t kOneBar = 768;
  TEST_ASSERT_FALSE(NoteUtils::isWrappedLoopNotePair(100, 50, kOneBar));
  TEST_ASSERT_TRUE(
      OverlapNoteIdObservation::displayNotePresentAtHold(100, 50, 200, kOneBar));
  TEST_ASSERT_TRUE(
      OverlapNoteIdObservation::displayNotePresentAtHold(100, 50, 100, kOneBar));
  TEST_ASSERT_FALSE(
      OverlapNoteIdObservation::displayNotePresentAtHold(100, 50, 50, kOneBar));
}

void test_gate1_unrelated_companion_other_pitch_does_not_select() {
  NoteUtils::DisplayNoteVec notes;
  notes.push_back(makeNote(1, 1000, 1800));
  notes.push_back(makeNote(2, 100, 150, 72));
  assertObservedEqualsGeometry(notes, 1200, 1600);
  OverlapNoteIdSet observed;
  OverlapNoteIdObservation::collectObservedOverlapNoteIds(notes, kPitch, 1200, 1600, kLoopLen,
                                                          observed);
  TEST_ASSERT_TRUE(observed.contains(1));
  TEST_ASSERT_FALSE(observed.contains(2));
  TEST_ASSERT_EQUAL_UINT32(1u, static_cast<uint32_t>(observed.size()));
}

int main(int /*argc*/, char** /*argv*/) {
  UNITY_BEGIN();
  RUN_TEST(test_gate0_insert_contains_and_duplicate);
  RUN_TEST(test_gate0_rejects_invalid_and_overflow_does_not_grow);
  RUN_TEST(test_gate0_full_loop_hold_unique_same_pitch_count_fits_provisional_capacity);
  RUN_TEST(test_gate1_starts_after_on_before_off);
  RUN_TEST(test_gate1_starts_exactly_at_incoming_end_excluded);
  RUN_TEST(test_gate1_starts_one_tick_before_incoming_end);
  RUN_TEST(test_gate1_already_sounding_at_incoming_start);
  RUN_TEST(test_gate1_ended_during_span_kept);
  RUN_TEST(test_gate1_nested_same_pitch);
  RUN_TEST(test_gate1_other_pitch_excluded);
  RUN_TEST(test_gate1_wrap_tail_to_head);
  RUN_TEST(test_gate1_incoming_in_tail_against_wrap);
  RUN_TEST(test_gate1_incoming_ending_after_wrap);
  RUN_TEST(test_gate1_ends_exactly_at_incoming_start);
  RUN_TEST(test_gate1_zero_length_note_excluded);
  RUN_TEST(test_gate1_split_chunk_on_off_outside_incoming_window);
  RUN_TEST(test_gate1_prior_shorten_companion_uses_shortened_span);
  RUN_TEST(test_gate1_prior_hide_companion_absent_from_geometry);
  RUN_TEST(test_display_note_present_at_hold_linearizes_short_wrap_pair);
  RUN_TEST(test_gate1_unrelated_companion_other_pitch_does_not_select);
  return UNITY_END();
}
