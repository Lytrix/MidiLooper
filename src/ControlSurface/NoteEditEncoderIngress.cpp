//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "ControlSurfaceManager.h"

#include "EditManager.h"
#include "Globals.h"
#include "LooperState.h"
#include "NoteEditSessionState.h"
#include "TrackManager.h"
#include "Utils/NoteEditMem.h"

NOTE_EDIT_MEM void ControlSurfaceManager::updateGpioEncoderButtonHold(bool encoderButtonHeld) {
    const uint32_t now = millis();
    if (encoderButtonHeld && !gpioEncoderButtonWasHeld_) {
        gpioEncoderButtonHoldStartMs_ = now;
    }
    if (encoderButtonHeld && (now - gpioEncoderButtonHoldStartMs_ >= kGpioEncoderHoldDelayMs) &&
        !looperState.isLoadSaveModeActive() &&
        (editManager.getNoteEditSessionState().kind == NoteEditKind::Select ||
         editManager.getNoteEditSessionState().kind == NoteEditKind::Move)) {
        if (!gpioEncoderPitchEditActive_) {
            editManager.applyGeometryKindFromControl(trackManager.getSelectedTrack(),
                                                     NoteEditKind::Pitch, false);
            editManager.syncNoteEditSessionStateToUi(trackManager.getSelectedTrack());
            gpioEncoderPitchEditActive_ = true;
        }
    }
    if (!encoderButtonHeld && gpioEncoderButtonWasHeld_) {
        if (editManager.getNoteEditSessionState().kind == NoteEditKind::Pitch) {
            Track& track = trackManager.getSelectedTrack();
            editManager.applySelectNav(track, editManager.getSelectedTick(),
                                       editManager.getLastFader1SelectNoteId());
            if (editManager.getSelectedNoteIdx() >= 0) {
                scheduleNoteSelectFaderSync(track);
            }
        }
        gpioEncoderButtonHoldStartMs_ = 0;
        gpioEncoderPitchEditActive_ = false;
    }
    gpioEncoderButtonWasHeld_ = encoderButtonHeld;
}
