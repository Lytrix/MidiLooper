//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "EditNoteHomeState.h"
#include "EditManager.h"
#include "Track.h"
#include "Logger.h"

void EditNoteHomeState::onEnter(EditManager& manager, Track& track, uint32_t startTick) {
    manager.selectClosestNote(track, startTick);
}

void EditNoteHomeState::onExit(EditManager& manager, Track& track) {
    (void)manager;
    (void)track;
}

void EditNoteHomeState::onEncoderTurn(EditManager& manager, Track& track, int delta) {
    manager.moveBracket(track, delta);
}

void EditNoteHomeState::onButtonPress(EditManager& manager, Track& track) {
    manager.switchToNextState(track);
}
