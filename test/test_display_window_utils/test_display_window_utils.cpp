//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include <unity.h>

#include "MidiEvent.h"
#include "Utils/IntervalProjection.h"

#include "../../src/Logger.cpp"
#include "../../src/Utils/IntervalProjection.cpp"
#include "../../src/Utils/NoteUtils.cpp"
#include "../../src/Utils/DisplayWindowUtils.cpp"

void test_make_viewport_interval() {
  const TickInterval viewport = DisplayWindowUtils::makeViewportInterval(16u * Config::TICKS_PER_BAR,
                                                                         4u * Config::TICKS_PER_BAR);
  TEST_ASSERT_EQUAL_INT32(static_cast<int32_t>(16u * Config::TICKS_PER_BAR), viewport.start);
  TEST_ASSERT_EQUAL_INT32(static_cast<int32_t>(20u * Config::TICKS_PER_BAR), viewport.end);
}

void test_note_intersects_window_tick_interval() {
  const uint32_t loopLength = 16u * Config::TICKS_PER_BAR;
  const TickInterval viewport = DisplayWindowUtils::makeViewportInterval(4u * Config::TICKS_PER_BAR,
                                                                         4u * Config::TICKS_PER_BAR);
  const TickInterval inside{static_cast<int32_t>(5u * Config::TICKS_PER_BAR),
                            static_cast<int32_t>(5u * Config::TICKS_PER_BAR + 48)};
  const TickInterval outside{0, 48};
  TEST_ASSERT_TRUE(DisplayWindowUtils::noteIntersectsWindow(inside, viewport, loopLength));
  TEST_ASSERT_FALSE(DisplayWindowUtils::noteIntersectsWindow(outside, viewport, loopLength));
}

void test_filter_display_notes_by_window_inclusion_tick_interval() {
  const uint32_t loopLength = 32u * Config::TICKS_PER_BAR;
  const TickInterval viewport = DisplayWindowUtils::makeViewportInterval(16u * Config::TICKS_PER_BAR,
                                                                         16u * Config::TICKS_PER_BAR);
  NoteUtils::DisplayNoteVec notes;
  notes.push_back({kInvalidNoteId, 60, 100, 17u * Config::TICKS_PER_BAR,
                   17u * Config::TICKS_PER_BAR + 48});
  notes.push_back({kInvalidNoteId, 72, 100, 2u * Config::TICKS_PER_BAR,
                   2u * Config::TICKS_PER_BAR + 48});
  const NoteUtils::DisplayNoteVec filtered =
      DisplayWindowUtils::filterDisplayNotesByWindowInclusion(notes, viewport, loopLength);
  TEST_ASSERT_EQUAL(1u, filtered.size());
  TEST_ASSERT_EQUAL_UINT8(60, filtered[0].note);
}

void test_head_tail_note_viewport_is_distinct_from_split() {
  constexpr uint32_t loopLength = 1536;
  CanonicalNoteSpanVec spans;
  CanonicalNoteSpan span;
  span.noteId = 1;
  span.pitch = 60;
  span.velocity = 100;
  span.interval = TickInterval{1400, 1587};
  span.splitHeadTail = true;
  spans.push_back(span);

  const TickInterval fullWindow = IntervalProjection::makeFullLoopDisplayWindow(loopLength);
  const ProjectionContext context =
      IntervalProjection::buildDisplayProjectionContext(loopLength, fullWindow);
  const NoteUtils::DisplayNoteVec projected =
      IntervalProjection::projectDisplayNotes(spans, context);
  TEST_ASSERT_EQUAL(2u, projected.size());

  const TickInterval tailOnlyViewport = DisplayWindowUtils::makeViewportInterval(1400, 100);
  const NoteUtils::DisplayNoteVec tailFiltered =
      DisplayWindowUtils::filterDisplayNotesByWindowInclusion(projected, tailOnlyViewport,
                                                              loopLength);
  TEST_ASSERT_EQUAL(1u, tailFiltered.size());
  TEST_ASSERT_EQUAL_UINT32(1400u, tailFiltered[0].startTick);
}

void test_choose_bars_per_segment_table() {
  TEST_ASSERT_EQUAL_UINT32(1u, DisplayWindowUtils::chooseBarsPerSegment(16, 16));
  TEST_ASSERT_EQUAL_UINT32(2u, DisplayWindowUtils::chooseBarsPerSegment(32, 16));
  TEST_ASSERT_EQUAL_UINT32(4u, DisplayWindowUtils::chooseBarsPerSegment(64, 16));
  TEST_ASSERT_EQUAL_UINT32(8u, DisplayWindowUtils::chooseBarsPerSegment(64, 8));
}

void test_note_intersects_window_inside_and_outside() {
  const uint32_t loopLength = 16u * Config::TICKS_PER_BAR;
  const uint32_t windowStart = 4u * Config::TICKS_PER_BAR;
  const uint32_t windowLength = 4u * Config::TICKS_PER_BAR;
  TEST_ASSERT_TRUE(DisplayWindowUtils::noteIntersectsWindow(
      5u * Config::TICKS_PER_BAR, 5u * Config::TICKS_PER_BAR + 48, windowStart, windowLength,
      loopLength));
  TEST_ASSERT_FALSE(DisplayWindowUtils::noteIntersectsWindow(
      0, 48, windowStart, windowLength, loopLength));
}

void test_filter_display_notes_to_window() {
  const uint32_t loopLength = 32u * Config::TICKS_PER_BAR;
  const uint32_t windowStart = 16u * Config::TICKS_PER_BAR;
  const uint32_t windowLength = 16u * Config::TICKS_PER_BAR;
  NoteUtils::DisplayNoteVec notes;
  notes.push_back({kInvalidNoteId, 60, 100, 17u * Config::TICKS_PER_BAR, 17u * Config::TICKS_PER_BAR + 48});
  notes.push_back({kInvalidNoteId, 72, 100, 2u * Config::TICKS_PER_BAR, 2u * Config::TICKS_PER_BAR + 48});

  const NoteUtils::DisplayNoteVec filtered =
      DisplayWindowUtils::filterDisplayNotesToWindow(notes, windowStart, windowLength,
                                                     loopLength);
  TEST_ASSERT_EQUAL(1u, filtered.size());
  TEST_ASSERT_EQUAL_UINT8(60, filtered[0].note);
  TEST_ASSERT_EQUAL(1u * Config::TICKS_PER_BAR, filtered[0].startTick);
}

void test_resolve_centered_window_start() {
  const uint32_t bar = Config::TICKS_PER_BAR;
  const uint32_t loopLength = 32u * bar;
  const uint32_t windowLength = 16u * bar;
  TEST_ASSERT_EQUAL_UINT32(0u, DisplayWindowUtils::resolveCenteredWindowStart(0, windowLength, loopLength));
  TEST_ASSERT_EQUAL_UINT32(8u * bar,
                           DisplayWindowUtils::resolveCenteredWindowStart(16u * bar, windowLength, loopLength));
  TEST_ASSERT_EQUAL_UINT32(16u * bar,
                           DisplayWindowUtils::resolveCenteredWindowStart(31u * bar, windowLength, loopLength));
}

int main(int /*argc*/, char** /*argv*/) {
  UNITY_BEGIN();
  RUN_TEST(test_make_viewport_interval);
  RUN_TEST(test_note_intersects_window_tick_interval);
  RUN_TEST(test_filter_display_notes_by_window_inclusion_tick_interval);
  RUN_TEST(test_head_tail_note_viewport_is_distinct_from_split);
  RUN_TEST(test_choose_bars_per_segment_table);
  RUN_TEST(test_note_intersects_window_inside_and_outside);
  RUN_TEST(test_filter_display_notes_to_window);
  RUN_TEST(test_resolve_centered_window_start);
  return UNITY_END();
}
