//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#pragma once
#include <cstdint>
#include "EditState.h"
#include "EditNoteState.h"
#include "EditStates/EditSelectNoteState.h"
#include "EditStartNoteState.h"
#include "EditLengthNoteState.h"
#include "EditPitchNoteState.h"
#include "MidiEvent.h"
#include <vector>
#include <map>

// Forward declarations
class Track;

/**
 * @class EditManager
 * @brief Implements the state machine for note- and parameter-edit overlays.
 *
 * Coordinates EditState instances (NoteState, StartNoteState, PitchNoteState) to handle
 * encoder movements and button presses for selecting notes, moving note start positions,
 * and changing note pitches. Tracks the bracket position over the piano roll, manages
 * commit-on-enter/commit-on-exit undo snapshots via TrackUndo, and delegates display
 * updates to DisplayManager for visual feedback.
 * 
 * Also includes EditModeManager and LoopManager functionality for managing edit modes
 * and loop editing operations.
 */
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
    void resetSelection();
    void setSelectedNoteIdx(int idx);
    // Allow direct setting of the bracket tick (for precise note movement)
    void setBracketTick(uint32_t tick) { bracketTick = tick; }
    void setHasMovedBracket(bool moved) { hasMovedBracket = moved; }

    // Get state instances
    EditNoteState* getNoteState() { return &noteState; }
    EditSelectNoteState* getSelectNoteState() { return &selectNoteState; }
    EditStartNoteState* getStartNoteState() { return &startNoteState; }
    EditLengthNoteState* getLengthNoteState() { return &lengthNoteState; }
    EditPitchNoteState* getPitchNoteState() { return &pitchNoteState; }
    // Add more state getters as needed

    // State instances (public for access from other managers)
    EditNoteState noteState;
    EditSelectNoteState selectNoteState;
    EditStartNoteState startNoteState;
    EditLengthNoteState lengthNoteState;
    EditPitchNoteState pitchNoteState;

    void enterPitchEditMode(Track& track);
    void exitPitchEditMode(Track& track);

    // EditModeManager functionality
    enum EditModeState {
        EDIT_MODE_NONE = 0,     // Not in edit mode
        EDIT_MODE_SELECT = 1,   // Select note or grid position
        EDIT_MODE_START = 2,    // Move start note position
        EDIT_MODE_LENGTH = 3,   // Change note length
        EDIT_MODE_PITCH = 4     // Change note pitch
    };
    
    EditModeState getCurrentEditMode() const { return currentEditMode; }
    void cycleEditMode(Track& track);
    void enterNextEditMode(Track& track);
    void sendEditModeProgram(EditModeState mode);
    
    // LoopManager functionality
    void cycleMainEditMode(Track& track);
    void sendMainEditModeChange(uint8_t mode);
    void sendCurrentLoopLengthCC(Track& track);
    void onTrackChanged(Track& newTrack);
    
    // Main edit mode management
    enum MainEditMode {
        MAIN_MODE_LOOP_EDIT = 0,    // Loop edit mode: Program 0, Note 100 trigger
        MAIN_MODE_NOTE_EDIT = 1     // Note edit mode: Program 1, Note 0 trigger
    };
    
    MainEditMode getCurrentMainEditMode() const { return currentMainEditMode; }
    void setMainEditMode(MainEditMode mode);

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
            bool wasShortened = false; // true if note was shortened, false if completely deleted
            uint32_t shortenedToTick = 0; // if shortened, what tick it was shortened to
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
    EditState* previousState = nullptr;
    // Add more states as needed
    
    // EditModeManager state
    EditModeState currentEditMode = EDIT_MODE_NONE;
    
    // LoopManager state
    MainEditMode currentMainEditMode = MAIN_MODE_NOTE_EDIT;  // Start in note edit mode
    
    // MIDI constants for program changes
    static constexpr uint8_t PROGRAM_CHANGE_CHANNEL = 16;
    static constexpr uint8_t LOOP_LENGTH_CC_CHANNEL = 16;
    static constexpr uint8_t LOOP_LENGTH_CC_NUMBER = 101;
};

extern EditManager editManager; 