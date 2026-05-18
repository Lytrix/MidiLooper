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

int main(int /*argc*/, char** /*argv*/) {
    UNITY_BEGIN();
    RUN_TEST(test_reconstruct_empty_loop_yields_empty);
    RUN_TEST(test_reconstruct_discards_note_on_at_or_beyond_loop);
    RUN_TEST(test_reconstruct_wraps_note_off_past_boundary);
    RUN_TEST(test_reconstruct_lifo_same_pitch);
    RUN_TEST(test_reconstruct_open_note_to_loop_end);
    RUN_TEST(test_reconstruct_dedupes_identical_segments);
    return UNITY_END();
}
