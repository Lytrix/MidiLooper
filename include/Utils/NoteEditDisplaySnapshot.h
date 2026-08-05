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

template <typename NotesVec>
inline int filteredDisplayNoteIndexForSelection(const EditorSelection& selection,
                                                const NotesVec& notes,
                                                uint32_t loopStartTick = 0,
                                                uint32_t loopLength = 0,
                                                bool lengthBracket = false) {
    if (!editorSelectionHasNote(selection)) {
        return -1;
    }
    if (lengthBracket) {
        return filteredDisplayNoteIndexForNoteIdAndEnd(notes, selection.primaryNote,
                                                       selection.selectedTick, loopStartTick,
                                                       loopLength);
    }
    return filteredDisplayNoteIndexForNoteIdAndStart(notes, selection.primaryNote,
                                                     selection.selectedTick, loopStartTick,
                                                     loopLength);
}

/// Identity-first NOTE_EDIT highlight index: primaryNote + focus.last, then selection tick.
template <typename NotesVec>
inline int resolveNoteEditHighlightIndex(const EditorSelection& selection,
                                         const NotesVec& notes, const NoteEditFocus& focus,
                                         uint32_t loopStartTick, uint32_t loopLength,
                                         bool lengthBracket) {
    if (!editorSelectionHasNote(selection) || loopLength == 0) {
        return -1;
    }
    if (focus.active && focus.movingNoteId == selection.primaryNote) {
        const uint32_t storageBracket =
            lengthBracket ? focus.last.endTick : focus.last.startTick;
        const uint32_t displayBracket =
            displayStartTickFromStorage(storageBracket, loopStartTick, loopLength);
        if (int byFocus = filteredDisplayNoteIndexForNoteIdAndStart(
                notes, focus.movingNoteId, displayBracket, loopStartTick, loopLength);
            byFocus >= 0) {
            return byFocus;
        }
    }
    return filteredDisplayNoteIndexForSelection(selection, notes, loopStartTick, loopLength,
                                                lengthBracket);
}

}  // namespace NoteEditDisplaySnapshot
