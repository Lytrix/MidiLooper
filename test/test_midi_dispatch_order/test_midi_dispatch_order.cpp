//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include <unity.h>

#include "Utils/MidiBatchComposition.h"
#include "Utils/MidiDispatchOrder.h"

namespace {

void assertOrder(const uint8_t* types, size_t count, const size_t* expected) {
  size_t actual[16] = {};
  TEST_ASSERT_TRUE(count <= 16);
  MidiDispatchOrder::planDispatchOrder(types, count, actual);
  for (size_t i = 0; i < count; ++i) {
    TEST_ASSERT_EQUAL_UINT32(static_cast<uint32_t>(expected[i]),
                             static_cast<uint32_t>(actual[i]));
  }
}

}  // namespace

void test_clock_note_clock_preserves_relative_fifo() {
  const uint8_t types[] = {midi::Clock, midi::NoteOn, midi::Clock};
  const size_t expected[] = {0, 1, 2};
  assertOrder(types, 3, expected);
}

void test_note_before_start_still_transport_first() {
  // BeatStep-style: NoteOn before Start on same downbeat → Start still first.
  const uint8_t types[] = {midi::NoteOn, midi::Start};
  const size_t expected[] = {1, 0};
  assertOrder(types, 2, expected);
}

void test_start_clock_note_order() {
  const uint8_t types[] = {midi::Start, midi::Clock, midi::NoteOn};
  const size_t expected[] = {0, 1, 2};
  assertOrder(types, 3, expected);
}

void test_stop_continues_transport_first_among_notes() {
  const uint8_t types[] = {midi::NoteOff, midi::Stop, midi::Clock, midi::NoteOn};
  const size_t expected[] = {1, 0, 2, 3};
  assertOrder(types, 4, expected);
}

void test_continue_before_clock_and_note() {
  const uint8_t types[] = {midi::Clock, midi::Continue, midi::NoteOn};
  const size_t expected[] = {1, 0, 2};
  assertOrder(types, 3, expected);
}

void test_is_sequence_transport_excludes_clock() {
  TEST_ASSERT_TRUE(MidiDispatchOrder::isSequenceTransport(midi::Start));
  TEST_ASSERT_TRUE(MidiDispatchOrder::isSequenceTransport(midi::Stop));
  TEST_ASSERT_TRUE(MidiDispatchOrder::isSequenceTransport(midi::Continue));
  TEST_ASSERT_FALSE(MidiDispatchOrder::isSequenceTransport(midi::Clock));
  TEST_ASSERT_FALSE(MidiDispatchOrder::isSequenceTransport(midi::NoteOn));
}

void test_clock_note_clock_composition_keeps_split_runs() {
  const uint8_t types[] = {midi::Clock, midi::NoteOn, midi::Clock};
  size_t order[3] = {};
  MidiDispatchOrder::planDispatchOrder(types, 3, order);
  const uint8_t dispatched[] = {types[order[0]], types[order[1]], types[order[2]]};
  const auto summary = MidiBatchComposition::summarize(dispatched, 3);
  TEST_ASSERT_EQUAL_UINT32(3, summary.size);
  TEST_ASSERT_EQUAL_UINT32(2, summary.clockCount);
  TEST_ASSERT_EQUAL_UINT32(1, summary.nonClockCount);
  TEST_ASSERT_EQUAL_UINT32(1, summary.maxConsecutiveClocks);
  TEST_ASSERT_EQUAL_UINT8(0, MidiBatchComposition::countBucket(summary.size));
}

void test_composition_buckets_and_consecutive_clocks() {
  uint8_t types[12] = {};
  for (uint8_t i = 0; i < 9; ++i) {
    types[i] = midi::Clock;
  }
  types[9] = midi::NoteOn;
  types[10] = midi::Clock;
  types[11] = midi::Clock;
  const auto summary = MidiBatchComposition::summarize(types, 12);
  TEST_ASSERT_EQUAL_UINT32(12, summary.size);
  TEST_ASSERT_EQUAL_UINT32(11, summary.clockCount);
  TEST_ASSERT_EQUAL_UINT32(1, summary.nonClockCount);
  TEST_ASSERT_EQUAL_UINT32(9, summary.maxConsecutiveClocks);
  TEST_ASSERT_EQUAL_UINT8(1, MidiBatchComposition::countBucket(summary.size));
  TEST_ASSERT_EQUAL_UINT8(1, MidiBatchComposition::countBucket(summary.clockCount));
  TEST_ASSERT_EQUAL_UINT8(2, MidiBatchComposition::countBucket(17));
  TEST_ASSERT_EQUAL_UINT8(3, MidiBatchComposition::countBucket(64));
  TEST_ASSERT_EQUAL_UINT8(4, MidiBatchComposition::countBucket(65));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_clock_note_clock_preserves_relative_fifo);
  RUN_TEST(test_note_before_start_still_transport_first);
  RUN_TEST(test_start_clock_note_order);
  RUN_TEST(test_stop_continues_transport_first_among_notes);
  RUN_TEST(test_continue_before_clock_and_note);
  RUN_TEST(test_is_sequence_transport_excludes_clock);
  RUN_TEST(test_clock_note_clock_composition_keeps_split_runs);
  RUN_TEST(test_composition_buckets_and_consecutive_clocks);
  return UNITY_END();
}
