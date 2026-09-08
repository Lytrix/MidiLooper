//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "Utils/SelectNavigation.h"
#include "Utils/IntervalProjection.h"
#include "NoteEditSessionState.h"
#include <algorithm>
#include <cstdint>
#include <unordered_set>

namespace SelectNavigation {

uint32_t noteRelativeTick(uint32_t absolutePos, uint32_t loopStartTick, uint32_t loopLength) {
    return IntervalProjection::noteRelativeTick(absolutePos, loopStartTick, loopLength);
}

uint32_t noteStorageTick(uint32_t relativeTick, uint32_t loopStartTick, uint32_t loopLength) {
    return IntervalProjection::noteStorageTick(relativeTick, loopStartTick, loopLength);
}

uint32_t selectTickFromPitchbend(int32_t pitchValue, int32_t pitchMin, int32_t pitchMax,
                                 uint32_t navLength) {
    if (navLength == 0) {
        return 0;
    }
    const uint32_t numSteps = navLength / Config::TICKS_PER_16TH_STEP;
    if (numSteps == 0) {
        return 0;
    }
    const int32_t span = pitchMax - pitchMin;
    int32_t step = 0;
    if (span != 0) {
        step = static_cast<int32_t>((static_cast<int64_t>(pitchValue - pitchMin) *
                                     static_cast<int64_t>(numSteps - 1)) /
                                    span);
    }
    if (step < 0) {
        step = 0;
    }
    if (step >= static_cast<int32_t>(numSteps)) {
        step = static_cast<int32_t>(numSteps - 1);
    }
    return static_cast<uint32_t>(step) * Config::TICKS_PER_16TH_STEP;
}

std::vector<SelectNavSlot> buildSelectNavigationSlots(
    uint32_t loopLength,
    uint32_t loopStartTick,
    const std::vector<NoteUtils::DisplayNote>& notes,
    uint32_t selectedTick,
    bool includeSelectedTickIfMissing,
    SelectNavigationBuildStats* stats) {
    std::vector<SelectNavSlot> slots;
    if (stats != nullptr) {
        *stats = SelectNavigationBuildStats{};
    }
    if (loopLength == 0) {
        return slots;
    }

    const uint32_t numSteps = loopLength / Config::TICKS_PER_16TH_STEP;
    if (numSteps == 0) {
        return slots;
    }

    const uint32_t phaseSelectedTick = displayPhaseTick(selectedTick, loopLength);
    const uint32_t centerStep = phaseSelectedTick / Config::TICKS_PER_16TH_STEP;
    std::vector<uint32_t> neighborhoodSteps;
    neighborhoodSteps.reserve(2 * kSelectNavigationNeighborhoodRadiusSteps + 1);
    for (int32_t offset = -static_cast<int32_t>(kSelectNavigationNeighborhoodRadiusSteps);
         offset <= static_cast<int32_t>(kSelectNavigationNeighborhoodRadiusSteps); ++offset) {
        const uint32_t step =
            static_cast<uint32_t>((static_cast<int32_t>(centerStep) + static_cast<int32_t>(numSteps) +
                                   offset) %
                                  static_cast<int32_t>(numSteps));
        if (std::find(neighborhoodSteps.begin(), neighborhoodSteps.end(), step) ==
            neighborhoodSteps.end()) {
            neighborhoodSteps.push_back(step);
        }
    }
    if (stats != nullptr) {
        stats->stepsScanned = static_cast<uint32_t>(neighborhoodSteps.size());
        stats->notesExamined = static_cast<uint32_t>(notes.size());
    }

    std::vector<std::vector<int>> notesInStep(neighborhoodSteps.size());
    for (int i = 0; i < static_cast<int>(notes.size()); ++i) {
        const uint32_t rel = noteRelativeTick(notes[static_cast<size_t>(i)].startTick, loopStartTick,
                                              loopLength);
        const uint32_t noteStep = rel / Config::TICKS_PER_16TH_STEP;
        for (size_t n = 0; n < neighborhoodSteps.size(); ++n) {
            if (noteStep == neighborhoodSteps[n]) {
                notesInStep[n].push_back(i);
                break;
            }
        }
    }

    for (size_t n = 0; n < neighborhoodSteps.size(); ++n) {
        const uint32_t step = neighborhoodSteps[n];
        const uint32_t stepTick = step * Config::TICKS_PER_16TH_STEP;
        std::sort(notesInStep[n].begin(), notesInStep[n].end(), [&](int a, int b) {
            const uint32_t relA = noteRelativeTick(notes[static_cast<size_t>(a)].startTick,
                                                   loopStartTick, loopLength);
            const uint32_t relB = noteRelativeTick(notes[static_cast<size_t>(b)].startTick,
                                                   loopStartTick, loopLength);
            if (relA != relB) {
                return relA < relB;
            }
            return notes[static_cast<size_t>(a)].note < notes[static_cast<size_t>(b)].note;
        });

        if (notesInStep[n].empty()) {
            slots.push_back({stepTick, kInvalidNoteId, -1});
        } else {
            for (int idx : notesInStep[n]) {
                const uint32_t rel = noteRelativeTick(notes[static_cast<size_t>(idx)].startTick,
                                                      loopStartTick, loopLength);
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

    std::sort(slots.begin(), slots.end(), [](const SelectNavSlot& a, const SelectNavSlot& b) {
        if (a.relativeTick != b.relativeTick) {
            return a.relativeTick < b.relativeTick;
        }
        return a.noteIdx < b.noteIdx;
    });

    if (stats != nullptr) {
        stats->slotsEmitted = static_cast<uint32_t>(slots.size());
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
