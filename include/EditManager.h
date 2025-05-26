#pragma once
#include <cstdint>
#include "EditState.h"
#include "EditNoteState.h"
#include "EditStartNoteState.h"
#include <vector>

// Forward declarations
class Track;

// Manages all state and logic for edit overlays (note, param, track, etc.)
class EditManager {
public:
    EditManager();

    // State pattern core methods
    void setState(EditState* newState, Track& track, uint32_t startTick = 0);
    void onEncoderTurn(Track& track, int delta);
    void onButtonPress(Track& track);

    // State pattern helpers
    void selectClosestNote(Track& track, uint32_t startTick);
    void moveBracket(Track& track, int delta);
    void switchToNextState(Track& track);

    // Enter/exit edit mode
    void enterEditMode(EditState* newState, uint32_t startTick);
    void exitEditMode(Track& track);

    // Move bracket by delta steps (e.g., encoder movement)
    void moveBracket(int delta, const Track& track, uint32_t ticksPerStep);
    // Select next/previous note in current bracket (for chords)
    void selectNextNote(const Track& track);
    void selectPrevNote(const Track& track);

    // Getters
    EditState* getCurrentState() const { return currentState; }
    uint32_t getBracketTick() const { return bracketTick; }
    int getSelectedNoteIdx() const { return selectedNoteIdx; }
    // Reset selection
    void resetSelection() { selectedNoteIdx = -1; }

    // Get state instances
    EditNoteState* getNoteState() { return &noteState; }
    EditStartNoteState* getStartNoteState() { return &startNoteState; }
    // Add more state getters as needed

private:
    uint32_t bracketTick = 0;
    int selectedNoteIdx = -1; // -1 means no note selected
    bool hasMovedBracket = false; // true if the bracket has been moved since entering edit mode
    
    /// @brief If multiple notes at bracket, cycle through them before moving bracket
    int notesAtBracketIdx = 0;
    std::vector<int> notesAtBracketTick;

    EditState* currentState = nullptr;
    EditNoteState noteState;
    EditStartNoteState startNoteState;
    // Add more states as needed
};

extern EditManager editManager; 