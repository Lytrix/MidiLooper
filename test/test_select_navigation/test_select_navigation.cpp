//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include <unity.h>
#include <vector>

#include "../../src/Utils/SelectNavigation.cpp"
#include "Utils/SelectNavigation.h"
#include "Utils/NoteUtils.h"

static int countNoteSlotsAtTick(const std::vector<SelectNavigation::SelectNavSlot>& slots,
                                uint32_t relativeTick) {
    int count = 0;
    for (const SelectNavigation::SelectNavSlot& slot : slots) {
        if (slot.relativeTick == relativeTick && slot.noteIdx >= 0) {
            count++;
        }
    }
    return count;
}

void test_same_step_three_notes_yield_three_nav_slots() {
    const uint32_t loopLength = 1536;  // 2 bars
    const uint32_t chordTick = 8 * 48;

    std::vector<NoteUtils::DisplayNote> notes;
    notes.push_back({60, 100, 0, 96});
    notes.push_back({67, 100, chordTick, chordTick + 96});
    notes.push_back({72, 100, chordTick, chordTick + 96});
    notes.push_back({76, 100, chordTick, chordTick + 96});
    notes.push_back({64, 100, 16 * 48, 16 * 48 + 96});

    const auto slots =
        SelectNavigation::buildSelectNavigationSlots(loopLength, 0, notes, 0, false);

    TEST_ASSERT_EQUAL(32 + 2, (int)slots.size());
    TEST_ASSERT_EQUAL(3, countNoteSlotsAtTick(slots, chordTick));

    std::vector<int> chordNoteIdx;
    for (const SelectNavigation::SelectNavSlot& slot : slots) {
        if (slot.relativeTick == chordTick && slot.noteIdx >= 0) {
            chordNoteIdx.push_back(slot.noteIdx);
        }
    }
    TEST_ASSERT_EQUAL(3, (int)chordNoteIdx.size());
    TEST_ASSERT_EQUAL(1, chordNoteIdx[0]);
    TEST_ASSERT_EQUAL(2, chordNoteIdx[1]);
    TEST_ASSERT_EQUAL(3, chordNoteIdx[2]);
    TEST_ASSERT_EQUAL(67, notes[chordNoteIdx[0]].note);
    TEST_ASSERT_EQUAL(72, notes[chordNoteIdx[1]].note);
    TEST_ASSERT_EQUAL(76, notes[chordNoteIdx[2]].note);
}

void test_find_slot_index_matches_selected_note() {
    const uint32_t loopLength = 1536;
    const uint32_t chordTick = 8 * 48;

    std::vector<NoteUtils::DisplayNote> notes;
    notes.push_back({67, 100, chordTick, chordTick + 96});
    notes.push_back({72, 100, chordTick, chordTick + 96});

    const auto slots =
        SelectNavigation::buildSelectNavigationSlots(loopLength, 0, notes, chordTick, false);

    TEST_ASSERT_EQUAL(32 + 1, (int)slots.size());
    const int slotIdx =
        SelectNavigation::findSlotIndexForSelection(slots, 1, chordTick, 0, loopLength);
    TEST_ASSERT_TRUE(slotIdx >= 0);
    TEST_ASSERT_EQUAL(1, slots[slotIdx].noteIdx);
}

void test_empty_step_single_slot() {
    const uint32_t loopLength = 768;
    std::vector<NoteUtils::DisplayNote> notes;
    notes.push_back({60, 100, 0, 96});

    const auto slots =
        SelectNavigation::buildSelectNavigationSlots(loopLength, 0, notes, 0, false);

    TEST_ASSERT_EQUAL(16, (int)slots.size());
    TEST_ASSERT_EQUAL(0, slots[0].noteIdx);
    TEST_ASSERT_EQUAL(-1, slots[1].noteIdx);
}

void test_loop_start_offset_maps_first_slot_to_storage_tick() {
    const uint32_t loopLength = 1536;
    const uint32_t loopStartTick = 48;
    const uint32_t bracketStorageTick = loopStartTick;

    std::vector<NoteUtils::DisplayNote> notes;
    notes.push_back({60, 100, loopStartTick, loopStartTick + 96});

    const auto slots = SelectNavigation::buildSelectNavigationSlots(
        loopLength, loopStartTick, notes, bracketStorageTick, false);

    TEST_ASSERT_EQUAL(0, slots[0].relativeTick);
    TEST_ASSERT_EQUAL(0, slots[0].noteIdx);
    TEST_ASSERT_EQUAL(
        loopStartTick,
        SelectNavigation::noteStorageTick(slots[0].relativeTick, loopStartTick, loopLength));

    const int slotIdx = SelectNavigation::findSlotIndexForSelection(
        slots, 0, bracketStorageTick, loopStartTick, loopLength);
    TEST_ASSERT_EQUAL(0, slotIdx);
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    UNITY_BEGIN();
    RUN_TEST(test_same_step_three_notes_yield_three_nav_slots);
    RUN_TEST(test_find_slot_index_matches_selected_note);
    RUN_TEST(test_empty_step_single_slot);
    RUN_TEST(test_loop_start_offset_maps_first_slot_to_storage_tick);
    return UNITY_END();
}
