//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#pragma once
#include <cstdint>
#include "EditNoteState.h"
#include "NoteEditSession.h"
#include "EditNoteHomeState.h"
#include "EditStates/EditSelectNoteState.h"
#include "EditStartNoteState.h"
#include "EditLengthNoteState.h"
#include "EditPitchNoteState.h"
#include "MidiEvent.h"
#include "MidiConfig.h"
#include <vector>
#include <map>

// Forward declarations
class Track;

/**
 * @class EditManager
 * @brief Implements the state machine for note- and parameter-edit overlays.
 *
 * Coordinates EditNoteState instances (NoteState, StartNoteState, PitchNoteState) to handle
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
    void setState(EditNoteState* newState, Track& track, uint32_t startTick = 0);
    void onEncoderTurn(Track& track, int delta);
    void onButtonPress(Track& track);

    // State pattern helpers
    void selectClosestNote(Track& track, uint32_t startTick);
    /// Select the note whose start equals bracketTick (falls back to closest).
    void selectNoteAtBracket(Track& track, uint32_t bracketTick);
    void moveBracket(Track& track, int delta);
    void switchToNextState(Track& track);

    // Enter/exit edit mode
    void enterEditMode(EditNoteState* newState, uint32_t startTick);
    void exitEditMode(Track& track);

    /// NoteEditSession lifecycle (M8).
    bool isNoteEditActive() const { return noteEditSession.active; }
    NoteEditSession& getNoteEditSession() { return noteEditSession; }
    const NoteEditSession& getNoteEditSession() const { return noteEditSession; }
    void openNoteEditSession(Track& track);
    void closeNoteEditSession(Track& track);
    void closeNoteEditPass(Track& track);
    EditPassId commitEditAction(Track& track, EditChangeList changes);
    void pushSessionUndoBeforeMutation(Track& track);
    bool sessionUndo(Track& track);
    bool sessionRedo(Track& track);
    /// Pre-commit resolve + single saveEdit at fader-1 reselect / exit / overdub start.
    void commitAllPendingNoteEditActions(Track& track);
    /// Persist overlap note Hidden/Shortened scratch into Edits[] before restore-on-move-away.
    void commitPendingOverlapNoteEdits(Track& track);
    /// Live edit hot path: materialize overlap scratch into session store only (no rematerialize).
    void materializeOverlapScratchToSessionStore(Track& track);
    /// Ensure **focus** is active before overlap utils (rebuild from live **DisplayNote** when needed).
    void ensureNoteEditFocusForLiveEdit(Track& track,
                                        const NoteUtils::DisplayNote& fallbackWhenNoFocus);
    /// Clear focus only (`selectedNoteIdx == -1`). Do **not** pass a filtered or unfiltered list index —
    /// use **rebuildNoteEditFocusForDisplayNote** for fader-1 select (C14).
    void rebuildNoteEditFocusAtSelect(Track& track, int selectedNoteIdx);
    /// Fader-1 select: baseline from Takes+Edits; **focus.last** from live **DisplayNote** (filtered index safe).
    void rebuildNoteEditFocusForDisplayNote(Track& track, const NoteUtils::DisplayNote& liveSelected);
    /// Remap or clear **selectedNoteIdx** when filtered inventory no longer matches **focus.last**.
    void syncSelectedNoteIdxToFilteredInventory(Track& track);
    /// Filtered select inventory during note edit; else cached notes (encoder + fader).
    std::vector<NoteUtils::DisplayNote> selectableDisplayNotesAtEditSelect(const Track& track) const;
    /// Live mover geometry: **focus.last** when active, else inventory at **selectedNoteIdx**.
    NoteUtils::DisplayNote liveEditDisplayNoteAtSelect(const Track& track) const;
    MidiEventVec& sessionMidiEvents();
    const MidiEventVec& sessionMidiEvents() const;

    /// Returns session store during note edit, else loop materialized events.
    MidiEventVec& editMidiEvents(Track& track);
    const MidiEventVec& editMidiEvents(const Track& track) const;

    // Move bracket by delta steps (e.g., encoder movement)
    void moveBracket(int delta, const Track& track, uint32_t ticksPerStep);
    // Select next/previous note in current bracket (for chords)
    void selectNextNote(const Track& track);
    void selectPrevNote(const Track& track);

    // Getters
    EditNoteState* getCurrentState() const { return currentState; }
    uint32_t getBracketTick() const { return bracketTick; }
    int getSelectedNoteIdx() const { return selectedNoteIdx; }
    /// Last successful fader-1 select **NoteRef** (delete target when set).
    bool hasLastFader1SelectRef() const { return lastFader1SelectRef.channel != 0; }
    const NoteRef& getLastFader1SelectRef() const { return lastFader1SelectRef; }
    void setLastFader1SelectRef(const NoteRef& ref);
    void clearLastFader1SelectRef();
    // Reset selection
    void resetSelection();
    void setSelectedNoteIdx(int idx);
    // Allow direct setting of the bracket tick (for precise note movement)
    void setBracketTick(uint32_t tick) { bracketTick = tick; }
    void setHasMovedBracket(bool moved) { hasMovedBracket = moved; }

    // Get state instances
    EditNoteHomeState* getNoteHomeState() { return &noteHomeState; }
    EditSelectNoteState* getSelectNoteState() { return &selectNoteState; }
    EditStartNoteState* getStartNoteState() { return &startNoteState; }
    EditLengthNoteState* getLengthNoteState() { return &lengthNoteState; }
    EditPitchNoteState* getPitchNoteState() { return &pitchNoteState; }
    // Add more state getters as needed

    // State instances (public for access from other managers)
    EditNoteHomeState noteHomeState;
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
        // Logical mode IDs (MIDI program mapping is handled by implementation).
        MAIN_MODE_LOOP_EDIT = 0,
        MAIN_MODE_NOTE_EDIT = 1
    };
    
    MainEditMode getCurrentMainEditMode() const { return currentMainEditMode; }
    void setMainEditMode(MainEditMode mode);

    struct RemovedNote {
        uint8_t note;
        uint8_t velocity;
        uint32_t startTick;
        uint32_t endTick;
        MidiEventVec events; // The original events for restoration
    };
    // Map: Track* -> note -> list of removed notes
    std::map<const Track*, std::map<uint8_t, std::vector<RemovedNote>>> temporarilyRemovedNotes;

    /**
     * @brief Returns the undo count to display: frozen during edit states, real count otherwise
     */
    size_t getDisplayUndoCount(const Track& track) const;

private:
    uint32_t bracketTick = 0;
    int selectedNoteIdx = -1; // -1 means no note selected
    NoteRef lastFader1SelectRef{};
    bool hasMovedBracket = false; // true if the bracket has been moved since entering edit mode
    // Temporarily store undo count when entering an edit state to freeze display until exit
    size_t undoCountOnStateEnter = 0;

    /// @brief If multiple notes at bracket, cycle through them before moving bracket
    int notesAtBracketIdx = 0;
    std::vector<int> notesAtBracketTick;

    EditNoteState* currentState = nullptr;
    EditNoteState* previousState = nullptr;
    NoteEditSession noteEditSession;
    // Add more states as needed
    
    // EditModeManager state
    EditModeState currentEditMode = EDIT_MODE_NONE;
    
    // LoopManager state
    MainEditMode currentMainEditMode = MAIN_MODE_NOTE_EDIT;
    
};

extern EditManager editManager; 