//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#pragma once
#include "EditNoteState.h"
#include "EditPass.h"

// New state for editing the length of a note
class EditLengthNoteState : public EditNoteState {
public:
    void onEnter(EditManager& manager, Track& track, uint32_t startTick) override;
    void onExit(EditManager& manager, Track& track) override;
    void onEncoderTurn(EditManager& manager, Track& track, int delta) override;
    void onButtonPress(EditManager& manager, Track& track) override;
    const char* getName() const override { return "EditLengthNote"; }
    
    uint32_t getInitialHash() const { return initialHash; }
    NoteRef getTargetRef() const { return targetRef_; }
    
private:
    uint32_t initialHash = 0;
    NoteRef targetRef_{};
}; 