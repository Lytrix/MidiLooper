//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "Utils/SelectNavigation.h"
#include <algorithm>

namespace SelectNavigation {

uint32_t noteRelativeTick(uint32_t absolutePos, uint32_t loopStartTick, uint32_t loopLength) {
    if (loopLength == 0) {
        return 0;
    }
    uint32_t relativePos = (absolutePos >= loopStartTick)
                               ? (absolutePos - loopStartTick)
                               : (absolutePos + loopLength - loopStartTick);
    return relativePos % loopLength;
}

std::vector<SelectNavSlot> buildSelectNavigationSlots(
    uint32_t loopLength,
    uint32_t loopStartTick,
    const std::vector<NoteUtils::DisplayNote>& notes,
    uint32_t bracketTick,
    bool includeBracketIfMissing) {
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
            slots.push_back({stepTick, -1});
        } else {
            for (int idx : notesInStep) {
                const uint32_t rel = noteRelativeTick(notes[idx].startTick, loopStartTick, loopLength);
                slots.push_back({rel, idx});
            }
        }
    }

    if (includeBracketIfMissing) {
        const uint32_t relativeBracketTick = noteRelativeTick(bracketTick, loopStartTick, loopLength);
        bool bracketTickFound = false;
        for (const SelectNavSlot& slot : slots) {
            if (slot.relativeTick == relativeBracketTick) {
                bracketTickFound = true;
                break;
            }
        }
        if (!bracketTickFound) {
            slots.push_back({relativeBracketTick, -1});
            std::sort(slots.begin(), slots.end(), [](const SelectNavSlot& a, const SelectNavSlot& b) {
                if (a.relativeTick != b.relativeTick) {
                    return a.relativeTick < b.relativeTick;
                }
                return a.noteIdx < b.noteIdx;
            });
        }
    }

    return slots;
}

int findSlotIndexForSelection(const std::vector<SelectNavSlot>& slots,
                              int selectedNoteIdx,
                              uint32_t bracketTick,
                              uint32_t loopStartTick,
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

    const uint32_t relativeBracketTick = noteRelativeTick(bracketTick, loopStartTick, loopLength);
    for (int i = 0; i < (int)slots.size(); i++) {
        if (slots[i].relativeTick == relativeBracketTick) {
            return i;
        }
    }

    return -1;
}

}  // namespace SelectNavigation
