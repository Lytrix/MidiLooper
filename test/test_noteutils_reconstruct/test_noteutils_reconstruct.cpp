//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include <unity.h>
#include <vector>
#include <algorithm>

// Compile production translation units into this test only (native stub Logger + real NoteUtils).
#include "../../src/Logger.cpp"
#include "../../src/Utils/IntervalProjection.cpp"
#include "../../src/Utils/NoteUtils.cpp"

#include "Utils/NoteUtils.h"
#include "Utils/IntervalProjection.h"
#include "MidiEvent.h"

static void assert_has_note(const std::vector<NoteUtils::DisplayNote>& notes,
                            uint8_t pitch,
                            uint32_t startTick,
                            uint32_t endTick,
                            uint8_t velocity) {
    auto it = std::find_if(notes.begin(), notes.end(), [&](const NoteUtils::DisplayNote& n) {
        return n.note == pitch && n.startTick == startTick && n.endTick == endTick && n.velocity == velocity;
    });
    TEST_ASSERT_TRUE(it != notes.end());
}

void test_project_display_notes_simple_span() {
    constexpr uint32_t loopLength = 48;
    CanonicalNoteSpanVec spans;
    CanonicalNoteSpan span;
    span.noteId = 1;
    span.pitch = 60;
    span.velocity = 100;
    span.interval = TickInterval{10, 41};
    spans.push_back(span);

    const TickInterval window = IntervalProjection::makeFullLoopDisplayWindow(loopLength);
    const ProjectionContext context =
        IntervalProjection::buildDisplayProjectionContext(loopLength, window);
    const NoteUtils::DisplayNoteVec notes =
        IntervalProjection::projectDisplayNotes(spans, context);
    TEST_ASSERT_EQUAL(1u, notes.size());
    assert_has_note(std::vector<NoteUtils::DisplayNote>(notes.begin(), notes.end()), 60, 10, 40,
                    100);
}

void test_project_display_notes_head_tail_split() {
    constexpr uint32_t loopLength = 1536;
    CanonicalNoteSpanVec spans;
    CanonicalNoteSpan span;
    span.noteId = 1;
    span.pitch = 60;
    span.velocity = 100;
    span.interval = TickInterval{1400, 1587};
    span.splitHeadTail = true;
    spans.push_back(span);

    const TickInterval window = IntervalProjection::makeFullLoopDisplayWindow(loopLength);
    const ProjectionContext context =
        IntervalProjection::buildDisplayProjectionContext(loopLength, window);
    const NoteUtils::DisplayNoteVec notes =
        IntervalProjection::projectDisplayNotes(spans, context);
    TEST_ASSERT_EQUAL(2u, notes.size());
    TEST_ASSERT_EQUAL_UINT32(1400u, notes[0].startTick);
    TEST_ASSERT_EQUAL_UINT32(loopLength - 1, notes[0].endTick);
    TEST_ASSERT_EQUAL_UINT32(0u, notes[1].startTick);
    TEST_ASSERT_EQUAL_UINT32(50u, notes[1].endTick);
}

void test_project_display_notes_open_tail_with_playhead() {
    constexpr uint32_t loopLength = 100;
    CanonicalNoteSpanVec spans;
    CanonicalNoteSpan span;
    span.noteId = 1;
    span.pitch = 60;
    span.velocity = 100;
    span.interval = TickInterval{90, static_cast<int32_t>(loopLength)};
    span.isOpen = true;
    spans.push_back(span);

    const TickInterval window = IntervalProjection::makeFullLoopDisplayWindow(loopLength);
    const ProjectionContext context =
        IntervalProjection::buildDisplayProjectionContext(loopLength, window);
    const NoteUtils::DisplayNoteVec closed =
        IntervalProjection::projectDisplayNotes(spans, context);
    TEST_ASSERT_EQUAL(1u, closed.size());
    TEST_ASSERT_EQUAL_UINT32(99u, closed[0].endTick);

    const NoteUtils::DisplayNoteVec live =
        IntervalProjection::projectDisplayNotes(spans, context, 95);
    TEST_ASSERT_EQUAL(1u, live.size());
    TEST_ASSERT_EQUAL_UINT32(95u, live[0].endTick);
}

void test_reconstruct_empty_loop_yields_empty() {
    MidiEventVec ev;
    ev.push_back(MidiEvent::NoteOn(0, 1, 60, 100));
    ev.push_back(MidiEvent::NoteOff(10, 1, 60, 0));
    auto notes = NoteUtils::reconstructNotes(ev, 0);
    TEST_ASSERT_EQUAL(0u, notes.size());
}

void test_reconstruct_discards_note_on_at_or_beyond_loop() {
    MidiEventVec ev;
    ev.push_back(MidiEvent::NoteOn(10, 1, 60, 100));
    ev.push_back(MidiEvent::NoteOn(50, 1, 60, 80)); // 50 >= 48 -> discard
    ev.push_back(MidiEvent::NoteOff(40, 1, 60, 0));
    auto notes = NoteUtils::reconstructNotes(ev, 48);
    TEST_ASSERT_EQUAL(1u, notes.size());
    assert_has_note(notes, 60, 10, 40, 100);
}

void test_reconstruct_wraps_note_off_past_boundary() {
    MidiEventVec ev;
    ev.push_back(MidiEvent::NoteOn(40, 1, 60, 100));
    ev.push_back(MidiEvent::NoteOff(50, 1, 60, 0)); // 50 >= 48 -> wraps to 2
    auto notes = NoteUtils::reconstructNotes(ev, 48);
    TEST_ASSERT_EQUAL(1u, notes.size());
    assert_has_note(notes, 60, 40, 2, 100);
}

void test_reconstruct_lifo_same_pitch() {
    MidiEventVec ev;
    ev.push_back(MidiEvent::NoteOn(0, 1, 60, 100));
    ev.push_back(MidiEvent::NoteOn(10, 1, 60, 80));
    ev.push_back(MidiEvent::NoteOff(20, 1, 60, 0));
    ev.push_back(MidiEvent::NoteOff(30, 1, 60, 0));
    auto notes = NoteUtils::reconstructNotes(ev, 100);
    TEST_ASSERT_EQUAL(2u, notes.size());
    assert_has_note(notes, 60, 10, 20, 80);
    assert_has_note(notes, 60, 0, 30, 100);
}

void test_reconstruct_open_note_to_loop_end() {
    MidiEventVec ev;
    ev.push_back(MidiEvent::NoteOn(90, 1, 60, 100));
    auto notes = NoteUtils::reconstructNotes(ev, 100);
    TEST_ASSERT_EQUAL(1u, notes.size());
    assert_has_note(notes, 60, 90, 99, 100);
}

void test_reconstruct_display_omits_open_tails_when_finish_open_notes_false() {
    MidiEventVec ev;
    ev.push_back(MidiEvent::NoteOn(10, 1, 60, 100));
    ev.push_back(MidiEvent::NoteOff(58, 1, 60, 0));
    ev.push_back(MidiEvent::NoteOn(80, 1, 72, 90));
    const NoteUtils::DisplayNoteVec withTails =
        NoteUtils::reconstructDisplayNotes(ev, 100, false, true);
    TEST_ASSERT_EQUAL(2u, withTails.size());
    const NoteUtils::DisplayNoteVec committed =
        NoteUtils::reconstructDisplayNotes(ev, 100, false, false);
    TEST_ASSERT_EQUAL(1u, committed.size());
    TEST_ASSERT_EQUAL(60, committed[0].note);
    TEST_ASSERT_EQUAL_UINT32(10u, committed[0].startTick);
    TEST_ASSERT_EQUAL_UINT32(58u, committed[0].endTick);
}

void test_reconstruct_dedupes_identical_segments() {
    MidiEventVec ev;
    ev.push_back(MidiEvent::NoteOn(0, 1, 60, 100));
    ev.push_back(MidiEvent::NoteOff(10, 1, 60, 0));
    ev.push_back(MidiEvent::NoteOn(0, 1, 60, 100));
    ev.push_back(MidiEvent::NoteOff(10, 1, 60, 0));
    auto notes = NoteUtils::reconstructNotes(ev, 100);
    TEST_ASSERT_EQUAL(1u, notes.size());
}

void test_reconstruct_dedupes_many_identical_geometry_notes() {
    MidiEventVec ev;
    for (int i = 0; i < 64; ++i) {
        ev.push_back(MidiEvent::NoteOn(0, 1, 60, 100));
        ev.push_back(MidiEvent::NoteOff(10, 1, 60, 0));
    }
    auto notes = NoteUtils::reconstructNotes(ev, 100);
    TEST_ASSERT_EQUAL(1u, notes.size());
    assert_has_note(notes, 60, 0, 10, 100);
}

void test_reconstruct_wrapped_tail_on_head_off() {
    constexpr uint32_t loopLength = 1536;
    MidiEventVec ev;
    ev.push_back(MidiEvent::NoteOff(50, 1, 60, 0));
    ev.push_back(MidiEvent::NoteOn(1400, 1, 60, 100));
    auto notes = NoteUtils::reconstructNotes(ev, loopLength, false);
    TEST_ASSERT_EQUAL(2u, notes.size());
    assert_has_note(notes, 60, 1400, loopLength - 1, 100);
    assert_has_note(notes, 60, 0, 50, 100);
}

void test_reconstruct_wrapped_tail_on_head_off_chronological() {
    constexpr uint32_t loopLength = 1536;
    MidiEventVec ev;
    ev.push_back(MidiEvent::NoteOn(1535, 5, 48, 100));
    ev.push_back(MidiEvent::NoteOff(55, 5, 48, 0));
    auto notes = NoteUtils::reconstructNotes(ev, loopLength, false);
    TEST_ASSERT_EQUAL(2u, notes.size());
    assert_has_note(notes, 48, 1535, loopLength - 1, 100);
    assert_has_note(notes, 48, 0, 55, 100);
}

void test_reconstruct_note_off_at_wrap_zero_is_boundary_end() {
    constexpr uint32_t loopLength = 1536;
    MidiEventVec ev;
    ev.push_back(MidiEvent::NoteOn(loopLength - 96, 5, 48, 100));
    ev.push_back(MidiEvent::NoteOff(0, 5, 48, 0));
    auto notes = NoteUtils::reconstructNotes(ev, loopLength, false);
    TEST_ASSERT_EQUAL(1u, notes.size());
    assert_has_note(notes, 48, loopLength - 96, loopLength - 1, 100);
    for (const auto& n : notes) {
        TEST_ASSERT_FALSE(n.startTick == 0u && n.endTick == 0u);
    }
}

void test_is_wrap_held_open_note_accepts_head_off_at_zero() {
    constexpr uint32_t loopLength = 1536;
    MidiEventVec ev;
    // Tail on
    ev.push_back(MidiEvent::NoteOn(1440, 4, 30, 100));
    // Head off at wrap
    ev.push_back(MidiEvent::NoteOff(0, 4, 30, 0));
    NoteUtils::OpenNoteOn open{30, 100, 1440};
    TEST_ASSERT_TRUE(NoteUtils::isWrapHeldOpenNote(ev, open, loopLength));
}

void test_reconstruct_wrap_with_synthetic_loop_end_before_head_off() {
    constexpr uint32_t loopLength = 1536;
    MidiEventVec ev;
    ev.push_back(MidiEvent::NoteOn(1487, 5, 48, 100));
    ev.push_back(MidiEvent::NoteOn(1535, 5, 48, 90));
    ev.push_back(MidiEvent::NoteOff(1535, 5, 48, 0));
    ev.push_back(MidiEvent::NoteOff(55, 5, 48, 0));
    auto notes = NoteUtils::reconstructNotes(ev, loopLength, false);
    TEST_ASSERT_EQUAL(3u, notes.size());
    assert_has_note(notes, 48, 1487, 1535, 100);
    assert_has_note(notes, 48, 1535, loopLength - 1, 90);
    assert_has_note(notes, 48, 0, 55, 90);
}

void test_reconstruct_tick0_wrap_pair_survives_later_same_pitch() {
    constexpr uint32_t loopLength = 3072;
    MidiEventVec ev;
    ev.push_back(MidiEvent::NoteOn(2976, 4, 30, 100));
    ev.push_back(MidiEvent::NoteOff(0, 4, 30, 0));
    ev.push_back(MidiEvent::NoteOn(672, 4, 30, 100));
    ev.push_back(MidiEvent::NoteOff(768, 4, 30, 0));
    std::stable_sort(ev.begin(), ev.end(),
                     [](const MidiEvent& a, const MidiEvent& b) { return a.tick < b.tick; });
    auto notes = NoteUtils::reconstructNotes(ev, loopLength, false);
    assert_has_note(notes, 30, 2976, loopLength - 1, 100);
    assert_has_note(notes, 30, 672, 768, 100);
}

void test_reconstruct_wrap_pair_blocked_by_intervening_note_on() {
    constexpr uint32_t loopLength = 1536;
    MidiEventVec ev;
    ev.push_back(MidiEvent::NoteOff(144, 1, 60, 0));
    ev.push_back(MidiEvent::NoteOn(500, 1, 60, 100));
    ev.push_back(MidiEvent::NoteOn(1400, 1, 60, 90));
    auto notes = NoteUtils::reconstructNotes(ev, loopLength, false);
    TEST_ASSERT_EQUAL(2u, notes.size());
    for (const auto& n : notes) {
        TEST_ASSERT_FALSE(n.startTick == 1400u && n.endTick == 144u);
    }
    assert_has_note(notes, 60, 500, loopLength - 1, 100);
    assert_has_note(notes, 60, 1400, loopLength - 1, 90);
}

void test_reconstruct_adjacent_same_pitch_boundary_order() {
    constexpr uint32_t loopLength = 1536;
    MidiEventVec badOrder;
    badOrder.push_back(MidiEvent::NoteOn(392, 5, 67, 100));
    badOrder.push_back(MidiEvent::NoteOn(488, 5, 67, 100));
    badOrder.push_back(MidiEvent::NoteOff(488, 5, 67, 0));
    badOrder.push_back(MidiEvent::NoteOff(1160, 5, 67, 0));
    auto corrupted = NoteUtils::reconstructNotes(badOrder, loopLength, false);
    TEST_ASSERT_EQUAL(2u, corrupted.size());
    assert_has_note(corrupted, 67, 392, 488, 100);
    assert_has_note(corrupted, 67, 488, 1160, 100);

    NoteUtils::ensureNoteOffsBeforeNoteOnsAtTick(badOrder, 67, 488);
    auto fixed = NoteUtils::reconstructNotes(badOrder, loopLength, false);
    TEST_ASSERT_EQUAL(2u, fixed.size());
    assert_has_note(fixed, 67, 392, 488, 100);
    assert_has_note(fixed, 67, 488, 1160, 100);
}

void test_reconstruct_record_and_overdub_pitch_ranges() {
    constexpr uint32_t loopLength = 1536;
    MidiEventVec ev;
    ev.push_back(MidiEvent::NoteOn(7, 5, 48, 100));
    ev.push_back(MidiEvent::NoteOff(55, 5, 48, 0));
    ev.push_back(MidiEvent::NoteOn(95, 5, 34, 100));
    ev.push_back(MidiEvent::NoteOff(191, 5, 34, 0));
    ev.push_back(MidiEvent::NoteOn(1535, 5, 48, 100));
    auto notes = NoteUtils::reconstructNotes(ev, loopLength, false);
    TEST_ASSERT_EQUAL(3u, notes.size());
    assert_has_note(notes, 48, 7, 55, 100);
    assert_has_note(notes, 34, 95, 191, 100);
    assert_has_note(notes, 48, 1535, 1535, 100);
}

static void push_wrapped_lane_note(MidiEventVec& ev, uint8_t pitch, NoteId noteId) {
    constexpr uint32_t kLoopLength = 1536;
    constexpr uint32_t kStart = 1499;
    constexpr uint32_t kLinearOff = 1595;
  MidiEvent on = MidiEvent::NoteOn(kStart, 1, pitch, 100);
  on.noteId = noteId;
  ev.push_back(on);
  MidiEvent off = MidiEvent::NoteOff(kLinearOff, 1, pitch, 0);
  off.noteId = noteId;
  ev.push_back(off);
  (void)kLoopLength;
}

static void assert_wrapped_lane_intact(const std::vector<NoteUtils::DisplayNote>& notes,
                                       uint8_t pitch) {
  constexpr uint32_t kLoopLength = 1536;
  assert_has_note(notes, pitch, 1499, kLoopLength - 1, 100);
  assert_has_note(notes, pitch, 0, 59, 100);
}

void test_reconstruct_neighbor_wrapped_lanes_after_mover_sort() {
  constexpr uint32_t kLoopLength = 1536;
  MidiEventVec ev;
  push_wrapped_lane_note(ev, 54, 54);
  push_wrapped_lane_note(ev, 55, 55);
  push_wrapped_lane_note(ev, 56, 56);

  for (auto& evt : ev) {
    if (evt.isNoteOn() && evt.data.noteData.note == 55) {
      evt.tick = 1451;
    }
    if (evt.isNoteOff() && evt.data.noteData.note == 55) {
      evt.tick = 1547;
    }
  }
  NoteUtils::sortMidiEventsChronologically(ev);

  auto notes = NoteUtils::reconstructNotes(ev, kLoopLength, false);
  assert_wrapped_lane_intact(notes, 54);
  assert_wrapped_lane_intact(notes, 56);

  for (auto& evt : ev) {
    if (evt.isNoteOn() && evt.data.noteData.note == 55) {
      evt.tick = 1403;
    }
    if (evt.isNoteOff() && evt.data.noteData.note == 55) {
      evt.tick = 1499;
    }
  }
  NoteUtils::sortMidiEventsChronologically(ev);
  notes = NoteUtils::reconstructNotes(ev, kLoopLength, false);
  assert_wrapped_lane_intact(notes, 54);
  assert_wrapped_lane_intact(notes, 56);
}

void test_reconstruct_duplicate_pitch_non_overlapping_spans() {
    constexpr uint32_t loopLength = 1536;
    MidiEventVec ev;
    MidiEvent earlyOn = MidiEvent::NoteOn(24, 1, 32, 100);
    earlyOn.noteId = 10;
    ev.push_back(earlyOn);
    MidiEvent lateOn = MidiEvent::NoteOn(1487, 1, 32, 100);
    lateOn.noteId = 32;
    ev.push_back(lateOn);
    ev.push_back(MidiEvent::NoteOff(47, 1, 32, 0));
    ev.push_back(MidiEvent::NoteOff(1579, 1, 32, 0));
    auto notes = NoteUtils::reconstructNotes(ev, loopLength, false);
    TEST_ASSERT_EQUAL(3u, notes.size());
    assert_has_note(notes, 32, 24, 47, 100);
    assert_has_note(notes, 32, 1487, loopLength - 1, 100);
    assert_has_note(notes, 32, 0, 43, 100);
}

void test_resolve_wrap_head_segment_live_playhead_at_zero() {
    constexpr uint32_t loopLength = 1536;
    const NoteUtils::WrapHeadSegment head = NoteUtils::resolveWrapHeadSegment(
        loopLength, 0, NoteUtils::WrapHeadSegmentContext::LivePlayhead);
    TEST_ASSERT_TRUE(head.visible);
    TEST_ASSERT_EQUAL_UINT32(0u, head.startTick);
    TEST_ASSERT_EQUAL_UINT32(0u, head.endTickInclusive);
}

void test_resolve_wrap_head_segment_live_playhead_hidden_before_tail_on() {
    constexpr uint32_t loopLength = 1536;
    constexpr uint32_t tailOnTick = 1400;
    const NoteUtils::WrapHeadSegment head = NoteUtils::resolveWrapHeadSegment(
        loopLength, 1500, NoteUtils::WrapHeadSegmentContext::LivePlayhead, 768, tailOnTick);
    TEST_ASSERT_FALSE(head.visible);
}

void test_is_live_wrap_head_continuation_display_tail_open_playhead_zero() {
    constexpr uint32_t loopLength = 1536;
    constexpr uint32_t tailOnTick = 1400;
    TEST_ASSERT_TRUE(NoteUtils::isLiveWrapHeadContinuationDisplay(tailOnTick, 0, loopLength, true));
    TEST_ASSERT_FALSE(
        NoteUtils::isLiveWrapHeadContinuationDisplay(tailOnTick, tailOnTick, loopLength, true));
    TEST_ASSERT_FALSE(
        NoteUtils::isLiveWrapHeadContinuationDisplay(100, 0, loopLength, true));
}

void test_held_tail_on_after_session_wrap_matches_loop_end_continuation() {
    // 012925 wrap 66.604: note 30 ON storage 2880, then playhead returns to S.
    // closeTick behind that ON paints a tail to loop end until NoteOff.
    constexpr uint32_t loopLength = 3072;
    constexpr uint32_t tailOnTick = 2880;
    TEST_ASSERT_TRUE(
        NoteUtils::isLiveWrapHeadContinuationDisplay(tailOnTick, 0, loopLength, true));
    TEST_ASSERT_FALSE(
        NoteUtils::isLiveWrapHeadContinuationDisplay(tailOnTick, 2904, loopLength, true));
}

void test_is_live_wrap_head_continuation_false_when_loop_shorter_than_wrap_window() {
    // Growing live-record lengths under the wrap window must not treat playhead-behind-note
    // as wrap continuation (false head from tick 0).
    constexpr uint32_t loopLength = 400;  // < 768 wrap window → wrapTailStart == 0
    TEST_ASSERT_FALSE(
        NoteUtils::isLiveWrapHeadContinuationDisplay(100, 99, loopLength, true));
    TEST_ASSERT_FALSE(
        NoteUtils::isLiveWrapHeadContinuationDisplay(300, 0, loopLength, true));
}

void test_resolve_wrap_head_segment_committed_head_off_at_zero() {
    constexpr uint32_t loopLength = 1536;
    const NoteUtils::WrapHeadSegment head = NoteUtils::resolveWrapHeadSegment(
        loopLength, 0, NoteUtils::WrapHeadSegmentContext::CommittedHeadOff);
    TEST_ASSERT_FALSE(head.visible);
}

void test_resolve_wrap_head_segment_committed_head_off_mid_loop() {
    constexpr uint32_t loopLength = 1536;
    const NoteUtils::WrapHeadSegment head = NoteUtils::resolveWrapHeadSegment(
        loopLength, 48, NoteUtils::WrapHeadSegmentContext::CommittedHeadOff);
    TEST_ASSERT_TRUE(head.visible);
    TEST_ASSERT_EQUAL_UINT32(0u, head.startTick);
    TEST_ASSERT_EQUAL_UINT32(48u, head.endTickInclusive);
}

void test_wrap_head_exclusive_end_for_draw_at_zero() {
    constexpr uint32_t loopLength = 1536;
    TEST_ASSERT_EQUAL_UINT32(1u, NoteUtils::wrapHeadExclusiveEndForDraw(0, loopLength));
    TEST_ASSERT_EQUAL_UINT32(49u, NoteUtils::wrapHeadExclusiveEndForDraw(48, loopLength));
    TEST_ASSERT_EQUAL_UINT32(0u, NoteUtils::wrapHeadExclusiveEndForDraw(loopLength - 1, loopLength));
}

int main(int /*argc*/, char** /*argv*/) {
    UNITY_BEGIN();
    RUN_TEST(test_project_display_notes_head_tail_split);
    RUN_TEST(test_project_display_notes_open_tail_with_playhead);
    RUN_TEST(test_project_display_notes_simple_span);
    RUN_TEST(test_reconstruct_empty_loop_yields_empty);
    RUN_TEST(test_reconstruct_discards_note_on_at_or_beyond_loop);
    RUN_TEST(test_reconstruct_wraps_note_off_past_boundary);
    RUN_TEST(test_reconstruct_lifo_same_pitch);
    RUN_TEST(test_reconstruct_open_note_to_loop_end);
    RUN_TEST(test_reconstruct_display_omits_open_tails_when_finish_open_notes_false);
    RUN_TEST(test_reconstruct_dedupes_identical_segments);
    RUN_TEST(test_reconstruct_dedupes_many_identical_geometry_notes);
    RUN_TEST(test_reconstruct_wrapped_tail_on_head_off);
    RUN_TEST(test_reconstruct_wrapped_tail_on_head_off_chronological);
    RUN_TEST(test_reconstruct_note_off_at_wrap_zero_is_boundary_end);
    RUN_TEST(test_is_wrap_held_open_note_accepts_head_off_at_zero);
    RUN_TEST(test_reconstruct_wrap_with_synthetic_loop_end_before_head_off);
    RUN_TEST(test_reconstruct_tick0_wrap_pair_survives_later_same_pitch);
    RUN_TEST(test_reconstruct_wrap_pair_blocked_by_intervening_note_on);
    RUN_TEST(test_reconstruct_adjacent_same_pitch_boundary_order);
    RUN_TEST(test_reconstruct_record_and_overdub_pitch_ranges);
    RUN_TEST(test_reconstruct_duplicate_pitch_non_overlapping_spans);
    RUN_TEST(test_reconstruct_neighbor_wrapped_lanes_after_mover_sort);
    RUN_TEST(test_resolve_wrap_head_segment_live_playhead_at_zero);
    RUN_TEST(test_resolve_wrap_head_segment_live_playhead_hidden_before_tail_on);
    RUN_TEST(test_is_live_wrap_head_continuation_display_tail_open_playhead_zero);
    RUN_TEST(test_held_tail_on_after_session_wrap_matches_loop_end_continuation);
    RUN_TEST(test_is_live_wrap_head_continuation_false_when_loop_shorter_than_wrap_window);
    RUN_TEST(test_resolve_wrap_head_segment_committed_head_off_at_zero);
    RUN_TEST(test_resolve_wrap_head_segment_committed_head_off_mid_loop);
    RUN_TEST(test_wrap_head_exclusive_end_for_draw_at_zero);
    return UNITY_END();
}
