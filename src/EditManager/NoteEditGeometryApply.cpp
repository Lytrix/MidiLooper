//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "NoteEditGeometryApply.h"
#include "Utils/NoteEditMem.h"

namespace NoteEditGeometryApply {

NOTE_EDIT_MEM bool applyNoteEditChange(Track& track, EditManager& manager, NoteEditChangeKind kind,
                                     const NoteUtils::DisplayNote& currentNote, uint32_t targetTick,
                                     int delta, uint32_t targetEndTick, uint8_t currentPitch,
                                     uint8_t newPitch, uint32_t& inOutStart, uint32_t& inOutEnd,
                                     bool refreshPlaybackPreview) {
    switch (kind) {
        case NoteEditChangeKind::Move:
            return moveNoteWithOverlapHandling(track, manager, currentNote, targetTick, delta,
                                               refreshPlaybackPreview);
        case NoteEditChangeKind::Length:
            changeLengthWithOverlapHandling(track, manager, currentNote, targetEndTick,
                                            refreshPlaybackPreview);
            return true;
        case NoteEditChangeKind::Pitch:
            inOutStart = currentNote.startTick;
            inOutEnd = currentNote.endTick;
            return applyPitchChange(track, manager, currentPitch, newPitch, inOutStart, inOutEnd,
                                    refreshPlaybackPreview);
    }
    return false;
}

}  // namespace NoteEditGeometryApply
