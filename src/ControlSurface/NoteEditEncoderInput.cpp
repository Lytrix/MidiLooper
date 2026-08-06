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

NOTE_EDIT_MEM void ControlSurfaceManager::processEncoderMovement(int rawDelta) {
    if (rawDelta == 0) {
        return;
    }

    static uint32_t lastEncoderTime = 0;
    const uint32_t now = millis();
    const uint32_t interval = now - lastEncoderTime;
    lastEncoderTime = now;

    int accel = 1;
    switch (editManager.getNoteEditSessionState().kind) {
        case NoteEditKind::Move:
            if (interval < 25) {
                accel = 24;
            } else if (interval < 50) {
                accel = 8;
            } else if (interval < 100) {
                accel = 4;
            }
            break;
        case NoteEditKind::Length:
            if (interval < 25) {
                accel = 8;
            } else if (interval < 50) {
                accel = 4;
            } else if (interval < 100) {
                accel = 2;
            }
            break;
        case NoteEditKind::Pitch:
            if (interval < 50) {
                accel = 4;
            } else if (interval < 75) {
                accel = 3;
            } else if (interval < 100) {
                accel = 2;
            }
            break;
        default:
            if (interval < 50) {
                accel = 4;
            } else if (interval < 75) {
                accel = 3;
            } else if (interval < 100) {
                accel = 2;
            }
            break;
    }

    const int finalDelta = rawDelta * accel;
    if (editManager.getCurrentState() != nullptr) {
        editManager.onEncoderTurn(trackManager.getSelectedTrack(), finalDelta);
    }
}
