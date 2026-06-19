//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include <unity.h>

#include "../../src/Logger.cpp"
#include "../../src/Utils/NoteUtils.cpp"
#include "../../src/NoteEditFocus.cpp"

#include "NoteEditFocus.h"
#include "MidiEvent.h"
#include "Utils/NoteMovementWrap.h"

namespace {

MidiEventVec makeTwoNoteFlat(uint32_t startA, uint32_t endA, uint32_t startB, uint32_t endB,
                           uint8_t pitch) {
  MidiEventVec flat;
  flat.push_back(MidiEvent::NoteOn(startA, 1, pitch, 100));
  flat.push_back(MidiEvent::NoteOff(endA, 1, pitch, 0));
  flat.push_back(MidiEvent::NoteOn(startB, 1, pitch, 100));
  flat.push_back(MidiEvent::NoteOff(endB, 1, pitch, 0));
  return flat;
}

}  // namespace

void test_baseline_map_includes_all_store_notes_at_select() {
  NoteEditFocus focus;
  const MidiEventVec flat = makeTwoNoteFlat(8, 104, 584, 680, 60);
  rebuildNoteEditFocusFromStore(focus, flat, 1, 768, 0);

  TEST_ASSERT_TRUE(focus.active);
  TEST_ASSERT_EQUAL(2, static_cast<int>(focus.baselineMap.size()));
  TEST_ASSERT_EQUAL_UINT32(8, focus.commitBaseline.startTick);
  TEST_ASSERT_EQUAL_UINT32(104, focus.commitBaseline.endTick);
  TEST_ASSERT_EQUAL_UINT8(60, focus.commitBaseline.pitch);
  TEST_ASSERT_EQUAL_UINT32(8, focus.overlapFootprint.start);
  TEST_ASSERT_EQUAL_UINT32(104, focus.overlapFootprint.end);
  TEST_ASSERT_EQUAL(0, static_cast<int>(focus.overlapNotes.size()));
}

void test_a1_length_updates_footprint_not_commit_baseline() {
  NoteEditFocus focus;
  focus.active = true;
  focus.commitBaseline = {60, 64, 100, 200};
  focus.overlapFootprint = {100, 200};
  focus.last = focus.commitBaseline;

  noteEditFocusApplyLengthEnd(focus, 300);

  TEST_ASSERT_EQUAL_UINT32(200, focus.commitBaseline.endTick);
  TEST_ASSERT_EQUAL_UINT32(300, focus.overlapFootprint.end);
  TEST_ASSERT_EQUAL_UINT32(300, focus.last.endTick);
  TEST_ASSERT_TRUE(noteEditFocusHasPendingLengthChange(focus));
}

void test_a1_no_pending_length_when_footprint_matches_baseline() {
  NoteEditFocus focus;
  focus.active = true;
  focus.commitBaseline = {60, 64, 100, 200};
  focus.overlapFootprint = {100, 200};
  focus.last = focus.commitBaseline;

  TEST_ASSERT_FALSE(noteEditFocusHasPendingLengthChange(focus));
}

void test_inner_note_under_overlap_footprint() {
  NoteEditFocus focus;
  focus.active = true;
  focus.commitBaseline = {60, 64, 496, 1168};
  focus.overlapFootprint = {496, 1168};
  focus.last = focus.commitBaseline;

  TEST_ASSERT_TRUE(isInnerUnderOverlapFootprint(focus, 67, 520, 600, 1536));
  TEST_ASSERT_FALSE(isInnerUnderOverlapFootprint(focus, 67, 403, 496, 1536));
  TEST_ASSERT_FALSE(isInnerUnderOverlapFootprint(focus, 60, 592, 688, 1536));
}

void test_overlap_note_effective_end_shortened_vs_hidden() {
  OverlapNote shortened{};
  shortened.state = OverlapNoteStoreState::Shortened;
  shortened.baseline.endTick = 688;
  shortened.shortenedEndTick = 495;
  TEST_ASSERT_EQUAL_UINT32(495, overlapNoteEffectiveEnd(shortened));

  OverlapNote hidden{};
  hidden.state = OverlapNoteStoreState::Hidden;
  hidden.baseline.endTick = 688;
  TEST_ASSERT_EQUAL_UINT32(688, overlapNoteEffectiveEnd(hidden));
}

void test_shorten_under_49_ticks_classifies_as_hidden_candidate() {
  const uint32_t loopLength = 1536;
  const uint32_t moverStart = 410;
  const uint32_t neighborStart = 400;
  const uint32_t shortenedEnd = moverStart - 1;
  const uint32_t shortenedLength =
      NoteMovementUtils::calculateNoteLength(neighborStart, shortenedEnd, loopLength);
  TEST_ASSERT_TRUE(shortenedLength < 49);
}

int main(int /*argc*/, char** /*argv*/) {
  UNITY_BEGIN();
  RUN_TEST(test_baseline_map_includes_all_store_notes_at_select);
  RUN_TEST(test_a1_length_updates_footprint_not_commit_baseline);
  RUN_TEST(test_a1_no_pending_length_when_footprint_matches_baseline);
  RUN_TEST(test_inner_note_under_overlap_footprint);
  RUN_TEST(test_overlap_note_effective_end_shortened_vs_hidden);
  RUN_TEST(test_shorten_under_49_ticks_classifies_as_hidden_candidate);
  return UNITY_END();
}
