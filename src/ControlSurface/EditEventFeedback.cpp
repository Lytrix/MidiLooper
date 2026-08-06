//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "ControlSurfaceManager.h"

#include <Arduino.h>

#include "EditManager.h"
#include "Globals.h"
#include "Logger.h"
#include "LoopEditManager.h"
#include "MidiConfig.h"
#include "Track.h"
#include "TrackManager.h"
#include "Utils/NoteEditFaderOutboundPlan.h"
#include "Utils/NoteEditMem.h"
#include "Utils/NoteUtils.h"

NOTE_EDIT_MEM void ControlSurfaceManager::cycleEditSession(Track& track) {
    const EditSessionType priorSession = editManager.getEditSessionType();
    editManager.cycleEditSession(track);
    if (priorSession == EditSessionType::Note) {
        releaseEditedNoteAudition();
    }
}
NOTE_EDIT_MEM void ControlSurfaceManager::prepareNoteEditSessionOpen() {
    suppressSelectDependentMotorSync_ = true;
    clearPendingSelectDependentMotorSync();
    clearPendingGeometryDriverMotorSync();
    lastGeometryF1SyncedBracketTick_ = UINT32_MAX;
    startEditingEnabled = false;
}

NOTE_EDIT_MEM void ControlSurfaceManager::scheduleNoteSelectFaderSync(Track& track) {
    noteSelectionTime = millis();
    startEditingEnabled = false;
    requestFaderOutbound(NoteEditFaderOutbound::Trigger::NoteSelectWithFader1);
    (void)track;
}

NOTE_EDIT_MEM void ControlSurfaceManager::sendNoteEditSessionFaderFeedback(Track& track) {
    if (editManager.getEditSessionType() != EditSessionType::Note) {
        return;
    }

    resetSelectNavSlotApplyState();
    noteSelectionTime = millis();
    startEditingEnabled = false;
    if constexpr (!kNoteEditFaderFeedbackEnabled) {
        suppressSelectDependentMotorSync_ = false;
        logger.info("NOTE_EDIT session fader sync: feedback disabled");
        (void)track;
        return;
    }
    requestFaderOutbound(NoteEditFaderOutbound::Trigger::SessionOpen);
    logger.info("NOTE_EDIT session fader sync: outbound coordinator");
    (void)track;
}
NOTE_EDIT_MEM void ControlSurfaceManager::resetLengthEditingModeOnSessionBoundary() {
    resetSelectNavSlotApplyState();
    suppressSelectDependentMotorSync_ = false;
    const bool wasLength = editManager.isLengthEditingMode();
    editManager.clearLengthEditingMode(false);
    if (!wasLength) {
        return;
    }
    currentDriverFader = MidiMapping::FaderType::FADER_SELECT;
    logger.info("[MIDI] Length editing mode DISABLED (edit session boundary)");
}

NOTE_EDIT_MEM void ControlSurfaceManager::resetLengthEditingModeOnNoteSelect() {
    if (editManager.isLengthEditingMode()) {
        editManager.clearLengthEditingModeOnNoteSelect();
        logger.info("[MIDI] Length editing mode DISABLED (note select)");
    }
    currentDriverFader = MidiMapping::FaderType::FADER_SELECT;
    lastUserCoarseFaderValue = 0;
    lastCoarseFaderTime = 0;
    lastUserNoteValueCc = 64;
    lastNoteValueFaderTime = 0;
    lastUserFineCc = 64;
    lastFineFaderTime = 0;
}

NOTE_EDIT_MEM void ControlSurfaceManager::toggleLengthEditingMode() {
    uint32_t now = millis();

    if (now - lastLengthModeToggleTime < LENGTH_MODE_DEBOUNCE_TIME) {
        logger.log(CAT_MIDI, LOG_DEBUG, "Length mode toggle ignored (debounce protection)");
        return;
    }
    lastLengthModeToggleTime = now;

    Track& track = trackManager.getSelectedTrack();
    const bool wasLength = editManager.isLengthEditingMode();
    editManager.toggleLengthEditMode(track);
    if (wasLength && !editManager.isLengthEditingMode()) {
        currentDriverFader = MidiMapping::FaderType::FADER_SELECT;
        lastDriverFaderTime = now;
        lastUserCoarseFaderValue = 0;
        lastCoarseFaderTime = 0;
    }
}
NOTE_EDIT_MEM void ControlSurfaceManager::onEditEvent(EditEvent event) {
    Track& track = trackManager.getSelectedTrack();
    switch (event) {
        case EditEvent::SessionOpened:
            handleSessionOpenedEvent(track);
            break;
        case EditEvent::SessionClosed:
            handleSessionClosedEvent(track);
            break;
        case EditEvent::LengthModeChanged:
            handleLengthModeChangedEvent(track);
            break;
        case EditEvent::SelectionChanged:
            handleSelectionChangedEvent(track);
            break;
        case EditEvent::GeometryChanged:
            handleGeometryChangedEvent(track);
            break;
        default:
            break;
    }
}


NOTE_EDIT_MEM void ControlSurfaceManager::sendEditSessionMidi(EditSessionType sessionType) {
    uint8_t program = MidiConfig::SessionProgram::LOOP_EDIT;
    uint8_t triggerNote = 0;
    const char* modeName = "LOOP_EDIT";

    switch (sessionType) {
        case EditSessionType::Loop:
            program = MidiConfig::SessionProgram::LOOP_EDIT;
            triggerNote = 100;
            modeName = "LOOP_EDIT";
            break;
        case EditSessionType::Note:
            program = MidiConfig::SessionProgram::NOTE_EDIT;
            triggerNote = 0;
            modeName = "NOTE_EDIT";
            break;
        case EditSessionType::ControlChange:
            return;
    }

    midiHandler.sendProgramChange(MidiConfig::PROGRAM_CHANGE_CHANNEL, program);
    midiHandler.sendLedFeedbackNoteOn(triggerNote, 64);
    delay(10);
    midiHandler.sendLedFeedbackNoteOff(triggerNote);
    logger.log(CAT_MIDI, LOG_INFO, "Edit session: %s (Program %d, Note %d trigger)",
               modeName, program, triggerNote);
}

NOTE_EDIT_MEM void ControlSurfaceManager::handleSessionOpenedEvent(Track& track) {
    if (editManager.sessionOpenedIncludesMidi()) {
        sendEditSessionMidi(editManager.getEditSessionType());
    }
    if (editManager.sessionOpenedIncludesFaderFeedback() && editManager.isNoteEditActive()) {
        prepareNoteEditSessionOpen();
        sendNoteEditSessionFaderFeedback(track);
        drainFaderOutboundUntilIdle();
    }
}

NOTE_EDIT_MEM void ControlSurfaceManager::handleSessionClosedEvent(Track& track) {
    releaseEditedNoteAudition();
    (void)track;
}

NOTE_EDIT_MEM void ControlSurfaceManager::handleGeometryChangedEvent(Track& track) {
    resetLengthEditingModeOnNoteSelect();
    releaseEditedNoteAudition();
    (void)track;
}

NOTE_EDIT_MEM void ControlSurfaceManager::handleLengthModeChangedEvent(Track& track) {
    const std::vector<NoteUtils::DisplayNote> notes = editManager.selectableDisplayNotesForEditUi(track);
    const int selectedIdx = editManager.getSelectedNoteIdx();
    if (notes.empty() || selectedIdx < 0 || selectedIdx >= static_cast<int>(notes.size())) {
        return;
    }
    releaseEditedNoteAudition();
    if (editManager.isLengthEditingMode()) {
        requestFaderOutbound(NoteEditFaderOutbound::Trigger::LengthModeEnter);
    } else {
        requestFaderOutbound(NoteEditFaderOutbound::Trigger::LengthModeExit);
    }
    (void)track;
}

NOTE_EDIT_MEM void ControlSurfaceManager::handleSelectionChangedEvent(Track& track) {
    const EditorSelection& prior = editManager.selectionChangePrior();
    const EditorSelection& next = editManager.getNoteEditSessionState().selection;
    if (editManager.selectionChangeRequestFaderSync()) {
        scheduleNoteSelectFaderSync(track);
        return;
    }
    scheduleSelectDependentMotorSync(track, prior, next);
}

NOTE_EDIT_MEM void ControlSurfaceManager::onTrackChanged(Track& newTrack) {
    releaseEditedNoteAudition();
    // If we're in loop edit mode, send the new track's loop length as CC feedback
    if (editManager.getEditSessionType() == EditSessionType::Loop) {
        loopEditManager.onTrackChanged(newTrack);
    }
}