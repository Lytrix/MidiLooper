#pragma once
#include "EditState.h"

// New state for editing the pitch of a note
class EditPitchNoteState : public EditState {
public:
    void onEnter(EditManager& manager, Track& track, uint32_t startTick) override;
    void onExit(EditManager& manager, Track& track) override;
    void onEncoderTurn(EditManager& manager, Track& track, int delta) override;
    void onButtonPress(EditManager& manager, Track& track) override;
    const char* getName() const override { return "EditPitchNote"; }
    /**
     * @brief Get the initial MIDI-event hash before editing started
     */
    uint32_t getInitialHash() const { return initialHash; }
private:
    uint32_t initialHash = 0;         // hash of midiEvents at onEnter for undo commit-on-exit
};
