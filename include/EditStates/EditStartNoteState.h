#pragma once
#include "EditState.h"

// Forward declarations
class EditManager;
class Track;

class EditStartNoteState : public EditState {
public:
    void onEnter(EditManager& manager, Track& track, uint32_t startTick) override;
    void onExit(EditManager& manager, Track& track) override;
    void onEncoderTurn(EditManager& manager, Track& track, int delta) override;
    void onButtonPress(EditManager& manager, Track& track) override;
    const char* getName() const override { return "EditStartNote"; }

private:
    // Helper methods for common operations
    bool validatePreconditions(EditManager& manager, Track& track);
    void updateMovementDirection(EditManager& manager, int delta);
    uint32_t calculateNoteLength(uint32_t start, uint32_t end, uint32_t loopLength);
    uint32_t wrapPosition(int32_t position, uint32_t loopLength);
    bool notesOverlap(uint32_t start1, uint32_t end1, uint32_t start2, uint32_t end2);
}; 