#pragma once
#include "EditState.h"

class EditStartNoteState : public EditState {
public:
    void onEnter(EditManager& manager, Track& track, uint32_t startTick) override;
    void onExit(EditManager& manager, Track& track) override;
    void onEncoderTurn(EditManager& manager, Track& track, int delta) override;
    void onButtonPress(EditManager& manager, Track& track) override;
    const char* getName() const override { return "EditStartNote"; }

    /**
     * @brief Get the initial MIDI-event hash before editing started
     */
    uint32_t getInitialHash() const { return initialHash; }

private:
    uint32_t initialHash = 0; // hash of midiEvents at onEnter for undo commit-on-exit
}; 