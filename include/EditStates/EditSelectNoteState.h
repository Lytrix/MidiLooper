//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#pragma once
#include <array>
#include "EditNoteState.h"
#include "MidiEvent.h"
#include "Utils/NoteUtils.h"

class EditManager;
class Track;

/**
 * @class EditSelectNoteState
 * @brief Initial selection state that shows bracket and snaps to 16th notes/existing notes
 * 
 * This state is entered first when the encoder button is pressed. It:
 * - Shows the bracket at the current position
 * - Snaps to the nearest 16th step or existing note
 * - On second click: creates a 32nd note if empty, or enters start note editing if note exists
 */
class EditSelectNoteState : public EditNoteState {
public:
    void onEnter(EditManager& manager, Track& track, uint32_t startTick) override;
    void onExit(EditManager& manager, Track& track) override;
    void onEncoderTurn(EditManager& manager, Track& track, int delta) override;
    void onButtonPress(EditManager& manager, Track& track) override;
    
    const char* getName() const override { return "EditSelectNote"; }
    
    // Called to update bracket position during overdubbing
    void updateForOverdubbing(EditManager& manager, Track& track);
    
public:
    static void sendTargetPitchbend(EditManager& manager, Track& track);
    /// Creates a default 32nd note at tick and returns its {noteOn, noteOff} events so
    /// callers can record an exact AddNote edit (the buffer is sorted, so the created
    /// events are not necessarily the last two entries).
    static std::array<MidiEvent, 2> createNoteAtTick(Track& track, uint32_t tick);

private:
    std::array<MidiEvent, 2> createDefaultNote(Track& track, uint32_t tick) const;
    
    // Track MIDI events count to detect new notes during overdubbing
    size_t lastMidiEventCount = 0;
}; 