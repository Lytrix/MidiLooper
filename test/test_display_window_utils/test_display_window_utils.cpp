//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include <unity.h>

#include "MidiEvent.h"
#include "Utils/IntervalProjection.h"
#include "VisualCache.h"

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

void test_clamp_preserved_display_note_count_drops_capture_suffix() {
  // session_20260811_013056: 1872 committed + 1011 capture must not preserve as 2883.
  TEST_ASSERT_EQUAL_UINT32(1872u, DisplayWindowUtils::clampPreservedDisplayNoteCount(2883u, 1872u));
  TEST_ASSERT_EQUAL_UINT32(0u, DisplayWindowUtils::clampPreservedDisplayNoteCount(1011u, 0u));
  TEST_ASSERT_EQUAL_UINT32(64u, DisplayWindowUtils::clampPreservedDisplayNoteCount(64u, 128u));
}

void test_preserved_overdub_stop_keeps_capture_suffix() {
  // RC5a: overdub stop must keep capture rows; only trim temporary tails beyond compose base.
  TEST_ASSERT_EQUAL_UINT32(2883u,
                           DisplayWindowUtils::preservedOverdubStopDisplayNoteCount(2883u, 2883u));
  TEST_ASSERT_EQUAL_UINT32(2883u,
                           DisplayWindowUtils::preservedOverdubStopDisplayNoteCount(2900u, 2883u));
  TEST_ASSERT_EQUAL_UINT32(2883u,
                           DisplayWindowUtils::preservedOverdubStopDisplayNoteCount(2883u, 0u));
  TEST_ASSERT_EQUAL_UINT32(64u, DisplayWindowUtils::preservedOverdubStopDisplayNoteCount(64u, 128u));
}

void test_committed_display_visual_cache_authoritative() {
  TEST_ASSERT_TRUE(DisplayWindowUtils::committedDisplayVisualCacheAuthoritative(false, true));
  TEST_ASSERT_FALSE(DisplayWindowUtils::committedDisplayVisualCacheAuthoritative(true, true));
  TEST_ASSERT_FALSE(DisplayWindowUtils::committedDisplayVisualCacheAuthoritative(false, false));
}

void test_prefer_incremental_committed_display_includes_stopped() {
  // RC5f: STOPPED must share PLAYING deferred incremental path (174742).
  TEST_ASSERT_TRUE(DisplayWindowUtils::preferIncrementalCommittedDisplay(true, false));
  TEST_ASSERT_TRUE(DisplayWindowUtils::preferIncrementalCommittedDisplay(false, true));
  TEST_ASSERT_TRUE(DisplayWindowUtils::preferIncrementalCommittedDisplay(true, true));
  TEST_ASSERT_FALSE(DisplayWindowUtils::preferIncrementalCommittedDisplay(false, false));
}

void test_should_preserve_overdub_stop_handoff() {
  // RC-H / session_20260812_174843: keep composed overdub-stop frame while STOPPED.
  TEST_ASSERT_TRUE(DisplayWindowUtils::shouldPreserveOverdubStopHandoff(true, true, true));
  TEST_ASSERT_FALSE(DisplayWindowUtils::shouldPreserveOverdubStopHandoff(true, false, true));
  TEST_ASSERT_FALSE(DisplayWindowUtils::shouldPreserveOverdubStopHandoff(false, true, true));
  TEST_ASSERT_FALSE(DisplayWindowUtils::shouldPreserveOverdubStopHandoff(true, true, false));
}

void test_clamp_non_wrap_display_note_bar_ticks_frontier_overflow() {
  // NoteOn tick can lead growing display length — clamp, do not classify as wrap (end < start).
  uint32_t start = 100;
  uint32_t end = 100;
  TEST_ASSERT_TRUE(DisplayWindowUtils::clampNonWrapDisplayNoteBarTicks(start, end, 99u));
  TEST_ASSERT_EQUAL_UINT32(98u, start);
  TEST_ASSERT_EQUAL_UINT32(98u, end);

  start = 50;
  end = 100;
  TEST_ASSERT_TRUE(DisplayWindowUtils::clampNonWrapDisplayNoteBarTicks(start, end, 99u));
  TEST_ASSERT_EQUAL_UINT32(50u, start);
  TEST_ASSERT_EQUAL_UINT32(98u, end);

  // Exact frontier equality (start/end == length) is also overflow, not wrap.
  start = 99;
  end = 99;
  TEST_ASSERT_TRUE(DisplayWindowUtils::clampNonWrapDisplayNoteBarTicks(start, end, 99u));
  TEST_ASSERT_EQUAL_UINT32(98u, start);
  TEST_ASSERT_EQUAL_UINT32(98u, end);

  // Genuine wrap pair (end < start) is left alone for the wrap draw path.
  start = 90;
  end = 10;
  TEST_ASSERT_FALSE(DisplayWindowUtils::clampNonWrapDisplayNoteBarTicks(start, end, 99u));
  TEST_ASSERT_EQUAL_UINT32(90u, start);
  TEST_ASSERT_EQUAL_UINT32(10u, end);
}

void test_map_display_note_bar_ticks_frontier_equals_length_stays_at_end() {
  // drawAllNotes used (startTick % loopLength) before clamp — start==length → tick 0 blip.
  uint32_t outStart = 0;
  uint32_t outEnd = 0;
  DisplayWindowUtils::mapDisplayNoteBarTicksForLoopPaint(100u, 100u, 0u, 100u, outStart, outEnd);
  TEST_ASSERT_EQUAL_UINT32(99u, outStart);
  TEST_ASSERT_EQUAL_UINT32(99u, outEnd);

  // Playhead-tail shape: start at frontier, end one tick behind — must not become 0..(L-1).
  DisplayWindowUtils::mapDisplayNoteBarTicksForLoopPaint(100u, 99u, 0u, 100u, outStart, outEnd);
  TEST_ASSERT_EQUAL_UINT32(99u, outStart);
  TEST_ASSERT_EQUAL_UINT32(99u, outEnd);
}

void test_filter_display_notes_to_window_clamps_frontier_equals_length() {
  const uint32_t loopLength = 32u * Config::TICKS_PER_BAR;
  const uint32_t windowStart = 16u * Config::TICKS_PER_BAR;
  const uint32_t windowLength = 16u * Config::TICKS_PER_BAR;
  NoteUtils::DisplayNoteVec notes;
  notes.push_back({kInvalidNoteId, 60, 100, loopLength, loopLength});

  const NoteUtils::DisplayNoteVec filtered =
      DisplayWindowUtils::filterDisplayNotesToWindow(notes, windowStart, windowLength, loopLength);
  TEST_ASSERT_EQUAL(1u, filtered.size());
  // Window-relative: last tick of the window, not column 0 (normalizeTick(length)==0).
  TEST_ASSERT_EQUAL_UINT32(windowLength - 1u, filtered[0].startTick);
  TEST_ASSERT_EQUAL_UINT32(windowLength - 1u, filtered[0].endTick);
}

void test_overdub_committed_window_cache_rejects_zero_committed_count() {
  TEST_ASSERT_FALSE(DisplayWindowUtils::overdubCommittedWindowCacheReusable(0u, 300u));
  TEST_ASSERT_TRUE(DisplayWindowUtils::overdubCommittedWindowCacheReusable(256u, 300u));
  TEST_ASSERT_FALSE(DisplayWindowUtils::overdubCommittedWindowCacheReusable(301u, 300u));
}

void test_overdub_committed_promote_to_full_visual_cache() {
  TEST_ASSERT_TRUE(
      DisplayWindowUtils::shouldPromoteOverdubCommittedToFullVisualCache(true, false, true, true));
  TEST_ASSERT_FALSE(
      DisplayWindowUtils::shouldPromoteOverdubCommittedToFullVisualCache(true, true, true, true));
  TEST_ASSERT_FALSE(
      DisplayWindowUtils::shouldPromoteOverdubCommittedToFullVisualCache(true, false, false, true));
  TEST_ASSERT_FALSE(
      DisplayWindowUtils::shouldPromoteOverdubCommittedToFullVisualCache(true, false, true, false));
  TEST_ASSERT_FALSE(
      DisplayWindowUtils::shouldPromoteOverdubCommittedToFullVisualCache(false, false, true, true));
}

void test_paint_window_inside_gather_detects_follow_exit() {
  const uint32_t bar = Config::TICKS_PER_BAR;
  // First gather: bars 0..18 (16 + 2 margin). Follow window at bar 0 fits; at bar 8 does not.
  TEST_ASSERT_TRUE(DisplayWindowUtils::paintWindowInsideGather(0, 16u * bar, 0, 18u * bar));
  TEST_ASSERT_FALSE(
      DisplayWindowUtils::paintWindowInsideGather(8u * bar, 16u * bar, 0, 18u * bar));
  TEST_ASSERT_TRUE(
      DisplayWindowUtils::paintWindowInsideGather(2u * bar, 16u * bar, 0, 18u * bar));
}

void test_visual_cache_covers_window_requires_fully_built() {
  const uint32_t bar = Config::TICKS_PER_BAR;
  const uint32_t loopLength = 195u * bar;
  VisualBarVec dirty(195u, 1);
  for (uint32_t b = 170; b < 195; ++b) {
    dirty[b] = 0;
  }
  // Partial neighborhood clean must not authorize filter paint (sparse-cache gaps).
  TEST_ASSERT_FALSE(visualCacheCoversWindow(true, dirty, 0, 16u * bar, loopLength, bar));
  TEST_ASSERT_FALSE(
      visualCacheCoversWindow(true, dirty, 179u * bar, 16u * bar, loopLength, bar));
  TEST_ASSERT_TRUE(visualCacheCoversWindow(false, VisualBarVec{}, 0, 16u * bar, loopLength, bar));
  TEST_ASSERT_FALSE(
      visualCacheCoversWindow(true, VisualBarVec{}, 0, 16u * bar, loopLength, bar));
}

void test_format_loop_length_bars_info_strip() {
  char lenStr[8];
  const uint32_t bar = Config::TICKS_PER_BAR;

  DisplayWindowUtils::formatLoopLengthBars(lenStr, sizeof(lenStr), 16u * bar, bar);
  TEST_ASSERT_EQUAL_STRING(" 16", lenStr);

  DisplayWindowUtils::formatLoopLengthBars(lenStr, sizeof(lenStr), 99u * bar, bar);
  TEST_ASSERT_EQUAL_STRING(" 99", lenStr);

  DisplayWindowUtils::formatLoopLengthBars(lenStr, sizeof(lenStr), 133u * bar, bar);
  TEST_ASSERT_EQUAL_STRING("133", lenStr);

  DisplayWindowUtils::formatLoopLengthBars(lenStr, sizeof(lenStr), 0, bar);
  TEST_ASSERT_EQUAL_STRING(" --", lenStr);
}

void test_adopt_partial_visual_cache_marks_uncovered_bars_dirty() {
  const uint32_t bar = Config::TICKS_PER_BAR;
  const uint32_t loopLength = 84u * bar;
  VisualCache cache;
  bool visualCacheDirty = false;

  NoteUtils::DisplayNote note{};
  note.startTick = 20u * bar;
  note.endTick = 35u * bar + (bar - 1);
  std::vector<NoteUtils::DisplayNote> adopted = {note};

  adoptPartialVisualCacheNotes(cache, visualCacheDirty, adopted, loopLength, bar);

  TEST_ASSERT_EQUAL_UINT32(1u, cache.notes.size());
  TEST_ASSERT_EQUAL_UINT32(84u, cache.dirtyBars.size());
  TEST_ASSERT_TRUE(visualCacheDirty);
  for (uint32_t b = 0; b < 20; ++b) {
    TEST_ASSERT_EQUAL_UINT8(1, cache.dirtyBars[b]);
  }
  for (uint32_t b = 20; b <= 35; ++b) {
    TEST_ASSERT_EQUAL_UINT8(0, cache.dirtyBars[b]);
  }
  for (uint32_t b = 36; b < 84; ++b) {
    TEST_ASSERT_EQUAL_UINT8(1, cache.dirtyBars[b]);
  }
}

void test_adopt_partial_visual_cache_clears_dirty_when_loop_fits_window() {
  const uint32_t bar = Config::TICKS_PER_BAR;
  const uint32_t loopLength = 8u * bar;
  VisualCache cache;
  bool visualCacheDirty = true;

  NoteUtils::DisplayNote note{};
  note.startTick = 0;
  note.endTick = loopLength - 1;
  std::vector<NoteUtils::DisplayNote> adopted = {note};

  adoptPartialVisualCacheNotes(cache, visualCacheDirty, adopted, loopLength, bar);

  TEST_ASSERT_FALSE(visualCacheDirty);
  TEST_ASSERT_EQUAL_UINT32(8u, cache.dirtyBars.size());
  for (uint8_t flag : cache.dirtyBars) {
    TEST_ASSERT_EQUAL_UINT8(0, flag);
  }
}

void test_overview_band_mask_marks_every_bar_the_note_spans() {
  const uint32_t bar = Config::TICKS_PER_BAR;
  std::vector<uint8_t> mask(8, 0);

  NoteUtils::DisplayNote note{};
  note.startTick = 2u * bar;
  note.endTick = 4u * bar + 10u;
  note.note = 60;  // band 3

  DisplayWindowUtils::accumulateOverviewBandMask(mask, note, bar);

  const uint8_t expected = static_cast<uint8_t>(1u << 3);
  TEST_ASSERT_EQUAL_UINT8(0, mask[1]);
  TEST_ASSERT_EQUAL_UINT8(expected, mask[2]);
  TEST_ASSERT_EQUAL_UINT8(expected, mask[3]);
  TEST_ASSERT_EQUAL_UINT8(expected, mask[4]);
  TEST_ASSERT_EQUAL_UINT8(0, mask[5]);
}

void test_overview_band_mask_accumulates_bands_and_clamps_range() {
  const uint32_t bar = Config::TICKS_PER_BAR;
  std::vector<uint8_t> mask(4, 0);

  NoteUtils::DisplayNote low{};
  low.startTick = 0;
  low.endTick = bar - 1;
  low.note = 5;  // band 0

  // Runs past the mask end: must clamp, not write out of range.
  NoteUtils::DisplayNote high{};
  high.startTick = 0;
  high.endTick = 40u * bar;
  high.note = 127;  // band 7

  DisplayWindowUtils::accumulateOverviewBandMask(mask, low, bar);
  DisplayWindowUtils::accumulateOverviewBandMask(mask, high, bar);

  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(0x01 | 0x80), mask[0]);
  TEST_ASSERT_EQUAL_UINT8(0x80, mask[3]);
  TEST_ASSERT_EQUAL_UINT32(4u, mask.size());

  // A note starting beyond the mask is ignored entirely.
  NoteUtils::DisplayNote beyond{};
  beyond.startTick = 10u * bar;
  beyond.endTick = 11u * bar;
  beyond.note = 20;
  DisplayWindowUtils::accumulateOverviewBandMask(mask, beyond, bar);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(0x01 | 0x80), mask[0]);
}

void test_overview_band_for_note_covers_full_midi_range() {
  TEST_ASSERT_EQUAL_UINT8(0, DisplayWindowUtils::overviewBandForNote(0));
  TEST_ASSERT_EQUAL_UINT8(3, DisplayWindowUtils::overviewBandForNote(60));
  TEST_ASSERT_EQUAL_UINT8(7, DisplayWindowUtils::overviewBandForNote(127));
}

void test_paint_window_inside_gather_tolerates_margin_slide() {
  const uint32_t bar = Config::TICKS_PER_BAR;
  // RC-F: gather is the paint window widened by kWindowedGatherMarginBars on each side, so
  // auto-follow may advance within the margin before the committed layer must be rebuilt.
  const uint32_t gatherStart = 10u * bar;
  const uint32_t gatherLength = 20u * bar;  // 16-bar window + 2 bars either side

  TEST_ASSERT_TRUE(
      DisplayWindowUtils::paintWindowInsideGather(12u * bar, 16u * bar, gatherStart, gatherLength));
  TEST_ASSERT_TRUE(
      DisplayWindowUtils::paintWindowInsideGather(14u * bar, 16u * bar, gatherStart, gatherLength));
  // One bar past the margin leaves the gather and must force a rebuild.
  TEST_ASSERT_FALSE(
      DisplayWindowUtils::paintWindowInsideGather(15u * bar, 16u * bar, gatherStart, gatherLength));
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
  RUN_TEST(test_clamp_preserved_display_note_count_drops_capture_suffix);
  RUN_TEST(test_preserved_overdub_stop_keeps_capture_suffix);
  RUN_TEST(test_committed_display_visual_cache_authoritative);
  RUN_TEST(test_prefer_incremental_committed_display_includes_stopped);
  RUN_TEST(test_should_preserve_overdub_stop_handoff);
  RUN_TEST(test_clamp_non_wrap_display_note_bar_ticks_frontier_overflow);
  RUN_TEST(test_map_display_note_bar_ticks_frontier_equals_length_stays_at_end);
  RUN_TEST(test_filter_display_notes_to_window_clamps_frontier_equals_length);
  RUN_TEST(test_overdub_committed_window_cache_rejects_zero_committed_count);
  RUN_TEST(test_overdub_committed_promote_to_full_visual_cache);
  RUN_TEST(test_paint_window_inside_gather_detects_follow_exit);
  RUN_TEST(test_visual_cache_covers_window_requires_fully_built);
  RUN_TEST(test_adopt_partial_visual_cache_marks_uncovered_bars_dirty);
  RUN_TEST(test_adopt_partial_visual_cache_clears_dirty_when_loop_fits_window);
  RUN_TEST(test_overview_band_mask_marks_every_bar_the_note_spans);
  RUN_TEST(test_overview_band_mask_accumulates_bands_and_clamps_range);
  RUN_TEST(test_overview_band_for_note_covers_full_midi_range);
  RUN_TEST(test_paint_window_inside_gather_tolerates_margin_slide);
  RUN_TEST(test_format_loop_length_bars_info_strip);
  return UNITY_END();
}
