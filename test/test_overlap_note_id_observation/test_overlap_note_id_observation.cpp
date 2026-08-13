//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0
//
// Gate 0 / Gate 1 — OverlapNoteIdSet + observed vs geometry candidate equality.

#include <unity.h>

#include "Globals.h"
#include "OverlapNoteIdObservation.h"
#include "OverlapNoteIdSet.h"
#include "Utils/NoteUtils.h"

#include "../../src/Logger.cpp"
#include "../../src/Utils/IntervalProjection.cpp"
#include "../../src/Utils/NoteUtils.cpp"
#include "../../src/Utils/DisplayWindowUtils.cpp"

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
  OverlapNoteIdSet observed;
  OverlapNoteIdSet geometry;
  collectBoth(notes, kLoopLen - 30, kLoopLen - 5, observed, geometry);
  TEST_ASSERT_TRUE(observed.contains(1));
  // Pin G1-wrap-probe: noteIntersectsWindow steps by 16th and only also tests note start/end.
  // Incoming [L-30, L-5) is 25 ticks; wrap on/off sit outside it, so geometry misses a
  // still-sounding wrap note. Observation keeps it. Do not change DisplayWindowUtils here.
  TEST_ASSERT_FALSE(geometry.contains(1));
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
  OverlapNoteIdSet observed;
  OverlapNoteIdSet geometry;
  collectBoth(notes, 1000, 1800, observed, geometry);
  TEST_ASSERT_FALSE(observed.contains(1));
  // Pin G1-end-touch: noteIntersectsWindow treats endTick as a probe, so a note that ends
  // exactly at S is a geometry candidate. Observation [S,E) sounding excludes it.
  TEST_ASSERT_TRUE(geometry.contains(1));
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
  return UNITY_END();
}
