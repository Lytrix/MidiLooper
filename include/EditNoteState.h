//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#pragma once
#include <cstdint>

class EditManager;
class Track;

/**
 * @class EditNoteState
 * @brief Abstract interface for note-edit state machine overlays.
 *
 * Subclasses implement onEnter/onExit/onEncoderTurn/onButtonPress/getName for each
 * note-edit overlay (select, start, pitch, length, home).
 */
class EditNoteState {
public:
    virtual ~EditNoteState() {}
    virtual void onEnter(EditManager& manager, Track& track, uint32_t startTick) {}
    virtual void onExit(EditManager& manager, Track& track) {}
    virtual void onEncoderTurn(EditManager& manager, Track& track, int delta) = 0;
    virtual void onButtonPress(EditManager& manager, Track& track) = 0;
    virtual const char* getName() const = 0;
};
