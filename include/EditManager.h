#pragma once
#include <cstdint>
#include "LooperState.h" // For EditContext

// Forward declarations
class Track;

// Manages all state and logic for edit overlays (note, param, track, etc.)
class EditManager {
public:
    EditManager();
    // Enter edit mode with context and starting tick
    void enterEditMode(EditContext ctx, uint32_t startTick);
    // Exit edit mode
    void exitEditMode();
    // Move bracket by delta steps (e.g., encoder movement)
    void moveBracket(int delta, const Track& track, uint32_t ticksPerStep);
    // Select next/previous note in current bracket (for chords)
    void selectNextNote(const Track& track);
    void selectPrevNote(const Track& track);
    // Getters
    EditContext getContext() const;
    uint32_t getBracketTick() const;
    int getSelectedNoteIdx() const;
    // Reset selection
    void resetSelection();
private:
    EditContext context = EDIT_NONE;
    uint32_t bracketTick = 0;
    int selectedNoteIdx = -1; // -1 means no note selected
    bool hasMovedBracket = false; // true if the bracket has been moved since entering edit mode
}; extern EditManager editManager; 