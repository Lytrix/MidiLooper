//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "Utils/SelectNavigation.h"
#include "Utils/IntervalProjection.h"
#include "NoteEditSessionState.h"
#include <algorithm>
#include <unordered_set>

namespace SelectNavigation {

uint32_t noteRelativeTick(uint32_t absolutePos, uint32_t loopStartTick, uint32_t loopLength) {
    return IntervalProjection::noteRelativeTick(absolutePos, loopStartTick, loopLength);
}

uint32_t noteStorageTick(uint32_t relativeTick, uint32_t loopStartTick, uint32_t loopLength) {
    return IntervalProjection::noteStorageTick(relativeTick, loopStartTick, loopLength);
}

std::vector<SelectNavSlot> buildSelectNavigationSlots(
    uint32_t loopLength,
    uint32_t loopStartTick,
    const std::vector<NoteUtils::DisplayNote>& notes,
    uint32_t selectedTick,
    bool includeSelectedTickIfMissing) {
    std::vector<SelectNavSlot> slots;
    if (loopLength == 0) {
        return slots;
    }

    const uint32_t numSteps = loopLength / Config::TICKS_PER_16TH_STEP;

    for (uint32_t step = 0; step < numSteps; step++) {
        const uint32_t stepTick = step * Config::TICKS_PER_16TH_STEP;
        std::vector<int> notesInStep;
        notesInStep.reserve(notes.size());

        for (int i = 0; i < (int)notes.size(); i++) {
            const uint32_t rel = noteRelativeTick(notes[i].startTick, loopStartTick, loopLength);
            const uint32_t noteStep = rel / Config::TICKS_PER_16TH_STEP;
            if (noteStep == step) {
                notesInStep.push_back(i);
            }
        }

        std::sort(notesInStep.begin(), notesInStep.end(), [&](int a, int b) {
            const uint32_t relA = noteRelativeTick(notes[a].startTick, loopStartTick, loopLength);
            const uint32_t relB = noteRelativeTick(notes[b].startTick, loopStartTick, loopLength);
            if (relA != relB) {
                return relA < relB;
            }
            return notes[a].note < notes[b].note;
        });

        if (notesInStep.empty()) {
            slots.push_back({stepTick, kInvalidNoteId, -1});
        } else {
            for (int idx : notesInStep) {
                const uint32_t rel = noteRelativeTick(notes[idx].startTick, loopStartTick, loopLength);
                slots.push_back({rel, notes[static_cast<size_t>(idx)].noteId, idx});
            }
        }
    }

    if (includeSelectedTickIfMissing) {
        const uint32_t phaseSelectedTick = displayPhaseTick(selectedTick, loopLength);
        bool selectedTickFound = false;
        for (const SelectNavSlot& slot : slots) {
            if (slot.relativeTick == phaseSelectedTick) {
                selectedTickFound = true;
                break;
            }
        }
        if (!selectedTickFound) {
            slots.push_back({phaseSelectedTick, kInvalidNoteId, -1});
            std::sort(slots.begin(), slots.end(), [](const SelectNavSlot& a, const SelectNavSlot& b) {
                if (a.relativeTick != b.relativeTick) {
                    return a.relativeTick < b.relativeTick;
                }
                return a.noteIdx < b.noteIdx;
            });
        }
    }

    std::unordered_set<uint32_t> ticksWithNotes;
    for (const SelectNavSlot& slot : slots) {
        if (slot.noteIdx >= 0) {
            ticksWithNotes.insert(slot.relativeTick);
        }
    }
    if (!ticksWithNotes.empty()) {
        slots.erase(std::remove_if(slots.begin(), slots.end(),
                                   [&](const SelectNavSlot& slot) {
                                       return slot.noteIdx < 0 &&
                                              ticksWithNotes.find(slot.relativeTick) !=
                                                  ticksWithNotes.end();
                                   }),
                    slots.end());
    }

    return slots;
}

int findSlotIndexForSelection(const std::vector<SelectNavSlot>& slots,
                              int selectedNoteIdx,
                              uint32_t selectedTick,
                              uint32_t loopLength) {
    if (slots.empty()) {
        return -1;
    }

    if (selectedNoteIdx >= 0) {
        for (int i = 0; i < (int)slots.size(); i++) {
            if (slots[i].noteIdx == selectedNoteIdx) {
                return i;
            }
        }
    }

    const uint32_t phaseSelectedTick = displayPhaseTick(selectedTick, loopLength);
    for (int i = 0; i < (int)slots.size(); i++) {
        if (slots[i].relativeTick == phaseSelectedTick) {
            return i;
        }
    }

    return -1;
}

int findSlotIndexForNoteId(const std::vector<SelectNavSlot>& slots,
                           const std::vector<NoteUtils::DisplayNote>& notes,
                           NoteId noteId, uint32_t selectedTick,
                           uint32_t loopLength) {
    if (noteId != kInvalidNoteId) {
        for (int i = 0; i < static_cast<int>(slots.size()); ++i) {
            if (slots[static_cast<size_t>(i)].noteId == noteId) {
                return i;
            }
            const int noteIdx = slots[static_cast<size_t>(i)].noteIdx;
            if (noteIdx >= 0 && noteIdx < static_cast<int>(notes.size()) &&
                notes[static_cast<size_t>(noteIdx)].noteId == noteId) {
                return i;
            }
        }
    }
    return findSlotIndexForSelection(slots, -1, selectedTick, loopLength);
}

int resolveNoteIdxAtSlot(const SelectNavSlot& slot) {
    return slot.noteIdx;
}

}  // namespace SelectNavigation
