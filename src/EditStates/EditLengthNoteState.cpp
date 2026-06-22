//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "EditLengthNoteState.h"
#include "EditManager.h"
#include "Track.h"
#include "Logger.h"
#include "Utils/MidiEventVecFnvHash.h"
#include "Utils/NoteMovementUtils.h"
#include "NoteEditFocus.h"

void EditLengthNoteState::onEnter(EditManager& manager, Track& track, uint32_t startTick) {
    logger.debug("EditLengthNoteState::onEnter");

    initialHash = midiEventVecFnv1aHash(track.editAwareMidiEvents());

    manager.selectClosestNote(track, startTick);

    if (manager.getSelectedNoteIdx() >= 0) {
        const NoteUtils::DisplayNote selected = manager.liveEditDisplayNoteAtSelect(track);
        targetRef_ = {track.getMidiChannel(), selected.note, selected.startTick,
                      selected.endTick};
        const uint32_t loopLength = track.getLoopLength();
        manager.setBracketTick(selected.endTick % loopLength);
        logger.info("EditLengthNoteState: selected note for length edit, bracket at end %lu",
                    static_cast<unsigned long>(selected.endTick % loopLength));
    } else {
        logger.info("EditLengthNoteState: No note selected, will create new note");
    }
}

void EditLengthNoteState::onExit(EditManager& manager, Track& track) {
    logger.debug("EditLengthNoteState::onExit");
}

void EditLengthNoteState::onEncoderTurn(EditManager& manager, Track& track, int delta) {
    logger.debug("EditLengthNoteState::onEncoderTurn called with delta=%d", delta);

    if (manager.getSelectedNoteIdx() < 0 || delta == 0) {
        return;
    }

    const uint32_t loopLength = track.getLoopLength();
    if (loopLength == 0) {
        return;
    }

    const NoteUtils::DisplayNote liveNote = manager.liveEditDisplayNoteAtSelect(track);
    manager.ensureNoteEditFocusForLiveEdit(track, liveNote);

    const uint8_t notePitch = liveNote.note;
    const uint32_t noteStart = liveNote.startTick;
    uint32_t currentEnd = liveNote.endTick;
    const NoteEditFocus& focus = manager.getNoteEditSession().focus;
    if (focus.active && focus.last.pitch == liveNote.note &&
        focus.last.startTick == liveNote.startTick) {
        currentEnd = focus.last.endTick;
    }

    const int lengthDelta = delta;
    uint32_t newEnd;
    if (lengthDelta >= 0) {
        newEnd = currentEnd + static_cast<uint32_t>(lengthDelta);
    } else {
        const uint32_t deltaAbs = static_cast<uint32_t>(-lengthDelta);
        const uint32_t minEnd = noteStart + 1;
        if (currentEnd <= minEnd) {
            newEnd = minEnd;
        } else if (deltaAbs >= (currentEnd - noteStart)) {
            newEnd = minEnd;
        } else {
            newEnd = currentEnd - deltaAbs;
            if (newEnd <= noteStart) {
                newEnd = minEnd;
            }
        }
    }

    uint32_t newLength;
    if (newEnd >= noteStart) {
        newLength = newEnd - noteStart;
    } else {
        newLength = (loopLength - noteStart) + newEnd;
    }
    if (newLength > loopLength) {
        newEnd = noteStart + loopLength;
    }

    logger.debug("EditLengthNoteState: changing note end from %lu to %lu",
                 static_cast<unsigned long>(currentEnd),
                 static_cast<unsigned long>(newEnd));

    NoteUtils::DisplayNote selected = liveNote;
    if (focus.active) {
        selected = {focus.last.pitch, focus.last.velocity, focus.last.startTick, focus.last.endTick};
    }
    NoteMovementUtils::changeLengthWithOverlapHandling(track, manager, selected, newEnd);

    const std::vector<NoteUtils::DisplayNote> updatedNotes =
        manager.selectableDisplayNotesAtEditSelect(track);
    for (int i = 0; i < static_cast<int>(updatedNotes.size()); ++i) {
        if (updatedNotes[static_cast<size_t>(i)].note == notePitch &&
            updatedNotes[static_cast<size_t>(i)].startTick == noteStart) {
            manager.setSelectedNoteIdx(i);
            break;
        }
    }
}

void EditLengthNoteState::onButtonPress(EditManager& manager, Track& track) {
    logger.debug("EditLengthNoteState::onButtonPress - exiting length edit mode");
}
