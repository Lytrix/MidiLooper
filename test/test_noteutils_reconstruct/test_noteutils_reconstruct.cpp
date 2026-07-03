//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include <unity.h>
#include <vector>
#include <algorithm>

// Compile production translation units into this test only (native stub Logger + real NoteUtils).
#include "../../src/Logger.cpp"
#include "../../src/Utils/NoteUtils.cpp"

#include "Utils/NoteUtils.h"
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

void test_reconstruct_dedupes_identical_segments() {
    MidiEventVec ev;
    ev.push_back(MidiEvent::NoteOn(0, 1, 60, 100));
    ev.push_back(MidiEvent::NoteOff(10, 1, 60, 0));
    ev.push_back(MidiEvent::NoteOn(0, 1, 60, 100));
    ev.push_back(MidiEvent::NoteOff(10, 1, 60, 0));
    auto notes = NoteUtils::reconstructNotes(ev, 100);
    TEST_ASSERT_EQUAL(1u, notes.size());
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

int main(int /*argc*/, char** /*argv*/) {
    UNITY_BEGIN();
    RUN_TEST(test_reconstruct_empty_loop_yields_empty);
    RUN_TEST(test_reconstruct_discards_note_on_at_or_beyond_loop);
    RUN_TEST(test_reconstruct_wraps_note_off_past_boundary);
    RUN_TEST(test_reconstruct_lifo_same_pitch);
    RUN_TEST(test_reconstruct_open_note_to_loop_end);
    RUN_TEST(test_reconstruct_dedupes_identical_segments);
    RUN_TEST(test_reconstruct_wrapped_tail_on_head_off);
    RUN_TEST(test_reconstruct_wrapped_tail_on_head_off_chronological);
    RUN_TEST(test_reconstruct_wrap_with_synthetic_loop_end_before_head_off);
    RUN_TEST(test_reconstruct_wrap_pair_blocked_by_intervening_note_on);
    RUN_TEST(test_reconstruct_adjacent_same_pitch_boundary_order);
    RUN_TEST(test_reconstruct_record_and_overdub_pitch_ranges);
    RUN_TEST(test_reconstruct_duplicate_pitch_non_overlapping_spans);
    RUN_TEST(test_reconstruct_neighbor_wrapped_lanes_after_mover_sort);
    return UNITY_END();
}
