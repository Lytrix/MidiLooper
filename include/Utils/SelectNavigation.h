//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#pragma once

#include <cstdint>
#include <vector>
#include "Globals.h"
#include "MidiEvent.h"
#include "Utils/NoteUtils.h"

namespace SelectNavigation {

struct SelectNavSlot {
    uint32_t relativeTick = 0;
    NoteId noteId = kInvalidNoteId;
    /// Index into **selectableDisplayNotesForEditUi** / **selectableDisplayNotesAtEditSelect** when **NoteEditSession** is active; else full note list.
    int noteIdx = -1;  // -1 = empty 16th step (no note)
};

/// Hydrate 1: slots cover this many 16ths on each side of `selectedTick`.
constexpr uint32_t kSelectNavigationNeighborhoodRadiusSteps = 1;

struct SelectNavigationBuildStats {
    uint32_t notesExamined = 0;
    uint32_t stepsScanned = 0;
    uint32_t slotsEmitted = 0;
};

uint32_t noteRelativeTick(uint32_t absolutePos, uint32_t loopStartTick, uint32_t loopLength);
uint32_t noteStorageTick(uint32_t relativeTick, uint32_t loopStartTick, uint32_t loopLength);

/// NOTE_EDIT bracket / nav tick in loop display phase [0, loopLength).
inline uint32_t displayPhaseTick(uint32_t tick, uint32_t loopLength) {
    return loopLength > 0 ? tick % loopLength : 0;
}

std::vector<SelectNavSlot> buildSelectNavigationSlots(
    uint32_t loopLength,
    uint32_t loopStartTick,
    const std::vector<NoteUtils::DisplayNote>& notes,
    uint32_t selectedTick,
    bool includeSelectedTickIfMissing = true,
    SelectNavigationBuildStats* stats = nullptr);

/// Map select-fader pitchbend onto a 16th in `[0, navLength)`.
uint32_t selectTickFromPitchbend(int32_t pitchValue, int32_t pitchMin, int32_t pitchMax,
                                 uint32_t navLength);

/// Inverse of `selectTickFromPitchbend` (and stacked intra-step when `stackedCount` > 1).
/// The returned value decodes back to the same sixteenth and stack index.
int32_t selectPitchbendFromTick(uint32_t tick, int32_t pitchMin, int32_t pitchMax,
                                uint32_t navLength, uint32_t stackedIndex = 0,
                                uint32_t stackedCount = 1);

/// Intra-step index in `[0, stackedCount)` using the same linear map as
/// `selectTickFromPitchbend`. Same-sixteenth note starts and the F1 grid
/// tick are one stack (`153550` exact tick, `161839` off-grid note ticks).
uint32_t selectStackedNoteIndexFromPitchbend(int32_t pitchValue, int32_t pitchMin, int32_t pitchMax,
                                             uint32_t navLength, uint32_t stackedCount);

int findSlotIndexForSelection(const std::vector<SelectNavSlot>& slots,
                              int selectedNoteIdx,
                              uint32_t selectedTick,
                              uint32_t loopLength);

int findSlotIndexForSelection(const std::vector<SelectNavSlot>& slots,
                              int selectedNoteIdx,
                              uint32_t selectedTick,
                              uint32_t loopLength,
                              int32_t pitchValue,
                              int32_t pitchMin,
                              int32_t pitchMax,
                              uint32_t navLength);

int findSlotIndexForNoteId(const std::vector<SelectNavSlot>& slots,
                           const std::vector<NoteUtils::DisplayNote>& notes,
                           NoteId noteId, uint32_t selectedTick,
                           uint32_t loopLength);

/// Resolve filtered note index for a nav slot (always **slot.noteIdx**; -1 for empty steps).
int resolveNoteIdxAtSlot(const SelectNavSlot& slot);

}  // namespace SelectNavigation
