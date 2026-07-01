//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#pragma once

#include <cstdint>
#include <vector>

#include "EditPass.h"
#include "NoteEditSessionState.h"
#include "Utils/NoteUtils.h"

namespace NoteEditDisplaySnapshot {

struct DisplayNoteInfoSnapshot {
    uint8_t pitch = 255;
    uint32_t storageStart = UINT32_MAX;
    uint32_t displayStartTick = UINT32_MAX;
};

inline uint32_t displayStartTickFromStorage(uint32_t storageStart, uint32_t loopStartTick,
                                            uint32_t loopLength) {
    if (loopLength == 0) {
        return 0;
    }
    const uint32_t displayStart =
        (storageStart >= loopStartTick)
            ? (storageStart - loopStartTick)
            : (storageStart + loopLength - loopStartTick);
    return displayStart % loopLength;
}

inline DisplayNoteInfoSnapshot buildDisplayNoteInfoSnapshotFromRef(const NoteRef& ref,
                                                                   uint32_t loopStartTick,
                                                                   uint32_t loopLength) {
    DisplayNoteInfoSnapshot snap;
    snap.pitch = ref.note;
    snap.storageStart = ref.startTick;
    snap.displayStartTick =
        displayStartTickFromStorage(ref.startTick, loopStartTick, loopLength);
    return snap;
}

inline DisplayNoteInfoSnapshot buildDisplayNoteInfoSnapshotForSelection(
    const NoteEditSelection& selection, uint32_t loopStartTick, uint32_t loopLength) {
    if (!selection.hasNote) {
        return {};
    }
    return buildDisplayNoteInfoSnapshotFromRef(selection.ref, loopStartTick, loopLength);
}

inline bool displayNoteInfoChanged(const DisplayNoteInfoSnapshot& prior,
                                   const DisplayNoteInfoSnapshot& next) {
    return prior.pitch != next.pitch || prior.storageStart != next.storageStart ||
           prior.displayStartTick != next.displayStartTick;
}

inline int filteredDisplayNoteIndexForSelection(
    const NoteEditSelection& selection, const std::vector<NoteUtils::DisplayNote>& notes,
    uint8_t channel) {
    if (!selection.hasNote) {
        return -1;
    }
    for (int i = 0; i < static_cast<int>(notes.size()); ++i) {
        const NoteUtils::DisplayNote& dn = notes[static_cast<size_t>(i)];
        const NoteRef candidate = {channel, dn.note, dn.startTick, dn.endTick};
        if (noteRefSameTarget(selection.ref, candidate)) {
            return i;
        }
    }
    return -1;
}

}  // namespace NoteEditDisplaySnapshot
