//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#ifndef LOOPERSTATE_H
#define LOOPERSTATE_H

#include <Arduino.h>

// Expanded looper modes
enum LooperState {
  LOOPER_IDLE,
  LOOPER_RECORDING,
  LOOPER_PLAYING,
  LOOPER_OVERDUBBING,
  LOOPER_EDIT,      // Overlay: editing notes/params
  LOOPER_SETTINGS   // Overlay: settings menu
};

// Context for edit mode
enum EditContext {
  EDIT_NONE,
  EDIT_NOTE,
  EDIT_PARAM,
  EDIT_TRACK
};

/**
 * @class LooperStateManager
 * @brief Manages the global looper operating mode and overlay contexts.
 *
 * LooperStateManager drives the core looper modes (idle, recording, playing, overdubbing)
 * defined by the `LooperState` enum. State transitions can be queued via
 * requestStateTransition(newState, quantize) for optionally quantized changes.
 * It also handles entering/exiting edit and settings overlays with `EditContext`.
 * The `update()` method should be invoked regularly (e.g., in the main loop) to
 * process pending transitions and update internal flags.
 */
class LooperStateManager {
public:
    void update();  // Call this regularly (e.g., once per loop)
void requestStateTransition(LooperState newState, bool quantize = false); // Queues a transition (optionally quantized)
    void enterEditMode(EditContext ctx);   // Enter edit overlay
    void exitEditMode();                   // Exit edit overlay
    void enterSettingsMode();               // Enter settings overlay
    void exitSettingsMode();                // Exit settings overlay
    void setEditContext(EditContext ctx);   // Change edit context
    EditContext getEditContext();
    // ---
    // getLooperState() overloads:
    // - LooperState& getLooperState():       mutable reference for direct state access (e.g., serialization)
    // - const LooperState& getLooperState() const: const reference for read-only access
    // Use these when a reference is required (e.g., by functions expecting LooperState&)
    // ---
    LooperState& getLooperState() { return looperState; }
    const LooperState& getLooperState() const { return looperState; }
private:
    void actuallyTransition();
    LooperState looperState = LOOPER_IDLE;
    LooperState pendingState = LOOPER_IDLE;
    bool pendingQuantized = false;
    bool transitionArmed = false;
    bool editOverlayActive = false;
    bool settingsOverlayActive = false;
    EditContext editContext = EDIT_NONE;
};

extern LooperStateManager looperState;

#endif // LOOPERSTATE_H
