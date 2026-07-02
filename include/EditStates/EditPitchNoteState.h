//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#pragma once
#include "EditNoteState.h"
#include "MidiEvent.h"

// New state for editing the pitch of a note
class EditPitchNoteState : public EditNoteState {
public:
    void onEnter(EditManager& manager, Track& track, uint32_t startTick) override;
    void onExit(EditManager& manager, Track& track) override;
    void onEncoderTurn(EditManager& manager, Track& track, int delta) override;
    void onButtonPress(EditManager& manager, Track& track) override;
    const char* getName() const override { return "EditPitchNote"; }
    uint32_t getInitialHash() const { return initialHash; }
    NoteId getTargetNoteId() const { return targetNoteId_; }
private:
    uint32_t initialHash = 0;
    NoteId targetNoteId_ = kInvalidNoteId;
};
