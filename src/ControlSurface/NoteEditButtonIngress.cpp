//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "ControlSurfaceManager.h"

#include <array>

#include "BarStepButtonHandler.h"
#include "ClockManager.h"
#include "Globals.h"
#include "EditManager.h"
#include "EditStates/EditSelectNoteState.h"
#include "Logger.h"
#include "NoteEditSessionState.h"
#include "TrackManager.h"
#include "Utils/NoteEditMem.h"
#include "Utils/NoteUtils.h"
#include "Utils/SelectNavigation.h"

namespace {

NOTE_EDIT_MEM constexpr uint32_t kBracketSnapWindow = 24;

NOTE_EDIT_MEM bool hasNoteNearBracket(const Track& track, uint32_t selectedTick) {
    const uint32_t loopLength = track.getLoopLength();
    if (loopLength == 0) {
        return false;
    }
    const uint32_t loopStartTick = track.getLoopStartTick();
    const uint32_t bracket =
        SelectNavigation::displayPhaseTick(selectedTick, loopLength);
    const auto& notes = track.getCachedNotes();
    for (const auto& n : notes) {
        const uint32_t noteTick =
            SelectNavigation::noteRelativeTick(n.startTick, loopStartTick, loopLength);
        const uint32_t dist = std::min((noteTick + loopLength - bracket) % loopLength,
                                       (bracket + loopLength - noteTick) % loopLength);
        if (dist <= kBracketSnapWindow) {
            return true;
        }
    }
    return false;
}

}  // namespace

NOTE_EDIT_MEM void ControlSurfaceManager::handleCycleNoteEditType(Track& track) {
    const bool wasInEditOverlay = editManager.getCurrentState() != nullptr;

    if (editManager.getEditSessionType() != EditSessionType::Note) {
        editManager.sendEditSessionChange(EditSessionType::Note);
    }

    if (!shouldCycleNoteEditTypeOnShortPress(wasInEditOverlay)) {
        if (!editManager.isNoteEditActive()) {
            editManager.openNoteEditSession(track);
            editManager.emitSessionOpenedToSurface(false, true);
        } else if (editManager.getCurrentState() == nullptr) {
            editManager.enterDefaultNoteEditSessionState(track, clockManager.getCurrentTick());
            scheduleNoteSelectFaderSync(track);
        }
        logger.info("MIDI Encoder: Short press - entered note edit mode");
        return;
    }

    editManager.cycleNoteEditType(track);
}

NOTE_EDIT_MEM void ControlSurfaceManager::handleExitEditMode(Track& track) {
    logger.info("MIDI Encoder: Long press - exited edit mode");
    editManager.exitEditMode(track);
    releaseEditedNoteAudition();
}

NOTE_EDIT_MEM void ControlSurfaceManager::handleDeleteSelectedNote(Track& track) {
    const auto filteredStd = editManager.selectableDisplayNotesForEditUi(track);
    const NoteUtils::DisplayNoteVec filtered(filteredStd.begin(), filteredStd.end());
    if (editManager.deleteSelectedNote(track, filtered)) {
        releaseEditedNoteAudition();
    }
}

NOTE_EDIT_MEM void ControlSurfaceManager::handleCreateNoteAtBracket(Track& track) {
    if (editManager.getCurrentState() == nullptr) {
        logger.info("Create note ignored (not in edit mode)");
        return;
    }
    if (!editManager.isNoteEditActive()) {
        editManager.openNoteEditSession(track);
    }
    const uint32_t loopLength = track.getLoopLength();
    if (loopLength == 0) {
        return;
    }

    const uint32_t selectedTick = editManager.getSelectedTick() % loopLength;
    if (hasNoteNearBracket(track, selectedTick)) {
        logger.info("Create note ignored (note at bracket)");
        return;
    }

    const uint32_t loopStartTick = track.getLoopStartTick();
    const uint32_t bracketRel =
        SelectNavigation::displayPhaseTick(selectedTick, loopLength);
    const uint32_t storageTick =
        SelectNavigation::noteStorageTick(bracketRel, loopStartTick, loopLength);

    editManager.setSelectedNoteIdx(-1);
    editManager.beginGeometryMutation(track, NoteEditKind::Add, false);
    const std::array<MidiEvent, 2> created =
        EditSelectNoteState::createNoteAtTick(track, storageTick);
    EditPass add{};
    add.passType = EditPassType::Note;
    add.actionType = EditActionType::Create;
    add.propertyType = EditPropertyType::None;
    add.addedEvents.push_back(created[0]);
    add.addedEvents.push_back(created[1]);
    const EditPassId id = editManager.commitEditAction(track, EditPassVec{add});
    if (id == kInvalidEditPassId) {
        logger.info("Create note failed (edit session commit rejected)");
        return;
    }
    editManager.setSelectedTick(selectedTick);
    track.invalidateCaches();
    editManager.selectNoteAtBracket(track, bracketRel);
    scheduleNoteSelectFaderSync(track);
}

NOTE_EDIT_MEM void ControlSurfaceManager::handleDeleteOrCreateNoteAtBracket(Track& track) {
    if (editManager.getCurrentState() == nullptr) {
        logger.info("NOTELEN double: ignored (not in edit mode)");
        return;
    }
    if (editManager.getSelectedNoteIdx() >= 0 ||
        editManager.getLastFader1SelectNoteId() != kInvalidNoteId) {
        logger.info("NOTELEN double: delete selected note");
        handleDeleteSelectedNote(track);
        return;
    }
    const uint32_t loopLength = track.getLoopLength();
    if (loopLength == 0) {
        return;
    }
    const uint32_t selectedTick = editManager.getSelectedTick() % loopLength;
    if (hasNoteNearBracket(track, selectedTick)) {
        logger.info("NOTELEN double: ignored (note at bracket, none selected)");
        return;
    }
    logger.info("NOTELEN double: create note at bracket");
    handleCreateNoteAtBracket(track);
}

NOTE_EDIT_MEM void ControlSurfaceManager::handleBarStepNoteEditGesture(Track& track,
                                                                       const BarStepButtonInfo& info,
                                                                       BarStepPressType pressType) {
    if (info.type != BarStepButtonType::SIXTEENTH) {
        return;
    }
    if (editManager.getCurrentState() == nullptr) {
        return;
    }

    const uint32_t loopLength = track.getLoopLength();
    if (loopLength == 0) {
        return;
    }

    const uint32_t loopStartTick = track.getLoopStartTick();
    const uint32_t stepTick =
        (loopStartTick + info.stepIndex * Config::TICKS_PER_16TH_STEP) % loopLength;

    editManager.setSelectedTick(stepTick);
    editManager.selectClosestNote(track, stepTick);

    auto noteAtStep = [&]() -> bool {
        const int idx = editManager.getSelectedNoteIdx();
        if (idx < 0) {
            return false;
        }
        const auto& notes = track.getCachedNotes();
        if (idx >= static_cast<int>(notes.size())) {
            return false;
        }
        const uint32_t noteTick = notes[static_cast<size_t>(idx)].startTick % loopLength;
        const uint32_t dist = std::min((noteTick + loopLength - stepTick) % loopLength,
                                       (stepTick + loopLength - noteTick) % loopLength);
        return dist <= kBracketSnapWindow;
    };

    switch (pressType) {
        case BarStepPressType::SHORT_PRESS:
            if (!noteAtStep()) {
                editManager.setSelectedNoteIdx(-1);
            }
            if (editManager.getSelectedNoteIdx() >= 0) {
                scheduleNoteSelectFaderSync(track);
            }
            break;

        case BarStepPressType::DOUBLE_PRESS:
            if (noteAtStep()) {
                handleDeleteSelectedNote(track);
            } else {
                editManager.setSelectedNoteIdx(-1);
                editManager.setState(editManager.getSelectNoteState(), track, stepTick);
                editManager.onButtonPress(track);
                editManager.setState(editManager.getNoteHomeState(), track, stepTick);
            }
            break;

        default:
            break;
    }
}
