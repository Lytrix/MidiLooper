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
    /// Index into **filterSelectableDisplayNotes** when **NoteEditSession** is active; else full note list.
    int noteIdx = -1;  // -1 = empty 16th step (no note)
};

uint32_t noteRelativeTick(uint32_t absolutePos, uint32_t loopStartTick, uint32_t loopLength);
uint32_t noteStorageTick(uint32_t relativeTick, uint32_t loopStartTick, uint32_t loopLength);

std::vector<SelectNavSlot> buildSelectNavigationSlots(
    uint32_t loopLength,
    uint32_t loopStartTick,
    const std::vector<NoteUtils::DisplayNote>& notes,
    uint32_t bracketTick,
    bool includeBracketIfMissing = true);

int findSlotIndexForSelection(const std::vector<SelectNavSlot>& slots,
                              int selectedNoteIdx,
                              uint32_t bracketTick,
                              uint32_t loopStartTick,
                              uint32_t loopLength);

int findSlotIndexForNoteId(const std::vector<SelectNavSlot>& slots,
                           const std::vector<NoteUtils::DisplayNote>& notes,
                           NoteId noteId, uint32_t bracketTick, uint32_t loopStartTick,
                           uint32_t loopLength);

/// Resolve filtered note index for a nav slot (always **slot.noteIdx**; -1 for empty steps).
int resolveNoteIdxAtSlot(const SelectNavSlot& slot);

}  // namespace SelectNavigation
