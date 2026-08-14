//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include <unity.h>

#include <algorithm>
#include "MidiEvent.h"
#include <vector>

#include "../../src/Logger.cpp"
#include "../../src/Utils/IntervalProjection.cpp"
#include "../../src/Utils/NoteUtils.cpp"
#include "../../src/Utils/SelectNavigation.cpp"
#include "Utils/SelectNavigation.h"
#include "Utils/NoteEditDisplaySnapshot.h"
#include "Utils/NoteUtils.h"
#include "NoteEditSessionState.h"

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
    notes.push_back({kInvalidNoteId, 60, 100, 0, 96});
    notes.push_back({kInvalidNoteId, 67, 100, chordTick, chordTick + 96});
    notes.push_back({kInvalidNoteId, 72, 100, chordTick, chordTick + 96});
    notes.push_back({kInvalidNoteId, 76, 100, chordTick, chordTick + 96});
    notes.push_back({kInvalidNoteId, 64, 100, 16 * 48, 16 * 48 + 96});

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
    notes.push_back({kInvalidNoteId, 67, 100, chordTick, chordTick + 96});
    notes.push_back({kInvalidNoteId, 72, 100, chordTick, chordTick + 96});

    const auto slots =
        SelectNavigation::buildSelectNavigationSlots(loopLength, 0, notes, chordTick, false);

    TEST_ASSERT_EQUAL(32 + 1, (int)slots.size());
    const int slotIdx =
        SelectNavigation::findSlotIndexForSelection(slots, 1, chordTick, loopLength);
    TEST_ASSERT_TRUE(slotIdx >= 0);
    TEST_ASSERT_EQUAL(1, slots[slotIdx].noteIdx);
}

void test_empty_step_single_slot() {
    const uint32_t loopLength = 768;
    std::vector<NoteUtils::DisplayNote> notes;
    notes.push_back({kInvalidNoteId, 60, 100, 0, 96});

    const auto slots =
        SelectNavigation::buildSelectNavigationSlots(loopLength, 0, notes, 0, false);

    TEST_ASSERT_EQUAL(16, (int)slots.size());
    TEST_ASSERT_EQUAL(0, slots[0].noteIdx);
    TEST_ASSERT_EQUAL(-1, slots[1].noteIdx);
}

void test_loop_start_offset_maps_display_bracket_to_note() {
    const uint32_t loopLength = 1536;
    const uint32_t loopStartTick = 11;
    const uint32_t storageStart = 808;
    const uint32_t displayStart = 797;

    std::vector<NoteUtils::DisplayNote> notes;
    notes.push_back({34, 78, 100, storageStart, storageStart + 48});

    const auto slots = SelectNavigation::buildSelectNavigationSlots(
        loopLength, loopStartTick, notes, displayStart, false);

    int slotIdx = -1;
    for (int i = 0; i < static_cast<int>(slots.size()); ++i) {
        if (slots[static_cast<size_t>(i)].noteIdx == 0) {
            slotIdx = i;
            break;
        }
    }
    TEST_ASSERT_TRUE(slotIdx >= 0);
    TEST_ASSERT_EQUAL(displayStart, slots[static_cast<size_t>(slotIdx)].relativeTick);
    TEST_ASSERT_EQUAL(0, slots[static_cast<size_t>(slotIdx)].noteIdx);

    EditorSelection sel{};
    sel.primaryNote = 34;
    sel.selectedTick = displayStart;
    TEST_ASSERT_EQUAL(0, NoteEditDisplaySnapshot::filteredDisplayNoteIndexForSelection(
                                sel, notes, loopStartTick, loopLength));
}

void test_loop_start_offset_maps_first_slot_to_storage_tick() {
    const uint32_t loopLength = 1536;
    const uint32_t loopStartTick = 48;
    const uint32_t bracketDisplayTick = 0;

    // UIP display projection: note at storage loopStartTick appears at display phase 0.
    std::vector<NoteUtils::DisplayNote> notes;
    notes.push_back({kInvalidNoteId, 60, 100, loopStartTick, loopStartTick + 96});

    const auto slots = SelectNavigation::buildSelectNavigationSlots(
        loopLength, loopStartTick, notes, bracketDisplayTick, false);

    const int slotIdx = SelectNavigation::findSlotIndexForSelection(
        slots, 0, bracketDisplayTick, loopLength);
    TEST_ASSERT_TRUE(slotIdx >= 0);
    TEST_ASSERT_EQUAL(0, slots[static_cast<size_t>(slotIdx)].relativeTick);
    TEST_ASSERT_EQUAL(0, slots[static_cast<size_t>(slotIdx)].noteIdx);
    TEST_ASSERT_EQUAL(
        loopStartTick,
        SelectNavigation::noteStorageTick(slots[static_cast<size_t>(slotIdx)].relativeTick,
                                          loopStartTick, loopLength));
}

void test_loop_start_offset_no_phantom_slot_at_selected_tick() {
    const uint32_t loopLength = 1536;
    const uint32_t loopStartTick = 11;
    const uint32_t displayStart = 1099;

    std::vector<NoteUtils::DisplayNote> notes;
    notes.push_back({41, 71, 100, 1110, 1110 + 46});

    const auto slots = SelectNavigation::buildSelectNavigationSlots(
        loopLength, loopStartTick, notes, displayStart, true);

    int phantomCount = 0;
    for (const SelectNavigation::SelectNavSlot& slot : slots) {
        if (slot.relativeTick == 1088 && slot.noteIdx < 0) {
            phantomCount++;
        }
    }
    TEST_ASSERT_EQUAL(0, phantomCount);

    const int slotIdx = SelectNavigation::findSlotIndexForNoteId(
        slots, notes, 41, displayStart, loopLength);
    TEST_ASSERT_TRUE(slotIdx >= 0);
    TEST_ASSERT_EQUAL(displayStart, slots[static_cast<size_t>(slotIdx)].relativeTick);
}

void test_build_select_slots_no_empty_duplicate_at_note_tick() {
    const uint32_t loopLength = 5376;
    std::vector<NoteUtils::DisplayNote> notes;
    notes.push_back({1, 60, 100, 720, 768});
    const auto slots =
        SelectNavigation::buildSelectNavigationSlots(loopLength, 0, notes, 720, true);

    bool hasNote720 = false;
    bool hasEmpty720 = false;
    for (const SelectNavigation::SelectNavSlot& slot : slots) {
        if (slot.relativeTick != 720) {
            continue;
        }
        if (slot.noteIdx >= 0) {
            hasNote720 = true;
        } else {
            hasEmpty720 = true;
        }
    }
    TEST_ASSERT_TRUE(hasNote720);
    TEST_ASSERT_FALSE(hasEmpty720);
}

static std::vector<SelectNavigation::SelectNavSlot> trimSelectSlotsToContentSpan(
    std::vector<SelectNavigation::SelectNavSlot> slots, uint32_t navLength) {
    slots.erase(std::remove_if(slots.begin(), slots.end(),
                               [navLength](const SelectNavigation::SelectNavSlot& slot) {
                                   return slot.relativeTick >= navLength;
                               }),
                slots.end());
    return slots;
}

void test_trim_select_slots_to_committed_content_span() {
    const uint32_t fullLoop = 3072;
    const uint32_t navLength = 1536;
    std::vector<NoteUtils::DisplayNote> notes;
    notes.push_back({1, 60, 0, 0, 48});
    notes.push_back({2, 62, 0, 768, 816});
    const auto fullSlots =
        SelectNavigation::buildSelectNavigationSlots(fullLoop, 0, notes, 0, false);
    const auto trimmedSlots = trimSelectSlotsToContentSpan(fullSlots, navLength);

    TEST_ASSERT_EQUAL(64, fullLoop / 48);
    TEST_ASSERT_EQUAL(32, navLength / 48);
    TEST_ASSERT_TRUE((int)fullSlots.size() > (int)trimmedSlots.size());
    for (const SelectNavigation::SelectNavSlot& slot : trimmedSlots) {
        TEST_ASSERT_LESS_THAN(navLength, slot.relativeTick);
    }
    TEST_ASSERT_EQUAL(1, countNoteSlotsAtTick(trimmedSlots, 0));
    TEST_ASSERT_EQUAL(1, countNoteSlotsAtTick(trimmedSlots, 768));
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    UNITY_BEGIN();
    RUN_TEST(test_same_step_three_notes_yield_three_nav_slots);
    RUN_TEST(test_find_slot_index_matches_selected_note);
    RUN_TEST(test_empty_step_single_slot);
    RUN_TEST(test_loop_start_offset_maps_display_bracket_to_note);
    RUN_TEST(test_loop_start_offset_maps_first_slot_to_storage_tick);
    RUN_TEST(test_loop_start_offset_no_phantom_slot_at_selected_tick);
    RUN_TEST(test_build_select_slots_no_empty_duplicate_at_note_tick);
    RUN_TEST(test_trim_select_slots_to_committed_content_span);
    return UNITY_END();
}
