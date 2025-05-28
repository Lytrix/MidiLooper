#pragma once
#include <cstdint>
#include "EditState.h"
#include "EditNoteState.h"
#include "EditStartNoteState.h"
#include "EditPitchNoteState.h"
#include "MidiEvent.h"
#include <vector>
#include <map>

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
    void setSelectedNoteIdx(int idx) { selectedNoteIdx = idx; }
    // Allow direct setting of the bracket tick (for precise note movement)
    void setBracketTick(uint32_t tick) { bracketTick = tick; }

    // Get state instances
    EditNoteState* getNoteState() { return &noteState; }
    EditStartNoteState* getStartNoteState() { return &startNoteState; }
    EditPitchNoteState* getPitchNoteState() { return &pitchNoteState; }
    // Add more state getters as needed

    void enterPitchEditMode(Track& track);
    void exitPitchEditMode(Track& track);

    struct RemovedNote {
        uint8_t note;
        uint8_t velocity;
        uint32_t startTick;
        uint32_t endTick;
        std::vector<MidiEvent> events; // The original events for restoration
    };
    // Map: Track* -> note -> list of removed notes
    std::map<const Track*, std::map<uint8_t, std::vector<RemovedNote>>> temporarilyRemovedNotes;

    struct MovingNoteIdentity {
        uint8_t note = 0;
        uint32_t origStart = 0;
        uint32_t origEnd = 0;
        uint32_t lastStart = 0;  // Track previous position for direction detection
        uint32_t lastEnd = 0;    // Track previous end position
        int wrapCount = 0; // how many times the note has wrapped
        bool active = false;
        int movementDirection = 0; // -1 = left, 0 = none, 1 = right
        std::vector<MidiEvent> deletedEvents; // Events that were deleted due to overlap
        std::vector<uint32_t> deletedEventIndices; // Original indices for restoration
        
        // Simple note storage for restoration
        struct DeletedNote {
            uint8_t note, velocity;
            uint32_t startTick, endTick;
            uint32_t originalLength; // Store original note length for consistent restoration
        };
        std::vector<DeletedNote> deletedNotes;
        bool undoSnapshotPushed = false; // snapshot only once after first movement
    };
    MovingNoteIdentity movingNote;

    /**
     * @brief Returns the undo count to display: frozen during edit states, real count otherwise
     */
    size_t getDisplayUndoCount(const Track& track) const;

private:
    uint32_t bracketTick = 0;
    int selectedNoteIdx = -1; // -1 means no note selected
    bool hasMovedBracket = false; // true if the bracket has been moved since entering edit mode
    // Temporarily store undo count when entering an edit state to freeze display until exit
    size_t undoCountOnStateEnter = 0;

    /// @brief If multiple notes at bracket, cycle through them before moving bracket
    int notesAtBracketIdx = 0;
    std::vector<int> notesAtBracketTick;

    EditState* currentState = nullptr;
    EditNoteState noteState;
    EditStartNoteState startNoteState;
    EditPitchNoteState pitchNoteState;
    EditState* previousState = nullptr;
    // Add more states as needed
};

extern EditManager editManager; 