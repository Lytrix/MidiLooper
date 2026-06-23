//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "EditStartNoteState.h"
#include "EditManager.h"
#include "Track.h"
#include "Logger.h"
#include "Utils/MidiEventVecFnvHash.h"
#include "Utils/NoteMovementUtils.h"
#include "Utils/NoteMovementWrap.h"
#include "NoteEditFocus.h"

void EditStartNoteState::onEnter(EditManager& manager, Track& track, uint32_t startTick) {
    logger.debug("Entered EditStartNoteState");

    initialHash = midiEventVecFnv1aHash(track.editAwareMidiEvents());
    logger.debug("EditStartNoteState enter, initial hash: %u", initialHash);

    const int idx = manager.getSelectedNoteIdx();
    if (idx >= 0) {
        const std::vector<NoteUtils::DisplayNote> notes =
            manager.selectableDisplayNotesAtEditSelect(track);
        if (idx < static_cast<int>(notes.size())) {
            const NoteUtils::DisplayNote& selected = notes[static_cast<size_t>(idx)];
            manager.rebuildNoteEditFocusForDisplayNote(track, selected);
            manager.setBracketTick(selected.startTick);
            logger.debug("Set up move from focus: note=%d, start=%lu, end=%lu",
                         selected.note,
                         static_cast<unsigned long>(selected.startTick),
                         static_cast<unsigned long>(selected.endTick));
        }
    } else {
        manager.selectClosestNote(track, startTick);
    }
}

void EditStartNoteState::onExit(EditManager& manager, Track& track) {
    (void)manager;
    (void)track;
    logger.debug("Exited EditStartNoteState");
}

void EditStartNoteState::onEncoderTurn(EditManager& manager, Track& track, int delta) {
    logger.debug("EditStartNoteState::onEncoderTurn called with delta=%d", delta);

    const uint32_t loopLength = track.getLoopLength();
    if (loopLength == 0 || delta == 0) {
        return;
    }
    if (manager.getSelectedNoteIdx() < 0) {
        logger.debug("EditStartNoteState: No note selected");
        return;
    }

    const NoteUtils::DisplayNote liveNote = manager.liveEditDisplayNoteAtSelect(track);
    manager.ensureNoteEditFocusForLiveEdit(track, liveNote);

    uint32_t fromStart = liveNote.startTick;
    const NoteEditFocus& focus = manager.getEditSession().focus;
    if (focus.active && focus.last.pitch == liveNote.note &&
        focus.last.startTick == liveNote.startTick) {
        fromStart = focus.last.startTick;
    }

    const int32_t rawNewStart = static_cast<int32_t>(fromStart) + delta;
    const uint32_t newStart = NoteMovementUtils::wrapPosition(rawNewStart, loopLength);

    NoteUtils::DisplayNote currentNote = liveNote;
    if (focus.active) {
        currentNote = {focus.last.pitch, focus.last.velocity, focus.last.startTick,
                       focus.last.endTick};
    }

    uint32_t dummyStart = currentNote.startTick;
    uint32_t dummyEnd = currentNote.endTick;
    NoteMovementUtils::applyNoteEditChange(track, manager, NoteMovementUtils::NoteEditChangeKind::Move,
                                           currentNote, newStart, delta, 0, 0, 0, dummyStart,
                                           dummyEnd);
}

void EditStartNoteState::onButtonPress(EditManager& manager, Track& track) {
    manager.setState(manager.getNoteHomeState(), track, manager.getBracketTick());
}
