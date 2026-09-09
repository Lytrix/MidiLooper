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

    SelectNavigation::SelectNavigationBuildStats stats;
    const auto slots = SelectNavigation::buildSelectNavigationSlots(
        loopLength, 0, notes, chordTick, false, &stats);

    TEST_ASSERT_EQUAL(3, (int)stats.stepsScanned);
    TEST_ASSERT_EQUAL(5, (int)slots.size());
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

    TEST_ASSERT_EQUAL(4, (int)slots.size());
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

    TEST_ASSERT_EQUAL(3, (int)slots.size());
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
    const auto wrapSlots =
        SelectNavigation::buildSelectNavigationSlots(fullLoop, 0, notes, 0, false);
    const auto trimmedSlots = trimSelectSlotsToContentSpan(wrapSlots, navLength);
    const auto midSlots =
        SelectNavigation::buildSelectNavigationSlots(fullLoop, 0, notes, 768, false);

    TEST_ASSERT_EQUAL(64, fullLoop / 48);
    TEST_ASSERT_EQUAL(32, navLength / 48);
    TEST_ASSERT_TRUE((int)wrapSlots.size() > (int)trimmedSlots.size());
    for (const SelectNavigation::SelectNavSlot& slot : trimmedSlots) {
        TEST_ASSERT_LESS_THAN(navLength, slot.relativeTick);
    }
    TEST_ASSERT_EQUAL(1, countNoteSlotsAtTick(trimmedSlots, 0));
    TEST_ASSERT_EQUAL(0, countNoteSlotsAtTick(trimmedSlots, 768));
    TEST_ASSERT_EQUAL(1, countNoteSlotsAtTick(midSlots, 768));
}

void test_neighborhood_does_not_scale_with_loop_length() {
    std::vector<NoteUtils::DisplayNote> notes;
    notes.push_back({1, 60, 100, 0, 96});
    notes.push_back({2, 62, 100, 768, 816});

    SelectNavigation::SelectNavigationBuildStats shortStats;
    SelectNavigation::SelectNavigationBuildStats longStats;
    const auto shortSlots = SelectNavigation::buildSelectNavigationSlots(
        1536, 0, notes, 0, false, &shortStats);
    const auto longSlots = SelectNavigation::buildSelectNavigationSlots(
        32u * Config::TICKS_PER_BAR, 0, notes, 0, false, &longStats);

    TEST_ASSERT_EQUAL(3, (int)shortStats.stepsScanned);
    TEST_ASSERT_EQUAL(3, (int)longStats.stepsScanned);
    TEST_ASSERT_EQUAL((int)shortSlots.size(), (int)longSlots.size());
    TEST_ASSERT_EQUAL(1, countNoteSlotsAtTick(shortSlots, 0));
    TEST_ASSERT_EQUAL(0, countNoteSlotsAtTick(shortSlots, 768));
}

void test_neighborhood_skips_sounding_through_notes() {
    const uint32_t loopLength = 1536;
    const uint32_t selectedTick = 8 * 48;
    std::vector<NoteUtils::DisplayNote> notes;
    notes.push_back({1, 60, 100, 0, selectedTick + 96});
    notes.push_back({2, 64, 100, selectedTick, selectedTick + 48});

    const auto slots =
        SelectNavigation::buildSelectNavigationSlots(loopLength, 0, notes, selectedTick, false);

    TEST_ASSERT_EQUAL(0, countNoteSlotsAtTick(slots, 0));
    TEST_ASSERT_EQUAL(1, countNoteSlotsAtTick(slots, selectedTick));
}

void test_find_slot_prefers_off_grid_note_in_same_sixteenth_134549() {
    const uint32_t loopLength = 2304;
    const uint32_t stepTick = 33 * 48;
    const uint32_t offGridTick = 1616;
    TEST_ASSERT_EQUAL(stepTick, (offGridTick / 48) * 48);

    std::vector<NoteUtils::DisplayNote> notes;
    notes.push_back({59, 48, 100, offGridTick, offGridTick + 96});

    const auto slots = SelectNavigation::buildSelectNavigationSlots(loopLength, 0, notes,
                                                                    offGridTick, true);
    const int fromStep =
        SelectNavigation::findSlotIndexForSelection(slots, -1, stepTick, loopLength);
    TEST_ASSERT_TRUE(fromStep >= 0);
    TEST_ASSERT_EQUAL(0, slots[static_cast<size_t>(fromStep)].noteIdx);
    TEST_ASSERT_EQUAL(offGridTick, slots[static_cast<size_t>(fromStep)].relativeTick);
}

void test_find_slot_picks_stacked_note_from_intra_step_pitchbend_153550() {
    // session_20260909_153550: after move+pitch to 2304, three notes share the
    // tick. F1 with selectedNoteIdx=-1 must be able to reach the later stacked
    // note (moved pitch 82), not only the first (63).
    const uint32_t loopLength = 3840;
    const uint32_t stackedTick = 2304;
    std::vector<NoteUtils::DisplayNote> notes;
    notes.push_back({84, 63, 100, stackedTick, stackedTick + 480});
    notes.push_back({80, 70, 100, stackedTick, stackedTick + 48});
    notes.push_back({81, 82, 100, stackedTick, stackedTick + 479});

    const auto slots = SelectNavigation::buildSelectNavigationSlots(loopLength, 0, notes,
                                                                    stackedTick, false);
    const int first = SelectNavigation::findSlotIndexForSelection(slots, -1, stackedTick,
                                                                  loopLength);
    TEST_ASSERT_TRUE(first >= 0);
    TEST_ASSERT_EQUAL(0, slots[static_cast<size_t>(first)].noteIdx);

    const int32_t pitchMin = -8192;
    const int32_t pitchMax = 8192;
    const int32_t enterStep = 1763;
    TEST_ASSERT_EQUAL(stackedTick, SelectNavigation::selectTickFromPitchbend(
                                       enterStep, pitchMin, pitchMax, loopLength));
    TEST_ASSERT_EQUAL(0, SelectNavigation::selectStackedNoteIndexFromPitchbend(
                             enterStep, pitchMin, pitchMax, loopLength, 3));
    const int32_t lateInStep = 1910;
    TEST_ASSERT_EQUAL(stackedTick, SelectNavigation::selectTickFromPitchbend(
                                       lateInStep, pitchMin, pitchMax, loopLength));
    TEST_ASSERT_EQUAL(2, SelectNavigation::selectStackedNoteIndexFromPitchbend(
                             lateInStep, pitchMin, pitchMax, loopLength, 3));
    const int stacked = SelectNavigation::findSlotIndexForSelection(
        slots, -1, stackedTick, loopLength, lateInStep, pitchMin, pitchMax, loopLength);
    TEST_ASSERT_TRUE(stacked >= 0);
    TEST_ASSERT_EQUAL(2, slots[static_cast<size_t>(stacked)].noteIdx);
    TEST_ASSERT_EQUAL(81, notes[static_cast<size_t>(slots[static_cast<size_t>(stacked)].noteIdx)]
                              .noteId);
}

void test_find_slot_picks_off_grid_stack_from_grid_tick_161839() {
    // session_20260909_161839: movers stay at +4 (628). F1 aims at grid 624.
    // Notes at 628 must be a selectable stack, not one same-step fallback.
    const uint32_t loopLength = 1536;
    const uint32_t gridTick = 13 * 48;
    const uint32_t noteTick = gridTick + 4;
    TEST_ASSERT_EQUAL(624, gridTick);
    TEST_ASSERT_EQUAL(628, noteTick);

    std::vector<NoteUtils::DisplayNote> notes;
    notes.push_back({1, 60, 100, noteTick, noteTick + 288});
    notes.push_back({2, 64, 100, noteTick, noteTick + 192});
    notes.push_back({3, 67, 100, noteTick, noteTick + 192});

    const auto slots = SelectNavigation::buildSelectNavigationSlots(loopLength, 0, notes,
                                                                    noteTick, false);
    const int32_t pitchMin = -8192;
    const int32_t pitchMax = 8192;
    TEST_ASSERT_EQUAL(gridTick, SelectNavigation::selectTickFromPitchbend(
                                    -1175, pitchMin, pitchMax, loopLength));
    TEST_ASSERT_EQUAL(0, SelectNavigation::selectStackedNoteIndexFromPitchbend(
                             -1175, pitchMin, pitchMax, loopLength, 3));
    const int first = SelectNavigation::findSlotIndexForSelection(
        slots, -1, gridTick, loopLength, -1175, pitchMin, pitchMax, loopLength);
    TEST_ASSERT_TRUE(first >= 0);
    TEST_ASSERT_EQUAL(0, slots[static_cast<size_t>(first)].noteIdx);
    TEST_ASSERT_EQUAL(noteTick, slots[static_cast<size_t>(first)].relativeTick);

    const int32_t lateInStep = -900;
    TEST_ASSERT_EQUAL(gridTick, SelectNavigation::selectTickFromPitchbend(
                                    lateInStep, pitchMin, pitchMax, loopLength));
    TEST_ASSERT_EQUAL(2, SelectNavigation::selectStackedNoteIndexFromPitchbend(
                             lateInStep, pitchMin, pitchMax, loopLength, 3));
    const int stacked = SelectNavigation::findSlotIndexForSelection(
        slots, -1, gridTick, loopLength, lateInStep, pitchMin, pitchMax, loopLength);
    TEST_ASSERT_TRUE(stacked >= 0);
    TEST_ASSERT_EQUAL(2, slots[static_cast<size_t>(stacked)].noteIdx);
    TEST_ASSERT_EQUAL(3, notes[static_cast<size_t>(slots[static_cast<size_t>(stacked)].noteIdx)]
                             .noteId);

    notes.clear();
    notes.push_back({10, 60, 100, gridTick, gridTick + 192});
    notes.push_back({11, 67, 100, noteTick, noteTick + 192});
    const auto mixed = SelectNavigation::buildSelectNavigationSlots(loopLength, 0, notes,
                                                                    noteTick, false);
    const int mixedFirst = SelectNavigation::findSlotIndexForSelection(
        mixed, -1, gridTick, loopLength, -1321, pitchMin, pitchMax, loopLength);
    const int mixedLate = SelectNavigation::findSlotIndexForSelection(
        mixed, -1, gridTick, loopLength, lateInStep, pitchMin, pitchMax, loopLength);
    TEST_ASSERT_TRUE(mixedFirst >= 0);
    TEST_ASSERT_TRUE(mixedLate >= 0);
    TEST_ASSERT_EQUAL(gridTick, mixed[static_cast<size_t>(mixedFirst)].relativeTick);
    TEST_ASSERT_EQUAL(noteTick, mixed[static_cast<size_t>(mixedLate)].relativeTick);
    TEST_ASSERT_EQUAL(10, notes[static_cast<size_t>(mixed[static_cast<size_t>(mixedFirst)].noteIdx)]
                              .noteId);
    TEST_ASSERT_EQUAL(11, notes[static_cast<size_t>(mixed[static_cast<size_t>(mixedLate)].noteIdx)]
                              .noteId);
}

void test_select_tick_from_pitchbend_is_step_linear() {
    const uint32_t navLength = 1536;
    TEST_ASSERT_EQUAL(0, SelectNavigation::selectTickFromPitchbend(-8192, -8192, 8192, navLength));
    TEST_ASSERT_EQUAL(1536 - 48,
                      SelectNavigation::selectTickFromPitchbend(8192, -8192, 8192, navLength));
    TEST_ASSERT_EQUAL(0, SelectNavigation::selectTickFromPitchbend(-8192, -8192, 8192, navLength) %
                             Config::TICKS_PER_16TH_STEP);
    const uint32_t midTick =
        SelectNavigation::selectTickFromPitchbend(0, -8192, 8192, navLength);
    TEST_ASSERT_EQUAL(0, midTick % Config::TICKS_PER_16TH_STEP);
    TEST_ASSERT_TRUE(midTick < navLength);
}

void test_select_pitchbend_from_tick_round_trips_234815() {
    const uint32_t navLength = 3072;
    const uint32_t tick = 2640;
    const int32_t pb =
        SelectNavigation::selectPitchbendFromTick(tick, -8192, 8192, navLength);
    TEST_ASSERT_NOT_EQUAL(-4915, pb);
    TEST_ASSERT_EQUAL(tick, SelectNavigation::selectTickFromPitchbend(pb, -8192, 8192, navLength));
}

void test_select_pitchbend_from_tick_round_trips_stacked() {
    const uint32_t navLength = 1536;
    const uint32_t tick = 384;
    const int32_t first =
        SelectNavigation::selectPitchbendFromTick(tick, -8192, 8192, navLength, 0, 3);
    const int32_t last =
        SelectNavigation::selectPitchbendFromTick(tick, -8192, 8192, navLength, 2, 3);
    TEST_ASSERT_EQUAL(tick, SelectNavigation::selectTickFromPitchbend(first, -8192, 8192, navLength));
    TEST_ASSERT_EQUAL(tick, SelectNavigation::selectTickFromPitchbend(last, -8192, 8192, navLength));
    TEST_ASSERT_EQUAL(0, SelectNavigation::selectStackedNoteIndexFromPitchbend(
                             first, -8192, 8192, navLength, 3));
    TEST_ASSERT_EQUAL(2, SelectNavigation::selectStackedNoteIndexFromPitchbend(
                             last, -8192, 8192, navLength, 3));
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
    RUN_TEST(test_neighborhood_does_not_scale_with_loop_length);
    RUN_TEST(test_neighborhood_skips_sounding_through_notes);
    RUN_TEST(test_find_slot_prefers_off_grid_note_in_same_sixteenth_134549);
    RUN_TEST(test_find_slot_picks_stacked_note_from_intra_step_pitchbend_153550);
    RUN_TEST(test_find_slot_picks_off_grid_stack_from_grid_tick_161839);
    RUN_TEST(test_select_tick_from_pitchbend_is_step_linear);
    RUN_TEST(test_select_pitchbend_from_tick_round_trips_234815);
    RUN_TEST(test_select_pitchbend_from_tick_round_trips_stacked);
    return UNITY_END();
}
