//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#include "EditNoteState.h"
#include "EditManager.h"
#include "Track.h"
#include "Logger.h"

void EditNoteState::onEnter(EditManager& manager, Track& track, uint32_t startTick) {
    manager.selectClosestNote(track, startTick);
}

void EditNoteState::onExit(EditManager& manager, Track& track) {
    // Cleanup if needed
}

void EditNoteState::onEncoderTurn(EditManager& manager, Track& track, int delta) {
    manager.moveBracket(track, delta);
}

void EditNoteState::onButtonPress(EditManager& manager, Track& track) {
    manager.switchToNextState(track);
}
