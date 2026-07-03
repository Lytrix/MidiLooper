//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include <unordered_set>
#include <unity.h>

#include "../../src/Logger.cpp"
#include "../../src/Utils/LoopTickNormalize.cpp"
#include "../../src/Utils/LoopEventValidation.cpp"
#include "../../src/Utils/NoteUtils.cpp"
#include "Utils/LoopEventValidation.h"
#include "Utils/LoopTickNormalize.h"
#include "Utils/NoteMovementWrap.h"
#include "MidiEvent.h"

static bool hasNoteOffAt(const MidiEventVec& events, uint32_t tick, uint8_t ch, uint8_t note) {
  for (const MidiEvent& evt : events) {
    if (evt.isNoteOff() && evt.tick == tick && evt.channel == ch && evt.data.noteData.note == note) {
      return true;
    }
  }
  return false;
}

static bool hasNoteOnAt(const MidiEventVec& events, uint32_t tick, uint8_t ch, uint8_t note) {
  for (const MidiEvent& evt : events) {
    if (evt.isNoteOn() && evt.data.noteData.velocity > 0 && evt.tick == tick &&
        evt.channel == ch && evt.data.noteData.note == note) {
      return true;
    }
  }
  return false;
}

void test_normalize_wrap_pair_merges_to_linear() {
  constexpr uint32_t loopLength = 1536;
  MidiEventVec events;
  events.push_back(MidiEvent::NoteOff(50, 1, 60, 0));
  events.push_back(MidiEvent::NoteOn(1400, 1, 60, 100));

  const LoopTickNormalize::NormalizeResult result =
      LoopTickNormalize::normalizeWindow(events, loopLength, 1400, 1400);

  TEST_ASSERT_GREATER_THAN(0u, result.wrapPairsMerged);
  TEST_ASSERT_TRUE(hasNoteOnAt(events, 1400, 1, 60));
  TEST_ASSERT_TRUE(hasNoteOffAt(events, 1586, 1, 60));
  TEST_ASSERT_FALSE(hasNoteOffAt(events, 50, 1, 60));

  const auto validation = LoopEventValidation::validateLoopEvents(
      events, loopLength, LoopEventValidation::kCanonicalInvariantMask);
  TEST_ASSERT_TRUE(validation.passed);
}

void test_normalize_already_linear_idempotent() {
  constexpr uint32_t loopLength = 1536;
  MidiEventVec events;
  events.push_back(MidiEvent::NoteOn(1344, 1, 60, 100));
  events.push_back(MidiEvent::NoteOff(1536, 1, 60, 0));

  LoopTickNormalize::normalizeWindow(events, loopLength, 1344, 1536);
  const size_t countAfterFirst = events.size();
  const uint32_t offAfterFirst = events[1].tick;

  LoopTickNormalize::normalizeWindow(events, loopLength, 1344, 1536);
  TEST_ASSERT_EQUAL(countAfterFirst, events.size());
  TEST_ASSERT_EQUAL_UINT32(1536u, offAfterFirst);
  TEST_ASSERT_EQUAL_UINT32(1536u, events[1].tick);
}

void test_normalize_invariant_seven_after_wrap_pair_merge() {
  constexpr uint32_t loopLength = 1536;
  MidiEventVec events;
  events.push_back(MidiEvent::NoteOff(50, 1, 60, 0));
  events.push_back(MidiEvent::NoteOn(1400, 1, 60, 100));

  LoopTickNormalize::normalizeWindow(events, loopLength, 1400, 1400);
  uint32_t onTick = 0;
  uint32_t offTick = 0;
  for (const MidiEvent& evt : events) {
    if (evt.isNoteOn() && evt.data.noteData.note == 60) {
      onTick = evt.tick;
    }
    if (evt.isNoteOff() && evt.data.noteData.note == 60) {
      offTick = evt.tick;
    }
  }
  TEST_ASSERT_EQUAL_UINT32(186u, offTick - onTick);
}

void test_normalize_synth_loop_end_promoted() {
  constexpr uint32_t loopLength = 1536;
  MidiEventVec events;
  events.push_back(MidiEvent::NoteOn(1487, 5, 48, 100));
  events.push_back(MidiEvent::NoteOff(1535, 5, 48, 0));

  LoopTickNormalize::normalizeWindow(events, loopLength, 1487, 1535);
  TEST_ASSERT_TRUE(hasNoteOffAt(events, 1536, 5, 48));
  TEST_ASSERT_FALSE(hasNoteOffAt(events, 1535, 5, 48));
}

void test_normalize_moved_note_loop_end_off_not_promoted() {
  constexpr uint32_t loopLength = 1536;
  MidiEventVec events;
  events.push_back(MidiEvent::NoteOn(1410, 1, 94, 100));
  events.push_back(MidiEvent::NoteOff(1535, 1, 94, 0));

  LoopTickNormalize::normalizeAll(events, loopLength);
  TEST_ASSERT_TRUE(hasNoteOffAt(events, 1535, 1, 94));
  TEST_ASSERT_FALSE(hasNoteOffAt(events, 1536, 1, 94));

  const auto validation = LoopEventValidation::validateLoopEvents(
      events, loopLength, LoopEventValidation::kCanonicalInvariantMask);
  TEST_ASSERT_TRUE(validation.passed);
}

void test_normalize_all_retains_note_beyond_shortened_loop() {
  constexpr uint32_t loopLength = 1536;
  MidiEventVec events;
  events.push_back(MidiEvent::NoteOn(2000, 1, 60, 100));
  events.push_back(MidiEvent::NoteOff(2100, 1, 60, 0));

  LoopTickNormalize::normalizeAll(events, loopLength);
  TEST_ASSERT_TRUE(hasNoteOnAt(events, 2000, 1, 60));
  TEST_ASSERT_TRUE(hasNoteOffAt(events, 2100, 1, 60));
}

static void assignNoteId(MidiEvent& evt, NoteId noteId) { evt.noteId = noteId; }

void test_normalize_note_id_scope_leaves_other_wrap_pair() {
  constexpr uint32_t loopLength = 1536;
  constexpr NoteId kNoteIdA = 1;
  constexpr NoteId kNoteIdB = 2;

  MidiEventVec events;
  events.push_back(MidiEvent::NoteOff(50, 1, 60, 0));
  MidiEvent onA = MidiEvent::NoteOn(1400, 1, 60, 100);
  assignNoteId(onA, kNoteIdA);
  events.push_back(onA);

  events.push_back(MidiEvent::NoteOff(30, 1, 72, 0));
  MidiEvent onB = MidiEvent::NoteOn(1200, 1, 72, 100);
  assignNoteId(onB, kNoteIdB);
  events.push_back(onB);

  LoopTickNormalize::NormalizeOptions microOptions;
  microOptions.closeOpenTails = false;
  const LoopTickNormalize::NormalizeResult partial = LoopTickNormalize::normalize(
      events, loopLength, LoopTickNormalize::NormalizeScope::noteIds({kNoteIdA}), microOptions);

  TEST_ASSERT_GREATER_THAN(0u, partial.wrapPairsMerged);
  TEST_ASSERT_TRUE(hasNoteOffAt(events, 1586, 1, 60));
  TEST_ASSERT_TRUE(hasNoteOffAt(events, 30, 1, 72));

  const auto partialValidation = LoopEventValidation::validateLoopEvents(
      events, loopLength, LoopEventValidation::kCanonicalInvariantMask);
  TEST_ASSERT_FALSE(partialValidation.passed);

  LoopTickNormalize::normalizeAll(events, loopLength);
  const auto fullValidation = LoopEventValidation::validateLoopEvents(
      events, loopLength, LoopEventValidation::kCanonicalInvariantMask);
  TEST_ASSERT_TRUE(fullValidation.passed);
}

void test_normalize_micro_scope_skips_open_tail_close() {
  constexpr uint32_t loopLength = 1536;
  constexpr NoteId kNoteId = 7;

  MidiEventVec events;
  MidiEvent onOnly = MidiEvent::NoteOn(1400, 1, 60, 100);
  assignNoteId(onOnly, kNoteId);
  events.push_back(onOnly);

  LoopTickNormalize::NormalizeOptions microOptions;
  microOptions.closeOpenTails = false;
  const LoopTickNormalize::NormalizeResult result = LoopTickNormalize::normalize(
      events, loopLength, LoopTickNormalize::NormalizeScope::noteIds({kNoteId}), microOptions);

  TEST_ASSERT_EQUAL(0u, result.openTailsClosed);
  TEST_ASSERT_EQUAL(1u, events.size());
}

void test_move_past_loop_end_linear_off_at_macro_commit() {
  constexpr uint32_t loopLength = 1536;
  constexpr uint32_t noteLen = 191;
  constexpr NoteId kNoteId = 42;

  MidiEventVec events;
  events.push_back(MidiEvent::NoteOff(50, 1, 60, 0));
  MidiEvent on = MidiEvent::NoteOn(1344, 1, 60, 100);
  on.noteId = kNoteId;
  events.push_back(on);

  // Linear move +1 (152335 repro): on@1345, off@1536 — not off@0.
  events[1].tick = 1345;
  events[0].tick =
      NoteMovementUtils::linearStorageOffTickForSpanEnd(1345, noteLen);

  LoopTickNormalize::normalizeAll(events, loopLength);

  TEST_ASSERT_TRUE(hasNoteOnAt(events, 1345, 1, 60));
  TEST_ASSERT_TRUE(hasNoteOffAt(events, 1536, 1, 60));
  TEST_ASSERT_FALSE(hasNoteOffAt(events, 0, 1, 60));
  TEST_ASSERT_FALSE(hasNoteOffAt(events, 50, 1, 60));

  const auto validation = LoopEventValidation::validateLoopEvents(
      events, loopLength, LoopEventValidation::kCanonicalInvariantMask);
  TEST_ASSERT_TRUE(validation.passed);
}

int main(int /*argc*/, char** /*argv*/) {
  UNITY_BEGIN();
  RUN_TEST(test_normalize_wrap_pair_merges_to_linear);
  RUN_TEST(test_normalize_already_linear_idempotent);
  RUN_TEST(test_normalize_invariant_seven_after_wrap_pair_merge);
  RUN_TEST(test_normalize_synth_loop_end_promoted);
  RUN_TEST(test_normalize_moved_note_loop_end_off_not_promoted);
  RUN_TEST(test_normalize_all_retains_note_beyond_shortened_loop);
  RUN_TEST(test_normalize_note_id_scope_leaves_other_wrap_pair);
  RUN_TEST(test_normalize_micro_scope_skips_open_tail_close);
  RUN_TEST(test_move_past_loop_end_linear_off_at_macro_commit);
  return UNITY_END();
}
