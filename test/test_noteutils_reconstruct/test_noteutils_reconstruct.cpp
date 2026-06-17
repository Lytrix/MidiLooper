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
    RUN_TEST(test_reconstruct_record_and_overdub_pitch_ranges);
    return UNITY_END();
}
