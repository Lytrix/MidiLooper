#pragma once
#include <cstdint>

class EditManager;
class Track;

/**
 * @class EditState
 * @brief Abstract interface for editor state machine overlays.
 *
 * EditState defines the lifecycle and input handlers for edit modes (e.g., note selection,
 * start-note movement, pitch editing). Subclasses implement:
 *   - onEnter(): initialize state and UI when overlay activates
 *   - onExit(): cleanup when overlay deactivates
 *   - onEncoderTurn(): respond to encoder rotations for live edits
 *   - onButtonPress(): respond to button presses (e.g., finalize or cancel edits)
 *   - getName(): return a string identifier for the state
 */
class EditState {
public:
    virtual ~EditState() {}
    virtual void onEnter(EditManager& manager, Track& track, uint32_t startTick) {}
    virtual void onExit(EditManager& manager, Track& track) {}
    virtual void onEncoderTurn(EditManager& manager, Track& track, int delta) = 0;
    virtual void onButtonPress(EditManager& manager, Track& track) = 0;
    virtual const char* getName() const = 0;
};
