//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "EditPitchNoteState.h"
#include "EditManager.h"
#include "Track.h"
#include "Logger.h"
#include "Utils/MidiEventVecFnvHash.h"
#include "Utils/NoteMovementUtils.h"
#include "NoteEditFocus.h"

void EditPitchNoteState::onEnter(EditManager& manager, Track& track, uint32_t startTick) {
    logger.debug("Entered EditPitchNoteState");
    initialHash = midiEventVecFnv1aHash(track.editAwareMidiEvents());
    logger.debug("EditPitchNoteState enter, initial hash: %u", initialHash);

    if (manager.getSelectedNoteIdx() >= 0) {
        const NoteUtils::DisplayNote selected = manager.liveEditDisplayNoteAtSelect(track);
        targetNoteId_ = selected.noteId;
        const uint32_t loopLength = track.getLoopLength();
        manager.setBracketTick(selected.startTick % loopLength);
        logger.debug("EditPitchNoteState: bracket at note start %lu",
                     static_cast<unsigned long>(selected.startTick % loopLength));
    }
}

void EditPitchNoteState::onExit(EditManager& manager, Track& track) {
    logger.debug("Exited EditPitchNoteState");
}

void EditPitchNoteState::onEncoderTurn(EditManager& manager, Track& track, int delta) {
    if (manager.getSelectedNoteIdx() < 0 || delta == 0) {
        return;
    }

    const NoteUtils::DisplayNote liveNote = manager.liveEditDisplayNoteAtSelect(track);
    manager.ensureNoteEditFocusForLiveEdit(track, liveNote);

    const int newPitch = (static_cast<int>(liveNote.note) + delta + 128) % 128;
    if (newPitch == static_cast<int>(liveNote.note)) {
        return;
    }

    uint32_t noteStart = liveNote.startTick;
    uint32_t noteEnd = liveNote.endTick;
    NoteUtils::DisplayNote pitchTarget = liveNote;
    pitchTarget.startTick = noteStart;
    pitchTarget.endTick = noteEnd;
    NoteMovementUtils::applyNoteEditChange(
        track, manager, NoteMovementUtils::NoteEditChangeKind::Pitch, pitchTarget, 0, 0, 0,
        liveNote.note, static_cast<uint8_t>(newPitch), noteStart, noteEnd);
}

void EditPitchNoteState::onButtonPress(EditManager& manager, Track& track) {
    (void)manager;
    (void)track;
}
