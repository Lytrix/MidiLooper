//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include <unity.h>

#include "MidiEvent.h"
#include "Utils/DisplayWindowUtils.h"
#include "Utils/IntervalProjection.h"
#include "Utils/NoteUtils.h"
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

void test_capture_playhead_tails_are_paint_only() {
  // 225243: D#2 ON@624 lengthened in-place to stop phase 704, then RC5a froze it.
  TEST_ASSERT_TRUE(DisplayWindowUtils::capturePlayheadTailsArePaintOnly());
}

void test_overdub_playhead_tails_apply_when_over_budget() {
  TEST_ASSERT_TRUE(DisplayWindowUtils::shouldApplyLiveCapturePlayheadTails(
      false, true, true, true, true));
  TEST_ASSERT_FALSE(DisplayWindowUtils::shouldApplyLiveCapturePlayheadTails(
      false, true, true, true, false));
  TEST_ASSERT_FALSE(DisplayWindowUtils::shouldApplyLiveCapturePlayheadTails(
      true, false, true, true, true));
  TEST_ASSERT_TRUE(DisplayWindowUtils::shouldApplyLiveCapturePlayheadTails(
      true, false, false, false, true));
}

void test_should_allow_capture_playhead_wrap_continuation_stage2_truth_table() {
  // Pinned table (overdub_live_wrap_playhead_tail_display_bugfix) after Stage 2.
  // RECORDING, not playing
  TEST_ASSERT_FALSE(DisplayWindowUtils::shouldAllowCapturePlayheadWrapContinuation(
      true, false, false));
  // RECORDING + PLAYING
  TEST_ASSERT_FALSE(DisplayWindowUtils::shouldAllowCapturePlayheadWrapContinuation(
      true, true, false));
  // OVERDUBBING + PLAYING → YES
  TEST_ASSERT_TRUE(DisplayWindowUtils::shouldAllowCapturePlayheadWrapContinuation(
      false, true, true));
  // OVERDUBBING without playing arg stays NO
  TEST_ASSERT_FALSE(DisplayWindowUtils::shouldAllowCapturePlayheadWrapContinuation(
      false, false, true));
  // stopped / non-capture
  TEST_ASSERT_FALSE(DisplayWindowUtils::shouldAllowCapturePlayheadWrapContinuation(
      false, true, false));
  TEST_ASSERT_FALSE(DisplayWindowUtils::shouldAllowCapturePlayheadWrapContinuation(
      false, false, false));
}

void test_should_complete_stopped_draw_source_wrap_held_stage3b_truth_table() {
  // STOPPED + capture idle + current snapshot → YES
  TEST_ASSERT_TRUE(DisplayWindowUtils::shouldCompleteStoppedDrawSourceWrapHeld(
      true, false, true));
  // PLAYING
  TEST_ASSERT_FALSE(DisplayWindowUtils::shouldCompleteStoppedDrawSourceWrapHeld(
      false, false, true));
  // STOPPED but capture still active
  TEST_ASSERT_FALSE(DisplayWindowUtils::shouldCompleteStoppedDrawSourceWrapHeld(
      true, true, true));
  // STOPPED idle without current snapshot
  TEST_ASSERT_FALSE(DisplayWindowUtils::shouldCompleteStoppedDrawSourceWrapHeld(
      true, false, false));
}

void test_prefer_preserved_overdub_stop_handoff_while_cache_dirty() {
  // session_20260820_000553: first PLAYING after overdub stop must keep the composed
  // frame while visualCache is dirty (655 → 583 flash when cache notes were used).
  TEST_ASSERT_TRUE(DisplayWindowUtils::preferPreservedOverdubStopHandoff(true, true, true));
  TEST_ASSERT_FALSE(DisplayWindowUtils::preferPreservedOverdubStopHandoff(true, true, false));
  TEST_ASSERT_FALSE(DisplayWindowUtils::preferPreservedOverdubStopHandoff(true, false, true));
  TEST_ASSERT_FALSE(DisplayWindowUtils::preferPreservedOverdubStopHandoff(false, true, true));
}

void test_resolve_note_edit_committed_base_from_snapshot() {
  TEST_ASSERT_TRUE(DisplayWindowUtils::shouldResolveNoteEditCommittedBaseFromSnapshot(true, true));
  TEST_ASSERT_FALSE(DisplayWindowUtils::shouldResolveNoteEditCommittedBaseFromSnapshot(true, false));
  TEST_ASSERT_FALSE(DisplayWindowUtils::shouldResolveNoteEditCommittedBaseFromSnapshot(false, true));
  TEST_ASSERT_FALSE(DisplayWindowUtils::shouldResolveNoteEditCommittedBaseFromSnapshot(false, false));
}

void test_select_note_edit_projection_cache_reuse_fingerprint_does_not_regather() {
  using Reuse = DisplayWindowUtils::NoteEditProjectionCacheReuse;
  TEST_ASSERT_EQUAL(static_cast<int>(Reuse::FullHit),
                    static_cast<int>(DisplayWindowUtils::selectNoteEditProjectionCacheReuse(
                        true, true, true, true)));
  TEST_ASSERT_EQUAL(static_cast<int>(Reuse::ReprojectFromCommittedBase),
                    static_cast<int>(DisplayWindowUtils::selectNoteEditProjectionCacheReuse(
                        true, true, true, false)));
  TEST_ASSERT_EQUAL(static_cast<int>(Reuse::ReprojectFromCommittedBase),
                    static_cast<int>(DisplayWindowUtils::selectNoteEditProjectionCacheReuse(
                        true, true, false, false)));
  TEST_ASSERT_EQUAL(static_cast<int>(Reuse::RebuildCommittedBase),
                    static_cast<int>(DisplayWindowUtils::selectNoteEditProjectionCacheReuse(
                        true, false, true, true)));
  TEST_ASSERT_EQUAL(static_cast<int>(Reuse::RebuildCommittedBase),
                    static_cast<int>(DisplayWindowUtils::selectNoteEditProjectionCacheReuse(
                        false, true, true, true)));
}

void test_preview_revision_is_paint_key_not_committed_base_key() {
  // RC1 / compose 124645: same loopLength/playbackRevision/visualRevision with a
  // changed sessionPreviewRevision must ReprojectFromCommittedBase — not
  // RebuildCommittedBase / resolveDisplayNotes.
  const void* identityA = reinterpret_cast<const void*>(0xA);
  TEST_ASSERT_TRUE(DisplayWindowUtils::noteEditCommittedBaseKeysMatch(2304, 2304, 3, 3, 9, 9,
                                                                      identityA, identityA));
  TEST_ASSERT_FALSE(
      DisplayWindowUtils::noteEditPaintKeysMatch(11, 11, 0, 0, 1, 1, /*preview*/ 2, /*cached*/ 1));

  const bool committedBaseKeysMatch =
      DisplayWindowUtils::noteEditCommittedBaseKeysMatch(2304, 2304, 3, 3, 9, 9, identityA,
                                                         identityA);
  const bool paintKeysMatch =
      DisplayWindowUtils::noteEditPaintKeysMatch(11, 11, 0, 0, 1, 1, 2, 1);
  TEST_ASSERT_EQUAL(
      static_cast<int>(DisplayWindowUtils::NoteEditProjectionCacheReuse::ReprojectFromCommittedBase),
      static_cast<int>(DisplayWindowUtils::selectNoteEditProjectionCacheReuse(
          true, committedBaseKeysMatch, true, paintKeysMatch)));

  TEST_ASSERT_FALSE(DisplayWindowUtils::noteEditCommittedBaseKeysMatch(
      2304, 2304, 3, 3, /*visual*/ 10, 9, identityA, identityA));
  TEST_ASSERT_EQUAL(
      static_cast<int>(DisplayWindowUtils::NoteEditProjectionCacheReuse::RebuildCommittedBase),
      static_cast<int>(DisplayWindowUtils::selectNoteEditProjectionCacheReuse(
          true, false, true, true)));
}

void test_rebuild_note_edit_committed_base_on_snapshot_identity_same_revision() {
  // OpenSpec 4.6 Stage 4: same loopLength/playback/visual revision with replaced
  // published root identity must RebuildCommittedBase.
  const void* identityA = reinterpret_cast<const void*>(0xA);
  const void* identityB = reinterpret_cast<const void*>(0xB);
  TEST_ASSERT_TRUE(DisplayWindowUtils::noteEditCommittedBaseKeysMatch(2304, 2304, 3, 3, 9, 9,
                                                                      identityA, identityA));
  TEST_ASSERT_FALSE(DisplayWindowUtils::noteEditCommittedBaseKeysMatch(2304, 2304, 3, 3, 9, 9,
                                                                       identityB, identityA));
  TEST_ASSERT_TRUE(DisplayWindowUtils::noteEditCommittedBaseKeysMatch(2304, 2304, 3, 3, 9, 9,
                                                                      nullptr, nullptr));
  TEST_ASSERT_FALSE(DisplayWindowUtils::noteEditCommittedBaseKeysMatch(2304, 2304, 3, 3, 9, 9,
                                                                       identityA, nullptr));
  TEST_ASSERT_EQUAL(
      static_cast<int>(DisplayWindowUtils::NoteEditProjectionCacheReuse::RebuildCommittedBase),
      static_cast<int>(DisplayWindowUtils::selectNoteEditProjectionCacheReuse(
          true,
          DisplayWindowUtils::noteEditCommittedBaseKeysMatch(2304, 2304, 3, 3, 9, 9, identityB,
                                                             identityA),
          true, true)));
  TEST_ASSERT_EQUAL(
      static_cast<int>(DisplayWindowUtils::NoteEditProjectionCacheReuse::FullHit),
      static_cast<int>(DisplayWindowUtils::selectNoteEditProjectionCacheReuse(
          true,
          DisplayWindowUtils::noteEditCommittedBaseKeysMatch(2304, 2304, 3, 3, 9, 9, identityA,
                                                             identityA),
          true, true)));
}

void test_select_note_edit_committed_base_kind_capture_vs_playing() {
  // RC-A: selector result space is Snapshot | VisualCache only. dirty∧¬current
  // must not PassMaterialize (stale VisualCache is a legal request state).
  using Kind = DisplayWindowUtils::NoteEditCommittedBaseKind;
  TEST_ASSERT_EQUAL(static_cast<int>(Kind::Snapshot),
                    static_cast<int>(DisplayWindowUtils::selectNoteEditCommittedBaseKind(
                        true, true, true)));
  TEST_ASSERT_EQUAL(static_cast<int>(Kind::VisualCache),
                    static_cast<int>(DisplayWindowUtils::selectNoteEditCommittedBaseKind(
                        true, false, true)));
  TEST_ASSERT_EQUAL(static_cast<int>(Kind::Snapshot),
                    static_cast<int>(DisplayWindowUtils::selectNoteEditCommittedBaseKind(
                        false, true, true)));
  TEST_ASSERT_EQUAL(static_cast<int>(Kind::VisualCache),
                    static_cast<int>(DisplayWindowUtils::selectNoteEditCommittedBaseKind(
                        false, true, false)));
  TEST_ASSERT_EQUAL(static_cast<int>(Kind::VisualCache),
                    static_cast<int>(DisplayWindowUtils::selectNoteEditCommittedBaseKind(
                        true, true, false)));
  TEST_ASSERT_EQUAL(static_cast<int>(Kind::VisualCache),
                    static_cast<int>(DisplayWindowUtils::selectNoteEditCommittedBaseKind(
                        false, false, true)));
  TEST_ASSERT_EQUAL(
      static_cast<int>(DisplayWindowUtils::selectNoteEditCommittedBaseKind(false, true, true)),
      static_cast<int>(DisplayWindowUtils::selectNoteEditCommittedBaseKind(true, true, true)));
}

void test_hold_overdub_stop_handoff_until_current_snapshot() {
  TEST_ASSERT_FALSE(DisplayWindowUtils::shouldHoldOverdubStopHandoffUntilCurrentSnapshot(
      true, true, true, true, true));
  TEST_ASSERT_TRUE(DisplayWindowUtils::shouldHoldOverdubStopHandoffUntilCurrentSnapshot(
      true, true, false, false, true));
  TEST_ASSERT_TRUE(DisplayWindowUtils::shouldHoldOverdubStopHandoffUntilCurrentSnapshot(
      true, true, true, false, false));
  TEST_ASSERT_FALSE(DisplayWindowUtils::shouldHoldOverdubStopHandoffUntilCurrentSnapshot(
      true, false, false, false, true));
  TEST_ASSERT_FALSE(DisplayWindowUtils::shouldHoldOverdubStopHandoffUntilCurrentSnapshot(
      true, true, false, false, false));
}

void test_preserve_composed_notes_on_overdub_wrap_invalidate() {
  // session_20260826_011829: wrap invalidate without preserve left no frame for
  // ODUB,stop,display (155 ms gather). RC-W1 still resets bookkeeping.
  TEST_ASSERT_TRUE(DisplayWindowUtils::preserveComposedNotesOnOverdubWrapInvalidate());
}

void test_should_hold_composed_notes_on_overdub_wrap_revision() {
  // 231953 wrap 1: 109→65 when resize(committedCount) dropped sealed capture.
  TEST_ASSERT_TRUE(DisplayWindowUtils::shouldHoldComposedNotesOnOverdubWrapRevision(
      true, true, true, false));
  TEST_ASSERT_FALSE(DisplayWindowUtils::shouldHoldComposedNotesOnOverdubWrapRevision(
      false, true, true, false));
  TEST_ASSERT_FALSE(DisplayWindowUtils::shouldHoldComposedNotesOnOverdubWrapRevision(
      true, false, true, false));
  TEST_ASSERT_FALSE(DisplayWindowUtils::shouldHoldComposedNotesOnOverdubWrapRevision(
      true, true, false, false));
  TEST_ASSERT_FALSE(DisplayWindowUtils::shouldHoldComposedNotesOnOverdubWrapRevision(
      true, true, true, true));
}

void test_committed_display_visual_cache_authoritative() {
  TEST_ASSERT_TRUE(DisplayWindowUtils::committedDisplayVisualCacheAuthoritative(false, true));
  // Dirty + notes present: paint must consume stale/handoff, not ensureVisualCacheBuilt.
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

void test_should_hold_overdub_committed_handoff_requires_gather_coverage() {
  // 001907: hold the first 16-bar PLAYING slice only while follow stays inside it.
  TEST_ASSERT_TRUE(DisplayWindowUtils::shouldHoldOverdubCommittedHandoff(true, false, false, false));
  TEST_ASSERT_TRUE(DisplayWindowUtils::shouldHoldOverdubCommittedHandoff(true, true, true, true));
  TEST_ASSERT_FALSE(DisplayWindowUtils::shouldHoldOverdubCommittedHandoff(true, true, true, false));
  TEST_ASSERT_FALSE(DisplayWindowUtils::shouldHoldOverdubCommittedHandoff(true, true, false, true));
  TEST_ASSERT_FALSE(DisplayWindowUtils::shouldHoldOverdubCommittedHandoff(false, true, true, true));
}

void test_should_hold_dirty_overdub_layer_before_window_gather() {
  TEST_ASSERT_TRUE(
      DisplayWindowUtils::shouldHoldDirtyOverdubLayerBeforeWindowGather(true, true, false));
  TEST_ASSERT_FALSE(
      DisplayWindowUtils::shouldHoldDirtyOverdubLayerBeforeWindowGather(false, true, false));
  TEST_ASSERT_FALSE(
      DisplayWindowUtils::shouldHoldDirtyOverdubLayerBeforeWindowGather(true, false, false));
  TEST_ASSERT_FALSE(
      DisplayWindowUtils::shouldHoldDirtyOverdubLayerBeforeWindowGather(true, true, true));
}

void test_should_hold_dirty_visual_cache_until_idle() {
  TEST_ASSERT_TRUE(DisplayWindowUtils::shouldHoldDirtyVisualCacheUntilIdle(true, true, true, false));
  TEST_ASSERT_FALSE(
      DisplayWindowUtils::shouldHoldDirtyVisualCacheUntilIdle(true, true, true, true));
  TEST_ASSERT_FALSE(
      DisplayWindowUtils::shouldHoldDirtyVisualCacheUntilIdle(false, true, true, false));
  TEST_ASSERT_FALSE(
      DisplayWindowUtils::shouldHoldDirtyVisualCacheUntilIdle(true, false, true, false));
  TEST_ASSERT_FALSE(
      DisplayWindowUtils::shouldHoldDirtyVisualCacheUntilIdle(true, true, false, false));
}

void test_should_assign_grown_visual_cache_over_overdub_hold() {
  // 201556: visualCache 206 vs held 126 after wrap. Equal or smaller stays a hold.
  TEST_ASSERT_TRUE(DisplayWindowUtils::shouldAssignGrownVisualCacheOverOverdubHold(206u, 126u));
  TEST_ASSERT_FALSE(DisplayWindowUtils::shouldAssignGrownVisualCacheOverOverdubHold(126u, 126u));
  TEST_ASSERT_FALSE(DisplayWindowUtils::shouldAssignGrownVisualCacheOverOverdubHold(0u, 126u));
  // 232910 wrap 1: cache 89 > prefix 63 must not beat compose 104.
  TEST_ASSERT_FALSE(
      DisplayWindowUtils::shouldAssignGrownVisualCacheOverOverdubHold(89u, 63u, 104u));
  TEST_ASSERT_TRUE(
      DisplayWindowUtils::shouldAssignGrownVisualCacheOverOverdubHold(206u, 126u, 134u));
}

void test_snapshot_backed_display_ready() {
  TEST_ASSERT_TRUE(DisplayWindowUtils::snapshotBackedDisplayReady(false, false));
  TEST_ASSERT_TRUE(DisplayWindowUtils::snapshotBackedDisplayReady(false, true));
  TEST_ASSERT_FALSE(DisplayWindowUtils::snapshotBackedDisplayReady(true, false));
  TEST_ASSERT_TRUE(DisplayWindowUtils::snapshotBackedDisplayReady(true, true));
}

void test_should_gather_published_snapshot_for_draw_source() {
  auto gather = [](bool hasSnapshot, bool capture, bool session, bool nonempty, bool dirty,
                   bool current) {
    return DisplayWindowUtils::shouldGatherPublishedSnapshotForDrawSource(
        hasSnapshot, capture, session, nonempty, dirty, current);
  };

  // No usable snapshot: never manufacture one.
  TEST_ASSERT_FALSE(gather(false, true, true, true, false, false));
  TEST_ASSERT_FALSE(gather(false, true, true, false, true, false));
  TEST_ASSERT_FALSE(gather(false, false, false, true, false, false));

  // Active + current snapshot → snapshot.
  TEST_ASSERT_TRUE(gather(true, true, false, true, false, true));
  TEST_ASSERT_TRUE(gather(true, false, true, true, false, true));

  // Active + stale + dirty nonempty → last snapshot (004351 wrap).
  TEST_ASSERT_TRUE(gather(true, true, false, true, true, false));
  TEST_ASSERT_TRUE(gather(true, false, true, true, true, false));

  // Active + stale + empty → last snapshot.
  TEST_ASSERT_TRUE(gather(true, true, false, false, false, false));

  // Active + stale + clean nonempty → visualCache (092344 OD2 after adopt).
  TEST_ASSERT_FALSE(gather(true, true, false, true, false, false));
  TEST_ASSERT_FALSE(gather(true, false, true, true, false, false));

  // After stop: clean nonempty visualCache is paint authority (010217).
  TEST_ASSERT_FALSE(gather(true, false, false, true, false, true));
  TEST_ASSERT_FALSE(gather(true, false, false, true, false, false));
  // Closed + current + dirty nonempty → snapshot, not the idle prefix (151552).
  TEST_ASSERT_TRUE(gather(true, false, false, true, true, true));
  // Closed + current + empty → snapshot.
  TEST_ASSERT_TRUE(gather(true, false, false, false, true, true));
  TEST_ASSERT_TRUE(gather(true, false, false, false, false, true));
  // Closed + stale → never snapshot (092344). Empty/dirty uses the handoff seed.
  TEST_ASSERT_FALSE(gather(true, false, false, true, true, false));
  TEST_ASSERT_FALSE(gather(true, false, false, false, false, false));
}

void test_visual_cache_reusable_as_committed_draw_source() {
  TEST_ASSERT_TRUE(DisplayWindowUtils::visualCacheReusableAsCommittedDrawSource(
      false, true, true, false));
  TEST_ASSERT_FALSE(DisplayWindowUtils::visualCacheReusableAsCommittedDrawSource(
      true, true, true, false));
  TEST_ASSERT_FALSE(DisplayWindowUtils::visualCacheReusableAsCommittedDrawSource(
      false, false, true, false));
  TEST_ASSERT_FALSE(DisplayWindowUtils::visualCacheReusableAsCommittedDrawSource(
      false, true, false, false));
  TEST_ASSERT_FALSE(DisplayWindowUtils::visualCacheReusableAsCommittedDrawSource(
      false, true, true, true));
}

void test_published_snapshot_is_complete_draw_source() {
  TEST_ASSERT_TRUE(DisplayWindowUtils::publishedSnapshotIsCompleteDrawSource(true, true, 58u, 58u));
  TEST_ASSERT_TRUE(DisplayWindowUtils::publishedSnapshotIsCompleteDrawSource(true, true, 123u, 58u));
  TEST_ASSERT_FALSE(DisplayWindowUtils::publishedSnapshotIsCompleteDrawSource(true, true, 58u, 123u));
  TEST_ASSERT_FALSE(DisplayWindowUtils::publishedSnapshotIsCompleteDrawSource(true, false, 123u, 58u));
  TEST_ASSERT_FALSE(DisplayWindowUtils::publishedSnapshotIsCompleteDrawSource(false, true, 123u, 58u));
}

void test_should_gather_published_snapshot_for_complete_draw_source() {
  auto gather = [](bool hasSnapshot, bool capture, bool session, bool current, bool reusable,
                   bool complete) {
    return DisplayWindowUtils::shouldGatherPublishedSnapshotForCompleteDrawSource(
        hasSnapshot, capture, session, current, reusable, complete);
  };

  TEST_ASSERT_FALSE(gather(false, true, true, false, true, true));
  TEST_ASSERT_FALSE(gather(true, true, true, true, true, true));
  TEST_ASSERT_FALSE(gather(true, true, true, true, true, false));
  TEST_ASSERT_FALSE(gather(true, true, true, false, true, false));
  TEST_ASSERT_TRUE(gather(true, true, true, false, false, false));
  TEST_ASSERT_TRUE(gather(true, true, true, true, false, true));
  TEST_ASSERT_TRUE(gather(true, false, false, true, false, true));
  TEST_ASSERT_FALSE(gather(true, false, false, false, false, false));
}

void test_sealed_companion_hide_waits_for_replacement() {
  TEST_ASSERT_TRUE(DisplayWindowUtils::sealedCompanionHideShouldWaitForReplacement(
      true, true, false));
  TEST_ASSERT_FALSE(DisplayWindowUtils::sealedCompanionHideShouldWaitForReplacement(
      true, true, true));
  TEST_ASSERT_FALSE(DisplayWindowUtils::sealedCompanionHideShouldWaitForReplacement(
      false, true, false));
  TEST_ASSERT_FALSE(DisplayWindowUtils::sealedCompanionHideShouldWaitForReplacement(
      true, false, false));
}

void test_sealed_companion_transform_already_represented() {
  TEST_ASSERT_TRUE(DisplayWindowUtils::sealedCompanionTransformAlreadyRepresented(true, false));
  TEST_ASSERT_TRUE(DisplayWindowUtils::sealedCompanionTransformAlreadyRepresented(false, true));
  TEST_ASSERT_FALSE(DisplayWindowUtils::sealedCompanionTransformAlreadyRepresented(false, false));
}

void test_sealed_capture_preview_row_covered_by_visual_cache() {
  NoteUtils::DisplayNoteVec cache;
  NoteUtils::DisplayNote row{};
  row.noteId = 112;
  row.note = 72;
  row.startTick = 768;
  row.endTick = 900;
  TEST_ASSERT_FALSE(DisplayWindowUtils::sealedCapturePreviewRowCoveredByVisualCache(
      112, 72, 768, 900, cache));
  cache.push_back(row);
  TEST_ASSERT_TRUE(DisplayWindowUtils::sealedCapturePreviewRowCoveredByVisualCache(
      112, 72, 768, 900, cache));
  TEST_ASSERT_FALSE(DisplayWindowUtils::sealedCapturePreviewRowCoveredByVisualCache(
      kInvalidNoteId, 60, 100, 200, cache));
}

void test_should_retain_record_stop_handoff_seed() {
  TEST_ASSERT_TRUE(DisplayWindowUtils::shouldRetainRecordStopHandoffSeed(false, false));
  TEST_ASSERT_FALSE(DisplayWindowUtils::shouldRetainRecordStopHandoffSeed(true, false));
  TEST_ASSERT_FALSE(DisplayWindowUtils::shouldRetainRecordStopHandoffSeed(false, true));
  TEST_ASSERT_FALSE(DisplayWindowUtils::shouldRetainRecordStopHandoffSeed(true, true));
}

void test_should_retain_unpublished_visual_cache_over_stale_gather() {
  // 214900: dirty unpublished compose 65 vs this stale gather 56. Sealed wrap
  // gather (`170537`) keeps the snapshot+sealed prefix.
  TEST_ASSERT_TRUE(DisplayWindowUtils::shouldRetainUnpublishedVisualCacheOverStaleGather(
      false, true, 65u, 56u, false));
  TEST_ASSERT_FALSE(DisplayWindowUtils::shouldRetainUnpublishedVisualCacheOverStaleGather(
      false, true, 65u, 56u, true));
  TEST_ASSERT_FALSE(DisplayWindowUtils::shouldRetainUnpublishedVisualCacheOverStaleGather(
      true, true, 65u, 56u, false));
  TEST_ASSERT_FALSE(DisplayWindowUtils::shouldRetainUnpublishedVisualCacheOverStaleGather(
      false, false, 65u, 56u, false));
  TEST_ASSERT_FALSE(DisplayWindowUtils::shouldRetainUnpublishedVisualCacheOverStaleGather(
      false, true, 56u, 56u, false));
  TEST_ASSERT_FALSE(DisplayWindowUtils::shouldRetainUnpublishedVisualCacheOverStaleGather(
      false, true, 0u, 56u, false));
  TEST_ASSERT_FALSE(DisplayWindowUtils::shouldRetainUnpublishedVisualCacheOverStaleGather(
      false, true, 56u, 65u, false));
}

void test_should_retain_visual_cache_over_incomplete_current_snapshot_gather() {
  // 194531: current snapshot OD open notes=31 vs clean visual=32 (bar-crossing
  // id 27). Unpublished-stale retain stays false when snapshot is current.
  TEST_ASSERT_FALSE(DisplayWindowUtils::shouldRetainUnpublishedVisualCacheOverStaleGather(
      true, true, 32u, 31u, false));
  TEST_ASSERT_TRUE(DisplayWindowUtils::shouldRetainVisualCacheOverIncompleteCurrentSnapshotGather(
      true, true, false, 32u, 31u));
  TEST_ASSERT_FALSE(DisplayWindowUtils::shouldRetainVisualCacheOverIncompleteCurrentSnapshotGather(
      false, true, false, 32u, 31u));
  TEST_ASSERT_FALSE(DisplayWindowUtils::shouldRetainVisualCacheOverIncompleteCurrentSnapshotGather(
      true, false, false, 32u, 31u));
  TEST_ASSERT_FALSE(DisplayWindowUtils::shouldRetainVisualCacheOverIncompleteCurrentSnapshotGather(
      true, true, true, 32u, 31u));
  TEST_ASSERT_FALSE(DisplayWindowUtils::shouldRetainVisualCacheOverIncompleteCurrentSnapshotGather(
      true, true, false, 31u, 31u));
  TEST_ASSERT_FALSE(DisplayWindowUtils::shouldRetainVisualCacheOverIncompleteCurrentSnapshotGather(
      true, true, false, 0u, 31u));
  TEST_ASSERT_FALSE(DisplayWindowUtils::shouldRetainVisualCacheOverIncompleteCurrentSnapshotGather(
      true, true, false, 31u, 32u));
}

void test_should_write_live_display_notes_on_transport_stop() {
  TEST_ASSERT_TRUE(DisplayWindowUtils::shouldWriteLiveDisplayNotesOnTransportStop(true));
  TEST_ASSERT_FALSE(DisplayWindowUtils::shouldWriteLiveDisplayNotesOnTransportStop(false));
}

void test_should_union_sealed_capture_preview_for_draw_source() {
  // Live session: uncovered sealed stays in the frame (012714 wrap 2).
  TEST_ASSERT_TRUE(DisplayWindowUtils::shouldUnionSealedCapturePreviewForDrawSource(
      true, false, false, true));
  TEST_ASSERT_TRUE(DisplayWindowUtils::shouldUnionSealedCapturePreviewForDrawSource(
      true, false, true, false));
  // Covered, or session closed: do not suffix-union onto adopted visual.
  TEST_ASSERT_FALSE(DisplayWindowUtils::shouldUnionSealedCapturePreviewForDrawSource(
      true, true, false, true));
  TEST_ASSERT_FALSE(DisplayWindowUtils::shouldUnionSealedCapturePreviewForDrawSource(
      true, false, false, false));
  TEST_ASSERT_FALSE(DisplayWindowUtils::shouldUnionSealedCapturePreviewForDrawSource(
      false, false, true, true));
}

void test_should_adopt_composed_notes_over_visual_cache() {
  // 000013: 4-bar compose 141 > cache 95 must adopt. Long-loop window must not.
  TEST_ASSERT_TRUE(
      DisplayWindowUtils::shouldAdoptComposedNotesOverVisualCache(141u, 95u, 4u));
  TEST_ASSERT_TRUE(
      DisplayWindowUtils::shouldAdoptComposedNotesOverVisualCache(141u, 0u, 64u));
  TEST_ASSERT_FALSE(
      DisplayWindowUtils::shouldAdoptComposedNotesOverVisualCache(496u, 2403u, 64u));
  TEST_ASSERT_FALSE(
      DisplayWindowUtils::shouldAdoptComposedNotesOverVisualCache(95u, 95u, 4u));
  TEST_ASSERT_FALSE(
      DisplayWindowUtils::shouldAdoptComposedNotesOverVisualCache(0u, 95u, 4u));
}

void test_closed_capture_preview_identity_requires_closed_preview_row() {
  NoteUtils::DisplayNote painted{};
  painted.noteId = 168;
  painted.note = 69;
  painted.startTick = 48;
  painted.endTick = 88;
  NoteUtils::DisplayNoteVec preview;
  TEST_ASSERT_FALSE(
      DisplayWindowUtils::displayNoteIsClosedCapturePreviewIdentity(painted, preview));

  NoteUtils::DisplayNote openHold{};
  openHold.noteId = 168;
  openHold.note = 69;
  openHold.startTick = 48;
  openHold.endTick = 48;
  preview.push_back(openHold);
  TEST_ASSERT_FALSE(
      DisplayWindowUtils::displayNoteIsClosedCapturePreviewIdentity(painted, preview));

  preview[0].endTick = 88;
  TEST_ASSERT_TRUE(
      DisplayWindowUtils::displayNoteIsClosedCapturePreviewIdentity(painted, preview));

  painted.noteId = kInvalidNoteId;
  TEST_ASSERT_FALSE(
      DisplayWindowUtils::displayNoteIsClosedCapturePreviewIdentity(painted, preview));
}

void test_keep_shortest_preserves_distinct_valid_note_ids() {
  NoteUtils::DisplayNoteVec notes;
  NoteUtils::DisplayNote first{};
  first.noteId = 32;
  first.note = 69;
  first.startTick = 48;
  first.endTick = 88;
  NoteUtils::DisplayNote second{};
  second.noteId = 86;
  second.note = 69;
  second.startTick = 48;
  second.endTick = 88;
  NoteUtils::DisplayNote longer{};
  longer.noteId = 87;
  longer.note = 69;
  longer.startTick = 48;
  longer.endTick = 96;
  notes.push_back(first);
  notes.push_back(second);
  notes.push_back(longer);
  DisplayWindowUtils::keepShortestEndAtPitchStart(notes);
  TEST_ASSERT_EQUAL(3u, notes.size());
  TEST_ASSERT_TRUE(DisplayWindowUtils::displayNoteIdentityPresent(notes, first));
  TEST_ASSERT_TRUE(DisplayWindowUtils::displayNoteIdentityPresent(notes, second));
  TEST_ASSERT_TRUE(DisplayWindowUtils::displayNoteIdentityPresent(notes, longer));
}

void test_keep_shortest_end_at_pitch_start() {
  NoteUtils::DisplayNoteVec notes;
  NoteUtils::DisplayNote shorter{};
  shorter.note = 63;
  shorter.startTick = 768;
  shorter.endTick = 864;
  NoteUtils::DisplayNote longer{};
  longer.note = 63;
  longer.startTick = 768;
  longer.endTick = 1056;
  NoteUtils::DisplayNote other{};
  other.note = 63;
  other.startTick = 1152;
  other.endTick = 1248;
  notes.push_back(longer);
  notes.push_back(shorter);
  notes.push_back(other);
  DisplayWindowUtils::keepShortestEndAtPitchStart(notes);
  TEST_ASSERT_EQUAL(2u, notes.size());
  bool keptShort = false;
  bool keptLong = false;
  bool keptOther = false;
  for (const NoteUtils::DisplayNote& note : notes) {
    if (note.startTick == 768 && note.endTick == 864) {
      keptShort = true;
    }
    if (note.startTick == 768 && note.endTick == 1056) {
      keptLong = true;
    }
    if (note.startTick == 1152) {
      keptOther = true;
    }
  }
  TEST_ASSERT_TRUE(keptShort);
  TEST_ASSERT_FALSE(keptLong);
  TEST_ASSERT_TRUE(keptOther);
}

void test_upsert_display_note_at_pitch_start_replaces_stale_committed_row() {
  // 150650: committed tier still holds the previous pass at s0 len40 / s768 len48 while the
  // sealed pass is 624 long at both starts.
  NoteUtils::DisplayNoteVec committed;
  NoteUtils::DisplayNote stalePhaseZero{};
  stalePhaseZero.noteId = 11;
  stalePhaseZero.note = 64;
  stalePhaseZero.startTick = 0;
  stalePhaseZero.endTick = 40;
  NoteUtils::DisplayNote otherPitch{};
  otherPitch.noteId = 12;
  otherPitch.note = 69;
  otherPitch.startTick = 96;
  otherPitch.endTick = 240;
  NoteUtils::DisplayNote staleSecondBar{};
  staleSecondBar.noteId = 13;
  staleSecondBar.note = 64;
  staleSecondBar.startTick = 768;
  staleSecondBar.endTick = 816;
  committed.push_back(stalePhaseZero);
  committed.push_back(otherPitch);
  committed.push_back(staleSecondBar);

  NoteUtils::DisplayNote sealedPhaseZero{};
  sealedPhaseZero.noteId = 21;
  sealedPhaseZero.note = 64;
  sealedPhaseZero.startTick = 0;
  sealedPhaseZero.endTick = 624;
  DisplayWindowUtils::upsertDisplayNoteAtPitchStart(committed, sealedPhaseZero);
  NoteUtils::DisplayNote sealedSecondBar{};
  sealedSecondBar.noteId = 22;
  sealedSecondBar.note = 64;
  sealedSecondBar.startTick = 768;
  sealedSecondBar.endTick = 1392;
  DisplayWindowUtils::upsertDisplayNoteAtPitchStart(committed, sealedSecondBar);

  // Replaced in place: no duplicate rows, unrelated pitch untouched, order preserved.
  TEST_ASSERT_EQUAL(3u, committed.size());
  TEST_ASSERT_EQUAL(64u, committed[0].note);
  TEST_ASSERT_EQUAL(0u, committed[0].startTick);
  TEST_ASSERT_EQUAL(624u, committed[0].endTick);
  TEST_ASSERT_EQUAL(21u, committed[0].noteId);
  TEST_ASSERT_EQUAL(69u, committed[1].note);
  TEST_ASSERT_EQUAL(240u, committed[1].endTick);
  TEST_ASSERT_EQUAL(64u, committed[2].note);
  TEST_ASSERT_EQUAL(768u, committed[2].startTick);
  TEST_ASSERT_EQUAL(1392u, committed[2].endTick);
}

void test_upsert_display_note_at_pitch_start_appends_when_no_match() {
  NoteUtils::DisplayNoteVec committed;
  NoteUtils::DisplayNote samePitchOtherStart{};
  samePitchOtherStart.note = 64;
  samePitchOtherStart.startTick = 384;
  samePitchOtherStart.endTick = 576;
  committed.push_back(samePitchOtherStart);

  // Same pitch, different start: a companion-shortened row must not be overridden.
  NoteUtils::DisplayNote sealed{};
  sealed.note = 64;
  sealed.startTick = 0;
  sealed.endTick = 624;
  DisplayWindowUtils::upsertDisplayNoteAtPitchStart(committed, sealed);
  TEST_ASSERT_EQUAL(2u, committed.size());
  TEST_ASSERT_EQUAL(384u, committed[0].startTick);
  TEST_ASSERT_EQUAL(576u, committed[0].endTick);
  TEST_ASSERT_EQUAL(0u, committed[1].startTick);

  // Same start, different pitch: appended.
  NoteUtils::DisplayNote otherPitch{};
  otherPitch.note = 67;
  otherPitch.startTick = 384;
  otherPitch.endTick = 400;
  DisplayWindowUtils::upsertDisplayNoteAtPitchStart(committed, otherPitch);
  TEST_ASSERT_EQUAL(3u, committed.size());
  TEST_ASSERT_EQUAL(67u, committed[2].note);
}

void test_stopped_sealed_upsert_skips_longer_same_pitch_start_154310() {
  // First STOPPED frame: overlay-shortened prefix 63@0 len 143, sealed remnant 576.
  NoteUtils::DisplayNoteVec notes;
  NoteUtils::DisplayNote prefix{};
  prefix.noteId = 1;
  prefix.note = 63;
  prefix.startTick = 0;
  prefix.endTick = 143;
  notes.push_back(prefix);
  const size_t committedEnd = notes.size();

  NoteUtils::DisplayNote sealed{};
  sealed.noteId = 2;
  sealed.note = 63;
  sealed.startTick = 0;
  sealed.endTick = 576;
  DisplayWindowUtils::upsertDisplayNoteAtPitchStartFrom(notes, committedEnd, sealed, false);

  TEST_ASSERT_EQUAL(1u, notes.size());
  TEST_ASSERT_EQUAL(63u, notes[0].note);
  TEST_ASSERT_EQUAL_UINT32(0u, notes[0].startTick);
  TEST_ASSERT_EQUAL_UINT32(143u, notes[0].endTick);
  TEST_ASSERT_EQUAL(1, notes[0].noteId);
}

void test_capture_sealed_upsert_keeps_longer_suffix_150650() {
  // Capture seam: stale prefix 64@0 len 40, live sealed 624 must still append.
  NoteUtils::DisplayNoteVec notes;
  NoteUtils::DisplayNote prefix{};
  prefix.noteId = 11;
  prefix.note = 64;
  prefix.startTick = 0;
  prefix.endTick = 40;
  notes.push_back(prefix);
  const size_t committedEnd = notes.size();

  NoteUtils::DisplayNote sealed{};
  sealed.noteId = 21;
  sealed.note = 64;
  sealed.startTick = 0;
  sealed.endTick = 624;
  DisplayWindowUtils::upsertDisplayNoteAtPitchStartFrom(notes, committedEnd, sealed, true);

  TEST_ASSERT_EQUAL(2u, notes.size());
  TEST_ASSERT_EQUAL_UINT32(40u, notes[0].endTick);
  TEST_ASSERT_EQUAL(11, notes[0].noteId);
  TEST_ASSERT_EQUAL_UINT32(624u, notes[1].endTick);
  TEST_ASSERT_EQUAL(21, notes[1].noteId);
}

void test_overlay_keeps_distinct_valid_ids_at_same_pitch_start_195409() {
  NoteUtils::DisplayNoteVec notes;
  notes.push_back({32, 69, 90, 48, 200});
  const size_t committedEnd = notes.size();

  DisplayWindowUtils::upsertDisplayNoteAtPitchStartFrom(
      notes, committedEnd, NoteUtils::DisplayNote{124, 69, 90, 48, 96}, true);
  DisplayWindowUtils::upsertDisplayNoteAtPitchStartFrom(
      notes, committedEnd, NoteUtils::DisplayNote{148, 69, 90, 48, 96}, true);

  TEST_ASSERT_EQUAL(3u, notes.size());
  TEST_ASSERT_EQUAL(32, notes[0].noteId);
  TEST_ASSERT_EQUAL_UINT32(200u, notes[0].endTick);
  TEST_ASSERT_EQUAL(124, notes[1].noteId);
  TEST_ASSERT_EQUAL_UINT32(96u, notes[1].endTick);
  TEST_ASSERT_EQUAL(148, notes[2].noteId);
  TEST_ASSERT_EQUAL_UINT32(96u, notes[2].endTick);
}

void test_overlay_same_id_replaces_suffix_row() {
  NoteUtils::DisplayNoteVec notes;
  notes.push_back({32, 69, 90, 48, 200});
  const size_t committedEnd = notes.size();
  notes.push_back({148, 69, 90, 48, 88});

  DisplayWindowUtils::upsertDisplayNoteAtPitchStartFrom(
      notes, committedEnd, NoteUtils::DisplayNote{148, 69, 90, 48, 96}, true);

  TEST_ASSERT_EQUAL(2u, notes.size());
  TEST_ASSERT_EQUAL(32, notes[0].noteId);
  TEST_ASSERT_EQUAL(148, notes[1].noteId);
  TEST_ASSERT_EQUAL_UINT32(96u, notes[1].endTick);
}

/// [`191636`] pitch 18 WRAP On@2181 Off@100: closed preview emits On→loopEnd and
/// head with the same NoteId. Upsert-by-id alone must not let the head wipe the tail.
void test_overlay_same_id_keeps_wrap_on_to_loop_end_and_head_191636() {
  NoteUtils::DisplayNoteVec notes;
  const size_t committedEnd = 0;
  constexpr NoteId kId = 57;
  constexpr uint8_t kPitch = 18;
  constexpr uint32_t kOn = 2181;
  constexpr uint32_t kLoopEndInclusive = 2303;
  constexpr uint32_t kHeadOff = 100;

  DisplayWindowUtils::upsertDisplayNoteAtPitchStartFrom(
      notes, committedEnd, NoteUtils::DisplayNote{kId, kPitch, 98, kOn, kLoopEndInclusive},
      true);
  DisplayWindowUtils::upsertDisplayNoteAtPitchStartFrom(
      notes, committedEnd, NoteUtils::DisplayNote{kId, kPitch, 98, 0, kHeadOff}, true);

  TEST_ASSERT_EQUAL(2u, notes.size());
  bool sawTail = false;
  bool sawHead = false;
  for (const NoteUtils::DisplayNote& note : notes) {
    TEST_ASSERT_EQUAL(kId, note.noteId);
    TEST_ASSERT_EQUAL_UINT8(kPitch, note.note);
    if (note.startTick == kOn && note.endTick == kLoopEndInclusive) {
      sawTail = true;
    }
    if (note.startTick == 0u && note.endTick == kHeadOff) {
      sawHead = true;
    }
  }
  TEST_ASSERT_TRUE(sawTail);
  TEST_ASSERT_TRUE(sawHead);
}

void test_overlay_invalid_id_still_replaces_pitch_start() {
  NoteUtils::DisplayNoteVec notes;
  notes.push_back({32, 69, 90, 48, 200});
  const size_t committedEnd = notes.size();
  notes.push_back({kInvalidNoteId, 69, 90, 48, 88});

  DisplayWindowUtils::upsertDisplayNoteAtPitchStartFrom(
      notes, committedEnd, NoteUtils::DisplayNote{kInvalidNoteId, 69, 90, 48, 96}, true);

  TEST_ASSERT_EQUAL(2u, notes.size());
  TEST_ASSERT_EQUAL(32, notes[0].noteId);
  TEST_ASSERT_EQUAL(kInvalidNoteId, notes[1].noteId);
  TEST_ASSERT_EQUAL_UINT32(96u, notes[1].endTick);
}

void test_overlay_repeated_sealed_ids_keep_equal_geometry_195409() {
  NoteUtils::DisplayNoteVec notes;
  const size_t committedEnd = 0;
  DisplayWindowUtils::upsertDisplayNoteAtPitchStartFrom(
      notes, committedEnd, NoteUtils::DisplayNote{124, 69, 90, 48, 96}, true);
  DisplayWindowUtils::upsertDisplayNoteAtPitchStartFrom(
      notes, committedEnd, NoteUtils::DisplayNote{136, 69, 90, 48, 96}, true);
  DisplayWindowUtils::upsertDisplayNoteAtPitchStartFrom(
      notes, committedEnd, NoteUtils::DisplayNote{148, 69, 90, 48, 96}, true);

  TEST_ASSERT_EQUAL(3u, notes.size());
  TEST_ASSERT_EQUAL(124, notes[0].noteId);
  TEST_ASSERT_EQUAL(136, notes[1].noteId);
  TEST_ASSERT_EQUAL(148, notes[2].noteId);
  TEST_ASSERT_EQUAL_UINT32(96u, notes[0].endTick);
  TEST_ASSERT_EQUAL_UINT32(96u, notes[1].endTick);
  TEST_ASSERT_EQUAL_UINT32(96u, notes[2].endTick);
}

void test_stopped_playhead_tail_takes_closed_capture_off_153848() {
  // Last OVERDUBBING paint left 52@2172 at playhead 2230. Sealed Off is 2231.
  // RC1b skip keeps 2230; this-pass suffix takes the closed Off (not stopPhase).
  NoteUtils::DisplayNoteVec notes;
  NoteUtils::DisplayNote live{};
  live.noteId = 40;
  live.note = 52;
  live.startTick = 2172;
  live.endTick = 2230;
  notes.push_back(live);
  const size_t committedEnd = notes.size();

  NoteUtils::DisplayNote sealed{};
  sealed.noteId = 40;
  sealed.note = 52;
  sealed.startTick = 2172;
  sealed.endTick = 2231;
  DisplayWindowUtils::upsertDisplayNoteAtPitchStartFrom(notes, committedEnd, sealed, false);
  TEST_ASSERT_EQUAL(1u, notes.size());
  TEST_ASSERT_EQUAL_UINT32(2230u, notes[0].endTick);

  NoteUtils::DisplayNoteVec closedPreview;
  closedPreview.push_back(sealed);
  DisplayWindowUtils::realizeClosedCaptureOffOnStopPlayheadTails(notes, closedPreview, 0);
  TEST_ASSERT_EQUAL(1u, notes.size());
  TEST_ASSERT_EQUAL_UINT32(2172u, notes[0].startTick);
  TEST_ASSERT_EQUAL_UINT32(2231u, notes[0].endTick);
}

void test_stopped_playhead_tail_does_not_take_sealed_remnant_154310() {
  NoteUtils::DisplayNoteVec notes;
  NoteUtils::DisplayNote prefix{};
  prefix.noteId = 1;
  prefix.note = 63;
  prefix.startTick = 0;
  prefix.endTick = 143;
  notes.push_back(prefix);

  NoteUtils::DisplayNote sealed{};
  sealed.noteId = 2;
  sealed.note = 63;
  sealed.startTick = 0;
  sealed.endTick = 576;
  NoteUtils::DisplayNoteVec closedPreview;
  closedPreview.push_back(sealed);
  DisplayWindowUtils::realizeClosedCaptureOffOnStopPlayheadTails(notes, closedPreview, 1);
  TEST_ASSERT_EQUAL(1u, notes.size());
  TEST_ASSERT_EQUAL_UINT32(143u, notes[0].endTick);
  TEST_ASSERT_EQUAL(1, notes[0].noteId);
}

void test_stopped_closed_off_takes_sealed_165050_52() {
  // Mid-pass close: live tail 2192 is not stopPhase. Sealed Off 2193 is product.
  NoteUtils::DisplayNoteVec notes;
  NoteUtils::DisplayNote live{};
  live.noteId = 11;
  live.note = 52;
  live.startTick = 2179;
  live.endTick = 2192;
  notes.push_back(live);

  NoteUtils::DisplayNote sealed{};
  sealed.noteId = 11;
  sealed.note = 52;
  sealed.startTick = 2179;
  sealed.endTick = 2193;
  NoteUtils::DisplayNoteVec closedPreview;
  closedPreview.push_back(sealed);
  DisplayWindowUtils::realizeClosedCaptureOffOnStopPlayheadTails(notes, closedPreview, 0);
  TEST_ASSERT_EQUAL(1u, notes.size());
  TEST_ASSERT_EQUAL_UINT32(2179u, notes[0].startTick);
  TEST_ASSERT_EQUAL_UINT32(2193u, notes[0].endTick);
}

void test_stopped_closed_off_takes_sealed_165050_48() {
  NoteUtils::DisplayNoteVec notes;
  NoteUtils::DisplayNote live{};
  live.noteId = 12;
  live.note = 48;
  live.startTick = 2564;
  live.endTick = 2607;
  notes.push_back(live);

  NoteUtils::DisplayNote sealed{};
  sealed.noteId = 12;
  sealed.note = 48;
  sealed.startTick = 2564;
  sealed.endTick = 2608;
  NoteUtils::DisplayNoteVec closedPreview;
  closedPreview.push_back(sealed);
  DisplayWindowUtils::realizeClosedCaptureOffOnStopPlayheadTails(notes, closedPreview, 0);
  TEST_ASSERT_EQUAL(1u, notes.size());
  TEST_ASSERT_EQUAL_UINT32(2564u, notes[0].startTick);
  TEST_ASSERT_EQUAL_UINT32(2608u, notes[0].endTick);
}

void test_stopped_sealed_upsert_appends_when_prefix_lacks_pitch_start() {
  NoteUtils::DisplayNoteVec notes;
  NoteUtils::DisplayNote prefix{};
  prefix.noteId = 1;
  prefix.note = 63;
  prefix.startTick = 0;
  prefix.endTick = 143;
  notes.push_back(prefix);
  const size_t committedEnd = notes.size();

  NoteUtils::DisplayNote sealed{};
  sealed.noteId = 3;
  sealed.note = 63;
  sealed.startTick = 192;
  sealed.endTick = 576;
  DisplayWindowUtils::upsertDisplayNoteAtPitchStartFrom(notes, committedEnd, sealed, false);

  TEST_ASSERT_EQUAL(2u, notes.size());
  TEST_ASSERT_EQUAL_UINT32(192u, notes[1].startTick);
  TEST_ASSERT_EQUAL_UINT32(576u, notes[1].endTick);
}

void test_upsert_display_note_at_pitch_start_wrap_held_sealed_row_never_replaces() {
  NoteUtils::DisplayNoteVec committed;
  NoteUtils::DisplayNote closedRow{};
  closedRow.note = 64;
  closedRow.startTick = 1440;
  closedRow.endTick = 1500;
  committed.push_back(closedRow);

  // An open wrap-held hold must not clobber the closed committed row at that pitch+start.
  NoteUtils::DisplayNote wrapHeld{};
  wrapHeld.note = 64;
  wrapHeld.startTick = 1440;
  wrapHeld.endTick = 96;
  DisplayWindowUtils::upsertDisplayNoteAtPitchStart(committed, wrapHeld);
  TEST_ASSERT_EQUAL(2u, committed.size());
  TEST_ASSERT_EQUAL(1500u, committed[0].endTick);
  TEST_ASSERT_EQUAL(96u, committed[1].endTick);
}

void test_clamp_display_note_resize_never_grows() {
  TEST_ASSERT_EQUAL(3u, DisplayWindowUtils::clampDisplayNoteResizeCount(3u, 5u));
  TEST_ASSERT_EQUAL(2u, DisplayWindowUtils::clampDisplayNoteResizeCount(3u, 2u));
  TEST_ASSERT_EQUAL(0u, DisplayWindowUtils::clampDisplayNoteResizeCount(3u, 0u));
  TEST_ASSERT_EQUAL(0u, DisplayWindowUtils::clampDisplayNoteResizeCount(0u, 4u));
}

void test_sync_live_display_compose_counts_clamps_stale_layers() {
  size_t committed = 101u;
  size_t capture = 0u;
  size_t base = 101u;
  DisplayWindowUtils::syncLiveDisplayComposeCounts(91u, committed, capture, base);
  TEST_ASSERT_EQUAL(91u, committed);
  TEST_ASSERT_EQUAL(0u, capture);
  TEST_ASSERT_EQUAL(91u, base);

  committed = 80u;
  capture = 20u;
  base = 100u;
  DisplayWindowUtils::syncLiveDisplayComposeCounts(91u, committed, capture, base);
  TEST_ASSERT_EQUAL(80u, committed);
  TEST_ASSERT_EQUAL(11u, capture);
  TEST_ASSERT_EQUAL(91u, base);

  NoteUtils::DisplayNoteVec notes;
  for (size_t i = 0; i < 91; ++i) {
    NoteUtils::DisplayNote note{};
    note.note = 60;
    note.startTick = static_cast<uint32_t>(i * 48u);
    note.endTick = note.startTick + 48u;
    notes.push_back(note);
  }
  notes.resize(DisplayWindowUtils::clampDisplayNoteResizeCount(notes.size(), 101u));
  TEST_ASSERT_EQUAL(91u, notes.size());
  for (const NoteUtils::DisplayNote& note : notes) {
    TEST_ASSERT_EQUAL_UINT8(60, note.note);
    TEST_ASSERT_FALSE(note.note == 0 && note.startTick == 0 && note.endTick == 0);
  }
}

void test_keep_shortest_end_on_committed_prefix_preserves_capture_suffix() {
  NoteUtils::DisplayNoteVec notes;
  NoteUtils::DisplayNote shorter{};
  shorter.note = 60;
  shorter.startTick = 768;
  shorter.endTick = 864;
  NoteUtils::DisplayNote longer{};
  longer.note = 60;
  longer.startTick = 768;
  longer.endTick = 1056;
  NoteUtils::DisplayNote captureLive{};
  captureLive.note = 60;
  captureLive.startTick = 768;
  captureLive.endTick = 1200;
  notes.push_back(longer);
  notes.push_back(shorter);
  notes.push_back(captureLive);
  DisplayWindowUtils::keepShortestEndAtPitchStartOnCommittedPrefix(notes, 2u);
  TEST_ASSERT_EQUAL(2u, notes.size());
  TEST_ASSERT_EQUAL(864u, notes[0].endTick);
  TEST_ASSERT_EQUAL(1200u, notes[1].endTick);
}

void test_should_keep_visual_cache_over_smaller_full_loop_slice() {
  TEST_ASSERT_TRUE(
      DisplayWindowUtils::shouldKeepVisualCacheOverSmallerFullLoopSlice(95u, 80u));
  TEST_ASSERT_FALSE(
      DisplayWindowUtils::shouldKeepVisualCacheOverSmallerFullLoopSlice(95u, 141u));
  TEST_ASSERT_FALSE(
      DisplayWindowUtils::shouldKeepVisualCacheOverSmallerFullLoopSlice(0u, 80u));
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

void test_visual_cache_covers_window_uses_clean_neighborhood() {
  const uint32_t bar = Config::TICKS_PER_BAR;
  const uint32_t loopLength = 195u * bar;
  VisualBarVec dirty(195u, 1);
  for (uint32_t b = 170; b < 195; ++b) {
    dirty[b] = 0;
  }
  // Dirty bars in the requested window still block the filter.
  TEST_ASSERT_FALSE(visualCacheCoversWindow(true, dirty, 0, 16u * bar, loopLength, bar));
  // A fully clean 16-bar neighborhood may authorize filter while the rest of the loop is dirty.
  TEST_ASSERT_TRUE(
      visualCacheCoversWindow(true, dirty, 179u * bar, 16u * bar, loopLength, bar));
  TEST_ASSERT_TRUE(visualCacheCoversWindow(false, VisualBarVec{}, 0, 16u * bar, loopLength, bar));
  TEST_ASSERT_FALSE(
      visualCacheCoversWindow(true, VisualBarVec{}, 0, 16u * bar, loopLength, bar));
}

void test_visual_cache_covers_window_rejects_first_slice_prefix() {
  const uint32_t bar = Config::TICKS_PER_BAR;
  const uint32_t loopLength = 20u * bar;
  VisualBarVec dirty(20u, 1);
  for (uint32_t b = 0; b < 4; ++b) {
    dirty[b] = 0;
  }
  TEST_ASSERT_FALSE(visualCacheCoversWindow(true, dirty, 0, 16u * bar, loopLength, bar));
  TEST_ASSERT_TRUE(visualCacheCoversWindow(true, dirty, 0, 2u * bar, loopLength, bar));
}

void test_visual_cache_covers_window_follow_lookahead_requires_next_bar() {
  const uint32_t bar = Config::TICKS_PER_BAR;
  const uint32_t loopLength = 66u * bar;
  VisualBarVec dirty(66u, 0);
  dirty[16] = 1;
  const uint32_t windowStart = 0;
  const uint32_t windowLength = 16u * bar;
  TEST_ASSERT_TRUE(visualCacheCoversWindow(true, dirty, windowStart, windowLength, loopLength, bar,
                                           0));
  TEST_ASSERT_FALSE(visualCacheCoversWindow(true, dirty, windowStart, windowLength, loopLength, bar,
                                            DisplayWindowUtils::kFollowReadyLookaheadBars));
  dirty[16] = 0;
  TEST_ASSERT_TRUE(visualCacheCoversWindow(true, dirty, windowStart, windowLength, loopLength, bar,
                                           DisplayWindowUtils::kFollowReadyLookaheadBars));
}

void test_follow_window_filter_includes_newly_exposed_bars() {
  // session_20260819_232337: after bar 16 the paint window leaves bars 0-15. Filtering the
  // full visualCache must include the newly exposed bar and drop the vacated start bar.
  const uint32_t bar = Config::TICKS_PER_BAR;
  const uint32_t loopLength = 66u * bar;
  NoteUtils::DisplayNoteVec notes;
  notes.push_back({kInvalidNoteId, 36, 100, 0, 48});
  notes.push_back({kInvalidNoteId, 48, 100, 16u * bar, 16u * bar + 48});
  notes.push_back({kInvalidNoteId, 60, 100, 40u * bar, 40u * bar + 48});
  uint32_t gatherStart = 0;
  uint32_t gatherLength = 0;
  const uint32_t marginBars =
      2u > DisplayWindowUtils::kFollowReadyLookaheadBars
          ? 2u
          : DisplayWindowUtils::kFollowReadyLookaheadBars;
  DisplayWindowUtils::expandWindowWithMargin(8u * bar, 16u * bar, loopLength, marginBars * bar,
                                             gatherStart, gatherLength);
  const NoteUtils::DisplayNoteVec filtered =
      DisplayWindowUtils::filterDisplayNotesByWindowInclusion(notes, gatherStart, gatherLength,
                                                              loopLength);
  TEST_ASSERT_FALSE(DisplayWindowUtils::paintWindowInsideGather(8u * bar, 16u * bar, 0, 18u * bar));
  bool sawStartBar = false;
  bool sawNewlyExposed = false;
  bool sawFarBar = false;
  for (const auto& note : filtered) {
    if (note.note == 36) {
      sawStartBar = true;
    }
    if (note.note == 48) {
      sawNewlyExposed = true;
    }
    if (note.note == 60) {
      sawFarBar = true;
    }
  }
  TEST_ASSERT_FALSE(sawStartBar);
  TEST_ASSERT_TRUE(sawNewlyExposed);
  TEST_ASSERT_FALSE(sawFarBar);
}

void test_expand_window_with_margin_clamps_to_loop() {
  const uint32_t bar = Config::TICKS_PER_BAR;
  const uint32_t loopLength = 66u * bar;
  const uint32_t margin = 2u * bar;
  uint32_t gatherStart = 0;
  uint32_t gatherLength = 0;
  DisplayWindowUtils::expandWindowWithMargin(0, 16u * bar, loopLength, margin, gatherStart,
                                             gatherLength);
  TEST_ASSERT_EQUAL_UINT32(0u, gatherStart);
  TEST_ASSERT_EQUAL_UINT32(18u * bar, gatherLength);

  DisplayWindowUtils::expandWindowWithMargin(16u * bar, 16u * bar, loopLength, margin, gatherStart,
                                             gatherLength);
  TEST_ASSERT_EQUAL_UINT32(14u * bar, gatherStart);
  TEST_ASSERT_EQUAL_UINT32(20u * bar, gatherLength);

  DisplayWindowUtils::expandWindowWithMargin(50u * bar, 16u * bar, loopLength, margin, gatherStart,
                                             gatherLength);
  TEST_ASSERT_EQUAL_UINT32(48u * bar, gatherStart);
  TEST_ASSERT_EQUAL_UINT32(18u * bar, gatherLength);
}

void test_expand_window_with_margin_includes_short_loop_tail() {
  // 002634: 20-bar loop, 16-bar window, 2-bar margin must include the last 2 bars.
  const uint32_t bar = Config::TICKS_PER_BAR;
  const uint32_t margin = 2u * bar;
  uint32_t gatherStart = 0;
  uint32_t gatherLength = 0;
  DisplayWindowUtils::expandWindowWithMargin(0, 16u * bar, 20u * bar, margin, gatherStart,
                                             gatherLength);
  TEST_ASSERT_EQUAL_UINT32(0u, gatherStart);
  TEST_ASSERT_EQUAL_UINT32(20u * bar, gatherLength);

  DisplayWindowUtils::expandWindowWithMargin(0, 16u * bar, 21u * bar, margin, gatherStart,
                                             gatherLength);
  TEST_ASSERT_EQUAL_UINT32(0u, gatherStart);
  TEST_ASSERT_EQUAL_UINT32(21u * bar, gatherLength);

  DisplayWindowUtils::expandWindowWithMargin(0, 16u * bar, 24u * bar, margin, gatherStart,
                                             gatherLength);
  TEST_ASSERT_EQUAL_UINT32(0u, gatherStart);
  TEST_ASSERT_EQUAL_UINT32(18u * bar, gatherLength);
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

void test_select_draw_source_for_bar_live_capture_wins() {
  TEST_ASSERT_EQUAL(static_cast<int>(DisplayWindowUtils::DrawSourcePath::Capture),
                    static_cast<int>(DisplayWindowUtils::selectDrawSourceForBar(true, true, true)));
  TEST_ASSERT_EQUAL(static_cast<int>(DisplayWindowUtils::DrawSourcePath::Capture),
                    static_cast<int>(DisplayWindowUtils::selectDrawSourceForBar(true, true, false)));
}

void test_select_draw_source_for_bar_snapshot_when_current() {
  TEST_ASSERT_EQUAL(
      static_cast<int>(DisplayWindowUtils::DrawSourcePath::Snapshot),
      static_cast<int>(DisplayWindowUtils::selectDrawSourceForBar(false, false, true)));
}

void test_select_draw_source_for_bar_capture_active_unions_whole_window() {
  TEST_ASSERT_EQUAL(
      static_cast<int>(DisplayWindowUtils::DrawSourcePath::Capture),
      static_cast<int>(DisplayWindowUtils::selectDrawSourceForBar(true, false, true)));
  TEST_ASSERT_EQUAL(
      static_cast<int>(DisplayWindowUtils::DrawSourcePath::Capture),
      static_cast<int>(DisplayWindowUtils::selectDrawSourceForBar(true, false, false)));
  TEST_ASSERT_FALSE(DisplayWindowUtils::capturePreviewNoteIsOpenHold(100, 400));
  TEST_ASSERT_TRUE(DisplayWindowUtils::capturePreviewNoteIsOpenHold(100, 100));
  TEST_ASSERT_FALSE(DisplayWindowUtils::capturePreviewNoteIsOpenHold(2304, 96));
}

void test_visual_cache_gather_covers_playback_revision_rejects_overlay() {
  TEST_ASSERT_FALSE(DisplayWindowUtils::visualCacheGatherCoversPlaybackRevision(
      true, false, false, true));
  TEST_ASSERT_FALSE(DisplayWindowUtils::visualCacheGatherCoversPlaybackRevision(
      true, false, true, false));
  TEST_ASSERT_TRUE(DisplayWindowUtils::visualCacheGatherCoversPlaybackRevision(
      false, true, false, false));
  TEST_ASSERT_TRUE(DisplayWindowUtils::visualCacheGatherCoversPlaybackRevision(
      false, false, true, false));
  TEST_ASSERT_TRUE(DisplayWindowUtils::visualCacheGatherCoversPlaybackRevision(
      false, false, false, true));
  TEST_ASSERT_FALSE(DisplayWindowUtils::visualCacheGatherCoversPlaybackRevision(
      false, false, false, false));
}

void test_select_draw_source_for_bar_visual_cache_fallback() {
  TEST_ASSERT_EQUAL(
      static_cast<int>(DisplayWindowUtils::DrawSourcePath::VisualCache),
      static_cast<int>(DisplayWindowUtils::selectDrawSourceForBar(false, false, false)));
  TEST_ASSERT_EQUAL(
      static_cast<int>(DisplayWindowUtils::DrawSourcePath::VisualCache),
      static_cast<int>(DisplayWindowUtils::selectDrawSourceForBar(false, true, false)));
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

namespace {

bool displayNotesShareIdentity(const NoteUtils::DisplayNote& left,
                               const NoteUtils::DisplayNote& right) {
  return left.noteId == right.noteId && left.note == right.note &&
         left.startTick == right.startTick && left.endTick == right.endTick;
}

void assertSameDisplayNoteIdentitySet(const NoteUtils::DisplayNoteVec& left,
                                      const NoteUtils::DisplayNoteVec& right) {
  TEST_ASSERT_EQUAL(left.size(), right.size());
  for (const NoteUtils::DisplayNote& note : left) {
    bool found = false;
    for (const NoteUtils::DisplayNote& other : right) {
      if (displayNotesShareIdentity(note, other)) {
        found = true;
        break;
      }
    }
    TEST_ASSERT_TRUE(found);
  }
}

}  // namespace

void test_resolve_detailed_paint_window_covers_four_bar_loop() {
  const uint32_t bar = Config::TICKS_PER_BAR;
  const uint32_t loopLength = 4u * bar;
  uint32_t windowStart = 99;
  uint32_t windowLength = 99;
  uint8_t windowBars = 99;
  TEST_ASSERT_TRUE(DisplayWindowUtils::resolveDetailedPaintWindow(
      loopLength, DisplayWindowUtils::kMaxDetailedWindowBars, 2u * bar, 3u * bar, true, windowStart,
      windowLength, windowBars));
  TEST_ASSERT_EQUAL_UINT32(0u, windowStart);
  TEST_ASSERT_EQUAL_UINT32(loopLength, windowLength);
  TEST_ASSERT_EQUAL_UINT8(4, windowBars);
  TEST_ASSERT_TRUE(DisplayWindowUtils::windowCoversLoop(windowLength, loopLength));
}

void test_resolve_detailed_paint_window_centers_thirty_two_bar_loop() {
  const uint32_t bar = Config::TICKS_PER_BAR;
  const uint32_t loopLength = 32u * bar;
  const uint32_t playhead = 16u * bar;
  uint32_t windowStart = 0;
  uint32_t windowLength = 0;
  uint8_t windowBars = 0;
  TEST_ASSERT_TRUE(DisplayWindowUtils::resolveDetailedPaintWindow(
      loopLength, DisplayWindowUtils::kMaxDetailedWindowBars, 0, playhead, true, windowStart,
      windowLength, windowBars));
  TEST_ASSERT_EQUAL_UINT32(16u * bar, windowLength);
  TEST_ASSERT_EQUAL_UINT8(16, windowBars);
  TEST_ASSERT_EQUAL_UINT32(
      DisplayWindowUtils::resolveCenteredWindowStart(playhead, windowLength, loopLength),
      windowStart);
  TEST_ASSERT_FALSE(DisplayWindowUtils::windowCoversLoop(windowLength, loopLength));
}

void test_short_loop_window_inclusion_keeps_full_identity_set() {
  const uint32_t bar = Config::TICKS_PER_BAR;
  const uint32_t loopLength = 4u * bar + 100u;
  NoteUtils::DisplayNoteVec notes;
  notes.push_back({1, 60, 100, bar, bar + 48});
  notes.push_back({2, 62, 100, loopLength - 50u, 80});
  notes.push_back({3, 64, 100, 4u * bar + 10u, 4u * bar + 40u});

  uint32_t windowStart = 0;
  uint32_t windowLength = 0;
  uint8_t windowBars = 0;
  TEST_ASSERT_TRUE(DisplayWindowUtils::resolveDetailedPaintWindow(
      loopLength, DisplayWindowUtils::kMaxDetailedWindowBars, 0, 2u * bar, true, windowStart,
      windowLength, windowBars));
  TEST_ASSERT_EQUAL_UINT32(0u, windowStart);
  TEST_ASSERT_EQUAL_UINT32(loopLength, windowLength);

  const NoteUtils::DisplayNoteVec filtered =
      DisplayWindowUtils::filterDisplayNotesByWindowInclusion(notes, windowStart, windowLength,
                                                             loopLength);
  assertSameDisplayNoteIdentitySet(notes, filtered);
}

void test_long_loop_window_inclusion_matches_pre_stage_one_window() {
  const uint32_t bar = Config::TICKS_PER_BAR;
  const uint32_t loopLength = 32u * bar;
  const uint32_t playhead = 16u * bar;
  uint32_t windowStart = 0;
  uint32_t windowLength = 0;
  uint8_t windowBars = 0;
  TEST_ASSERT_TRUE(DisplayWindowUtils::resolveDetailedPaintWindow(
      loopLength, DisplayWindowUtils::kMaxDetailedWindowBars, 0, playhead, true, windowStart,
      windowLength, windowBars));
  const uint32_t expectedStart =
      DisplayWindowUtils::resolveCenteredWindowStart(playhead, 16u * bar, loopLength);
  TEST_ASSERT_EQUAL_UINT32(expectedStart, windowStart);
  TEST_ASSERT_EQUAL_UINT32(16u * bar, windowLength);

  NoteUtils::DisplayNoteVec notes;
  notes.push_back({10, 60, 100, expectedStart + 24u, expectedStart + 72u});
  notes.push_back({11, 61, 100, 0, 48});
  notes.push_back({12, 62, 100, expectedStart + 16u * bar - 12u, expectedStart + 16u * bar + 24u});

  const NoteUtils::DisplayNoteVec filtered =
      DisplayWindowUtils::filterDisplayNotesByWindowInclusion(notes, windowStart, windowLength,
                                                             loopLength);
  const NoteUtils::DisplayNoteVec expected =
      DisplayWindowUtils::filterDisplayNotesByWindowInclusion(notes, expectedStart, 16u * bar,
                                                             loopLength);
  assertSameDisplayNoteIdentitySet(expected, filtered);
  TEST_ASSERT_EQUAL(2u, filtered.size());
  TEST_ASSERT_EQUAL_UINT32(10u, filtered[0].noteId);
  TEST_ASSERT_EQUAL_UINT32(12u, filtered[1].noteId);
}

DisplayWindowUtils::DrawSourceGatherKey makeDrawSourceTestKey(uint32_t windowStart,
                                                              uint32_t windowLength,
                                                              uint32_t playheadBar) {
  DisplayWindowUtils::DrawSourceGatherKey key;
  key.slot = 0;
  key.track = 4;
  key.windowStart = windowStart;
  key.windowLength = windowLength;
  key.playbackRevision = 3;
  key.captureDisplayRevision = 2;
  key.capturePreviewRevision = 8;
  key.visualCacheRevision = 5;
  key.publishedSnapshotRevision = 3;
  key.publishedSnapshotIdentity = reinterpret_cast<const void*>(0xA);
  key.playheadBar = playheadBar;
  key.valid = 1;
  return key;
}

void test_reuse_committed_draw_source_when_keys_match() {
  const uint32_t bar = Config::TICKS_PER_BAR;
  const DisplayWindowUtils::DrawSourceGatherKey last = makeDrawSourceTestKey(0, 16u * bar, 2);
  DisplayWindowUtils::DrawSourceGatherKey current = last;
  TEST_ASSERT_EQUAL(
      static_cast<int>(DisplayWindowUtils::DrawSourceReuseKind::ReuseCommittedDrawSource),
      static_cast<int>(DisplayWindowUtils::selectDrawSourceReuseKind(last, current, bar)));
}

void test_reuse_committed_draw_source_ignores_playhead_tick_same_bar() {
  const uint32_t bar = Config::TICKS_PER_BAR;
  const DisplayWindowUtils::DrawSourceGatherKey last = makeDrawSourceTestKey(0, 4u * bar, 1);
  DisplayWindowUtils::DrawSourceGatherKey current = last;
  current.windowStart = 0;
  current.playheadBar = 1;
  TEST_ASSERT_EQUAL(
      static_cast<int>(DisplayWindowUtils::DrawSourceReuseKind::ReuseCommittedDrawSource),
      static_cast<int>(DisplayWindowUtils::selectDrawSourceReuseKind(last, current, bar)));
}

void test_reuse_committed_draw_source_when_playhead_bar_changes() {
  const uint32_t bar = Config::TICKS_PER_BAR;
  const DisplayWindowUtils::DrawSourceGatherKey last = makeDrawSourceTestKey(0, 4u * bar, 1);
  DisplayWindowUtils::DrawSourceGatherKey current = last;
  current.playheadBar = 2;
  TEST_ASSERT_EQUAL(
      static_cast<int>(DisplayWindowUtils::DrawSourceReuseKind::ReuseCommittedDrawSource),
      static_cast<int>(DisplayWindowUtils::selectDrawSourceReuseKind(last, current, bar)));
}

void test_slide_committed_draw_source_exactly_one_bar() {
  const uint32_t bar = Config::TICKS_PER_BAR;
  const DisplayWindowUtils::DrawSourceGatherKey last = makeDrawSourceTestKey(4u * bar, 16u * bar, 12);
  DisplayWindowUtils::DrawSourceGatherKey current = last;
  current.windowStart = 5u * bar;
  current.playheadBar = 13;
  TEST_ASSERT_EQUAL(
      static_cast<int>(DisplayWindowUtils::DrawSourceReuseKind::SlideCommittedDrawSource),
      static_cast<int>(DisplayWindowUtils::selectDrawSourceReuseKind(last, current, bar)));
  current.windowStart = 3u * bar;
  current.playheadBar = 11;
  TEST_ASSERT_EQUAL(
      static_cast<int>(DisplayWindowUtils::DrawSourceReuseKind::SlideCommittedDrawSource),
      static_cast<int>(DisplayWindowUtils::selectDrawSourceReuseKind(last, current, bar)));
}

void test_rebuild_committed_draw_source_two_bar_jump() {
  const uint32_t bar = Config::TICKS_PER_BAR;
  const DisplayWindowUtils::DrawSourceGatherKey last = makeDrawSourceTestKey(4u * bar, 16u * bar, 12);
  DisplayWindowUtils::DrawSourceGatherKey current = last;
  current.windowStart = 6u * bar;
  TEST_ASSERT_EQUAL(
      static_cast<int>(DisplayWindowUtils::DrawSourceReuseKind::RebuildCommittedDrawSource),
      static_cast<int>(DisplayWindowUtils::selectDrawSourceReuseKind(last, current, bar)));
}

void test_rebuild_committed_draw_source_on_committed_revision() {
  const uint32_t bar = Config::TICKS_PER_BAR;
  const DisplayWindowUtils::DrawSourceGatherKey last = makeDrawSourceTestKey(0, 16u * bar, 2);
  DisplayWindowUtils::DrawSourceGatherKey current = last;
  current.playbackRevision = last.playbackRevision + 1;
  TEST_ASSERT_EQUAL(
      static_cast<int>(DisplayWindowUtils::DrawSourceReuseKind::RebuildCommittedDrawSource),
      static_cast<int>(DisplayWindowUtils::selectDrawSourceReuseKind(last, current, bar)));
  current = last;
  current.visualCacheRevision = last.visualCacheRevision + 1;
  TEST_ASSERT_EQUAL(
      static_cast<int>(DisplayWindowUtils::DrawSourceReuseKind::RebuildCommittedDrawSource),
      static_cast<int>(DisplayWindowUtils::selectDrawSourceReuseKind(last, current, bar)));
  current = last;
  current.publishedSnapshotRevision = last.publishedSnapshotRevision + 1;
  TEST_ASSERT_EQUAL(
      static_cast<int>(DisplayWindowUtils::DrawSourceReuseKind::RebuildCommittedDrawSource),
      static_cast<int>(DisplayWindowUtils::selectDrawSourceReuseKind(last, current, bar)));
  current = last;
  current.publishedSnapshotIdentity = reinterpret_cast<const void*>(0xB);
  TEST_ASSERT_EQUAL(
      static_cast<int>(DisplayWindowUtils::DrawSourceReuseKind::RebuildCommittedDrawSource),
      static_cast<int>(DisplayWindowUtils::selectDrawSourceReuseKind(last, current, bar)));
  current = last;
  current.snapshotCurrent = 1;
  TEST_ASSERT_EQUAL(
      static_cast<int>(DisplayWindowUtils::DrawSourceReuseKind::RebuildCommittedDrawSource),
      static_cast<int>(DisplayWindowUtils::selectDrawSourceReuseKind(last, current, bar)));
}

void test_rebuild_committed_draw_source_on_snapshot_identity_same_revision() {
  const uint32_t bar = Config::TICKS_PER_BAR;
  DisplayWindowUtils::DrawSourceGatherKey last = makeDrawSourceTestKey(0, 16u * bar, 2);
  last.publishedSnapshotIdentity = reinterpret_cast<const void*>(0xA);
  last.snapshotCurrent = 1;
  DisplayWindowUtils::DrawSourceGatherKey current = last;
  // Same playback / published revision, replaced root identity → rebuild (4.6 Stage 2).
  current.publishedSnapshotIdentity = reinterpret_cast<const void*>(0xB);
  TEST_ASSERT_TRUE(last.playbackRevision == current.playbackRevision);
  TEST_ASSERT_TRUE(last.publishedSnapshotRevision == current.publishedSnapshotRevision);
  TEST_ASSERT_FALSE(DisplayWindowUtils::drawSourceCommittedSourceKeysMatch(last, current));
  TEST_ASSERT_EQUAL(
      static_cast<int>(DisplayWindowUtils::DrawSourceReuseKind::RebuildCommittedDrawSource),
      static_cast<int>(DisplayWindowUtils::selectDrawSourceReuseKind(last, current, bar)));
}

void test_reuse_committed_draw_source_when_only_overlay_keys_change() {
  const uint32_t bar = Config::TICKS_PER_BAR;
  const DisplayWindowUtils::DrawSourceGatherKey last = makeDrawSourceTestKey(0, 16u * bar, 2);
  DisplayWindowUtils::DrawSourceGatherKey current = last;
  current.pendingChangeGeneration = 11;
  TEST_ASSERT_TRUE(DisplayWindowUtils::drawSourceCommittedSourceKeysMatch(last, current));
  TEST_ASSERT_FALSE(DisplayWindowUtils::drawSourceOverlayKeysMatch(last, current));
  TEST_ASSERT_EQUAL(
      static_cast<int>(DisplayWindowUtils::DrawSourceReuseKind::ReuseCommittedDrawSource),
      static_cast<int>(DisplayWindowUtils::selectDrawSourceReuseKind(last, current, bar)));
  current = last;
  current.capturePreviewRevision = last.capturePreviewRevision + 1;
  TEST_ASSERT_EQUAL(
      static_cast<int>(DisplayWindowUtils::DrawSourceReuseKind::ReuseCommittedDrawSource),
      static_cast<int>(DisplayWindowUtils::selectDrawSourceReuseKind(last, current, bar)));
  current = last;
  current.sealedPreviewCount = 1;
  TEST_ASSERT_EQUAL(
      static_cast<int>(DisplayWindowUtils::DrawSourceReuseKind::ReuseCommittedDrawSource),
      static_cast<int>(DisplayWindowUtils::selectDrawSourceReuseKind(last, current, bar)));
  current = last;
  current.sealedCompanionOverlay = 1;
  TEST_ASSERT_EQUAL(
      static_cast<int>(DisplayWindowUtils::DrawSourceReuseKind::ReuseCommittedDrawSource),
      static_cast<int>(DisplayWindowUtils::selectDrawSourceReuseKind(last, current, bar)));
}

void test_pending_paint_representation_and_overview_source() {
  TEST_ASSERT_TRUE(DisplayWindowUtils::shouldPaintPendingDrawSourceRepresentation(true, true, false));
  TEST_ASSERT_TRUE(DisplayWindowUtils::shouldPaintPendingDrawSourceRepresentation(true, false, true));
  TEST_ASSERT_FALSE(DisplayWindowUtils::shouldPaintPendingDrawSourceRepresentation(true, false, false));
  TEST_ASSERT_FALSE(DisplayWindowUtils::shouldPaintPendingDrawSourceRepresentation(false, true, true));
  TEST_ASSERT_TRUE(DisplayWindowUtils::shouldUseCleanVisualCacheForOverview(true, false));
  TEST_ASSERT_FALSE(DisplayWindowUtils::shouldUseCleanVisualCacheForOverview(true, true));
  TEST_ASSERT_FALSE(DisplayWindowUtils::shouldUseCleanVisualCacheForOverview(false, false));
}

void test_deferred_wrap_append_source_identity() {
  TEST_ASSERT_TRUE(DisplayWindowUtils::shouldApplyDeferredVisualCacheWrapAppend(true, 4, 4));
  TEST_ASSERT_FALSE(DisplayWindowUtils::shouldApplyDeferredVisualCacheWrapAppend(true, 4, 5));
  TEST_ASSERT_FALSE(DisplayWindowUtils::shouldApplyDeferredVisualCacheWrapAppend(false, 4, 4));
  TEST_ASSERT_TRUE(DisplayWindowUtils::visualCacheIsCurrentSourceRevision(5, 5));
  TEST_ASSERT_FALSE(DisplayWindowUtils::visualCacheIsCurrentSourceRevision(4, 5));
  const void* identityA = reinterpret_cast<const void*>(0xA);
  const void* identityB = reinterpret_cast<const void*>(0xB);
  TEST_ASSERT_TRUE(DisplayWindowUtils::visualCacheIsCurrentPublishedSnapshot(5, 5, identityA,
                                                                             identityA));
  TEST_ASSERT_FALSE(DisplayWindowUtils::visualCacheIsCurrentPublishedSnapshot(5, 5, identityA,
                                                                              identityB));
  TEST_ASSERT_FALSE(DisplayWindowUtils::visualCacheIsCurrentPublishedSnapshot(4, 5, identityA,
                                                                              identityA));
  TEST_ASSERT_FALSE(
      DisplayWindowUtils::visualCacheIsCurrentPublishedSnapshot(5, 5, nullptr, nullptr));
  TEST_ASSERT_FALSE(
      DisplayWindowUtils::visualCacheIsCurrentPublishedSnapshot(5, 5, nullptr, identityA));
}

void test_noteon_shorten_paint_copy_keeps_compose_original_end() {
  NoteUtils::DisplayNoteVec compose;
  compose.push_back({40, 63, 90, 768, 1440});
  NoteUtils::DisplayNoteVec paint = compose;
  paint[0].endTick = 1103;
  const DisplayWindowUtils::PaintedPendingShorten described =
      DisplayWindowUtils::describePaintedPendingShorten(compose, paint, 40, 1103);
  TEST_ASSERT_TRUE(described.composeHasTarget);
  TEST_ASSERT_EQUAL_UINT32(1440u, described.composeEndTick);
  TEST_ASSERT_TRUE(described.paintHasTarget);
  TEST_ASSERT_TRUE(described.paintHasShortenedEnd);
  TEST_ASSERT_EQUAL_UINT32(1103u, described.paintEndTick);
}

void test_noteon_shorten_authoritative_absence_omits_compose_and_paint() {
  NoteUtils::DisplayNoteVec compose;
  NoteUtils::DisplayNoteVec visual;
  visual.push_back({40, 63, 90, 768, 1440});
  NoteUtils::DisplayNoteVec authoritative;
  const NoteId shortenIds[] = {40};
  const DisplayWindowUtils::PendingShortenRecoveryCounts absence =
      DisplayWindowUtils::recoverMissingPendingShortenRows(compose, visual, shortenIds, 1,
                                                           authoritative);
  TEST_ASSERT_EQUAL_UINT32(0u, absence.recovered);
  TEST_ASSERT_EQUAL_UINT32(1u, absence.authoritativeAbsence);
  TEST_ASSERT_TRUE(compose.empty());
  const DisplayWindowUtils::PaintedPendingShorten described =
      DisplayWindowUtils::describePaintedPendingShorten(compose, compose, 40, 1103);
  TEST_ASSERT_FALSE(described.composeHasTarget);
  TEST_ASSERT_FALSE(described.paintHasTarget);
  TEST_ASSERT_FALSE(described.paintHasShortenedEnd);
}

void test_noteon_hide_absence_is_not_recovered() {
  NoteUtils::DisplayNoteVec compose;
  NoteUtils::DisplayNoteVec visual;
  visual.push_back({40, 63, 90, 768, 1440});
  NoteUtils::DisplayNoteVec authoritative;
  authoritative.push_back({40, 63, 90, 768, 1440});
  const DisplayWindowUtils::PendingShortenRecoveryCounts hideCounts =
      DisplayWindowUtils::recoverMissingPendingShortenRows(compose, visual, nullptr, 0,
                                                           authoritative);
  TEST_ASSERT_EQUAL_UINT32(0u, hideCounts.recovered);
  TEST_ASSERT_TRUE(compose.empty());
}

void test_pending_shorten_source_classify_and_recover_212700() {
  TEST_ASSERT_EQUAL(
      static_cast<int>(DisplayWindowUtils::PendingShortenSourceKind::NotNeeded),
      static_cast<int>(DisplayWindowUtils::classifyPendingShortenSource(true, true, true)));
  TEST_ASSERT_EQUAL(
      static_cast<int>(DisplayWindowUtils::PendingShortenSourceKind::RecoverAuthoritative),
      static_cast<int>(DisplayWindowUtils::classifyPendingShortenSource(false, false, true)));
  TEST_ASSERT_EQUAL(
      static_cast<int>(DisplayWindowUtils::PendingShortenSourceKind::RecoverAuthoritative),
      static_cast<int>(DisplayWindowUtils::classifyPendingShortenSource(false, true, true)));
  TEST_ASSERT_EQUAL(
      static_cast<int>(DisplayWindowUtils::PendingShortenSourceKind::AuthoritativeAbsence),
      static_cast<int>(DisplayWindowUtils::classifyPendingShortenSource(false, true, false)));
  TEST_ASSERT_EQUAL(
      static_cast<int>(DisplayWindowUtils::PendingShortenSourceKind::NotNeeded),
      static_cast<int>(DisplayWindowUtils::classifyPendingShortenSource(false, false, false)));

  NoteUtils::DisplayNoteVec compose;
  NoteUtils::DisplayNoteVec visual;
  visual.push_back({84, 84, 100, 4560, 4608});
  NoteUtils::DisplayNoteVec authoritative;
  authoritative.push_back({84, 84, 100, 4560, 4608});
  const NoteId shortenIds[] = {84};
  const DisplayWindowUtils::PendingShortenRecoveryCounts recovered =
      DisplayWindowUtils::recoverMissingPendingShortenRows(compose, visual, shortenIds, 1,
                                                           authoritative);
  TEST_ASSERT_EQUAL_UINT32(1u, recovered.recovered);
  TEST_ASSERT_EQUAL_UINT32(0u, recovered.authoritativeAbsence);
  TEST_ASSERT_EQUAL(1u, compose.size());
  TEST_ASSERT_EQUAL(84, compose[0].noteId);
  TEST_ASSERT_EQUAL_UINT32(4560u, compose[0].startTick);
  TEST_ASSERT_EQUAL_UINT32(4608u, compose[0].endTick);

  NoteUtils::DisplayNoteVec wrapCompose;
  NoteUtils::DisplayNoteVec emptyVisual;
  const DisplayWindowUtils::PendingShortenRecoveryCounts wrapRecovered =
      DisplayWindowUtils::recoverMissingPendingShortenRows(wrapCompose, emptyVisual, shortenIds, 1,
                                                           authoritative);
  TEST_ASSERT_EQUAL_UINT32(1u, wrapRecovered.recovered);
  TEST_ASSERT_EQUAL_UINT32(0u, wrapRecovered.authoritativeAbsence);
  TEST_ASSERT_EQUAL(1u, wrapCompose.size());
  TEST_ASSERT_EQUAL(84, wrapCompose[0].noteId);
  TEST_ASSERT_EQUAL_UINT32(4560u, wrapCompose[0].startTick);
  TEST_ASSERT_EQUAL_UINT32(4608u, wrapCompose[0].endTick);

  NoteUtils::DisplayNoteVec absentCompose;
  NoteUtils::DisplayNoteVec emptyAuthoritative;
  const DisplayWindowUtils::PendingShortenRecoveryCounts absence =
      DisplayWindowUtils::recoverMissingPendingShortenRows(absentCompose, visual, shortenIds, 1,
                                                           emptyAuthoritative);
  TEST_ASSERT_EQUAL_UINT32(0u, absence.recovered);
  TEST_ASSERT_EQUAL_UINT32(1u, absence.authoritativeAbsence);
  TEST_ASSERT_TRUE(absentCompose.empty());

  NoteUtils::DisplayNoteVec hideCompose;
  const DisplayWindowUtils::PendingShortenRecoveryCounts hideCounts =
      DisplayWindowUtils::recoverMissingPendingShortenRows(hideCompose, visual, nullptr, 0,
                                                           authoritative);
  TEST_ASSERT_EQUAL_UINT32(0u, hideCounts.recovered);
  TEST_ASSERT_TRUE(hideCompose.empty());
}

void test_recovered_shorten_joins_prefix_not_overlay_suffix_192322() {
  NoteUtils::DisplayNoteVec compose;
  compose.push_back({150, 69, 90, 48, 96});
  size_t committedEnd = 0;
  DisplayWindowUtils::admitRecoveredCommittedShortenRow(compose, committedEnd,
                                                        NoteUtils::DisplayNote{32, 69, 90, 48, 200});
  TEST_ASSERT_EQUAL(2u, compose.size());
  TEST_ASSERT_EQUAL(1u, committedEnd);
  TEST_ASSERT_EQUAL(32, compose[0].noteId);
  TEST_ASSERT_EQUAL(150, compose[1].noteId);
  TEST_ASSERT_EQUAL_UINT32(96u, compose[1].endTick);

  NoteUtils::DisplayNoteVec visual;
  NoteUtils::DisplayNoteVec authoritative;
  authoritative.push_back({84, 84, 100, 4560, 4608});
  NoteUtils::DisplayNoteVec sealedCompose;
  sealedCompose.push_back({150, 69, 90, 48, 96});
  size_t sealedEnd = 0;
  const NoteId shortenIds[] = {84};
  const DisplayWindowUtils::PendingShortenRecoveryCounts recovered =
      DisplayWindowUtils::recoverMissingPendingShortenRows(sealedCompose, visual, shortenIds, 1,
                                                           authoritative, &sealedEnd);
  TEST_ASSERT_EQUAL_UINT32(1u, recovered.recovered);
  TEST_ASSERT_EQUAL(1u, sealedEnd);
  TEST_ASSERT_EQUAL(84, sealedCompose[0].noteId);
  TEST_ASSERT_EQUAL(150, sealedCompose[1].noteId);
}

void test_one_bar_slide_keeps_spanning_note_and_swaps_edge_bars() {
  const uint32_t bar = Config::TICKS_PER_BAR;
  const uint32_t loopLength = 32u * bar;
  NoteUtils::DisplayNoteVec notes;
  notes.push_back({10, 60, 100, 0, 48});
  notes.push_back({11, 61, 100, 2u * bar, 4u * bar + 24});
  notes.push_back({12, 62, 100, 16u * bar, 16u * bar + 48});

  const uint32_t firstStart = 0;
  const uint32_t firstLength = 16u * bar;
  const uint32_t slidStart = bar;
  const uint32_t slidLength = 16u * bar;

  const NoteUtils::DisplayNoteVec firstWindow =
      DisplayWindowUtils::filterDisplayNotesByWindowInclusion(notes, firstStart, firstLength,
                                                             loopLength);
  NoteUtils::DisplayNoteVec slidFromFirst =
      DisplayWindowUtils::filterDisplayNotesByWindowInclusion(firstWindow, slidStart, slidLength,
                                                             loopLength);
  for (const NoteUtils::DisplayNote& note : notes) {
    if (!DisplayWindowUtils::noteIntersectsWindow(note.startTick, note.endTick, 16u * bar, bar,
                                                  loopLength)) {
      continue;
    }
    bool present = false;
    for (const NoteUtils::DisplayNote& existing : slidFromFirst) {
      if (existing.noteId == note.noteId) {
        present = true;
        break;
      }
    }
    if (!present) {
      slidFromFirst.push_back(note);
    }
  }

  const NoteUtils::DisplayNoteVec fullSlid =
      DisplayWindowUtils::filterDisplayNotesByWindowInclusion(notes, slidStart, slidLength,
                                                             loopLength);
  assertSameDisplayNoteIdentitySet(fullSlid, slidFromFirst);
  bool hasLeaving = false;
  bool hasSpanning = false;
  bool hasEntering = false;
  for (const NoteUtils::DisplayNote& note : slidFromFirst) {
    if (note.noteId == 10) {
      hasLeaving = true;
    }
    if (note.noteId == 11) {
      hasSpanning = true;
    }
    if (note.noteId == 12) {
      hasEntering = true;
    }
  }
  TEST_ASSERT_FALSE(hasLeaving);
  TEST_ASSERT_TRUE(hasSpanning);
  TEST_ASSERT_TRUE(hasEntering);
}

void test_display_frame_admission_sequences() {
  using DisplayWindowUtils::DisplayFramePhase;
  using DisplayWindowUtils::admitDisplayFrameTurn;
  using DisplayWindowUtils::advanceDisplayFramePhase;
  using DisplayWindowUtils::displayFramePhaseToRun;

  TEST_ASSERT_FALSE(admitDisplayFrameTurn(DisplayFramePhase::Idle, false, false));

  TEST_ASSERT_TRUE(admitDisplayFrameTurn(DisplayFramePhase::Idle, true, false));
  DisplayFramePhase pending = DisplayFramePhase::Idle;
  DisplayFramePhase ran = displayFramePhaseToRun(pending);
  TEST_ASSERT_EQUAL(static_cast<int>(DisplayFramePhase::Resolve), static_cast<int>(ran));
  auto advanced = advanceDisplayFramePhase(ran);
  TEST_ASSERT_EQUAL(static_cast<int>(DisplayFramePhase::Draw), static_cast<int>(advanced.pending));
  TEST_ASSERT_FALSE(advanced.frameComplete);
  pending = advanced.pending;

  TEST_ASSERT_TRUE(admitDisplayFrameTurn(pending, false, false));
  ran = displayFramePhaseToRun(pending);
  TEST_ASSERT_EQUAL(static_cast<int>(DisplayFramePhase::Draw), static_cast<int>(ran));
  advanced = advanceDisplayFramePhase(ran);
  TEST_ASSERT_EQUAL(static_cast<int>(DisplayFramePhase::Transfer), static_cast<int>(advanced.pending));
  TEST_ASSERT_FALSE(advanced.frameComplete);
  pending = advanced.pending;

  TEST_ASSERT_TRUE(admitDisplayFrameTurn(pending, false, false));
  ran = displayFramePhaseToRun(pending);
  TEST_ASSERT_EQUAL(static_cast<int>(DisplayFramePhase::Transfer), static_cast<int>(ran));
  advanced = advanceDisplayFramePhase(ran);
  TEST_ASSERT_EQUAL(static_cast<int>(DisplayFramePhase::Idle), static_cast<int>(advanced.pending));
  TEST_ASSERT_TRUE(advanced.frameComplete);
  pending = advanced.pending;

  TEST_ASSERT_FALSE(admitDisplayFrameTurn(pending, false, false));
  TEST_ASSERT_TRUE(admitDisplayFrameTurn(pending, true, false));
  ran = displayFramePhaseToRun(pending);
  TEST_ASSERT_EQUAL(static_cast<int>(DisplayFramePhase::Resolve), static_cast<int>(ran));
}

void test_display_frame_note_edit_request_uses_same_phases() {
  using DisplayWindowUtils::DisplayFramePhase;
  using DisplayWindowUtils::admitDisplayFrameTurn;
  using DisplayWindowUtils::advanceDisplayFramePhase;
  using DisplayWindowUtils::displayFramePhaseToRun;

  TEST_ASSERT_TRUE(admitDisplayFrameTurn(DisplayFramePhase::Idle, false, true));
  DisplayFramePhase pending = DisplayFramePhase::Idle;
  for (const DisplayFramePhase expected :
       {DisplayFramePhase::Resolve, DisplayFramePhase::Draw, DisplayFramePhase::Transfer}) {
    TEST_ASSERT_TRUE(admitDisplayFrameTurn(pending, false, true));
    const DisplayFramePhase ran = displayFramePhaseToRun(pending);
    TEST_ASSERT_EQUAL(static_cast<int>(expected), static_cast<int>(ran));
    const auto advanced = advanceDisplayFramePhase(ran);
    pending = advanced.pending;
    if (expected == DisplayFramePhase::Transfer) {
      TEST_ASSERT_TRUE(advanced.frameComplete);
    } else {
      TEST_ASSERT_FALSE(advanced.frameComplete);
    }
  }
  TEST_ASSERT_EQUAL(static_cast<int>(DisplayFramePhase::Idle), static_cast<int>(pending));
}

void test_display_frame_invalidate_during_draw_coalesces() {
  using DisplayWindowUtils::DisplayFramePhase;
  using DisplayWindowUtils::advanceDisplayFramePhase;
  using DisplayWindowUtils::noteDisplayFrameInvalidate;
  using DisplayWindowUtils::shouldClearResolvedPaintNotesNow;

  DisplayFramePhase pending = DisplayFramePhase::Draw;
  bool coalesce = false;
  noteDisplayFrameInvalidate(pending, coalesce);
  TEST_ASSERT_TRUE(coalesce);
  TEST_ASSERT_FALSE(shouldClearResolvedPaintNotesNow(pending));
  TEST_ASSERT_EQUAL(static_cast<int>(DisplayFramePhase::Draw), static_cast<int>(pending));

  auto advanced = advanceDisplayFramePhase(DisplayFramePhase::Draw);
  pending = advanced.pending;
  TEST_ASSERT_FALSE(advanced.frameComplete);
  advanced = advanceDisplayFramePhase(DisplayFramePhase::Transfer);
  pending = advanced.pending;
  TEST_ASSERT_TRUE(advanced.frameComplete);
  TEST_ASSERT_TRUE(shouldClearResolvedPaintNotesNow(pending));
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
  RUN_TEST(test_capture_playhead_tails_are_paint_only);
  RUN_TEST(test_overdub_playhead_tails_apply_when_over_budget);
  RUN_TEST(test_should_allow_capture_playhead_wrap_continuation_stage2_truth_table);
  RUN_TEST(test_should_complete_stopped_draw_source_wrap_held_stage3b_truth_table);
  RUN_TEST(test_prefer_preserved_overdub_stop_handoff_while_cache_dirty);
  RUN_TEST(test_resolve_note_edit_committed_base_from_snapshot);
  RUN_TEST(test_select_note_edit_projection_cache_reuse_fingerprint_does_not_regather);
  RUN_TEST(test_preview_revision_is_paint_key_not_committed_base_key);
  RUN_TEST(test_rebuild_note_edit_committed_base_on_snapshot_identity_same_revision);
  RUN_TEST(test_select_note_edit_committed_base_kind_capture_vs_playing);
  RUN_TEST(test_hold_overdub_stop_handoff_until_current_snapshot);
  RUN_TEST(test_preserve_composed_notes_on_overdub_wrap_invalidate);
  RUN_TEST(test_should_hold_composed_notes_on_overdub_wrap_revision);
  RUN_TEST(test_committed_display_visual_cache_authoritative);
  RUN_TEST(test_prefer_incremental_committed_display_includes_stopped);
  RUN_TEST(test_clamp_non_wrap_display_note_bar_ticks_frontier_overflow);
  RUN_TEST(test_map_display_note_bar_ticks_frontier_equals_length_stays_at_end);
  RUN_TEST(test_filter_display_notes_to_window_clamps_frontier_equals_length);
  RUN_TEST(test_overdub_committed_window_cache_rejects_zero_committed_count);
  RUN_TEST(test_should_hold_overdub_committed_handoff_requires_gather_coverage);
  RUN_TEST(test_should_hold_dirty_overdub_layer_before_window_gather);
  RUN_TEST(test_should_hold_dirty_visual_cache_until_idle);
  RUN_TEST(test_should_assign_grown_visual_cache_over_overdub_hold);
  RUN_TEST(test_snapshot_backed_display_ready);
  RUN_TEST(test_should_gather_published_snapshot_for_draw_source);
  RUN_TEST(test_visual_cache_reusable_as_committed_draw_source);
  RUN_TEST(test_published_snapshot_is_complete_draw_source);
  RUN_TEST(test_should_gather_published_snapshot_for_complete_draw_source);
  RUN_TEST(test_sealed_companion_hide_waits_for_replacement);
  RUN_TEST(test_sealed_companion_transform_already_represented);
  RUN_TEST(test_sealed_capture_preview_row_covered_by_visual_cache);
  RUN_TEST(test_should_retain_record_stop_handoff_seed);
  RUN_TEST(test_should_retain_unpublished_visual_cache_over_stale_gather);
  RUN_TEST(test_should_retain_visual_cache_over_incomplete_current_snapshot_gather);
  RUN_TEST(test_should_write_live_display_notes_on_transport_stop);
  RUN_TEST(test_should_union_sealed_capture_preview_for_draw_source);
  RUN_TEST(test_should_adopt_composed_notes_over_visual_cache);
  RUN_TEST(test_keep_shortest_end_at_pitch_start);
  RUN_TEST(test_closed_capture_preview_identity_requires_closed_preview_row);
  RUN_TEST(test_keep_shortest_preserves_distinct_valid_note_ids);
  RUN_TEST(test_clamp_display_note_resize_never_grows);
  RUN_TEST(test_sync_live_display_compose_counts_clamps_stale_layers);
  RUN_TEST(test_keep_shortest_end_on_committed_prefix_preserves_capture_suffix);
  RUN_TEST(test_should_keep_visual_cache_over_smaller_full_loop_slice);
  RUN_TEST(test_overdub_committed_promote_to_full_visual_cache);
  RUN_TEST(test_paint_window_inside_gather_detects_follow_exit);
  RUN_TEST(test_visual_cache_covers_window_uses_clean_neighborhood);
  RUN_TEST(test_visual_cache_covers_window_rejects_first_slice_prefix);
  RUN_TEST(test_visual_cache_covers_window_follow_lookahead_requires_next_bar);
  RUN_TEST(test_follow_window_filter_includes_newly_exposed_bars);
  RUN_TEST(test_expand_window_with_margin_clamps_to_loop);
  RUN_TEST(test_expand_window_with_margin_includes_short_loop_tail);
  RUN_TEST(test_adopt_partial_visual_cache_marks_uncovered_bars_dirty);
  RUN_TEST(test_adopt_partial_visual_cache_clears_dirty_when_loop_fits_window);
  RUN_TEST(test_overview_band_mask_marks_every_bar_the_note_spans);
  RUN_TEST(test_overview_band_mask_accumulates_bands_and_clamps_range);
  RUN_TEST(test_overview_band_for_note_covers_full_midi_range);
  RUN_TEST(test_select_draw_source_for_bar_live_capture_wins);
  RUN_TEST(test_select_draw_source_for_bar_snapshot_when_current);
  RUN_TEST(test_select_draw_source_for_bar_capture_active_unions_whole_window);
  RUN_TEST(test_visual_cache_gather_covers_playback_revision_rejects_overlay);
  RUN_TEST(test_select_draw_source_for_bar_visual_cache_fallback);
  RUN_TEST(test_upsert_display_note_at_pitch_start_replaces_stale_committed_row);
  RUN_TEST(test_upsert_display_note_at_pitch_start_appends_when_no_match);
  RUN_TEST(test_stopped_sealed_upsert_skips_longer_same_pitch_start_154310);
  RUN_TEST(test_stopped_playhead_tail_takes_closed_capture_off_153848);
  RUN_TEST(test_stopped_playhead_tail_does_not_take_sealed_remnant_154310);
  RUN_TEST(test_stopped_closed_off_takes_sealed_165050_52);
  RUN_TEST(test_stopped_closed_off_takes_sealed_165050_48);
  RUN_TEST(test_capture_sealed_upsert_keeps_longer_suffix_150650);
  RUN_TEST(test_overlay_keeps_distinct_valid_ids_at_same_pitch_start_195409);
  RUN_TEST(test_overlay_same_id_replaces_suffix_row);
  RUN_TEST(test_overlay_same_id_keeps_wrap_on_to_loop_end_and_head_191636);
  RUN_TEST(test_overlay_invalid_id_still_replaces_pitch_start);
  RUN_TEST(test_overlay_repeated_sealed_ids_keep_equal_geometry_195409);
  RUN_TEST(test_stopped_sealed_upsert_appends_when_prefix_lacks_pitch_start);
  RUN_TEST(test_upsert_display_note_at_pitch_start_wrap_held_sealed_row_never_replaces);
  RUN_TEST(test_paint_window_inside_gather_tolerates_margin_slide);
  RUN_TEST(test_format_loop_length_bars_info_strip);
  RUN_TEST(test_resolve_detailed_paint_window_covers_four_bar_loop);
  RUN_TEST(test_resolve_detailed_paint_window_centers_thirty_two_bar_loop);
  RUN_TEST(test_short_loop_window_inclusion_keeps_full_identity_set);
  RUN_TEST(test_long_loop_window_inclusion_matches_pre_stage_one_window);
  RUN_TEST(test_reuse_committed_draw_source_when_keys_match);
  RUN_TEST(test_reuse_committed_draw_source_ignores_playhead_tick_same_bar);
  RUN_TEST(test_reuse_committed_draw_source_when_playhead_bar_changes);
  RUN_TEST(test_slide_committed_draw_source_exactly_one_bar);
  RUN_TEST(test_rebuild_committed_draw_source_two_bar_jump);
  RUN_TEST(test_rebuild_committed_draw_source_on_committed_revision);
  RUN_TEST(test_rebuild_committed_draw_source_on_snapshot_identity_same_revision);
  RUN_TEST(test_reuse_committed_draw_source_when_only_overlay_keys_change);
  RUN_TEST(test_pending_paint_representation_and_overview_source);
  RUN_TEST(test_deferred_wrap_append_source_identity);
  RUN_TEST(test_noteon_shorten_paint_copy_keeps_compose_original_end);
  RUN_TEST(test_noteon_shorten_authoritative_absence_omits_compose_and_paint);
  RUN_TEST(test_noteon_hide_absence_is_not_recovered);
  RUN_TEST(test_pending_shorten_source_classify_and_recover_212700);
  RUN_TEST(test_recovered_shorten_joins_prefix_not_overlay_suffix_192322);
  RUN_TEST(test_one_bar_slide_keeps_spanning_note_and_swaps_edge_bars);
  RUN_TEST(test_display_frame_admission_sequences);
  RUN_TEST(test_display_frame_note_edit_request_uses_same_phases);
  RUN_TEST(test_display_frame_invalidate_during_draw_coalesces);
  return UNITY_END();
}
