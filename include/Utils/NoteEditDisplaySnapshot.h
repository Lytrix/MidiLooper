//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#pragma once

#include <cstdint>
#include <vector>

#include "MidiEvent.h"
#include "NoteEditFocus.h"
#include "NoteEditSessionState.h"
#include "Utils/NoteUtils.h"

namespace NoteEditDisplaySnapshot {

struct DisplayNoteInfoSnapshot {
    NoteId noteId = kInvalidNoteId;
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

inline DisplayNoteInfoSnapshot buildDisplayNoteInfoSnapshotFromNoteId(
    NoteId noteId, uint8_t pitch, uint32_t storageStart, uint32_t loopStartTick,
    uint32_t loopLength) {
    DisplayNoteInfoSnapshot snap;
    snap.noteId = noteId;
    snap.pitch = pitch;
    snap.storageStart = storageStart;
    snap.displayStartTick =
        displayStartTickFromStorage(storageStart, loopStartTick, loopLength);
    return snap;
}

inline DisplayNoteInfoSnapshot buildDisplayNoteInfoSnapshotForSelection(
    const EditorSelection& selection, uint32_t loopStartTick, uint32_t loopLength) {
    if (!editorSelectionHasNote(selection)) {
        return {};
    }
    (void)loopStartTick;
    (void)loopLength;
    DisplayNoteInfoSnapshot snap;
    snap.noteId = selection.primaryNote;
    return snap;
}

inline bool displayNoteInfoChanged(const DisplayNoteInfoSnapshot& prior,
                                   const DisplayNoteInfoSnapshot& next) {
    return prior.noteId != next.noteId || prior.pitch != next.pitch ||
           prior.storageStart != next.storageStart ||
           prior.displayStartTick != next.displayStartTick;
}

inline int filteredDisplayNoteIndexForSelection(
    const EditorSelection& selection, const std::vector<NoteUtils::DisplayNote>& notes) {
    if (!editorSelectionHasNote(selection)) {
        return -1;
    }
    return filteredDisplayNoteIndexForNoteIdAndStart(notes, selection.primaryNote,
                                                     selection.bracketTick);
}

}  // namespace NoteEditDisplaySnapshot
