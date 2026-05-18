//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include <unity.h>

#include "MidiEvent.h"
#include "Utils/MidiEventVecFnvHash.h"

void test_fnv1a_single_event_matches_python_reference() {
    MidiEventVec ev;
    ev.push_back(MidiEvent::NoteOn(1, 1, 60, 100));
    TEST_ASSERT_EQUAL_UINT32(1707116626u, midiEventVecFnv1aHash(ev));
}

void test_fnv1a_order_matters() {
    MidiEventVec a;
    a.push_back(MidiEvent::NoteOn(0, 1, 60, 100));
    a.push_back(MidiEvent::NoteOff(10, 1, 60, 0));
    MidiEventVec b;
    b.push_back(MidiEvent::NoteOff(10, 1, 60, 0));
    b.push_back(MidiEvent::NoteOn(0, 1, 60, 100));
    TEST_ASSERT_TRUE(midiEventVecFnv1aHash(a) != midiEventVecFnv1aHash(b));
}

int main(int /*argc*/, char** /*argv*/) {
    UNITY_BEGIN();
    RUN_TEST(test_fnv1a_single_event_matches_python_reference);
    RUN_TEST(test_fnv1a_order_matters);
    return UNITY_END();
}
