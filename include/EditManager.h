//  Copyright (c)  2025 Lytrix (Eelke Jager)
//  Licensed under the PolyForm Noncommercial 1.0.0

#pragma once
#include <cstdint>
#include "NoteEditSessionState.h"
#include "EditNoteState.h"
#include "NoteEditSessionUndo.h"
#include "EditSession.h"
#include "EditEvent.h"
#include "EditStates/EditNoteHomeState.h"
#include "EditStates/EditSelectNoteState.h"
#include "EditStartNoteState.h"
#include "EditLengthNoteState.h"
#include "EditPitchNoteState.h"
#include "NoteEditSessionState.h"
#include "MidiEvent.h"
#include "MidiConfig.h"
#include "Utils/SelectNavigation.h"
#include <vector>
#include <map>

// Forward declarations
class Track;

/**
 * @class EditManager
 * @brief Owns live EditSession (note-edit store, focus, undo) and EditNoteState FSM.
 *
 * Coordinates EditNoteState instances (home, select, start, length, pitch) for encoder
 * and button input. Loop geometry edit UI lives on LoopEditManager; MIDI fader/button
 * control-surface routing lives on ControlSurfaceManager. Display updates go through
 * DisplayManager.
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
    /// Select the note whose start equals selectedTick (falls back to closest).
    void selectNoteAtBracket(Track& track, uint32_t selectedTick);
    void moveBracket(Track& track, int delta);
    void switchToNextState(Track& track);

    // Enter/exit edit mode
    void enterEditMode(EditNoteState* newState, uint32_t startTick);
    void exitEditMode(Track& track);

    /// EditSession lifecycle (M8).
    bool isNoteEditActive() const { return editSession.active; }
    /// Loop geometry for NOTE_EDIT — follows **selected** slot, not `activeLoopIndex`.
    uint32_t noteEditLoopLengthTicks(const Track& track) const;
    uint32_t noteEditLoopStartTick(const Track& track) const;
    bool isLoopEditSession() const {
        return editSession.sessionType == EditSessionType::Loop && !editSession.active;
    }
    bool isNoteSessionStoreOpen() const { return editSession.active; }
    EditSession& getEditSession() { return editSession; }
    const EditSession& getEditSession() const { return editSession; }
    EditSessionType getEditSessionType() const { return editSession.sessionType; }
    void cycleEditSession(Track& track);
    void sendEditSessionChange(EditSessionType sessionType, bool notifySurfaceMidi = false);
    void emitSessionOpenedToSurface(bool includeMidi, bool includeNoteFaderFeedback = false);
    bool sessionOpenedIncludesMidi() const { return sessionOpenedIncludesMidi_; }
    bool sessionOpenedIncludesFaderFeedback() const { return sessionOpenedIncludesFaderFeedback_; }
    void openNoteEditSession(Track& track);
    void reopenNoteEditSession(Track& track);
    void closeNoteEditSession(Track& track);
    /// Discard open note-edit RAM and revert UI to loop edit when loop content is cleared.
    void revertNoteEditSessionForLoopClear(Track& track);
    /// Rebuild session store from loop passes after workspace revision load replaced RAM.
    void rematerializeNoteEditSessionAfterWorkspaceReload(Track& track);
    void closeNoteEditPass(Track& track);
    /// Push **NoteEditPassClosed** when committed **editPass** rows have become durable (SD, depart).
    void markCurrentEditBatchDurable(Track& track);
    EditPassId commitEditAction(Track& track, EditPassVec rows);
    bool pushSessionUndoOnKindChange(Track& track, NoteEditKind kind);
    void foldLiveCaptureIntoNoteEditSession(Track& track);
    void restoreSessionUndoEntry(Track& track, const SessionUndoEntry& entry);
    bool beginGeometryMutation(Track& track, NoteEditKind kind, bool fromFaderControl);
    bool sessionUndo(Track& track);
    bool sessionRedo(Track& track);

    NoteEditSessionState& getNoteEditSessionState() { return sessionState; }
    const NoteEditSessionState& getNoteEditSessionState() const { return sessionState; }
    void applySelectNav(Track& track, uint32_t selectedTick, NoteId primaryNote,
                        bool requestFaderSync = false, bool skipFader1Outbound = false);
    /** Geometry edit: refresh EditorSelection + UI immediately; no dependent motor scheduling. */
    void applySelectionFromGeometryEdit(Track& track, uint32_t selectedTick, NoteId primaryNote);
    /// Geometry edit: refresh bracket + display only; preserve selectedNoteIdx and edit state.
    void syncGeometrySelectionToUi(Track& track);
    /// Encoder / legacy helpers: step fader-1 nav slots over windowed selectable inventory.
    void stepSelectNavSlot(Track& track, int delta);
    void applyCycleEditKind(Track& track);
    void applyGeometryKindFromControl(Track& track, NoteEditKind kind, bool fromFaderControl);
    void applyUndoRedoLanding(Track& track);
    void resetNoteEditSessionState();

    /// Edit operations (moved from ControlSurfaceManager — Phase 1).
    bool deleteSelectedNote(Track& track, const NoteUtils::DisplayNoteVec& filteredNotes);
    /// After **Create** — run geometry pipeline with the new note as causing (same-pitch overlap).
    void applyCreatedNoteOverlapGeometry(Track& track, const NoteUtils::DisplayNote& createdNote);
    bool moveNoteToPosition(Track& track, const NoteUtils::DisplayNote& currentNote,
                            uint32_t targetTick);
    bool changeNoteEndWithOverlapHandling(Track& track, const NoteUtils::DisplayNote& currentNote,
                                          uint32_t targetEndTick);

    /// Windowed selectable inventory for NOTE_EDIT UI (session filter + display window).
    std::vector<NoteUtils::DisplayNote> selectableDisplayNotesForEditUi(const Track& track) const;
    std::vector<SelectNavigation::SelectNavSlot> buildSelectNavigationSlots(
        const Track& track, uint32_t selectedTick, bool includeSelectedTickIfMissing = true) const;
    void syncReferenceStepFromSelectedTick(uint32_t selectedTick);
    uint32_t getReferenceStep() const { return referenceStep_; }
    void setReferenceStep(uint32_t step) { referenceStep_ = step; }

    void setEditEventListener(EditEventListener* listener) { editEventListener_ = listener; }
    bool isLengthEditingMode() const { return lengthEditingMode_; }
    uint32_t lengthFineAnchorEndTick() const { return lengthFineAnchorEndTick_; }
    void setLengthFineAnchorEndTick(uint32_t tick) { lengthFineAnchorEndTick_ = tick; }
    void toggleLengthEditMode(Track& track);
    void clearLengthEditingMode(bool emitEvent = true);
    void clearLengthEditingModeOnNoteSelect();
    const EditorSelection& selectionChangePrior() const { return selectionChangePrior_; }
    bool selectionChangeRequestFaderSync() const { return selectionChangeRequestFaderSync_; }

    void syncNoteEditSessionStateToUi(Track& track);
    void enterDefaultNoteEditSessionState(Track& track, uint32_t startTick);
    /// Pre-commit resolve + single saveEdit at fader-1 reselect / exit / overdub start.
    void commitAllPendingNoteEditActions(Track& track);
    /// True when live edit driver satisfies D19a (NoteId + store linear span).
    bool isLiveEditDriverValidForTrack(const Track& track) const;
    /// True when select bracket matches **focus.last** for the same mover (RC7b).
    bool isMacroCommitAlignedWithSelectTargetForTrack(const Track& track, NoteId selectNoteId,
                                                      uint32_t selectBracketTick) const;
    /// Drop pending apply-owned Delete row for a note the user is navigating to via fader-1.
    void cancelPendingDeleteForSelectNote(NoteId noteId);
    /// Persist overlap note Hidden/Shortened scratch into Edits[] before restore-on-move-away.
    void commitPendingOverlapNoteEdits(Track& track);
    /// Ensure **focus** is active for `EditorSelection.primaryNote` before overlap utils.
    void ensureNoteEditFocusForLiveEdit(Track& track,
                                        const NoteUtils::DisplayNote& fallbackWhenNoFocus);
    /// Clear focus only (`selectedNoteIdx == -1`). Do **not** pass a filtered or unfiltered list index —
    /// use **rebuildNoteEditFocusForDisplayNote** for fader-1 select (C14).
    void rebuildNoteEditFocusAtSelect(Track& track, int selectedNoteIdx);
    /// Fader-1 select: baseline from passes materialize; **focus.last** from live **DisplayNote** (filtered index safe).
    void rebuildNoteEditFocusForDisplayNote(Track& track, const NoteUtils::DisplayNote& liveSelected);
    /// Remap or clear **selectedNoteIdx** when filtered inventory no longer matches **focus.last**.
    void syncSelectedNoteIdxToFilteredInventory(Track& track);
    /// Filtered select inventory during note edit; else cached notes (encoder + fader).
    NoteUtils::DisplayNoteVec selectableDisplayNotesAtEditSelect(const Track& track) const;
    /// Single cached NOTE_EDIT display projection (session store + focus).
    NoteUtils::DisplayNoteVec projectedNoteEditDisplayNotes(const Track& track) const;
    /// Cached NOTE_EDIT selectable inventory (session reconstruction minus Hidden overlap).
    NoteUtils::DisplayNoteVec filteredSelectableDisplayNotesForNoteEdit(const Track& track) const;
    void invalidateProjectedNoteEditDisplayCache() const;
    uint32_t noteEditDisplayInvalidateEpoch() const { return noteEditDisplayInvalidateEpoch_; }
    uint32_t noteEditDisplayPaintedEpoch() const { return noteEditDisplayPaintedEpoch_; }
    bool noteEditDisplayRefreshPending() const {
        return noteEditDisplayPaintedEpoch_ < noteEditDisplayInvalidateEpoch_;
    }
    bool shouldForceNoteEditDisplayUpdate() const {
        return noteEditDisplayImmediatePaintRequested_ || noteEditDisplayRefreshPending();
    }
    void markNoteEditDisplayPainted();
    /// Live mover geometry: **focus.last** only when it matches `EditorSelection.primaryNote`.
    bool isLengthBracketEditActive() const;
    NoteUtils::DisplayNote liveEditDisplayNoteAtSelect(const Track& track) const;
    /// Refresh **focus.last** start/end from the live session store note-on/off pair.
    void syncNoteEditFocusLastFromSessionStore(Track& track);
    /// Read-only canonical projection of note edit current state during NOTE_EDIT.
    const MidiEventVec& noteEditSessionProjectionEvents() const;
    /// Projection owner: rebuild EditSession.store from noteEditCurrentState.
    void refreshNoteEditSessionProjection(uint8_t channel);
    NoteEditCurrentState& noteEditCurrentStateMut();
    const NoteEditCurrentState& noteEditCurrentState() const;
#if NOTE_EDIT_PROJECTED_STORE_COMPAT
    /// Compat direct projected-store mutation — remove after tasks.md §5–7 writer migration.
    MidiEventVec& mutNoteEditSessionProjectionEventsCompat();
    MidiEventVec& mutEditProjectionEventsCompat(Track& track);
#endif
    MidiEventVec& sessionMidiEvents();
    const MidiEventVec& sessionMidiEvents() const;
    void bumpSessionPreviewRevision();
    uint32_t sessionPreviewRevision() const { return sessionPreviewRevision_; }
    void bumpSessionPlaybackPreviewRevision();
    uint32_t sessionPlaybackPreviewRevision() const { return sessionPlaybackPreviewRevision_; }
    void scheduleDeferredNoteEditDisplayRefresh();
    void processDeferredNoteEditDisplayRefresh(Track& track);
    void flushDeferredNoteEditDisplayRefresh(Track& track);
    /// Pre-build kind-boundary undo after fader-1 selection (idle frame, not fader path).
    void processKindBoundaryUndoWarm(Track& track);

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
    uint32_t getSelectedTick() const { return selectedTick; }
    [[deprecated("use getSelectedTick")]] uint32_t getBracketTick() const { return selectedTick; }
    int getSelectedNoteIdx() const { return selectedNoteIdx; }
    /// Last successful fader-1 select **NoteId** (delete target when set).
    NoteId getLastFader1SelectNoteId() const { return lastFader1SelectNoteId; }
    void setLastFader1SelectNoteId(NoteId noteId);
    void clearLastFader1SelectNoteId();
    // Reset selection
    void resetSelection();
    void setSelectedNoteIdx(int idx);
      void setSelectedTick(uint32_t tick) { selectedTick = tick; }
    [[deprecated("use setSelectedTick")]] void setBracketTick(uint32_t tick) { selectedTick = tick; }
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
    
    void cycleNoteEditType(Track& track);
    void sendEditModeProgram(EditModeState mode);
    
    /// Commit pending note-edit work on the departing track before selection changes.
    void beforeSelectedTrackChange(Track& departingTrack);
    void onTrackChanged(Track& newTrack);

    /// Commit pending note-edit work before the selected loop slot changes.
    void beforeSelectedSlotChange(Track& track);
    void onSelectedSlotChanged(Track& track, uint8_t previousSlot);

    void commitEditSessionOnDepart(Track& track);
    void reenterEditSessionForFocusChange(Track& track, uint8_t previousSlot);

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
     * @brief Sidebar shows selected-loop pass undo depth (U:). Session undo during note edit
     * is handled separately via MIDI undo while in NOTE_EDIT.
     */
    size_t getDisplayUndoCount(const Track& track, const Loop& loop) const;
    bool isSessionUndoDisplayActive() const;

    /// Committed loop MIDI (passes materialized), cached for note-edit baseline discovery (D21).
    const MidiEventVec& materializedLoopEventsForNoteEditFocus(Track& track);

private:
    size_t bakeNoteEditSessionStoreToPasses(Track& track);
    void persistActiveNoteEditSession(Track& track);
    /// After **Delete** — restore overlap notes the removed causing note had hidden or shortened.
    void applyDeleteNoteOverlapRestore(Track& track);
    void invalidateNoteEditDerivedCaches();
    void emitEditEvent(EditEvent event);
    uint32_t selectedTick = 0;
    int selectedNoteIdx = -1; // -1 means no note selected
    uint32_t referenceStep_ = 0;
    bool lengthEditingMode_ = false;
    uint32_t lengthFineAnchorEndTick_ = 0;
    NoteId lastFader1SelectNoteId = kInvalidNoteId;
    bool hasMovedBracket = false; // true if the bracket has been moved since entering edit mode

    EditNoteState* currentState = nullptr;
    EditNoteState* previousState = nullptr;
    EditEventListener* editEventListener_ = nullptr;
    EditorSelection selectionChangePrior_{};
    bool selectionChangeRequestFaderSync_ = false;
    /** Suppress SelectionChanged surface events while reopening NOTE_EDIT (SessionOpen owns motor sync). */
    bool deferSelectionSurfaceEvents_ = false;
    bool sessionOpenedIncludesMidi_ = false;
    bool sessionOpenedIncludesFaderFeedback_ = false;
    EditSession editSession;
    NoteEditSessionState sessionState;
    NoteEditKind lastPushedGeometryKind_ = NoteEditKind::Select;
    bool encoderCycleNeedsAnchor_ = false;
    uint32_t sessionPreviewRevision_ = 0;
    uint32_t sessionPlaybackPreviewRevision_ = 0;
    uint32_t noteEditFocusMaterializeLoopRevision_ = UINT32_MAX;
    uint8_t noteEditFocusMaterializeSlot_ = 255;
    uint32_t noteEditFocusMaterializeLoopLength_ = 0;
    MidiEventVec noteEditFocusMaterializedLoopEvents_;
    mutable uint32_t noteEditSelectableDisplayCachePreviewRevision_ = UINT32_MAX;
    mutable uint32_t noteEditSelectableDisplayCacheFingerprint_ = static_cast<uint32_t>(-1);
    mutable uint32_t noteEditSelectableDisplayCacheLoopLength_ = 0;
    mutable uint32_t noteEditSelectableDisplayCachePlaybackRevision_ = UINT32_MAX;
    mutable NoteUtils::DisplayNoteVec noteEditSelectableDisplayCacheNotes_;
    mutable uint32_t noteEditDisplayInvalidateEpoch_ = 0;
    mutable uint32_t noteEditDisplayPaintedEpoch_ = 0;
    mutable bool noteEditDisplayImmediatePaintRequested_ = false;
    bool deferredNoteEditDisplayRefreshPending_ = false;
    uint32_t deferredNoteEditDisplayRefreshArmedAtMs_ = 0;
    static constexpr uint32_t kDeferredNoteEditPlaybackRefreshIdleMs = 80;
    bool kindBoundaryUndoWarmPending_ = false;
    bool kindBoundaryUndoCacheValid_ = false;
    SessionUndoEntry kindBoundaryUndoCache_{};
    uint32_t kindBoundaryUndoCacheRevision_ = UINT32_MAX;
    void scheduleKindBoundaryUndoWarm();
    // Add more states as needed
    
    // EditModeManager state
    EditModeState currentEditMode = EDIT_MODE_NONE;
    
};

extern EditManager editManager; 